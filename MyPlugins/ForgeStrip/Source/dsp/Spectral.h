// Forge Strip: Sculptor（音色を楽器ごとの目標へ寄せる）と Unmask（サイドチェーンとぶつかる所を下げる）
// どちらも 1/3 オクターブ 30 帯域で量を決め、STFT（1024 点、1/4 ずつ）でかける。遅れ = 1024 サンプル（48 kHz）
#pragma once
#include "Common.h"
#include "ToneTargets.h"

namespace fs
{
struct FFT
{
    int n = 0; std::vector<std::complex<float>> tw; std::vector<int> rev;
    void prepare (int size)
    {
        n = size; tw.resize ((size_t) n / 2); rev.resize ((size_t) n);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
    }
    void run (std::complex<float>* a, bool inverse) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (a[i], a[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    auto w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v; a[i + j + len / 2] = u - v;
                }
        }
        if (inverse) { const float s = 1.0f / (float) n; for (int i = 0; i < n; ++i) a[i] *= s; }
    }
};

// 30 帯域の中心 25 Hz × 2^(k/3)
inline float toneBandHz (int k) { return 25.0f * std::pow (2.0f, (float) k / 3.0f); }

// STFT の枠組み（ステレオ、主信号＋任意のサイドチェーン）。派生先が bandGains を決める
class SpectralBase
{
public:
    virtual ~SpectralBase() = default;
    void prepare (double s)
    {
        sr = s;
        N = 1024; while (N < (int) std::round (1024.0 * s / 48000.0)) N <<= 1;
        hop = N / 4;
        fft.prepare (N);
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos ((float) (2.0 * kPi * i / N)));
        for (int c = 0; c < 2; ++c) { in[c].assign ((size_t) N, 0.0f); sc[c].assign ((size_t) N, 0.0f); acc[c].assign ((size_t) N * 2, 0.0f); }
        work.resize ((size_t) N); work2.resize ((size_t) N);
        pos = 0; outPos = 0;
        nb = N / 2 + 1;
        binGain.assign ((size_t) nb, 1.0f);
        // 帯域 → ビンの対応（隣の帯域の中心との間を対数で線形補間）
        binLo.resize ((size_t) nb); binT.resize ((size_t) nb);
        for (int k = 0; k < nb; ++k)
        {
            const float f = std::max (1.0f, (float) k * (float) sr / (float) N);
            const float pk = 3.0f * std::log2 (f / 25.0f);
            const float c = clampv (pk, 0.0f, (float) kToneBands - 1.001f);
            binLo[(size_t) k] = (int) std::floor (c); binT[(size_t) k] = c - std::floor (c);
        }
        bandPow.assign (kToneBands, 0.0f); scPow.assign (kToneBands, 0.0f);
        gainsDb.assign (kToneBands, 0.0f);
        onPrepare();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill (in[c].begin(), in[c].end(), 0.0f); std::fill (sc[c].begin(), sc[c].end(), 0.0f); std::fill (acc[c].begin(), acc[c].end(), 0.0f); }
        pos = 0; outPos = 0; std::fill (gainsDb.begin(), gainsDb.end(), 0.0f); onReset();
    }
    int latency() const { return N; }

    // scL/scR は nullptr でもよい
    void process (float* L, float* R, int n, const float* scL, const float* scR)
    {
        float* io[2] = { L, R };
        const float* sci[2] = { scL, scR };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                in[c][(size_t) pos] = io[c][i];
                sc[c][(size_t) pos] = sci[c] != nullptr ? sci[c][i] : 0.0f;
                io[c][i] = acc[c][(size_t) outPos];
                acc[c][(size_t) outPos] = 0.0f;
            }
            if (++outPos >= N * 2) outPos = 0;
            if (++pos >= N) pos = 0;
            if (++count >= hop) { count = 0; frame (sci[0] != nullptr); }
        }
    }
    const std::vector<float>& currentGainsDb() const { return gainsDb; }

protected:
    virtual void onPrepare() {}
    virtual void onReset() {}
    // bandPow（主信号）・scPow（サイドチェーン）は 30 帯域のパワー（dB）。gainsDb を決める
    virtual void decide (bool hasSc) = 0;

    double sr = 48000; int N = 1024, hop = 256, nb = 513;
    std::vector<float> bandPow, scPow, gainsDb;
    float framesPerSec() const { return (float) (sr / hop); }

private:
    void analyse (std::vector<float>* src, std::vector<float>& outBands, bool keepSpectrum)
    {
        std::vector<double> acc2 (kToneBands, 0.0);
        std::vector<double> cnt (kToneBands, 0.0);
        for (int c = 0; c < 2; ++c)
        {
            auto* w = c == 0 ? work.data() : work2.data();
            for (int i = 0; i < N; ++i)
            {
                const int idx = (pos + i) % N;   // 古い順
                w[i] = { src[c][(size_t) idx] * win[(size_t) i], 0.0f };
            }
            fft.run (w, false);
            for (int k = 1; k < nb; ++k)
            {
                const float p = std::norm (w[k]);
                const int b = binLo[(size_t) k]; const float t = binT[(size_t) k];
                acc2[(size_t) b] += p * (1.0f - t); cnt[(size_t) b] += (1.0f - t);
                acc2[(size_t) b + 1] += p * t; cnt[(size_t) b + 1] += t;
            }
        }
        (void) keepSpectrum;
        // 帯域の幅で割らず「帯域内の合計」= 1/3 オクターブのパワー。ビンが少ない低域は平均 × 帯域の幅（ビン数）
        // ビンが無い低い帯域は、上でいちばん近い帯域の密度を使う
        for (int b = kToneBands - 2; b >= 0; --b)
            if (cnt[(size_t) b] < 0.25) { acc2[(size_t) b] = acc2[(size_t) b + 1]; cnt[(size_t) b] = cnt[(size_t) b + 1]; }
        for (int b = 0; b < kToneBands; ++b)
        {
            const float lo = toneBandHz (b) * std::pow (2.0f, -1.0f / 6.0f), hi = toneBandHz (b) * std::pow (2.0f, 1.0f / 6.0f);
            const double widthBins = (hi - lo) * N / sr;
            const double mean = cnt[(size_t) b] > 0 ? acc2[(size_t) b] / cnt[(size_t) b] : 0.0;
            outBands[(size_t) b] = (float) (10.0 * std::log10 (mean * widthBins + 1e-20) - 20.0 * std::log10 (N * 0.5));
        }
    }

    void frame (bool hasSc)
    {
        analyse (sc, scPow, false);
        analyse (in, bandPow, true);   // work / work2 に主信号のスペクトルが残る
        decide (hasSc);
        // 帯域の量 → ビンの量
        for (int k = 0; k < nb; ++k)
        {
            const int b = binLo[(size_t) k]; const float t = binT[(size_t) k];
            binGain[(size_t) k] = dbToGain (gainsDb[(size_t) b] * (1.0f - t) + gainsDb[(size_t) b + 1] * t);
        }
        for (int c = 0; c < 2; ++c)
        {
            auto* w = c == 0 ? work.data() : work2.data();
            w[0] *= binGain[0];
            for (int k = 1; k < nb; ++k) { w[k] *= binGain[(size_t) k]; if (k < N / 2) w[N - k] = std::conj (w[k]); }
            fft.run (w, true);
            // 重ね合わせ（sqrt-Hann × sqrt-Hann、1/4 ずつで合計 2 → 0.5 倍）
            for (int i = 0; i < N; ++i)
            {
                const int o = (outPos + i) % (N * 2);
                acc[c][(size_t) o] += w[i].real() * win[(size_t) i] * 0.5f;
            }
        }
    }

    FFT fft;
    std::vector<float> win, binGain, binT;
    std::vector<int> binLo;
    std::vector<float> in[2], sc[2], acc[2];
    std::vector<std::complex<float>> work, work2;
    int pos = 0, outPos = 0, count = 0;
};

// ---------------------------------------------------------------------
struct SculptorSettings
{
    int target = 22;          // 0..22 = 楽器、23 = なし
    float amount = 0;         // 0..100
    float tone = 0;           // -50..50（目標を傾ける）
    float speed = 50;         // 0..100
    float lowHz = 20, highHz = 20000;   // 効かせる範囲
};

class SculptorModule : public SpectralBase
{
public:
    void set (const SculptorSettings& s) { cfg = s; }
protected:
    void onPrepare() override { smooth.assign (kToneBands, -200.0f); gSm.assign (kToneBands, 0.0f); }
    void onReset() override { std::fill (smooth.begin(), smooth.end(), -200.0f); std::fill (gSm.begin(), gSm.end(), 0.0f); }
    void decide (bool) override
    {
        // 速さ: SPEED 0 → 約 2 s、50 → 約 300 ms、100 → 約 40 ms でならす
        const float tauMs = 2000.0f * std::pow (0.02f, cfg.speed / 100.0f);
        const float a = std::exp (-1000.0f / (tauMs * framesPerSec()));
        float total = -200;
        for (int b = 0; b < kToneBands; ++b)
        {
            if (smooth[(size_t) b] < -150.0f) smooth[(size_t) b] = bandPow[(size_t) b];
            smooth[(size_t) b] = bandPow[(size_t) b] + a * (smooth[(size_t) b] - bandPow[(size_t) b]);
            total = std::max (total, smooth[(size_t) b]);
        }
        const bool active = cfg.target >= 0 && cfg.target < kNumToneTargets && cfg.amount > 0.0f && total > -75.0f;
        std::vector<float> want (kToneBands, 0.0f);
        if (active)
        {
            const auto& T = toneTarget (cfg.target);
            // 入力の形（100 Hz〜8 kHz の平均を 0 に）
            double mIn = 0, mT = 0; int cnt = 0;
            for (int b = 4; b <= 25; ++b) { mIn += smooth[(size_t) b]; mT += T[b]; ++cnt; }
            mIn /= cnt; mT /= cnt;
            const float amt = std::pow (cfg.amount / 100.0f, 1.15f);
            for (int b = 0; b < kToneBands; ++b)
            {
                const float f = toneBandHz (b);
                const float tilt = (cfg.tone / 50.0f) * 0.55f * std::log2 (f / 1000.0f);
                float d = (T[b] - (float) mT + tilt) - (smooth[(size_t) b] - (float) mIn);
                if (smooth[(size_t) b] < total - 70.0f) d = std::min (d, 0.0f);   // ほぼ無い帯域は持ち上げない
                if (f < cfg.lowHz || f > cfg.highHz) d = 0.0f;
                want[(size_t) b] = clampv (d * amt, -30.0f, 30.0f);
            }
            // 全体のパワーは変えない（測定: 基準は出力のパワー平均が入力とほぼ同じ）
            double pin = 0, pout = 0;
            for (int b = 0; b < kToneBands; ++b) { const double pw = std::pow (10.0, smooth[(size_t) b] / 10.0); pin += pw; pout += pw * std::pow (10.0, want[(size_t) b] / 10.0); }
            const float off = pin > 0 ? (float) (10.0 * std::log10 (pout / pin)) : 0.0f;
            for (auto& w : want) w -= off;
        }
        // 量そのものも少しならす（ザラつき防止）
        const float ga = std::exp (-1000.0f / (60.0f * framesPerSec()));
        for (int b = 0; b < kToneBands; ++b)
        {
            gSm[(size_t) b] = want[(size_t) b] + ga * (gSm[(size_t) b] - want[(size_t) b]);
            gainsDb[(size_t) b] = gSm[(size_t) b];
        }
    }
private:
    SculptorSettings cfg;
    std::vector<float> smooth, gSm;
};

// ---------------------------------------------------------------------
struct UnmaskSettings
{
    float amount = 0;        // 0..100
    float sensitivity = 50;  // 0..100
    float attack = 16, release = 100;
    float lowHz = 20, highHz = 20000;
};

namespace unmaskTune
{
    // 周波数ごとの効きやすさ（低中域ほど強い。測定の形）と感度の換算。tools/fs_fit で合わせる
    struct T { float sensOffset = 3.0f, sensPerPct = 0.6f, slope = 0.7f, maxCut = 30.0f; };
    inline T& t() { static T v; return v; }
    // 帯域ごとの効きやすさ（基準をピンク雑音どうしで測った形、最大 = 1）
    inline float weightBand (int k)
    {
        static const float w[30] = { 0.136f, 0.136f, 0.144f, 0.226f, 0.259f, 0.333f, 0.424f, 0.568f, 0.687f, 0.872f, 1.000f, 0.934f, 0.860f, 0.749f, 0.650f, 0.539f, 0.453f, 0.383f, 0.296f, 0.210f, 0.119f, 0.086f, 0.066f, 0.049f, 0.033f, 0.029f, 0.053f, 0.053f, 0.053f, 0.053f };
        return w[clampv (k, 0, 29)];
    }
}

class UnmaskModule : public SpectralBase
{
public:
    void set (const UnmaskSettings& s) { cfg = s; }
protected:
    void onPrepare() override { mainS.assign (kToneBands, -200.0f); scS.assign (kToneBands, -200.0f); gSm.assign (kToneBands, 0.0f); }
    void onReset() override { std::fill (mainS.begin(), mainS.end(), -200.0f); std::fill (scS.begin(), scS.end(), -200.0f); std::fill (gSm.begin(), gSm.end(), 0.0f); }
    void decide (bool hasSc) override
    {
        const auto& tu = unmaskTune::t();
        const float fps = framesPerSec();
        const float ma = std::exp (-1000.0f / (150.0f * fps));
        for (int b = 0; b < kToneBands; ++b)
        {
            if (mainS[(size_t) b] < -150.0f) mainS[(size_t) b] = bandPow[(size_t) b];
            mainS[(size_t) b] = bandPow[(size_t) b] + ma * (mainS[(size_t) b] - bandPow[(size_t) b]);
            scS[(size_t) b] = scPow[(size_t) b];
        }
        std::vector<float> want (kToneBands, 0.0f);
        if (hasSc && cfg.amount > 0)
        {
            // サイドチェーンが主信号をどれだけ上回るか（隣の帯域にも少し広げる）
            const float sens = tu.sensOffset + tu.sensPerPct * (cfg.sensitivity - 50.0f);
            for (int b = 0; b < kToneBands; ++b)
            {
                float s = scS[(size_t) b];
                if (b > 0) s = std::max (s, scS[(size_t) b - 1] - 9.0f);
                if (b < kToneBands - 1) s = std::max (s, scS[(size_t) b + 1] - 9.0f);
                const float m = s - mainS[(size_t) b] + sens;
                const float f = toneBandHz (b);
                // なめらかに 0 から増える（softplus）
                float cut = tu.slope * std::log1p (std::exp (std::min (m, 40.0f) / 3.0f)) * 3.0f;
                cut *= unmaskTune::weightBand (b) * (cfg.amount / 100.0f);
                if (scS[(size_t) b] < -90.0f) cut = 0;
                if (f < cfg.lowHz || f > cfg.highHz) cut = 0;
                want[(size_t) b] = -std::min (cut, tu.maxCut);
            }
        }
        const float aA = std::exp (-1000.0f / (std::max (1.0f, cfg.attack) * fps));
        const float aR = std::exp (-1000.0f / (std::max (1.0f, cfg.release) * fps));
        for (int b = 0; b < kToneBands; ++b)
        {
            const float w = want[(size_t) b];
            const float k = w < gSm[(size_t) b] ? aA : aR;
            gSm[(size_t) b] = w + k * (gSm[(size_t) b] - w);
            gainsDb[(size_t) b] = gSm[(size_t) b];
        }
    }
private:
    UnmaskSettings cfg;
    std::vector<float> mainS, scS, gSm;
};
} // namespace fs
