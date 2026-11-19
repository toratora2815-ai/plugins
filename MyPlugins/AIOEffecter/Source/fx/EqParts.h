#pragma once
// EQ の部品。2026-11-19〜
// フィルターは TPT の状態変数フィルター（Svf、VerbEngine.h）と TPT の 1 次（TptShelf1、DelayKit.h）で、なめらかにしたつまみから 32 サンプルごとに計算し直して
// 32 サンプルで直線につなぐ（係数を動かしてもプチッとしにくい）。RBJ の 2 次フィルターと伝達関数は同じ（棚の傾き S は Q に直す）。
// 種類の切り替え（BELL / SHELF）は両方いつも回して混ぜ具合で、入／切は素通しとの混ぜ具合で（古い状態が急に出ない）
#include "DynKit.h"
#include "DelayKit.h"
#include "VerbEngine.h"
#include <array>

namespace aiofx
{
// 対数の表（周波数など）: xs の点で log10 の値を直線補間
template <int N> inline float logTable (const float (&xs)[N], const float (&hz)[N], float v)
{
    float ly[N]; for (int i = 0; i < N; ++i) ly[i] = std::log10 (hz[i]);
    return std::pow (10.0f, interpTable (xs, ly, v));
}
// RBJ の棚の傾き S → Q（A = 10^(dB/40)）: 1/Q = √((A + 1/A)(1/S − 1) + 2)
inline double shelfQFromS (double gainDb, double S)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    return 1.0 / std::sqrt (std::max (1.0e-6, (A + 1.0 / A) * (1.0 / S - 1.0) + 2.0));
}


// 1 次のなめらかは float の丸めで目標の数百 ulp 手前で止まる（192 kHz で約 2900 ulp。PARAM EQ の Q 40 の肩で 0.79 dB ずれた: DSP 検査 0.5.0）。
// 32 サンプルごとの計算で、目標の 0.1 % 以内なら目標ちょうどへ寄せる（ゲインは 0.009 dB 以下、周波数は最後に 0.7〜1 % まとめて寄るが、
// プチッ・段差とも寄せない場合と同じ: DSP 検査 0.5.0 の 2 回目）
inline float settleTo (Smooth& s)
{
    if (std::abs (s.cur - s.target) <= 1.0e-3f * std::max (1.0f, std::abs (s.target))) s.cur = s.target;
    return s.cur;
}

// ---- SHELF EQ（Guitar Rig の EQ Shelving を測って合わせ込み）----
// 測った結果（cap/mod/EQShelving）: LO = 低い棚（Q 0.42）、GAIN = 40 × (つまみ − 0.5) dB、FREQ = 40 × 53.5^つまみ Hz（0.5 で 290 Hz）。
//   HI = 高い棚（Q 0.45）、GAIN 同じ、FREQ = 4600 × 53.5^(つまみ − 0.785) Hz（本物の初期値 0.785 で 4.6 kHz）。OUTPUT = 40 × (つまみ − 0.5) dB。
//   本物の初期値は LO GAIN 0（−20 dB の低音カット）・HI GAIN 0.362（−5.5 dB）。HI を大きく持ち上げた所は本物が中で飽和するので、周波数は式で延ばした（大まか）
struct ShelfEq final : Module
{
    static float gDb (float v) { return 40.0f * (std::clamp (v, 0.0f, 1.0f) - 0.5f); }
    static float loHz (float v) { return 40.0f * std::pow (53.5f, std::clamp (v, 0.0f, 1.0f)); }
    static float hiHz (float v) { return 4600.0f * std::pow (53.5f, std::clamp (v, 0.0f, 1.0f) - 0.785f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "shelfeq", "SHELF EQ", "EQ",
            "Simple low and high shelves. Gains at 50 % = flat.",
            {
                { "OUTPUT",  0.5f,      0, [] (float v) { return fmt (gDb (v), 1, " dB"); } },
                { "LO FREQ", 0.2475f,   0, [] (float v) { return fmtHz (loHz (v)); } },
                { "LO GAIN", 0.0f,      0, [] (float v) { return fmt (gDb (v), 1, " dB"); } },
                { "HI FREQ", 0.785f,    0, [] (float v) { return fmtHz (std::min (hiHz (v), 22000.0f)); } },
                { "HI GAIN", 0.362069f, 0, [] (float v) { return fmt (gDb (v), 1, " dB"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        sOut.prepare (sr, 0.03);
        for (auto* q : { &sLoF, &sLoG, &sHiF, &sHiG }) q->prepare (sr, 0.03);
        setTargets (true); tone (true); reset();
    }
    void reset() override { for (auto& c : f) for (auto& x : c) x.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 5 || same (k[i], v)) return; k[i] = v; setTargets (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& c : f) for (auto& x : c) x.step();
            const float g = sOut.next();
            L[s] = f[0][1].process (f[0][0].process (L[s])) * g;
            R[s] = f[1][1].process (f[1][0].process (R[s])) * g;
        }
    }

private:
    void setTargets (bool snap)
    {
        sOut.set (dbToGain (gDb (k[0])), snap);
        sLoF.set (std::log (loHz (k[1])), snap); sLoG.set (gDb (k[2]), snap);
        sHiF.set (std::log (std::min (hiHz (k[3]), 22000.0f)), snap); sHiG.set (gDb (k[4]), snap);
    }
    void tone (bool snap)
    {
        float v[4] { sLoF.cur, sLoG.cur, sHiF.cur, sHiG.cur };
        if (! snap) { for (int i = 0; i < 32; ++i) { sLoF.next(); sLoG.next(); sHiF.next(); sHiG.next(); } v[0] = settleTo (sLoF); v[1] = settleTo (sLoG); v[2] = settleTo (sHiF); v[3] = settleTo (sHiG); }
        if (! snap && same (v[0], last[0]) && same (v[1], last[1]) && same (v[2], last[2]) && same (v[3], last[3]))
        {
            // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
            for (auto& c : f) for (auto& x : c) x.ramp (true, 1.0f);
            return;
        }
        for (int i = 0; i < 4; ++i) last[i] = v[i];
        for (int c = 0; c < 2; ++c)
        {
            f[c][0].lowShelf (sr, std::exp (v[0]), v[1], 0.42);
            f[c][1].highShelf (sr, std::min ((double) std::exp (v[2]), 0.45 * sr), v[3], 0.45);
            for (auto& x : f[c]) x.ramp (snap, 32.0f);
        }
    }
    double sr = 48000.0;
    float k[5] { 0.5f, 0.2475f, 0.0f, 0.785f, 0.362069f };
    float last[4] { -99, -99, -99, -99 };
    Svf f[2][2];
    Smooth sOut, sLoF, sLoG, sHiF, sHiG;
    unsigned ctl = 0;
};

// ---- CONSOLE EQ（Guitar Rig の Solid EQ を測って合わせ込み）----
// 測った結果（cap/mod/SolidEQ、周波数・Q は GAIN を上げた状態で追加で測った cap/list10.tsv）:
//   HMF・LMF はピーク（GAIN 0 / .25 / .75 / 1 → −27〜−30 / −9 / +8 / +20 dB、下げる側は Q が 1.6 倍細い）。
//   HMF FREQ 695 / 1317 / 2667 / 5378 / 7409 Hz、LMF FREQ 223 / 402 / 812 / 1692 / 2231 Hz、Q つまみ 0 → 1 で Q 0.6 → 0.18（右へ回すほど広い）
//   LF はピーク（BELL ON、初期値）で Q 1.06、FREQ 40 / 81 / 184 / 433 / 698 Hz、BELL OFF で低い棚（周波数 1.5 倍、傾き 0.7）。HF は棚（BELL OFF、初期値 0.5 は OFF、傾き 0.8）、BELL ON でピーク（Q 1.14、周波数 1.5 倍）
//   HP（2026-11-19 測り直し）: 3 次（2 次 + 1 次）、.25 / .5 / .75 / 1 → 28 / 57 / 169 / 364 Hz（Q 1.0 / 0.88 / 0.81 / 0.92）。.5 から上は −24〜−28 dB で止まる（素通しが少し混ざる形）
//   LP（同）: 2 次、.25 / .5 / .75 / 1 → 16.8 / 14.7 / 8.7 / 4.3 kHz（Q 0.79 / 0.57 / 0.51 / 0.52）。どちらも 0.1 より下は OFF
//   OUTPUT（同）: 0 / .25 / .5 / .75 / 1 → 無音 / −40.3 / −0.3 / +5.7 / +11.7 dB（初期値 1）
//   本物は大きく持ち上げると中で飽和する（HF GAIN 1 ＋ OUTPUT 1 で低い所が −10 dB）→ ここでは飽和させない（大まか）。MODE（E / G）の違いはまだ（細かい測定で）
struct ConsoleEq final : Module
{
    static float gainDb (float v, float maxCut)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        const float ys[5] { maxCut, maxCut * 0.31f, 0.0f, 8.2f, 20.2f };
        return interpTable (xs, ys, v);
    }
    static float hmfHz (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, hz[5] { 695.0f, 1317.0f, 2667.0f, 5378.0f, 7409.0f }; return logTable (xs, hz, v); }
    static float lmfHz (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, hz[5] { 223.0f, 402.0f, 812.0f, 1692.0f, 2231.0f }; return logTable (xs, hz, v); }
    static float lfHz (float v)  { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, hz[5] { 40.0f, 81.0f, 184.0f, 433.0f, 698.0f }; return logTable (xs, hz, v); }
    static float hfHz (float v)  { static const float xs[3] { 0.0f, 0.5f, 1.0f }, hz[3] { 1500.0f, 4500.0f, 12000.0f }; return logTable (xs, hz, v); }
    static float midQ (float v)  { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, q[5] { 0.6f, 0.5f, 0.365f, 0.25f, 0.185f }; return interpTable (xs, q, v); }
    static float hpHz (float v)  { static const float xs[5] { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f }, hz[5] { 20.0f, 28.4f, 57.4f, 169.3f, 363.8f }; return logTable (xs, hz, std::max (0.1f, v)); }
    static float hpQ (float v)   { static const float xs[4] { 0.25f, 0.5f, 0.75f, 1.0f }, q[4] { 1.0f, 0.88f, 0.81f, 0.92f }; return interpTable (xs, q, std::max (0.25f, v)); }
    static float hpFloor (float v) { static const float xs[4] { 0.25f, 0.5f, 0.75f, 1.0f }, e[4] { 0.0f, 0.0624f, 0.0407f, 0.0412f }; return interpTable (xs, e, std::max (0.25f, v)); }
    static float lpHz (float v)  { static const float xs[4] { 0.25f, 0.5f, 0.75f, 1.0f }, hz[4] { 16761.0f, 14692.0f, 8716.0f, 4265.0f }; return logTable (xs, hz, std::max (0.25f, v)); }
    static float lpQ (float v)   { static const float xs[4] { 0.25f, 0.5f, 0.75f, 1.0f }, q[4] { 0.79f, 0.57f, 0.51f, 0.52f }; return interpTable (xs, q, std::max (0.25f, v)); }
    static float outGain (float v)
    {
        v = std::clamp (v, 0.0f, 1.0f);
        if (v < 0.25f) return dbToGain (-40.3f) * v / 0.25f;
        static const float xs[4] { 0.25f, 0.5f, 0.75f, 1.0f }, db[4] { -40.3f, -0.3f, 5.7f, 11.7f };
        return dbToGain (interpTable (xs, db, v));
    }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "consoleeq", "CONSOLE EQ", "EQ",
            "Four-band console EQ with high-pass and low-pass. Gains at 50 % = flat.",
            {
                { "HF GAIN",  0.5f, 0, [] (float v) { return fmt (gainDb (v, -18.0f), 1, " dB"); } },
                { "HF FREQ",  0.5f, 0, [] (float v) { return fmtHz (hfHz (v)); } },
                { "HF BELL",  0.5f, 0, [] (float v) { return std::string (v > 0.75f ? "BELL" : "SHELF"); } },
                { "HMF GAIN", 0.5f, 0, [] (float v) { return fmt (gainDb (v, -29.7f), 1, " dB"); } },
                { "HMF FREQ", 0.5f, 0, [] (float v) { return fmtHz (hmfHz (v)); } },
                { "HMF Q",    0.5f, 0, [] (float v) { return fmt (midQ (v), 2, ""); } },
                { "LMF GAIN", 0.5f, 0, [] (float v) { return fmt (gainDb (v, -26.9f), 1, " dB"); } },
                { "LMF FREQ", 0.5f, 0, [] (float v) { return fmtHz (lmfHz (v)); } },
                { "LMF Q",    0.5f, 0, [] (float v) { return fmt (midQ (v), 2, ""); } },
                { "LF GAIN",  0.5f, 0, [] (float v) { return fmt (gainDb (v, -21.3f), 1, " dB"); } },
                { "LF FREQ",  0.5f, 0, [] (float v) { return fmtHz (lfHz (v)); } },
                { "LF BELL",  1.0f, 0, [] (float v) { return std::string (v > 0.75f ? "BELL" : "SHELF"); } },
                { "HP FREQ",  0.0f, 0, [] (float v) { return v < 0.1f ? std::string ("OFF") : fmtHz (hpHz (v)); } },
                { "LP FREQ",  0.0f, 0, [] (float v) { return v < 0.1f ? std::string ("OFF") : fmtHz (lpHz (v)); } },
                { "MODE",     0.0f, 2, [] (float v) { return std::string (v >= 0.5f ? "G" : "E"); } },
                { "OUTPUT",   1.0f, 0, [] (float v) { return v < 0.001f ? std::string ("OFF") : fmt (gainToDb (std::max (1.0e-9f, outGain (v))), 1, " dB"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& q : sm) q.prepare (sr, 0.03);
        // HP・LP の周波数・入／切は 0.1 秒で（一気に OFF にすると、切っていた低い音と位相が急に戻ってザラッとしていた）
        for (int j : { 10, 11, 12, 13, 14, 17, 18 }) sm[j].prepare (sr, 0.1);
        sOut.prepare (sr, 0.03);
        setTargets (true); tone (true); reset();
    }
    void reset() override { for (auto& c : f) for (auto& x : c) x.reset(); for (auto& x : hp1) x.reset(); }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 16 || same (k[i], v)) return;
        k[i] = v; setTargets (false);
    }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& c : f) for (auto& x : c) x.step();
            for (auto& x : hp1) x.step();
            const float g = sOut.next();
            // 混ぜ具合は 1 サンプルずつ（32 サンプルごとの階段だと、小さな段差が並んでザラッとした）
            mixHfBell = sm[15].next(); mixLfBell = sm[16].next(); mixHp = sm[17].next(); mixLp = sm[18].next(); hpEps = sm[12].next();
            for (int c = 0; c < 2; ++c)
            {
                float v = io[c][s];
                // HF: 棚とピークをいつも回して混ぜる / HMF / LMF / LF: ピークと低い棚をいつも回して混ぜる
                const float hs = f[c][0].process (v), hb = f[c][1].process (v);
                v = hs + mixHfBell * (hb - hs);
                v = f[c][3].process (f[c][2].process (v));
                const float lb = f[c][4].process (v), ls = f[c][5].process (v);
                v = lb + (1.0f - mixLfBell) * (ls - lb);
                // HP（3 次。.5 から上は少し素通しが混ざる）・LP（2 次）。OFF は素通しとの混ぜ具合で
                const float hp = hp1[c].process (f[c][6].process (v));
                v += mixHp * (1.0f - hpEps) * (hp - v);
                v += mixLp * (f[c][7].process (v) - v);
                io[c][s] = v * g;
            }
        }
    }

private:
    // なめらかにする値: 0 HF dB、1 HF log f、2 HMF dB、3 HMF log f、4 HMF Q、5 LMF dB、6 LMF log f、7 LMF Q、8 LF dB、9 LF log f、
    // 10 HP log f、11 HP Q、12 HP 床、13 LP log f、14 LP Q、15 HF BELL の混ぜ、16 LF BELL の混ぜ、17 HP の入、18 LP の入
    static constexpr int kSm = 19;
    void setTargets (bool snap)
    {
        const float t[kSm] {
            gainDb (k[0], -18.0f), std::log (hfHz (k[1])), gainDb (k[3], -29.7f), std::log (hmfHz (k[4])), midQ (k[5]),
            gainDb (k[6], -26.9f), std::log (lmfHz (k[7])), midQ (k[8]), gainDb (k[9], -21.3f), std::log (lfHz (k[10])),
            std::log (hpHz (k[12])), hpQ (k[12]), hpFloor (k[12]), std::log (std::min (lpHz (k[13]), 0.45f * (float) sr)), lpQ (k[13]),
            k[2] > 0.75f ? 1.0f : 0.0f, k[11] > 0.75f ? 1.0f : 0.0f, k[12] >= 0.1f ? 1.0f : 0.0f, k[13] >= 0.1f ? 1.0f : 0.0f };
        for (int j = 0; j < kSm; ++j) sm[j].set (t[j], snap);
        sOut.set (k[15] < 0.001f ? 0.0f : outGain (k[15]), snap);
    }
    void tone (bool snap)
    {
        float v[kSm];
        for (int j = 0; j < kSm; ++j)
        {
            v[j] = sm[j].cur;
            const bool perSample = j == 12 || j >= 15;   // 混ぜ具合と HP の床は process で 1 サンプルずつ進める
            if (! snap && ! perSample) { for (int i = 0; i < 32; ++i) sm[j].next(); v[j] = settleTo (sm[j]); }
        }
        if (snap) { mixHfBell = v[15]; mixLfBell = v[16]; mixHp = v[17]; mixLp = v[18]; hpEps = v[12]; }
        bool changed = snap;
        for (int j = 0; j < 15; ++j) if (j != 12 && ! same (v[j], last[j])) { changed = true; last[j] = v[j]; }
        if (! changed) { for (auto& c : f) for (auto& x : c) x.ramp (true, 1.0f); return; }   // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
        const double hfF = std::exp ((double) v[1]), lfF = std::exp ((double) v[9]);
        const double qh = v[4] * (v[2] < 0.0f ? 1.6 : 1.0), ql = v[7] * (v[5] < 0.0f ? 1.6 : 1.0);
        for (int c = 0; c < 2; ++c)
        {
            f[c][0].highShelf (sr, std::min (hfF, 0.45 * sr), v[0], shelfQFromS (v[0], 0.8));
            f[c][1].peak (sr, std::min (hfF * 1.5, 0.45 * sr), 1.14, v[0]);
            f[c][2].peak (sr, std::exp ((double) v[3]), qh, v[2]);
            f[c][3].peak (sr, std::exp ((double) v[6]), ql, v[5]);
            f[c][4].peak (sr, lfF, 1.06, v[8]);
            f[c][5].lowShelf (sr, lfF * 1.5, v[8], shelfQFromS (v[8], 0.7));
            f[c][6].hp (sr, std::exp ((double) v[10]), v[11]);
            f[c][7].lp (sr, std::exp ((double) v[13]), v[14]);
            hp1[c].set (sr, std::exp ((double) v[10]), 0.0, 1.0);
            for (auto& x : f[c]) x.ramp (snap, 32.0f);
            hp1[c].ramp (snap, 32);
        }
    }

    double sr = 48000.0;
    float k[16] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    float last[kSm] {};
    float mixHfBell = 0.0f, mixLfBell = 1.0f, mixHp = 0.0f, mixLp = 0.0f, hpEps = 0.0f;
    Svf f[2][8];   // 0 HF 棚、1 HF ピーク、2 HMF、3 LMF、4 LF ピーク、5 LF 棚、6 HP 2 次、7 LP 2 次（棚とピークは同じ入口から並べて回して混ぜる）
    TptShelf1 hp1[2];
    Smooth sm[kSm], sOut;
    unsigned ctl = 0;
};

// ---- PARAM EQ（Guitar Rig の EQ Parametric を測って合わせ込み）----
// 測った結果（cap/mod/EQParametric、1/96 オクターブの返りに RBJ のピークを当てはめ）:
//   FREQ = 20 × 1000^v Hz。GAIN は表（0 → −13.9、.111 → −11.6、.25 → −8.0、.5 → 0、.75 → +9.2、1 → +19.0 dB）。
//   Q = min(40, 0.72 / v)（0 でとても鋭い）、下げる側は 0.75 倍の広さ。VOLUME = −19 + 40 v dB。全体に 2 次のバターワースのローパス 16 kHz（本物も 20 kHz で −6 dB）
struct ParamEq final : Module
{
    static float volDb (float v) { return -19.0f + 40.0f * std::clamp (v, 0.0f, 1.0f); }
    static float freqHz (float v) { return 20.0f * std::pow (1000.0f, std::clamp (v, 0.0f, 1.0f)); }
    static float gainDb (float v) { static const float xs[6] { 0.0f, 0.111111f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[6] { -13.9f, -11.6f, -8.0f, 0.0f, 9.2f, 19.0f }; return interpTable (xs, ys, v); }
    static float qOf (float v) { return std::min (40.0f, 0.72f / std::max (0.018f, std::clamp (v, 0.0f, 1.0f))); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "parameq", "PARAM EQ", "EQ",
            "Two fully parametric bands. Q at 0 = very narrow notch / peak, at 100 % = wide.",
            {
                { "VOLUME", 0.35536f,  0, [] (float v) { return fmt (volDb (v), 1, " dB"); } },
                { "FREQ 1", 1.0f / 3.0f, 0, [] (float v) { return fmtHz (freqHz (v)); } },
                { "GAIN 1", 0.111111f, 0, [] (float v) { return fmt (gainDb (v), 1, " dB"); } },
                { "Q 1",    0.0f,      0, [] (float v) { return fmt (qOf (v), 2, ""); } },
                { "FREQ 2", 0.30303f,  0, [] (float v) { return fmtHz (freqHz (v)); } },
                { "GAIN 2", 0.15873f,  0, [] (float v) { return fmt (gainDb (v), 1, " dB"); } },
                { "Q 2",    0.0f,      0, [] (float v) { return fmt (qOf (v), 2, ""); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& q : sm) q.prepare (sr, 0.03);
        sVol.prepare (sr, 0.03);
        setTargets (true); tone (true); reset();
    }
    void reset() override { for (auto& c : f) for (auto& x : c) x.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 7 || same (k[i], v)) return; k[i] = v; setTargets (false); }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& c : f) for (auto& x : c) x.step();
            const float g = sVol.next();
            for (int c = 0; c < 2; ++c) io[c][s] = f[c][2].process (f[c][1].process (f[c][0].process (io[c][s]))) * g;
        }
    }

private:
    // 0 帯域 1 の log f、1 dB、2 log Q、3〜5 帯域 2
    void setTargets (bool snap)
    {
        const float t[6] { std::log (freqHz (k[1])), gainDb (k[2]), std::log (qOf (k[3])), std::log (freqHz (k[4])), gainDb (k[5]), std::log (qOf (k[6])) };
        for (int j = 0; j < 6; ++j) sm[j].set (t[j], snap);
        sVol.set (dbToGain (volDb (k[0])), snap);
    }
    void tone (bool snap)
    {
        float v[6];
        for (int j = 0; j < 6; ++j) { v[j] = sm[j].cur; if (! snap) { for (int i = 0; i < 32; ++i) sm[j].next(); v[j] = settleTo (sm[j]); } }
        bool changed = snap;
        for (int j = 0; j < 6; ++j) if (! same (v[j], last[j])) { changed = true; last[j] = v[j]; }
        if (! changed) { for (auto& c : f) for (auto& x : c) x.ramp (true, 1.0f); return; }   // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
        for (int c = 0; c < 2; ++c)
        {
            for (int b = 0; b < 2; ++b)
            {
                const double q = std::exp ((double) v[3 * b + 2]) * (v[3 * b + 1] < 0.0f ? 0.75 : 1.0);
                f[c][b].peak (sr, std::min ((double) std::exp (v[3 * b]), 0.45 * sr), q, v[3 * b + 1]);
            }
            f[c][2].lp (sr, std::min (16000.0, 0.45 * sr), 0.70710678);
            for (auto& x : f[c]) x.ramp (snap, 32.0f);
        }
    }
    double sr = 48000.0;
    float k[7] { 0.35536f, 1.0f / 3.0f, 0.111111f, 0.0f, 0.30303f, 0.15873f, 0.0f };
    float last[6] { -99, -99, -99, -99, -99, -99 };
    Svf f[2][3];
    Smooth sm[6], sVol;
    unsigned ctl = 0;
};

// ---- GRAPHIC EQ（Guitar Rig の EQ Graphic を測って合わせ込み）----
// 測った結果（cap/mod/EQGraphic）: 8 帯域のピーク。k 番目の周波数 = fmin × (fmax / fmin)^((k − 1) / 7)、
//   fmin = 128 × 24.5^(MIN − 0.5)（0 → 26、1 → 630 Hz）、fmax = 3960 × 3.59^(2 (MAX − 0.5))（0 → 1100、1 → 14200 Hz）。
//   量 = 28 × (BAND − 0.5) × 2 × RANGE dB（RANGE 0.5 で ±14 dB）。Q は上げる 2.08・下げる 1.63（間隔 0.71 オクターブのとき）、間隔が狭いと √ で上がる。OUTPUT = −18.9 + 40 v dB
struct GraphicEq final : Module
{
    static float fMin (float v) { return 128.0f * std::pow (24.5f, std::clamp (v, 0.0f, 1.0f) - 0.5f); }
    static float fMax (float v) { return 3960.0f * std::pow (3.59f, 2.0f * (std::clamp (v, 0.0f, 1.0f) - 0.5f)); }
    static float bandHz (const float* kk, int b)
    {
        const float lo = fMin (kk[1]), hi = std::max (lo * 1.01f, fMax (kk[2]));
        return lo * std::pow (hi / lo, (float) b / 7.0f);
    }
    static std::string bandText (float v, const float* all) { return fmt (28.0f * (v - 0.5f) * 2.0f * (all != nullptr ? all[3] : 0.5f), 1, " dB"); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "graphiceq", "GRAPHIC EQ", "EQ",
            "Eight-band graphic EQ. MIN / MAX FREQ set the lowest and highest band, RANGE scales all bands.",
            {
                { "OUTPUT",   0.5f, 0, [] (float v) { return fmt (-18.9f + 40.0f * v, 1, " dB"); } },
                { "MIN FREQ", 0.5f, 0, [] (float v) { return fmtHz (fMin (v)); } },
                { "MAX FREQ", 0.5f, 0, [] (float v) { return fmtHz (fMax (v)); } },
                { "RANGE",    0.5f, 0, [] (float v) { return fmt (28.0f * v, 0, " dB"); } },
                { "BAND 1",   0.5f,      0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 2",   0.603448f, 0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 3",   0.5f,      0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 4",   0.5f,      0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 5",   0.62069f,  0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 6",   0.5f,      0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 7",   0.5f,      0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
                { "BAND 8",   0.534483f, 0, [] (float v) { return fmt (28.0f * (v - 0.5f), 1, " dB"); }, &bandText },
            } };
        return i;
    }
    GraphicEq() { for (int i = 0; i < 12; ++i) k[i] = info().knobs[(size_t) i].def; }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& q : sm) q.prepare (sr, 0.03);
        sOut.prepare (sr, 0.03);
        setTargets (true); tone (true); reset();
    }
    void reset() override { for (auto& c : f) for (auto& x : c) x.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 12 || same (k[i], v)) return; k[i] = v; setTargets (false); }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& c : f) for (auto& x : c) x.step();
            const float g = sOut.next();
            for (int c = 0; c < 2; ++c)
            {
                float v = io[c][s];
                for (auto& x : f[c]) v = x.process (v);
                io[c][s] = v * g;
            }
        }
    }

private:
    // 0〜7 帯域の dB、8 log fmin、9 log fmax
    void setTargets (bool snap)
    {
        for (int b = 0; b < 8; ++b) sm[b].set (28.0f * (k[4 + b] - 0.5f) * 2.0f * k[3], snap);
        sm[8].set (std::log (fMin (k[1])), snap); sm[9].set (std::log (std::max (fMin (k[1]) * 1.01f, fMax (k[2]))), snap);
        sOut.set (dbToGain (-18.9f + 40.0f * k[0]), snap);
    }
    void tone (bool snap)
    {
        float v[10];
        for (int j = 0; j < 10; ++j) { v[j] = sm[j].cur; if (! snap) { for (int i = 0; i < 32; ++i) sm[j].next(); v[j] = settleTo (sm[j]); } }
        bool changed = snap;
        for (int j = 0; j < 10; ++j) if (! same (v[j], last[j])) { changed = true; last[j] = v[j]; }
        if (! changed) { for (auto& c : f) for (auto& x : c) x.ramp (true, 1.0f); return; }   // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
        const double lo = std::exp ((double) v[8]), hi = std::exp ((double) v[9]);
        const double spacingOct = std::max (0.05, std::log2 (hi / lo) / 7.0);
        const double qScale = std::sqrt (0.708 / spacingOct);
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 8; ++b)
            {
                const double fb = std::min (lo * std::pow (hi / lo, b / 7.0), 0.45 * sr);
                f[c][b].peak (sr, fb, (v[b] < 0.0f ? 1.63 : 2.08) * qScale, v[b]);
                f[c][b].ramp (snap, 32.0f);
            }
    }
    double sr = 48000.0;
    float k[12] {};
    float last[10] { -99, -99, -99, -99, -99, -99, -99, -99, -99, -99 };
    Svf f[2][8];
    Smooth sm[10], sOut;
    unsigned ctl = 0;
};

// ---- SCOOP EQ（Guitar Rig の Custom EQ を測って合わせ込み）----
// 測った結果（cap/mod/CustomEQ）: アンプの音色回路のように TONE と SCOOP が影響し合う（大まか。端で 5〜7 dB ずれる）。
//   TONE = 2.25 × ((1 − t) LP1 + t HP1)、角 = 194 × (544 / 194)^t Hz。SCOOP = FREQ の位置のピーク（下げる）: 表（.25 → −9.0 dB Q 0.61、.5 → −25.2 Q 0.89、.75 → −25.0 Q 0.43、1 → −18.7 Q 0.15）。
//   FREQ（谷の位置、表）0 / .25 / .396 / .75 / 1 → 54 / 167 / 312 / 1074 / 2289 Hz。OUTPUT = 40 (v − 0.495) dB
struct ScoopEq final : Module
{
    static float scoopDb (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 0.0f, -9.0f, -25.2f, -25.0f, -18.7f }; return interpTable (xs, ys, v); }
    static float scoopQ (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 0.61f, 0.61f, 0.89f, 0.43f, 0.15f }; return interpTable (xs, ys, v); }
    static float freqHz (float v) { static const float xs[5] { 0.0f, 0.25f, 0.39583f, 0.75f, 1.0f }, hz[5] { 54.0f, 167.0f, 312.0f, 1074.0f, 2289.0f }; return logTable (xs, hz, v); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "scoopeq", "SCOOP EQ", "EQ",
            "Amp-style tone: TONE tilts lows against highs, SCOOP cuts the mids at FREQ.",
            {
                { "TONE",   0.655f,   0, [] (float v) { return fmtPct (v); } },
                { "OUTPUT", 0.495f,   0, [] (float v) { return fmt (40.0f * (v - 0.495f), 1, " dB"); } },
                { "FREQ",   0.39583f, 0, [] (float v) { return fmtHz (freqHz (v)); } },
                { "SCOOP",  0.27f,    0, [] (float v) { return fmt (scoopDb (v), 1, " dB"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& q : sm) q.prepare (sr, 0.03);
        sOut.prepare (sr, 0.03); sT.prepare (sr, 0.03);
        setTargets (true); tone (true); reset();
    }
    void reset() override { for (auto& x : lp) x.reset(); for (auto& x : hp) x.reset(); for (auto& x : pk) x.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 4 || same (k[i], v)) return; k[i] = v; setTargets (false); }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (int c = 0; c < 2; ++c) { lp[c].step(); hp[c].step(); pk[c].step(); }
            const float g = sOut.next(), t = sT.next();
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][s];
                const float y = 2.25f * ((1.0f - t) * lp[c].process (x) + t * hp[c].process (x));
                io[c][s] = pk[c].process (y) * g;
            }
        }
    }

private:
    // 0 TONE の角（log）、1 谷の log f、2 谷の dB、3 谷の Q
    void setTargets (bool snap)
    {
        const float t = std::clamp (k[0], 0.0f, 1.0f);
        sm[0].set (std::log (194.0f * std::pow (544.0f / 194.0f, t)), snap);
        sm[1].set (std::log (freqHz (k[2])), snap); sm[2].set (scoopDb (k[3]), snap); sm[3].set (scoopQ (k[3]), snap);
        sT.set (t, snap);
        sOut.set (dbToGain (40.0f * (k[1] - 0.495f)), snap);
    }
    void tone (bool snap)
    {
        float v[4];
        for (int j = 0; j < 4; ++j) { v[j] = sm[j].cur; if (! snap) { for (int i = 0; i < 32; ++i) sm[j].next(); v[j] = settleTo (sm[j]); } }
        bool changed = snap;
        for (int j = 0; j < 4; ++j) if (! same (v[j], last[j])) { changed = true; last[j] = v[j]; }
        if (! changed) { for (auto& x : pk) x.ramp (true, 1.0f); return; }   // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
        for (int c = 0; c < 2; ++c)
        {
            lp[c].set (sr, std::exp ((double) v[0]), 1.0, 0.0); hp[c].set (sr, std::exp ((double) v[0]), 0.0, 1.0);
            pk[c].peak (sr, std::exp ((double) v[1]), v[3], v[2]);
            lp[c].ramp (snap, 32); hp[c].ramp (snap, 32); pk[c].ramp (snap, 32.0f);
        }
    }
    double sr = 48000.0;
    float k[4] { 0.655f, 0.495f, 0.39583f, 0.27f };
    float last[4] { -99, -99, -99, -99 };
    TptShelf1 lp[2], hp[2];
    Svf pk[2];
    Smooth sm[4], sOut, sT;
    unsigned ctl = 0;
};
} // namespace aiofx
