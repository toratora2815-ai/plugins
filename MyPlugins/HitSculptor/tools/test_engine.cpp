// Hit Sculptor の数値テスト:  c++ -std=c++17 -O2 -I../Source test_engine.cpp -o /tmp/t && /tmp/t
#include "HitEngine.h"
#include <cstdio>
#include <random>
#include <vector>

static const double fs = 48000.0;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what); if (! ok) ++fails; }

// キック（150→50 Hz の下がるサイン＋クリック）と、遅く減る部屋鳴り（-22 dB、T60 0.8 s）
static std::vector<float> makeKick (int hits, double spacing, unsigned seed = 1)
{
    const int n = (int) (fs * (spacing * hits + 1.5));
    std::vector<float> x (n, 0.0f);
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    for (int h = 0; h < hits; ++h)
    {
        const int s0 = (int) (h * spacing * fs);
        double ph = 0;
        float lp = 0;
        for (int i = 0; s0 + i < n && i < (int) (fs * 1.4); ++i)
        {
            const double t = i / fs;
            const double f = 50.0 + 100.0 * std::exp (-t / 0.03);
            ph += 2 * M_PI * f / fs;
            double v = 0.8 * std::sin (ph) * std::exp (-t / 0.18);
            if (t < 0.004) v += 0.5 * nd (rng) * (1.0 - t / 0.004);
            lp += 0.2f * (nd (rng) - lp);
            v += 0.08 * lp * std::exp (-t * 6.9 / 0.8) * std::min (1.0, t / 0.01);
            x[s0 + i] += (float) v;
        }
    }
    return x;
}

static std::vector<float> run (const std::vector<float>& in, hs::Settings s, hs::Engine* keep = nullptr)
{
    static hs::Engine e;
    hs::Engine& E = keep ? *keep : e;
    E.setSettings (s);
    E.prepare (fs);
    std::vector<float> l = in, r = in;
    const int blk = 256;
    for (int i = 0; i < (int) in.size(); i += blk)
    {
        float* ch[2] = { l.data() + i, r.data() + i };
        E.process (ch, 2, std::min (blk, (int) in.size() - i));
    }
    return l;
}

static float peakDb (const std::vector<float>& x, double t0, double t1)
{
    float p = 0;
    for (int i = (int) (t0 * fs); i < (int) (t1 * fs) && i < (int) x.size(); ++i) p = std::max (p, std::abs (x[i]));
    return hs::ampDb (p);
}
static float rmsDb (const std::vector<float>& x, double t0, double t1)
{
    double e = 0; int c = 0;
    for (int i = (int) (t0 * fs); i < (int) (t1 * fs) && i < (int) x.size(); ++i) { e += x[i] * x[i]; ++c; }
    return hs::powDb ((float) (e / std::max (1, c)));
}
// 鳴り始めから、10 ms ごとの RMS が最初の 30 ms より 40 dB 下がるまでの時間
static double decayMs (const std::vector<float>& x)
{
    const float ref = rmsDb (x, 0.0, 0.03);
    for (double t = 0.03; t < 1.4; t += 0.005)
        if (rmsDb (x, t, t + 0.01) < ref - 40.0f) return t * 1000.0;
    return 1400.0;
}
static bool finite (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v) || std::abs (v) > 20.0f) return false; return true; }

int main()
{
    const auto kick = makeKick (1, 2.0);
    hs::Settings neutral;

    // 1. 何もしなければそのまま
    {
        const auto y = run (kick, neutral);
        float md = 0; for (size_t i = 0; i < y.size(); ++i) md = std::max (md, std::abs (y[i] - kick[i]));
        std::printf ("     neutral max diff %.2e\n", md);
        check (md < 1e-4f, "neutral = input");
    }
    const double d0 = decayMs (kick);
    std::printf ("     input: first 5 ms peak %.1f dB, 40 dB decay %.0f ms, tail(0.4-1.0 s) %.1f dB\n",
                 peakDb (kick, 0, 0.005), d0, rmsDb (kick, 0.4, 1.0));

    // 2. ATTACK
    for (float a : { 1.0f, -1.0f })
    {
        hs::Settings s; s.attack = a;
        const auto y = run (kick, s);
        const float dHead = rmsDb (y, 0, 0.006) - rmsDb (kick, 0, 0.006);
        const float dBody = rmsDb (y, 0.08, 0.2) - rmsDb (kick, 0.08, 0.2);
        std::printf ("     ATTACK %+.0f: head %+.1f dB, body %+.1f dB\n", a * 100, dHead, dBody);
        check (a > 0 ? dHead > 4.0f : dHead < -4.0f, a > 0 ? "ATTACK + boosts the head" : "ATTACK - softens the head");
        check (std::abs (dBody) < 1.5f, "ATTACK leaves the body");
        check (finite (y), "ATTACK finite");
    }
    // 3. LENGTH
    for (float L : { -1.0f, -0.5f, 0.5f, 1.0f })
    {
        hs::Settings s; s.length = L;
        const auto y = run (kick, s);
        const double d = decayMs (y);
        std::printf ("     LENGTH %+.0f: 40 dB decay %.0f ms (input %.0f), head %+.1f dB\n", L * 100, d, d0,
                     rmsDb (y, 0, 0.006) - rmsDb (kick, 0, 0.006));
        if (L < 0) check (d < d0 * (L < -0.9f ? 0.35 : 0.8), "LENGTH - shortens");
        else       check (d > d0 * 1.25, "LENGTH + lengthens");
        check (std::abs (rmsDb (y, 0, 0.006) - rmsDb (kick, 0, 0.006)) < 1.0f, "LENGTH keeps the head");
        check (finite (y), "LENGTH finite");
    }
    // 4. ROOM
    {
        hs::Settings s; s.room = -1.0f;
        const auto y = run (kick, s);
        const float dt = rmsDb (y, 0.4, 1.0) - rmsDb (kick, 0.4, 1.0);
        const float dh = rmsDb (y, 0, 0.03) - rmsDb (kick, 0, 0.03);
        std::printf ("     ROOM -100: tail %+.1f dB, head %+.1f dB\n", dt, dh);
        check (dt < -20.0f, "ROOM - removes the room tail");
        check (std::abs (dh) < 1.0f, "ROOM - keeps the hit");
    }
    for (float sz : { 0.0f, 0.5f, 1.0f })
    {
        hs::Settings s; s.room = 1.0f; s.size = sz; s.tone = 1.0f;
        const auto y = run (kick, s);
        const float dt = rmsDb (y, 0.25, 0.6) - rmsDb (kick, 0.25, 0.6);
        std::printf ("     ROOM +100 size %.1f: tail(0.25-0.6) %+.1f dB, end(1.4-1.9 s) %.1f dB\n", sz, dt, rmsDb (y, 1.5, 2.0));
        check (dt > 1.0f || sz < 0.9f, "ROOM + adds a tail (kick, big size)");
        check (finite (y), "ROOM + finite");
    }
    // スネア（ノイズの破裂 + 200 Hz）で、部屋の響きの大きさ
    {
        std::vector<float> sn ((size_t) (fs * 2), 0.0f);
        std::mt19937 rng (5); std::normal_distribution<float> nd (0, 1);
        for (int i = 0; i < (int) (fs * 0.3); ++i)
        {
            const double t = i / fs;
            sn[i] = (float) (0.3 * nd (rng) * std::exp (-t / 0.06) + 0.5 * std::sin (2 * M_PI * 200 * t) * std::exp (-t / 0.05));
        }
        for (float sz : { 0.2f, 0.5f, 0.9f })
        {
            hs::Settings s; s.room = 1.0f; s.size = sz; s.tone = 0.5f;
            const auto y = run (sn, s);
            std::vector<float> wet (y.size()); for (size_t i = 0; i < y.size(); ++i) wet[i] = y[i] - sn[i];
            std::printf ("     snare ROOM +100 size %.1f: wet(0-0.5 s) vs dry(0-0.1 s) %+.1f dB, wet peak vs dry peak %+.1f dB\n", sz,
                         rmsDb (wet, 0, 0.5) - rmsDb (sn, 0, 0.1), peakDb (wet, 0, 2) - peakDb (sn, 0, 0.1));
        }
    }
    // 5. 発振しないか（インパルス、最大サイズ・明るさで 10 秒）
    {
        std::vector<float> imp ((size_t) (fs * 10), 0.0f);
        imp[100] = 1.0f;
        hs::Settings s; s.room = 1.0f; s.size = 1.0f; s.tone = 1.0f;
        const auto y = run (imp, s);
        std::printf ("     impulse: 0.1-0.5 s %.1f dB, 3 s %.1f dB, 9-10 s %.1f dB\n", rmsDb (y, 0.1, 0.5), rmsDb (y, 3, 3.5), rmsDb (y, 9, 10));
        check (rmsDb (y, 9, 10) < -100.0f && finite (y), "room decays (no feedback blow-up)");
        // T60 の目安（-60 dB までの時間）
        const float r0 = rmsDb (y, 0.05, 0.1);
        double t60 = 0; for (double t = 0.1; t < 9; t += 0.01) if (rmsDb (y, t, t + 0.05) < r0 - 60) { t60 = t; break; }
        std::printf ("     size 1.0 T60 about %.2f s\n", t60);
    }
    // 6. 連打で全部見つけるか
    {
        const auto loop = makeKick (8, 0.25, 7);
        hs::Engine e;
        hs::Settings s; s.length = -0.6f; s.attack = 0.4f; s.room = -0.5f;
        const auto y = run (loop, s, &e);
        std::printf ("     8 hits at 240 bpm -> detected %d\n", e.hitCount.load());
        check (e.hitCount.load() == 8, "every hit detected");
        // 各打の頭が残っているか
        float worst = 0;
        for (int h = 0; h < 8; ++h) worst = std::min (worst, rmsDb (y, h * 0.25, h * 0.25 + 0.006) - rmsDb (loop, h * 0.25, h * 0.25 + 0.006));
        std::printf ("     worst head change %+.1f dB (ATTACK +40)\n", worst);
        check (worst > -1.0f, "short LENGTH does not eat the next hit");
    }
    // 7. 雑音・無音・大きい音
    {
        std::vector<float> nz ((size_t) (fs * 3));
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-1, 1);
        for (auto& v : nz) v = u (rng);
        for (size_t i = fs; i < 2 * fs; ++i) nz[i] = 0;
        for (float L : { -1.0f, 1.0f })
        {
            hs::Settings s; s.attack = 1; s.length = L; s.room = L; s.size = 1;
            const auto y = run (nz, s);
            check (finite (y), "noise / silence finite");
        }
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
