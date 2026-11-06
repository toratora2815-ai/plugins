// Forge Strip: コンプレッサー・ゲート・Density（上向きのコンプ）・帯域分け
// 基準のプラグインを測った結果:
//   - 検出は L/R の大きい方の、直近 3.5 ms の最大値（サイン波でも波打たない）
//   - 量のなめらかさは「直線の音量（倍率）」の領域で、時定数 = アタック / 2、リリース / 2
//   - ニーの幅 = KNEE × 1.5 dB（2 次のなめらかなニー）
//   - 比が 1 より小さいと、しきい値より下を持ち上げる（上向きのコンプ）
#pragma once
#include "Common.h"

namespace fs
{
// ---------------------------------------------------------------------
// 帯域分け（最大 3 帯域）。各モジュールが持つ
struct BandSplit
{
    static constexpr int maxBlock = 512;
    Crossover3 xo;
    float buf[3][2][maxBlock] {};
    void prepare (double sr) { xo.prepare (sr); }
    void reset() { xo.reset(); }
    void set (int bands, float f1, float f2) { xo.set (clampv (bands, 1, 3), f1, f2); }
    int bands() const { return xo.bands; }
    void split (const float* L, const float* R, int n)
    {
        float o[3];
        for (int i = 0; i < n; ++i)
        {
            xo.split (L[i], 0, o); for (int b = 0; b < xo.bands; ++b) buf[b][0][i] = o[b];
            xo.split (R[i], 1, o); for (int b = 0; b < xo.bands; ++b) buf[b][1][i] = o[b];
        }
    }
    void sum (float* L, float* R, int n) const
    {
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            for (int b = 0; b < xo.bands; ++b) { l += buf[b][0][i]; r += buf[b][1][i]; }
            L[i] = l; R[i] = r;
        }
    }
};

// 検出用のフィルター（サイドチェーンの HP / LP）
struct DetectFilter
{
    Biquad hp, lp; bool useHp = false, useLp = false; float hpF = -1, lpF = -1, hpQ = -1, lpQ = -1;
    void set (float hf, float hq, float lf, float lq, double sr)
    {
        if (hf != hpF || hq != hpQ) { hpF = hf; hpQ = hq; useHp = hf > 21.0f; if (useHp) hp.highpass (hf, hq, sr); }
        if (lf != lpF || lq != lpQ) { lpF = lf; lpQ = lq; useLp = lf < 19900.0f; if (useLp) lp.lowpass (lf, lq, sr); }
    }
    inline float process (float x, int ch)
    {
        if (useHp) x = hp.process (x, ch);
        if (useLp) x = lp.process (x, ch);
        return x;
    }
    void reset() { hp.reset(); lp.reset(); }
};

// ---------------------------------------------------------------------
struct CompBandSettings
{
    bool bypass = false;
    float threshold = 0, ratio = 4, attack = 10, release = 50, knee = 0, gain = 0, mix = 1.0f;
};

struct CompSettings
{
    CompBandSettings band[3];
    int bands = 1; float xover1 = 200, xover2 = 6000;
    int detect = 0;          // 0 Peak, 1 RMS, 2 True Peak
    bool autoRelease = false, autoGain = true, vintage = false;
    float globalGain = 0;    // dB
    float scHpf = 40, scHpfQ = 0.707f, scLpf = 18000, scLpfQ = 0.707f;
    bool scExternal = false; // 外部サイドチェーンで検出
};

// 1 帯域ぶんの量の計算（コンプ）
struct CompCore
{
    double sr = 48000;
    SlidingMax peak;
    float g = 1.0f;          // 今の倍率（直線）
    float rmsAcc = 0; int rmsN = 0; std::vector<float> rmsBuf; int rmsPos = 0;
    float makeA = 0, makeB = 0;   // 自動ゲイン（2 段のならし、dB）
    float relFast = 1.0f;         // 自動リリース用の速い方
    float lastGr = 0;             // 表示用（dB、正 = 下げ）
    void prepare (double s)
    {
        sr = s; peak.prepare ((int) std::round (0.0035 * sr));
        rmsBuf.assign ((size_t) std::max (1, (int) std::round (0.010 * sr)), 0.0f); rmsAcc = 0; rmsPos = 0;
        g = 1.0f; u2 = 1.0f; makeA = makeB = 0; relFast = 1.0f;
    }
    void reset() { peak.reset(); std::fill (rmsBuf.begin(), rmsBuf.end(), 0.0f); rmsAcc = 0; g = 1.0f; u2 = 1.0f; makeA = makeB = 0; relFast = 1.0f; }

    // 静的なカーブ: 入力 x dB → 下げる量 dB（負なら持ち上げ）
    static float curve (float x, const CompBandSettings& b, bool vintage)
    {
        const float T = b.threshold, R = std::max (0.1f, b.ratio);
        if (R >= 1.0f)
        {
            const float s = 1.0f - 1.0f / R;
            float W = 1.5f * b.knee;
            float lo = T - W * 0.5f, hi = T + W * 0.5f;
            if (vintage)
            {
                // Vintage: しきい値の 0.8 倍ぶん下から始まる広いニー（測定に合わせた近似）
                const float w = std::max (6.0f, 0.8f * std::abs (T)) + W * 0.5f;
                lo = T - w; hi = T + w;
            }
            if (x <= lo) return 0.0f;
            if (x >= hi) return s * (x - T);
            const float d = x - lo, w2 = hi - lo;
            // 2 次のつなぎ（lo で 0、hi で s(x-T) と傾きがつながる）
            return s * d * d / (2.0f * w2);
        }
        // 上向き（比 < 1）: しきい値より下を (1 - R) の割合で持ち上げる
        if (x < -100.0f) return 0.0f;
        const float W = 1.5f * b.knee;
        const float s = 1.0f - R;   // 持ち上げの傾き
        float boost;
        if (W > 0 && std::abs (x - T) < W * 0.5f) { const float d = (T + W * 0.5f) - x; boost = s * d * d / (2.0f * W); }
        else boost = x < T ? s * (T - x) : 0.0f;
        return -std::min (boost, 40.0f);
    }

    // det: 検出用の音（直線の振幅）。戻り値: 倍率
    inline float process (float det, const CompBandSettings& b, const CompSettings& c)
    {
        float lvl;
        if (c.detect == 1)
        {
            const float v = det * det;
            rmsAcc += v - rmsBuf[(size_t) rmsPos]; rmsBuf[(size_t) rmsPos] = v;
            if (++rmsPos >= (int) rmsBuf.size()) rmsPos = 0;
            // サイン波のピークと同じ値になるよう √2 倍（測定: RMS でも静的カーブはピークと同じ）
            lvl = std::sqrt (std::max (0.0f, rmsAcc) / (float) rmsBuf.size() * 2.0f);
        }
        else lvl = peak.push (det);
        const float x = gainToDb (lvl + 1e-12f);
        const float gr = curve (x, b, c.vintage);
        const float target = dbToGain (-gr);
        const float aC = coefMs (b.attack * 0.5f, sr);
        float rMs = b.release * 0.5f;
        if (c.autoRelease)
        {
            // 自動リリース: 短い山のあとは速く、長く下げ続けたあとは遅く戻す
            const float over = clampv (gr / 12.0f, 0.0f, 1.0f);
            relFast += (over - relFast) * (1.0f - coefMs (300.0f, sr));
            rMs *= 0.35f + 1.3f * relFast;
        }
        const float rC = coefMs (rMs, sr);
        const float k = target < g ? aC : rC;
        g = target + k * (g - target);
        // 2 段目（測定）: 戻るときは「1/倍率」の領域で τ = アタック のならし → 戻りの速さが 8.7/アタック dB/ms で頭打ち
        if (g < 1.0f || u2 > 1.0f)
        {
            const float u1 = 1.0f / std::max (g, 1.0e-9f);
            if (u1 >= u2) u2 = u1;
            else { const float c2 = coefMs (std::min (b.attack, b.release), sr); u2 = u1 + c2 * (u2 - u1); }
        }
        else u2 = 1.0f / std::max (g, 1.0e-9f);
        const float gOut = 1.0f / u2;
        lastGr = -gainToDb (gOut);
        if (c.autoGain)
        {
            // 自動ゲイン: 下げている量の平均を、上がるときは速く、下がるときはゆっくり戻す（測定に合わせた 2 段）
            const float grDb = lastGr;
            const float k1 = grDb > makeA ? coefMs (60.0f, sr) : coefMs (300.0f, sr);
            makeA = grDb + k1 * (makeA - grDb);
            const float k2 = makeA > makeB ? coefMs (80.0f, sr) : coefMs (350.0f, sr);
            makeB = makeA + k2 * (makeB - makeA);
        }
        return gOut;
    }
    float makeupDb() const { return makeB; }
    float u2 = 1.0f;
};

class CompModule
{
public:
    void prepare (double s)
    {
        sr = s; split.prepare (s);
        for (int b = 0; b < 3; ++b) { core[b].prepare (s); detf[b].reset(); }
        bandSplitDet.prepare (s);
    }
    void reset() { split.reset(); bandSplitDet.reset(); for (int b = 0; b < 3; ++b) { core[b].reset(); detf[b].reset(); } }
    void set (const CompSettings& s) { cfg = s; split.set (s.bands, s.xover1, s.xover2); bandSplitDet.set (s.bands, s.xover1, s.xover2);
        for (int b = 0; b < 3; ++b) detf[b].set (s.scHpf, s.scHpfQ, s.scLpf, s.scLpfQ, sr); }

    // scL/scR: 外部サイドチェーン（なければ nullptr）
    void process (float* L, float* R, int n, const float* scL = nullptr, const float* scR = nullptr)
    {
        split.split (L, R, n);
        const bool ext = cfg.scExternal && scL != nullptr;
        if (ext) bandSplitDet.split (scL, scR, n);
        const int nb = split.bands();
        for (int b = 0; b < nb; ++b)
        {
            const auto& bs = cfg.band[b];
            float* l = split.buf[b][0]; float* r = split.buf[b][1];
            const float* dl = ext ? bandSplitDet.buf[b][0] : l;
            const float* dr = ext ? bandSplitDet.buf[b][1] : r;
            if (bs.bypass) { core[b].lastGr = 0; continue; }
            const float mk = dbToGain (bs.gain);
            for (int i = 0; i < n; ++i)
            {
                const float d = std::max (std::abs (detf[b].process (dl[i], 0)), std::abs (detf[b].process (dr[i], 1)));
                const float g = core[b].process (d, bs, cfg);
                const float auto_ = cfg.autoGain ? dbToGain (core[b].makeupDb()) : 1.0f;
                const float wet = g * auto_ * mk;
                const float gg = bs.mix * wet + (1.0f - bs.mix) * 1.0f;
                l[i] *= gg; r[i] *= gg;
            }
        }
        split.sum (L, R, n);
        const float gg = dbToGain (cfg.globalGain);
        if (gg != 1.0f) for (int i = 0; i < n; ++i) { L[i] *= gg; R[i] *= gg; }
    }
    float gainReduction (int b) const { return core[b].lastGr; }
    const CompSettings& settings() const { return cfg; }

private:
    CompSettings cfg;
    BandSplit split, bandSplitDet;
    CompCore core[3];
    DetectFilter detf[3];
    double sr = 48000;
};

// ---------------------------------------------------------------------
// ゲート（下向きのエキスパンダー）
struct GateBandSettings
{
    bool bypass = false;
    float attack = 1, ratio = 8, release = 100, open = -80, close = 0, hold = 0;   // hold は秒
};

struct GateSettings
{
    GateBandSettings band[3];
    int bands = 1; float xover1 = 200, xover2 = 6000;
    float scHpf = 40, scHpfQ = 0.707f, scLpf = 18000, scLpfQ = 0.707f;
    bool scExternal = false;
};

class GateModule
{
public:
    void prepare (double s)
    {
        sr = s; split.prepare (s); detSplit.prepare (s);
        for (int b = 0; b < 3; ++b) { st[b] = {}; st[b].pk.prepare ((int) std::round (0.0035 * sr)); detf[b].reset(); }
    }
    void reset() { split.reset(); detSplit.reset(); for (int b = 0; b < 3; ++b) { st[b].g = 1; st[b].open = false; st[b].holdLeft = 0; st[b].pk.reset(); detf[b].reset(); } }
    void set (const GateSettings& s) { cfg = s; split.set (s.bands, s.xover1, s.xover2); detSplit.set (s.bands, s.xover1, s.xover2);
        for (int b = 0; b < 3; ++b) detf[b].set (s.scHpf, s.scHpfQ, s.scLpf, s.scLpfQ, sr); }

    void process (float* L, float* R, int n, const float* scL = nullptr, const float* scR = nullptr)
    {
        split.split (L, R, n);
        const bool ext = cfg.scExternal && scL != nullptr;
        if (ext) detSplit.split (scL, scR, n);
        for (int b = 0; b < split.bands(); ++b)
        {
            const auto& bs = cfg.band[b];
            auto& s = st[b];
            float* l = split.buf[b][0]; float* r = split.buf[b][1];
            const float* dl = ext ? detSplit.buf[b][0] : l;
            const float* dr = ext ? detSplit.buf[b][1] : r;
            if (bs.bypass) { s.lastGr = 0; continue; }
            const float aC = coefMs (bs.attack * 0.5f, sr), rC = coefMs (bs.release * 0.5f, sr);
            const float R = std::max (1.0f, bs.ratio);
            const int holdN = (int) ((bs.hold + 0.0205f) * sr);   // 測定: HOLD 0 でも約 20 ms は開いたまま
            for (int i = 0; i < n; ++i)
            {
                const float d = std::max (std::abs (detf[b].process (dl[i], 0)), std::abs (detf[b].process (dr[i], 1)));
                const float x = gainToDb (s.pk.push (d) + 1e-12f);
                if (x >= bs.open) { s.open = true; s.holdLeft = holdN; }
                else if (s.open && x < bs.open - bs.close) { if (s.holdLeft > 0) --s.holdLeft; else s.open = false; }
                const float thr = s.open ? bs.open - bs.close : bs.open;
                float gr = x < thr ? (thr - x) * (R - 1.0f) : 0.0f;
                if (s.open && s.holdLeft > 0) gr = 0.0f;
                gr = std::min (gr, 200.0f);
                const float target = dbToGain (-gr);
                const float k = target > s.g ? aC : rC;     // 開く = アタック、閉じる = リリース
                s.g = target + k * (s.g - target);
                l[i] *= s.g; r[i] *= s.g;
            }
            s.lastGr = -gainToDb (s.g);
        }
        split.sum (L, R, n);
    }
    float gainReduction (int b) const { return st[b].lastGr; }

private:
    struct St { float g = 1; bool open = false; int holdLeft = 0; SlidingMax pk; float lastGr = 0; };
    GateSettings cfg;
    BandSplit split, detSplit;
    St st[3];
    DetectFilter detf[3];
    double sr = 48000;
};

// ---------------------------------------------------------------------
// Density: 小さい所を持ち上げる（上向きのコンプ）。SPEED = 持ち上げが戻る速さ（ms）
struct DensityBandSettings
{
    bool bypass = false;
    float range = 0, ratio = 4, threshold = 0, speed = 120;
};

struct DensitySettings
{
    DensityBandSettings band[3];
    int bands = 1; float xover1 = 200, xover2 = 6000;
};

class DensityModule
{
public:
    // 基準は 34.6 ms 先読み（遅れ 1662 サンプル @48k）。大きくなる前に持ち上げを外しておく
    void prepare (double s)
    {
        sr = s; la = (int) std::round (0.034625 * s); split.prepare (s);
        for (int b = 0; b < 3; ++b) { st[b] = {}; st[b].win = -1; for (int c = 0; c < 2; ++c) { st[b].dl[c].prepare (la + 2); st[b].dl2[c].prepare (la + 2); } }
    }
    void reset() { split.reset(); for (int b = 0; b < 3; ++b) { st[b].g = 1; st[b].t2 = 1; st[b].pk.reset(); for (int c = 0; c < 2; ++c) { st[b].dl[c].reset(); st[b].dl2[c].reset(); } } }
    int latency() const { return la; }
    void set (const DensitySettings& s) { cfg = s; split.set (s.bands, s.xover1, s.xover2); }

    void process (float* L, float* R, int n)
    {
        split.split (L, R, n);
        for (int b = 0; b < split.bands(); ++b)
        {
            const auto& bs = cfg.band[b];
            auto& s = st[b];
            float* l = split.buf[b][0]; float* r = split.buf[b][1];
            // 先読みの窓: SPEED が遅いほど早めに外す（測定: 10 → 約 9 ms、120 → 18 ms、300 → 32 ms）
            const int w = clampv ((int) std::round ((8.0f + 0.08f * bs.speed) * 0.001f * (float) sr), 1, la);
            if (w != s.win) { s.win = w; s.pk.prepare (w); }
            // 測定: 持ち上げが増える向きは「直線の倍率で τ = SPEED」＋その前に τ = 0.3 × SPEED のならし
            const float up1 = coefMs (bs.speed * 0.3f, sr), up2 = coefMs (bs.speed, sr);
            const float down = coefMs (0.35f * (float) w * 1000.0f / (float) sr, sr);
            const float s1 = 1.0f - 1.0f / std::max (1.01f, bs.ratio);
            const bool active = ! bs.bypass && bs.range > 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float d = std::max (std::abs (l[i]), std::abs (r[i]));
                // 先の窓の最大（今の音は la サンプル後に出る。窓は出る位置の手前 w サンプル）
                const float x = gainToDb (s.pk.push (d) + 1e-12f);
                float boost = 0.0f;
                if (active)
                {
                    boost = x < bs.threshold ? std::min (bs.range, (bs.threshold - x) * s1) : 0.0f;
                    if (x < -90.0f) boost *= clampv ((x + 110.0f) / 20.0f, 0.0f, 1.0f);   // 無音は持ち上げない
                }
                const float target = dbToGain (boost);
                if (target < s.t2) { s.t2 = target + down * (s.t2 - target); s.g = target + down * (s.g - target); }
                else { s.t2 = target + up1 * (s.t2 - target); s.g = s.t2 + up2 * (s.g - s.t2); }
                l[i] = s.dl[0].process (l[i], w) * s.g;   // 窓の分だけ遅らせた音に今の量（＝窓は先 w サンプル）
                r[i] = s.dl[1].process (r[i], w) * s.g;
            }
            s.lastBoost = gainToDb (s.g);
        }
        // 残り（la - w）を遅らせて合計 la にそろえる
        for (int b = 0; b < split.bands(); ++b)
        {
            auto& s = st[b];
            for (int c = 0; c < 2; ++c) { float* p = split.buf[b][c]; for (int i = 0; i < n; ++i) p[i] = s.dl2[c].process (p[i], la - s.win); }
        }
        split.sum (L, R, n);
    }
    float boost (int b) const { return st[b].lastBoost; }

private:
    struct St { float g = 1, t2 = 1; SlidingMax pk; int win = -1; float lastBoost = 0; Delay dl[2], dl2[2]; };
    DensitySettings cfg;
    BandSplit split;
    St st[3];
    double sr = 48000;
    int la = 1662;
};
} // namespace fs
