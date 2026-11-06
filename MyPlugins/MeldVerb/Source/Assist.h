#pragma once
// Meld Verb — 自動で EQ を決める手伝い（JUCE なし）
//   LEARN（PRE EQ）… 入力の「耳に痛い響き（まわりより飛び出た帯域）」を探して、ベルで下げる。低いゴロゴロが多ければローカット
//   UNMASK（REVERB EQ）… 元の音と響きを比べて、響きが元の音を隠している帯域だけ響きを下げる
#include <cmath>
#include <algorithm>
#include <vector>

namespace assist
{
// 1/6 オクターブの帯域（31.5 Hz〜20 kHz）
struct Spectrum
{
    static constexpr int numBands = 57;
    static float centre (int b) { return 31.5f * std::pow (2.0f, (float) b / 6.0f); }
    float db[numBands] {};

    // パワースペクトル（bin 0..n-1、bin 幅 sr / fftSize）→ 帯域ごとの dB
    static void fromPower (const float* pow, int nBins, double sr, int fftSize, float* outDb)
    {
        const double binHz = sr / fftSize;
        for (int b = 0; b < numBands; ++b)
        {
            const double lo = centre (b) * std::pow (2.0, -1.0 / 12.0), hi = centre (b) * std::pow (2.0, 1.0 / 12.0);
            int i0 = std::max (1, (int) std::floor (lo / binHz)), i1 = std::min (nBins - 1, (int) std::ceil (hi / binHz));
            double e = 0; int n = 0;
            for (int i = i0; i <= i1; ++i) { e += pow[i]; ++n; }
            outDb[b] = (float) (10.0 * std::log10 (e / std::max (1, n) + 1e-20));
        }
    }
};

// LEARN 用: パワーをためて平均する
struct Accumulator
{
    double sum[Spectrum::numBands] {}; int frames = 0;
    void clear() { std::fill (std::begin (sum), std::end (sum), 0.0); frames = 0; }
    void add (const float* db) { for (int b = 0; b < Spectrum::numBands; ++b) sum[b] += std::pow (10.0, db[b] / 10.0); ++frames; }
    void mean (float* out) const { for (int b = 0; b < Spectrum::numBands; ++b) out[b] = (float) (10.0 * std::log10 (sum[b] / std::max (1, frames) + 1e-20)); }
};

// ならす（帯域の数で左右に広げた平均）
inline void smoothBands (const float* in, float* out, int radius)
{
    for (int b = 0; b < Spectrum::numBands; ++b)
    {
        double e = 0; int n = 0;
        for (int k = std::max (0, b - radius); k <= std::min (Spectrum::numBands - 1, b + radius); ++k) { e += std::pow (10.0, in[k] / 10.0); ++n; }
        out[b] = (float) (10.0 * std::log10 (e / n + 1e-20));
    }
}

struct PreResult
{
    bool found = false;
    float bellFreq = 2000, bellGain = 0, bellQ = 1;   // Q は Meld Verb の BELL Q（0.2..12）
    bool lowCut = false; float lowCutHz = 60;
    float excessDb = 0;
};

// 入力の平均スペクトルから、まわりより飛び出た帯域（150 Hz〜8 kHz）を探す
inline PreResult learnPre (const float* dryDb)
{
    PreResult r;
    float broad[Spectrum::numBands];
    smoothBands (dryDb, broad, 6);   // ±1 オクターブのならし
    float mx = -1e9f; for (int b = 0; b < Spectrum::numBands; ++b) mx = std::max (mx, dryDb[b]);
    int best = -1; float bestEx = 0;
    for (int b = 0; b < Spectrum::numBands; ++b)
    {
        const float f = Spectrum::centre (b);
        if (f < 150 || f > 8000 || dryDb[b] < mx - 40) continue;
        const float ex = dryDb[b] - broad[b];
        if (ex > bestEx) { bestEx = ex; best = b; }
    }
    r.excessDb = bestEx;
    if (best >= 0 && bestEx > 2.0f)
    {
        // 飛び出た所の幅（半分の高さまでの帯域数）→ Q
        int lo = best, hi = best;
        while (lo > 0 && dryDb[lo - 1] - broad[lo - 1] > bestEx * 0.5f) --lo;
        while (hi < Spectrum::numBands - 1 && dryDb[hi + 1] - broad[hi + 1] > bestEx * 0.5f) ++hi;
        const float octaves = (float) (hi - lo + 1) / 6.0f;
        // Meld Verb のベルは RBJ の Q の半分の値で表す（BELL Q 0.707 ≒ 1 オクターブ強）
        const float qRbj = std::sqrt (std::pow (2.0f, octaves)) / (std::pow (2.0f, octaves) - 1.0f);
        r.found = true;
        r.bellFreq = Spectrum::centre (best);
        r.bellGain = -std::min (12.0f, bestEx * 0.8f);
        r.bellQ = std::clamp (qRbj * 0.5f, 0.4f, 6.0f);
    }
    // 60 Hz より下が中域より大きい（ゴロゴロ・吹かれ）ならローカット
    double lowE = 0, midE = 0; int nl = 0, nm = 0;
    for (int b = 0; b < Spectrum::numBands; ++b)
    {
        const float f = Spectrum::centre (b);
        if (f < 60) { lowE += std::pow (10.0, dryDb[b] / 10.0); ++nl; }
        else if (f > 200 && f < 2000) { midE += std::pow (10.0, dryDb[b] / 10.0); ++nm; }
    }
    if (nl > 0 && nm > 0 && 10.0 * std::log10 ((lowE / nl) / (midE / nm) + 1e-20) > -3.0) { r.lowCut = true; r.lowCutHz = 70; }
    return r;
}

struct UnmaskResult
{
    bool found = false;
    float lowGain = 0, lowFreq = 250;      // ローシェルフ（GAIN は境目での値＝平らな所はその 2 倍）
    float bellGain = 0, bellFreq = 2000, bellQ = 0.7f;
    float highGain = 0, highFreq = 6000;   // ハイシェルフ
    float maskDb[Spectrum::numBands] {};   // 帯域ごとの「隠している量」
};

// dryDb: 元の音、wetDb: 響き（REVERB EQ の前）。wetGainDb は MIX を反映した響きの大きさの補正
inline UnmaskResult learnUnmask (const float* dryDb, const float* wetDb, float wetGainDb)
{
    UnmaskResult r;
    float d[Spectrum::numBands], w[Spectrum::numBands];
    smoothBands (dryDb, d, 2); smoothBands (wetDb, w, 2);
    float mx = -1e9f; for (int b = 0; b < Spectrum::numBands; ++b) mx = std::max (mx, d[b]);
    double lowSum = 0, highSum = 0; int nLow = 0, nHigh = 0; float bellMax = 0; int bellAt = -1;
    for (int b = 0; b < Spectrum::numBands; ++b)
    {
        const float f = Spectrum::centre (b);
        // 響きが元の音の -6 dB より大きいと「隠している」とみなす。元の音が小さい帯域（-35 dB 以下）は気にしない
        float m = (w[b] + wetGainDb) - (d[b] - 6.0f);
        if (d[b] < mx - 35.0f) m = 0;
        m = std::clamp (m, 0.0f, 12.0f);
        r.maskDb[b] = m;
        if (f >= 50 && f <= 250) { lowSum += m; ++nLow; }
        else if (f >= 6000 && f <= 16000) { highSum += m; ++nHigh; }
        else if (f > 300 && f < 5000 && m > bellMax) { bellMax = m; bellAt = b; }
    }
    const float lowM = nLow ? (float) (lowSum / nLow) : 0.0f, highM = nHigh ? (float) (highSum / nHigh) : 0.0f;
    r.lowGain = -0.5f * std::min (12.0f, lowM);     // シェルフの GAIN は平らな所の半分
    r.highGain = -0.5f * std::min (12.0f, highM);
    if (bellAt >= 0)
    {
        r.bellFreq = Spectrum::centre (bellAt);
        r.bellGain = -std::min (9.0f, bellMax * 0.75f);
        r.bellQ = 0.7f;
    }
    r.found = lowM > 0.5f || highM > 0.5f || bellMax > 0.5f;
    return r;
}
} // namespace assist
