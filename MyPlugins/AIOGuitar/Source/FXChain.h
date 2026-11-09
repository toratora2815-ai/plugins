#pragma once
// AMP ページの流れ: [4 倍オーバーサンプリング: AMP] → CAB（＋マイク / 読み込んだ IR）→ REVERB → OUTPUT
// AMP と CAB は 15 ms のクロスフェードで ON/OFF。AMP を OFF にしても同じだけ遅らせる（申告した遅れのまま）。
// 音の部品は Amp.h（ampsim::Amp / ampsim::Cab）。ペダルは EFFECTS（Pedals.h）、この前に通る。
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Biquad.h"
#include "Amp.h"

struct FXParams
{
    // amp (knobs 0..10)
    bool  ampOn = true;
    int   ampModel = 3;         // ampsim::modelNames()
    float ampGain = 5, ampBass = 5, ampMid = 5, ampTreble = 5, ampPresence = 5, ampMaster = 5;
    // cab
    bool  cabOn = true;
    int   cabType = 2;          // ampsim::cabNames() (the last = loaded IR)
    int   cabMicType = 0;       // ampsim::micNames()
    float cabPos = 3, cabDist = 2;
    // reverb
    float revMix = 0.0f, revSize = 0.5f;
    // output
    float outGain = 0.0f;
    bool  offline = false;   // rendering offline (bounce): may wait for a loaded IR
};

//==============================================================================
class FXChain
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (16, maxBlockSize);
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
        {
            juce::Reverb::Parameters rp0;   // start from "dry as it is, no wet" (JUCE's own default would dip the first 10 ms)
            rp0.wetLevel = 0.0f; rp0.dryLevel = 0.5f;
            reverb.setParameters (rp0);
        }
        reverb.prepare (spec);
        os = std::make_unique<juce::dsp::Oversampling<float>> (
                2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlock);
        osFs = fs * 4.0;
        amp.prepare (osFs);
        cab.prepare (fs, maxBlock);
        for (auto& d : bypassDelay) d.assign ((size_t) getLatencySamples() + 1, 0.0f);   // AMP off keeps the same latency
        bypassPos = 0;
        dryBuf.setSize (2, maxBlock);
        ampMix.reset (fs, 0.015); cabMix.reset (fs, 0.015);
        ampFirst = true;
        outS.reset (fs, 0.03);
        reset();
    }

    void reset()
    {
        firstBlock = true;
        reverb.reset();
        revTail = 0;
        if (os) os->reset();
        amp.reset();
        cab.reset();
    }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

    ampsim::Cab& getCab() { return cab; }

    // nothing is playing (the plugin rests): take new settings at once, without fades, and get a loaded IR ready,
    // so the next note starts exactly as set
    void rest (const FXParams& p, int numSamples)
    {
        cab.prepareIR (numSamples, p.offline, true);
        amp.snapTo (p.ampModel, p.ampGain, p.ampBass, p.ampMid, p.ampTreble, p.ampPresence, p.ampMaster);
        cab.snapTo (p.cabType, p.cabMicType, p.cabPos, p.cabDist);
        ampMix.setCurrentAndTargetValue (p.ampOn ? 1.0f : 0.0f);
        cabMix.setCurrentAndTargetValue (p.cabOn ? 1.0f : 0.0f);
        outS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.outGain));
        for (auto& d : bypassDelay) std::fill (d.begin(), d.end(), 0.0f);   // no old sound on the AMP-off path
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p)
    {
        // a broken sample (NaN / inf / absurdly large) would stay in the filters: drop the block and start clean
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
        {
            const float* d = buffer.getReadPointer (c);
            for (int i = 0; i < numSamples; ++i)
                if (! (std::abs (d[i]) < 1.0e4f)) { buffer.clear (0, numSamples); reset(); return; }   // NaN, inf or absurd
        }
        amp.setParams (p.ampModel, p.ampGain, p.ampBass, p.ampMid, p.ampTreble, p.ampPresence, p.ampMaster);
        cab.setParams (p.cabType, p.cabMicType, p.cabPos, p.cabDist);
        outS.setTargetValue (juce::Decibels::decibelsToGain (p.outGain));
        if (firstBlock) { outS.setCurrentAndTargetValue (outS.getTargetValue()); firstBlock = false; }
        if (ampFirst)   // start where the switches are, without a fade
        {
            ampMix.setCurrentAndTargetValue (p.ampOn ? 1.0f : 0.0f);
            cabMix.setCurrentAndTargetValue (p.cabOn ? 1.0f : 0.0f);
            ampFirst = false;
        }

        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.65f * p.revSize;
        rp.damping  = 0.45f;
        rp.wetLevel = p.revMix;
        rp.dryLevel = 0.5f * (1.0f - 0.6f * p.revMix);   // juce::Reverb doubles the dry level: 0.5 = unchanged
        rp.width    = 1.0f;
        reverb.setParameters (rp);

        for (int pos = 0; pos < numSamples; pos += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - pos);
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) pos, (size_t) n);
            processChunk (block, p);
        }
    }

private:
    void processChunk (juce::dsp::AudioBlock<float>& block, const FXParams& p)
    {
        const int n = (int) block.getNumSamples();
        auto* L = block.getChannelPointer (0);
        auto* R = block.getChannelPointer (1);

        // ---- Amp (oversampled). The way around it is delayed by the same amount (the reported latency
        // is always right), so switching AMP on / off is a 15 ms crossfade, not a click
        ampMix.setTargetValue (p.ampOn ? 1.0f : 0.0f);
        const bool runAmp = p.ampOn || ampMix.isSmoothing();
        const bool blendAmp = ampMix.isSmoothing() || ! p.ampOn;
        if (blendAmp)
        {
            const int lat = getLatencySamples();
            const int len = lat + 1;
            int q = bypassPos;
            for (int i = 0; i < n; ++i)
            {
                bypassDelay[0][(size_t) q] = L[i]; dryBuf.setSample (0, i, lat > 0 ? bypassDelay[0][(size_t) ((q + 1) % len)] : L[i]);
                bypassDelay[1][(size_t) q] = R[i]; dryBuf.setSample (1, i, lat > 0 ? bypassDelay[1][(size_t) ((q + 1) % len)] : R[i]);
                q = (q + 1) % len;
            }
            bypassPos = q;
        }
        if (runAmp)
        {
            if (! ampWasRunning) { os->reset(); amp.reset(); }   // starts again from silence (not the cabinet, which may be playing)
            auto up = os->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            float* ch[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
            amp.process (ch[0], ch[1], un);
            os->processSamplesDown (block);
        }
        ampWasRunning = runAmp;
        if (blendAmp)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m0 = ampMix.getNextValue(), m = m0 * m0 * (3.0f - 2.0f * m0);   // S-curve: no corner at either end
                L[i] = (runAmp ? L[i] * m : 0.0f) + dryBuf.getSample (0, i) * (1.0f - m);
                R[i] = (runAmp ? R[i] * m : 0.0f) + dryBuf.getSample (1, i) * (1.0f - m);
            }
        }
        else ampMix.skip (n);

        // ---- Cabinet + mic (crossfaded on / off as well). A loaded IR gets ready even while the CAB is off
        cab.prepareIR (n, p.offline, ! (p.cabOn || cabMix.isSmoothing()));   // CAB not heard = resting, as far as the IR goes
        cabMix.setTargetValue (p.cabOn ? 1.0f : 0.0f);
        if (p.cabOn || cabMix.isSmoothing())
        {
            const bool fading = cabMix.isSmoothing();
            if (! cabWasRunning) cab.reset();
            if (fading) { dryBuf.copyFrom (0, 0, L, n); dryBuf.copyFrom (1, 0, R, n); }
            cab.process (L, R, n);
            if (fading)
                for (int i = 0; i < n; ++i)
                {
                    const float m0 = cabMix.getNextValue(), m = m0 * m0 * (3.0f - 2.0f * m0);
                    L[i] = L[i] * m + dryBuf.getSample (0, i) * (1.0f - m);
                    R[i] = R[i] * m + dryBuf.getSample (1, i) * (1.0f - m);
                }
            cabWasRunning = true;
        }
        else { cabMix.skip (n); cabWasRunning = false; }

        // ---- Reverb (after it is turned down it keeps running for 0.3 s so its own gains ramp down, then rests)
        if (p.revMix > 0.001f) revTail = (int) (fs * 0.3);
        if (revTail > 0)
        {
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            reverb.process (ctx);
            if (p.revMix <= 0.001f && (revTail -= n) <= 0) { revTail = 0; reverb.reset(); }
        }

        // ---- Output
        for (int i = 0; i < n; ++i)
        {
            const float g = outS.getNextValue();
            L[i] *= g; R[i] *= g;
        }
    }

    double fs = 44100.0, osFs = 176400.0;
    int maxBlock = 512;
    bool firstBlock = true;
    juce::dsp::Reverb reverb;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    ampsim::Amp amp;
    ampsim::Cab cab;
    std::vector<float> bypassDelay[2];
    int bypassPos = 0;
    juce::AudioBuffer<float> dryBuf;
    juce::SmoothedValue<float> ampMix, cabMix, outS;
    bool ampWasRunning = false, cabWasRunning = false, ampFirst = true;
    int revTail = 0;
};
