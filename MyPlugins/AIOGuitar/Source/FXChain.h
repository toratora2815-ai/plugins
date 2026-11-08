#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Biquad.h"

struct FXParams
{
    // gate
    float gateThresh = -80.0f;
    // compressor
    bool  compOn = false;
    float compThresh = -20.0f, compRatio = 4.0f, compAttack = 5.0f, compRelease = 120.0f, compMakeup = 4.0f;
    // drive pedal (knobs 0..10)
    bool  drvOn = false;
    int   drvType = 0;          // 0 Overdrive, 1 Distortion, 2 Fuzz
    float drvDrive = 5, drvTone = 5, drvLevel = 5;
    // amp (knobs 0..10)
    bool  ampOn = true;
    int   ampChannel = 1;       // 0 Clean, 1 Crunch, 2 Lead
    float ampGain = 5, ampBass = 5, ampMid = 5, ampTreble = 5, ampPresence = 5, ampMaster = 5;
    // cab
    bool  cabOn = true;
    int   cabType = 0;          // 0 4x12 Closed, 1 2x12 Combo, 2 1x12 Open
    float cabMic = 5;
    // reverb
    float revMix = 0.12f, revSize = 0.5f;
    // output
    float outGain = 0.0f;
};

//==============================================================================
// Gate -> Compressor -> [4x oversampled: Drive pedal -> Amp] -> Cab -> Reverb -> Out
class FXChain
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (16, maxBlockSize);

        juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
        comp.prepare (spec);
        reverb.prepare (spec);

        os = std::make_unique<juce::dsp::Oversampling<float>> (
                2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlock);
        osFs = fs * 4.0;

        drvDC.prepare (osFs);
        ampDC.prepare (osFs);

        for (auto* s : { &drvGainS, &drvLevelS, &ampPreS, &ampMasterS, &outS, &makeupS })
            s->reset (fs, 0.03);

        reset();
    }

    void reset()
    {
        gateEnv = 0; gateGain = 0;
        firstBlock = true;
        comp.reset();
        reverb.reset();
        if (os) os->reset();
        for (auto* b : allFilters()) b->reset();
        drvDC.reset(); ampDC.reset();
    }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p)
    {
        updateFilters (p);

        drvGainS.setTargetValue (driveGainLinear (p));
        drvLevelS.setTargetValue (juce::Decibels::decibelsToGain (-20.0f + p.drvLevel * 3.0f));
        ampPreS.setTargetValue (ampPreGainLinear (p));
        ampMasterS.setTargetValue (juce::Decibels::decibelsToGain (-30.0f + p.ampMaster * 3.6f));
        outS.setTargetValue (juce::Decibels::decibelsToGain (p.outGain));
        makeupS.setTargetValue (juce::Decibels::decibelsToGain (p.compMakeup));
        if (firstBlock)
        {
            for (auto* sv : { &drvGainS, &drvLevelS, &ampPreS, &ampMasterS, &outS, &makeupS })
                sv->setCurrentAndTargetValue (sv->getTargetValue());
            firstBlock = false;
        }

        comp.setThreshold (p.compThresh);
        comp.setRatio (p.compRatio);
        comp.setAttack (p.compAttack);
        comp.setRelease (p.compRelease);

        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.65f * p.revSize;
        rp.damping  = 0.45f;
        rp.wetLevel = p.revMix;
        rp.dryLevel = 1.0f - 0.6f * p.revMix;
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
    //--------------------------------------------------------------------------
    void processChunk (juce::dsp::AudioBlock<float>& block, const FXParams& p)
    {
        const int n = (int) block.getNumSamples();
        auto* L = block.getChannelPointer (0);
        auto* R = block.getChannelPointer (1);

        // ---- Noise gate
        if (p.gateThresh > -79.5f)
        {
            const float thr  = juce::Decibels::decibelsToGain (p.gateThresh);
            const float envR = std::exp (-1.0f / (0.05f * (float) fs));
            const float atk  = 1.0f - std::exp (-1.0f / (0.001f * (float) fs));
            const float rel  = 1.0f - std::exp (-1.0f / (0.06f * (float) fs));
            for (int i = 0; i < n; ++i)
            {
                const float lvl = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                gateEnv = lvl > gateEnv ? lvl : gateEnv * envR;
                const float target = gateEnv > thr ? 1.0f : 0.0f;
                gateGain += (target - gateGain) * (target > gateGain ? atk : rel);
                L[i] *= gateGain; R[i] *= gateGain;
            }
        }

        // ---- Compressor
        if (p.compOn)
        {
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            comp.process (ctx);
            for (int i = 0; i < n; ++i)
            {
                const float m = makeupS.getNextValue();
                L[i] *= m; R[i] *= m;
            }
        }
        else makeupS.skip (n);

        // ---- Drive + Amp (oversampled)
        if (p.drvOn || p.ampOn)
        {
            auto up = os->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            float* ch[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };

            for (int i = 0; i < un; ++i)
            {
                // smoothed values advance at base rate
                const bool step = (i & 3) == 0;
                const float dg  = step ? drvGainS.getNextValue()  : drvGainS.getCurrentValue();
                const float dl  = step ? drvLevelS.getNextValue() : drvLevelS.getCurrentValue();
                const float pre = step ? ampPreS.getNextValue()   : ampPreS.getCurrentValue();
                const float mst = step ? ampMasterS.getNextValue(): ampMasterS.getCurrentValue();

                for (int c = 0; c < 2; ++c)
                {
                    float x = ch[c][i];
                    if (p.drvOn) x = drivePedal (x, c, p.drvType, dg) * dl;
                    if (p.ampOn) x = ampSection (x, c, p.ampChannel, pre) * mst;
                    ch[c][i] = x;
                }
            }
            os->processSamplesDown (block);
        }
        else
        {
            drvGainS.skip (n); drvLevelS.skip (n); ampPreS.skip (n); ampMasterS.skip (n);
        }

        // ---- Cabinet
        if (p.cabOn)
        {
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                for (int k = 0; k < numCab; ++k) { l = cab[(size_t) k].process (l, 0); r = cab[(size_t) k].process (r, 1); }
                L[i] = l; R[i] = r;
            }
        }

        // ---- Reverb
        if (p.revMix > 0.001f)
        {
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            reverb.process (ctx);
        }

        // ---- Output
        for (int i = 0; i < n; ++i)
        {
            const float g = outS.getNextValue();
            L[i] *= g; R[i] *= g;
        }
    }

    //--------------------------------------------------------------------------
    inline float drivePedal (float x, int c, int type, float gain) noexcept
    {
        float y;
        switch (type)
        {
            case 0: // Overdrive (TS style: clean lows + clipped mids)
            {
                const float hp = drvPre.process (x, c);
                y = 0.35f * x + std::tanh (hp * gain);
                break;
            }
            case 1: // Distortion (hard-ish clipping)
            {
                const float v = drvPre.process (x, c) * gain;
                const float av = std::abs (v);
                y = v / std::pow (1.0f + av * av * std::sqrt (av), 0.4f);
                y = drvPre2.process (y, c);  // mid hump
                break;
            }
            default: // Fuzz (asymmetric, bassy)
            {
                const float v = drvPre.process (x, c) * gain + 0.25f;
                y = v > 0.0f ? std::tanh (v) : 0.55f * std::tanh (v * 1.8f);
                y = drvDC.process (y, c);
                break;
            }
        }
        y = drvTone.process (y, c);
        return drvPost.process (y, c);
    }

    static inline float stage (float x, float bias) noexcept
    {
        return std::tanh (x + bias) - std::tanh (bias);
    }

    inline float ampSection (float x, int c, int channel, float preGain) noexcept
    {
        x = ampIn.process (x, c);
        x = ampBright.process (x, c);
        x *= preGain;

        // preamp stages
        x = stage (x, 0.08f);
        x = c1LP.process (c1HP.process (x, c), c);
        float norm = 2.6f;
        if (channel >= 1)
        {
            x = stage (x * 3.0f, -0.18f);
            x = c2LP.process (c2HP.process (x, c), c);
            norm = 0.9f;
        }
        if (channel >= 2)
        {
            x = stage (x * 3.5f, 0.06f);
            x = c3LP.process (x, c);
            norm = 0.75f;
        }
        x = ampDC.process (x, c);

        // tone stack
        x = bass.process (x, c);
        x = mid.process (x, c);
        x = treble.process (x, c);
        x = presence.process (x, c);

        // power amp
        x = std::tanh (1.4f * x * norm) * 0.5f;
        return pwrLP.process (x, c);
    }

    //--------------------------------------------------------------------------
    float driveGainLinear (const FXParams& p) const
    {
        float db = 0;
        switch (p.drvType)
        {
            case 0:  db = 0.0f  + p.drvDrive * 3.6f; break;  //  0..36 dB
            case 1:  db = 10.0f + p.drvDrive * 4.0f; break;  // 10..50 dB
            default: db = 16.0f + p.drvDrive * 3.6f; break;  // 16..52 dB
        }
        return juce::Decibels::decibelsToGain (db);
    }

    float ampPreGainLinear (const FXParams& p) const
    {
        float db = 0;
        switch (p.ampChannel)
        {
            case 0:  db = -8.0f + p.ampGain * 2.4f; break;   // -8..16 dB
            case 1:  db =  0.0f + p.ampGain * 3.0f; break;   //  0..30 dB
            default: db =  8.0f + p.ampGain * 3.4f; break;   //  8..42 dB
        }
        return juce::Decibels::decibelsToGain (db);
    }

    void updateFilters (const FXParams& p)
    {
        using B = Biquad;
        // drive pedal (oversampled rate)
        switch (p.drvType)
        {
            case 0:  drvPre.set (B::HighPass, osFs, 720.0, 0.5);  drvPre2.setBypass(); break;
            case 1:  drvPre.set (B::HighPass, osFs, 90.0, 0.7);   drvPre2.set (B::Peak, osFs, 1000.0, 0.7, 3.0); break;
            default: drvPre.set (B::LowShelf, osFs, 200.0, 0.7, 4.0); drvPre2.setBypass(); break;
        }
        drvTone.set (B::LowPass, osFs, 700.0 * std::pow (2.0, p.drvTone * 0.4), 0.707);
        drvPost.set (B::HighPass, osFs, 40.0, 0.707);

        // amp (oversampled rate)
        const int ch = p.ampChannel;
        ampIn.set (B::HighPass, osFs, ch == 0 ? 40.0 : (ch == 1 ? 70.0 : 110.0), 0.707);
        if (ch == 0) ampBright.set (B::HighShelf, osFs, 2500.0, 0.7, 4.0 * (1.0 - p.ampGain / 10.0));
        else         ampBright.setBypass();
        c1HP.set (B::HighPass, osFs, 25.0, 0.707);
        c1LP.set (B::LowPass,  osFs, 9000.0, 0.707);
        c2HP.set (B::HighPass, osFs, ch == 2 ? 90.0 : 45.0, 0.707);
        c2LP.set (B::LowPass,  osFs, 7000.0, 0.707);
        c3LP.set (B::LowPass,  osFs, 6000.0, 0.707);
        bass.set     (B::LowShelf,  osFs, 120.0,  0.7, (p.ampBass   - 5.0f) * 2.4f);
        mid.set      (B::Peak,      osFs, 650.0,  0.6, (p.ampMid    - 5.0f) * 2.4f);
        treble.set   (B::HighShelf, osFs, 2200.0, 0.7, (p.ampTreble - 5.0f) * 2.4f);
        presence.set (B::HighShelf, osFs, 4000.0, 0.7, (p.ampPresence - 5.0f) * 1.8f);
        pwrLP.set    (B::LowPass,   osFs, 12000.0, 0.707);

        // cabinet (base rate)
        const double mic = (p.cabMic - 5.0f) * 1.4f;
        int k = 0;
        auto add = [&] (B::Type t, double f, double q, double g = 0.0) { cab[(size_t) k++].set (t, fs, f, q, g); };
        switch (p.cabType)
        {
            case 0: // 4x12 closed back: tight thump, scooped fizz
                add (B::HighPass, 75, 0.8); add (B::Peak, 110, 1.3, 4.0); add (B::Peak, 400, 1.0, -2.0);
                add (B::Peak, 2200, 1.5, 3.0); add (B::Peak, 4200, 2.0, -5.0);
                add (B::LowPass, 5200, 0.8); add (B::LowPass, 5600, 0.6);
                break;
            case 1: // 2x12 combo
                add (B::HighPass, 70, 0.7); add (B::Peak, 130, 1.0, 2.5); add (B::Peak, 2500, 1.4, 4.0);
                add (B::Peak, 4500, 2.0, -4.0); add (B::LowPass, 6000, 0.75); add (B::LowPass, 6400, 0.6);
                break;
            default: // 1x12 open back: airy, less bass
                add (B::HighPass, 90, 0.7); add (B::Peak, 150, 1.0, 1.0); add (B::Peak, 1500, 1.2, 2.0);
                add (B::Peak, 3000, 1.5, 3.0); add (B::LowPass, 7000, 0.75); add (B::LowPass, 7500, 0.6);
                break;
        }
        add (B::HighShelf, 3000, 0.7, mic);
        numCab = k;
    }

    std::vector<Biquad*> allFilters()
    {
        std::vector<Biquad*> v { &drvPre, &drvPre2, &drvTone, &drvPost, &ampIn, &ampBright, &c1HP, &c1LP, &c2HP,
                                 &c2LP, &c3LP, &bass, &mid, &treble, &presence, &pwrLP };
        for (auto& b : cab) v.push_back (&b);
        return v;
    }

    double fs = 44100.0, osFs = 176400.0;
    int maxBlock = 512;

    float gateEnv = 0, gateGain = 0;
    bool firstBlock = true;
    juce::dsp::Compressor<float> comp;
    juce::dsp::Reverb reverb;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;

    Biquad drvPre, drvPre2, drvTone, drvPost;
    DCBlocker drvDC, ampDC;
    Biquad ampIn, ampBright, c1HP, c1LP, c2HP, c2LP, c3LP, bass, mid, treble, presence, pwrLP;
    std::array<Biquad, 10> cab;
    int numCab = 0;

    juce::SmoothedValue<float> drvGainS, drvLevelS, ampPreS, ampMasterS, outS, makeupS;
};
