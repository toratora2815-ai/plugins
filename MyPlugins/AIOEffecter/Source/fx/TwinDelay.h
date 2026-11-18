#pragma once
// DUAL DELAY … 左右別々の時間のディレイ（Guitar Rig 7 の Twin Delay を測って合わせ込み、2026-11-18）
//
// 測った結果（build/aiofx/td、インパルスで反射の時刻と大きさ）:
//   TIME    … 1 ms + 1999 ms × つまみ²（0.1 ms 以内）
//   PREDELAY … くり返しの外の遅れ 2000 ms × つまみ
//   LEVEL   … 出口の大きさ = つまみ（直線）
//   FEEDBACK … くり返しの大きさ = つまみ（1 で減らない）
//   CROSS   … 反対側の出力をくり返しへ混ぜる量 = つまみ（本物は 0.5 で頭打ち）
//   WIDTH   … 0 = 左右入れ替え、0.5 = モノ、1 = そのまま
//   MIX     … ドライ = min(1, 2 (1 − m))、ウェット = min(1, 2 m)。どちらにも 15/16（−0.56 dB）が掛かる
//   入口     … 本物は左だけを使う。ここでは (L + R) / 2（ギターのモノの音なら同じ）
//   SYNC     … ON で TIME を音符に（つまみを 16 等分: 1/32, 1/16T, 1/32., 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/2T, 1/4., 1/2, 1/1T, 1/2., 1/1。
//             本物を 120 BPM で測って 0 → 62.5 ms、0.25 → 166.7、0.5 → 375、0.5666 → 500、0.75 → 1000、1 → 2000 ms）。本物の初期値は ON
//   遅れ      … 本物は輪の中に 1 サンプル余分な遅れがある（くり返しの間隔が TIME + 1 サンプル）。端数は 4 点のなめらかな補間
#include "Module.h"

namespace aiofx
{
struct TwinDelay final : Module
{
    static float timeMs (float v)  { return 1.0f + 1999.0f * v * v; }
    static int noteIndex (float v) { return std::clamp ((int) std::floor (v * 16.0f), 0, 15); }
    static float noteQuarters (float v)
    {
        static const float q[16] { 1.0f / 8, 1.0f / 6, 3.0f / 16, 1.0f / 4, 1.0f / 3, 3.0f / 8, 1.0f / 2, 2.0f / 3,
                                   3.0f / 4, 1.0f, 4.0f / 3, 3.0f / 2, 2.0f, 8.0f / 3, 3.0f, 4.0f };
        return q[noteIndex (v)];
    }
    static const char* noteName (float v)
    {
        static const char* n[16] { "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1T", "1/2.", "1/1" };
        return n[noteIndex (v)];
    }
    static std::string timeText (float v, const float* all)
    {
        if (all != nullptr && all[11] >= 0.5f) return noteName (v);
        return fmt (timeMs (v), timeMs (v) < 100.0f ? 1 : 0, " ms");
    }
    static float preMs (float v)   { return 2000.0f * v; }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "twindly", "DUAL DELAY", "DELAY",
            "Two delays, one per side. FEEDBACK repeats, CROSS sends each side into the other.",
            {
                { "TIME L",   0.5666f, 0, [] (float v) { return fmt (timeMs (v), timeMs (v) < 100.0f ? 1 : 0, " ms"); }, &timeText },
                { "TIME R",   0.5666f, 0, [] (float v) { return fmt (timeMs (v), timeMs (v) < 100.0f ? 1 : 0, " ms"); }, &timeText },
                { "LEVEL L",  0.5f,    0, [] (float v) { return fmtPct (v); } },
                { "LEVEL R",  0.77f,   0, [] (float v) { return fmtPct (v); } },
                { "FDBK L",   0.31f,   0, [] (float v) { return fmtPct (v); } },
                { "FDBK R",   0.31f,   0, [] (float v) { return fmtPct (v); } },
                { "PRE L",    0.0f,    0, [] (float v) { return fmt (preMs (v), 0, " ms"); } },
                { "PRE R",    0.016f,  0, [] (float v) { return fmt (preMs (v), 0, " ms"); } },
                { "CROSS",    0.0f,    0, [] (float v) { return fmtPct (std::min (v, 0.5f)); } },
                { "WIDTH",    1.0f,    0, [] (float v) { return fmtPct (v); } },
                { "MIX",      0.613f,  0, [] (float v) { return fmtPct (v); } },
                { "SYNC",     1.0f,    2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        const size_t n = (size_t) std::ceil (sampleRate * 8.1) + 8;   // 本体（SYNC でテンポが遅いと 2 s を超える）
        for (auto& b : line) b.assign (n, 0.0f);
        for (auto& b : pre) b.assign ((size_t) std::ceil (sampleRate * 2.05) + 8, 0.0f);
        for (int i = 0; i < 11; ++i) sm[i].prepare (sr, i < 2 || i == 6 || i == 7 ? 0.08 : 0.02);
        for (int i = 0; i < 11; ++i) sm[i].set (k[i], true);
        for (int c = 0; c < 2; ++c) { dsm[c].prepare (sr, 0.08); dsm[c].set (targetDelay (c), true); }
        reset();
    }
    void reset() override
    {
        for (auto& b : line) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : pre) std::fill (b.begin(), b.end(), 0.0f);
        wpos = ppos = 0; y[0] = y[1] = 0.0f;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 12) return;
        k[i] = v;
        if (i < 11) sm[i].set (v);
        if (i == 0 || i == 1 || i == 11) for (int c = 0; c < 2; ++c) dsm[c].set (targetDelay (c));
    }
    void setTempo (double b) override
    {
        if (b < 20.0 || b > 999.0 || std::abs (b - bpm) < 1.0e-3) return;
        bpm = b;
        for (int c = 0; c < 2; ++c) dsm[c].set (targetDelay (c));
    }
    void process (float* L, float* R, int n) override
    {
        const size_t N = line[0].size(), PN = pre[0].size();
        for (int s = 0; s < n; ++s)
        {
            float v[11];
            for (int i = 0; i < 11; ++i) v[i] = sm[i].next();
            float in = 0.5f * (L[s] + R[s]);
            if (! std::isfinite (in)) in = 0.0f;   // 壊れた数を遅れ線に入れない
            float wet[2];
            // 輪の大きさ（2 行 2 列の行列のいちばん大きい固有値）が 1 を超えたら、FDBK と CROSS を同じ割合で縮める（1 以下は本物のまま）
            float fbk[2] { v[4], v[5] }, crs = std::min (v[8], 0.5f);
            { const float mid = 0.5f * (fbk[0] + fbk[1]), half = 0.5f * (fbk[0] - fbk[1]);
              const float lam = mid + std::sqrt (half * half + crs * crs);
              if (lam > 1.0f) { const float sc = 1.0f / lam; fbk[0] *= sc; fbk[1] *= sc; crs *= sc; } }
            for (int c = 0; c < 2; ++c)
            {
                // 予備の遅れ（くり返しの外）
                pre[c][ppos] = in;
                const float pd = std::min ((float) (PN - 4), preMs (v[6 + c]) * 0.001f * (float) sr);
                const float pin = readFrac (pre[c], ppos, pd, PN);
                // 本体（くり返し）
                const float d = std::min ((float) (N - 4), dsm[c].next() + 1.0f);
                const float out = readFrac (line[c], wpos, d, N);
                const float cross = crs;
                // FEEDBACK 100 % と CROSS を同時に上げると輪の大きさが 1 を超えて際限なく大きくなる → 0.8 を超えた分だけ丸める（ふだんの音量では素通し）。
                // FEEDBACK 100 %・CROSS 0 の「減らないくり返し」は本物と同じ仕様
                line[c][wpos] = flush (guard (pin + fbk[c] * out + cross * y[1 - c]));
                wet[c] = out;
            }
            y[0] = wet[0]; y[1] = wet[1];
            // 出口: LEVEL → WIDTH → MIX
            const float wl = v[2] * wet[0], wr = v[3] * wet[1];
            const float w2 = 2.0f * v[9] - 1.0f, a = 0.5f * (1.0f + w2), b = 0.5f * (1.0f - w2);
            const float ol = a * wl + b * wr, orr = a * wr + b * wl;
            const float m = v[10], dg = std::min (1.0f, 2.0f * (1.0f - m)), wg = std::min (1.0f, 2.0f * m);
            L[s] = 0.9375f * (dg * L[s] + wg * ol);
            R[s] = 0.9375f * (dg * R[s] + wg * orr);
            wpos = (wpos + 1) % N; ppos = (ppos + 1) % PN;
        }
    }
    double tailSeconds() const override { return 30.0; }

private:
    // 遅れ（サンプル）: SYNC なら音符 × テンポ、でなければ ms（どちらも 2 秒まで = 本物と同じ。SYNC は 30 BPM の全音符でも 8 秒まで入る長さを確保）
    float targetDelay (int c) const
    {
        const float ms = k[11] >= 0.5f ? noteQuarters (k[c]) * (float) (60000.0 / bpm) : timeMs (k[c]);
        return std::min (ms, 8000.0f) * 0.001f * (float) sr;
    }
    static float guard (float x)
    {
        // 0.8 から曲がり始めて 1.0 で頭打ち（自己発振しても −0.6 dBFS 以下）。ふだんの大きさでは素通し
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float o = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -o : o;
    }
    static float readFrac (const std::vector<float>& b, size_t pos, float delay, size_t N)
    {
        // 書いた位置から delay サンプル前。4 点のなめらかな補間（Catmull-Rom）。delay 0 は今書いた値
        const float rp = (float) pos - delay;
        const float base = std::floor (rp);
        const float t = rp - base;
        long i1 = (long) base;
        while (i1 < 1) i1 += (long) N;
        const size_t a0 = (size_t) (i1 - 1) % N, a1 = (size_t) i1 % N, a2 = (a1 + 1) % N, a3 = (a1 + 2) % N;
        const float y0 = b[a0], y1 = b[a1], y2 = b[a2], y3 = b[a3];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return y1 + t * (c1 + t * (c2 + t * c3));
    }
    double sr = 48000.0;
    float k[12] { 0.5666f, 0.5666f, 0.5f, 0.77f, 0.31f, 0.31f, 0.0f, 0.016f, 0.0f, 1.0f, 0.613f, 1.0f };
    Smooth sm[11], dsm[2];
    double bpm = 120.0;
    std::vector<float> line[2], pre[2];
    size_t wpos = 0, ppos = 0;
    float y[2] { 0.0f, 0.0f };
};
} // namespace aiofx
