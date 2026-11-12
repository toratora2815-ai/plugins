// Elder Verb のエンジン（JUCE なし。テスト用の書き出しツールからも使う）
// つまみの値はすべて 0..1 で受け取り、ここで秒・Hz などに直す。
//
// 流れ（左右それぞれ）:
//   プリディレイ → 入口の拡散（オールパス 4 段）→ 粒のかたまり（初期の跳ね返り）
//                                               └→ 4 つの枝の輪（ループ）→ 響き
//   かたまりとループの響きを足し、外の HIGH CUT / LOW CUT と音色を通して、ドライと混ぜる
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "VintageFilter.h"

namespace elder
{
static constexpr float kPi = 3.14159265358979f;
static constexpr int kNumModes = 22;
static constexpr int kBranches = 4;
static constexpr int kTaps = 24;

// ---------------------------------------------------------------- つまみ（0..1）
struct Params
{
    float mix = 1.0f, predelay = 0.0f, decay = 0.42f, size = 1.0f, attack = 0.5f;
    float bassMult = 0.623f, bassXover = 0.381f, highShelf = 0.0f, highFreq = 0.5f;
    float earlyDiff = 1.0f, lateDiff = 1.0f, modRate = 0.245f, modDepth = 0.38f;
    float highCut = 0.59f, lowCut = 0.0f;
    int mode = 0;      // 0..21
    int color = 0;     // 0 = 70s, 1 = 80s, 2 = NOW
};

// ---------------------------------------------------------------- 値の変換（測って合わせた式）
struct Maps
{
    static float decaySec (float v)   { return 0.2f + 69.8f * std::pow (v, 3.33f); }
    static float predelayMs (float v) { return 500.0f * v * v; }
    static float bassMult (float v)   { return std::pow (4.0f, 2.0f * v - 1.0f); }
    static float table5 (const float* t, float v, bool logScale)   // 0 / 0.25 / 0.5 / 0.75 / 1 の 5 点の間をつなぐ
    {
        v = std::clamp (v, 0.0f, 1.0f) * 4.0f; const int i = std::min (3, (int) v); const float f = v - (float) i;
        return logScale ? t[i] * std::pow (t[i + 1] / t[i], f) : t[i] + (t[i + 1] - t[i]) * f;
    }
    static float bassXoverHz (float v){ static const float t[5] = { 96, 156, 765, 4163, 18406 }; return table5 (t, v, true); }
    static float highShelfDb (float v){ return 20.0f * std::log10 (0.229f + 0.771f * v * v * v * v); }   // ループ 1 周あたり（0 〜 -12.8 dB）
    static float highFreqHz (float v) { static const float t[5] = { 101, 1723, 5145, 9554, 13697 }; return table5 (t, v, false); }
    static float highCutHz (float v)  // 0.05 きざみの表（本物の 1 次ローパスに当てはめた値）。1.0 で切らない
    {
        static const float t[21] = { 102.5f, 207.0f, 458.2f, 832.5f, 1327.6f, 1948.3f, 2673.8f, 3522.9f, 4504.1f, 5637.1f, 6941.8f,
                                     8449.6f, 10202.2f, 11502.8f, 13549.0f, 14852.3f, 17040.5f, 18261.4f, 20363.4f, 21558.7f, 0.0f };
        v = std::clamp (v, 0.0f, 1.0f) * 20.0f; const int i = std::min (19, (int) v); const float f = v - (float) i;
        if (i == 19) return t[19] * std::pow (2.0f, f * 2.0f);   // 最後は 22 kHz より上へ逃がす
        return t[i] + (t[i + 1] - t[i]) * f;
    }
    static float lowCutHz (float v)   { return 1343.0f * std::pow (std::max (v, 0.0f), 0.93f); }   // 0 で切らない
    static float modRateHz (float v)  { return 5.0f * (0.044f + 0.956f * v); }               // 0.22 〜 5 Hz
    static float modDepthAmt (float v){ const float e = std::max (0.0f, v - 0.75f); return v + 4.5f * e * e; }   // 最後の 1/4 で急に深く
    static float sizeFactor (float v, float minF) { return minF + (1.0f - minF) * v; }
};

// ---------------------------------------------------------------- モードごとの性格
struct Profile
{
    int kind = 0;               // 0 = 響きがぐるぐる回る形, 1 = ゲート（決まった長さで切れる）
    float loopMs = 340.0f;      // SIZE 100% のときのループ 1 周の長さ
    float sizeMin = 0.15f;      // SIZE 0% のときの縮み（長さに掛ける）
    float inDiffMs = 19.0f;     // 入口の拡散 4 段の合計の長さ
    float inDiffAmt = 0.72f;    // EARLY DIFFUSION 100% のときの拡散の強さ
    float lateAmt = 0.55f;      // LATE DIFFUSION 100% のときの拡散の強さ
    float rtScale = 1.0f;       // DECAY に掛ける倍率
    float gainDb = 0.0f;        // 響き全体の音量
    float earlyMs = 9.0f;       // 同じ側の最初の音（SIZE 100%）
    float crossMs = 41.0f;      // 反対側の最初の音（SIZE 100%）
    float crossDb = -3.0f;      // 反対側へ回る量
    float clusterMs = 100.0f;   // 最初の粒のかたまりの長さ
    float clusterDecayDb = 20;  // かたまりの中での減り方
    float clusterDb = 4.0f;     // かたまりの大きさ（ループの響きに対して）
    float clusterN = 24.0f;     // かたまりの粒の数（1〜24）
    float tankMs = 100.0f;      // ループの響きが聞こえ始めるまで
    float attackMs = 50.0f;     // ATTACK 100% で始まりが遅れる長さ
    float hsScale = 1.0f;       // HIGH SHELF の効き
    float modMs = 0.6f;         // MOD DEPTH 100% のときの揺れ幅（ms）
    float modStyle = 0.0f;      // 0 = COLOR で決まる, 1 = ランダム, 2 = サイン, 3 = コーラス（強い）
    float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;   // 響きの音色（250 Hz 以下 / 1.5 kHz / 5 kHz 以上）
    float dampHz = 0.0f;        // ループの中の高音の自然な減り（0 = なし）
    float gateMs = 950.0f;      // ゲートの長さ（kind 1）
};
#include "Profiles.h"   // 測って合わせた値（applyFittedProfiles）

// ---------------------------------------------------------------- 部品
struct OnePole   // 双一次変換の 1 次ローパス（ハイパスは x - LP）
{
    float a = 0, b = 0, z = 0, z0 = 0;
    void set (float hz, float fs)
    {
        hz = std::clamp (hz, 1.0f, fs * 0.49f);
        const float k = std::tan (kPi * hz / fs);
        a = k / (1 + k); b = (1 - k) / (1 + k);
    }
    float lp (float x) { const float y = a * x + a * z0 + b * z; z0 = x; z = y; return y; }
    void reset() { z = z0 = 0; }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void lowpass (float hz, float q, float fs)
    {
        const float w = 2 * kPi * std::min (hz, fs * 0.49f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const float n = 1 + al; b0 = (1 - c) / 2 / n; b1 = (1 - c) / n; b2 = b0; a1 = -2 * c / n; a2 = (1 - al) / n;
    }
    void shelf (bool high, float hz, float db, float fs)
    {
        const float A = std::pow (10.0f, db / 40), w = 2 * kPi * std::min (hz, fs * 0.45f) / fs, c = std::cos (w), s = std::sin (w);
        const float al = s / 2 * std::sqrt (2.0f), sq = 2 * std::sqrt (A) * al;
        float B0, B1, B2, A0, A1, A2;
        if (high) { B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
                    A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; }
        else      { B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq);
                    A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void peak (float hz, float q, float db, float fs)
    {
        const float A = std::pow (10.0f, db / 40), w = 2 * kPi * hz / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        const float A0 = 1 + al / A; b0 = (1 + al * A) / A0; b1 = -2 * c / A0; b2 = (1 - al * A) / A0; a1 = b1; a2 = (1 - al / A) / A0;
    }
};

struct SosD   // 急なフィルター用（double で計算）
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float xf) { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
};

struct Delay
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxLen) { int n = 1; while (n < maxLen + 8) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float at (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }   // d = 0 が今入れた値
    float frac (float d) const
    {   // 3 次補間
        const int i = (int) d; const float f = d - (float) i;
        const float y0 = at (i > 0 ? i - 1 : 0), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Allpass   // Schroeder 型（長さ固定 or 揺らす）
{
    Delay d; float len = 1;
    void init (int maxLen) { d.init (maxLen + 64); }
    float process (float x, float g, float mod = 0)
    {
        const float v = mod == 0.0f ? d.at ((int) len - 1) : d.frac (std::max (1.0f, len + mod) - 1.0f);
        const float u = x + g * v;
        d.push (u);
        return v - g * u;
    }
};

struct Lfo   // 揺れ: 0 = なめらかなランダム, 1 = サイン
{
    float phase = 0, inc = 0, a = 0, b = 0; uint32_t seed = 1; int type = 0;
    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }
    void setRate (float hz, float fs) { inc = hz / fs; }
    float next()
    {
        phase += inc;
        if (type == 1) { if (phase >= 1) phase -= 1; return std::sin (2 * kPi * phase); }
        if (phase >= 1) { phase -= 1; a = b; b = rnd(); }
        const float t = phase * phase * (3 - 2 * phase);
        return a + (b - a) * t;
    }
};

// ---------------------------------------------------------------- 本体
class Engine
{
public:
    Profile profiles[kNumModes];

    Engine() { for (auto& q : profiles) q = Profile(); profiles[14].kind = 1; applyFittedProfiles (profiles); }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float ms = fs / 1000.0f;
        for (int c = 0; c < 2; ++c)
        {
            pre[c].init ((int) (520 * ms));
            for (auto& a : inAp[c]) a.init ((int) (60 * ms));
            spread[c].init ((int) (2600 * ms));
            clusterOut[c].init ((int) (400 * ms));
            crossOut[c].init ((int) (400 * ms));
            tankOut[c].init ((int) (400 * ms));
        }
        for (int i = 0; i < kBranches; ++i)
        {
            br[i].ap1.init ((int) (80 * ms)); br[i].ap2.init ((int) (80 * ms));
            br[i].d1.init ((int) (300 * ms)); br[i].d2.init ((int) (300 * ms));
        }
        // 粒の並び（どのモードでも同じ乱数。左右・反対側で別）
        uint32_t s = 20240611u;
        auto rnd = [&] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f; };
        for (int set = 0; set < 4; ++set)
            for (int k = 0; k < kTaps; ++k) { jit[set][k] = rnd() - 0.5f; amp[set][k] = (0.55f + 0.9f * rnd()) * (rnd() < 0.5f ? -1.0f : 1.0f); }
        for (int set = 0; set < 2; ++set)
            for (int k = 0; k < kGateTaps; ++k) { gJit[set][k] = rnd(); gAmp[set][k] = (0.5f + rnd()) * (rnd() < 0.5f ? -1.0f : 1.0f); }
        reset();
        dirty = true; cutInit = false;
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            pre[c].clear(); for (auto& a : inAp[c]) a.d.clear(); spread[c].clear();
            clusterOut[c].clear(); crossOut[c].clear(); tankOut[c].clear();
            hc[c].reset(); lc[c].reset(); for (auto& q : tone[c]) q.reset(); for (auto& q : vint[c]) q.reset();
        }
        for (auto& b : br) { b.ap1.d.clear(); b.ap2.d.clear(); b.d1.clear(); b.d2.clear(); b.xo.reset(); b.hs.reset(); b.damp.reset(); b.vdamp.reset(); }
        for (int i = 0; i < kBranches; ++i)
        {
            fb[i] = 0;
            lfo[i].seed = 12345u + 7919u * (uint32_t) i; lfo[i].phase = 0.25f * (float) i; lfo[i].a = lfo[i].b = 0;
        }
    }

    void setParams (const Params& np) { p = np; dirty = true; }
    const Params& getParams() const { return p; }
    float getLatencyFreeTailSeconds() const { return Maps::decaySec (p.decay) * profiles[p.mode].rtScale * 1.2f + 1.0f; }

    // 左右の入力を受けて、左右の出力（ドライとの混ぜ込み済み）を書く
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (dirty) update();
        const bool gate = profiles[p.mode].kind == 1;
        const float aOn = 1.0f - std::exp (-1.0f / (0.02f * fs));    // ON ⇄ OFF の混ぜ替え 20 ms
        const float aHz = 1.0f - std::exp (-16.0f / (0.03f * fs));   // 周波数（対数）は 30 ms で追う。係数は 16 サンプルごと
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0 && (hcLog != hcLogT || lcLog != lcLogT))
            {
                { const float nv = hcLog + (hcLogT - hcLog) * aHz; hcLog = (nv == hcLog || std::abs (hcLogT - nv) < 1.0e-4f) ? hcLogT : nv; }   // 丸めでもう進まないなら目標ちょうど
                { const float nv = lcLog + (lcLogT - lcLog) * aHz; lcLog = (nv == lcLog || std::abs (lcLogT - nv) < 1.0e-4f) ? lcLogT : nv; }
                setCuts();
            }
            { const float nv = hcOn + (hcOnT - hcOn) * aOn; hcOn = (nv == hcOn || std::abs (hcOnT - nv) < 1.0e-6f) ? hcOnT : nv; }
            { const float nv = lcOn + (lcOnT - lcOn) * aOn; lcOn = (nv == lcOn || std::abs (lcOnT - nv) < 1.0e-6f) ? lcOnT : nv; }
            const float x[2] = { inL[i], inR[i] };
            float s[2];
            for (int c = 0; c < 2; ++c)
            {
                float v = x[c];
                pre[c].push (v);
                v = preSamp < 1.0f ? v : pre[c].frac (preSamp);
                for (int k = 0; k < 4; ++k) v = inAp[c][k].process (v, (k & 1) ? -inG : inG);
                spread[c].push (v);
                s[c] = v;
            }
            float w[2];
            if (gate) gateTick (w);
            else tankTick (w);
            for (int c = 0; c < 2; ++c)
            {
                float v = w[c];
                for (auto& q : tone[c]) v = q.process (v);
                const float hl = hc[c].lp (v); v += hcOn * (hl - v);
                const float ll = lc[c].lp (v); v -= lcOn * ll;
                if (vintage) for (int k = 0; k < vintN; ++k) v = vint[c][k].process (v);
                w[c] = v;
            }
            outL[i] = dryG * x[0] + wetG * w[0];
            outR[i] = dryG * x[1] + wetG * w[1];
        }
    }

    // ---- テストツール用：プロファイルの値を名前で書き換える
    bool setProfileField (int mode, const char* name, float v)
    {
        Profile& q = profiles[mode];
        struct F { const char* n; float* f; } fl[] = {
            { "loopMs", &q.loopMs }, { "sizeMin", &q.sizeMin }, { "inDiffMs", &q.inDiffMs }, { "inDiffAmt", &q.inDiffAmt }, { "lateAmt", &q.lateAmt },
            { "rtScale", &q.rtScale }, { "gainDb", &q.gainDb }, { "earlyMs", &q.earlyMs }, { "crossMs", &q.crossMs }, { "crossDb", &q.crossDb },
            { "clusterMs", &q.clusterMs }, { "clusterDecayDb", &q.clusterDecayDb }, { "clusterDb", &q.clusterDb }, { "clusterN", &q.clusterN }, { "tankMs", &q.tankMs },
            { "attackMs", &q.attackMs }, { "hsScale", &q.hsScale }, { "modMs", &q.modMs }, { "modStyle", &q.modStyle },
            { "lowDb", &q.lowDb }, { "midDb", &q.midDb }, { "highDb", &q.highDb }, { "dampHz", &q.dampHz }, { "gateMs", &q.gateMs } };
        for (auto& f : fl) if (! std::strcmp (f.n, name)) { *f.f = v; dirty = true; return true; }
        if (! std::strcmp (name, "kind")) { q.kind = (int) v; dirty = true; return true; }
        return false;
    }

private:
    struct Branch
    {
        Allpass ap1, ap2; Delay d1, d2;
        OnePole xo, hs, damp, vdamp;
        int l1 = 1, l2 = 1; float gMid = 0.5f, gLow = 0.5f, hsGain = 1; bool useDamp = false, useVDamp = false;
    };
    // 1 周の中の長さの比（ms、合計 340）
    static constexpr float kAp1[kBranches] = { 8.9f, 7.6f, 10.1f, 6.8f };
    static constexpr float kD1[kBranches]  = { 34.1f, 39.7f, 31.3f, 42.9f };
    static constexpr float kAp2[kBranches] = { 12.3f, 14.9f, 11.1f, 13.7f };
    static constexpr float kD2[kBranches]  = { 27.6f, 22.9f, 30.7f, 25.1f };
    static constexpr float kInAp[4] = { 0.157f, 0.118f, 0.419f, 0.306f };   // 入口の拡散（合計 1）
    static constexpr int kGateTaps = 160;

    Params p; float fs = 48000; bool dirty = true;
    Delay pre[2], spread[2], clusterOut[2], crossOut[2], tankOut[2];
    Allpass inAp[2][4];
    Branch br[kBranches]; Lfo lfo[kBranches]; float fb[kBranches] {};
    OnePole hc[2], lc[2]; Biquad tone[2][3];
    // HIGH CUT / LOW CUT のなめらかさ: 周波数は対数で追い、ON / OFF は混ぜる量（0〜1）で切り替える
    float hcLog = 9.9f, hcLogT = 9.9f, lcLog = 3.0f, lcLogT = 3.0f, hcOn = 1, hcOnT = 1, lcOn = 0, lcOnT = 0; bool cutInit = false;
    float hcHzT = 20000.0f, lcHzT = 20.0f;   // 目標の周波数そのもの（着いたらこれを使う = 前とまったく同じ係数）
    void setCuts() { for (int c = 0; c < 2; ++c) { hc[c].set (hcLog == hcLogT ? hcHzT : std::exp (hcLog), fs); lc[c].set (lcLog == lcLogT ? lcHzT : std::exp (lcLog), fs); } } SosD vint[2][6]; int vintN = 0; bool vintage = false;
    float preSamp = 0, inG = 0.7f, lateG = 0.55f, dryG = 0, wetG = 1, modAmp = 0;
    float clusterG = 1, crossG = 0.7f, tankG = 1, wetGain = 1;
    int earlyS = 0, crossS = 0, tankS = 0;
    // 粒: 時刻（サンプル）と重み。0 = 左の同じ側, 1 = 右の同じ側, 2 = 左→右, 3 = 右→左
    float jit[4][kTaps] {}, amp[4][kTaps] {};
    int tapT[4][kTaps] {}; float tapW[4][kTaps] {}; float injW[kTaps] {};
    // ゲート
    float gJit[2][kGateTaps] {}, gAmp[2][kGateTaps] {};
    int gT[2][kGateTaps] {}; float gW[2][kGateTaps] {};

    void update()
    {
        dirty = false;
        const Profile& pr = profiles[std::clamp (p.mode, 0, kNumModes - 1)];
        const float ms = fs / 1000.0f;
        const float sf = Maps::sizeFactor (p.size, pr.sizeMin);
        const float scale = (pr.loopMs / 340.0f) * sf;
        preSamp = Maps::predelayMs (p.predelay) * ms;
        inG = pr.inDiffAmt * p.earlyDiff;
        lateG = pr.lateAmt * p.lateDiff;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) inAp[c][k].len = std::max (1.0f, std::round (kInAp[k] * pr.inDiffMs * sf * ms * (c ? 1.061f : 1.0f)));

        // ---- ループ
        const float rt = std::max (0.05f, Maps::decaySec (p.decay) * pr.rtScale);
        const float bm = Maps::bassMult (p.bassMult);
        const float hsLin = std::pow (10.0f, Maps::highShelfDb (p.highShelf) * pr.hsScale / (20.0f * kBranches));   // 1 枝ぶん
        for (int i = 0; i < kBranches; ++i)
        {
            Branch& b = br[i];
            b.ap1.len = std::max (2.0f, std::round (kAp1[i] * scale * ms)); b.ap2.len = std::max (2.0f, std::round (kAp2[i] * scale * ms));
            b.l1 = std::max (1, (int) (kD1[i] * scale * ms)); b.l2 = std::max (1, (int) (kD2[i] * scale * ms));
            const float lenSec = (kAp1[i] + kD1[i] + kAp2[i] + kD2[i]) * scale / 1000.0f;
            b.gMid = std::pow (10.0f, -3.0f * lenSec / rt);
            b.gLow = std::pow (10.0f, -3.0f * lenSec / (rt * bm));
            b.hsGain = hsLin;
            b.xo.set (Maps::bassXoverHz (p.bassXover), fs);
            b.hs.set (Maps::highFreqHz (p.highFreq), fs);
            b.useDamp = pr.dampHz > 1.0f;
            if (b.useDamp) b.damp.set (pr.dampHz, fs);
            // 70s: 本物はループの中でも高音が 1 周ごとに減る（5〜12 kHz）。1 枝ごとに 12.7 kHz の 1 次ローパスで同じ量
            b.useVDamp = p.color == 0;
            b.vdamp.set (12700.0f, fs);
        }
        const float rate = Maps::modRateHz (p.modRate);
        const int style = pr.modStyle > 0.5f ? (int) (pr.modStyle + 0.5f) : (p.color == 2 ? 2 : 1);
        for (int i = 0; i < kBranches; ++i)
        {
            lfo[i].type = style == 1 ? 0 : 1;
            lfo[i].setRate (rate * (style == 3 ? 1.0f : 1.0f + 0.137f * (float) i), fs);
        }
        // ランダムの揺れはサインより音程の動きが小さいので、80s（COLOR で決まるランダム）は本物に合わせて 3.5 倍
        const float styleGain = style == 3 ? 3.0f : (pr.modStyle < 0.5f && style == 1 ? 3.5f : 1.0f);
        modAmp = Maps::modDepthAmt (p.modDepth) * pr.modMs * ms * styleGain;

        // ---- 粒のかたまりと ATTACK
        const float a = p.attack;
        const float start = a > 0.5f ? (a - 0.5f) * 2.0f * pr.attackMs * sf : 0.0f;   // ms
        const float gamma = (a - 0.5f) * 5.0f;          // 前が大きい ← → 後ろが大きい
        const float clen = std::max (5.0f, pr.clusterMs * sf);
        float sumSq[4] = {}, sumInj = 0;
        const int nAct = std::clamp ((int) std::lround (pr.clusterN), 1, kTaps);
        const float nf = (float) nAct;
        for (int k = 0; k < kTaps; ++k)
        {
            const bool on = k < nAct;
            const float x = std::min (1.0f, ((float) k + 0.5f) / nf);
            const float env = std::exp (gamma * x);
            const float dec = std::pow (10.0f, -pr.clusterDecayDb * x / 20.0f);
            for (int set = 0; set < 4; ++set)
            {
                const float t = start + clen * std::clamp (x + jit[set][k] / nf, 0.0f, 1.0f);
                tapT[set][k] = (int) (t * ms);
                tapW[set][k] = on ? amp[set][k] * env * dec : 0.0f;
                sumSq[set] += tapW[set][k] * tapW[set][k];
            }
            injW[k] = on ? env * (k & 1 ? -1.0f : 1.0f) : 0.0f;
            sumInj += injW[k] * injW[k];
        }
        for (int set = 0; set < 4; ++set) for (auto& w : tapW[set]) w /= std::sqrt (sumSq[set]);
        for (auto& w : injW) w /= std::sqrt (sumInj);

        const float esf = Maps::sizeFactor (p.size, std::max (0.35f, pr.sizeMin));
        // 最初の粒はかたまりの頭から少し後ろにあるので、その分を引いて「最初の音 = earlyMs」にする
        int firstTap = tapT[0][0];
        for (int k = 0; k < kTaps; ++k) if (tapW[0][k] != 0.0f) firstTap = std::min (firstTap, tapT[0][k]);
        earlyS = std::max (0, (int) (pr.earlyMs * esf * ms) - firstTap + (int) (start * ms));
        crossS = std::max (0, (int) (pr.crossMs * sf * ms));
        tankS = std::max (0, (int) (pr.tankMs * sf * ms));
        clusterG = std::pow (10.0f, pr.clusterDb / 20.0f);
        crossG = std::pow (10.0f, pr.crossDb / 20.0f);
        tankG = 0.35f;
        wetGain = std::pow (10.0f, pr.gainDb / 20.0f);

        for (int c = 0; c < 2; ++c)
        {
            tone[c][0].shelf (false, 250, pr.lowDb, fs);
            tone[c][1].peak (1500, 0.7f, pr.midDb, fs);
            tone[c][2].shelf (true, 5000, pr.highDb, fs);
            setupVintage (vint[c]);
        }
        // HIGH CUT / LOW CUT は目標だけ決め、process で 16 サンプルごとになめらかに追う（ブロックごとの段でジジッと鳴らないように）。
        // OFF ⇄ ON は「フィルターを通した音」と「通さない音」を 20 ms で混ぜ替える（周波数は聞こえていない間にだけ一気に合わせる）
        hcOnT = (p.highCut < 0.999f && Maps::highCutHz (p.highCut) < fs * 0.49f) ? 1.0f : 0.0f;
        lcOnT = p.lowCut > 0.002f ? 1.0f : 0.0f;
        if (hcOnT > 0.0f) { hcHzT = Maps::highCutHz (p.highCut); hcLogT = std::log (hcHzT); if (hcOn <= 0.0f) hcLog = hcLogT; }
        if (lcOnT > 0.0f) { lcHzT = Maps::lowCutHz (p.lowCut); lcLogT = std::log (lcHzT); if (lcOn <= 0.0f) lcLog = lcLogT; }
        if (! cutInit) { hcLog = hcLogT; lcLog = lcLogT; hcOn = hcOnT; lcOn = lcOnT; cutInit = true; }
        setCuts();
        vintage = p.color == 0 && vintN > 0;
        const float m = std::clamp (p.mix, 0.0f, 1.0f);
        dryG = std::cos (m * kPi / 2); wetG = std::sin (m * kPi / 2);
        if (pr.kind == 1) updateGate (pr, sf);
    }

    void setupVintage (SosD* f)
    {
        vintN = 0;
        for (auto& t : kVintageSos)
            if (std::abs ((float) t.rate - fs) < 1.0f)
            {
                for (int k = 0; k < 6; ++k)
                { f[k].b0 = t.s[k][0]; f[k].b1 = t.s[k][1]; f[k].b2 = t.s[k][2]; f[k].a1 = t.s[k][4]; f[k].a2 = t.s[k][5]; }
                vintN = 6; return;
            }
        // 表にないサンプルレート: 2 次のローパス 6 段で近いものを
        for (int k = 0; k < 6; ++k) { Biquad b; b.lowpass (12000, 0.54f + 0.35f * (float) k, fs); f[k].b0 = b.b0; f[k].b1 = b.b1; f[k].b2 = b.b2; f[k].a1 = b.a1; f[k].a2 = b.a2; }
        vintN = 6;
    }

    float clusterSum (int c, int set) const
    {
        float v = 0;
        for (int k = 0; k < kTaps; ++k) v += tapW[set][k] * spread[c].at (tapT[set][k]);
        return v;
    }

    void tankTick (float* w)
    {
        // ループへ入れる（ATTACK の形をそのまま使う）
        float inj[2] = { 0, 0 };
        for (int k = 0; k < kTaps; ++k) { inj[0] += injW[k] * spread[0].at (tapT[0][k]); inj[1] += injW[k] * spread[1].at (tapT[1][k]); }
        float outv[kBranches];
        for (int i = 0; i < kBranches; ++i)
        {
            Branch& b = br[i];
            float x = fb[i];
            if (i == 0) x += inj[0];
            if (i == 2) x += inj[1];
            x = b.ap1.process (x, (i & 1) ? -lateG : lateG, modAmp * lfo[i].next());
            b.d1.push (x);
            float y = b.d1.at (b.l1 - 1);
            const float lo = b.xo.lp (y);
            y = b.gLow * lo + b.gMid * (y - lo);
            const float hlo = b.hs.lp (y);
            y = hlo + b.hsGain * (y - hlo);
            if (b.useDamp) y = b.damp.lp (y);
            if (b.useVDamp) y = b.vdamp.lp (y);
            y = b.ap2.process (y, lateG * 0.85f);
            b.d2.push (y);
            outv[i] = b.d2.at (b.l2 - 1);
        }
        for (int i = 0; i < kBranches; ++i) fb[(i + 1) % kBranches] = outv[i];
        auto taps = [&] (int a, int c) {
            const Branch& A = br[a]; const Branch& C = br[c];
            return A.d1.at ((int) ((float) A.l1 * 0.13f)) + A.d1.at ((int) ((float) A.l1 * 0.71f)) - A.d2.at ((int) ((float) A.l2 * 0.42f))
                 + C.d1.at ((int) ((float) C.l1 * 0.37f)) - C.d2.at ((int) ((float) C.l2 * 0.09f)) + C.d2.at ((int) ((float) C.l2 * 0.83f));
        };
        tankOut[0].push (taps (1, 2)); tankOut[1].push (taps (3, 0));
        clusterOut[0].push (clusterSum (0, 0)); clusterOut[1].push (clusterSum (1, 1));
        crossOut[0].push (clusterSum (1, 3)); crossOut[1].push (clusterSum (0, 2));
        for (int c = 0; c < 2; ++c)
            w[c] = wetGain * (clusterG * (clusterOut[c].at (earlyS) + crossG * crossOut[c].at (crossS)) + tankG * tankOut[c].at (tankS));
    }

    // ゲート: 決まった長さだけ鳴って切れる（DECAY は効かない）
    void updateGate (const Profile& pr, float sf)
    {
        const float ms = fs / 1000.0f;
        const float len = std::max (20.0f, pr.gateMs * sf);
        const float fadeMs = 60.0f / 80.0f * 1000.0f * 0.5f;       // 最後に 80 dB/秒 で落ちる部分
        const float gamma = (p.attack - 0.5f) * 3.0f;
        for (int c = 0; c < 2; ++c)
        {
            float sum = 0;
            for (int k = 0; k < kGateTaps; ++k)
            {
                const float t = ((float) k + gJit[c][k]) / (float) kGateTaps * (len + fadeMs);
                float wv = std::exp (gamma * std::min (t, len) / len);
                if (t > len) wv *= std::pow (10.0f, -(t - len) * 0.08f / 20.0f);
                gT[c][k] = std::min ((int) ((t + pr.earlyMs) * ms), spread[c].mask - 4);
                gW[c][k] = wv * gAmp[c][k];
                sum += gW[c][k] * gW[c][k];
            }
            for (auto& v : gW[c]) v /= std::sqrt (sum);
        }
        (void) pr;
    }
    void gateTick (float* w)
    {
        float a[2] = { 0, 0 }, b[2] = { 0, 0 };   // a = 同じ側, b = 反対側へ（別の粒の並び）
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kGateTaps; ++k) { a[c] += gW[c][k] * spread[c].at (gT[c][k]); b[c] += gW[1 - c][k] * spread[c].at (gT[1 - c][k]); }
        w[0] = wetGain * clusterG * (a[0] + crossG * b[1]);
        w[1] = wetGain * clusterG * (a[1] + crossG * b[0]);
    }
};
} // namespace elder
