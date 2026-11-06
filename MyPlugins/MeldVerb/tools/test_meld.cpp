// 数値テスト: 発振しないか、無音で消えるか、つまみを乱暴に動かしても壊れないか、LEARN の計算
// clang++ -std=c++17 -O2 tools/test_meld.cpp -o /tmp/test_meld && /tmp/test_meld
#include "../Source/MeldEngine.h"
#include "../Source/Assist.h"
#include <cstdio>
#include <random>
using namespace meld;

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what); if (! ok) ++fails; }

struct Result { bool finite = true; float peak = 0; float endDb = 0; };
static Result run (const Params& p, double sr, double secs, int kind, bool automate = false, uint32_t seed = 1)
{
    Engine e; e.prepare (sr); e.set (p, true); e.reset();
    std::mt19937 rng (seed); std::uniform_real_distribution<float> u (-1, 1);
    const int n = (int) (secs * sr), blk = 256;
    std::vector<float> L (blk), R (blk);
    Result r; double endE = 0; int endN = 0;
    Params q = p;
    for (int pos = 0; pos < n; pos += blk)
    {
        for (int i = 0; i < blk; ++i)
        {
            const int t = pos + i; float x = 0;
            if (kind == 0) x = t == 0 ? 1.0f : 0.0f;                       // インパルス
            else if (kind == 1) x = t < sr * 2 ? 0.5f * u (rng) : 0.0f;    // 2 秒ノイズ → 無音
            else x = 0.5f * u (rng);                                       // ずっとノイズ
            L[(size_t) i] = x; R[(size_t) i] = kind == 0 ? 0.0f : 0.5f * u (rng) * (t < sr * 2 || kind == 2 ? 1.0f : 0.0f);
        }
        if (automate && (pos / blk) % 4 == 0)
        {
            std::uniform_real_distribution<float> z (0, 1);
            q.plate.timeSec = 0.02f + 24 * z (rng) * z (rng); q.hall.timeSec = 0.02f + 24 * z (rng) * z (rng);
            q.plate.size = 4 + 36 * z (rng); q.hall.size = 4 + 36 * z (rng); q.early.size = 4 + 36 * z (rng);
            q.early.timeMs = 250 * z (rng); q.early.angle = 100 * z (rng); q.early.diffusion = 100 * z (rng);
            q.plate.lowpassHz = q.hall.lowpassHz = 30 + 19970 * z (rng); q.plate.damping = 10 * z (rng); q.hall.balance = z (rng);
            q.plate.attackMs = 250 * z (rng); q.modDepth = 100 * z (rng); q.modRate = 0.25f + 3.75f * z (rng); q.modType = z (rng) > 0.5f;
            q.predelayMs = 1000 * z (rng); q.mix = 100 * z (rng);
            q.preEq.on = q.revEq.on = true; q.preEq.bellGain = -30 + 45 * z (rng); q.preEq.bellQ = 0.2f + 11.8f * z (rng); q.preEq.lowShape = z (rng) > 0.5f;
            q.preEq.lowSlope = 0.2f + 11.8f * z (rng); q.revEq.highShape = z (rng) > 0.5f; q.revEq.highFreq = 20 + 19980 * z (rng); q.revEq.highSlope = 0.2f + 11.8f * z (rng);
            q.smoothOn = z (rng) > 0.3f;
            e.set (q);
        }
        e.process (L.data(), R.data(), blk);
        for (int i = 0; i < blk; ++i)
        {
            if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) r.finite = false;
            r.peak = std::max (r.peak, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
            if (pos + i > n - (int) (sr * 0.5)) { endE += L[(size_t) i] * L[(size_t) i]; ++endN; }
        }
    }
    r.endDb = (float) (10.0 * std::log10 (endE / std::max (1, endN) + 1e-30));
    return r;
}

int main()
{
    Params p = defaultParams(); p.mix = 100;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto r = run (p, sr, 6, 1);
        char b[200]; std::snprintf (b, sizeof b, "default %g Hz: finite, peak %.2f, fades after the input stops (end %.1f dB)", sr, r.peak, r.endDb);
        check (r.finite && r.peak < 4 && r.endDb < -60, b);
    }
    // 極端な設定（長い時間・大きい部屋・揺れ最大）
    Params x = p;
    x.plate.timeSec = x.hall.timeSec = 24; x.plate.size = x.hall.size = 40; x.plate.damping = x.hall.damping = 10;
    x.plate.lowpassHz = x.hall.lowpassHz = 20000; x.plate.balance = 0.0f; x.hall.balance = 1.0f; x.modDepth = 100; x.modRate = 4; x.modType = 1;
    x.early.timeMs = 250; x.early.size = 40; x.levelDb[0] = x.levelDb[1] = x.levelDb[2] = 0;
    {
        auto r = run (x, 48000, 20, 2);
        char b[200]; std::snprintf (b, sizeof b, "24 s / size 40 / mod 100 with constant noise: no blow-up (peak %.2f)", r.peak);
        check (r.finite && r.peak < 6, b);
        auto r2 = run (x, 48000, 3, 0);
        std::snprintf (b, sizeof b, "24 s impulse: finite, peak %.2f", r2.peak);
        check (r2.finite && r2.peak < 2, b);
    }
    // 短い時間で 0.02 s
    { Params s = p; s.plate.timeSec = s.hall.timeSec = 0.02f; auto r = run (s, 48000, 4, 1); check (r.finite && r.endDb < -90, "0.02 s: dies quickly"); }
    // ランダムにつまみを動かし続ける
    for (uint32_t seed = 1; seed <= 6; ++seed)
    {
        auto r = run (p, 48000, 12, 2, true, seed);
        char b[200]; std::snprintf (b, sizeof b, "random automation (seed %u): finite, peak %.2f", seed, r.peak);
        check (r.finite && r.peak < 20, b);
    }
    // RT（中域）が TIME どおりか: 1 kHz 帯の T30
    {
        // 簡易: ホールだけ、インパルスの 2〜4 秒の減り方
        Params h = p; h.levelDb[0] = h.levelDb[1] = -70; h.levelDb[2] = 0; h.hall.lowpassHz = 20000; h.hall.balance = 0.5f; h.predelayMs = 0;
        Engine e; e.prepare (48000); e.set (h, true); e.reset();
        std::vector<float> L (48000 * 4, 0.0f), R (48000 * 4, 0.0f); L[0] = 1;
        e.process (L.data(), R.data(), (int) L.size());
        auto seg = [&] (double a, double b) { double s = 0; for (int i = (int) (a * 48000); i < (int) (b * 48000); ++i) s += L[(size_t) i] * L[(size_t) i]; return 10 * std::log10 (s); };
        const double slope = (seg (1.5, 2.0) - seg (0.5, 1.0)) / 1.0;   // dB / 秒
        const double rt = -60.0 / slope;
        char b[200]; std::snprintf (b, sizeof b, "hall TIME 2.5 s -> measured decay %.2f s (target about 3.1 s = 1.25 x TIME)", rt);
        check (rt > 2.5 && rt < 3.8, b);
    }
    // SMOOTHING: 単発の音は +1.9 dB、続く音は約 -3 dB
    {
        // 長い無音のあとの 1 発: 実際に出てくる音の大きさで確かめる（1 ms 遅れて出る）
        SendSmoother s; s.prepare (48000);
        for (int i = 0; i < 48000; ++i) { float a = 0, c = 0; s.process (a, c, 1); }
        float outPeak = 0;
        for (int i = 0; i < 200; ++i) { float a = i == 0 ? 1.0f : 0.0f, c = 0; s.process (a, c, 1); outPeak = std::max (outPeak, std::abs (a)); }
        const float hitDb = 20.0f * std::log10 (outPeak);
        SendSmoother s2; s2.prepare (48000); std::mt19937 g (3); std::uniform_real_distribution<float> u (-1, 1);
        for (int i = 0; i < 48000 * 4; ++i) { float a = 0.3f * u (g), c = 0.3f * u (g); s2.process (a, c, 1); }
        char b[200]; std::snprintf (b, sizeof b, "smoothing: hit %.1f dB (target +1.9), sustained %.1f dB (target about -3)", hitDb, s2.currentDb());
        check (std::abs (hitDb - 1.9f) < 0.3f && std::abs (s2.currentDb() + 3.1f) < 0.8f, b);
    }
    // LEARN: 2 kHz が飛び出たスペクトル → 2 kHz のベルで下げる
    {
        float d[assist::Spectrum::numBands];
        for (int b = 0; b < assist::Spectrum::numBands; ++b) { const float f = assist::Spectrum::centre (b); d[b] = -30 - 3 * std::log2 (f / 1000) + (std::abs (std::log2 (f / 2000)) < 0.2f ? 9.0f : 0.0f); }
        auto r = assist::learnPre (d);
        char b[200]; std::snprintf (b, sizeof b, "learn pre: found %d at %.0f Hz, gain %.1f dB, Q %.2f", r.found, r.bellFreq, r.bellGain, r.bellQ);
        check (r.found && r.bellFreq > 1700 && r.bellFreq < 2400 && r.bellGain < -3, b);
        float w[assist::Spectrum::numBands];
        for (int k = 0; k < assist::Spectrum::numBands; ++k) { const float f = assist::Spectrum::centre (k); w[k] = d[k] - 2 + (f < 250 ? 8.0f : 0.0f); }
        auto m = assist::learnUnmask (d, w, 0);
        std::snprintf (b, sizeof b, "unmask: low %.1f dB, bell %.0f Hz %.1f dB, high %.1f dB", m.lowGain * 2, m.bellFreq, m.bellGain, m.highGain * 2);
        check (m.found && m.lowGain < -1, b);
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
