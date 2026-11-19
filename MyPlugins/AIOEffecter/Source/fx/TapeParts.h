#pragma once
// テープの揺れ（0.7.0〜）。JUCE なし
#include "DelayKit.h"
#include "VerbEngine.h"  // Svf
#include "Oversampler.h"

namespace aiofx
{
// ---- TAPE WOW（Guitar Rig の Tape Wobble を測って合わせ込み。大まかな版）----
// 測った結果（cap/mod/TapeWobble の 440 Hz の正弦波・ピンクノイズ・無音、cap/modi のインパルス列）:
//   揺れ（遅れの振れ）: WOW = 1.39 Hz（TAPE SPEED 0 / 0.465 / 1 → 0.14 / 1.39 / 2.8 Hz）で ±10 ms × WOW²（0.5 → ±2.5 ms）、
//   FLUTTER = 8.78 Hz で ±1.23 ms × FLUTTER²、SCRAPE = でたらめな揺れ（遅れの揺れのスペクトルは 50 Hz まで平ら −35.5 dB（ms²/Hz、SCRAPE 1）、
//   上は 60 / 100 / 160 / 200 / 300 Hz で −2.7 / −7.3 / −15.6 / −21.1 / −31.8 dB。rms 0.15 ms。量は SCRAPE に比例 → 1 次 LP 60 Hz × 2 次 LP 130 Hz の雑音。
//   0.7.0 の最初の版は 42 Hz の帯域通過・量は 2 乗で、上に広がりすぎていた: DSP 検査 0.7.0）。本物は毎回少し違う（でたらめが混ざる）
//   SATURATION = 奇数の歪み（3 倍音。4 倍オーバーサンプリングの中で歪ませる。ドライも同じ 4 倍のフィルターを通して、ずれないようにする = 遅れ 4 サンプルを申告）。−15 dBFS の正弦波で 3 倍音 −60 / −37 / −18.5 / −11 / −9.7 dB、音量 0 / +0.45 / −1.5 / −5.9 / −9.9 dB（0 / .25 / .5 / .75 / 1）
//     → tanh(d x) / d × 持ち上げ（d = 0.44 / 1.67 / 5.92 / 14.65 / 20.83、持ち上げ +0.0 / +0.9 / +1.8 / +3.2 / +2.1 dB）。歪むほど高い所が下がる（15 kHz −3 → −25 dB）
//   TAPE AGE = 帯域が狭くなる（.25: 15 kHz −4 dB、.5: −11、1: 100 Hz −12・3 kHz −16・15 kHz −29 dB）。雑音は AGE 0 でサーッ −66 dBFS、
//     AGE を上げるとそれが消えて、AGE とともに別の雑音が増える（.25 → −91、.5 → −80、1 → −72 dBFS。どれも TAPE AGE の帯域を通ったあとで測った値）
//   STEREO = 左右の揺れが少しずれる。MIX = 等パワー（cos / sin。MIX 0.5 で +3 dB）。テープの遅れはほぼ 0（ドライより 3.4 サンプル遅れ = MIX 0.5 で 7 kHz に谷。本物も同じ）から
//   揺れの分だけ長くなる（本物も同じ。WOW 1 で 0〜20 ms）
//   測った音色: 歪みのローパス（1 次）= なし / なし / 7.5 k / 3.8 k / 2.25 kHz、TAPE AGE のローパス（1 次）= なし / 11.5 k / 3.8 k / 1.7 k / 630 Hz と
//   ハイパス（2 次）= なし / 15 / 40 / 110 / 220 Hz（Q 0.5 → 0.9 = 1 で 300 Hz あたりが少し持ち上がる）。SCRAPE = 42 Hz の帯域雑音で遅れを揺らす（1 で rms 0.13 ms）
struct TapeWobble final : Module
{
    static float wowHz (float v)
    {
        static const float xs[3] { 0.0f, 0.465f, 1.0f }, lg[3] { -0.8539f, 0.1430f, 0.4472f };   // log10 Hz
        return std::pow (10.0f, interpTable (xs, lg, std::clamp (v, 0.0f, 1.0f)));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tapewow", "TAPE WOW", "MODULATION",
            "Worn tape machine: WOW is the slow pitch drift, FLUTTER the fast one, SCRAPE a gritty jitter. SATURATION squashes and darkens, TAPE AGE narrows the sound and adds hiss.",
            {
                { "FLUTTER",    0.0f,      0, [] (float v) { return fmtPct (v); } },
                { "WOW",        0.043938f, 0, [] (float v) { return fmtPct (v); } },
                { "SCRAPE",     0.0f,      0, [] (float v) { return fmtPct (v); } },
                { "TAPE SPEED", 0.465f,    0, [] (float v) { return fmt (wowHz (v), 2, " Hz"); } },
                { "STEREO",     0.0f,      0, [] (float v) { return fmtPct (v); } },
                { "TAPE AGE",   0.0f,      0, [] (float v) { return fmtPct (v); } },
                { "SATURATION", 0.265f,    0, [] (float v) { return fmtPct (v); } },
                { "MIX",        1.0f,      0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.06 * sr) + 16);
        for (auto* q : { &sDrive, &sMake, &sDry, &sWet, &sHiss, &sAgeAmt }) q->prepare (sr, 0.03);
        // 揺れの深さと STEREO（右の揺れのずれ）は 0.25 秒でならす（WOW を一気に 0 → 1 にすると遅れが 30 ms で 20 ms 動いて音程が大きく跳ねた。
        // STEREO を 30 ms でならしていたときは WOW 1 で右の音程が +3.0 半音跳ねた: DSP 検査 0.7.0）
        for (auto* q : { &sWow, &sFl, &sSc, &sSt }) q->prepare (sr, 0.25);
        sWowHz.prepare (sr, 0.1);
        for (auto* q : { &sAgeLp, &sAgeHp, &sAgeQ, &sSatLp }) q->prepare (sr, 0.03);
        // SCRAPE の雑音: 1 次 LP 60 Hz → 2 次 LP 130 Hz（本物の揺れのスペクトルの形）
        for (int c = 0; c < 2; ++c) { scLp1[c].setOnePoleLP (sr, 60.0); scLp1[c].ramp (true); scLp2[c].lp (sr, 130.0, 0.70710678); scLp2[c].ramp (true, 1.0f); }
        // 1 サンプルごとの乱数の大きさは、サンプルレートが上がると聞こえる所が薄くなる → √(sr / 48000) を掛けて 48 kHz と同じ密度に（DSP 検査 0.7.0）
        nScale = (float) std::sqrt (sr / 48000.0);
        driftA = (float) (1.0 - std::exp (-1.0 / (0.104 * sr)));   // ゆっくりのでたらめの追いかけ（48 kHz で 1 サンプル 0.0002 だった = 0.104 秒）
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); satLp[c].reset(); scLp1[c].reset(); scLp2[c].reset(); osW[c].reset(); osD[c].reset(); drift[c] = driftT[c] = 0.0f; }
        phW = 0.0; phF = 0.0; seed = 0x2545F491u; cnt = 0; last[0] = last[1] = last[2] = last[3] = -1.0f;
        tone (true);
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 8 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        const float ms = (float) (sr * 0.001);
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            const float wHz = sWowHz.next(), wow = sWow.next(), fl = sFl.next(), sc = sSc.next(), drive = sDrive.next(), make = sMake.next();
            const float dry = sDry.next(), wet = sWet.next(), st = sSt.next(), hiss = sHiss.next(), ageAmt = sAgeAmt.next();
            phW += wHz / sr; phW -= std::floor (phW);
            phF += 8.78 / sr; phF -= std::floor (phF);
            // ゆっくりのでたらめ（0.3 秒ごとの目標へなめらかに。WOW の 15 % ほど）
            if (--cnt <= 0) { cnt = (int) (0.3 * sr); for (int c = 0; c < 2; ++c) driftT[c] = uni(); }
            for (int c = 0; c < 2; ++c)
            {
                drift[c] += driftA * (driftT[c] - drift[c]);
                const float off = c == 1 ? st * 0.08f : 0.0f;   // STEREO: 右の揺れを少しずらす
                const float w = std::sin (2.0f * (float) kPiD * (float) (phW + off)) + 0.15f * drift[c];
                const float f = std::sin (2.0f * (float) kPiD * (float) (phF + 2.0 * off));
                // SCRAPE: 60 Hz までのでたらめ（上はなだらかに下がる）。遅れはいちばん短い所 kBaseMs から、揺れの分だけ長くなる（本物と同じ: ドライとほぼ同じ位置から）
                scLp1[c].step();
                const float nz = scLp2[c].process (scLp1[c].process (nScale * uni()));
                const float dMs = kBaseMs + wow * (1.0f + w) + fl * (1.0f + f) + sc * (0.4f + 4.87f * nz);
                const float x = io[c][s];
                line[(size_t) c].write (x);
                float v = line[(size_t) c].read (std::max (1.0f, dMs * ms));
                // 歪み（tanh、4 倍の中で）→ 歪むほど高い所が下がる → テープの帯域 → サーッ
                float u[4]; osW[c].up (v, u);
                for (auto& q : u) q = make * std::tanh (drive * q) / drive;
                v = osW[c].down (u);
                float ud[4]; osD[c].up (x, ud);
                const float xd = osD[c].down (ud);   // ドライも同じ 4 倍のフィルターを通す（ずれない）
                satLp[c].step(); lp[c].step(); hp[c].step();
                v = satLp[c].process (v);
                v += hiss * nScale * uni();
                v = lp[c].process (v);
                v += ageAmt * (hp[c].process (v) - v);   // TAPE AGE 0 ではハイパスを通さない（通すと止めたあとに低い所の余韻が残った）
                io[c][s] = dry * xd + wet * v;
            }
        }
    }
    double tailSeconds() const override { return 0.06; }
    int latency() const override { return 4; }   // 4 倍のフィルター（ドライも同じだけ遅れる）

private:
    float uni() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }
    void recalc (bool snap)
    {
        const float fl = std::clamp (k[0], 0.0f, 1.0f), wo = std::clamp (k[1], 0.0f, 1.0f), sc = std::clamp (k[2], 0.0f, 1.0f);
        sWowHz.set (wowHz (k[3]), snap);
        sWow.set (10.0f * wo * wo, snap); sFl.set (1.23f * fl * fl, snap); sSc.set (sc, snap);   // SCRAPE は量に比例（本物: 0.5 で rms 0.077、1 で 0.150 ms）
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, dr[5] { 0.44f, 1.67f, 5.92f, 14.65f, 20.83f }, mk[5] { 0.0f, 0.9f, 1.8f, 3.2f, 2.1f };
        const float sat = std::clamp (k[6], 0.0f, 1.0f);
        sDrive.set (interpTable (xs, dr, sat), snap); sMake.set (dbToGain (interpTable (xs, mk, sat)), snap);
        const float m = std::clamp (k[7], 0.0f, 1.0f);
        sDry.set (std::cos (0.5f * (float) kPiD * m), snap); sWet.set (std::sin (0.5f * (float) kPiD * m), snap);   // 等パワー（MIX 0.5 で +3 dB = 本物と同じ）
        sSt.set (std::clamp (k[4], 0.0f, 1.0f), snap);
        // 雑音: TAPE AGE 0 は −66 dBFS のサーッ。本物は AGE を少し上げると消えて（.25 で −91 dBFS）、AGE とともに別の雑音が増える（.5 −80、1 −72 dBFS）
        const float ageN = std::clamp (k[5], 0.0f, 1.0f);
        sHiss.set (0.00087f * std::max (0.0f, 1.0f - 8.0f * ageN) + 0.00233f * std::pow (ageN, 2.67f), snap);
        const float age = std::clamp (k[5], 0.0f, 1.0f);
        static const float ageLp[5] { 30000.0f, 11500.0f, 3800.0f, 1700.0f, 630.0f }, ageHp[5] { 10.0f, 15.0f, 40.0f, 110.0f, 220.0f }, ageQ[5] { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };
        static const float satLpT[5] { 30000.0f, 30000.0f, 7500.0f, 3800.0f, 2250.0f };
        sAgeAmt.set (std::min (1.0f, 4.0f * age), snap);
        sAgeLp.set (interpTable (xs, ageLp, age), snap); sAgeHp.set (interpTable (xs, ageHp, age), snap); sAgeQ.set (interpTable (xs, ageQ, age), snap);
        sSatLp.set (interpTable (xs, satLpT, sat), snap);
        if (snap) tone (true);
    }
    void tone (bool snap)
    {
        if (! snap) { for (int i = 0; i < 32; ++i) { sAgeLp.next(); sAgeHp.next(); sAgeQ.next(); sSatLp.next(); } }
        if (! snap && same (sAgeLp.cur, last[0]) && same (sAgeHp.cur, last[1]) && same (sSatLp.cur, last[2]) && same (sAgeQ.cur, last[3]))
        {
            // 止まったら係数を目標ちょうどに（Svf は ramp で止まる。TptShelf1 は残り回数で止まる）
            return;
        }
        last[0] = sAgeLp.cur; last[1] = sAgeHp.cur; last[2] = sSatLp.cur; last[3] = sAgeQ.cur;
        const double ny = 0.45 * sr;
        for (int c = 0; c < 2; ++c)
        {
            lp[c].setOnePoleLP (sr, std::min ((double) sAgeLp.cur, ny)); lp[c].ramp (snap);
            hp[c].hp (sr, std::min ((double) sAgeHp.cur, ny), sAgeQ.cur); hp[c].ramp (snap, 32.0f);
            satLp[c].setOnePoleLP (sr, std::min ((double) sSatLp.cur, ny)); satLp[c].ramp (snap);
        }
    }
    double sr = 48000.0, phW = 0.0, phF = 0.0;
    float k[8] { 0.0f, 0.043938f, 0.0f, 0.465f, 0.0f, 0.0f, 0.265f, 1.0f };
    std::array<FracDelay, 2> line;
    static constexpr float kBaseMs = 0.0496f;   // 読み出しの 1 サンプルと合わせてドライより 3.38 サンプル遅れ（MIX 0.5 の谷が本物と同じ 7 kHz）
    Svf hp[2], scLp2[2];
    TptShelf1 lp[2], satLp[2], scLp1[2];
    Oversampler4 osW[2], osD[2];
    float nScale = 1.0f, driftA = 0.0002f;
    Smooth sWowHz, sWow, sFl, sSc, sDrive, sMake, sDry, sWet, sSt, sHiss, sAgeLp, sAgeHp, sAgeQ, sSatLp, sAgeAmt;
    float drift[2] {}, driftT[2] {}, last[4] { -1.0f, -1.0f, -1.0f, -1.0f };
    uint32_t seed = 0x2545F491u;
    int cnt = 0;
    unsigned ctl = 0;
};
} // namespace aiofx
