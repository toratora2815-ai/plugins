#pragma once
// LIMITER … 頭を天井で抑える（Guitar Rig 7 の Limiter を測って合わせ込み、2026-11-18）
//
// 測った結果（build/aiofx/lim、cap/dyn/Limiter）:
//   順番     … 先に OUTPUT で上げ下げしてから、天井で抑える
//   OUTPUT  … −20 dB + 40 dB × つまみ
//   LIMIT   … 天井 −40 dB + 80 dB × つまみ（1 kHz の正弦波で 0.05 dB 以内）
//   抑える   … 一瞬（1 ms の窓で最初から天井ちょうど）
//   HOLD    … 抑える量が減っても戻し始めるまで 0.1 s × 300^つまみ（Noise Gate と同じ式）
//   RELEASE … 戻すときは dB で一定の速さ: 86.9 dB/s × 100^(−つまみ)（0.87〜87 dB/s）
//             戻すのは出口の山が「天井 − 3 dB」に届くまで（強く抑えたあと、天井より 3 dB 下で止まる。本物の動き）
#include "Module.h"

namespace aiofx
{
struct Limiter final : Module
{
    static float ceilDb (float v)    { return -40.0f + 80.0f * v; }
    static float outDb (float v)     { return -20.0f + 40.0f * v; }
    static float holdSec (float v)   { return 0.1f * std::pow (300.0f, v); }
    static float relDbPerSec (float v) { return 86.9f * std::pow (100.0f, -v); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "limit", "LIMITER", "DYNAMICS",
            "Keeps peaks under LIMIT. OUTPUT is applied first, then the ceiling.",
            {
                { "LIMIT",   0.484f, 0, [] (float v) { return fmt (ceilDb (v), 1, " dB"); } },
                { "RELEASE", 0.0f,   0, [] (float v) { return fmt (relDbPerSec (v), relDbPerSec (v) < 10.0f ? 1 : 0, " dB/s"); } },
                { "HOLD",    0.0f,   0, [] (float v) { return fmtTime (holdSec (v)); } },
                { "OUTPUT",  0.5f,   0, [] (float v) { return fmt (outDb (v), 1, " dB"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; outGain.prepare (sr, 0.01); ceilSm.prepare (sr, 0.02);
        recalc(); outGain.set (dbToGain (outDb (k[3])), true); ceilSm.set (ceilDb (k[0]), true);
        ceil = dbToGain (ceilDb (k[0])); relCeil = ceil * 0.70794578f; reset();
    }
    void reset() override { gainDb = 0.0f; holdLeft = 0; pkHold = 0.0f; }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 4 || same (k[i], v)) return;
        k[i] = v; recalc();
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float og = outGain.next();
            const float cdb = ceilSm.next();
            { ceil = dbToGain (cdb); relCeil = ceil * 0.70794578f; }   // 天井はなめらかに（一気に下げても波形を急に削らない）
            float l = L[s] * og, r = R[s] * og;
            const float pk = std::max (std::abs (l), std::abs (r));
            const float needDb = pk > ceil ? gainToDb (ceil / pk) : 0.0f;
            pkHold = std::max (pk, pkHold * pkDecay);
            if (needDb <= gainDb) { gainDb = needDb; holdLeft = holdN; }
            else if (holdLeft > 0) --holdLeft;
            else
            {
                // 最近の山が天井 − 3 dB に届くまでだけ戻す
                const float roomDb = pkHold > relCeil ? gainToDb (relCeil / pkHold) : 0.0f;
                if (gainDb < roomDb) gainDb = std::min (roomDb, gainDb + relStep);
            }
            const float g = dbToGain (gainDb);
            L[s] = l * g; R[s] = r * g;
        }
    }
    float meter() const override { return std::min (1.0f, -gainDb / 24.0f); }

private:
    void recalc()
    {
        ceilSm.set (ceilDb (k[0]));
        pkDecay = (float) std::exp (-1.0 / (0.15 * sr));
        relStep = (float) (relDbPerSec (k[1]) / sr);
        holdN = (int64_t) std::llround (holdSec (k[2]) * sr);
        outGain.set (dbToGain (outDb (k[3])));
    }
    double sr = 48000.0;
    float k[4] { 0.484f, 0.0f, 0.0f, 0.5f };
    float ceil = 1.0f, relCeil = 0.7f, relStep = 0.002f, pkHold = 0.0f, pkDecay = 0.999f;
    int64_t holdN = 4800, holdLeft = 0;
    float gainDb = 0.0f;
    Smooth outGain, ceilSm;
};
} // namespace aiofx
