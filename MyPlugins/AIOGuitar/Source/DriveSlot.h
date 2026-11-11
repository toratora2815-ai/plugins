#pragma once
// DRIVE A / DRIVE B の箱の中身（0.6.0）: 5 機種から 1 つを選んで鳴らす。
//  PORCH DRIVE / WOOL FUZZ … 単体プラグインと同じ音の部品（PorchDriveDSP / WoolFuzzDSP）
//  HEARTH DRIVE / BRASS BOOST / BARN DIST … AIO Guitar だけ（DrivePedals.h、定番の回路をそのまま式にしたもの）
// つまみは箱ごとに 3 つ（GAIN / TONE / LEVEL）で、機種を替えてもパラメーターは同じ（Logic のオートメーションの意味が変わらない）。
//  - LEVEL: 箱の初期値（A −6 dB、B −12 dB）のとき、どの機種もその機種の「ちょうどいい大きさ」になるようにずらして渡す
//    （PORCH は −6 dB、WOOL は −12 dB が単体プラグインの初期値。新しい 3 機種は 0 dB で PORCH の初期値と同じくらいに合わせてある）。
//    なので A の PORCH・B の WOOL は 0.5.0 までとまったく同じ音
//  - 機種の切り替え: 鳴っている機種を 5 ms で下げる → 次のブロックで切り替えて（まっさらから）→ 10 ms で上げる。プチッといわない
//  - 5 機種とも同じ 4 倍オーバーサンプリングなので、遅れはどれでも同じ（並びや機種で Logic の遅れの補正が変わらない）
#include "WoolFuzzDSP.h"
#include "PorchDriveDSP.h"
#include "DrivePedals.h"

namespace pedal
{
class DriveSlotDSP
{
public:
    enum Model { porch = 0, wool, hearth, brass, barn, numModels };

    static const juce::StringArray& modelNames()
    {
        static const juce::StringArray n { "Porch Drive", "Wool Fuzz", "Hearth Drive", "Brass Boost", "Barn Dist" };
        return n;
    }

    // the slot's LEVEL default (A -6 dB, B -12 dB): at that value every model plays at its own right level
    explicit DriveSlotDSP (float slotDefaultLevelDb = -6.0f) : slotDefault (slotDefaultLevelDb) {}
    void setSlotDefaultLevel (float db) { slotDefault = db; }

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        porchD.prepare (sampleRate, maxBlock, numChannels);
        woolD.prepare (sampleRate, maxBlock, numChannels);
        hearthD.prepare (sampleRate, maxBlock, numChannels);
        brassD.prepare (sampleRate, maxBlock, numChannels);
        barnD.prepare (sampleRate, maxBlock, numChannels);
        lat = porchD.latency();
        jassert (woolD.latency() == lat && hearthD.latency() == lat && brassD.latency() == lat && barnD.latency() == lat);
        fadeOutLen = juce::jmax (1, (int) (sampleRate * 0.005));
        fadeInLen = juce::jmax (1, (int) (sampleRate * 0.010));
        current = want;
        state = steady;
        apply (current, true);
    }

    int latency() const { return lat; }

    void reset()
    {
        // a pending switch happens now (the slot was off, or it starts again from silence)
        current = want; state = steady;
        porchD.reset(); woolD.reset(); hearthD.reset(); brassD.reset(); barnD.reset();
        apply (current, true);
    }

    void setModel (int m, bool snap = false)
    {
        m = juce::jlimit (0, numModels - 1, m);
        if (snap)   // prepare / load / resting: at once (a model that does not change is not reset again)
        {
            if (m != current || state != steady) { current = m; state = steady; resetModel (current); }
            want = m;
            apply (current, true);
            return;
        }
        if (m == want) return;
        want = m;
        // changed again in the middle of a fade: carry on from the level the fade has reached (no jump)
        if (m == current && state == fadingOut)
        {
            state = fadingIn;
            pos = juce::jlimit (0, fadeInLen, (int) std::lround ((double) pos * fadeInLen / fadeOutLen));
            return;
        }
        if (state == steady) { state = fadingOut; pos = fadeOutLen; }
        else if (state == fadingIn) { state = fadingOut; pos = juce::jlimit (0, fadeOutLen, (int) std::lround ((double) pos * fadeOutLen / fadeInLen)); }
    }
    int model() const { return current; }

    // gain / tone 0..10, level dB (the slot's own range)
    void setParams (float gain, float tone, float levelDb, bool snap = false)
    {
        g = gain; t = tone; l = levelDb;
        apply (current, snap);
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        int done = 0;
        while (done < numSamples)
        {
            if (state == silentSwitch)   // the old model has faded out: change now, from rest, and fade in
            {
                current = want;
                resetModel (current);
                apply (current, true);
                state = fadingIn; pos = 0;
            }
            // this part: up to where a fade-out ends (the rest of the block then goes to the new model)
            int len = numSamples - done;
            if (state == fadingOut) len = juce::jmin (len, pos + 1);
            float* part[2] { data[0] + done, numChannels > 1 ? data[1] + done : nullptr };
            run (current, part, numChannels, len);
            if (state != steady)
                for (int i = 0; i < len; ++i)
                {
                    float k = 1.0f;
                    if (state == fadingOut)       { k = (float) pos / (float) fadeOutLen; if (pos > 0) --pos; else state = silentSwitch; }
                    else if (state == fadingIn)   { k = (float) pos / (float) fadeInLen; if (++pos >= fadeInLen) state = steady; }
                    k = k * k * (3.0f - 2.0f * k);   // S-curve
                    for (int c = 0; c < numChannels; ++c) part[c][i] *= k;
                }
            done += len;
        }
    }

private:
    float levelFor (int m) const
    {
        const float home = m == porch ? -6.0f : (m == wool ? -12.0f : 0.0f);
        return l + (home - slotDefault);   // A's PORCH / B's WOOL get exactly the old value
    }

    void apply (int m, bool snap)
    {
        const float lv = levelFor (m);
        switch (m)
        {
            case porch:  porchD.setParams (g, t, lv, snap); break;
            case wool:   woolD.setParams (g, t, lv, snap); break;
            case hearth: hearthD.setParams (g, t, lv, snap); break;
            case brass:  brassD.setParams (g, t, lv, snap); break;
            default:     barnD.setParams (g, t, lv, snap); break;
        }
    }

    void resetModel (int m)
    {
        switch (m)
        {
            case porch:  porchD.reset(); break;
            case wool:   woolD.reset(); break;
            case hearth: hearthD.reset(); break;
            case brass:  brassD.reset(); break;
            default:     barnD.reset(); break;
        }
    }

    void run (int m, float* const* data, int numChannels, int numSamples)
    {
        switch (m)
        {
            case porch:  porchD.process (data, numChannels, numSamples); break;
            case wool:   woolD.process (data, numChannels, numSamples); break;
            case hearth: hearthD.process (data, numChannels, numSamples); break;
            case brass:  brassD.process (data, numChannels, numSamples); break;
            default:     barnD.process (data, numChannels, numSamples); break;
        }
    }

    PorchDriveDSP porchD;
    WoolFuzzDSP woolD;
    HearthDriveDSP hearthD;
    BrassBoostDSP brassD;
    BarnDistDSP barnD;
    float slotDefault = -6.0f, g = 5.0f, t = 5.0f, l = -6.0f;
    int current = 0, want = 0, lat = 0, pos = 0, fadeOutLen = 240, fadeInLen = 480;
    enum State { steady, fadingOut, silentSwitch, fadingIn };
    State state = steady;
};
} // namespace pedal
