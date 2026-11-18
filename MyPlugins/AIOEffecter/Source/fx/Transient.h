#pragma once
// TRANSIENT … 音の頭（ATTACK）と伸び（SUSTAIN）を別々に上げ下げする（Guitar Rig の Transient Master を測って合わせ込み、2026-11-19）
//
// しくみ（よくある形）: 大きさの追いかけ（山）を 2 組
//   頭: 速く上がる追いかけ − ゆっくり上がる追いかけ（どちらも同じ速さで下がる）= 鳴り始めだけ正（dB）
//   伸び: ゆっくり下がる追いかけ − 速く下がる追いかけ（どちらも速く上がる）= 音が減っていく間だけ正（dB）
//   下げる / 上げる量 = kA × (ATTACK − 0.5) × 2 × 頭 ＋ (SUSTAIN − 0.5) × 2 × (kS × 伸び ＋ kSt × しきい値より小さい分)（dB）
//   （本物の SUSTAIN は、減っていく所だけでなく、−36 dB くらいより小さい音もずっと上げ下げする）
// SMOOTH ON で量をなめらかに。GAIN（表）→ CLIP ON でやわらかい頭打ち（4 倍）
// 係数は build/aiofx/fit/tmfit.py で合わせ込み
#include "DynKit.h"

namespace aiofx
{
struct TransientFit
{
    // build/aiofx/fit/tmfit.py で合わせ込んだ値（2026-11-19、Transient Master の 21 通り。静的な曲線の差 0.2〜1.1 dB）
    float aFastAtt = -2.604f, aSlowAtt = -2.01f, aRel = -0.7993f;     // 頭の追いかけ（log10 秒）
    float sAtt = -3.979f, sFastRel = -1.617f, sSlowRel = -0.128f;     // 伸びの追いかけ
    float kA = 1.959f, kS = 0.7035f;                                 // dB あたりの効き
    float kSt = 0.8867f, tSus = -40.57f;                            // 伸びの静的な分: しきい値より小さい音を (SUSTAIN − 0.5) × 2 × kSt dB/dB 上げ下げ
    float maxA = 13.88f, maxS = 36.58f;                           // 上げ下げの上限 dB
    float smooth = -3.988f;                                       // SMOOTH の時定数（log10 秒）
    float g0 = -24.15f, g1 = -12.13f, g2 = -0.04545f, g3 = 2.877f, g4 = 5.829f;   // GAIN 0, .25, .5, .75, 1 → dB
    float clipC = 1.169f, clipP = 7.999f;                           // CLIP: x / (1 + |x/c|^p)^(1/p)
};

struct Transient final : Module
{
    static TransientFit& fit() { static TransientFit f; return f; }
    static float gainDb (float v)
    {
        const auto& F = fit();
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        const float ys[5] { F.g0, F.g1, F.g2, F.g3, F.g4 };
        return interpTable (xs, ys, v);
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "transient", "TRANSIENT", "DYNAMICS",
            "Shapes the start (ATTACK) and the ring-out (SUSTAIN) of each note. 50 % = no change.",
            {
                { "ATTACK",  0.63f, 0, [] (float v) { return fmt ((v - 0.5f) * 200.0f, 0, " %"); } },
                { "SUSTAIN", 0.36f, 0, [] (float v) { return fmt ((v - 0.5f) * 200.0f, 0, " %"); } },
                { "GAIN",    0.56f, 0, [] (float v) { return fmt (gainDb (v), 1, " dB"); } },
                { "CLIP",    1.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "SMOOTH",  1.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; outG.prepare (sr, 0.02); amtA.prepare (sr, 0.02); amtS.prepare (sr, 0.02); clipAmt.prepare (sr, 0.02);
        recalc(); outG.set (dbToGain (gainDb (k[2])), true); amtA.set ((k[0] - 0.5f) * 2.0f, true); amtS.set ((k[1] - 0.5f) * 2.0f, true);
        clipAmt.set (k[3] >= 0.5f ? 1.0f : 0.0f, true);
        reset();
    }
    void reset() override
    {
        eA1 = eA2 = eS1 = eS2 = 0.0f; gSm = 0.0f;
        for (auto& o : os) o.reset();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 5 || same (k[i], v)) return;
        k[i] = v; recalc();
    }
    void process (float* L, float* R, int n) override
    {
        const auto& F = fit();
        for (int s = 0; s < n; ++s)
        {
            const float x = std::max (std::abs (L[s]), std::abs (R[s]));
            eA1 = x > eA1 ? x + (eA1 - x) * cA1 : x + (eA1 - x) * cAr;
            eA2 = x > eA2 ? x + (eA2 - x) * cA2 : x + (eA2 - x) * cAr;
            eS1 = x > eS1 ? x + (eS1 - x) * cSa : x + (eS1 - x) * cSs;
            eS2 = x > eS2 ? x + (eS2 - x) * cSa : x + (eS2 - x) * cSf;
            eA1 = flush (eA1); eA2 = flush (eA2); eS1 = flush (eS1); eS2 = flush (eS2);
            const float dA = std::max (0.0f, levelDb (eA1) - levelDb (eA2));
            const float dS = std::max (0.0f, levelDb (eS1) - levelDb (eS2));
            const float a = amtA.next(), su = amtS.next();
            const float below = std::min (24.0f, std::max (0.0f, F.tSus - levelDb (eS2)));   // しきい値より小さい分（速く下がる追いかけで。24 dB まで）
            float g = std::clamp (F.kA * a * dA, -F.maxA, F.maxA) + std::clamp (su * (F.kS * dS + F.kSt * below), -F.maxS, F.maxS);
            if (smoothOn) { gSm = g + (gSm - g) * cSm; g = gSm; } else gSm = g;
            meterDb = g;
            const float m = dbToGain (g) * outG.next();
            float l = L[s] * m, r = R[s] * m;
            // CLIP: いつも 4 倍の中を通し、効き具合を 20 ms でならす（ON / OFF でプチッとしない。遅れもいつも同じ 4 サンプル）
            const float ca = clipAmt.next();
            l = clip4 (os[0], l, ca); r = clip4 (os[1], r, ca);
            L[s] = l; R[s] = r;
        }
    }
    float meter() const override { return std::min (1.0f, std::abs (meterDb) / 24.0f); }
    int latency() const override { return 4; }   // 4 倍のフィルター（CLIP の ON / OFF にかかわらず通す）

private:
    static float clipShape (float x)
    {
        const auto& F = fit();
        return x / std::pow (1.0f + std::pow (std::abs (x / F.clipC), F.clipP), 1.0f / F.clipP);
    }
    static float clip4 (Oversampler4& o, float x, float amt)
    {
        float u[4]; o.up (x, u);
        if (amt > 1.0e-4f) for (auto& v : u) v += amt * (clipShape (v) - v);
        return o.down (u);
    }
    void recalc()
    {
        const auto& F = fit();
        cA1 = tauCoef (sr, std::pow (10.0, F.aFastAtt)); cA2 = tauCoef (sr, std::pow (10.0, F.aSlowAtt)); cAr = tauCoef (sr, std::pow (10.0, F.aRel));
        cSa = tauCoef (sr, std::pow (10.0, F.sAtt)); cSf = tauCoef (sr, std::pow (10.0, F.sFastRel)); cSs = tauCoef (sr, std::pow (10.0, F.sSlowRel));
        cSm = tauCoef (sr, std::pow (10.0, F.smooth));
        outG.set (dbToGain (gainDb (k[2])));
        amtA.set ((k[0] - 0.5f) * 2.0f); amtS.set ((k[1] - 0.5f) * 2.0f);
        clipAmt.set (k[3] >= 0.5f ? 1.0f : 0.0f); smoothOn = k[4] >= 0.5f;
    }
    double sr = 48000.0;
    float k[5] { 0.63f, 0.36f, 0.56f, 1.0f, 1.0f };
    float cA1 = 0, cA2 = 0, cAr = 0, cSa = 0, cSf = 0, cSs = 0, cSm = 0;
    float eA1 = 0, eA2 = 0, eS1 = 0, eS2 = 0, gSm = 0, meterDb = 0;
    bool smoothOn = true;
    Smooth outG, amtA, amtS, clipAmt;
    Oversampler4 os[2];
};

inline FitAdd transientFitAdd {
    { "tm.aFastAtt", &Transient::fit().aFastAtt }, { "tm.aSlowAtt", &Transient::fit().aSlowAtt }, { "tm.aRel", &Transient::fit().aRel },
    { "tm.sAtt", &Transient::fit().sAtt }, { "tm.sFastRel", &Transient::fit().sFastRel }, { "tm.sSlowRel", &Transient::fit().sSlowRel },
    { "tm.kA", &Transient::fit().kA }, { "tm.kS", &Transient::fit().kS }, { "tm.kSt", &Transient::fit().kSt }, { "tm.tSus", &Transient::fit().tSus }, { "tm.maxA", &Transient::fit().maxA }, { "tm.maxS", &Transient::fit().maxS },
    { "tm.smooth", &Transient::fit().smooth },
    { "tm.g0", &Transient::fit().g0 }, { "tm.g1", &Transient::fit().g1 }, { "tm.g2", &Transient::fit().g2 }, { "tm.g3", &Transient::fit().g3 }, { "tm.g4", &Transient::fit().g4 },
    { "tm.clipC", &Transient::fit().clipC }, { "tm.clipP", &Transient::fit().clipP },
};
} // namespace aiofx
