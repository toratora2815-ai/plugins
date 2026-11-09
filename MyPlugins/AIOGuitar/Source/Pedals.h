#pragma once
// EFFECTS ページのペダル。中身は単体プラグインと同じ音の部品（WoolFuzzDSP / PorchDriveDSP）と、
// AIO Guitar だけのコンプ（CottonCompDSP、いちばん前に固定）。
// OFF のペダルは計算しない（タイミングをそろえる短い遅らせだけ）。ON/OFF は 15 ms でつなぐ。
#include <JuceHeader.h>
#include "WoolFuzzDSP.h"
#include "PorchDriveDSP.h"
#include "CottonCompDSP.h"

template <typename DSP>
class PedalSlot
{
public:
    void prepare (double sr, int maxBlock)
    {
        dsp.prepare (sr, maxBlock, 2);
        lat = dsp.latency();
        wet.setSize (2, maxBlock, false, true, false);
        for (auto& d : delay) d.assign ((size_t) lat + 1, 0.0f);
        delayPos = 0;
        mix.reset (sr, 0.015);
        mix.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
        wasRunning = on;
    }

    int latency() const { return lat; }

    void setParams (float a, float b, float c) { dsp.setParams (a, b, c); }
    void snapParams (float a, float b, float c) { dsp.setParams (a, b, c, true); }

    void setOn (bool shouldBeOn)
    {
        if (shouldBeOn == on) return;
        on = shouldBeOn;
        mix.setTargetValue (on ? 1.0f : 0.0f);
    }

    bool isRunning() const { return on || mix.isSmoothing(); }

    void process (float* const* data, int numSamples)
    {
        const bool running = isRunning();
        if (running && ! wasRunning) dsp.reset();   // starts from silence, not from where it stopped
        wasRunning = running;

        if (! running)   // OFF: only the short delay that keeps the timing the same as ON
        {
            delayOnly (data, numSamples);
            return;
        }
        if (! mix.isSmoothing())   // fully ON: the pedal alone (no dry path needed)
        {
            dsp.process (data, 2, numSamples);
            return;
        }
        // switching: fade between the delayed dry signal and the pedal
        for (int c = 0; c < 2; ++c) wet.copyFrom (c, 0, data[c], numSamples);
        dsp.process (wet.getArrayOfWritePointers(), 2, numSamples);
        delayOnly (data, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mix.getNextValue();
            for (int c = 0; c < 2; ++c) data[c][i] = data[c][i] * (1.0f - m) + wet.getSample (c, i) * m;
        }
    }

private:
    void delayOnly (float* const* data, int numSamples)
    {
        if (lat <= 0) return;
        const int len = lat + 1;
        int p = delayPos;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                auto& d = delay[c];
                d[(size_t) p] = data[c][i];
                data[c][i] = d[(size_t) ((p + 1) % len)];
            }
            p = (p + 1) % len;
        }
        delayPos = p;
    }

    DSP dsp;
    juce::AudioBuffer<float> wet;
    std::vector<float> delay[2];
    int lat = 0, delayPos = 0;
    bool on = false, wasRunning = false;
    juce::SmoothedValue<float> mix;
};

struct PedalBoard
{
    PedalSlot<pedal::CottonCompDSP> comp;    // Cotton Comp (always first)
    PedalSlot<pedal::PorchDriveDSP> drive;   // Porch Drive
    PedalSlot<pedal::WoolFuzzDSP>   fuzz;    // Wool Fuzz

    void prepare (double sr, int maxBlock)
    {
        comp.prepare (sr, maxBlock); drive.prepare (sr, maxBlock); fuzz.prepare (sr, maxBlock);
        fadeLen = juce::jmax (1, (int) (sr * 0.005));
        fadePos = 0; fadeState = 0;
    }
    int latency() const { return comp.latency() + drive.latency() + fuzz.latency(); }
    bool anyRunning() const { return comp.isRunning() || drive.isRunning() || fuzz.isRunning(); }
    bool isFuzzFirst() const { return fuzzFirst; }
    void setOrderNow (bool fuzzFirstNow) { fuzzFirst = fuzzFirstNow; fadeState = 0; }   // prepare / load: no fade

    // changing the order: 5 ms down, swap at the next block, 5 ms up (no click)
    void process (float* const* data, int n, bool wantFuzzFirst)
    {
        if (fadeState == 3) { fuzzFirst = wantFuzzFirst; fadeState = 2; fadePos = 0; }
        if (fadeState == 0 && wantFuzzFirst != fuzzFirst) { fadeState = 1; fadePos = fadeLen; }

        comp.process (data, n);
        if (fuzzFirst) { fuzz.process (data, n); drive.process (data, n); }
        else           { drive.process (data, n); fuzz.process (data, n); }

        if (fadeState == 0) return;
        for (int i = 0; i < n; ++i)
        {
            float g = 1.0f;
            if (fadeState == 1)      { g = (float) fadePos / (float) fadeLen; if (fadePos > 0) --fadePos; else fadeState = 3; }
            else if (fadeState == 3) g = 0.0f;   // silent until the next block, where the order changes
            else if (fadeState == 2) { g = (float) fadePos / (float) fadeLen; if (++fadePos >= fadeLen) fadeState = 0; }
            data[0][i] *= g; data[1][i] *= g;
        }
    }

private:
    bool fuzzFirst = false;
    int fadeLen = 240, fadePos = 0, fadeState = 0;   // 0 steady, 1 fading out, 3 silent (swap next block), 2 fading in
};
