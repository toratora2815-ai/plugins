// Flipside の数値テスト:  c++ -std=c++17 -O2 -I../Source test_engine.cpp -o /tmp/t && /tmp/t
#include "FlipEngine.h"
#include <cstdio>
#include <random>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what); if (! ok) ++fails; }
static const double fs = 48000;

// x[i] = i を少し縮めた値（どのサンプルが出てきたか分かるように）
static std::vector<float> ramp (int n) { std::vector<float> x (n); for (int i = 0; i < n; ++i) x[i] = (float) (i + 1) * 1e-6f; return x; }

static std::vector<float> run (const std::vector<float>& in, fl::Block b, bool syncFromCounter, int blk = 512, fl::Engine* ep = nullptr)
{
    fl::Engine local; fl::Engine& e = ep ? *ep : local;
    e.prepare (fs, 2.0);
    std::vector<float> l = in, r = in;
    for (int i = 0; i < (int) in.size(); i += blk)
    {
        const int m = std::min (blk, (int) in.size() - i);
        if (syncFromCounter)
        {
            b.synced = true;
            b.index = (int64_t) (i / b.sliceLen);
            b.phase = i - b.index * b.sliceLen;
        }
        float* ch[2] = { l.data() + i, r.data() + i };
        e.process (ch, 2, m, b);
    }
    return l;
}

int main()
{
    const int L = 4800;
    const auto x = ramp (L * 6);
    fl::Block b; b.sliceLen = L; b.latency = L; b.fadeMs = 0.0f; b.mix = 1.0f;   // フェードなし（1 サンプル）

    for (bool sync : { false, true })
    {
        const auto y = run (x, b, sync);
        // 枠 k（入力 [kL,(k+1)L)）が、出力 [(k+1)L,(k+2)L) に逆向きで出る
        int bad = 0;
        for (int k = 0; k < 4; ++k)
            for (int j = 1; j < L - 1; ++j)
            {
                const float want = x[(size_t) ((k + 1) * L - 1 - j)];
                if (std::abs (y[(size_t) ((k + 1) * L + j)] - want) > 1e-9f) ++bad;
            }
        std::printf ("     %s: mismatches %d\n", sync ? "synced" : "free", bad);
        check (bad == 0, "each slice comes out reversed, one slice late (= same place after Logic's delay compensation)");
    }
    // MIX 0 = 遅れただけの元の音
    {
        fl::Block d = b; d.mix = 0.0f;
        const auto y = run (x, d, false);
        int bad = 0;
        for (int i = L; i < (int) x.size(); ++i) if (std::abs (y[(size_t) i] - x[(size_t) (i - L)]) > 1e-9f) ++bad;
        check (bad == 0, "MIX 0 = input delayed by the reported latency");
    }
    // 2 枠に 1 回: 奇数番目の枠だけ逆、偶数は元の音
    {
        fl::Block d = b; d.pattern = fl::every2nd; d.fadeMs = 0.0f;
        const auto y = run (x, d, true);
        bool okRev = true, okDry = true;
        for (int k = 0; k < 4; ++k)
            for (int j = 50; j < L - 50; ++j)
            {
                const float out = y[(size_t) ((k + 1) * L + j)];
                if (k % 2 == 1) okRev &= std::abs (out - x[(size_t) ((k + 1) * L - 1 - j)]) < 1e-9f;
                else            okDry &= std::abs (out - x[(size_t) (k * L + j)]) < 1e-9f;
            }
        check (okRev && okDry, "EVERY 2ND: odd slices reversed, even slices untouched");
    }
    // サイン波で、つなぎ目のクリック（FADE 5 ms）
    {
        std::vector<float> s (L * 8);
        for (size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * std::sin (2 * M_PI * 220 * i / fs);
        fl::Block d = b; d.fadeMs = 5.0f;
        const auto y = run (s, d, true);
        float maxStep = 0;
        for (size_t i = L + 1; i < s.size(); ++i) maxStep = std::max (maxStep, std::abs (y[i] - y[i - 1]));
        const float sineStep = 0.5f * (float) (2 * M_PI * 220 / fs);
        std::printf ("     max sample step %.4f (sine itself %.4f)\n", maxStep, sineStep);
        check (maxStep < sineStep * 1.6f, "no clicks at slice edges with FADE");
    }
    // 位置移動・テンポ変更・ブロックの大きさばらばら
    {
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> u (-1, 1);
        std::vector<float> nz ((size_t) (fs * 6)); for (auto& v : nz) v = u (rng);
        fl::Engine e; e.prepare (fs, 2.0);
        std::vector<float> l = nz, r = nz;
        double ppqSamples = 0; bool ok = true; int i = 0;
        while (i < (int) nz.size())
        {
            const int m = std::min ((int) (16 + rng() % 1500), (int) nz.size() - i);
            fl::Block d; d.sliceLen = (rng() % 50 == 0) ? 3000 + rng() % 40000 : 12000; d.latency = (int) d.sliceLen;
            if (rng() % 80 == 0) ppqSamples = rng() % 500000;   // 位置を飛ばす
            d.synced = rng() % 30 != 0;
            d.index = (int64_t) (ppqSamples / d.sliceLen); d.phase = ppqSamples - d.index * d.sliceLen;
            d.pattern = (int) (rng() % 4); d.fadeMs = 5; d.mix = 1;
            float* ch[2] = { l.data() + i, r.data() + i };
            e.process (ch, 2, m, d);
            for (int k = 0; k < m; ++k) ok &= std::isfinite (l[(size_t) (i + k)]) && std::abs (l[(size_t) (i + k)]) <= 1.0001f;
            i += m; ppqSamples += m;
        }
        check (ok, "jumps / tempo changes / odd block sizes stay finite and never louder than input");
    }
    // モノラル
    {
        fl::Engine e; e.prepare (fs, 2.0);
        auto m = x; float* ch[1] = { m.data() };
        e.process (ch, 1, (int) m.size(), b);
        check (std::abs (m[(size_t) (L + 10)] - x[(size_t) (L - 11)]) < 1e-9f, "mono works");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
