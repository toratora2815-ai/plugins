#pragma once
// AMP ページの部品: [4 倍オーバーサンプリング: AMP] と CAB（＋マイク / 読み込んだ IR）→ OUTPUT。
// AMP はペダルと一緒に好きな位置に並べられる（並べるのは Rig.h）。CAB と OUTPUT はいつも最後。
// AMP と CAB は 15 ms のクロスフェードで ON/OFF。AMP を OFF にしても同じだけ遅らせる（申告した遅れのまま）。
// 音の部品は Amp.h（ampsim::Amp / ampsim::Cab）。内蔵リバーブは 0.3.0 で外した（自作のリバーブを後ろにつなぐ）。
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
        if (os) os->reset();
        amp.reset();
        cab.reset();
        ampWasRunning = false;
        for (auto& d : bypassDelay) std::fill (d.begin(), d.end(), 0.0f);
    }

    // the AMP only (the cabinet keeps its tail): from silence, e.g. when the blocks are reordered
    void resetAmp()
    {
        if (os) os->reset();
        amp.reset();
        for (auto& d : bypassDelay) std::fill (d.begin(), d.end(), 0.0f);
    }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }
    int getMaxBlock() const { return maxBlock; }

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

    // once per host block, before the parts below
    void beginBlock (const FXParams& p)
    {
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
    }

    // ---- AMP (oversampled), n <= getMaxBlock(). The way around it is delayed by the same amount (the reported
    // latency is always right), so switching AMP on / off is a 15 ms crossfade, not a click
    void processAmp (float* L, float* R, int n, const FXParams& p)
    {
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
            float* chans[2] { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            amp.process (up.getChannelPointer (0), up.getChannelPointer (1), un);
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
    }

    // ---- Cabinet + mic (crossfaded on / off as well), then OUTPUT. n <= getMaxBlock().
    // A loaded IR gets ready even while the CAB is off
    void processCabOut (float* L, float* R, int n, const FXParams& p)
    {
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

        for (int i = 0; i < n; ++i)
        {
            const float g = outS.getNextValue();
            L[i] *= g; R[i] *= g;
        }
    }

    // AMP then CAB / OUTPUT (the order used before the blocks could be moved; also the AMP probe)
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p)
    {
        beginBlock (p);
        for (int pos = 0; pos < numSamples; pos += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - pos);
            float* L = buffer.getWritePointer (0) + pos;
            float* R = buffer.getWritePointer (1) + pos;
            processAmp (L, R, n, p);
            processCabOut (L, R, n, p);
        }
    }

private:
    double fs = 44100.0, osFs = 176400.0;
    int maxBlock = 512;
    bool firstBlock = true;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    ampsim::Amp amp;
    ampsim::Cab cab;
    std::vector<float> bypassDelay[2];
    int bypassPos = 0;
    juce::AudioBuffer<float> dryBuf;
    juce::SmoothedValue<float> ampMix, cabMix, outS;
    bool ampWasRunning = false, cabWasRunning = false, ampFirst = true;
};
