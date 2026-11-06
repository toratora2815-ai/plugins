// Forge Strip: エキサイター・クリッパー・トランジェント・位相・リミッター
#pragma once
#include "Dynamics.h"

namespace fs
{
// ---------------------------------------------------------------------
// エキサイター: XY パッドの 4 つの角の歪み方を混ぜる。DRIVE は角ごとに測った「入れる量」の表で
//   左下 = やわらかいクリップ x/(1+1.25|x|)（奇数倍音が多い）
//   右下 = なめらかな奇数（小さい音ではほぼきれい）
//   左上 = 偶数だけ（x + 偶関数）
//   右上 = 少し片寄った固めのクリップ（奇数＋偶数）
struct ExciterBandSettings { bool bypass = false; float x = 0.5f, y = 0.5f, drive = 0, blend = 1.0f; bool trash = false; };
struct ExciterSettings
{
    ExciterBandSettings band[3];
    int bands = 1; float xover1 = 200, xover2 = 6000;
    float postFreq = 14000, postGain = 0;   // 仕上げの高域シェルフ（下げるだけ）
    int filterMode = 3;                     // 0 Full / 1 Defined / 2 Clear / 3 Flat（足した倍音の色）
    bool tame = false;                      // 耳に痛い 2〜5 kHz の倍音をおさえる
    float tone = 0;                         // -100..100（倍音の明るさ）
};

namespace exc
{
    // 角ごとの DRIVE → 入れる量（dB）。DRIVE 1, 3, 8, 15 で測った
    inline float driveDb (int corner, float d)
    {
        static const float D[4] = { 1, 3, 8, 15 };
        static const float G[4][4] = { { 9.5f, 11.0f, 16.0f, 23.0f },
                                       { -22.0f, -17.0f, 8.0f, 15.5f },
                                       { 9.5f, 9.5f, 11.0f, 14.0f },
                                       { 11.0f, 12.0f, 16.5f, 36.0f } };
        if (d <= D[0]) return G[corner][0];
        for (int i = 0; i < 3; ++i)
            if (d <= D[i + 1]) { const float t = (d - D[i]) / (D[i + 1] - D[i]); return G[corner][i] + t * (G[corner][i + 1] - G[corner][i]); }
        return G[corner][3];
    }
    inline float pnorm (float v, float p) { return v / std::pow (1.0f + std::pow (std::abs (v), p), 1.0f / p); }
    inline float shape (int corner, float v)
    {
        switch (corner)
        {
            case 0: return v / (1.0f + 1.25f * std::abs (v));
            case 1: return pnorm (v, 0.8f);
            case 2: return v + 0.35f * (std::sqrt (1.0f + v * v) - 1.0f);   // 偶数倍音だけ
            default: { const float q = 0.04f; return pnorm (v + q, 16.0f) - pnorm (q, 16.0f); }
        }
    }
}

class ExciterModule
{
public:
    void prepare (double s)
    {
        sr = s; split.prepare (s);
        for (int b = 0; b < 3; ++b) { os[b].prepare (s); dcb[b].highpass1 (8.0, s); color[b].reset(); colorHp[b].reset();
                                      for (int c = 0; c < 2; ++c) { dly[b][c].prepare (Oversampler4::latency + 2); dly2[b][c].prepare (Oversampler4::latency + 2); } }
        post.reset(); tameF.reset(); tone.reset();
        setFilters();
    }
    void reset() { split.reset(); for (int b = 0; b < 3; ++b) { os[b].reset(); dcb[b].reset(); color[b].reset(); colorHp[b].reset(); for (int c = 0; c < 2; ++c) { dly[b][c].reset(); dly2[b][c].reset(); } } post.reset(); tameF.reset(); tone.reset(); }
    int latency() const { return Oversampler4::latency; }
    void set (const ExciterSettings& c)
    {
        const bool f = c.postFreq != cfg.postFreq || c.postGain != cfg.postGain || c.filterMode != cfg.filterMode || c.tone != cfg.tone || c.tame != cfg.tame;
        cfg = c; split.set (c.bands, c.xover1, c.xover2);
        if (f) setFilters();
    }

    void process (float* L, float* R, int n)
    {
        split.split (L, R, n);
        for (int b = 0; b < split.bands(); ++b)
        {
            const auto& bs = cfg.band[b];
            if (bs.bypass || bs.drive <= 0.0f)
            {
                for (int ch = 0; ch < 2; ++ch) { float* p = split.buf[b][ch]; for (int i = 0; i < n; ++i) p[i] = dly[b][ch].process (p[i], Oversampler4::latency); }
                continue;
            }
            float w4[4];
            const float x = clampv (bs.x, 0.0f, 1.0f), y = clampv (bs.y, 0.0f, 1.0f);
            w4[0] = (1 - x) * (1 - y); w4[1] = x * (1 - y); w4[2] = (1 - x) * y; w4[3] = x * y;
            float gd[4], inv[4];
            for (int c = 0; c < 4; ++c)
            {
                gd[c] = dbToGain (exc::driveDb (c, bs.drive));
                // 音量の合わせ: -12 dBFS のサイン波の山が同じ高さになるように（測定は -12 dBFS で基音がほぼ同じ）
                const float a0 = 0.25f;
                const float odd = 0.5f * (exc::shape (c, gd[c] * a0) - exc::shape (c, -gd[c] * a0));
                inv[c] = a0 / std::max (1e-6f, odd);
            }
            const float fade = clampv (bs.drive, 0.0f, 1.0f);   // DRIVE 0 = きれいなまま
            const float trashG = bs.trash ? 2.5f : 1.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = split.buf[b][ch];
                for (int i = 0; i < n; ++i)
                {
                    const float in = p[i];
                    float wet = os[b].process (in, ch, [&] (float u)
                    {
                        float acc = 0;
                        for (int c = 0; c < 4; ++c) if (w4[c] > 1e-4f) acc += w4[c] * exc::shape (c, u * gd[c] * trashG) * inv[c] / (bs.trash ? 1.6f : 1.0f);
                        return acc;
                    });
                    wet = dcb[b].process (wet, ch);
                    // 足した倍音（wet - in）に色をつける
                    float add = wet - dly2[b][ch].process (in, Oversampler4::latency);
                    if (cfg.filterMode != 3) add = colorHp[b].process (add, ch);
                    if (cfg.filterMode == 0) add = color[b].process (add, ch) * 1.0f;
                    const float dryIn = dly[b][ch].process (in, Oversampler4::latency);
                    wet = dryIn + add;
                    const float bl = bs.blend * fade;
                    p[i] = dryIn + bl * (wet - dryIn);
                }
            }
        }
        split.sum (L, R, n);
        for (int i = 0; i < n; ++i)
        {
            if (cfg.tame) { L[i] = tameF.process (L[i], 0); R[i] = tameF.process (R[i], 1); }
            if (cfg.tone != 0) { L[i] = tone.process (L[i], 0); R[i] = tone.process (R[i], 1); }
            if (cfg.postGain < 0) { L[i] = post.process (L[i], 0); R[i] = post.process (R[i], 1); }
        }
    }

private:
    void setFilters()
    {
        post.highShelf (cfg.postFreq, 0.7071, cfg.postGain, sr);
        tameF.peak (3500, 0.8, -3.0, sr);
        tone.highShelf (2500, 0.6, cfg.tone * 0.06, sr);   // ±6 dB
        for (int b = 0; b < 3; ++b)
        {
            // Defined = 600 Hz から上の倍音、Clear = 2 kHz から上、Full = 低めも厚く
            const double hpF = cfg.filterMode == 1 ? 600.0 : (cfg.filterMode == 2 ? 2000.0 : 40.0);
            colorHp[b].highpass (hpF, 0.7071, sr);
            color[b].lowShelf (300, 0.7071, 4.0, sr);
        }
    }
    ExciterSettings cfg;
    BandSplit split;
    Oversampler4 os[3];
    Biquad dcb[3], color[3], colorHp[3], post, tameF, tone;
    Delay dly[3][2], dly2[3][2];
    double sr = 48000;
};

// ---------------------------------------------------------------------
// クリッパー: しきい値で切る。SOFTCLIP BOOST は前で持ち上げ、しきい値の BOOST/2 下から丸める（測定）
struct ClipperBandSettings { bool bypass = false; float threshold = 0, boost = 0; };
struct ClipperSettings { ClipperBandSettings band[3]; int bands = 1; float xover1 = 200, xover2 = 6000; };

class ClipperModule
{
public:
    void prepare (double s) { sr = s; split.prepare (s); for (auto& o : os) o.prepare (s); for (auto& d : dly) for (auto& x : d) x.prepare (Oversampler4::latency + 2); }
    void reset() { split.reset(); for (auto& o : os) o.reset(); for (auto& d : dly) for (auto& x : d) x.reset(); }
    int latency() const { return Oversampler4::latency; }
    void set (const ClipperSettings& c) { cfg = c; split.set (c.bands, c.xover1, c.xover2); }
    static inline float clip (float u, float k)
    {
        const float a = std::abs (u);
        if (a <= k) return u;
        const float y = k >= 0.999f ? 1.0f : k + (1.0f - k) * std::tanh ((a - k) / (1.0f - k));
        return u < 0 ? -y : y;
    }
    void process (float* L, float* R, int n)
    {
        split.split (L, R, n);
        for (int b = 0; b < split.bands(); ++b)
        {
            const auto& bs = cfg.band[b];
            if (bs.bypass)
            {
                lastGr[b] = 0;
                for (int ch = 0; ch < 2; ++ch) { float* p = split.buf[b][ch]; for (int i = 0; i < n; ++i) p[i] = dly[b][ch].process (p[i], Oversampler4::latency); }
                continue;
            }
            const float t = dbToGain (bs.threshold), pre = dbToGain (bs.boost) / t, k = dbToGain (-bs.boost * 0.5f);
            float peakIn = 0, peakOut = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = split.buf[b][ch];
                for (int i = 0; i < n; ++i)
                {
                    peakIn = std::max (peakIn, std::abs (p[i]) * dbToGain (bs.boost));
                    p[i] = os[b].process (p[i], ch, [&] (float u) { return clip (u * pre, k) * t; });
                    peakOut = std::max (peakOut, std::abs (p[i]));
                }
            }
            lastGr[b] = std::max (0.0f, gainToDb (peakIn + 1e-9f) - gainToDb (peakOut + 1e-9f));
        }
        split.sum (L, R, n);
    }
    float reduction (int b) const { return lastGr[b]; }
private:
    ClipperSettings cfg; BandSplit split; Oversampler4 os[3]; Delay dly[3][2]; double sr = 48000; float lastGr[3] {};
};

// ---------------------------------------------------------------------
// トランジェント: 3 ms 先読み（遅れ 3 ms）。速い包絡と遅い包絡の差でアタック、ピーク保持との差でサステイン
struct TransientBandSettings { bool bypass = false; float attack = 0, sustain = 0; int contour = 0; };
struct TransientSettings { TransientBandSettings band[3]; int bands = 1; float xover1 = 200, xover2 = 6000; int mode = 1; };

namespace tsTune
{
    // モード（Precise / Balanced / Loose）ごとの値。基準を測って tools の当てはめで決めた
    //   slowAtk / slowRel: 遅い包絡（dB）の上がり・下がりの時定数 ms
    //   attackRange: 速い包絡と遅い包絡の差がこの dB で全量
    //   holdRel: サステイン用のピーク保持の下がり ms、sustainRange: 保持と今の差がこの dB で全量
    //   gainRel: 量が減るときのなめらかさ ms
    struct Mode { float slowAtk, slowRel, attackRange, holdRel, sustainRange, gainRel; };
    // ATTACK がマイナスのとき（基準はプラスより速く戻る）
    struct Neg { float slowAtk = 40.0f, attackRange = 40.0f, gainRel = 10.0f; };
    inline Neg& neg() { static Neg n; return n; }
    inline Mode& mode (int m)
    {
        static Mode t[3] = { { 110.0f, 300.0f, 20.0f, 120.0f, 14.0f, 35.0f },
                             { 110.0f,  90.0f, 40.0f, 120.0f, 14.0f, 100.0f },
                             {  30.0f, 150.0f, 40.0f, 120.0f, 14.0f, 200.0f } };
        return t[clampv (m, 0, 2)];
    }
    inline float contourMs (int c) { static const float m[3] = { 0.6f, 2.5f, 7.0f }; return m[clampv (c, 0, 2)]; }
}

class TransientModule
{
public:
    void prepare (double s)
    {
        sr = s; la = (int) std::round (0.003 * s);
        split.prepare (s);
        for (int b = 0; b < 3; ++b) { st[b] = {}; for (int c = 0; c < 2; ++c) dl[b][c].prepare (la + 2); }
    }
    void reset() { split.reset(); for (int b = 0; b < 3; ++b) { st[b] = {}; for (int c = 0; c < 2; ++c) dl[b][c].reset(); } }
    int latency() const { return la; }
    void set (const TransientSettings& c) { cfg = c; split.set (c.bands, c.xover1, c.xover2); }
    void process (float* L, float* R, int n)
    {
        split.split (L, R, n);
        const auto& md = tsTune::mode (cfg.mode);
        const float fRel = coefMs (5.0f, sr), sAtk = coefMs (md.slowAtk, sr), sRel = coefMs (md.slowRel, sr);
        const float hRel = coefMs (md.holdRel, sr), gRel = coefMs (md.gainRel, sr), sRet = coefMs (3.0f, sr);
        const float nAtk = coefMs (tsTune::neg().slowAtk, sr), nRel = coefMs (tsTune::neg().gainRel, sr);
        for (int b = 0; b < split.bands(); ++b)
        {
            const auto& bs = cfg.band[b];
            auto& s = st[b];
            float* l = split.buf[b][0]; float* r = split.buf[b][1];
            const float up = coefMs (tsTune::contourMs (bs.contour), sr);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::max (std::abs (l[i]), std::abs (r[i]));
                const float adb = gainToDb (a + 1e-9f);
                // 速い包絡（dB、すぐ上がり 5 ms で下がる）
                s.fast = adb > s.fast ? adb : adb + fRel * (s.fast - adb);
                // 遅い包絡（dB）
                s.slow = s.fast + (s.fast > s.slow ? sAtk : sRel) * (s.slow - s.fast);
                // ピーク保持（サステイン用）
                s.hold = s.fast > s.hold ? s.fast : s.fast + hRel * (s.hold - s.fast);
                const auto& ng = tsTune::neg();
                const bool negA = bs.attack < 0.0f;
                s.slow2 = s.fast + (s.fast > s.slow2 ? nAtk : sRel) * (s.slow2 - s.fast);
                const float att = negA ? clampv ((s.fast - s.slow2) / ng.attackRange, 0.0f, 1.0f)
                                       : clampv ((s.fast - s.slow) / md.attackRange, 0.0f, 1.0f);
                const float sus = s.fast > -80.0f ? clampv ((s.hold - s.fast) / md.sustainRange, 0.0f, 1.0f) : 0.0f;
                const float ga = bs.bypass ? 0.0f : bs.attack * att;
                const float gs = bs.bypass ? 0.0f : bs.sustain * sus;
                // 量が「0 から離れる」ときは輪郭（CONTOUR）の速さ、「0 へ戻る」ときはアタックはモードの速さ・サステインは 3 ms
                s.gA = ga + (std::abs (ga) > std::abs (s.gA) ? up : (negA ? nRel : gRel)) * (s.gA - ga);
                s.gS = gs + (std::abs (gs) > std::abs (s.gS) ? up : sRet) * (s.gS - gs);
                s.g = s.gA + s.gS;
                const float g = dbToGain (s.g);
                l[i] = dl[b][0].process (l[i], la) * g;
                r[i] = dl[b][1].process (r[i], la) * g;
            }
            s.lastDb = s.g;
        }
        split.sum (L, R, n);
    }
    float currentGain (int b) const { return st[b].lastDb; }
private:
    struct St { float fast = -120, slow = -120, slow2 = -120, hold = -120, g = 0, gA = 0, gS = 0, lastDb = 0; };
    TransientSettings cfg; BandSplit split; St st[3]; Delay dl[3][2]; int la = 144; double sr = 48000;
};

// ---------------------------------------------------------------------
// 位相: 全帯域で同じ角度だけ位相を回す（90° ずれた 2 本の全域通過）＋遅らせる
struct PhaseSettings { float rotation = 0, delayMs = 0; };

class PhaseModule
{
public:
    void prepare (double s) { sr = s; for (int c = 0; c < 2; ++c) dly[c].prepare ((int) (0.08 * s) + 4); reset(); }
    void reset() { std::memset (sA, 0, sizeof (sA)); std::memset (sB, 0, sizeof (sB)); std::memset (prevB, 0, sizeof (prevB)); for (auto& d : dly) d.reset(); }
    void set (const PhaseSettings& c) { cfg = c; }
    void process (float* L, float* R, int n)
    {
        const float th = cfg.rotation * (float) kPi / 180.0f, cs = std::cos (th), sn = std::sin (th);
        const int d = (int) std::round (cfg.delayMs * 0.001 * sr);
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float a = chain (x, c, coA, sA[c]);
                const float bRaw = chain (x, c, coB, sB[c]);
                const float b = prevB[c]; prevB[c] = bRaw;   // 1 サンプル遅らせた方が 90° ずれる
                float y = cs * a + sn * b;
                ch[c][i] = d > 0 ? dly[c].process (y, d) : y;
            }
    }
private:
    // 2 次（z^-2）の全域通過 4 段: y = a²(x + y[n-2]) - x[n-2]
    static constexpr float coA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float coB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    static inline float chain (float x, int, const float* co, float (&s)[4][4])
    {
        for (int k = 0; k < 4; ++k)
        {
            const float a2 = co[k] * co[k];
            const float y = a2 * (x + s[k][2]) - s[k][0];
            s[k][0] = s[k][1]; s[k][1] = x;     // x[n-2], x[n-1]
            s[k][2] = s[k][3]; s[k][3] = y;     // y[n-2], y[n-1]
            x = y;
        }
        return x;
    }
    PhaseSettings cfg; double sr = 48000;
    float sA[2][4][4] {}, sB[2][4][4] {}, prevB[2] {};
    Delay dly[2];
};

// ---------------------------------------------------------------------
// リミッター（最後に置く）: 1 ms 先読み、天井、スタイル
struct LimiterSettings { int mode = 3; float ceiling = 0; int style = 1; };

class LimiterModule
{
public:
    void prepare (double s)
    {
        sr = s; la = (int) std::round (0.001 * s);
        pk.prepare (la + 1); for (auto& d : dl) d.prepare (la + 2); g = 1; env = 1;
    }
    void reset() { pk.reset(); for (auto& d : dl) d.reset(); g = 1; env = 1; }
    int latency() const { return cfg.mode == 2 ? 0 : la; }
    void set (const LimiterSettings& c) { cfg = c; }
    void process (float* L, float* R, int n)
    {
        const float ceil = dbToGain (cfg.ceiling);
        if (cfg.mode == 2)   // Hard: そのまま切る
        {
            for (int i = 0; i < n; ++i) { L[i] = clampv (L[i], -ceil, ceil); R[i] = clampv (R[i], -ceil, ceil); }
            return;
        }
        static const float relMs[3] = { 30.0f, 120.0f, 400.0f };   // Clear / Thick / Smooth
        const float rel = coefMs (relMs[clampv (cfg.style, 0, 2)], sr);
        const float att = coefMs (0.001f * 1000.0f * 0.3f, sr);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::abs (L[i]), std::abs (R[i]));
            const float m = pk.push (a);
            const float target = m > ceil ? ceil / m : 1.0f;
            env = target < env ? target : target + rel * (env - target);
            g = env + att * (g - env);
            const float gg = std::min (g, env);
            float l = dl[0].process (L[i], la) * gg, r = dl[1].process (R[i], la) * gg;
            L[i] = clampv (l, -ceil, ceil); R[i] = clampv (r, -ceil, ceil);
            lastGr = -gainToDb (gg);
        }
    }
    float reduction() const { return lastGr; }
private:
    LimiterSettings cfg; SlidingMax pk; Delay dl[2]; int la = 48; float g = 1, env = 1, lastGr = 0; double sr = 48000;
};
} // namespace fs
