// Forge Strip: EQ（12 帯域、13 種類の形、ダイナミック）
// 形は基準のプラグインをインパルスで測って合わせた（tools/README 参照）
#pragma once
#include "Common.h"

namespace fs
{
enum EqShape
{
    PropBell = 0, Bell, BandShelf, LowShelfAnalog, LowShelfBax, LowShelfVintage,
    HighShelfAnalog, HighShelfBax, HighShelfVintage, LowpassFlat, LowpassRes, HighpassFlat, HighpassRes,
    numEqShapes
};

inline const char* eqShapeName (int s)
{
    static const char* n[] = { "Proportional Q", "Bell", "Band Shelf", "Analog Low Shelf", "Baxandall Low Shelf", "Vintage Low Shelf",
                               "Analog High Shelf", "Baxandall High Shelf", "Vintage High Shelf", "Flat Low Pass", "Resonant Low Pass",
                               "Flat High Pass", "Resonant High Pass" };
    return n[clampv (s, 0, (int) numEqShapes - 1)];
}

inline bool eqShapeHasGain (int s) { return s <= HighShelfVintage; }

struct EqBandSettings
{
    bool on = false;
    int shape = Bell;
    float freq = 1000, gain = 0, q = 1;
    bool dynamic = false;  // ダイナミック（量が音量で変わる）
    bool up = false;       // false = しきい値を超えたら効く（Down）、true = 下回ったら効く（Up）
    float threshold = -40;
};

// 1 帯域の係数を作る（最大 4 段）。gain は今の量（ダイナミックで動く）
struct EqBandFilter
{
    Biquad st[4];
    int stages = 0;
    float scale = 1.0f;   // バンドシェルフの全体の割り算
    void reset() { for (auto& b : st) b.reset(); }

    static int flatOrder (float q)   // 平らな LP/HP の急さ（Q の範囲で 6/12/24/48 dB/oct）
    {
        if (q < 3.0f) return 1;
        if (q < 11.0f) return 2;
        if (q < 27.0f) return 4;
        return 8;
    }

    void design (int shape, float f, float g, float q, double sr)
    {
        scale = 1.0f;
        stages = 1;
        f = clampv (f, 10.0f, (float) (sr * 0.49));
        q = clampv (q, 0.1f, 40.0f);
        switch (shape)
        {
            case PropBell:  st[0].peak (f, q * std::pow (10.0f, std::abs (g) / 40.0f), g, sr); break;
            case Bell:      st[0].peak (f, q, g, sr); break;
            case BandShelf:
            {
                // 2 本のシェルフ（fc/k から上げる高域シェルフ × fc·k まで上げる低域シェルフ）÷ 量
                const float lq = std::log2 (q);
                // log2 k: Q 0.5 → 1.546, 1 → 0.956, 4 → 0.124（測定）
                float lk;
                if (lq <= 0.0f) lk = 0.956f + (1.546f - 0.956f) * clampv (-lq, 0.0f, 2.5f);
                else            lk = std::max (0.03f, 0.956f + (0.124f - 0.956f) * (lq / 2.0f));
                if (lq > 2.0f) lk = 0.124f * std::pow (0.5f, lq - 2.0f);
                const float k = std::pow (2.0f, lk);
                const float qs = lq <= 0.0f ? 0.8f : clampv (0.8f + 0.6f * lq, 0.8f, 3.0f);
                st[0].highShelf (f / k, qs, g, sr);
                st[1].lowShelf (std::min ((double) f * k, sr * 0.49), qs, g, sr);
                stages = 2;
                scale = dbToGain (-g);
                break;
            }
            case LowShelfAnalog:  st[0].lowShelf (f, 0.70710678, 2.0f * g, sr); break;
            case HighShelfAnalog: st[0].highShelf (f, 0.70710678, 2.0f * g, sr); break;
            case LowShelfBax:     st[0].lowShelf1 (f * 0.0794f, 2.0f * g, sr); break;
            case HighShelfBax:    st[0].highShelf1 (std::min (f * 1.15, sr * 0.49), g, sr); break;
            case LowShelfVintage:
            case HighShelfVintage:
            {
                const float qs = clampv (0.5f + 0.033f * (q - 1.0f), 0.45f, 0.9f);
                const float gt = g * clampv (0.985f - 0.022f * (q - 1.0f), 0.8f, 1.0f);
                if (shape == LowShelfVintage) st[0].lowShelf (std::min (f * 5.25, sr * 0.49), qs, gt, sr);
                else                          st[0].highShelf (f * 0.191f, qs, gt, sr);
                break;
            }
            case LowpassRes:  st[0].lowpassMatched (f, q, sr); break;
            case HighpassRes: st[0].highpassMatched (f, q, sr); break;
            case LowpassFlat:
            case HighpassFlat:
            {
                const int order = flatOrder (q);
                const bool lp = shape == LowpassFlat;
                if (order == 1) { if (lp) st[0].lowpass1Matched (f, sr); else st[0].highpass1Matched (f, sr); break; }
                const int n = order / 2;
                for (int i = 0; i < n; ++i)
                {
                    // バターワースの各段の Q
                    const double qq = 1.0 / (2.0 * std::cos (kPi * (2.0 * i + 1.0) / (2.0 * order)));
                    if (lp) st[i].lowpassMatched (f, qq, sr); else st[i].highpassMatched (f, qq, sr);
                }
                stages = n;
                break;
            }
            default: st[0].setIdentity(); break;
        }
    }
    inline float process (float x, int ch)
    {
        for (int i = 0; i < stages; ++i) x = st[i].process (x, ch);
        return x * scale;
    }
    double magnitude (double f, double sr) const
    {
        std::complex<double> h = 1.0;
        for (int i = 0; i < stages; ++i) h *= st[i].response (f, sr);
        return std::abs (h) * scale;
    }
};

struct EqSettings
{
    static constexpr int numBands = 12;
    EqBandSettings band[numBands];
    float mix = 1.0f;   // 0..1
    bool softSat = false;   // EQ の前で軽く飽和（測定: y = x - 0.24 x|x| + 0.036 x²、+0.16 dB）
};

class EqModule
{
public:
    void prepare (double s) { sr = s; dcBlock.highpass1 (5.0, s); for (auto& b : bands) { b.f.reset(); b.det.reset(); b.env = -120; b.cur = 1e9f; } dirty = true; }
    void reset() { for (auto& b : bands) { b.f.reset(); b.det.reset(); b.env = -120; } }

    void set (const EqSettings& s) { cfg = s; }

    // 今のダイナミックの量（表示用）
    float currentGain (int b) const { return bands[(size_t) b].cur > 1e8f ? cfg.band[b].gain : bands[(size_t) b].cur; }

    void process (float* L, float* R, int n)
    {
        if (cfg.softSat)
        {
            auto sat = [] (float x)
            {
                const float c = clampv (x, -2.0f, 2.0f);
                return 1.0186f * (c - 0.24f * c * std::abs (c) + 0.036f * c * c);
            };
            for (int i = 0; i < n; ++i)
            {
                L[i] = dcBlock.process (sat (L[i]), 0);
                R[i] = dcBlock.process (sat (R[i]), 1);
            }
        }
        for (int b = 0; b < EqSettings::numBands; ++b)
        {
            const auto& c = cfg.band[b];
            auto& st = bands[(size_t) b];
            if (! c.on) { st.cur = 1e9f; continue; }
            const bool dyn = c.dynamic && eqShapeHasGain (c.shape);
            // 検出の帯域（ベル・シェルフの中心で）
            if (dyn && (st.detF != c.freq || st.detQ != c.q || st.detShape != c.shape))
            {
                st.detF = c.freq; st.detQ = c.q; st.detShape = c.shape;
                const float fq = c.shape == LowShelfBax ? c.freq * 0.2f : (c.shape == LowShelfVintage ? c.freq * 2.0f
                               : (c.shape == HighShelfVintage ? c.freq * 0.5f : c.freq));
                if (c.shape >= LowShelfAnalog && c.shape <= LowShelfVintage) st.det.lowpass (fq, 0.7071, sr);
                else if (c.shape >= HighShelfAnalog && c.shape <= HighShelfVintage) st.det.highpass (fq, 0.7071, sr);
                else st.det.bandpass (c.freq, std::max (0.3f, c.q), sr);
            }
            const float att = coefMs (13.0f, sr), rel = coefMs (32.0f, sr);   // 測定
            for (int i0 = 0; i0 < n; i0 += 32)
            {
                const int len = std::min (32, n - i0);
                float g = c.gain;
                if (dyn)
                {
                    // 32 サンプルの検出 → 量を決める（しきい値から 12 dB で全量）
                    float pk = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const float d = std::max (std::abs (st.det.process (L[i0 + i], 0)), std::abs (st.det.process (R[i0 + i], 1)));
                        pk = std::max (pk, d);
                    }
                    const float lvl = gainToDb (pk + 1e-9f);
                    const float a = lvl > st.env ? std::pow (att, (float) len) : std::pow (rel, (float) len);
                    st.env = lvl + a * (st.env - lvl);
                    // 測定: しきい値で 0、0 dBFS で全量の直線。Down = 大きいほど下がる向き、Up = 大きいほど上がる向き
                    const float f = c.threshold < -0.1f ? clampv ((st.env - c.threshold) / -c.threshold, 0.0f, 1.0f) : 0.0f;
                    const bool louderLower = ! c.up;
                    if (c.gain < 0) g = louderLower ? c.gain * f : c.gain * (1.0f - f);
                    else            g = louderLower ? c.gain * (1.0f - f) : c.gain * f;
                }
                if (std::abs (g - st.cur) > 0.01f || st.shape != c.shape || st.freq != c.freq || st.q != c.q)
                {
                    st.f.design (c.shape, c.freq, g, c.q, sr);
                    st.cur = g; st.shape = c.shape; st.freq = c.freq; st.q = c.q;
                }
                for (int i = 0; i < len; ++i)
                {
                    L[i0 + i] = st.f.process (L[i0 + i], 0);
                    R[i0 + i] = st.f.process (R[i0 + i], 1);
                }
            }
        }
    }

    // 表示用: f での合計の量（dB）
    float responseDb (double f) const
    {
        double m = 1.0;
        for (int b = 0; b < EqSettings::numBands; ++b)
        {
            const auto& c = cfg.band[b];
            if (! c.on) continue;
            EqBandFilter t; t.design (c.shape, c.freq, c.gain, c.q, sr);
            m *= t.magnitude (f, sr);
        }
        return (float) (20.0 * std::log10 (std::max (m, 1e-9)));
    }

    double sampleRate() const { return sr; }
    const EqSettings& settings() const { return cfg; }

private:
    struct BandState
    {
        EqBandFilter f; Biquad det; float env = -120; float cur = 1e9f;
        int shape = -1; float freq = -1, q = -1;
        float detF = -1, detQ = -1; int detShape = -1;
    };
    EqSettings cfg;
    Biquad dcBlock;
    std::array<BandState, EqSettings::numBands> bands;
    double sr = 48000;
    bool dirty = true;
};
} // namespace fs
