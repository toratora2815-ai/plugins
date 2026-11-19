#pragma once
// 抑える系の共通エンジン（表で動く）。2026-11-19
//
// 部品（VC 76 など）ごとに「つまみ → エンジンの値」の表を持つ。表は Guitar Rig の記録（つまみを 1 本ずつ動かした記録）から
// 自動で合わせ込んだもの（build/aiofx/fit/dynfit.py → DynTables.h）。つまみの効き目は「初期値からの差」を足し合わせる。
//
// 流れ: 入口 → PRE → [前の飽和（4 倍）] → 検出（側鎖のハイパス → 山 / RMS）→ 静的な曲線（コンプ＋ゲート）→ ATTACK / RELEASE
//        → 下げる → MAKEUP → [後の飽和（4 倍）] → 棚 2 つ（100 Hz・8 kHz）→ ウェット × WET ＋ ドライ × DRY → 出口
#include "DynKit.h"
#include "KnobTable.h"

namespace aiofx
{
enum DynP : int
{
    dpPre,       // 入口の増減 dB
    dpThr,       // しきい値 dB（正弦波の山 = 0 dBFS の尺度）
    dpRatio,     // 比率の log2（0 = 1:1、1 = 2:1、2 = 4:1 …）
    dpKnee,      // ニー幅 dB
    dpAtt,       // ATTACK log10 秒
    dpRel,       // RELEASE log10 秒
    dpMakeup,    // 出口の増減 dB
    dpDry,       // ドライの量（直線。負もある）
    dpWet,       // ウェットの量（直線）
    dpSat1,      // 前の飽和の押し込み dB（−40 以下 = なし）
    dpBias1,     // 前の飽和の偏り（偶数次の歪み）
    dpSat2,      // 後の飽和
    dpBias2,
    dpScHp,      // 側鎖のハイパス log10 Hz（1.3 以下 = なし）
    dpRms,       // RMS の時定数 log10 秒（−4 以下 = 山）
    dpLow,       // ウェットの低い棚 dB（100 Hz）
    dpHigh,      // ウェットの高い棚 dB（8 kHz）
    dpRange,     // コンプの下げる量の上限 dB
    dpExpThr,    // ゲート（下向き）のしきい値 dB
    dpExpRatio,  // ゲートの比率 log2（0 = なし）
    dpExpRange,  // ゲートの下げる量の上限 dB
    dpGAtt,      // ゲートの開く速さ log10 秒
    dpGRel,      // ゲートの閉じる速さ log10 秒
    dpRel2,      // 2 段目の戻り log10 秒（光センサーふう）
    dpRel2Frac,  // 2 段目の割合 0..1（0 = 1 段だけ）
    dpLook,      // 先読み ms（遅れとして申告）
    dpOut,       // ウェットの最後の増減 dB（後の飽和・棚のあと）
    dpGHold,     // ゲートが閉じ始めるまでの保持 log10 秒（しきい値を下回ってからこの間は開いたまま）
    dpCount
};

// エンジンの値の初期値（表にない値はこれ）
inline const float* dynDefaults()
{
    static const float d[dpCount] {
        0.0f, 0.0f, 0.0f, 0.0f, -2.5f, -1.0f, 0.0f, 0.0f, 1.0f,
        -60.0f, 0.0f, -60.0f, 0.0f, 0.0f, -5.0f, 0.0f, 0.0f, 100.0f,
        -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
    return d;
}

struct DynSpec
{
    float base[dpCount];         // 全部のつまみが初期値のときのエンジンの値
    DynKnobTable knobs[kMaxKnobs];
    float knobDef[kMaxKnobs];    // つまみの初期値 0..1
    int nKnobs = 0;
};

class DynEngine : public Module
{
public:
    explicit DynEngine (const DynSpec& s) : spec (s)
    {
        for (int i = 0; i < kMaxKnobs; ++i) k[i] = spec.knobDef[i];
        for (int i = 0; i < dpCount; ++i) p[i] = spec.base[i];
        // 飽和の段があるか（どこかの設定で使うなら、いつも 4 倍の中を通す。ドライも同じフィルターを通して、混ぜたときにずれない）
        useSat1 = usesParam (dpSat1); useSat2 = usesParam (dpSat2);
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* s : { &sDry, &sWet, &sSat1, &sSat2, &sBias1, &sBias2, &sLow, &sHigh, &sThr, &sKnee, &sSlope, &sExpThr, &sExpRange, &sFrac2 })
            s->prepare (sr, 0.02);
        // 音量（入口・MAKEUP・出口、dB でならす）は少しゆっくり、同じ速さで（INPUT を一気に動かしてもコンプが追いつく）
        sPre.prepare (sr, 0.06); sMake.prepare (sr, 0.06); sOut.prepare (sr, 0.06);
        dirty = true; recalc (true);
        lookBuf.assign (2 * ((size_t) (0.05 * sr) + 2), 0.0f);   // ステレオ交互 = 2 × 50 ms
        for (auto& d : dryBuf) d.assign ((size_t) (0.05 * sr) + 2, 0.0f);
        reset();
    }
    void reset() override
    {
        gC = gC2 = 0.0f; gG = 0.0f; rmsP = 0.0f; holdT = 0.0f; holdCnt = 0; gHoldCnt = 0;
        for (auto& b : scHp) b.reset();
        for (auto& b : lowSh) b.reset();
        for (auto& b : highSh) b.reset();
        for (auto& o : os1) o.reset();
        for (auto& o : os2) o.reset();
        for (auto& o : dos1) o.reset();
        for (auto& o : dos2) o.reset();
        for (auto& d : dc1) d.reset();
        for (auto& d : dc2) d.reset();
        std::fill (lookBuf.begin(), lookBuf.end(), 0.0f);
        lookPos = 0;
        for (auto& d : dryBuf) std::fill (d.begin(), d.end(), 0.0f);
        dryPos[0] = dryPos[1] = 0;
        meterDb = 0.0f;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= spec.nKnobs || same (k[i], v)) return;
        k[i] = v; dirty = true;
    }
    int latency() const override { return lookN + 4 * ((useSat1 ? 1 : 0) + (useSat2 ? 1 : 0)); }   // 4 倍のフィルター 1 段で約 4 サンプル

    void process (float* L, float* R, int n) override
    {
        if (dirty) recalc (false);
        float* buf = lookBuf.data();
        const int lbN = (int) lookBuf.size() / 2;
        for (int s = 0; s < n; ++s)
        {
            const float inL = L[s], inR = R[s];
            const float pre = dbToGain (sPre.next());
            float l = inL * pre, r = inR * pre;
            // 前の飽和
            const float d1 = sSat1.next(), b1 = sBias1.next();
            if (useSat1) { const auto k1 = shapeK (d1, b1); l = dc1[0].hp (shape4 (os1[0], l, k1)); r = dc1[1].hp (shape4 (os1[1], r, k1)); }
            // 検出
            float cl = l, cr = r;
            if (scOn) { cl = scHp[0].process (cl); cr = scHp[1].process (cr); }
            float lev;
            if (rmsOn)
            {
                const float pw = 0.5f * (cl * cl + cr * cr);
                rmsP = flush (pw + (rmsP - pw) * rmsA);
                lev = powerDb (2.0f * rmsP);
            }
            else lev = levelDb (std::max (std::abs (cl), std::abs (cr)));
            // コンプ（しきい値・ニー・傾きはなめらかに動かす。つまみを回しても下げる量が飛ばない）
            const float thrS = sThr.next(), kneeS = sKnee.next(), slopeS = sSlope.next();
            float t = std::max (-p[dpRange], compGainDbSlope (lev, thrS, slopeS, kneeS));
            if (lookN > 0)
            {
                // 先読みの間は、いちばん深い下げ量を保つ（音が遅れて届く間に戻さない）
                if (t <= holdT) { holdT = t; holdCnt = lookN; }
                else if (holdCnt > 0) --holdCnt;
                else holdT = t;
                t = holdT;
            }
            {
                const float a1 = t < gC ? attA : relA, a2 = t < gC2 ? attA : rel2A;
                gC = t + (gC - t) * a1; gC2 = t + (gC2 - t) * a2;   // 2 段目もいつも回す（割合を変えても飛ばない）
            }
            const float f2 = sFrac2.next();
            float gDb = (1.0f - f2) * gC + f2 * gC2;
            // ゲート
            if (gateOn || gG < -0.001f)
            {
                const float eThr = sExpThr.next(), eRange = sExpRange.next();
                float gt = 0.0f;
                if (gateOn && lev < eThr)
                {
                    if (gHoldCnt > 0) --gHoldCnt;                                  // 下回っても HOLD の間は開いたまま
                    else gt = std::max (-eRange, (lev - eThr) * expSlope);
                }
                else gHoldCnt = gHoldN;
                const float a = gt < gG ? gRelA : gAttA;   // 閉じる（下がる）= RELEASE、開く = ATTACK
                gG = gt + (gG - gt) * a;
                gDb += gG;
            }
            meterDb = gDb;
            // 先読み（音だけ遅らせる）
            if (lookN > 0)
            {
                const int rp = (lookPos - lookN + lbN) % lbN;
                buf[2 * lookPos] = l; buf[2 * lookPos + 1] = r;
                l = buf[2 * rp]; r = buf[2 * rp + 1];
                if (++lookPos >= lbN) lookPos = 0;
            }
            const float g = dbToGain (gDb + sMake.next());
            l *= g; r *= g;
            // 後の飽和
            const float d2 = sSat2.next(), b2 = sBias2.next();
            if (useSat2) { const auto k2 = shapeK (d2, b2); l = dc2[0].hp (shape4 (os2[0], l, k2)); r = dc2[1].hp (shape4 (os2[1], r, k2)); }
            // 音色の棚
            sLow.next(); sHigh.next();
            if (eqOn)
            {
                if (eqMoving) setShelves();   // 動いている間は毎サンプル（ギザギザの音を出さない）
                l = highSh[0].process (lowSh[0].process (l));
                r = highSh[1].process (lowSh[1].process (r));
            }
            const float dry = sDry.next(), wet = sWet.next() * dbToGain (sOut.next());
            float dl = inL, dr = inR;
            // ドライもウェットと同じ 4 倍のフィルターを通す（位相をそろえる）
            if (useSat1) { dl = pass4 (dos1[0], dl); dr = pass4 (dos1[1], dr); }
            if (useSat2) { dl = pass4 (dos2[0], dl); dr = pass4 (dos2[1], dr); }
            if (lookN > 0)
            {
                // ドライも同じだけ遅らせる（ずれない）
                dl = dryDelay (0, dl); dr = dryDelay (1, dr);
            }
            L[s] = wet * l + dry * dl;
            R[s] = wet * r + dry * dr;
        }
    }
    float meter() const override { return std::min (1.0f, -meterDb / 24.0f); }

    // 道具・画面用: 今のエンジンの値
    float param (int i) const { return p[i]; }

protected:
    const DynSpec& spec;
    float k[kMaxKnobs] {};

private:
    // tanh の飽和（小さい音の増幅は 1）。b で偏らせると偶数次の歪み。|d·b| は 1.5 まで（偏りが大きいと片側が何倍にもなるため）
    struct ShapeK { float d, b, tb, inv; };
    static ShapeK shapeK (float d, float b)
    {
        d = std::max (d, 1.0e-6f);
        b = std::clamp (b, -1.5f / d, 1.5f / d);
        const float tb = std::tanh (d * b);
        return { d, b, tb, 1.0f / std::max (1.0e-6f, d * (1.0f - tb * tb)) };
    }
    static float shape (float x, const ShapeK& k) { return (std::tanh (k.d * (x + k.b)) - k.tb) * k.inv; }
    static float shape4 (Oversampler4& o, float x, const ShapeK& k)
    {
        float u[4]; o.up (x, u);
        for (auto& v : u) v = shape (v, k);
        return o.down (u);
    }
    static float pass4 (Oversampler4& o, float x)
    {
        float u[4]; o.up (x, u);
        return o.down (u);
    }
    bool usesParam (int idx) const
    {
        if (spec.base[idx] > -40.0f) return true;
        for (int i = 0; i < spec.nKnobs; ++i)
        {
            const auto& t = spec.knobs[i];
            for (int j = 0; j < t.nParams; ++j)
                if (t.params[j] == idx)
                    for (int q = 0; q < t.points; ++q)
                        if (t.vals[q][j] > -40.0f) return true;
        }
        return false;
    }
    float dryDelay (int ch, float x)
    {
        // 先読みと同じだけドライを遅らせる（バッファーは prepare で確保済み。オーディオスレッドで確保しない）
        auto& db = dryBuf[ch];
        const int len = (int) db.size();
        if (len < 2) return x;
        db[(size_t) dryPos[ch]] = x;
        const int rp = (dryPos[ch] - lookN + len) % len;
        if (++dryPos[ch] >= len) dryPos[ch] = 0;
        return db[(size_t) rp];
    }
    void recalc (bool snap)
    {
        dirty = false;
        for (int i = 0; i < dpCount; ++i) p[i] = spec.base[i];
        for (int i = 0; i < spec.nKnobs; ++i)
        {
            const auto& t = spec.knobs[i];
            if (t.points == 0) continue;
            for (int j = 0; j < t.nParams; ++j)
                p[t.params[j]] += dynKnobValue (t, j, k[i]) - dynKnobValue (t, j, spec.knobDef[i]);
        }
        ratio = std::pow (2.0f, std::max (0.0f, p[dpRatio]));
        attA = tauCoef (sr, std::pow (10.0, p[dpAtt]));
        relA = tauCoef (sr, std::pow (10.0, p[dpRel]));
        rel2A = tauCoef (sr, std::pow (10.0, p[dpRel2]));
        sFrac2.set (std::clamp (p[dpRel2Frac], 0.0f, 1.0f), snap);
        rmsOn = p[dpRms] > -4.0f;
        rmsA = tauCoef (sr, std::pow (10.0, p[dpRms]));
        scOn = p[dpScHp] > 1.3f;
        if (scOn) for (auto& b : scHp) b.setHP (sr, std::pow (10.0, p[dpScHp]), 0.7071);
        gateOn = p[dpExpRatio] > 0.01f && (p[dpExpRange] > 0.1f || sExpRange.cur > 0.1f);
        expSlope = std::pow (2.0f, p[dpExpRatio]) - 1.0f;
        gAttA = tauCoef (sr, std::pow (10.0, p[dpGAtt]));
        gRelA = tauCoef (sr, std::pow (10.0, p[dpGRel]));
        gHoldN = (int) std::lround (std::pow (10.0, std::clamp (p[dpGHold], -4.0f, 1.0f)) * sr);
        for (auto& d : dc1) d.setLP (sr, 5.0);
        for (auto& d : dc2) d.setLP (sr, 5.0);
        lookN = std::clamp ((int) std::lround (p[dpLook] * 0.001 * sr), 0, (int) (0.045 * sr));
        // 入口・MAKEUP・出口は dB のままならす（逆向きに動く 2 つを倍率でならすと途中でふくらむ。DIRT の ON/OFF、INPUT を一気に）
        sPre.set (p[dpPre], snap);
        sMake.set (p[dpMakeup], snap);
        sOut.set (p[dpOut], snap);
        sDry.set (p[dpDry], snap);
        sWet.set (p[dpWet], snap);
        sSat1.set (dbToGain (p[dpSat1]), snap); sBias1.set (p[dpBias1], snap);
        sSat2.set (dbToGain (p[dpSat2]), snap); sBias2.set (p[dpBias2], snap);
        sLow.set (p[dpLow], snap); sHigh.set (p[dpHigh], snap);
        sThr.set (p[dpThr], snap); sKnee.set (std::max (0.0f, p[dpKnee]), snap); sSlope.set (1.0f / ratio - 1.0f, snap);
        sExpThr.set (p[dpExpThr], snap); sExpRange.set (std::max (0.0f, p[dpExpRange]), snap);
        eqOn = std::abs (p[dpLow]) > 0.01f || std::abs (p[dpHigh]) > 0.01f || std::abs (sLow.cur) > 0.01f || std::abs (sHigh.cur) > 0.01f;
        eqMoving = true;
        setShelves();
    }
    void setShelves()
    {
        for (auto& b : lowSh) b.setLowShelf (sr, 100.0, sLow.cur, 0.7);
        for (auto& b : highSh) b.setHighShelf (sr, 8000.0, sHigh.cur, 0.7);
        eqMoving = ! (sLow.settled() && sHigh.settled());
    }

    double sr = 48000.0;
    float p[dpCount] {};
    bool dirty = true;
    float ratio = 1.0f, attA = 0, relA = 0, rel2A = 0, rmsA = 0, expSlope = 0, gAttA = 0, gRelA = 0;
    bool rmsOn = false, scOn = false, gateOn = false, eqOn = false, eqMoving = false;
    float gC = 0, gC2 = 0, gG = 0, rmsP = 0, meterDb = 0, holdT = 0;
    int holdCnt = 0, gHoldN = 0, gHoldCnt = 0;
    int lookN = 0, lookPos = 0;
    std::vector<float> lookBuf;
    std::vector<float> dryBuf[2];
    int dryPos[2] {};
    Biquad scHp[2], lowSh[2], highSh[2];
    Oversampler4 os1[2], os2[2], dos1[2], dos2[2];
    bool useSat1 = false, useSat2 = false;
    OnePole dc1[2], dc2[2];
    Smooth sPre, sMake, sOut, sDry, sWet, sSat1, sSat2, sBias1, sBias2, sLow, sHigh, sThr, sKnee, sSlope, sExpThr, sExpRange, sFrac2;
};
} // namespace aiofx
