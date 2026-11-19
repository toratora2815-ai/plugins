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

// 頭打ち D(u) = u / (1 + |u|^n)^(1/n) ＋ e u の表と、1 次の ADAA（折り返しを減らす）。MID DRIVE・BRICK FUZZ で共通
//   D は s = |u| / (1 + |u|) の表（8192 点・直線補間）。積分 F(u) = |u| − H(|u|) ＋ e u²/2、H(u) = ∫0^u (1 − D)（有限）を同じ s の double の表で
//   H は区間ごとに Simpson（8 等分）で積み、最後の区間（u = 8191 → ∞）は漸近の式 1 / (n (n − 1) u^(n−1)) で足す
//   表は重い（約 1.5 ms・約 96 KB）ので、部品の prepare で作る（オーディオのスレッドで作らない: DSP 検査 0.13.0。前は最初の process で 15 ms 止まった）
struct PowClipTable
{
    static constexpr int kTab = 8192;
    double n = 2.0, e = 0.0;
    std::vector<float> T;
    std::vector<double> H;
    PowClipTable (double n_, double e_) : n (n_), e (e_), T ((size_t) kTab + 2), H ((size_t) kTab + 2, 0.0)
    {
        auto oneMinusD = [this] (double u) { return 1.0 - u / std::pow (1.0 + std::pow (u, n), 1.0 / n); };
        for (int i = 0; i <= kTab + 1; ++i)
        {
            const double s = std::min (1.0, (double) i / kTab);
            T[(size_t) i] = (float) (s / std::pow (std::pow (1.0 - s, n) + std::pow (s, n), 1.0 / n));
        }
        double acc = 0.0, uPrev = 0.0, fPrev = 1.0;
        for (int i = 1; i < kTab; ++i)
        {
            const double s1 = (double) i / kTab, u1 = s1 / (1.0 - s1);
            const int m = 8; const double h = (u1 - uPrev) / m;
            double sum = fPrev;
            for (int j = 1; j < m; ++j) sum += (j % 2 == 1 ? 4.0 : 2.0) * oneMinusD (uPrev + h * j);
            const double f1 = oneMinusD (u1);
            sum += f1;
            acc += sum * h / 3.0;
            H[(size_t) i] = acc; uPrev = u1; fPrev = f1;
        }
        H[(size_t) kTab] = acc + 1.0 / (n * (n - 1.0) * std::pow (uPrev, n - 1.0));
        H[(size_t) kTab + 1] = H[(size_t) kTab];
    }
    // 表の番号（NaN・無限大でも配列の外を読まない: DSP 検査 0.13.0）
    static inline int index (double f) { return f < (double) kTab ? (int) f : kTab - 1; }
    inline float raw (float u) const
    {
        const float a = std::abs (u), s = a / (1.0f + a), f = s * (float) kTab;
        const int i = index ((double) f);
        const float d = T[(size_t) i] + (T[(size_t) i + 1] - T[(size_t) i]) * (f - (float) i);
        return std::copysign (d, u) + (float) e * u;
    }
    inline double anti (double u) const
    {
        const double a = std::abs (u), s = a / (1.0 + a), f = s * (double) kTab;
        const int i = index (f);
        const double hh = H[(size_t) i] + (H[(size_t) i + 1] - H[(size_t) i]) * (f - (double) i);
        return a - hh + 0.5 * e * u * u;
    }
    // 1 次の ADAA。x1 = 1 つ前の入口（4 倍の中）。差が小さいときは中点の D
    inline float adaa (float u, double& x1) const
    {
        const double x0 = (double) u, d = x0 - x1;
        const float y = std::abs (d) > 1.0e-3 ? (float) ((anti (x0) - anti (x1)) / d) : raw ((float) (0.5 * (x0 + x1)));
        x1 = x0;
        return y;
    }
};

// ---- MID DRIVE（Guitar Rig の Skreamer を測って合わせ込み。中音の山のあるオーバードライブ）----
// 測った結果（cap/amp/Skreamer 35 通り。小さな音の複素の伝達関数をまとめて当てはめた差 rms 0.14 dB、正弦波 5 周波数 × 9 音量の利得 ±0.3 dB 前後・3 次 ±1 dB、
// ギターの DI の 1/3 オクターブで ±0.5 dB）。形（中は全部 4 倍）:
//   入口 HP 24.7 Hz → 歪む側: HP（BASS で 5.8 kHz〜58 Hz）× DRIVE（−1.8〜+18.1 dB）→ 頭打ち → 直流止め 3 Hz → LP（DRIVE で 68〜5.9 kHz）
//                      原音側: × CLEAN（0〜+20 dB、100^v に近い）
//   → 足して LP 400 Hz × TONE の棚（零 z・極 p）× BRIGHT（0 のときだけ高音を少し下げる棚）× VOLUME（v / 0.582、0 で無音）
//   頭打ち: D(u) = (u / (1 + |u|^n)^(1/n) + e u) / (1 + e)、n = 2.161、e = 0.00114。入口の目盛り a = 45.39、わずかな非対称 δ = −0.0151（2 次 −44〜−64 dB）
//   頭打ちは表＋ 1 次の ADAA（PowClipTable）。4 倍だけでは本物より折り返しが多かった（2.5 kHz 0 dBFS で −37〜−46 dB、本物 −50〜−66 dB）
class MidDrive final : public Module
{
public:
    static float driveDb (float d)
    {
        static const float x[6] { 0.0f, 0.09f, 0.25f, 0.5f, 0.75f, 1.0f }, y[6] { -1.808f, 0.0f, 3.196f, 8.192f, 13.117f, 18.104f };
        return interpTable (x, y, std::clamp (d, 0.0f, 1.0f));
    }
    static float driveLpLog2 (float d)
    {
        static const float x[6] { 0.0f, 0.09f, 0.25f, 0.5f, 0.75f, 1.0f }, y[6] { 16.0610f, 16.1201f, 16.0167f, 15.8500f, 13.7474f, 12.5383f };
        return interpTable (x, y, std::clamp (d, 0.0f, 1.0f));
    }
    static float bassLog2 (float b)
    {
        static const float x[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] { 12.5091f, 10.8576f, 9.1892f, 7.5207f, 5.8475f };
        return interpTable (x, y, std::clamp (b, 0.0f, 1.0f));
    }
    static float toneZLog2 (float t)
    {
        static const float x[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] { 9.2415f, 8.9928f, 8.7287f, 8.6674f, 8.6439f };
        return interpTable (x, y, std::clamp (t, 0.0f, 1.0f));
    }
    static float tonePLog2 (float t)
    {
        static const float x[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] { 9.2415f, 9.2415f, 9.6995f, 10.8587f, 12.6915f };
        return interpTable (x, y, std::clamp (t, 0.0f, 1.0f));
    }
    static float cleanGain (float c)
    {
        c = std::clamp (c, 0.0f, 1.0f);
        static const float x[4] { 0.25f, 0.5f, 0.75f, 1.0f }, y[4] { -11.374f, 0.0f, 10.066f, 20.031f };
        const float g25 = kB * dbToGain (y[0]);
        if (c < 0.25f) return g25 * c / 0.25f;
        return kB * dbToGain (interpTable (x, y, c));
    }
    static float volGain (float v) { return std::clamp (v, 0.0f, 1.0f) / 0.58225f; }
    static float brightAmount (float v) { return std::clamp ((0.25f - v) / 0.25f, 0.0f, 1.0f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "middrive", "MID DRIVE", "PEDAL",
            "Mid-hump overdrive: DRIVE pushes a mid-focused signal into soft clipping, TONE opens the top, BASS lets more lows into the clipping, CLEAN blends the dry signal back in.",
            {
                { "DRIVE",  0.09f,     0, [] (float v) { return fmtPct (v); } },
                { "TONE",   0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "VOLUME", 0.58225f,  0, [] (float v) { return v <= 0.0005f ? std::string ("OFF") : fmt (gainToDb (volGain (v)), 1, " dB"); } },
                { "BASS",   0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "CLEAN",  0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "BRIGHT", 0.5f,      0, [] (float v) { return v >= 0.25f ? std::string ("ON") : "DARK " + fmtPct (brightAmount (v)); } },
                { "STEREO", 1.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF (mono)"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        (void) clip();   // 表はここで作る（オーディオのスレッドで作らない: DSP 検査 0.13.0）
        sr = sampleRate; fs4 = 4.0 * sr;
        for (auto* s : { &sLpD, &sB, &sZ, &sP }) s->prepare (sr, 0.03);
        for (auto* s : { &sG, &sC, &sV, &sBr, &sMono }) s->prepare (sr, 0.02);
        recalc (true);
        gIn = tptG (kFin); gLp0 = tptG (kF0); gPb = tptG (kPb); gDc = tptG (3.0);
        design (cur); tgt = cur; left = 0;
        reset();
    }
    void reset() override { for (auto& o : os) o.reset(); for (auto& st : z) st = {}; }
    void setKnob (int i, float v) override { if (i < 0 || i >= 7 || same (k[i], v)) return; k[i] = v; recalc (false); }
    int latency() const override { return 4; }   // 4 倍の上げ下げ
    bool dryAfterReset() const override { return false; }   // 音色を大きく変える（ドライを漏らさない）
    void process (float* L, float* R, int n) override
    {
        const auto& C = clip();
        const float dl0 = C.raw (kDelta);
        for (int s = 0; s < n; ++s)
        {
            // 量（1 サンプルずつ）と、周波数（16 サンプルごとに作り直して直線でつなぐ）
            const float g = sG.next(), c = sC.next(), vol = sV.next(), br = sBr.next(), mono = sMono.next();
            if (left == 0 && ! (sLpD.settled() && sB.settled() && sZ.settled() && sP.settled()))
            {
                for (int i = 0; i < kStep; ++i)
                    for (auto* sm : { &sLpD, &sB, &sZ, &sP }) { const float b = sm->cur; sm->next(); if (same (b, sm->cur)) sm->cur = sm->target; }
                design (tgt);
                for (int q = 0; q < kCo; ++q) inc.v[q] = (tgt.v[q] - cur.v[q]) / (float) kStep;
                left = kStep;
            }
            if (left > 0)
            {
                if (--left == 0) cur = tgt;
                else for (int q = 0; q < kCo; ++q) cur.v[q] += inc.v[q];
            }
            const float gB = cur.v[0], gD = cur.v[1], gP = cur.v[2], pz = cur.v[3];
            const float mid = 0.5f * (L[s] + R[s]);
            const float in[2] { L[s] + mono * (mid - L[s]), R[s] + mono * (mid - R[s]) };   // STEREO OFF = 左右を混ぜた音を両方に
            for (int ch = 0; ch < 2; ++ch)
            {
                float* io = ch == 0 ? L : R;
                auto& st = z[ch];
                float u4[4]; os[ch].up (in[ch], u4);
                for (auto& x : u4)
                {
                    const float xi = hp (st.in, gIn, x);                         // 入口 24.7 Hz
                    const float u = g * hp (st.bass, gB, xi);                     // BASS の HP × DRIVE
                    float w = (C.adaa (kA * u + kDelta, st.x1) - dl0) * kOutScale;   // 頭打ち（ADAA。傾き 1 に戻して × kA）
                    w = hp (st.dc, gDc, w);                                       // 直流止め 3 Hz
                    w = lp (st.lpd, gD, w);                                       // DRIVE の LP（頭打ちの後ろ）
                    float y = w + c * xi;                                         // 原音を足す
                    y = lp (st.lp0, gLp0, y);                                     // 400 Hz
                    { const float l = lp (st.tone, gP, y); y = l + pz * (y - l); } // TONE の棚 (1 + s/z)/(1 + s/p)
                    if (br > 0.0f) { const float l = lp (st.br, gPb, y); const float sh = l + kPbZb * (y - l); y += br * (sh - y); }
                    else lp (st.br, gPb, y);                                      // 状態は回しておく（切り替えでプチッとしないように）
                    x = vol * y;
                }
                io[s] = os[ch].down (u4);
            }
        }
    }

private:
    // 当てはめた値（sk4.py・sk7.py）
    static constexpr float kFin = 24.652f, kF0 = 400.275f, kZb = 32288.5f, kPb = 20384.3f, kPbZb = kPb / kZb;
    static constexpr float kAg = 7.25824f, kB = 0.29391f;            // 歪む側（DRIVE 0.09 のとき）・原音側（CLEAN 0.5 のとき）の利得
    static constexpr float kA = 45.3907f, kDelta = -0.0150694f;     // 頭打ちの入口の目盛り・非対称
    static constexpr float kN = 2.16097f, kE = 0.00114262f;          // 頭打ちの形
    static constexpr float kOutScale = kAg / kA / (1.0f + kE);
    static constexpr int kStep = 16, kCo = 4;
    struct Co { float v[kCo] { 0.0f, 0.0f, 0.0f, 1.0f }; };   // BASS の HP・DRIVE の LP・TONE の極（TPT の G）・p/z
    struct OneSt { float s = 0.0f; };
    struct St { OneSt in, bass, dc, lpd, lp0, tone, br; double x1 = (double) kDelta; };
    static const PowClipTable& clip() { static const PowClipTable t ((double) kN, (double) kE); return t; }
    static inline float lp (OneSt& o, float G, float x) { const float v = G * (x - o.s), y = v + o.s; o.s = flush (y + v); return y; }
    static inline float hp (OneSt& o, float G, float x) { return x - lp (o, G, x); }
    float tptG (double f) const { const double gg = std::tan (kPiD * std::min (f, 0.45 * fs4) / fs4); return (float) (gg / (1.0 + gg)); }
    void recalc (bool snap)
    {
        sG.set (dbToGain (driveDb (k[0])) , snap);
        sLpD.set (driveLpLog2 (k[0]), snap);
        sZ.set (toneZLog2 (k[1]), snap); sP.set (tonePLog2 (k[1]), snap);
        sV.set (volGain (k[2]), snap);
        sB.set (bassLog2 (k[3]), snap);
        sC.set (cleanGain (k[4]), snap);
        sBr.set (brightAmount (k[5]), snap);
        sMono.set (k[6] >= 0.5f ? 0.0f : 1.0f, snap);
    }
    void design (Co& o) const
    {
        o.v[0] = tptG (std::exp2 ((double) sB.cur));
        o.v[1] = tptG (std::exp2 ((double) sLpD.cur));
        const double zz = std::exp2 ((double) sZ.cur), pp = std::exp2 ((double) sP.cur);
        o.v[2] = tptG (pp);
        o.v[3] = (float) (pp / zz);
    }
    double sr = 48000.0, fs4 = 192000.0;
    float k[7] { 0.09f, 0.5f, 0.58225f, 0.5f, 0.5f, 0.5f, 1.0f };
    float gIn = 0.0f, gLp0 = 0.0f, gPb = 0.0f, gDc = 0.0f;
    Smooth sG, sC, sV, sBr, sMono, sLpD, sB, sZ, sP;
    Co cur, tgt, inc;
    int left = 0;
    St z[2];
    Oversampler4 os[2];
};

// ---- BRICK FUZZ（Guitar Rig の Fuzz を測って合わせ込み。矩形波まで潰れるファズ）----
// 測った結果（cap/amp/Fuzz 25 通り）: 強く潰れた出口は 18 kHz まで矩形波そのもの（倍音が 1/k から 0.4 dB 以内）= 頭打ちの後ろに音色のフィルターはない（5.7 Hz の直流止めだけ: DSP 検査 0.14.0）。
//   形（中は全部 4 倍）: HP（BASS で 3.3 kHz〜11 Hz）× LP（TREBLE で 8.4 Hz〜31 kHz）× FUZZ（24.1 ＋ 24.9 × FUZZ dB）→ 頭打ち → × VOLUME
//   小さな音の形 16 通りで rms 0.05〜0.4 dB（FUZZ 1 は頭打ちが近く 0.7）。頭打ち D(u) = u / (1 + |u|^n)^(1/n)、n = 8.07（硬い）、目盛り a = 26.49、非対称 δ = −0.141（2 次）。
//   正弦波 5 周波数 × 9 音量で利得 ±0.2 dB・3 次 1〜3 dB、ギターの DI の 1/3 オクターブで ±0.2 dB（TREBLE 0 の 12.5 kHz だけ −1.8）
//   VOLUME: .25 / .5 / .694 / .75 / 1 で −18.7 / −7.5 / 0 / +1.9 / +10.7 dB、0 で無音。STEREO 0（初期値）= 左右を混ぜてから歪ませる（本物の初期値もモノ）
class BrickFuzz final : public Module
{
public:
    static float fuzzDb (float f) { return 24.137f + 24.947f * std::clamp (f, 0.0f, 1.0f); }
    static float bassLog2 (float b)
    {
        static const float x[5] { 0.0f, 0.25f, 0.50001f, 0.75f, 1.0f }, y[5] { 11.6937f, 9.3335f, 6.9868f, 4.8394f, 3.4128f };
        return interpTable (x, y, std::clamp (b, 0.0f, 1.0f));
    }
    static float trebleLog2 (float t)
    {
        static const float x[6] { 0.0f, 0.25f, 0.5f, 0.60663f, 0.75f, 1.0f }, y[6] { 3.0704f, 3.8176f, 6.0682f, 7.4919f, 9.8522f, 14.9180f };
        return interpTable (x, y, std::clamp (t, 0.0f, 1.0f));
    }
    static float volGain (float v)
    {
        v = std::clamp (v, 0.0f, 1.0f);
        static const float x[5] { 0.25f, 0.5f, 0.69398f, 0.75f, 1.0f }, y[5] { -18.741f, -7.470f, 0.0f, 1.879f, 10.678f };
        if (v < 0.25f) return dbToGain (y[0]) * v / 0.25f;
        return dbToGain (interpTable (x, y, v));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "brickfuzz", "BRICK FUZZ", "PEDAL",
            "Square-wave fuzz: FUZZ sets how hard it slams into the clipper, BASS and TREBLE shape what goes in (after the fuzz only a DC blocker). STEREO OFF mixes left and right before the fuzz.",
            {
                { "FUZZ",   0.70833f, 0, [] (float v) { return fmtPct (v); } },
                { "BASS",   0.50001f, 0, [] (float v) { return fmtPct (v); } },
                { "TREBLE", 0.60663f, 0, [] (float v) { return fmtPct (v); } },
                { "VOLUME", 0.69398f, 0, [] (float v) { return v <= 0.0005f ? std::string ("OFF") : fmt (gainToDb (volGain (v)), 1, " dB"); } },
                { "STEREO", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF (mono)"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        (void) clip();   // 表はここで作る（オーディオのスレッドで作らない）
        sr = sampleRate; fs4 = 4.0 * sr;
        for (auto* s : { &sB, &sT }) s->prepare (sr, 0.03);
        for (auto* s : { &sG, &sV, &sMono }) s->prepare (sr, 0.02);
        gDc = tptG (5.7);   // 頭打ちの後ろの直流止め（本物は止めたあと約 26 ms で戻る = 5.6〜5.75 Hz の 1 次の HP）
        recalc (true);
        design (cur); tgt = cur; left = 0;
        reset();
    }
    void reset() override { for (auto& o : os) o.reset(); for (auto& st : z) st = {}; }
    void setKnob (int i, float v) override { if (i < 0 || i >= 5 || same (k[i], v)) return; k[i] = v; recalc (false); }
    int latency() const override { return 4; }   // 4 倍の上げ下げ
    bool dryAfterReset() const override { return false; }
    void process (float* L, float* R, int n) override
    {
        const auto& C = clip();
        const float d0 = C.raw (kDelta);
        const int nCh = (sMono.settled() && sMono.target >= 0.999f) ? 1 : 2;
        for (int s = 0; s < n; ++s)
        {
            const float g = sG.next(), vol = sV.next(), mono = sMono.next();
            if (left == 0 && ! (sB.settled() && sT.settled()))
            {
                for (int i = 0; i < kStep; ++i)
                    for (auto* sm : { &sB, &sT }) { const float b = sm->cur; sm->next(); if (same (b, sm->cur)) sm->cur = sm->target; }
                design (tgt);
                for (int q = 0; q < kCo; ++q) inc.v[q] = (tgt.v[q] - cur.v[q]) / (float) kStep;
                left = kStep;
            }
            if (left > 0)
            {
                if (--left == 0) cur = tgt;
                else for (int q = 0; q < kCo; ++q) cur.v[q] += inc.v[q];
            }
            const float gB = cur.v[0], gT = cur.v[1];
            const float mid = 0.5f * (L[s] + R[s]);
            const float in[2] { L[s] + mono * (mid - L[s]), R[s] + mono * (mid - R[s]) };
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto& st = z[ch];
                float u4[4]; os[ch].up (in[ch], u4);
                for (auto& x : u4)
                {
                    float u = x - lp (st.bass, gB, x);            // BASS の HP
                    u = lp (st.treb, gT, u);                       // TREBLE の LP
                    float w = C.adaa (kA * g * u + kDelta, st.x1) - d0;         // 頭打ち（ADAA）
                    w -= lp (st.dc, gDc, w);                                     // 直流止め 5.7 Hz（δ の非対称で出る直流を本物どおり止める）
                    x = w * (vol / kA);
                }
                (ch == 0 ? L : R)[s] = os[ch].down (u4);
            }
            if (nCh == 1) R[s] = L[s];
        }
        // STEREO OFF のまま止まっている間は左だけ計算して右へ写す（CPU が約半分）。右の中身も左にそろえておく（ON に戻したとき続きから）
        if (nCh == 1) { z[1] = z[0]; os[1] = os[0]; }
    }

private:
    static constexpr float kA = 26.4857f, kDelta = -0.140694f;
    static constexpr double kN = 8.07451;
    static constexpr int kStep = 16, kCo = 2;
    struct Co { float v[kCo] { 0.0f, 0.0f }; };   // BASS の HP・TREBLE の LP（TPT の G）
    struct OneSt { float s = 0.0f; };
    struct St { OneSt bass, treb, dc; double x1 = (double) kDelta; };
    static const PowClipTable& clip() { static const PowClipTable t (kN, 0.0); return t; }
    static inline float lp (OneSt& o, float G, float x) { const float v = G * (x - o.s), y = v + o.s; o.s = flush (y + v); return y; }
    float tptG (double f) const { const double gg = std::tan (kPiD * std::min (f, 0.45 * fs4) / fs4); return (float) (gg / (1.0 + gg)); }
    void recalc (bool snap)
    {
        sG.set (dbToGain (fuzzDb (k[0])), snap);
        sB.set (bassLog2 (k[1]), snap);
        sT.set (trebleLog2 (k[2]), snap);
        sV.set (volGain (k[3]), snap);
        sMono.set (k[4] >= 0.5f ? 0.0f : 1.0f, snap);
    }
    void design (Co& o) const
    {
        o.v[0] = tptG (std::exp2 ((double) sB.cur));
        o.v[1] = tptG (std::exp2 ((double) sT.cur));
    }
    double sr = 48000.0, fs4 = 192000.0;
    float k[5] { 0.70833f, 0.50001f, 0.60663f, 0.69398f, 0.0f };
    float gDc = 0.0f;
    Smooth sG, sV, sMono, sB, sT;
    Co cur, tgt, inc;
    int left = 0;
    St z[2];
    Oversampler4 os[2];
};
} // namespace aiofx
