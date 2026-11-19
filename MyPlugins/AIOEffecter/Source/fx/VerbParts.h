#pragma once
// 響き系の部品（共通エンジン VerbEngine ＋ 自動で作った表 VerbTables.h）。2026-11-19
// 部品ごとに、つまみの並び・文字・表で表せない決まり（MIX の法則・SYNC・FREEZE など）だけを書く
#include "VerbTables.h"
#include "TwinDelay.h"   // 音符の表（SYNC）

namespace aiofx
{
// そのつまみだけを v にしたときのエンジンの値 P（表の差を初期値の値に足したもの。文字の表示用）
template <const VerbSpec& (*S)(), int K, int P>
inline float verbKnobParam (float v)
{
    const auto& s = S();
    float q = s.base[P];
    const auto& t = s.knobs[K];
    for (int j = 0; j < t.nParams; ++j)
        if (t.params[j] == P) q += knobValue (t, j, v) - knobValue (t, j, s.knobDef[K]);
    return q;
}

// MIX（本物の測定どおり）: ドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)^wetPow。
//   ドライの土台（vpDry の表の値）は「ドライだけのときの大きさ」、ウェットは表の基準（初期値）からの差を足す
inline void verbMixLaw (float* q, float m, float mDef, float wetPow = 1.0f)
{
    q[vpDry] *= std::min (1.0f, 2.0f * (1.0f - m));
    q[vpWet] += wetPow * (gainToDb (std::max (1.0e-5f, std::min (1.0f, 2.0f * m))) - gainToDb (std::max (1.0e-5f, std::min (1.0f, 2.0f * mDef))));
}

// MIX（エネルギーで混ぜる型）: ドライ = cos(m π / 2)、ウェット = sin(m π / 2)
inline void verbMixLawCos (float* q, float m, float mDef)
{
    constexpr float hp = 1.5707963f;
    q[vpDry] *= std::cos (m * hp);
    q[vpWet] += gainToDb (std::max (1.0e-5f, std::sin (m * hp))) - gainToDb (std::max (1.0e-5f, std::sin (mDef * hp)));
}

// ---- SPACE VERB（Guitar Rig の Raum を測って合わせ込み）----
// 測った結果（cap/verb/Raum、2 回測りで一致したもの。2026-11-19）:
//   ドライ・ウェットとも中で 16 サンプル遅れる（直線位相のフィルター）→ ここでは遅れなしで出す
//   MODE は 3 種類（4 以上は 2 と同じ）。DENSITY は 2 種類。PREDELAY 0 / .25 / .5 / .75 / 1 → 0 / 20 / 80 / 560 / 2000 ms（響きは 9 ms から）
//   LOW CUT は低い棚（1 で 63 Hz が −22 dB）。REVERB 0 = こだま（予備の遅れの出口）だけ、1 = 響きだけ。
//   間はエネルギーで混ぜる（こだま √(1 − r)、響き √r。本物の量 −13.2 / −14.0 / −14.9 / −16.2 / −17.9 dB と一致）
//   MIX はドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)^1.5（0.25 で +3.2 dB、0.5 で +12.2 dB。初期値 0.195 から）
//   FEEDBACK・MODULATION・RATE は響きの特徴に出ない（予備の遅れ 0・揺れ）→ 大まかな式（細かい測定で詰める）
struct SpaceVerb final : VerbEngine
{
    SpaceVerb() : VerbEngine (specRaum()) {}
    static float addPreMs (float v) { return verbKnobParam<specRaum, 2, vpPre> (v) - verbKnobParam<specRaum, 2, vpPre> (0.0f); }
    static std::string preText (float v, const float* all)
    {
        if (all != nullptr && all[15] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (addPreMs (v), addPreMs (v) < 100.0f ? 1 : 0, " ms");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "spacevb", "SPACE VERB", "REVERB",
            "Big, smooth space. DECAY sets the length, SIZE the room, REVERB 0 % = only the pre-delay echoes.",
            {
                { "MODE",      0.5f,   3, [] (float v) { static const char* n[3] { "TIGHT", "OPEN", "SWELL" }; return std::string (n[std::clamp ((int) std::lround (v * 2.0f), 0, 2)]); } },
                { "DENSITY",   0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "HIGH" : "LOW"); } },
                { "PREDELAY",  0.0f,   0, [] (float v) { return fmt (addPreMs (v), addPreMs (v) < 100.0f ? 1 : 0, " ms"); }, &preText },
                { "FEEDBACK",  0.0f,   0, [] (float v) { return fmtPct (v); } },
                { "SIZE",      0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "DIFFUSION", 0.25f,  0, [] (float v) { return fmtPct (v); } },
                { "DECAY",     0.315f, 0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specRaum, 6, vpT60> (v))); } },
                { "DAMP",      0.25f,  0, [] (float v) { return fmtPct (v); } },
                { "LOW CUT",   0.0f,   0, [] (float v) { return fmtPct (v); } },
                { "HIGH CUT",  1.0f,   0, [] (float v) { return fmtPct (v); } },
                { "MOD",       0.25f,  0, [] (float v) { return fmtPct (v); } },
                { "RATE",      0.25f,  0, [] (float v) { return fmt (0.1f * std::pow (50.0f, v), 2, " Hz"); } },
                { "REVERB",    1.0f,   0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.195f, 0, [] (float v) { return fmtPct (v); } },
                { "FREEZE",    0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "SYNC",      0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        q[vpPreFb] = 0.95f * kk[3];
        q[vpModD] = std::max (0.0f, q[vpModD] + 2.4f * (kk[10] - 0.25f));
        q[vpModR] = std::log10 (0.1f * std::pow (50.0f, kk[11]));
        const float rv = std::clamp (kk[12], 0.0f, 1.0f);
        q[vpEr] *= std::sqrt (rv); q[vpLate] *= std::sqrt (rv);
        // こだま（予備の遅れの出口）の量: 響きの量の表の補正（DECAY・SIZE・MODE など）はこだまには掛けない。HIGH CUT・LOW CUT の分だけ残す
        //   （DSP 検査 0.3.0: REVERB 0 % ＋ DECAY 100 % で入口より +13.4 dB になっていた）
        const auto& sp = specRaum();
        const float wetTables = q[vpWet] - sp.base[vpWet];
        const float cutPart = (verbKnobParam<specRaum, 8, vpWet> (kk[8]) - sp.base[vpWet]) + (verbKnobParam<specRaum, 9, vpWet> (kk[9]) - sp.base[vpWet]);
        q[vpEcho] = 1.057f * std::sqrt (1.0f - rv) * dbToGain (cutPart - wetTables);
        verbMixLaw (q, kk[13], 0.195f, 1.5f);
        q[vpFreeze] = kk[14] >= 0.5f ? 1.0f : 0.0f;
        if (kk[15] >= 0.5f)
        {
            const float pre0 = verbKnobParam<specRaum, 2, vpPre> (0.0f);
            q[vpPre] = pre0 + std::min (2900.0f, (float) (TwinDelay::noteQuarters (kk[2]) * 60000.0 / std::max (20.0, tempo)));
        }
    }
};

// ---- CLASSIC VERB（Guitar Rig の Vintage Verb を測って合わせ込み）----
// 測った結果（cap/verb/VintageVerb）: MODE は 8 種類（7 以上は 7 と同じ）。6・7 はモノの響き、7 は 8 kHz から上がない暗い響き。
//   MIX はドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)（初期値 0.09 から 0.25 で +8.8 dB、0.5 で +14.9 dB）
struct ClassicVerb final : VerbEngine
{
    ClassicVerb() : VerbEngine (specVintageVerb()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "classicvb", "CLASSIC VERB", "REVERB",
            "Eight classic digital reverb types. MONO 1 / 2 are old mono units.",
            {
                { "MODE", 2.0f / 7.0f, 8, [] (float v) { static const char* n[8] { "ROOM", "HALL", "PLATE", "BIG PLATE", "CHAMBER", "BIG HALL", "MONO 1", "MONO 2" };
                                                          return std::string (n[std::clamp ((int) std::lround (v * 7.0f), 0, 7)]); } },
                { "MIX",  0.09f,       0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override { verbMixLaw (q, kk[1], 0.09f, 1.0f); }
};

// ---- SHAPE ROOM（Guitar Rig の Reflektor を測って合わせ込み）----
// 本物は録った部屋の響き（IR）をたたみ込む方式。ここでは共通エンジンで、長さ・始まり・音色・量を合わせた（IR は使っていない）。
// 測った結果（cap/verb/Reflektor）: WET・DRY はそれぞれ直線（WET 0.25 → 0.5 で +6 dB）。PREDELAY 0 / .25 / .5 / .75 / 1 → 0 / 32 / 119 / 356 / 1000 ms。
//   LOW / HIGH / PEAK の GAIN は 0.5 で 0 dB（そのため初期値では FREQ・Q を回しても変わらない → FREQ・Q は大まかな式。細かい測定で詰める）。
//   REVERSE は響きを逆回し（約 1.85 秒かけて大きくなって切れる）。1〜3 は同じ
struct ShapeRoom final : VerbEngine
{
    ShapeRoom() : VerbEngine (specReflektor()) {}
    static float preMs (float v) { return verbKnobParam<specReflektor, 6, vpPre> (v) - verbKnobParam<specReflektor, 6, vpPre> (0.0f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "shaperoom", "SHAPE ROOM", "REVERB",
            "Room you can shape: SIZE stretches it, START skips the beginning, DEPTH moves it farther away. REVERSE plays it backwards.",
            {
                { "WET",       0.265f,  0, [] (float v) { return fmtPct (v); } },
                { "DRY",       1.0f,    0, [] (float v) { return fmtPct (v); } },
                { "SIZE",      0.3333f, 0, [] (float v) { return fmtPct (v); } },
                { "START",     0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "DECAY",     0.6644f, 0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specReflektor, 4, vpT60> (v))); } },
                { "DEPTH",     0.5f,    0, [] (float v) { return fmtPct (v); } },
                { "PREDELAY",  0.0f,    0, [] (float v) { return fmt (preMs (v), preMs (v) < 100.0f ? 1 : 0, " ms"); } },
                { "REVERSE",   0.0f,    2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "LOW GAIN",  0.5f,    0, [] (float v) { return fmt ((v - 0.5f) * 24.0f, 1, " dB"); } },
                { "LOW FREQ",  0.7686f, 0, [] (float v) { return fmtPct (v); } },
                { "PEAK GAIN", 0.5f,    0, [] (float v) { return fmt ((v - 0.5f) * 24.0f, 1, " dB"); } },
                { "PEAK FREQ", 0.5f,    0, [] (float v) { return fmtPct (v); } },
                { "PEAK Q",    0.5f,    0, [] (float v) { return fmtPct (v); } },
                { "HIGH GAIN", 0.5f,    0, [] (float v) { return fmt ((v - 0.5f) * 24.0f, 1, " dB"); } },
                { "HIGH FREQ", 0.4215f, 0, [] (float v) { return fmtPct (v); } },
                { "WIDTH",     0.5985f, 0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }

protected:
    bool usesReverse() const override { return true; }
    void adjust (float* q, const float* kk) override
    {
        q[vpWet] += gainToDb (std::max (1.0e-5f, kk[0]) / 0.265f);
        q[vpDry] = kk[1];
        q[vpReverse] = kk[7] >= 0.5f ? 1.0f : 0.0f;
        q[vpRevLen] = 1850.0f * std::clamp (q[vpSize] / std::max (0.2f, specReflektor().base[vpSize]), 0.3f, 2.0f);
        constexpr float oct = 0.30103f;
        q[vpEqLoF] += oct * 8.0f * (kk[9] - 0.7686f);
        q[vpEqMidF] += oct * 8.0f * (kk[11] - 0.5f);
        q[vpEqMidQ] += 0.6f * (kk[12] - 0.5f);
        q[vpEqHiF] += oct * 6.0f * (kk[14] - 0.4215f);
        q[vpWidth] *= std::clamp (kk[15] / 0.5985f, 0.0f, 1.67f);
    }
};

// ---- PICK ROOM（Guitar Rig の Little Reflektor を測って合わせ込み）----
// 本物は 8 個の録った響き（IR）から選ぶ方式。ここでは共通エンジンで、それぞれの長さ・始まり・音色・量・左右の広がりを合わせた（IR は使っていない）。
// 測った結果（cap/verb/LittleReflektor、足りない IR は cap/list6.tsv で追加で測った）:
//   IR 0〜7 の T60（1 kHz）0.32 / 0.68 / 1.23 / 1.60 / 2.49 / 1.56 / 4.94 / 8.29 s。0・3・6 は左右がほぼ同じ（モノに近い）
//   DECAY で T60 0.58〜5.07 s（初期の IR 4 で）。LOW CUT は 1 で 500 Hz から下を急に削る。MIX はドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)
struct PickRoom final : VerbEngine
{
    PickRoom() : VerbEngine (specLittleReflektor()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "pickroom", "PICK ROOM", "REVERB",
            "Eight rooms to pick from, from a tiny booth to a huge cave. DECAY makes them shorter or longer.",
            {
                { "ROOM",    4.0f / 7.0f, 8, [] (float v) { static const char* n[8] { "BOOTH", "ROOM", "STUDIO", "CHAMBER", "HALL", "PLATE", "CHURCH", "CAVE" };
                                                            return std::string (n[std::clamp ((int) std::lround (v * 7.0f), 0, 7)]); } },
                { "DECAY",   0.6144f,     0, [] (float v) { return fmtPct (v); } },
                { "LOW CUT", 0.155f,      0, [] (float v) { return fmtPct (v); } },
                { "MIX",     0.2204f,     0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override { verbMixLaw (q, kk[3], 0.2204f, 1.0f); }
};

// ---- CLUB VERB（Guitar Rig の Traktor Reverb を測って合わせ込み）----
// 測った結果（cap/verb/TraktorReverb）: SIZE で T60（1 kHz）0.53〜8.2 s。LP は輪の中のローパス（0 で暗く小さく、1 で明るく長い高域）、
//   HP は輪の中のハイパス（0 で 125 Hz の T60 が 6.6 s、1 で 1.8 s）。MIX はドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)。
//   響きには鳴り始めにすぐ出る成分がある（ウェット 100 % で入口の 0.21 倍）→ こだまの量で再現
struct ClubVerb final : VerbEngine
{
    ClubVerb() : VerbEngine (specTraktorReverb()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "clubverb", "CLUB VERB", "REVERB",
            "Simple big reverb for builds and breaks. LP / HP shape the tail, FREEZE holds it.",
            {
                { "SIZE",   0.5f,    0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specTraktorReverb, 0, vpT60> (v))); } },
                { "LP",     0.3644f, 0, [] (float v) { return fmtPct (v); } },
                { "HP",     0.6676f, 0, [] (float v) { return fmtPct (v); } },
                { "MIX",    0.2686f, 0, [] (float v) { return fmtPct (v); } },
                { "FREEZE", 0.0f,    2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        // すぐ出る成分: ウェット 100 % のときの響きの大きさに対して 0.21
        static const float wetFull = dbToGain (specTraktorReverb().base[vpWet] - gainToDb (std::min (1.0f, 2.0f * 0.2686f)));
        q[vpEcho] = 0.206f / std::max (1.0e-4f, wetFull);
        verbMixLaw (q, kk[3], 0.2686f, 1.0f);
        q[vpFreeze] = kk[4] >= 0.5f ? 1.0f : 0.0f;
    }
};

// ---- DIGI HALL（Guitar Rig の RC 24 を測って合わせ込み）----
// 測った結果（cap/verb/RC24）: ALGORITHM は 3 種類（3 以上は 2 と同じ）。PREDELAY = 3.4 + 128 × つまみ ms。
//   MIX はエネルギーで混ぜる（ドライ = cos、ウェット = sin。0.25 → 0.5 で +5.4 dB、0.5 → 1 で +3.0 dB）。
//   INPUT・OUTPUT はどちらもドライとウェットの両方の音量（0.25 / 0.5 / 0.75 / 1 → −10.1 / 0 / +5.1 / +10.0 dB、0 で無音）。
//   MODULATION・Q NOISE は響きの特徴に出ない（揺れ・雑音）→ 大まかな式
struct DigiHall final : VerbEngine
{
    DigiHall() : VerbEngine (specRC24()) {}
    static float ioDb (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { -80.0f, -10.1f, 0.0f, 5.06f, 10.0f };
        return interpTable (xs, ys, v);
    }
    static float preMs (float v) { return verbKnobParam<specRC24, 1, vpPre> (v) - verbKnobParam<specRC24, 1, vpPre> (0.0f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "digihall", "DIGI HALL", "REVERB",
            "Early-80s digital hall. INTENSITY sets the length, BASS / MID / X-OVER how lows and mids ring, REV COLOR the brightness.",
            {
                { "ALGORITHM", 0.5f,    3, [] (float v) { static const char* n[3] { "ALG 1", "ALG 2", "ALG 3" }; return std::string (n[std::clamp ((int) std::lround (v * 2.0f), 0, 2)]); } },
                { "PREDELAY",  0.2018f, 0, [] (float v) { return fmt (preMs (v), 1, " ms"); } },
                { "DEPTH",     0.5179f, 0, [] (float v) { return fmtPct (v); } },
                { "INTENSITY", 0.4551f, 0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specRC24, 3, vpT60> (v))); } },
                { "MODULATION", 1.0f,   4, [] (float v) { return std::to_string (std::clamp ((int) std::lround (v * 3.0f), 0, 3)); } },
                { "BASS",      0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "MID",       0.1783f, 0, [] (float v) { return fmtPct (v); } },
                { "X-OVER",    0.6078f, 0, [] (float v) { return fmtPct (v); } },
                { "HI CUT",    0.7668f, 0, [] (float v) { return fmtPct (v); } },
                { "REV COLOR", 1.0f,    0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.14f,   0, [] (float v) { return fmtPct (v); } },
                { "INPUT",     0.5f,    0, [] (float v) { return fmt (ioDb (v), 1, " dB"); } },
                { "OUTPUT",    0.5f,    0, [] (float v) { return fmt (ioDb (v), 1, " dB"); } },
                { "Q NOISE",   0.0f,    4, [] (float v) { return std::to_string (std::clamp ((int) std::lround (v * 3.0f), 0, 3)); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        q[vpModD] = 0.3f * (float) std::lround (std::clamp (kk[4], 0.0f, 1.0f) * 3.0f);
        verbMixLawCos (q, kk[10], 0.14f);
        const float g = ioDb (kk[11]) + ioDb (kk[12]);
        q[vpWet] += g; q[vpDry] *= dbToGain (g);
    }
};

// ---- DIGI HALL XL（Guitar Rig の RC 48 を測って合わせ込み）----
// 測った結果（cap/verb/RC48）: ALGORITHM・SIZE MODE はそれぞれ 2 種類（2 以上は 1 と同じ）。初期値は予備の遅れが長い大きなホール。
//   SPIN・WANDER は響きの揺れ（特徴に出ない）→ 大まかな式。本物の ECHO（初期反射 6 本）・INPUT・OUTPUT は入れていない（つまみ 16 本まで）。
//   本物は MID 1 で響きがほぼ消える（−38 dB）。表はそこを入れず 0.75 の値のまま
struct DigiHallXL final : VerbEngine
{
    DigiHallXL() : VerbEngine (specRC48()) {}
    static float preMs (float v) { return verbKnobParam<specRC48, 2, vpPre> (v) - verbKnobParam<specRC48, 2, vpPre> (0.0f); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "digihallxl", "DIGI HALL XL", "REVERB",
            "Big late-80s digital hall. SIZE and MID set the length, BASS / X-OVER the low end, SPIN / WANDER the movement.",
            {
                { "ALGORITHM", 1.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "ALG 2" : "ALG 1"); } },
                { "SIZE MODE", 0.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "LARGE" : "NORMAL"); } },
                { "PREDELAY",  0.235f, 0, [] (float v) { return fmt (preMs (v), preMs (v) < 100.0f ? 1 : 0, " ms"); } },
                { "SIZE",      0.735f, 0, [] (float v) { return fmtPct (v); } },
                { "SHAPE",     0.67f,  0, [] (float v) { return fmtPct (v); } },
                { "SPREAD",    0.815f, 0, [] (float v) { return fmtPct (v); } },
                { "DIFFUSION", 0.615f, 0, [] (float v) { return fmtPct (v); } },
                { "BASS",      0.725f, 0, [] (float v) { return fmtPct (v); } },
                { "MID",       0.79f,  0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specRC48, 8, vpT60> (v))); } },
                { "X-OVER",    0.71f,  0, [] (float v) { return fmtPct (v); } },
                { "HI CUT",    0.73f,  0, [] (float v) { return fmtPct (v); } },
                { "REV DAMP",  0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "SHELF",     0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "SPIN",      0.5f,   0, [] (float v) { return fmt (0.1f * std::pow (30.0f, v), 2, " Hz"); } },
                { "WANDER",    0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.5f,   0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        q[vpModR] = std::log10 (0.1f * std::pow (30.0f, kk[13]));
        q[vpModD] = 2.0f * kk[14];
        // MIX（測った値の表）: ドライ 1 / 0.973 / 0.716 / 0.192 / 0、ウェット −∞ / −11.7 / 0 / +3.3 / +3.6 dB（0 / .25 / .5 / .75 / 1、初期値 0.5 が基準）
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, dr[5] { 1.0f, 0.973f, 0.716f, 0.192f, 0.0f }, wd[5] { -80.0f, -11.7f, 0.0f, 3.3f, 3.6f };
        const float m = std::clamp (kk[15], 0.0f, 1.0f);
        q[vpDry] *= interpTable (xs, dr, m);
        q[vpWet] += interpTable (xs, wd, m);
    }
};

// ---- FROST VERB（Guitar Rig の Ice Verb を測って合わせ込み）----
// 測った結果（cap/verb/IceVerb、SIZE / MIX / STEREO の途中は cap/list8.tsv で追加）: 響きは狭い帯域（初期値で 500 Hz〜1 kHz）。
//   COLOR はその帯域の真ん中を 63 Hz（0）〜 8 kHz（1）へ動かし、量も大きく変わる（0 で −27 dB、1 で 0 dB）。ICE で T60 1.5〜8.6 s、SIZE で 0.35〜4.6 s。
//   MIX はドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)（初期値 1 = 響きだけ）。STEREO は 1 でモノ（途中はステレオのまま）
struct FrostVerb final : VerbEngine
{
    FrostVerb() : VerbEngine (specIceVerb()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "frostverb", "FROST VERB", "REVERB",
            "Narrow, shimmering band of reverb. COLOR moves the band from deep lows to airy highs, ICE makes it longer.",
            {
                { "COLOR",  0.52422f, 0, [] (float v) { return fmtPct (v); } },
                { "ICE",    0.37943f, 0, [] (float v) { return fmtTime (std::pow (10.0f, verbKnobParam<specIceVerb, 1, vpT60> (v))); } },
                { "SIZE",   1.0f,     0, [] (float v) { return fmtPct (v); } },
                { "MIX",    1.0f,     0, [] (float v) { return fmtPct (v); } },
                { "STEREO", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "MONO" : "STEREO"); } },
                { "FREEZE", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        verbMixLaw (q, kk[3], 1.0f, 1.0f);
        if (kk[4] >= 0.5f) q[vpWidth] = 0.0f;
        q[vpFreeze] = kk[5] >= 0.5f ? 1.0f : 0.0f;
    }
};

// ---- EIGHT VERB（Guitar Rig の Octaverb を測って合わせ込み）----
// 測った結果（cap/verb/Octaverb）: PRESET は 8 種類（7 以上は 7 と同じ）。DRY/WET はドライ = min(1, 2 (1 − m))・ウェット = min(1, 2 m)、
//   WET LEVEL はウェットの直線の量（0.5 で 0 dB）、SOURCE もドライを min(1, 2 (1 − s)) で下げる。WIDTH は 0.5 でモノ、0 と 1 で広い（0 は左右入れ替え）
struct EightVerb final : VerbEngine
{
    EightVerb() : VerbEngine (specOctaverb()) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "eightverb", "EIGHT VERB", "REVERB",
            "Eight compact reverb presets with SIZE, DAMP, BASSTRAP (low end) and EQ (tone).",
            {
                { "PRESET",    3.0f / 7.0f, 8, [] (float v) { return std::to_string (std::clamp ((int) std::lround (v * 7.0f), 0, 7) + 1); } },
                { "SIZE",      0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "DAMP",      0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "BASSTRAP",  0.39505f, 0, [] (float v) { return fmtPct (v); } },
                { "EQ",        0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "WET LEVEL", 0.5f,     0, [] (float v) { return fmt (gainToDb (std::max (1.0e-5f, v * 2.0f)), 1, " dB"); } },
                { "DRY/WET",   0.24167f, 0, [] (float v) { return fmtPct (v); } },
                { "SOURCE",    0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "WIDTH",     1.0f,     0, [] (float v) { return fmtPct (v); } },
                { "FREEZE",    0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

protected:
    void adjust (float* q, const float* kk) override
    {
        verbMixLaw (q, kk[6], 0.24167f, 1.0f);
        q[vpWet] += gainToDb (std::max (1.0e-5f, kk[5]) / 0.5f);
        q[vpDry] *= std::min (1.0f, 2.0f * (1.0f - kk[7]));
        q[vpWidth] *= 2.0f * kk[8] - 1.0f;
        q[vpFreeze] = kk[9] >= 0.5f ? 1.0f : 0.0f;
    }
};
} // namespace aiofx
