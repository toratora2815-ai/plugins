#pragma once
// Porch Drive の音の部分（単体の Porch Drive と AIO Guitar の EFFECTS が同じものを使う）。
// BD-2 系: 入力の低域カット → 4 倍オーバーサンプリングで トランジスタ段 → ダイオード段 → 900 Hz より上のトーン → 音量
#include <JuceHeader.h>

namespace pedal
{
class PorchDriveDSP
{
public:
    static constexpr int maxCh = 2;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        numCh = juce::jlimit (1, maxCh, numChannels);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing ((size_t) maxBlock);

        g1Buf.assign ((size_t) maxBlock * 4 + 16, 1.0f);
        g2Buf.assign ((size_t) maxBlock * 4 + 16, 1.0f);

        const double os = sampleRate * 4.0;
        for (int ch = 0; ch < maxCh; ++ch)
        {
            inHP[ch].setCutoff (70.0f, sampleRate);
            dc1[ch].setCutoff (20.0f, os);
            dc2[ch].setCutoff (20.0f, os);
            lp1[ch].setCutoff (9000.0f, os);
            lp2[ch].setCutoff (7000.0f, os);
            toneSplit[ch].setCutoff (900.0f, sampleRate);
            fizzLP[ch].setCutoff (9500.0f, sampleRate);
        }
        reset();
        for (auto* s : { &g1, &g2, &toneGain, &outGain })
            s->reset (sampleRate * 4.0, 0.03);
    }

    void reset()
    {
        for (int ch = 0; ch < maxCh; ++ch)
            for (auto* f : { &inHP[ch], &dc1[ch], &dc2[ch], &lp1[ch], &lp2[ch], &toneSplit[ch], &fizzLP[ch] })
                f->reset();
        if (oversampling) oversampling->reset();
    }

    int latency() const { return oversampling ? (int) std::round (oversampling->getLatencyInSamples()) : 0; }

    // gain / tone 0..10, level dB
    void setParams (float gain, float tone, float levelDb, bool snap = false)
    {
        const float a = juce::Decibels::decibelsToGain (gain1dB (gain)), b = juce::Decibels::decibelsToGain (gain2dB (gain));
        const float t = juce::Decibels::decibelsToGain (toneDB (tone)), o = juce::Decibels::decibelsToGain (levelDb);
        if (snap) { g1.setCurrentAndTargetValue (a); g2.setCurrentAndTargetValue (b); toneGain.setCurrentAndTargetValue (t); outGain.setCurrentAndTargetValue (o); }
        else      { g1.setTargetValue (a);           g2.setTargetValue (b);           toneGain.setTargetValue (t);           outGain.setTargetValue (o); }
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = juce::jmin (numChannels, numCh);
        for (int ch = 0; ch < nc; ++ch)
            for (int n = 0; n < numSamples; ++n)
                data[ch][n] = inHP[ch].hp (data[ch][n]);

        juce::dsp::AudioBlock<float> block (data, (size_t) nc, (size_t) numSamples);
        auto os = oversampling->processSamplesUp (block);
        const size_t osN = os.getNumSamples();
        jassert (g1Buf.size() >= osN);   // prepare() sized it for maxBlock (a bigger block uses the last gain for the rest)
        const size_t m = juce::jmin (osN, g1Buf.size());
        for (size_t n = 0; n < m; ++n) { g1Buf[n] = g1.getNextValue(); g2Buf[n] = g2.getNextValue(); }
        const float g1Last = m > 0 ? g1Buf[m - 1] : g1.getCurrentValue(), g2Last = m > 0 ? g2Buf[m - 1] : g2.getCurrentValue();

        for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
        {
            auto* d = os.getChannelPointer (ch);
            for (size_t n = 0; n < osN; ++n)
            {
                float x = softStage (d[n] * (n < m ? g1Buf[n] : g1Last));
                x = lp1[ch].lp (dc1[ch].hp (x));
                x = diodeStage (x * (n < m ? g2Buf[n] : g2Last));
                x = lp2[ch].lp (dc2[ch].hp (x));
                d[n] = x;
            }
        }
        oversampling->processSamplesDown (block);

        // トーン：900Hz より上をブースト／カット
        for (int n = 0; n < numSamples; ++n)
        {
            float tg = 0.0f, og = 0.0f;
            for (int k = 0; k < 4; ++k) { tg = toneGain.getNextValue(); og = outGain.getNextValue(); }
            for (int ch = 0; ch < nc; ++ch)
            {
                const float lo = toneSplit[ch].lp (data[ch][n]);
                const float hi = data[ch][n] - lo;
                data[ch][n] = fizzLP[ch].lp (lo + hi * tg) * og;
            }
        }
    }

private:
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void setCutoff (float fc, double fs) { a = std::exp (-juce::MathConstants<float>::twoPi * fc / (float) fs); }
        float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
        float hp (float x) { return x - lp (x); }
        void reset() { z = 0.0f; }
    };

    // 1段目：トランジスタ的なゆるい非対称ソフトクリップ（偶数倍音で温かく）
    static inline float softStage (float x) { const float b = 0.2f; return std::tanh (x + b) - std::tanh (b); }
    // 2段目：ダイオード対のクリップ（膝がやや硬め）
    static inline float diodeStage (float x) { const float p = 2.5f; return x / std::pow (1.0f + std::pow (std::abs (x), p), 1.0f / p); }

    static float gain1dB (float knob) { return knob * 2.6f; }          // 0〜26 dB
    static float gain2dB (float knob) { return 2.0f + knob * 1.9f; }   // 2〜21 dB
    static float toneDB  (float knob) { return (knob - 5.0f) * 2.4f; } // 高域 ±12 dB

    int numCh = 2;
    juce::SmoothedValue<float> g1, g2, toneGain, outGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> g1Buf, g2Buf;
    OnePole inHP[maxCh], dc1[maxCh], dc2[maxCh], lp1[maxCh], lp2[maxCh];
    OnePole toneSplit[maxCh], fizzLP[maxCh];
};
} // namespace pedal
