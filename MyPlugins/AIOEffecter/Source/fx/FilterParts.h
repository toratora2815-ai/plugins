#pragma once
// フィルター（0.8.0〜）。JUCE なし
#include "DelayKit.h"
#include "VerbEngine.h"  // Svf
#include "Oversampler.h"

namespace aiofx
{
// 2 次の状態変数フィルター（TPT）の 3 つの出口を同時に出す形（LP・BP（山 Q 倍）・HP）。係数はサンプルごとに変えてよい
struct Svf3
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    inline void process (float x, float g, float k, float& lp, float& bp, float& hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1); ic2 = flush (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// ---- MORPH FILTER（Guitar Rig の Pro-Filter を測って合わせ込み）----
// 測った結果（cap/mod/ProFilter のピンクノイズから伝達関数を出して、複素数で当てはめ）:
//   1 段 = 2 次の状態変数フィルター。MODE で出口を直線につなぐ: 0 = LP、.25 = 0.5 LP − 0.5 BP、.5 = −BP、.75 = −0.5 BP + 0.5 HP、1 = HP（BP は山が Q 倍の形。0.2〜0.5 dB）
//   SLOPE: 2 段目（LP・BP・HP をそれぞれ同じ角・同じ Q でもう一度通した 4 次の出口を、同じ割合で混ぜたもの）へ移る量 ≈ 0.26 / 0.37 / 0.51 / 0.74 / 0.98
//   （0 / .25 / .5 / .75 / 1 = 12 → 24 dB/oct）
//   FREQ: 角 52 / 194 / 741 / 3024 / 15000 Hz（0 / .25 / .5 / .75 / 1）。RES: Q 0.48 / 0.93 / 1.59 / 2.86 / 4.42 と、全体の大きさ 1.63 / 1.11 / 0.87 / 0.72 / 0.63（山が高いほど下げる）
//   本物は角を上げきったとき高い所がなだらかに落ちる（20 kHz で −12 dB。2 倍で計算しているらしい）→ こちらも 2 倍（ハーフバンド 1 段）で計算。遅れ 3 サンプルを申告。
//   全部の記録（45 通り）で 中央 0.70 dB。合っていない所: RES 0.75〜1 の山（本物は 700 Hz に +28 dB の鋭い山、こちらは +18 dB で広い）、
//   FREQ 1 の BP / HP（10 kHz から上）。ラダー型（2 段 ＋ 全体の戻り）でも RES の端は 2.4 dB → 後の細かい測定で詰める
struct MorphFilter final : Module
{
    static float freqHz (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, lg[5] { 1.7160f, 2.2878f, 2.8698f, 3.4806f, 4.1761f };   // log10 Hz
        return std::pow (10.0f, interpTable (xs, lg, std::clamp (v, 0.0f, 1.0f)));
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "morphflt", "MORPH FILTER", "FILTER",
            "Studio filter that morphs from low-pass through band-pass to high-pass. SLOPE goes from gentle (12 dB) to steep (24 dB).",
            {
                { "FREQ",  1.0f,  0, [] (float v) { return fmtHz (freqHz (v)); } },
                { "RES",   0.33f, 0, [] (float v) { return fmtPct (v); } },
                { "SLOPE", 0.66f, 0, [] (float v) { return fmt (12.0f + 12.0f * std::clamp (v, 0.0f, 1.0f), 0, " dB/oct"); } },
                { "MODE",  0.0f,  0, [] (float v) { return v < 0.125f ? std::string ("LP") : v < 0.375f ? std::string ("LP-BP") : v < 0.625f ? std::string ("BP") : v < 0.875f ? std::string ("BP-HP") : std::string ("HP"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto* q : { &sLogF, &sK, &sGain, &sMix2, &sMode }) q->prepare (sr, 0.02);
        static const float c1[8] { 0.040633460924f, 0.150505129023f, 0.300757055992f, 0.460774504961f,
                                   0.609524314896f, 0.738503841119f, 0.849223810392f, 0.949742783705f };
        for (int c = 0; c < 2; ++c) { up[c].c = c1; dn[c].c = c1; }
        recalc (true); reset();
    }
    void reset() override { for (int c = 0; c < 2; ++c) { a[c].reset(); bl[c].reset(); bb[c].reset(); bh[c].reset(); up[c].reset(); dn[c].reset(); } }
    int latency() const override { return 3; }   // 2 倍のハーフバンド（上げて下げて 3.07 サンプル）
    bool dryAfterReset() const override { return false; }   // ラックの reset のあとドライを混ぜない（フィルターを通らない音が漏れる）
    void setKnob (int i, float v) override { if (i < 0 || i >= 4 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            const float lf = sLogF.next(), kk = sK.next(), gain = sGain.next(), mix2 = sMix2.next(), m = sMode.next();
            const float fc = std::min (std::pow (10.0f, lf), 0.45f * (float) sr);
            const float g = std::tan ((float) kPiD * fc / (float) (2.0 * sr));   // 2 倍の中で
            // MODE: LP → −BP → HP を直線に
            const float cl = std::max (0.0f, 1.0f - 2.0f * m), ch = std::max (0.0f, 2.0f * m - 1.0f), cb = -(1.0f - cl - ch);
            for (int c = 0; c < 2; ++c)
            {
                float u0, u1; up[c].up (io[c][s], u0, u1);
                float* uu[2] { &u0, &u1 };
                for (auto* u : uu)
                {
                    float lp, bp, hp, l2, b2, h2, t1, t2;
                    a[c].process (*u, g, kk, lp, bp, hp);
                    const float o1 = cl * lp + cb * bp + ch * hp;
                    // 2 段目: LP・BP・HP をそれぞれもう一度同じ形に通して（LP²・BP²・HP²）、同じ割合で混ぜる（本物: MODE .25 の SLOPE 1 は低い所 −6 dB のまま、
                    // 高い所は BP² の分が残る = 段ごとに通した 4 次の出口を同じ割合で混ぜる形）
                    bl[c].process (lp, g, kk, l2, t1, t2);
                    bb[c].process (bp, g, kk, t1, b2, t2);
                    bh[c].process (hp, g, kk, t1, t2, h2);
                    const float o2 = cl * l2 + cb * b2 + ch * h2;
                    *u = gain * (o1 + mix2 * (o2 - o1));
                }
                io[c][s] = dn[c].down (u0, u1);
            }
        }
    }

private:
    void recalc (bool snap)
    {
        sLogF.set (std::log10 (freqHz (k[0])), snap);
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, q[5] { 0.48f, 0.93f, 1.59f, 2.86f, 4.42f }, gn[5] { 1.63f, 1.11f, 0.87f, 0.72f, 0.63f }, m2[5] { 0.26f, 0.37f, 0.51f, 0.74f, 0.98f };
        const float r = std::clamp (k[1], 0.0f, 1.0f);
        sK.set (1.0f / interpTable (xs, q, r), snap); sGain.set (interpTable (xs, gn, r), snap);
        sMix2.set (interpTable (xs, m2, std::clamp (k[2], 0.0f, 1.0f)), snap);
        sMode.set (std::clamp (k[3], 0.0f, 1.0f), snap);
    }
    double sr = 48000.0;
    float k[4] { 1.0f, 0.33f, 0.66f, 0.0f };
    Svf3 a[2], bl[2], bb[2], bh[2];
    Polyphase2x<8> up[2], dn[2];
    Smooth sLogF, sK, sGain, sMix2, sMode;
};

// ---- ワウ 2 種（HOWL WAH = Guitar Rig の Cry Wah、VINTAGE WAH = Real Wah を測って合わせ込み）----
// 測った結果（cap/mod/<部品>/Wah_*.npz のピンクノイズの区間から伝達関数。WAH 0, 1/8, …, 1 の 9 か所）:
//   どちらも「全体の大きさ × 2 次（山と谷を自由に置ける形）× 2 次（同じ）× 1 次のハイパス」で 0.03〜0.12 dB（いちばんずれる所で 0.4 dB）。
//   HOWL は 6 Hz まで平ら（ハイパスなし。表の 1 Hz は直流よけだけ = 6 Hz で −0.13 dB。0.045 Hz だと大きな音のあと直流が数秒残った）、
//   VINTAGE は 200〜235 Hz の 1 次ハイパス。8 Hz〜19 kHz で当てはめ。
//   本物は 20 kHz から上が少し下がる（23.5 kHz で −5 dB。聞こえない所なのでまねていない）。遅れは本物が 1 サンプル多い（音は同じ）
//   9 か所をまとめて当てはめて、位置の間で係数がなめらかに動くようにした（係数の対数を位置で直線補間。間の山は両隣の山の間を動く）。
//   STEREO は本物では音が変わらない（入口がモノ）→ OFF で入口の左右をまとめて 1 本で通す形にした
//   表の値は 48 kHz で双一次変換する前のアナログの形（Hz・Q）。ほかのサンプルレートでも 48 kHz と同じ周波数特性になるように変換して使う
struct WahRow { float g, fz1, qz1, fp1, qp1, fz2, qz2, fp2, qp2, fh; };
inline constexpr WahRow kHowlWah[9] {
    { 0.088173f, 555.77f, 0.18214f, 463.58f, 9.7537f, 3485.8f, 2.4815f, 3389.2f, 0.40431f, 1.0f },
    { 0.13743f, 603.71f, 0.20751f, 564.12f, 7.7749f, 3486.4f, 2.4939f, 3311.3f, 0.37202f, 1.0f },
    { 0.23547f, 600.64f, 0.23554f, 686.7f, 6.4375f, 3482.2f, 2.5136f, 3297.9f, 0.33302f, 1.0f },
    { 0.38646f, 619.26f, 0.25036f, 835.36f, 5.4495f, 3485.2f, 2.4633f, 3447.1f, 0.32139f, 1.0f },
    { 0.57703f, 732.12f, 0.23045f, 1017.5f, 4.6233f, 3482.5f, 2.3833f, 4000.6f, 0.34206f, 1.0f },
    { 0.78444f, 884.95f, 0.19902f, 1242.6f, 4.0864f, 3469.8f, 2.3521f, 4554.6f, 0.36515f, 1.0f },
    { 0.97097f, 793.08f, 0.21726f, 1520.0f, 3.8481f, 3489.0f, 2.2939f, 3718.3f, 0.38264f, 1.0f },
    { 1.1951f, 454.56f, 0.3341f, 1832.4f, 3.6677f, 3528.9f, 2.3929f, 1981.7f, 0.35572f, 1.0f },
    { 1.5433f, 243.88f, 0.44449f, 2230.4f, 3.1347f, 3504.6f, 2.5034f, 987.79f, 0.24976f, 1.0f } };
inline constexpr WahRow kVintageWah[9] {
    { 0.014774f, 1481.4f, 0.15601f, 434.22f, 9.1731f, 8271.2f, 0.050000f, 4864.6f, 0.051678f, 201.54f },
    { 0.019488f, 1491.4f, 0.15677f, 445.57f, 8.9422f, 7157.1f, 0.057324f, 4650.6f, 0.053263f, 205.59f },
    { 0.026394f, 1503.6f, 0.15760f, 481.42f, 8.7314f, 6257.6f, 0.064840f, 4367.5f, 0.055799f, 209.72f },
    { 0.037337f, 1516.4f, 0.15839f, 547.73f, 8.3057f, 5529.9f, 0.072545f, 4031.1f, 0.059519f, 213.92f },
    { 0.056192f, 1528.4f, 0.15924f, 656.29f, 7.5867f, 4934.9f, 0.080648f, 3666.5f, 0.064522f, 218.36f },
    { 0.090297f, 1540.3f, 0.16012f, 827.92f, 6.6498f, 4426.4f, 0.089134f, 3280.7f, 0.071153f, 222.68f },
    { 0.15208f,  1550.4f, 0.16101f, 1100.3f, 5.5808f, 3997.9f, 0.098312f, 2873.1f, 0.080383f, 227.04f },
    { 0.25429f,  1560.9f, 0.16209f, 1540.6f, 4.6404f, 3651.0f, 0.10728f,  2416.1f, 0.094684f, 231.35f },
    { 0.38356f,  1607.0f, 0.16012f, 2275.7f, 4.0974f, 3308.1f, 0.11730f,  1946.6f, 0.11619f,  235.33f } };

class WahPedal : public Module
{
public:
    WahPedal (const WahRow* table, float defStereo, float defWah) : tab (table) { k[0] = defStereo; k[1] = defWah; }
    static constexpr double kSweepSec = 0.04;   // ペダルは端から端まで最短 0.04 秒（CUSTOM WAH・VOWEL WAH も同じ値を使う）
    // 48 kHz のアナログの形の周波数 → このサンプルレートの TPT の g（48 kHz と同じ周波数特性になるように）
    static float gOf (double sr, float fa) { return (float) std::tan (std::min (std::atan (kPiD * fa / 48000.0) * 48000.0 / sr, 0.49 * kPiD)); }
    // 表の値（48 kHz で双一次変換する前のアナログの Hz）→ 実際に山が来る周波数（表示用。9.84 kHz の表の値は 8.78 kHz に出る: DSP 検査 0.8.0）
    static float digitalHz (float fa) { return (float) (48000.0 / kPiD * std::atan (kPiD * fa / 48000.0)); }
    // WAH の位置 → 山の周波数のおよそ（表示用）
    static float peakHz (const WahRow* t, float v)
    {
        const float u = std::clamp (v, 0.0f, 1.0f) * 8.0f; const int i = std::min (7, (int) u); const float f = u - (float) i;
        return std::exp (std::log (t[i].fp1) + f * (std::log (t[i + 1].fp1) - std::log (t[i].fp1)));
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        sPos.prepare (sr, 0.008); sMono.prepare (sr, 0.03);
        recalc (true); update (true); reset();
    }
    void reset() override { for (int c = 0; c < 2; ++c) { f1[c].reset(); f2[c].reset(); hp[c].reset(); } }
    void setKnob (int i, float v) override { if (i < 0 || i >= 2 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);   // 16 サンプルごとに係数を出し、その間は直線で動かす
            const float a = sMono.next(), mid = 0.5f * (L[s] + R[s]);
            float x[2] { L[s] + a * (mid - L[s]), R[s] + a * (mid - R[s]) };
            gCur += gStep;
            for (int c = 0; c < 2; ++c)
            {
                f1[c].step(); f2[c].step(); hp[c].step();
                x[c] = gCur * hp[c].process (f2[c].process (f1[c].process (x[c])));
            }
            L[s] = x[0]; R[s] = x[1];
        }
    }

private:
    void recalc (bool snap)
    {
        posGoal = std::clamp (k[1], 0.0f, 1.0f);
        if (snap) { posLin = posGoal; sPos.set (posLin, true); }
        sMono.set (k[0] >= 0.5f ? 0.0f : 1.0f, snap);
    }
    static void setSec (Svf& f, double sr, float fz, float qz, float fp, float qp)
    {
        const float gp = gOf (sr, fp), rz = gOf (sr, fz) / gp;
        f.tg = gp; f.tk = 1.0f / qp; f.tm0 = 1.0f; f.tm1 = rz / qz - 1.0f / qp; f.tm2 = rz * rz - 1.0f;
    }
    void update (bool snap)
    {
        if (! snap)
        {
            // ペダルはかかとからつま先まで最短 kSweepSec 秒で動く（本物のペダルも一瞬では動かない。
            // 一瞬で飛ばすと HOWL の 2 段の中の大きな分が追いつかず、高い所に短い「チッ」が出る: 0.04 秒で 27 → 1.3。
            // 0.08 秒では速いオートメーション（全幅 8 Hz）の振れが 59 % に削られた → 0.04 秒で 93 %: DSP 検査 0.8.0）
            const float maxStep = 16.0f / (float) (kSweepSec * sr);
            posLin += std::clamp (posGoal - posLin, -maxStep, maxStep);
            sPos.set (posLin);
            for (int i = 0; i < 16; ++i) sPos.next();
        }
        const float u = sPos.cur * 8.0f; const int i0 = std::clamp ((int) u, 0, 7); const float fr = std::clamp (u - (float) i0, 0.0f, 1.0f);
        const WahRow& A = tab[i0]; const WahRow& B = tab[i0 + 1];
        auto gi = [fr] (float a, float b) { return std::exp (std::log (a) + fr * (std::log (b) - std::log (a))); };   // 対数で直線
        for (int c = 0; c < 2; ++c)
        {
            setSec (f1[c], sr, gi (A.fz1, B.fz1), gi (A.qz1, B.qz1), gi (A.fp1, B.fp1), gi (A.qp1, B.qp1));
            setSec (f2[c], sr, gi (A.fz2, B.fz2), gi (A.qz2, B.qz2), gi (A.fp2, B.fp2), gi (A.qp2, B.qp2));
            hp[c].set (sr, 48000.0 / kPiD * std::atan (kPiD * gi (A.fh, B.fh) / 48000.0), 0.0, 1.0);
            f1[c].ramp (snap, 16.0f); f2[c].ramp (snap, 16.0f); hp[c].ramp (snap, 16);
        }
        const float gT = gi (A.g, B.g);
        if (snap) { gCur = gT; gStep = 0.0f; } else gStep = (gT - gCur) / 16.0f;
    }
    const WahRow* tab;
    double sr = 48000.0;
    float k[2] { 0.0f, 1.0f }, gCur = 1.0f, gStep = 0.0f, posGoal = 1.0f, posLin = 1.0f;
    Svf f1[2], f2[2];
    TptShelf1 hp[2];
    Smooth sPos, sMono;
    unsigned ctl = 0;
};

struct HowlWah final : WahPedal
{
    HowlWah() : WahPedal (kHowlWah, 0.0f, 1.0f) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "howlwah", "HOWL WAH", "FILTER",
            "Classic crying wah pedal: a narrow peak that sweeps from about 460 Hz (heel) to 2.2 kHz (toe). Automate WAH to rock it.",
            {
                { "STEREO", 0.0f, 2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "WAH",    1.0f, 0, [] (float v) { return fmtHz (digitalHz (peakHz (kHowlWah, v))); } },
            } };
        return i;
    }
};

struct VintageWah final : WahPedal
{
    VintageWah() : WahPedal (kVintageWah, 1.0f, 0.86f) {}
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "vintwah", "VINTAGE WAH", "FILTER",
            "Vintage-voiced wah pedal: darker heel, vocal peak up to about 2.3 kHz, gentle top end. Automate WAH to rock it.",
            {
                { "STEREO", 1.0f,  2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "WAH",    0.86f, 0, [] (float v) { return fmtHz (digitalHz (peakHz (kVintageWah, v))); } },
            } };
        return i;
    }
};

// ---- CUSTOM WAH（Guitar Rig の Wahwah を測って合わせ込み）----
// 測った結果（cap/mod/Wahwah の 197 通りを、1 つずつ「2 次（分子は自由）」に当てはめ = どれも 0.14 dB 以内。build/aiofx の list23 で WAH 0 / .5 / .75 / 1 でも各つまみを動かした）:
//   出口 = DRY × x ＋ k × (αL LP ＋ αB BP ＋ αH HP)（状態変数フィルターの 3 つの出口、k = 1 / Q）
//   周波数・Q・LEVEL: かかと（MIN）とつま先（MAX）の間を、曲線 t(p) = p (2p − 1) ＋ 4p (1 − p) × MID でつなぐ（t(0.5) = MID。MID 0.5 で直線）。
//     周波数は つまみの位置 u で混ぜてから Hz に: u = FREQ MIN ＋ (FREQ MAX ＋ 0.399 − FREQ MIN) × t、Hz = Φ(u)（147 Hz × e^(2.9 u) ほど。上で少し急）
//     k = 0.80 − 0.698 × (RES MIN ＋ (RES MAX − RES MIN) × t)。LEVEL: λ = 1.99 × (3.17^u − 1)、u = LEVEL MIN ＋ (LEVEL MAX − LEVEL MIN) × t
//   α は MODE と λ の表（WAH 0.245 で MODE 0 / .25 / .52 / .75 / 1 × LEVEL MIN 0 / 初期値 / 1 を測った）。MODE 0 = 2 × LP（LEVEL は効かない）、
//     0.5 = BP ＋ 低い所を消す −LP、1 = HP ＋ −BP（鳴きが強い）。DRY = 2 × 1000^(DRY − 1)（0 で無音、0.88 で −1.2 dB、1 で +6 dB）
//   全部の記録で 中央 0.09 dB・9 割 0.42 dB・いちばん悪い所 1.1 dB（つま先で 10 kHz のとき Q が本物より 26 % 高い）。STEREO は本物では音が変わらない → OFF で左右をまとめる
class CustomWah final : public Module
{
public:
    static float uToHz (float u)
    {
        static const float xs[8] { 0.0f, 0.14f, 0.399f, 0.5f, 0.899f, 1.0f, 1.150f, 1.399f };
        static const float lg[8] { 4.99043f, 5.39499f, 6.14654f, 6.43759f, 7.60246f, 7.90455f, 8.35975f, 9.19451f };   // ln Hz
        float l;
        if (u < 0.0f) l = lg[0] + 2.89f * u;
        else if (u > 1.399f) l = lg[7] + 3.3f * (u - 1.399f);
        else l = interpTable (xs, lg, u);
        return std::exp (l);
    }
    static float kOf (float u) { return std::clamp (0.80f - 0.698f * u, 0.02f, 1.6f); }
    static float lamOf (float u) { return std::max (-0.5f, 1.99f * (std::pow (3.17f, u) - 1.0f)); }
    static float dryOf (float v) { return v <= 0.0f ? 0.0f : 2.0f * std::pow (1000.0f, std::min (v, 1.0f) - 1.0f); }
    static float tcurve (float p, float mid) { return p * (2.0f * p - 1.0f) + 4.0f * p * (1.0f - p) * mid; }
    static const ModuleInfo& info()
    {
        auto pctT = [] (float v) { return fmtPct (v); };
        static const ModuleInfo i { "customwah", "CUSTOM WAH", "FILTER",
            "Wah you can shape: set frequency, resonance and level for heel (MIN), middle (MID bends the curve) and toe (MAX). MODE morphs low-pass, band-pass and high-pass.",
            {
                { "STEREO",    1.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "WAH",       0.245f,    0, pctT },
                { "MODE",      0.52f,     0, [] (float v) { return v < 0.125f ? std::string ("LP") : v < 0.375f ? std::string ("LP-BP") : v < 0.625f ? std::string ("BP") : v < 0.875f ? std::string ("BP-HP") : std::string ("HP"); } },
                { "DRY",       0.88f,     0, [] (float v) { return v <= 0.0f ? std::string ("OFF") : fmt (6.0f + 60.0f * (std::min (v, 1.0f) - 1.0f), 1, " dB"); } },
                { "FREQ MIN",  0.14f,     0, [] (float v) { return fmtHz (WahPedal::digitalHz (uToHz (v))); } },
                { "FREQ MID",  0.57f,     0, pctT },
                { "FREQ MAX",  0.60281f,  0, [] (float v) { return fmtHz (WahPedal::digitalHz (uToHz (v + 0.399f))); } },
                { "RES MIN",   0.62211f,  0, [] (float v) { return fmt (1.0f / kOf (v), 1, " Q"); } },
                { "RES MID",   0.5f,      0, pctT },
                { "RES MAX",   0.81f,     0, [] (float v) { return fmt (1.0f / kOf (v), 1, " Q"); } },
                { "LEVEL MIN", 0.65078f,  0, pctT },
                { "LEVEL MID", 0.49437f,  0, pctT },
                { "LEVEL MAX", 0.85f,     0, pctT },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        // つまみは 0.04 秒でならす（DRY は dB で 60 dB 動くので、0.02 秒では DRY 1 → 0 のプチッが 8.5 あった）
        for (auto& q : sk) q.prepare (sr, 0.04);
        sPos.prepare (sr, 0.008); sMono.prepare (sr, 0.03);
        recalc (true); update (true); reset();
    }
    void reset() override { for (auto& f : flt) f.reset(); }
    void setKnob (int i, float v) override { if (i < 0 || i >= 13 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            const float a = sMono.next(), mid = 0.5f * (L[s] + R[s]);
            float x[2] { L[s] + a * (mid - L[s]), R[s] + a * (mid - R[s]) };
            for (int c = 0; c < 2; ++c) { flt[c].step(); x[c] = flt[c].process (x[c]); }
            L[s] = x[0]; R[s] = x[1];
        }
    }

private:
    void recalc (bool snap)
    {
        posGoal = std::clamp (k[1], 0.0f, 1.0f);
        if (snap) { posLin = posGoal; sPos.set (posLin, true); }
        sMono.set (k[0] >= 0.5f ? 0.0f : 1.0f, snap);
        for (int i = 2; i < 13; ++i) sk[(size_t) i].set (std::clamp (k[i], 0.0f, 1.0f), snap);
    }
    // α（LP・BP・HP の係数）を MODE と λ の表から
    static void alphaOf (float mode, float lam, float& aL, float& aB, float& aH)
    {
        static const float lamPts[3] { 0.534f, 2.466f, 4.06f };
        static const float modes[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float tab[5][3][3] {
            { { 1.995f, 0.06f, 0.0f }, { 1.995f, 0.06f, 0.0f }, { 1.995f, 0.06f, 0.0f } },
            { { 0.735f, 1.287f, 0.018f }, { -0.23f, 2.227f, 0.03f }, { -1.03f, 3.0f, 0.05f } },
            { { -0.5343f, 2.512f, 0.0352f }, { -2.4665f, 4.3894f, 0.0674f }, { -4.0609f, 5.9352f, 0.093f } },
            { { -0.53f, 0.912f, 1.62f }, { -2.46f, -0.603f, 5.0f }, { -4.05f, -3.255f, 9.18f } },
            { { -0.53f, -0.69f, 3.2f }, { -2.46f, -5.59f, 9.93f }, { -4.05f, -12.44f, 18.27f } } };
        const float m = std::clamp (mode, 0.0f, 1.0f) * 4.0f; const int mi = std::min (3, (int) m); const float mw = m - (float) mi;
        // λ は 3 点の折れ線（外は端の傾きで延ばす）
        const int li = lam < lamPts[1] ? 0 : 1; const float lw = (lam - lamPts[li]) / (lamPts[li + 1] - lamPts[li]);
        float out[3];
        for (int j = 0; j < 3; ++j)
        {
            const float a0 = tab[mi][li][j] + lw * (tab[mi][li + 1][j] - tab[mi][li][j]);
            const float a1 = tab[mi + 1][li][j] + lw * (tab[mi + 1][li + 1][j] - tab[mi + 1][li][j]);
            out[j] = a0 + mw * (a1 - a0);
        }
        aL = out[0]; aB = out[1]; aH = out[2];
        (void) modes;
    }
    void update (bool snap)
    {
        if (! snap)
        {
            const float maxStep = 16.0f / (float) (WahPedal::kSweepSec * sr);   // ペダルは端から端まで最短 0.04 秒（HOWL WAH と同じ）
            posLin += std::clamp (posGoal - posLin, -maxStep, maxStep);
            sPos.set (posLin);
            for (int i = 0; i < 16; ++i) { sPos.next(); for (int j = 2; j < 13; ++j) sk[(size_t) j].next(); }
        }
        const float p = sPos.cur;
        float v[13]; for (int j = 2; j < 13; ++j) v[j] = sk[(size_t) j].cur;
        const float tf = tcurve (p, v[5]), tr = tcurve (p, v[8]), tl = tcurve (p, v[11]);
        const float uf = v[4] + (v[6] + 0.399f - v[4]) * tf, ur = v[7] + (v[9] - v[7]) * tr, ul = v[10] + (v[12] - v[10]) * tl;
        // RES の位置は 1 まで（MID の曲線で 1 を超えると k が 0 に近づき Q が 9.8 → 50 に跳ねた: DSP 検査 0.8.0。本物の記録は 1 の内側だけ）
        const float hz = std::clamp (uToHz (uf), 20.0f, 20000.0f), kk = kOf (std::min (ur, 1.0f)), lam = lamOf (ul), d = dryOf (v[3]);
        float aL, aB, aH; alphaOf (v[2], lam, aL, aB, aH);
        const float g = WahPedal::gOf (sr, hz);
        for (auto& f : flt)
        {
            f.tg = g; f.tk = kk; f.tm0 = d + kk * aH; f.tm1 = kk * (aB - kk * aH); f.tm2 = kk * (aL - aH);
            f.ramp (snap, 16.0f);
        }
    }
    double sr = 48000.0;
    float k[13] { 1.0f, 0.245f, 0.52f, 0.88f, 0.14f, 0.57f, 0.60281f, 0.62211f, 0.5f, 0.81f, 0.65078f, 0.49437f, 0.85f };
    float posGoal = 0.245f, posLin = 0.245f;
    std::array<Smooth, 13> sk;
    Smooth sPos, sMono;
    Svf flt[2];
    unsigned ctl = 0;
};

// ---- VOWEL WAH（Guitar Rig の Talk Wah を測って合わせ込み）----
// 測った結果（cap/mod/TalkWah、WAH を 1/16 ずつ 17 か所・SIZE × WAH・BRIGHT × WAH / SIZE）:
//   帯域通過（山の高さで正規化した BP）4 本の並列: 母音の山 F1・F2・F3 と広い山 F4（8〜10 kHz、Q 0.3）。0.2〜0.35 dB（いちばんずれる所で 1 dB）
//   WAH で母音が U → O → A → E → I と動く（0 / .25 / .5 / .75 / 1。F1 168 → 462 → 681 → 498 → 168 Hz、F2 825 → 1071 → 1438 → 1877 → 2242 Hz）。表は 1/16 ごとに測った値
//   SIZE は F1 だけを 3.185^(SIZE − 0.49) 倍に動かす（どの WAH でも同じ割合）。OUTPUT = 30 × (OUTPUT − 0.5) dB（±15 dB）
//   BRIGHT（1〜3 は同じ音 → ON / OFF）= 全体に「f / 900 Hz」の傾き（+6 dB/oct、16 kHz まで 0.4 dB）を掛ける
//     → 各帯域の出口を BP から HP（× f_i / (900 Q_i)）に替える = 傾きを掛けたのと同じ（高い所で無限に上がる微分を作らなくてよい）。
//     ただし双一次変換で傾きが高い所で強くなる（16 kHz で +4.4 dB）→ 出口に (1 + 0.43 z⁻¹) / 1.43 をかけて戻す（8 / 12 / 16 kHz で 0.2 / 0.3 / 0.0 dB 以内）
//   STEREO は本物では音が変わらない → OFF で左右をまとめる
struct VowelRow { float f1, q1, g1, f2, q2, g2, f3, q3, g3, f4, q4, g4; };
inline constexpr VowelRow kVowel[17] {
    { 167.99f, 7.002f, 11.176f, 824.92f, 12.8f, 5.2364f, 2066.7f, 26.612f, 3.0667f, 8055.7f, 0.3f, 0.027661f },
    { 216.28f, 7.0802f, 11.286f, 880.57f, 12.771f, 5.2274f, 2112.8f, 26.614f, 3.0721f, 7919.8f, 0.3f, 0.030476f },
    { 278.48f, 7.1337f, 11.362f, 940.06f, 12.643f, 5.1919f, 2159.8f, 26.413f, 3.0625f, 7932.4f, 0.3f, 0.034726f },
    { 358.71f, 7.1166f, 11.327f, 1003.6f, 12.464f, 5.1432f, 2207.9f, 25.934f, 3.0261f, 8093.1f, 0.3f, 0.041246f },
    { 462.05f, 7.1159f, 11.314f, 1071.4f, 12.25f, 5.0958f, 2257.3f, 25.363f, 2.9848f, 8347.6f, 0.3f, 0.050125f },
    { 595.31f, 7.1413f, 11.317f, 1143.8f, 12.005f, 5.0587f, 2307.8f, 24.784f, 2.9492f, 8547.8f, 0.3f, 0.060739f },
    { 655.86f, 7.1519f, 11.314f, 1231.1f, 11.945f, 5.053f, 2338.7f, 24.497f, 2.942f, 8622.5f, 0.3f, 0.066289f },
    { 668.15f, 7.1383f, 11.301f, 1330.5f, 12.044f, 5.064f, 2359.4f, 24.21f, 2.9238f, 8647.6f, 0.3f, 0.068834f },
    { 680.66f, 7.1271f, 11.293f, 1438.1f, 12.162f, 5.0771f, 2380.5f, 23.978f, 2.9165f, 8664.9f, 0.3f, 0.071535f },
    { 693.42f, 7.1154f, 11.283f, 1554.3f, 12.301f, 5.0968f, 2401.8f, 23.614f, 2.9013f, 8685.5f, 0.3f, 0.074341f },
    { 706.4f, 7.1078f, 11.277f, 1680.1f, 12.47f, 5.122f, 2423.3f, 23.133f, 2.8808f, 8709.1f, 0.3f, 0.077378f },
    { 653.18f, 7.1012f, 11.29f, 1795.9f, 12.759f, 5.1729f, 2483.2f, 22.753f, 2.8633f, 8726.9f, 0.3f, 0.076232f },
    { 497.7f, 7.0882f, 11.317f, 1877.1f, 13.163f, 5.2695f, 2625.2f, 22.802f, 2.8517f, 8869.8f, 0.3f, 0.068921f },
    { 379.36f, 7.0445f, 11.296f, 1962.1f, 13.374f, 5.3115f, 2775.9f, 22.751f, 2.8372f, 9210.9f, 0.3f, 0.065428f },
    { 289.23f, 6.9641f, 11.225f, 2051.2f, 13.338f, 5.2707f, 2935.6f, 22.772f, 2.8245f, 9508.1f, 0.3f, 0.064602f },
    { 220.54f, 6.8554f, 11.098f, 2144.5f, 13.266f, 5.2139f, 3104.7f, 22.923f, 2.8246f, 9641.5f, 0.3f, 0.063596f },
    { 168.15f, 6.7621f, 10.973f, 2241.9f, 13.194f, 5.1702f, 3284.5f, 22.986f, 2.8265f, 9801.2f, 0.31953f, 0.062292f } };

struct VowelWah final : Module
{
    static std::string vowelName (float v)
    {
        static const char* n[5] { "U", "O", "A", "E", "I" };
        return n[std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 4.0f), 0, 4)];
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "vowelwah", "VOWEL WAH", "FILTER",
            "Talking wah: WAH glides the vowel U-O-A-E-I, SIZE moves the first vowel peak (small to big mouth), BRIGHT tilts it brighter.",
            {
                { "STEREO", 0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "WAH",    0.454876f, 0, [] (float v) { return vowelName (v); } },
                { "SIZE",   0.49f,     0, [] (float v) { return fmtPct (v); } },
                { "OUTPUT", 0.5f,      0, [] (float v) { return fmt (30.0f * (v - 0.5f), 1, " dB"); } },
                { "BRIGHT", 0.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        sPos.prepare (sr, 0.008); sMono.prepare (sr, 0.03);
        for (auto* q : { &sSize, &sOut, &sBright }) q->prepare (sr, 0.03);
        recalc (true); update (true); reset();
    }
    void reset() override { for (auto& c : band) for (auto& f : c) f.reset(); prev[0] = prev[1] = 0.0f; }
    void setKnob (int i, float v) override { if (i < 0 || i >= 5 || same (k[i], v)) return; k[i] = v; recalc (false); }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 15u) == 0u) update (false);
            const float a = sMono.next(), mid = 0.5f * (L[s] + R[s]);
            const float x[2] { L[s] + a * (mid - L[s]), R[s] + a * (mid - R[s]) };
            float y[2];
            const float bb = 0.43f * brNow;   // BRIGHT の高い所の戻し（BRIGHT OFF では素通し）
            for (int c = 0; c < 2; ++c)
            {
                float acc = 0.0f;
                for (auto& f : band[c]) { f.step(); acc += f.process (x[c]); }
                y[c] = (acc + bb * prev[c]) / (1.0f + bb);
                prev[c] = acc;
            }
            L[s] = y[0]; R[s] = y[1];
        }
    }

private:
    void recalc (bool snap)
    {
        posGoal = std::clamp (k[1], 0.0f, 1.0f);
        if (snap) { posLin = posGoal; sPos.set (posLin, true); }
        sMono.set (k[0] >= 0.5f ? 0.0f : 1.0f, snap);
        sSize.set (std::clamp (k[2], 0.0f, 1.0f), snap);
        sOut.set (dbToGain (30.0f * (std::clamp (k[3], 0.0f, 1.0f) - 0.5f)), snap);
        sBright.set (k[4] >= 0.5f ? 1.0f : 0.0f, snap);
    }
    void update (bool snap)
    {
        if (! snap)
        {
            const float maxStep = 16.0f / (float) (WahPedal::kSweepSec * sr);   // ペダルは端から端まで最短 0.04 秒（ほかのワウと同じ）
            posLin += std::clamp (posGoal - posLin, -maxStep, maxStep);
            sPos.set (posLin);
            for (int i = 0; i < 16; ++i) { sPos.next(); sSize.next(); sOut.next(); sBright.next(); }
        }
        const float u = std::clamp (sPos.cur, 0.0f, 1.0f) * 16.0f; const int i0 = std::min (15, (int) u); const float fr = u - (float) i0;
        const VowelRow& A = kVowel[i0]; const VowelRow& B = kVowel[i0 + 1];
        auto lgi = [fr] (float a, float b) { return std::exp (std::log (a) + fr * (std::log (b) - std::log (a))); };
        auto lin = [fr] (float a, float b) { return a + fr * (b - a); };
        const float sizeMul = std::pow (3.185f, sCur (sSize) - 0.49f), out = sCur (sOut), br = sCur (sBright);
        brNow = br;
        const float f[4] { lgi (A.f1, B.f1) * sizeMul, lgi (A.f2, B.f2), lgi (A.f3, B.f3), lgi (A.f4, B.f4) };
        const float q[4] { lin (A.q1, B.q1), lin (A.q2, B.q2), lin (A.q3, B.q3), lin (A.q4, B.q4) };
        const float g[4] { lin (A.g1, B.g1), lin (A.g2, B.g2), lin (A.g3, B.g3), lin (A.g4, B.g4) };
        for (int j = 0; j < 4; ++j)
        {
            const float hz = std::clamp (f[j], 20.0f, 0.45f * (float) sr), kk = 1.0f / q[j], gg = WahPedal::gOf (sr, hz);
            const float nb = out * g[j];                              // BP（山 1）× g
            const float hb = out * g[j] * hz / (900.0f * q[j]);       // BRIGHT: HP × g f / (900 Q)
            for (int c = 0; c < 2; ++c)
            {
                auto& fl = band[c][(size_t) j];
                // 出口 = (1 − br) × nb × k v1 ＋ br × hb × (x − k v1 − v2)
                fl.tg = gg; fl.tk = kk;
                fl.tm0 = br * hb; fl.tm1 = (1.0f - br) * nb * kk - br * hb * kk; fl.tm2 = -br * hb;
                fl.ramp (snap, 16.0f);
            }
        }
    }
    static float sCur (const Smooth& q) { return q.cur; }
    double sr = 48000.0;
    float k[5] { 0.0f, 0.454876f, 0.49f, 0.5f, 0.0f };
    float posGoal = 0.454876f, posLin = 0.454876f, brNow = 0.0f, prev[2] {};
    std::array<std::array<Svf, 4>, 2> band;
    Smooth sPos, sMono, sSize, sOut, sBright;
    unsigned ctl = 0;
};
} // namespace aiofx
