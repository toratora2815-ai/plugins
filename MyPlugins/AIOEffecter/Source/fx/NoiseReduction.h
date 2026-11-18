#pragma once
// NOISE REDUCTION … 静かな所の「サーッ」を下げる（Guitar Rig 7 の Noise Reduction を測って合わせ込み、2026-11-18）
//
// 測った結果（build/aiofx/nr、0.5 dB 刻みの階段・ピンクノイズ 6 dB 刻み・段差）:
//   下げ方   … 1:4 のエキスパンダー（しきい値より下では 1 dB 下がるごとに 3 dB 余分に下げる）。
//             形は g = 1 / (1 + (しきい値 / 大きさ)^3)（しきい値ちょうどで −6 dB）。正弦波で 0.5 dB 以内
//   THRESHOLD … −85.75 dB + 90 dB × つまみ（1 kHz の正弦波の山の大きさで）
//   大きさ   … 山をすぐ捕まえて、指数で下がる。下がる時定数 26 ms × 16^(つまみ − 0.5)（6.5〜105 ms）= RELEASE
//             戻る（開く）ときは約 1 ms
//   DEHISS  … 高い音（3 kHz より上）の大きさを別に測り、静かなほど 1 次のローパスを下げる（いちばん下で 1 kHz）。
//             ローパスの高さは高い音の大きさに比例（6 dB 上がると 1 オクターブ上がる）。DEHISS が大きいほど高い所まで削る
#include "Module.h"

namespace aiofx
{
struct NoiseReductionFit
{
    // build/aiofx/nrfit で合わせ込んだ値（2026-11-18、ピンクノイズ 7 段 × 帯域 6 本 ＋ 1 kHz の階段、6 通りで帯域の差 0.5〜1.3 dB）
    float scHz = 4164.0f;     // 高い音を測るハイパス（2 次）の角
    float ldDb = -2.18f;      // DEHISS 1 で 1 kHz から開き始める高い音の大きさ（しきい値に対して dB）
    float ldPow = 3.21f;      // DEHISS が下がると、開き始める大きさがどれだけ下がるか（D^ldPow）
    float fcPow = 1.143f;     // 高い音の大きさ → ローパスの高さの指数（1 = 6 dB で 1 オクターブ）
};

struct NoiseReduction final : Module
{
    static NoiseReductionFit& fit() { static NoiseReductionFit f; return f; }
    static float thrDb (float v)  { return -85.75f + 90.0f * v; }
    static float relSec (float v) { return 0.026f * std::pow (16.0f, v - 0.5f); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "nred", "NOISE REDUCTION", "DYNAMICS",
            "Turns quiet hiss down (1:4 below THRESHOLD). DEHISS also darkens the quiet parts.",
            {
                { "THRESHOLD", 0.17f, 0, [] (float v) { return fmt (thrDb (v), 1, " dB"); } },
                { "DEHISS",    0.5f,  0, [] (float v) { return fmtPct (v); } },
                { "RELEASE",   0.6f,  0, [] (float v) { return fmtTime (relSec (v)); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        attMul = (float) std::exp (-1.0 / (0.0007 * sr));
        thrSm.prepare (sr, 0.06);
        dSm.prepare (sr, 0.05);
        // 高い音を測るための 2 次ハイパス（バターワース）
        {
            const double w0 = 2.0 * M_PI * fit().scHz / sr, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071), a0 = 1.0 + al;
            sb0 = (float) ((1.0 + c) * 0.5 / a0); sb1 = (float) (-(1.0 + c) / a0); sb2 = sb0; sa1 = (float) (-2.0 * c / a0); sa2 = (float) ((1.0 - al) / a0);
        }
        recalc(); thrSm.set (thr, true); dSm.set (k[1], true); reset();
    }
    void reset() override
    {
        env = envHF = 0.0f; gain = 0.0f; lpL = lpR = 0.0f; hpX = hpY = 0.0f; lpCoef = 1.0f; counter = 0;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 3 || same (k[i], v)) return;
        k[i] = v; recalc();
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float x = std::max (std::abs (L[s]), std::abs (R[s]));
            env = std::max (x, env * relMul);
            // 高い音の大きさ（左右の和をハイパス）
            const float m = 0.5f * (L[s] + R[s]);
            const float h = sb0 * m + hpX;
            hpX = flush (sb1 * m - sa1 * h + hpY);
            hpY = flush (sb2 * m - sa2 * h);
            envHF = std::max (std::abs (h), envHF * relMul);

            const float r = thrSm.next() / std::max (env, 1.0e-12f);
            const float target = 1.0f / (1.0f + r * r * r);
            gain = target > gain ? target + (gain - target) * attMul : target;

            float l = L[s], rr = R[s];
            // DEHISS のローパスはいつも回す（DEHISS 0 に近づくと開ききって素通し = 切り替えの跳ねがない）。しきい値も DEHISS もなめらかにした値から
            const float dv = dSm.next();
            if ((counter++ & 15) == 0)
            {
                ldHF = thrSm.cur * dbToGain (fit().ldDb) * std::pow (std::max (dv, 0.0f), fit().ldPow);
                const float fc = ldHF < 1.0e-12f ? 30000.0f : std::min (1000.0f * std::pow (std::max (envHF / ldHF, 1.0f), fit().fcPow), 30000.0f);
                lpCoef = fc >= 20000.0f ? 1.0f : (float) (1.0 - std::exp (-2.0 * M_PI * std::max (1000.0f, fc) / sr));
            }
            lpL = flush (lpL + lpCoef * (l - lpL)); lpR = flush (lpR + lpCoef * (rr - lpR));
            l = lpL; rr = lpR;
            L[s] = l * gain; R[s] = rr * gain;
        }
    }
    float meter() const override { return gain; }

private:
    void recalc()
    {
        thr = dbToGain (thrDb (k[0]));
        thrSm.set (thr);   // しきい値はなめらかに動かす（一気に回したときのプチッ対策）
        dSm.set (k[1]);
        relMul = (float) std::exp (-1.0 / (relSec (k[2]) * sr));
        // DEHISS の量 → 高い音がどの大きさのとき 1 kHz から開き始めるか（0 なら効かない）
        ldHF = thr * dbToGain (fit().ldDb) * std::pow (std::max (k[1], 0.001f), fit().ldPow);
    }
    double sr = 48000.0;
    float k[3] { 0.17f, 0.5f, 0.6f };
    float thr = 0.0f, relMul = 0.99f, attMul = 0.97f, ldHF = 1.0f;
    float sb0 = 1, sb1 = 0, sb2 = 0, sa1 = 0, sa2 = 0;
    Smooth thrSm, dSm;
    float env = 0.0f, envHF = 0.0f, gain = 0.0f, lpL = 0.0f, lpR = 0.0f, hpX = 0.0f, hpY = 0.0f, lpCoef = 1.0f;
    unsigned counter = 0;
};
} // namespace aiofx
