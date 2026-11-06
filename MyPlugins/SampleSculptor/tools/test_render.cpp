// Sample Sculptor の作り直し（Render.h）の数値テスト:
//   c++ -std=c++17 -O2 -I../Source test_render.cpp -o /tmp/t && /tmp/t
#include "Render.h"
#include <cstdio>
#include <random>
#include <chrono>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what); if (! ok) ++fails; }
static const double fs = 48000;

static float peak (const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX)
{ float p = 0; for (size_t i = a; i < std::min (b, x.size()); ++i) p = std::max (p, std::abs (x[i])); return p; }
static float rmsDb (const std::vector<float>& x, double t0, double t1)
{
    double e = 0; int c = 0;
    for (size_t i = (size_t) (t0 * fs); i < (size_t) (t1 * fs) && i < x.size(); ++i) { e += x[i] * x[i]; ++c; }
    return hs::powDb ((float) (e / std::max (1, c)));
}
static bool finite (const ss::Channels& c) { for (auto& v : c) for (float y : v) if (! std::isfinite (y) || std::abs (y) > 4) return false; return true; }

// キック（1 秒、ステレオ）
static ss::Channels kick()
{
    ss::Channels c (2, std::vector<float> ((size_t) fs));
    double ph = 0;
    for (size_t i = 0; i < c[0].size(); ++i)
    {
        const double t = i / fs;
        ph += 2 * M_PI * (50.0 + 100.0 * std::exp (-t / 0.03)) / fs;
        c[0][i] = c[1][i] = (float) (0.7 * std::sin (ph) * std::exp (-t / 0.15));
    }
    return c;
}

int main()
{
    const auto k = kick();
    ss::Settings plain; plain.normalize = false;

    // 何もしない = そのまま（最後の 0.5 ms のフェードだけ）
    {
        const auto y = ss::render (k, fs, plain);
        float md = 0; for (size_t i = 0; i + 30 < k[0].size(); ++i) md = std::max (md, std::abs (y[0][i] - k[0][i]));
        check (y[0].size() == k[0].size() && md < 1e-6f, "default settings = unchanged");
    }
    // 逆再生
    {
        auto s = plain; s.reverse = ss::revSound;
        const auto y = ss::render (k, fs, s);
        bool ok = y[0].size() == k[0].size();
        for (size_t i = 100; ok && i < k[0].size() - 100; ++i) ok = std::abs (y[0][i] - k[0][k[0].size() - 1 - i]) < 1e-6f;
        check (ok, "REVERSE flips the sound exactly");
    }
    // 切り取り
    {
        auto s = plain; s.start = 0.25f; s.end = 0.75f;
        const auto y = ss::render (k, fs, s);
        check (y[0].size() == (size_t) (fs / 2) && std::abs (y[0][10] - k[0][(size_t) (fs / 4) + 10]) < 1e-6f, "START / END trim");
    }
    // ピッチ: +12 で半分の長さ・2 倍の高さ、-12 で 2 倍の長さ
    {
        ss::Channels sine (1, std::vector<float> ((size_t) fs));
        for (size_t i = 0; i < sine[0].size(); ++i) sine[0][i] = 0.5f * (float) std::sin (2 * M_PI * 440 * i / fs);
        for (float st : { 12.0f, -12.0f, 7.0f })
        {
            auto s = plain; s.pitch = st;
            const auto y = ss::render (sine, fs, s);
            int zc = 0; for (size_t i = 1; i < y[0].size(); ++i) if ((y[0][i - 1] < 0) != (y[0][i] < 0)) ++zc;
            const double f = zc / 2.0 / (y[0].size() / fs);
            const double want = 440 * std::pow (2.0, st / 12.0);
            std::printf ("     PITCH %+.0f: length %.3f s, freq %.1f Hz (want %.1f)\n", st, y[0].size() / fs, f, want);
            check (std::abs (f - want) < want * 0.01 && std::abs (y[0].size() / fs - 1.0 / std::pow (2.0, st / 12.0)) < 0.01, "PITCH changes speed and length");
        }
        // 上げたときの折り返し: 18 kHz を +12 → 36 kHz は消えるはず
        ss::Channels hi (1, std::vector<float> ((size_t) fs));
        for (size_t i = 0; i < hi[0].size(); ++i) hi[0][i] = 0.5f * (float) std::sin (2 * M_PI * 18000 * i / fs);
        auto s = plain; s.pitch = 12;
        const auto y = ss::render (hi, fs, s);
        std::printf ("     18 kHz pitched +12: left over %.1f dB\n", hs::ampDb (peak (y[0], 200, y[0].size() - 200)));
        check (peak (y[0], 200, y[0].size() - 200) < 0.01f, "no aliasing when pitching up");
    }
    // LENGTH / ATTACK（Hit Sculptor のエンジンがファイルにかかるか）
    {
        auto s = plain; s.length = -0.8f;
        const auto y = ss::render (k, fs, s);
        std::printf ("     LENGTH -80: 0.3-0.6 s %.1f dB (was %.1f)\n", rmsDb (y[0], 0.3, 0.6), rmsDb (k[0], 0.3, 0.6));
        check (rmsDb (y[0], 0.3, 0.6) < rmsDb (k[0], 0.3, 0.6) - 30, "LENGTH - shortens the file");
        s = plain; s.attack = 1.0f;
        const auto z = ss::render (k, fs, s);
        std::printf ("     ATTACK +100: first 5 ms %+.1f dB\n", rmsDb (z[0], 0, 0.005) - rmsDb (k[0], 0, 0.005));
        check (rmsDb (z[0], 0, 0.005) > rmsDb (k[0], 0, 0.005) + 4, "ATTACK + works from the very first sample");
    }
    // 響き: 尾の分だけ長くなる、逆＋響きで最後に一番大きい所が来る
    {
        auto s = plain; s.room = 0.8f; s.size = 0.6f;
        const auto y = ss::render (k, fs, s);
        std::printf ("     ROOM +80: length %.2f s (was 1.00)\n", y[0].size() / fs);
        check (y[0].size() > k[0].size() && finite (y), "ROOM + adds a tail (file gets longer)");
        s.reverse = ss::revAll;
        const auto r = ss::render (k, fs, s);
        size_t pi = 0; for (size_t i = 0; i < r[0].size(); ++i) if (std::abs (r[0][i]) > std::abs (r[0][pi])) pi = i;
        std::printf ("     REVERSE SOUND+ROOM: loudest at %.0f %% of the file, swell 0.1-0.4 s %.1f dB, 0.4-0.5 s %.1f dB\n", 100.0 * pi / r[0].size(), rmsDb (r[0], 0.1, 0.4), rmsDb (r[0], 0.4, 0.5));
        check (pi > r[0].size() * 0.8 && rmsDb (r[0], 0.4, 0.5) > rmsDb (r[0], 0.1, 0.4) + 5, "reverse with room = swell into the hit");
        s.reverse = ss::revSound;
        const auto q = ss::render (k, fs, s);
        std::printf ("     REVERSE SOUND then ROOM: length %.2f s\n", q[0].size() / fs);
        check (q[0].size() > k[0].size() && finite (q), "reverse sound only, room after");
    }
    // DRIVE（4 倍）: 低い音は太く、大きさは発散しない
    {
        auto s = plain; s.drive = 1.0f;
        const auto t0 = std::chrono::steady_clock::now();
        const auto y = ss::render (k, fs, s);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("     DRIVE 100: peak %.2f, 0.3-0.6 s %+.1f dB, render %.0f ms for 1 s stereo\n", peak (y[0]),
                     rmsDb (y[0], 0.3, 0.6) - rmsDb (k[0], 0.3, 0.6), ms);
        check (finite (y) && peak (y[0]) < 1.0f && rmsDb (y[0], 0.3, 0.6) > rmsDb (k[0], 0.3, 0.6) + 3, "DRIVE saturates (tail up, peak held)");
    }
    // フェード・NORMALIZE
    {
        auto s = plain; s.fadeInMs = 100; s.fadeOutMs = 300; s.normalize = true; s.gainDb = -6;
        const auto y = ss::render (k, fs, s);
        check (std::abs (y[0][0]) < 1e-6f && std::abs (y[0].back()) < 1e-6f, "fades start and end at zero");
        std::printf ("     NORMALIZE + GAIN -6: peak %.2f dB\n", hs::ampDb (peak (y[0])));
        check (std::abs (hs::ampDb (peak (y[0])) - (-6.3f)) < 0.05f, "NORMALIZE to -0.3 dB, then GAIN");
    }
    // 全部のせ（モノラル）と、とても短いもの
    {
        ss::Channels m (1, k[0]);
        ss::Settings s; s.attack = 0.5f; s.length = 0.6f; s.room = 1; s.size = 1; s.tone = 1; s.drive = 0.5f; s.pitch = -5; s.reverse = ss::revAll;
        check (finite (ss::render (m, fs, s)), "everything at once, mono");
        ss::Channels tiny (2, std::vector<float> (40, 0.3f));
        s.start = 0.9f; s.end = 0.1f;
        check (finite (ss::render (tiny, fs, s)), "tiny file and START after END");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
