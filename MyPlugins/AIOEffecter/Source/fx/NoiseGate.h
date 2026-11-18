#pragma once
// GATE … 弾いていない間の雑音を止める門（Guitar Rig 7 の Noise Gate を測って合わせ込み、2026-11-18）
//
// 測った結果（build/aiofx/gate、0.25 dB 刻みの階段・開閉の時間）:
//   判定  … 音の山（サンプルの絶対値、L / R の大きいほう）。周波数で変わらない。開く大きさ = 閉じる大きさ（ずれの幅なし）
//   THRESHOLD … −63 dB + 60 dB × つまみ（1 kHz の正弦波の山で 0.25 dB 以内）
//   HOLD    … 山がしきい値を下回ってから閉じ始めるまで 0.1 s × 300^つまみ（0.1〜30 s）
//   ATTACK  … 開くときは音量を直線で上げる。0 → 1 まで 0.1 ms × 10000^つまみ（0.1 ms〜1 s）
//   RELEASE … 閉じるときは指数で下げる。時定数 1 ms × 10000^つまみ（1 ms〜10 s）。閉じきると完全に無音
#include "Module.h"

namespace aiofx
{
struct NoiseGate final : Module
{
    static float thrDb (float v)   { return -63.0f + 60.0f * v; }
    static float holdSec (float v) { return 0.1f * std::pow (300.0f, v); }
    static float attSec (float v)  { return 1.0e-4f * std::pow (10000.0f, v); }
    static float relSec (float v)  { return 1.0e-3f * std::pow (10000.0f, v); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "gate", "NOISE GATE", "DYNAMICS",
            "Silences the noise between notes. Opens on peaks above THRESHOLD, closes after HOLD.",
            {
                { "THRESHOLD", 0.2f, 0, [] (float v) { return fmt (thrDb (v), 1, " dB"); } },
                { "HOLD",      0.0f, 0, [] (float v) { return fmtTime (holdSec (v)); } },
                { "ATTACK",    0.0f, 0, [] (float v) { return fmtTime (attSec (v)); } },
                { "RELEASE",   0.0f, 0, [] (float v) { return fmtTime (relSec (v)); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override { sr = sampleRate; recalc(); reset(); }
    void reset() override { gain = 0.0f; holdLeft = 0; }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 4 || same (k[i], v)) return;
        k[i] = v; recalc();
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float det = std::max (std::abs (L[s]), std::abs (R[s]));
            if (det >= thr) holdLeft = holdN;
            if (holdLeft > 0)
            {
                --holdLeft;
                gain = std::min (1.0f, gain + attStep);
            }
            else
            {
                gain *= relMul;
                if (gain < 1.0e-7f) gain = 0.0f;
            }
            L[s] *= gain; R[s] *= gain;
        }
    }
    float meter() const override { return gain; }

private:
    void recalc()
    {
        thr = dbToGain (thrDb (k[0]));
        holdN = (int64_t) std::llround (holdSec (k[1]) * sr);
        attStep = (float) (1.0 / std::max (1.0, attSec (k[2]) * sr));
        relMul = (float) std::exp (-1.0 / (relSec (k[3]) * sr));
    }
    double sr = 48000.0;
    float k[4] { 0.2f, 0.0f, 0.0f, 0.0f };
    float thr = 0.0f, attStep = 1.0f, relMul = 0.0f;
    int64_t holdN = 4800, holdLeft = 0;
    float gain = 0.0f;
};
} // namespace aiofx
