#pragma once
// PUNCH 表示用: 低・中・高それぞれの「迫力」（直近 約3秒のピーク ÷ 平均の音量 = クレスト、dB）。
// キックやスネアのアタックが立っているほど大きく、コンプやリミッターで潰すほど小さくなる（JUCE 非依存）
#include "ListenSim.h"
#include <atomic>
#include <array>

namespace refdsp
{

class PunchMeter
{
public:
    static constexpr int kBands = 3;          // 0 LOW 〜200Hz / 1 MID 200Hz〜4kHz / 2 HIGH 4kHz〜
    static constexpr int kSegments = 6;       // 0.5 秒 × 6 = 3 秒

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        enum { lp, hp };
        const double q = 0.70710678;
        for (int s = 0; s < 2; ++s)          // 2 段重ねて 24dB/oct
        {
            lowLp[s]  = designBiquad (lp, fs, 200.0, q, 0.0);
            midHp[s]  = designBiquad (hp, fs, 200.0, q, 0.0);
            midLp[s]  = designBiquad (lp, fs, 4000.0, q, 0.0);
            highHp[s] = designBiquad (hp, fs, 4000.0, q, 0.0);
        }
        segLen = (int) (fs * 0.5);
        clear();
    }

    void requestReset() noexcept { resetFlag = true; }

    void process (const float* L, const float* R, int n) noexcept
    {
        if (resetFlag.exchange (false)) clear();
        for (int i = 0; i < n; ++i)
        {
            const double x = 0.5 * ((double) L[i] + (double) R[i]);
            double y[kBands] = { x, x, x };
            for (int s = 0; s < 2; ++s)
            {
                y[0] = lowLp[s].process (y[0]);
                y[1] = midLp[s].process (midHp[s].process (y[1]));
                y[2] = highHp[s].process (y[2]);
            }
            for (int b = 0; b < kBands; ++b)
            {
                const double a = std::abs (y[b]);
                if (a > curPeak[b]) curPeak[b] = a;
                curSq[b] += y[b] * y[b];
            }
            if (++curCount >= segLen) finishSegment();
        }
    }

    // UI 用。測れないとき（音がほとんどない帯域）は -1
    std::array<std::atomic<float>, kBands> crestDb { { -1.0f, -1.0f, -1.0f } };

private:
    void clear() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            curPeak[b] = curSq[b] = 0.0;
            for (int s = 0; s < kSegments; ++s) segPeak[b][s] = segMs[b][s] = 0.0;
            for (int s = 0; s < 2; ++s) { lowLp[s].reset(); midHp[s].reset(); midLp[s].reset(); highHp[s].reset(); }
            crestDb[(size_t) b] = -1.0f;
        }
        curCount = 0; segPos = 0; segFilled = 0;
    }

    void finishSegment() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            segPeak[b][segPos] = curPeak[b];
            segMs[b][segPos] = curSq[b] / (double) curCount;
            curPeak[b] = curSq[b] = 0.0;
        }
        curCount = 0;
        segPos = (segPos + 1) % kSegments;
        segFilled = std::min (kSegments, segFilled + 1);

        double ms[kBands] {}, pk[kBands] {}, loudest = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            for (int s = 0; s < segFilled; ++s) { ms[b] += segMs[b][s]; pk[b] = std::max (pk[b], segPeak[b][s]); }
            ms[b] /= segFilled;
            loudest = std::max (loudest, ms[b]);
        }
        for (int b = 0; b < kBands; ++b)
        {
            // 無音（-70dBFS 未満）や、いちばん大きい帯域より 50dB 以上小さい帯域は測らない
            const bool ok = segFilled >= 2 && ms[b] > 1.0e-7 && ms[b] > loudest * 1.0e-5;
            crestDb[(size_t) b] = ok ? (float) (20.0 * std::log10 (pk[b] / std::sqrt (ms[b]))) : -1.0f;
        }
    }

    double fs = 48000.0;
    Biquad lowLp[2], midHp[2], midLp[2], highHp[2];
    double curPeak[kBands] {}, curSq[kBands] {};
    double segPeak[kBands][kSegments] {}, segMs[kBands][kSegments] {};
    int segLen = 24000, curCount = 0, segPos = 0, segFilled = 0;
    std::atomic<bool> resetFlag { false };
};

} // namespace refdsp
