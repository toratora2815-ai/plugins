// Hybrid Kit — 生ドラム（Drum Kit Designer の録音）と EDM の層を重ねて鳴らす本体（JUCE なし）
//
// 流れ: ノート → 部品・奏法・強さの層を選ぶ → 全マイクを同じ瞬間から鳴らす（頭 → 尾は 10 ms で重ねてつなぐ）
//       → 部品ごとのステレオの束（マイクのフェーダーを掛けたもの）→ キックだけ低域を分ける → まとめ
// 中の時間はキットの録音のサンプルレート（44.1 kHz）。ホストのサンプルレートへの変換は Resampler.h（プロセッサー側）
//
// EDM の層（部品ごとに BLEND / SOUND / TUNE だけ。合わせ方は自動）:
//   キック … 808 型のサブ（正弦波・音程が下がる）。生キックの低音（全マイクぶん）を LR4 で切って、そこをサブに任せる（等パワーで入れ替え）。
//            サブは同時に 1 本だけ（次のキックで前のサブは 5 ms で消す）、真ん中・モノ。
//            生の鳴り始めに合わせ、±3 ms と極性は低音がいちばん重なる所を、裏のスレッドが層ごとに選ぶ（analyzeEdm）。大きさも自動で合わせる
//   スネア … クラップ / 大きいクラップ / 909 スネア / スナップ。生の鳴り始めより 1.5 ms 遅らせ、頭を 2 ms 丸めて生のアタックを先に出す。
//            弱い音（ゴースト）では鳴らさない。大きさは生のスネアと同じ帯域の大きさに自動で合わせる
//   EDM の層はルームのマイクには入れない（一体感は、後で足すミックスの処理を共有して出す）
//
// オーディオスレッドではメモリを取らない・ロックしない・ディスクを待たない（叩いた瞬間はメモリの写し、その先は Prefetcher が先読み）
#pragma once
#include "ExsKit.h"
#include <random>

namespace hk
{
constexpr float kPi = 3.14159265358979f;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fs, double f, double q)  { set (fs, f, q, false); }
    void highpass (double fs, double f, double q) { set (fs, f, q, true); }
    void bandpass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2.0 * 3.141592653589793 * f / fs, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0);
        a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void set (double fs, double f, double q, bool hp)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2.0 * 3.141592653589793 * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        if (hp) { b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0; }
        else    { b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0);  b2 = b0; }
        a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    float run (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// LR4（バターワース 2 次を 2 段）。低い方と高い方を足すと平ら（位相だけ回る）
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    void setup (double fs, double f)
    {
        lp1.lowpass (fs, f, 0.70710678); lp2 = lp1;
        hp1.highpass (fs, f, 0.70710678); hp2 = hp1;
    }
    void run (float x, float& lo, float& hi) { lo = lp2.run (lp1.run (x)); hi = hp2.run (hp1.run (x)); }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
};

//==============================================================================
// つまみの組（部品はまとめて 5 組）
enum Group { GKick, GSnare, GToms, GHiHat, GCymbals, NumGroups };
inline int groupOf (int piece)
{
    switch (piece)
    {
        case Kick: return GKick;
        case Snare: return GSnare;
        case HiHat: return GHiHat;
        case CrashL: case CrashR: case Ride: return GCymbals;
        default: return GToms;
    }
}

struct KickSound { const char* name; float sweep, sweepMs, decay, levelDb; };
inline const KickSound& kickSound (int i)
{
    static const KickSound s[] = {
        { "808 Short", 2.2f, 28.0f, 0.45f, 3.0f },
        { "808 Long",  2.0f, 35.0f, 1.20f, 2.0f },
        { "Sub Thump", 3.5f, 15.0f, 0.25f, 3.0f },
        { "Deep Sub",  1.2f, 50.0f, 0.80f, 2.0f } };
    return s[std::clamp (i, 0, 3)];
}
constexpr int kNumKickSounds = 4;

inline float kickNoteHz (int i)   // E1 .. D#2
{
    static const float hz[] = { 41.20f, 43.65f, 46.25f, 49.00f, 51.91f, 55.00f, 58.27f, 61.74f, 65.41f, 69.30f, 73.42f, 77.78f };
    return hz[std::clamp (i, 0, 11)];
}
inline const char* kickNoteName (int i)
{
    static const char* n[] = { "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2", "D#2" };
    return n[std::clamp (i, 0, 11)];
}
constexpr int kNumKickNotes = 12;

enum SnareSoundType { SClap, SBigClap, S909, SSnap, kNumSnareSounds };
inline const char* snareSoundName (int i)
{
    static const char* n[] = { "Clap", "Big Clap", "909 Snare", "Snap" };
    return n[std::clamp (i, 0, kNumSnareSounds - 1)];
}

struct Settings
{
    float close = 1, oh = 1, room = 1, bleed = 1, output = 1;   // 直線の倍率
    std::array<float, NumGroups> vol { 1, 1, 1, 1, 1 };          // 直線
    std::array<float, NumGroups> tune { 0, 0, 0, 0, 0 };         // 半音
    std::array<float, NumGroups> length { 1, 1, 1, 1, 1 };       // 0..1（1 = そのまま）
    float kickBlend = 0; int kickSound = 0; int kickNote = 1;
    float snareBlend = 0; int snareSound = 0; float snareTune = 0;
    float velCurve = 1.25f;                                      // 強さ → 音量（Drummer のループの音から 1.2〜1.5）
    float xover = 100;                                           // キックの低音をサブに任せる所
};

//==============================================================================
// EDM の音の形（裏のスレッドの「合わせ」とオーディオスレッドで同じ式を使う）
struct SubOsc
{
    double phase = 0; float t = 0;
    float sample (double fs, float noteHz, const KickSound& k)
    {
        const float f = noteHz * (1.0f + k.sweep * std::exp (-t * 1000.0f / k.sweepMs));
        const float env = std::min (1.0f, t / 0.0006f) * std::exp (-t * 6.9078f / k.decay);
        const float y = (float) std::sin (phase) * env;
        phase += 2.0 * 3.141592653589793 * f / fs;
        if (phase > 6.283185307179586) phase -= 6.283185307179586;
        t += (float) (1.0 / fs);
        return y;
    }
};

struct SnareOsc
{
    int type = SClap;
    float t = 0, width = 0.35f;
    uint32_t seedL = 1, seedR = 2;
    std::array<float, 4> offs {};
    Biquad bpL, bpR, hpL, hpR, lpN;
    double ph1 = 0, ph2 = 0;
    float tuneRatio = 1;

    static float noise (uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }

    void start (double fs, int soundType, float tuneSemis, uint32_t seed, float jitter1, float jitter2, float jitter3)
    {
        type = std::clamp (soundType, 0, kNumSnareSounds - 1); t = 0; ph1 = ph2 = 0;
        tuneRatio = std::pow (2.0f, tuneSemis / 12.0f);
        seedL = seed | 1u; seedR = (seed * 2654435761u) | 1u;
        const float sp = type == SBigClap ? 0.012f : 0.010f;
        offs = { 0.0f, sp + jitter1, 2 * sp + jitter2, 3.1f * sp + jitter3 };
        const float bpF = (type == SSnap ? 2200.0f : type == SBigClap ? 1200.0f : 1350.0f) * tuneRatio;
        const float bpQ = type == SBigClap ? 0.7f : type == SSnap ? 0.8f : 0.9f;
        bpL.bandpass (fs, bpF, bpQ); bpR = bpL; bpL.reset(); bpR.reset();
        if (type == S909) { hpL.highpass (fs, 1500, 0.7); lpN.lowpass (fs, 9000, 0.7); }
        else hpL.highpass (fs, type == SSnap ? 600.0f : 450.0f, 0.7);
        hpR = hpL; hpL.reset(); hpR.reset(); lpN.reset();
        width = type == SBigClap ? 0.5f : type == S909 ? 0.15f : 0.35f;
    }

    float clapEnv (float tt) const
    {
        float e = 0;
        if (type == SSnap)
        {
            if (tt >= 0) e = std::min (1.0f, tt / 0.0004f) * (0.6f * std::exp (-tt / 0.0025f) + 0.4f * std::exp (-tt / 0.035f));
            return e;
        }
        for (int k = 0; k < 3; ++k)
        {
            const float u = tt - offs[(size_t) k];
            if (u >= 0) e += std::min (1.0f, u / 0.0004f) * std::exp (-u / 0.0035f);
        }
        const float u = tt - offs[3];
        if (u >= 0)
        {
            if (type == SBigClap) e += std::min (1.0f, u / 0.0006f) * (0.4f * std::exp (-u / 0.005f) + 0.6f * std::exp (-u / 0.13f));
            else e += std::min (1.0f, u / 0.0006f) * (0.55f * std::exp (-u / 0.004f) + 0.45f * std::exp (-u / 0.075f));
        }
        return e;
    }

    void sample (double fs, float& l, float& r)
    {
        const float soft = std::min (1.0f, t / 0.002f);   // 頭を 2 ms 丸めて生のアタックを先に
        const float nm = noise (seedL), ns = noise (seedR);
        if (type == S909)
        {
            const float drop = 1.0f + 0.15f * std::exp (-t / 0.01f);
            const float tone = 0.6f * (float) std::sin (ph1) * std::exp (-t / 0.045f) + 0.4f * (float) std::sin (ph2) * std::exp (-t / 0.03f);
            ph1 += 2.0 * 3.141592653589793 * 185.0 * tuneRatio * drop / fs;
            ph2 += 2.0 * 3.141592653589793 * 330.0 * tuneRatio * drop / fs;
            const float nenv = std::min (1.0f, t / 0.0005f) * std::exp (-t / 0.07f);
            const float nl = lpN.run (hpL.run (nm * (1 - width) + ns * width)) * nenv;
            const float nr = hpR.run (nm * (1 - width) - ns * width) * nenv;
            l = (0.55f * tone + 0.45f * nl) * soft;
            r = (0.55f * tone + 0.45f * nr) * soft;
        }
        else
        {
            const float e = clapEnv (t) * soft;
            const float w = std::min (1.0f, t / 0.04f) * width;   // 尾だけ少し広げる
            l = hpL.run (bpL.run (nm * (1 - w) + ns * w)) * e;
            r = hpR.run (bpR.run (nm * (1 - w) - ns * w)) * e;
        }
        t += (float) (1.0 / fs);
    }
    float length() const { return type == SBigClap ? 0.9f : type == SSnap ? 0.35f : 0.6f; }
};

//==============================================================================
// Drummer・GM のノート → 部品・奏法・鍵盤（候補を順番に使う＝同じ録音の連続を避ける）
struct NoteTarget
{
    int8_t piece = -1;
    int8_t kind = 0;             // articKind（0=all 1=stop 2=foot 3=splash）
    int8_t hhOpen = 0;           // ハイハット: 1=閉じ〜7=全開。0 = CC4 に従う（スティック）
    bool hh = false;
    int8_t chokePiece = -1;      // このノートで止める部品（シンバルを手でつかむ）
    int8_t numKeys = 0;
    std::array<int8_t, 6> keys {};
    bool edm = false;            // スネアの EDM の層を付ける（センター・リムだけ。サイドスティック・エッジには付けない）
};

inline std::array<NoteTarget, 128> buildNoteMap (const Kit& kit)
{
    std::array<NoteTarget, 128> m {};
    auto set = [&] (int note, int piece, std::initializer_list<int> keys, int kind = 0)
    {
        NoteTarget t; t.piece = (int8_t) piece; t.kind = (int8_t) kind;
        for (int k : keys) if (t.numKeys < 6) t.keys[(size_t) t.numKeys++] = (int8_t) k;
        m[(size_t) note] = t;
    };
    auto tom = [&] (int want, int f1, int f2) { return kit.present[(size_t) want] ? want : kit.present[(size_t) f1] ? f1 : f2; };
    set (35, Kick, { 36 });
    set (36, Kick, { 36 });
    set (38, Snare, { 38, 14, 86 }); m[38].edm = true;
    set (40, Snare, { 40 });         m[40].edm = true;
    set (37, Snare, { 37 });
    set (34, Snare, { 34, 10, 82 });
    set (32, Snare, { 32 });
    set (41, tom (TomLo, TomMidLo, TomMidHi), { 43, 19, 91 });
    set (43, tom (TomLo, TomMidLo, TomMidHi), { 43, 19, 91 });
    set (45, tom (TomMidLo, TomLo, TomMidHi), { 45, 21, 93, 43, 19, 91 });
    set (47, tom (TomMidHi, TomMidLo, TomHi), { 47, 23, 95 });
    set (48, tom (TomHi, TomMidHi, TomMidLo), { 48, 24, 96 });
    set (50, tom (TomHi, TomMidHi, TomMidLo), { 48, 24, 96 });
    set (49, CrashL, { 49, 97 });
    set (55, CrashL, { 49, 97 });
    set (57, CrashR, { 57, 105 });
    set (51, Ride, { 51, 99 });
    set (53, Ride, { 53, 101 });
    set (59, Ride, { 59, 107 });
    set (52, Ride, { 52, 100 });
    set (28, CrashL, { 28 }, 1); m[28].chokePiece = CrashL;
    set (29, CrashR, { 29 }, 1); m[29].chokePiece = CrashR;
    // ハイハット（Drummer は 42 = チップ、44 = スティックの肩で叩く音として使っている。2026-11-12 のループの音で確認）
    set (42, HiHat, { 42, 18, 90 }); m[42].hh = true;
    set (44, HiHat, { 44, 92 });     m[44].hh = true; m[44].hhOpen = 1;
    set (46, HiHat, { 46, 94, 22 }); m[46].hh = true; m[46].hhOpen = 6;
    set (33, HiHat, { 33 }, 2);      m[33].hh = true;
    set (31, HiHat, { 31 }, 3);      m[31].hh = true;
    return m;
}

//==============================================================================
// EDM の層の合わせ（裏のスレッド）: キックの層ごとにサブの遅れ・極性・大きさ、スネアの層ごとに EDM の大きさ
inline void analyzeEdm (Kit& kit, const Settings& s, const std::atomic<bool>* cancel = nullptr)
{
    if (kit.fx == nullptr) return;
    const double fs = kit.sampleRate;
    const KickSound& ks = kickSound (s.kickSound);
    const float noteHz = kickNoteHz (s.kickNote);
    const int n = (int) (0.25 * fs);

    // サブの形（低域だけ）
    std::vector<float> sub ((size_t) n);
    { SubOsc o; LR4 lp; lp.setup (fs, s.xover); for (int i = 0; i < n; ++i) { float lo, hi; lp.run (o.sample (fs, noteHz, ks), lo, hi); sub[(size_t) i] = lo; } }
    double eSub = 0; for (float v : sub) eSub += (double) v * v;

    std::vector<float> ac;
    for (auto& a : kit.artics)
    {
        if (a.piece != Kick) continue;
        for (auto& L : a.layers)
        {
            if (cancel != nullptr && cancel->load()) return;
            // 生キックの全マイクを既定のバランスでモノにまとめ、低域だけにする
            ac.assign ((size_t) (L.onset + n + 200), 0.0f);
            for (int m = 0; m < NumMics; ++m)
            {
                if (! L.has[(size_t) m]) continue;
                const MicPart& p = L.mic[(size_t) m];
                for (size_t i = 0; i < ac.size(); ++i)
                {
                    float l, r;
                    if (! p.fetch (i, l, r)) break;
                    ac[i] += 0.5f * (l + r) * p.gain;
                }
            }
            LR4 lp; lp.setup (fs, s.xover);
            for (auto& x : ac) { float lo, hi; lp.run (x, lo, hi); x = lo; }
            const int on = L.onset, maxLag = (int) (0.003 * fs);
            double best = 0; int bestLag = 0;
            for (int lag = -maxLag; lag <= maxLag; ++lag)
            {
                double c = 0;
                for (int i = 0; i < n; ++i)
                {
                    const int j = on + lag + i;
                    if (j >= 0 && j < (int) ac.size()) c += (double) ac[(size_t) j] * sub[(size_t) i];
                }
                if (std::abs (c) > std::abs (best)) { best = c; bestLag = lag; }
            }
            double eAc = 0;
            for (int i = 0; i < n; ++i) { const int j = on + i; if (j < (int) ac.size()) eAc += (double) ac[(size_t) j] * ac[(size_t) j]; }
            const float g = eSub > 0 ? (float) std::sqrt (eAc / eSub) : 0.0f;
            LayerFx& f = kit.fx[(size_t) L.fx];
            f.subDelay.store ((float) std::max (0, on + bestLag));
            f.subGain.store (best < 0 ? -g : g);
        }
    }

    // スネア: 生の全マイクと EDM の音を、同じ帯域（クラップ系 400 Hz〜8 kHz、909 は 150 Hz〜8 kHz）・鳴り始めから 150 ms で比べる
    const int m150 = (int) (0.15 * fs);
    const float bandLo = s.snareSound == S909 ? 150.0f : 400.0f;
    double eEdm = 0;
    {
        SnareOsc o; o.start (fs, s.snareSound, s.snareTune, 7u, 0, 0, 0);
        Biquad h, l; h.highpass (fs, bandLo, 0.707); l.lowpass (fs, 8000, 0.707);
        for (int i = 0; i < m150; ++i) { float a, b; o.sample (fs, a, b); const float y = l.run (h.run (0.5f * (a + b))); eEdm += (double) y * y; }
    }
    for (auto& a : kit.artics)
    {
        if (a.piece != Snare || articKind (a.name) != 0) continue;
        for (auto& L : a.layers)
        {
            if (cancel != nullptr && cancel->load()) return;
            Biquad h, l; h.highpass (fs, bandLo, 0.707); l.lowpass (fs, 8000, 0.707);
            double e = 0;
            for (int i = 0; i < L.onset + m150; ++i)
            {
                float sm = 0;
                for (int m = 0; m < NumMics; ++m)
                {
                    if (! L.has[(size_t) m]) continue;
                    float lv, rv;
                    if (L.mic[(size_t) m].fetch ((uint64_t) i, lv, rv)) sm += 0.5f * (lv + rv) * L.mic[(size_t) m].gain;
                }
                const float y = l.run (h.run (sm));
                if (i >= L.onset) e += (double) y * y;
            }
            kit.fx[(size_t) L.fx].edmGain.store (eEdm > 0 ? (float) std::sqrt (e / eEdm) : 0.0f);
        }
    }
}

//==============================================================================
// 時間で一定の速さで目標へ近づける（ブロックの区切り方に関係なく 15 ms）
struct Smoother
{
    float cur = 0, target = 0, step = 0;
    int left = 0;
    void snap (float v) { cur = target = v; left = 0; step = 0; }
    void setTarget (float t, int rampSamples)
    {
        if (t == target) return;
        target = t; left = std::max (1, rampSamples); step = (target - cur) / (float) left;
    }
    float next()
    {
        if (left > 0) { cur += step; if (--left == 0) cur = target; }
        return cur;
    }
};

class Engine
{
public:
    static constexpr int maxBlock = 512;
    // 出口（並びは保存した曲・Logic の Multi Output の番号になるので変えない。Logic の「＋」は番号順に 1 本ずつ作るので、
    // よく分ける Kick・Snare・Room を前に。ミックスエンジニア係 2026-11-12）
    enum OutIndex { OutMain, OutKick, OutSnare, OutRoom, OutToms, OutHiHat, OutCymbals, maxOuts };
    static constexpr float outputTrim = 0.63095734f;   // −4 dB（全部の部品を 127 で同時に叩いても 0 dBFS を越えにくく。DSP 検査係 2026-11-12）
    std::array<std::atomic<float>, NumPieces> hitFlash {};   // 画面の光（叩いた強さ 0..1）

    Engine()
    {
        noteMap = buildNoteMap (emptyKit());   // キットがなくても EDM の層は鳴らす（空のキットの用意はここ＝メッセージスレッドで）
    }

    void prepare (double internalRate)
    {
        fs = internalRate;
        hardReset();
        setupSplits (next.xover);
        first = true;
    }

    // すぐ全部止める（prepare・ホストのリセット）
    void hardReset()
    {
        for (auto& v : voices) v.active = false;
        for (auto& b : bus) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : busRoom) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& k : kickSplit) k.reset();
        for (auto& k : kickSplitRoom) k.reset();
        subLp.reset();
        for (int p = 0; p < NumPieces; ++p)   // 出口は今の割り当てのまま始める（Main から移すと、リセット直後の一打の頭が Main に漏れる。DSP 検査係）
        {
            rDirect[(size_t) p] = { wantDirect[(size_t) p], wantDirect[(size_t) p], 0 };
            rRoom[(size_t) p] = { wantRoom[(size_t) p], wantRoom[(size_t) p], 0 };
        }
        subs[0].active = subs[1].active = false;
        for (auto& c : snares) c.active = false;
    }

    void setPrefetcher (Prefetcher* p) { prefetcher = p; }

    // 新しいキット（オーディオスレッドから）。前のキットの音は鳴り終わるまで鳴らす（前のキットは 8 個まで並べて待つ）
    void setKit (const Kit* k)
    {
        if (k == kit) return;
        for (auto& o : oldKits) if (o == k) o = nullptr;   // 並んでいたものに戻した
        if (kit != nullptr)
        {
            auto slot = std::find (oldKits.begin(), oldKits.end(), nullptr);
            if (slot == oldKits.end())
            {
                // 列が満杯（とても速く何度も替えた）だけ: いちばん前のキットの音を止めて、渡す側の列へ移す
                const Kit* victim = oldKits[0];
                for (auto& v : voices) if (v.active && v.kit == victim) v.active = false;
                auto h = std::find (handout.begin(), handout.end(), nullptr);
                if (h != handout.end()) *h = victim;   // 渡す側も満杯なら（まずない）消さずに残す。落ちるよりよい
                std::rotate (oldKits.begin(), oldKits.begin() + 1, oldKits.end());
                oldKits.back() = nullptr;
                slot = oldKits.end() - 1;
            }
            *slot = kit;
        }
        kit = k;
        noteMap = buildNoteMap (kit != nullptr ? *kit : emptyKit());
        rr = {};
        lastArtic = {};
    }
    const Kit* currentKit() const { return kit; }
    // 前のキットのうち、もう鳴っていないものを 1 つ返す（呼んだ側が後で消す）
    const Kit* takeRetiredKit()
    {
        for (auto& h : handout) if (h != nullptr) { const Kit* k = h; h = nullptr; return k; }
        for (auto& o : oldKits)
        {
            if (o == nullptr) continue;
            bool used = false;
            for (auto& v : voices) if (v.active && v.kit == o) { used = true; break; }
            if (! used) { const Kit* k = o; o = nullptr; return k; }
        }
        return nullptr;
    }

    void setSettings (const Settings& s) { next = s; }

    // 部品ごとの出口（オーディオスレッドから毎ブロック）。direct = 近い・OH・かぶり、room = ルーム 2 本。
    // 変わったら 15 ms で前の出口から移す（プチッとさせない）。出口の数より大きい番号は Main に出す
    void setRouting (const std::array<int, NumPieces>& direct, const std::array<int, NumPieces>& room) { wantDirect = direct; wantRoom = room; }

    void allSoundOff()
    {
        for (auto& v : voices) if (v.active) v.fadeStep = 1.0f / (float) (0.01 * fs);
        for (auto& s : subs) if (s.active) s.fadeStep = 1.0f / (float) (0.01 * fs);
        for (auto& c : snares) if (c.active) c.fadeStep = 1.0f / (float) (0.01 * fs);
    }

    void controller (int cc, int value)
    {
        if (cc == 4) hhPedal = std::clamp (value, 0, 127);
        else if (cc == 120) allSoundOff();
    }

    void noteOn (int note, int vel)
    {
        if (note < 0 || note > 127 || vel <= 0) return;
        applySettingsNow();
        const NoteTarget& t = noteMap[(size_t) note];
        if (t.piece < 0) return;
        if (t.chokePiece >= 0) choke (t.chokePiece, -1, 0.04f);
        const float amp = std::pow (vel / 127.0f, cur.velCurve);
        hitFlash[(size_t) t.piece].store (std::max (0.15f, vel / 127.0f));

        if (kit == nullptr)   // 録音がないときも EDM の層だけは鳴らす
        {
            if (t.piece == Kick && cur.kickBlend > 0) startSub (0.0f, 0.25f, amp);
            if (t.piece == Snare && t.edm && cur.snareBlend > 0) startSnare (0.0f, 0.2f, amp, vel);
            return;
        }

        int open = 0, kind = t.kind;
        if (t.hh)
        {
            if (t.kind == 2 || t.kind == 3)
            {
                choke (HiHat, 1, 0.025f);
                open = t.kind == 2 ? 1 : 7;   // スプラッシュは開いて鳴る（次に閉じたら止まる）
            }
            else
            {
                open = t.hhOpen > 0 ? t.hhOpen : 1 + (hhPedal * 6 + 63) / 127;
                kind = 3 + open;               // o1 = 4
                choke (HiHat, open, open <= 2 ? 0.025f : 0.06f);
            }
        }

        const Artic* a = nullptr;
        for (int tries = 0; tries < t.numKeys && a == nullptr; ++tries)
        {
            const int key = t.keys[(size_t) ((rr[(size_t) note]++) % (uint32_t) t.numKeys)];
            a = kit->lookup (t.piece, key, kind);
        }
        if (a == nullptr || a->layers.empty()) return;

        int li = a->layerFor (vel);
        if (li < 0) return;
        // となりの層も少し使う・同じ録音を 2 回続けない（マシンガンを避ける）
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        if (a->layers.size() > 2 && u (rng) < 0.35f)
        {
            const int alt = li + (u (rng) < 0.5f ? -1 : 1);
            if (alt >= 0 && alt < (int) a->layers.size() && std::abs (a->layers[(size_t) alt].velLo - vel) < 14) li = alt;
        }
        if (lastArtic[(size_t) t.piece] == a && lastLayer[(size_t) t.piece] == li && a->layers.size() > 1)
            li = li > 0 ? li - 1 : li + 1;
        lastArtic[(size_t) t.piece] = a; lastLayer[(size_t) t.piece] = li;
        const Layer& L = a->layers[(size_t) li];

        const int g = groupOf (t.piece);
        const double ratio = std::pow (2.0, cur.tune[(size_t) g] / 12.0);
        startVoice (t.piece, L, amp, open, ratio);

        const LayerFx& f = kit->fx[(size_t) L.fx];
        if (t.piece == Kick && cur.kickBlend > 0.0f)
            startSub (f.subDelay.load() / (float) ratio, f.subGain.load(), amp);
        if (t.piece == Snare && t.edm && cur.snareBlend > 0.0f)
            startSnare ((float) L.onset / (float) ratio + (float) (0.0015 * fs), f.edmGain.load(), amp, vel);
    }

    // n サンプルぶん（中のサンプルレート）作る。n ≤ maxBlock。outs は numOuts 組のステレオ（L0, R0, L1, R1, …）。中は 0 から作る
    void render (float* const* outs, int numOuts, int n)
    {
        n = std::max (0, std::min (n, maxBlock));
        numOuts = std::clamp (numOuts, 1, (int) maxOuts);
        applySettingsNow();
        cur = next;
        const int ramp = (int) (0.015 * fs);
        if (std::abs (cur.xover - xoverNow) > 0.01f)
        {
            xoverNow = cur.xover;
            setupSplits (xoverNow);
        }
        // つまみは時間で一定の速さ（15 ms）で動かす（ブロックがノートで細かく切られても途切れない）
        setTargets (ramp);
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < kNumSmooth; ++k) ramps[(size_t) k][(size_t) i] = sm[(size_t) k].next();
        for (int p = 0; p < NumPieces; ++p)
        {
            updateRoute (rDirect[(size_t) p], wantDirect[(size_t) p], wDirect[(size_t) p].data(), n);
            updateRoute (rRoom[(size_t) p], wantRoom[(size_t) p], wRoom[(size_t) p].data(), n);
        }

        for (int p = 0; p < NumPieces; ++p)
        {
            std::fill (bus[(size_t) p].begin(), bus[(size_t) p].begin() + 2 * n, 0.0f);
            std::fill (busRoom[(size_t) p].begin(), busRoom[(size_t) p].begin() + 2 * n, 0.0f);
        }
        for (auto& v : voices)
            if (v.active) renderVoice (v, n);

        for (int k = 0; k < 2 * numOuts; ++k) std::fill (outs[k], outs[k] + n, 0.0f);
        auto outOf = [numOuts] (int d) { return d >= 0 && d < numOuts ? d : 0; };
        auto mixTo = [&] (const float* b, const Route& r, const float* w)   // b はインターリーブのステレオ
        {
            const int c = outOf (r.cur), q = outOf (r.prev);
            float* cl = outs[2 * c]; float* cr = outs[2 * c + 1];
            float* ql = outs[2 * q]; float* qr = outs[2 * q + 1];
            for (int i = 0; i < n; ++i)
            {
                const float x = b[2 * i], y = b[2 * i + 1], ww = w[i];
                cl[i] += x * ww; cr[i] += y * ww;
                if (ww < 1.0f) { ql[i] += x * (1.0f - ww); qr[i] += y * (1.0f - ww); }
            }
        };
        for (int p = 0; p < NumPieces; ++p)
        {
            float* b = bus[(size_t) p].data();
            float* br = busRoom[(size_t) p].data();
            if (p == Kick)   // 低音をサブに任せる分は、ルームに入ったキックの低音も同じ所で切る
            {
                const float* bl = ramps[sKickBlend].data();
                for (int i = 0; i < n; ++i)
                {
                    const float keepLow = std::cos (std::clamp (bl[i], 0.0f, 1.0f) * kPi * 0.5f);
                    float lo, hi;
                    kickSplit[0].run (b[2 * i], lo, hi);          b[2 * i] = hi + keepLow * lo;
                    kickSplit[1].run (b[2 * i + 1], lo, hi);      b[2 * i + 1] = hi + keepLow * lo;
                    kickSplitRoom[0].run (br[2 * i], lo, hi);     br[2 * i] = hi + keepLow * lo;
                    kickSplitRoom[1].run (br[2 * i + 1], lo, hi); br[2 * i + 1] = hi + keepLow * lo;
                }
            }
            mixTo (b, rDirect[(size_t) p], wDirect[(size_t) p].data());
            mixTo (br, rRoom[(size_t) p], wRoom[(size_t) p].data());
        }
        // EDM の層は、その部品（近いマイクの側）と同じ出口へ
        renderSubs (edmBuf.data(), n);
        mixTo (edmBuf.data(), rDirect[Kick], wDirect[Kick].data());
        renderSnares (edmBuf.data(), n);
        mixTo (edmBuf.data(), rDirect[Snare], wDirect[Snare].data());
        const float* og = ramps[sOutput].data();
        for (int k = 0; k < numOuts; ++k)
        {
            float* l = outs[2 * k]; float* r = outs[2 * k + 1];
            for (int i = 0; i < n; ++i) { const float g = og[i] * outputTrim; l[i] *= g; r[i] *= g; }
        }
    }

    // ステレオ 1 組だけ（試作・テスト用。全部 Main）
    void render (float* outL, float* outR, int n)
    {
        float* o[2] = { outL, outR };
        render (o, 1, n);
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    struct Voice
    {
        bool active = false;
        int piece = 0, group = 0, open = 0;
        const Layer* layer = nullptr;
        const Kit* kit = nullptr;
        float amp = 1;
        double pos = 0, step = 1;
        float fade = 1, fadeStep = 0;
        float t = 0;
        uint64_t age = 0;
        SpliceParams splice;
        std::array<float, NumMics> tailGain {};
    };

    struct SubVoice
    {
        bool active = false;
        SubOsc osc;
        KickSound sound {};            // 鳴らし始めの SOUND・NOTE のまま鳴らす（途中で替えても形が変わらない）
        float noteHz = 43.65f;
        float gain = 0, fade = 1, fadeStep = 0;
        int delay = 0;
    };

    struct SnareVoice
    {
        bool active = false;
        SnareOsc osc;
        float gain = 0, fade = 1, fadeStep = 0;
        int delay = 0;
    };

    // ならすつまみ
    enum { sClose, sOH, sBleed, sRoom, sOutput, sVol0, sLen0 = sVol0 + NumGroups, sKickBlend = sLen0 + NumGroups, sSnareBlend, kNumSmooth };

    double fs = 44100;
    const Kit* kit = nullptr;
    std::array<const Kit*, 8> oldKits {};
    std::array<const Kit*, 8> handout {};   // 音を止めて、すぐ渡すもの
    static const Kit& emptyKit() { static const Kit k {}; return k; }
    Prefetcher* prefetcher = nullptr;
    Settings cur, next;
    bool first = true;
    float xoverNow = 100;
    std::array<Smoother, kNumSmooth> sm {};
    std::array<std::array<float, maxBlock>, kNumSmooth> ramps {};
    std::array<Voice, 128> voices {};
    std::array<std::array<float, 2 * maxBlock>, NumPieces> bus {}, busRoom {};   // 部品ごと: 近い・OH・かぶり / ルーム
    std::array<float, 2 * maxBlock> edmBuf {};
    struct Route { int cur = 0, prev = 0, left = 0; };
    static constexpr int routeFade = 662;   // 出口を替えたら 15 ms で移す
    std::array<Route, NumPieces> rDirect {}, rRoom {};
    std::array<int, NumPieces> wantDirect {}, wantRoom {};
    std::array<std::array<float, maxBlock>, NumPieces> wDirect {}, wRoom {};
    LR4 kickSplitRoom[2];
    std::array<NoteTarget, 128> noteMap {};
    std::array<uint32_t, 128> rr {};
    std::array<const Artic*, NumPieces> lastArtic {};
    std::array<int, NumPieces> lastLayer {};
    std::mt19937 rng { 12345 };
    int hhPedal = 0;
    uint64_t ageCounter = 0;
    LR4 kickSplit[2], subLp;
    SubVoice subs[2];
    std::array<SnareVoice, 6> snares {};

    void setTargets (int ramp)
    {
        const Settings& s = next;
        sm[sClose].setTarget (s.close, ramp); sm[sOH].setTarget (s.oh, ramp);
        sm[sBleed].setTarget (s.bleed, ramp); sm[sRoom].setTarget (s.room, ramp);
        sm[sOutput].setTarget (s.output, ramp);
        for (int g = 0; g < NumGroups; ++g)
        {
            sm[(size_t) (sVol0 + g)].setTarget (s.vol[(size_t) g], ramp);
            sm[(size_t) (sLen0 + g)].setTarget (s.length[(size_t) g], ramp);
        }
        sm[sKickBlend].setTarget (s.kickBlend, ramp);
        sm[sSnareBlend].setTarget (s.snareBlend, ramp);
    }

    void setupSplits (double f)
    {
        for (auto& k : kickSplit) k.setup (fs, f);
        for (auto& k : kickSplitRoom) k.setup (fs, f);
        subLp.setup (fs, f);
    }

    static void updateRoute (Route& r, int want, float* w, int n)
    {
        if (want != r.cur)
        {
            if (r.left == 0) { r.prev = r.cur; r.cur = want; r.left = routeFade; }
            else if (want == r.prev) { std::swap (r.cur, r.prev); r.left = routeFade - r.left; }   // 移している途中で元に戻した: 向きを逆にして続ける
            // 移している途中で別の出口にした: 今の移しが終わってから（次のブロックで）移す（前の前の出口の分を 1 サンプルで消さない）
        }
        for (int i = 0; i < n; ++i)
        {
            if (r.left > 0) { w[i] = 1.0f - (float) r.left / (float) routeFade; --r.left; }
            else w[i] = 1.0f;
        }
    }

    // 最初（prepare の後）は、ならさずにその時のつまみから始める
    void applySettingsNow()
    {
        if (! first) return;
        cur = next; first = false;
        setTargets (1);
        for (auto& x : sm) x.snap (x.target);
        xoverNow = cur.xover;
        setupSplits (xoverNow);
        for (int p = 0; p < NumPieces; ++p)   // 最初の出口は移さずにそこから
        {
            rDirect[(size_t) p] = { wantDirect[(size_t) p], wantDirect[(size_t) p], 0 };
            rRoom[(size_t) p] = { wantRoom[(size_t) p], wantRoom[(size_t) p], 0 };
        }
    }

    static int maxVoices (int piece)
    {
        switch (piece) { case Kick: return 3; case Snare: return 6; case HiHat: return 8; case Ride: return 6; default: return 4; }
    }

    static int micSmooth (int m)
    {
        switch (m)
        {
            case Close: case Close2: return sClose;
            case OH: return sOH;
            case Bleed: return sBleed;
            default: return sRoom;
        }
    }

    void choke (int piece, int aboveOpen, float seconds)
    {
        for (auto& v : voices)
            if (v.active && v.piece == piece && v.open > aboveOpen && v.fadeStep == 0)
                v.fadeStep = 1.0f / (float) std::max (1.0, seconds * fs);
    }

    void startVoice (int piece, const Layer& L, float amp, int open, double ratio)
    {
        int count = 0; Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.piece == piece && v.fadeStep == 0)
            {
                ++count;
                if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            }
        if (count >= maxVoices (piece) && oldest != nullptr) oldest->fadeStep = 1.0f / (float) (0.01 * fs);

        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
            for (auto& v : voices) if (slot == nullptr || v.age < slot->age) slot = &v;
        *slot = {};
        slot->active = true; slot->piece = piece; slot->group = groupOf (piece); slot->open = open;
        slot->layer = &L; slot->kit = kit; slot->amp = amp; slot->step = ratio;
        slot->age = ++ageCounter;
        const LayerFx& f = kit->fx[(size_t) L.fx];
        slot->splice.skip = f.spliceSkip.load (std::memory_order_relaxed);
        slot->splice.trim = f.spliceTrim.load (std::memory_order_relaxed);
        slot->splice.rho = f.spliceRho.load (std::memory_order_relaxed);
        for (int m = 0; m < NumMics; ++m) slot->tailGain[(size_t) m] = f.tailGain[(size_t) m].load (std::memory_order_relaxed);
        if (prefetcher != nullptr)
        {
            for (int m = 0; m < NumMics; ++m) if (L.has[(size_t) m]) L.mic[(size_t) m].prefetchHead (*prefetcher);   // 全マイクの頭を先に
            for (int m = 0; m < NumMics; ++m) if (L.has[(size_t) m]) L.mic[(size_t) m].prefetchTail (*prefetcher);
        }
    }

    // LENGTH: 1 = そのまま。小さいほど早く消す（保持してから指数で）。90 % から上は「そのまま」へ少しずつ寄せる（99 ↔ 100 % で段差を出さない）
    static float lengthEnv (float len, float t)
    {
        if (len >= 0.999f) return 1.0f;
        const float hold = 0.03f + 2.5f * len * len, tau = 0.02f + 0.3f * len;
        const float e = t < hold ? 1.0f : std::exp (-(t - hold) / tau);
        const float w = std::clamp ((len - 0.9f) / 0.1f, 0.0f, 1.0f);
        return e + (1.0f - e) * w * w;
    }

    void renderVoice (Voice& v, int n)
    {
        float* b = bus[(size_t) v.piece].data();
        float* br = busRoom[(size_t) v.piece].data();
        const Layer& L = *v.layer;
        const auto pn = v.kit->pan[(size_t) v.piece];
        const float dt = (float) (1.0 / fs);
        const bool tuned = std::abs (v.step - 1.0) > 1e-6;
        const float* volR = ramps[(size_t) (sVol0 + v.group)].data();
        const float* lenR = ramps[(size_t) (sLen0 + v.group)].data();
        for (int i = 0; i < n; ++i)
        {
            if (v.fadeStep > 0) { v.fade -= v.fadeStep; if (v.fade <= 0) { v.active = false; return; } }
            const float env = lengthEnv (lenR[i], v.t);
            if (env < 1.0e-4f) { v.active = false; return; }
            bool any = false;
            float sl = 0, sr = 0, rl = 0, rr2 = 0;
            for (int m = 0; m < NumMics; ++m)
            {
                if (! L.has[(size_t) m]) continue;
                const MicPart& part = L.mic[(size_t) m];
                const float tg = v.tailGain[(size_t) m];
                float l, r;
                if (! tuned)
                {
                    if (! part.fetch ((uint64_t) v.pos, l, r, v.splice, tg)) continue;
                }
                else
                {
                    // 4 点のエルミート補間（TUNE を動かしたときだけ）
                    const int64_t i0 = (int64_t) v.pos;
                    const float fr = (float) (v.pos - (double) i0);
                    float xl[4], xr[4]; bool ok = false;
                    for (int q = 0; q < 4; ++q)
                    {
                        const int64_t j = i0 - 1 + q;
                        if (j >= 0 && part.fetch ((uint64_t) j, xl[q], xr[q], v.splice, tg)) { if (q == 1) ok = true; }
                        else { xl[q] = 0; xr[q] = 0; }
                    }
                    if (! ok) continue;
                    auto herm = [fr] (const float* x)
                    {
                        const float c1 = 0.5f * (x[2] - x[0]), c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3], c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
                        return ((c3 * fr + c2) * fr + c1) * fr + x[1];
                    };
                    l = herm (xl); r = herm (xr);
                }
                any = true;
                const float g = part.gain * ramps[(size_t) micSmooth (m)][(size_t) i];
                if (m == RoomA || m == RoomB) { rl += l * g; rr2 += r * g; }   // ルーム B はモノ（真ん中）
                else if (part.channels == 1) { sl += l * g * pn[0]; sr += r * g * pn[1]; }
                else { sl += l * g; sr += r * g; }
            }
            if (! any) { v.active = false; return; }
            const float a = v.amp * v.fade * env * volR[i];
            b[2 * i] += sl * a; b[2 * i + 1] += sr * a;
            br[2 * i] += rl * a; br[2 * i + 1] += rr2 * a;
            v.pos += v.step;
            v.t += dt;
        }
    }

    // ---- キックのサブ ----
    void startSub (float delaySamples, float matchedGain, float amp)
    {
        SubVoice* slot = ! subs[0].active ? &subs[0] : (! subs[1].active ? &subs[1] : (subs[0].fade < subs[1].fade ? &subs[0] : &subs[1]));
        for (auto& s : subs)
            if (&s != slot && s.active && s.fadeStep == 0) s.fadeStep = 1.0f / (float) (0.005 * fs);   // 同時に 1 本
        *slot = {};
        slot->active = true;
        slot->delay = std::max (0, (int) std::lround (delaySamples));
        slot->sound = kickSound (cur.kickSound);
        slot->noteHz = kickNoteHz (cur.kickNote);
        slot->gain = matchedGain * amp * std::pow (10.0f, slot->sound.levelDb / 20.0f);
    }

    void renderSubs (float* dst, int n)   // dst はインターリーブのステレオ（0 から作る）
    {
        const float* bl = ramps[sKickBlend].data();
        const float* vol = ramps[(size_t) (sVol0 + GKick)].data();
        for (int i = 0; i < n; ++i)
        {
            const float take = std::sin (std::clamp (bl[i], 0.0f, 1.0f) * kPi * 0.5f) * vol[i];
            float x = 0;
            for (auto& s : subs)
            {
                if (! s.active) continue;
                if (s.delay > 0) { --s.delay; continue; }
                x += s.osc.sample (fs, s.noteHz, s.sound) * s.gain * s.fade;
                if (s.fadeStep > 0) { s.fade -= s.fadeStep; if (s.fade <= 0) s.active = false; }
                if (s.osc.t > s.sound.decay * 1.3f) s.active = false;
            }
            float lo, hi; subLp.run (x * take, lo, hi);
            dst[2 * i] = lo; dst[2 * i + 1] = lo;
        }
    }

    // ---- スネアの EDM の層 ----
    void startSnare (float delaySamples, float matchedGain, float amp, int vel)
    {
        const float vf = std::clamp ((vel - 45.0f) / 25.0f, 0.0f, 1.0f);   // 弱い音（ゴースト）では鳴らさない
        if (vf <= 0) return;
        SnareVoice* slot = nullptr;
        for (auto& c : snares) if (! c.active) { slot = &c; break; }
        if (slot == nullptr) { slot = &snares[0]; for (auto& c : snares) if (c.osc.t > slot->osc.t) slot = &c; }
        *slot = {};
        slot->active = true;
        slot->delay = std::max (0, (int) std::lround (delaySamples));
        std::uniform_real_distribution<float> j (-0.0012f, 0.0012f);
        slot->osc.start (fs, cur.snareSound, cur.snareTune, (uint32_t) rng(), j (rng), j (rng), j (rng));
        slot->gain = matchedGain * amp * vf;
    }

    void renderSnares (float* dst, int n)   // dst はインターリーブのステレオ（0 から作る）
    {
        std::fill (dst, dst + 2 * n, 0.0f);
        const float* blr = ramps[sSnareBlend].data();
        const float* vol = ramps[(size_t) (sVol0 + GSnare)].data();
        for (auto& c : snares)
        {
            if (! c.active) continue;
            for (int i = 0; i < n; ++i)
            {
                if (c.delay > 0) { --c.delay; continue; }
                float l, r; c.osc.sample (fs, l, r);
                const float g = c.gain * std::clamp (blr[i], 0.0f, 1.0f) * vol[i] * c.fade;
                dst[2 * i] += l * g; dst[2 * i + 1] += r * g;
                if (c.fadeStep > 0) { c.fade -= c.fadeStep; if (c.fade <= 0) { c.active = false; break; } }
                if (c.osc.t > c.osc.length()) { c.active = false; break; }
            }
        }
    }
};
} // namespace hk
