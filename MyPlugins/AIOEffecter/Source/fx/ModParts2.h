#pragma once
// 揺れものの続き（0.11.0〜）。JUCE なし
#include "ModParts.h"

namespace aiofx
{
// ---- RING SWEEP（Guitar Rig の Ring Modulator を測って合わせ込み。大まかな版。Ringmod を測った RING MOD とは別のもの）----
// 測った結果（cap/mod/RingModulator 54 通り ＋ list28 の 14 通り。440 Hz −12 dBFS の正弦波の区間で 出口 ÷ 入口 = 搬送波 c(t) を読んだ）:
//   出口 = (1 − r) × 入口 ＋ r × 入口 × c、r = 0.02 ＋ 0.98 RING（RING 0.5 の記録で平均 0.498・振れ 0.5 ± 0.8。RING 0 でも掛け算が約 2 % 残る = 横帯 −35 dB。
//   掛け算のあとの飽和はない: FM 0・AMOUNT 0 で 3 × 440 の成分 −112 dB）
//   c: 振れ幅 1.6 の正弦波（3 倍音 −44 dB・5 倍音 −36.5 dB だけ）。EDGE 0〜0.5 はほぼ正弦波、0.5 → 1 で三角波へ（1 で振れ幅 1.44）
//   周波数: MODE 0 = 31.25 Hz × 128^FREQ（0〜1 で 31.25 Hz〜4 kHz、ちょうど 7 オクターブ）、MODE 1〜12 = その 1/50（0.625〜80 Hz）。MODE・WAVEFORM は 0 か それ以外の 2 つだけ
//   LFO: 速さ 0.0316 × 1000^RATE Hz（0.5 → 1.005 Hz、0.75 → 5.65 Hz）、周波数をオクターブで上下対称に揺らす。幅（片側）は AMOUNT 0.25 / 0.5 / 0.75 で 0.082 / 0.224 / 0.466 オクターブ
//     （1 は低い所で追えなかったので 0.85 と延ばした）。WAVEFORM 1〜12 = 正弦波、0 = 矩形波（切り替えは一瞬）
//   FM: 搬送波が自分の出口で自分の位相を揺らす形（フィードバック FM）にすると、搬送波の平均（負の直流）と形が本物に合う。帰還は 3 kHz の 1 次のローパスを通す
//     （1 サンプル前の値をそのまま戻すと β ≥ 1 で搬送波が 1 サンプルごとに入れ替わり、19〜24 kHz に出口の半分のエネルギー: DSP 検査 0.11.0）。
//     β = 0 / 0.12 / 0.3 / 1.0 / 1.17 / 1.49（FM 0 / 0.145 / 0.25 / 0.5 / 0.75 / 1。出口のスペクトルの差がいちばん小さくなる組。LFO なしの初期値の記録で 1.1 dB）。
//     本物は入口の大きさでも変わるかもしれない（−12 dBFS だけで測った）
//   SYNC は本物のテンポの割り当てを測っていないので効かない（表示だけ）
class RingSweep final : public Module
{
public:
    static double carrierHz (float freq, bool low) { return (low ? 0.625 : 31.25) * std::pow (128.0, (double) std::clamp (freq, 0.0f, 1.0f)); }
    static float lfoHz (float rate) { return 0.0316f * std::pow (1000.0f, std::clamp (rate, 0.0f, 1.0f)); }
    static float depthOct (float amount)
    {
        static const float x[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] { 0.0f, 0.082f, 0.224f, 0.466f, 0.85f };
        return interpTable (x, y, std::clamp (amount, 0.0f, 1.0f));
    }
    static float fmBeta (float fm)
    {
        static const float x[6] { 0.0f, 0.145453f, 0.25f, 0.5f, 0.75f, 1.0f }, y[6] { 0.0f, 0.12f, 0.3f, 1.0f, 1.17f, 1.49f };   // 帰還のローパス 3 kHz の形で、本物の出口のスペクトルに合わせ直した（71 通りの差の中央 6.5 → 4.7 dB）
        return interpTable (x, y, std::clamp (fm, 0.0f, 1.0f));
    }
    static std::string freqText (float v, const float* all)
    {
        const bool low = all != nullptr && std::lround (std::clamp (all[3], 0.0f, 1.0f) * 12.0f) != 0;
        return fmtHz ((float) carrierHz (v, low));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "ringsweep", "RING SWEEP", "MODULATION",
            "Ring modulator with its own sweeping oscillator: FREQ sets the carrier, AMOUNT and RATE sweep it, EDGE turns the sine into a triangle, FM roughens it. MODE LOW drops the carrier to 0.6-80 Hz for a choppy tremolo.",
            {
                { "SYNC",     0.0f,        2, [] (float v) { return std::string (v >= 0.5f ? "ON (no effect)" : "OFF"); } },
                { "RING",     1.0f,        0, [] (float v) { return fmtPct (v); } },
                { "FM",       0.145453f,   0, [] (float v) { return fmtPct (v); } },
                { "MODE",     0.0f,        13, [] (float v) { return std::string (std::lround (std::clamp (v, 0.0f, 1.0f) * 12.0f) == 0 ? "AUDIO" : "LOW"); } },
                { "FREQ",     0.639823f,   0, [] (float v) { return fmtHz ((float) carrierHz (v, false)); }, &freqText },
                { "AMOUNT",   0.22f,       0, [] (float v) { return fmt (depthOct (v) * 12.0f, 1, " st"); } },
                { "WAVEFORM", 1.0f / 12.0f, 13, [] (float v) { return std::string (std::lround (std::clamp (v, 0.0f, 1.0f) * 12.0f) == 0 ? "SQUARE" : "SINE"); } },
                { "RATE",     0.14f,       0, [] (float v) { return fmt (lfoHz (v), 2, " Hz"); } },
                { "EDGE",     0.5f,        0, [] (float v) { return v <= 0.5f ? std::string ("SINE") : fmtPct ((v - 0.5f) * 2.0f) + " TRI"; } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* s : { &sRing, &sFm, &sLogF, &sDepth, &sRate, &sEdge }) s->prepare (sr, 0.03);
        fbA = 1.0 - std::exp (-2.0 * kPiD * std::min (3000.0, 0.12 * sr) / sr);   // FM の帰還のローパス 3 kHz（どのレートでも同じ。12 kHz より下の変わったレートでも安定するよう 0.12 sr まで）
        for (auto& d : dcb) d.prepare (sr, 10.0);            // 掛け算の分の直流止め 10 Hz（RING MOD と同じ）
        recalc (true); reset();
    }
    void reset() override { ph = 0.0; lph = 0.0; fbz = 0.0; for (auto& d : dcb) d.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 9 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        const double twoPi = 2.0 * kPiD;
        for (int s = 0; s < n; ++s)
        {
            const float ring = sRing.next(), beta = sFm.next(), lf = sLogF.next(), dep = sDepth.next(), rate = sRate.next(), edge = sEdge.next();
            // LFO（正弦波か矩形波）
            lph += (double) rate / sr; if (lph >= 1.0) lph -= 1.0;
            const float lv = square ? (lph < 0.5 ? 1.0f : -1.0f) : (float) std::sin (twoPi * lph);
            const double f = std::exp2 ((double) lf + (double) (dep * lv));
            ph += f / sr; ph -= std::floor (ph);
            // 搬送波: フィードバック FM の正弦波 → EDGE で三角波へ
            const double th = twoPi * ph + (double) beta * fbz;
            const float sn = (float) std::sin (th);
            float c = sn;
            if (edge > 0.0f)
            {
                double pp = th / twoPi; pp -= std::floor (pp);   // 三角波（sin と同じ位相: 0 → 0、1/4 → +1）
                const float tri = (float) (pp < 0.25 ? 4.0 * pp : pp < 0.75 ? 2.0 - 4.0 * pp : 4.0 * pp - 4.0);
                c = sn + edge * (0.9f * tri - sn);   // 三角波の振れ幅は正弦波の 0.9 倍（1.44 / 1.6）
            }
            fbz += fbA * ((double) c - fbz);
            // (1 − r) x ＋ r × 1.6 c x。掛け算の分だけ 10 Hz の直流止め（入口と搬送波の高さが近いと 20 Hz より下に −8 dBFS が出た: DSP 検査 0.11.0）
            const float r = 0.02f + 0.98f * ring;
            const float wl = dcb[0].process (1.6f * r * c * L[s]), wr = dcb[1].process (1.6f * r * c * R[s]);
            L[s] = (1.0f - r) * L[s] + wl; R[s] = (1.0f - r) * R[s] + wr;
        }
    }

private:
    void recalc (bool snap)
    {
        low = std::lround (std::clamp (k[3], 0.0f, 1.0f) * 12.0f) != 0;
        square = std::lround (std::clamp (k[6], 0.0f, 1.0f) * 12.0f) == 0;
        sRing.set (std::clamp (k[1], 0.0f, 1.0f), snap);
        sFm.set (fmBeta (k[2]), snap);
        sLogF.set ((float) std::log2 (carrierHz (k[4], low)), snap);
        sDepth.set (depthOct (k[5]), snap);
        sRate.set (lfoHz (k[7]), snap);
        sEdge.set (std::clamp ((k[8] - 0.5f) * 2.0f, 0.0f, 1.0f), snap);
    }
    double sr = 48000.0, ph = 0.0, lph = 0.0;
    float k[9] { 0.0f, 1.0f, 0.145453f, 0.0f, 0.639823f, 0.22f, 1.0f / 12.0f, 0.14f, 0.5f };
    double fbz = 0.0, fbA = 0.3;   // FM の帰還（ローパスを通した搬送波）
    DcBlock dcb[2];
    bool low = false, square = false;
    Smooth sRing, sFm, sLogF, sDepth, sRate, sEdge;
};
} // namespace aiofx
