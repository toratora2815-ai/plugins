#pragma once
// ROOM VERB … 部屋のリバーブ（Guitar Rig 7 の Studio Reverb を測って合わせ込み、2026-11-18）
//
// 測った結果（cap/verb/StudioReverb、インパルス）:
//   ROOM SIZE … 響きの長さ T60（1 kHz）0.37 / 0.56 / 0.91 / 1.58 / 4.28 s（0 / .25 / .5 / .75 / 1）。間は対数で補間
//   BRIGHT   … 高い音の消え方（4 kHz の T60 が 1 kHz の 0.56 倍 〜 1 倍）
//   TREBLE   … 響きの音色のローパス（2 次のバターワース）200 Hz × 81^つまみ（0.5 で 1.8 kHz、1 で 16 kHz）
//   はじめの響き … 本物は最初の 50 ms に響きの約 7 割が集まる（前が濃い）→ 初期反射のタップを足して合わせた
//   WET      … ドライ = 15/16 × (1 − WET²)。ウェットは下の表
//   PRE-DELAY … 響きの始まりの遅れ
//   STEREO   … 0 = モノの響き、1 以上 = 左右で別の響き
// 中身は 8 本の遅れ線のフィードバック網（FDN）＋入口の拡散。波形そのものは本物と違うが、長さ・高域の消え方・音色・量を合わせる
#include "Module.h"
#include <array>

namespace aiofx
{
struct StudioReverb final : Module
{
    static float t60 (float size)
    {
        static const float tab[5] { 0.37f, 0.56f, 0.91f, 1.58f, 4.28f };
        const float p = std::clamp (size, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p);
        const float f = p - (float) i;
        return std::exp (std::log (tab[i]) + f * (std::log (tab[i + 1]) - std::log (tab[i])));
    }
    static float preMs (float v) { return 186.0f * v; }      // 0.257 → 47.8 ms、1 → 186 ms
    static float trebleHz (float v) { return 200.0f * std::pow (81.0f, v); }   // 0.5 → 1.8 kHz、1 → 16 kHz
    static float hfRatio (float bright)                                        // 4 kHz の T60 ÷ 1 kHz の T60
    {
        static const float tab[5] { 0.50f, 0.70f, 0.84f, 0.95f, 1.0f };
        const float p = std::clamp (bright, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p);
        return tab[i] + (p - (float) i) * (tab[i + 1] - tab[i]);
    }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "studiovb", "ROOM VERB", "REVERB",
            "Smooth room reverb. ROOM SIZE sets the length, BRIGHT how long the highs ring, TREBLE the tone.",
            {
                { "WET",       0.61f,  0, [] (float v) { return fmtPct (v); } },
                { "PRE-DELAY", 0.257f, 0, [] (float v) { return fmt (preMs (v), 1, " ms"); } },
                { "ROOM SIZE", 0.53f,  0, [] (float v) { return fmt (t60 (v), 2, " s"); } },
                { "BRIGHT",    0.64f,  0, [] (float v) { return fmtPct (v); } },
                { "TREBLE",    0.5f,   0, [] (float v) { return fmtHz (trebleHz (v)); } },
                { "STEREO",    1.0f,   2, [] (float v) { return std::string (v >= 0.5f ? "STEREO" : "MONO"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        const double sc = sr / 48000.0;
        static const int base[8] { 1559, 1687, 1801, 1931, 2053, 2203, 2347, 2503 };
        static const int ap[4] { 142, 107, 379, 277 };
        for (int i = 0; i < 8; ++i) { dl[i].assign ((size_t) (base[i] * sc * 1.0) + 64, 0.0f); len[i] = (int) (base[i] * sc); }
        for (int i = 0; i < 4; ++i) { apb[(size_t) i].assign ((size_t) (ap[i] * sc) + 2, 0.0f); }
        preBuf.assign ((size_t) (sr * 0.2) + 4, 0.0f);
        erBuf.assign ((size_t) (sr * 0.06) + 4, 0.0f);
        // 初期反射: 1〜48 ms に 12 本、だんだん小さく、符号を交互に（左右で時刻をずらす）
        for (int t = 0; t < kEr; ++t)
        {
            const double ms = 1.0 + 47.0 * std::pow ((t + 0.5) / kEr, 1.3);
            erTapL[t] = (int) (ms * 0.001 * sr);
            erTapR[t] = (int) (ms * 1.09 * 0.001 * sr) + 7;
            erGain[t] = (float) ((t % 2 ? -1.0 : 1.0) * std::exp (-2.2 * ms / 48.0));
        }
        for (int i = 0; i < 6; ++i) { sm[i].prepare (sr, 0.03); sm[i].set (k[i], true); }
        recalc(); reset();
    }
    void reset() override
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& a : apb) std::fill (a.begin(), a.end(), 0.0f);
        std::fill (preBuf.begin(), preBuf.end(), 0.0f);
        std::fill (erBuf.begin(), erBuf.end(), 0.0f); erPos = 0;
        pos.fill (0); apPos.fill (0); prePos = 0; lp.fill (0.0f);
        tl1L = tl2L = tl1R = tl2R = 0.0f; t1L = t2L = t1R = t2R = 0.0f; h1L = h2L = h1R = h2R = 0.0f;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 6 || same (k[i], v)) return;
        k[i] = v; sm[i].set (v);
        if (i == 5) stereo = v >= 0.5f;   // ROOM SIZE・BRIGHT・TREBLE はなめらかにした値から process の中で計算し直す
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float wetK = sm[0].next(), preK = sm[1].next();
            const float vSize = sm[2].next(), vBright = sm[3].next(), vTreble = sm[4].next();
            if ((counter++ & 31) == 0 && (! sm[2].settled() || ! sm[3].settled() || ! sm[4].settled()))
                recalcFrom (vSize, vBright, vTreble);   // 一気に回してもプチッとしないよう、32 サンプルごとに少しずつ
            // 予備の遅れ
            const float in = 0.5f * (L[s] + R[s]);
            preBuf[(size_t) prePos] = in;
            const int pd = std::min ((int) preBuf.size() - 2, (int) (preMs (preK) * 0.001f * (float) sr));
            int rp = prePos - pd; if (rp < 0) rp += (int) preBuf.size();
            float x = preBuf[(size_t) rp];
            prePos = (prePos + 1) % (int) preBuf.size();
            // 入口の拡散（オールパス 4 段）
            for (int i = 0; i < 4; ++i)
            {
                auto& b = apb[(size_t) i]; const int N = (int) b.size() - 1;
                const float d = b[(size_t) apPos[(size_t) i]];
                const float u = x + 0.6f * d;
                b[(size_t) apPos[(size_t) i]] = u;
                x = d - 0.6f * u;
                apPos[(size_t) i] = (apPos[(size_t) i] + 1) % N;
            }
            // 初期反射（左右で別の時刻のタップ）
            erBuf[(size_t) erPos] = x;
            float erL = 0.0f, erR = 0.0f;
            for (int t = 0; t < kEr; ++t)
            {
                int iL = erPos - erTapL[t]; if (iL < 0) iL += (int) erBuf.size();
                int iR = erPos - erTapR[t]; if (iR < 0) iR += (int) erBuf.size();
                erL += erGain[t] * erBuf[(size_t) iL];
                erR += erGain[t] * erBuf[(size_t) iR];
            }
            erPos = (erPos + 1) % (int) erBuf.size();
            // 8 本の FDN（アダマール行列）
            float o[8];
            for (int i = 0; i < 8; ++i)
            {
                const float v = dl[i][(size_t) pos[(size_t) i]];
                lp[(size_t) i] = flush (v * dampB[i] + lp[(size_t) i] * dampA[i]);   // 高い音ほど早く消える
                o[i] = lp[(size_t) i] * g[i];
            }
            float h[8];
            hadamard (o, h);
            for (int i = 0; i < 8; ++i)
            {
                dl[i][(size_t) pos[(size_t) i]] = flush (h[i] + (i < 4 ? x : -x) * 0.35f);
                pos[(size_t) i] = (pos[(size_t) i] + 1) % len[i];
            }
            float wl = (o[0] + o[2] - o[4] + o[6]) + erAmt * erL, wr = (o[1] - o[3] + o[5] + o[7]) + erAmt * erR;
            if (! stereo) { const float m = 0.5f * (wl + wr); wl = wr = m * 1.414f; }
            // TREBLE（2 次のローパスを 2 回 = 本物の 12 dB/oct）
            wl = biq (wl, tl1L, tl2L); wr = biq (wr, tl1R, tl2R);
            wl = hpf (wl, h1L, h2L); wr = hpf (wr, h1R, h2R);
            const float w2 = wetK * wetK;
            const float dryG = 0.9375f * (1.0f - w2);
            const float wetG = wetGain (wetK) * outScale;
            L[s] = dryG * L[s] + wetG * wl;
            R[s] = dryG * R[s] + wetG * wr;
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    static void hadamard (const float* a, float* b)
    {
        float t[8];
        for (int i = 0; i < 8; i += 2) { t[i] = a[i] + a[i + 1]; t[i + 1] = a[i] - a[i + 1]; }
        float u[8];
        for (int i = 0; i < 8; i += 4) { u[i] = t[i] + t[i + 2]; u[i + 1] = t[i + 1] + t[i + 3]; u[i + 2] = t[i] - t[i + 2]; u[i + 3] = t[i + 1] - t[i + 3]; }
        for (int i = 0; i < 4; ++i) { b[i] = (u[i] + u[i + 4]) * 0.35355339f; b[i + 4] = (u[i] - u[i + 4]) * 0.35355339f; }
    }
    // ウェットの量（WET 0..1 → 大きさ）。本物の響きのエネルギーの差から（0.25: −14.6、0.5: −10.8、0.75: −4.9 dB、1 を 0 dB）
    static float wetGain (float w)
    {
        static const float tab[5] { 0.0f, 0.20f, 0.327f, 0.575f, 1.0f };
        const float p = std::clamp (w, 0.0f, 1.0f) * 4.0f;
        const int i = std::min (3, (int) p);
        return tab[i] + (p - (float) i) * (tab[i + 1] - tab[i]);
    }
    float biq (float x, float& z1, float& z2)
    {
        const float y = tb0 * x + z1;
        z1 = tb1 * x - ta1 * y + z2;
        z2 = tb2 * x - ta2 * y;
        z1 = flush (z1); z2 = flush (z2);
        return y;
    }
    float hpf (float x, float& z1, float& z2)
    {
        const float y = hb0 * x + z1;
        z1 = flush (hb1 * x - ha1 * y + z2);
        z2 = flush (hb2 * x - ha2 * y);
        return y;
    }
    void recalc() { recalcFrom (k[2], k[3], k[4]); }
    void recalcFrom (float vSize, float vBright, float vTreble)
    {
        const float T = t60 (vSize);
        const float ratio = hfRatio (vBright);
        for (int i = 0; i < 8; ++i)
        {
            const float dsec = (float) len[i] / (float) sr;
            g[i] = std::pow (10.0f, -3.0f * dsec / T);
            // 1 次のローパスで 4 kHz の T60 を ratio 倍に: 4 kHz での 1 周の下げを (1/ratio − 1) だけ増やす
            const float extraDb = -60.0f * dsec / T * (1.0f / ratio - 1.0f);   // 4 kHz で足りない分（負）
            const float target = std::pow (10.0f, extraDb / 20.0f);           // 4 kHz での振幅比
            // |H(4k)| = b / |1 − a e^{-jw}| を target に、DC は 1（b = 1 − a）
            const float w = 2.0f * 3.14159265f * 4000.0f / (float) sr;
            float a = 0.0f;
            for (int it = 0; it < 40; ++it)
            {
                const float bb = 1.0f - a;
                const float mag = bb / std::sqrt (1.0f - 2.0f * a * std::cos (w) + a * a);
                if (mag > target) a += 0.5f / (float) (1 << std::min (it, 20)); else a -= 0.5f / (float) (1 << std::min (it, 20));
                a = std::clamp (a, 0.0f, 0.95f);
            }
            dampA[i] = a; dampB[i] = 1.0f - a;
        }
        stereo = k[5] >= 0.5f;
        // TREBLE: 2 次ローパス（Q 0.707）
        const float fc = std::min (trebleHz (vTreble), 0.45f * (float) sr);
        const float w0 = 2.0f * 3.14159265f * fc / (float) sr, c = std::cos (w0), al = std::sin (w0) / (2.0f * 0.7071f);
        const float a0 = 1.0f + al;
        tb0 = (1.0f - c) * 0.5f / a0; tb1 = (1.0f - c) / a0; tb2 = tb0; ta1 = -2.0f * c / a0; ta2 = (1.0f - al) / a0;
        // 響きの量: 本物の WET 1 の響きのエネルギー（−10.5 dB、インパルス 1 に対して）に合わせる係数（ROOM SIZE で少し変わる）
        // 本物は ROOM SIZE を変えても響きのエネルギーがほぼ同じ（長いほど少しだけ大きい）
        outScale = 0.30f * dbToGain (7.8f - 1.35f * std::log (T));
        // 低い音の出すぎを抑える 2 次ハイパス（120 Hz）
        {
            const float w0h = 2.0f * 3.14159265f * 120.0f / (float) sr, ch = std::cos (w0h), alh = std::sin (w0h) / (2.0f * 0.7071f), a0h = 1.0f + alh;
            hb0 = (1.0f + ch) * 0.5f / a0h; hb1 = -(1.0f + ch) / a0h; hb2 = hb0; ha1 = -2.0f * ch / a0h; ha2 = (1.0f - alh) / a0h;
        }
    }
    double sr = 48000.0;
    float k[6] { 0.61f, 0.257f, 0.53f, 0.64f, 0.5f, 1.0f };
    Smooth sm[6];
    static constexpr int kEr = 12;
    std::vector<float> dl[8], apb[4], preBuf, erBuf;
    int erTapL[kEr] {}, erTapR[kEr] {}, erPos = 0;
    float erGain[kEr] {}, erAmt = 0.6f;
    int len[8] {};
    std::array<int, 8> pos {};
    std::array<int, 4> apPos {};
    int prePos = 0;
    std::array<float, 8> lp {};
    float g[8] {}, dampA[8] {}, dampB[8] {};
    bool stereo = true;
    float tb0 = 1, tb1 = 0, tb2 = 0, ta1 = 0, ta2 = 0;
    float tl1L = 0, tl2L = 0, tl1R = 0, tl2R = 0, t1L = 0, t2L = 0, t1R = 0, t2R = 0;
    float outScale = 0.3f;
    unsigned counter = 0;
    float hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0, h1L = 0, h2L = 0, h1R = 0, h2R = 0;
};
} // namespace aiofx
