// Hybrid Kit — 中のサンプルレート（キットの録音 44.1 kHz）→ ホストのサンプルレートへの変換（JUCE なし）
//
// 1 本の窓付き sinc（片側 32 タップ・カイザー β 7.86 ≒ 80 dB）を、512 位相の表と直線補間で使う。
// 位置は整数の比（44100 : 48000 → 147 : 160）で数えるので、何時間鳴らしてもずれない。
// 20 kHz までの音の写り込み（44.1 − f）は 80 dB 下がる。遅れは H 個ぶん（48 kHz で 35 サンプル ≒ 0.73 ms、ホストに申告する）
// 同じサンプルレートのときは使わない（遅れゼロ）
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace hk
{
class StreamResampler
{
public:
    static constexpr int H = 32;
    static constexpr int P = 512;
    static constexpr int ringSize = 1 << 15;

    void setup (double inRate, double outRate)
    {
        bypass = std::abs (inRate - outRate) < 0.5;
        inR = std::max<int64_t> (1, (int64_t) std::llround (inRate));
        outR = std::max<int64_t> (1, (int64_t) std::llround (outRate));
        const int64_t g = std::gcd (inR, outR);
        inR /= g; outR /= g;
        const double fc = 0.5 * std::min (1.0, outRate / inRate) * (outRate < inRate ? 0.95 : 1.0);   // 入力 1 サンプルあたりのサイクル
        const double beta = 7.86, i0b = besselI0 (beta);
        table.assign ((size_t) (P + 1) * 2 * H, 0.0f);
        for (int p = 0; p <= P; ++p)
            for (int k = 0; k < 2 * H; ++k)
            {
                const double t = (double) (k - H + 1) - (double) p / P;
                const double x = 2.0 * fc * t;
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (3.141592653589793 * x) / (3.141592653589793 * x);
                const double w = t / (double) H;
                const double win = std::abs (w) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - w * w)) / i0b;
                table[(size_t) p * 2 * H + (size_t) k] = (float) (2.0 * fc * sinc * win);
            }
        ringL.assign (ringSize, 0.0f); ringR.assign (ringSize, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (ringL.begin(), ringL.end(), 0.0f);
        std::fill (ringR.begin(), ringR.end(), 0.0f);
        written = 0; outPos = 0;
    }

    bool isBypass() const { return bypass; }
    int latencyOut() const { return bypass ? 0 : (int) std::ceil ((double) H * (double) outR / (double) inR); }
    int64_t inputWritten() const { return written; }
    int64_t outputPos() const { return outPos; }
    // 出さない出口（Logic がつないでいない）も、ほかの出口と同じ位置に進めておく（計算はしない）
    void skip (int n) { outPos += n; }

    // ホストの出力 j 番目に当たるイベントを、中の何番目で鳴らすか（遅れ H の分を足す）
    int64_t eventIndexFor (int64_t hostIndex) const { return (hostIndex * inR) / outR + H; }

    // nOut 個出すのに必要な、中のサンプルの終わり（この番号の手前まで）
    int64_t needInputUpTo (int nOut) const
    {
        const int64_t jLast = outPos + std::max (0, nOut - 1);
        return (jLast * inR) / outR + H + 1;
    }

    void push (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const size_t w = (size_t) ((written + i) & (ringSize - 1));
            ringL[w] = l[i]; ringR[w] = r[i];
        }
        written += n;
    }

    void produce (float* outL, float* outRt, int n)
    {
        const int64_t mask = ringSize - 1;
        for (int j = 0; j < n; ++j)
        {
            const int64_t num = (outPos + j) * inR;
            const int64_t i0 = num / outR;
            const double frac = (double) (num % outR) / (double) outR;
            const double ph = frac * P;
            const int p = std::min (P - 1, (int) ph);
            const float a = (float) (ph - p);
            const float* c0 = table.data() + (size_t) p * 2 * H;
            const float* c1 = c0 + 2 * H;
            float sl = 0, sr = 0;
            const int64_t base = i0 - H + 1;
            for (int k = 0; k < 2 * H; ++k)
            {
                const float c = c0[k] + a * (c1[k] - c0[k]);
                const size_t idx = (size_t) ((base + k) & mask);
                sl += ringL[idx] * c; sr += ringR[idx] * c;
            }
            outL[j] = sl; outRt[j] = sr;
        }
        outPos += n;
    }

private:
    bool bypass = true;
    int64_t inR = 1, outR = 1;
    std::vector<float> table, ringL, ringR;
    int64_t written = 0, outPos = 0;

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-12) break;
        }
        return sum;
    }
};
} // namespace hk
