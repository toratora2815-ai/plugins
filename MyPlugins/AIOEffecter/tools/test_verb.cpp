// test_verb.cpp — AIO Effecter 0.3.0 の響き系 9 部品（VerbEngine）の DSP 検査（DSP 検査係 2026-11-19）。JUCE なし・ビルドには入らない
//   clang++ -std=c++17 -O2 -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_verb.cpp -o build/aiofx/tv
//   ./tv <mode>
//     stab      … 発振の確かめ（本物のエンジンを鳴らす）: 下の式で 1 周の大きさが 0.995 を超える組を探し、上位を 25 秒鳴らして増えていないか
//     loop N / grid 5 / few <id> 2|3 / map <id> <縦のつまみ> <横のつまみ> … 1 周の大きさ（帯域ごとの最大）を VerbEngine::updateCoefs と同じ式で計算
//                 （※ updateCoefs の消え方の式を変えたら loopGain() も合わせる。stab は本物を鳴らすので式を変えても使える）
//                 ※ 2 回目の検査の直し（1 周の最大をちょうど計算して押さえる）より前の式の写し。今のエンジンの 1 周は tools/test_verb_wb.cpp の static / refine / ramp で見る
//     transgrid … FREEZE を入れる / 切る途中の 1 周の大きさ
//     decay N T / decay1 <id> <つまみ…> <秒> … 長い余韻（1 秒ごとの RMS）。freeze H N / freeze1 <id> <つまみ…> … FREEZE を長く・ON/OFF の膨らみ
//     click all [wet] / click2 <id> <k> <a> <b> [k v …] … つまみのプチッ（220 Hz。C = 2 階差の目安（10 以上でプチッ）、Hd = 6 kHz 以上の山の大きさ（出口の山に対する dB））
//     rev       … SHAPE ROOM の REVERSE ON のまま SIZE / DECAY などを動かす
//     fb / fbpk … SPACE VERB の FEEDBACK（PREDELAY 0 で 1 サンプルの輪）。lv <id> [k v …] … 音量
//     fuzzv N / rates / block / reset / cpu / cpunofz / mem / level / knoblevel / ranges / diff <id> <k> <a> <b>
#include "fx/Registry.h"
#include "fx/Rack.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <cmath>
#include <complex>
#include <functional>
#include <algorithm>

using namespace aiofx;

static const char* kVerbs[] = { "spacevb", "classicvb", "shaperoom", "pickroom", "clubverb", "digihall", "digihallxl", "frostverb", "eightverb" };

struct Sig { std::vector<float> L, R; size_t size() const { return L.size(); } };
static Sig zeros (double sr, double s) { Sig x; x.L.assign ((size_t) (sr * s), 0.0f); x.R = x.L; return x; }
static Sig sine (double sr, double s, double hz, float a) { Sig x = zeros (sr, s); for (size_t i = 0; i < x.size(); ++i) x.L[i] = x.R[i] = a * (float) std::sin (2 * M_PI * hz * (double) i / sr); return x; }
static Sig noise (double sr, double s, float a, uint32_t seed = 1) { Sig x = zeros (sr, s); std::mt19937 g (seed); std::uniform_real_distribution<float> d (-1, 1); for (size_t i = 0; i < x.size(); ++i) { x.L[i] = a * d (g); x.R[i] = a * d (g); } return x; }
static void append (Sig& a, const Sig& b) { a.L.insert (a.L.end(), b.L.begin(), b.L.end()); a.R.insert (a.R.end(), b.R.begin(), b.R.end()); }
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }
static double nowSec() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

static const ModuleInfo& infoOf (const char* id) { return *infoFor (typeForId (id)); }
static std::vector<float> defaults (const char* id) { std::vector<float> v; for (auto& k : infoOf (id).knobs) v.push_back (k.def); return v; }
static int knobIdx (const char* id, const char* name) { const auto& inf = infoOf (id); for (size_t k = 0; k < inf.knobs.size(); ++k) if (! std::strcmp (inf.knobs[k].name, name)) return (int) k; return -1; }
static int freezeIdx (const char* id) { return knobIdx (id, "FREEZE"); }

// render.cpp / test_inspect と同じ作り方（なめらかさを今の値に合わせる）
static std::unique_ptr<Module> make (const char* id, double sr, int block, const std::vector<float>& kv)
{
    auto m = makeType (typeForId (id));
    const auto& inf = infoOf (id);
    for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, k < (int) kv.size() ? kv[(size_t) k] : inf.knobs[(size_t) k].def);
    m->prepare (sr, block);
    m->reset();
    return m;
}
static void run (Module& m, Sig& s, int B, size_t from = 0, size_t to = (size_t) -1)
{
    to = std::min (to, s.size());
    for (size_t off = from; off < to; off += (size_t) B) m.process (s.L.data() + off, s.R.data() + off, (int) std::min ((size_t) B, to - off));
}
static std::vector<float> randomKnobs (const char* id, std::mt19937& g, bool respectSteps, bool noFreeze)
{
    const auto& inf = infoOf (id);
    std::uniform_real_distribution<float> u (0, 1);
    std::vector<float> v;
    for (auto& k : inf.knobs)
    {
        float x = u (g); const int pick = (int) (u (g) * 6);
        if (pick == 0) x = 0; else if (pick == 1) x = 1;
        if (respectSteps && k.steps > 1) x = std::round (x * (float) (k.steps - 1)) / (float) (k.steps - 1);
        v.push_back (x);
    }
    if (noFreeze && freezeIdx (id) >= 0) v[(size_t) freezeIdx (id)] = 0.0f;
    return v;
}
static std::string kvStr (const std::vector<float>& kv) { std::string s = "{"; for (size_t i = 0; i < kv.size(); ++i) { char b[16]; std::snprintf (b, sizeof b, "%s%.2f", i ? "," : "", kv[i]); s += b; } return s + "}"; }

static const char* pName[vpCount] = { "Pre", "PreFb", "Diff", "DiffSz", "Er", "ErSize", "Late", "Size", "T60", "LowR", "HighR", "XLo", "XHi", "ModD", "ModR", "LoCut", "HiCut",
                                       "EqLo", "EqMid", "EqHi", "Width", "Wet", "Dry", "Freeze", "Echo", "VDelay", "EqLoF", "EqMidF", "EqMidQ", "EqHiF", "Reverse", "RevLen", "Band" };

static std::vector<float> targets (const char* id, const std::vector<float>& kv, double sr = 48000)
{
    auto m = make (id, sr, 256, kv);
    auto* v = dynamic_cast<VerbEngine*> (m.get());
    std::vector<float> p (vpCount);
    for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j);
    return p;
}

// ---- FDN の 1 周の大きさ（帯域ごとの最大）: VerbEngine::updateCoefs と同じ式 ----
struct Sh { double b0, b1, a1; };
static Sh shelf (float K, float gDC, float gHF)
{
    const float r = std::sqrt (std::max (1.0e-6f, gDC / std::max (1.0e-6f, gHF)));
    const float d = K + 1.0f / r;
    return { gHF * (K + r) / d, gHF * (r - K) / d, (1.0f / r - K) / d };
}
static double shMag (const Sh& s, double w) { std::complex<double> z1 = std::polar (1.0, -w); return std::abs ((s.b0 + s.b1 * z1) / (1.0 + s.a1 * z1)); }
static double loopGain (const std::vector<float>& p, double sr, float frz, double* worstF = nullptr)
{
    static const int kLen[8] { 1559, 1687, 1801, 1931, 2053, 2203, 2347, 2503 };
    const float fsr = (float) sr;
    const float Tm = std::pow (10.0f, p[vpT60]);
    const float Tl = Tm * std::pow (2.0f, p[vpLowR]), Th = Tm * std::pow (2.0f, p[vpHighR]);
    const float Klo = 1.0f / std::tan ((float) M_PI * std::min (std::pow (10.0f, p[vpXLo]), 0.45f * fsr) / fsr);
    const float Khi = 1.0f / std::tan ((float) M_PI * std::min (std::pow (10.0f, p[vpXHi]), 0.45f * fsr) / fsr);
    double worst = 0; double wf = 0;
    for (int i = 0; i < 8; ++i)
    {
        const float baseLen = (float) (kLen[i] * sr / 48000.0);
        const float dsec = baseLen * p[vpSize] / fsr;
        float gm = std::pow (10.0f, -3.0f * dsec / Tm);
        float al = std::pow (10.0f, -3.0f * dsec * (1.0f / Tl - 1.0f / Tm));
        float bh = std::pow (10.0f, -3.0f * dsec * (1.0f / Th - 1.0f / Tm));
        al = std::min (al, 1.0f / gm); bh = std::min (bh, 1.0f / gm);
        gm += (1.0f - gm) * frz; al += (1.0f - al) * frz; bh += (1.0f - bh) * frz;
        const Sh lo = shelf (Klo, al, 1.0f), hi = shelf (Khi, 1.0f, bh);
        for (int fi = 0; fi <= 600; ++fi)
        {
            const double f = 5.0 * std::pow (0.499 * sr / 5.0, fi / 600.0);
            const double w = 2 * M_PI * f / sr;
            const double g = gm * shMag (lo, w) * shMag (hi, w);
            if (g > worst) { worst = g; wf = f; }
        }
    }
    if (worstF) *worstF = wf;
    return worst;
}

// ============================================================================
static void modeRanges()
{
    std::printf ("== 部品ごとの、つまみ 1 本を 0..1 で動かしたときのエンジンの値の範囲（他は初期値）==\n");
    const int show[] = { vpPre, vpPreFb, vpT60, vpLowR, vpHighR, vpXLo, vpXHi, vpSize, vpErSize, vpVDelay, vpWet, vpDry, vpEcho, vpEr, vpLate, vpHiCut, vpLoCut, vpBand, vpModD, vpDiffSize };
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        auto p0 = targets (id, d);
        std::printf ("  %s 初期値:", id); for (int j : show) std::printf (" %s=%.3g", pName[j], p0[(size_t) j]); std::printf ("\n");
        for (size_t k = 0; k < inf.knobs.size(); ++k)
        {
            std::vector<float> mn (vpCount, 1e9f), mx (vpCount, -1e9f);
            const int N = inf.knobs[k].steps > 1 ? inf.knobs[k].steps : 41;
            for (int s = 0; s < N; ++s)
            {
                auto kv = d; kv[k] = (float) s / (float) (N - 1);
                auto p = targets (id, kv);
                for (int j = 0; j < vpCount; ++j) { mn[(size_t) j] = std::min (mn[(size_t) j], p[(size_t) j]); mx[(size_t) j] = std::max (mx[(size_t) j], p[(size_t) j]); }
            }
            std::printf ("    %-10s", inf.knobs[k].name);
            for (int j : show) if (mx[(size_t) j] - mn[(size_t) j] > 1e-4f) std::printf (" %s %.3g..%.3g", pName[j], mn[(size_t) j], mx[(size_t) j]);
            std::printf ("\n");
        }
    }
}

// ============================================================================
static void modeLoop (int nRand)
{
    std::printf ("== FDN の 1 周の大きさの最大（帯域ごと、全部の周波数で）。1 を超えると発振。FREEZE の途中（frz 0..1）も ==\n");
    for (const char* id : kVerbs)
    {
        std::mt19937 g (4242);
        auto d = defaults (id);
        std::vector<std::vector<float>> sets { d, std::vector<float> (d.size(), 0.0f), std::vector<float> (d.size(), 1.0f), std::vector<float> (d.size(), 0.5f) };
        for (int r = 0; r < nRand; ++r) sets.push_back (randomKnobs (id, g, r % 3 != 0, false));
        // つまみ 1 本ずつ端
        for (size_t k = 0; k < d.size(); ++k) for (float v : { 0.0f, 1.0f }) { auto kv = d; kv[k] = v; sets.push_back (kv); }
        double worstSteady = 0, worstTrans = 0; std::string atS, atT; double fS = 0, fT = 0; float frzT = 0; int nOver = 0, nTransOver = 0;
        for (auto& kv : sets)
        {
            for (double sr : { 44100.0, 48000.0, 192000.0 })
            {
                auto p = targets (id, kv, sr);
                double f; const double g0 = loopGain (p, sr, 0.0f, &f);
                if (g0 > 1.0 + 1e-6) ++nOver;
                if (g0 > worstSteady) { worstSteady = g0; atS = kvStr (kv) + " sr " + std::to_string ((int) sr); fS = f; }
                for (int fi = 1; fi < 20; ++fi)
                {
                    const float frz = fi / 20.0f;
                    const double gt = loopGain (p, sr, frz, &f);
                    if (gt > 1.0 + 1e-6) ++nTransOver;
                    if (gt > worstTrans) { worstTrans = gt; atT = kvStr (kv) + " sr " + std::to_string ((int) sr); fT = f; frzT = frz; }
                }
            }
        }
        std::printf ("  %-10s ふだんの最大 %.5f（%.0f Hz）%s  1 超えの組 %d  [%s]\n", id, worstSteady, fS, worstSteady > 1.0 + 1e-6 ? "★1 超え" : "", nOver, atS.c_str());
        std::printf ("  %-10s FREEZE の途中の最大 %.4f（frz %.2f、%.0f Hz）%s  1 超えの組 %d / %zu  [%s]\n", "", worstTrans, frzT, fT, worstTrans > 1.0 + 1e-6 ? "★" : "", nTransOver, sets.size() * 3 * 19, atT.c_str());
    }
}

// ============================================================================
// 長い余韻: ノイズ 0.5 秒 → 無音。1 秒ごとの RMS。増えていないか・最後の大きさ
static std::vector<double> decayCurve (const char* id, const std::vector<float>& kv, double sr, double inSec, double totalSec, float amp, int B = 512)
{
    auto m = make (id, sr, B, kv);
    const size_t win = (size_t) sr;
    const size_t total = (size_t) (totalSec * sr), inN = (size_t) (inSec * sr);
    std::vector<float> L (B), R (B);
    std::mt19937 g (99); std::uniform_real_distribution<float> u (-1, 1);
    std::vector<double> out; double e = 0; size_t cnt = 0;
    for (size_t off = 0; off < total; off += (size_t) B)
    {
        const int n = (int) std::min ((size_t) B, total - off);
        for (int i = 0; i < n; ++i) { const bool on = off + (size_t) i < inN; L[(size_t) i] = on ? amp * u (g) : 0.0f; R[(size_t) i] = on ? amp * u (g) : 0.0f; }
        m->process (L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) { out.push_back (1e30); return out; }
            e += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i];
            if (++cnt == win) { out.push_back (std::sqrt (e / (2.0 * (double) win))); e = 0; cnt = 0; }
        }
    }
    return out;
}
static void modeDecay (int nRand, double total)
{
    std::printf ("== 長い余韻（−12 dBFS ノイズ 0.5 秒 → 無音 %.0f 秒、48 kHz）。FREEZE は OFF。1 秒ごとの RMS ==\n", total);
    std::printf ("   増え = 2 秒目より後で、5 秒前より大きくなった最大 dB（響きの揺れで ±1 dB はある）\n");
    for (const char* id : kVerbs)
    {
        std::mt19937 g (31);
        auto d = defaults (id);
        std::vector<std::vector<float>> sets { d, std::vector<float> (d.size(), 1.0f), std::vector<float> (d.size(), 0.0f) };
        for (int r = 0; r < nRand; ++r) sets.push_back (randomKnobs (id, g, true, true));
        if (freezeIdx (id) >= 0) for (auto& s : sets) s[(size_t) freezeIdx (id)] = 0.0f;
        double worstGrow = -999; std::string atG; double worstLast = -999; std::string atL; double defAt10 = 0, defLast = 0, def1 = 0;
        for (size_t si = 0; si < sets.size(); ++si)
        {
            auto c = decayCurve (id, sets[si], 48000, 0.5, total, 0.25f);
            double grow = -999;
            for (size_t i = 7; i < c.size(); ++i) grow = std::max (grow, db (c[i]) - db (c[i - 5]));
            if (grow > worstGrow) { worstGrow = grow; atG = kvStr (sets[si]); }
            if (db (c.back()) > worstLast) { worstLast = db (c.back()); atL = kvStr (sets[si]); }
            if (si == 0) { def1 = db (c[0]); defAt10 = db (c[std::min<size_t> (10, c.size() - 1)]); defLast = db (c.back()); }
        }
        std::printf ("  %-10s 初期値: 0〜1 秒 %+6.1f、10〜11 秒 %+6.1f、最後 %+6.1f dBFS | 全部の組で 増えの最大 %+5.1f dB [%s]\n", id, def1, defAt10, defLast, worstGrow, atG.c_str());
        std::printf ("  %-10s 最後の 1 秒の最大 %+6.1f dBFS [%s]\n", "", worstLast, atL.c_str());
    }
}

// 指定の 1 組の余韻を詳しく
static void modeDecay1 (const char* id, const std::vector<float>& kv, double total)
{
    auto c = decayCurve (id, kv, 48000, 0.5, total, 0.25f);
    auto p = targets (id, kv);
    std::printf ("  %s %s T60=%.2f s LowR=%.2f HighR=%.2f XLo=%.0f XHi=%.0f Size=%.2f PreFb=%.2f loop=%.5f\n", id, kvStr (kv).c_str(), std::pow (10.0, p[vpT60]), p[vpLowR], p[vpHighR],
                 std::pow (10.0, p[vpXLo]), std::pow (10.0, p[vpXHi]), p[vpSize], p[vpPreFb], loopGain (p, 48000, 0));
    std::printf ("   ");
    for (size_t i = 0; i < c.size(); ++i) { std::printf (" %.1f", db (c[i])); if (i % 20 == 19) std::printf ("\n   "); }
    std::printf ("\n");
}

// ============================================================================
// FREEZE: 鳴らしながら ON → 無音で長く → OFF。止めている間の大きさの変化と、ON / OFF の瞬間の膨らみ（低域も）
static void modeFreeze (double holdSec, int nRand)
{
    std::printf ("== FREEZE（ノイズ −12 dBFS を 1.5 秒、1.0 秒で ON → 無音で %.0f 秒止める → OFF → 10 秒）48 kHz ==\n", holdSec);
    const double sr = 48000; const int B = 256;
    for (const char* id : kVerbs)
    {
        const int fz = freezeIdx (id);
        if (fz < 0) continue;
        std::mt19937 g (55);
        auto d = defaults (id);
        std::vector<std::vector<float>> sets { d, std::vector<float> (d.size(), 1.0f), std::vector<float> (d.size(), 0.0f) };
        for (int r = 0; r < nRand; ++r) sets.push_back (randomKnobs (id, g, true, true));
        for (size_t si = 0; si < sets.size(); ++si)
        {
            auto kv = sets[si]; kv[(size_t) fz] = 0.0f;
            auto m = make (id, sr, B, kv);
            const size_t tOn = (size_t) (1.0 * sr) / B * B, tIn = (size_t) (1.5 * sr), tOff = (size_t) ((1.5 + holdSec) * sr) / B * B, tEnd = tOff + (size_t) (10.0 * sr);
            std::vector<float> L (B), R (B);
            std::mt19937 gg (7); std::uniform_real_distribution<float> u (-1, 1);
            // 20 ms ごとの RMS（全帯域と 150 Hz 以下）
            const size_t w = (size_t) (0.02 * sr);
            std::vector<double> rms, rmsLo; double e = 0, eLo = 0; size_t cnt = 0; float lpL = 0, lpR = 0; const float a = (float) std::exp (-2 * M_PI * 150.0 / sr);
            bool bad = false;
            for (size_t off = 0; off < tEnd; off += B)
            {
                if (off == tOn) m->setKnob (fz, 1.0f);
                if (off == tOff) m->setKnob (fz, 0.0f);
                for (int i = 0; i < B; ++i) { const bool on = off + (size_t) i < tIn; L[(size_t) i] = on ? 0.25f * u (gg) : 0.0f; R[(size_t) i] = on ? 0.25f * u (gg) : 0.0f; }
                m->process (L.data(), R.data(), B);
                for (int i = 0; i < B; ++i)
                {
                    if (! std::isfinite (L[(size_t) i])) bad = true;
                    lpL = L[(size_t) i] + (lpL - L[(size_t) i]) * a; lpR = R[(size_t) i] + (lpR - R[(size_t) i]) * a;
                    e += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; eLo += (double) lpL * lpL + (double) lpR * lpR;
                    if (++cnt == w) { rms.push_back (std::sqrt (e / (2.0 * w))); rmsLo.push_back (std::sqrt (eLo / (2.0 * w))); e = eLo = 0; cnt = 0; }
                }
            }
            auto at = [&] (double t) { size_t i = std::min (rms.size() - 1, (size_t) (t / 0.02)); double s = 0; int n = 0; for (size_t j = i; j < std::min (rms.size(), i + 25); ++j) { s += rms[j] * rms[j]; ++n; } return db (std::sqrt (s / n)); };
            auto atLo = [&] (double t) { size_t i = std::min (rmsLo.size() - 1, (size_t) (t / 0.02)); double s = 0; int n = 0; for (size_t j = i; j < std::min (rmsLo.size(), i + 25); ++j) { s += rmsLo[j] * rmsLo[j]; ++n; } return db (std::sqrt (s / n)); };
            // OFF の直後 2 秒の 20 ms の最大 と OFF の直前 0.5 秒
            double preOff = 0, postOff = 0, preOffLo = 0, postOffLo = 0;
            const size_t iOff = tOff / w;
            for (size_t j = iOff - 25; j < iOff; ++j) { preOff = std::max (preOff, rms[j]); preOffLo = std::max (preOffLo, rmsLo[j]); }
            for (size_t j = iOff; j < std::min (rms.size(), iOff + 100); ++j) { postOff = std::max (postOff, rms[j]); postOffLo = std::max (postOffLo, rmsLo[j]); }
            // ON の膨らみ: 入力が止まってから（1.6〜2.5 秒）の最大と 3 秒の値
            double peakAfterOn = 0, peakAfterOnLo = 0; for (size_t j = (size_t) (1.55 / 0.02); j < (size_t) (2.6 / 0.02); ++j) { peakAfterOn = std::max (peakAfterOn, rms[j]); peakAfterOnLo = std::max (peakAfterOnLo, rmsLo[j]); }
            std::printf ("  %-10s %-6s ON 前 %+6.1f | 止めて 3 s %+6.1f / %3.0f s %+6.1f / 最後 %+6.1f dBFS（低域 3 s %+6.1f → 最後 %+6.1f）| OFF の膨らみ %+5.1f dB（低域 %+5.1f）| OFF 後 5 s %+6.1f%s\n",
                         id, si == 0 ? "初期値" : si == 1 ? "全部1" : si == 2 ? "全部0" : "ランダム", at (0.7), at (3.0), holdSec / 2 + 1.5, at (holdSec / 2 + 1.5), at (1.5 + holdSec - 0.6), atLo (3.0), atLo (1.5 + holdSec - 0.6),
                         db (postOff / std::max (1e-30, preOff)), db (postOffLo / std::max (1e-30, preOffLo)), at (1.5 + holdSec + 5.0), bad ? " ★NaN" : "");
            if (si >= 2 && nRand > 0 && si > 6) {}
            (void) peakAfterOn; (void) peakAfterOnLo;
        }
    }
}

// ============================================================================
// プチッ: 220 Hz のサインを通して、つまみを一気に / 0.3 秒で動かす。
//   C = test_inspect と同じ「2 階差 / (山 × ω²)」（ふだん 1〜3、10 以上はプチッ）
//   H = 6 kHz から上の山（4 段のハイパス）の、動かした所の最大 ÷ 動かす前後の最大（サインの響きに 6 kHz より上はほぼない → プチッで大きく出る）
struct HP4 { Biquad b[4]; void init (double sr, double f) { for (auto& x : b) { x.setHP (sr, f, 0.7071); x.reset(); } } float p (float x) { for (auto& y : b) x = y.process (x); return x; } };
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
struct ClickRes { float C = 0, H = 0, Hdb = -200; };
static ClickRes clickOne (const char* id, std::vector<float> kv, int k, float a, float b, bool drag, double sr = 48000, int B = 256)
{
    kv[(size_t) k] = a;
    auto m = make (id, sr, B, kv);
    Sig s = sine (sr, 2.4, 220, 0.25f);
    for (size_t i = 0; i < (size_t) (0.1 * sr); ++i) { s.L[i] *= (float) i / (float) (0.1 * sr); s.R[i] = s.L[i]; }
    const size_t t1 = (size_t) (1.2 * sr) / B * B, t2 = t1 + (drag ? (size_t) (0.3 * sr) / B * B : 0);
    for (size_t off = 0; off < s.size(); off += B)
    {
        if (! drag && off == t1) m->setKnob (k, b);
        if (drag && off >= t1 && off <= t2) m->setKnob (k, a + (b - a) * (float) (off - t1) / (float) std::max<size_t> (1, t2 - t1));
        m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off));
    }
    ClickRes r;
    r.C = cleanliness (s.L, t1, t2 + (size_t) (0.12 * sr), sr);
    HP4 h; h.init (sr, 6000);
    std::vector<float> y (s.size()); for (size_t i = 0; i < s.size(); ++i) y[i] = h.p (s.L[i]);
    float before = 0, at = 0, after = 0, sig = 0;
    for (size_t i = (size_t) (0.7 * sr); i < t1; ++i) before = std::max (before, std::abs (y[i]));
    for (size_t i = t1; i < t2 + (size_t) (0.15 * sr); ++i) at = std::max (at, std::abs (y[i]));
    for (size_t i = t2 + (size_t) (0.6 * sr); i < s.size(); ++i) after = std::max (after, std::abs (y[i]));
    for (size_t i = t1 - (size_t) (0.2 * sr); i < s.size(); ++i) sig = std::max (sig, std::abs (s.L[i]));
    r.H = at / std::max (1e-9f, std::max (before, after));
    r.Hdb = (float) db (at / std::max (1e-9f, sig));   // 出口の山に対するプチッの大きさ
    return r;
}
static void modeClick (const char* only, bool wetOnly)
{
    std::printf ("== つまみのプチッ（220 Hz −12 dBFS、%s）C = 2 階差の目安（10 以上でプチッ）、H = 6 kHz 以上の山の増え（倍）、Hd = 出口の山に対するその大きさ dB ==\n", wetOnly ? "ウェットだけ（MIX 100 %）" : "初期値の MIX");
    for (const char* id : kVerbs)
    {
        if (only && std::strcmp (only, id)) continue;
        const auto& inf = infoOf (id);
        auto base = defaults (id);
        if (wetOnly)
        {
            if (! std::strcmp (id, "shaperoom")) { base[0] = 1.0f; base[1] = 0.0f; }
            else if (! std::strcmp (id, "eightverb")) { base[6] = 1.0f; }
            else { int mi = knobIdx (id, "MIX"); if (mi >= 0) base[(size_t) mi] = 1.0f; }
        }
        std::printf ("  %s\n", id);
        for (int k = 0; k < (int) inf.knobs.size(); ++k)
        {
            const auto& ki = inf.knobs[(size_t) k];
            std::vector<std::pair<float, float>> mv;
            if (ki.steps > 1) { for (int i = 0; i < ki.steps; ++i) for (int j = 0; j < ki.steps; ++j) if (i != j) mv.push_back ({ (float) i / (ki.steps - 1), (float) j / (ki.steps - 1) }); }
            else mv = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.2f, 0.8f }, { 0.8f, 0.2f }, { ki.def, std::min (1.0f, ki.def + 0.1f) } };
            ClickRes wj, wd; std::string wm, wmd;
            for (auto x : mv)
            {
                if (wetOnly && (! std::strcmp (ki.name, "MIX") || ! std::strcmp (ki.name, "DRY/WET") || ! std::strcmp (ki.name, "WET") || ! std::strcmp (ki.name, "DRY"))) continue;
                auto j = clickOne (id, base, k, x.first, x.second, false);
                if (j.Hdb > wj.Hdb) { wj = j; char t[32]; std::snprintf (t, sizeof t, "%.2f→%.2f", x.first, x.second); wm = t; }
                if (ki.steps <= 1)
                {
                    auto d = clickOne (id, base, k, x.first, x.second, true);
                    if (d.Hdb > wd.Hdb) { wd = d; char t[32]; std::snprintf (t, sizeof t, "%.2f→%.2f", x.first, x.second); wmd = t; }
                }
            }
            if (wm.empty()) continue;
            const bool bad = wj.Hdb > -50 && wj.H > 4;
            const bool badD = ki.steps <= 1 && wd.Hdb > -50 && wd.H > 4;
            std::printf ("    %-10s 一気に C %6.1f H %7.1f Hd %+6.1f [%s]", ki.name, wj.C, wj.H, wj.Hdb, wm.c_str());
            if (ki.steps <= 1) std::printf (" | 0.3 秒 C %6.1f H %7.1f Hd %+6.1f [%s]", wd.C, wd.H, wd.Hdb, wmd.c_str());
            std::printf ("%s\n", (bad || badD) ? "  ★" : "");
        }
    }
}

// REVERSE ON のまま DECAY・SIZE を動かす、REVERSE / FREEZE の ON/OFF（音楽っぽい入力でも）
static void modeRev()
{
    std::printf ("== SHAPE ROOM の REVERSE ON のまま DECAY / SIZE / START / DEPTH を動かす（ウェットだけ）==\n");
    auto base = defaults ("shaperoom"); base[0] = 1.0f; base[1] = 0.0f; base[7] = 1.0f;
    for (const char* nm : { "DECAY", "SIZE", "START", "DEPTH", "PREDELAY", "LOW GAIN", "HIGH FREQ", "WIDTH" })
    {
        const int k = knobIdx ("shaperoom", nm);
        ClickRes wj, wd; std::string wm, wmd;
        for (auto x : std::vector<std::pair<float, float>> { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.2f, 0.8f }, { 0.6f, 0.7f } })
        {
            auto j = clickOne ("shaperoom", base, k, x.first, x.second, false);
            auto d = clickOne ("shaperoom", base, k, x.first, x.second, true);
            if (j.Hdb > wj.Hdb) { wj = j; char t[32]; std::snprintf (t, sizeof t, "%.2f→%.2f", x.first, x.second); wm = t; }
            if (d.Hdb > wd.Hdb) { wd = d; char t[32]; std::snprintf (t, sizeof t, "%.2f→%.2f", x.first, x.second); wmd = t; }
        }
        std::printf ("    %-10s 一気に C %6.1f H %7.1f Hd %+6.1f [%s] | 0.3 秒 C %6.1f H %7.1f Hd %+6.1f [%s]\n", nm, wj.C, wj.H, wj.Hdb, wm.c_str(), wd.C, wd.H, wd.Hdb, wmd.c_str());
    }
    // ゆっくり 3 秒かけて DECAY を動かす（つまみを手で回すくらい）
    for (const char* nm : { "DECAY", "SIZE" })
    {
        const int k = knobIdx ("shaperoom", nm);
        for (int rev = 0; rev < 2; ++rev)
        {
            auto kv = base; kv[7] = (float) rev; kv[(size_t) k] = 0.2f;
            auto m = make ("shaperoom", 48000, 256, kv);
            const double sr = 48000; Sig s = sine (sr, 6.0, 220, 0.25f);
            for (size_t i = 0; i < (size_t) (0.1 * sr); ++i) { s.L[i] *= (float) i / (float) (0.1 * sr); s.R[i] = s.L[i]; }
            const size_t t1 = (size_t) (2.0 * sr) / 256 * 256, t2 = (size_t) (5.0 * sr) / 256 * 256;
            for (size_t off = 0; off < s.size(); off += 256)
            {
                if (off >= t1 && off <= t2) m->setKnob (k, 0.2f + 0.6f * (float) (off - t1) / (float) (t2 - t1));
                m->process (s.L.data() + off, s.R.data() + off, 256);
            }
            HP4 h; h.init (sr, 6000); std::vector<float> y (s.size()); for (size_t i = 0; i < s.size(); ++i) y[i] = h.p (s.L[i]);
            float before = 0, at = 0, sig = 0;
            for (size_t i = (size_t) (1.2 * sr); i < t1; ++i) before = std::max (before, std::abs (y[i]));
            for (size_t i = t1; i < t2; ++i) at = std::max (at, std::abs (y[i]));
            for (size_t i = t1; i < t2; ++i) sig = std::max (sig, std::abs (s.L[i]));
            std::printf ("    %s を 3 秒かけて 0.2→0.8（REVERSE %s）: 6 kHz 以上の山 %.1f 倍、出口の山に対して %+.1f dB、C %.1f\n", nm, rev ? "ON " : "OFF", at / std::max (1e-9f, before), db (at / std::max (1e-9f, sig)), cleanliness (s.L, t1, t2, sr));
        }
    }
}

// ============================================================================
static double rt60 (const std::vector<double>& e2, double sr)
{
    // e2 = サンプルごとのエネルギー
    std::vector<double> sch (e2.size()); double acc = 0;
    for (size_t i = e2.size(); i-- > 0;) { acc += e2[i]; sch[i] = acc; }
    if (acc <= 0) return 0;
    long i5 = -1, i35 = -1;
    for (size_t i = 0; i < sch.size(); ++i) { const double d = 10 * std::log10 (sch[i] / sch[0] + 1e-300); if (i5 < 0 && d <= -5) i5 = (long) i; if (i35 < 0 && d <= -35) { i35 = (long) i; break; } }
    if (i5 < 0 || i35 < 0) return -1;
    return 2.0 * (double) (i35 - i5) / sr;
}
static void modeRates()
{
    std::printf ("== サンプルレートごとの残響時間（インパルス、ウェットだけ、T30×2）・山・NaN・10 秒後 ==\n");
    for (const char* id : kVerbs)
    {
        auto kv = defaults (id);
        if (! std::strcmp (id, "shaperoom")) { kv[0] = 1.0f; kv[1] = 0.0f; } else if (! std::strcmp (id, "eightverb")) kv[6] = 1.0f; else { int mi = knobIdx (id, "MIX"); if (mi >= 0) kv[(size_t) mi] = 1.0f; }
        std::printf ("  %-10s", id);
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto m = make (id, sr, 512, kv);
            Sig s = zeros (sr, 12.0); s.L[0] = s.R[0] = 1.0f;
            run (*m, s, 512);
            std::vector<double> e (s.size()); long bad = 0; float pk = 0;
            for (size_t i = 0; i < s.size(); ++i) { if (! std::isfinite (s.L[i])) ++bad; e[i] = (double) s.L[i] * s.L[i] + (double) s.R[i] * s.R[i]; pk = std::max (pk, std::abs (s.L[i])); }
            float t10 = 0; for (size_t i = (size_t) (10 * sr); i < s.size(); ++i) t10 = std::max (t10, std::abs (s.L[i]));
            double ein = 0; for (size_t i = 0; i < s.size(); ++i) ein += e[i];
            std::printf (" | %3.0fk T60 %5.2f s E %+5.1f dB 10s後 %+6.1f%s", sr / 1000, rt60 (e, sr), 10 * std::log10 (ein / 2 + 1e-30), db (t10), bad ? " NaN" : "");
        }
        std::printf ("\n");
    }
}

static void modeBlock()
{
    std::printf ("== ブロックの大きさ 1 / 7 / 64 / 2048 と 256 の差（つまみ止めたまま・途中で動かす）==\n");
    const double sr = 48000;
    for (const char* id : kVerbs)
    {
        std::mt19937 g (5);
        float worst = 0, worstMove = 0;
        for (int r = 0; r < 4; ++r)
        {
            auto kv = r == 0 ? defaults (id) : randomKnobs (id, g, true, true);
            auto mk = [&] (int B) { Sig s = noise (sr, 1.0, 0.25f); append (s, zeros (sr, 1.0)); auto m = make (id, sr, B, kv); run (*m, s, B); return s; };
            auto ref = mk (256);
            for (int B : { 1, 7, 64, 2048 }) { auto s = mk (B); for (size_t i = 0; i < s.size(); ++i) worst = std::max (worst, std::abs (s.L[i] - ref.L[i])); }
            // 途中でつまみを動かす: setKnob を同じサンプル位置（2048 の倍数）で呼ぶ
            auto mk2 = [&] (int B) {
                Sig s = noise (sr, 1.0, 0.25f); append (s, zeros (sr, 1.0)); auto m = make (id, sr, B, kv);
                std::mt19937 g2 (77); std::uniform_real_distribution<float> u (0, 1);
                for (size_t off = 0; off < s.size(); off += (size_t) B)
                {
                    if (off % 2048 == 0 && off > 0) { const int k = (int) (u (g2) * kv.size()) % (int) kv.size(); m->setKnob (k, u (g2)); }
                    m->process (s.L.data() + off, s.R.data() + off, (int) std::min ((size_t) B, s.size() - off));
                }
                return s; };
            auto ref2 = mk2 (256);
            for (int B : { 1, 64, 2048 }) { auto s = mk2 (B); for (size_t i = 0; i < s.size(); ++i) worstMove = std::max (worstMove, std::abs (s.L[i] - ref2.L[i])); }
        }
        std::printf ("  %-10s 止めたまま 差の最大 %.2e / 動かす %.2e\n", id, worst, worstMove);
    }
}

static void modeReset()
{
    std::printf ("== reset: 大きい音 → reset → 無音の最大、新品との差（48 kHz）・reset 1 回の時間（オーディオスレッドで呼ばれる: OFF→ON・ホストの reset）==\n");
    for (const char* id : kVerbs)
    {
        std::mt19937 g (9);
        float resid = 0, diff = 0;
        for (int r = 0; r < 5; ++r)
        {
            auto kv = r == 0 ? defaults (id) : randomKnobs (id, g, true, false);
            auto m = make (id, 48000, 256, kv);
            Sig a = noise (48000, 1.0, 1.0f); run (*m, a, 256);
            m->reset();
            Sig z = zeros (48000, 1.0); run (*m, z, 256);
            for (size_t i = 0; i < z.size(); ++i) resid = std::max (resid, std::max (std::abs (z.L[i]), std::abs (z.R[i])));
            m->reset();
            Sig p = noise (48000, 1.0, 0.25f, 3), q = p; run (*m, p, 256);
            auto f = make (id, 48000, 256, kv); run (*f, q, 256);
            for (size_t i = 0; i < p.size(); ++i) diff = std::max (diff, std::abs (p.L[i] - q.L[i]));
        }
        std::printf ("  %-10s 残り %.2e、新品との差 %.2e |", id, resid, diff);
        for (double sr : { 48000.0, 96000.0, 192000.0 })
        {
            auto m = make (id, sr, 256, defaults (id));
            double best = 1e9; for (int rep = 0; rep < 7; ++rep) { const double t0 = nowSec(); m->reset(); best = std::min (best, nowSec() - t0); }
            double tp = 1e9; for (int rep = 0; rep < 3; ++rep) { auto m2 = makeType (typeForId (id)); const double t0 = nowSec(); m2->prepare (sr, 256); tp = std::min (tp, nowSec() - t0); }
            std::printf (" %3.0fk reset %.2f ms（prepare %.1f ms）", sr / 1000, best * 1000, tp * 1000);
        }
        std::printf ("\n");
    }
}

static void setFZ()
{
#if defined(__aarch64__)
    uint64_t f; asm volatile ("mrs %0, fpcr" : "=r"(f)); f |= (1ull << 24); asm volatile ("msr fpcr, %0" :: "r"(f));
#endif
}
static void modeCpu (bool fz)
{
    if (fz) setFZ();
    std::printf ("== CPU（1 コアに対する %%、ブロック 256、5 回の最速。FTZ %s）==\n", fz ? "ON" : "OFF（デノーマルの確認）");
    for (double sr : { 48000.0, 96000.0, 192000.0 })
    {
        std::printf (" %.0f Hz:", sr);
        for (const char* id : kVerbs)
        {
            auto kv = defaults (id);
            auto runIt = [&] (const std::vector<float>& k) {
                double best = 1e9;
                for (int rep = 0; rep < 5; ++rep)
                {
                    auto m = make (id, sr, 256, k);
                    Sig s = noise (sr, 1.0, 0.1f); append (s, zeros (sr, 2.0));
                    const double t0 = nowSec(); run (*m, s, 256); best = std::min (best, nowSec() - t0);
                }
                return best / 3.0 * 100.0; };
            const double c0 = runIt (kv);
            double c1 = -1;
            if (! std::strcmp (id, "shaperoom")) { auto k2 = kv; k2[7] = 1.0f; c1 = runIt (k2); }
            std::printf (" %s %.2f%%", id, c0); if (c1 >= 0) std::printf ("（REV %.2f%%）", c1);
        }
        std::printf ("\n");
        // 8 段（全部 SHAPE ROOM の REVERSE）
        using Want = std::array<SlotKnobs, kSlots>;
        Want w {}; for (auto& s : w) { s.type = typeForId ("shaperoom"); s.on = true; auto d = defaults ("shaperoom"); for (int k = 0; k < kMaxKnobs; ++k) s.k[k] = k < (int) d.size() ? d[(size_t) k] : 0.5f; s.k[7] = 1.0f; }
        double best = 1e9;
        for (int rep = 0; rep < 2; ++rep)
        {
            Rack r; r.prepare (sr, 256, w);
            Sig s = noise (sr, 2.0, 0.05f);
            const double t0 = nowSec();
            for (size_t off = 0; off < s.size(); off += 256) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> (256, s.size() - off), w, 120.0);
            best = std::min (best, nowSec() - t0);
        }
        std::printf ("   8 段すべて SHAPE ROOM（REVERSE ON）: %.1f %%\n", best / 2.0 * 100);
    }
}

static void modeMem()
{
    std::printf ("== 1 個あたりのメモリ（prepare の式から、MB）==\n");
    auto pow2 = [] (size_t n) { size_t m = 1; while (m < n) m <<= 1; return m; };
    static const int kLen[8] { 1559, 1687, 1801, 1931, 2053, 2203, 2347, 2503 };
    static const int kAp[4] { 142, 107, 379, 277 };
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double sc = sr / 48000;
        size_t n = pow2 ((size_t) (3.05 * sr) + 8) + pow2 ((size_t) (0.9 * sr) + 8);
        for (int i = 0; i < 4; ++i) n += pow2 ((size_t) (kAp[i] * sc * 3.2) + 8);
        for (int i = 0; i < 8; ++i) n += pow2 ((size_t) ((float) (kLen[i] * sc) * 3.1f + 0.011 * sr) + 8);
        const size_t rv = pow2 ((size_t) (4.0 * sr) + 8);
        std::printf ("  %6.0f Hz: ふつう %.2f MB、SHAPE ROOM %.2f MB（8 段全部 SHAPE ROOM で %.1f MB）\n", sr, n * 4.0 / 1048576, (n + rv) * 4.0 / 1048576, 8 * (n + rv) * 4.0 / 1048576);
    }
}

// 初期値で挿したときの音量の変化（ギターふう・ノイズ）と +12 dBFS
static Sig pluckSeq (double sr, double secs, float peak)
{
    Sig s = zeros (sr, secs);
    const double f0s[4] { 110.0, 146.8, 196.0, 164.8 };
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / sr; const int k = (int) (t / 0.5) % 4; const double tt = std::fmod (t, 0.5);
        double v = 0; for (int h = 1; h <= 8; ++h) v += std::sin (2 * M_PI * f0s[k] * h * tt) / h;
        s.L[i] = s.R[i] = (float) (peak * 0.55 * v * std::exp (-tt * 4.0));
    }
    return s;
}
static void modeLevel()
{
    std::printf ("== 初期値で挿したときの音量（ギターふう −6 dBFS 4 秒、RMS の差 dB）・山、+12 dBFS 100 Hz の山、全部 1 / 0 の山（FREEZE は OFF）==\n");
    const double sr = 48000;
    for (const char* id : kVerbs)
    {
        auto d = defaults (id);
        Sig in = pluckSeq (sr, 4.0, 0.5f), s = in;
        auto m = make (id, sr, 256, d); run (*m, s, 256);
        double ei = 0, eo = 0; float pk = 0; for (size_t i = 0; i < s.size(); ++i) { ei += (double) in.L[i] * in.L[i]; eo += 0.5 * ((double) s.L[i] * s.L[i] + (double) s.R[i] * s.R[i]); pk = std::max (pk, std::max (std::abs (s.L[i]), std::abs (s.R[i]))); }
        Sig b = sine (sr, 2.0, 100, 4.0f); auto m2 = make (id, sr, 256, d); run (*m2, b, 256); float pkb = 0; for (size_t i = 0; i < b.size(); ++i) pkb = std::max (pkb, std::abs (b.L[i]));
        auto one = std::vector<float> (d.size(), 1.0f), zero = std::vector<float> (d.size(), 0.0f);
        if (freezeIdx (id) >= 0) { one[(size_t) freezeIdx (id)] = 0; zero[(size_t) freezeIdx (id)] = 0; }
        float pk1 = 0, pk0 = 0;
        { Sig x = pluckSeq (sr, 3.0, 0.5f); append (x, zeros (sr, 2.0)); auto mm = make (id, sr, 256, one); run (*mm, x, 256); for (size_t i = 0; i < x.size(); ++i) pk1 = std::max (pk1, std::abs (x.L[i])); }
        { Sig x = pluckSeq (sr, 3.0, 0.5f); append (x, zeros (sr, 2.0)); auto mm = make (id, sr, 256, zero); run (*mm, x, 256); for (size_t i = 0; i < x.size(); ++i) pk0 = std::max (pk0, std::abs (x.L[i])); }
        std::printf ("  %-10s 初期値: 音量 %+5.1f dB、山 %+5.1f dBFS | +12 dBFS 100 Hz → 山 %+5.1f dBFS | 全部 1: 山 %+5.1f / 全部 0: 山 %+5.1f dBFS（入口 −6 dBFS）\n", id, 10 * std::log10 (eo / ei), db (pk), db (pkb), db (pk1), db (pk0));
    }
}

// SPACE VERB の FEEDBACK（予備の遅れのくり返し）: PREDELAY 0 で 1 サンプルの輪になる → 低域の持ち上がり
static void modeFb()
{
    std::printf ("== SPACE VERB: FEEDBACK と PREDELAY（ウェットだけ・REVERB 0 %% = こだまだけ / 100 %%）の周波数ごとの大きさ（dB、FEEDBACK 0 との差）==\n");
    const double sr = 48000;
    for (float rev : { 0.0f, 1.0f })
        for (float pre : { 0.0f, 0.25f, 0.5f })
        {
            std::printf ("  REVERB %3.0f %% PREDELAY %.2f:", rev * 100, pre);
            for (float fb : { 0.5f, 0.8f, 1.0f })
            {
                std::printf ("  FB %.1f:", fb);
                for (double f : { 50.0, 200.0, 1000.0, 5000.0 })
                {
                    auto lvl = [&] (float fbv) {
                        auto kv = defaults ("spacevb"); kv[13] = 1.0f; kv[12] = rev; kv[2] = pre; kv[3] = fbv;
                        auto m = make ("spacevb", sr, 256, kv); Sig s = sine (sr, 6.0, f, 0.1f); run (*m, s, 256);
                        double e = 0; for (size_t i = (size_t) (4 * sr); i < s.size(); ++i) e += (double) s.L[i] * s.L[i]; return std::sqrt (e / (2 * sr)); };
                    std::printf (" %+5.1f", db (lvl (fb) / lvl (0.0f)));
                }
            }
            std::printf ("\n");
        }
    // 音楽ふうの入力（ギターふう）で FEEDBACK 0 → 1 の山（初期値の MIX）
    for (float pre : { 0.0f, 0.25f })
    {
        float pk[2] {}; double rms[2] {};
        for (int i = 0; i < 2; ++i)
        {
            auto kv = defaults ("spacevb"); kv[3] = (float) i; kv[2] = pre;
            Sig s = pluckSeq (sr, 4.0, 0.5f); auto m = make ("spacevb", sr, 256, kv); run (*m, s, 256);
            double e = 0; for (size_t j = 0; j < s.size(); ++j) { pk[i] = std::max (pk[i], std::abs (s.L[j])); e += (double) s.L[j] * s.L[j]; } rms[i] = std::sqrt (e / s.size());
        }
        std::printf ("  初期値の MIX・PREDELAY %.2f・ギターふう −6 dBFS: FEEDBACK 0 → 山 %+5.1f dBFS / RMS %+5.1f、FEEDBACK 1 → 山 %+5.1f dBFS / RMS %+5.1f\n", pre, db (pk[0]), db (rms[0]), db (pk[1]), db (rms[1]));
    }
}


static void modeDiff (const char* id, int k, float a, float b)
{
    auto d = defaults (id); auto kv = d; kv[(size_t) k] = a; auto pa = targets (id, kv); kv[(size_t) k] = b; auto pb = targets (id, kv);
    std::printf ("  %s knob %d (%s) %.3f -> %.3f\n", id, k, infoOf (id).knobs[(size_t) k].name, a, b);
    for (int j = 0; j < vpCount; ++j) if (std::abs (pa[(size_t) j] - pb[(size_t) j]) > 1e-6f) std::printf ("    %-8s %10.4f -> %10.4f\n", pName[j], pa[(size_t) j], pb[(size_t) j]);
}

static void modeClick2 (const char* id, int k, float a, float b, std::vector<std::pair<int,float>> extra)
{
    auto base = defaults (id);
    if (! std::strcmp (id, "shaperoom")) { base[0] = 1.0f; base[1] = 0.0f; } else if (! std::strcmp (id, "eightverb")) base[6] = 1.0f; else { int mi = knobIdx (id, "MIX"); if (mi >= 0) base[(size_t) mi] = 1.0f; }
    for (auto& e : extra) base[(size_t) e.first] = e.second;
    auto pa = targets (id, [&]{ auto x = base; x[(size_t) k] = a; return x; }());
    auto pb = targets (id, [&]{ auto x = base; x[(size_t) k] = b; return x; }());
    auto j = clickOne (id, base, k, a, b, false), d = clickOne (id, base, k, a, b, true);
    std::printf ("  %s %s %.2f→%.2f（ウェットだけ）HiCut %.3f→%.3f LoCut %.3f→%.3f Band %.3f→%.3f Size %.3f→%.3f ErSize %.1f→%.1f | 一気に C %.1f Hd %+.1f | 0.3 秒 C %.1f Hd %+.1f\n", id, infoOf (id).knobs[(size_t) k].name, a, b,
                 pa[vpHiCut], pb[vpHiCut], pa[vpLoCut], pb[vpLoCut], pa[vpBand], pb[vpBand], pa[vpSize], pb[vpSize], pa[vpErSize], pb[vpErSize], j.C, j.Hdb, d.C, d.Hdb);
}

static void modeFbPk()
{
    const double sr = 48000;
    std::printf ("== SPACE VERB の FEEDBACK（PREDELAY 0）: ギターふう −6 dBFS（110〜196 Hz）・ベースふう −6 dBFS（41 Hz）の出口の山 ==\n");
    for (float mix : { 0.195f, 0.5f, 1.0f })
        for (float fb : { 0.0f, 0.5f, 0.8f, 1.0f })
        {
            float pk[2] {};
            for (int src = 0; src < 2; ++src)
            {
                auto kv = defaults ("spacevb"); kv[13] = mix; kv[3] = fb;
                Sig s = src == 0 ? pluckSeq (sr, 4.0, 0.5f) : sine (sr, 4.0, 41.2, 0.5f);
                auto m = make ("spacevb", sr, 256, kv); run (*m, s, 256);
                for (size_t j = 0; j < s.size(); ++j) pk[src] = std::max (pk[src], std::max (std::abs (s.L[j]), std::abs (s.R[j])));
            }
            std::printf ("  MIX %3.0f %% FEEDBACK %3.0f %%: ギター 山 %+5.1f dBFS / ベース 山 %+5.1f dBFS\n", mix * 100, fb * 100, db (pk[0]), db (pk[1]));
        }
}

// どの入力（−12 dBFS のノイズ・ピンク寄り・ギター・ドラムふう）で、つまみ 1 本を動かすと音量がどれだけ変わるか（初期値の MIX）
static void modeKnobLevel ()
{
    const double sr = 48000;
    std::printf ("== つまみ 1 本を 0..1 で動かしたときの出口の RMS の幅（−12 dBFS ホワイトノイズ 2 秒、初期値の MIX、FREEZE 以外）。初期値との差の最大 / 最小 dB ==\n");
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        auto lvl = [&] (const std::vector<float>& kv) { Sig s = noise (sr, 2.0, 0.25f, 4); auto m = make (id, sr, 256, kv); run (*m, s, 256); double e = 0; for (size_t i = (size_t) (0.5 * sr); i < s.size(); ++i) e += 0.5 * ((double) s.L[i] * s.L[i] + (double) s.R[i] * s.R[i]); return std::sqrt (e / (1.5 * sr)); };
        const double l0 = lvl (d);
        std::printf ("  %-10s 初期値 %+5.1f dBFS（入口 RMS %+5.1f）:", id, db (l0), db (0.25 / std::sqrt (3.0)));
        for (size_t k = 0; k < d.size(); ++k)
        {
            if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue;
            double mx = -1e9, mn = 1e9; float vx = 0, vn = 0;
            const int N = inf.knobs[k].steps > 1 ? inf.knobs[k].steps : 5;
            for (int i = 0; i < N; ++i) { auto kv = d; kv[k] = (float) i / (N - 1); const double g = db (lvl (kv) / l0); if (g > mx) { mx = g; vx = kv[k]; } if (g < mn) { mn = g; vn = kv[k]; } }
            if (mx > 3.0 || mn < -12.0) std::printf (" %s %+.1f(%.2f)/%+.1f(%.2f)", inf.knobs[k].name, mx, vx, mn, vn);
        }
        std::printf ("\n");
    }
}

static void modeFuzzV (int nRand)
{
    std::printf ("== 響き 9 部品の fuzz（初期値・全部 0・全部 1・全部 0.5・ランダム %d 通り × 入力 6 種 × レート・ブロック、FREEZE も含む、入力のあと 3 秒）==\n", nRand);
    static const char* inName[] = { "+24dBFS noise", "+12dBFS 100Hz", "DC 0.5", "impulse", "silence", "tiny 1e-30" };
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 }; const int blocks[] = { 1, 7, 64, 256, 2048 };
    for (const char* id : kVerbs)
    {
        std::mt19937 g (2024);
        auto d = defaults (id);
        std::vector<std::vector<float>> sets { d, std::vector<float> (d.size(), 0.0f), std::vector<float> (d.size(), 1.0f), std::vector<float> (d.size(), 0.5f) };
        for (int r = 0; r < nRand; ++r) sets.push_back (randomKnobs (id, g, r % 3 != 0, false));
        float pk = 0, pkNoFb = 0; std::string at, atNoFb; long bad = 0; int run_ = 0; float silentOut = 0;
        for (auto& kv : sets)
            for (int in = 0; in < 6; ++in, ++run_)
            {
                const double sr = rates[run_ % 4]; const int B = blocks[run_ % 5];
                auto m = make (id, sr, std::max (16, B), kv);
                Sig s;
                switch (in) { case 0: s = noise (sr, 1.0, 16.0f, 5); break; case 1: s = sine (sr, 1.0, 100, 4.0f); break; case 2: s = zeros (sr, 1.0); for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = 0.5f; break; case 3: s = zeros (sr, 0.3); s.L[0] = s.R[0] = 1; break; case 4: s = zeros (sr, 0.5); break; default: s = noise (sr, 1.0, 1e-30f, 6); break; }
                append (s, zeros (sr, 3.0));
                run (*m, s, B);
                for (size_t i = 0; i < s.size(); ++i)
                {
                    const float a = std::max (std::abs (s.L[i]), std::abs (s.R[i]));
                    if (! std::isfinite (a)) { ++bad; continue; }
                    if (a > pk) { pk = a; at = std::string (inName[in]) + " " + std::to_string ((int) sr) + " B" + std::to_string (B) + " " + kvStr (kv); }
                    const bool fbHigh = ! std::strcmp (id, "spacevb") && kv[3] > 0.5f;
                    const bool ioHigh = ! std::strcmp (id, "digihall") && (kv[11] > 0.5f || kv[12] > 0.5f);
                    if (! fbHigh && ! ioHigh && in != 2 && a > pkNoFb) { pkNoFb = a; atNoFb = std::string (inName[in]) + " " + kvStr (kv); }
                    if (in == 4 || in == 5) silentOut = std::max (silentOut, a);
                }
            }
        std::printf ("  %-10s 山 %+6.1f dBFS [%s]\n", id, db (pk), at.c_str());
        std::printf ("  %-10s （SPACE VERB の FEEDBACK > 50 %%・DIGI HALL の INPUT/OUTPUT > 0 dB・DC を除くと %+6.1f dBFS [%s]）NaN %ld、無音・1e-30 の入力での出口の最大 %.1e\n", "", db (pkNoFb), atNoFb.c_str(), bad, silentOut);
    }
}

static void modeLv (const char* id, std::vector<std::pair<int,float>> ch)
{
    const double sr = 48000;
    auto kv = defaults (id); for (auto& c : ch) kv[(size_t) c.first] = c.second;
    Sig in = pluckSeq (sr, 4.0, 0.5f), s = in; auto m = make (id, sr, 256, kv); run (*m, s, 256);
    Sig n = noise (sr, 3.0, 0.25f, 9), sn = n; auto m2 = make (id, sr, 256, kv); run (*m2, sn, 256);
    double ei = 0, eo = 0, eni = 0, eno = 0; float pk = 0;
    for (size_t i = 0; i < s.size(); ++i) { ei += (double) in.L[i] * in.L[i]; eo += 0.5 * ((double) s.L[i] * s.L[i] + (double) s.R[i] * s.R[i]); pk = std::max (pk, std::abs (s.L[i])); }
    for (size_t i = (size_t) sr; i < sn.size(); ++i) { eni += 0.5 * ((double) n.L[i] * n.L[i] + (double) n.R[i] * n.R[i]); eno += 0.5 * ((double) sn.L[i] * sn.L[i] + (double) sn.R[i] * sn.R[i]); }
    auto p = targets (id, kv);
    std::printf ("  %s %s: ギターふう −6 dBFS → 音量 %+5.1f dB・山 %+5.1f dBFS / ノイズ → 音量 %+5.1f dB | Wet %.1f dB Dry %.2f Echo %.2f Er %.2f Late %.2f T60 %.2f s\n", id, kvStr (kv).c_str(), 10 * std::log10 (eo / ei), db (pk), 10 * std::log10 (eno / eni), p[vpWet], p[vpDry], p[vpEcho], p[vpEr], p[vpLate], std::pow (10.0, p[vpT60]));
}
// 格子で全部の組み合わせ（1 周の大きさに効くつまみだけ）
static void modeGrid (int nCont)
{
    std::printf ("== 1 周の大きさの格子探し（T60・LowR・HighR・XLo・XHi・Size に効くつまみだけ、連続は %d 点、選ぶつまみは全部）44.1 / 48 / 96 kHz ==\n", nCont);
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        auto m = make (id, 48000, 256, d);
        auto* v = dynamic_cast<VerbEngine*> (m.get());
        auto readP = [&] (const std::vector<float>& kv) { for (size_t k = 0; k < kv.size(); ++k) m->setKnob ((int) k, kv[k]); std::vector<float> p (vpCount); for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j); return p; };
        // 効くつまみを探す
        const int loopP[] = { vpT60, vpLowR, vpHighR, vpXLo, vpXHi, vpSize };
        std::vector<int> ks;
        for (size_t k = 0; k < d.size(); ++k)
        {
            if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue;
            auto p0 = readP (d); bool eff = false;
            for (float x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) { auto kv = d; kv[k] = x; auto p = readP (kv); for (int j : loopP) if (std::abs (p[(size_t) j] - p0[(size_t) j]) > 1e-5f) eff = true; }
            if (eff) ks.push_back ((int) k);
        }
        std::vector<std::vector<float>> vals;
        size_t total = 1;
        for (int k : ks)
        {
            std::vector<float> vv; const int st = inf.knobs[(size_t) k].steps;
            if (st > 1) for (int i = 0; i < st; ++i) vv.push_back ((float) i / (st - 1)); else for (int i = 0; i < nCont; ++i) vv.push_back ((float) i / (nCont - 1));
            total *= vv.size(); vals.push_back (vv);
        }
        double worst[3] {}; std::string at[3]; size_t over[3] {}; int bestChanged = 99; std::string minimal; double minimalG = 0;
        const double rates[3] { 44100.0, 48000.0, 96000.0 };
        std::vector<size_t> idx (ks.size(), 0);
        for (size_t c = 0; c < total; ++c)
        {
            auto kv = d; size_t r = c;
            for (size_t i = 0; i < ks.size(); ++i) { idx[i] = r % vals[i].size(); r /= vals[i].size(); kv[(size_t) ks[i]] = vals[i][idx[i]]; }
            auto p = readP (kv);
            int changed = 0; for (size_t k = 0; k < d.size(); ++k) if (std::abs (kv[k] - d[k]) > 1e-4f) ++changed;
            for (int ri = 0; ri < 3; ++ri)
            {
                double f; const double g = loopGain (p, rates[ri], 0.0f, &f);
                if (g > 1.0 + 1e-6) { ++over[ri]; if (changed < bestChanged || (changed == bestChanged && g > minimalG)) { bestChanged = changed; minimal = kvStr (kv) + " " + std::to_string ((int) rates[ri]) + " Hz → " + std::to_string (g) + " @" + std::to_string ((int) f) + " Hz"; minimalG = g; } }
                if (g > worst[ri]) { worst[ri] = g; char b[64]; std::snprintf (b, sizeof b, " @%.0f Hz", f); at[ri] = kvStr (kv) + b; }
            }
        }
        std::printf ("  %-10s 効くつまみ:", id); for (int k : ks) std::printf (" %s", inf.knobs[(size_t) k].name); std::printf ("（%zu 通り）\n", total);
        for (int ri = 0; ri < 3; ++ri) std::printf ("     %5.1fk 最大 %.5f%s  1 超え %zu 通り（%.2f %%）[%s]\n", rates[ri] / 1000, worst[ri], worst[ri] > 1 + 1e-6 ? " ★" : "", over[ri], 100.0 * over[ri] / total, at[ri].c_str());
        if (! minimal.empty()) std::printf ("     初期値から変えるつまみがいちばん少ない発振: %d 本 %s\n", bestChanged, minimal.c_str());
    }
}

// 初期値から 1〜3 本だけ動かした組で発振するもの（DIGI HALL など）
static double loopGainFast (const std::vector<float>& p, double sr)
{
    static const int kLen[8] { 1559, 1687, 1801, 1931, 2053, 2203, 2347, 2503 };
    const float fsr = (float) sr;
    const float Tm = std::pow (10.0f, p[vpT60]);
    const float Tl = Tm * std::pow (2.0f, p[vpLowR]), Th = Tm * std::pow (2.0f, p[vpHighR]);
    const float Klo = 1.0f / std::tan ((float) M_PI * std::min (std::pow (10.0f, p[vpXLo]), 0.45f * fsr) / fsr);
    const float Khi = 1.0f / std::tan ((float) M_PI * std::min (std::pow (10.0f, p[vpXHi]), 0.45f * fsr) / fsr);
    double worst = 0;
    for (int i = 0; i < 8; i += 7)   // いちばん短い線と長い線
    {
        const float dsec = (float) (kLen[i] * sr / 48000.0) * p[vpSize] / fsr;
        float gm = std::pow (10.0f, -3.0f * dsec / Tm);
        float al = std::pow (10.0f, -3.0f * dsec * (1.0f / Tl - 1.0f / Tm));
        float bh = std::pow (10.0f, -3.0f * dsec * (1.0f / Th - 1.0f / Tm));
        al = std::min (al, 1.0f / gm); bh = std::min (bh, 1.0f / gm);
        const Sh lo = shelf (Klo, al, 1.0f), hi = shelf (Khi, 1.0f, bh);
        for (int fi = 0; fi <= 120; ++fi)
        {
            const double f = 100.0 * std::pow (100.0, fi / 120.0);   // 100 Hz〜10 kHz
            const double w = 2 * M_PI * f / sr;
            worst = std::max (worst, gm * shMag (lo, w) * shMag (hi, w));
        }
    }
    return worst;
}
static void modeFew (const char* id, int maxK)
{
    const auto& inf = infoOf (id);
    auto d = defaults (id);
    auto m = make (id, 48000, 256, d);
    auto* v = dynamic_cast<VerbEngine*> (m.get());
    auto readP = [&] (const std::vector<float>& kv) { for (size_t k = 0; k < kv.size(); ++k) m->setKnob ((int) k, kv[k]); std::vector<float> p (vpCount); for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j); return p; };
    std::printf ("== %s: 初期値から %d 本まで動かして発振する組（1 周の大きさ > 1）==\n", id, maxK);
    const int K = (int) d.size();
    auto valsOf = [&] (int k, int n) { std::vector<float> vv; const int st = inf.knobs[(size_t) k].steps; if (st > 1) for (int i = 0; i < st; ++i) vv.push_back ((float) i / (st - 1)); else for (int i = 0; i < n; ++i) vv.push_back ((float) i / (n - 1)); return vv; };
    // 1 本
    for (int a = 0; a < K; ++a) for (float x : valsOf (a, 41)) { auto kv = d; kv[(size_t) a] = x; const double g = loopGainFast (readP (kv), 48000); if (g > 1.0) std::printf ("  1 本: %s=%.3f → %.4f\n", inf.knobs[(size_t) a].name, x, g); }
    // 2 本: いちばん大きい値と、発振する範囲の大まかな割合
    for (int a = 0; a < K; ++a) for (int b = a + 1; b < K; ++b)
    {
        int n = 0, tot = 0; double best = 0; float bx = 0, by = 0;
        for (float x : valsOf (a, 21)) for (float y : valsOf (b, 21)) { auto kv = d; kv[(size_t) a] = x; kv[(size_t) b] = y; const double g = loopGainFast (readP (kv), 48000); ++tot; if (g > 1.0) ++n; if (g > best) { best = g; bx = x; by = y; } }
        if (n) std::printf ("  2 本: %s × %s で %d / %d 通り発振（最大 %.4f: %.2f / %.2f）\n", inf.knobs[(size_t) a].name, inf.knobs[(size_t) b].name, n, tot, best, bx, by);
    }
    if (maxK >= 3)
        for (int a = 0; a < K; ++a) for (int b = a + 1; b < K; ++b) for (int c = b + 1; c < K; ++c)
        {
            int n = 0, tot = 0; double best = 0; float bx = 0, by = 0, bz = 0;
            for (float x : valsOf (a, 11)) for (float y : valsOf (b, 11)) for (float z : valsOf (c, 11)) { auto kv = d; kv[(size_t) a] = x; kv[(size_t) b] = y; kv[(size_t) c] = z; const double g = loopGainFast (readP (kv), 48000); ++tot; if (g > 1.0) ++n; if (g > best) { best = g; bx = x; by = y; bz = z; } }
            if (n) std::printf ("  3 本: %s × %s × %s で %d / %d 通り（最大 %.4f: %.2f / %.2f / %.2f）\n", inf.knobs[(size_t) a].name, inf.knobs[(size_t) b].name, inf.knobs[(size_t) c].name, n, tot, best, bx, by, bz);
        }
}

// FREEZE の ON / OFF の瞬間（指定の組）: 低域（150 Hz 以下）の 20 ms ごとの大きさの、切り替え前との差の最大
static void modeFreeze1 (const char* id, std::vector<float> kv)
{
    const int fz = freezeIdx (id); const double sr = 48000; const int B = 256;
    kv[(size_t) fz] = 0.0f;
    auto p = targets (id, kv);
    std::printf ("  %s %s T60 %.2f s LowR %.2f HighR %.2f / FREEZE の途中の 1 周の最大 %.4f\n", id, kvStr (kv).c_str(), std::pow (10.0, p[vpT60]), p[vpLowR], p[vpHighR], [&]{ double w = 0; for (int i = 1; i < 20; ++i) w = std::max (w, loopGain (p, sr, i / 20.0f)); return w; }());
    for (int dir = 0; dir < 2; ++dir)
    {
        auto m = make (id, sr, B, kv);
        if (dir == 1) m->setKnob (fz, 1.0f);   // OFF にする向き: 先に ON で鳴らしておく…ではなく、鳴らしてから ON → 3 秒後に OFF
        const size_t tOn = (size_t) (0.6 * sr) / B * B, tOff = (size_t) (4.0 * sr) / B * B, tEnd = (size_t) (6.0 * sr);
        std::vector<float> L (B), R (B); std::mt19937 g (3); std::uniform_real_distribution<float> u (-1, 1);
        const float a = (float) std::exp (-2 * M_PI * 150.0 / sr); float lpL = 0;
        std::vector<double> lo; double e = 0; size_t c = 0; const size_t w = (size_t) (0.02 * sr);
        m->setKnob (fz, 0.0f);
        for (size_t off = 0; off < tEnd; off += B)
        {
            if (off == tOn) m->setKnob (fz, 1.0f);
            if (off == tOff && dir == 1) m->setKnob (fz, 0.0f);
            for (int i = 0; i < B; ++i) { const bool on = off + (size_t) i < (size_t) (0.5 * sr); L[(size_t) i] = on ? 0.25f * u (g) : 0.0f; R[(size_t) i] = on ? 0.25f * u (g) : 0.0f; }
            m->process (L.data(), R.data(), B);
            for (int i = 0; i < B; ++i) { lpL = L[(size_t) i] + (lpL - L[(size_t) i]) * a; e += (double) lpL * lpL; if (++c == w) { lo.push_back (std::sqrt (e / w)); e = 0; c = 0; } }
        }
        const size_t i0 = (dir ? tOff : tOn) / w;
        double before = 0; for (size_t j = i0 - 5; j < i0; ++j) before = std::max (before, lo[j]);
        double after = 0; for (size_t j = i0; j < std::min (lo.size(), i0 + 40); ++j) after = std::max (after, lo[j]);
        double later = lo[std::min (lo.size() - 1, i0 + 60)];
        std::printf ("     %s: 低域の切り替え直前 %+6.1f dBFS → 直後 0.8 秒の最大 %+6.1f dBFS（%+5.1f dB）→ 1.2 秒後 %+6.1f\n", dir ? "FREEZE OFF" : "FREEZE ON（入力が止まった後）", db (before), db (after), db (after / before), db (later));
    }
}

static void modeTransGrid()
{
    std::printf ("== FREEZE のある 4 部品: FREEZE を入れる / 切る途中の 1 周の大きさの最大（格子、連続 7 点）==\n");
    for (const char* id : { "spacevb", "clubverb", "frostverb", "eightverb" })
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        auto m = make (id, 48000, 256, d);
        auto* v = dynamic_cast<VerbEngine*> (m.get());
        auto readP = [&] (const std::vector<float>& kv) { for (size_t k = 0; k < kv.size(); ++k) m->setKnob ((int) k, kv[k]); std::vector<float> p (vpCount); for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j); return p; };
        const int loopP[] = { vpT60, vpLowR, vpHighR, vpXLo, vpXHi, vpSize };
        std::vector<int> ks;
        for (size_t k = 0; k < d.size(); ++k)
        {
            if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue;
            auto p0 = readP (d); bool eff = false;
            for (float x : { 0.0f, 0.5f, 1.0f }) { auto kv = d; kv[k] = x; auto p = readP (kv); for (int j : loopP) if (std::abs (p[(size_t) j] - p0[(size_t) j]) > 1e-5f) eff = true; }
            if (eff) ks.push_back ((int) k);
        }
        std::vector<std::vector<float>> vals; size_t total = 1;
        for (int k : ks) { std::vector<float> vv; const int st = inf.knobs[(size_t) k].steps; if (st > 1) for (int i = 0; i < st; ++i) vv.push_back ((float) i / (st - 1)); else for (int i = 0; i < 7; ++i) vv.push_back ((float) i / 6); total *= vv.size(); vals.push_back (vv); }
        double worst = 0; std::string at; float maxAl = 0, maxBh = 0;
        for (size_t c = 0; c < total; ++c)
        {
            auto kv = d; size_t r = c;
            for (size_t i = 0; i < ks.size(); ++i) { kv[(size_t) ks[i]] = vals[i][r % vals[i].size()]; r /= vals[i].size(); }
            auto p = readP (kv);
            for (int fi = 1; fi < 20; ++fi) { double f; const double g = loopGain (p, 48000, fi / 20.0f, &f); if (g > worst) { worst = g; at = kvStr (kv); } }
            const float Tm = std::pow (10.0f, p[vpT60]); const float dsec = 2503.0f / 48000.0f * p[vpSize];
            maxAl = std::max (maxAl, std::pow (10.0f, -3.0f * dsec * (1.0f / (Tm * std::pow (2.0f, p[vpLowR])) - 1.0f / Tm)));
            maxBh = std::max (maxBh, std::pow (10.0f, -3.0f * dsec * (1.0f / (Tm * std::pow (2.0f, p[vpHighR])) - 1.0f / Tm)));
        }
        std::printf ("  %-10s %zu 通り: 途中の最大 %.4f [%s]、低域の棚の最大 %.2f 倍・高域 %.2f 倍\n", id, total, worst, at.c_str(), maxAl, maxBh);
    }
}

static void modeMap (const char* id, int ka, int kb)
{
    auto d = defaults (id); const auto& inf = infoOf (id);
    auto m = make (id, 48000, 256, d); auto* v = dynamic_cast<VerbEngine*> (m.get());
    auto readP = [&] (const std::vector<float>& kv) { for (size_t k = 0; k < kv.size(); ++k) m->setKnob ((int) k, kv[k]); std::vector<float> p (vpCount); for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j); return p; };
    std::printf ("== %s: %s（縦）× %s（横）の 1 周の最大（ほかは初期値、48 kHz）。* = 1 を超える（発振）==\n        ", id, inf.knobs[(size_t) ka].name, inf.knobs[(size_t) kb].name);
    for (int j = 0; j <= 10; ++j) std::printf ("  %4.1f  ", j / 10.0); std::printf ("\n");
    for (int i = 10; i >= 0; --i)
    {
        std::printf ("  %4.1f ", i / 10.0);
        for (int j = 0; j <= 10; ++j) { auto kv = d; kv[(size_t) ka] = i / 10.0f; kv[(size_t) kb] = j / 10.0f; const double g = loopGain (readP (kv), 48000, 0.0f); std::printf (" %6.4f%s", g, g > 1.0 ? "*" : " "); }
        std::printf ("\n");
    }
}


// 発振の確かめ（本物を鳴らす）: 式で 1 周 > 0.995 の組を集め、大きい順に最大 40 組 + ランダム 20 組を 25 秒鳴らす
static void modeStab()
{
    std::printf ("== 発振の確かめ: 1 周の大きさ（式）> 0.995 の組を本物のエンジンで 25 秒（−12 dBFS ノイズ 0.5 秒のあと無音）。15 秒目以降に 5 秒前より大きくなったら ★ ==\n");
    for (const char* id : kVerbs)
    {
        const auto& inf = infoOf (id);
        auto d = defaults (id);
        auto m = make (id, 48000, 256, d); auto* v = dynamic_cast<VerbEngine*> (m.get());
        auto readP = [&] (const std::vector<float>& kv) { for (size_t k = 0; k < kv.size(); ++k) m->setKnob ((int) k, kv[k]); std::vector<float> p (vpCount); for (int j = 0; j < vpCount; ++j) p[(size_t) j] = v->param (j); return p; };
        const int loopP[] = { vpT60, vpLowR, vpHighR, vpXLo, vpXHi, vpSize };
        std::vector<int> ks;
        for (size_t k = 0; k < d.size(); ++k)
        {
            if (! std::strcmp (inf.knobs[k].name, "FREEZE")) continue;
            auto p0 = readP (d); bool eff = false;
            for (float x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) { auto kv = d; kv[k] = x; auto p = readP (kv); for (int j : loopP) if (std::abs (p[(size_t) j] - p0[(size_t) j]) > 1e-5f) eff = true; }
            if (eff) ks.push_back ((int) k);
        }
        std::vector<std::vector<float>> vals; size_t total = 1;
        for (int k : ks) { std::vector<float> vv; const int st = inf.knobs[(size_t) k].steps; if (st > 1) for (int i = 0; i < st; ++i) vv.push_back ((float) i / (st - 1)); else { for (int i = 0; i < 5; ++i) vv.push_back ((float) i / 4); vv.push_back (inf.knobs[(size_t) k].def); } total *= vv.size(); vals.push_back (vv); }
        std::vector<std::pair<double, std::vector<float>>> cand;
        for (size_t c = 0; c < total; ++c)
        {
            auto kv = d; size_t r = c;
            for (size_t i = 0; i < ks.size(); ++i) { kv[(size_t) ks[i]] = vals[i][r % vals[i].size()]; r /= vals[i].size(); }
            const double g = loopGainFast (readP (kv), 48000);
            if (g > 0.995) cand.push_back ({ g, kv });
        }
        std::sort (cand.begin(), cand.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::vector<std::vector<float>> runSet;
        for (size_t i = 0; i < cand.size() && i < 40; ++i) runSet.push_back (cand[i].second);
        std::mt19937 g (5); for (int i = 0; i < 20 && cand.size() > 40; ++i) runSet.push_back (cand[40 + g() % (cand.size() - 40)].second);
        int grow = 0; double worst = -999; std::string at;
        for (auto& kv : runSet)
        {
            if (freezeIdx (id) >= 0) kv[(size_t) freezeIdx (id)] = 0.0f;
            auto c = decayCurve (id, kv, 48000, 0.5, 25.0, 0.25f);
            double gr = -999; for (size_t i = 15; i < c.size(); ++i) gr = std::max (gr, db (c[i]) - db (c[i - 5]));
            if (gr > 0.5) ++grow;
            if (gr > worst) { worst = gr; at = kvStr (kv); }
        }
        std::printf ("  %-10s %zu 通り中 式で > 0.995 が %zu 通り（最大 %.4f）。鳴らした %zu 組のうち増えた %d 組%s（5 秒での増えの最大 %+.1f dB %s）\n", id, total, cand.size(), cand.empty() ? 0.0 : cand[0].first, runSet.size(), grow, grow ? " ★発振" : "", worst, at.c_str());
    }
}
int main (int argc, char** argv)
{
    const std::string m = argc > 1 ? argv[1] : "ranges";
    if (m == "ranges") modeRanges();
    else if (m == "loop") modeLoop (argc > 2 ? std::atoi (argv[2]) : 300);
    else if (m == "decay") modeDecay (argc > 2 ? std::atoi (argv[2]) : 12, argc > 3 ? std::atof (argv[3]) : 40.0);
    else if (m == "decay1") { std::vector<float> kv; for (int i = 3; i < argc - 1; ++i) kv.push_back ((float) std::atof (argv[i])); modeDecay1 (argv[2], kv, std::atof (argv[argc - 1])); }
    else if (m == "freeze") modeFreeze (argc > 2 ? std::atof (argv[2]) : 60.0, argc > 3 ? std::atoi (argv[3]) : 4);
    else if (m == "click") modeClick (argc > 2 && std::strcmp (argv[2], "all") ? argv[2] : nullptr, argc > 3 && ! std::strcmp (argv[3], "wet"));
    else if (m == "rev") modeRev();
    else if (m == "rates") modeRates();
    else if (m == "block") modeBlock();
    else if (m == "reset") modeReset();
    else if (m == "cpu") modeCpu (true);
    else if (m == "cpunofz") modeCpu (false);
    else if (m == "mem") modeMem();
    else if (m == "level") modeLevel();
    else if (m == "fb") modeFb();
    else if (m == "diff") modeDiff (argv[2], std::atoi (argv[3]), (float) std::atof (argv[4]), (float) std::atof (argv[5]));
    else if (m == "click2") { std::vector<std::pair<int,float>> ex; for (int i = 6; i + 1 < argc; i += 2) ex.push_back ({ std::atoi (argv[i]), (float) std::atof (argv[i + 1]) }); modeClick2 (argv[2], std::atoi (argv[3]), (float) std::atof (argv[4]), (float) std::atof (argv[5]), ex); }
    else if (m == "few") modeFew (argv[2], argc > 3 ? std::atoi (argv[3]) : 2);
    else if (m == "freeze1") { std::vector<float> kv; for (int i = 3; i < argc; ++i) kv.push_back ((float) std::atof (argv[i])); modeFreeze1 (argv[2], kv); }
    else if (m == "fbpk") modeFbPk();
    else if (m == "knoblevel") modeKnobLevel();
    else if (m == "fuzzv") modeFuzzV (argc > 2 ? std::atoi (argv[2]) : 20);
    else if (m == "lv") { std::vector<std::pair<int,float>> ch; for (int i = 3; i + 1 < argc; i += 2) ch.push_back ({ std::atoi (argv[i]), (float) std::atof (argv[i + 1]) }); modeLv (argv[2], ch); }
    else if (m == "transgrid") modeTransGrid();
    else if (m == "map") modeMap (argv[2], std::atoi (argv[3]), std::atoi (argv[4]));
    else if (m == "stab") modeStab();
    else if (m == "grid") modeGrid (argc > 2 ? std::atoi (argv[2]) : 5);
    return 0;
}
