// Mod Host の変調エンジン（JUCE なし。tools/test_mod.cpp で数値テスト）
// 1 つの「モジュレーター」が、短い区切り（ふつう 64 サンプル）ごとに 1 つの値を出す。
//   片側（UNI）: 0〜1、両側（BI）: -1〜+1
// その値 × AMOUNT を、中のプラグインのつまみの「元の位置」に足す（applyOffset）。
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mh
{
constexpr int numMods = 4;      // MOD 1〜4
constexpr int numTargets = 4;   // 1 つの MOD からつなげる先
constexpr int maxSteps = 16;
constexpr int chunk = 64;       // この長さごとに値を作り、中のプラグインのつまみを動かす

enum Type  { typeLfo = 0, typeEnv, typeSidechain, typeRandom, typeSteps, numTypes };
enum Shape { shapeSine = 0, shapeTriangle, shapeSawUp, shapeSawDown, shapeSquare, numShapes };

// テンポ同期の長さ（4 分音符いくつ分か。小節は拍子から）
constexpr int numDivs = 13;
inline const char* divName (int i)
{
    static const char* n[numDivs] = { "1/32", "1/16", "1/8T", "1/8", "1/4T", "1/8.", "1/4", "1/4.", "1/2",
                                      "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
    return n[std::clamp (i, 0, numDivs - 1)];
}
// barQuarters = 1 小節が 4 分音符いくつ分か（4/4 なら 4）
inline double divQuarters (int i, double barQuarters)
{
    static const double q[numDivs] = { 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, -1, -2, -4, -8 };
    const double v = q[std::clamp (i, 0, numDivs - 1)];
    return v > 0 ? v : -v * std::max (0.5, barQuarters);
}

struct Settings
{
    int type = typeLfo;
    int shape = shapeSine;
    bool sync = true;
    int div = 6;              // 1/4
    float rateHz = 1.0f;      // SYNC が OFF のとき
    float attackMs = 5.0f;    // ENV / SIDECHAIN
    float releaseMs = 150.0f;
    float gainDb = 0.0f;      // ENV / SIDECHAIN の感度（大きいほど小さな音でも振れる）
    float smooth = 0.0f;      // RANDOM / STEPS のなめらかさ 0〜1
    int steps = 8;            // STEPS の数
    bool bipolar = true;      // true = -1〜+1、false = 0〜1
    float phase = 0.0f;       // LFO の始まりの位置 0〜1
};

// 区切りごとに渡す、そのときの様子
struct Block
{
    int numSamples = chunk;
    double sampleRate = 48000.0;
    bool playing = false;     // 再生中で、拍の位置が分かる
    double ppq = 0.0;         // この区切りの頭の位置（4 分音符の数）
    double bpm = 120.0;
    double barQuarters = 4.0;
    float inputPeak = 0.0f;     // この区切りのメイン入力の最大値（直線）
    float sidechainPeak = 0.0f; // サイドチェーンの最大値
};

inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
// -1〜+1 の決まった乱数（同じ番号なら毎回同じ＝再生するたびに同じ動き）
inline float hashToBipolar (uint32_t x) { return (float) (hash32 (x) >> 8) / 8388607.5f - 1.0f; }

inline float frac (double x) { return (float) (x - std::floor (x)); }

inline float lfoShape (int shape, float ph)
{
    constexpr float twoPi = 6.283185307f;
    switch (shape)
    {
        case shapeTriangle: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case shapeSawUp:    return 2.0f * ph - 1.0f;
        case shapeSawDown:  return 1.0f - 2.0f * ph;
        case shapeSquare:   return ph < 0.5f ? 1.0f : -1.0f;
        default:            return std::sin (twoPi * ph);
    }
}

// つまみの元の位置（0〜1）に変調を足して 0〜1 に収める
inline float applyOffset (float base, float offset) { return std::clamp (base + offset, 0.0f, 1.0f); }

class Modulator
{
public:
    void setSeed (uint32_t s) { seed = s; }
    void reset()
    {
        freePhase = 0.0; env = 0.0f; out = 0.0f; lastIndex = INT64_MIN; target = 0.0f; freeCount = 0;
    }

    // steps[] = 0〜1 の値（maxSteps 個）。戻り値 = この区切りの値（UNI 0〜1 / BI -1〜1）
    float process (const Settings& s, const Block& b, const float* steps)
    {
        const double sr = std::max (1000.0, b.sampleRate);
        const double dt = b.numSamples / sr;
        const double beatsPerSec = std::max (1.0, b.bpm) / 60.0;

        // 1 周（RANDOM / STEPS は 1 段）の長さ。拍で表す（sync）か秒で表す
        const double cycleQ = divQuarters (s.div, b.barQuarters);
        const double cycleSec = s.sync ? cycleQ / beatsPerSec : 1.0 / std::max (0.001f, s.rateHz);

        float v = 0.0f;        // 両側（-1〜1）で作る。ENV 系だけは片側で作って最後に揃える
        bool unipolarNative = false;

        switch (s.type)
        {
            case typeLfo:
            {
                double ph;
                if (s.sync && b.playing) ph = b.ppq / cycleQ;   // 曲の位置にぴったり合わせる
                else { freePhase += dt / cycleSec; freePhase -= std::floor (freePhase); ph = freePhase; }
                v = lfoShape (s.shape, frac (ph + s.phase));
                out = v;
                break;
            }
            case typeEnv:
            case typeSidechain:
            {
                const float x = s.type == typeEnv ? b.inputPeak : b.sidechainPeak;
                const float tau = (x > env ? s.attackMs : s.releaseMs) * 0.001f;
                const float a = 1.0f - std::exp (-(float) dt / std::max (1.0e-4f, tau));
                env += (x - env) * a;
                const float db = 20.0f * std::log10 (std::max (1.0e-6f, env)) + s.gainDb;
                out = std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f);   // -48 dB → 0、0 dB → 1
                unipolarNative = true;
                break;
            }
            case typeRandom:
            case typeSteps:
            {
                const int n = std::clamp (s.steps, 1, maxSteps);
                int64_t idx;
                if (s.sync && b.playing) idx = (int64_t) std::floor (b.ppq / cycleQ + 1.0e-9);
                else
                {
                    freePhase += dt / cycleSec;
                    if (freePhase >= 1.0) { freePhase -= std::floor (freePhase); ++freeCount; }
                    idx = freeCount;
                }
                if (idx != lastIndex)
                {
                    lastIndex = idx;
                    if (s.type == typeRandom) target = hashToBipolar (seed * 0x9E3779B9u + (uint32_t) idx);
                    else
                    {
                        const int k = (int) (((idx % n) + n) % n);
                        target = std::clamp (steps[k], 0.0f, 1.0f) * 2.0f - 1.0f;
                    }
                }
                // なめらかさ: 0 = 段のまま（ただ 1 ms でカチッを防ぐ）、1 = 1 段かけて移る
                const double glide = std::max (0.001, (double) s.smooth * cycleSec / 3.0);
                const float a = 1.0f - (float) std::exp (-dt / glide);
                out += (target - out) * a;
                v = out;
                break;
            }
            default: break;
        }

        if (unipolarNative) return s.bipolar ? out * 2.0f - 1.0f : out;
        return s.bipolar ? v : 0.5f * (v + 1.0f);
    }

private:
    uint32_t seed = 1;
    double freePhase = 0.0;
    int64_t freeCount = 0;
    float env = 0.0f, out = 0.0f, target = 0.0f;
    int64_t lastIndex = INT64_MIN;
};
} // namespace mh
