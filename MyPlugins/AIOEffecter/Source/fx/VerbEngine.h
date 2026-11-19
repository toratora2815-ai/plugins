#pragma once
// 響き系の共通エンジン（表で動く）。2026-11-19
//
// 部品（Raum など）ごとに「つまみ → エンジンの値」の表を持つ。表は Guitar Rig の記録（つまみを 1 本ずつ動かした記録）から
// 自動で合わせ込んだもの（build/aiofx/vfit → VerbTables.h）。つまみの効き目は「初期値からの差」を足し合わせる。
// 波形そのものは本物と違うが、響きの始まり・帯域ごとの長さ・量・音色・前の濃さ・左右の広がりを合わせる。
//
// 流れ: 入口（L + R）→ 予備の遅れ（くり返しつき）→ ┬ こだま（そのまま）
//                                                   ├ 初期反射（16 本のタップ、左右で別の時刻）
//                                                   └ 入口の拡散（オールパス 4 段）→ 8 本の遅れ線の網（FDN。低・中・高で消え方を変える・揺れ）
//        → こだま × ECHO ＋ 初期反射 × ER ＋ 後の響き × LATE → ローカット・ハイカット・音色 3 つ → 広がり → × WET ＋ ドライ × DRY
#include "DynKit.h"
#include "KnobTable.h"
#include <array>

namespace aiofx
{
enum VerbP : int
{
    vpPre,       // 予備の遅れ ms
    vpPreFb,     // 予備の遅れのくり返し 0..0.95
    vpDiff,      // 入口の拡散の強さ（オールパスの係数 0..0.85）
    vpDiffSize,  // 入口の拡散の長さの倍率
    vpEr,        // 初期反射の量（直線）
    vpErSize,    // 初期反射の広がり ms
    vpLate,      // 後の響きの量（直線）
    vpSize,      // 後の響きの遅れ線の長さの倍率
    vpT60,       // 中域の T60 log10 秒
    vpLowR,      // 低域の T60 ÷ 中域の T60 の log2
    vpHighR,     // 高域の T60 ÷ 中域の T60 の log2
    vpXLo,       // 低域の境目 log10 Hz
    vpXHi,       // 高域の境目 log10 Hz
    vpModD,      // 揺れの深さ ms
    vpModR,      // 揺れの速さ log10 Hz
    vpLoCut,     // 響きのローカット log10 Hz（1.3 以下 = なし）
    vpHiCut,     // 響きのハイカット log10 Hz（4.3 以上 = なし）
    vpEqLo,      // 響きの音色: 低い棚 dB（周波数は vpEqLoF）
    vpEqMid,     // 中の山 dB（vpEqMidF・vpEqMidQ）
    vpEqHi,      // 高い棚 dB（vpEqHiF）
    vpWidth,     // 響きの広がり（0 = モノ、1 = 左右別々）
    vpWet,       // 響きの量 dB
    vpDry,       // ドライの量（直線）
    vpFreeze,    // 0.5 以上 = 響きを止めずに鳴らし続ける（入口は閉じる）
    vpEcho,      // 予備の遅れの出口（くり返しのこだま）をそのまま響きに混ぜる量（直線）
    vpVDelay,    // 響き（初期反射・後の響き）だけの遅れ ms（こだまは通らない。部品の中の遅れ）
    vpEqLoF,     // 音色の低い棚の周波数 log10 Hz（初期 250 Hz）
    vpEqMidF,    // 中の山の周波数 log10 Hz（初期 1.5 kHz）
    vpEqMidQ,    // 中の山の Q の log10（初期 0.7）
    vpEqHiF,     // 高い棚の周波数 log10 Hz（初期 5 kHz）
    vpReverse,   // 0.5 以上 = 逆回しの響き（長さ vpRevLen の間だんだん大きくなって切れる）
    vpRevLen,    // 逆回しの響きの長さ ms
    vpBand,      // 響きの帯域の上限 log10 Hz（4 次のバターワース、24 dB/オクターブ。4.3 以上 = なし）。昔のデジタル機材の狭い帯域
    vpCount
};

inline const float* verbDefaults()
{
    static const float d[vpCount] {
        0.0f, 0.0f, 0.6f, 1.0f, 0.5f, 60.0f, 1.0f, 1.0f, 0.2f, 0.0f, -0.5f, 2.4f, 3.6f,
        0.3f, -0.3f, 1.0f, 4.4f, 0.0f, 0.0f, 0.0f, 1.0f, -10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 2.39794f, 3.17609f, -0.1549f, 3.69897f, 0.0f, 1850.0f, 4.4f };
    return d;
}

struct VerbSpec
{
    float base[vpCount];         // 全部のつまみが初期値のときのエンジンの値
    KnobTable knobs[kMaxKnobs];
    float knobDef[kMaxKnobs];
    int nKnobs = 0;
};

// 1 次の棚（低い所 gDC、高い所 gHF、中点 = fc）。K = 1 / tan(π fc / sr)。
// TPT（台形積分）の 1 次ローパスで y = gHF·x + (gDC − gHF)·LP(x) と作る（極は (1 − g) / (1 + g)、g = 1 / (r K)、r = √(gDC / gHF)）。
// 0 Hz の大きさはいつもちょうど gDC、ナイキストはちょうど gHF。前の (b0 + b1 z⁻¹) / (1 + a1 z⁻¹) の形と同じ伝達関数だが、
// あちらは角が低いと 0 Hz の近くで係数の丸め誤差が 60〜250 倍になり、FREEZE 中に 1 周が 1.00001 を超えていた（DSP 検査 0.3.0 の 2 回目）
struct Shelf1
{
    float G = 0.5f, lo = 1.0f, hi = 1.0f, z = 0.0f;   // 今の値（G = g / (1 + g)）と状態
    float tG = 0.5f, tLo = 1.0f, tHi = 1.0f;          // 目標
    float sG = 0.5f, sLo = 1.0f, sHi = 1.0f;          // つなぎの始まり
    int left = 0, len = 32;
    void set (float K, float gDC, float gHF)
    {
        const float r = std::sqrt (std::max (1.0e-6f, gDC / std::max (1.0e-6f, gHF)));
        const float g = 1.0f / (r * K);
        tG = g / (1.0f + g); tLo = gDC; tHi = gHF;
    }
    // snap = すぐ目標に。そうでなければ n サンプルかけて直線でつなぐ（最後はちょうど目標に着く）
    void ramp (bool snap, float n)
    {
        if (snap) { G = tG; lo = tLo; hi = tHi; left = 0; }
        else { sG = G; sLo = lo; sHi = hi; len = std::max (1, (int) n); left = len; }
    }
    inline void step()
    {
        if (left <= 0) return;
        if (--left == 0) { G = tG; lo = tLo; hi = tHi; return; }
        const float f = (float) (len - left) / (float) len;
        G = sG + f * (tG - sG); lo = sLo + f * (tLo - sLo); hi = sHi + f * (tHi - sHi);
    }
    inline float process (float x) { const float v = (x - z) * G; const float y = v + z; z = flush (y + v); return hi * x + (lo - hi) * y; }
    // 同じ伝達関数の (b0 + b1 z⁻¹) / (1 + a1 z⁻¹) の係数（double。1 周の最大の計算用）。target = 目標、false = 今の値
    void eq (bool target, double& b0, double& b1, double& a1) const
    {
        const double g = target ? tG : G, l = target ? tLo : lo, h = target ? tHi : hi;
        a1 = 2.0 * g - 1.0; b0 = l * g + h * (1.0 - g); b1 = l * g - h * (1.0 - g);
    }
};

// 1 次の 2 段（係数 (b0, b1, a1) × 2）の大きさの 2 乗の、0 Hz〜ナイキストの中の最大をちょうど計算する。
// |H|² は c = cos ω の「2 次式 ÷ 2 次式」なので、最大は両端（c = ±1）か、微分 = 0 の 2 次方程式の根のどれか
// （周波数を何点か調べる形だと、点の間の山を見逃して 96 / 192 kHz で 1 周が 1 を超えていた: DSP 検査 0.3.0 の 2 回目）
inline double maxMag2Pair (double b0, double b1, double a1, double c0, double c1, double e1)
{
    const double A0 = b0 * b0 + b1 * b1, A1 = 2.0 * b0 * b1, B0 = 1.0 + a1 * a1, B1 = 2.0 * a1;
    const double C0 = c0 * c0 + c1 * c1, C1 = 2.0 * c0 * c1, D0 = 1.0 + e1 * e1, D1 = 2.0 * e1;
    const double p0 = A0 * C0, p1 = A0 * C1 + A1 * C0, p2 = A1 * C1, q0 = B0 * D0, q1 = B0 * D1 + B1 * D0, q2 = B1 * D1;
    auto R = [&] (double c) { return (p0 + c * (p1 + c * p2)) / std::max (1.0e-30, q0 + c * (q1 + c * q2)); };
    double m = std::max (R (-1.0), R (1.0));
    const double qa = p2 * q1 - p1 * q2, qb = 2.0 * (p2 * q0 - p0 * q2), qc = p1 * q0 - p0 * q1;   // R' = 0 ⇔ qa c² + qb c + qc = 0
    if (std::abs (qa) > 1.0e-18)
    {
        const double disc = qb * qb - 4.0 * qa * qc;
        if (disc >= 0.0)
        {
            const double sq = std::sqrt (disc);
            for (double c : { (-qb + sq) / (2.0 * qa), (-qb - sq) / (2.0 * qa) })
                if (c > -1.0 && c < 1.0) m = std::max (m, R (c));
        }
    }
    else if (std::abs (qb) > 1.0e-18) { const double c = -qc / qb; if (c > -1.0 && c < 1.0) m = std::max (m, R (c)); }
    return m;
}

// TPT（台形積分）の状態変数フィルター（Cytomic の形）。出口 = m0·入口 + m1·帯域 + m2·低域。
// g・k・m を 32 サンプルかけて直線でつなぐ（係数を動かしてもプチッとしにくい）。LP / HP / ピーク / 低い棚 / 高い棚
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.0f, k = 1.414f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    float tg = 0.0f, tk = 1.414f, tm0 = 1.0f, tm1 = 0.0f, tm2 = 0.0f;
    float dg = 0.0f, dk = 0.0f, dm0 = 0.0f, dm1 = 0.0f, dm2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    static float tanw (double sr, double f) { return (float) std::tan (kPiD * std::clamp (f, 5.0, 0.49 * sr) / sr); }
    void lp (double sr, double f, double q) { tg = tanw (sr, f); tk = (float) (1.0 / q); tm0 = 0.0f; tm1 = 0.0f; tm2 = 1.0f; }
    void hp (double sr, double f, double q) { tg = tanw (sr, f); tk = (float) (1.0 / q); tm0 = 1.0f; tm1 = -tk; tm2 = -1.0f; }
    void peak (double sr, double f, double q, double db)
    {
        const float A = (float) std::pow (10.0, db / 40.0);
        tg = tanw (sr, f); tk = (float) (1.0 / (q * A)); tm0 = 1.0f; tm1 = tk * (A * A - 1.0f); tm2 = 0.0f;
    }
    void lowShelf (double sr, double f, double db, double q = 0.70710678)
    {
        const float A = (float) std::pow (10.0, db / 40.0);
        tg = tanw (sr, f) / std::sqrt (A); tk = (float) (1.0 / q); tm0 = 1.0f; tm1 = tk * (A - 1.0f); tm2 = A * A - 1.0f;
    }
    void highShelf (double sr, double f, double db, double q = 0.70710678)
    {
        const float A = (float) std::pow (10.0, db / 40.0);
        tg = tanw (sr, f) * std::sqrt (A); tk = (float) (1.0 / q); tm0 = A * A; tm1 = tk * (1.0f - A) * A; tm2 = 1.0f - A * A;
    }
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    bool moving = false;
    int left = 0;
    void calcA() { a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void ramp (bool snap, float n)
    {
        if (snap) { g = tg; k = tk; m0 = tm0; m1 = tm1; m2 = tm2; dg = dk = dm0 = dm1 = dm2 = 0.0f; moving = false; left = 0; calcA(); }
        else
        {
            dg = (tg - g) / n; dk = (tk - k) / n; dm0 = (tm0 - m0) / n; dm1 = (tm1 - m1) / n; dm2 = (tm2 - m2) / n;
            moving = dg != 0.0f || dk != 0.0f || dm0 != 0.0f || dm1 != 0.0f || dm2 != 0.0f;
            left = std::max (1, (int) n);
        }
    }
    // 動いている間だけ係数を進めて計算し直す（止まっている間は割り算をしない）。n 回で目標にちょうど着いて止まる
    // （止めないと同じ歩みで足し続ける。ramp を毎回呼ばない部品で係数が流れて壊れていた: DSP 検査 0.4.0）
    inline void step()
    {
        if (! moving) return;
        if (--left <= 0) { g = tg; k = tk; m0 = tm0; m1 = tm1; m2 = tm2; moving = false; }
        else { g += dg; k += dk; m0 += dm0; m1 += dm1; m2 += dm2; }
        calcA();
    }
    inline float process (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1); ic2 = flush (2.0f * v2 - ic2);
        return m0 * v0 + m1 * v1 + m2 * v2;
    }
};

class VerbEngine : public Module
{
public:
    explicit VerbEngine (const VerbSpec& s) : spec (s)
    {
        for (int i = 0; i < kMaxKnobs; ++i) k[i] = kReq[i] = spec.knobDef[i];
        computeTargets();
        for (int j = 0; j < vpCount; ++j) ps[j] = p[j];
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        computeTargets();   // 部品ごとの決まり（adjust）は作る途中（コンストラクター）では呼ばれないので、ここで必ず計算し直す
        const double sc = sr / 48000.0;
        auto pow2 = [] (size_t n) { size_t m = 1; while (m < n) m <<= 1; return m; };
        // 予備の遅れ（3 秒）・初期反射（予備の遅れの出口を 0.35 秒ためる）
        preBuf.assign (pow2 ((size_t) (3.05 * sr) + 8), 0.0f); preMask = (int) preBuf.size() - 1;
        erBuf.assign (pow2 ((size_t) (0.9 * sr) + 8), 0.0f); erMask = (int) erBuf.size() - 1;   // 響きだけの遅れ（最大 0.5 秒）＋初期反射の広がり（最大 0.3 秒）
        for (int i = 0; i < 4; ++i) { apBuf[i].assign (pow2 ((size_t) (kAp[i] * sc * 3.2) + 8), 0.0f); apMask[i] = (int) apBuf[i].size() - 1; }
        for (int i = 0; i < 8; ++i)
        {
            baseLen[i] = (float) (kLen[i] * sc);
            dl[i].assign (pow2 ((size_t) (baseLen[i] * 3.1f + 0.011 * sr) + 8), 0.0f); dlMask[i] = (int) dl[i].size() - 1;
        }
        if (usesReverse()) { revBuf.assign (pow2 ((size_t) (4.0 * sr) + 8), 0.0f); revMask = (int) revBuf.size() - 1; }
        else { revBuf.clear(); revMask = 0; }
        aCtl = 1.0f - (float) std::exp (-32.0 / (0.03 * sr));
        // 直流カット（2 次のバターワース 10 Hz）: 63 Hz で −0.003 dB。直流は全部の遅れ線で必ず共振するので、入口で切る（DSP 検査 0.3.0 の 2 回目）
        dcCut.hp (sr, 10.0, 0.70710678); dcCut.ramp (true, 1.0f);
        preFadeStep = 1.0f / (float) (0.04 * sr);
        revFadeStep = 1.0f / (float) (0.03 * sr);
        duckDown = 1.0f / (float) (0.015 * sr); duckUp = 1.0f / (float) (0.04 * sr);
        for (auto* q : { &sWet, &sDry, &sEr, &sLate, &sWidth, &sFrz, &sEcho, &sRev }) q->prepare (sr, 0.02);
        sVDel.prepare (sr, 0.06);
        for (int j = 0; j < vpCount; ++j) ps[j] = p[j];
        snapGains();
        updateCoefs (true);
        reset();
        prepared = true;
    }
    void reset() override
    {
        std::fill (preBuf.begin(), preBuf.end(), 0.0f); std::fill (erBuf.begin(), erBuf.end(), 0.0f);
        for (auto& a : apBuf) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        prePos = erPos = 0; apPos.fill (0); dlPos.fill (0);
        std::fill (revBuf.begin(), revBuf.end(), 0.0f); revPos = 0;
        for (auto& f : lossLo) f.z = 0.0f;
        for (auto& f : lossHi) f.z = 0.0f;
        for (auto& c : svf) for (auto& f : c) f.reset();
        for (int i = 0; i < 8; ++i) { const double ph = 0.785398 * i + 0.3; lfoC[i] = (float) std::cos (ph); lfoS[i] = (float) std::sin (ph); }
        ctl = 0; dcCut.reset(); inG = 1.0f;
        for (int j = 0; j < vpCount; ++j) ps[j] = p[j];
        snapGains(); updateCoefs (true);
        preD[0] = preD[1] = preTarget (); preCur = 0; preFading = false; preF = 0.0f;
        revFading = false; revF = 0.0f; duckState = 0; duckG = 1.0f;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= spec.nKnobs) return;
        kReq[i] = v;
        if (! same (k[i], v))
        {
            // 選ぶつまみ（MODE など）の段の変化はここでは入れず、process の頭で「そのブロックの FREEZE」を見てから決める。
            // FREEZE 中に切り替えると、中身を空にしたあと入口が閉じたままで止めていた音が消えていた（DSP 検査 0.3.0 の 2 回目）。
            // ラックはつまみを番号の小さい順に入れるので、プリセット・UNDO で「選ぶつまみ → FREEZE ON」が同じブロックに来ても待てるように（3 回目）
            if (stepJump (i, v) && prepared) stepPending = true;
            else { k[i] = v; computeTargets(); }
        }
    }
    void setTempo (double bpm) override
    {
        if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; computeTargets(); }
    }
    void process (float* L, float* R, int n) override
    {
        // 待っている選ぶつまみの切り替え: FREEZE が外れていれば、まとめて 1 回のダック（15 ms で下げる → 中身（響きの尾も）を空にして切り替える → 40 ms で戻す）
        if (stepPending && p[vpFreeze] < 0.5f) { stepPending = false; applyPending(); }
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) controlStep();
            // 切り替えのダック（選ぶつまみを変えたとき）
            if (duckState == 1) { duckG -= duckDown; if (duckG <= 0.0f) { duckG = 0.0f; snapAll(); duckState = 3; } }
            else if (duckState == 3) { duckG += duckUp; if (duckG >= 1.0f) { duckG = 1.0f; duckState = 0; } }
            // 長さ・タップ・混ぜ具合を 1 サンプルずつつなぐ（32 サンプルごとの階段にしない）
            for (int i = 0; i < 8; ++i) curLen[i] += lenStep[i];
            for (int t = 0; t < kEr; ++t) { erTL[t] += erStepL[t]; erTR[t] += erStepR[t]; }
            for (int i = 0; i < 4; ++i) apLen[i] += apStep[i];
            mixHP += mixHPStep; mixLP += mixLPStep; mixBand += mixBandStep; apG += apGStep;
            lateNorm += lateNormStep; modDepth += modDepthStep;
            for (int i = 0; i < 8; ++i) { lossG[i] += lossGStep[i]; lossLo[i].step(); lossHi[i].step(); }
            for (int c = 0; c < 2; ++c) for (int j = 0; j < kSvf; ++j) svf[c][j].step();
            const float in = dcCut.process (0.5f * (L[s] + R[s]));   // 網・こだま・逆回しに入る音だけ直流を切る（ドライはそのまま）
            const float frz = sFrz.next();
            // 予備の遅れ（くり返しつき）。長さが変わったら 40 ms で新しい読み口へクロスフェード（音程が鳴き上がらない）
            if (! preFading)
            {
                const float tgtD = preTarget();
                if (std::abs (tgtD - preD[preCur]) > 0.5f) { preD[1 - preCur] = tgtD; preFading = true; preF = 0.0f; }
            }
            float pre = readFrac (preBuf, preMask, prePos, preD[preCur]);
            if (preFading)
            {
                preF += preFadeStep;
                const float pb = readFrac (preBuf, preMask, prePos, preD[1 - preCur]);
                const float f = std::min (1.0f, preF);
                pre += f * (pb - pre);
                if (preF >= 1.0f) { preCur = 1 - preCur; preFading = false; }
            }
            preBuf[(size_t) prePos] = flush (in * (1.0f - frz) + preFb * pre);
            prePos = (prePos + 1) & preMask;
            // 初期反射（響きだけの遅れ vd のあと）。切り替えで中身を空にしたら、入る音も 0 から 40 ms で戻す（急に始まる段差を作らない）。
            // 戻りはダックの状態と別に「上がるだけ」（戻る途中で次の切り替えが来ても飛ばない: DSP 検査 0.3.0 の 2 回目）
            if (inG < 1.0f) inG = std::min (1.0f, inG + duckUp);
            const float preN = pre * inG;
            erBuf[(size_t) erPos] = preN;
            const float vd = sVDel.next();
            float erL = 0.0f, erR = 0.0f;
            for (int t = 0; t < kEr; ++t)
            {
                erL += erG[t] * readFrac (erBuf, erMask, erPos, vd + erTL[t]);
                erR += erGR[t] * readFrac (erBuf, erMask, erPos, vd + erTR[t]);
            }
            // 入口の拡散（オールパス 4 段、長さは DIFF SIZE で変わる）
            float x = vd < 0.5f ? preN : readFrac (erBuf, erMask, erPos, vd);
            erPos = (erPos + 1) & erMask;
            for (int i = 0; i < 4; ++i)
            {
                auto& b = apBuf[i];
                const float d = readFrac (b, apMask[i], apPos[(size_t) i], apLen[i]);
                const float u = x + apG * d;
                b[(size_t) apPos[(size_t) i]] = flush (u);
                x = d - apG * u;
                apPos[(size_t) i] = (apPos[(size_t) i] + 1) & apMask[i];
            }
            // 8 本の FDN（アダマール行列）。読む位置は揺れで少し動く
            float o[8];
            for (int i = 0; i < 8; ++i)
            {
                // 揺れ（回る針。32 サンプルごとに長さを 1 に戻す）
                const float c = lfoC[i] * lfoRc[i] - lfoS[i] * lfoRs[i];
                lfoS[i] = lfoS[i] * lfoRc[i] + lfoC[i] * lfoRs[i]; lfoC[i] = c;
                const float d = std::max (2.0f, curLen[i] + modDepth * lfoS[i]);
                float v = readFrac (dl[i], dlMask[i], dlPos[(size_t) i], d);
                v = lossHi[i].process (lossLo[i].process (v)) * lossG[i];
                o[i] = v;
            }
            float h[8];
            hadamard (o, h);
            const float inj = x * (1.0f - frz) * 0.5f;
            for (int i = 0; i < 8; ++i)
            {
                dl[i][(size_t) dlPos[(size_t) i]] = flush (h[i] + (kInSign[i] > 0 ? inj : -inj));
                dlPos[(size_t) i] = (dlPos[(size_t) i] + 1) & dlMask[i];
            }
            // 出口の取り出し（アダマール行列の別々の行 = 左右が無相関）
            float lateL = (o[0] - o[1] - o[2] + o[3] + o[4] - o[5] - o[6] + o[7]) * lateNorm;
            float lateR = (o[0] - o[1] + o[2] - o[3] - o[4] + o[5] - o[6] + o[7]) * lateNorm;
            const float eA = sEr.next(), lA = sLate.next(), cA = sEcho.next();
            float wl = cA * pre + eA * erL + lA * lateL, wr = cA * pre + eA * erR + lA * lateR;
            // 逆回しの響き（拡散した入口を、だんだん大きくなるタップで読む）。タップを作り直したら新旧を 30 ms で入れ替える
            if (revMask > 0)
            {
                revBuf[(size_t) revPos] = x;
                const float rv = sRev.next();
                if (rv > 1.0e-4f)
                {
                    float rl = 0.0f, rr = 0.0f;
                    revSum (revSet, rl, rr);
                    if (revFading)
                    {
                        float nl = 0.0f, nr = 0.0f;
                        revSum (1 - revSet, nl, nr);
                        const float f = std::min (1.0f, revF);
                        rl += f * (nl - rl); rr += f * (nr - rr);
                    }
                    const float g = revLevel * (eA + lA);
                    wl += rv * (g * rl - wl); wr += rv * (g * rr - wr);
                }
                if (revFading) { revF += revFadeStep; if (revF >= 1.0f) { revSet = 1 - revSet; revFading = false; } }
                revPos = (revPos + 1) & revMask;
            }
            // 音色（ローカット・ハイカット・帯域の上限はいつも計算して、効き具合を混ぜる = 入る／外れるでプチッとしない）
            {
                float w[2] { wl, wr };
                for (int c = 0; c < 2; ++c)
                {
                    auto* f = svf[c];
                    float v = w[c];
                    v += mixHP * (f[0].process (v) - v);
                    v += mixLP * (f[1].process (v) - v);
                    v = f[4].process (f[3].process (f[2].process (v)));
                    v += mixBand * (f[6].process (f[5].process (v)) - v);
                    w[c] = v;
                }
                wl = w[0]; wr = w[1];
            }
            // 広がり
            const float wd = sWidth.next();
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * wd;
            wl = m + sd; wr = m - sd;
            const float wg = sWet.next() * duckG, dg = sDry.next();
            L[s] = dg * L[s] + wg * wl;
            R[s] = dg * R[s] + wg * wr;
        }
    }
    double tailSeconds() const override { return std::min (30.0, 1.5 * std::pow (10.0, (double) std::max (ps[vpT60], p[vpT60])) + 0.5); }

    // つまみの値 → エンジンの値（表の読み。部品の文字の表示にも使う）
    float param (int j) const { return p[j]; }

protected:
    // 部品ごとの特別な決まり（SYNC で予備の遅れを音符にする、など）。表を足したあとに呼ぶ
    virtual void adjust (float* /*prm*/, const float* /*knobs*/) {}
    // 逆回しの響きを使う部品だけ true（4 秒のバッファーを用意する）
    virtual bool usesReverse() const { return false; }
    double tempo = 120.0;
    float k[kMaxKnobs] {};

private:
    static constexpr int kEr = 16, kRev = 96;
    static constexpr int kLen[8] { 1559, 1687, 1801, 1931, 2053, 2203, 2347, 2503 };
    static constexpr int kAp[4] { 142, 107, 379, 277 };
    static constexpr int kInSign[8] { 1, -1, 1, -1, 1, 1, -1, -1 };

    static void hadamard (const float* a, float* b)
    {
        float t[8];
        for (int i = 0; i < 8; i += 2) { t[i] = a[i] + a[i + 1]; t[i + 1] = a[i] - a[i + 1]; }
        float u[8];
        for (int i = 0; i < 8; i += 4) { u[i] = t[i] + t[i + 2]; u[i + 1] = t[i + 1] + t[i + 3]; u[i + 2] = t[i] - t[i + 2]; u[i + 3] = t[i + 1] - t[i + 3]; }
        for (int i = 0; i < 4; ++i) { b[i] = (u[i] + u[i + 4]) * 0.35355339f; b[i + 4] = (u[i] - u[i + 4]) * 0.35355339f; }
    }
    // 書いた位置 pos から d サンプル前を直線補間で読む（d ≥ 1）
    static inline float readFrac (const std::vector<float>& b, int mask, int pos, float d)
    {
        const int di = (int) d; const float f = d - (float) di;
        const float a = b[(size_t) ((pos - di) & mask)], c = b[(size_t) ((pos - di - 1) & mask)];
        return a + f * (c - a);
    }

    void computeTargets()
    {
        for (int j = 0; j < vpCount; ++j) p[j] = spec.base[j];
        for (int i = 0; i < spec.nKnobs; ++i)
        {
            const auto& t = spec.knobs[i];
            if (t.points == 0) continue;
            for (int j = 0; j < t.nParams; ++j)
                p[t.params[j]] += knobValue (t, j, k[i]) - knobValue (t, j, spec.knobDef[i]);
        }
        adjust (p, k);
        clampParams (p);
        p[vpFreeze] = p[vpFreeze] >= 0.5f ? 1.0f : 0.0f;
        sWet.set (dbToGain (p[vpWet])); sDry.set (p[vpDry]); sEr.set (p[vpEr]); sLate.set (p[vpLate]);
        sWidth.set (p[vpWidth]); sFrz.set (p[vpFreeze] >= 0.5f ? 1.0f : 0.0f); sEcho.set (p[vpEcho]); sRev.set (p[vpReverse]);
        sVDel.set (p[vpVDelay] * 0.001f * (float) sr);
    }
    static void clampParams (float* q)
    {
        q[vpPre] = std::clamp (q[vpPre], 0.0f, 3000.0f);     q[vpPreFb] = std::clamp (q[vpPreFb], 0.0f, 0.95f);
        q[vpDiff] = std::clamp (q[vpDiff], 0.0f, 0.85f);     q[vpDiffSize] = std::clamp (q[vpDiffSize], 0.1f, 3.0f);
        q[vpEr] = std::clamp (q[vpEr], 0.0f, 8.0f);          q[vpErSize] = std::clamp (q[vpErSize], 2.0f, 300.0f);
        q[vpLate] = std::clamp (q[vpLate], 0.0f, 8.0f);      q[vpSize] = std::clamp (q[vpSize], 0.2f, 3.0f);
        q[vpT60] = std::clamp (q[vpT60], -1.3f, 2.0f);       q[vpLowR] = std::clamp (q[vpLowR], -4.0f, 4.0f);
        q[vpHighR] = std::clamp (q[vpHighR], -5.0f, 2.0f);   q[vpXLo] = std::clamp (q[vpXLo], 1.7f, 3.3f);
        q[vpXHi] = std::clamp (q[vpXHi], 2.7f, 4.2f);        q[vpModD] = std::clamp (q[vpModD], 0.0f, 10.0f);
        q[vpModR] = std::clamp (q[vpModR], -2.0f, 1.0f);     q[vpLoCut] = std::clamp (q[vpLoCut], 1.0f, 3.6f);
        q[vpHiCut] = std::clamp (q[vpHiCut], 2.5f, 4.5f);    q[vpEqLo] = std::clamp (q[vpEqLo], -30.0f, 24.0f);
        q[vpEqMid] = std::clamp (q[vpEqMid], -30.0f, 24.0f); q[vpEqHi] = std::clamp (q[vpEqHi], -30.0f, 24.0f);
        q[vpWidth] = std::clamp (q[vpWidth], -2.0f, 2.0f);   q[vpWet] = std::clamp (q[vpWet], -80.0f, 30.0f);   // 広がりが負 = 左右入れ替え
        q[vpDry] = std::clamp (q[vpDry], -16.0f, 16.0f);       q[vpEcho] = std::clamp (q[vpEcho], 0.0f, 8.0f);
        q[vpVDelay] = std::clamp (q[vpVDelay], 0.0f, 500.0f);
        q[vpEqLoF] = std::clamp (q[vpEqLoF], 1.3f, 3.5f);    q[vpEqMidF] = std::clamp (q[vpEqMidF], 1.7f, 4.2f);
        q[vpEqMidQ] = std::clamp (q[vpEqMidQ], -1.0f, 1.3f); q[vpEqHiF] = std::clamp (q[vpEqHiF], 2.5f, 4.25f);
        q[vpReverse] = q[vpReverse] >= 0.5f ? 1.0f : 0.0f;   q[vpRevLen] = std::clamp (q[vpRevLen], 50.0f, 3900.0f);
        q[vpBand] = std::clamp (q[vpBand], 3.0f, 4.5f);
    }
    void snapGains()
    {
        sWet.set (dbToGain (p[vpWet]), true); sDry.set (p[vpDry], true); sEr.set (p[vpEr], true); sLate.set (p[vpLate], true);
        sWidth.set (p[vpWidth], true); sFrz.set (p[vpFreeze] >= 0.5f ? 1.0f : 0.0f, true); sEcho.set (p[vpEcho], true); sRev.set (p[vpReverse], true);
        sVDel.set (p[vpVDelay] * 0.001f * (float) sr, true);
    }
    // 32 サンプルごと: なめらかにした値を目標へ近づけ、動いていればフィルターを計算し直す
    void controlStep()
    {
        bool moved = false;
        for (int i = 0; i < 8; ++i) lossG[i] = lossGT[i];   // 前のつなぎ（32 サンプル）はここで終わっている → ちょうど目標に
        if (duckState == 1)   // 切り替えのダックで下げている間は、古い値のまま止めておく（底で一気に切り替える）
        {
            for (int i = 0; i < 8; ++i) { lenStep[i] = 0.0f; lossGStep[i] = 0.0f; lossLo[i].ramp (true, 1.0f); lossHi[i].ramp (true, 1.0f); }
            for (int t = 0; t < kEr; ++t) erStepL[t] = erStepR[t] = 0.0f;
            for (int i = 0; i < 4; ++i) apStep[i] = 0.0f;
            mixHPStep = mixLPStep = mixBandStep = apGStep = lateNormStep = modDepthStep = 0.0f;
            for (auto& c : svf) for (auto& f : c) f.ramp (true, 1.0f);
            return;
        }
        for (int j = 0; j < vpCount; ++j)
        {
            const float d = p[j] - ps[j];
            if (std::abs (d) > 1.0e-5f) { ps[j] += d * aCtl; moved = true; }
            else ps[j] = p[j];
        }
        if (moved || first) updateCoefs (false);
        else
        {
            for (int i = 0; i < 8; ++i) { lenStep[i] = 0.0f; lossGStep[i] = 0.0f; lossLo[i].ramp (true, 1.0f); lossHi[i].ramp (true, 1.0f); }
            for (int t = 0; t < kEr; ++t) erStepL[t] = erStepR[t] = 0.0f;
            for (int i = 0; i < 4; ++i) apStep[i] = 0.0f;
            mixHPStep = mixLPStep = mixBandStep = apGStep = lateNormStep = modDepthStep = 0.0f;
            for (auto& c : svf) for (auto& f : c) f.ramp (true, 1.0f);
        }
        first = false;
        // 揺れの針の長さを 1 に戻す
        for (int i = 0; i < 8; ++i)
        {
            const float m = 1.0f / std::sqrt (lfoC[i] * lfoC[i] + lfoS[i] * lfoS[i] + 1.0e-12f);
            lfoC[i] *= m; lfoS[i] *= m;
        }
    }
    // snap = true: 長さ・混ぜ具合をすぐ目標にする（準備・リセット・切り替えのダックの底）。false: 32 サンプルかけてつなぐ
    void updateCoefs (bool snap)
    {
        const float fsr = (float) sr;
        const float frz = std::clamp (ps[vpFreeze], 0.0f, 1.0f);   // なめらかにした FREEZE（消え方を 30 ms で止める）
        // 予備の遅れのくり返し: 遅れが短い（10 ms 未満）ときは弱める（1 サンプルの輪で低音が +26 dB になっていた。本物も予備の遅れ 0 では効かない）
        preFb = ps[vpPreFb] * (1.0f - frz) * std::clamp ((ps[vpPre] - 1.0f) / 9.0f, 0.0f, 1.0f);
        // 初期反射: 広がり ms の中に 16 本、だんだん小さく、符号を交互に（左右で時刻と符号の並びを変える）
        {
            const float span = ps[vpErSize] * 0.001f * fsr;
            float e2 = 0.0f;
            for (int t = 0; t < kEr; ++t)
            {
                const float u = std::pow ((t + 0.5f) / kEr, 1.4f);
                const float tl = std::min ((float) erMask - 0.505f * fsr, 1.0f + span * u);
                const float tr = std::min ((float) erMask - 0.505f * fsr, 1.0f + span * u * 1.07f + 0.00037f * fsr);
                if (snap) { erTL[t] = tl; erTR[t] = tr; erStepL[t] = erStepR[t] = 0.0f; }
                else { erStepL[t] = (tl - erTL[t]) / 32.0f; erStepR[t] = (tr - erTR[t]) / 32.0f; }
                const float g = std::exp (-3.0f * u);
                erG[t] = ((t % 2) ? -g : g); erGR[t] = ((t % 3) == 1 ? -g : g);
                e2 += g * g;
            }
            const float nrm = 1.0f / std::sqrt (std::max (1.0e-6f, e2));
            for (int t = 0; t < kEr; ++t) { erG[t] *= nrm; erGR[t] *= nrm; }
        }
        // 入口の拡散
        if (snap) { apG = ps[vpDiff]; apGStep = 0.0f; } else apGStep = (ps[vpDiff] - apG) / 32.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float al = std::clamp ((float) (kAp[i] * sr / 48000.0) * ps[vpDiffSize], 1.0f, (float) apMask[i] - 4.0f);
            if (snap) { apLen[i] = al; apStep[i] = 0.0f; } else apStep[i] = (al - apLen[i]) / 32.0f;
        }
        // FDN: 長さ・揺れ・帯域ごとの消え方
        const float size = ps[vpSize];
        {
            const float md = ps[vpModD] * 0.001f * fsr;
            if (snap) { modDepth = md; modDepthStep = 0.0f; } else modDepthStep = (md - modDepth) / 32.0f;
        }
        const float rate = std::pow (10.0f, ps[vpModR]);
        float meanLen = 0.0f, tgtLen[8];
        for (int i = 0; i < 8; ++i)
        {
            tgtLen[i] = std::min (baseLen[i] * size, (float) dlMask[i] - 10.0e-3f * fsr - 4.0f);
            if (snap) { curLen[i] = tgtLen[i]; lenStep[i] = 0.0f; } else lenStep[i] = (tgtLen[i] - curLen[i]) / 32.0f;
            meanLen += tgtLen[i] / 8.0f;
            const float w = 2.0f * (float) kPiD * rate * (1.0f + 0.13f * ((float) i - 3.5f) / 3.5f) / fsr;
            lfoRc[i] = std::cos (w); lfoRs[i] = std::sin (w);
        }
        const float Tm = std::pow (10.0f, ps[vpT60]);
        const float Tl = Tm * std::pow (2.0f, ps[vpLowR]), Th = Tm * std::pow (2.0f, ps[vpHighR]);
        const float Klo = 1.0f / std::tan ((float) kPiD * std::min (std::pow (10.0f, ps[vpXLo]), 0.45f * fsr) / fsr);
        const float Khi = 1.0f / std::tan ((float) kPiD * std::min (std::pow (10.0f, ps[vpXHi]), 0.45f * fsr) / fsr);
        for (int i = 0; i < 8; ++i)
        {
            const float dsec = std::max (tgtLen[i], curLen[i]) / fsr;   // 長いほうで（つなぎの途中も 1 周の大きさが 1 を超えない）
            float gm = std::pow (10.0f, -3.0f * dsec / Tm);
            float al = std::pow (10.0f, -3.0f * dsec * (1.0f / Tl - 1.0f / Tm));
            float bh = std::pow (10.0f, -3.0f * dsec * (1.0f / Th - 1.0f / Tm));
            al = std::min (al, 1.0f / gm); bh = std::min (bh, 1.0f / gm);
            gm += (1.0f - gm) * frz; al += (1.0f - al) * frz; bh += (1.0f - bh) * frz;
            lossLo[i].set (Klo, al, 1.0f);
            lossHi[i].set (Khi, 1.0f, bh);
            // どの周波数でも 1 周の大きさ ≤ 1（低い棚と高い棚の持ち上げが重なると 1 を超えて発振していた。DSP 検査 0.3.0）。最大はちょうど計算する
            double l0, l1, la, h0, h1, ha;
            lossLo[i].eq (true, l0, l1, la); lossHi[i].eq (true, h0, h1, ha);
            const float mxT = (float) std::sqrt (maxMag2Pair (l0, l1, la, h0, h1, ha));
            const float lim = frz > 0.999f ? 0.99995f : 0.9995f;
            if (gm * mxT > lim) gm = lim / mxT;
            // つなぎの途中も 1 を超えないよう、大きさは下げるときはすぐ、上げるときだけ 32 サンプルかけて。
            // 上げるときは、つなぐ前の棚（今の係数）の最大でも押さえる（大きさが上がるのと棚が下がるのが重なると 1.0001 まで出ていた）
            if (! snap && gm > lossG[i])
            {
                lossLo[i].eq (false, l0, l1, la); lossHi[i].eq (false, h0, h1, ha);
                const float mxC = (float) std::sqrt (maxMag2Pair (l0, l1, la, h0, h1, ha));
                if (gm * mxC > lim) gm = std::max (lossG[i], lim / mxC);
            }
            lossGT[i] = gm;
            if (snap || gm <= lossG[i]) { lossG[i] = gm; lossGStep[i] = 0.0f; } else lossGStep[i] = (gm - lossG[i]) / 32.0f;
            lossLo[i].ramp (snap, 32.0f); lossHi[i].ramp (snap, 32.0f);
        }
        // 逆回しの響きのタップ: 長さ T の中に、終わりに近いほど密に。大きさは T60 で終わりへ向かって上がる（60 dB / T60）
        if (revMask > 0)
        {
            const float T = std::min (ps[vpRevLen] * 0.001f * fsr, (float) revMask - 8.0f);
            const float Trv = std::pow (10.0f, ps[vpT60]) * fsr;
            if ((std::abs (T - revT) > 0.5f || std::abs (Trv - revT60) > 0.002f * Trv) && (snap || ! revFading))
            {
                revT = T; revT60 = Trv;
                const int dst = snap ? revSet : 1 - revSet;   // 入れ替え中でなければ、使っていない組に作って 30 ms で入れ替える
                makeRevTaps (dst, T, Trv);
                if (! snap) { revFading = true; revF = 0.0f; }
            }
        }
        // 後の響きの量: T60 と長さで変わらないように（エネルギー ∝ T60 / 1 周の長さ）
        const float Tn = std::min (Tm, 30.0f);
        {
            const float ln = 0.5f * std::sqrt (13.8f * meanLen / (Tn * fsr));
            if (snap) { lateNorm = ln; lateNormStep = 0.0f; } else lateNormStep = (ln - lateNorm) / 32.0f;
        }
        // 音色
        const float lc = std::pow (10.0f, std::max (ps[vpLoCut], 1.0f)), hc = std::min (std::pow (10.0f, ps[vpHiCut]), 0.45f * fsr);
        // 混ぜる幅は元のしきい値（ローカット 1.31、ハイカット・帯域 4.29）の前後だけ（合わせ込んだ部品の音を変えない）。
        // フィルターはいつも動かしているので、ここを一気にまたいでもプチッとしない（20 Hz・19.5 kHz の端なので差もごく小さい）
        const float tHP = std::clamp ((ps[vpLoCut] - 1.32f) / 0.02f + 0.5f, 0.0f, 1.0f);
        const float tLP = std::clamp ((4.29f - ps[vpHiCut]) / 0.01f + 0.5f, 0.0f, 1.0f);
        if (snap) { mixHP = tHP; mixLP = tLP; mixHPStep = mixLPStep = 0.0f; }
        else { mixHPStep = (tHP - mixHP) / 32.0f; mixLPStep = (tLP - mixLP) / 32.0f; }
        const double fLo = std::pow (10.0, (double) ps[vpEqLoF]), fMid = std::pow (10.0, (double) ps[vpEqMidF]);
        const double qMid = std::pow (10.0, (double) ps[vpEqMidQ]), fHi = std::pow (10.0, (double) ps[vpEqHiF]);
        // 帯域の上限（4 次のバターワース = Q 0.541 と 1.307 の 2 段）
        const double fb = std::min (std::pow (10.0, (double) ps[vpBand]), 0.45 * sr);
        for (int c = 0; c < 2; ++c)
        {
            auto* f = svf[c];
            f[0].hp (sr, lc, 0.70710678); f[1].lp (sr, hc, 0.70710678);
            f[2].lowShelf (sr, fLo, ps[vpEqLo]); f[3].peak (sr, fMid, qMid, ps[vpEqMid]); f[4].highShelf (sr, fHi, ps[vpEqHi]);
            f[5].lp (sr, fb, 0.5412); f[6].lp (sr, fb, 1.3066);
            for (int j = 0; j < kSvf; ++j) f[j].ramp (snap, 32.0f);
        }
        const float tB = std::clamp ((4.29f - ps[vpBand]) / 0.01f + 0.5f, 0.0f, 1.0f);
        if (snap) { mixBand = tB; mixBandStep = 0.0f; } else mixBandStep = (tB - mixBand) / 32.0f;
    }
    // 逆回しのタップを組 dst に作る
    void makeRevTaps (int dst, float T, float Trv)
    {
        uint32_t seed = 0x2545F491u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
        float e2 = 0.0f, e2r = 0.0f;
        for (int t = 0; t < kRev; ++t)
        {
            const float u = std::sqrt ((t + rnd()) / (float) kRev), v = std::sqrt ((t + rnd()) / (float) kRev);
            revTL[dst][t] = std::max (1, (int) (T * u)); revTR[dst][t] = std::max (1, (int) (T * v));
            const float gl = std::exp (-6.9f * (T - (float) revTL[dst][t]) / std::max (1.0f, Trv));
            const float gr = std::exp (-6.9f * (T - (float) revTR[dst][t]) / std::max (1.0f, Trv));
            revG[dst][t] = (rnd() < 0.5f ? -gl : gl); revGR[dst][t] = (rnd() < 0.5f ? -gr : gr);
            e2 += gl * gl; e2r += gr * gr;
        }
        const float nl = 1.0f / std::sqrt (std::max (1.0e-9f, e2)), nr = 1.0f / std::sqrt (std::max (1.0e-9f, e2r));
        for (int t = 0; t < kRev; ++t) { revG[dst][t] *= nl; revGR[dst][t] *= nr; }
    }
    inline void revSum (int set, float& rl, float& rr) const
    {
        for (int t = 0; t < kRev; ++t)
        {
            rl += revG[set][t] * revBuf[(size_t) ((revPos - revTL[set][t]) & revMask)];
            rr += revGR[set][t] * revBuf[(size_t) ((revPos - revTR[set][t]) & revMask)];
        }
    }
    // 選ぶつまみの段が変わるか
    bool stepJump (int i, float v) const
    {
        const auto& t = spec.knobs[i];
        return t.steps && t.points > 1
               && std::lround (std::clamp (k[i], 0.0f, 1.0f) * (float) (t.points - 1)) != std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (t.points - 1));
    }
    // FREEZE 中に待っていた切り替えを、まとめて 1 回のダックで
    void applyPending()
    {
        bool any = false, jump = false;
        for (int j = 0; j < spec.nKnobs; ++j)
            if (! same (k[j], kReq[j])) { jump = jump || stepJump (j, kReq[j]); k[j] = kReq[j]; any = true; }
        if (! any) return;
        computeTargets();
        if (jump && prepared) duckState = 1;
    }
    // 予備の遅れの目標（サンプル、最低 1）
    float preTarget() const { return std::clamp (p[vpPre] * 0.001f * (float) sr, 1.0f, (float) (preBuf.size() - 4)); }
    // 切り替えのダックの底: なめらかにしていた値・長さ・混ぜ具合を全部すぐ目標に
    void snapAll()
    {
        // 響きの中身（初期反射・拡散・FDN・逆回し・フィルターの状態）を空にしてから切り替える。
        // 中身を残したまま長さを飛ばすと、その段差が輪の中を回って、戻したときにプチッと鳴る（DSP 検査 0.3.0）。予備の遅れの中身（入る音の履歴）は残す
        std::fill (erBuf.begin(), erBuf.end(), 0.0f);
        for (auto& a : apBuf) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (revBuf.begin(), revBuf.end(), 0.0f);
        for (auto& f : lossLo) f.z = 0.0f;
        for (auto& f : lossHi) f.z = 0.0f;
        for (auto& c : svf) for (auto& f : c) f.reset();
        for (int j = 0; j < vpCount; ++j) ps[j] = p[j];
        snapGains();
        updateCoefs (true);
        duckG = 0.0f; inG = 0.0f;
    }

    const VerbSpec& spec;
    double sr = 48000.0;
    float p[vpCount] {}, ps[vpCount] {};
    float aCtl = 0.1f;
    unsigned ctl = 0;
    bool first = true;
    std::vector<float> preBuf, erBuf, apBuf[4], dl[8];
    int preMask = 0, erMask = 0, apMask[4] {}, dlMask[8] {};
    int prePos = 0, erPos = 0;
    std::array<int, 4> apPos {};
    std::array<int, 8> dlPos {};
    float preFb = 0.0f, apG = 0.6f, apLen[4] {};
    float erTL[kEr] {}, erTR[kEr] {}, erG[kEr] {}, erGR[kEr] {};
    float baseLen[8] {}, curLen[8] {}, lenStep[8] {}, lossG[8] {}, modDepth = 0.0f, lateNorm = 0.1f;
    float erStepL[kEr] {}, erStepR[kEr] {}, apStep[4] {};
    float mixHP = 0.0f, mixLP = 0.0f, mixBand = 0.0f, mixHPStep = 0.0f, mixLPStep = 0.0f, mixBandStep = 0.0f;
    float apGStep = 0.0f, lossGStep[8] {}, lossGT[8] {}, lateNormStep = 0.0f, modDepthStep = 0.0f;
    static constexpr int kSvf = 7;   // HP / LP / 低い棚 / 山 / 高い棚 / 帯域の上限 2 段
    Svf svf[2][kSvf];
    float preD[2] { 1.0f, 1.0f }, preF = 0.0f, preFadeStep = 0.001f;
    int preCur = 0;
    bool preFading = false, prepared = false;
    float duckG = 1.0f, duckDown = 0.001f, duckUp = 0.001f, inG = 1.0f;
    int duckState = 0;
    bool stepPending = false;
    Svf dcCut;
    float kReq[kMaxKnobs] {};   // 頼まれたつまみの値（FREEZE 中に待っている選ぶつまみは k と違う）
    float lfoC[8] {}, lfoS[8] {}, lfoRc[8] {}, lfoRs[8] {};
    Shelf1 lossLo[8], lossHi[8];
    std::vector<float> revBuf;
    int revMask = 0, revPos = 0, revTL[2][kRev] {}, revTR[2][kRev] {}, revSet = 0;
    float revG[2][kRev] {}, revGR[2][kRev] {}, revT = -1.0f, revT60 = -1.0f, revLevel = 0.47f;   // 本物の Reflektor の REVERSE の山の大きさに合わせた
    float revF = 0.0f, revFadeStep = 0.001f;
    bool revFading = false;
    Smooth sWet, sDry, sEr, sLate, sWidth, sFrz, sEcho, sRev, sVDel;
};
} // namespace aiofx
