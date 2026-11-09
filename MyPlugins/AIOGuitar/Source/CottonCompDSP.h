#pragma once
// Cotton Comp: EFFECTS のいちばん前に固定のコンプ（ギター用ペダルの「パコッとつぶれる」OTA 系の動き）。
// COMP（つぶす量）/ LEVEL（自動で音量を補い、0 dB で ON/OFF がほぼ同じ音量）/ BLEND（原音を混ぜる）。
// アタックは固定 6 ms（ピックの頭が少し抜ける）、リリースは COMP に合わせて自動。
// 持ち上げは最大 +20 dB。ノイズくらいの小さい音（30 ms の RMS で −56 dBFS より下）は持ち上げない（録音の尻尾のハム・サーッを大きくしない）。
// 持ち上げの「開け閉め」は圧縮とは別: 開くのは速く（2 ms）、閉じるのはゆっくり（60 ms）。
// ピックが圧縮より先に抜ける量は、その音量で決まる増幅の +5 dB まで（2 音目のピックだけ飛び出さないように）。
// 歪ませないのでオーバーサンプリングなし、遅れ 0。
#include <JuceHeader.h>

namespace pedal
{
class CottonCompDSP
{
public:
    static constexpr int maxCh = 2;

    void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
    {
        sr = sampleRate;
        numCh = juce::jlimit (1, maxCh, numChannels);
        holdLen = juce::jmax (1, (int) (sr * 0.013));          // longer than one period of low E (12 ms): no ripple
        detRel = std::exp (-1.0f / (float) (sr * 0.040));       // detector release 40 ms
        atk    = std::exp (-1.0f / (float) (sr * 0.006));       // gain attack 6 ms
        msCoef = std::exp (-1.0f / (float) (sr * 0.030));       // 30 ms mean square (noise or not)
        openC  = std::exp (-1.0f / (float) (sr * 0.002));       // the lift opens in 2 ms
        closeC = std::exp (-1.0f / (float) (sr * 0.060));       // and closes in 60 ms
        for (auto* s : { &outGain, &blendMix })
            s->reset (sampleRate, 0.03);
        updateCurve();
        reset();
    }

    void reset()
    {
        env = 0.0f; hold = 0; ms = 0.0f; lift = 0.0f;
        gainDb = makeupDb;   // at rest: nothing to compress
    }

    int latency() const { return 0; }

    // comp 0..10, level dB (-12..12), blend 0..100 %. snap = jump there (no smoothing), e.g. right after prepare
    void setParams (float comp, float levelDb, float blendPct, bool snap = false)
    {
        if (comp != compAmt) { compAmt = comp; updateCurve(); }
        const float o = juce::Decibels::decibelsToGain (levelDb), b = juce::jlimit (0.0f, 1.0f, blendPct / 100.0f);
        if (snap) { outGain.setCurrentAndTargetValue (o); blendMix.setCurrentAndTargetValue (b); gainDb = compGainDb (env > 0 ? juce::Decibels::gainToDecibels (env) : -200.0f); }
        else      { outGain.setTargetValue (o);           blendMix.setTargetValue (b); }
    }

    // how much the gain is turned down right now (dB, >= 0) - for a meter
    float reductionDb() const { return juce::jmax (0.0f, makeupDb - gainDb); }

    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = juce::jmin (numChannels, numCh);
        for (int n = 0; n < numSamples; ++n)
        {
            // detector: stereo-linked peak with a 13 ms hold, then 40 ms release
            float a = std::abs (data[0][n]);
            if (nc > 1) a = juce::jmax (a, std::abs (data[1][n]));
            if (! std::isfinite (a)) { reset(); a = 0.0f; }   // never stay broken after a bad sample
            if (a >= env) { env = a; hold = holdLen; }
            else if (hold > 0) --hold;
            else env *= detRel;
            ms = a * a + msCoef * (ms - a * a);

            // compression: going down = attack (6 ms), coming back up = release (auto).
            // A pick may get through ahead of it, but at most 5 dB over what its level calls for
            const float target = compGainDb (env > 1.0e-10f ? 20.0f * std::log10 (env) : -200.0f);
            gainDb = target + (target < gainDb ? atk : rel) * (gainDb - target);
            float gDb = juce::jmin (gainDb, target + 5.0f);

            // the lift (gain above 0 dB) only for sound louder than noise: judged on the 30 ms RMS
            const float rmsDb = ms > 1.0e-14f ? 10.0f * std::log10 (ms) : -140.0f;
            const float want = juce::jlimit (0.0f, 1.0f, 1.0f - (kNoiseDb - rmsDb) / 14.0f);
            lift = want + (want > lift ? openC : closeC) * (lift - want);
            if (gDb > 0.0f) gDb *= lift;

            const float g = std::pow (10.0f, gDb * 0.05f);
            const float b = blendMix.getNextValue(), o = outGain.getNextValue();
            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = data[ch][n];
                data[ch][n] = (x * (1.0f - b) + x * g * b) * o;
            }
        }
    }

private:
    // the static curve: make-up minus soft-knee compression above the threshold (dB, for a peak level in dBFS)
    float compGainDb (float xDb) const
    {
        const float over = xDb - thresh;
        float gr = 0.0f;
        if (2.0f * over >= knee)            gr = slope * over;
        else if (2.0f * over > -knee)       { const float t = over + 0.5f * knee; gr = slope * t * t / (2.0f * knee); }
        return makeupDb - gr;
    }

    void updateCurve()
    {
        const float c = juce::jlimit (0.0f, 10.0f, compAmt);
        thresh = -12.0f - 3.0f * c;                       // -12 .. -42 dBFS
        const float ratio = 2.0f + 0.8f * c;               // 2 .. 10 : 1
        slope = 1.0f - 1.0f / ratio;
        // auto make-up: give back what a typical note (peaks around -14 dBFS) loses, at most +20 dB
        makeupDb = 0.0f;
        makeupDb = juce::jlimit (0.0f, kMaxLiftDb, -0.9f * compGainDb (-14.0f));
        rel = std::exp (-1.0f / (float) (sr * (0.12 + 0.03 * c)));   // release 120 .. 420 ms
    }

    static constexpr float kNoiseDb = -56.0f, kMaxLiftDb = 20.0f, knee = 8.0f;   // kNoiseDb: 30 ms RMS
    double sr = 48000.0;
    int numCh = 2, holdLen = 600, hold = 0;
    float compAmt = -1.0f, thresh = -27.0f, slope = 0.8f, makeupDb = 0.0f;
    float env = 0.0f, gainDb = 0.0f, ms = 0.0f, lift = 0.0f, detRel = 0.999f, atk = 0.99f, rel = 0.9999f;
    float msCoef = 0.999f, openC = 0.99f, closeC = 0.999f;
    juce::SmoothedValue<float> outGain, blendMix;
};
} // namespace pedal
