#pragma once
// フィルターの続き（0.10.0〜）。JUCE なし
#include "FilterParts2.h"

namespace aiofx
{
// ---- RESO BANK（Guitar Rig の Filterbank を測って合わせ込み）----
// 測った結果（cap/mod/Filterbank 75 通り。インパルス 1 発とピンクノイズから伝達関数。どの設定も線形で時間で変わらない = コヒーレンス 1.000）:
//   8 帯域の BP を並べて足す。帯域 b（0〜7）の中心 f_b = F × R^b。各帯域は 2 つの山 f_b × 2^(±d)（それぞれ半分の大きさ。d = 0 なら 1 つに重なる）
//   F: 中心 C = 20 Hz × 5^SHIFT。LR SPLIT で左は C × 2^(−x)、右は C × 2^(+x)、x = 2 (LR SPLIT − 0.5) オクターブ（初期値 0.8 で左 27.2 Hz・右 62.6 Hz）
//   R: log2 R = 0.3334 + 1.0468 × STRETCH（0.75 まで。1 では 1.331 で少し頭打ち）。0 で 1/3 オクターブおき、初期値 0.555 で 1.885 倍
//   d = 2.5 × BAND SPLIT × log2 R（間隔の 2.5 × BAND SPLIT 段。BAND SPLIT 0.5 / 1 の山の位置が全部この式どおり）
//   Q: 1/Q = 0.603 (1 − RES)³ + 0.0033（RES 0 で 1.65、初期値 0.408 で 7.8、0.75 で 79、1 で 303）
//   大きさ: 帯域のつまみに比例（0 で消える。0.5 でちょうど半分）× G(RES)（RES 0 / .25 / .408 / .5 / .75 / 1 で 0.848 / 1.454 / 2.229 / 3.115 / 14.2 / 44.7）
//   1 つの山の形: 0.5 k (BP ＋ g HP)（TPT の状態変数フィルター。分子は (1 − z^-1) だけでナイキストにゼロがない）。
//   和に共通の N(z) = s (1 − 0.37919 z^-1) / (1 − 0.08355 z^-1 ＋ 0.18263 z^-2)（N(1) = 2）。
//   この形で 3 kHz より上の平らなすそ（−14 dB、位相 −25〜−60°）まで本物に合う（RES 0〜0.5 で差 0.3〜0.4 dB、位相 1〜4°。RES 1 は記録 2 秒に響きが収まらず粗い）
//   MIX: s = smoothstep(MIX)、出口 = 0.968 (1 − s) ドライ ＋ s ウェット（本物のドライは −0.28 dB）
//   STEREO: モノの入口で測ったので 4 つの違いは測れなかった（左右とも 1 ビットも同じ）。0 = MONO（入口の左右を混ぜて両方のバンクへ）、1〜3 = STEREO
//   ナイキストを超える帯域（STRETCH 1 で LR SPLIT の右側など）は、本物は変わった形（930 Hz に −40 dB の谷・18 kHz に +16 dB）になる。
//   こちらは 20 kHz（0.45 sr）で止め、超えた分 1/2 オクターブで大きさを 0 へ（10 Hz の下限も同じ）
class ResoBank final : public Module
{
public:
    static constexpr int kBands = 8, kCopies = 16;
    static float centerHz (float shift) { return 20.0f * std::pow (5.0f, std::clamp (shift, 0.0f, 1.0f)); }
    static float log2R (float st)
    {
        st = std::clamp (st, 0.0f, 1.0f);
        return st <= 0.75f ? 0.3334f + 1.0468f * st : 1.1185f + (st - 0.75f) * ((1.3309f - 1.1185f) / 0.25f);
    }
    static float qOf (float res) { const float r = 1.0f - std::clamp (res, 0.0f, 1.0f); return 1.0f / (0.603f * r * r * r + 0.0033f); }
    static float gainOf (float res)
    {
        static const float rx[6] { 0.0f, 0.25f, 0.40803f, 0.5f, 0.75f, 1.0f };
        static const float lg[6] { -0.16487f, 0.37432f, 0.80155f, 1.13623f, 2.65324f, 3.80000f };   // ln G
        return std::exp (interpTable (rx, lg, std::clamp (res, 0.0f, 1.0f)));
    }
    static std::string lrText (float v)
    {
        const float x = 2.0f * (std::clamp (v, 0.0f, 1.0f) - 0.5f);
        if (std::abs (x) < 0.005f) return "0 oct";
        return (x > 0.0f ? std::string ("R +") : std::string ("L +")) + fmt (std::abs (x) * 2.0f, 2, " oct");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "resobank", "RESO BANK", "FILTER",
            "Eight resonant band-pass filters in a row: SHIFT moves them all, STRETCH spreads them, BAND SPLIT doubles each band, LR SPLIT moves left and right apart. BAND 1-8 set each band's level.",
            {
                { "SHIFT",      0.45f,     0, [] (float v) { return fmtHz (centerHz (v)); } },
                { "STRETCH",    0.555f,    0, [] (float v) { return fmt (log2R (v), 2, " oct"); } },
                { "RES",        0.40803f,  0, [] (float v) { return fmt (qOf (v), 1, " Q"); } },
                { "BAND SPLIT", 0.13f,     0, [] (float v) { return fmtPct (v); } },
                { "LR SPLIT",   0.8f,      0, &lrText },
                { "MIX",        1.0f,      0, [] (float v) { return fmtPct (v); } },
                { "STEREO",     1.0f / 3.0f, 4, [] (float v) { return std::string (std::lround (v * 3.0f) == 0 ? "MONO" : "STEREO"); } },
                { "BAND 1",     0.515f,    0, [] (float v) { return fmtPct (v); } },
                { "BAND 2",     0.675f,    0, [] (float v) { return fmtPct (v); } },
                { "BAND 3",     0.24f,     0, [] (float v) { return fmtPct (v); } },
                { "BAND 4",     0.695f,    0, [] (float v) { return fmtPct (v); } },
                { "BAND 5",     0.765f,    0, [] (float v) { return fmtPct (v); } },
                { "BAND 6",     0.25f,     0, [] (float v) { return fmtPct (v); } },
                { "BAND 7",     0.86f,     0, [] (float v) { return fmtPct (v); } },
                { "BAND 8",     0.695f,    0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* s : { &sShift, &sStretch, &sRes, &sSplit, &sLr }) s->prepare (sr, 0.03);
        for (auto& s : sBand) s.prepare (sr, 0.02);
        sMix.prepare (sr, 0.02);
        wA20 = (float) (1.0 - std::exp (-16.0 / (0.02 * sr)));   // 16 サンプルごとの 20 ms のなめらかさ
        // 共通の N(z)（48 kHz で測った z の式のまま。本物もホストのレートで同じ z の式を使っていると見て、レートで直さない）
        const double n1 = -0.37919, m1 = -0.08355, m2 = 0.18263, s = 2.0 * (1.0 + m1 + m2) / (1.0 + n1);
        for (auto& b : post) b.setNorm (s, s * n1, 0.0, 1.0, m1, m2);
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& ch : flt) for (auto& f : ch) f.reset();
        for (auto& b : post) b.reset();
        ctl = 0; update (true);
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 15 || same (k[i], v)) return; k[i] = v; recalc (false); }
    double tailSeconds() const override { return tail; }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            dryW += dryStep; wetW += wetStep;
            const float xl = L[s], xr = R[s];
            const float il = mono ? 0.5f * (xl + xr) : xl, ir = mono ? 0.5f * (xl + xr) : xr;
            float yl = 0.0f, yr = 0.0f;
            for (int j = 0; j < kCopies; ++j)
            {
                auto& fl = flt[0][(size_t) j]; auto& fr = flt[1][(size_t) j];
                fl.step(); fr.step();
                yl += fl.process (il); yr += fr.process (ir);
            }
            yl = post[0].process (yl); yr = post[1].process (yr);
            L[s] = dryW * xl + wetW * yl;
            R[s] = dryW * xr + wetW * yr;
        }
    }

private:
    void recalc (bool snap)
    {
        sShift.set (std::clamp (k[0], 0.0f, 1.0f), snap); sStretch.set (std::clamp (k[1], 0.0f, 1.0f), snap);
        sRes.set (std::clamp (k[2], 0.0f, 1.0f), snap); sSplit.set (std::clamp (k[3], 0.0f, 1.0f), snap);
        sLr.set (std::clamp (k[4], 0.0f, 1.0f), snap); sMix.set (std::clamp (k[5], 0.0f, 1.0f), snap);
        mono = std::lround (std::clamp (k[6], 0.0f, 1.0f) * 3.0f) == 0;
        for (int b = 0; b < kBands; ++b) sBand[(size_t) b].set (std::clamp (k[7 + b], 0.0f, 1.0f), snap);
        dirty = true;
    }
    // 16 サンプルごと: つまみのなめらかさを進め、動いている間だけ 32 個の山の係数を作り直す（16 サンプルで直線につなぐ）
    void update (bool snap)
    {
        bool moving = snap || dirty;
        if (! snap)
        {
            for (int i = 0; i < 16; ++i)
            {
                sShift.next(); sStretch.next(); sRes.next(); sSplit.next(); sLr.next(); sMix.next();
                for (auto& b : sBand) b.next();
            }
            moving = moving || ! (sShift.settled() && sStretch.settled() && sRes.settled() && sSplit.settled() && sLr.settled() && sMix.settled());
            for (auto& b : sBand) moving = moving || ! b.settled();
            moving = moving || wvMoving;
        }
        // MIX（smoothstep の重み。16 サンプルで直線に）
        const float mx = sMix.cur, sm = mx * mx * (3.0f - 2.0f * mx);
        const float wT = sm, dT = 0.968f * (1.0f - sm);
        if (snap) { wetW = wT; dryW = dT; wetStep = dryStep = 0.0f; }
        else { wetStep = (wT - wetW) / 16.0f; dryStep = (dT - dryW) / 16.0f; }
        if (! moving) return;
        dirty = false;
        const float C = centerHz (sShift.cur), x = 2.0f * (sLr.cur - 0.5f);
        const float l2 = log2R (sStretch.cur), Rr = std::exp2 (l2), d = 2.5f * sSplit.cur * l2;
        const float Q = qOf (sRes.cur), kq = 1.0f / Q, G = gainOf (sRes.cur);
        const double fmax = std::min (20000.0, 0.45 * sr);
        float fLow = 1.0e9f;
        bool still = false;   // 消す量がまだ動いている
        for (int c = 0; c < 2; ++c)
        {
            const float F = C * std::exp2 (c == 0 ? -x : x);
            for (int b = 0; b < kBands; ++b)
            {
                const float fb = F * std::pow (Rr, (float) b), A = 0.5f * sBand[(size_t) b].cur * G;
                for (int h = 0; h < 2; ++h)
                {
                    const double fRaw = std::max (1.0e-3, (double) fb * std::exp2 (h == 0 ? -d : d));
                    const double fc = std::clamp (fRaw, 10.0, fmax);
                    // 上限（20 kHz か 0.45 sr）・下限（10 Hz）を超えた山は、超えた分 1/2 オクターブで大きさを 0 へ
                    // （止めた所に何本も重ねると STRETCH 1 だけで 20 kHz に +26〜30 dB: DSP 検査 0.10.0。本物は 18 kHz 付近に +16 dB ほど）
                    const float wt = (float) (std::clamp (1.0 - std::log2 (fRaw / fmax) / 0.5, 0.0, 1.0) * std::clamp (1.0 - std::log2 (10.0 / fRaw) / 0.5, 0.0, 1.0));
                    // 消す量は山ごとに 20 ms でなめらかに（つまみを一気に動かすと 10 Hz の下限の消える区間を 10 ms で通り抜け、40 Hz で目安 111: DSP 検査 0.10.0 の 2 回目）
                    float& wv = wvS[(size_t) c][(size_t) (2 * b + h)];
                    wv = snap ? wt : flush (wv + wA20 * (wt - wv));
                    if (std::abs (wt - wv) > 1.0e-4f) still = true;
                    if (wv > 1.0e-4f) fLow = std::min (fLow, (float) fc);
                    auto& f = flt[(size_t) c][(size_t) (2 * b + h)];
                    const float g = (float) std::tan (kPiD * fc / sr), Aw = A * wv;
                    // 0.5 k (BP + g HP)、HP = x − k v1 − v2 → m0 = 0.5 k g、m1 = 0.5 k (1 − g k)、m2 = −0.5 k g（× この山の大きさ A）
                    f.tg = g; f.tk = kq; f.tm0 = Aw * 0.5f * kq * g; f.tm1 = Aw * 0.5f * kq * (1.0f - g * kq); f.tm2 = -Aw * 0.5f * kq * g;
                    f.ramp (snap, 16.0f);
                }
            }
        }
        // 響きの長さ（−60 dB まで）: 6.91 τ、τ = Q / (π f)。いちばん低い山で
        tail = std::min (30.0, 6.91 * (double) Q / (kPiD * std::max (10.0, (double) fLow)));
        wvMoving = still;
    }
    double sr = 48000.0, tail = 1.0;
    float k[15] { 0.45f, 0.555f, 0.40803f, 0.13f, 0.8f, 1.0f, 1.0f / 3.0f, 0.515f, 0.675f, 0.24f, 0.695f, 0.765f, 0.25f, 0.86f, 0.695f };
    std::array<std::array<Svf, kCopies>, 2> flt;
    std::array<std::array<float, kCopies>, 2> wvS {};   // 山ごとの消す量（なめらか）
    float wA20 = 0.01f;
    Biquad post[2];
    Smooth sShift, sStretch, sRes, sSplit, sLr, sMix;
    std::array<Smooth, kBands> sBand;
    float wetW = 1.0f, dryW = 0.0f, wetStep = 0.0f, dryStep = 0.0f;
    bool mono = false, dirty = true, wvMoving = false;
    unsigned ctl = 0;
};

// ---- DUAL FILTER（Guitar Rig の Doppel-Filter を測って合わせ込み。大まかな版）----
// 測った結果（cap/mod/DoppelFilter 74 通り。ピンクノイズ −20 dBFS の伝達関数。DRIVE 0・FM 0 なら線形）:
//   2 つの 2 次の状態変数フィルター A・B（双一次。中心は 1 倍のときと同じ Hz のまま、全体を 4 倍の中で回す: DSP 検査 0.10.0）。出口 = LP・BP・HP を MODE で混ぜる（線形の記録 52 通り × 左右をまとめて当てはめ、差 0.6 dB）。
//   MODE: 0 = 0.869 LP、0.5 = 0.918 BP（間は直線）、0.75 = 0.866 BP ＋ 0.439 HP、1 = 0.850 BP ＋ 0.883 HP
//   CUTOFF: 中心 537 Hz × 2^(11.555 (CUTOFF − 0.5))（A・B 同じ。0.68 で 2.25 kHz、0.475 で 440 Hz）
//   SPREAD: 左は中心 × 2^σ、右は × 2^(−σ)。σ は表（0 / .25 / .5 / .75 / 1 で +2.01 / +0.30 / 0 / −0.81 / −2.01 オクターブ。A・B 同じ）
//   RESONANCE → Q: 0 / .25 / .285 / .315 / .5 で 0.48 / 2.13 / 2.86 / 3.53 / 13.3。0.5 と 0.75 の間で自己発振（本物は入口なしで 0.75 で −22〜−25 dBFS、1 で −8〜−11 dBFS）
//     → 0.5 から上はダンピングを直線で負へ、振れ幅の 2 乗でダンピングを足して振れ幅を止める（ファン・デル・ポール型。0.45 より下では足さない = 線形のまま）
//   ROUTING 0 = 直列（A → B）、1〜3 = 並列（B は入口から。モノの入口では 3 つとも 1 ビットも同じ）。
//   MIX = A の出口 ↔ B の出口を等パワー（cos / sin）で（MIX 0.5 の記録が 0.707 A ＋ 0.707 A→B にぴったり）。素通しはない
//   DRIVE: −20 dBFS のピンクノイズで +1.4 / +3.9 / +5.9 / +7.2 dB（0.25 / .5 / .75 / 1）、上げるほど歪む → そのフィルターの前で持ち上げて tanh
//   FM: 入口の音でそのフィルターの中心を揺らす（平均の音量は変わらない）。SAFETY はこの音量では 4 つとも 1 ビットも同じ（効きは測れていない）
//   本物は全体が 8 サンプル遅れる（こちらは 4 倍の上げ下げで 4 サンプル。素通しを混ぜないので音には関係しない）
class DualFilter final : public Module
{
public:
    static float centerHz (float c) { return 537.07f * std::exp2 (11.5552f * (std::clamp (c, 0.0f, 1.0f) - 0.5f)); }
    static float spreadOct (float s)
    {
        static const float sx[7] { 0.0f, 0.235f, 0.25f, 0.5f, 0.75f, 0.8f, 1.0f };
        static const float so[7] { 2.0064f, 0.3445f, 0.3027f, 0.0f, -0.8118f, -1.0592f, -2.0067f };
        return interpTable (sx, so, std::clamp (s, 0.0f, 1.0f));
    }
    // ダンピング k（= 1/Q）。0.5 から上は負へ（自己発振）
    static float dampOf (float r)
    {
        static const float rx[5] { 0.0f, 0.25f, 0.285f, 0.315f, 0.5f };
        static const float lq[5] { -0.7401f, 0.7576f, 1.0514f, 1.2614f, 2.5842f };   // ln Q
        r = std::clamp (r, 0.0f, 1.0f);
        if (r <= 0.5f) return std::exp (-interpTable (rx, lq, r));
        const float k5 = std::exp (-2.5842f);
        return k5 - (r - 0.5f) * (k5 + kNegAt1) / 0.5f;   // 0.5 → k5、1 → −kNegAt1（直線）
    }
    static float driveDb (float d)
    {
        static const float dx[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, dd[5] { 0.0f, 1.45f, 3.95f, 6.0f, 7.5f };
        return interpTable (dx, dd, std::clamp (d, 0.0f, 1.0f));
    }
    static std::string routeText (float v) { return std::string (std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f) == 0 ? "SERIAL" : "PARALLEL"); }
    static std::string modeText (float v)
    {
        return v < 0.125f ? std::string ("LP") : v < 0.375f ? std::string ("LP-BP") : v < 0.625f ? std::string ("BP") : v < 0.875f ? std::string ("BP-HP") : std::string ("HP");
    }
    static std::string spreadText (float v)
    {
        const float o = spreadOct (v);
        if (std::abs (o) < 0.005f) return "0 oct";
        return (o > 0.0f ? std::string ("L +") : std::string ("R +")) + fmt (std::abs (o) * 2.0f, 2, " oct");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "dualflt", "DUAL FILTER", "FILTER",
            "Two resonant filters, A then B (or side by side with ROUTING). MODE morphs each from low-pass through band-pass to high-pass, SPREAD moves left and right apart, MIX fades from A to B. RESONANCE above about 75 % sings on its own.",
            {
                { "CUTOFF A",    0.68f,   0, [] (float v) { return fmtHz (std::min (centerHz (v), 18000.0f)); } },
                { "RESONANCE A", 0.315f,  0, [] (float v) { return fmtPct (v); } },
                { "DRIVE A",     0.0f,    0, [] (float v) { return fmt (driveDb (v), 1, " dB"); } },
                { "SPREAD A",    0.235f,  0, &spreadText },
                { "MODE A",      0.355f,  0, &modeText },
                { "CUTOFF B",    0.475f,  0, [] (float v) { return fmtHz (std::min (centerHz (v), 18000.0f)); } },
                { "RESONANCE B", 0.285f,  0, [] (float v) { return fmtPct (v); } },
                { "DRIVE B",     0.0f,    0, [] (float v) { return fmt (driveDb (v), 1, " dB"); } },
                { "SPREAD B",    0.8f,    0, &spreadText },
                { "MODE B",      1.0f,    0, &modeText },
                { "MIX",         1.0f,    0, [] (float v) { return v < 0.005f ? std::string ("A") : v > 0.995f ? std::string ("B") : fmtPct (v); } },
                { "ROUTING",     0.0f,    4, &routeText },
                { "SAFETY",      0.0f,    4, [] (float v) { return std::to_string (std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f)); } },
                { "FM A",        0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "FM B",        0.0f,    0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }
    static constexpr float kNegAt1 = 0.082f;  // RESONANCE 1 のダンピング（負）。0.75 と 1 の発振の大きさの比（本物 14 dB）から
    static constexpr float kGamma = 0.39f;    // 振れ幅の 2 乗で足すダンピング（RESONANCE 1 の発振の大きさから）
    static constexpr float kDriveSat = 2.5f;  // DRIVE 1 での飽和の強さ（tanh の手前の倍率。小さな音の持ち上げは driveDb のまま）
    static constexpr float kFmOct = 4.0f;     // FM 1 で入口 1.0 のとき中心を何オクターブ動かすか（揺らす元はそのフィルターの入口。B の FM がほとんど効かないのも本物どおり）

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& s : sm) s.prepare (sr, 0.03);
        sRoute.prepare (sr, 0.005);
        sr4 = 4.0 * sr;
        fMax = (float) std::min (0.45 * sr, 18000.0);   // 96 / 192 kHz でも超音波で発振しない。上げきったときの自己発振も聞こえる所に（21.8 kHz で −6.5 dBFS が鳴り続けた: DSP 検査 0.10.0）
        dcR = (float) std::exp (-2.0 * kPiD * 5.0 / sr);
        recalc (true); reset();
    }
    void reset() override
    {
        for (auto& f : st) f = State {};
        for (auto& o : os) o.reset();
        dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.0f;
        ctl = 0; update (true);
    }
    void setKnob (int i, float v) override { if (i < 0 || i >= 15 || same (k[i], v)) return; k[i] = v; recalc (false); }
    double tailSeconds() const override { return tail; }
    int latency() const override { return 4; }   // 4 倍の上げ下げ（TAPE WOW と同じ）
    bool dryAfterReset() const override { return false; }   // reset のあと、フィルターを通らない音を出さない（MORPH FILTER と同じ: DSP 検査 0.10.0 の 2 回目）
    // 全体を 4 倍の中で回す（DRIVE の tanh・FM・自己発振で 1 倍だと折り返しがはっきり出た: DSP 検査 0.10.0。素通しがないので A・B の位置は自然にそろう）
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            par1 = sRoute.next();
            // 係数は 16 サンプルごとに作り、間は 1 サンプルずつ直線でつなぐ（段々に変えると MODE・MIX を一気に動かしたときプチッ 36 / 23: 自己検査 0.10.0）
            for (int f = 0; f < 2; ++f) par[f].add (dpar[f]);
            wA += dwA; wB += dwB;
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                float u[4];
                os[(size_t) c].up (io[s], u);
                for (int j = 0; j < 4; ++j)
                {
                    const float x = u[j];
                    const float ya = tick (st[(size_t) c], x, c, 0);
                    // B（直列なら A の出口、並列なら入口。ROUTING を変えたら 5 ms でつなぎ替える: 一気に替えると −40 dBFS のプチッ）
                    const float yb = tick (st[(size_t) (2 + c)], ya + par1 * (x - ya), c, 1);
                    u[j] = wA * ya + wB * yb;
                }
                const float y = os[(size_t) c].down (u);
                // 出口の直流カット（5 Hz、1 次）: 直列で A が自己発振し B の FM がそれで揺れると直流 −0.29 が出た（DSP 検査 0.10.0）
                const float o = y - dcx[c] + dcR * dcy[c];
                dcx[c] = y; dcy[c] = flush (o);
                io[s] = o;
            }
        }
    }

private:
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };
    // 状態の頭打ち: 6 までは線形のまま、そこから 8 へやわらかく（硬い ±8 だと大きな入口で中が角ばった歪みになった: DSP 検査 0.10.0）
    static inline float softCap (float v)
    {
        const float a = std::abs (v);
        if (a <= 6.0f) return v;
        return std::copysign (6.0f + 2.0f * std::tanh ((a - 6.0f) * 0.5f), v);
    }
    struct Par
    {
        float g[2] { 0.1f, 0.1f }, hz[2] { 1000.0f, 1000.0f }, k = 0.25f, gam = 0.0f, wl = 1.0f, wb = 0.0f, wh = 0.0f, sat = 1.0f, post = 1.0f, dw = 0.0f, fm = 0.0f;
        void add (const Par& d)
        {
            g[0] += d.g[0]; g[1] += d.g[1]; hz[0] += d.hz[0]; hz[1] += d.hz[1]; k += d.k; gam += d.gam;
            wl += d.wl; wb += d.wb; wh += d.wh; sat += d.sat; post += d.post; dw += d.dw; fm += d.fm;
        }
        static Par diff (const Par& a, const Par& b, float inv)   // (a − b) × inv
        {
            Par d;
            d.g[0] = (a.g[0] - b.g[0]) * inv; d.g[1] = (a.g[1] - b.g[1]) * inv; d.hz[0] = (a.hz[0] - b.hz[0]) * inv; d.hz[1] = (a.hz[1] - b.hz[1]) * inv;
            d.k = (a.k - b.k) * inv; d.gam = (a.gam - b.gam) * inv; d.wl = (a.wl - b.wl) * inv; d.wb = (a.wb - b.wb) * inv; d.wh = (a.wh - b.wh) * inv;
            d.sat = (a.sat - b.sat) * inv; d.post = (a.post - b.post) * inv; d.dw = (a.dw - b.dw) * inv; d.fm = (a.fm - b.fm) * inv;
            return d;
        }
    };
    // 1 サンプル: 入口を持ち上げて tanh → 状態変数フィルター（ダンピングは k ＋ γ s²、s = 前の BP の状態）→ LP / BP / HP を混ぜる
    inline float tick (State& z, float x, int c, int which)
    {
        const Par& p = par[which];
        float v0 = x;
        // DRIVE: tanh の音を DRIVE の最初の 5 % でなめらかに混ぜ入れる（0 で素通しにぴったり戻る。切り替えると 0.5 の音で 0.04 の段: 自己検査 0.10.0）
        if (p.dw > 1.0e-6f) v0 = x + p.dw * (std::tanh (x * p.sat) * p.post - x);
        float g = p.g[c];
        if (p.fm > 0.0f)
        {
            const float hz = std::clamp (p.hz[c] * std::exp2 (kFmOct * p.fm * std::clamp (x, -1.5f, 1.5f)), 10.0f, fMax);
            const float a = (float) (kPiD / sr4) * hz, a2 = a * a;   // 4 倍の中では 0.36 rad 以下: tan を多項式で（誤差 1e-5 以下。1 サンプル 32 回の tan が重かった: DSP 検査 0.10.0）
            g = a * (1.0f + a2 * (1.0f / 3.0f + a2 * (2.0f / 15.0f + a2 * (17.0f / 315.0f))));
        }
        const float kk = p.k + p.gam * z.ic1 * z.ic1;
        const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
        const float v3 = v0 - z.ic2;
        const float v1 = a1 * z.ic1 + a2 * v3;
        const float v2 = z.ic2 + a2 * z.ic1 + a3 * v3;
        z.ic1 = flush (softCap (2.0f * v1 - z.ic1)); z.ic2 = flush (softCap (2.0f * v2 - z.ic2));
        const float hp = v0 - kk * v1 - v2;
        return p.wl * v2 + p.wb * v1 + p.wh * hp;
    }
    void recalc (bool snap)
    {
        for (int i = 0; i < 15; ++i) sm[(size_t) i].set (std::clamp (k[i], 0.0f, 1.0f), snap);
        sRoute.set (std::lround (std::clamp (k[11], 0.0f, 1.0f) * 3.0f) == 0 ? 0.0f : 1.0f, snap);
        if (snap) par1 = sRoute.cur;
    }
    void update (bool snap)
    {
        if (! snap) for (int i = 0; i < 16; ++i) for (auto& s : sm) s.next();
        for (int f = 0; f < 2; ++f)
        {
            const int b = f * 5;
            Par& p = tgt[f];
            const float fc = centerHz (sm[(size_t) b].cur), so = spreadOct (sm[(size_t) (b + 3)].cur);
            for (int c = 0; c < 2; ++c)
            {
                const double hz = std::clamp ((double) fc * std::exp2 (c == 0 ? so : -so), 10.0, (double) fMax);
                p.g[c] = (float) std::tan (kPiD * hz / sr4); p.hz[c] = (float) hz;   // 4 倍のレートで（中心は同じ Hz に来る）
            }
            const float rr = sm[(size_t) (b + 1)].cur;
            p.k = dampOf (rr);
            p.gam = kGamma * std::clamp ((rr - 0.45f) / 0.1f, 0.0f, 1.0f);   // 0.45 より下は線形のまま
            const float m = sm[(size_t) (b + 4)].cur;
            if (m <= 0.5f) { p.wl = 0.8694f * (1.0f - 2.0f * m); p.wb = 0.9177f * 2.0f * m; p.wh = 0.0f; }
            else if (m <= 0.75f) { const float t = (m - 0.5f) / 0.25f; p.wl = 0.0f; p.wb = 0.9177f + (0.8659f - 0.9177f) * t; p.wh = 0.4386f * t; }
            else { const float t = (m - 0.75f) / 0.25f; p.wl = 0.0f; p.wb = 0.8659f + (0.8495f - 0.8659f) * t; p.wh = 0.4386f + (0.8828f - 0.4386f) * t; }
            // DRIVE: 小さな音で dg 倍、強さ β で飽和（dg × tanh(β x) / β）
            const float dv = sm[(size_t) (b + 2)].cur, dg = dbToGain (driveDb (dv)), beta = 1.0f + (kDriveSat - 1.0f) * dv;
            p.sat = dg * beta; p.post = 1.0f / beta; p.dw = std::min (1.0f, dv / 0.05f);
            p.fm = sm[(size_t) (13 + f)].cur;
            if (snap) { par[f] = p; dpar[f] = Par::diff (p, p, 0.0f); }
            else dpar[f] = Par::diff (p, par[f], 1.0f / 16.0f);
        }
        const float mx = sm[10].cur, ta = std::cos (0.5f * (float) kPiD * mx), tb = std::sin (0.5f * (float) kPiD * mx);
        if (snap) { wA = ta; wB = tb; dwA = dwB = 0.0f; }
        else { dwA = (ta - wA) / 16.0f; dwB = (tb - wB) / 16.0f; }
        // 響きの長さ（−60 dB まで 6.91 τ、τ = Q / (π f)）。自己発振する設定は 30 秒
        double t = 0.05;
        for (int f = 0; f < 2; ++f)
        {
            const double kq = tgt[f].k;
            if (kq <= 0.0) { t = 30.0; break; }
            const double fl = std::max (10.0, std::min ((double) tgt[f].hz[0], (double) tgt[f].hz[1]));
            t = std::max (t, 6.91 / (kq * kPiD * fl));
        }
        tail = std::min (30.0, t);
    }
    double sr = 48000.0;
    float k[15] { 0.68f, 0.315f, 0.0f, 0.235f, 0.355f, 0.475f, 0.285f, 0.0f, 0.8f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<Smooth, 15> sm;
    std::array<State, 4> st;   // A 左・A 右・B 左・B 右
    Par par[2], tgt[2], dpar[2];   // 今の値・16 サンプル後の目標・1 サンプルの歩み
    float wA = 0.0f, wB = 1.0f, dwA = 0.0f, dwB = 0.0f, fMax = 21600.0f, par1 = 0.0f, dcR = 0.9993f, dcx[2] {}, dcy[2] {};
    double sr4 = 192000.0;
    Oversampler4 os[2];
    Smooth sRoute;
    double tail = 2.0;
    unsigned ctl = 0;
};

// ---- SPEAK FILTER（Guitar Rig の Formant Filter を測って合わせ込み。大まかな版）----
// 測った結果（cap/mod/FormantFilter 67 通り。ピンクノイズの伝達関数。線形で時間で変わらない = コヒーレンス 1.000）:
//   出口 = (1 − MIX) × 素通し（ちょうど 0.5 サンプル遅れ。大きさは平ら、位相の差 0.01°）＋ MIX × (3 本の BP（双一次、山 1 で正規化）× 大きさ) の和（MIX は直線）
//   TALK: 0 / .25 / .5 / .75 / 1 の 5 つの母音（ほぼ U / O / A / E / I）のあいだを、周波数・Q・大きさとも直線で動く（TALK 17 通りで確かめた）
//   COLOR: 1〜12 は 1 ビットも同じ、0 だけ別の母音の組（COLOR 0 の TALK 0.5 は 0.7 と 0.75 から逆算）
//   SIZE: 全部のフォルマントを同じ倍率で（SIZE 0 / .25 / .5 / .75 / 1 で 1 / 1.255 / 1.513 / 1.900 / 2.289 倍、初期値 0.295 で 1.301）
//   SHARP: Q を 1 / 1.27 / 1.43 / 2.06 / 2.80 / 3.17 倍、大きさを 1 / 1.23 / 1.35 / 1.67 / 1.89 / 1.97 倍（SHARP 0 / .169 / .25 / .5 / .75 / 1、TALK 0.7 の 3 本の平均。
//     本物は母音によって 3 本目がもっと鋭くなる = 大まか）。差は母音ごとに 0.2〜0.5 dB、ウェットだけ（MIX 1）で 0.85 dB
class SpeakFilter final : public Module
{
public:
    struct Vowel { float f[3], q[3], g[3]; };
    static const Vowel& vowel (int color, int i)
    {
        static const Vowel t[2][5] {
            {   // COLOR 0
                { { 195.0f, 384.0f, 1642.0f }, { 2.87f, 5.38f, 5.96f }, { 3.47f, 0.0f, 0.24f } },
                { { 270.0f, 384.0f, 1944.0f }, { 2.28f, 5.38f, 3.93f }, { 2.34f, 2.18f, 0.83f } },
                { { 603.0f, 1013.0f, 2045.0f }, { 3.07f, 4.79f, 2.90f }, { 4.91f, 4.46f, 3.28f } },
                { { 358.0f, 1643.0f, 2575.0f }, { 3.97f, 4.09f, 4.75f }, { 3.86f, 5.56f, 2.93f } },
                { { 308.0f, 2211.0f, 4080.0f }, { 3.79f, 4.79f, 4.81f }, { 2.74f, 3.46f, 2.78f } },
            },
            {   // COLOR 1〜12
                { { 206.0f, 587.0f, 1762.0f }, { 2.73f, 3.50f, 5.58f }, { 1.19f, 1.52f, 2.13f } },
                { { 138.0f, 696.0f, 1782.0f }, { 2.71f, 3.01f, 3.90f }, { 1.23f, 3.10f, 4.29f } },
                { { 531.0f, 937.0f, 2112.0f }, { 2.31f, 5.11f, 3.32f }, { 2.06f, 5.64f, 4.62f } },
                { { 582.0f, 1510.0f, 2126.0f }, { 3.47f, 4.19f, 4.71f }, { 3.25f, 5.53f, 6.06f } },
                { { 139.0f, 1852.0f, 2790.0f }, { 3.91f, 4.52f, 5.55f }, { 1.70f, 4.77f, 4.41f } },
            } };
        return t[std::clamp (color, 0, 1)][std::clamp (i, 0, 4)];
    }
    static float sizeMul (float v)
    {
        static const float x[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] { 1.0f, 1.2551f, 1.5125f, 1.8998f, 2.2893f };
        return interpTable (x, y, std::clamp (v, 0.0f, 1.0f)) / 1.3014f;   // 初期値 0.295 で 1
    }
    static float sharpQ (float v)
    {
        static const float x[6] { 0.0f, 0.169155f, 0.25f, 0.5f, 0.75f, 1.0f }, y[6] { 1.0f, 1.27f, 1.43f, 2.06f, 2.80f, 3.17f };
        return interpTable (x, y, std::clamp (v, 0.0f, 1.0f)) / 1.27f;
    }
    static float sharpG (float v)
    {
        static const float x[6] { 0.0f, 0.169155f, 0.25f, 0.5f, 0.75f, 1.0f }, y[6] { 1.0f, 1.23f, 1.35f, 1.67f, 1.89f, 1.97f };
        return interpTable (x, y, std::clamp (v, 0.0f, 1.0f)) / 1.23f;
    }
    static std::string talkText (float v)
    {
        static const char* n[5] { "U", "O", "A", "E", "I" };
        const float p = std::clamp (v, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p); const float fr = p - (float) i;
        if (fr < 0.15f) return n[i];
        if (fr > 0.85f) return n[i + 1];
        return std::string (n[i]) + "-" + n[i + 1];
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "speakflt", "SPEAK FILTER", "FILTER",
            "Three formant filters that make the sound say vowels: TALK moves from U through O, A, E to I, SIZE makes the mouth bigger or smaller, SHARP narrows the formants.",
            {
                { "TALK",  0.7f,      0, &talkText },
                { "SIZE",  0.295f,    0, [] (float v) { return fmt (sizeMul (v), 2, " x"); } },
                { "SHARP", 0.169155f, 0, [] (float v) { return fmtPct (v); } },
                { "MIX",   0.605f,    0, [] (float v) { return fmtPct (v); } },
                { "COLOR", 1.0f / 12.0f, 13, [] (float v) { return std::to_string (std::lround (std::clamp (v, 0.0f, 1.0f) * 12.0f) + 1); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sTalk, &sSize, &sSharp, &sMix, &sColor }) q->prepare (sr, 0.03);
        recalc (true); reset();
    }
    void reset() override { for (auto& ch : flt) for (auto& f : ch) f.reset(); ap[0] = ap[1] = 0.0f; ctl = 0; update (true); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 5 || same (k[i], v)) return; k[i] = v; recalc (false); }
    double tailSeconds() const override { return 0.2; }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            dryW += dryStep; wetW += wetStep;
            for (int c = 0; c < 2; ++c)
            {
                float* io = c == 0 ? L : R;
                const float x = io[s];
                float y = 0.0f;
                for (auto& f : flt[(size_t) c]) { f.step(); y += f.process (x); }
                // 素通しは 0.5 サンプル遅らせる（1 次のオールパス a = 1/3。本物の素通しは大きさ平らで 0.5 サンプル遅れ）
                const float d = kAp * x + ap[c]; ap[c] = flush (x - kAp * d);
                io[s] = dryW * d + wetW * y;
            }
        }
    }

private:
    static constexpr float kAp = 1.0f / 3.0f;
    void recalc (bool snap)
    {
        sTalk.set (std::clamp (k[0], 0.0f, 1.0f), snap); sSize.set (std::clamp (k[1], 0.0f, 1.0f), snap);
        sSharp.set (std::clamp (k[2], 0.0f, 1.0f), snap); sMix.set (std::clamp (k[3], 0.0f, 1.0f), snap);
        sColor.set (std::lround (std::clamp (k[4], 0.0f, 1.0f) * 12.0f) == 0 ? 0.0f : 1.0f, snap);   // COLOR は 0 か それ以外（30 ms でつなぐ）
    }
    void update (bool snap)
    {
        if (! snap) for (int i = 0; i < 16; ++i) { sTalk.next(); sSize.next(); sSharp.next(); sMix.next(); sColor.next(); }
        const float p = sTalk.cur * 4.0f;
        const int i0 = std::min (3, (int) p); const float fr = p - (float) i0;
        const float cb = sColor.cur, sm = sizeMul (sSize.cur), qm = sharpQ (sSharp.cur), gm = sharpG (sSharp.cur);
        for (int b = 0; b < 3; ++b)
        {
            float f = 0.0f, q = 0.0f, g = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float w = c == 0 ? 1.0f - cb : cb;
                if (w <= 0.0f) continue;
                const Vowel& a = vowel (c, i0); const Vowel& z = vowel (c, i0 + 1);
                f += w * (a.f[b] + fr * (z.f[b] - a.f[b])); q += w * (a.q[b] + fr * (z.q[b] - a.q[b])); g += w * (a.g[b] + fr * (z.g[b] - a.g[b]));
            }
            const double hz = std::clamp ((double) (f * sm), 20.0, 0.45 * sr);
            const float kq = 1.0f / std::max (0.3f, q * qm), gain = g * gm;
            for (auto& ch : flt)
            {
                auto& fl = ch[(size_t) b];
                fl.tg = (float) std::tan (kPiD * hz / sr); fl.tk = kq; fl.tm0 = 0.0f; fl.tm1 = gain * kq; fl.tm2 = 0.0f;   // 山 1 の BP（k × v1）× 大きさ
                fl.ramp (snap, 16.0f);
            }
        }
        const float mx = sMix.cur;
        if (snap) { wetW = mx; dryW = 1.0f - mx; wetStep = dryStep = 0.0f; }
        else { wetStep = (mx - wetW) / 16.0f; dryStep = ((1.0f - mx) - dryW) / 16.0f; }
    }
    double sr = 48000.0;
    float k[5] { 0.7f, 0.295f, 0.169155f, 0.605f, 1.0f / 12.0f };
    std::array<std::array<Svf, 3>, 2> flt;
    Smooth sTalk, sSize, sSharp, sMix, sColor;
    float ap[2] {}, wetW = 0.605f, dryW = 0.395f, wetStep = 0.0f, dryStep = 0.0f;
    unsigned ctl = 0;
};
} // namespace aiofx
