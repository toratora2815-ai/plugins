// Echo Drift のエンジンの数値テスト
// ビルド: c++ -O2 -std=c++17 -I../Source test_echo.cpp -o test_echo && ./test_echo
#include "EchoEngine.h"
#include <chrono>
#include <cstdio>

static int fails = 0;
static void check (bool ok, const char* what, double v)
{
    std::printf ("%s %-58s %.4g\n", ok ? "ok  " : "FAIL", what, v);
    if (! ok) ++fails;
}

struct Out { std::vector<float> L, R; };
static Out run (echo::Params p, double fs, const std::vector<float>& inL, const std::vector<float>& inR, double tailSec)
{
    echo::Engine e; e.prepare (fs); e.set (p); e.reset();
    Out o; o.L = inL; o.R = inR;
    const size_t total = inL.size() + (size_t) (tailSec * fs);
    o.L.resize (total, 0.0f); o.R.resize (total, 0.0f);
    for (size_t i = 0; i < total; i += 256)
    {
        const int n = (int) std::min<size_t> (256, total - i);
        e.process (o.L.data() + i, o.R.data() + i, n);
    }
    return o;
}
static double peakDb (const std::vector<float>& x, size_t a, size_t b)
{
    float m = 0; for (size_t i = a; i < std::min (b, x.size()); ++i) m = std::max (m, std::abs (x[i]));
    return 20 * std::log10 (m + 1e-12);
}
static size_t argmax (const std::vector<float>& x, size_t a, size_t b)
{
    size_t k = a; float m = -1;
    for (size_t i = a; i < std::min (b, x.size()); ++i) if (std::abs (x[i]) > m) { m = std::abs (x[i]); k = i; }
    return k;
}

int main()
{
    const double fs = 48000;
    // 1) Digital: 1 回目のエコーの時刻（左 = T − 3.44 ms、右 = T + 4.56 ms、測った参考と同じ交互のずれ）
    {
        echo::Params p; p.style = 15; p.time1Ms = 250; p.feedback = 0; p.mix = 1;
        std::vector<float> L (4800, 0.0f), R (4800, 0.0f); L[100] = R[100] = 0.5f;
        auto o = run (p, fs, L, R, 1.0);
        const double tL = (double) (argmax (o.L, 1000, 40000) - 100) / fs * 1000;
        const double tR = (double) (argmax (o.R, 1000, 40000) - 100) / fs * 1000;
        check (std::abs (tL - (250 - 3.44)) < 0.3, "digital echo L time (ms)", tL);
        check (std::abs (tR - (250 + 4.56)) < 0.3, "digital echo R time (ms)", tR);
        double e = 0; for (size_t i = 1000; i < 40000; ++i) e += (double) o.L[i] * o.L[i];
        const double g = 10 * std::log10 (e / 0.25);
        check (std::abs (g - (-2.8)) < 0.5, "digital echo energy gain (dB, ref -2.8)", g);
    }
    // 2) フィードバック 50% → 1 回ごとに −6 dB（Digital）
    {
        echo::Params p; p.style = 15; p.time1Ms = 200; p.feedback = 0.5f; p.mix = 1;
        std::vector<float> L (4800, 0.0f), R (4800, 0.0f); L[100] = R[100] = 0.5f;
        auto o = run (p, fs, L, R, 2.0);
        auto en = [&] (size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double) o.L[i] * o.L[i]; return 10 * std::log10 (e); };
        const double e1 = en (8600, 10600), e3 = en (27800, 29800);
        check (std::abs ((e3 - e1) / 2 - (-6.02)) < 0.3, "digital feedback 50% step (dB)", (e3 - e1) / 2);
    }
    // 3) MIX 50% = 両方 −3 dB
    {
        echo::Params p; p.style = 15; p.time1Ms = 100; p.feedback = 0; p.mix = 0.5f;
        std::vector<float> L (480, 0.0f), R (480, 0.0f); L[10] = R[10] = 0.5f;
        auto o = run (p, fs, L, R, 0.5);
        const double dry = 20 * std::log10 (std::abs (o.L[10]) / 0.5);
        check (std::abs (dry - (-3.01)) < 0.1, "mix 50% dry (dB)", dry);
    }
    // 4) 全スタイル・全モードで FEEDBACK 125% + SATURATION 24 dB に大きな音を 20 秒 → 有限で、+12 dBFS を超えない
    {
        double worst = -200; bool finite = true;
        std::vector<float> L ((size_t) (fs * 2)), R ((size_t) (fs * 2));
        uint32_t s = 7;
        for (size_t i = 0; i < L.size(); ++i) { s = s * 1664525u + 1013904223u; L[i] = R[i] = ((float) (s >> 9) / 8388608.0f - 1.0f) * 0.9f; }
        for (int st = 0; st < echo::numProfiles(); ++st)
            for (int mode = 0; mode < 4; ++mode)
            {
                echo::Params p; p.style = st; p.mode = mode; p.time1Ms = 180; p.time2Ms = 270; p.feedback = 1.25f; p.mix = 1;
                p.saturationDb = 24; p.inputDb = 12; p.rhythmMs = 60; p.repeats = 6;
                auto o = run (p, fs, L, R, 18.0);
                for (auto v : o.L) if (! std::isfinite (v)) finite = false;
                worst = std::max (worst, std::max (peakDb (o.L, 0, o.L.size()), peakDb (o.R, 0, o.R.size())));
            }
        check (finite, "all styles/modes stay finite at 125% feedback", finite ? 1 : 0);
        check (worst < 12.0, "worst peak at 125% feedback (dBFS)", worst);
    }
    // 5) FEEDBACK 0 で無音を入れると無音（ノイズが出ない）
    {
        double worst = -200;
        std::vector<float> L ((size_t) fs, 0.0f), R ((size_t) fs, 0.0f);
        for (int st = 0; st < echo::numProfiles(); ++st)
        {
            echo::Params p; p.style = st; p.feedback = 0.9f; p.mix = 1;
            auto o = run (p, fs, L, R, 1.0);
            worst = std::max (worst, peakDb (o.L, 0, o.L.size()));
        }
        check (worst < -120, "silence in -> silence out (dBFS)", worst);
    }
    // 6) Ping-Pong: 左に ECHO 1、右に ECHO 1 + ECHO 2
    {
        echo::Params p; p.style = 15; p.mode = 2; p.time1Ms = 200; p.time2Ms = 300; p.feedback = 0; p.mix = 1;
        std::vector<float> L (4800, 0.0f), R (4800, 0.0f); L[100] = 0.5f;
        auto o = run (p, fs, L, R, 1.0);
        const double tL = (double) (argmax (o.L, 1000, 40000) - 100) / fs * 1000;
        const double tR = (double) (argmax (o.R, 1000, 40000) - 100) / fs * 1000;
        check (std::abs (tL - 200) < 0.5, "ping-pong left (ms)", tL);
        check (std::abs (tR - 500) < 0.5, "ping-pong right (ms)", tR);
    }
    // 7) 44.1 / 96 kHz でも同じ時刻
    for (double rate : { 44100.0, 96000.0 })
    {
        echo::Params p; p.style = 15; p.time1Ms = 300; p.feedback = 0; p.mix = 1;
        std::vector<float> L ((size_t) (rate * 0.1), 0.0f), R ((size_t) (rate * 0.1), 0.0f); L[100] = R[100] = 0.5f;
        auto o = run (p, rate, L, R, 1.0);
        const double tL = (double) (argmax (o.L, 1000, (size_t) rate) - 100) / rate * 1000;
        check (std::abs (tL - (300 - 3.44)) < 0.3, rate < 50000 ? "44.1 kHz echo time (ms)" : "96 kHz echo time (ms)", tL);
    }
    // 8) 揺れ: Vibrato の 1 kHz の音程の振れ幅（参考 ±23.5 セント）
    {
        echo::Params p; p.style = 20; p.time1Ms = 250; p.feedback = 0; p.mix = 1;
        const size_t n = (size_t) (fs * 4);
        std::vector<float> L (n), R (n);
        for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.1f * (float) std::sin (2 * echo::kPi * 1000 * i / fs);
        auto o = run (p, fs, L, R, 0.0);
        // ゼロ交差の間隔から音程
        double lo = 1e9, hi = -1e9; size_t last = 0; int cnt = 0;
        for (size_t i = (size_t) fs; i + 1 < n; ++i)
            if (o.L[i] < 0 && o.L[i + 1] >= 0)
            {
                const double t = i + o.L[i] / (o.L[i] - o.L[i + 1]);
                if (last) { ++cnt; }
                static double prevT = 0; static std::vector<double> per;
                if (prevT > 0) per.push_back (t - prevT);
                prevT = t; last = i;
                if (per.size() >= 10)
                {
                    double s = 0; for (auto x : per) s += x;
                    const double f = fs / (s / per.size()); per.clear();
                    const double c = 1200 * std::log2 (f / 1000);
                    lo = std::min (lo, c); hi = std::max (hi, c);
                }
            }
        check (hi > 15 && hi < 30 && lo < -15 && lo > -30, "vibrato pitch swing max (cents)", hi);
    }
    // 9) 処理の重さ（ステレオ 10 秒、最も重いスタイル）
    {
        echo::Params p; p.style = 30; p.time1Ms = 400; p.feedback = 0.6f; p.mix = 0.5f; p.saturationDb = 12;
        std::vector<float> L ((size_t) (fs * 10)), R ((size_t) (fs * 10));
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.2f * (float) std::sin (i * 0.01);
        const auto t0 = std::chrono::steady_clock::now();
        run (p, fs, L, R, 0.0);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        check (sec < 10.0 * 0.15, "CPU: seconds to process 10 s stereo", sec);
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
