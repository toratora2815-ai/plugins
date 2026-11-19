#pragma once
// 揺れもの（MODULATION）の部品。2026-11-19〜。DelayKit の部品を使う
#include "DelayKit.h"
#include "TwinDelay.h"   // 音符の表（SYNC）

namespace aiofx
{
// ---- TREMOLO（Guitar Rig の Tremolo を測って合わせ込み）----
// 測った結果（cap/mod/Tremolo、440 Hz の正弦波の大きさの揺れ）:
//   揺れ u（0..1、1 = いちばん下げる）は四角い波を「上がり UP・下がり DOWN の時定数」でなまらせた形（コンデンサーの充電・放電）。
//   時定数 = 周期 × 0.00592 × (e^(5.4 × つまみ) − 1)（0 = すぐ、0.5 = 周期の 8 %、1 = 周期の 1.3 倍 = 上がりきらず浅くなる）。
//   WIDTH = 上（下げる側）にいる時間の割合 0.92 − 0.84 × WIDTH。大きさ = 1 − INTENSITY × u（1 で全部切れる）。
//   速さ = 0.2559 × 64^RATE Hz（初期値 0.74 → 5.55 Hz）。PAN 0 = モノ、1〜3 = 右は逆向き（オートパン。2・3 の違いは見えない）
struct Tremolo final : Module
{
    static float rateHz (float v) { return 0.2559f * std::pow (64.0f, std::clamp (v, 0.0f, 1.0f)); }
    static float tauPer (float v) { return 0.00592f * (std::exp (5.4f * std::clamp (v, 0.0f, 1.0f)) - 1.0f); }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (rateHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tremolo", "TREMOLO", "MODULATION",
            "Square-ish tremolo / auto-pan. UP and DOWN round off the edges, WIDTH sets how long it stays down.",
            {
                { "SYNC",      0.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "WIDTH",     0.5f,  0, [] (float v) { return fmtPct (v); } },
                { "RATE",      0.74f, 0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); }, &rateText },
                { "PAN",       1.0f / 3.0f, 4, [] (float v) { return std::string (std::lround (v * 3.0f) == 0 ? "MONO" : "STEREO"); } },
                { "INTENSITY", 0.36f, 0, [] (float v) { return fmtPct (v); } },
                { "DOWN",      0.5f,  0, [] (float v) { return fmtPct (v); } },
                { "UP",        0.5f,  0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sInt, &sDuty, &sTauU, &sTauD, &sStereo }) q->prepare (sr, 0.02);
        sRate.prepare (sr, 0.05);
        recalc (true); reset();
    }
    void reset() override { ph = 0.0; u = 0.0f; syncPending = false; }
    void setKnob (int i, float v) override { if (i < 0 || i >= 7 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    // SYNC ON で再生中は、揺れの位置を曲の位置にそろえる（拍の頭で「上がっている側」が始まる。位置を飛ばしても、なめらかな UP / DOWN でプチッとしない）
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double p = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2])) + (double) sDuty.target;
        syncPh = p - std::floor (p); syncPending = true;
        // そろえている間は速さのなめらかを目標ちょうどに（テンポを変えた直後、古い速さで角を越えてから次のブロックで引き戻されると、
        // 角が二重になって刻みが一瞬つっかえた: DSP 検査 0.5.0 の 2 回目）
        sRate.cur = sRate.target;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending) { ph = syncPh; syncPending = false; }
        for (int s = 0; s < n; ++s)
        {
            const float hz = sRate.next(), inten = sInt.next(), duty = sDuty.next(), tu = sTauU.next(), td = sTauD.next(), st = sStereo.next();
            ph += hz / sr; ph -= std::floor (ph);
            // 四角い波（周期の前の duty だけ「下げる側」）→ 1 次の追いかけ（上がり・下がりで時定数が別）
            const float target = ph < duty ? 1.0f : 0.0f;
            // 秒。UP / DOWN 0 でも 1 ms はかける（一気に切り替わるとプチッと鳴る。本物の測定ではこの短さは見分けられない）
            const float tau = std::max (1.0e-3f, (target > u ? tu : td) / std::max (0.01f, hz));
            const float a = std::exp (-1.0f / (tau * (float) sr));
            u = target + (u - target) * a;
            const float gl = 1.0f - inten * u;
            const float gr = 1.0f - inten * (u + st * (1.0f - 2.0f * u));   // ステレオ: 右は 1 − u
            L[s] *= gl; R[s] *= gr;
        }
    }

private:
    void recalc (bool snap)
    {
        float hz = rateHz (k[2]);
        if (k[0] >= 0.5f) hz = (float) (std::max (5.0, tempo) / 60.0 / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2])));
        sRate.set (hz, snap);
        sInt.set (std::clamp (k[4], 0.0f, 1.0f), snap);
        sDuty.set (std::clamp (0.92f - 0.84f * k[1], 0.02f, 0.98f), snap);
        sTauU.set (tauPer (k[6]), snap); sTauD.set (tauPer (k[5]), snap);
        sStereo.set (std::lround (k[3] * 3.0f) == 0 ? 0.0f : 1.0f, snap);
    }
    double sr = 48000.0, tempo = 120.0, ph = 0.0, syncPh = 0.0;
    float k[7] { 0.0f, 0.5f, 0.74f, 1.0f / 3.0f, 0.36f, 0.5f, 0.5f };
    float u = 0.0f;
    bool syncPending = false;
    Smooth sRate, sInt, sDuty, sTauU, sTauD, sStereo;
};

// ---- RING MOD（Guitar Rig の Ringmod を測って合わせ込み）----
// 測った結果（cap/mod/Ringmod。440 Hz の正弦波を通した出口の成分を、山の前後 ±4 Hz のエネルギーで読んだ）:
//   運び手の高さ log10 f = 1.9988 + 0.65414 v + 1.26984 v²（0 → 99.7 Hz、初期値 0.452 → 358 Hz、0.5 → 440 Hz、1 → 8.37 kHz。測った 6 点と 0.01 % 以内）
//   ウェット = x × (A + c)。c = 運び手（1・2・3・5・6・7 倍音。4・8 倍音はない形）
//   RAW v（r = 1.25 v − 0.25 v²）: 1 倍音 0.7006 − 0.4082 v + 0.0582 v²、ほかの倍音は r × (0.3289, 0.1048, 0.0536, 0.0819, 0.0322)、A = 0.6556 − 0.1311 v
//   PITCH: 倍音ごとに高さで少し変わる（表。8.4 kHz では 1 倍音 −9.6 dB、2 倍音 −15.7 dB）、A も（低い所 +0.6 dB、8.4 kHz で −2 dB）
//   AM/RM（0 / .25 / .5 / .75 / 1）: A × 1 / .710 / .446 / .210 / 0、運び手 × 1 / 1.183 / 1.339 / 1.469 / 1.571。0 = AM、1 = リング変調
//   DRY/WET: ドライ 1 − v²、ウェット 1 − (1 − v)²（真ん中でどちらも 0.75）。本物の初期値は DRY/WET 0（素通し）
//   倍音がサンプルレートの 0.40〜0.45 倍に来たら消していく（本物は折り返したまま）
struct RingMod final : Module
{
    static float carrierHz (float v) { v = std::clamp (v, 0.0f, 1.0f); return std::pow (10.0f, 1.998806f + 0.654144f * v + 1.269838f * v * v); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "ringmod", "RING MOD", "MODULATION",
            "Ring / amplitude modulator. PITCH sets the carrier, RAW adds its harmonics, AM/RM keeps or removes the original note.",
            {
                { "PITCH",   0.45213f,  0, [] (float v) { return fmtHz (carrierHz (v)); } },
                { "RAW",     0.270771f, 0, [] (float v) { return fmtPct (v); } },
                { "AM/RM",   0.0f,      0, [] (float v) { return v < 0.01f ? std::string ("AM") : v > 0.99f ? std::string ("RM") : fmtPct (v); } },
                { "DRY/WET", 0.0f,      0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sHz, &sA, &sDry, &sWet }) q->prepare (sr, 0.02);
        for (auto& q : sG) q.prepare (sr, 0.02);
        for (auto& d : dcb) d.prepare (sr, 10.0);
        recalc (true); reset();
    }
    void reset() override { ph = 0.0; for (auto& d : dcb) d.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 4 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float hz = sHz.next(), a = sA.next(), dry = sDry.next(), wet = sWet.next();
            float g[6]; for (int q = 0; q < 6; ++q) g[q] = sG[q].next();
            const double inc = hz / sr;
            ph += inc; ph -= std::floor (ph);
            const float w = (float) (2.0 * kPiD * ph), s1 = std::sin (w), t = 2.0f * std::cos (w);
            const float s2 = t * s1, s3 = t * s2 - s1, s4 = t * s3 - s2, s5 = t * s4 - s3, s6 = t * s5 - s4, s7 = t * s6 - s5;
            const float x = (float) inc;
            const auto fade = [x] (float m) { return std::clamp ((0.45f - m * x) * 20.0f, 0.0f, 1.0f); };
            const float c = g[0] * s1 + fade (2.0f) * g[1] * s2 + fade (3.0f) * g[2] * s3
                          + fade (5.0f) * g[3] * s5 + fade (6.0f) * g[4] * s6 + fade (7.0f) * g[5] * s7;
            // 運び手を掛けた分（c x）だけ直流止め 10 Hz: 入口と運び手の高さが近いと、差の音（20 Hz より下）が大きく出て
            // ヘッドルームを食い後ろのコンプを揺らすため（110 Hz × 110.5 Hz の RM で −5 dBFS → −35 dBFS。元の音 A x は変わらない: DSP 検査 0.5.0）
            const float yl = dry * L[s] + wet * (a * L[s] + dcb[0].process (c * L[s]));
            const float yr = dry * R[s] + wet * (a * R[s] + dcb[1].process (c * R[s]));
            L[s] = yl; R[s] = yr;
        }
    }

private:
    static float db (float d) { return std::pow (10.0f, d / 20.0f); }
    void recalc (bool snap)
    {
        const float pv = std::clamp (k[0], 0.0f, 1.0f), raw = std::clamp (k[1], 0.0f, 1.0f), am = std::clamp (k[2], 0.0f, 1.0f), dw = std::clamp (k[3], 0.0f, 1.0f);
        sHz.set (carrierHz (pv), snap);
        // PITCH ごとの差（dB、初期値 0.452 = 358 Hz の時が 0）。1 の所の 3 倍音から上はナイキストより上で測れないので、手前からの延長
        static const float xp[5] { 0.0f, 0.25f, 0.45213f, 0.75f, 1.0f };
        static const float pA[5] { 0.62f, 0.61f, 0.0f, 0.10f, -1.99f };
        static const float pK[6][5] { { 0.00f, 0.05f, 0.0f, -1.80f,  -9.56f }, { -0.20f, 0.44f, 0.0f, -3.11f, -15.65f }, { -0.62f, 0.48f, 0.0f, -4.74f, -20.0f },
                                      { -1.48f, 0.90f, 0.0f, -6.95f, -20.0f }, { -1.72f, 1.11f, 0.0f, -7.68f, -20.0f }, { -2.01f, 1.29f, 0.0f, -8.26f, -20.0f } };
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float mA[5] { 1.0f, 0.7097f, 0.4464f, 0.2098f, 0.0f }, mC[5] { 1.0f, 1.1829f, 1.3392f, 1.4687f, 1.5713f };
        static const float wK[5] { 0.3289f, 0.1048f, 0.0536f, 0.0819f, 0.0322f };
        const float r = 1.25f * raw - 0.25f * raw * raw, cm = interpTable (xs, mC, am);
        sA.set ((0.6556f - 0.1311f * raw) * interpTable (xs, mA, am) * db (interpTable (xp, pA, pv)), snap);
        sG[0].set ((0.7006f - 0.4082f * raw + 0.0582f * raw * raw) * cm * db (interpTable (xp, pK[0], pv)), snap);
        for (int q = 1; q < 6; ++q) sG[q].set (r * wK[q - 1] * cm * db (interpTable (xp, pK[q], pv)), snap);
        sDry.set (1.0f - dw * dw, snap); sWet.set (1.0f - (1.0f - dw) * (1.0f - dw), snap);
    }
    double sr = 48000.0, ph = 0.0;
    float k[4] { 0.45213f, 0.270771f, 0.0f, 0.0f };
    Smooth sHz, sA, sDry, sWet, sG[6];
    DcBlock dcb[2];
};

// ---- FLANGER（Guitar Rig の Flanger を測って合わせ込み）----
// 測った結果（cap/modi/Flanger と追加の cap/list14.tsv。50 ms ごとのインパルスの返りで、こだまの遅れ・大きさ・音色を 1 発ずつ読んだ）:
//   揺れ = 四角い波を 1 次のローパス 2 段（fc = 0.993 × min(速さ, 3.28 Hz)）でなまらせた形（3 倍音 6.6 %・位相 −0.92 rad まで本物と同じ）。
//   速さ = 0.1 × 100^RATE Hz（初期値 0.097 → 0.156 Hz）。3.28 Hz から上はなまらせ方が追いつかず浅くなる（5.6 Hz で半分、10 Hz で 1/5）
//   遅れ = 基本 + 1.745 S + S / 0.3158 / 2 × なまった四角（S = 揺れの 1 倍音の大きさ = Smax × WIDTH²）
//   MODE: 0 = 基本 4.41 ms・Smax 3.36 ms / 1 = 1.40 ms・2.48 ms / 2 = 1.39 ms・3.98 ms（くり返しが強い）
//   こだま = −HP1 500 Hz × (0.084 + 1.081 I × LP1 7.6 kHz)（I = INTENSITY。MODE 1 は 1.26 × 10^((0.8 − 1.2 I) / 20)・LP 8 kHz、
//   MODE 2 は 0.96 × 10^((1.1 − 1.8 I) / 20)・LP 8〜13 kHz）。くり返し −0.093（MODE 2 −0.78 = 本物の 1 周 0.724（高い所）に、補間の高い音の減りと HP の分を足して 1 周の強さを合わせた。戻りは HP だけ通す）
//   ドライ = 1 次の高い棚（7.2 / 7.6 / 6.9 kHz）。低い所は INTENSITY で下がる: MODE 0 は 0.933 → 0.447（1 で −7 dB）、MODE 1 は 0.969 → 0.060（−24 dB）、
//   MODE 2 は 0.349 → 0.228（はじめから −9 dB）。表は ModParts の recalc。MODE 1・2 のこだまは INTENSITY ではほとんど変わらない
//   STEREO: 右はこだまが逆向き・ドライが × 1.081（MODE 1: 1.098、MODE 2: 1.86）。揺れは左右で同じ
struct Flanger final : Module
{
    static float rateHz (float v) { return 0.1f * std::pow (100.0f, std::clamp (v, 0.0f, 1.0f)); }
    static int modeOf (float v) { return std::clamp ((int) std::lround (v * 2.0f), 0, 2); }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (rateHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "flanger", "FLANGER", "MODULATION",
            "Classic sweeping flanger. MODE picks the delay range and how much it rings, WIDTH sets the sweep, INTENSITY the depth of the notches.",
            {
                { "SYNC",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "MODE",      0.0f,     3, [] (float v) { static const char* n[3] { "CLASSIC", "DEEP", "RINGING" }; return std::string (n[modeOf (v)]); } },
                { "RATE",      0.09677f, 0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); }, &rateText },
                { "INTENSITY", 0.66935f, 0, [] (float v) { return fmtPct (v); } },
                { "WIDTH",     0.66936f, 0, [] (float v) { return fmtPct (v); } },
                { "STEREO",    1.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.05 * sr) + 16);
        for (auto& t : tap) t.prepare (sr, 0.04);
        for (auto* q : { &sWet0, &sWet1, &sFb, &sStW, &sStD }) q->prepare (sr, 0.02);
        for (auto* q : { &sGL, &sGH, &sSh, &sLp }) q->prepare (sr, 0.02);
        sS.prepare (sr, 0.05);
        sRate.prepare (sr, 0.05);
        for (auto& f : hp) { f.setOnePoleHP (sr, 500.0); f.ramp (true); }
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : dryF) f.reset();
        for (auto& t : tap) t.snapNow();
        back[0] = back[1] = 0.0f; ph = 0.0; z1 = z2 = 0.0f; syncPending = false;
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 6 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    // SYNC ON で再生中は、四角い波の位置を曲の位置にそろえる（拍の頭で上がり始める。なまらせる 2 段のローパスがあるので飛んでもプチッとしない）
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double p = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2]));
        syncPh = p - std::floor (p); syncPending = true;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending) { ph = syncPh; syncPending = false; }
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& f : hp) f.step();
            for (auto& f : lp) f.step();
            for (auto& f : dryF) f.step();
            const float hz = sRate.next(), S = sS.next(), w0 = sWet0.next(), w1 = sWet1.next(), fb = sFb.next(), stW = sStW.next(), stD = sStD.next();
            // 揺れ: 四角い波 → 1 次のローパス 2 段（fc = 0.993 × min(速さ, 3.28 Hz)）
            ph += hz / sr; ph -= std::floor (ph);
            const float sq = ph < 0.5 ? 1.0f : -1.0f;
            const float a = 1.0f - std::exp (-2.0f * (float) kPiD * 0.993f * std::min (hz, 3.28f) / (float) sr);
            z1 += a * (sq - z1); z2 += a * (z1 - z2);
            // 基本の遅れ（MODE で変わる）は XTap で 40 ms のクロスフェード（MODE を切り替えてもプチッとしない・音程がすべらない）。揺れはその上に足す
            const float mod = (1.745f * S + (S / 0.3158f) * 0.5f * z2) * 0.001f * (float) sr;
            float* io[2] { L, R };
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][s];
                line[(size_t) c].write (flush (softKnee2 (x + fb * back[c])));
                const float v = hp[c].process (tap[c].read (line[(size_t) c], mod));
                const float vl = lp[c].process (v);
                back[c] = v;   // 戻りは HP だけを通した音（本物は 2 回目以降も高い音が減らない: 1 周で高い所 −2.8 dB = 0.724、MODE 0・1 は −20.6 dB）
                const float wet = -(w0 * v + w1 * vl);
                const float dry = dryF[c].process (x);
                io[c][s] = c == 0 ? dry + wet : stD * dry + stW * wet;
            }
        }
    }
    double tailSeconds() const override { return 0.5; }

private:
    void recalc (bool snap)
    {
        const int m = modeOf (k[1]);
        float hz = rateHz (k[2]);
        if (k[0] >= 0.5f) hz = (float) (std::max (5.0, tempo) / 60.0 / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[2])));
        sRate.set (hz, snap);
        static const float baseMs[3] { 4.41f, 1.40f, 1.39f }, sMax[3] { 3.361f, 2.478f, 3.981f }, wetK[3] { 1.02f, 1.560f, 1.19f }, fbK[3] { -0.093f, -0.093f, -0.78f };
        static const float stDry[3] { 1.081f, 1.098f, 1.86f };
        const float I = std::clamp (k[3], 0.0f, 1.0f), W = std::clamp (k[4], 0.0f, 1.0f);
        for (auto& t : tap) t.set (baseMs[m] * 0.001f * (float) sr, snap);
        sS.set (sMax[m] * W * W, snap);
        // こだま: MODE 0 は INTENSITY で強くなる（0.084 + 1.081 I）。MODE 1・2 は INTENSITY ではほとんど変わらない（1 で −1.2 / −1.8 dB）（cap/list17.tsv）
        if (m == 0) { sWet0.set (0.084f * wetK[0], snap); sWet1.set (1.081f * I * wetK[0], snap); }
        else if (m == 1) { sWet0.set (0.0f, snap); sWet1.set (1.259f * dbToGain (0.8f - 1.2f * I), snap); }
        else { sWet0.set (0.0f, snap); sWet1.set (0.960f * dbToGain (1.1f - 1.8f * I), snap); }
        sFb.set (fbK[m], snap);
        const bool st = k[5] >= 0.5f;
        sStW.set (st ? -1.0f : 1.0f, snap); sStD.set (st ? stDry[m] : 1.0f, snap);
        // ドライの棚・こだまの LP は、なめらかにした値から 32 サンプルごとに（INTENSITY・MODE を一気に動かしてもプチッとしない）
        // ドライ = 1 次の高い棚。低い所と 16 kHz の大きさを INTENSITY 0 / .25 / .5 / .75 / 1 で測った表（MODE 0 は Intensity_*、1・2 は cap/list17.tsv）。
        // 高い所の大きさ gH は、16 kHz の値と棚の角から逆算
        static const float lpHz[3] { 7600.0f, 8000.0f, 10000.0f }, shHz[3] { 7200.0f, 7600.0f, 6900.0f };
        static const float xi[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float lo[3][5] { { 0.933f, 0.813f, 0.692f, 0.569f, 0.447f }, { 0.969f, 0.698f, 0.436f, 0.184f, 0.060f }, { 0.349f, 0.301f, 0.265f, 0.236f, 0.228f } };
        static const float h16[3][5] { { 0.933f, 0.912f, 0.891f, 0.871f, 0.861f }, { 0.971f, 0.911f, 0.860f, 0.818f, 0.785f }, { 0.753f, 0.649f, 0.571f, 0.509f, 0.406f } };
        const float gL = interpTable (xi, lo[m], I), g16 = interpTable (xi, h16[m], I);
        // x はデジタルの棚の値（双一次変換の tan。アナログの 16000 / 角 だと 48 kHz で 16 kHz が表より 0.4〜0.6 dB 大きく出た）
        const double fx = std::min (16000.0, 0.45 * sr);
        const float xr = (float) (std::tan (kPiD * fx / sr) / std::tan (kPiD * std::min ((double) shHz[m], 0.45 * sr) / sr)), x2 = xr * xr;
        const float gH = std::sqrt (std::max (gL * gL, (g16 * g16 * (1.0f + x2) - gL * gL) / x2));
        sGL.set (gL, snap); sGH.set (gH, snap);
        // こだまの LP: MODE 2 は INTENSITY で開く（0 で 8 kHz、1 で 13 kHz）
        sSh.set (shHz[m], snap); sLp.set (m == 2 ? 8000.0f + 5000.0f * I : lpHz[m], snap);
        if (snap) tone (true);
    }
    void tone (bool snap)
    {
        if (! snap)
        {
            sGL.next32(); sGH.next32(); sSh.next32(); sLp.next32();
            if (same (sGL.cur, last[0]) && same (sGH.cur, last[1]) && same (sSh.cur, last[2]) && same (sLp.cur, last[3])) return;
        }
        last[0] = sGL.cur; last[1] = sGH.cur; last[2] = sSh.cur; last[3] = sLp.cur;
        for (int c = 0; c < 2; ++c)
        {
            lp[c].setOnePoleLP (sr, sLp.cur); lp[c].ramp (snap);
            dryF[c].set (sr, sSh.cur, sGL.cur, sGH.cur); dryF[c].ramp (snap);
        }
    }
    struct Smooth32 : Smooth { void next32() { for (int i = 0; i < 32; ++i) next(); } };
    double sr = 48000.0, tempo = 120.0, ph = 0.0, syncPh = 0.0;
    bool syncPending = false;
    float k[6] { 0.0f, 0.0f, 0.09677f, 0.66935f, 0.66936f, 1.0f };
    std::array<FracDelay, 2> line;
    XTap tap[2];
    TptShelf1 hp[2], lp[2], dryF[2];
    Smooth sRate, sS, sWet0, sWet1, sFb, sStW, sStD;
    Smooth32 sGL, sGH, sSh, sLp;
    float back[2] {}, z1 = 0.0f, z2 = 0.0f, last[4] { -1.0f, -1.0f, -1.0f, -1.0f };
    unsigned ctl = 0;
};
} // namespace aiofx
