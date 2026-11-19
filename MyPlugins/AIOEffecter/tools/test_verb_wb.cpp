// test_verb_wb.cpp — AIO Effecter の響き系（VerbEngine）を「中の係数を直接読んで」確かめる検査（DSP 検査係 2026-11-19、2 回目）。JUCE なし・ビルドには入らない
//   clang++ -std=c++17 -O2 -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_verb_wb.cpp -o build/aiofx/tvw
//   ./tvw <mode>
//     static          … 止まっているときの 1 周の大きさ（帯域ごとの最大）を、エンジンの今の係数（lossG・lossLo・lossHi）から周波数 2000 点で。44.1〜192 kHz
//                       （押さえの目標は 0.9995。1 を超えたら発振のおそれ。test_verb の loop / grid は直す前の式の写しなので、押さえの入った今のエンジンには使えない）
//     refine <sr> [id] … 1 周の大きさがいちばん大きくなるつまみをランダム＋山登りで探す
//     long <id> <sr> <つまみ…> <秒> … その組を長く鳴らして 10 秒ごとの RMS（192 kHz のゆっくりした発振はこれで見る）
//     ramp [N]        … つまみを動かしている途中（つなぎの途中）の 1 周の最大（4 サンプルごと）
//     svf             … Svf の係数をでたらめに速く動かしても壊れないか
//     duck            … 選ぶつまみの切り替え（FREEZE 中・響きが残っている所・速い切り替え）と snapAll の時間
//     hold            … FREEZE を 10 分
//     dc              … 直流 0.01 を 30 秒入れた出口の直流、低い音の大きさ、直流の段
//   private / protected を public にして読むので、標準ヘッダーを先に読んでから部品を読む
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <vector>
#define private public
#define protected public
#include "fx/Registry.h"
#include "fx/Rack.h"
#undef private
#undef protected

using namespace aiofx;
static const char* kVerbs[] = { "spacevb", "classicvb", "shaperoom", "pickroom", "clubverb", "digihall", "digihallxl", "frostverb", "eightverb" };
static const ModuleInfo& infoOf (const char* id) { return *infoFor (typeForId (id)); }
static std::vector<float> defaults (const char* id) { std::vector<float> v; for (auto& k : infoOf (id).knobs) v.push_back (k.def); return v; }
static int knobIdx (const char* id, const char* name) { const auto& inf = infoOf (id); for (size_t k = 0; k < inf.knobs.size(); ++k) if (! std::strcmp (inf.knobs[k].name, name)) return (int) k; return -1; }
static std::string kvStr (const std::vector<float>& kv) { std::string s = "{"; for (size_t i = 0; i < kv.size(); ++i) { char b[16]; std::snprintf (b, sizeof b, "%s%.2f", i ? "," : "", kv[i]); s += b; } return s + "}"; }
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }
static double nowSec() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

static std::unique_ptr<Module> make (const char* id, double sr, const std::vector<float>& kv)
{
    auto m = makeType (typeForId (id));
    for (int k = 0; k < (int) kv.size(); ++k) m->setKnob (k, kv[(size_t) k]);
    m->prepare (sr, 256); m->reset();
    return m;
}
static VerbEngine* ve (std::unique_ptr<Module>& m) { return dynamic_cast<VerbEngine*> (m.get()); }

// 今の係数（b0 b1 a1・lossG）での 1 周の大きさの最大（周波数を細かく）
struct Grid { std::vector<std::complex<double>> z1; std::vector<double> f; };
static Grid makeGrid (double sr, int n)
{
    Grid g; g.f.push_back (0.0);
    for (int i = 0; i < n; ++i) g.f.push_back (1.0 * std::pow (0.5 * sr / 1.0, i / (double) (n - 1)));
    g.f.back() = 0.5 * sr;
    for (double f : g.f) g.z1.push_back (std::polar (1.0, -2 * M_PI * f / sr));
    return g;
}
static double shelfMag (double b0, double b1, double a1, std::complex<double> z1) { return std::abs ((b0 + b1 * z1) / (1.0 + a1 * z1)); }
static double trueLoop (VerbEngine* v, const Grid& g, double* fAt = nullptr, int* lineAt = nullptr)
{
    double worst = 0;
    for (int i = 0; i < 8; ++i)
    {
        double l0, l1, la, h0, h1, ha;   // 棚は TPT の形になったので、同じ伝達関数の係数をもらって調べる（3 回目の直し）
        v->lossLo[i].eq (false, l0, l1, la); v->lossHi[i].eq (false, h0, h1, ha);
        for (size_t j = 0; j < g.z1.size(); ++j)
        {
            const double m = (double) v->lossG[i] * shelfMag (l0, l1, la, g.z1[j]) * shelfMag (h0, h1, ha, g.z1[j]);
            if (m > worst) { worst = m; if (fAt) *fAt = g.f[j]; if (lineAt) *lineAt = i; }
        }
    }
    return worst;
}

// ============================================================================
// A. 止まっている状態（準備・リセットのあと）の 1 周の最大: 効くつまみの格子（5 点＋初期値）× 44.1 / 48 / 96 / 192 kHz
static void modeStatic()
{
    std::printf ("== A. 止まっているときの 1 周の最大（中の係数から、周波数 2000 点）。押さえの目標は 0.9995 ==\n");
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        std::vector<int> ks;
        {
            auto m = make (id, 48000, d); auto* v = ve (m);
            const int loopP[] = { vpT60, vpLowR, vpHighR, vpXLo, vpXHi, vpSize };
            std::vector<float> p0 (vpCount); for (int j = 0; j < vpCount; ++j) p0[(size_t) j] = v->param (j);
            for (size_t k = 0; k < d.size(); ++k)
            {
                if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue;
                bool eff = false;
                for (float x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) { auto kv = d; kv[k] = x; for (int kk = 0; kk < (int) kv.size(); ++kk) m->setKnob (kk, kv[(size_t) kk]); for (int j : loopP) if (std::abs (v->param (j) - p0[(size_t) j]) > 1e-5f) eff = true; }
                for (int kk = 0; kk < (int) d.size(); ++kk) m->setKnob (kk, d[(size_t) kk]);
                if (eff) ks.push_back ((int) k);
            }
        }
        std::vector<std::vector<float>> vals; size_t total = 1;
        for (int k : ks) { std::vector<float> vv; const int st = inf.knobs[(size_t) k].steps; if (st > 1) for (int i = 0; i < st; ++i) vv.push_back ((float) i / (st - 1)); else { for (int i = 0; i < 5; ++i) vv.push_back ((float) i / 4); vv.push_back (inf.knobs[(size_t) k].def); } total *= vv.size(); vals.push_back (vv); }
        // 多すぎるときは間引く（DIGI HALL / XL）
        const size_t stride = std::max<size_t> (1, total / 6000);
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto g = makeGrid (sr, 2000);
            auto m = make (id, sr, d); auto* v = ve (m);
            double worst = 0, wf = 0; std::string at; size_t over = 0, cnt = 0;
            for (size_t c = 0; c < total; c += stride)
            {
                auto kv = d; size_t r = c;
                for (size_t i = 0; i < ks.size(); ++i) { kv[(size_t) ks[i]] = vals[i][r % vals[i].size()]; r /= vals[i].size(); }
                for (int kk = 0; kk < (int) kv.size(); ++kk) m->setKnob (kk, kv[(size_t) kk]);
                v->duckState = 0; v->duckG = 1.0f;
                v->reset();   // 係数をすぐ目標に（snap）
                double f; const double lg = trueLoop (v, g, &f); ++cnt;
                if (lg > 1.0) ++over;
                if (lg > worst) { worst = lg; wf = f; at = kvStr (kv); }
            }
            std::printf ("  %-10s %6.0f Hz: %zu 組で最大 %.6f（%.0f Hz）%s 1 超え %zu 組 [%s]\n", id, sr, cnt, worst, wf, worst > 1.0 ? "★" : "", over, at.c_str());
        }
    }
}

// ============================================================================
// B. つまみを動かしている途中（つなぎの途中）の 1 周の最大: 4 サンプルごとに今の係数で調べる
//    動かし方: (1) 効くつまみをランダムな組へ一気に（0.4 秒ごと）、(2) 1 本ずつ 0→1 / 1→0 を 0.3 秒で、(3) FREEZE の ON/OFF
static void modeRamp (int nJumps)
{
    std::printf ("== B. 動かしている途中の 1 周の最大（中の係数から、4 サンプルごと、周波数 160 点）==\n");
    const double sr = 48000; auto g = makeGrid (sr, 160);
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        const int fz = knobIdx (id, "FREEZE");
        std::mt19937 rg (99); std::uniform_real_distribution<float> u (0, 1);
        double worst = 0, wf = 0; std::string at; long overSamples = 0, checks = 0;
        auto runCase = [&] (std::vector<std::pair<size_t, std::vector<float>>> sched, size_t len, const std::string& label)
        {
            auto m = make (id, sr, sched[0].second); auto* v = ve (m);
            std::vector<float> L (32), R (32);
            std::mt19937 ng (3); std::uniform_real_distribution<float> nd (-0.25f, 0.25f);
            size_t si = 1;
            for (size_t off = 0; off < len; off += 32)
            {
                while (si < sched.size() && sched[si].first <= off) { for (int k = 0; k < (int) sched[si].second.size(); ++k) m->setKnob (k, sched[si].second[(size_t) k]); ++si; }
                for (int s = 0; s < 32; s += 4)
                {
                    for (int i = 0; i < 4; ++i) { L[(size_t) (s + i)] = nd (ng); R[(size_t) (s + i)] = nd (ng); }
                    m->process (L.data() + s, R.data() + s, 4);
                    double f; const double lg = trueLoop (v, g, &f); ++checks;
                    if (lg > 1.0) ++overSamples;
                    if (lg > worst) { worst = lg; wf = f; at = label; }
                }
            }
        };
        // (1) ランダムな組へ一気に
        {
            std::vector<std::pair<size_t, std::vector<float>>> sched { { 0, d } };
            for (int j = 1; j <= nJumps; ++j)
            {
                std::vector<float> kv;
                for (auto& ki : inf.knobs) { float x = u (rg); const int pick = (int) (u (rg) * 5); if (pick == 0) x = 0; else if (pick == 1) x = 1; if (ki.steps > 1) x = std::round (x * (ki.steps - 1)) / (ki.steps - 1); kv.push_back (x); }
                if (fz >= 0) kv[(size_t) fz] = u (rg) < 0.25f ? 1.0f : 0.0f;
                sched.push_back ({ (size_t) (j * 0.4 * sr), kv });
            }
            runCase (sched, (size_t) ((nJumps + 1) * 0.4 * sr), "ランダムへ一気に");
        }
        // (2) 1 本ずつ 0→1 / 1→0 を 0.3 秒で（ほかは初期値）
        for (size_t k = 0; k < d.size(); ++k)
            for (int dir = 0; dir < 2; ++dir)
            {
                std::vector<std::pair<size_t, std::vector<float>>> sched;
                auto kv = d; kv[k] = (float) dir; sched.push_back ({ 0, kv });
                for (int s = 0; s <= 45; ++s) { auto k2 = d; k2[k] = dir ? 1.0f - s / 45.0f : s / 45.0f; sched.push_back ({ (size_t) (0.3 * sr) + (size_t) s * 320, k2 }); }
                runCase (sched, (size_t) (1.0 * sr), std::string (inf.knobs[k].name) + (dir ? " 1→0" : " 0→1"));
            }
        std::printf ("  %-10s 最大 %.6f（%.0f Hz）%s 1 を超えた回数 %ld / %ld [%s]\n", id, worst, wf, worst > 1.0 ? "★" : "", overSamples, checks, at.c_str());
    }
}

// ============================================================================
// C. Svf をでたらめに速く動かしても壊れないか（係数は 32 サンプルごとに新しい目標、間は直線）
static void modeSvf()
{
    std::printf ("== C. Svf（TPT 状態変数フィルター）の係数をでたらめに動かす（32 サンプルごとに新しい目標）: ノイズ ±1 を 2000 万サンプル ==\n");
    for (double sr : { 44100.0, 48000.0, 192000.0 })
        for (int type = 0; type < 5; ++type)
        {
            Svf f; f.reset();
            std::mt19937 g (type * 7 + 1); std::uniform_real_distribution<float> u (0, 1), n (-1, 1);
            auto setRandom = [&] () {
                const double fr = 5.0 * std::pow (0.49 * sr / 5.0, u (g));
                const double q = std::pow (10.0, -1.0 + 2.3 * u (g));
                const double dbv = -30.0 + 54.0 * u (g);
                switch (type) { case 0: f.lp (sr, fr, q); break; case 1: f.hp (sr, fr, q); break; case 2: f.peak (sr, fr, q, dbv); break; case 3: f.lowShelf (sr, fr, dbv); break; default: f.highShelf (sr, fr, dbv); break; }
            };
            setRandom(); f.ramp (true, 1.0f);
            float pk = 0; long bad = 0;
            for (long s = 0; s < 20000000L; ++s)
            {
                if ((s & 31) == 0) { setRandom(); f.ramp (false, 32.0f); }
                f.step();
                const float y = f.process (n (g));
                if (! std::isfinite (y)) { ++bad; f.reset(); continue; }
                pk = std::max (pk, std::abs (y));
            }
            static const char* nm[] = { "LP", "HP", "ピーク", "低い棚", "高い棚" };
            std::printf ("  %6.0f Hz %-6s 出口の最大 %+6.1f dBFS（入口 0 dBFS、+24 dB の持ち上げ・Q 20 まで）NaN %ld\n", sr, nm[type], db (pk), bad);
        }
}

// ============================================================================
// D. 切り替えのダック
static void runSig (Module& m, std::vector<float>& L, std::vector<float>& R, size_t from, size_t to, int B = 256)
{
    for (size_t off = from; off < to; off += (size_t) B) m.process (L.data() + off, R.data() + off, (int) std::min ((size_t) B, to - off));
}
static double rmsDb (const std::vector<float>& L, const std::vector<float>& R, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
    return 10 * std::log10 (e / (double) (b - a) + 1e-300);
}
static void modeDuck()
{
    const double sr = 48000;
    std::printf ("== D1. FREEZE で止めている響きに、選ぶつまみ（MODE など）を変えるとどうなるか（ウェットだけ）==\n");
    struct C { const char* id; const char* knob; float a, b; } cs[] = { { "spacevb", "MODE", 0.5f, 1.0f }, { "spacevb", "DENSITY", 0.0f, 1.0f }, { "eightverb", "PRESET", 3.0f / 7.0f, 4.0f / 7.0f } };
    for (auto& c : cs)
    {
        auto kv = defaults (c.id);
        if (! std::strcmp (c.id, "eightverb")) kv[6] = 1.0f; else kv[(size_t) knobIdx (c.id, "MIX")] = 1.0f;
        const int fz = knobIdx (c.id, "FREEZE"), kk = knobIdx (c.id, c.knob);
        kv[(size_t) kk] = c.a;
        auto m = make (c.id, sr, kv);
        const size_t N = (size_t) (8 * sr);
        std::vector<float> L (N, 0.0f), R (N, 0.0f);
        std::mt19937 g (4); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
        for (size_t i = 0; i < (size_t) (1.0 * sr); ++i) { L[i] = u (g); R[i] = u (g); }
        runSig (*m, L, R, 0, (size_t) (0.9 * sr));
        m->setKnob (fz, 1.0f);                      // 鳴っている間に FREEZE
        runSig (*m, L, R, (size_t) (0.9 * sr), (size_t) (4.0 * sr));
        m->setKnob (kk, c.b);                       // 止めている間に選ぶつまみを変える
        runSig (*m, L, R, (size_t) (4.0 * sr), N);
        std::printf ("  %-9s FREEZE 中に %s %.2f→%.2f: 変える前 3〜4 秒 %+6.1f dBFS → 変えた後 5〜8 秒 %+6.1f dBFS\n", c.id, c.knob, c.a, c.b, rmsDb (L, R, (size_t) (3 * sr), (size_t) (4 * sr)), rmsDb (L, R, (size_t) (5 * sr), N));
    }
    std::printf ("== D2. 入力が止まって響きが残っている所で選ぶつまみを変える（ウェットだけ）: 尾が消えるか ==\n");
    struct C2 { const char* id; const char* knob; float a, b; } c2[] = { { "spacevb", "MODE", 0.5f, 0.0f }, { "classicvb", "MODE", 2.0f / 7.0f, 1.0f / 7.0f }, { "pickroom", "ROOM", 4.0f / 7.0f, 5.0f / 7.0f }, { "digihall", "ALGORITHM", 0.5f, 1.0f }, { "digihallxl", "ALGORITHM", 1.0f, 0.0f }, { "eightverb", "PRESET", 3.0f / 7.0f, 2.0f / 7.0f } };
    for (auto& c : c2)
    {
        auto kv = defaults (c.id);
        if (! std::strcmp (c.id, "eightverb")) kv[6] = 1.0f; else kv[(size_t) knobIdx (c.id, "MIX")] = 1.0f;
        const int kk = knobIdx (c.id, c.knob); kv[(size_t) kk] = c.a;
        for (int sw = 0; sw < 2; ++sw)
        {
            auto m = make (c.id, sr, kv);
            const size_t N = (size_t) (3 * sr);
            std::vector<float> L (N, 0.0f), R (N, 0.0f);
            std::mt19937 g (4); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
            for (size_t i = 0; i < (size_t) (0.5 * sr); ++i) { L[i] = u (g); R[i] = u (g); }
            runSig (*m, L, R, 0, (size_t) (0.8 * sr));
            if (sw) m->setKnob (kk, c.b);
            runSig (*m, L, R, (size_t) (0.8 * sr), N);
            if (sw == 0) std::printf ("  %-10s %s を変えない: 0.7〜0.8 秒 %+6.1f、0.9〜1.0 秒 %+6.1f dBFS", c.id, c.knob, rmsDb (L, R, (size_t) (0.7 * sr), (size_t) (0.8 * sr)), rmsDb (L, R, (size_t) (0.9 * sr), (size_t) (1.0 * sr)));
            else std::printf (" | 0.8 秒で %.2f→%.2f: 0.9〜1.0 秒 %+6.1f dBFS\n", c.a, c.b, rmsDb (L, R, (size_t) (0.9 * sr), (size_t) (1.0 * sr)));
        }
    }
    std::printf ("== D3. 選ぶつまみを速く切り替え続ける（オートメーション）: 220 Hz −12 dBFS、初期値の MIX。プチッの目安（2 階差）と、ウェットが戻っている割合 ==\n");
    auto clean = [] (const std::vector<float>& x, size_t a, size_t b, double sr2) {
        const double w = 2 * M_PI * 220.0 / sr2; const size_t win = (size_t) (0.005 * sr2), ctx = (size_t) (0.02 * sr2);
        float worst = 0;
        for (size_t s = std::max (a, ctx + 2); s + win < std::min (b, x.size() - ctx); s += win)
        {
            float d2 = 0, pk = 0;
            for (size_t i = s; i < s + win; ++i) d2 = std::max (d2, std::abs (x[i] - 2 * x[i - 1] + x[i - 2]));
            for (size_t i = s - ctx; i < s + win + ctx; ++i) pk = std::max (pk, std::abs (x[i]));
            if (pk > 1e-4f) worst = std::max (worst, (float) (d2 / (pk * w * w)));
        }
        return worst; };
    for (const char* id : { "spacevb", "pickroom", "eightverb", "digihallxl" })
        for (double per : { 0.005, 0.02, 0.06, 0.25 })
        {
            const auto& inf = infoOf (id);
            int kk = -1; for (size_t k = 0; k < inf.knobs.size(); ++k) if (inf.knobs[k].steps > 1 && std::strcmp (inf.knobs[k].name, "FREEZE") && std::strcmp (inf.knobs[k].name, "SYNC")) { kk = (int) k; break; }
            auto kv = defaults (id);
            auto m = make (id, sr, kv); auto* v = ve (m);
            const size_t N = (size_t) (4 * sr);
            std::vector<float> L (N), R (N);
            for (size_t i = 0; i < N; ++i) L[i] = R[i] = 0.25f * (float) std::sin (2 * M_PI * 220.0 * (double) i / sr) * std::min (1.0f, (float) i / (float) (0.05 * sr));
            const int st = inf.knobs[(size_t) kk].steps; int idx = 0; long ducked = 0, total = 0;
            for (size_t off = 0; off < N; off += 64)
            {
                if (off >= (size_t) sr && (off % (size_t) (per * sr)) < 64) { idx = (idx + 1) % st; m->setKnob (kk, (float) idx / (st - 1)); }
                m->process (L.data() + off, R.data() + off, 64);
                if (off >= (size_t) sr) { ++total; if (v->duckG < 0.99f) ++ducked; }
            }
            std::printf ("  %-10s %-9s を %5.0f ms ごとに切り替え: プチッ %6.1f（切り替えなし 0.3〜0.9 秒 %4.1f）、ウェットを下げている時間 %3.0f %%\n", id, inf.knobs[(size_t) kk].name, per * 1000, clean (L, (size_t) sr, N, sr), clean (L, (size_t) (0.3 * sr), (size_t) (0.9 * sr), sr), 100.0 * ducked / std::max (1L, total));
        }
    std::printf ("== D4. ダックの底（snapAll）と reset の時間（オーディオスレッドで呼ばれる）==\n");
    for (double sr2 : { 48000.0, 192000.0 })
        for (const char* id : { "spacevb", "shaperoom" })
        {
            auto m = make (id, sr2, defaults (id)); auto* v = ve (m);
            double best = 1e9, worst = 0;
            for (int r = 0; r < 20; ++r)
            {
                // キャッシュを冷やす（大きな配列を書く）
                static std::vector<float> junk (64 * 1024 * 1024 / 4); for (size_t i = 0; i < junk.size(); i += 16) junk[i] += 1.0f;
                const double t0 = nowSec(); v->snapAll(); const double t = nowSec() - t0;
                best = std::min (best, t); worst = std::max (worst, t);
            }
            std::printf ("  %6.0f Hz %-10s snapAll 1 回 最速 %.3f ms / 冷えたキャッシュで最大 %.3f ms（1 ブロック 64 サンプル = %.3f ms）\n", sr2, id, best * 1000, worst * 1000, 64000.0 / sr2);
        }
}

// ============================================================================
// E. FREEZE の保ち（0.99995 の押さえで少しずつ減る）: 10 分
static void modeHold()
{
    std::printf ("== E. FREEZE を 10 分止める（ウェットだけ、48 kHz）: 1 分ごとの RMS ==\n");
    for (const char* id : { "spacevb", "clubverb", "frostverb", "eightverb" })
        for (float size : { -1.0f, 0.0f })
        {
            auto kv = defaults (id);
            if (! std::strcmp (id, "eightverb")) kv[6] = 1.0f; else if (knobIdx (id, "MIX") >= 0) kv[(size_t) knobIdx (id, "MIX")] = 1.0f;
            const int sk = knobIdx (id, "SIZE"); if (size >= 0 && sk >= 0) kv[(size_t) sk] = size;
            if (size >= 0 && sk < 0) continue;
            const int fz = knobIdx (id, "FREEZE");
            auto m = make (id, 48000, kv);
            const int B = 4800; std::vector<float> L (B), R (B);
            std::mt19937 g (2); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
            std::printf ("  %-10s%s:", id, size >= 0 ? " SIZE 0" : "       ");
            double e = 0; size_t n = 0; int minute = 0;
            for (size_t off = 0; off < (size_t) (48000 * 601.5); off += B)
            {
                for (int i = 0; i < B; ++i) { const bool on = off + (size_t) i < 48000; L[(size_t) i] = on ? u (g) : 0.0f; R[(size_t) i] = on ? u (g) : 0.0f; }
                if (off == 43200) m->setKnob (fz, 1.0f);
                m->process (L.data(), R.data(), B);
                if (off >= (size_t) (48000 * 1.5)) { for (int i = 0; i < B; ++i) e += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]); n += (size_t) B; }
                if (n >= (size_t) (48000 * 60) || (minute == 0 && n >= (size_t) (48000 * 1))) { std::printf (" %+.1f", 10 * std::log10 (e / (double) n + 1e-300)); e = 0; n = 0; ++minute; }
            }
            std::printf ("（最初は 1.5〜2.5 秒、あとは 1 分ごと）\n");
        }
}


// 長く鳴らす（指定の組・レート）: ノイズ 0.5 秒 → 無音。10 秒ごとの RMS と、中の係数での 1 周の最大
static void modeLong (const char* id, double sr, std::vector<float> kv, double secs)
{
    auto m = make (id, sr, kv); auto* v = ve (m);
    auto g = makeGrid (sr, 4000); double f; int line;
    const double lg = trueLoop (v, g, &f, &line);
    std::printf ("  %s %.0f Hz %s: 中の係数での 1 周の最大 %.6f（%.1f Hz、線 %d）\n   10 秒ごとの RMS dBFS:", id, sr, kvStr (kv).c_str(), lg, f, line);
    const int B = 4096; std::vector<float> L (B), R (B);
    std::mt19937 rg (8); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
    double e = 0; size_t n = 0; const size_t win = (size_t) (10 * sr);
    for (size_t off = 0; off < (size_t) (secs * sr); off += B)
    {
        for (int i = 0; i < B; ++i) { const bool on = off + (size_t) i < (size_t) (0.5 * sr); L[(size_t) i] = on ? u (rg) : 0.0f; R[(size_t) i] = on ? u (rg) : 0.0f; }
        m->process (L.data(), R.data(), B);
        for (int i = 0; i < B; ++i) e += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]);
        n += (size_t) B;
        if (n >= win) { std::printf (" %.1f", 10 * std::log10 (e / (double) n + 1e-300)); e = 0; n = 0; }
    }
    std::printf ("\n");
}

// 1 周の最大（中の係数）がいちばん大きくなるつまみを、ランダム＋山登りで探す（係数だけ計算: reset はせず updateCoefs(true)）
static void modeRefine (const char* id, double sr, int nRand)
{
    const auto& inf = infoOf (id);
    auto d = defaults (id);
    auto m = make (id, sr, d); auto* v = ve (m);
    auto g = makeGrid (sr, 1500);
    auto evalKv = [&] (const std::vector<float>& kv, double* f = nullptr) {
        for (int k = 0; k < (int) kv.size(); ++k) m->setKnob (k, kv[(size_t) k]);
        v->duckState = 0; v->duckG = 1.0f;
        for (int j = 0; j < vpCount; ++j) v->ps[j] = v->p[j];
        v->updateCoefs (true);
        return trueLoop (v, g, f); };
    std::mt19937 rg (12345); std::uniform_real_distribution<float> u (0, 1);
    std::vector<std::pair<double, std::vector<float>>> top;
    for (int r = 0; r < nRand; ++r)
    {
        std::vector<float> kv = d;
        for (size_t k = 0; k < kv.size(); ++k) { if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue; float x = u (rg); if (inf.knobs[k].steps > 1) x = std::round (x * (inf.knobs[k].steps - 1)) / (inf.knobs[k].steps - 1); kv[k] = x; }
        const double lg = evalKv (kv);
        top.push_back ({ lg, kv });
        if (top.size() > 400) { std::sort (top.begin(), top.end(), [] (auto& a, auto& b) { return a.first > b.first; }); top.resize (40); }
    }
    std::sort (top.begin(), top.end(), [] (auto& a, auto& b) { return a.first > b.first; }); top.resize (std::min<size_t> (top.size(), 8));
    double best = 0; std::vector<float> bestKv; double bestF = 0;
    for (auto& t : top)
    {
        auto kv = t.second; double cur = t.first;
        for (float step : { 0.1f, 0.03f, 0.01f, 0.003f })
            for (int it = 0; it < 6; ++it)
            {
                bool imp = false;
                for (size_t k = 0; k < kv.size(); ++k)
                {
                    if (inf.knobs[k].steps > 1) continue;
                    for (float sgn : { -1.0f, 1.0f })
                    {
                        auto k2 = kv; k2[k] = std::clamp (k2[k] + sgn * step, 0.0f, 1.0f);
                        const double lg = evalKv (k2);
                        if (lg > cur) { cur = lg; kv = k2; imp = true; }
                    }
                }
                if (! imp) break;
            }
        double f; cur = evalKv (kv, &f);
        if (cur > best) { best = cur; bestKv = kv; bestF = f; }
    }
    std::printf ("  %-10s %6.0f Hz: 中の係数での 1 周の最大 %.6f（%.0f Hz）%s [%s]\n", id, sr, best, bestF, best > 1.0 ? "★" : "", kvStr (bestKv).c_str());
}

static std::vector<float> wetOnlyKv (const char* id, std::vector<float> kv) { if (! std::strcmp (id, "shaperoom")) { kv[0] = 1; kv[1] = 0; } else if (! std::strcmp (id, "eightverb")) kv[6] = 1; else { int mi = knobIdx (id, "MIX"); if (mi >= 0) kv[(size_t) mi] = 1; } return kv; }
static std::unique_ptr<Module> makeB (const char* id, double sr, const std::vector<float>& kv) { auto m = makeType (typeForId (id)); for (int k = 0; k < (int) kv.size(); ++k) m->setKnob (k, kv[(size_t) k]); m->prepare (sr, 512); m->reset(); return m; }
static void modeDc()
{
    const double sr = 48000; const int B = 512;
    std::printf ("== 直流 0.01 を 30 秒入れた出口の直流（最後 1 秒の平均 ÷ 0.01）、FREEZE は OFF ==\n");
    for (const char* id : kVerbs)
    {
        std::printf ("  %-10s", id);
        for (int set = 0; set < 3; ++set)
        {
            auto kv = set == 0 ? defaults (id) : set == 1 ? std::vector<float> (defaults (id).size(), 1.0f) : wetOnlyKv (id, defaults (id));
            if (knobIdx (id, "FREEZE") >= 0) kv[(size_t) knobIdx (id, "FREEZE")] = 0.0f;
            auto m = makeB (id, sr, kv);
            std::vector<float> L (B), R (B); double acc = 0; size_t n = 0;
            for (size_t off = 0; off < (size_t) (30 * sr); off += B)
            {
                for (int i = 0; i < B; ++i) L[(size_t) i] = R[(size_t) i] = 0.01f;
                m->process (L.data(), R.data(), B);
                if (off >= (size_t) (29 * sr)) { for (int i = 0; i < B; ++i) acc += 0.5 * (L[(size_t) i] + R[(size_t) i]); n += B; }
            }
            std::printf ("  %s %+8.2f 倍", set == 0 ? "初期値" : set == 1 ? "全部1" : "初期値ウェット", acc / n / 0.01);
        }
        std::printf ("\n");
    }
    std::printf ("== ウェットだけの低い音の大きさ（初期値、正弦波 −20 dBFS を 8 秒、最後 4 秒の RMS、dB）==\n");
    for (const char* id : kVerbs)
    {
        auto kv = wetOnlyKv (id, defaults (id));
        std::printf ("  %-10s", id);
        for (double f : { 2.0, 5.0, 10.0, 20.0, 40.0, 63.0, 100.0 })
        {
            auto m = makeB (id, sr, kv);
            const size_t N = (size_t) (8 * sr); std::vector<float> L (N), R (N);
            for (size_t i = 0; i < N; ++i) L[i] = R[i] = 0.1f * (float) std::sin (2 * M_PI * f * (double) i / sr);
            for (size_t off = 0; off < N; off += B) m->process (L.data() + off, R.data() + off, B);
            double e = 0; for (size_t i = (size_t) (4 * sr); i < N; ++i) e += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
            std::printf ("  %3.0f Hz %+6.1f", f, 10 * std::log10 (e / (4 * sr) / (0.1 * 0.1 / 2)));
        }
        std::printf ("\n");
    }
    std::printf ("== 直流の段（0 → 0.01 を 1 秒目、0.01 → 0 を 11 秒目、ほかの音なし）: 出口のずれの最大（全部 1、FREEZE OFF）==\n");
    for (const char* id : kVerbs)
    {
        auto kv = std::vector<float> (defaults (id).size(), 1.0f);
        if (knobIdx (id, "FREEZE") >= 0) kv[(size_t) knobIdx (id, "FREEZE")] = 0.0f;
        auto m = makeB (id, sr, kv);
        std::vector<float> L (B), R (B); float pk = 0, pkAfter = 0;
        for (size_t off = 0; off < (size_t) (40 * sr); off += B)
        {
            for (int i = 0; i < B; ++i) { const double t = (double) (off + i) / sr; L[(size_t) i] = R[(size_t) i] = (t >= 1 && t < 11) ? 0.01f : 0.0f; }
            m->process (L.data(), R.data(), B);
            for (int i = 0; i < B; ++i) { const double t = (double) (off + i) / sr; const float a = std::abs (L[(size_t) i]); pk = std::max (pk, a); if (t > 12) pkAfter = std::max (pkAfter, a); }
        }
        std::printf ("  %-10s 出口の最大 %+6.1f dBFS（入口 −40 dBFS の直流）、止めて 1 秒後からの最大 %+6.1f dBFS\n", id, db (pk), db (pkAfter));
    }
}

int main (int argc, char** argv)
{
    const std::string m = argc > 1 ? argv[1] : "static";
    if (m == "static") modeStatic();
    else if (m == "ramp") modeRamp (argc > 2 ? std::atoi (argv[2]) : 40);
    else if (m == "svf") modeSvf();
    else if (m == "duck") modeDuck();
    else if (m == "hold") modeHold();
    else if (m == "dc") modeDc();
    else if (m == "refine") { for (const char* id : kVerbs) { if (argc > 3 && std::strcmp (argv[3], id)) continue; modeRefine (id, std::atof (argv[2]), argc > 4 ? std::atoi (argv[4]) : 3000); } }
    else if (m == "long") { std::vector<float> kv; for (int i = 4; i < argc - 1; ++i) kv.push_back ((float) std::atof (argv[i])); modeLong (argv[2], std::atof (argv[3]), kv, std::atof (argv[argc - 1])); }
    return 0;
}
