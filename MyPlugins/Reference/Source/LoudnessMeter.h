#pragma once
// ITU-R BS.1770 / EBU R128 に沿ったラウドネス計測（JUCE 非依存）
// オーディオスレッドで process() を呼び、結果は atomic で UI から読む
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace refdsp
{

constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

inline float energyToLufs (double e) noexcept
{
    return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -100.0f;
}

inline float toDb (double a) noexcept
{
    return a > 1.0e-6 ? (float) (20.0 * std::log10 (a)) : -120.0f;
}

class LoudnessMeter
{
public:
    static constexpr int kShortBlocks = 30;          // 100ms × 30 = 3秒
    static constexpr float kHistMin = -70.0f, kHistMax = 10.0f;
    static constexpr int kHistBins = 800;            // 0.1 LU 刻み

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        blockLen = std::max (1, (int) std::lround (fs * 0.1));
        designKWeighting();
        doReset();
    }

    void requestReset() noexcept { resetFlag.store (true); }

    // R は L と同じポインタでもよい（モノラル）
    void process (const float* L, const float* R, int n) noexcept
    {
        if (resetFlag.exchange (false))
            doReset();

        const bool mono = (L == R);
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = R[i];
            const double kl = hp[0].process (shelf[0].process (l));
            accE += kl * kl;
            if (! mono)
            {
                const double kr = hp[1].process (shelf[1].process (r));
                accE += kr * kr;
            }
            else
            {
                accE += kl * kl;   // モノラルは左右に同じ信号があるものとして扱う
            }
            accLL += l * l;
            accRR += r * r;
            accLR += l * r;
            blockPeak = std::max (blockPeak, std::max (std::abs (l), std::abs (r)));

            if (++count >= blockLen)
                finishBlock();
        }
    }

    // ---- 結果（どのスレッドからでも読める）----
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f };
    std::atomic<float> peakShort { -120.0f }, peakMax { -120.0f };
    std::atomic<float> correlation { 1.0f }, sideRatio { 0.0f };
    std::atomic<float> gatedSeconds { 0.0f };
    std::atomic<bool>  signalPresent { false };       // 直近3秒に音があるか

private:
    void designKWeighting()
    {
        // 係数はサンプルレートに合わせて計算（BS.1770 の 48kHz 係数と一致する式）
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& s : shelf)
            {
                s.b0 = (Vh + Vb * K / Q + K * K) / a0;
                s.b1 = 2.0 * (K * K - Vh) / a0;
                s.b2 = (Vh - Vb * K / Q + K * K) / a0;
                s.a1 = 2.0 * (K * K - 1.0) / a0;
                s.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& s : hp)
            {
                s.b0 = 1.0; s.b1 = -2.0; s.b2 = 1.0;
                s.a1 = 2.0 * (K * K - 1.0) / a0;
                s.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
    }

    void doReset() noexcept
    {
        for (auto& s : shelf) s.reset();
        for (auto& s : hp) s.reset();
        accE = accLL = accRR = accLR = blockPeak = 0.0;
        count = 0;
        numBlocks = 0;
        ringPos = 0;
        energies.fill (0.0); peaks.fill (0.0); ll.fill (0.0); rr.fill (0.0); lr.fill (0.0);
        histCount.fill (0); histEnergy.fill (0.0);
        maxPeak = 0.0;
        momentary = shortTerm = integrated = -100.0f;
        peakShort = peakMax = -120.0f;
        correlation = 1.0f; sideRatio = 0.0f;
        gatedSeconds = 0.0f;
        signalPresent = false;
    }

    void finishBlock() noexcept
    {
        const double e = accE / count;
        energies[(size_t) ringPos] = e;
        peaks[(size_t) ringPos]    = blockPeak;
        ll[(size_t) ringPos] = accLL; rr[(size_t) ringPos] = accRR; lr[(size_t) ringPos] = accLR;
        ringPos = (ringPos + 1) % kShortBlocks;
        ++numBlocks;
        maxPeak = std::max (maxPeak, blockPeak);
        accE = accLL = accRR = accLR = blockPeak = 0.0;
        count = 0;

        auto meanOfLast = [this] (int k)
        {
            k = std::min (k, (int) std::min<long long> (numBlocks, kShortBlocks));
            double s = 0.0;
            for (int j = 1; j <= k; ++j)
                s += energies[(size_t) ((ringPos - j + kShortBlocks) % kShortBlocks)];
            return k > 0 ? s / k : 0.0;
        };

        const double m = meanOfLast (4);
        momentary = energyToLufs (m);
        shortTerm = energyToLufs (meanOfLast (kShortBlocks));

        double pk = 0.0, sLL = 0.0, sRR = 0.0, sLR = 0.0;
        for (int j = 0; j < kShortBlocks; ++j)
        {
            pk = std::max (pk, peaks[(size_t) j]);
            sLL += ll[(size_t) j]; sRR += rr[(size_t) j]; sLR += lr[(size_t) j];
        }
        peakShort = toDb (pk);
        peakMax   = toDb (maxPeak);
        signalPresent = shortTerm.load() > -60.0f;

        if (sLL + sRR > 1.0e-6)
        {
            correlation = (float) (sLR / std::sqrt (sLL * sRR + 1.0e-20));
            const double mid  = (sLL + sRR + 2.0 * sLR) * 0.25;
            const double side = (sLL + sRR - 2.0 * sLR) * 0.25;
            sideRatio = (float) (side / (mid + side + 1.0e-20));
        }

        // 積算ラウドネス（400msブロックを100msごとに、ゲーティング付き）
        if (numBlocks >= 4)
        {
            const float lufs = energyToLufs (m);
            if (lufs > kHistMin)
            {
                const int bin = std::clamp ((int) ((lufs - kHistMin) * 10.0f), 0, kHistBins - 1);
                ++histCount[(size_t) bin];
                histEnergy[(size_t) bin] += m;
                updateIntegrated();
            }
        }
    }

    void updateIntegrated() noexcept
    {
        long long n = 0; double e = 0.0;
        for (int b = 0; b < kHistBins; ++b) { n += histCount[(size_t) b]; e += histEnergy[(size_t) b]; }
        if (n == 0) return;
        const float relGate = energyToLufs (e / (double) n) - 10.0f;
        const int firstBin = std::clamp ((int) std::ceil ((relGate - kHistMin) * 10.0f), 0, kHistBins);
        long long n2 = 0; double e2 = 0.0;
        for (int b = firstBin; b < kHistBins; ++b) { n2 += histCount[(size_t) b]; e2 += histEnergy[(size_t) b]; }
        if (n2 > 0)
        {
            integrated = energyToLufs (e2 / (double) n2);
            gatedSeconds = (float) n2 * 0.1f;
        }
    }

    double fs = 48000.0;
    int blockLen = 4800, count = 0, ringPos = 0;
    long long numBlocks = 0;
    std::array<Biquad, 2> shelf, hp;
    double accE = 0, accLL = 0, accRR = 0, accLR = 0, blockPeak = 0, maxPeak = 0;
    std::array<double, kShortBlocks> energies {}, peaks {}, ll {}, rr {}, lr {};
    std::array<long long, kHistBins> histCount {};
    std::array<double, kHistBins> histEnergy {};
    std::atomic<bool> resetFlag { false };
};

// ストリーミングの音量正規化をまねたゲイン（dB）
// 目標より大きい曲は下げる。小さい曲はピークが -1dBFS を超えない範囲でだけ上げる
inline float streamingGainDb (float lufs, float peakDb, float targetLufs = -14.0f) noexcept
{
    float g = targetLufs - lufs;
    if (g > 0.0f) g = std::min (g, std::max (0.0f, -1.0f - peakDb));
    return g;
}

// 比較に使うラウドネス。十分たまっていれば積算値、なければ3秒値。不明なら -200
inline float comparisonLoudness (const LoudnessMeter& m) noexcept
{
    if (m.gatedSeconds.load() >= 3.0f) return m.integrated.load();
    if (m.signalPresent.load())        return m.shortTerm.load();
    return -200.0f;
}

} // namespace refdsp
