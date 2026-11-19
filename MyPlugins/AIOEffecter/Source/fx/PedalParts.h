#pragma once
// 足元のペダル（0.12.0〜）。JUCE なし
#include "ModParts2.h"

namespace aiofx
{
// ---- LEVEL BOOST（Guitar Rig の Gain Booster を測って合わせ込み）----
// 測った結果（cap/amp/GainBooster: 小さな音のスイープと 5 周波数 × 9 音量の正弦波）: どの周波数も平ら（±0.0 dB）、利得 = 30 × BOOST dB（初期値 0.3 で +9 dB）。
// 出口の振れが 9 までは 1 ビットも歪まず、そこから 10（+20 dBFS）へなめらかに頭打ち: y = 9 ＋ tanh(|x| − 9)（振れ 13.3 / 15.8 / 31.6 で 3 次 −19.5 / −15.6 / −10.8、
// 5 次 −30.4 / −44.2 / −17.9 dB = 本物 −19 / −16 / −11、−30 / −44 / −18）。歪むので 4 倍の中で
class LevelBoost final : public Module
{
public:
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "levelboost", "LEVEL BOOST", "PEDAL",
            "Clean level boost up to +30 dB. It stays perfectly clean until the output reaches about +19 dBFS, then rounds off softly at +20 dBFS.",
            {
                { "BOOST", 0.3f, 0, [] (float v) { return fmt (30.0f * std::clamp (v, 0.0f, 1.0f), 1, " dB"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override { sr = sampleRate; sG.prepare (sr, 0.02); sG.set (dbToGain (30.0f * k0), true); reset(); }
    void reset() override { for (auto& o : os) o.reset(); }
    void setKnob (int i, float v) override { if (i != 0 || same (k0, v)) return; k0 = std::clamp (v, 0.0f, 1.0f); sG.set (dbToGain (30.0f * k0)); }
    int latency() const override { return 4; }   // 4 倍の上げ下げ
    bool dryAfterReset() const override { return false; }   // ホストの reset のあとドライ（上げていない音）を混ぜない（BOOST 1 で −22 dB 落ちた: DSP 検査 0.12.0）
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float g = sG.next();
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                float u[4]; os[c].up (io[s] * g, u);
                for (auto& v : u) v = clip (v);
                io[s] = os[c].down (u);
            }
        }
    }
    static inline float clip (float x)
    {
        const float a = std::abs (x);
        if (a <= 9.0f) return x;
        return std::copysign (9.0f + std::tanh (a - 9.0f), x);
    }

private:
    double sr = 48000.0;
    float k0 = 0.3f;
    Smooth sG;
    Oversampler4 os[2];
};

// ---- TREBLE LIFT（Guitar Rig の Treble Booster を測って合わせ込み）----
// 測った結果（cap/amp/TrebleBooster 11 通り）: 0 dBFS・+36 dB の持ち上げでも 1 ビットも歪まない = 線形の EQ。
//   H = (BOOST の持ち上げ fin / 26.11) × HP(fin) × LP(fLP)。fin から上を持ち上げ、下は元の高さのまま（26 Hz から fin まで 6 dB/oct）
//   fin（BOOST 0 / .25 / .315 / .5 / .75 / 1）= 26.1 / 82.8 / 111.9 / 263.3 / 847.5 / 2836 Hz（2 を底にした対数で補間。BOOST 1 で +40.7 dB）
//   fLP（BRIGHT 0 / .25 / .5 / .603 / .75 / 1）= 849 / 1800 / 3961 / 5611 / 9611 / 32969 Hz
//   本物の高音はアナログの 1 次のローパスより少しだけ高い（16 kHz で +0.2〜0.3 dB）。ふつうの双一次だと 16 kHz で 3.7 dB 足りないので、
//   LP は「極 1 つ（exp）＋ 零 2 つ」の 2 次の式で、DC・7.2 kHz・ナイキストの 3 点の大きさを「アナログ × 高音の持ち上げ 1.72 (f / 24 kHz)² dB」に合わせる（Vicanek の matched の考え方）。
//   11 通りの差: 1/24 オクターブで 40 Hz〜18 kHz の rms 0.018 dB・最大 0.16 dB（18 kHz）
class TrebleLift final : public Module
{
public:
    static constexpr double kC = 26.1123, kKap = 1.7164;
    static double finHz (float b)
    {
        static const float x[6] { 0.0f, 0.25f, 0.315f, 0.5f, 0.75f, 1.0f }, y[6] { 4.7082f, 6.3719f, 6.8057f, 8.0407f, 9.7271f, 11.4695f };   // log2 Hz
        return std::exp2 ((double) interpTable (x, y, std::clamp (b, 0.0f, 1.0f)));
    }
    static double lpHz (float br)
    {
        static const float x[6] { 0.0f, 0.25f, 0.5f, 0.60318f, 0.75f, 1.0f }, y[6] { 9.7301f, 10.8141f, 11.9515f, 12.454f, 13.2305f, 15.0088f };   // log2 Hz
        return std::exp2 ((double) interpTable (x, y, std::clamp (br, 0.0f, 1.0f)));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "treblelift", "TREBLE LIFT", "PEDAL",
            "Treble booster: everything above the BOOST corner is lifted (up to +40 dB), the lows stay where they were. BRIGHT opens the top end. Very loud at high BOOST: put an amp after it.",
            {
                { "BOOST",  0.315f,   0, [] (float v) { return "+" + fmt (std::max (0.0f, gainToDb ((float) (finHz (v) / kC))), 1, " dB"); } },
                { "BRIGHT", 0.60318f, 0, [] (float v) { return fmtHz ((float) lpHz (v)); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        sFin.prepare (sr, 0.03); sLp.prepare (sr, 0.03);
        const double fN = 0.5 * sr, f1 = std::min (7200.0, 0.3 * fN);
        w1 = 2.0 * kPiD * f1 / sr; cw1 = std::cos (w1); c2w1 = std::cos (2.0 * w1);
        tiltN = tilt2 (fN); tilt1 = tilt2 (f1);
        sFin.set ((float) std::log2 (finHz (kb)), true); sLp.set ((float) std::log2 (lpHz (kr)), true);
        design (cur); tgt = cur; left = 0;
        reset();
    }
    void reset() override { for (auto& st : z) st = {}; }
    void setKnob (int i, float v) override
    {
        if (i == 0 && ! same (kb, v)) { kb = std::clamp (v, 0.0f, 1.0f); sFin.set ((float) std::log2 (finHz (kb))); }
        if (i == 1 && ! same (kr, v)) { kr = std::clamp (v, 0.0f, 1.0f); sLp.set ((float) std::log2 (lpHz (kr))); }
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if (left == 0 && ! (sFin.settled() && sLp.settled()))
            {
                // 16 サンプルごとに係数を作り直し、その間は 1 サンプルずつ直線でつなぐ
                // float の 1 次のなめらかは目標の手前（48 kHz で約 7e-4）で止まり settled にならない → 止まったら目標へ（作り直しが永遠に続かないように: DSP 検査 0.12.0）
                for (int i = 0; i < kStep; ++i)
                    for (auto* sm : { &sFin, &sLp }) { const float b = sm->cur; sm->next(); if (same (b, sm->cur)) sm->cur = sm->target; }
                design (tgt);
                for (int q = 0; q < kCo; ++q) inc.v[q] = (tgt.v[q] - cur.v[q]) / (float) kStep;
                left = kStep;
            }
            if (left > 0)
            {
                if (--left == 0) cur = tgt;
                else for (int q = 0; q < kCo; ++q) cur.v[q] += inc.v[q];
            }
            const float gain = cur.v[0], gg = cur.v[1], b0 = cur.v[2], b1 = cur.v[3], b2 = cur.v[4], p = cur.v[5];
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                auto& st = z[c];
                // 持ち上げ: 1 次の TPT ハイパス × 量
                const float v = gg * (io[s] - st.hs), lp = v + st.hs;
                st.hs = flush (lp + v);
                const float u = gain * (io[s] - lp);
                // 高音: 極 1 つ・零 2 つ
                const float y = b0 * u + b1 * st.u1 + b2 * st.u2 + p * st.y1;
                st.u2 = st.u1; st.u1 = u; st.y1 = flush (y);
                io[s] = y;
            }
        }
    }

private:
    static constexpr int kStep = 16, kCo = 6;
    struct Co { float v[kCo] { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }; };   // 量・HP の gg・b0・b1・b2・極
    struct St { float hs = 0.0f, u1 = 0.0f, u2 = 0.0f, y1 = 0.0f; };
    static double tilt2 (double f) { const double t = std::min (f / 24000.0, 1.0); return std::pow (10.0, kKap / 10.0 * t * t); }
    void design (Co& o) const
    {
        const double fin = std::exp2 ((double) sFin.cur), fc = std::exp2 ((double) sLp.cur);
        const double g = std::tan (kPiD * std::min (fin, 0.45 * sr) / sr);
        // LP: 極 p = exp(−ωc)。零は |B|² = c0 + 2 c1 cos ω + 2 c2 cos 2ω を 3 点（0・ω1・π）で目標 × |1 − p e^{−jω}|² に合わせて、最小位相に分ける
        const double wc = 2.0 * kPiD * fc / sr, p = std::exp (-wc);
        const double r1 = w1 / wc, rN = kPiD / wc;
        const double d0 = (1.0 - p) * (1.0 - p);
        const double dN = tiltN / (1.0 + rN * rN) * (1.0 + p) * (1.0 + p);
        const double d1 = tilt1 / (1.0 + r1 * r1) * (1.0 + p * p - 2.0 * p * cw1);
        const double c1 = 0.25 * (d0 - dN);
        const double c2 = (d1 - 0.5 * (d0 + dN) - 2.0 * c1 * cw1) / (2.0 * (c2w1 - 1.0));
        const double W = 1.0 - p, V = std::sqrt (std::max (dN, 0.0)), sm = 0.5 * (W + V);
        const double disc = sm * sm - 4.0 * c2;
        double b0 = 0.5 * sm, b2 = 0.5 * sm;
        if (disc >= 0.0) { const double r = std::sqrt (disc); b0 = 0.5 * (sm + r); b2 = 0.5 * (sm - r); }
        o.v[0] = (float) (fin / kC);
        o.v[1] = (float) (g / (1.0 + g));
        o.v[2] = (float) b0; o.v[3] = (float) (0.5 * (W - V)); o.v[4] = (float) b2; o.v[5] = (float) p;
    }
    double sr = 48000.0, w1 = 0.94, cw1 = 0.59, c2w1 = -0.31, tiltN = 1.48, tilt1 = 1.04;
    float kb = 0.315f, kr = 0.60318f;
    Smooth sFin, sLp;
    Co cur, tgt, inc;
    int left = 0;
    St z[2];
};
} // namespace aiofx
