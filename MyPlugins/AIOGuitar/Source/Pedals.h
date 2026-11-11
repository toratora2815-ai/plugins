#pragma once
// EFFECTS ページのペダル 1 台分。中身は DRIVE A / B（DriveSlot.h: 単体プラグインと同じ PORCH DRIVE / WOOL FUZZ と
// AIO Guitar だけの HEARTH DRIVE / BRASS BOOST / BARN DIST から 1 つ）と、AIO Guitar だけのコンプ（CottonCompDSP）、
// My Tremolo の小型版（TremPedalDSP）。並べる順番は Rig.h。
// OFF のペダルは計算しない（タイミングをそろえる短い遅らせだけ）。ON/OFF は 15 ms でつなぐ。
#include <JuceHeader.h>
#include "DriveSlot.h"
#include "CottonCompDSP.h"
#include "TremPedalDSP.h"

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
    DSP& get() { return dsp; }   // TREM の時計など、3 つのつまみ以外を渡すとき
    void snapParams (float a, float b, float c) { dsp.setParams (a, b, c, true); }

    void setOn (bool shouldBeOn)
    {
        if (shouldBeOn == on) return;
        on = shouldBeOn;
        mix.setTargetValue (on ? 1.0f : 0.0f);
    }

    bool isRunning() const { return on || mix.isSmoothing(); }

    // a broken sample went through: start clean
    void reset()
    {
        dsp.reset();
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
    }

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
