#pragma once
// ディレイ系の共通の部品（JUCE なし）。2026-11-19
//   FracDelay … 端数の遅れを 4 点のなめらかな補間（エルミート）で読む遅れ線（揺れる遅れに）
//   satSoft   … やわらかい頭打ち（tanh に近い有理式。1 を超えると丸まる）
//   Phasor    … 回る針（サイン波の揺れ。ずれない）
//   WowFlutter… テープのゆっくりした揺れ（ワウ）と細かい揺れ（フラッター）。サイン＋なめらかな乱数
//   Grain2    … 2 つの読み口を交互に重ねる音程変え（窓はハン窓）。逆回しにもできる
#include "DynKit.h"
#include <array>

namespace aiofx
{
// 端数の遅れ線（1 チャンネル）。長さは 2 のべき乗にそろえる
struct FracDelay
{
    std::vector<float> b;
    int mask = 0, pos = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 8) n <<= 1;
        b.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0;
    }
    void reset() { std::fill (b.begin(), b.end(), 0.0f); pos = 0; }
    inline void write (float x) { b[(size_t) pos] = x; pos = (pos + 1) & mask; }
    // d サンプル前の値（write の後に呼ぶ。d = 0 が今書いた値。補間に 1 つ新しい値も使うので d ≥ 1）
    inline float read (float d) const
    {
        d = std::clamp (d, 1.0f, (float) (mask - 4));
        const int di = (int) d; const float f = d - (float) di;
        const int i0 = pos - 1 - di;   // pos は次に書く所。pos − 1 が今書いた値（x[n]）→ i0 = x[n − di]
        const float xm1 = b[(size_t) ((i0 + 1) & mask)], x0 = b[(size_t) (i0 & mask)];
        const float x1 = b[(size_t) ((i0 - 1) & mask)], x2 = b[(size_t) ((i0 - 2) & mask)];
        // エルミート（4 点）
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    inline float readInt (int d) const { return b[(size_t) ((pos - 1 - std::clamp (d, 0, mask - 1)) & mask)]; }
};

// 直流止め（1 次のハイパス、y = (1 + R) / 2 × (x − x₋₁) + R y₋₁）。くり返しの輪に入れて、直流や聞こえない低音がたまらないように（DSP 検査 0.4.0）。
// (1 + R) / 2 を掛けないと高い音で 1/√R 倍（5 Hz で +0.003 dB）になり、FEEDBACK 100 %・FREEZE の輪が 1 周ごとに少しずつ大きくなる（DSP 検査 0.4.0 の 2 回目）。
// 掛けると最大がちょうど 1（ナイキストだけ）、ほかは 1 より小さい
struct DcBlock
{
    float R = 0.9993f, gn = 0.99965f, x1 = 0.0f, y1 = 0.0f;
    void prepare (double sr, double fc) { R = (float) std::exp (-2.0 * kPiD * fc / sr); gn = 0.5f * (1.0f + R); }
    void reset() { x1 = y1 = 0.0f; }
    inline float process (float x) { const float y = gn * (x - x1) + R * y1; x1 = x; y1 = flush (y); return y; }
};

// ±1（0 dBFS）までは素通し、その上はなめらかに ±2 で頭打ち（ふつうの大きさでは歪ませない。くり返しの輪の入口に）
inline float softKnee2 (float x)
{
    const float a = std::abs (x);
    if (a <= 1.0f) return x;
    const float o = 1.0f + std::tanh (a - 1.0f);
    return x < 0.0f ? -o : o;
}

// やわらかい頭打ち（|x| が小さい所はほぼそのまま、大きい所は ±1 に近づく）
inline float satSoft (float x)
{
    const float a = std::clamp (x, -3.0f, 3.0f);
    return a * (27.0f + a * a) / (27.0f + 9.0f * a * a);
}

// 回る針（サイン・コサイン）。set で速さ、next で 1 サンプル進めて sin を返す
struct Phasor
{
    float c = 1.0f, s = 0.0f, rc = 1.0f, rs = 0.0f;
    unsigned n = 0;
    void setRate (double sr, double hz) { const double w = 2.0 * kPiD * hz / sr; rc = (float) std::cos (w); rs = (float) std::sin (w); }
    void setPhase (double ph) { c = (float) std::cos (ph); s = (float) std::sin (ph); }
    inline float next()
    {
        const float c2 = c * rc - s * rs; s = s * rc + c * rs; c = c2;
        if ((++n & 255) == 0) { const float m = 1.0f / std::sqrt (c * c + s * s + 1.0e-12f); c *= m; s *= m; }
        return s;
    }
};

// テープの揺れ: ワウ（0.5〜2 Hz のサイン＋とてもゆっくりの乱数）とフラッター（数 Hz〜十数 Hz のサイン＋乱数）。戻り値はサンプル数の揺れ
struct WowFlutter
{
    Phasor wow, flt;
    float rnd = 0.0f, rndT = 0.0f, rndA = 0.0f, fr = 0.0f, frT = 0.0f, frA = 0.0f;
    uint32_t seed = 0x1234567u;
    int cnt = 0, cnt2 = 0;
    double sr = 48000.0;
    void prepare (double sampleRate, double wowHz = 0.9, double fltHz = 9.0)
    {
        sr = sampleRate; wow.setRate (sr, wowHz); flt.setRate (sr, fltHz); wow.setPhase (0.3); flt.setPhase (1.1);
        rndA = tauCoef (sr, 0.35); frA = tauCoef (sr, 0.02);
    }
    // 揺れの位置・乱数を最初に戻す（reset のあとも新品と同じ音にする）
    void reset() { wow.setPhase (0.3); flt.setPhase (1.1); wow.n = flt.n = 0; rnd = rndT = fr = frT = 0.0f; seed = 0x1234567u; cnt = cnt2 = 0; }
    float uni() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }
    // wowS / fltS: 揺れの深さ（サンプル）
    inline float next (float wowS, float fltS)
    {
        if (--cnt <= 0) { cnt = (int) (0.25 * sr); rndT = uni(); }
        if (--cnt2 <= 0) { cnt2 = (int) (0.012 * sr); frT = uni(); }
        rnd = rndT + (rnd - rndT) * rndA; fr = frT + (fr - frT) * frA;
        return wowS * (0.7f * wow.next() + 0.3f * rnd) + fltS * (0.6f * flt.next() + 0.4f * fr);
    }
};

// 1 次の棚・ハイパス（TPT の 1 次ローパスで y = gHF·x + (gDC − gHF)·LP(x)）。極 fp を、周波数 fw で合わせた双一次変換で。32 サンプルで直線につなぐ
struct TptShelf1
{
    float G = 0.5f, lo = 1.0f, hi = 1.0f, z = 0.0f, tG = 0.5f, tLo = 1.0f, tHi = 1.0f, sG = 0.5f, sLo = 1.0f, sHi = 1.0f;
    int left = 0, len = 32;
    void set (double sr, double fp, double gDC, double gHF, double fw = 0.0)
    {
        if (fw <= 0.0) fw = fp;
        fw = std::min (fw, 0.49 * sr);
        const double g = fp * std::tan (kPiD * fw / sr) / fw;
        tG = (float) (g / (1.0 + g)); tLo = (float) gDC; tHi = (float) gHF;
    }
    // DynKit の OnePole（極 a = e^(−2π f / sr)）と同じ伝達関数（合わせ込んだ部品の音を変えずに TPT の形にするとき）。
    // 1 次のフィルターは極・0 Hz・ナイキストの大きさで決まる: LP は 0 Hz 1・ナイキスト (1 − a) / (1 + a)、HP（x − LP）は 0・2a / (1 + a)
    void setOnePoleLP (double sr, double f)
    {
        const double a = std::exp (-2.0 * kPiD * std::min (f, 0.45 * sr) / sr), g = (1.0 - a) / (1.0 + a);
        tG = (float) (g / (1.0 + g)); tLo = 1.0f; tHi = (float) g;
    }
    void setOnePoleHP (double sr, double f)
    {
        const double a = std::exp (-2.0 * kPiD * std::min (f, 0.45 * sr) / sr), g = (1.0 - a) / (1.0 + a);
        tG = (float) (g / (1.0 + g)); tLo = 0.0f; tHi = (float) (1.0 - g);
    }
    void ramp (bool snap, int n = 32)
    {
        if (snap) { G = tG; lo = tLo; hi = tHi; left = 0; }
        else { sG = G; sLo = lo; sHi = hi; len = std::max (1, n); left = len; }
    }
    inline void step()
    {
        if (left <= 0) return;
        if (--left == 0) { G = tG; lo = tLo; hi = tHi; return; }
        const float f = (float) (len - left) / (float) len;
        G = sG + f * (tG - sG); lo = sLo + f * (tLo - sLo); hi = sHi + f * (tHi - sHi);
    }
    void reset() { z = 0.0f; }
    inline float process (float x) { const float v = (x - z) * G; const float y = v + z; z = flush (y + v); return hi * x + (lo - hi) * y; }
};

// 遅れの読み口: 長さが変わったら、古い読み口と新しい読み口を fade 秒でクロスフェードする（読む位置を飛ばさない・すべらせない =
// プチッも音程の鳴き上がりも出ない）。クロスフェード中に次の長さが来たら、終わってから次へ。mod = 揺れ（両方の読み口に足す）
struct XTap
{
    float cur = 2.0f, nxt = 2.0f, tgt = 2.0f, f = 1.0f, step = 0.001f;
    void prepare (double sr, double fadeSec = 0.04) { step = 1.0f / (float) std::max (1.0, fadeSec * sr); }
    void set (float t, bool snap) { tgt = std::max (2.0f, t); if (snap) { cur = nxt = tgt; f = 1.0f; } }
    void snapNow() { cur = nxt = tgt; f = 1.0f; }
    inline float read (const FracDelay& d, float mod = 0.0f)
    {
        if (f >= 1.0f && std::abs (tgt - cur) > 0.5f) { nxt = tgt; f = 0.0f; }
        float y = d.read (std::max (2.0f, cur + mod));
        if (f < 1.0f)
        {
            f = std::min (1.0f, f + step);
            const float y2 = d.read (std::max (2.0f, nxt + mod));
            y += f * (y2 - y);
            if (f >= 1.0f) cur = nxt;
        }
        return y;
    }
};

// 2 つの読み口を交互に重ねる音程変え。ratio = 2^(半音/12)、win = 窓の長さ（サンプル）。reverse = 窓の中を逆に読む
struct Grain2
{
    float ph = 0.0f;
    // 入口は呼ぶ側の FracDelay。戻り値は音程を変えた 1 サンプル
    inline float process (const FracDelay& d, float ratio, float win, bool reverse = false)
    {
        // 読み口の遅れ = 窓 × (1 − 進み)（ratio > 1 で遅れが縮む = 音程が上がる）
        const float step = reverse ? (1.0f + 1.0f) / win : (1.0f - ratio) / win;
        ph += step; ph -= std::floor (ph);
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            float p = ph + 0.5f * (float) k; p -= std::floor (p);
            const float dl = 2.0f + p * win;
            const float w = std::sin ((float) kPiD * p);
            y += w * w * d.read (dl);
        }
        return y;
    }
};
} // namespace aiofx
