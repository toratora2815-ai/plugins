// エンジンの数値テスト: c++ -O2 -std=c++17 tools/test_engine.cpp -o /tmp/fs_test && /tmp/fs_test
#include "../Source/dsp/Strip.h"
#include <cstdio>
#include <random>

static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { std::printf ("NG: %s\n", msg); ++fails; } else std::printf ("ok: %s\n", msg); } while (0)

static void run (fs::Strip& s, std::vector<float>& L, std::vector<float>& R, const float* sc = nullptr)
{
    const int n = (int) L.size();
    for (int i = 0; i < n; i += 300)
    {
        const int len = std::min (300, n - i);
        s.process (L.data() + i, R.data() + i, len, sc ? sc + i : nullptr, sc ? sc + i : nullptr);
    }
}

int main()
{
    const double sr = 48000;
    std::mt19937 rng (3); std::normal_distribution<float> nd;

    // 1. 全部オフなら何もしない
    {
        fs::StripSettings st; for (auto& o : st.on) o = false;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        std::vector<float> L (48000), R (48000); for (int i = 0; i < 48000; ++i) { L[(size_t) i] = 0.1f * nd (rng); R[(size_t) i] = L[(size_t) i]; }
        auto L0 = L; run (s, L, R);
        double d = 0; for (int i = 0; i < 48000; ++i) d = std::max (d, (double) std::abs (L[(size_t) i] - L0[(size_t) i]));
        CHECK (d < 1e-6, "all modules off = identity");
    }
    // 2. 何もしない設定で全部オン: 遅れの申告どおりに元の音が出る（位相モジュールは位相が回るので除く）
    {
        fs::StripSettings st; for (auto& o : st.on) o = true; st.on[fs::ModPhase] = false; st.on[fs::ModExciter] = true;
        st.comp[0].autoGain = st.comp[1].autoGain = false; st.comp[1].vintage = false;
        st.sculptor.amount = 0; st.unmask.amount = 0;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        const int lat = s.latencyFor (st);
        std::vector<float> L (96000, 0.0f), R (96000, 0.0f);
        for (int i = 0; i < 96000; ++i) { L[(size_t) i] = 0.05f * std::sin (2 * 3.14159265 * 440 * i / sr); R[(size_t) i] = L[(size_t) i]; }
        auto L0 = L; run (s, L, R);
        double err = 0, ref = 0;
        for (int i = 60000; i < 90000; ++i) { const double e = L[(size_t) i] - L0[(size_t) (i - lat)]; err += e * e; ref += L0[(size_t) i] * L0[(size_t) i]; }
        std::printf ("   latency %d, residual %.1f dB\n", lat, 10 * std::log10 (err / ref + 1e-30));
        CHECK (10 * std::log10 (err / ref + 1e-30) < -25.0, "neutral chain passes the sound (delayed by the reported latency)");
    }
    // 3. 強い設定で全部オン: NaN なし、発散しない
    {
        fs::StripSettings st; for (auto& o : st.on) o = true; st.limiterOn = true;
        st.eq.band[0] = { true, fs::Bell, 1000, 12, 8, false, false, -40 };
        st.eq.band[1] = { true, fs::LowpassRes, 8000, 0, 30, false, false, -40 };
        st.eq.band[2] = { true, fs::LowShelfVintage, 100, 15, 0.3f, true, false, -30 };
        for (int k = 0; k < 2; ++k) { auto& c = st.comp[k]; c.bands = 3; for (auto& b : c.band) { b.threshold = -40; b.ratio = 30; b.attack = 0.1f; b.release = 5; } c.band[1].ratio = 0.3f; }
        st.exciter.bands = 3; for (auto& b : st.exciter.band) { b.drive = 15; b.x = 1; b.y = 1; b.trash = true; }
        st.transient.bands = 3; for (auto& b : st.transient.band) { b.attack = 15; b.sustain = 15; }
        st.density.bands = 3; for (auto& b : st.density.band) { b.range = 20; b.threshold = -10; b.speed = 1; }
        st.clipper.bands = 3; for (auto& b : st.clipper.band) { b.threshold = -30; b.boost = 20; }
        st.gate.band[0].open = -50; st.phase.rotation = 90; st.phase.delayMs = 5;
        st.comp[1].punch = true; for (auto& b : st.comp[1].band) { b.punchAmount = 300; b.punchSustain = 5; b.punchAttack = 1; }
        st.comp[0].autoRelease = true; st.eq.softSat = true; st.eq.band[3] = { true, fs::Bell, 3000, -12, 2, true, true, -30 };
        st.sculptor.amount = 100; st.sculptor.target = 16; st.unmask.amount = 100; st.unmask.sensitivity = 100;
        st.global.width = 100; st.global.pan = 0.3f;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        std::vector<float> L (sr * 4), R (sr * 4), SC (sr * 4);
        for (size_t i = 0; i < L.size(); ++i) { const float e = (i % 24000) < 2000 ? 1.0f : 0.05f; L[i] = e * nd (rng); R[i] = e * nd (rng); SC[i] = 0.3f * nd (rng); }
        run (s, L, R, SC.data());
        bool finite = true; float peak = 0;
        for (size_t i = 0; i < L.size(); ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false; peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i]))); }
        CHECK (finite, "no NaN / Inf with extreme settings");
        CHECK (peak <= 1.0001f, "limiter keeps the output under 0 dBFS");
        // 無音を入れると静かになる（発振しない）
        std::vector<float> Z (sr * 3, 0.0f), Z2 (sr * 3, 0.0f);
        run (s, Z, Z2);
        float tail = 0; for (size_t i = sr * 2; i < Z.size(); ++i) tail = std::max (tail, std::abs (Z[i]));
        std::printf ("   tail peak after silence %.2g\n", tail);
        CHECK (tail < 1e-4f, "goes quiet after the input stops (no oscillation)");
    }
    // 4. コンプ: しきい値 -30、比 4 で 0 dBFS のサイン → -22.5 dBFS
    {
        fs::StripSettings st; for (auto& o : st.on) o = false; st.on[fs::ModComp1] = true;
        st.comp[0].autoGain = false; st.comp[0].band[0].threshold = -30; st.comp[0].band[0].ratio = 4;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        std::vector<float> L (48000), R (48000);
        for (int i = 0; i < 48000; ++i) { L[(size_t) i] = (float) std::sin (2 * 3.14159265 * 1000 * i / sr); R[(size_t) i] = L[(size_t) i]; }
        run (s, L, R);
        float pk = 0; for (int i = 40000; i < 48000; ++i) pk = std::max (pk, std::abs (L[(size_t) i]));
        std::printf ("   comp output peak %.2f dBFS\n", fs::gainToDb (pk));
        CHECK (std::abs (fs::gainToDb (pk) + 22.5f) < 0.3f, "compressor static curve (thr -30, 4:1)");
    }
    // 5. 3 帯域の分割を足すと平ら
    {
        fs::Crossover3 x; x.prepare (sr); x.set (3, 200, 6000, true);
        std::vector<float> imp (8192, 0.0f); imp[0] = 1;
        std::vector<float> y (8192); float o[3];
        for (int i = 0; i < 8192; ++i) { x.split (imp[(size_t) i], 0, o); y[(size_t) i] = o[0] + o[1] + o[2]; }
        double worst = 0;
        for (double f : { 50.0, 200.0, 1000.0, 6000.0, 15000.0 })
        {
            std::complex<double> h = 0; for (int i = 0; i < 8192; ++i) h += (double) y[(size_t) i] * std::polar (1.0, -2 * 3.14159265 * f * i / sr);
            worst = std::max (worst, std::abs (20 * std::log10 (std::abs (h))));
        }
        std::printf ("   crossover sum worst %.3f dB\n", worst);
        CHECK (worst < 0.05, "3-band crossover sums flat");
    }
    // 6. Add Punch / Polish、Vintage でも NaN なし
    for (int tgt : { 23, 24 })
    {
        fs::StripSettings st; for (auto& o : st.on) o = false; st.on[fs::ModSculptor] = true; st.on[fs::ModComp1] = true;
        st.sculptor.target = tgt; st.sculptor.amount = 100; st.comp[0].vintage = true; st.comp[0].band[0].threshold = -40;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        std::vector<float> L (sr * 2), R (sr * 2);
        for (size_t i = 0; i < L.size(); ++i) { const float e = (i % 12000) < 800 ? 0.8f : 0.01f; L[i] = e * nd (rng); R[i] = L[i]; }
        run (s, L, R);
        bool ok = true; for (auto v : L) if (! std::isfinite (v) || std::abs (v) > 10.0f) ok = false;
        CHECK (ok, tgt == 23 ? "Add Punch stays finite" : "Add Polish stays finite");
    }
    // 7. 位相 90° は振幅がそのまま（1 kHz）
    {
        fs::StripSettings st; for (auto& o : st.on) o = false; st.on[fs::ModPhase] = true; st.phase.rotation = 90;
        fs::Strip s; s.prepare (sr, 512); s.set (st);
        std::vector<float> L (sr), R (sr);
        for (int i = 0; i < (int) sr; ++i) { L[(size_t) i] = 0.5f * (float) std::sin (2 * 3.14159265 * 1000 * i / sr); R[(size_t) i] = L[(size_t) i]; }
        run (s, L, R);
        float pk = 0; for (int i = 30000; i < 40000; ++i) pk = std::max (pk, std::abs (L[(size_t) i]));
        std::printf ("   phase 90 peak %.3f (latency %d)\n", pk, s.latencyFor (st));
        CHECK (std::abs (pk - 0.5f) < 0.01f, "phase rotation keeps level");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
