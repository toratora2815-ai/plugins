#pragma once
// Wool Fuzz の音の部分（単体の Wool Fuzz と AIO Guitar の EFFECTS が同じものを使う）。
// ビッグマフ系: 入力の低域カット → 4 倍オーバーサンプリングで非対称クリップ 2 段 → LP/HP ブレンドのトーン → 音量
#include <JuceHeader.h>

namespace pedal
{
class WoolFuzzDSP
{
public:
    static constexpr int maxCh = 2;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        numCh = juce::jlimit (1, maxCh, numChannels);
        // 4倍オーバーサンプリング（折り返しノイズ対策）
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing ((size_t) maxBlock);

        const double osRate = sampleRate * 4.0;
        for (int ch = 0; ch < maxCh; ++ch)
        {
            inHP[ch].setCutoff (90.0f, sampleRate);
            stage1LP[ch].setCutoff (6000.0f, osRate);
            stage2LP[ch].setCutoff (4500.0f, osRate);
            dcBlock1[ch].setCutoff (25.0f, osRate);
            dcBlock2[ch].setCutoff (25.0f, osRate);
            toneLP[ch].setCutoff (480.0f, sampleRate);   // ビッグマフ系トーン回路の定数に近い値
            toneHP[ch].setCutoff (1100.0f, sampleRate);
        }
        reset();
        for (auto* s : { &driveGain, &toneMix, &outGain })
            s->reset (sampleRate, 0.03);
    }

    void reset()
    {
        for (int ch = 0; ch < maxCh; ++ch)
            for (auto* f : { &inHP[ch], &stage1LP[ch], &stage2LP[ch], &dcBlock1[ch], &dcBlock2[ch], &toneLP[ch], &toneHP[ch] })
                f->reset();
        if (oversampling) oversampling->reset();
    }

    int latency() const { return oversampling ? (int) std::round (oversampling->getLatencyInSamples()) : 0; }

    // sustain / tone 0..10, volume dB. snap = jump there (no smoothing), e.g. right after prepare
    void setParams (float sustain, float tone, float volumeDb, bool snap = false)
    {
        // Sustain 0〜10 → +6〜+51 dB
        const float d = juce::Decibels::decibelsToGain (sustain * 4.5f + 6.0f), t = tone / 10.0f, o = juce::Decibels::decibelsToGain (volumeDb);
        if (snap) { driveGain.setCurrentAndTargetValue (d); toneMix.setCurrentAndTargetValue (t); outGain.setCurrentAndTargetValue (o); }
        else      { driveGain.setTargetValue (d);           toneMix.setTargetValue (t);           outGain.setTargetValue (o); }
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = juce::jmin (numChannels, numCh);

        // 1) 入力：低域カット＋ドライブ
        for (int n = 0; n < numSamples; ++n)
        {
            const float g = driveGain.getNextValue();
            for (int ch = 0; ch < nc; ++ch)
                data[ch][n] = inHP[ch].hp (data[ch][n]) * g;
        }

        // 2) オーバーサンプリングした状態で2段クリップ
        juce::dsp::AudioBlock<float> block (data, (size_t) nc, (size_t) numSamples);
        auto os = oversampling->processSamplesUp (block);
        for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
        {
            auto* d = os.getChannelPointer (ch);
            for (size_t n = 0; n < os.getNumSamples(); ++n)
            {
                float x = d[n];
                x = clipStage (x, 0.25f);
                x = dcBlock1[ch].hp (x);
                x = stage1LP[ch].lp (x);

                x = clipStage (x * 9.0f, -0.15f);
                x = dcBlock2[ch].hp (x);
                x = stage2LP[ch].lp (x);
                d[n] = x;
            }
        }
        oversampling->processSamplesDown (block);

        // 3) トーン（LPとHPのブレンド → 真ん中で中域がへこむ）＋出力音量
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = toneMix.getNextValue();
            const float g = outGain.getNextValue() * 2.0f; // トーン回路での減衰を補正
            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = data[ch][n];
                const float lo = toneLP[ch].lp (x);
                const float hi = toneHP[ch].hp (x);
                data[ch][n] = ((1.0f - t) * lo + t * hi) * g;
            }
        }
    }

private:
    // 1次フィルター（ローパス／ハイパス兼用）
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void setCutoff (float fc, double fs) { a = std::exp (-juce::MathConstants<float>::twoPi * fc / (float) fs); }
        float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
        float hp (float x) { return x - lp (x); }
        void reset() { z = 0.0f; }
    };

    // ダイオードっぽい非対称ソフトクリップ
    static inline float clipStage (float x, float bias) { return std::tanh (x + bias) - std::tanh (bias); }

    int numCh = 2;
    juce::SmoothedValue<float> driveGain, toneMix, outGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    OnePole inHP[maxCh];                      // 入力の低域カット
    OnePole stage1LP[maxCh], stage2LP[maxCh]; // 段間のハイ落とし
    OnePole dcBlock1[maxCh], dcBlock2[maxCh]; // 非対称クリップ後のDCカット
    OnePole toneLP[maxCh], toneHP[maxCh];     // トーン回路
};
} // namespace pedal
