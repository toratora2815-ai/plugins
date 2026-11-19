#pragma once
// 回るスピーカー（0.7.0〜）。JUCE なし
#include "DelayKit.h"
#include "VerbEngine.h"  // Svf

namespace aiofx
{
// ---- ROTARY（Guitar Rig の Rotator を測って合わせ込み。大まかな版）----
// 測った結果（cap/modi/Rotator の 50 ms ごとのインパルスを帯域に分けて 1 発ずつ: ホーン 2〜6 kHz、ドラム 80〜400 Hz の遅れと大きさ）:
//   ホーンとドラムが別々に回る。速い: ホーン 1.45 + 10.35 × T FAST Hz（.5 → 6.62）、ドラム 1.34 + 9.23 × B FAST Hz（.5 → 5.95）。遅い: ホーン 0.72・ドラム 0.66 Hz（.5）
//   遅れの揺れ（ドップラー）: ホーン ±0.41 ms（DISTANCE 0 で 0.2 倍、遅い回転で 0.33 倍）、ドラム ±0.8 ms。音量の揺れ: DISTANCE 0 で大きく 1 で小さい（ホーン 6.5 → 2.2 dB、ドラム 3.0 → 1.4 dB）。
//   キャビなし（CAB 0）では揺れが約 0.2 倍
//   回り始めは遅い速さから加速する（ACCEL 0 = ゆっくり〜 1 = すぐ。ホーン 4 s → 0.06 s、ドラム 12 s → 0.15 s の時定数）。
//   遅い速さから回り始めるのは画面で部品を選んだとき（startFresh）だけ。prepareToPlay（書き出しへの切り替え・プロジェクトを開いたとき）やホストの reset では
//   今の SPEED の速さから（DSP 検査 0.7.0: reset・prepare のたびに加速し直して、書き出しの頭が再生と違う音になりえた）
//   SPREAD: 左右のマイクの角度の差（0 = 左右同じ）。BALANCE: ホーンとドラムの大きさ（0 でホーン −11 dB・ドラム +4 dB、1 でホーン +8.1 dB・ドラム −13.7 dB）。PAN: 0 = ホーン左・ドラム右、1 = 逆
//   CAB: 0 = キャビなし / 1 以上 = キャビあり（1〜45 は全部同じ音）。キャビの音色はウェットの平均を当てはめた
//     ON: −0.95 dB ＋ ピーク 203 Hz Q 0.65 +12.2 dB ＋ ピーク 1878 Hz Q 2.67 +10.1 dB ＋ LP 2899 Hz Q 1.84 ＋ HP 23 Hz（1.2 dB rms）
//     OFF: −2.27 dB ＋ ピーク 576 Hz Q 0.47 −7.6 dB ＋ ピーク 11.9 kHz Q 0.23 −10.9 dB（0.8 dB rms）
//   SPEED は 0 = 遅い / 1 以上 = 速い。MIX は直線。入口は左右をまとめる（本物は左だけの入口で測った）
struct Rotary final : Module
{
    static float hornFast (float v) { return 1.45f + 10.35f * std::clamp (v, 0.0f, 1.0f); }
    static float drumFast (float v) { return 1.34f + 9.23f * std::clamp (v, 0.0f, 1.0f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "rotary", "ROTARY", "MODULATION",
            "Rotating speaker cabinet: a fast horn for the highs and a drum for the lows. SPEED switches slow / fast, ACCEL sets how fast they spin up.",
            {
                { "SPEED",    1.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "FAST" : "SLOW"); } },
                { "BALANCE",  0.365f, 0, [] (float v) { return fmtPct (v); } },
                { "PAN",      0.59f,  0, [] (float v) { return fmtPct (v); } },
                { "DISTANCE", 0.895f, 0, [] (float v) { return fmtPct (v); } },
                { "T SLOW",   0.5f,   0, [] (float v) { return fmt (0.109f * hornFast (v), 2, " Hz"); } },
                { "T FAST",   0.5f,   0, [] (float v) { return fmt (hornFast (v), 2, " Hz"); } },
                { "T ACCEL",  0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "T SPREAD", 0.5f,   0, [] (float v) { return fmt (120.0f * v, 0, " deg"); } },
                { "B SLOW",   0.5f,   0, [] (float v) { return fmt (0.111f * drumFast (v), 2, " Hz"); } },
                { "B FAST",   0.5f,   0, [] (float v) { return fmt (drumFast (v), 2, " Hz"); } },
                { "B ACCEL",  0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "B SPREAD", 0.5f,   0, [] (float v) { return fmt (120.0f * v, 0, " deg"); } },
                { "CAB",      1.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "MIX",      0.655f, 0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (0.01 * sr) + 16);
        for (auto* q : { &sHornA, &sDrumA, &sHornG, &sDrumG, &sPanHL, &sPanHR, &sPanDL, &sPanDR, &sSprH, &sSprD, &sCab, &sDry, &sWet, &sHornDop }) q->prepare (sr, 0.03);
        for (int i = 0; i < 2; ++i) { xLp[i].lp (sr, 800.0, 0.7071); xLp[i].ramp (true, 1.0f); xHp[i].hp (sr, 800.0, 0.7071); xHp[i].ramp (true, 1.0f); }
        for (int c = 0; c < 2; ++c)
        {
            on1[c].peak (sr, 203.0, 0.65, 12.2); on2[c].peak (sr, 1878.0, 2.67, 10.1); on3[c].lp (sr, std::min (2899.0, 0.45 * sr), 1.84); on4[c].hp (sr, 23.0, 0.7071);
            of1[c].peak (sr, 576.0, 0.47, -7.6); of2[c].peak (sr, std::min (11929.0, 0.45 * sr), 0.23, -10.9);
            for (Svf* f : { &on1[c], &on2[c], &on3[c], &on4[c], &of1[c], &of2[c] }) f->ramp (true, 1.0f);
        }
        recalc (true); reset();
    }
    // 画面で選んだとき: 本物と同じく遅い速さから加速（ラックは動かし始めに reset を呼ぶので、最初の process まで印を残す）
    void startFresh() override { spinUp = true; rateH = slowH; rateD = slowD; }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (int i = 0; i < 2; ++i) { xLp[i].reset(); xHp[i].reset(); }
        for (int c = 0; c < 2; ++c) for (Svf* f : { &on1[c], &on2[c], &on3[c], &on4[c], &of1[c], &of2[c] }) f->reset();
        phH = phD = 0.0;
        // ホストの reset では、今の SPEED の速さで回っている所から（回転は止まっていないもの）。画面で選んだ直後だけ遅い速さから
        if (spinUp) { rateH = slowH; rateD = slowD; } else { rateH = targetH; rateD = targetD; }
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 14 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        spinUp = false;
        const float ms = (float) (sr * 0.001);
        const float aH = 1.0f - std::exp (-1.0f / (std::max (0.01f, tauH) * (float) sr)), aD = 1.0f - std::exp (-1.0f / (std::max (0.01f, tauD) * (float) sr));
        for (int s = 0; s < n; ++s)
        {
            rateH += aH * (targetH - rateH); rateD += aD * (targetD - rateD);
            phH += rateH / sr; phH -= std::floor (phH);
            phD += rateD / sr; phD -= std::floor (phD);
            const float cab = sCab.next(), modK = 0.2f + 0.8f * cab;   // キャビなしでは揺れが小さい（本物: 音量の揺れ・遅れの揺れとも約 0.2 倍）
            const float hA = sHornA.next() * modK, dA = sDrumA.next() * modK, hG = sHornG.next(), dG = sDrumG.next(), sprH = sSprH.next(), sprD = sSprD.next(), dry = sDry.next(), wet = sWet.next();
            // ドップラー: ホーンは遠いマイクほど・速いほど大きい（DISTANCE 0 で 0.2 倍、遅い回転で 0.33 倍）、ドラムは 0.8 ms
            const float hD = sHornDop.next() * std::sqrt (std::min (1.5f, rateH / 6.62f)) * modK, dD = 0.8f * modK;
            const float pHL = sPanHL.next(), pHR = sPanHR.next(), pDL = sPanDL.next(), pDR = sPanDR.next();
            const float xL = L[s], xR = R[s], x = 0.5f * (xL + xR);
            // 2 帯域に分ける（LR4 800 Hz、足すと全通過）
            xLp[0].step(); xLp[1].step(); xHp[0].step(); xHp[1].step();
            const float lo = xLp[1].process (xLp[0].process (x)), hi = xHp[1].process (xHp[0].process (x));
            line[0].write (hi); line[1].write (lo);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float side = c == 0 ? -0.5f : 0.5f;
                const float aHc = 2.0f * (float) kPiD * ((float) phH - side * sprH), aDc = 2.0f * (float) kPiD * ((float) phD - side * sprD);
                const float horn = line[0].read (std::max (1.0f, (0.6f + hD * std::sin (aHc)) * ms)) * dbToGain (hA * std::cos (aHc));
                const float drum = line[1].read (std::max (1.0f, (1.0f + dD * std::sin (aDc)) * ms)) * dbToGain (dA * std::cos (aDc));
                float v = hG * (c == 0 ? pHL : pHR) * horn + dG * (c == 0 ? pDL : pDR) * drum;
                // キャビ: ON と OFF の音色を混ぜる（切り替えは 30 ms でなめらか）
                for (Svf* f : { &on1[c], &on2[c], &on3[c], &on4[c], &of1[c], &of2[c] }) f->step();
                const float vOn = 0.8933f * on4[c].process (on3[c].process (on2[c].process (on1[c].process (v))));
                const float vOff = 0.7700f * of2[c].process (of1[c].process (v));
                out[c] = vOff + cab * (vOn - vOff);
            }
            L[s] = dry * xL + wet * out[0];
            R[s] = dry * xR + wet * out[1];
        }
    }
    double tailSeconds() const override { return 0.05; }

private:
    void recalc (bool snap)
    {
        const bool fast = k[0] >= 0.5f;
        slowH = 0.109f * hornFast (k[4]); slowD = 0.111f * drumFast (k[8]);
        targetH = fast ? hornFast (k[5]) : slowH; targetD = fast ? drumFast (k[9]) : slowD;
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, tH[5] { 4.0f, 1.5f, 0.45f, 0.15f, 0.06f }, tD[5] { 12.0f, 4.0f, 1.4f, 0.4f, 0.15f };
        tauH = interpTable (xs, tH, std::clamp (k[6], 0.0f, 1.0f)); tauD = interpTable (xs, tD, std::clamp (k[10], 0.0f, 1.0f));
        if (snap) { rateH = targetH; rateD = targetD; }
        const float dist = std::clamp (k[3], 0.0f, 1.0f);
        sHornA.set (6.5f - 4.3f * dist, snap); sDrumA.set (3.0f - 1.6f * dist, snap);
        sHornDop.set (0.41f * (0.2f + 0.8f * std::min (1.0f, dist / 0.895f)), snap);
        // BALANCE: 初期値 0.365 から下げるとホーンが −11 dB（ドラムは +4 dB）まで、上げるとホーンが +8.1 dB・ドラムが −13.7 dB まで（本物の帯域の大きさの変わり方）
        const float bal = std::clamp (k[1], 0.0f, 1.0f), db = bal - 0.365f;
        sHornG.set (dbToGain (db < 0.0f ? 30.0f * db : 12.8f * db), snap); sDrumG.set (dbToGain (db < 0.0f ? -11.0f * db : -21.6f * db), snap);
        // PAN: ホーンは PAN、ドラムは 1 − PAN の位置へ（端で左右の差 約 10 dB）。真ん中で両方 1
        auto panL = [] (float p) { return 1.41421356f * std::cos (0.25f * (float) kPiD * (1.0f + 0.6f * (2.0f * p - 1.0f))); };
        auto panR = [] (float p) { return 1.41421356f * std::sin (0.25f * (float) kPiD * (1.0f + 0.6f * (2.0f * p - 1.0f))); };
        const float pan = std::clamp (k[2], 0.0f, 1.0f);
        sPanHL.set (panL (pan), snap); sPanHR.set (panR (pan), snap); sPanDL.set (panL (1.0f - pan), snap); sPanDR.set (panR (1.0f - pan), snap);
        sSprH.set (std::clamp (k[7], 0.0f, 1.0f) / 3.0f, snap); sSprD.set (std::clamp (k[11], 0.0f, 1.0f) / 3.0f, snap);   // 120° × SPREAD（周の 1/3）
        sCab.set (k[12] >= 0.5f ? 1.0f : 0.0f, snap);
        const float m = std::clamp (k[13], 0.0f, 1.0f);
        sDry.set (1.0f - m, snap); sWet.set (m, snap);
    }
    double sr = 48000.0, phH = 0.0, phD = 0.0;
    float k[14] { 1.0f, 0.365f, 0.59f, 0.895f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.655f };
    std::array<FracDelay, 2> line;
    Svf xLp[2], xHp[2], on1[2], on2[2], on3[2], on4[2], of1[2], of2[2];
    Smooth sHornA, sDrumA, sHornG, sDrumG, sPanHL, sPanHR, sPanDL, sPanDR, sSprH, sSprD, sCab, sDry, sWet, sHornDop;
    bool spinUp = false;
    float rateH = 0.72f, rateD = 0.66f, targetH = 6.62f, targetD = 5.95f, slowH = 0.72f, slowD = 0.66f, tauH = 0.45f, tauD = 1.4f;
};
} // namespace aiofx
