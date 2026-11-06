// Forge Strip: モジュールを並べてつなぐ本体（JUCE なし）
#pragma once
#include "Eq.h"
#include "Dynamics.h"
#include "Color.h"
#include "Spectral.h"
#include <string>

namespace fs
{
enum ModuleId
{
    ModSculptor = 0, ModEq, ModComp1, ModExciter, ModTransient, ModComp2, ModGate, ModDensity, ModClipper, ModPhase, ModUnmask,
    numModules
};

inline const char* moduleName (int m)
{
    static const char* n[] = { "SCULPTOR", "EQ", "COMP 1", "EXCITER", "TRANSIENT", "COMP 2", "GATE", "DENSITY", "CLIPPER", "PHASE", "UNMASK" };
    return n[clampv (m, 0, (int) numModules - 1)];
}

struct GlobalSettings
{
    bool bypass = false;
    float inGain = 0, outGain = 0;   // dB
    float pan = 0;                   // -1..1
    float width = 0;                 // -100..100（-100 = モノラル、+100 = サイド 2 倍）
    bool mono = false, swap = false, invert = false;
    float delayL = 0, delayR = 0;    // ms
};

struct StripSettings
{
    GlobalSettings global;
    bool on[numModules] {};
    float mix[numModules];          // 0..1（モジュールごとの全体ミックス）
    int order[numModules];          // 並び順（モジュール番号）
    EqSettings eq;
    CompSettings comp[2];
    ExciterSettings exciter;
    TransientSettings transient;
    GateSettings gate;
    DensitySettings density;
    ClipperSettings clipper;
    PhaseSettings phase;
    SculptorSettings sculptor;
    UnmaskSettings unmask;
    bool limiterOn = false;
    LimiterSettings limiter;
    StripSettings()
    {
        for (int i = 0; i < numModules; ++i) { mix[i] = 1.0f; order[i] = i; }
        comp[1].vintage = true;   // 2 台目は Vintage が最初
    }
};

inline std::string orderToString (const int* o)
{
    std::string s; for (int i = 0; i < numModules; ++i) { if (i) s += ","; s += std::to_string (o[i]); } return s;
}
inline void orderFromString (const std::string& s, int* o)
{
    int tmp[numModules]; int n = 0; bool seen[numModules] {};
    size_t p = 0;
    while (p < s.size() && n < numModules)
    {
        size_t q = s.find (',', p); if (q == std::string::npos) q = s.size();
        const int v = std::atoi (s.substr (p, q - p).c_str());
        if (v >= 0 && v < numModules && ! seen[v]) { tmp[n++] = v; seen[v] = true; }
        p = q + 1;
    }
    for (int m = 0; m < numModules; ++m) if (! seen[m]) tmp[n++] = m;   // 足りないものは後ろへ
    for (int i = 0; i < numModules; ++i) o[i] = tmp[i];
}

class Strip
{
public:
    static constexpr int maxBlock = BandSplit::maxBlock;

    void prepare (double s, int)
    {
        sr = s;
        eq.prepare (s); comp[0].prepare (s); comp[1].prepare (s); exciter.prepare (s); transient.prepare (s);
        gate.prepare (s); density.prepare (s); clipper.prepare (s); phase.prepare (s); sculptor.prepare (s); unmask.prepare (s);
        limiter.prepare (s);
        const int maxLat = std::max (sculptor.latency(), density.latency()) + 8;
        for (int m = 0; m < numModules; ++m) for (int c = 0; c < 2; ++c) dryDelay[m][c].prepare (maxLat);
        for (int c = 0; c < 2; ++c) { chDelay[c].prepare ((int) (0.1 * s) + 4); scRing[c].assign ((size_t) (maxLat * 3 + maxBlock * 2), 0.0f); }
        scW = 0;
        reset();
    }
    void reset()
    {
        eq.reset(); comp[0].reset(); comp[1].reset(); exciter.reset(); transient.reset(); gate.reset(); density.reset();
        clipper.reset(); phase.reset(); sculptor.reset(); unmask.reset(); limiter.reset();
        for (int m = 0; m < numModules; ++m) for (int c = 0; c < 2; ++c) dryDelay[m][c].reset();
        for (int c = 0; c < 2; ++c) { chDelay[c].reset(); std::fill (scRing[c].begin(), scRing[c].end(), 0.0f); }
        scW = 0;
        for (int m = 0; m < numModules; ++m) wasOn[m] = false;
    }

    void set (const StripSettings& s)
    {
        cfg = s;
        eq.set (s.eq); comp[0].set (s.comp[0]); comp[1].set (s.comp[1]); exciter.set (s.exciter); transient.set (s.transient);
        gate.set (s.gate); density.set (s.density); clipper.set (s.clipper); phase.set (s.phase); sculptor.set (s.sculptor);
        unmask.set (s.unmask); limiter.set (s.limiter);
    }

    int moduleLatency (int m) const
    {
        switch (m)
        {
            case ModTransient: return transient.latency();
            case ModDensity: return density.latency();
            case ModExciter: return exciter.latency();
            case ModClipper: return clipper.latency();
            case ModSculptor: return sculptor.latency();
            case ModUnmask: return unmask.latency();
            default: return 0;
        }
    }
    // 今の設定での全体の遅れ
    int latencyFor (const StripSettings& s) const
    {
        int l = 0;
        for (int m = 0; m < numModules; ++m) if (s.on[m]) l += moduleLatency (m);
        if (s.limiterOn) l += s.limiter.mode == 2 ? 0 : (int) std::round (0.001 * sr);
        return l;
    }

    // L/R をその場で処理。scL/scR はサイドチェーン（なければ nullptr）
    void process (float* L, float* R, int n, const float* scL, const float* scR)
    {
        for (int i0 = 0; i0 < n; i0 += maxBlock)
            processChunk (L + i0, R + i0, std::min (maxBlock, n - i0), scL ? scL + i0 : nullptr, scR ? scR + i0 : nullptr);
    }

    // 表示用
    const EqModule& eqModule() const { return eq; }
    const CompModule& compModule (int i) const { return comp[i]; }
    const GateModule& gateModule() const { return gate; }
    const DensityModule& densityModule() const { return density; }
    const ClipperModule& clipperModule() const { return clipper; }
    const TransientModule& transientModule() const { return transient; }
    const SculptorModule& sculptorModule() const { return sculptor; }
    const UnmaskModule& unmaskModule() const { return unmask; }
    const LimiterModule& limiterModule() const { return limiter; }
    double sampleRate() const { return sr; }

private:
    void processChunk (float* L, float* R, int n, const float* scLin, const float* scRin)
    {
        const auto& g = cfg.global;
        if (g.bypass) return;
        // 入口
        const float ig = dbToGain (g.inGain);
        const int dL = (int) std::round (g.delayL * 0.001 * sr), dR = (int) std::round (g.delayR * 0.001 * sr);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i] * ig, r = R[i] * ig;
            if (g.invert) { l = -l; r = -r; }
            if (g.swap) std::swap (l, r);
            l = chDelay[0].process (l, dL); r = chDelay[1].process (r, dR);
            L[i] = l; R[i] = r;
        }
        // サイドチェーン（主信号と同じだけ遅らせて使う: 前のモジュールの遅れ分）
        const bool hasSc = scLin != nullptr;
        const int ringN = (int) scRing[0].size();
        const int base = scW;
        if (hasSc)
            for (int i = 0; i < n; ++i) { scRing[0][(size_t) ((base + i) % ringN)] = scLin[i]; scRing[1][(size_t) ((base + i) % ringN)] = scRin[i]; }
        scW = (base + n) % ringN;
        int latSoFar = 0;

        float dry[2][maxBlock];
        for (int k = 0; k < numModules; ++k)
        {
            const int m = cfg.order[k];
            if (! cfg.on[m]) { wasOn[m] = false; continue; }
            if (! wasOn[m]) { resetModule (m); wasOn[m] = true; }
            const float mix = clampv (cfg.mix[m], 0.0f, 1.0f);
            const int lat = moduleLatency (m);
            const bool needDry = mix < 0.999f;
            if (needDry)
                for (int i = 0; i < n; ++i) { dry[0][i] = dryDelay[m][0].process (L[i], lat); dry[1][i] = dryDelay[m][1].process (R[i], lat); }
            // サイドチェーンをここまでの遅れに合わせる
            float scl[maxBlock], scr[maxBlock];
            const float* a = nullptr; const float* b = nullptr;
            if (hasSc)
            {
                for (int i = 0; i < n; ++i)
                {
                    const int r = ((base + i - latSoFar) % ringN + ringN) % ringN;
                    scl[i] = scRing[0][(size_t) r]; scr[i] = scRing[1][(size_t) r];
                }
                a = scl; b = scr;
            }
            switch (m)
            {
                case ModSculptor:  sculptor.process (L, R, n, nullptr, nullptr); break;
                case ModEq:        eq.process (L, R, n); break;
                case ModComp1:     comp[0].process (L, R, n, a, b); break;
                case ModComp2:     comp[1].process (L, R, n, a, b); break;
                case ModExciter:   exciter.process (L, R, n); break;
                case ModTransient: transient.process (L, R, n); break;
                case ModGate:      gate.process (L, R, n, a, b); break;
                case ModDensity:   density.process (L, R, n); break;
                case ModClipper:   clipper.process (L, R, n); break;
                case ModPhase:     phase.process (L, R, n); break;
                case ModUnmask:    unmask.process (L, R, n, a, b); break;
                default: break;
            }
            if (needDry)
                for (int i = 0; i < n; ++i) { L[i] = mix * L[i] + (1.0f - mix) * dry[0][i]; R[i] = mix * R[i] + (1.0f - mix) * dry[1][i]; }
            latSoFar += lat;
        }
        // 出口: パン・幅・モノラル・出力
        const float og = dbToGain (g.outGain);
        const float side = clampv (1.0f + g.width / 100.0f, 0.0f, 2.0f);
        const float pl = g.pan > 0 ? 1.0f - g.pan : 1.0f, pr = g.pan < 0 ? 1.0f + g.pan : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float mid = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]) * (g.mono ? 0.0f : side);
            float l = (mid + sd) * pl * og, r = (mid - sd) * pr * og;
            L[i] = l; R[i] = r;
        }
        if (cfg.limiterOn) limiter.process (L, R, n);
    }

    void resetModule (int m)
    {
        switch (m)
        {
            case ModSculptor: sculptor.reset(); break; case ModEq: eq.reset(); break; case ModComp1: comp[0].reset(); break;
            case ModComp2: comp[1].reset(); break; case ModExciter: exciter.reset(); break; case ModTransient: transient.reset(); break;
            case ModGate: gate.reset(); break; case ModDensity: density.reset(); break; case ModClipper: clipper.reset(); break;
            case ModPhase: phase.reset(); break; case ModUnmask: unmask.reset(); break; default: break;
        }
        for (int c = 0; c < 2; ++c) dryDelay[m][c].reset();
    }

    StripSettings cfg;
    EqModule eq; CompModule comp[2]; ExciterModule exciter; TransientModule transient; GateModule gate; DensityModule density;
    ClipperModule clipper; PhaseModule phase; SculptorModule sculptor; UnmaskModule unmask; LimiterModule limiter;
    Delay dryDelay[numModules][2], chDelay[2];
    std::vector<float> scRing[2]; int scW = 0;
    bool wasOn[numModules] {};
    double sr = 48000;
};
} // namespace fs
