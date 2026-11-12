#pragma once
// Meld Verb — 3 つの響き（EARLY / PLATE / HALL）を混ぜるリバーブのエンジン
// JUCE に依存しない素の C++（テスト用の小さなプログラム tools/render_meld.cpp からも使う）
//
// 流れ
//   入力 → PRE EQ → SMOOTHING（響きへ送る量を自動で上げ下げ）→ PRE-DELAY
//        → EARLY（初期反射）/ PLATE / HALL（それぞれ 16 本ディレイの FDN）を並べて鳴らす
//        → 3 つを LEVEL で混ぜる → REVERB EQ → DRY/WET → OUTPUT
//
// 数値（つまみの範囲・効き方・減衰の仕方）は、実際のプラグインに音を通して測った結果に合わせてある。
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <complex>

namespace meld
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// 小さな部品
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 12345u) { if (s == 0) s = 1; }
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f * 2.0f - 1.0f; }   // -1..1
    float uni() { return 0.5f + 0.5f * next(); }   // 0..1
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void flat() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    double magDb (double f, double sr) const
    {
        const double w = 2 * kPi * f / sr;
        const std::complex<double> e1 = std::polar (1.0, -w), e2 = e1 * e1;
        const auto h = ((double) b0 + (double) b1 * e1 + (double) b2 * e2) / (1.0 + (double) a1 * e1 + (double) a2 * e2);
        return 20.0 * std::log10 (std::abs (h) + 1e-12);
    }
    static double clampF (double f, double sr) { return std::clamp (f, 5.0, 0.48 * sr); }
    void lp (double f, double q, double sr) { f = clampF (f, sr); double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void hp (double f, double q, double sr) { f = clampF (f, sr); double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void lp1 (double f, double sr) { f = clampF (f, sr); double k = std::tan (kPi * f / sr); set (k, k, 0, 1 + k, k - 1, 0); }
    void hp1 (double f, double sr) { f = clampF (f, sr); double k = std::tan (kPi * f / sr); set (1, -1, 0, 1 + k, k - 1, 0); }
    void peak (double f, double q, double db, double sr)
    { f = clampF (f, sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
      set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowShelf (double f, double q, double db, double sr)
    { f = clampF (f, sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), r = 2 * std::sqrt (A) * al;
      set (A * ((A + 1) - (A - 1) * c + r), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - r),
           (A + 1) + (A - 1) * c + r, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - r); }
    void highShelf (double f, double q, double db, double sr)
    { f = clampF (f, sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), r = 2 * std::sqrt (A) * al;
      set (A * ((A + 1) + (A - 1) * c + r), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - r),
           (A + 1) - (A - 1) * c + r, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - r); }
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void set (float fc, float sr) { a = std::exp (-2.0f * (float) kPi * std::min (fc, 0.49f * sr) / sr); if (fc >= 0.49f * sr) a = 0; }
    inline float process (float x) { z = (1 - a) * x + a * z; return z; }
    void reset() { z = 0; }
};

// 1 次シェルフ（ループ内の減衰の周波数差に使う。境目の周波数で dB が半分になる形）
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void flat() { b0 = 1; b1 = 0; a1 = 0; }
    void lowShelf (float G, float fc, float sr)   // DC で G、高域で 1
    {
        if (std::abs (G - 1.0f) < 1e-6f) { flat(); return; }
        fc = std::max (fc, 10.0f);
        float c = sr / ((float) kPi * fc), g = std::sqrt (G);   // 周波数を曲げない（ナイキスト近くでも効き方が変わらない）
        float a0 = c + 1 / g;
        b0 = (c + g) / a0; b1 = (g - c) / a0; a1 = (1 / g - c) / a0;
    }
    void highShelf (float G, float fc, float sr)  // DC で 1、高域で G
    {
        if (std::abs (G - 1.0f) < 1e-6f) { flat(); return; }
        fc = std::max (fc, 10.0f);
        float c = sr / ((float) kPi * fc), g = std::sqrt (G);
        float a0 = c + g;
        b0 = (G * c + g) / a0; b1 = (g - G * c) / a0; a1 = (g - c) / a0;
    }
    inline float process (float x) { float y = b0 * x + z; z = b1 * x - a1 * y; return y; }
    void reset() { z = 0; }
};

struct Delay
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxLen) { int n = 1; while (n < maxLen + 8) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float at (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }   // d=0 が直前に入れた値
    inline float frac (float d) const    // 3 次補間
    {
        int i = (int) d; float f = d - (float) i;
        float xm1 = at (i > 0 ? i - 1 : 0), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Allpass
{
    Delay d; int len = 1; float g = 0.5f;
    void init (int maxLen) { d.init (maxLen + 2); }
    void set (int l, float gg) { len = std::clamp (l, 1, (int) d.buf.size() - 4); g = gg; }
    inline float process (float x)
    {
        float v = d.at (len - 1);
        float u = x + g * v;
        d.push (u);
        return v - g * u;
    }
    void clear() { d.clear(); }
};

// ゆっくり動く乱数（揺らし用）。rate Hz ごとに次の目標へなめらかに移る
struct SmoothRandom
{
    Rng r { 1 }; float cur = 0, from = 0, to = 0, ph = 0;
    void seed (uint32_t s) { r = Rng (s); from = r.next(); to = r.next(); ph = r.uni(); }
    inline float next (float inc)
    {
        ph += inc;
        if (ph >= 1.0f) { ph -= 1.0f; from = to; to = r.next(); }
        const float s = ph * ph * (3.0f - 2.0f * ph);
        return from + (to - from) * s;
    }
};

//==============================================================================
// EQ（PRE EQ と REVERB EQ。低域＝シェルフかハイパス、ベル、高域＝シェルフかローパス）
struct EqSettings
{
    bool  on = false;
    float lowGain = 0, lowFreq = 100, lowSlope = 0.707f; int lowShape = 0;    // 0 = Low Shelf, 1 = High Pass
    float highGain = 0, highFreq = 11000, highSlope = 0.707f; int highShape = 0; // 0 = High Shelf, 1 = Low Pass
    float bellGain = 0, bellFreq = 2000, bellQ = 0.707f;
    bool operator== (const EqSettings& o) const
    {
        return on == o.on && lowGain == o.lowGain && lowFreq == o.lowFreq && lowSlope == o.lowSlope && lowShape == o.lowShape
            && highGain == o.highGain && highFreq == o.highFreq && highSlope == o.highSlope && highShape == o.highShape
            && bellGain == o.bellGain && bellFreq == o.bellFreq && bellQ == o.bellQ;
    }
    bool operator!= (const EqSettings& o) const { return ! (*this == o); }
};

class TriEQ
{
public:
    static constexpr int MaxSec = 9;
    // 測定で合わせた形: シェルフは「その周波数でちょうど GAIN」（平らな所は 2 倍）、SLOPE は Q の代わり。
    // ハイパス／ローパスは SLOPE 1.5 ごとに 1 次ずつ急になる（0.707 = 6 dB/oct, 3 = 12, 6 = 24 dB/oct）。ベルの Q は 2 倍の RBJ。
    static int passOrder (float slope) { return std::clamp ((int) std::lround (slope / 1.5f), 1, 8); }
    static float shelfQ (float slope) { return 0.5f * std::pow (std::max (0.2f, slope) / 0.707f, 0.33f); }

    void setup (const EqSettings& s, double sr)
    {
        on = s.on; n = 0;
        if (! on) return;
        auto addPass = [&] (bool high, float f, float slope)
        {
            const int ord = passOrder (slope);
            for (int k = 0; k < ord / 2; ++k)
            {
                const double q = 1.0 / (2.0 * std::cos (kPi * (2 * k + 1) / (2.0 * ord)));
                if (high) sec[n++].hp (f, q, sr); else sec[n++].lp (f, q, sr);
            }
            if (ord & 1) { if (high) sec[n++].hp1 (f, sr); else sec[n++].lp1 (f, sr); }
        };
        // シェルフ・ベルは 0 dB でも区間として残す（0 dB ではちょうど平ら）。量をなめらかに動かしても並びが変わらず、プチッとしない
        if (s.lowShape == 1) addPass (true, s.lowFreq, s.lowSlope);
        else sec[n++].lowShelf (s.lowFreq, shelfQ (s.lowSlope), 2.0 * s.lowGain, sr);
        sec[n++].peak (s.bellFreq, 2.0 * std::max (0.2f, s.bellQ), s.bellGain, sr);
        if (s.highShape == 1) addPass (false, s.highFreq, s.highSlope);
        else sec[n++].highShelf (s.highFreq, shelfQ (s.highSlope), 2.0 * s.highGain, sr);
    }
    inline float process (float x) { for (int i = 0; i < n; ++i) x = sec[i].process (x); return x; }
    void reset() { for (auto& b : sec) b.reset(); }
    double magDb (double f, double sr) const { double d = 0; if (on) for (int i = 0; i < n; ++i) d += sec[i].magDb (f, sr); return d; }
private:
    Biquad sec[MaxSec]; int n = 0; bool on = false;
};

//==============================================================================
// SMOOTHING：響きへ送る量を自動で上げ下げする（音の大きさには関係なく、「今」と「ここ数秒」の比べ方だけで決まる）
//  ・新しく鳴り始めた音（今が最近よりずっと大きい）… 最大 +1.9 dB
//  ・同じ大きさで続く音 … 約 -3.3 dB（伸ばした音で響きが溜まりすぎない）
//  ・前より静かになった所 … さらに下げる（大きい音の響きの後ろで小さい音がにごらない）
// 測定から合わせた値: 速い包絡 4.9 ms、遅い包絡 2.78 s、gain = -3.29 + 0.253 × (速い/遅い の dB)、範囲 -31.9〜+1.9 dB
class SendSmoother
{
public:
    // 1 ms ごとのかたまりで計算する（測定から合わせた形）。そのぶん響きへ送る音だけ 1 ms 遅れる（PRE-DELAY から差し引く）
    struct Tune { double fastMs = 4.9, slowMs = 2779.0, base = -3.29, slope = 0.253, lo = -31.9, hi = 1.9, glideMs = 0.567; } tune;
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        blk = std::max (8, (int) std::lround (sampleRate / 1000.0));
        bufL.assign ((size_t) blk, 0.0f); bufR.assign ((size_t) blk, 0.0f);
        setCoefs(); reset();
    }
    int latency() const { return blk; }
    void setCoefs()
    {
        af = std::exp (-1.0 / tune.fastMs);   // 1 ms きざみ
        as = std::exp (-1.0 / tune.slowMs);
        ag = std::exp (-1.0 / tune.glideMs);
    }
    void reset()
    {
        ef = es = 0; gDb = 0; gPrev = gCur = 1; pos = 0; acc = 0; lastDb = 0;
        std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f);
    }
    // 入力を 1 サンプル入れて、1 ms 前のサンプルにゲインをかけて返す
    inline void process (float& l, float& r, float amount)
    {
        const float outL = bufL[(size_t) pos], outR = bufR[(size_t) pos];
        // ためた 1 ms は、その 1 ms から決めたゲインで出す（測定どおり。音の出だしがそのまま +1.9 dB になる）
        const float g = gCur;
        bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
        acc += 0.5 * ((double) l * l + (double) r * r);
        l = outL * g; r = outR * g;
        if (++pos >= blk)
        {
            pos = 0;
            const double pb = acc / blk; acc = 0;
            ef = af * ef + (1 - af) * pb;
            es = as * es + (1 - as) * pb;
            const double ratio = 10.0 * std::log10 ((ef + 1e-14) / (es + 1e-14));
            // しばらく何も鳴っていない（遅い包絡がほぼ 0）ときは「次の音は出だし」として上限にしておく（測定: 無音のあとの 1 発は +1.9 dB）
            const double t = es < 1e-10 ? tune.hi : std::clamp (tune.base + tune.slope * ratio, tune.lo, tune.hi);
            gDb = ag * gDb + (1 - ag) * t;
            lastDb = (float) gDb * amount;
            gPrev = gCur; gCur = std::pow (10.0f, lastDb / 20.0f);   // 次の 1 ms（今ためた音）はこのゲインで出す
        }
    }
    float currentDb() const { return lastDb; }
private:
    double sr = 48000, af = 0, as = 0, ag = 0, ef = 0, es = 0, gDb = 0, acc = 0;
    float gPrev = 1, gCur = 1, lastDb = 0; int blk = 48, pos = 0;
    std::vector<float> bufL, bufR;
};

//==============================================================================
// EARLY（初期反射）。左右それぞれの入力から「同じ側」と「反対側」へ短い跳ね返りを並べる
struct EarlyParams
{
    float timeMs = 30.6f;    // 跳ね返りが続く長さ 0..250 ms
    float size = 4.9f;       // 4..40（跳ね返りの間隔と、最後の消え方の長さ）
    float diffusion = 100;   // 0..100（0 でパチパチ、100 でなめらか）
    float angle = 40.2f;     // 0..100（0 = 音源と同じ側から、100 = 反対側から遅れて）
    float lowpassHz = 414.9f; // 反対側の跳ね返りの高域カット（頭の陰）
    bool operator== (const EarlyParams& o) const { return timeMs == o.timeMs && size == o.size && diffusion == o.diffusion && angle == o.angle && lowpassHz == o.lowpassHz; }
};

class Early
{
public:
    // 測定でわかった形:
    //  ・跳ね返りは 12 個。TIME の中に並ぶ（TIME が長いほど間が空く）
    //  ・SIZE は 1 つ 1 つの跳ね返りの「にじみの長さ」（小さいとパチッ、大きいと長く尾を引いて次とつながる）
    //  ・DIFFUSION 0 で 1 本ずつの鋭い音、100 でにじんだ音
    //  ・ANGLE は時間とともに大きくなるか小さくなるか（0 = 前が大きく後ろほど小さい／40 = 平ら／100 = 後ろほど大きい）と、
    //    同じ側と反対側の割合
    // つまみの変化（2026-11-12）: 目標へ 4 サンプルごとに 30 ms で近づける。にじみ（オールパス）と小さな響きの長さは
    // 小数のまま少しずつ動かし（一気に変えるとプチッと鳴る）、跳ね返りの並びは前の並びから 30 ms で混ぜ替える（途中で次が来たら、終わってから）
    struct Tune
    {
        float firstMs = 0.4f, firstDry = 3.0f, burstMs = 0.7f, burstPow = 1.0f, apG = 0.6f, apPow = 0.7f;
        float decayDb = 37.0f, riseDb = 27.0f, levelDb = 0.8f, lrTrim = -1.2f;
        float tailDb = -28.0f, tailRt = 0.09f, tailPow = 0.5f;   // 跳ね返りのあとに残る小さな響き
        float shadowDb = -3.0f, hpHz = 180.0f;
    } tune;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : in) d.init ((int) (0.3f * sr));
        for (auto& c : diff) for (auto& a : c) a.init ((int) (0.012f * 8.2f * sr) + 4);
        for (auto& d : rLine) d.init ((int) (0.014f * 3.0f * sr) + 8);
        aSub = 1.0f - std::exp (-(float) kSub / (0.03f * sr));
        aMix = 1.0f - std::exp (-1.0f / (0.01f * sr));
        aLen = 1.0f - std::exp (-1.0f / (0.01f * sr));
        fadeInc = 1.0f / (0.03f * sr);
        reset();
        built = false;
    }
    void reset()
    {
        for (auto& d : in) d.clear();
        for (auto& c : diff) for (auto& a : c) a.clear();
        for (auto& l : crossLp) l.reset();
        for (auto& h : outHp) h.reset();
        for (auto& h : outSh) h.reset();
        for (auto& d : rLine) d.clear();
        fade = 1.0f; tapsDirty = false; apMix = apOn ? 1.0f : 0.0f;
    }
    // now = true: なめらかにせず、すぐその形にする（最初・開き直したとき・画面の表示用）
    void setParams (const EarlyParams& p, bool now = false)
    {
        tgt = p;
        if (! built || now) { cur = p; apply (true); built = true; }
    }
    const EarlyParams& params() const { return cur; }

    inline void process (float inL, float inR, float& outL, float& outR)
    {
        if (--subCount <= 0) { subCount = kSub; if (step()) apply (false); }
        if (tapsDirty && fade >= 1.0f) { old = taps; build (taps, cur); fade = 0.0f; tapsDirty = false; }
        in[0].push (inL); in[1].push (inR);
        float own[2], cross[2];
        tapSum (taps, own, cross);
        if (fade < 1.0f)
        {
            float o2[2], c2[2]; tapSum (old, o2, c2);
            const float a = fade * fade * (3.0f - 2.0f * fade), b = 1.0f - a;
            for (int c = 0; c < 2; ++c) { own[c] = a * own[c] + b * o2[c]; cross[c] = a * cross[c] + b * c2[c]; }
            fade = std::min (1.0f, fade + fadeInc);
        }
        float l = own[0] + crossLp[1].process (cross[1]);
        float r = own[1] + crossLp[0].process (cross[0]);
        // にじみ: いつも通しておき、DIFFUSION 0 ⇄ 0 より上の切り替えだけ 10 ms で混ぜる（0 のときは素通し。にじみが 0 でも遅れはあるため）
        { const float t = apOn ? 1.0f : 0.0f, nv = apMix + (t - apMix) * aMix; apMix = (nv == apMix || std::abs (t - nv) < 1.0e-6f) ? t : nv; }
        {
            float dl = l, dr = r;
            for (int i = 0; i < 5; ++i) { dl = diff[0][i].process (dl); dr = diff[1][i].process (dr); }
            l += apMix * (dl - l); r += apMix * (dr - r);
        }
        float v[4];
        for (int i = 0; i < 4; ++i)
        {
            if (rLenF[i] != rLenT[i]) { const float nv = rLenF[i] + (rLenT[i] - rLenF[i]) * aLen; rLenF[i] = (nv == rLenF[i] || std::abs (rLenT[i] - nv) < 1.0e-3f) ? rLenT[i] : nv; }
            v[i] = rLenF[i] == rLenT[i] ? rLine[i].at ((int) rLenF[i] - 1) * rG[i] : rLine[i].frac (rLenF[i] - 1.0f) * rG[i];
        }
        const float tl = v[0] + v[1], tr = v[2] - v[3];
        const float h0 = v[0] + v[1], h1 = v[0] - v[1], h2 = v[2] + v[3], h3 = v[2] - v[3];
        rLine[0].push (0.5f * (h0 + h2) + l); rLine[1].push (0.5f * (h1 + h3) - l);
        rLine[2].push (0.5f * (h0 - h2) + r); rLine[3].push (0.5f * (h1 - h3) + r);
        outL = outSh[0].process (outHp[0].process (l + rGain * tl)); outR = outSh[1].process (outHp[1].process (r + rGain * tr));
    }

    struct View { int n = 0; float ms[24] {}; float db[24] {}; float side[24] {}; };
    View view() const
    {
        View v; const auto& s = taps; float mx = 1e-9f;
        for (int k = 0; k < s.n; ++k) mx = std::max (mx, std::abs (s.own[k]) + std::abs (s.cross[k]));
        for (int k = 0; k < s.n && v.n < 24; ++k)
        {
            v.ms[v.n] = (float) s.pos[k] / sr * 1000.0f;
            v.db[v.n] = 20.0f * std::log10 ((std::abs (s.own[k]) + std::abs (s.cross[k])) / mx + 1e-9f);
            v.side[v.n] = std::abs (s.cross[k]) / (std::abs (s.own[k]) + std::abs (s.cross[k]) + 1e-9f);
            ++v.n;
        }
        return v;
    }

private:
    static constexpr int K = 12;
    static constexpr int kSub = 4;
    struct TapSet { int n = 0; int pos[K] {}, xpos[K] {}; float own[K] {}, cross[K] {}; };

    // 長さを小数のまま動かせるオールパス。目標は前と同じ整数の長さで、そこへ 10 ms でなめらかに近づく
    // （止まっていれば整数ちょうど = 前とまったく同じ音。動いている間だけ直線補間で読む）
    struct FracAllpass
    {
        Delay d; float len = 1, lenT = 1, k = 0.002f, g = 0.5f;
        void init (int maxLen) { d.init (maxLen + 4); }
        inline float process (float x)
        {
            if (len != lenT) { const float nv = len + (lenT - len) * k; len = (nv == len || std::abs (lenT - nv) < 1.0e-3f) ? lenT : nv; }
            const int i = (int) len; const float f = len - (float) i;
            const float v = d.at (i - 1) + f * (d.at (i) - d.at (i - 1));
            const float u = x + g * v;
            d.push (u);
            return v - g * u;
        }
        void clear() { d.clear(); }
    };

    // 目標へ 1 歩近づける（動いたら true）
    bool step()
    {
        bool moved = false;
        auto lin = [&] (float& v, float t, float eps) { if (v == t) return; const float nv = v + (t - v) * aSub; v = (nv == v || std::abs (t - nv) < eps) ? t : nv; moved = true; };
        lin (cur.timeMs, tgt.timeMs, 1.0e-3f); lin (cur.size, tgt.size, 1.0e-4f); lin (cur.diffusion, tgt.diffusion, 1.0e-3f); lin (cur.angle, tgt.angle, 1.0e-3f);
        if (cur.lowpassHz != tgt.lowpassHz)
        {
            const float lv = std::log (std::max (10.0f, cur.lowpassHz)), lt = std::log (std::max (10.0f, tgt.lowpassHz));
            const float nl = lv + (lt - lv) * aSub; cur.lowpassHz = (nl == lv || std::abs (lt - nl) < 1.0e-4f) ? tgt.lowpassHz : std::exp (nl);
            moved = true;
        }
        return moved;
    }

    // cur の値をにじみ・小さな響き・フィルターに入れる。跳ね返りの並びは now ならすぐ、そうでなければ混ぜ替えの予約
    void apply (bool now)
    {
        const EarlyParams& p = cur;
        const float d = std::clamp (p.diffusion / 100.0f, 0.0f, 1.0f);
        static const float apMs[5] = { 0.31f, 0.67f, 1.13f, 1.71f, 2.43f };
        const float sc = tune.burstMs * std::pow (std::clamp (p.size, 4.0f, 40.0f) / 4.9f, tune.burstPow);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 5; ++i)
            {
                auto& a = diff[c][i];
                a.lenT = (float) std::clamp (std::max (1, (int) (apMs[i] * (c ? 1.09f : 1.0f) * sc * 0.001f * sr)), 1, (int) a.d.buf.size() - 6);
                a.k = 1.0f - std::exp (-1.0f / (0.01f * sr));
                if (now) a.len = a.lenT;
                a.g = tune.apG * std::pow (d, tune.apPow);
            }
        // 反対側の高域を少しだけ下げる（頭の陰。測定では最大 -3 dB ほどのなだらかなシェルフ）
        for (auto& l : crossLp) l.highShelf (std::min (p.lowpassHz * 1.5f, 0.45f * sr), 0.6, tune.shadowDb, sr);
        for (auto& h : outHp) h.hp1 (tune.hpHz, sr);
        { const float a = std::clamp (p.angle / 100.0f, 0.0f, 1.0f); const float db = 1.15f * (-0.5f - 4.0f * std::pow (a, 1.5f));
          for (auto& h : outSh) h.highShelf (4000.0, 0.6, db, sr); }   // 角度が大きい（反対側から）ほど暗い
        apOn = d > 0.001f;
        // 残りの小さな響き（4 本の FDN）
        static const float rMs[4] = { 7.3f, 9.1f, 11.7f, 13.9f };
        const float szk = std::pow (std::clamp (p.size, 4.0f, 40.0f) / 4.9f, tune.tailPow);
        const float rt = tune.tailRt * szk;
        for (int i = 0; i < 4; ++i)
        {
            rLenT[i] = (float) std::clamp (std::max (8, (int) (rMs[i] * szk * 0.001f * sr)), 8, (int) rLine[i].buf.size() - 12);
            if (now) rLenF[i] = rLenT[i];
            rG[i] = std::pow (10.0f, -3.0f * rLenT[i] / sr / rt);
        }
        rGain = std::pow (10.0f, tune.tailDb / 20.0f) * std::sqrt (0.11f / rt) * 2.0f;
        // 跳ね返りの並び（TIME・ANGLE・DIFFUSION で変わる）
        if (now) { build (taps, p); old = taps; fade = 1.0f; tapsDirty = false; apMix = apOn ? 1.0f : 0.0f; tapsBuiltFor = p; }
        else if (p.timeMs != tapsBuiltFor.timeMs || p.angle != tapsBuiltFor.angle || p.diffusion != tapsBuiltFor.diffusion) { tapsDirty = true; }
    }

    inline void tapSum (const TapSet& s, float* own, float* cross) const
    {
        own[0] = own[1] = cross[0] = cross[1] = 0;
        for (int k = 0; k < s.n; ++k)
        {
            own[0] += s.own[k] * in[0].at (s.pos[k]); own[1] += s.own[k] * in[1].at (s.pos[k]);
            cross[0] += s.cross[k] * in[0].at (s.xpos[k]); cross[1] -= s.cross[k] * in[1].at (s.xpos[k]);
        }
    }

    void build (TapSet& s, const EarlyParams& p)
    {
        tapsBuiltFor = p;
        // 並び（0..1）。1 本目は 0（いちばん最初に強い跳ね返り）
        static const float xs[K] = { 0.0f, 0.10f, 0.17f, 0.33f, 0.35f, 0.47f, 0.53f, 0.64f, 0.71f, 0.80f, 0.89f, 0.97f };
        static const float sg[K] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1 };
        static const float sx[K] = { 1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1 };
        static const float jx[K] = { 0.0f, 0.31f, 0.77f, 0.52f, 0.12f, 0.93f, 0.44f, 0.68f, 0.25f, 0.85f, 0.57f, 0.38f };
        const float T = std::clamp (p.timeMs, 0.0f, 250.0f);
        const float a = std::clamp (p.angle / 100.0f, 0.0f, 1.0f);
        // 同じ側と反対側の音量差（dB、左から入れたとき）
        static const float aTab[6] = { 0.0f, 0.25f, 0.40f, 0.50f, 0.75f, 1.0f };
        static const float lTab[6] = { 8.8f, 6.6f, 3.3f, 2.2f, -1.3f, -2.0f };
        int ii = 0; while (ii < 4 && a > aTab[ii + 1]) ++ii;
        const float fr = std::clamp ((a - aTab[ii]) / (aTab[ii + 1] - aTab[ii]), 0.0f, 1.0f);
        const float lrDb = lTab[ii] + (lTab[ii + 1] - lTab[ii]) * fr + tune.lrTrim;
        const float ratio = std::pow (10.0f, lrDb / 20.0f);
        const float go = ratio / std::sqrt (1 + ratio * ratio), gc = 1.0f / std::sqrt (1 + ratio * ratio);
        double e = 0;
        s.n = K;
        for (int k = 0; k < K; ++k)
        {
            const float x = xs[k];
            float db;
            if (a <= 0.4f) db = -tune.decayDb * (0.4f - a) / 0.4f * x;              // 後ろほど小さい
            else db = -tune.riseDb * (a - 0.4f) / 0.6f * (1.0f - x);                 // 後ろほど大きい
            const float g = std::pow (10.0f, db / 20.0f);
            const float t = tune.firstMs + tune.firstDry * (1.0f - std::clamp (p.diffusion / 100.0f, 0.0f, 1.0f)) + T * x;
            s.pos[k] = (int) std::lround (t * 0.001f * sr);
            const float xoff = std::min (1.0f, T / 10.0f) * (k == 0 ? 0.0f : (0.2f + 0.6f * jx[k]));
            s.xpos[k] = (int) std::lround ((t + xoff) * 0.001f * sr);
            s.own[k] = g * go * (T > 0 ? sg[k] : 1.0f);
            s.cross[k] = g * gc * (T > 0 ? sx[k] : 1.0f);
            e += (double) s.own[k] * s.own[k] + (double) s.cross[k] * s.cross[k];
            if (T <= 0.0f) { if (k == 0) { s.n = 1; break; } }
        }
        // 全体の大きさ（測定: TIME 0 → -0.6, 30 → -1.3, 80 → +1.3, 150 → +3.1, 250 → +4.5 dB）
        static const float tT[5] = { 0, 30, 80, 150, 250 }, tE[5] = { -0.6f, -1.3f, 1.3f, 3.1f, 4.5f };
        int j = 0; while (j < 3 && T > tT[j + 1]) ++j;
        const float fe = std::clamp ((T - tT[j]) / (tT[j + 1] - tT[j]), 0.0f, 1.0f);
        const float eDb = tE[j] + (tE[j + 1] - tE[j]) * fe + tune.levelDb;
        const float norm = (float) std::sqrt (std::pow (10.0, eDb / 10.0) / std::max (1e-12, e));
        for (int k = 0; k < s.n; ++k) { s.own[k] *= norm; s.cross[k] *= norm; }
    }

    float sr = 48000;
    EarlyParams tgt, cur, tapsBuiltFor;
    TapSet taps, old;
    bool built = false, apOn = true, tapsDirty = false;
    float fade = 1, fadeInc = 0.001f, aSub = 0.01f, aMix = 0.002f, apMix = 1.0f, aLen = 0.002f;
    int subCount = 0;
    Delay in[2];
    FracAllpass diff[2][5];
    Biquad crossLp[2], outHp[2], outSh[2];
    Delay rLine[4]; float rLenF[4] { 8, 8, 8, 8 }, rLenT[4] { 8, 8, 8, 8 }; float rG[4] {}, rGain = 0;
};

//==============================================================================
// PLATE / HALL（16 本ディレイの FDN）
struct TankParams
{
    float timeSec = 2.5f;      // 0.02..24
    float size = 11.0f;        // 4..40
    float diffusion = 30.0f;   // 0..100
    float attackMs = 43.1f;    // 0..250（響きがふくらむまでの時間）
    float lowpassHz = 7843.0f; // 30..20000
    float damping = 3.0f;      // 0..10（高域の減り方の深さ。大きいほど高域が残る）
    float balance = 0.61f;     // 0..1（0.5 より小さいと高域、大きいと低域の響きが短くなる）
    float crossoverHz = 1818.0f;
    float modRate = 0.48f, modDepth = 25.0f; int modType = 0;   // 0 = Random, 1 = Pitch
    bool freeze = false;
};

class Tank
{
public:
    static constexpr int N = 16;
    struct Tune
    {
        float msPerSize = 3.0f;      // 線の平均の長さ（ms）= msPerSize × SIZE
        float sizeRef = 7.44f;       // ここでの 1 回あたりの損失 = c × 60 × 平均の長さ
        float rtMul = 1.4f;          // 中域の残響時間 = rtMul × TIME（BALANCE 0.5 のとき）
        float c0 = 0.0f;             // いつもある中域の損失（1/秒、sizeRef で）
        float lfLoss = 0.0f, lfHz = 500.0f;   // 低域だけの損失（HALL）
        float lpMax0 = 1.65f, lpMaxPerDamp = 0.08f;   // LOWPASS より上の残響時間の上限 = 1 / (lpMax0 - lpMaxPerDamp × DAMPING) 秒
        float knee = 4.0f, capSizePow = 0.9f, lpQ = 0.6f;           // 残響時間と上限の「やわらかい小さいほう」の曲がり具合
        float airC = 0.73f, airHz = 20500.0f;   // 空気の吸収（1 回あたり一定。sizeRef で 1/秒）
        float balMax = 0.8f, balPow = 1.3f;
        float onsetMs = 0.0f, onsetPerSize = 0.18f;
        float attackCurve = 1.0f, attackFloor = 0.3f;
        float beta = 1.0f;           // 左右の出力の作り方（1 = 相関 0、大きいほど左右が逆相寄り）
        float sameSide = 0.25f;      // 入力と同じ側に先に出す量
        float apG = 0.55f;
        float modMaxMs = 1.6f;
        float levelDb = 0.0f;
        float inDiffG = 0.7f;
        float spanPerSize = 0.0f;    // ATTACK のタップの広がりに SIZE ぶん足す（ms / SIZE）
        float outHp = 0.0f, outLp = 0.0f;   // 出力の決まった色づけ（0 = なし）
        float shelfHz = 1500.0f, shelfDb = 0.0f;   // 出力の高域シェルフ
        float gainPow = 0.33f;       // 音量 = 1 / 中域の残響時間^gainPow
        float crossSide = 0.0f;      // 反対側に先に出す量
        float lpStaticDb = 0.0f;     // LOWPASS の周波数より上を出力でも少し下げる（dB）
        float sizeGainPow = 0.2f;    // SIZE が大きいと音量を少し上げる（測定どおり大きさがそろう）
        float onsetPerAttack = 0.045f;
        float crossNeg = 0.0f;       // 1 = 反対側の先出しを同じ側の逆相にする（左右が逆相寄りの HALL 用）
    } tune;

    void setMode (int m)
    {
        mode = m;
        tune = Tune();
        if (m == 0)
        {
            tune.spanPerSize = 2.0f; tune.outHp = 170.0f; tune.attackFloor = 1.0f; tune.attackCurve = 0.0f; tune.onsetPerSize = 0.05f; tune.lpStaticDb = -1.6f;
            tune.rtMul = 1.32f; tune.lpMax0 = 2.0f; tune.lpMaxPerDamp = 0.1f; tune.shelfDb = -1.5f;
            tune.sameSide = 0.35f; tune.crossSide = 0.15f; tune.levelDb = 4.0f; tune.gainPow = 0.27f;
        }
        if (m == 1)   // HALL
        {
            tune.msPerSize = 3.84f; tune.sizeRef = 11.0f; tune.c0 = 0.0f; tune.lfLoss = 0.0f;
            tune.beta = 0.95f; tune.sameSide = 0.3f; tune.crossSide = 0.3f; tune.onsetMs = 0.5f; tune.onsetPerSize = 1.0f; tune.onsetPerAttack = 0.05f;
            tune.attackCurve = 1.6f; tune.attackFloor = 0.25f; tune.levelDb = 3.0f; tune.balMax = 0.5f; tune.gainPow = 0.13f; tune.rtMul = 1.25f;
            tune.crossNeg = 1.0f; tune.shelfDb = -1.0f; tune.shelfHz = 4000.0f;
        }
    }

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = (float) sampleRate;
        static const float base[N] = { 0.613f, 0.671f, 0.733f, 0.787f, 0.839f, 0.893f, 0.947f, 0.991f,
                                       1.031f, 1.087f, 1.129f, 1.181f, 1.237f, 1.283f, 1.339f, 1.399f };
        Rng r (seed * 977u + 13u);
        int order[N]; for (int i = 0; i < N; ++i) order[i] = i;
        for (int i = N - 1; i > 0; --i) { int j = (int) (r.uni() * (float) (i + 1)) % (i + 1); std::swap (order[i], order[j]); }
        const float maxMs = 40.0f * 3.84f * 1.45f + tune.modMaxMs + 10.0f;
        for (int i = 0; i < N; ++i)
        {
            rel[i] = base[order[i]] * (1.0f + 0.01f * r.next());
            line[i].init ((int) (maxMs * 0.001f * sr));
            apRel[i] = 0.06f + 0.05f * r.uni();
            ap[i].init ((int) (0.2f * 40.0f * 3.84f * 0.001f * sr) + 8);
            inSignL[i] = r.next() > 0 ? 1.0f : -1.0f; inSignR[i] = (i & 1) ? inSignL[i] : -inSignL[i];
            o1[i] = r.next() > 0 ? 1.0f : -1.0f; o2[i] = (i & 2) ? o1[i] : -o1[i];
            rnd[i].seed (seed * 31u + (uint32_t) i * 7u + 1u);
            lfoPh[i] = r.uni(); lfoMul[i] = 0.8f + 0.4f * r.uni();
        }
        static const float difMs[4] = { 1.31f, 2.11f, 3.43f, 5.03f };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) { inDiff[c][i].init ((int) (difMs[i] * 3.0f * 0.001f * sr)); difLen[c][i] = difMs[i] * (c ? 1.09f : 1.0f); }
        for (int k = 0; k < kTaps; ++k) { tapJit[k] = r.uni(); tapSign[k] = r.next() > 0 ? 1.0f : -1.0f; }
        bloom[0].init ((int) (0.3f * sr)); bloom[1].init ((int) (0.3f * sr));
        aStep = 1.0f - std::exp (-32.0f / (0.03f * sr));
        aSmooth = 1.0f - std::exp (-1.0f / (0.02f * sr));
        tFadeInc = 1.0f / (0.02f * sr);
        applied = false; subCount = 0;
        reset();
        curLen = -1;
    }
    void reset()
    {
        for (int i = 0; i < N; ++i) { line[i].clear(); ap[i].clear(); s1[i].reset(); s2[i].reset(); s3[i].reset(); s4[i].reset(); apState[i] = 0; }
        for (auto& c : inDiff) for (auto& a : c) a.clear();
        bloom[0].clear(); bloom[1].clear();
        for (auto& f : colHp) f.reset();
        for (auto& f : colLp) f.reset();
        for (auto& f : colSh) f.reset();
        inGain = 1;
        modS = modSamples; incS = modInc; outGS = outGain; tFade = 1.0f; tPend = false;
    }

    // つまみの目標（Engine から毎ブロック）。TIME・SIZE・LOWPASS などは process の中で 32 サンプルごとに 30 ms でなめらかに近づける
    // （ブロックごとに一気に変えるとジジッと鳴っていた）。FREEZE・揺れの種類の切り替えはすぐ
    // now = true: なめらかにせず、すぐその値にする（prepareToPlay・開き直したとき）
    void setParams (const TankParams& p, bool now = false)
    {
        tgtP = p;
        if (now && applied) { applied = false; tFade = 1.0f; tPend = false; }   // 跳ね返りの並びも、混ぜ替えずにすぐ
        if (! applied) { curP = p; applyParams (curP); modS = modSamples; incS = modInc; outGS = outGain; applied = true; return; }
        if (p.freeze != curP.freeze || p.modType != curP.modType) { curP.freeze = p.freeze; curP.modType = p.modType; applyParams (curP); }
    }
    const TankParams& params() const { return P; }

private:
    // 目標へ 1 歩（32 サンプルぶん）近づける。動いたら true
    bool stepParams()
    {
        const float a = aStep;
        bool moved = false;
        auto lin = [&] (float& v, float t, float eps) { if (v == t) return; const float nv = v + (t - v) * a; v = (nv == v || std::abs (t - nv) < eps) ? t : nv; moved = true; };
        auto lg = [&] (float& v, float t, float lo) { if (v == t) return; const float lv = std::log (std::max (lo, v)), lt = std::log (std::max (lo, t));
                                                     const float nl = lv + (lt - lv) * a; v = (nl == lv || std::abs (lt - nl) < 1.0e-4f) ? t : std::exp (nl); moved = true; };
        lg (curP.timeSec, tgtP.timeSec, 0.01f); lin (curP.size, tgtP.size, 1.0e-3f); lin (curP.diffusion, tgtP.diffusion, 1.0e-3f);
        lin (curP.attackMs, tgtP.attackMs, 1.0e-3f); lg (curP.lowpassHz, tgtP.lowpassHz, 10.0f); lin (curP.damping, tgtP.damping, 1.0e-4f);
        lin (curP.balance, tgtP.balance, 1.0e-5f); lg (curP.crossoverHz, tgtP.crossoverHz, 10.0f);
        lg (curP.modRate, tgtP.modRate, 0.01f); lin (curP.modDepth, tgtP.modDepth, 1.0e-3f);
        return moved;
    }

    void applyParams (const TankParams& p)
    {
        P = p;
        const float size = std::clamp (p.size, 4.0f, 40.0f);
        meanMs = tune.msPerSize * size;
        const float T = std::clamp (p.timeSec, 0.02f, 24.0f);
        const float rtMid = tune.rtMul * T;
        const float dampC = std::max (0.05f, tune.lpMax0 - tune.lpMaxPerDamp * std::clamp (p.damping, 0.0f, 10.0f));
        const float u = std::abs (2.0f * std::clamp (p.balance, 0.0f, 1.0f) - 1.0f);
        const float balC = tune.balMax * std::pow (u, tune.balPow);
        // 上限は SIZE が大きいほど長く、LOWPASS が高いほど効きが弱い（20 kHz でなし）
        const float lpFac = std::max (0.0f, 1.0f - std::clamp (p.lowpassHz, 30.0f, 20000.0f) / 20000.0f) / (1.0f - 4016.0f / 20000.0f);
        const float rtCap = std::pow (size / tune.sizeRef, tune.capSizePow) / std::max (1e-4f, dampC * lpFac);
        const float rtHf = std::pow (std::pow (rtMid, -tune.knee) + std::pow (rtCap, -tune.knee), -1.0f / tune.knee);
        const float capC = std::max (0.0f, 1.0f / rtHf - 1.0f / rtMid);
        for (int i = 0; i < N; ++i)
        {
            const float dsec = (rel[i] * meanMs) * 0.001f;
            const float dref = (rel[i] * tune.msPerSize * tune.sizeRef) * 0.001f;   // 「1 回あたり一定の損失」はこの長さで決めた値
            lenT[i] = rel[i] * meanMs * 0.001f * sr;
            const float midDb = p.freeze ? 0.0f : 60.0f * dsec / rtMid + 60.0f * dref * tune.c0;
            g[i] = std::pow (10.0f, -midDb / 20.0f);
            // 高域 1: 空気の吸収（1 回あたり一定の損失）
            s1[i].highShelf (p.freeze ? 1.0f : std::pow (10.0f, -tune.airC * 60.0f * dref / 20.0f), tune.airHz, sr);
            // 高域 2: LOWPASS より上は、残響時間が「上限」を超えないように（DAMPING で上限が変わる）
            s4[i].highShelf (p.freeze ? 1.0f : std::pow (10.0f, -capC * 60.0f * dsec / 20.0f), p.lowpassHz, sr);
            // BALANCE: CROSSOVER より下（>0.5）か上（<0.5）だけ損失を足す
            const float bDb = p.freeze ? 0.0f : balC * 60.0f * dref;
            if (p.balance > 0.5f) s2[i].lowShelf (std::pow (10.0f, -bDb / 20.0f), p.crossoverHz, sr);
            else s2[i].highShelf (std::pow (10.0f, -bDb / 20.0f), p.crossoverHz, sr);
            // HALL の低域の損失
            if (tune.lfLoss > 0 && ! p.freeze) s3[i].lowShelf (std::pow (10.0f, -tune.lfLoss * 60.0f * dref / 20.0f), tune.lfHz, sr); else s3[i].flat();
            ap[i].set ((int) (apRel[i] * rel[i] * meanMs * 0.001f * sr), tune.apG);
        }
        if (curLen < 0) for (int i = 0; i < N; ++i) len[i] = lenT[i];
        curLen = 1;
        // 入力の拡散
        const float d = std::clamp (p.diffusion / 100.0f, 0.0f, 1.0f);
        const float sc = std::pow (size / tune.sizeRef, 0.5f);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) inDiff[c][i].set ((int) (difLen[c][i] * sc * 0.001f * sr), tune.inDiffG * d);
        // 立ち上がり（ATTACK）: 入力を少しずつずらしたタップで流し込む。
        // タップの位置が変わったら、前の並びから 20 ms で混ぜ替える（混ぜ替えの途中で次が来たら、終わってから次へ）
        const float onset = tune.onsetMs + tune.onsetPerSize * size + tune.onsetPerAttack * std::clamp (p.attackMs, 0.0f, 250.0f);
        Taps nt;
        double e = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const float x = k == 0 ? 0.0f : ((float) k + tapJit[k]) / (float) kTaps;
            const float span = std::clamp (p.attackMs, 0.0f, 250.0f) + tune.spanPerSize * size;
            nt.pos[k] = std::clamp ((int) ((onset + span * x) * 0.001f * sr), 0, (int) (0.29f * sr));
            nt.gain[k] = tapSign[k] * (tune.attackFloor + std::pow (x, tune.attackCurve));
            e += nt.gain[k] * nt.gain[k];
        }
        for (auto& t : nt.gain) t /= (float) std::sqrt (e);
        if (! applied) { tCur = nt; tFade = 1.0f; tPend = false; }
        else if (! (nt == tCur))
        {
            if (tFade >= 1.0f) { tOld = tCur; tCur = nt; tFade = 0.0f; tPend = false; }
            else { tNext = nt; tPend = true; }
        }
        else tPend = false;
        // 揺れ
        // 揺れの深さ（測定: 速さを上げると音程の揺れは速さの平方根くらいで増える）
        modSamples = std::clamp (p.modDepth / 100.0f, 0.0f, 1.0f) * tune.modMaxMs * 0.001f * sr / std::sqrt (std::clamp (p.modRate, 0.05f, 10.0f) / 0.48f);
        modInc = std::clamp (p.modRate, 0.05f, 10.0f) / sr;
        // 音量: 残響時間によらず全体の大きさがそろうように（測定どおり）
        outGain = std::pow (10.0f, tune.levelDb / 20.0f) * std::pow (size / tune.sizeRef, tune.sizeGainPow) / std::pow (std::max (0.05f, p.freeze ? 24.0f * tune.rtMul : rtMid), tune.gainPow);
        const float b = tune.beta;
        outA = 1.0f / std::sqrt (1.0f + b * b); outB = b * outA;
        for (auto& f : colHp) { if (tune.outHp > 0) f.hp1 (tune.outHp, sr); else f.flat(); }
        for (auto& f : colLp)
        {
            const float sd = tune.lpStaticDb * (0.35f + 0.65f * lpFac * (1.0f - 4016.0f / 20000.0f));
            if (std::abs (sd) > 0.01f) f.highShelf (std::min (p.lowpassHz, 0.45f * sr), 0.6, sd, sr); else f.flat();
        }
        for (auto& f : colSh) { if (std::abs (tune.shelfDb) > 0.01f) f.highShelf (tune.shelfHz, 0.6, tune.shelfDb, sr); else f.flat(); }
    }

public:
    inline void process (float inL, float inR, float& outL, float& outR)
    {
        if (--subCount <= 0) { subCount = 32; if (stepParams()) applyParams (curP); }
        // 揺れの深さ・速さと音量は 1 サンプルずつ（深さが一気に変わると読む位置が跳んでプチッと鳴る）
        modS += (modSamples - modS) * aSmooth; if (std::abs (modSamples - modS) < 1.0e-5f) modS = modSamples;
        incS += (modInc - incS) * aSmooth; if (std::abs (modInc - incS) < 1.0e-9f) incS = modInc;
        outGS += (outGain - outGS) * aSmooth; if (std::abs (outGain - outGS) < 1.0e-7f) outGS = outGain;
        inGain += ((P.freeze ? 0.0f : 1.0f) - inGain) * 0.001f;
        inL *= inGain; inR *= inGain;
        for (auto& a : inDiff[0]) inL = a.process (inL);
        for (auto& a : inDiff[1]) inR = a.process (inR);
        bloom[0].push (inL); bloom[1].push (inR);
        float feed[N] = {};
        float earlyL = 0, earlyR = 0, crossL = 0, crossR = 0;
        auto addTaps = [&] (const Taps& t, float w)
        {
            for (int k = 0; k < kTaps; ++k)
            {
                const float gk = t.gain[k] * w;
                const float vl = bloom[0].at (t.pos[k]) * gk, vr = bloom[1].at (t.pos[k]) * gk;
                const int i = k % N;
                feed[i] += inSignL[i] * vl + inSignR[i] * vr;
                earlyL += vl; earlyR += vr;
                const float alt = (k & 1) ? 1.0f : -1.0f;   // 反対側は符号の並びを変えて、同じ側と似ないように
                crossL += (tune.crossNeg > 0.5f ? -1.0f : alt) * vl; crossR += (tune.crossNeg > 0.5f ? -1.0f : alt) * vr;
            }
        };
        if (tFade < 1.0f)
        {
            const float w = tFade * tFade * (3.0f - 2.0f * tFade);
            addTaps (tCur, w); addTaps (tOld, 1.0f - w);
            tFade += tFadeInc;
            if (tFade >= 1.0f) { tFade = 1.0f; if (tPend) { tOld = tCur; tCur = tNext; tFade = 0.0f; tPend = false; } }
        }
        else addTaps (tCur, 1.0f);
        float y[N], u = 0, v = 0;
        const bool pitch = P.modType == 1;
        for (int i = 0; i < N; ++i)
        {
            len[i] += (lenT[i] - len[i]) * 0.0005f;
            float d = len[i];
            if (modS > 0.0f || modSamples > 0.0f)
            {
                float m;
                if (pitch) { lfoPh[i] += incS * lfoMul[i]; if (lfoPh[i] >= 1.0f) lfoPh[i] -= 1.0f; m = 0.5f + 0.5f * std::sin (2.0f * (float) kPi * lfoPh[i]); }
                else m = 0.5f + 0.5f * rnd[i].next (incS * lfoMul[i] * 2.0f);
                d += modS * m;
            }
            d = std::max (2.0f, d);
            // オールパス補間（揺らしても高域が痩せない）
            const int ip = (int) (d - 0.3f);
            const float f = d - (float) ip, eta = (1.0f - f) / (1.0f + f);
            const float u0 = line[i].at (ip - 1), u1 = line[i].at (ip);
            y[i] = eta * u0 + u1 - eta * apState[i];
            apState[i] = y[i];
            u += o1[i] * y[i]; v += o2[i] * y[i];
        }
        for (int i = 0; i < N; ++i)
            y[i] = ap[i].process (s4[i].process (s3[i].process (s2[i].process (s1[i].process (y[i]))))) * g[i];
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        for (int i = 0; i < N; ++i) line[i].push (y[i] * 0.25f + feed[i]);
        u *= 0.25f; v *= 0.25f;
        outL = colSh[0].process (colLp[0].process (colHp[0].process ((outA * u + outB * v + tune.sameSide * earlyL + tune.crossSide * crossR) * outGS)));
        outR = colSh[1].process (colLp[1].process (colHp[1].process ((outA * u - outB * v + tune.sameSide * earlyR + tune.crossSide * crossL) * outGS)));
    }

private:
    static constexpr int kTaps = 16;
    struct Taps
    {
        int pos[kTaps] {}; float gain[kTaps] {};
        bool operator== (const Taps& o) const
        { for (int k = 0; k < kTaps; ++k) if (pos[k] != o.pos[k] || gain[k] != o.gain[k]) return false; return true; }
    };
    Taps tCur, tOld, tNext; float tFade = 1.0f, tFadeInc = 0.001f; bool tPend = false;
    TankParams tgtP, curP; bool applied = false; int subCount = 0;
    float aStep = 0.02f, aSmooth = 0.001f;                // 32 サンプルぶんの 30 ms・1 サンプルぶんの 20 ms
    float modS = 0, incS = 0, outGS = 1;                  // なめらかにした揺れの深さ・速さ・音量
    int mode = 0;
    float sr = 48000;
    TankParams P;
    float meanMs = 20;
    Delay line[N]; Allpass ap[N]; Shelf1 s1[N], s2[N], s3[N], s4[N];
    float rel[N] {}, apRel[N] {}, len[N] {}, lenT[N] {}, g[N] {}, apState[N] {}, inSignL[N] {}, inSignR[N] {}, o1[N] {}, o2[N] {};
    SmoothRandom rnd[N]; float lfoPh[N] {}, lfoMul[N] {};
    Allpass inDiff[2][4]; float difLen[2][4] {};
    Delay bloom[2];
    Biquad colHp[2], colLp[2], colSh[2];
    float tapJit[kTaps] {}, tapSign[kTaps] {};
    float modSamples = 0, modInc = 0, outGain = 1, outA = 1, outB = 0, inGain = 1;
    int curLen = -1;
};

//==============================================================================
struct Params
{
    float mix = 50;               // 0..100 %
    float outputDb = 0;           // -60..10
    float levelDb[3] = { -3, -3, -3 };   // EARLY / PLATE / HALL（-70 = 消す）
    // 響きごとの「形」: 出だしの遅れ（PRE-DELAY に足す）、低音・高音カット、広がり。標準は何もしない
    struct Voice
    {
        float delayMs = 0, lowCutHz = 20, highCutHz = 20000, width = 100;
        bool operator== (const Voice& o) const { return delayMs == o.delayMs && lowCutHz == o.lowCutHz && highCutHz == o.highCutHz && width == o.width; }
        bool operator!= (const Voice& o) const { return ! (*this == o); }
    };
    Voice voice[3];
    EarlyParams early;
    TankParams plate, hall;
    float modRate = 0.48f, modDepth = 25; int modType = 0;
    float predelayMs = 20;
    EqSettings preEq, revEq;
    bool smoothOn = false; float smoothAmount = 100;   // 0..100
    bool freeze = false;
};

inline Params defaultParams()
{
    Params p;
    p.plate.timeSec = 1.1f; p.plate.size = 7.44f; p.plate.diffusion = 39.6f; p.plate.attackMs = 24.6f; p.plate.lowpassHz = 4016;
    p.plate.damping = 4; p.plate.balance = 0.41f; p.plate.crossoverHz = 2678;
    return p;
}

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        early.prepare (sampleRate); eBuilt = false;
        aSub = (float) (1.0 - std::exp (-(double) kSubStep / (0.03 * sampleRate))); aOnOff = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        plate.setMode (0); hall.setMode (1);
        plate.prepare (sampleRate, 1); hall.prepare (sampleRate, 2);
        smoother.prepare (sampleRate);
        pre[0].init ((int) (1.56 * sampleRate)); pre[1].init ((int) (1.56 * sampleRate));
        set (params, true);
        reset();
    }
    void reset()
    {
        early.reset(); plate.reset(); hall.reset(); smoother.reset();
        pre[0].clear(); pre[1].clear();
        for (auto& e : preEqF) e.reset();
        for (auto& e : revEqF) e.reset();
        for (auto& v : vf) for (auto& b : v) b.reset();
    }
    // つまみの変化は、ブロックごとに一気に変えずになめらかに追う（2026-11-12、健康診断でオートメーションのジジッが出たため）:
    //  ・響きごとの LOW CUT / HIGH CUT と PRE / REVERB EQ … 4 サンプルごとに 30 ms で近づける（周波数は対数）
    //  ・EARLY … Early の中で近づける（にじみの長さは小数で少しずつ、跳ね返りの並びは 30 ms の混ぜ替え）
    //  ・PLATE / HALL … Tank の中で 32 サンプルごとに近づける
    void set (const Params& p, bool force = false)
    {
        params = p;
        for (int j = 0; j < 3; ++j)
        {
            const auto& v = p.voice[j];
            vLcHz[j] = std::clamp (v.lowCutHz, 20.0f, 20000.0f); vHcHz[j] = std::clamp (v.highCutHz, 20.0f, 20000.0f);
            vLcT[j] = std::log (vLcHz[j]);    // 20 Hz 以下 = OFF
            vHcT[j] = std::log (vHcHz[j]);    // 19.9 kHz 以上 = OFF
            if (force) { vLc[j] = vLcT[j]; vHc[j] = vHcT[j]; setVoiceFilters (j); }
            tgtVDelay[j] = (float) (std::clamp (v.delayMs, 0.0f, 500.0f) * 0.001 * sr);
            tgtWidth[j] = std::clamp (v.width, 0.0f, 200.0f) / 100.0f;
        }
        early.setParams (p.early, force || ! eBuilt); eBuilt = true;
        TankParams a = p.plate, b = p.hall;
        a.modRate = b.modRate = p.modRate; a.modDepth = b.modDepth = p.modDepth; a.modType = b.modType = p.modType;
        a.freeze = b.freeze = p.freeze;
        plate.setParams (a, force); hall.setParams (b, force);
        for (int k = 0; k < 2; ++k)
        {
            const EqSettings& t = k == 0 ? p.preEq : p.revEq;
            eqTgt[k] = t;
            // 形が変わる所（ON / OFF、シェルフ⇄パス、パスの傾きの段）はすぐ切り替える
            const bool jump = force || t.on != eqCur[k].on || t.lowShape != eqCur[k].lowShape || t.highShape != eqCur[k].highShape
                           || (t.lowShape == 1 && TriEQ::passOrder (t.lowSlope) != TriEQ::passOrder (eqCur[k].lowSlope))
                           || (t.highShape == 1 && TriEQ::passOrder (t.highSlope) != TriEQ::passOrder (eqCur[k].highSlope));
            if (jump) { eqCur[k] = t; setupEq (k); }
        }
        for (int i = 0; i < 3; ++i) tgtLevel[i] = p.levelDb[i] <= -69.9f ? 0.0f : std::pow (10.0f, p.levelDb[i] / 20.0f);
        tgtMix = std::clamp (p.mix / 100.0f, 0.0f, 1.0f);
        tgtOut = std::pow (10.0f, std::clamp (p.outputDb, -60.0f, 10.0f) / 20.0f);
        tgtPre = (float) (std::clamp (p.predelayMs, 0.0f, 1000.0f) * 0.001 * sr);
        if (force) for (int i = 0; i < 3; ++i) { hpMix[i] = hpOn[i] ? 1.0f : 0.0f; lpMix[i] = lpOn[i] ? 1.0f : 0.0f; }
        if (force) { for (int i = 0; i < 3; ++i) { level[i] = tgtLevel[i]; vDelay[i] = tgtVDelay[i]; widthV[i] = tgtWidth[i]; } mixV = tgtMix; outV = tgtOut; preV = tgtPre; }
    }
    const Params& get() const { return params; }
    void setDuck (float gain) { tgtDuck = gain; }   // Plugin Link: 歌っている間の響きの倍率

    void setVoiceFilters (int j)
    {
        hpOn[j] = vLc[j] > std::log (20.5f); lpOn[j] = vHc[j] < std::log (19900.0f);
        for (int c = 0; c < 2; ++c)
        {
            // 着いたら目標の周波数そのもの（前とまったく同じ係数）
            vf[j][c].hp (vLc[j] == vLcT[j] ? vLcHz[j] : std::exp (vLc[j]), 0.7071, sr);
            vf[j][c + 2].lp (vHc[j] == vHcT[j] ? vHcHz[j] : std::exp (vHc[j]), 0.7071, sr);
        }
    }
    void setupEq (int k) { for (auto& e : (k == 0 ? preEqF : revEqF)) e.setup (eqCur[k], sr); }
    // kSubStep サンプルごとに、響きごとのカットと EQ を目標へ 1 歩近づける
    void stepSmooth()
    {
        const float a = aSub;
        for (int j = 0; j < 3; ++j)
        {
            if (vLc[j] == vLcT[j] && vHc[j] == vHcT[j]) continue;
            { const float nv = vLc[j] + (vLcT[j] - vLc[j]) * a; vLc[j] = (nv == vLc[j] || std::abs (vLcT[j] - nv) < 1.0e-4f) ? vLcT[j] : nv; }
            { const float nv = vHc[j] + (vHcT[j] - vHc[j]) * a; vHc[j] = (nv == vHc[j] || std::abs (vHcT[j] - nv) < 1.0e-4f) ? vHcT[j] : nv; }
            setVoiceFilters (j);
        }
        for (int k = 0; k < 2; ++k)
        {
            EqSettings& c = eqCur[k]; const EqSettings& t = eqTgt[k];
            if (c == t) continue;
            auto lin = [&] (float& v, float tv, float eps) { const float nv = v + (tv - v) * a; v = (nv == v || std::abs (tv - nv) < eps) ? tv : nv; };
            auto lg = [&] (float& v, float tv) { const float lv = std::log (std::max (1.0f, v)), lt = std::log (std::max (1.0f, tv));
                                                 const float nl = lv + (lt - lv) * a; v = (nl == lv || std::abs (lt - nl) < 1.0e-4f) ? tv : std::exp (nl); };
            lin (c.lowGain, t.lowGain, 1.0e-3f); lin (c.highGain, t.highGain, 1.0e-3f); lin (c.bellGain, t.bellGain, 1.0e-3f);
            lg (c.lowFreq, t.lowFreq); lg (c.highFreq, t.highFreq); lg (c.bellFreq, t.bellFreq);
            lin (c.lowSlope, t.lowSlope, 1.0e-4f); lin (c.highSlope, t.highSlope, 1.0e-4f); lin (c.bellQ, t.bellQ, 1.0e-4f);
            setupEq (k);
        }
    }

    // 1 ブロック分。wetMono / dryMono は画面表示用（nullptr なら使わない）
    void process (float* L, float* R, int n, float* dryMono = nullptr, float* wetMono = nullptr)
    {
        const float k = 0.0015f;
        for (int i = 0; i < n; ++i)
        {
            if (--subCount <= 0) { subCount = kSubStep; stepSmooth(); }
            for (int j = 0; j < 3; ++j)
            {
                level[j] += (tgtLevel[j] - level[j]) * k;
                vDelay[j] += (tgtVDelay[j] - vDelay[j]) * 0.0008f;
                widthV[j] += (tgtWidth[j] - widthV[j]) * k;
            }
            mixV += (tgtMix - mixV) * k; outV += (tgtOut - outV) * k; duckV += (tgtDuck - duckV) * k;
            preV += (tgtPre - preV) * 0.0008f;
            const float dl = L[i], dr = R[i];
            float xl = dl, xr = dr;
            xl = preEqF[0].process (xl); xr = preEqF[1].process (xr);
            float preRead = preV;
            if (params.smoothOn)
            {
                smoother.process (xl, xr, std::clamp (params.smoothAmount / 100.0f, 0.0f, 1.0f));
                preRead -= (float) smoother.latency();   // SMOOTHING の 1 ms をプリディレイから引く
            }
            pre[0].push (xl); pre[1].push (xr);
            float wl = 0, wr = 0, a, b;
            for (int j = 0; j < 3; ++j)
            {
                if (! (level[j] > 1e-5f || tgtLevel[j] > 0)) continue;
                // その響きの入口: PRE-DELAY ＋ その響きだけの DELAY
                const float d = preRead + vDelay[j];
                float il = xl, ir = xr;
                if (d >= 0.5f) { il = pre[0].frac (d); ir = pre[1].frac (d); }
                if (j == 0) early.process (il, ir, a, b);
                else if (j == 1) plate.process (il, ir, a, b);
                else hall.process (il, ir, a, b);
                // その響きだけの音色（LOW CUT / HIGH CUT、2 次）と広がり（M/S）
                { const float t = hpOn[j] ? 1.0f : 0.0f, nv = hpMix[j] + (t - hpMix[j]) * aOnOff; hpMix[j] = (nv == hpMix[j] || std::abs (t - nv) < 1.0e-6f) ? t : nv; }
                { const float t = lpOn[j] ? 1.0f : 0.0f, nv = lpMix[j] + (t - lpMix[j]) * aOnOff; lpMix[j] = (nv == lpMix[j] || std::abs (t - nv) < 1.0e-6f) ? t : nv; }
                { const float ha = vf[j][0].process (a), hb = vf[j][1].process (b); a += hpMix[j] * (ha - a); b += hpMix[j] * (hb - b); }
                { const float la = vf[j][2].process (a), lb = vf[j][3].process (b); a += lpMix[j] * (la - a); b += lpMix[j] * (lb - b); }
                if (std::abs (widthV[j] - 1.0f) > 1e-4f)
                {
                    const float m = 0.5f * (a + b), sd = 0.5f * (a - b) * widthV[j];
                    a = m + sd; b = m - sd;
                }
                wl += level[j] * a; wr += level[j] * b;
            }
            if (dryMono) dryMono[i] = 0.5f * (dl + dr);
            if (wetMono) wetMono[i] = 0.5f * (wl + wr);    // REVERB EQ の前（UNMASK の学習用）
            wl = revEqF[0].process (wl); wr = revEqF[1].process (wr);
            L[i] = outV * ((1.0f - mixV) * dl + mixV * duckV * wl);
            R[i] = outV * ((1.0f - mixV) * dr + mixV * duckV * wr);
        }
    }

    Early& earlyEngine() { return early; }
    Tank& plateEngine() { return plate; }
    Tank& hallEngine() { return hall; }
    SendSmoother& smoothing() { return smoother; }
    double magPre (double f) const { return preEqF[0].magDb (f, sr); }
    double magRev (double f) const { return revEqF[0].magDb (f, sr); }

private:
    double sr = 48000;
    Params params = defaultParams();
    Early early; Tank plate, hall;
    bool eBuilt = false;
    static constexpr int kSubStep = 4;   // カットと EQ を近づける間隔（サンプル）
    float vLc[3] { 3.0f, 3.0f, 3.0f }, vLcT[3] { 3.0f, 3.0f, 3.0f }, vHc[3] { 9.9f, 9.9f, 9.9f }, vHcT[3] { 9.9f, 9.9f, 9.9f };   // 響きごとのカット（log Hz）
    float vLcHz[3] { 20, 20, 20 }, vHcHz[3] { 20000, 20000, 20000 };   // その目標の周波数そのもの
    EqSettings eqCur[2], eqTgt[2];       // PRE EQ / REVERB EQ の今の値と目標
    float aSub = 0.01f; int subCount = 0;
    SendSmoother smoother;
    TriEQ preEqF[2], revEqF[2];
    Delay pre[2];
    float duckV = 1.0f, tgtDuck = 1.0f;
    Biquad vf[3][4];                     // 響きごと: [0,1] = LOW CUT の L/R、[2,3] = HIGH CUT の L/R
    bool hpOn[3] {}, lpOn[3] {}; float hpMix[3] {}, lpMix[3] {}; float aOnOff = 1.0f - std::exp (-1.0f / (0.02f * 48000.0f));
    float vDelay[3] {}, tgtVDelay[3] {}, widthV[3] { 1, 1, 1 }, tgtWidth[3] { 1, 1, 1 };
    float level[3] {}, tgtLevel[3] {}, mixV = 0.5f, tgtMix = 0.5f, outV = 1, tgtOut = 1, preV = 0, tgtPre = 0;
};
} // namespace meld
