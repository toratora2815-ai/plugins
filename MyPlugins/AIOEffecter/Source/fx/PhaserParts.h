#pragma once
// フェイザー（0.7.0〜）。全通過フィルター（1 次）を何段もつないで、元の音と混ぜて谷を作る。JUCE なし
#include "DelayKit.h"
#include "TwinDelay.h"   // 音符の表（SYNC）
#include "VerbEngine.h"  // Svf（CLASSIC の入口の 15 Hz HP）

namespace aiofx
{
// ---- CLASSIC PHASER（Guitar Rig の Phaser Nine を測って合わせ込み）----
// 測った結果（cap/modi/PhaserNine。50 ms ごとのインパルスの返りの大きさに、段の形を当てはめた。本物は全体が 16 サンプル遅れて出る → こちらは遅れなし）:
//   反転する 1 次の全通過（低い所 −1、高い所 +1）を M 段。NOTCHES = 段の数（0〜2 → 2 段、3〜8 → 3〜8 段、8 から上は 8 段）。谷の数は 3 段 1 つ、4・5 段 2 つ、6・7 段 3 つ、8 段 4 つ
//   （奇数の段は COLOR 0 なら低い所でも打ち消し合う = 低音が下がる。COLOR を上げると逆に低い所が持ち上がる: 輪の段が奇数なので直流で輪の利得が +0.96 × COLOR^1.15。
//   本物も同じで COLOR 1 なら 3 Hz +25 dB・20 Hz +20 dB。こちらは入口に 15 Hz の 4 次のハイパス（バターワース）を 1 回だけ入れて、ドライにも揺れる側にも同じ音を使う
//   = 20 Hz より下（直流）だけが消え、ドライと揺れる側の打ち消し合い方は本物のまま（20 Hz で −0.4 dB、10 Hz −14 dB、5 Hz −38 dB。
//   揺れる側だけに入れたときは位相がずれて、初期値で 16〜25 Hz が +6〜8 dB・COLOR 0 の奇数段で 80 Hz に −26 dB の谷になった: DSP 検査 0.7.0 の 1 回目・2 回目）
//   くり返し（COLOR）は最後の段の出口 → 3 段目の入口（輪 = M − 2 段）、強さ = −0.96 × COLOR^1.15
//   （山 +6.6 / +7.8 / +9.8 / +13.8 / +27.5 dB @ COLOR 0 / .25 / .5 / .75 / 1）。出口 = 2 × ((1 − DRY WET) × x ＋ DRY WET × ウェット)（DRY WET 0 で +6 dB のドライ）
//   当てはまり: 5 段 0.9〜1.2 dB、3〜8 段 1.3〜1.8 dB、2 段 2.6 dB（1 発ごとの大きさの差の rms）
//   揺れ: 全通過の角の周波数の log2 が三角で動く（一番低い所から始まる）。三角は 1 次のローパス 3.2 Hz を通る（RATE 1 = 10.2 Hz で幅 0.3 倍）
//   速さ RATE 0 / .25 / .5 / .75 / 1 → 0.118 / 0.244 / 1.069 / 1.926 / 10.2 Hz。中心 = 8.93 + 4.0 × FREQUENCY（log2 Hz）、幅 = 4.0 × DEPTH オクターブ（.25 で 0.92）
//   上は 5 kHz あたりで頭打ち: log2 fc = v（v ≤ 11.2）、上は 11.2 + 1.45 (1 − e^(−(v − 11.2) / 1.45))
//   ROTATE: 右の揺れの位相 = 左 + 360° × (ROTATE − 0.5)。INVERT: ウェットの向き
struct ClassicPhaser final : Module
{
    static constexpr int kMaxStages = 8;
    static float rateHz (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, lg[5] { -0.9281f, -0.6126f, 0.0290f, 0.2846f, 1.0086f };   // log10 Hz
        return std::pow (10.0f, interpTable (xs, lg, std::clamp (v, 0.0f, 1.0f)));
    }
    static int stagesOf (float v)
    {
        const int n = std::clamp ((int) std::lround (v * 12.0f), 0, 12);
        return n <= 2 ? 2 : std::min (n, 8);
    }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (rateHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "classicph", "CLASSIC PHASER", "MODULATION",
            "Orange-box style phaser. NOTCHES sets how many stages, COLOR adds the ringing feedback, DEPTH and FREQUENCY set the sweep.",
            {
                { "SYNC",      0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "RATE",      0.5f,      0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); }, &rateText },
                { "NOTCHES",   5.0f / 12.0f, 13, [] (float v) { return std::to_string (stagesOf (v)) + " stages"; } },
                { "COLOR",     0.55064f,  0, [] (float v) { return fmtPct (v); } },
                { "INVERT",    0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "ROTATE",    0.5f,      0, [] (float v) { return fmt (360.0f * (v - 0.5f), 0, " deg"); } },
                { "DRY WET",   0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "DEPTH",     0.30833f,  0, [] (float v) { return fmtPct (v); } },
                { "FREQUENCY", 0.47578f,  0, [] (float v) { return fmtHz (std::pow (2.0f, std::min (8.93f + 4.0f * v, 11.2f + 1.45f * (1.0f - std::exp (-(std::max (0.0f, 8.93f + 4.0f * v - 11.2f)) / 1.45f))))); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sCenter, &sRange, &sFb, &sDry, &sWet, &sRot }) q->prepare (sr, 0.03);
        sRate.prepare (sr, 0.05);
        lfoA = 1.0f - std::exp (-2.0f * (float) kPiD * 3.2f / (float) sr);
        xfStep = 1.0f / (float) std::max (1.0, 0.1 * sr);
        for (auto& hh : inHp) { hh[0].hp (sr, 15.0, 0.5411961); hh[1].hp (sr, 15.0, 1.3065630); for (auto& h : hh) h.ramp (true, 1.0f); }
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& sl : z) for (auto& c : sl) for (auto& v : c) v = 0.0f;
        for (auto& hh : inHp) for (auto& h : hh) h.reset();
        slot = 0;
        ph = 0.0; syncPending = false; lfo[0] = lfo[1] = 0.0f;
        curM = wantM; xf = 1.0f;
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 9 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double p = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[1]));
        syncPh = p - std::floor (p); syncPending = true; sRate.cur = sRate.target;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending) { ph = syncPh; syncPending = false; }
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            // 段の数が変わったら、前の段数と新しい段数の出口を 100 ms のなめらかな形（smoothstep）でクロスフェードし、
            // 新しい組のくり返しは 0 から上げる（COLOR が高いと 30 ms の直線では 3→8 段で 19.6 のプチッ。この形で 1.1: DSP 検査 0.7.0）
            if (xf >= 1.0f && wantM != curM)
            {
                nxtM = wantM; xf = 0.0f;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < kMaxStages; ++i) z[slot ^ 1][c][i] = i < curM ? z[slot][c][i] : 0.0f;
            }
            float a = 0.0f;
            if (xf < 1.0f) { xf = std::min (1.0f, xf + xfStep); a = xf * xf * (3.0f - 2.0f * xf); }
            const float hz = sRate.next(), cen = sCenter.next(), rng = sRange.next(), fb = sFb.next(), dry = sDry.next(), wet = sWet.next(), rot = sRot.next();
            ph += hz / sr; ph -= std::floor (ph);
            for (int c = 0; c < 2; ++c)
            {
                double p = ph + (c == 1 ? (double) rot : 0.0); p -= std::floor (p);
                const float tri = (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
                lfo[c] += lfoA * (tri - lfo[c]);
                const float v = cen + rng * (lfo[c] - 0.5f);
                const float lg = v <= 11.2f ? v : 11.2f + 1.45f * (1.0f - std::exp (-(v - 11.2f) / 1.45f));
                const float fc = std::min (std::pow (2.0f, lg), 0.45f * (float) sr);
                const float g = std::tan ((float) kPiD * fc / (float) sr), G = g / (1.0f + g);
                const float x = inHp[c][1].process (inHp[c][0].process (io[c][s]));   // 入口で 1 回だけ 20 Hz より下を止める（ドライにも同じ音）
                float w = chain (z[slot][c], curM, x, fb, G);
                if (a > 0.0f) w += a * (chain (z[slot ^ 1][c], nxtM, x, fb * a, G) - w);
                io[c][s] = dry * x + wet * w;
            }
            if (xf >= 1.0f && a > 0.0f) { curM = nxtM; slot ^= 1; }
        }
    }
    double tailSeconds() const override { return 0.5; }

private:
    // M 段の鎖（くり返しは M 段目の出口 → 3 段目の入口。遅れなしで解く）。zc = この鎖の状態
    // 1 段 = 反転する全通過: y = x − 2 LP(x)、LP は TPT の 1 次（lp = G x + (1 − G) z、z ← 2 lp − z）
    static inline float chain (float* zc, int M, float x, float fb, float G)
    {
        auto stage = [G] (float in, float& zz) { const float lp = G * in + (1.0f - G) * zz; zz = flush (2.0f * lp - zz); return in - 2.0f * lp; };
        float u = x;
        const int k0 = M >= 3 ? 2 : M;   // 輪に入らない前の段（3 段目から輪。2 段だけのときは輪なし）
        for (int i = 0; i < k0; ++i) u = stage (u, zc[i]);
        if (k0 < M)
        {
            // 輪の中の段を「出口 = A u + B」にまとめて、u + fb × 出口 を一度に解く
            float A = 1.0f, B = 0.0f;
            for (int i = k0; i < M; ++i)
            {
                const float a1 = 1.0f - 2.0f * G, b1 = -2.0f * (1.0f - G) * zc[i];
                A = a1 * A; B = a1 * B + b1;
            }
            const float w0 = (A * u + B) / (1.0f - fb * A);   // |fb| ≤ 0.96、|A| ≤ 1 なので 0 にならない
            float in = u + fb * w0;
            for (int i = k0; i < M; ++i) in = stage (in, zc[i]);
            u = in;
        }
        return u;
    }
    void recalc (bool snap)
    {
        float hz = rateHz (k[1]);
        if (k[0] >= 0.5f) hz = (float) (std::max (5.0, tempo) / 60.0 / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[1])));
        sRate.set (hz, snap);
        wantM = stagesOf (k[2]);
        sFb.set (-0.96f * std::pow (std::clamp (k[3], 0.0f, 1.0f), 1.15f), snap);
        const float inv = k[4] >= 0.5f ? -1.0f : 1.0f, dw = std::clamp (k[6], 0.0f, 1.0f);
        sDry.set (2.0f * (1.0f - dw), snap); sWet.set (2.0f * dw * inv, snap);
        sRot.set (k[5] - 0.5f, snap);
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, rg[5] { 0.0f, 0.92f, 1.92f, 2.96f, 4.0f };
        sRange.set (interpTable (xs, rg, std::clamp (k[7], 0.0f, 1.0f)), snap);
        sCenter.set (8.93f + 4.0f * std::clamp (k[8], 0.0f, 1.0f), snap);
        if (snap) { curM = nxtM = wantM; xf = 1.0f; }
    }
    double sr = 48000.0, tempo = 120.0, ph = 0.0, syncPh = 0.0;
    bool syncPending = false;
    float k[9] { 0.0f, 0.5f, 5.0f / 12.0f, 0.55064f, 0.0f, 0.5f, 0.5f, 0.30833f, 0.47578f };
    float z[2][2][kMaxStages] {};   // [クロスフェードの組][左右][段]
    int slot = 0;
    float lfo[2] {}, lfoA = 0.0004f, xf = 1.0f, xfStep = 0.001f;
    int wantM = 5, curM = 5, nxtM = 5;
    Svf inHp[2][2];   // [左右][4 次の 2 段]
    Smooth sRate, sCenter, sRange, sFb, sDry, sWet, sRot;
};
// ---- SWEEP PHASER（Guitar Rig の Stoned Phaser を測って合わせ込み。大まかな版）----
// 測った結果（cap/modi/StonedPhaser、本物は 16 サンプル遅れ → こちらは遅れなし）:
//   ふつうの 1 次の全通過（低い所 +1、高い所 −1）を M 段、くり返しは鎖全体（−0.141）、出口 = (1 − MIX) x ＋ MIX × 1.38 × ウェット（0.5〜1.0 dB）
//   NOTCHES 0・1 → 2 段、2 → 4、3 → 6、4 → 8、5 から上 → 10 段（0.75〜1.5 dB）
//   揺れ: 角の周波数の log2 が三角（一番低い所から）、速さ = 0.01 × 1000^RATE Hz、下限 = 2^(6.18 + 4.54 × SWEEP MIN) Hz、上限 = 2^(10.86 + 4.46 × SWEEP MAX) Hz
//   ROTATE: 右の位相 = 左 + 180° × ROTATE。COLOR は ON / OFF（ON の形はまだ合っていない: 輪を 1 段・+0.10 にした仮の形、2.4 dB）。
//   MODE・INVERT は本物を測った範囲では音が変わらなかった → こちらも変えない
struct SweepPhaser final : Module
{
    static constexpr int kMaxStages = 10;
    static float rateHz (float v) { return 0.01f * std::pow (1000.0f, std::clamp (v, 0.0f, 1.0f)); }
    static int stagesOf (float v)
    {
        const int n = std::clamp ((int) std::lround (v * 12.0f), 0, 12);
        return n <= 1 ? 2 : std::min (2 * n, 10);
    }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (rateHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "sweepph", "SWEEP PHASER", "MODULATION",
            "Wide-sweeping phaser. SWEEP MIN and SWEEP MAX set where the notches travel, NOTCHES how many, COLOR a different shape of notches.",
            {
                { "SYNC",      0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "NOTCHES",   2.0f / 12.0f, 13, [] (float v) { return std::to_string (stagesOf (v)) + " stages"; } },
                { "RATE",      0.481413f, 0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); }, &rateText },
                { "COLOR",     0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "MODE",      0.4f,      0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "SWEEP MIN", 0.5f,      0, [] (float v) { return fmtHz (std::pow (2.0f, 6.18f + 4.54f * v)); } },
                { "SWEEP MAX", 0.5f,      0, [] (float v) { return fmtHz (std::min (20000.0f, std::pow (2.0f, 10.86f + 4.46f * v))); } },
                { "ROTATE",    0.0f,      0, [] (float v) { return fmt (180.0f * v, 0, " deg"); } },
                { "INVERT",    0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sLo, &sHi, &sFb, &sFb1, &sDry, &sWet, &sRot }) q->prepare (sr, 0.03);
        sRate.prepare (sr, 0.05);
        xfStep = 1.0f / (float) std::max (1.0, 0.1 * sr);
        quietNeed = (int) (0.05 * sr);
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& sl : z) for (auto& c : sl) for (auto& v : c) v = 0.0f;
        slot = 0; ph = 0.0; errPh = 0.0; syncPending = false; curM = nxtM = wantM; xf = 1.0f; quietRun = 1 << 30;
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 10 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double p = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2]));
        syncPh = p - std::floor (p); syncPending = true; sRate.cur = sRate.target;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending)
        {
            // 曲の位置との差を 50 ms で埋める（一気に合わせると揺れの位置が飛んでプチッ: ループで戻る・音符を変える・再生開始で 82〜1,316。DSP 検査 0.7.0）。
            // 入口が 50 ms 以上無音のとき（全通過の中も空）と、差がごく小さいときだけ一気に合わせる
            const double e = syncPh - ph - std::floor (syncPh - ph + 0.5);
            if (quietRun >= quietNeed || std::abs (e) < 1.0e-4) { ph = syncPh; errPh = 0.0; } else errPh = e;
            syncPending = false;
        }
        // 追いかけの時定数は 0.1 / 揺れの速さ（0.05〜2 秒）。50 ms 固定では揺れが遅いとき一瞬 10 Hz で谷が走った（DSP 検査 0.7.0 の 2 回目）
        const double gk = 1.0 - std::exp (-1.0 / (std::clamp (0.1 / std::max (1.0e-3, (double) sRate.target), 0.05, 2.0) * sr));
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if (xf >= 1.0f && wantM != curM)
            {
                nxtM = wantM; xf = 0.0f;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < kMaxStages; ++i) z[slot ^ 1][c][i] = i < curM ? z[slot][c][i] : 0.0f;
            }
            float a = 0.0f;
            if (xf < 1.0f) { xf = std::min (1.0f, xf + xfStep); a = xf * xf * (3.0f - 2.0f * xf); }
            const float hz = sRate.next(), lo = sLo.next(), hi = sHi.next(), fb = sFb.next(), fb1 = sFb1.next(), dry = sDry.next(), wet = sWet.next(), rot = sRot.next();
            // 1 サンプルの補正は本来の速さの −0.5〜+1 倍まで（追いかけの間も速さは 0.5〜2 倍 = 逆に回らない。頭打ちなしでは一瞬 4〜6 倍・逆回り: DSP 検査 0.7.0 の 3 回目）
            const double cph = std::clamp (errPh * gk, -0.5 * (double) hz / sr, 1.0 * (double) hz / sr); errPh -= cph;
            ph += hz / sr + cph; ph -= std::floor (ph);
            quietRun = (std::abs (L[s]) < 1.0e-6f && std::abs (R[s]) < 1.0e-6f) ? std::min (quietRun + 1, 1 << 30) : 0;
            for (int c = 0; c < 2; ++c)
            {
                double p = ph + (c == 1 ? (double) rot : 0.0); p -= std::floor (p);
                const float tri = (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
                const float fc = std::min (std::pow (2.0f, lo + (hi - lo) * tri), 0.45f * (float) sr);
                const float g = std::tan ((float) kPiD * fc / (float) sr), G = g / (1.0f + g);
                const float x = io[c][s];
                float w = chain (z[slot][c], curM, x, fb, fb1, G);
                if (a > 0.0f) w += a * (chain (z[slot ^ 1][c], nxtM, x, fb * a, fb1 * a, G) - w);
                io[c][s] = dry * x + wet * w;
            }
            if (xf >= 1.0f && a > 0.0f) { curM = nxtM; slot ^= 1; }
        }
    }
    double tailSeconds() const override { return 0.3; }

private:
    // M 段（ふつうの全通過 y = 2 LP(x) − x）。くり返し: 鎖全体に fb、最後の 1 段だけに fb1（COLOR ON の仮の形）。遅れなしで解く
    static inline float chain (float* zc, int M, float x, float fb, float fb1, float G)
    {
        auto stage = [G] (float in, float& zz) { const float lp = G * in + (1.0f - G) * zz; zz = flush (2.0f * lp - zz); return 2.0f * lp - in; };
        const float a1 = 2.0f * G - 1.0f;
        // 1 段の形: 出口 = a1 × 入口 ＋ b（b = 2 (1 − G) z）
        // 最後の段の輪（fb1）: 出口 o = a1 (i + fb1 o) + b → o = (a1 i + b) / (1 − fb1 a1)
        float A = 1.0f, B = 0.0f;
        for (int i = 0; i < M; ++i)
        {
            const float b1 = 2.0f * (1.0f - G) * zc[i];
            if (i == M - 1 && fb1 != 0.0f) { const float d = 1.0f / (1.0f - fb1 * a1); A = a1 * d * A; B = (a1 * B + b1) * d; }
            else { A = a1 * A; B = a1 * B + b1; }
        }
        const float w0 = (A * x + B) / (1.0f - fb * A);   // |fb| < 1、|A| ≤ 1
        float in = x + fb * w0;
        for (int i = 0; i < M - 1; ++i) in = stage (in, zc[i]);
        // 最後の段（fb1 の輪を解いた値で状態を進める）
        const float b1 = 2.0f * (1.0f - G) * zc[M - 1];
        const float o = (a1 * in + b1) / (1.0f - fb1 * a1);
        stage (in + fb1 * o, zc[M - 1]);
        return o;
    }
    void recalc (bool snap)
    {
        float hz = rateHz (k[2]);
        if (k[0] >= 0.5f) hz = (float) (std::max (5.0, tempo) / 60.0 / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2])));
        sRate.set (hz, snap);
        wantM = stagesOf (k[1]);
        const bool color = k[3] >= 0.5f;
        sFb.set (color ? 0.0f : -0.141f, snap); sFb1.set (color ? 0.10f : 0.0f, snap);
        const float m = std::clamp (k[5], 0.0f, 1.0f);
        sDry.set (1.0f - m, snap); sWet.set (1.38f * m, snap);
        sLo.set (6.18f + 4.54f * std::clamp (k[6], 0.0f, 1.0f), snap);
        sHi.set (std::min (14.2f, 10.86f + 4.46f * std::clamp (k[7], 0.0f, 1.0f)), snap);
        sRot.set (0.5f * std::clamp (k[8], 0.0f, 1.0f), snap);
        if (snap) { curM = nxtM = wantM; xf = 1.0f; }
    }
    double sr = 48000.0, tempo = 120.0, ph = 0.0, syncPh = 0.0, errPh = 0.0;
    bool syncPending = false;
    int quietRun = 1 << 30, quietNeed = 2400;
    float k[10] { 0.0f, 2.0f / 12.0f, 0.481413f, 0.0f, 0.4f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f };
    float z[2][2][kMaxStages] {};
    int slot = 0, wantM = 4, curM = 4, nxtM = 4;
    float xf = 1.0f, xfStep = 0.001f;
    Smooth sRate, sLo, sHi, sFb, sFb1, sDry, sWet, sRot;
};
} // namespace aiofx
