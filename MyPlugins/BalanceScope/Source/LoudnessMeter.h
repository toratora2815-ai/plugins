#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

// ラウドネスメーター（ITU-R BS.1770 / EBU R128 の決まりどおり。JUCE を使わない素の C++）
//   MOMENTARY  … 直近 0.4 秒
//   SHORT-TERM … 直近 3 秒
//   INTEGRATED … 測り始めてからの全体（-70 LUFS の絶対ゲートと、-10 LU の相対ゲート）
//   PEAK       … サンプルの最大値（dBFS）
//   PLR        … PEAK − INTEGRATED（ピークとラウドネスの差。大きいほど抑揚がある）
class LoudnessMeter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // K 特性：高域シェルフ ＋ 低域カット（係数は 48 kHz 以外のときのために式で求める）
        {
            const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan (M_PI * f0 / sr), vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                      2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan (M_PI * f0 / sr);
            const double a0 = 1.0 + k / q + k * k;
            highPass = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        subLen = std::max (1, (int) std::lround (sr * 0.1));
        reset();
    }

    void reset()
    {
        for (auto& s : state) s = {};
        subSum = 0.0; subCount = 0;
        for (auto& v : subs) v = 0.0;
        subPos = 0; subsFilled = 0;
        for (auto& v : histCount) v = 0;
        for (auto& v : histEnergy) v = 0.0;
        peak = 0.0f;
        momentary.store (-200.0f); shortTerm.store (-200.0f); integrated.store (-200.0f); peakDb.store (-200.0f);
    }

    // accumulate = false のときは MOMENTARY / SHORT-TERM だけ動かす（INTEGRATED と PEAK は止める）
    void process (const float* const* ch, int numCh, int numSamples, bool accumulate)
    {
        numCh = std::clamp (numCh, 0, maxChannels);
        if (numCh == 0) return;

        for (int i = 0; i < numSamples; ++i)
        {
            double e = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                if (accumulate) peak = std::max (peak, std::abs (x));
                const double y = state[c].run (state[c].run1 (x, shelf), highPass);
                e += y * y;   // 左右の重みはどちらも 1.0
            }
            subSum += e;

            if (++subCount >= subLen)
            {
                pushSub (subSum / subCount, accumulate);
                subSum = 0.0; subCount = 0;
            }
        }
        if (accumulate) peakDb.store (peak > 0.0f ? 20.0f * std::log10 (peak) : -200.0f);
    }

    float getMomentary() const  { return momentary.load (std::memory_order_relaxed); }
    float getShortTerm() const  { return shortTerm.load (std::memory_order_relaxed); }
    float getIntegrated() const { return integrated.load (std::memory_order_relaxed); }
    float getPeak() const       { return peakDb.load (std::memory_order_relaxed); }

    static float toLufs (double meanSquare) { return meanSquare > 1.0e-20 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -200.0f; }

private:
    struct Coefs { double b0, b1, b2, a1, a2; };
    struct State
    {
        double s1a = 0, s2a = 0, s1b = 0, s2b = 0;
        double run1 (double x, const Coefs& c)
        {
            const double y = c.b0 * x + s1a;
            s1a = c.b1 * x - c.a1 * y + s2a;
            s2a = c.b2 * x - c.a2 * y;
            return y;
        }
        double run (double x, const Coefs& c)
        {
            const double y = c.b0 * x + s1b;
            s1b = c.b1 * x - c.a1 * y + s2b;
            s2b = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    // 100 ms ごとの小さなかたまり。400 ms（4 個）で 1 ブロック（75% 重ね）、3 秒（30 個）で SHORT-TERM
    void pushSub (double meanSquare, bool accumulate)
    {
        subs[subPos] = meanSquare;
        subPos = (subPos + 1) % numSubs;
        subsFilled = std::min (subsFilled + 1, numSubs);

        auto meanOf = [this] (int count)
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k) s += subs[(subPos - k + numSubs) % numSubs];
            return s / count;
        };
        const double block = subsFilled >= 4 ? meanOf (4) : 0.0;
        momentary.store (subsFilled >= 4 ? toLufs (block) : -200.0f, std::memory_order_relaxed);
        shortTerm.store (subsFilled >= numSubs ? toLufs (meanOf (numSubs)) : -200.0f, std::memory_order_relaxed);

        if (accumulate && subsFilled >= 4)
        {
            const float l = toLufs (block);
            if (l > absGate)
            {
                const int b = std::clamp ((int) ((l - absGate) / histStep), 0, histBins - 1);
                ++histCount[b];
                histEnergy[b] += block;
                updateIntegrated();
            }
        }
    }

    void updateIntegrated()
    {
        long n = 0;
        double e = 0.0;
        for (int b = 0; b < histBins; ++b) { n += histCount[b]; e += histEnergy[b]; }
        if (n == 0) return;
        const float relGate = toLufs (e / (double) n) - 10.0f;

        n = 0; e = 0.0;
        for (int b = 0; b < histBins; ++b)
        {
            const float binTop = absGate + histStep * (float) (b + 1);
            if (binTop <= relGate) continue;   // 0.1 LU の細かさで相対ゲートをかける
            n += histCount[b];
            e += histEnergy[b];
        }
        integrated.store (n > 0 ? toLufs (e / (double) n) : -200.0f, std::memory_order_relaxed);
    }

    static constexpr int numSubs = 30, histBins = 800;
    static constexpr float absGate = -70.0f, histStep = 0.1f;   // -70〜+10 LUFS を 0.1 刻み

    double sr = 48000.0;
    Coefs shelf {}, highPass {};
    State state[maxChannels];
    int subLen = 4800, subCount = 0, subPos = 0, subsFilled = 0;
    double subSum = 0.0;
    double subs[numSubs] {};
    long histCount[histBins] {};
    double histEnergy[histBins] {};
    float peak = 0.0f;

    std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f }, integrated { -200.0f }, peakDb { -200.0f };
};
