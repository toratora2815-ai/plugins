// test_jump.cpp — AIO Effecter の DSP 検査 4 回目（DSP 検査係、2026-11-19）。JUCE なし・ビルドには入らない
//   つまみが再生中に一気に変わったとき（プリセット・UNDO・オートメーションの段）の音量のふくらみと、ラックの prime の後戻り
//   clang++ -std=c++17 -O2 -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_jump.cpp -o build/aiofx/tj
//   ./tj <mode>
//     dirt  … TUBE PUSH の DIRT ON↔OFF: 5 ms rms の、前後の落ち着いた値からのはみ出し（上 = ふくらみ。0.5 dB より上に ★）
//     peak  … 一気に動かしたときの出口のサンプルの山（dBFS）と、+1 dB より上にいた長さ
//     sw    … DynEngine の 9 部品 × 全部のつまみ × 一気の動き（840 通り）: 部品ごとのいちばん大きいふくらみ・谷、1 dB を超えるふくらみの数
//     fault … 遅れのある「わざと NaN を出す部品」でラックの立て直し（理想 = 入口を遅らせたもの、との差）
//     swap  … 再生中の選び直し（ブロック 1〜2048）、20 Hz の service での続けての選び直し、prime の途中の prepare
//   test_inspect.cpp の道具（Sig・sine・cleanliness・setSlot など）をそのまま使う（main だけ名前を変えて取り込む）。
//   ラックの中身（slots）を見るため、Rack.h だけ private を public にして読み込む（このテストの中だけ）
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
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#define private public
#include "fx/Registry.h"
#include "fx/Rack.h"
#undef private
#define main ti_main
#include "test_inspect.cpp"
#undef main

// ---------------------------------------------------------------------------
static void runJump (const char* id, int k, float a, float b, float amp, Sig& s, size_t& t1)
{
    const double sr = 48000; const int B = 256;
    auto kv = defaults (id); kv[(size_t) k] = a;
    auto m = makeSnapped (id, sr, B, kv);
    s = sine (sr, 3.0, 220, amp);
    t1 = (size_t) (1.0 * sr) / B * B;
    for (size_t off = 0; off < s.size(); off += B) { if (off == t1) m->setKnob (k, b); m->process (s.L.data() + off, s.R.data() + off, B); }
}
// 前（切り替え前 0.2 秒）と後（2.6〜2.9 秒）の rms、途中の 5 ms rms の最大・最小
struct Swell { double before, after, over, under; };
static Swell swellOf (const Sig& s, size_t t1)
{
    const double sr = 48000;
    auto rmsDb = [&] (size_t a, size_t b) { return db (stats (s, a, b).rms); };
    Swell w; w.before = rmsDb (t1 - (size_t) (0.2 * sr), t1); w.after = rmsDb ((size_t) (2.6 * sr), (size_t) (2.9 * sr));
    double hi = -300, lo = 300;
    for (size_t a = t1; a + 240 < (size_t) (2.6 * sr); a += 240) { const double v = rmsDb (a, a + 240); hi = std::max (hi, v); lo = std::min (lo, v); }
    w.over = hi - std::max (w.before, w.after); w.under = std::min (w.before, w.after) - lo;
    return w;
}

static void tjDirt()
{
    std::printf ("== TUBE PUSH の DIRT を再生中に切り替え（220 Hz）。5 ms rms の、前後の落ち着いた値からのはみ出し ==\n");
    for (float amp : { 0.1f, 0.5f })
        for (int dir : { 0, 1 })
        {
            Sig s; size_t t1; runJump ("tubepush", 3, dir == 0 ? 1.0f : 0.0f, dir == 0 ? 0.0f : 1.0f, amp, s, t1);
            const Swell w = swellOf (s, t1);
            std::printf ("  入口 %+4.0f dBFS DIRT %s: 前 %+6.2f → 後 %+6.2f dBFS、上へはみ出し %+5.2f dB、下へ %+5.2f dB %s\n",
                         db (amp), dir == 0 ? "ON→OFF" : "OFF→ON", w.before, w.after, w.over, w.under, w.over > 0.5 ? "★" : "");
        }
}

static void tjPeak()
{
    const double sr = 48000;
    std::printf ("== 再生中につまみが一気に変わったときの出口のサンプルの山 ==\n");
    struct C { const char* id; int k; float a, b; const char* name; };
    for (C c : { C { "tubepush", 0, 0.433333f, 0.0f, "TUBE PUSH INPUT 初期値→0" }, C { "tubepush", 0, 0.0f, 0.433333f, "TUBE PUSH INPUT 0→初期値" },
                 C { "tubepush", 0, 0.0f, 1.0f, "TUBE PUSH INPUT 0→1" }, C { "tubepush", 1, 0.144444f, 1.0f, "TUBE PUSH COMPRESS 初期値→1" },
                 C { "tubepush", 3, 0.0f, 1.0f, "TUBE PUSH DIRT OFF→ON" }, C { "tubepush", 3, 1.0f, 0.0f, "TUBE PUSH DIRT ON→OFF" },
                 C { "tubepushxl", 0, 0.0f, 1.0f, "TUBE PUSH XL INPUT 0→1" }, C { "pedalcomp", 1, 0.0f, 1.0f, "PEDAL COMP SUSTAIN 0→1" } })
        for (float amp : { 0.1f, 0.5f })
        {
            Sig s; size_t t1; runJump (c.id, c.k, c.a, c.b, amp, s, t1);
            const float pb = stats (s, t1 - (size_t) (0.2 * sr), t1).peak, pa = stats (s, (size_t) (2.6 * sr), (size_t) (2.9 * sr)).peak, pd = stats (s, t1, (size_t) (2.6 * sr)).peak;
            size_t over = 0; for (size_t i = t1; i < (size_t) (2.6 * sr); ++i) if (std::abs (s.L[i]) > std::max (pb, pa) * 1.122f) ++over;
            std::printf ("  %-28s 入口 %+4.0f dBFS: 山 前 %+6.1f / 後 %+6.1f / 途中 %+6.1f dBFS、+1 dB より上の長さ %5.1f ms %s\n",
                         c.name, db (amp), db (pb), db (pa), db (pd), over * 1000.0 / sr, pd > std::max (pb, pa) * 1.413f ? "★" : "");
        }
}

static void tjSweep()
{
    std::printf ("== DynEngine の部品の全部のつまみを再生中に一気に動かす（220 Hz、入口 −20 / −6 dBFS）。部品ごとのいちばん大きいふくらみ（上）と谷（下）==\n");
    int total = 0, big = 0;
    for (const char* id : { "tubepush", "tubepushxl", "fetcomp", "optocomp", "vcacomp", "buscomp", "chandyn", "quickcomp", "pedalcomp" })
    {
        const auto& inf = *infoFor (typeForId (id));
        double worstUp = -300, worstDn = -300; std::string upAt, dnAt;
        for (int k = 0; k < (int) inf.knobs.size(); ++k)
        {
            const auto& ki = inf.knobs[(size_t) k];
            std::vector<std::pair<float, float>> mv;
            if (ki.steps > 1) { for (int i = 0; i < ki.steps; ++i) for (int j = 0; j < ki.steps; ++j) if (i != j) mv.push_back ({ (float) i / (ki.steps - 1), (float) j / (ki.steps - 1) }); }
            else mv = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { ki.def, 0.0f }, { ki.def, 1.0f }, { 0.0f, ki.def }, { 1.0f, ki.def } };
            for (auto x : mv)
                for (float amp : { 0.1f, 0.5f })
                {
                    Sig s; size_t t1; runJump (id, k, x.first, x.second, amp, s, t1);
                    const Swell w = swellOf (s, t1);
                    char lab[96]; std::snprintf (lab, sizeof (lab), "%s %.2f→%.2f %+.0f dBFS", ki.name, x.first, x.second, db (amp));
                    if (w.over > worstUp) { worstUp = w.over; upAt = lab; }
                    if (w.under > worstDn) { worstDn = w.under; dnAt = lab; }
                    ++total; if (w.over > 1.0) ++big;
                }
        }
        std::printf ("  %-10s 上 %+6.2f dB（%s）/ 下 %+6.2f dB（%s）%s\n", id, worstUp, upAt.c_str(), worstDn, dnAt.c_str(), worstUp > 3.0 ? "★" : "");
    }
    std::printf ("  1 dB を超えるふくらみ: %d / %d 通り（2026-11-19 の 4 回目: 今 38、MAKEUP・入口・出口を dB でならした写し 3）\n", big, total);
}

// ---------------------------------------------------------------------------
// 遅れ L の純粋な遅れ（× gain）。通算 nanAt サンプル目に 1 回 NaN を出す
struct Fault final : Module
{
    int L = 429; float gain = 1.0f; int64_t n = 0, nanAt = -1; int resets = 0;
    std::vector<float> bl, br; int pos = 0;
    void prepare (double, int) override { bl.assign ((size_t) L + 1, 0.0f); br = bl; reset(); }
    void reset() override { std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f); pos = 0; ++resets; }
    void setKnob (int, float) override {}
    int latency() const override { return L; }
    void process (float* l, float* r, int k) override
    {
        for (int i = 0; i < k; ++i, ++n)
        {
            bl[(size_t) pos] = l[i]; br[(size_t) pos] = r[i];
            const int rp = (pos + 1) % (L + 1);
            float ol = bl[(size_t) rp] * gain, orr = br[(size_t) rp] * gain; pos = rp;
            if (n == nanAt) ol = NAN;
            l[i] = ol; r[i] = orr;
        }
    }
};
static Fault* plant (Rack& r, int L, float gain, int64_t nanAt)
{
    auto* f = new Fault(); f->L = L; f->gain = gain; f->nanAt = nanAt; f->prepare (48000, 256);
    auto& s = r.slots[0];
    s.mod.reset (f);
    s.lat = s.oldLat = L; s.latFade = 1.0f; s.prime = 0; s.running = false;
    r.updateLatency();
    return f;
}
static void tjFault()
{
    const double sr = 48000;
    std::printf ("== NaN からの立て直し（遅れ L の純粋な遅れの部品）。gain 1: 理想 = 入口を L 遅らせたもの ==\n");
    for (int L : { 429, 72, 8, 0 })
        for (bool on : { true, false })
        {
            float worst = 0; long bad = 0;
            for (int B : { 1, 64, 256, 2048 })
            {
                Want w = emptyWant(); setSlot (w, 0, "maximizer", {}, on);
                Rack r; r.prepare (sr, B, w);
                plant (r, L, 1.0f, (int64_t) (1.0 * sr) + 37);
                Sig in = sine (sr, 2.0, 220, 0.25f), s = in;
                for (size_t off = 0; off < s.size(); off += (size_t) B) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
                for (size_t i = (size_t) (0.1 * sr); i < s.size(); ++i)
                {
                    if (! std::isfinite (s.L[i])) { ++bad; continue; }
                    worst = std::max (worst, std::abs (s.L[i] - (i >= (size_t) L ? in.L[i - (size_t) L] : 0.0f)));
                }
            }
            std::printf ("  L %3d %-3s（ブロック 1 / 64 / 256 / 2048）: 理想との差の最大 %.6f、NaN %ld\n", L, on ? "ON" : "OFF", worst, bad);
        }
    std::printf ("== NaN の後の戻り方（gain 0.5 = 部品の音は −6 dB。ドライ 0.25 → 部品 0.125）==\n");
    for (int L : { 429, 0 })
    {
        const int B = 256;
        Want w = emptyWant(); setSlot (w, 0, "maximizer");
        Rack r; r.prepare (sr, B, w);
        plant (r, L, 0.5f, (int64_t) (1.0 * sr) + 37);
        Sig s = sine (sr, 2.0, 220, 0.25f);
        for (size_t off = 0; off < s.size(); off += (size_t) B) r.process (s.L.data() + off, s.R.data() + off, B, w, 120.0);
        std::printf ("  L %3d: 1 周期の山（NaN から ms 後）", L);
        const size_t t0 = (size_t) (1.0 * sr) + 37;
        for (double ms : { -5.0, 2.0, 6.0, 9.0, 12.0, 15.0, 20.0, 25.0, 30.0 })
        {
            const size_t a = (size_t) ((double) t0 + ms * sr / 1000.0); float pk = 0;
            for (size_t i = a; i < a + 218; ++i) pk = std::max (pk, std::abs (s.L[i]));
            std::printf (" %+.0f:%.3f", ms, pk);
        }
        std::printf ("\n");
    }
}

static void b4 (int svcEvery, size_t gap, const char* a, const char* b, const char* c)
{
    const double sr = 48000; const int B = 64;
    Want w = emptyWant();
    Rack r; r.prepare (sr, B, w);
    Sig s = sine (sr, 2.5, 220, 0.25f);
    const size_t t1 = (size_t) (1.0 * sr), t2 = t1 + gap, t3 = t2 + gap;
    int stage = 0; size_t lastSvc = 0;
    for (size_t off = 0; off < s.size(); off += (size_t) B)
    {
        if (stage == 0 && off >= t1) { setSlot (w, 0, a); stage = 1; }
        else if (stage == 1 && off >= t2) { setSlot (w, 0, b); stage = 2; }
        else if (stage == 2 && off >= t3) { setSlot (w, 0, c); stage = 3; }
        if (off == 0 || off - lastSvc >= (size_t) svcEvery) { r.service (w); lastSvc = off; }
        r.process (s.L.data() + off, s.R.data() + off, B, w, 120.0);
    }
    std::printf ("  service %4d サンプルごと・選び直し %4zu サンプルおき %s → %s → %s: 遅れ %d、プチッの目安 %.1f\n", svcEvery, gap, a, b, c, r.latency(), cleanliness (s.L, t1 - 2000, t1 + (size_t) (0.6 * sr), sr));
}
static void tjSwap()
{
    const double sr = 48000;
    std::printf ("== 再生中に 空き → MAXIMIZER（押し込みなし）。プチッの目安（ふだん 1.0）と、落ち着いた後の理想（入口を 429 遅らせた音）との差 ==\n");
    for (int B : { 1, 7, 256, 2048 })
    {
        Want w = emptyWant();
        Rack r; r.prepare (sr, B, w);
        Sig in = sine (sr, 2.5, 220, 0.25f), s = in;
        const size_t tSw = (size_t) (1.0 * sr);
        bool done = false; size_t lastSvc = 0;
        for (size_t off = 0; off < s.size(); off += (size_t) B)
        {
            if (! done && off >= tSw) { setSlot (w, 0, "maximizer", { { 1, 1.0f }, { 2, 1.0f } }); done = true; }
            if (off - lastSvc >= 1024 || off == 0) { r.service (w); lastSvc = off; }
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        }
        float dev = 0; for (size_t i = (size_t) (2.0 * sr); i < s.size(); ++i) dev = std::max (dev, std::abs (s.L[i] - in.L[i - 429]));
        std::printf ("  ブロック %4d: 遅れ %d、プチッの目安 %.1f、理想との差 %.6f\n", B, r.latency(), cleanliness (s.L, tSw - 2000, tSw + (size_t) (0.4 * sr), sr), dev);
    }
    std::printf ("== 続けての選び直し（本体の service は 20 Hz = 2400 サンプルごと。64 は本体では起きない速さ）==\n");
    b4 (2400, 240, "maximizer", "fetcomp", "maximizer");
    b4 (2400, 2400, "maximizer", "fetcomp", "maximizer");
    b4 (2400, 2400, "maximizer", "tubepushxl", "maximizer");
    b4 (2400, 2400, "fetcomp", "maximizer", "studiovb");
    b4 (64, 240, "maximizer", "fetcomp", "maximizer");
    b4 (64, 240, "fetcomp", "studiovb", "optocomp");
    std::printf ("== 選び直した直後（prime の途中）に prepare → 再生。新しく作ったラックと比べる ==\n");
    {
        const int B = 64;
        Want w = emptyWant();
        Rack r; r.prepare (sr, B, w);
        Sig s = sine (sr, 1.0, 220, 0.1f);
        for (size_t off = 0; off + B <= s.size(); off += B)
        {
            if (off == (size_t) (0.3 * sr) / B * B) setSlot (w, 0, "maximizer");
            r.service (w);
            r.process (s.L.data() + off, s.R.data() + off, B, w, 120.0);
            if (r.slots[0].prime > 0) break;
        }
        const int pr = r.slots[0].prime;
        r.prepare (sr, B, w);
        std::printf ("  prepare の前 prime %d → 後 prime %d（0 なら Rack::prepare で消している）\n", pr, r.slots[0].prime);
        Sig u = sine (sr, 1.0, 220, 0.1f), u2 = u;
        for (size_t off = 0; off < u.size(); off += B) r.process (u.L.data() + off, u.R.data() + off, B, w, 120.0);
        Rack r2; r2.prepare (sr, B, w);
        for (size_t off = 0; off < u2.size(); off += B) r2.process (u2.L.data() + off, u2.R.data() + off, B, w, 120.0);
        float dmax = 0; for (size_t i = 0; i < u.size(); ++i) dmax = std::max (dmax, std::abs (u.L[i] - u2.L[i]));
        std::printf ("  新しく作ったラックとの差の最大 %.4f\n", dmax);
    }
}

int main (int argc, char** argv)
{
    const std::string m = argc > 1 ? argv[1] : "all";
    if (m == "dirt" || m == "all") tjDirt();
    if (m == "peak" || m == "all") tjPeak();
    if (m == "sw" || m == "all") tjSweep();
    if (m == "fault" || m == "all") tjFault();
    if (m == "swap" || m == "all") tjSwap();
    return 0;
}
