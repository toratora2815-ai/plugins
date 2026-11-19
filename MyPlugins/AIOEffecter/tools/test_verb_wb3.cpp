// test_verb_wb3.cpp — AIO Effecter の響き系（VerbEngine）の検査 3 回目の分（DSP 検査係 2026-11-19）。JUCE なし・ビルドには入らない
//   clang++ -std=c++17 -O2 -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_verb_wb3.cpp -o build/aiofx/tvw3
//   ./tvw3 <mode>
//     shelf       … TPT の Shelf1 が前の形（b0 + b1 z⁻¹）/（1 + a1 z⁻¹）と同じ伝達関数か・インパルス応答・直流の精度
//     exactramp N … つまみを動かしている途中の 1 周の大きさを、今の係数でちょうど計算（maxMag2Pair）して毎サンプル調べる（48 / 192 kHz）
//     freezestep  … FREEZE を入れるのと選ぶつまみ（MODE・DENSITY・PRESET）を変えるのが重なったとき、止めた音が残るか
//                   （「同じブロック（つまみ→FREEZE）」がラックで実際に起きる順番 = プリセットの読み込み・UNDO・オートメーション）
//     clubc       … CLUB VERB の SIZE を一気に 0.8→0.2 したときのプチッの目安 C が、動かす瞬間でどれだけぶれるか
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
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }
static std::unique_ptr<Module> make (const char* id, double sr, const std::vector<float>& kv)
{
    auto m = makeType (typeForId (id));
    for (int k = 0; k < (int) kv.size(); ++k) m->setKnob (k, kv[(size_t) k]);
    m->prepare (sr, 256); m->reset();
    return m;
}
static VerbEngine* ve (std::unique_ptr<Module>& m) { return dynamic_cast<VerbEngine*> (m.get()); }

// 前の形（2 回目まで）の Shelf1
struct OldShelf
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void set (float K, float gDC, float gHF)
    {
        const float r = std::sqrt (std::max (1.0e-6f, gDC / std::max (1.0e-6f, gHF)));
        const float d = K + 1.0f / r;
        b0 = gHF * (K + r) / d; b1 = gHF * (r - K) / d; a1 = (1.0f / r - K) / d;
    }
    float process (float x) { const float y = b0 * x + z; z = flush (b1 * x - a1 * y); return y; }
};

// ============================================================================
static void modeShelf()
{
    std::printf ("== A. TPT の Shelf1 と前の形: 伝達関数（double の式）・インパルス応答・直流の精度 ==\n");
    std::mt19937 g (5); std::uniform_real_distribution<float> u (0, 1);
    double worstTf = 0, worstIr = 0; float worstIrRel = 0;
    for (int r = 0; r < 20000; ++r)
    {
        const double sr = (r % 4 == 0) ? 44100.0 : (r % 4 == 1) ? 48000.0 : (r % 4 == 2) ? 96000.0 : 192000.0;
        const float fc = (float) std::pow (10.0, 1.7 + u (g) * 2.5);
        const float K = 1.0f / std::tan ((float) M_PI * std::min (fc, 0.45f * (float) sr) / (float) sr);
        const float gDC = (float) std::pow (10.0, -2.0 + u (g) * 3.0), gHF = (float) std::pow (10.0, -2.0 + u (g) * 3.0);
        Shelf1 n; n.set (K, gDC, gHF); n.ramp (true, 1.0f);
        OldShelf o; o.set (K, gDC, gHF);
        // 伝達関数: 前の形の係数を double で作り直して、新しい eq() と周波数ごとに比べる
        const double rr = std::sqrt ((double) gDC / gHF), d = K + 1.0 / rr;
        const double ob0 = gHF * (K + rr) / d, ob1 = gHF * (rr - K) / d, oa1 = (1.0 / rr - K) / d;
        double nb0, nb1, na1; n.eq (true, nb0, nb1, na1);
        for (int j = 0; j <= 50; ++j)
        {
            const double w = M_PI * j / 50.0; const std::complex<double> z1 = std::polar (1.0, -w);
            const double mo = std::abs ((ob0 + ob1 * z1) / (1.0 + oa1 * z1)), mn = std::abs ((nb0 + nb1 * z1) / (1.0 + na1 * z1));
            worstTf = std::max (worstTf, std::abs (mo - mn) / std::max (mo, 1e-9));
        }
        // インパルス応答（float で 200 サンプル）
        float pkO = 0;
        for (int s = 0; s < 200; ++s) { const float x = s == 0 ? 1.0f : 0.0f; const float yo = o.process (x), yn = n.process (x); worstIr = std::max (worstIr, (double) std::abs (yo - yn)); pkO = std::max (pkO, std::abs (yo)); }
        worstIrRel = std::max (worstIrRel, (float) (worstIr / std::max (1e-9f, pkO)));
    }
    std::printf ("  ランダム 2 万組（角 50 Hz〜16 kHz、gDC・gHF 0.01〜10、44.1〜192 kHz）: 大きさの差の最大 %.2e（相対）、インパルス応答の差の最大 %.2e\n", worstTf, worstIr);
    // 直流の精度: gDC = gHF = 1（FREEZE 中の素通し）・角 50 Hz・192 kHz で、直流 1 を 10 秒通したときの出口
    for (double sr : { 48000.0, 192000.0 })
        for (float fc : { 50.0f, 250.0f })
            for (float gd : { 1.0f, 1.0005f })
            {
                const float K = 1.0f / std::tan ((float) M_PI * fc / (float) sr);
                Shelf1 n; n.set (K, gd, 1.0f); n.ramp (true, 1.0f);
                OldShelf o; o.set (K, gd, 1.0f);
                double yn = 0, yo = 0;
                for (long s = 0; s < (long) (10 * sr); ++s) { yn = n.process (1.0f); yo = o.process (1.0f); }
                std::printf ("  %6.0f Hz・角 %4.0f Hz・gDC %.4f: 直流 1 を 10 秒 → 新しい形 %.8f（ずれ %+.1e）/ 前の形 %.8f（ずれ %+.1e）\n", sr, fc, gd, yn, yn - gd, yo, yo - gd);
            }
}

// ============================================================================
// B. つまみを動かしている途中の 1 周の最大（今の係数で、ちょうどの最大 = maxMag2Pair）を毎サンプル
static double exactLoop (VerbEngine* v, int* lineAt = nullptr)
{
    double worst = 0;
    for (int i = 0; i < 8; ++i)
    {
        double l0, l1, la, h0, h1, ha;
        v->lossLo[i].eq (false, l0, l1, la); v->lossHi[i].eq (false, h0, h1, ha);
        const double m = (double) v->lossG[i] * std::sqrt (maxMag2Pair (l0, l1, la, h0, h1, ha));
        if (m > worst) { worst = m; if (lineAt) *lineAt = i; }
    }
    return worst;
}
static void modeExactRamp (int nJumps)
{
    std::printf ("== B. 動かしている途中の 1 周の最大（今の係数でちょうどの最大、毎サンプル）。ランダムな組へ一気に・1 本ずつ 0.3 秒・FREEZE の ON/OFF・選ぶつまみ ==\n");
    for (double sr : { 48000.0, 192000.0 })
        for (const char* id : kVerbs)
        {
            const auto& inf = infoOf (id);
            auto d = defaults (id);
            const int fz = knobIdx (id, "FREEZE");
            std::mt19937 rg (77); std::uniform_real_distribution<float> u (0, 1);
            double worst = 0; std::string at; long over = 0, checks = 0;
            auto runCase = [&] (std::vector<std::pair<size_t, std::vector<float>>> sched, size_t len, const std::string& label)
            {
                auto m = make (id, sr, sched[0].second); auto* v = ve (m);
                float L[1], R[1]; std::mt19937 ng (3); std::uniform_real_distribution<float> nd (-0.25f, 0.25f);
                size_t si = 1;
                for (size_t s = 0; s < len; ++s)
                {
                    while (si < sched.size() && sched[si].first <= s) { for (int k = 0; k < (int) sched[si].second.size(); ++k) m->setKnob (k, sched[si].second[(size_t) k]); ++si; }
                    L[0] = nd (ng); R[0] = nd (ng);
                    m->process (L, R, 1);
                    const double lg = exactLoop (v); ++checks;
                    if (lg > 1.0) ++over;
                    if (lg > worst) { worst = lg; at = label; }
                }
            };
            {
                std::vector<std::pair<size_t, std::vector<float>>> sched { { 0, d } };
                for (int j = 1; j <= nJumps; ++j)
                {
                    std::vector<float> kv;
                    for (auto& ki : inf.knobs) { float x = u (rg); const int pick = (int) (u (rg) * 5); if (pick == 0) x = 0; else if (pick == 1) x = 1; if (ki.steps > 1) x = std::round (x * (ki.steps - 1)) / (ki.steps - 1); kv.push_back (x); }
                    if (fz >= 0) kv[(size_t) fz] = u (rg) < 0.3f ? 1.0f : 0.0f;
                    sched.push_back ({ (size_t) (j * 0.25 * sr), kv });
                }
                runCase (sched, (size_t) ((nJumps + 1) * 0.25 * sr), "ランダムへ一気に");
            }
            for (size_t k = 0; k < d.size(); ++k)
                for (int dir = 0; dir < 2; ++dir)
                {
                    std::vector<std::pair<size_t, std::vector<float>>> sched;
                    auto kv = d; kv[k] = (float) dir; sched.push_back ({ 0, kv });
                    for (int s = 0; s <= 45; ++s) { auto k2 = d; k2[k] = dir ? 1.0f - s / 45.0f : s / 45.0f; sched.push_back ({ (size_t) (0.2 * sr) + (size_t) (s * 0.3 / 45.0 * sr), k2 }); }
                    runCase (sched, (size_t) (0.8 * sr), std::string (inf.knobs[k].name) + (dir ? " 1→0" : " 0→1"));
                }
            std::printf ("  %6.0f Hz %-10s 最大 %.6f %s 1 を超えたサンプル %ld / %ld [%s]\n", sr, id, worst, worst > 1.0 ? "★" : "", over, checks, at.c_str());
        }
}

// ============================================================================
// C. FREEZE と選ぶつまみが同時・近い時刻に変わる（プリセットの読み込み・UNDO・オートメーション）
static double rmsDb (const std::vector<float>& L, const std::vector<float>& R, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
    return 10 * std::log10 (e / (double) (b - a) + 1e-300);
}
static void modeFreezeStep()
{
    std::printf ("== C. FREEZE を入れるのと選ぶつまみを変えるのが重なったとき（ウェットだけ、ノイズ −12 dBFS を 1 秒、0.9 秒で操作、止めたあと 2〜3 秒の RMS）==\n");
    const double sr = 48000; const int B = 256;
    struct C { const char* id; const char* knob; float a, b; } cs[] = { { "spacevb", "MODE", 0.5f, 1.0f }, { "spacevb", "DENSITY", 0.0f, 1.0f }, { "eightverb", "PRESET", 3.0f / 7.0f, 4.0f / 7.0f } };
    for (auto& c : cs)
    {
        const int fz = knobIdx (c.id, "FREEZE"), kk = knobIdx (c.id, c.knob);
        auto base = defaults (c.id);
        if (! std::strcmp (c.id, "eightverb")) base[6] = 1.0f; else base[(size_t) knobIdx (c.id, "MIX")] = 1.0f;
        base[(size_t) kk] = c.a;
        // 0: FREEZE だけ（くらべる基準） / 1: 同じブロックで「選ぶつまみ → FREEZE」（ラックの並び = 番号の小さい順） / 2: 同じブロックで「FREEZE → 選ぶつまみ」
        // 3〜6: 選ぶつまみを変えて 10 / 30 / 60 / 120 ms 後に FREEZE
        const char* lab[] = { "FREEZE だけ", "同じブロック（つまみ→FREEZE）", "同じブロック（FREEZE→つまみ）", "つまみ → 10 ms 後に FREEZE", "つまみ → 30 ms 後", "つまみ → 60 ms 後", "つまみ → 120 ms 後" };
        const double delays[] = { 0, 0, 0, 0.010, 0.030, 0.060, 0.120 };
        std::printf ("  %s %s %.2f→%.2f:\n", c.id, c.knob, c.a, c.b);
        for (int cs2 = 0; cs2 < 7; ++cs2)
        {
            auto m = make (c.id, sr, base);
            const size_t N = (size_t) (3.5 * sr);
            std::vector<float> L (N, 0.0f), R (N, 0.0f);
            std::mt19937 g (4); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
            for (size_t i = 0; i < (size_t) (1.0 * sr); ++i) { L[i] = u (g); R[i] = u (g); }
            const size_t tOp = (size_t) (0.9 * sr) / B * B, tF = tOp + (size_t) (delays[cs2] * sr) / B * B;
            for (size_t off = 0; off < N; off += B)
            {
                if (off == tOp)
                {
                    if (cs2 == 0) m->setKnob (fz, 1.0f);
                    else if (cs2 == 1) { m->setKnob (kk, c.b); m->setKnob (fz, 1.0f); }
                    else if (cs2 == 2) { m->setKnob (fz, 1.0f); m->setKnob (kk, c.b); }
                    else m->setKnob (kk, c.b);
                }
                if (cs2 >= 3 && off == tF) m->setKnob (fz, 1.0f);
                m->process (L.data() + off, R.data() + off, B);
            }
            std::printf ("     %-30s 止めた音 2〜3 秒 %+7.1f dBFS\n", lab[cs2], rmsDb (L, R, (size_t) (2.0 * sr), (size_t) (3.0 * sr)));
        }
    }
    // FREEZE 中に待たせた切り替えが、FREEZE を外すと 1 回だけ効くか（いろいろな並び）
    std::printf ("== C2. FREEZE 中に MODE を 2 回変えて元に戻す → 外す: ダックが起きないか / 変えたまま外す: 1 回だけダック ==\n");
    for (int variant = 0; variant < 3; ++variant)
    {
        auto base = defaults ("spacevb"); base[13] = 1.0f;
        auto m = make ("spacevb", sr, base); auto* v = ve (m);
        std::vector<float> L (B), R (B); std::mt19937 g (1); std::uniform_real_distribution<float> u (-0.25f, 0.25f);
        int ducks = 0; int prevState = 0;
        for (size_t off = 0; off < (size_t) (3 * sr); off += B)
        {
            const double t = (double) off / sr;
            if (std::abs (t - 0.50) < 0.003) m->setKnob (14, 1.0f);
            if (std::abs (t - 0.80) < 0.003) m->setKnob (0, 1.0f);
            if (std::abs (t - 1.00) < 0.003) m->setKnob (0, variant == 0 ? 0.5f : 0.0f);
            if (std::abs (t - 1.50) < 0.003) m->setKnob (14, 0.0f);
            for (int i = 0; i < B; ++i) { L[(size_t) i] = t < 0.4 ? u (g) : 0.0f; R[(size_t) i] = L[(size_t) i]; }
            m->process (L.data(), R.data(), B);
            if (v->duckState == 1 && prevState != 1) ++ducks;
            prevState = v->duckState;
        }
        std::printf ("  %s: ダック %d 回、外したあとの MODE の値 k %.2f / 頼まれた値 %.2f\n", variant == 0 ? "0.5→1.0→0.5（元に戻す）" : variant == 1 ? "0.5→1.0→0.0（変えたまま）" : "0.5→1.0→0.0（同じ）", ducks, v->k[0], v->kReq[0]);
        if (variant == 1) break;
    }
}

// ============================================================================
// D. CLUB VERB の SIZE を一気に 0.8→0.2（ウェットだけ）: 動かす瞬間をずらしたときの C のぶれ
static float cleanliness (const std::vector<float>& x, size_t a, size_t b, double sr, double f = 220.0)
{
    const double w = 2 * M_PI * f / sr; const size_t win = (size_t) (0.005 * sr), ctx = (size_t) (0.02 * sr);
    float worst = 0;
    for (size_t s = std::max (a, ctx + 2); s + win < std::min (b, x.size() - ctx); s += win)
    {
        float d2 = 0, pk = 0;
        for (size_t i = s; i < s + win; ++i) d2 = std::max (d2, std::abs (x[i] - 2 * x[i - 1] + x[i - 2]));
        for (size_t i = s - ctx; i < s + win + ctx; ++i) pk = std::max (pk, std::abs (x[i]));
        if (pk > 1e-4f) worst = std::max (worst, (float) (d2 / (pk * w * w)));
    }
    return worst;
}
static void modeClubC()
{
    std::printf ("== D. CLUB VERB の SIZE を一気に 0.8→0.2（ウェットだけ、220 Hz −12 dBFS）: 動かす瞬間を 1 ブロックずつずらしたときの C ==\n   ");
    const double sr = 48000; const int B = 256;
    float mn = 1e9, mx = 0;
    for (int shift = 0; shift < 16; ++shift)
    {
        auto kv = defaults ("clubverb"); kv[3] = 1.0f; kv[0] = 0.8f;
        auto m = make ("clubverb", sr, kv);
        const size_t N = (size_t) (2.4 * sr); std::vector<float> L (N), R (N);
        for (size_t i = 0; i < N; ++i) L[i] = R[i] = 0.25f * (float) std::sin (2 * M_PI * 220.0 * (double) i / sr) * std::min (1.0f, (float) i / (float) (0.1 * sr));
        const size_t t1 = (size_t) (1.2 * sr) / B * B + (size_t) shift * B;
        for (size_t off = 0; off < N; off += B) { if (off == t1) m->setKnob (0, 0.2f); m->process (L.data() + off, R.data() + off, B); }
        const float c = cleanliness (L, t1, t1 + (size_t) (0.12 * sr), sr);
        mn = std::min (mn, c); mx = std::max (mx, c);
        // 6 kHz から上（4 段のハイパス）の山 ÷ 出口の山
        Biquad hp[4]; for (auto& b : hp) { b.setHP (sr, 6000.0, 0.7071); b.reset(); }
        float hfAt = 0, hfBefore = 0, sig = 0;
        for (size_t i = 0; i < N; ++i) { float y = L[i]; for (auto& b : hp) y = b.process (y); if (i >= (size_t) (0.7 * sr) && i < t1) hfBefore = std::max (hfBefore, std::abs (y)); if (i >= t1 && i < t1 + (size_t) (0.15 * sr)) hfAt = std::max (hfAt, std::abs (y)); if (i >= t1 - (size_t) (0.2 * sr)) sig = std::max (sig, std::abs (L[i])); }
        std::printf (" C %.1f/Hd %+.0f", c, db (hfAt / std::max (1e-9f, sig)));
    }
    std::printf ("\n   最小 %.1f / 最大 %.1f\n", mn, mx);
}

int main (int argc, char** argv)
{
    const std::string m = argc > 1 ? argv[1] : "shelf";
    if (m == "shelf") modeShelf();
    else if (m == "exactramp") modeExactRamp (argc > 2 ? std::atoi (argv[2]) : 40);
    else if (m == "freezestep") modeFreezeStep();
    else if (m == "clubc") modeClubC();
    return 0;
}
