// Mod Host の通り道の部品（JUCE なし。tools/test_chain.cpp で数値テスト）
//   入力 ─┬─ 帯域分け（LOW / MID / HIGH、LR4）→ SEND の分だけ × DRIVE → 中のプラグイン → × MIX ─┐
//         └─ 送らなかった分・送った分の元の音（中のプラグインの遅れだけ遅らせる）──────────────┴→ AUTO GAIN → OUT
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace mh
{
constexpr float pi = 3.14159265358979f;

// TPT の 2 次フィルター（バターワース）。lp と hp を同時に出す
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f, ic1 = 0, ic2 = 0;
    void set (float fc, float sr)
    {
        const float g = std::tan (pi * std::clamp (fc, 10.0f, 0.45f * sr) / sr);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    inline void process (float x, float& lp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; hp = x - k * v1 - v2;
    }
};

// Linkwitz-Riley 4 次の分け目（lo + hi で元の音の大きさのまま＝オールパス）
struct LR4
{
    Svf l1, l2, h1, h2;
    void set (float fc, float sr) { l1.set (fc, sr); l2.set (fc, sr); h1.set (fc, sr); h2.set (fc, sr); }
    void reset() { l1.reset(); l2.reset(); h1.reset(); h2.reset(); }
    inline void split (float x, float& lo, float& hi)
    {
        float a, b, c, d;
        l1.process (x, a, b);  l2.process (a, lo, c);
        h1.process (x, c, b);  h2.process (b, d, hi);
    }
};

// 3 つに分ける。low は 2 つ目の分け目のオールパスも通して、足し合わせたときに位相をそろえる
struct Crossover3
{
    LR4 a[2], b[2], ap[2];
    void set (float f1, float f2, float sr)
    {
        f1 = std::clamp (f1, 20.0f, 0.4f * sr);
        f2 = std::clamp (std::max (f2, f1 * 1.25f), 25.0f, 0.45f * sr);
        for (int c = 0; c < 2; ++c) { a[c].set (f1, sr); b[c].set (f2, sr); ap[c].set (f2, sr); }
    }
    void reset() { for (int c = 0; c < 2; ++c) { a[c].reset(); b[c].reset(); ap[c].reset(); } }
    inline void process (int ch, float x, float& lo, float& mid, float& hi)
    {
        float rest, l, h;
        a[ch].split (x, lo, rest);
        b[ch].split (rest, mid, hi);
        ap[ch].split (lo, l, h);
        lo = l + h;
    }
};

// 遅らせるだけ（中のプラグインの遅れに、元の音を合わせる）
struct Delay
{
    std::vector<float> buf[2];
    int mask = 0, w = 0;
    void prepare (int maxDelay)
    {
        int n = 1; while (n < maxDelay + 2) n <<= 1;
        for (auto& b : buf) b.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); }
    // 1 サンプル入れて、d サンプル前を返す（2 チャンネル同時）
    inline void process (float& l, float& r, int d)
    {
        if (mask == 0) return;
        d = std::clamp (d, 0, mask - 1);
        buf[0][(size_t) w] = l; buf[1][(size_t) w] = r;
        const int rd = (w - d) & mask;
        l = buf[0][(size_t) rd]; r = buf[1][(size_t) rd];
        w = (w + 1) & mask;
    }
};

// 入口と出口の音量を比べて、出口を入口にそろえる（歪みで大きくなっただけで「良い」と思わないように）
struct AutoGain
{
    double pIn = 0, pOut = 0, pFast = 0;
    float gain = 1.0f;
    float aPow = 0, aFast = 0, aGain = 0;    // 1 サンプルあたり / 1 区切りあたりの係数
    void prepare (double sr, int chunk)
    {
        aPow = 1.0f - (float) std::exp (-1.0 / (0.4 * sr));                 // 音量は 0.4 秒でならす
        aFast = 1.0f - (float) std::exp (-1.0 / (0.03 * sr));               // 入口が今鳴っているか（30 ms）
        aGain = 1.0f - (float) std::exp (-(double) chunk / (0.5 * sr));     // 補正は 0.5 秒で動く
        reset();
    }
    void reset() { pIn = pOut = pFast = 0; gain = 1.0f; }
    inline void measure (float inL, float inR, float outL, float outR)
    {
        const double e = 0.5 * (inL * inL + inR * inR);
        pIn  += aPow * (e - pIn);
        pFast += aFast * (e - pFast);
        pOut += aPow * (0.5 * (outL * outL + outR * outR) - pOut);
    }
    // 区切りの終わりに呼ぶ。入口が静か（-60 dB 未満）か、消えていく途中なら止める（リバーブの残りで下げすぎない）
    float update()
    {
        if (pIn > 1.0e-6 && pFast > 0.5 * pIn && pOut > 1.0e-12)
        {
            const float target = std::clamp ((float) std::sqrt (pIn / pOut), 0.063f, 4.0f);   // -24〜+12 dB
            gain += (target - gain) * aGain;
        }
        return gain;
    }
};

// MORPH: A と B の間。切り替え式のつまみ（段が少ない）は真ん中で切り替える
inline float morphValue (float a, float b, float m, bool discrete)
{
    m = std::clamp (m, 0.0f, 1.0f);
    if (discrete) return m < 0.5f ? a : b;
    return a + (b - a) * m;
}

// 周波数と 0〜1 の行き来（20 Hz〜20 kHz の対数）
inline float hzToNorm (float hz, float lo, float hi) { return std::log (std::clamp (hz, lo, hi) / lo) / std::log (hi / lo); }
inline float normToHz (float n, float lo, float hi) { return lo * std::pow (hi / lo, std::clamp (n, 0.0f, 1.0f)); }
} // namespace mh
