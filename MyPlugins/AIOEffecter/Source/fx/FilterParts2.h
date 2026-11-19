#pragma once
// フィルターの続き（0.9.0〜）: 音量で動くフィルター。JUCE なし
#include "FilterParts.h"

namespace aiofx
{
// ---- TOUCH FILTER（Guitar Rig の Auto Filter を測って合わせ込み。大まかな版）----
// 測った結果（cap/dyn/AutoFilter: 1 kHz の音量の階段 −60〜0 dBFS・段（−40 → −10 → −40）・ピンクノイズ −60 / −40 / −20 dBFS。list25・list26）:
//   入口の山の大きさ（ピーク）を追う → dB → 制御 c = clip(((L − T) / 12.3)^0.62 ＋ o(OFFSET), 0, 1)、T = −15.8 − 20.7 × SENS dBFS（SENS で始まる音量が 21 dB 動く。曲線の形は同じ）
//   角 = 下限 × (上限 / 下限)^c（UP）。下限 46 × 3.65^RANGE Hz、上限 952 × 4.08^RANGE Hz（RANGE 0.536 で 89〜1991 Hz）
//   OFFSET: 0.3 までは静かな所の角は下限のまま、上げると 0.4 / 0.5 / 0.6 / 0.8 / 1 で c = 0.25 / 0.44 / 0.58 / 0.79 / 0.95
//   DIRECTION 0 = DOWN（静かな所 2727 Hz → 大きいと 272 Hz へ下がる）、1〜3 = UP（同じ音）
//   ATTACK: 時定数 9.5 × 13.6^ATTACK ms（初期値 0.72 で 63 ms）、RELEASE: 75 / 169 / 322 / 388 ms（0 / .5 / .77 / 1）。
//   山の記憶は角が上で止まる音量（T ＋ 12.3 dB）で頭打ち（本物は 0 dBFS のあとでも 0.6 秒で下まで戻る）
//   形: 状態変数フィルター、MODE で LP（0）→ BP（0.5）→ HP（1）を直線に（分子の係数の表。角が低い所と高い所で少し違う → 角で補間）、
//   RES → Q 0.98 × 5.37^RES（角 1.8 kHz）/ 1.03 × 3.67^RES（角 90 Hz）。MIX = ドライとの直線の混ぜ。SIDECHAIN はこのラックに外の音の入口がないので効かない
class TouchFilter final : public Module
{
public:
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "touchflt", "TOUCH FILTER", "FILTER",
            "Envelope filter: the harder you play, the more the filter opens (or closes with DIRECTION DOWN). SENS sets how easily it reacts.",
            {
                { "SENS",      0.40248f, 0, [] (float v) { return fmtPct (v); } },
                { "DIRECTION", 1.0f / 3.0f, 4, [] (float v) { return std::string (std::lround (v * 3.0f) == 0 ? "DOWN" : "UP"); } },
                { "RANGE",     0.53607f, 0, [] (float v) { return fmtHz (952.0f * std::pow (4.08f, std::clamp (v, 0.0f, 1.0f))); } },
                { "RES",       0.55664f, 0, [] (float v) { return fmt (0.98f * std::pow (5.37f, std::clamp (v, 0.0f, 1.0f)), 1, " Q"); } },
                { "MODE",      0.62007f, 0, [] (float v) { return v < 0.125f ? std::string ("LP") : v < 0.375f ? std::string ("LP-BP") : v < 0.625f ? std::string ("BP") : v < 0.875f ? std::string ("BP-HP") : std::string ("HP"); } },
                { "ATTACK",    0.72188f, 0, [] (float v) { return fmt (9.5f * std::pow (13.6f, std::clamp (v, 0.0f, 1.0f)), 0, " ms"); } },
                { "RELEASE",   0.77109f, 0, [] (float v) { return fmt (1000.0f * relSec (v), 0, " ms"); } },
                { "OFFSET",    0.18047f, 0, [] (float v) { return fmtPct (v); } },
                { "MIX",       1.0f,     0, [] (float v) { return fmtPct (v); } },
                { "SIDECHAIN", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON (no input)" : "OFF"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sRes, &sMode, &sMix, &sRange, &sSens, &sOff, &sDir }) q->prepare (sr, 0.03);
        recalc (true); reset();
    }
    void reset() override { for (auto& f : flt) f.reset(); env = 0.0f; ctl = 0; update (true); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 10 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            // 入口の山を追う（ATTACK / RELEASE）
            const float a = std::max (std::abs (L[s]), std::abs (R[s]));
            env += (a > env ? atk : rel) * (a - env);
            env = flush (std::min (env, envMax));   // 角が上で止まる音量より上は覚えない（本物も大きな音のあとすぐ戻る）
            if ((ctl++ & 7u) == 0u) update (false);   // 8 サンプルごとに角と係数（その間は直線）
            const float mix = sMix.next();   // MIX は毎サンプル（8 サンプルごとの段だとプチッ 12）
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                flt[c].step();
                const float x = io[s], w = flt[c].process (x);
                io[s] = x + mix * (w - x);
            }
        }
    }

private:
    // RELEASE の時定数（秒）: 段（−10 → −40 dBFS）の戻りを本物に合わせた表（0 / 0.5 / 0.77 / 1 → 75 / 169 / 322 / 388 ms、対数で補間）
    static float relSec (float r)
    {
        static const float xs[4] { 0.0f, 0.5f, 0.77f, 1.0f }, lg[4] { -2.5903f, -1.7778f, -1.1332f, -0.9467f };   // ln 秒
        return std::exp (interpTable (xs, lg, std::clamp (r, 0.0f, 1.0f)));
    }
    static float offsetC (float o)
    {
        static const float xs[7] { 0.0f, 0.3f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f }, cs[7] { -0.06f, 0.0f, 0.254f, 0.436f, 0.575f, 0.79f, 0.953f };
        return interpTable (xs, cs, std::clamp (o, 0.0f, 1.0f));
    }
    void recalc (bool snap)
    {
        const float ka = std::clamp (k[5], 0.0f, 1.0f), kr = std::clamp (k[6], 0.0f, 1.0f);
        atk = 1.0f - std::exp (-1.0f / (0.0095f * std::pow (13.6f, ka) * (float) sr));
        rel = 1.0f - std::exp (-1.0f / (relSec (kr) * (float) sr));
        sSens.set (std::clamp (k[0], 0.0f, 1.0f), snap); sRange.set (std::clamp (k[2], 0.0f, 1.0f), snap); sRes.set (std::clamp (k[3], 0.0f, 1.0f), snap);
        sMode.set (std::clamp (k[4], 0.0f, 1.0f), snap); sOff.set (offsetC (k[7]), snap); sMix.set (std::clamp (k[8], 0.0f, 1.0f), snap);
        sDir.set (std::lround (std::clamp (k[1], 0.0f, 1.0f) * 3.0f) == 0 ? 1.0f : 0.0f, snap);   // DOWN ↔ UP は 30 ms でつなぐ（一気に替えると角が飛んでプチッ 39）
    }
    // 分子の係数（LP・BP・HP の a0・a1・a2）: MODE の表、角が低い所（90 Hz）と高い所（1.8 kHz）
    static void numer (float mode, float w, float& a0, float& a1, float& a2)
    {
        static const float lo[9][3] { { 1.27f, 0.02f, 0.0f }, { 0.92f, 0.36f, 0.0f }, { 0.55f, 0.73f, 0.0f }, { 0.19f, 1.08f, 0.0f }, { -0.15f, 1.38f, 0.01f },
                                      { -0.11f, 1.00f, 0.36f }, { -0.07f, 0.61f, 0.71f }, { -0.02f, 0.23f, 1.07f }, { 0.03f, -0.14f, 1.43f } };
        static const float hi[9][3] { { 1.39f, 0.28f, 0.01f }, { 1.03f, 0.56f, 0.04f }, { 0.68f, 0.88f, 0.07f }, { 0.33f, 1.20f, 0.10f }, { -0.01f, 1.41f, 0.15f },
                                      { -0.01f, 1.06f, 0.44f }, { -0.01f, 0.70f, 0.78f }, { 0.0f, 0.35f, 1.13f }, { 0.0f, -0.01f, 1.47f } };
        const float u = std::clamp (mode, 0.0f, 1.0f) * 8.0f; const int i = std::min (7, (int) u); const float f = u - (float) i;
        float r[3];
        for (int j = 0; j < 3; ++j)
        {
            const float l = lo[i][j] + f * (lo[i + 1][j] - lo[i][j]), h = hi[i][j] + f * (hi[i + 1][j] - hi[i][j]);
            r[j] = l + w * (h - l);
        }
        a0 = r[0]; a1 = r[1]; a2 = r[2];
    }
    void update (bool snap)
    {
        if (! snap) for (int i = 0; i < 8; ++i) { sRes.next(); sMode.next(); sRange.next(); sSens.next(); sOff.next(); sDir.next(); }
        const float range = sRange.cur, sens = sSens.cur;
        const float lev = 20.0f * std::log10 (std::max (env, 1.0e-6f));
        const float thr = -19.3f - 20.7f * sens;   // 山の追い方の違い（こちらは正弦波で −1.7 dB に落ち着く）を足して、本物の階段に合わせた
        envMax = std::pow (10.0f, (thr + 12.3f) / 20.0f);
        const float x = lev - thr;
        const float c = std::clamp ((x > 0.0f ? std::pow (x / 12.3f, 0.62f) : 0.0f) + sOff.cur, 0.0f, 1.0f);
        const float fmin = 46.0f * std::pow (3.65f, range), fmax = 952.0f * std::pow (4.08f, range);
        const float m = std::pow (3.65f, range - 0.536f), fhi = 2727.0f * m, flo = 272.0f * m;
        const float lu = std::log (fmin) + c * std::log (fmax / fmin), ld = std::log (fhi) + c * std::log (flo / fhi);
        float hz = std::exp (lu + sDir.cur * (ld - lu));
        hz = std::clamp (hz, 20.0f, 0.45f * (float) sr);
        const float w = std::clamp (std::log (hz / 90.0f) / std::log (1840.0f / 90.0f), 0.0f, 1.0f);
        const float res = sRes.cur;
        const float q = (1.03f * std::pow (3.67f, res)) * (1.0f - w) + (0.98f * std::pow (5.37f, res)) * w, kk = 1.0f / q;
        float a0, a1, a2; numer (sMode.cur, w, a0, a1, a2);
        const float g = WahPedal::gOf (sr, hz);
        for (auto& f : flt)
        {
            f.tg = g; f.tk = kk; f.tm0 = a2; f.tm1 = a1 - a2 * kk; f.tm2 = a0 - a2;
            f.ramp (snap, 8.0f);
        }
    }
    double sr = 48000.0;
    float k[10] { 0.40248f, 1.0f / 3.0f, 0.53607f, 0.55664f, 0.62007f, 0.72188f, 0.77109f, 0.18047f, 1.0f, 0.0f };
    float env = 0.0f, envMax = 1.0f, atk = 0.01f, rel = 0.001f;
    Svf flt[2];
    Smooth sRes, sMode, sMix, sRange, sSens, sOff, sDir;
    unsigned ctl = 0;
};

// ---- PEAK FILTER（Guitar Rig の Peak Filter を測って合わせ込み。大まかな版）----
// 測った結果（cap/dyn/PeakFilter のピンクノイズ −40 dBFS = リミッターがかからない所。list25・list27 の 49 通り、どれも 2 次 1 本で 0.01〜0.15 dB）:
//   山 = PUMP の音量 × base × (S² ＋ S / Qn ＋ 1) / (S² ＋ S / Qd ＋ 1)（ベル型）。山の高さ R = Qd / Qn、幅 κ = √(Qd Qn)（= 戻りのある帯域通過の形）
//   FREQ: 69.6 × 283^FREQ Hz（1 で 20 kHz = 何もしない、+4.9 dB）。MIX: 山の高さ（0 は素通し、0.25 / 0.5 / 0.75 / 1 で ln R = 0.99 / 1.93 / 2.71 / 3.40）
//   ln R = (MIX の表) × h(FREQ) × e(EDGE)、κ = (EDGE の表) × (FREQ の表)、base は FREQ × MIX の表 × EDGE の補正（組み合わせで 1〜2 dB ずれる所あり）
//   KILL（1〜3 は同じ）: 1.76 × (1 − d × 帯域通過)（深い谷。MIX で d と幅）。PUMP は形を変えずに音量だけ（0.241 → 0.803、初期値 0.426）
//   最後にリミッター（波形を崩さず音量を下げる型）: 天井 0.655（−3.7 dBFS）、効き 1〜3 ms、戻り 0.3 秒ほど。STEREO はない（左右同じ処理）
class PeakFilter final : public Module
{
public:
    static float freqHz (float v) { return 69.6f * std::pow (283.0f, std::clamp (v, 0.0f, 1.0f)); }
    // 表示は実際に山が来る周波数（表の値はアナログの Hz。0.9 で 11.2 → 実際 9.67 kHz: DSP 検査 0.9.0）。
    // KILL ON のときは FREQ 1 でも高い所が削れる（10 kHz −13.6 dB）ので「OFF」と出さない
    static std::string freqText (float v, const float* all)
    {
        const bool kill = all != nullptr && std::lround (std::clamp (all[4], 0.0f, 1.0f) * 3.0f) != 0;
        if (v >= 0.98f && ! kill) return "OFF";
        return fmtHz (WahPedal::digitalHz (freqHz (v)));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "peakflt", "PEAK FILTER", "FILTER",
            "Resonant peak that you can push hard: FREQ sets where, MIX how tall, EDGE how narrow, PUMP how loud into the built-in limiter. KILL turns the peak into a deep notch.",
            {
                { "FREQ", 0.14893f, 0, [] (float v) { return v >= 0.98f ? std::string ("OFF") : fmtHz (WahPedal::digitalHz (freqHz (v))); }, &freqText },
                { "EDGE", 0.73138f, 0, [] (float v) { return fmtPct (v); } },
                { "PUMP", 0.61968f, 0, [] (float v) { return fmtPct (v); } },
                { "MIX",  0.0f,     0, [] (float v) { return fmtPct (v); } },
                { "KILL", 0.0f,     4, [] (float v) { return std::string (std::lround (v * 3.0f) == 0 ? "OFF" : "ON"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sE, &sP, &sK }) q->prepare (sr, 0.03);
        sF.prepare (sr, 0.02); sM.prepare (sr, 0.05);
        limA = 1.0f - std::exp (-1.0f / (0.0002f * (float) sr)); limR = 1.0f - std::exp (-1.0f / (0.3f * (float) sr));
        recalc (true); reset();
    }
    void reset() override { for (auto& f : flt) f.reset(); for (auto& f : fltK) f.reset(); lenv = 0.0f; ctl = 0; update (true); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 5 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            dryW += dryStep; wetW += wetStep; kW += kStep;
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                flt[c].step(); fltK[c].step();
                const float x = io[s];
                const float pk = flt[c].process (x), kl = fltK[c].process (x);   // 山と谷を並べて回し、KILL で混ぜる（係数を混ぜるとプチッ 12）
                y[c] = dryW * x + wetW * (pk + kW * (kl - pk));
            }
            // リミッター（左右の大きいほうの山を追う。効き 0.2 ms・戻り 0.3 秒、天井 0.655）
            const float pk = std::max (std::abs (y[0]), std::abs (y[1]));
            lenv += (pk > lenv ? limA : limR) * (pk - lenv);
            lenv = flush (lenv);
            const float g = (lenv > 0.655f ? 0.655f / lenv : 1.0f);
            const float lw = wetW;   // MIX 0（素通し）ではリミッターも通さない
            L[s] = y[0] * (1.0f + lw * (g - 1.0f)); R[s] = y[1] * (1.0f + lw * (g - 1.0f));
        }
    }

private:
    void recalc (bool snap)
    {
        fGoal = std::clamp (k[0], 0.0f, 1.0f);
        if (snap) { fLin = fGoal; sF.set (fLin, true); }
        sE.set (std::clamp (k[1], 0.0f, 1.0f), snap); sP.set (std::clamp (k[2], 0.0f, 1.0f), snap);
        sM.set (std::clamp (k[3], 0.0f, 1.0f), snap); sK.set (std::lround (std::clamp (k[4], 0.0f, 1.0f) * 3.0f) == 0 ? 0.0f : 1.0f, snap);
    }
    void update (bool snap)
    {
        if (! snap)
        {
            // FREQ は端から端まで最短 0.15 秒（鋭い山が一瞬で 8 オクターブ動くと「チッ」29）
            const float maxStep = 16.0f / (float) (0.15 * sr);
            fLin += std::clamp (fGoal - fLin, -maxStep, maxStep);
            sF.set (fLin);
            for (int i = 0; i < 16; ++i) { sF.next(); sE.next(); sP.next(); sM.next(); sK.next(); }
        }
        const float F = sF.cur, E = sE.cur, P = sP.cur, Mx = sM.cur, K = sK.cur;
        static const float fx[6] { 0.0f, 0.149f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float hF[6] { 0.875f, 1.0f, 1.025f, 1.0f, 0.636f, 0.0f };
        static const float kF[6] { 0.55f, 0.647f, 0.72f, 0.97f, 1.44f, 1.44f };
        static const float ex[5] { 0.0f, 0.25f, 0.731f, 0.75f, 1.0f };
        static const float eE[5] { 1.113f, 1.079f, 1.0f, 0.995f, 0.926f };
        static const float kE[5] { 0.170f, 0.252f, 0.647f, 0.67f, 1.53f };
        static const float bE[5] { 1.057f, 1.051f, 1.0f, 0.997f, 0.937f };   // EDGE: base が 1.765 から下がる量の倍率
        static const float mx[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float lnR[5] { 0.0f, 0.993f, 1.93f, 2.71f, 3.40f };
        // base: FREQ × MIX の表（FREQ 0 / .149 / .5 / .75 / 1、MIX .25 / .5 / .75 / 1）。MIX 0 へは 1.70 へ延ばす
        static const float bfx[5] { 0.0f, 0.149f, 0.5f, 0.75f, 1.0f };
        static const float bt[5][5] { { 1.70f, 1.325f, 0.933f, 0.575f, 0.265f }, { 1.70f, 1.369f, 1.014f, 0.699f, 0.426f }, { 1.70f, 1.431f, 1.134f, 0.873f, 0.647f },
                                      { 1.71f, 1.512f, 1.286f, 1.089f, 0.917f }, { 1.765f, 1.765f, 1.765f, 1.765f, 1.765f } };
        static const float px[6] { 0.0f, 0.25f, 0.5f, 0.61968f, 0.75f, 1.0f }, pg[6] { 0.241f, 0.292f, 0.371f, 0.426f, 0.508f, 0.803f };
        const float hz = std::min (freqHz (F), 0.45f * (float) sr);
        const float lr = interpTable (mx, lnR, Mx) * interpTable (fx, hF, F) * interpTable (ex, eE, E);
        const float Rr = std::exp (lr), kap = interpTable (ex, kE, E) * interpTable (fx, kF, F) / 0.647f;
        // base の表（双線形）
        const int fi = std::min (3, (int) (std::lower_bound (bfx, bfx + 5, F) - bfx) - (F > 0.0f ? 1 : 0));
        const int fj = std::clamp (fi, 0, 3); const float fw = std::clamp ((F - bfx[fj]) / (bfx[fj + 1] - bfx[fj]), 0.0f, 1.0f);
        const float b0 = interpTable (mx, bt[fj], Mx), b1 = interpTable (mx, bt[fj + 1], Mx);
        const float base = 1.765f - (1.765f - (b0 + fw * (b1 - b0))) * interpTable (ex, bE, E);
        const float pump = interpTable (px, pg, P) / 0.426f;
        // 山: base × (S² ＋ S √R / κ ＋ 1) / (S² ＋ S / (κ √R) ＋ 1)。KILL: 1.76 × (1 − d BP)（Qk は MIX で 0.65 → 0.11、d 0.78 → 0.99）
        const float sq = std::sqrt (Rr), Qd = kap * sq, Qn = kap / sq;
        static const float kq[5] { 1.0f, 0.65f, 0.33f, 0.18f, 0.11f }, kd[5] { 0.0f, 0.778f, 0.941f, 0.981f, 0.993f };
        static const float qE[5] { 0.45f, 0.55f, 1.0f, 1.05f, 3.1f };   // KILL の谷の幅の EDGE による倍率（E 1 で 0.11 → 0.31〜0.37）
        const float Qk = interpTable (mx, kq, Mx) * interpTable (ex, qE, E), dk = interpTable (mx, kd, Mx);
        // 山（分子 a0 = a2 = base、a1 = base / Qn、分母 Qd）と谷（1.76 × (1 − d BP)、分母 Qk）を別々に作って、出口を KILL で混ぜる
        const float aP0 = base * pump, aP1 = base * pump / Qn;
        const float aK0 = 1.76f * pump, aK1 = 1.76f * pump * (1.0f - dk) / Qk;
        const float g = WahPedal::gOf (sr, hz), kP = 1.0f / Qd, kK = 1.0f / Qk;
        for (auto& f : flt) { f.tg = g; f.tk = kP; f.tm0 = aP0; f.tm1 = aP1 - aP0 * kP; f.tm2 = 0.0f; f.ramp (snap, 16.0f); }
        for (auto& f : fltK) { f.tg = g; f.tk = kK; f.tm0 = aK0; f.tm1 = aK1 - aK0 * kK; f.tm2 = 0.0f; f.ramp (snap, 16.0f); }
        if (snap) { kW = K; kStep = 0.0f; } else kStep = (K - kW) / 16.0f;
        // MIX 0 は素通し（本物も MIX 0 で 1.0）。0 から 5 % までで素通し → 山へ混ぜる（本物は MIX を少し上げると +4.6 dB に飛ぶ）
        const float wT = std::clamp (Mx / 0.05f, 0.0f, 1.0f);
        if (snap) { wetW = wT; dryW = 1.0f - wT; wetStep = dryStep = 0.0f; }
        else { wetStep = (wT - wetW) / 16.0f; dryStep = ((1.0f - wT) - dryW) / 16.0f; }
    }
    double sr = 48000.0;
    float k[5] { 0.14893f, 0.73138f, 0.61968f, 0.0f, 0.0f };
    float lenv = 0.0f, limA = 0.01f, limR = 0.0001f, wetW = 0.0f, dryW = 1.0f, wetStep = 0.0f, dryStep = 0.0f, kW = 0.0f, kStep = 0.0f, fGoal = 0.14893f, fLin = 0.14893f;
    Svf flt[2], fltK[2];
    Smooth sF, sE, sP, sM, sK;
    unsigned ctl = 0;
};
} // namespace aiofx
