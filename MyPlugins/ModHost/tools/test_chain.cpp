// 通り道の部品の数値テスト（帯域分け・遅れ合わせ・AUTO GAIN・MORPH）
// clang++ -std=c++17 -O2 MyPlugins/ModHost/tools/test_chain.cpp -o /tmp/test_chain && /tmp/test_chain
#include "../Source/Chain.h"
#include <cstdio>
using namespace mh;

static int fails = 0;
static void check (bool ok, const char* what, double v = 0)
{
    std::printf ("%s  %-56s %g\n", ok ? "OK  " : "FAIL", what, v);
    if (! ok) ++fails;
}

static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }

int main()
{
    const float sr = 48000;

    // 1) 3 つに分けて足すと、どの周波数でも元の大きさのまま（±0.1 dB）
    {
        double worst = 0;
        for (float f : { 30.f, 100.f, 180.f, 200.f, 250.f, 700.f, 1500.f, 3000.f, 4000.f, 5000.f, 10000.f, 16000.f })
        {
            Crossover3 x; x.set (200, 4000, sr); x.reset();
            double eo = 0, ei = 0;
            for (int i = 0; i < 48000; ++i)
            {
                const float in = std::sin (2 * pi * f * i / sr);
                float l, m, h; x.process (0, in, l, m, h);
                if (i > 24000) { eo += (l + m + h) * (l + m + h); ei += in * in; }
            }
            worst = std::max (worst, std::abs (10 * std::log10 (eo / ei)));
        }
        check (worst < 0.1, "帯域分け: 足すと平ら（いちばんずれた dB）", worst);
    }

    // 2) 1 kHz は MID に、50 Hz は LOW に、12 kHz は HIGH に入る
    {
        auto band = [&] (float f, int which)
        {
            Crossover3 x; x.set (200, 4000, sr); x.reset();
            double e[3] = {};
            for (int i = 0; i < 48000; ++i)
            {
                float b[3]; x.process (0, std::sin (2 * pi * f * i / sr), b[0], b[1], b[2]);
                if (i > 24000) for (int k = 0; k < 3; ++k) e[k] += b[k] * b[k];
            }
            return 10 * std::log10 (e[which] / (e[0] + e[1] + e[2]));
        };
        check (band (1000, 1) > -0.5, "1 kHz はほぼ全部 MID (dB)", band (1000, 1));
        check (band (50, 0) > -0.5, "50 Hz はほぼ全部 LOW (dB)", band (50, 0));
        check (band (12000, 2) > -0.5, "12 kHz はほぼ全部 HIGH (dB)", band (12000, 2));
        check (band (1000, 0) < -25, "1 kHz は LOW にほとんど入らない (dB)", band (1000, 0));
    }

    // 3) 分け目をすばやく動かしても壊れない
    {
        Crossover3 x; x.reset(); bool finite = true; double peak = 0;
        for (int i = 0; i < 96000; ++i)
        {
            if (i % 64 == 0) x.set (normToHz ((float) (0.5 + 0.5 * std::sin (i * 0.001)), 20, 20000), normToHz ((float) (0.5 + 0.5 * std::cos (i * 0.0013)), 20, 20000), sr);
            float l, m, h; x.process (0, std::sin (2 * pi * 440 * i / sr), l, m, h);
            finite = finite && std::isfinite (l + m + h); peak = std::max (peak, (double) std::abs (l + m + h));
        }
        check (finite && peak < 4, "分け目を動かし続けても壊れない（最大値）", peak);
    }

    // 4) 遅れ合わせ: ちょうど d サンプル遅れる
    {
        Delay d; d.prepare (4096);
        int got = -1;
        for (int i = 0; i < 3000; ++i)
        {
            float l = i == 100 ? 1.0f : 0.0f, r = l;
            d.process (l, r, 1234);
            if (l > 0.5f) got = i;
        }
        check (got == 100 + 1234, "遅れ合わせ: 1234 サンプルぴったり", got);
    }

    // 5) AUTO GAIN: 出口が 2 倍（+6 dB）なら、3 秒で 0.5 倍（-6 dB）に
    {
        AutoGain a; a.prepare (sr, 64);
        float g = 1;
        for (int c = 0; c < (int) (3 * sr / 64); ++c)
        {
            for (int i = 0; i < 64; ++i)
            {
                const float x = 0.3f * std::sin (2 * pi * 300 * (c * 64 + i) / sr);
                a.measure (x, x, 2 * x, 2 * x);
            }
            g = a.update();
        }
        check (std::abs (db (g) + 6.02) < 0.3, "AUTO GAIN: +6 dB を -6 dB で戻す (dB)", db (g));
        // 入口が無音になったら、そのまま止まる（リバーブの残りを持ち上げない）
        for (int c = 0; c < (int) (3 * sr / 64); ++c)
        {
            for (int i = 0; i < 64; ++i) a.measure (0, 0, 0.01f, 0.01f);
            g = a.update();
        }
        check (std::abs (db (g) + 6.02) < 0.5, "AUTO GAIN: 入口が無音の間は動かない (dB)", db (g));
        // 上げ過ぎない（+12 dB まで）
        AutoGain b; b.prepare (sr, 64);
        for (int c = 0; c < (int) (10 * sr / 64); ++c) { for (int i = 0; i < 64; ++i) b.measure (0.5f, 0.5f, 0.001f, 0.001f); g = b.update(); }
        check (db (g) < 12.1, "AUTO GAIN: 上げるのは +12 dB まで (dB)", db (g));
    }

    // 6) MORPH
    check (std::abs (morphValue (0.2f, 0.8f, 0.5f, false) - 0.5f) < 1e-6f, "MORPH: 真ん中は半分");
    check (morphValue (0.0f, 1.0f, 0.49f, true) == 0.0f && morphValue (0.0f, 1.0f, 0.51f, true) == 1.0f, "MORPH: 切り替え式は真ん中で切り替え");
    check (morphValue (0.2f, 0.8f, 1.7f, false) == 0.8f, "MORPH: 範囲の外は B のまま");

    // 7) 周波数の 0〜1
    check (std::abs (normToHz (hzToNorm (1234, 20, 20000), 20, 20000) - 1234) < 0.1f, "周波数 ⇔ 0〜1 の行き来");

    std::printf ("\n%s (%d fail)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
