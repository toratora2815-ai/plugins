// 変調エンジンの数値テスト
// clang++ -std=c++17 -O2 MyPlugins/ModHost/tools/test_mod.cpp -o /tmp/test_mod && /tmp/test_mod
#include "../Source/ModEngine.h"
#include <cstdio>
#include <vector>
using namespace mh;

static int fails = 0;
static void check (bool ok, const char* what, double v = 0)
{
    std::printf ("%s  %-58s %g\n", ok ? "OK  " : "FAIL", what, v);
    if (! ok) ++fails;
}

int main()
{
    const double sr = 48000.0;
    float steps[maxSteps];
    for (int i = 0; i < maxSteps; ++i) steps[i] = (float) i / (maxSteps - 1);

    // 1) LFO 自由（SYNC OFF）: 2 Hz のサインを 10 秒 → 上向きのゼロ交差 20 回
    {
        Modulator m; m.reset(); Settings s; s.sync = false; s.rateHz = 2.0f;
        Block b; b.sampleRate = sr;
        int ups = 0; float prev = 0; float mn = 9, mx = -9;
        for (int i = 0; i < (int) (10 * sr / chunk); ++i)
        {
            const float v = m.process (s, b, steps);
            if (prev < 0 && v >= 0) ++ups;
            prev = v; mn = std::min (mn, v); mx = std::max (mx, v);
        }
        check (ups >= 19 && ups <= 21, "LFO 2 Hz free: 10 s で 20 周", ups);
        check (mn > -1.001f && mx < 1.001f && mx > 0.99f, "LFO BI の幅 -1〜1", mx);
        s.bipolar = false; m.reset(); mn = 9; mx = -9;
        for (int i = 0; i < 2000; ++i) { const float v = m.process (s, b, steps); mn = std::min (mn, v); mx = std::max (mx, v); }
        check (mn >= 0 && mx <= 1 && mn < 0.01f && mx > 0.99f, "LFO UNI の幅 0〜1", mn);
    }

    // 2) LFO 同期: 4 BARS の SAW UP（UNI）は 1 小節目頭 0、2 小節目頭 0.25、4 小節の終わりで 1 近く
    {
        Modulator m; m.reset(); Settings s; s.sync = true; s.div = 11; s.shape = shapeSawUp; s.bipolar = false;
        Block b; b.sampleRate = sr; b.playing = true; b.bpm = 120;
        b.ppq = 0;    const float a = m.process (s, b, steps);
        b.ppq = 4;    const float c = m.process (s, b, steps);
        b.ppq = 15.9; const float d = m.process (s, b, steps);
        b.ppq = 16;   const float e = m.process (s, b, steps);
        check (std::abs (a) < 1e-4f, "SYNC 4 BARS: 頭で 0", a);
        check (std::abs (c - 0.25f) < 1e-4f, "SYNC 4 BARS: 2 小節目の頭で 0.25", c);
        check (d > 0.99f, "SYNC 4 BARS: 終わり近くで 1 近く", d);
        check (std::abs (e) < 1e-4f, "SYNC 4 BARS: 5 小節目で 0 に戻る", e);
        b.barQuarters = 3; b.ppq = 3;  // 3/4 拍子なら 1 小節 = 3 拍
        check (std::abs (m.process (s, b, steps) - 0.25f) < 1e-4f, "SYNC 4 BARS: 3/4 拍子でも小節で数える");
    }

    // 3) ENV: 0 dB の音が来たら ATTACK 5 ms で 1 近くに、消えたら RELEASE 150 ms で下がる
    {
        Modulator m; m.reset(); Settings s; s.type = typeEnv; s.bipolar = false; s.attackMs = 5; s.releaseMs = 150;
        Block b; b.sampleRate = sr; b.inputPeak = 1.0f;
        int n = 0; float v = 0;
        while ((v = m.process (s, b, steps)) < 0.95f && n < 10000) ++n;
        const double tMs = (n + 1) * chunk / sr * 1000;
        check (tMs > 2 && tMs < 40, "ENV: 0 dB で 0.95 まで上がる時間 (ms)", tMs);
        b.inputPeak = 0; n = 0;
        while ((v = m.process (s, b, steps)) > 0.5f && n < 100000) ++n;
        const double rMs = (n + 1) * chunk / sr * 1000;
        // 1 → 0.5 は 24 dB 下がること。150 ms の時定数で約 0.42 秒
        check (rMs > 250 && rMs < 700, "ENV: 消えて 0.5 まで下がる時間 (ms)", rMs);
        Modulator q; q.reset(); b.inputPeak = 0.0039f;   // -48 dB
        for (int i = 0; i < 2000; ++i) v = q.process (s, b, steps);
        check (v < 0.02f, "ENV: -48 dB では 0 近く", v);
        s.gainDb = 24; for (int i = 0; i < 2000; ++i) v = q.process (s, b, steps);
        check (std::abs (v - 0.5f) < 0.02f, "ENV: GAIN +24 dB で -48 dB が 0.5 に", v);
    }

    // 4) SIDECHAIN はサイドチェーンの音だけを見る
    {
        Modulator m; m.reset(); Settings s; s.type = typeSidechain; s.bipolar = false;
        Block b; b.sampleRate = sr; b.inputPeak = 1; b.sidechainPeak = 0;
        float v = 0; for (int i = 0; i < 500; ++i) v = m.process (s, b, steps);
        check (v < 0.01f, "SIDECHAIN: メイン入力には反応しない", v);
        b.sidechainPeak = 1; for (int i = 0; i < 500; ++i) v = m.process (s, b, steps);
        check (v > 0.99f, "SIDECHAIN: サイドチェーンで 1 に", v);
    }

    // 5) RANDOM: 同期中は同じ位置なら毎回同じ値（再生し直しても同じ動き）、幅は -1〜1
    {
        Settings s; s.type = typeRandom; s.sync = true; s.div = 6; s.smooth = 0;
        Block b; b.sampleRate = sr; b.playing = true;
        std::vector<float> r1, r2;
        for (int pass = 0; pass < 2; ++pass)
        {
            Modulator m; m.setSeed (3); m.reset();
            for (int i = 0; i < 4000; ++i) { b.ppq = i * 0.01; (pass ? r2 : r1).push_back (m.process (s, b, steps)); }
        }
        bool same = true; float mn = 9, mx = -9;
        for (size_t i = 0; i < r1.size(); ++i) { same = same && std::abs (r1[i] - r2[i]) < 1e-6f; mn = std::min (mn, r1[i]); mx = std::max (mx, r1[i]); }
        check (same, "RANDOM: 再生し直しても同じ動き");
        check (mn >= -1.0f && mx <= 1.0f && mx - mn > 1.0f, "RANDOM: -1〜1 の中でばらける", mx - mn);
        // 1 拍の中ではほぼ一定（SMOOTH 0）
        check (std::abs (r1[250] - r1[290]) < 1e-3f, "RANDOM: SMOOTH 0 は 1 拍の間そのまま", r1[290] - r1[250]);
        Modulator a, c; a.setSeed (1); c.setSeed (2); a.reset(); c.reset(); b.ppq = 10.5;
        check (std::abs (a.process (s, b, steps) - c.process (s, b, steps)) > 1e-3f, "RANDOM: MOD ごとに違う値");
    }

    // 6) STEPS: 1/16 で 8 段、拍の位置で段が決まる
    {
        Modulator m; m.reset(); Settings s; s.type = typeSteps; s.sync = true; s.div = 1; s.steps = 8; s.bipolar = false;
        Block b; b.sampleRate = sr; b.playing = true;
        float st[maxSteps] = { 0, 1, 0.5f, 0.25f, 0, 0, 0, 0.75f };
        float v = 0;
        b.ppq = 0.25 * 3 + 0.01; for (int i = 0; i < 20; ++i) v = m.process (s, b, st);
        check (std::abs (v - 0.25f) < 0.01f, "STEPS: 4 段目 = 0.25", v);
        b.ppq = 0.25 * 15 + 0.01; for (int i = 0; i < 20; ++i) v = m.process (s, b, st);
        check (std::abs (v - 0.75f) < 0.01f, "STEPS: 16 番目 = 8 段目（くり返し）", v);
    }

    // 7) つまみへの足し算は 0〜1 に収まる
    check (applyOffset (0.8f, 0.5f) == 1.0f && applyOffset (0.2f, -0.5f) == 0.0f && std::abs (applyOffset (0.5f, 0.1f) - 0.6f) < 1e-6f,
           "applyOffset: 0〜1 に収める");

    // 8) 変な値を入れても NaN にならない
    {
        Modulator m; m.reset(); Settings s; Block b; b.sampleRate = 0; b.bpm = 0;
        bool finite = true;
        for (int t = 0; t < numTypes; ++t)
            for (int k = 0; k < 200; ++k)
            {
                s.type = t; s.rateHz = 0; s.attackMs = 0; s.releaseMs = 0; s.steps = 0; s.div = 99; s.sync = (k & 1); b.playing = (k & 2); b.ppq = -5 + k;
                finite = finite && std::isfinite (m.process (s, b, steps));
            }
        check (finite, "おかしな設定でも数字が壊れない");
    }

    std::printf ("\n%s (%d fail)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
