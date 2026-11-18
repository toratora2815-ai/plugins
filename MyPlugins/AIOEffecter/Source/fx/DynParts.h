#pragma once
// 抑える系の部品（0.2.0）。中身は共通エンジン（DynEngine.h）＋自動で合わせ込んだ表（DynTables.h）。
// つまみは Guitar Rig と同じ数・同じ 0..1 の尺度（選ぶつまみは 番号 / (数 − 1)）。見た目・名前はオリジナル。
#include "DynTables.h"

namespace aiofx
{
// ---- 値の文字（表から読む）----
template <const DynSpec& (*S)(), int K, int J> std::string dynTxDbRel (float v)
{
    const auto& s = S(); const float d = dynKnobValue (s.knobs[K], J, v) - dynKnobValue (s.knobs[K], J, s.knobDef[K]);
    return fmt (d, 1, " dB");
}
template <const DynSpec& (*S)(), int K, int J> std::string dynTxTime (float v)
{
    return fmtTime (std::pow (10.0f, dynKnobValue (S().knobs[K], J, v)));
}
template <const DynSpec& (*S)(), int K, int J> std::string dynTxRatio (float v)
{
    const float r = std::pow (2.0f, std::max (0.0f, dynKnobValue (S().knobs[K], J, v)));
    return r > 30.0f ? std::string ("inf:1") : fmt (r, r < 10.0f ? 1 : 0, ":1");
}
template <const DynSpec& (*S)(), int K, int J> std::string dynTxHz (float v)
{
    const float lg = dynKnobValue (S().knobs[K], J, v);
    return lg < 1.4f ? std::string ("OFF") : fmtHz (std::pow (10.0f, lg));
}
inline std::string txOnOff (float v) { return v >= 0.5f ? "ON" : "OFF"; }
inline std::string txDryPct (float v) { return "DRY " + fmtPct (v); }

// ---------------------------------------------------------------------
// TUBE PUSH … 1 つまみで太く・前に出す真空管ふうのコンプ（Guitar Rig の Supercharger を測って合わせ込み）
struct TubePush final : DynEngine
{
    TubePush() : DynEngine (specSupercharger()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tubepush", "TUBE PUSH", "DYNAMICS",
            "One-knob tube compressor: COMPRESS for thickness, PUNCH lets the attack through, DIRT adds tube grit. MIX 0 % = all processed.",
            {
                { "INPUT",    0.433333f, 0, fmtPct },
                { "COMPRESS", 0.144444f, 0, fmtPct },
                { "PUNCH",    0.0f,      2, txOnOff },
                { "DIRT",     0.0f,      2, txOnOff },
                { "MIX",      0.0f,      0, txDryPct },
                { "OUTPUT",   0.5f,      0, [] (float v) { return fmt (-20.0f + 40.0f * v, 1, " dB"); } },
            } };
        return i;
    }
};

// TUBE PUSH XL … TUBE PUSH に ATTACK・RELEASE・飽和・音色を足したもの（Guitar Rig の Supercharger GT を測って合わせ込み）
struct TubePushXL final : DynEngine
{
    TubePushXL() : DynEngine (specSuperchargerGT()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tubepushxl", "TUBE PUSH XL", "DYNAMICS",
            "Tube compressor with timing, saturation and tone. CHARACTER lifts lows and highs. MIX 0 % = all processed.",
            {
                { "INPUT",      0.433333f, 0, fmtPct },
                { "DETECT HP",  1.0f,      3, [] (float v) { return std::string (v < 0.25f ? "OFF" : (v < 0.75f ? "LOW" : "HIGH")); } },
                { "SATURATION", 0.0f,      0, fmtPct },
                { "SAT MODE",   1.0f,      3, [] (float v) { return std::string (v < 0.25f ? "A" : (v < 0.75f ? "B" : "C")); } },
                { "COMPRESS",   0.266666f, 0, fmtPct },
                { "ATTACK",     0.366667f, 0, fmtPct },
                { "RELEASE",    0.444444f, 0, fmtPct },
                { "CHARACTER",  0.0f,      0, fmtPct },
                { "CHAR MODE",  1.0f,      3, [] (float v) { return std::string (v < 0.25f ? "A" : (v < 0.75f ? "B" : "C")); } },
                { "MIX",        0.0f,      0, txDryPct },
                { "OUTPUT",     0.5f,      0, [] (float v) { return fmt (-20.0f + 40.0f * v, 1, " dB"); } },
            } };
        return i;
    }
};

// FET COMP … 速い FET コンプ（Guitar Rig の VC 76 を測って合わせ込み）。DRY 100 % = 元の音に圧縮した音を足す（初期値）
struct FetComp final : DynEngine
{
    FetComp() : DynEngine (specVC76()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "fetcomp", "FET COMP", "DYNAMICS",
            "Fast FET compressor. INPUT drives it harder, OUTPUT sets the compressed level. DRY 100 % adds it on top of the original (parallel).",
            {
                { "INPUT",   1.0f,    0, dynTxDbRel<specVC76, 0, 0> },
                { "RATIO",   1.0f,    6, [] (float v) { return v < 0.1f ? std::string ("ALL") : dynTxRatio<specVC76, 1, 1> (v); } },   // 0 = 全部押し（荒い）、ほかは合わせ込んだ比率
                { "ATTACK",  0.5f,    0, fmtPct },
                { "RELEASE", 0.8959f, 0, fmtPct },
                { "OUTPUT",  0.24f,   0, dynTxDbRel<specVC76, 4, 0> },
                { "DRY",     1.0f,    0, fmtPct },
            } };
        return i;
    }
};

// OPTO COMP … 光センサーのなめらかなコンプ（Guitar Rig の VC 2A を測って合わせ込み）
struct OptoComp final : DynEngine
{
    OptoComp() : DynEngine (specVC2A()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "optocomp", "OPTO COMP", "DYNAMICS",
            "Smooth optical compressor. PEAK RED sets how much it holds down, GAIN makes up the level (and drives the tube output).",
            {
                { "GAIN",      0.324f, 0, dynTxDbRel<specVC2A, 0, 0> },
                { "PEAK RED",  0.614f, 0, fmtPct },
                { "MODE",      0.0f,   2, [] (float v) { return std::string (v < 0.5f ? "COMP" : "LIMIT"); } },
                { "DRY",       0.0f,   0, fmtPct },
                { "DETECT HP", 0.0f,   0, fmtPct },
            } };
        return i;
    }
};

// VCA COMP … 素直な VCA コンプ（Guitar Rig の VC 160 を測って合わせ込み）
struct VcaComp final : DynEngine
{
    VcaComp() : DynEngine (specVC160()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "vcacomp", "VCA COMP", "DYNAMICS",
            "Punchy VCA compressor. THRESHOLD and COMPRESSION (ratio), OUTPUT, and DRY for parallel compression.",
            {
                { "THRESHOLD",   0.40824f, 0, dynTxDbRel<specVC160, 0, 0> },
                { "COMPRESSION", 0.5f,     0, dynTxRatio<specVC160, 1, 0> },
                { "OUTPUT",      0.64382f, 0, dynTxDbRel<specVC160, 2, 0> },
                { "DRY",         0.53f,    0, fmtPct },
                { "DETECT HP",   0.0f,     0, fmtPct },
            } };
        return i;
    }
};

// BUS COMP … まとめ（バス）用の VCA コンプ（Guitar Rig の Solid Buscomp を測って合わせ込み）
struct BusComp final : DynEngine
{
    BusComp() : DynEngine (specSolidBuscomp()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "buscomp", "BUS COMP", "DYNAMICS",
            "Glue compressor for groups and mixes: stepped ATTACK / RELEASE / RATIO, MAKEUP, DRY for parallel.",
            {
                { "THRESHOLD", 0.404f,   0, fmtPct },
                { "MAKEUP",    0.28862f, 0, dynTxDbRel<specSolidBuscomp, 1, 0> },
                { "ATTACK",    1.0f,     6, dynTxTime<specSolidBuscomp, 2, 0> },
                { "RELEASE",   0.0f,     6, dynTxTime<specSolidBuscomp, 3, 0> },
                { "RATIO",     0.6f,     6, dynTxRatio<specSolidBuscomp, 4, 0> },
                { "DRY",       0.0f,     0, fmtPct },
                { "OUTPUT",    0.785f,   0, dynTxDbRel<specSolidBuscomp, 6, 0> },
            } };
        return i;
    }
};

// CHANNEL DYN … チャンネルのコンプ＋ゲート（Guitar Rig の Solid Dynamics を測って合わせ込み）
struct ChannelDyn final : DynEngine
{
    ChannelDyn() : DynEngine (specSolidDynamics()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "chandyn", "CHANNEL DYN", "DYNAMICS",
            "Channel-strip compressor with a gate / expander. RANGE 0 = gate off. WET is the output level.",
            {
                { "RATIO",     0.39393f, 0, fmtPct },
                { "KNEE",      1.0f,     2, [] (float v) { return std::string (v < 0.5f ? "SOFT" : "HARD"); } },
                { "THRESHOLD", 0.81f,    0, fmtPct },
                { "REL TYPE",  0.0f,     2, [] (float v) { return std::string (v < 0.5f ? "A" : "B"); } },
                { "ATK TYPE",  0.0f,     2, [] (float v) { return std::string (v < 0.5f ? "A" : "B"); } },
                { "RELEASE",   0.29f,    0, fmtPct },
                { "RANGE",     0.0f,     0, fmtPct },
                { "GATE THR",  0.75f,    0, fmtPct },
                { "EXP MODE",  0.0f,     2, [] (float v) { return std::string (v < 0.5f ? "GATE" : "EXP"); } },
                { "GATE ATK",  0.0f,     2, [] (float v) { return std::string (v < 0.5f ? "A" : "B"); } },
                { "GATE REL",  0.5f,     0, fmtPct },
                { "WET",       0.74833f, 0, dynTxDbRel<specSolidDynamics, 11, 0> },
                { "OUTPUT",    0.0f,     0, fmtPct },
                { "BOOST",     1.0f,     2, txOnOff },
            } };
        return i;
    }
};

// QUICK COMP … つまみ 3 本のかんたんコンプ（Guitar Rig の Fast Comp を測って合わせ込み）
struct QuickComp final : DynEngine
{
    QuickComp() : DynEngine (specFastComp()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "quickcomp", "QUICK COMP", "DYNAMICS",
            "Simple compressor: INPUT pushes it harder (more sustain), MAKEUP sets the level.",
            {
                { "INPUT",  0.62829f, 0, dynTxDbRel<specFastComp, 0, 0> },
                { "MAKEUP", 0.5f,     0, dynTxDbRel<specFastComp, 1, 0> },
                { "ATTACK", 0.5f,     0, fmtPct },
            } };
        return i;
    }
};

// PEDAL COMP … 足元のコンプ（Guitar Rig の Stomp Compressor を測って合わせ込み）
struct PedalComp final : DynEngine
{
    PedalComp() : DynEngine (specStompCompressor()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "pedalcomp", "PEDAL COMP", "DYNAMICS",
            "Stompbox compressor for guitar. SUSTAIN ON squeezes hard for long notes.",
            {
                { "VOLUME",    0.5f, 0, dynTxDbRel<specStompCompressor, 0, 0> },
                { "SUSTAIN",   0.0f, 2, txOnOff },
                { "ATTACK",    1.0f, 2, [] (float v) { return std::string (v < 0.5f ? "FAST" : "SLOW"); } },
                { "RELEASE",   0.0f, 2, [] (float v) { return std::string (v < 0.5f ? "FAST" : "SLOW"); } },
                { "THRESHOLD", 0.5f, 0, fmtPct },
            } };
        return i;
    }
};
} // namespace aiofx
