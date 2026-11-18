#pragma once
// SPRING … アンプの中のスプリングリバーブ（Guitar Rig 7 の Spring Reverb を測って合わせ込み、2026-11-18）
//
// 測った結果（cap/verb/SpringReverb、インパルス）:
//   SPRING LENGTH … 最初の音が届くまで 55 ms × つまみ（0.25 → 14 ms、1 → 55 ms）。こちらは 2 ms より短くしない（短すぎる輪は共鳴する）
//   SIZE     … 響きの長さ T60（1 kHz）0.12 / 0.70 / 2.67 / 5.63 / 7.23 s（0 / .25 / .5 / .75 / 1）
//   BASS     … 低い音と高い音の傾き（0 = 細くて明るい、1 = 太くて暗い）
//   LOW DAMP … 低い音が早く消える量、HIGH DAMP … 高い音が早く消える量
//   MIX      … ドライ = 15/16 × (1 − MIX²)
//   音色      … 低い音が大きく下がった（63 Hz で −30 dB、250 Hz で −12 dB、1 kHz 基準）明るい響き
// 中身は、ばねの「ビヨン」（低い音ほど遅れて届く）をオールパスの列で作ったフィードバックの輪 × 左右 2 本
//
// 安全（DSP 検査 2026-11-18）: 輪の中に直流止め（25 Hz）、1 周の大きさは 0 Hz を含む全部の周波数の最大でそろえる、輪の中の頭打ち（1 を超えない）。
// つまみは全部なめらかに（32 サンプルごとに係数を計算し直す）、LENGTH は端数の遅れをなめらかに動かす。
// オールパスは 48 kHz の 1 サンプルぶん（K = レート / 48 kHz）の遅れで並べる = レートを変えても響きが同じ
#include "Module.h"
#include <array>
#include <complex>

namespace aiofx
{
struct SpringReverb final : Module
{
    static float t60 (float size)
    {
        static const float tab[5] { 0.12f, 0.70f, 2.67f, 5.63f, 7.23f };
        const float p = std::clamp (size, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p);
        const float f = p - (float) i;
        return std::exp (std::log (tab[i]) + f * (std::log (tab[i + 1]) - std::log (tab[i])));
    }
    static float lengthMs (float v) { return std::max (2.0f, 55.0f * v); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "spring", "SPRING", "REVERB",
            "Amp-style spring reverb with the typical 'boing'. SIZE = length of the ring.",
            {
                { "SIZE",      0.10f,  0, [] (float v) { return fmt (t60 (v), 2, " s"); } },
                { "MIX",       1.0f,   0, [] (float v) { return fmtPct (v); } },
                { "LENGTH",    0.257f, 0, [] (float v) { return fmt (lengthMs (v), 1, " ms"); } },
                { "BASS",      0.335f, 0, [] (float v) { return fmtPct (v); } },
                { "LOW DAMP",  0.5f,   0, [] (float v) { return fmtPct (v); } },
                { "HIGH DAMP", 0.98f,  0, [] (float v) { return fmtPct (v); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        apK = std::clamp ((int) std::lround (sr / 48000.0), 1, kMaxK);
        for (auto& d : dl) d.assign ((size_t) (sr * 0.07) + 16, 0.0f);
        for (int i = 0; i < 6; ++i) { sm[i].prepare (sr, 0.03); sm[i].set (k[i], true); }
        for (int c = 0; c < 2; ++c) { dsm[c].prepare (sr, 0.08); dsm[c].set (delaySamples (c, k[2]), true); }
        dcR = (float) std::exp (-2.0 * 3.14159265358979 * 25.0 / sr);
        recalc (k); reset();
    }
    void reset() override
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& c : apx) for (auto& s : c) s.fill (0.0f);
        for (auto& c : apy) for (auto& s : c) s.fill (0.0f);
        apIdx = 0;
        pos.fill (0); fb.fill (0.0f); hpz.fill (0.0f); lpz.fill (0.0f); lsz.fill (0.0f); hsz.fill (0.0f);
        e1.fill (0.0f); e2.fill (0.0f); f1.fill (0.0f); f2.fill (0.0f); dcx.fill (0.0f); dcy.fill (0.0f);
        counter = 0;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 6 || same (k[i], v)) return;
        k[i] = v; sm[i].set (v);
        if (i == 2) for (int c = 0; c < 2; ++c) dsm[c].set (delaySamples (c, v));
        dirty = true;
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            float v[6];
            for (int i = 0; i < 6; ++i) v[i] = sm[i].next();
            // 係数はなめらかにした値から 32 サンプルごとに（つまみが止まっている間は計算しない）
            if ((counter++ & 31) == 0 && (dirty || ! sm[0].settled() || ! sm[2].settled() || ! sm[3].settled() || ! sm[4].settled() || ! sm[5].settled()))
            {
                recalc (v);
                dirty = ! (sm[0].settled() && sm[2].settled() && sm[3].settled() && sm[4].settled() && sm[5].settled());
            }
            const float mixK = v[1];
            const float in = 0.5f * (L[s] + R[s]);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                // 輪: 入口 + 戻り → ばねの分散（オールパスの列）→ 遅れ線 → 直流止め → 低い音・高い音の消え方 → 頭打ち → 戻り
                float x = in + fb[(size_t) c];
                auto& xs = apx[(size_t) c];
                auto& ys = apy[(size_t) c];
                for (int a = 0; a < kAp; ++a)
                {
                    // (a + z^-K) / (1 + a z^-K)
                    const float y = apA * x + xs[(size_t) a][(size_t) apIdx] - apA * ys[(size_t) a][(size_t) apIdx];
                    xs[(size_t) a][(size_t) apIdx] = x;
                    ys[(size_t) a][(size_t) apIdx] = flush (y);
                    x = y;
                }
                auto& d = dl[(size_t) c];
                const int N = (int) d.size();
                d[(size_t) pos[(size_t) c]] = flush (x);
                float y = readFrac (d, pos[(size_t) c], std::min ((float) (N - 4), dsm[c].next()), N);
                pos[(size_t) c] = (pos[(size_t) c] + 1) % N;
                // 直流止め（1 次ハイパス 25 Hz）
                const float hp = y - dcx[(size_t) c] + dcR * dcy[(size_t) c];
                dcx[(size_t) c] = y; dcy[(size_t) c] = flush (hp); y = hp;
                // 低い音の消え方（400 Hz より下の棚）・高い音の消え方（1 次ローパス。いつも回す）
                hpz[(size_t) c] = flush (hpz[(size_t) c] + lowCoef * (y - hpz[(size_t) c]));
                y = y + lowAmt * hpz[(size_t) c];
                lpz[(size_t) c] = flush (lpz[(size_t) c] + highCoef * (y - lpz[(size_t) c]));
                y = lpz[(size_t) c];
                fb[(size_t) c] = guard (y * loopG);
                out[c] = y;
            }
            apIdx = (apIdx + 1) % apK;
            // 出口の音色: ハイパス（2 次）＋ BASS の傾き
            float wl = eqOut (out[0], 0), wr = eqOut (out[1], 1);
            const float dryG = 0.9375f * (1.0f - mixK * mixK);
            const float wetG = wetGain (mixK);
            L[s] = dryG * L[s] + wetG * wl;
            R[s] = dryG * R[s] + wetG * wr;
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    static constexpr int kAp = 24, kMaxK = 8;
    // 目標の遅れは整数のサンプルに（端数で読み続けると補間が 1 周ごとに高い音を削り、長い SIZE の響きが短くなる。端数は LENGTH を動かしている間だけ）
    float delaySamples (int c, float lenKnob) const { return std::round (lengthMs (lenKnob) * 0.001f * (float) sr * (c == 0 ? 1.0f : 1.07f)); }
    static float guard (float x)
    {
        // 念のための上限: 4 から曲がり始めて 5 で頭打ち（輪の中はギターの山の約 3 倍まで行くので、ふつう〜熱い音では素通し。
        // 発振止めは直流止めと正規化で済んでいる。0.8 だと 0 dBFS のギターで響きがつぶれた: DSP 再検査 2026-11-18）
        const float a = std::abs (x);
        if (a <= 4.0f) return x;
        const float o = 4.0f + std::tanh (a - 4.0f);
        return x < 0.0f ? -o : o;
    }
    static float readFrac (const std::vector<float>& b, int pos, float delay, int N)
    {
        const float rp = (float) pos - delay;
        const float base = std::floor (rp);
        const float fr = rp - base;
        int i0 = (int) base;
        while (i0 < 0) i0 += N;
        const int i1 = (i0 + 1) % N;
        return b[(size_t) (i0 % N)] + fr * (b[(size_t) i1] - b[(size_t) (i0 % N)]);
    }
    float eqOut (float x, int c)
    {
        // 低い音を切る 1 次ハイパス 2 段（BASS が 0.35 より上で角が下がる）
        e1[(size_t) c] = flush (e1[(size_t) c] + hpA * (x - e1[(size_t) c]));
        float y = x - e1[(size_t) c];
        e2[(size_t) c] = flush (e2[(size_t) c] + hpB * (y - e2[(size_t) c]));
        y = y - e2[(size_t) c];
        // BASS の傾き: 3 kHz より下を下げる／上げる（高い所はそのまま）と、BASS が上がると入る 5 kHz の 1 次ローパス
        lsz[(size_t) c] = flush (lsz[(size_t) c] + shelfCoef * (y - lsz[(size_t) c]));
        y = lowGain * lsz[(size_t) c] + (y - lsz[(size_t) c]);
        f1[(size_t) c] = flush (f1[(size_t) c] + lp5Coef * (y - f1[(size_t) c]));
        y = y + lp5Mix * (f1[(size_t) c] - y);
        // LOW DAMP の出口の低音（250 Hz の棚）・HIGH DAMP の出口の高音（2.5 kHz の棚）
        f2[(size_t) c] = flush (f2[(size_t) c] + ldCoef * (y - f2[(size_t) c]));
        y = ldGain * f2[(size_t) c] + (y - f2[(size_t) c]);
        hsz[(size_t) c] = flush (hsz[(size_t) c] + hdCoef * (y - hsz[(size_t) c]));
        return (hsz[(size_t) c] + hdGain * (y - hsz[(size_t) c])) * levelFix;
    }
    static float interp (const float* tab, int n, float v)
    {
        const float p = std::clamp (v, 0.0f, 1.0f) * (float) (n - 1);
        const int i = std::min (n - 2, (int) p);
        return tab[i] + (p - (float) i) * (tab[i + 1] - tab[i]);
    }
    // 輪の中のフィルター（直流止め・低い音の棚・高い音のローパス）の大きさ（周波数 f）
    float loopMag (float f, float lowA, float highC) const
    {
        const double w = 2.0 * 3.14159265358979 * f / sr;
        const std::complex<double> z = std::polar (1.0, -w);
        const std::complex<double> dc = (1.0 - z) / (1.0 - (double) dcR * z);
        const std::complex<double> lp = (double) lowCoef / (1.0 - (1.0 - (double) lowCoef) * z);
        std::complex<double> h = dc * (1.0 + (double) lowA * lp);
        h *= (double) highC / (1.0 - (1.0 - (double) highC) * z);
        return (float) std::abs (h);
    }
    void recalc (const float* v)
    {
        const float T = t60 (v[0]);
        // LOW DAMP: 400 Hz より下を 1 周ごとに +0.7 dB（0）〜 −1.2 dB（1）
        lowCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 400.0 / sr));
        lowAmt = dbToGain (0.7f - 1.9f * v[4]) - 1.0f;
        // HIGH DAMP: 1 次ローパス 7.5 kHz × (1 + 6 (1 − HD)²)。ナイキストに近づいたら素通し（係数 1）でも回し続ける
        const float hf = 7500.0f * (1.0f + 6.0f * (1.0f - v[5]) * (1.0f - v[5]));
        highCoef = std::min (1.0f, (float) (1.0 - std::exp (-2.0 * 3.14159265 * std::min ((double) hf, 0.49 * sr) / sr)));
        if (hf >= 20000.0f) highCoef = 1.0f;
        // 1 周の時間（遅れ線 + オールパスの遅れ ≒ 1 kHz で）。輪のフィルターの全部の周波数の最大（0 Hz を含む）で SIZE の T60 になるように
        const float loopSec = lengthMs (v[2]) * 0.001f + 0.0012f;
        // 直流止めがあるので 0 Hz は 0。低い所の棚の持ち上げは細かい周波数で拾う（最大の 0.2 % 増しで割る = どの周波数でも 1 周の大きさが 1 を超えない）
        float mx = 0.0f;
        for (float f : { 0.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f, 50.0f, 60.0f, 75.0f, 90.0f, 110.0f, 130.0f, 160.0f, 200.0f,
                         250.0f, 300.0f, 400.0f, 500.0f, 700.0f, 1000.0f, 1500.0f, 2000.0f, 3000.0f, 4000.0f, 6000.0f, 8000.0f, 12000.0f })
            mx = std::max (mx, loopMag (f, lowAmt, highCoef));
        loopG = std::min (0.999f, std::pow (10.0f, -3.0f * loopSec / T) / std::max (mx * 1.002f, 1.0e-3f));
        // 出口の音色
        const float sB = std::pow (10.0f, -1.54f * std::max (0.0f, v[3] - 0.35f));
        hpA = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 750.0 * sB / sr));
        hpB = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 380.0 * sB / sr));
        static const float shelfTab[5] { -7.0f, -3.4f, 3.4f, 4.6f, 4.7f };   // BASS 0 / .25 / .5 / .75 / 1: 3 kHz より下の上げ下げ
        lowGain = dbToGain (interp (shelfTab, 5, v[3]));
        shelfCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 3000.0 / sr));
        lp5Coef = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 5000.0 / sr));
        lp5Mix = std::clamp ((v[3] - 0.2f) / 0.2f, 0.0f, 1.0f);
        // LOW DAMP の出口の低音: 0 → +3 dB、0.5 → 0、1 → −9 dB
        static const float ldTab[3] { 3.0f, 0.0f, -9.0f };
        ldGain = dbToGain (interp (ldTab, 3, v[4]));
        ldCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 250.0 / sr));
        // HIGH DAMP の出口の高音: 0 / .25 / .5 / .75 / 1 → +5 / +5 / +4 / +2.6 / 0 dB
        static const float hdTab[5] { 5.0f, 5.0f, 4.0f, 2.6f, 0.0f };
        hdGain = dbToGain (interp (hdTab, 5, v[5]));
        hdCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265 * 2500.0 / sr));
        // 量: 本物は SIZE が長くなってもあまり大きくならない（T60 の −0.138 乗）。BASS ごとの量の補正（本物との差を測って）: BASS 0 / .25 / .335 / .5 / .75 / 1
        static const float bx[6] { 0.0f, 0.25f, 0.335f, 0.5f, 0.75f, 1.0f };
        static const float by[6] { -7.2f, -0.6f, 1.2f, -0.1f, -1.2f, -1.5f };
        float bfix = by[5];
        for (int i = 0; i < 5; ++i)
            if (v[3] <= bx[i + 1]) { bfix = by[i] + (v[3] - bx[i]) / (bx[i + 1] - bx[i]) * (by[i + 1] - by[i]); break; }
        levelFix = 0.767f * std::pow (std::max (T, 0.05f) / 0.28f, -0.138f) * dbToGain (bfix);
    }
    // ウェットの量（Room Verb と同じ形）
    static float wetGain (float w)
    {
        static const float tab[5] { 0.0f, 0.20f, 0.327f, 0.575f, 1.0f };
        return interp (tab, 5, w);
    }
    double sr = 48000.0;
    float k[6] { 0.10f, 1.0f, 0.257f, 0.335f, 0.5f, 0.98f };
    Smooth sm[6], dsm[2];
    std::array<std::vector<float>, 2> dl;
    std::array<std::array<std::array<float, kMaxK>, kAp>, 2> apx {}, apy {};
    int apK = 1, apIdx = 0;
    std::array<int, 2> pos {};
    std::array<float, 2> fb {}, hpz {}, lpz {}, lsz {}, hsz {}, e1 {}, e2 {}, f1 {}, f2 {}, dcx {}, dcy {};
    float loopG = 0.5f, apA = -0.62f, lowAmt = 0.0f, lowCoef = 0.04f, highCoef = 0.5f, dcR = 0.997f;
    float hpA = 0.07f, hpB = 0.04f, lowGain = 1.0f, shelfCoef = 0.3f, lp5Coef = 0.5f, lp5Mix = 0.0f;
    float ldGain = 1.0f, ldCoef = 0.03f, hdGain = 1.0f, hdCoef = 0.3f, levelFix = 1.0f;
    unsigned counter = 0;
    bool dirty = true;
};
} // namespace aiofx
