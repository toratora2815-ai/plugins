#pragma once
// TUBE COMP … 真空管ふうのコンプ（Guitar Rig 7 の Tube Compressor を測って合わせ込み、2026-11-18）
//
// 流れ（測った結果から）: SATURATION（押し込み＋左右対称の飽和、3 次の歪み）→ INPUT → コンプ → OUTPUT
//   （本物は INPUT を上げても歪みが増えない = INPUT は飽和のあと。小さい音の歪みはコンプの速い動きから）
//   INPUT / OUTPUT … ±48 dB（96 dB × (つまみ − 0.5)）
//   SATURATION … 押し込む量。1 単位で約 100 dB。押し込んだ分は戻さない（本物も音量が変わる）
//   THRESHOLD / RATIO / KNEE … 普通のコンプ（RATIO 0 で 1:1）
//   ATTACK / RELEASE / DYNAMIC … 時間（DYNAMIC は大きい音のあとで戻りを遅くする）
// 係数は Fit（tools/render の合わせ込みで決めた値）。
#include "Module.h"

namespace aiofx
{
struct TubeCompFit
{
    // build/aiofx/tcfit で合わせ込んだ値（2026-11-18。階段 31 段の各段の大きさ ＋ 3 次の歪み、つまみ 9 本 × 5 段 = 43 通り。初期値で差 0.49 dB）
    // ATTACK・RELEASE は本物の段差（静か → 大きい → 静か）から決めて固定（初期値で戻り約 80 ms、RELEASE 0.5 で数秒、0.75 から上はほぼ止まる）
    float drv0 = -62.412f, drv1 = 118.133f;       // SATURATION → 押し込み dB = drv0 + drv1 × つまみ
    float satCeil = 1.6338f;                  // 飽和の天井
    float satSoft = 1.0f;                     // 飽和の硬さ（x / (1 + |x/c|^p)^(1/p)。1 = いちばんやわらか）
    float satMix = 0.0f;                      // 小さい音でも出る 3 次の歪み（合わせ込みで 0 になった）
    float out0 = 2.644f;                     // OUTPUT の基準のずれ
    float thr0 = -57.461f, thr1 = 39.374f;        // THRESHOLD → dB
    float rat1 = 9.321f, ratP = 2.470f;        // RATIO → 1 + rat1 × つまみ^ratP
    float knee1 = 51.971f;                    // KNEE → 幅 dB
    float att0 = 0.5f, att1 = 2981.0f;        // ATTACK → ms = 0.5 × e^(8 × つまみ)
    float rel0 = 3.0f, rel1 = 729416.0f;      // RELEASE → ms = 3 × e^(13.5 × つまみ)
    float dyn1 = 4.0f;                        // DYNAMIC → 戻りを遅くする倍率（最大）
    float detRms = 1.0f;                      // 1 = RMS 型の検出、0 = 山
};

struct TubeComp final : Module
{
    static TubeCompFit& fit() { static TubeCompFit f; return f; }
    static float ioDb (float v) { return 96.0f * (v - 0.5f); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tubecomp", "TUBE COMP", "DYNAMICS",
            "Warm tube-style compressor. SATURATION drives the tube (and gets louder), then it compresses.",
            {
                { "SATURATION", 0.59f,  0, [] (float v) { return fmt (fit().drv0 + fit().drv1 * v, 1, " dB"); } },
                { "INPUT",      0.5f,   0, [] (float v) { return fmt (ioDb (v), 1, " dB"); } },
                { "THRESHOLD",  0.49f,  0, [] (float v) { return fmt (fit().thr0 + fit().thr1 * v, 1, " dB"); } },
                { "RATIO",      0.225f, 0, [] (float v) { return fmt (1.0f + fit().rat1 * std::pow (v, fit().ratP), 1, ":1"); } },
                { "KNEE",       0.54f,  0, [] (float v) { return fmt (fit().knee1 * v, 1, " dB"); } },
                { "DYNAMIC",    0.25f,  0, [] (float v) { return fmtPct (v); } },
                { "ATTACK",     0.09f,  0, [] (float v) { return fmtTime (0.001f * fit().att0 * std::pow (fit().att1, v)); } },
                { "RELEASE",    0.246f, 0, [] (float v) { return fmtTime (0.001f * fit().rel0 * std::pow (fit().rel1, v)); } },
                { "OUTPUT",     0.535f, 0, [] (float v) { return fmt (ioDb (v), 1, " dB"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (int i = 0; i < 9; ++i) { sm[i].prepare (sr, 0.02); sm[i].set (k[i], true); }
        recalc(); reset();
    }
    void reset() override { env = 0.0f; grDb = 0.0f; slow = 0.0f; }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 9 || same (k[i], v)) return;
        k[i] = v; sm[i].set (v);
        recalc();
    }
    void process (float* L, float* R, int n) override
    {
        const auto& F = fit();
        for (int s = 0; s < n; ++s)
        {
            const float pre = dbToGain (F.drv0 + F.drv1 * sm[0].next());
            const float inG = dbToGain (ioDb (sm[1].next()));
            const float outG = dbToGain (ioDb (sm[8].next()) + F.out0);
            // THRESHOLD・RATIO・KNEE はなめらかにした値から 16 サンプルごとに（一気に回してもプチッとしない）
            const float vThr = sm[2].next(), vRat = sm[3].next(), vKnee = sm[4].next();
            staticCurve (vThr, vRat, vKnee);
            float l = L[s] * pre, r = R[s] * pre;
            // 飽和（左右対称 → 3 次の歪み）→ INPUT
            l = shape (l, F) * inG; r = shape (r, F) * inG;
            // 検出（RMS 型か山）
            const float pk = std::max (std::abs (l), std::abs (r));
            const float lvl = F.detRms > 0.5f ? 0.5f * (l * l + r * r) * 2.0f : pk * pk;
            const float a = lvl > env ? attA : relA;
            env = flush (lvl + (env - lvl) * a);
            const float levDb = 10.0f * std::log10 (std::max (env, 1.0e-20f));
            // 静的な曲線（ソフトニー）
            const float over = levDb - thrDb;
            float red = 0.0f;
            if (2.0f * over < -kneeDb) red = 0.0f;
            else if (2.0f * std::abs (over) <= kneeDb && kneeDb > 0.0f)
                red = (1.0f / ratio - 1.0f) * (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) / (2.0f * kneeDb);
            else red = (1.0f / ratio - 1.0f) * over;
            // DYNAMIC: 強く下げたあとは戻りを遅く
            slow = std::max (-red, slow * dynA);
            const float g = dbToGain (red);
            L[s] = l * g * outG; R[s] = r * g * outG;
            grDb = red;
        }
    }
    float meter() const override { return std::min (1.0f, -grDb / 24.0f); }

private:
    static float shape (float x, const TubeCompFit& F)
    {
        const float c = F.satCeil, p = std::max (1.0f, F.satSoft);
        const float u = x / c;
        float y = c * u / std::pow (1.0f + std::pow (std::abs (u), p), 1.0f / p);
        const float y2 = y * y;
        return y - F.satMix * y * y2 / (1.0f + y2);
    }
    void staticCurve (float vThr, float vRat, float vKnee)
    {
        const auto& F = fit();
        thrDb = F.thr0 + F.thr1 * vThr;
        ratio = 1.0f + F.rat1 * std::pow (std::max (0.0f, vRat), F.ratP);
        kneeDb = F.knee1 * vKnee;
    }
    void recalc()
    {
        const auto& F = fit();
        staticCurve (sm[2].cur, sm[3].cur, sm[4].cur);
        const float attMs = F.att0 * std::pow (F.att1, k[6]);
        // 本物は RELEASE 0.75 から上でほとんど戻らない。こちらは 60 秒で頭打ち（下げたままにはしない。ホストのリセットでも消える）
        const float relMs = std::min (60000.0f, F.rel0 * std::pow (F.rel1, k[7]) * (1.0f + F.dyn1 * k[5] * 0.25f));
        attA = (float) std::exp (-1.0 / (attMs * 0.001 * sr));
        relA = (float) std::exp (-1.0 / (relMs * 0.001 * sr));
        dynA = (float) std::exp (-1.0 / (0.5 * sr));
    }
    double sr = 48000.0;
    float k[9] { 0.59f, 0.5f, 0.49f, 0.225f, 0.54f, 0.25f, 0.09f, 0.246f, 0.535f };
    Smooth sm[9];
    float thrDb = -24.0f, ratio = 2.0f, kneeDb = 12.0f, attA = 0.9f, relA = 0.999f, dynA = 0.9999f;
    float env = 0.0f, grDb = 0.0f, slow = 0.0f;
};
} // namespace aiofx
