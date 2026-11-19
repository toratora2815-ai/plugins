#pragma once
// コーラス系の揺れもの（0.6.0〜）。DelayKit の部品を使う
#include "DelayKit.h"
#include "TwinDelay.h"   // 音符の表（SYNC）
#include "VerbEngine.h"  // Svf（TPT の状態変数フィルター）
#include "EnsembleTone.h" // VIBE ENSEMBLE の音色の表（自動生成）

namespace aiofx
{
// ---- ELECTRIC LADY（Guitar Rig の Electric Lady を測って合わせ込み）----
// 測った結果（cap/modi/ElectricLady と追加の cap/list15.tsv・list16.tsv。50 ms ごとのインパルスの返り）:
//   揺れ = 三角の波（遅れ 0.235 ms 〜 3.37 + 6.09 × DEPTH ms）を 1 次のローパス 2.32 Hz に通した形（速い所で浅くなる: 4.46 Hz で 0.46 倍、11.4 Hz で 0.2 倍）。
//   速さは RATE の表（0 → 0.028 Hz、0.405 → 0.225 Hz、0.6 → 1.02 Hz、0.75 → 4.46 Hz、1 → 11.43 Hz で頭打ち）。STATIC ON で遅れ 0.466 + 7.277 × DEPTH ms に止める
//   くり返しは「混ぜたあと」から遅れの入口へ戻す: 中 = a x + b v、v = 遅れ（x + g × HP(a x + b m^(−e) v)）、出口 = G × 中。
//   a = 1.187 (1 − MIX)、b = 1.146 MIX、g = 0.805 × COLOR（戻りは 1 次の HP 150 Hz）。戻りのこだまの分は MIX が小さいほど強め（m^(−e)、
//   e = COLOR 0 / .25 / .5 / .75 / 1 で 0 / 0.11 / 0.25 / 0.47 / 0.80。MIX × COLOR の 25 通りの 2 回目 / 1 回目が合う）。音量 G = G0(COLOR)^(1 − MIX) × G1(COLOR)^MIX
//   （G0: 0 → 1、0.25 → 1.194、0.5 → 1.469、0.75 → 0.839、1 → 0.810。G1: 0〜0.5 → 1、0.75 → 0.421、1 → 0.181。0.5 と 0.6 の間で段がある）
//   ROTATE: 右の揺れの位相 = 左 + 360° × (ROTATE − 0.5)。INVERT: こだまの向き
struct ElectricLady final : Module
{
    static float rateHz (float v)
    {
        static const float xs[14] { 0.0f, 0.25f, 0.405f, 0.5f, 0.55f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f };
        static const float hz[14] { 0.0277f, 0.0889f, 0.2248f, 0.440f, 0.656f, 1.017f, 1.632f, 2.696f, 4.460f, 6.969f, 9.504f, 10.989f, 11.400f, 11.428f };
        float lg[14]; for (int i = 0; i < 14; ++i) lg[i] = std::log10 (hz[i]);
        return std::pow (10.0f, interpTable (xs, lg, std::clamp (v, 0.0f, 1.0f)));
    }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (rateHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "electricfl", "ELECTRIC FLANGE", "MODULATION",
            "Vintage pedal-style flanger. COLOR feeds the output back for a ringing sweep, STATIC parks the sweep at DEPTH, ROTATE offsets the right side.",
            {
                { "SYNC",   0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "COLOR",  0.36f,  0, [] (float v) { return fmtPct (v); } },
                { "DEPTH",  0.82f,  0, [] (float v) { return fmtPct (v); } },
                { "RATE",   0.405f, 0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); }, &rateText },
                { "STATIC", 0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "INVERT", 0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "ROTATE", 0.5f,   0, [] (float v) { return fmt (360.0f * (v - 0.5f), 0, " deg"); } },
                { "MIX",    0.5f,   0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.03 * sr) + 16);
        for (auto* q : { &sA, &sB, &sBf, &sG, &sOut, &sInv, &sLo, &sHi, &sStat, &sStatMs, &sRot }) q->prepare (sr, 0.02);
        sRate.prepare (sr, 0.05);
        for (auto& f : fbHp) { f.setOnePoleHP (sr, 150.0); f.ramp (true); }
        lpA = 1.0f - std::exp (-2.0f * (float) kPiD * 2.32f / (float) sr);
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& f : fbHp) f.reset();
        ph = 0.0; syncPending = false;
        // ローパスの中身は三角の真ん中から（つないだ直後に大きく動かないように）
        const float c = 0.5f * (sLo.cur + sHi.cur);
        lfo[0] = lfo[1] = c;
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 8 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    // SYNC ON で再生中は、三角の位置を曲の位置にそろえる（拍の頭でいちばん短い遅れ。後ろの 2.32 Hz のローパスで飛びはなめらかになる）
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double p = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[3]));
        syncPh = p - std::floor (p); syncPending = true;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending) { ph = syncPh; syncPending = false; }
        for (int s = 0; s < n; ++s)
        {
            const float hz = sRate.next(), a = sA.next(), b = sB.next(), bf = sBf.next(), g = sG.next(), go = sOut.next(), inv = sInv.next();
            const float lo = sLo.next(), hi = sHi.next(), st = sStat.next(), stMs = sStatMs.next(), rot = sRot.next();
            ph += hz / sr; ph -= std::floor (ph);
            float* io[2] { L, R };
            for (int c = 0; c < 2; ++c)
            {
                // 三角の波（0..1）→ 遅れ（ms）→ 1 次のローパス 2.32 Hz
                double p = ph + (c == 1 ? (double) rot : 0.0); p -= std::floor (p);
                const float tri = (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
                lfo[c] += lpA * (lo + (hi - lo) * tri - lfo[c]);
                const float dMs = lfo[c] + st * (stMs - lfo[c]);
                const float x = io[c][s];
                // 先に読む（書く前なので d − 1 で、書いた後の d と同じ位置）→ 同じサンプルの「混ぜた音」を戻す（1 サンプルずれると 1 回目のこだまが小さくなる）
                const float v = inv * line[(size_t) c].read (std::max (1.0f, dMs * 0.001f * (float) sr - 1.0f));
                const float mid = a * x + b * v;
                // 輪の頭打ちは ±4 から（本物は COLOR 1・STATIC で鳴きの山に −12 dBFS を入れても倍音 −98 dB 以下 = 歪まない。±1 からだと −17 dB 歪んだ: DSP 検査 0.6.0、cap/list20.tsv）
                line[(size_t) c].write (flush (4.0f * softKnee2 (0.25f * (x + g * fbHp[c].process (a * x + bf * v)))));
                io[c][s] = go * mid;
            }
        }
    }
    double tailSeconds() const override { return 1.0; }

private:
    void recalc (bool snap)
    {
        const float color = std::clamp (k[1], 0.0f, 1.0f), depth = std::clamp (k[2], 0.0f, 1.0f), mix = std::clamp (k[7], 0.0f, 1.0f);
        float hz = rateHz (k[3]);
        if (k[0] >= 0.5f) hz = (float) (std::max (5.0, tempo) / 60.0 / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[3])));
        sRate.set (hz, snap);
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ex[5] { 0.0f, 0.11f, 0.25f, 0.47f, 0.80f };
        sA.set (1.187f * (1.0f - mix), snap); sB.set (1.146f * mix, snap); sG.set (0.805f * color, snap);
        sBf.set (1.146f * std::pow (std::max (mix, 1.0e-3f), 1.0f - interpTable (xs, ex, color)), snap);
        static const float g0[5] { 1.0f, 1.194f, 1.469f, 0.839f, 0.810f }, g1[5] { 1.0f, 1.0f, 1.0f, 0.421f, 0.181f };
        const float G0 = interpTable (xs, g0, color), G1 = interpTable (xs, g1, color);
        sOut.set (std::pow (G0, 1.0f - mix) * std::pow (G1, mix), snap);
        sInv.set (k[5] >= 0.5f ? -1.0f : 1.0f, snap);
        sLo.set (0.235f, snap); sHi.set (3.37f + 6.09f * depth, snap);
        sStat.set (k[4] >= 0.5f ? 1.0f : 0.0f, snap); sStatMs.set (0.466f + 7.277f * depth, snap);
        sRot.set (k[6] - 0.5f, snap);
    }
    double sr = 48000.0, tempo = 120.0, ph = 0.0, syncPh = 0.0;
    bool syncPending = false;
    float k[8] { 0.0f, 0.36f, 0.82f, 0.405f, 0.0f, 0.0f, 0.5f, 0.5f };
    std::array<FracDelay, 2> line;
    TptShelf1 fbHp[2];
    Smooth sRate, sA, sB, sBf, sG, sOut, sInv, sLo, sHi, sStat, sStatMs, sRot;
    float lfo[2] {}, lpA = 0.0003f;
};

// ---- MULTI CHORUS（Guitar Rig の Choral を測って合わせ込み）----
// 測った結果（cap/modi/Choral と cap/list15.tsv。50 ms ごとのインパルスの返り。本物は全体が 32 サンプル遅れて出る → こちらは遅れなしでドライからの差で合わせた）:
//   声は 2 本 1 組（A = 左寄り、B = 右寄り）。A と B は逆向きの三角で動く。三角は少し曲がっている（遅れの長い側ほど速い）:
//   遅れ = いちばん短い所 + W × (a u + (1 − a) u²)、u = 三角の位置 0..1、a = 0.80（CLASSIC）/ 0.99（LUSH）/ 0.90（BRIGHT）/ A 1.0・B 0.80（TWIN）。どれも 1 発ごとの遅れの差 0.5 サンプル以内
//   幅 W = 傾き / (2 × 速さ)、傾き = 0.173 + 17.31 × AMOUNT^2.01 ms/s、速さ = 0.25 + 7.4 × RATE³ Hz（RATE 0.875 で 5.21 Hz。50 ms ごとの記録では 4.79 に折り返して見えていた）、
//   いちばん短い遅れ = 0.305 + 15.0 × DELAY ms（声の音色の遅れ 1.7 サンプルを足すと、測った山の位置 0.340 + 15.0 × DELAY ms）。揺れは A の一番短い所から始まる
//   VOICES: 組が 1 → 3。2 組目・3 組目は 4.0 ms・8.11 ms 後ろで、揺れの速さ 1.4286 倍・1.7265 倍（傾きは同じなので幅は狭い）、左右の広がりも 0.716 倍・0.431 倍。
//   組の大きさ 0.499 / 0.330 / 0.294 / 0.238 / 0.222（VOICES 0 / .25 / .5 / .75 / 1）、途中の組は .25 で 0.712 倍・.75 で 0.71 倍
//   WIDTH: A は左へ等パワー（角 = 45° × (1 − WIDTH × 広がり)）。ウェットの中のドライ Dw = 0.709 × cos(45° × WIDTH) × (VOICES の表) × (FEEDBACK の表)
//   MIX: 出口 = (1 − MIX) × x + MIX × (Dw × x + 声)（MIX 0 でドライ 1.000、MIX 1 で 0.654）
//   MODE（0〜3、4 以上は 3 と同じ）: 声の音色（2 次のローパス ＋ 1 次。21 点で 0.1 dB 以内）と遅れの足し・揺れ方
//     0 CLASSIC = 4993 Hz Q 0.715 ＋ 18.4 kHz / 1 LUSH = 3998 Hz Q 0.712 ＋ 19.1 kHz、4.0 ms ＋ 0.2437 W 後ろ、A・B に同じ向きのサイン（速さ 4.1 倍、−0.1217 W cos）が足される /
//     2 BRIGHT = 11.71 kHz Q 0.713 ＋ 20 kHz、8.0 ms 後ろ / 3 TWIN = ほぼ素通し（18 kHz の 1 次）、4.0 ms 後ろ、A と B が同じ向き（B だけ曲がる）
//   FEEDBACK: 声ごとに自分の遅れの入口へ戻す（1 周 = 遅れ ＋ 声の音色）。戻りの強さ = FEEDBACK^0.25（0.25 → 0.71、0.5 → 0.84、0.75 → 0.93。本物は 1 でほぼ止まらない → こちらは 0.99 まで）。
//     1 回目の声の大きさ ×1 / 1.46 / 1.28 / 0.94 / 0.36、Dw ×1 / 0.870 / 0.713 / 0.507 / 0.198（0 / .25 / .5 / .75 / 1、くり返しの残りが重ならない高い所で読んだ）
//   3 組目の声だけ音色が明るい（CLASSIC で 9821 Hz Q 0.717 ＋ 19.4 kHz。1・2 組目は 4993 Hz）
//   SCATTER: 本物でも測った範囲（声 1〜3 組）では音が変わらなかった → こちらも変えない。INVERT: 声の向き
struct Choral final : Module
{
    static constexpr int kGroups = 3;
    static float rateHz (float v) { v = std::clamp (v, 0.0f, 1.0f); return 0.25f + 7.4f * v * v * v; }
    static int modeOf (float v) { return std::clamp ((int) std::lround (v * 3.0f), 0, 3); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "multichorus", "MULTI CHORUS", "MODULATION",
            "Up to three pairs of chorus voices. MODE picks the character, VOICES adds pairs, AMOUNT sets the pitch wobble, FEEDBACK makes it ring.",
            {
                { "MODE",     0.0f,       4, [] (float v) { static const char* n[4] { "CLASSIC", "LUSH", "BRIGHT", "TWIN" }; return std::string (n[modeOf (v)]); } },
                { "VOICES",   0.0f,       0, [] (float v) { return fmt (2.0f + 4.0f * std::clamp (v, 0.0f, 1.0f), 1, ""); } },
                { "DELAY",    0.18f,      0, [] (float v) { return fmt (0.34f + 15.0f * std::clamp (v, 0.0f, 1.0f), 1, " ms"); } },
                { "FEEDBACK", 0.0f,       0, [] (float v) { return fmtPct (v); } },
                { "RATE",     0.0078125f, 0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); } },
                { "AMOUNT",   0.49495f,   0, [] (float v) { return fmtPct (v); } },
                { "WIDTH",    0.5f,       0, [] (float v) { return fmtPct (v); } },
                { "MIX",      0.995f,     0, [] (float v) { return fmtPct (v); } },
                { "SCATTER",  0.0f,       2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "INVERT",   0.0f,       2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        // いちばん長い遅れ: 15.3 ms ＋ 3 組目 8 ms ＋ MODE 2 の 8 ms ＋ 幅 35 ms × 1.37（LUSH の足し）≒ 80 ms
        for (auto& d : line) d.prepare ((int) (0.12 * sr) + 16);
        for (auto& d : dcb) d.prepare (sr, 5.0);
        for (auto* q : { &sDry, &sWet, &sFbG, &sMin }) q->prepare (sr, 0.02);
        for (auto& q : sGain) q.prepare (sr, 0.03);
        for (auto& q : sPanC) q.prepare (sr, 0.03);
        for (auto& q : sPanS) q.prepare (sr, 0.03);
        sRate.prepare (sr, 0.05); sW.prepare (sr, 0.05); sMin.prepare (sr, 0.08);
        xfStep = 1.0f / (float) std::max (1.0, 0.04 * sr);
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& d : dcb) d.reset();
        for (auto& f : svf) f.reset();
        for (auto& f : op) f.reset();
        for (auto& v : back) v = 0.0f;
        for (int g = 0; g < kGroups; ++g) { ph[g] = 0.0; pe[g] = 0.0; }
        curMode = nxtMode = modeOf (k[0]); slot = 0; xf = 1.0f;
        setFilters (0, curMode, true);
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 10 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        const float ms = (float) (sr * 0.001);
        for (int s = 0; s < n; ++s)
        {
            // MODE が変わったら、前の MODE と新しい MODE の声を 40 ms でクロスフェード（遅れの足し・音色・揺れ方が一度に変わってもプチッとしない）
            if (xf >= 1.0f && wantMode != curMode)
            {
                nxtMode = wantMode; setFilters (slot ^ 1, nxtMode, true);
                for (int v = 0; v < kVoices; ++v) { svf[(slot ^ 1) * kVoices + v].ic1 = svf[slot * kVoices + v].ic1; svf[(slot ^ 1) * kVoices + v].ic2 = svf[slot * kVoices + v].ic2; op[(slot ^ 1) * kVoices + v].z = op[slot * kVoices + v].z; }
                xf = 0.0f;
            }
            float a = 0.0f;
            if (xf < 1.0f) { xf = std::min (1.0f, xf + xfStep); a = xf; }
            const float hz = sRate.next(), W1 = sW.next(), mn = sMin.next(), fbG = sFbG.next(), dry = sDry.next(), wet = sWet.next();
            const float xL = L[s], xR = R[s];
            float oL = 0.0f, oR = 0.0f;
            for (int g = 0; g < kGroups; ++g)
            {
                const float gain = sGain[g].next(), pc = sPanC[g].next(), ps = sPanS[g].next();
                ph[g] += (double) (hz * kMul[g]) / sr; ph[g] -= std::floor (ph[g]);
                pe[g] += (double) (4.1f * hz * kMul[g]) / sr; pe[g] -= std::floor (pe[g]);
                const int vA = 2 * g, vB = 2 * g + 1;
                // 使っていない組（VOICES で消えている組）も、輪・音色・揺れをいつも回しておく。読み飛ばして足した瞬間に、残っていた古い中身と戻りが
                // 組の音量のならしを通らずに輪へ入ってプチッとなった（FEEDBACK 0.75 で目安 23、DSP 検査 0.6.0）。CPU は VOICES 0 で 0.19 → 0.45 %（48 kHz）
                const float Wg = W1 / kMul[g], ming = mn + kOff[g];
                float vOut[2];
                for (int j = 0; j < 2; ++j)
                {
                    const int vi = 2 * g + j;
                    float y = voice (curMode, slot, g, j, vi, Wg, ming, ms);
                    if (a > 0.0f) y += a * (voice (nxtMode, slot ^ 1, g, j, vi, Wg, ming, ms) - y);
                    vOut[j] = y;
                    back[vi] = y;
                }
                // 輪の頭打ちは ±16 から。本物は FEEDBACK 1 で鳴きの山の真ん中に −15 dBFS rms の正弦波を入れると出口 +2〜+3 dBFS まで鳴って、3 倍音 −63〜−68 dB
                // （ほとんど歪まない。cap/list20〜21.tsv）。±1 / ±4 からだとこちらは −27〜−34 dB 歪んだ（DSP 検査 0.6.0）
                line[(size_t) vA].write (flush (16.0f * softKnee2 (0.0625f * (xL + fbG * dcb[vA].process (back[vA])))));
                line[(size_t) vB].write (flush (16.0f * softKnee2 (0.0625f * (xR + fbG * dcb[vB].process (back[vB])))));
                oL += gain * (pc * vOut[0] + ps * vOut[1]);
                oR += gain * (ps * vOut[0] + pc * vOut[1]);
            }
            if (xf >= 1.0f && a > 0.0f) { curMode = nxtMode; slot ^= 1; }
            L[s] = dry * xL + wet * oL;
            R[s] = dry * xR + wet * oR;
        }
    }
    double tailSeconds() const override { return 2.0; }

private:
    static constexpr int kVoices = 2 * kGroups;
    static constexpr float kMul[kGroups] { 1.0f, 1.4286f, 1.7265f }, kOff[kGroups] { 0.0f, 4.0f, 8.11f };
    static float tri (double p) { p -= std::floor (p); return (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p); }
    // 1 本の声: 遅れ（ms）→ 読む（書く前なので d − 1）→ 声の音色
    inline float voice (int mode, int sl, int g, int j, int vi, float Wg, float ming, float ms)
    {
        static const float warp[4][2] { { 0.80f, 0.80f }, { 0.99f, 0.99f }, { 0.90f, 0.90f }, { 1.0f, 0.80f } };
        const float u = tri (ph[g] + (mode != 3 && j == 1 ? 0.5 : 0.0)), a = warp[mode][j];
        float dMs = ming + Wg * (a * u + (1.0f - a) * u * u);
        if (mode == 1) dMs += 4.0f + Wg * (0.2437f - 0.1217f * std::cos (2.0f * (float) kPiD * (float) pe[g]));
        else if (mode == 2) dMs += 8.0f;
        else if (mode == 3) dMs += 4.0f;
        const float x = line[(size_t) vi].read (std::max (1.0f, dMs * ms - 1.0f));
        const int fi = sl * kVoices + vi;
        svf[fi].step(); op[fi].step();
        return op[fi].process (svf[fi].process (x));
    }
    void setFilters (int sl, int mode, bool snap)
    {
        static const float fHz[4] { 4993.0f, 3998.0f, 11710.0f, 0.0f }, fQ[4] { 0.715f, 0.712f, 0.7126f, 0.707f }, opHz[4] { 18370.0f, 19060.0f, 20000.0f, 18000.0f };
        for (int v = 0; v < kVoices; ++v)
        {
            auto& f = svf[sl * kVoices + v];
            // 3 組目の声だけ明るい（CLASSIC で 9821 Hz Q 0.717。ほかの MODE は測っていないので同じ比 1.967 倍にした）
            const double fc = fHz[mode] * (v >= 4 ? 1.967 : 1.0);
            if (mode == 3 || fc >= 0.45 * sr) { f.tg = Svf::tanw (sr, 1000.0); f.tk = 1.414f; f.tm0 = 1.0f; f.tm1 = 0.0f; f.tm2 = 0.0f; }   // 素通し
            else f.lp (sr, fc, fQ[mode]);
            f.ramp (snap, 64.0f);
            auto& o = op[sl * kVoices + v];
            o.setOnePoleLP (sr, std::min ((double) opHz[mode], 0.45 * sr)); o.ramp (snap);
        }
    }
    void recalc (bool snap)
    {
        wantMode = modeOf (k[0]);
        const float V = std::clamp (k[1], 0.0f, 1.0f), fb = std::clamp (k[3], 0.0f, 1.0f), w = std::clamp (k[6], 0.0f, 1.0f), m = std::clamp (k[7], 0.0f, 1.0f);
        const float hz = rateHz (k[4]), amt = std::clamp (k[5], 0.0f, 1.0f);
        sRate.set (hz, snap);
        sW.set ((0.173f + 17.31f * std::pow (amt, 2.01f)) / (2.0f * hz), snap);   // 1 組目の三角の幅（ms）
        sMin.set (0.305f + 15.0f * std::clamp (k[2], 0.0f, 1.0f), snap);
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float g1[5] { 0.4986f, 0.3297f, 0.2939f, 0.2381f, 0.2219f }, f2[5] { 0.0f, 0.712f, 1.0f, 1.0f, 1.0f }, f3[5] { 0.0f, 0.0f, 0.0f, 0.71f, 1.0f };
        static const float dwV[5] { 0.655f, 0.754f, 0.790f, 0.876f, 0.909f };
        static const float cFb[5] { 1.0f, 1.46f, 1.28f, 0.94f, 0.36f }, dFb[5] { 1.0f, 0.870f, 0.713f, 0.507f, 0.198f };
        const float inv = k[9] >= 0.5f ? -1.0f : 1.0f;
        const float gBase = interpTable (xs, g1, V) * interpTable (xs, cFb, fb) * inv;
        sGain[0].set (gBase, snap); sGain[1].set (gBase * interpTable (xs, f2, V), snap); sGain[2].set (gBase * interpTable (xs, f3, V), snap);
        static const float spread[kGroups] { 1.0f, 0.716f, 0.431f };
        for (int g = 0; g < kGroups; ++g)
        {
            const float th = 0.25f * (float) kPiD * (1.0f - w * spread[g]);
            sPanC[g].set (std::cos (th), snap); sPanS[g].set (std::sin (th), snap);
        }
        const float Dw = interpTable (xs, dwV, V) * std::cos (0.25f * (float) kPiD * w) / 0.92388f * interpTable (xs, dFb, fb);
        sDry.set ((1.0f - m) + m * Dw, snap); sWet.set (m, snap);
        // 1 周の強さは 0.99 まで（本物の FEEDBACK 1 はほぼ止まらない。0.99 で鳴きの山の大きさは本物より約 7 dB 小さいが、歪まずに止まる）
        sFbG.set (fb <= 0.0f ? 0.0f : std::min (0.99f, std::pow (fb, 0.25f)), snap);
        if (snap) { curMode = nxtMode = wantMode; xf = 1.0f; }
    }
    double sr = 48000.0;
    float k[10] { 0.0f, 0.0f, 0.18f, 0.0f, 0.0078125f, 0.49495f, 0.5f, 0.995f, 0.0f, 0.0f };
    std::array<FracDelay, kVoices> line;
    DcBlock dcb[kVoices];
    Svf svf[2 * kVoices];
    TptShelf1 op[2 * kVoices];
    float back[kVoices] {};
    double ph[kGroups] {}, pe[kGroups] {};
    Smooth sRate, sW, sMin, sFbG, sDry, sWet, sGain[kGroups], sPanC[kGroups], sPanS[kGroups];
    int wantMode = 0, curMode = 0, nxtMode = 0, slot = 0;
    float xf = 1.0f, xfStep = 0.001f;
};
// ---- VIBE ENSEMBLE（Guitar Rig の Ensemble を測って合わせ込み）----
// 測った結果（cap/modi/Ensemble と cap/list15.tsv。本物は全体が 16 サンプル遅れて出る → こちらは遅れなしでドライからの差で合わせた）:
//   声は左右 1 本ずつ（左の入口 → 左の声、右 → 右）。右の揺れの位相 = 左 + 90° × STEREO
//   MODE 0 CHORUS: まっすぐの三角（曲がりなし）、幅 2.60 ms（INTENSITY では変わらない）、速さ = 0.1 + 2.9 × INTENSITY Hz、いちばん短い遅れ = 0.081 + 3.365 × DELAY ms（山の位置）
//   MODE 1 VIBRATO（1 以上は全部同じ）: サインの揺れ、速さ = 3.243 × 3.565^RATE Hz、幅 = (0.0385 + 0.522 × DEPTH) ms × (6.123 / 速さ)^0.90、
//     中心 = コーラスの中心 ＋ 0.104 ms（本物は DEPTH 0.25 / 0.75 で中心が 0.3 ms ほどずれる記録があったが、いちばん多い値にした）
//   声の音色: BASS × MID × TREBLE を 5 段ずつ全部の組み合わせ（125 通り、cap/list19.tsv）で測って、「全体 ＋ 帯域 7 本（低い棚 150 Hz・ピーク 300〜4800 Hz・高い棚 7 kHz）」
//     の dB の表（EnsembleTone.h、自動生成）にした。間は 3 つのつまみの直線補間。本物の音色つまみは強く影響し合う（BASS 1 では MID が効かない、
//     MID 0 では BASS で全体の大きさだけ変わる など）ので、つまみごとの式では合わなかった（ふつうのアンプの音色回路の式でも 5 dB ずれた）
//   MIX: 出口 = VOLUME × ((1 − MIX) × ドライ ＋ MIX × 声)。VOLUME = 30 × (v − 0.5) dB（ドライにもウェットにも）。
//   ドライだけ 1 次のハイパス 26 Hz（20 Hz −4.4 dB、50 Hz −1.0 dB。ウェットには掛かっていない）と、高い所が少し落ちる（10 kHz −1.0 dB、16 kHz −2.2 dB）
//   SYNC: CHORUS は INTENSITY、VIBRATO は RATE で音符を選ぶ
struct Ensemble final : Module
{
    static float chorusHz (float v) { return 0.1f + 2.9f * std::clamp (v, 0.0f, 1.0f); }
    static float vibHz (float v) { return 3.243f * std::pow (3.565f, std::clamp (v, 0.0f, 1.0f)); }
    static std::string intText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (chorusHz (v), 2, " Hz");
    }
    static std::string rateText (float v, const float* all)
    {
        if (all != nullptr && all[0] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (vibHz (v), 2, " Hz");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "vibeensemble", "VIBE ENSEMBLE", "MODULATION",
            "Vintage chorus / vibrato box. CHORUS uses INTENSITY for speed, VIBRATO uses DEPTH and RATE. BASS, MID and TREBLE shape only the effect.",
            {
                { "SYNC",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "VOLUME",    0.5f,     0, [] (float v) { return fmt (30.0f * (v - 0.5f), 1, " dB"); } },
                { "MODE",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "VIBRATO" : "CHORUS"); } },
                { "INTENSITY", 0.34282f, 0, [] (float v) { return fmt (chorusHz (v), 2, " Hz"); }, &intText },
                { "DEPTH",     0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "RATE",      0.5f,     0, [] (float v) { return fmt (vibHz (v), 2, " Hz"); }, &rateText },
                { "BASS",      0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "MID",       0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "TREBLE",    0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.355f,   0, [] (float v) { return fmtPct (v); } },
                { "DELAY",     0.5f,     0, [] (float v) { return fmt (0.08f + 3.37f * std::clamp (v, 0.0f, 1.0f), 2, " ms"); } },
                { "STEREO",    0.64f,    0, [] (float v) { return fmt (90.0f * v, 0, " deg"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.02 * sr) + 16);
        quietNeed = (int) (0.02 * sr) + 16;
        for (auto* q : { &sDry, &sWet, &sMin, &sStereo, &sVibW, &sMode, &sChW }) q->prepare (sr, 0.03);
        for (auto* q : { &sB, &sM, &sT }) q->prepare (sr, 0.03);
        sCh.prepare (sr, 0.05); sVib.prepare (sr, 0.05);
        for (int c = 0; c < 2; ++c)
        {
            base[c].lp (sr, std::min (9000.0, 0.45 * sr), 0.5); base[c].ramp (true, 1.0f);
            inHp[c].setOnePoleHP (sr, 26.0); inHp[c].ramp (true);
            dryLp[c].set (sr, 20000.0, 1.0, 0.0, 10000.0); dryLp[c].ramp (true);   // 10 kHz で合わせた 1 次（−1.0 dB）
        }
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (int c = 0; c < 2; ++c) { base[c].reset(); for (auto& f : band[c]) f.reset(); inHp[c].reset(); dryLp[c].reset(); }
        phC = 0.0; phV = 0.609; errC = errV = 0.0; syncPending = false; quietRun = 1 << 30;   // 本物のビブラートは少し進んだ位置から始まる（測った位置）
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 12 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void setTempo (double bpm) override { if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void setTransport (double ppq, bool playing) override
    {
        if (k[0] < 0.5f || ! playing || ppq < 0.0) return;
        const double pc = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[3]));
        const double pv = ppq / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[5]));
        syncC = pc - std::floor (pc); syncV = pv - std::floor (pv); syncPending = true;
        sCh.cur = sCh.target; sVib.cur = sVib.target;
    }
    void process (float* L, float* R, int n) override
    {
        if (syncPending)
        {
            // 曲の位置との差を一気に飛ばさず、50 ms で埋める（揺れにならしがないので、飛ぶと遅れが最大 2.6 ms 跳んでプチッとなる: DSP 検査 0.6.0）。
            // 遅れ線が空のとき（再生の頭など）と、ずれがごく小さいときはそのまま合わせる（DSP 検査 0.6.0 の 2 回目: reset からの時間で決めると、
            // 止めてすぐ再生し直したときに音が流れたまま飛んでプチッとなった）
            auto wrap = [] (double d) { d -= std::floor (d + 0.5); return d; };
            const double eC = wrap (syncC - phC), eV = wrap (syncV - phV);
            // 遅れ線の中が無音（遅れ線の長さ以上、入口が 0 のまま）のときだけ一気に合わせる（飛んでも音が出ない）
            const bool empty = quietRun >= quietNeed;
            if (empty || std::abs (eC) < 1.0e-4) { phC = syncC; errC = 0.0; } else errC = eC;
            if (empty || std::abs (eV) < 1.0e-4) { phV = syncV; errV = 0.0; } else errV = eV;
            syncPending = false;
        }
        const double gk = 1.0 - std::exp (-1.0 / (0.05 * sr));
        const float ms = (float) (sr * 0.001);
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            trG += trStep;
            const float fc = sCh.next(), fv = sVib.next(), dry = sDry.next(), wet = sWet.next() * trG, mn = sMin.next(), st = sStereo.next(), vw = sVibW.next(), mode = sMode.next(), cw = sChW.next();
            const double cC = errC * gk, cV = errV * gk; errC -= cC; errV -= cV;
            phC += fc / sr + cC; phC -= std::floor (phC);
            phV += fv / sr + cV; phV -= std::floor (phV);
            // 入口が無音の続いた長さ（遅れ線の中が空かどうか。SYNC で一気に合わせてよいかの判断に使う）
            quietRun = (std::abs (L[s]) < 1.0e-6f && std::abs (R[s]) < 1.0e-6f) ? std::min (quietRun + 1, 1 << 30) : 0;
            for (int c = 0; c < 2; ++c)
            {
                const double off = c == 1 ? (double) st : 0.0;
                double p = phC + off; p -= std::floor (p);
                const float tri = (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
                const float dC = mn + cw * tri;
                const float dV = mn + 1.404f - 0.5f * vw + vw * (0.5f + 0.5f * std::sin (2.0f * (float) kPiD * (float) (phV + off)));
                const float x = io[c][s];
                line[(size_t) c].write (x);
                float v = line[(size_t) c].read (std::max (1.0f, dC * ms));
                if (mode > 1.0e-4f) v += mode * (line[(size_t) c].read (std::max (1.0f, dV * ms)) - v);
                base[c].step(); v = base[c].process (v);
                for (auto& f : band[c]) { f.step(); v = f.process (v); }
                io[c][s] = dry * dryLp[c].process (inHp[c].process (x)) + wet * v;
            }
        }
    }
    double tailSeconds() const override { return 0.05; }

private:
    void recalc (bool snap)
    {
        float fc = chorusHz (k[3]), fv = vibHz (k[5]);
        if (k[0] >= 0.5f)
        {
            const double bps = std::max (5.0, tempo) / 60.0;
            fc = (float) (bps / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[3])));
            fv = (float) (bps / std::max (1.0e-3, (double) TwinDelay::noteQuarters (k[5])));
        }
        sCh.set (fc, snap); sVib.set (fv, snap);
        sMode.set (k[2] >= 0.5f ? 1.0f : 0.0f, snap);
        // 本物の速さの範囲（CHORUS 0.1〜3 Hz、VIBRATO 3.24〜11.6 Hz）を SYNC で超えたときに揺れが深くなりすぎないように: CHORUS は 3 Hz より速いと幅を 3 / 速さ 倍、
        // VIBRATO の幅の式は 3.243 Hz より遅い所へ伸ばさない（DSP 検査 0.6.0）
        sChW.set (2.60f * std::min (1.0f, 3.0f / std::max (0.01f, fc)), snap);
        sVibW.set ((0.0385f + 0.522f * std::clamp (k[4], 0.0f, 1.0f)) * std::pow (6.123f / std::max (3.243f, fv), 0.90f), snap);
        // 山の位置 0.081 + 3.365 × DELAY ms から、声の音色の遅れ（約 1.5 サンプル）を引いた
        sMin.set (std::max (0.03f, 0.05f + 3.365f * std::clamp (k[10], 0.0f, 1.0f)), snap);
        sStereo.set (0.25f * std::clamp (k[11], 0.0f, 1.0f), snap);
        const float vol = dbToGain (30.0f * (std::clamp (k[1], 0.0f, 1.0f) - 0.5f)), m = std::clamp (k[9], 0.0f, 1.0f);
        // 音色: つまみの値をなめらかにして、32 サンプルごとに係数を作り直す（一気に回してもプチッとしない。TREBLE は ±20 dB 以上動く）
        sB.set (std::clamp (k[6], 0.0f, 1.0f), snap); sM.set (std::clamp (k[7], 0.0f, 1.0f), snap); sT.set (std::clamp (k[8], 0.0f, 1.0f), snap);
        if (snap) tone (true);
        sDry.set (vol * (1.0f - m), snap);
        sWet.set (vol * m, snap);
    }
    inline float settle (Smooth& q)
    {
        for (int i = 0; i < 32; ++i) q.next();
        if (std::abs (q.cur - q.target) <= 1.0e-3f) q.cur = q.target;
        return q.cur;
    }
    void tone (bool snap)
    {
        const float B = snap ? sB.cur : settle (sB), Md = snap ? sM.cur : settle (sM), Tr = snap ? sT.cur : settle (sT);
        if (! snap && same (B, last[0]) && same (Md, last[1]) && same (Tr, last[2])) { trStep = 0.0f; trG = trGoal; return; }
        last[0] = B; last[1] = Md; last[2] = Tr;
        // 表を 3 つのつまみで直線補間（5 × 5 × 5）
        float gdb[8] {};
        const float fb = std::clamp (B, 0.0f, 1.0f) * 4.0f, fm = std::clamp (Md, 0.0f, 1.0f) * 4.0f, ft = std::clamp (Tr, 0.0f, 1.0f) * 4.0f;
        const int ib = std::min (3, (int) fb), im = std::min (3, (int) fm), it = std::min (3, (int) ft);
        const float ub = fb - (float) ib, um = fm - (float) im, ut = ft - (float) it;
        for (int c8 = 0; c8 < 8; ++c8)
        {
            float v = 0.0f;
            for (int db = 0; db < 2; ++db) for (int dm = 0; dm < 2; ++dm) for (int dt = 0; dt < 2; ++dt)
                v += (db ? ub : 1.0f - ub) * (dm ? um : 1.0f - um) * (dt ? ut : 1.0f - ut) * kEnsTone[ib + db][im + dm][it + dt][c8];
            gdb[c8] = v;
        }
        static const float fHz[7] { 150.0f, 300.0f, 600.0f, 1200.0f, 2400.0f, 4800.0f, 7000.0f };
        for (int c = 0; c < 2; ++c)
        {
            for (int j = 0; j < 7; ++j)
            {
                auto& f = band[c][j];
                const double fr = std::min ((double) fHz[j], 0.45 * sr);
                if (j == 0) f.lowShelf (sr, fr, gdb[1 + j], 0.6);
                else if (j == 6) f.highShelf (sr, fr, gdb[1 + j], 0.6);
                else f.peak (sr, fr, 0.9, gdb[1 + j]);
                f.ramp (snap, 32.0f);
            }
        }
        trGoal = dbToGain (gdb[0]);
        if (snap) { trG = trGoal; trStep = 0.0f; }
        else trStep = (trGoal - trG) / 32.0f;
    }
    double sr = 48000.0, tempo = 120.0, phC = 0.0, phV = 0.0, syncC = 0.0, syncV = 0.0, errC = 0.0, errV = 0.0;
    bool syncPending = false;
    int quietRun = 1 << 30, quietNeed = 976;
    float k[12] { 0.0f, 0.5f, 0.0f, 0.34282f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.355f, 0.5f, 0.64f };
    std::array<FracDelay, 2> line;
    Svf base[2], band[2][7];
    TptShelf1 inHp[2], dryLp[2];
    Smooth sCh, sVib, sDry, sWet, sMin, sStereo, sVibW, sMode, sB, sM, sT, sChW;
    float last[3] { -1.0f, -1.0f, -1.0f }, trG = 1.0f, trGoal = 1.0f, trStep = 0.0f;
    unsigned ctl = 0;
};
// ---- SCANNER VIBE（Guitar Rig の Vintage Vibrato を測って合わせ込み。オルガンのスキャナー式ビブラートの音）----
// 測った結果（cap/modi/VintageVibrato と cap/list15.tsv。本物は全体が 32 サンプル遅れて出る → こちらは遅れなしで合わせた）。大まかな版（0.6.0）:
//   ウェット = 揺れない道 × e(BLEND) ＋ 揺れる道 × l(BLEND) ＋ 高い音のもれ。e = 0 / 0.067 / 0.229 / 0.407 / 0.554、l = 0.979 / 0.913 / 0.756 / 0.583 / 0.439（BLEND 0 / .25 / .5 / .75 / 1）
//   揺れない道 = 2 次のローパス 2 段（19.1 kHz Q 0.30 ＋ 5737 Hz Q 0.867）。もれ = 0.433 × 2 次のハイパス 1671 Hz Q 0.2（10 kHz から上で −7 dB）
//   揺れる道: 遅れがサインで動く（揺れの速さ = 0.752 × 31.6^RATE Hz、いちばん短い所から始まる）。遅れが長いほど高い音が丸くなる（2 次 2 段のローパス 12 kHz → 3.3 kHz）、
//     群遅延（300〜3000 Hz）の幅 = 1.5 → 20.6 / 28.3 / 41.0 / 54.9 / 68.3 / 81.3 サンプル（MODE 0〜5、6 以上は 5 と同じ）。COLOR 1 / 2 で 2.33 / 2.38 倍（3 以上は 2 と同じ）。
//     一番短い所では大きさが少し下がる（× (1 − 0.19 (1 − u)^6)、u = 位置 0..1）
//   SPREAD OFF = 左右まったく同じ、ON = 右の揺れの位相 = 左 + 180° × WIDTH。STEREO は本物を測った範囲（左右同じ入口）では変わらなかった → OFF で入口を左右まとめる
//   MIX: 出口 = (1 − MIX) × x ＋ MIX × ウェット（MIX 0 でドライ 1.000）
struct VintageVibe final : Module
{
    static float rateHz (float v) { return 0.752f * std::pow (31.6f, std::clamp (v, 0.0f, 1.0f)); }
    static int modeOf (float v) { return std::clamp ((int) std::lround (v * 5.0f), 0, 5); }
    static int colorOf (float v) { return std::clamp ((int) std::lround (v * 2.0f), 0, 2); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "scannervibe", "SCANNER VIBE", "MODULATION",
            "Organ-style scanner vibrato. MODE sets the depth, BLEND mixes in the steady signal for the chorus sound, COLOR picks a deeper scanner.",
            {
                { "STEREO", 1.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "BLEND",  0.5f,  0, [] (float v) { return fmtPct (v); } },
                { "MODE",   0.6f,  6, [] (float v) { static const char* n[6] { "1", "2", "3", "4", "5", "6" }; return std::string (n[modeOf (v)]); } },
                { "RATE",   0.6f,  0, [] (float v) { return fmt (rateHz (v), 2, " Hz"); } },
                { "SPREAD", 1.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "COLOR",  0.0f,  3, [] (float v) { static const char* n[3] { "A", "B", "C" }; return std::string (n[colorOf (v)]); } },
                { "WIDTH",  0.25f, 0, [] (float v) { return fmt (180.0f * v, 0, " deg"); } },
                { "MIX",    1.0f,  0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.01 * sr) + 16);
        for (auto* q : { &sE, &sL, &sDry, &sWet, &sDepth, &sOff, &sMono }) q->prepare (sr, 0.03);
        sRate.prepare (sr, 0.05);
        for (int c = 0; c < 2; ++c)
        {
            fx1[c].lp (sr, std::min (19083.0, 0.45 * sr), 0.30); fx1[c].ramp (true, 1.0f);
            fx2[c].lp (sr, std::min (5737.0, 0.45 * sr), 0.867); fx2[c].ramp (true, 1.0f);
            lk[c].hp (sr, 1671.0, 0.2); lk[c].ramp (true, 1.0f);
        }
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (int c = 0; c < 2; ++c) { fx1[c].reset(); fx2[c].reset(); lk[c].reset(); sc[c][0] = sc[c][1] = Lp2 {}; }
        ph = 0.0;
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 8 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        const float rs = (float) (sr / 48000.0);   // 測ったのは 48 kHz のサンプル数
        for (int s = 0; s < n; ++s)
        {
            const float hz = sRate.next(), e = sE.next(), l = sL.next(), dry = sDry.next(), wet = sWet.next(), depth = sDepth.next(), off = sOff.next(), mono = sMono.next();
            ph += hz / sr; ph -= std::floor (ph);
            // STEREO OFF = 左右の入口をまとめてから揺らす。まとめる量は 30 ms でならす（一気に切り替えるとプチッ: DSP 検査 0.6.0）。ドライは元の左右のまま
            const float xd[2] { L[s], R[s] }, mid = 0.5f * (L[s] + R[s]);
            float xin[2] { L[s] + mono * (mid - L[s]), R[s] + mono * (mid - R[s]) };
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = xin[c];
                line[(size_t) c].write (x);
                const double p = ph + (c == 1 ? (double) off : 0.0);
                const float u = 0.5f - 0.5f * std::cos (2.0f * (float) kPiD * (float) (p - std::floor (p)));
                // 遅れが長いほど丸くなるローパス（2 次 2 段）。その低い所の遅れ（2√2 / (2π fc)）を引いて、群遅延が測った幅になるように読む
                const float fc = 12000.0f - 8700.0f * u;
                const float tau = 2.0f * 1.41421356f / (2.0f * (float) kPiD * fc) * (float) sr;
                const float gd = (1.5f + (depth - 1.5f) * u) * rs;
                float v = line[(size_t) c].read (std::max (1.0f, gd - tau));
                const float g = std::tan ((float) kPiD * std::min (fc, 0.45f * (float) sr) / (float) sr);
                v = sc[c][1].process (sc[c][0].process (v, g), g);
                v *= 1.0f - 0.19f * std::pow (1.0f - u, 6.0f);
                fx1[c].step(); fx2[c].step(); lk[c].step();
                const float fixedPath = fx2[c].process (fx1[c].process (x));
                const float leak = 0.433f * lk[c].process (x);
                out[c] = dry * xd[c] + wet * (e * fixedPath + l * v + leak);
            }
            L[s] = out[0]; R[s] = out[1];
        }
    }
    double tailSeconds() const override { return 0.01; }

private:
    // 2 次のローパス（TPT、Q 0.707。係数をサンプルごとに変えてもよい形）
    struct Lp2
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        inline float process (float x, float g)
        {
            const float kq = 1.41421356f, a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flush (2.0f * v1 - ic1); ic2 = flush (2.0f * v2 - ic2);
            return v2;
        }
    };
    void recalc (bool snap)
    {
        sRate.set (rateHz (k[3]), snap);
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, eT[5] { 0.0f, 0.067f, 0.229f, 0.407f, 0.554f }, lT[5] { 0.979f, 0.913f, 0.756f, 0.583f, 0.439f };
        const float B = std::clamp (k[1], 0.0f, 1.0f);
        sE.set (interpTable (xs, eT, B), snap); sL.set (interpTable (xs, lT, B), snap);
        static const float gdMax[6] { 20.6f, 28.3f, 41.0f, 54.9f, 68.3f, 81.3f }, colMul[3] { 1.0f, 2.33f, 2.38f };
        sDepth.set (1.5f + (gdMax[modeOf (k[2])] - 1.5f) * colMul[colorOf (k[5])], snap);
        sOff.set (k[4] >= 0.5f ? 0.5f * std::clamp (k[6], 0.0f, 1.0f) : 0.0f, snap);
        const float m = std::clamp (k[7], 0.0f, 1.0f);
        sDry.set (1.0f - m, snap); sWet.set (m, snap);
        sMono.set (k[0] >= 0.5f ? 0.0f : 1.0f, snap);
    }
    double sr = 48000.0, ph = 0.0;
    float k[8] { 1.0f, 0.5f, 0.6f, 0.6f, 1.0f, 0.0f, 0.25f, 1.0f };
    std::array<FracDelay, 2> line;
    Svf fx1[2], fx2[2], lk[2];
    Lp2 sc[2][2];
    Smooth sRate, sE, sL, sDry, sWet, sDepth, sOff, sMono;
};
} // namespace aiofx
