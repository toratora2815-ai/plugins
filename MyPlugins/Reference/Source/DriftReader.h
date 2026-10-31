#pragma once
// 別のオーディオデバイス（BlackHole など）から来る音を、ホストのクロックに合わせて読み出す。
// ・StereoRing: 書き込み1スレッド／読み出し1スレッドのロックフリー・リングバッファ
// ・DriftReader: バッファの溜まり具合を見て、再生速度をごくわずかに補正（±0.2%以内）
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace refdsp
{

inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

class StereoRing
{
public:
    explicit StereoRing (int capacityPow2 = 1 << 16)
        : cap (capacityPow2), mask ((uint64_t) capacityPow2 - 1),
          l ((size_t) capacityPow2, 0.0f), r ((size_t) capacityPow2, 0.0f) {}

    // 書き込み側（デバイスのコールバック）。止まらない、上書きもいとわない
    void write (const float* L, const float* R, int n) noexcept
    {
        auto w = writeCount.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++w)
        {
            l[(size_t) (w & mask)] = L != nullptr ? L[i] : 0.0f;
            r[(size_t) (w & mask)] = R != nullptr ? R[i] : (L != nullptr ? L[i] : 0.0f);
        }
        writeCount.store (w, std::memory_order_release);
    }

    uint64_t written() const noexcept { return writeCount.load (std::memory_order_acquire); }
    float left  (uint64_t i) const noexcept { return l[(size_t) (i & mask)]; }
    float right (uint64_t i) const noexcept { return r[(size_t) (i & mask)]; }
    int capacity() const noexcept { return cap; }

private:
    const int cap;
    const uint64_t mask;
    std::vector<float> l, r;
    std::atomic<uint64_t> writeCount { 0 };
};

class DriftReader
{
public:
    // target: 目標の溜まりフレーム数（デバイス側のサンプル単位）
    void setTarget (int frames) noexcept { target = (double) frames; primed = false; }
    void restart() noexcept { primed = false; }

    // ratio = デバイスのサンプルレート / ホストのサンプルレート
    // 戻り値: 音を出せたら true（準備中・途切れは false で無音）
    bool read (const StereoRing& ring, float* outL, float* outR, int n,
               double ratio, double hostRate) noexcept
    {
        const uint64_t w = ring.written();

        if (! primed)
        {
            if ((double) w < target + 8.0)
            {
                std::fill (outL, outL + n, 0.0f);
                std::fill (outR, outR + n, 0.0f);
                return false;
            }
            readPos = (double) w - target;
            fillAvg = target;
            corr = 0.0;
            primed = true;
        }

        const double step = ratio * (1.0 + corr);
        const double needed = step * n + 4.0;
        double avail = (double) w - readPos;

        // 溢れそう（読み出しが遅れすぎ）→ 目標位置に飛ぶ
        if (avail > (double) ring.capacity() - needed - 1024.0)
        {
            readPos = (double) w - target;
            avail = target;
            ++resyncCount;
        }
        // 足りない（デバイスが止まった等）→ 無音にして溜まるまで待つ
        if (avail < needed)
        {
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
            primed = false;
            ++underrunCount;
            return false;
        }

        for (int i = 0; i < n; ++i)
        {
            const double fl = std::floor (readPos);
            const uint64_t i0 = (uint64_t) fl;
            const float t = (float) (readPos - fl);
            outL[i] = hermite (ring.left (i0 - 1), ring.left (i0), ring.left (i0 + 1), ring.left (i0 + 2), t);
            outR[i] = hermite (ring.right (i0 - 1), ring.right (i0), ring.right (i0 + 1), ring.right (i0 + 2), t);
            readPos += step;
        }

        // 溜まり具合を約3秒でならし、比例制御でゆっくり速度を補正（時定数 約30秒）
        // 補正はごくわずか（実際のクロックずれは 0.01〜0.03% 程度）なので音程の揺れは聴こえない
        const double fill = (double) ring.written() - readPos;
        const double a = std::min (1.0, (double) n / (hostRate * 3.0));
        fillAvg += a * (fill - fillAvg);
        const double devRate = ratio * hostRate;
        const double kp = 1.0 / (30.0 * devRate);
        corr = std::clamp (kp * (fillAvg - target), -0.002, 0.002);
        return true;
    }

    double getCorrection() const noexcept { return corr; }
    double getFillAverage() const noexcept { return fillAvg; }
    int underrunCount = 0, resyncCount = 0;

private:
    double target = 4096.0, readPos = 0.0, fillAvg = 4096.0, corr = 0.0;
    bool primed = false;
};

} // namespace refdsp
