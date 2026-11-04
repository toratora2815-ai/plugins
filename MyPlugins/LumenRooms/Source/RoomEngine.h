#pragma once
// Lumen Rooms — 部屋の響きエンジン（JUCE に依存しない素の C++。テスト用の小さなプログラムからも使う）
//
// 構成
//   入力 ─┬─ 初期反射（12 個の短いノイズの粒を並べたインパルス応答。畳み込みは呼び出し側）
//         └─ テール（左右で別々の 16 本ディレイ FDN。反対側へはクロスフィードで流し込む）
//   → 反射とテールのバランス → WIDTH（M/S）→ ポスト EQ → ドライと混ぜる
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <complex>

namespace lumen
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// パラメーター（すべて実際の単位）
struct ReflParams
{
    int    pattern      = 0;     // 0 Natural, 1 Uniform, 2 Nonlinear U, 3 Dissipate, 4 Gather, 5 Cluster 1, 6 Cluster 2
    float  size         = 0.5f;  // 0..1
    int    proximity    = 2;     // 0..10（一番大きい粒の位置）
    float  reflectivity = 0.5f;  // 0..1（後ろの粒の減り方）
    float  diffusion    = 5.0f;  // 0..10
    float  rolloffHz    = 6000.0f;
    int    rolloffSlope = 1;     // 1..4 = 6/12/18/24 dB/oct
    float  xfeedDb      = 0.0f;  // 反対側の粒の量
    float  xfeedMs      = 0.0f;  // 反対側の粒の遅れ
    float  xfeedRollHz  = 18000.0f;
};

struct TailParams
{
    float timeSec      = 2.25f;  // 中域の残響時間 RT60（>= 60 で無限）
    float bloom        = 2.5f;   // 0..10 立ち上がりのふくらみ
    float bassFreq     = 290.0f, bassMult = 1.0f;
    float trebFreq     = 4300.0f, trebMult = 1.0f;
    float lowBoostDb   = -100.0f; // -100 = Off
    float lowBoostWeight = 0.5f;  // 0 Tight .. 1 Heavy
    float predelayMs   = 10.0f;
    float echoDb       = -100.0f, echoMs = 100.0f, echoRollHz = 18000.0f;
    float rolloffHz    = 6000.0f;
    int   rolloffSlope = 2;
    float lowCutHz     = 20.0f;
    float undDepth     = 0.25f, undShape = 0.25f; // 0..1
    float modDepth     = 0.75f, modRateHz = 0.7f;  // depth 0..1
    float xfeedDb      = -3.0f, xfeedMs = 10.0f, xfeedRollHz = 18000.0f;
    int   seed         = 0;
};

//==============================================================================
// 初期反射の表（size 0.5 のときの時刻 ms、並びは 自分側0, 反対側0, 自分側1, 反対側1 …）
inline const float* patternTimes (int p)
{
    static const float t[7][12] = {
        { 14, 26, 36, 52, 66, 79, 98, 118, 133, 150, 163, 183 },              // Natural
        { 15, 35, 55, 75, 95, 115, 135, 155, 175, 195, 215, 235 },            // Uniform
        { 15, 29, 43, 57, 71, 85, 99, 113, 127, 141, 155, 169 },              // Nonlinear U
        { 15, 28.4f, 47.1f, 70.1f, 97.2f, 128, 162.6f, 200.6f, 242, 286.7f, 334.7f, 385.8f }, // Dissipate
        { 15, 66, 114, 158, 200, 238, 272, 303, 330, 353, 372, 385 },         // Gather
        { 15, 27, 54, 65, 88, 104, 116, 131, 139, 148, 162, 174 },            // Cluster 1
        { 15, 27, 51, 72, 88, 104, 126, 128, 184, 204, 224, 252 } };          // Cluster 2
    return t[std::clamp (p, 0, 6)];
}
inline const char* patternName (int p)
{
    static const char* n[7] = { "Natural", "Uniform", "Nonlinear", "Dissipate", "Gather", "Cluster 1", "Cluster 2" };
    return n[std::clamp (p, 0, 6)];
}
inline float sizeScale (float s)
{
    return s < 0.5f ? 0.7f + 0.6f * s : 1.0f + 1.5f * (s - 0.5f);
}

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 12345u) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f * 2.0f - 1.0f; }
};

//==============================================================================
// 小さな部品
struct OnePole   // 1 次のローパス（バイリニア）
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void setLP (float fc, float sr)
    {
        fc = std::min (fc, 0.47f * sr);
        float c = 1.0f / std::tan ((float) kPi * fc / sr);
        float a0 = 1 + c;
        b0 = 1 / a0; b1 = 1 / a0; a1 = (1 - c) / a0;
    }
    inline float process (float x) { float y = b0 * x + z; z = b1 * x - a1 * y; return y; }
    void reset() { z = 0; }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    float magDb (double f, double sr) const   // 画面表示用：周波数 f での大きさ（dB）
    {
        const double w = 2 * kPi * f / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto h = ((double) b0 + (double) b1 * z1 + (double) b2 * z2) / (1.0 + (double) a1 * z1 + (double) a2 * z2);
        return (float) (20.0 * std::log10 (std::abs (h) + 1e-12));
    }
    void lp (double f, double q, double sr) { f = std::min (f, 0.47 * sr); double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void hp (double f, double q, double sr) { f = std::min (f, 0.47 * sr); double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void peak (double f, double q, double db, double sr)
    { f = std::min (f, 0.47 * sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
      set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowShelf (double f, double q, double db, double sr)
    { f = std::min (f, 0.47 * sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), r = 2 * std::sqrt (A) * al;
      set (A * ((A + 1) - (A - 1) * c + r), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - r),
           (A + 1) + (A - 1) * c + r, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - r); }
    void highShelf (double f, double q, double db, double sr)
    { f = std::min (f, 0.47 * sr); double A = std::pow (10, db / 40), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), r = 2 * std::sqrt (A) * al;
      set (A * ((A + 1) + (A - 1) * c + r), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - r),
           (A + 1) - (A - 1) * c + r, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - r); }
};

// 6/12/18/24 dB/oct の高域カット。なだらかに効くよう「1 次ローパス（マッチド z）」を 1〜4 段重ねる
struct Rolloff
{
    float p = 0, z[4] {}; int slope = 1; bool bypass = false;
    void set (float fc, int sl, float sr)
    {
        // 本物の効き方に合わせた補正（6 dB/oct は高い周波数ほど少しなだらか、急な傾きはわずかに低め）
        slope = std::clamp (sl, 1, 4);
        bypass = fc >= 0.49f * sr;
        const float khz = fc * 0.001f;
        const float k = slope == 1 ? 1.1f + 0.025f * khz : std::max (0.82f, 1.02f - 0.011f * khz);
        p = std::exp (-2.0f * (float) kPi * std::min (fc * k, 0.49f * sr) / sr);
    }
    inline float process (float x)
    {
        if (bypass) return x;
        for (int i = 0; i < slope; ++i) { z[i] = (1 - p) * x + p * z[i]; x = z[i]; }
        return x;
    }
    void reset() { for (auto& v : z) v = 0; }
    float magDb (float f, float sr) const
    {
        if (bypass) return 0.0f;
        const double w = 2 * kPi * f / sr;
        const auto h = (1.0 - p) / std::abs (1.0 - (double) p * std::polar (1.0, -w));
        return (float) (slope * 20.0 * std::log10 (h));
    }
};

// 音色の型（COLOR）用のグラフィック EQ。帯域は固定、ゲイン（dB）だけ外から入れる
struct GraphicEQ
{
    static constexpr int B = 11;
    static const float* freqs() { static const float f[B] = { 31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 11300, 16000 }; return f; }
    static constexpr float Q = 1.3f;
    Biquad b[B]; bool on = false;
    void set (const float* gainsDb, float sr)
    {
        on = gainsDb != nullptr;
        if (! on) return;
        for (int i = 0; i < B; ++i) b[i].peak (freqs()[i], Q, gainsDb[i], sr);
    }
    inline float process (float x) { if (! on) return x; for (auto& f : b) x = f.process (x); return x; }
    void reset() { for (auto& f : b) f.reset(); }
};

// 1 次シェルフ（ループ内の減衰の周波数差に使う。境目の周波数で dB が半分になる形）
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void lowShelf (float G, float fc, float sr)   // DC で G、高域で 1
    {
        fc = std::min (fc, 0.45f * sr);
        float c = 1.0f / std::tan ((float) kPi * fc / sr), g = std::sqrt (G);
        float a0 = c + 1 / g;
        b0 = (c + g) / a0; b1 = (g - c) / a0; a1 = (1 / g - c) / a0;
    }
    void highShelf (float G, float fc, float sr)  // DC で 1、高域で G
    {
        fc = std::min (fc, 0.45f * sr);
        float c = 1.0f / std::tan ((float) kPi * fc / sr), g = std::sqrt (G);
        float a0 = c + g;
        b0 = (G * c + g) / a0; b1 = (g - G * c) / a0; a1 = (g - c) / a0;
    }
    void lowpassAt (float G, float fc, float sr)  // DC で 1、fc でちょうど G になる 1 次ローパス（高域ほど下がり続ける）
    {
        G = std::clamp (G, 1e-4f, 1.0f);
        if (G > 0.99999f) { b0 = 1; b1 = 0; a1 = 0; return; }
        const float w = 2.0f * (float) kPi * std::min (fc, 0.45f * sr) / sr, G2 = G * G, cw = std::cos (w);
        const float B = 1 - G2 * cw, C = 1 - G2;
        const float a = (B - std::sqrt (std::max (0.0f, B * B - C * C))) / C;
        b0 = 1 - a; b1 = 0; a1 = -a;
    }
    inline float process (float x) { float y = b0 * x + z; z = b1 * x - a1 * y; return y; }
    void reset() { z = 0; }
};

struct Delay
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxLen) { int n = 1; while (n < maxLen + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float at (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }   // d=0 が直前に入れた値
    inline float frac (float d) const    // 3 次補間
    {
        int i = (int) d; float f = d - (float) i;
        float xm1 = at (i - 1 < 0 ? 0 : i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Allpass
{
    Delay d; int len = 1; float g = 0.5f;
    void init (int l, float gg) { len = std::max (1, l); g = gg; d.init (len + 1); }
    inline float process (float x)
    {
        float v = d.at (len - 1);
        float u = x + g * v;
        d.push (u);
        return v - g * u;
    }
    void clear() { d.clear(); }
};

//==============================================================================
// 初期反射のインパルス応答を作る（own = 入力と同じ側, cross = 反対側）
inline void buildReflections (const ReflParams& p, double sr, std::vector<float>& own, std::vector<float>& cross)
{
    static const float lenMs[12] = { 14, 12, 18, 17, 15, 20, 21, 17, 19, 15, 22, 15 };
    const float* t = patternTimes (p.pattern);
    const float a = sizeScale (p.size);
    const float post = 8.0f - 5.0f * std::clamp (p.reflectivity, 0.0f, 1.0f);
    const float pre = post * 0.5f;
    const int peak = std::clamp (p.proximity, 0, 10);
    const float xfGain = std::pow (10.0f, p.xfeedDb / 20.0f);
    const int xfDelay = (int) std::lround (p.xfeedMs * 0.001 * sr);

    const int total = (int) std::ceil (sr * (0.004 + a * (0.4 - 0.004) + 0.03 + 0.12)) + xfDelay + 64;
    own.assign ((size_t) total, 0.0f);
    cross.assign ((size_t) total, 0.0f);

    for (int k = 0; k < 12; ++k)
    {
        float db = (k >= peak) ? -13.0f - post * (float) (k - peak) : -13.0f - pre * (float) (peak - k);
        if (p.pattern == 2) db -= (k >= 3 ? 3.0f : (k == 0 ? -3.0f : 0.0f));   // Nonlinear は前寄り
        const bool isCross = (k & 1) != 0;
        if (isCross) db += p.xfeedDb;
        (void) xfGain;
        const double startMs = 4.0 + a * (t[k] - 4.0) - 1.5;
        int start = (int) std::lround (startMs * 0.001 * sr) + (isCross ? xfDelay : 0);
        const int n = (int) std::lround (lenMs[k] * 0.001 * sr);
        auto& dst = isCross ? cross : own;
        Rng r (1000u + (uint32_t) k * 7919u);
        std::vector<float> g ((size_t) n);
        double e = 0;
        for (int i = 0; i < n; ++i)
        {
            const float tm = (float) i * 1000.0f / (float) sr;
            const float envDb = -12.0f - 0.3f * tm + 12.0f * std::exp (-tm / 1.4f);
            float fade = i > n - 24 ? (float) (n - i) / 24.0f : 1.0f;
            g[(size_t) i] = r.next() * std::pow (10.0f, envDb / 20.0f) * fade;
            e += (double) g[(size_t) i] * g[(size_t) i];
        }
        const float norm = (float) std::sqrt (std::pow (10.0, (db + 1.8) / 10.0) / std::max (e, 1e-12));   // +1.8 dB: 標準の高域カットでの音量合わせ
        for (int i = 0; i < n && start + i < total; ++i)
            dst[(size_t) (start + i)] += g[(size_t) i] * norm;
    }

    // 拡散（Diffusion）：短いオールパスを通して粒をにじませる
    const float d = std::clamp (p.diffusion / 10.0f, 0.0f, 1.0f);
    if (d > 0.001f)
    {
        static const float apMs[5] = { 0.31f, 0.53f, 0.79f, 1.13f, 1.61f };
        for (auto* v : { &own, &cross })
        {
            std::vector<float> dry = *v;
            for (int s = 0; s < 5; ++s)
            {
                Allpass ap; ap.init ((int) (apMs[s] * 0.001 * sr * (0.5 + 1.3 * d)), 0.72f * std::pow (d, 0.5f));
                for (auto& x : *v) x = ap.process (x);
            }
            const float m = (float) kPi * 0.5f * std::sqrt (d);
            const float cd = std::cos (m), cw = std::sin (m);
            for (size_t i = 0; i < v->size(); ++i) (*v)[i] = cd * dry[i] + cw * (*v)[i];
        }
    }

    Rolloff ro; ro.set (p.rolloffHz, p.rolloffSlope, (float) sr);
    for (auto& x : own) x = ro.process (x);
    ro.reset(); ro.set (std::min (p.rolloffHz, p.xfeedRollHz), p.rolloffSlope, (float) sr);
    for (auto& x : cross) x = ro.process (x);
}

//==============================================================================
// テール（片側 1 本ぶん、モノラル入力 → モノラル出力の 16 本 FDN）
class Tail
{
public:
    static constexpr int N = 16;

    void prepare (double sampleRate, int seed)
    {
        sr = (float) sampleRate;
        const float sc = sr / 48000.0f;
        // 各ラインの長さ（ms）。左右で別の組み合わせにして相関をなくす
        static const float baseMs[2][N] = {
            { 31.7f, 37.1f, 41.3f, 44.9f, 49.3f, 53.9f, 57.1f, 61.7f, 66.1f, 69.7f, 73.3f, 78.1f, 83.9f, 88.3f, 93.1f, 98.9f },
            { 33.1f, 36.3f, 42.7f, 46.1f, 50.9f, 52.7f, 58.9f, 63.1f, 64.9f, 71.3f, 75.7f, 79.9f, 82.1f, 89.9f, 95.3f, 100.7f } };
        Rng r (77u + (uint32_t) seed * 31u);
        for (int i = 0; i < N; ++i)
        {
            len[i] = std::round (baseMs[seed & 1][i] * 0.001f * sr);
            line[i].init ((int) (len[i] + maxModSamples (sr) + 8));
            apLen[i] = (int) ((1.3f + 3.7f * (0.5f + 0.5f * r.next())) * 0.001f * sr);
            ap[i].init (apLen[i], 0.5f);
            phase[i] = (float) kPi * (1.0f + r.next());
            phase2[i] = (float) kPi * (1.0f + r.next());
            rateMul[i] = 0.75f + 0.5f * (0.5f + 0.5f * r.next());
            undPhase[i] = (float) kPi * (1.0f + r.next());
            inSign[i] = r.next() > 0 ? 1.0f : -1.0f;
            outSign[i] = r.next() > 0 ? 1.0f : -1.0f;
        }
        static const float difMs[4] = { 1.7f, 2.9f, 4.3f, 6.1f };
        for (int i = 0; i < 4; ++i) inDiff[i].init ((int) (difMs[i] * 0.001f * sr * (seed & 1 ? 1.07f : 1.0f)), 0.62f);
        for (int k = 0; k < kTaps; ++k) { tapJitter[k] = 0.5f + 0.5f * r.next(); tapSign[k] = r.next() > 0 ? 1.0f : -1.0f; }
        bloomLine.init ((int) (0.45f * sr));
        static const float bMs[8] = { 7.1f, 11.3f, 17.9f, 23.3f, 31.1f, 41.9f, 53.3f, 67.1f };
        for (int i = 0; i < 8; ++i) { boostLen[i] = (int) (bMs[i] * 0.001f * sr * (seed & 1 ? 1.06f : 1.0f)); boostLine[i].init (boostLen[i] + 2); }
        pre.init ((int) (0.55f * sr));
        echo.init ((int) (1.6f * sr));
        (void) sc;
        reset();
    }

    static float maxModSamples (float sr) { return 0.012f * sr; }
    float modSamplesFor (const TailParams& p) const { return std::pow (std::clamp (p.modDepth, 0.0f, 1.0f), 1.5f) * tune.modMaxMs * 0.001f * sr; }

    void reset()
    {
        for (int i = 0; i < N; ++i) { apState[i] = 0; line[i].clear(); ap[i].clear(); lo[i].reset(); hi[i].reset(); hi2[i].reset(); hi3[i].reset(); }
        for (auto& d : inDiff) d.clear();
        bloomLine.clear(); pre.clear(); echo.clear();
        echoLp.reset(); boostLp.reset(); boostLp2.reset();
        for (auto& b : boostLine) b.clear();
    }

    // 調整用の定数（テストプログラムから変えて合わせ込む）
    struct Tune
    {
        float tapMinMs = 6.0f, tapSpanMs = 50.0f, tapSpanPerBloom = 15.0f, tapCurve = 1.0f, tapFall = 1.0f;
        float earlyGain = 0.1f;
        float modMaxMs = 0.35f;
        float levelOffsetDb = 12.0f;
        float bassFc = 1.0f, bassK = 1.0f, bloomRtPow = 0.5f;
        float boostGain = 0.5f, boostHz = 110.0f, boostTimeMin = 0.03f, boostTimeMax = 0.5f, boostDirect = 0.0f;
    } tune;

    void setParams (const TailParams& p)
    {
        params = p;
        infinite = p.timeSec >= 60.0f;
        // 残響時間 = 60 dB 下がるまでの時間（RT60）
        const float T = std::max (0.05f, p.timeSec);
        for (int i = 0; i < N; ++i)
        {
            const float dsec = (len[i] + (float) apLen[i] + 0.5f * modSamplesFor (p)) / sr;
            const float gMid = infinite ? 1.0f : std::pow (10.0f, -3.0f * dsec / T);
            const float gLow = infinite ? 1.0f : std::pow (10.0f, -3.0f * dsec / (T * std::pow (p.bassMult, tune.bassK)));
            const float gHigh = 1.0f;
            g[i] = gMid;
            lo[i].lowShelf (gLow / gMid, p.bassFreq * tune.bassFc * std::pow (p.bassMult, 0.6f), sr);
            // 高域：中域の 1 回あたりの減衰（dB）に対して、fc の 1.5 oct 下・0.5 oct 上・2.5 oct 上の 3 段で
            // 少しずつ減衰を足していく（本物の「高域ほどなだらかに短くなり続ける」効き方に合わせた形）
            (void) gHigh;
            const float m = std::clamp (p.trebMult, 0.05f, 1.0f);
            const float lossMidDb = 60.0f * dsec / T;
            const float extra = std::min (1.0f / m - 1.0f, 1.0f) * lossMidDb;
            const float fcEff = p.trebFreq * std::pow (std::min (1.0f, m / 0.5f), 3.4f);
            hi[i].highShelf (std::pow (10.0f, -0.5f * extra / 20.0f), fcEff * 0.3536f, sr);
            hi2[i].highShelf (std::pow (10.0f, -0.75f * extra / 20.0f), fcEff * 1.4142f, sr);
            hi3[i].highShelf (std::pow (10.0f, -0.6f * extra / 20.0f), fcEff * 5.657f, sr);
        }
        // 立ち上がり（BLOOM）：入力を少しずつずらしたタップで FDN に流し込む
        // 立ち上がりの長さは残響時間の平方根に比例して伸び縮みする（本物の測定どおり）
        const float rtRatio = (infinite ? 40.0f : p.timeSec) / 2.25f;
        const float rtScale = std::clamp (std::pow (rtRatio, rtRatio < 1.0f ? 1.0f : tune.bloomRtPow), 0.12f, 3.0f);
        const float span = (tune.tapSpanMs + tune.tapSpanPerBloom * p.bloom) * rtScale;
        double e = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const float x = ((float) k + tapJitter[k]) / (float) kTaps;
            tapPos[k] = (int) ((tune.tapMinMs * std::min (1.0f, rtScale) + span * x) * 0.001f * sr);
            tapGain[k] = tapSign[k] * std::pow (1.0f - 0.999f * x, tune.tapFall) * (0.3f + std::pow (x, tune.tapCurve));
            e += tapGain[k] * tapGain[k];
        }
        for (auto& tg : tapGain) tg /= (float) std::sqrt (e);

        preSamples = std::clamp (p.predelayMs * 0.001f * sr, 0.0f, 0.5f * sr);
        echoSamples = std::clamp (p.echoMs * 0.001f * sr, 1.0f, 1.55f * sr);
        echoGain = p.echoDb <= -99.0f ? 0.0f : std::pow (10.0f, p.echoDb / 20.0f);
        echoLp.setLP (p.echoRollHz, sr);
        // LOW BOOST：出だしに低音のふくらみを足す。WEIGHT はふくらみが続く長さ（Tight 約 50 ms → Heavy 約 250 ms）
        boostGain = p.lowBoostDb <= -99.0f ? 0.0f : std::pow (10.0f, p.lowBoostDb / 20.0f) * tune.boostGain;
        boostLp.lp (tune.boostHz, 0.7071, sr);
        boostLp2.lp (tune.boostHz, 0.7071, sr);
        {
            const float tw = tune.boostTimeMin + (tune.boostTimeMax - tune.boostTimeMin) * std::clamp (p.lowBoostWeight, 0.0f, 1.0f);
            for (int i = 0; i < 8; ++i) boostG[i] = std::pow (10.0f, -3.0f * (float) boostLen[i] / sr / tw);
        }

        modSamples = modSamplesFor (p);
        modInc = 2.0f * (float) kPi * std::max (0.01f, p.modRateHz) / sr;
        undAmt = 0.25f * std::clamp (p.undDepth, 0.0f, 1.0f);
        undInc = 2.0f * (float) kPi * (0.12f + 5.0f * p.undShape * p.undShape) / sr;

        // 残響時間による音量の補正（短いときも長いときも聞こえ方の大きさがそろうように）
        const float Teff = infinite ? 40.0f : p.timeSec;
        const float lt = std::log10 (std::clamp (Teff, 0.2f, 40.0f));
        const float targetDb = interpTarget (lt);
        outGain = std::pow (10.0f, (targetDb - 10.0f * std::log10 (Teff) + tune.levelOffsetDb) / 20.0f);
    }

    inline float process (float x)
    {
        // 無限（FREEZE）のときは新しい音を入れない（今鳴っている響きをそのまま保つ）
        inGain += ((infinite ? 0.0f : 1.0f) - inGain) * 0.0005f;
        x *= inGain;
        // プリディレイとエコー
        pre.push (x);
        float in = preSamples < 1.0f ? x : pre.frac (preSamples);
        echo.push (in);
        if (echoGain > 0) in += echoGain * echoLp.process (echo.frac (echoSamples));

        float boostOut = 0;
        if (boostGain > 0)
        {
            const float bIn = boostLp2.process (boostLp.process (in));
            float v[8];
            for (int i = 0; i < 8; ++i) v[i] = boostLine[i].at (boostLen[i] - 1);
            boostOut = tune.boostDirect * bIn;
            for (int i = 0; i < 8; ++i) boostOut += v[i] * (i & 2 ? -0.35f : 0.35f);
            for (int h = 1; h < 8; h <<= 1)
                for (int i = 0; i < 8; i += h << 1)
                    for (int j = i; j < i + h; ++j) { const float a = v[j], b = v[j + h]; v[j] = a + b; v[j + h] = a - b; }
            const float nrm = 0.35355339f;
            for (int i = 0; i < 8; ++i) boostLine[i].push (v[i] * nrm * boostG[i] + (i & 1 ? -bIn : bIn));
        }
        for (auto& d : inDiff) in = d.process (in);
        bloomLine.push (in);

        float early = 0, feed[N] = {};
        for (int k = 0; k < kTaps; ++k)
        {
            const float v = bloomLine.at (tapPos[k]) * tapGain[k];
            feed[k % N] += v;
            early += v;
        }

        float y[N];
        float out = 0;
        const float m = modSamples;
        for (int i = 0; i < N; ++i)
        {
            float d = len[i];
            if (m > 0.0f)
                d += m * (0.5f + 0.35f * std::sin (phase[i]) + 0.15f * std::sin (phase2[i]));
            // オールパス補間（揺らしても高域が痩せない）
            const int ip = (int) (d - 0.3f);
            const float f = d - (float) ip, eta = (1.0f - f) / (1.0f + f);
            const float u0 = line[i].at (ip - 1), u1 = line[i].at (ip);
            y[i] = eta * u0 + u1 - eta * apState[i];
            apState[i] = y[i];
            out += outSign[i] * y[i];
        }
        // 減衰とフィルター
        for (int i = 0; i < N; ++i)
        {
            float v = hi3[i].process (hi2[i].process (hi[i].process (lo[i].process (y[i])))) * g[i];
            if (undAmt > 0) v *= 1.0f + undAmt * 0.1f * std::sin (undPhase[i]);
            y[i] = ap[i].process (v);
        }
        // 16x16 アダマール
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        for (int i = 0; i < N; ++i)
            line[i].push (y[i] * 0.25f + inSign[i] * feed[i]);

        for (int i = 0; i < N; ++i)
        {
            phase[i] += modInc * rateMul[i]; if (phase[i] > 2 * (float) kPi) phase[i] -= 2 * (float) kPi;
            phase2[i] += modInc * rateMul[i] * 2.37f; if (phase2[i] > 2 * (float) kPi) phase2[i] -= 2 * (float) kPi;
            undPhase[i] += undInc * rateMul[i]; if (undPhase[i] > 2 * (float) kPi) undPhase[i] -= 2 * (float) kPi;
        }

        float o = (out * 0.25f + tune.earlyGain * early) * outGain;
        o += boostGain * boostOut;
        return o;
    }

private:
    static float interpTarget (float lt)
    {
        // 実測した「残響時間ごとの全体の音量（dB）」。log10(秒) で補間
        static const float xs[] = { std::log10 (0.2f), std::log10 (0.26f), std::log10 (0.47f), std::log10 (0.86f), std::log10 (1.45f), std::log10 (2.26f), std::log10 (4.55f), std::log10 (9.8f), std::log10 (40.0f) };
        static const float ys[] = { -18.1f, -16.75f, -14.12f, -11.3f, -8.46f, -6.05f, -2.91f, -3.6f, -3.0f };
        constexpr int n = 9;
        if (lt <= xs[0]) return ys[0];
        for (int i = 0; i < n - 1; ++i)
            if (lt <= xs[i + 1]) { float f = (lt - xs[i]) / (xs[i + 1] - xs[i]); return ys[i] + f * (ys[i + 1] - ys[i]); }
        return ys[n - 1];
    }

    static constexpr int kTaps = 24;
    float sr = 48000.0f;
    TailParams params;
    bool infinite = false;
    Delay line[N]; Allpass ap[N]; Shelf1 lo[N], hi[N], hi2[N], hi3[N];
    int apLen[N] {}; float apState[N] {}; float len[N] {}, g[N] {}, phase[N] {}, phase2[N] {}, rateMul[N] {}, undPhase[N] {}, inSign[N] {}, outSign[N] {};
    Allpass inDiff[4];
    Delay bloomLine, pre, echo;
    int tapPos[kTaps] {}; float tapGain[kTaps] {}, tapJitter[kTaps] {}, tapSign[kTaps] {};
    float preSamples = 0, echoSamples = 1, echoGain = 0, modSamples = 0, modInc = 0, undAmt = 0, undInc = 0, outGain = 1;
    float boostGain = 0, inGain = 1;
    Delay boostLine[8]; int boostLen[8] {}; float boostG[8] {};
    OnePole echoLp; Biquad boostLp, boostLp2;
};

//==============================================================================
struct MixParams
{
    float dryWet = 0.5f;      // 0 dry .. 0.5 両方 0 dB .. 1 wet
    float reflRev = 0.5f;     // 0 反射だけ .. 1 テールだけ
    float masterDb = 0.0f, reflTrimDb = 0.0f, revTrimDb = 0.0f;
    float reflWidth = 1.0f;   // 0..1.5
    float erModDepth = 0.75f, erModRate = 0.7f; // 反射の揺らぎ（0..1, Hz）
    float revWidth = 1.0f;    // 0..1
    bool  eqOn = false;
    float eqLowCut = 20, eqLsF = 157, eqLsG = 0, eqLsQ = 0.7f, eqMidF = 1000, eqMidG = 0, eqMidQ = 0.7f,
          eqHsF = 6400, eqHsG = 0, eqHsQ = 0.7f, eqRoll = 18000; int eqRollSlope = 1;
};

inline float mixLaw (float v) { return std::min (1.0f, std::pow (std::max (0.0f, 2.0f * v), 1.5f)); }

// 反射の畳み込み結果を受け取って、テール・混ぜ合わせ・EQ をする本体
class RoomEngine
{
public:
    void prepare (double sampleRate, int maxBlock, int seedOffset = 0)
    {
        sr = (float) sampleRate;
        tail[0].prepare (sampleRate, 0 + seedOffset);
        tail[1].prepare (sampleRate, 1 + seedOffset);
        xf[0].init ((int) (0.11f * sr)); xf[1].init ((int) (0.11f * sr));
        erMod[0].init ((int) (0.01f * sr)); erMod[1].init ((int) (0.01f * sr));
        tmpL.assign ((size_t) maxBlock, 0); tmpR.assign ((size_t) maxBlock, 0);
        reset();
    }
    void reset()
    {
        tail[0].reset(); tail[1].reset(); xf[0].clear(); xf[1].clear(); erMod[0].clear(); erMod[1].clear();
        for (auto& r : roll) r.reset();
        for (auto& c : lowCut) c.reset();
        for (auto& e : eq) for (auto& b : e) b.reset();
        for (auto& r : eqRoll) r.reset();
        xfLp[0].reset(); xfLp[1].reset();
    }

    void setTail (const TailParams& p)
    {
        TailParams a = p; a.seed = 0; tail[0].setParams (a);
        a.seed = 1; tail[1].setParams (a);
        tp = p;
        for (auto& r : roll) r.set (p.rolloffHz, p.rolloffSlope, sr);
        for (auto& c : lowCut) c.hp (p.lowCutHz, 0.7071, sr);
        xfGain = std::pow (10.0f, p.xfeedDb / 20.0f);
        xfSamples = std::clamp (p.xfeedMs * 0.001f * sr, 0.0f, 0.105f * sr);
        xfLp[0].setLP (p.xfeedRollHz, sr); xfLp[1].setLP (p.xfeedRollHz, sr);
    }
    void setMix (const MixParams& m)
    {
        mp = m;
        erDepth = std::pow (std::clamp (m.erModDepth, 0.0f, 1.0f), 1.5f) * 0.0004f * sr;
        erInc = 2.0f * (float) kPi * std::max (0.05f, m.erModRate) / sr;
        for (int c = 0; c < 2; ++c)
        {
            eq[c][0].hp (m.eqLowCut, 0.7071, sr);
            eq[c][1].lowShelf (m.eqLsF, m.eqLsQ, m.eqLsG, sr);
            eq[c][2].peak (m.eqMidF, m.eqMidQ, m.eqMidG, sr);
            eq[c][3].highShelf (m.eqHsF, m.eqHsQ, m.eqHsG, sr);
            eqRoll[c].set (m.eqRoll, m.eqRollSlope, sr);
        }
    }
    Tail& tailAt (int i) { return tail[i]; }

    // 音色の型（nullptr = なし）。gains は GraphicEQ::B 個の dB
    void setProfiles (const float* revGains, const float* erGains)
    {
        const bool rc = revGains != lastRev, ec = erGains != lastEr;
        if (rc) { for (auto& e : revEq) { e.set (revGains, sr); e.reset(); } lastRev = revGains; }
        if (ec) { for (auto& e : erEq) { e.set (erGains, sr); e.reset(); } lastEr = erGains; }
    }

    // io: 入力（上書きで出力）、erL/erR: 反射の畳み込み結果
    void process (float* L, float* R, const float* erL, const float* erR, int n)
    {
        const float reflG = mixLaw (1.0f - mp.reflRev) * dbOrOff (mp.reflTrimDb);
        const float revG = mixLaw (mp.reflRev) * dbOrOff (mp.revTrimDb);
        const float wet = mixLaw (mp.dryWet) * std::pow (10.0f, (mp.masterDb + 0.8f) / 20.0f);   // +0.8 dB: 本物との音量合わせ
        const float dry = mixLaw (1.0f - mp.dryWet);
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            xf[0].push (inL); xf[1].push (inR);
            float fromL = xfSamples < 1 ? inL : xf[0].frac (xfSamples);
            float fromR = xfSamples < 1 ? inR : xf[1].frac (xfSamples);
            const float tl = tail[0].process (inL + xfGain * xfLp[1].process (fromR));
            const float tr = tail[1].process (inR + xfGain * xfLp[0].process (fromL));
            float rl = revEq[0].process (lowCut[0].process (roll[0].process (tl)));
            float rr = revEq[1].process (lowCut[1].process (roll[1].process (tr)));
            // テールの幅
            { float mm = 0.5f * (rl + rr), ss = 0.5f * (rl - rr) * mp.revWidth; rl = mm + ss; rr = mm - ss; }
            float el = erL[i], er = erR[i];
            {   // 揺らぎ用の 1.5 ms の遅れ（反射の表はこの分だけ前倒しにしてある）
                erMod[0].push (el); erMod[1].push (er);
                const float base = 0.0015f * sr;
                el = erMod[0].frac (base + erDepth * std::sin (erPhase));
                er = erMod[1].frac (base + erDepth * std::sin (erPhase + 1.9f));
                erPhase += erInc; if (erPhase > 2 * (float) kPi) erPhase -= 2 * (float) kPi;
                el = erEq[0].process (el); er = erEq[1].process (er);
            }
            { float mm = 0.5f * (el + er), ss = 0.5f * (el - er) * mp.reflWidth; el = mm + ss; er = mm - ss; }
            float wl = reflG * el + revG * rl, wr = reflG * er + revG * rr;
            if (mp.eqOn)
            {
                for (auto& b : eq[0]) wl = b.process (wl);
                for (auto& b : eq[1]) wr = b.process (wr);
                wl = eqRoll[0].process (wl); wr = eqRoll[1].process (wr);
            }
            L[i] = dry * inL + wet * wl;
            R[i] = dry * inR + wet * wr;
        }
    }

private:
    static float dbOrOff (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
    float sr = 48000.0f;
    Tail tail[2];
    TailParams tp; MixParams mp;
    Delay xf[2]; OnePole xfLp[2]; float xfGain = 0.7f, xfSamples = 0;
    Rolloff roll[2]; Biquad lowCut[2];
    Delay erMod[2]; float erDepth = 0, erInc = 0, erPhase = 0;
    GraphicEQ revEq[2], erEq[2]; const float* lastRev = nullptr; const float* lastEr = nullptr;
    Biquad eq[2][4]; Rolloff eqRoll[2];
    std::vector<float> tmpL, tmpR;
};
} // namespace lumen
