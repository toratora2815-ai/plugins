// Hybrid Kit の試作・聴き比べ用の書き出し（JUCE なし、KitEngine.h をそのまま使う）
// ビルド: clang++ -std=c++17 -O2 -o render render.cpp
// 使い方:
//   render demo <出力フォルダ> [kit=Studio Kit]   … 聴き比べの WAV（Drummer のループ＋ポップスのビート × 生だけ / ＋808 サブ / ＋クラップ / 両方）
//   render test                                     … 数値テスト（NaN・音割れ・鳴り止むか・ブロックの大きさ・サブの合わせ）
#include "../Source/KitEngine.h"
#include "../Source/Resampler.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hk;

struct Ev { double sec; int note, vel; };

// ---- MIDI（SMF。Apple Loops の .caf なら midi の塊から） ----
static bool readFile (const std::string& p, std::vector<uint8_t>& d)
{
    FILE* f = fopen (p.c_str(), "rb");
    if (! f) return false;
    fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
    d.resize ((size_t) n);
    const bool ok = fread (d.data(), 1, d.size(), f) == d.size();
    fclose (f);
    return ok;
}

static std::vector<Ev> parseSmf (const uint8_t* m, size_t len)
{
    std::vector<Ev> out;
    if (len < 14 || memcmp (m, "MThd", 4) != 0) return out;
    const int ntr = (m[10] << 8) | m[11], div = (m[12] << 8) | m[13];
    size_t p = 8 + rd32be (m + 4);
    struct Raw { uint64_t tick; int type; int a, b; uint32_t tempo; };
    std::vector<Raw> raw;
    for (int tr = 0; tr < ntr && p + 8 <= len; ++tr)
    {
        if (memcmp (m + p, "MTrk", 4) != 0) break;
        const size_t end = p + 8 + rd32be (m + p + 4);
        size_t q = p + 8; uint64_t t = 0; int rs = 0;
        auto vlq = [&] () { uint32_t v = 0; while (q < end) { const uint8_t b = m[q++]; v = (v << 7) | (b & 0x7f); if (b < 0x80) break; } return v; };
        while (q < end)
        {
            t += vlq();
            const int st = m[q];
            if (st == 0xff) { const int ty = m[q + 1]; q += 2; const uint32_t l = vlq(); if (ty == 0x51 && l == 3) raw.push_back ({ t, 1, 0, 0, (uint32_t) ((m[q] << 16) | (m[q + 1] << 8) | m[q + 2]) }); q += l; continue; }
            if (st == 0xf0 || st == 0xf7) { ++q; const uint32_t l = vlq(); q += l; continue; }
            if (st & 0x80) { rs = st; ++q; }
            const int hi = rs & 0xf0, n = (hi == 0xc0 || hi == 0xd0) ? 1 : 2;
            const int a = m[q], b = n > 1 ? m[q + 1] : 0; q += (size_t) n;
            if (hi == 0x90 && b > 0) raw.push_back ({ t, 0, a, b, 0 });
            if (hi == 0xb0 && a == 4) raw.push_back ({ t, 2, a, b, 0 });
        }
        p = end;
    }
    std::stable_sort (raw.begin(), raw.end(), [] (const Raw& x, const Raw& y) { return x.tick < y.tick; });
    double sec = 0, spt = 0.5 / div; uint64_t last = 0;
    for (auto& r : raw)
    {
        sec += (double) (r.tick - last) * spt; last = r.tick;
        if (r.type == 1) spt = r.tempo / 1e6 / div;
        else if (r.type == 0) out.push_back ({ sec, r.a, r.b });
        else out.push_back ({ sec, -4, r.b });   // CC4
    }
    return out;
}

static std::vector<Ev> midiFromCaf (const std::string& path)
{
    std::vector<uint8_t> d;
    if (! readFile (path, d) || d.size() < 8) return {};
    size_t p = 8;
    while (p + 12 <= d.size())
    {
        const uint64_t n = rd64be (d.data() + p + 4);
        if (memcmp (d.data() + p, "midi", 4) == 0) return parseSmf (d.data() + p + 12, (size_t) n);
        p += 12 + n;
    }
    return {};
}

// ---- ポップスのビート（100 BPM・8 小節。少しだけ人間らしく揺らす） ----
static std::vector<Ev> popGroove()
{
    std::vector<Ev> e;
    std::mt19937 r (7);
    std::normal_distribution<double> jt (0.0, 0.0022);
    std::uniform_int_distribution<int> jv (-5, 5);
    const double bpm = 100, step = 60.0 / bpm / 4;
    auto add = [&] (int bar, int st, int note, int vel) { e.push_back ({ (bar * 16 + st) * step + 0.05 + jt (r), note, std::clamp (vel + jv (r), 1, 127) }); };
    for (int bar = 0; bar < 8; ++bar)
    {
        const bool fill = bar == 7;
        // キック
        add (bar, 0, 36, 112);
        add (bar, 6, 36, 96);
        add (bar, 8, 36, 108);
        if (bar % 2 == 1 && ! fill) add (bar, 11, 36, 92);
        if (bar % 4 == 2) add (bar, 14, 36, 88);
        // スネア（2・4 拍目、ゴーストは弱く）
        add (bar, 4, 38, 116);
        if (! fill) add (bar, 12, 38, 118);
        if (bar % 2 == 0) { add (bar, 7, 38, 28); add (bar, 15, 38, 32); }
        if (bar % 4 == 3 && ! fill) add (bar, 10, 38, 26);
        // ハイハット（8 分の頭は強め、ところどころ 16 分）
        for (int s = 0; s < 16; s += 2)
        {
            if (fill && s >= 8) break;
            if (bar % 2 == 1 && s == 14) { add (bar, 14, 46, 92); continue; }   // オープン → 次の頭でペダル
            add (bar, s, 42, s % 4 == 0 ? 88 : 66);
            if (bar % 4 >= 2 && (s == 2 || s == 10)) add (bar, s + 1, 42, 44);
        }
        if (bar % 2 == 1 && bar < 7) add (bar + 1, 0, 33, 70);
        // クラッシュ
        if (bar == 0 || bar == 4) add (bar, 0, 49, 110);
        // 8 小節目のフィル: スネアの 16 分 → タム
        if (fill)
        {
            const int sn[] = { 8, 9, 10, 11 };
            for (int k = 0; k < 4; ++k) add (bar, sn[k], 38, 70 + k * 12);
            add (bar, 12, 48, 104); add (bar, 13, 48, 92);
            add (bar, 14, 47, 108); add (bar, 15, 43, 116);
        }
    }
    e.push_back ({ 8 * 16 * step + 0.05, 36, 118 });
    e.push_back ({ 8 * 16 * step + 0.05, 57, 118 });
    std::stable_sort (e.begin(), e.end(), [] (const Ev& a, const Ev& b) { return a.sec < b.sec; });
    return e;
}

// ---- 書き出し ----
static std::unique_ptr<Engine> makeEngine (Kit& kit, double fs, float kickBlend, float snareBlend, int kickSound = 0, int snareSound = 0)
{
    Settings st; st.kickBlend = kickBlend; st.snareBlend = snareBlend; st.kickSound = kickSound; st.snareSound = snareSound;
    analyzeEdm (kit, st);
    auto e = std::make_unique<Engine>();
    e->prepare (fs); e->setKit (&kit); e->setSettings (st);
    return e;
}

static void renderEvents (Engine& eng, const std::vector<Ev>& ev, double fs, double tailSec, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const double endSec = (ev.empty() ? 0 : ev.back().sec) + tailSec;
    const size_t N = (size_t) (endSec * fs);
    L.assign (N, 0.0f); R.assign (N, 0.0f);
    size_t i = 0, k = 0;
    while (i < N)
    {
        while (k < ev.size() && (size_t) std::llround (ev[k].sec * fs) <= i)
        {
            if (ev[k].note == -4) eng.controller (4, ev[k].vel);
            else eng.noteOn (ev[k].note, ev[k].vel);
            ++k;
        }
        size_t n = std::min<size_t> ((size_t) block, N - i);
        if (k < ev.size()) n = std::min (n, std::max<size_t> (1, (size_t) std::llround (ev[k].sec * fs) - i));
        eng.render (L.data() + i, R.data() + i, (int) n);
        i += n;
    }
}

// BS.1770 の K 特性で測った大きさ（LUFS、ゲートは −70 と相対 −10）
static double loudness (const std::vector<float>& L, const std::vector<float>& R, double fs)
{
    auto kfilter = [&] (const std::vector<float>& x)
    {
        // 1 段目: 高域の棚（+4 dB）、2 段目: 38 Hz の高域通過（BS.1770 の式をこの fs で作る）
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (3.141592653589793 * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        const double b0 = (Vh + Vb * K / Q + K * K) / a0, b1 = 2.0 * (K * K - Vh) / a0, b2 = (Vh - Vb * K / Q + K * K) / a0;
        const double a1 = 2.0 * (K * K - 1.0) / a0, a2 = (1.0 - K / Q + K * K) / a0;
        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan (3.141592653589793 * f1 / fs);
        const double c1 = 2.0 * (K1 * K1 - 1.0) / (1.0 + K1 / Q1 + K1 * K1), c2 = (1.0 - K1 / Q1 + K1 * K1) / (1.0 + K1 / Q1 + K1 * K1);
        std::vector<double> y (x.size());
        double z1 = 0, z2 = 0, w1 = 0, w2 = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double s = b0 * x[i] + z1; z1 = b1 * x[i] - a1 * s + z2; z2 = b2 * x[i] - a2 * s;
            const double v = s - c1 * w1 - c2 * w2;   // 2 段目（1 − 2z^-1 + z^-2）/（1 + c1 z^-1 + c2 z^-2）
            y[i] = v - 2.0 * w1 + w2;
            w2 = w1; w1 = v;
        }
        return y;
    };
    const auto kl = kfilter (L), kr = kfilter (R);
    const size_t blk = (size_t) (0.4 * fs), hop = (size_t) (0.1 * fs);
    std::vector<double> e;
    for (size_t s = 0; s + blk <= kl.size(); s += hop)
    {
        double a = 0;
        for (size_t i = s; i < s + blk; ++i) a += kl[i] * kl[i] + kr[i] * kr[i];
        e.push_back (a / (double) blk);
    }
    auto lufs = [] (double ms) { return -0.691 + 10.0 * std::log10 (ms + 1e-30); };
    double sum = 0; int cnt = 0;
    for (double v : e) if (lufs (v) > -70) { sum += v; ++cnt; }
    if (cnt == 0) return -100;
    const double rel = lufs (sum / cnt) - 10;
    sum = 0; cnt = 0;
    for (double v : e) if (lufs (v) > rel) { sum += v; ++cnt; }
    return cnt ? lufs (sum / cnt) : -100;
}

static bool writeWav16 (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, double fs)
{
    FILE* f = fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 4;
    auto u32 = [&] (uint32_t v) { fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { fwrite (&v, 2, 1, f); };
    fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); fwrite ("WAVE", 1, 4, f);
    fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) fs); u32 ((uint32_t) fs * 4); u16 (4); u16 (16);
    fwrite ("data", 1, 4, f); u32 (dataBytes);
    std::mt19937 r (1); std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    for (uint32_t i = 0; i < n; ++i)
        for (const float x : { L[i], R[i] })
        {
            const float v = std::clamp (x * 32767.0f + d (r) + d (r), -32768.0f, 32767.0f);
            const int16_t s = (int16_t) std::lround (v);
            fwrite (&s, 2, 1, f);
        }
    fclose (f);
    return true;
}

static float peakOf (const std::vector<float>& L, const std::vector<float>& R)
{
    float p = 0;
    for (size_t i = 0; i < L.size(); ++i) p = std::max ({ p, std::abs (L[i]), std::abs (R[i]) });
    return p;
}

static std::string kitPath (const std::string& kitName) { return dkdKitFolder() + "/" + kitName + ".exs"; }

static int demo (const std::string& outDir, const std::string& kitName)
{
    Kit kit; std::string err;
    if (! kit.loadProducerKit (kitPath (kitName), err)) { fprintf (stderr, "%s\n", err.c_str()); return 1; }
    const double fs = kit.sampleRate > 0 ? kit.sampleRate : 44100.0;
    printf ("キット %s: 奏法 %zu、録音ファイル %zu（%.0f Hz）\n", kit.name.c_str(), kit.artics.size(), kit.files.size(), fs);
    refineSplices (kit);

    const std::string loop = std::string (getenv ("HOME")) + "/Music/Logic Pro Library.bundle/Apple Loops/Session Player/Roots Songwriter Drummer - Edgewood.caf";
    struct Groove { std::string tag; std::vector<Ev> ev; };
    std::vector<Groove> grooves;
    auto lp = midiFromCaf (loop);
    if (! lp.empty()) grooves.push_back ({ "1_Drummer85", lp });
    grooves.push_back ({ "2_Pop100", popGroove() });

    struct Variant { std::string tag; float kick, clap; };
    const Variant vars[] = { { "A_live", 0, 0 }, { "B_live+808sub", 1, 0 }, { "C_live+clap", 0, 1 }, { "D_live+808sub+clap", 1, 1 } };

    struct Out { std::string name; std::vector<float> L, R; double lufs; float peak; };
    std::vector<Out> outs;
    for (auto& g : grooves)
        for (auto& v : vars)
        {
            auto eng = makeEngine (kit, fs, v.kick, v.clap);
            Out o; o.name = g.tag + "_" + v.tag;
            renderEvents (*eng, g.ev, fs, 3.0, o.L, o.R);
            o.lufs = loudness (o.L, o.R, fs); o.peak = peakOf (o.L, o.R);
            printf ("%-34s %6.1f LUFS  ピーク %6.1f dBFS\n", o.name.c_str(), o.lufs, 20 * std::log10 (o.peak + 1e-9));
            outs.push_back (std::move (o));
        }
    // 全部を同じ大きさにそろえる（いちばん余裕のないものでもピーク −1 dBFS 以内）
    double target = -14;
    for (auto& o : outs) target = std::min (target, o.lufs + (-1.0 - 20 * std::log10 (o.peak + 1e-9)));
    for (auto& o : outs)
    {
        const float g = (float) std::pow (10.0, (target - o.lufs) / 20.0);
        for (auto& x : o.L) x *= g;
        for (auto& x : o.R) x *= g;
        writeWav16 (outDir + "/" + o.name + ".wav", o.L, o.R, fs);
    }
    printf ("全部 %.1f LUFS にそろえて書き出し → %s\n", target, outDir.c_str());
    return 0;
}

// ---- 数値テスト ----
static int test (const std::string& kitName)
{
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { printf ("%s %s\n", ok ? "OK  " : "NG  ", what); if (! ok) ++fails; };
    Kit kit; std::string err;
    if (! kit.loadProducerKit (kitPath (kitName), err)) { fprintf (stderr, "%s\n", err.c_str()); return 1; }
    refineSplices (kit);
    const double fs = 44100;
    auto ev = popGroove();

    // 1) NaN・ピーク・鳴り止むか（生＋サブ＋クラップ）
    {
        auto e = makeEngine (kit, fs, 1, 1);
        std::vector<float> L, R; renderEvents (*e, ev, fs, 12.0, L, R);
        bool finite = true; for (size_t i = 0; i < L.size(); ++i) finite &= std::isfinite (L[i]) && std::isfinite (R[i]);
        check (finite, "NaN・無限大がない");
        const float pk = peakOf (L, R);
        printf ("     ピーク %.1f dBFS\n", 20 * std::log10 (pk + 1e-9));
        check (pk < 8.0f, "ピークがとんでもなく大きくない（+18 dB 未満）");
        float tail = 0; for (size_t i = L.size() - (size_t) (0.5 * fs); i < L.size(); ++i) tail = std::max ({ tail, std::abs (L[i]), std::abs (R[i]) });
        printf ("     最後の 0.5 秒 %.1f dBFS\n", 20 * std::log10 (tail + 1e-12));
        check (tail < 1e-5f, "最後の音（クラッシュ）から 12 秒で完全に鳴り止む");
    }
    // 2) ブロックの大きさで結果が変わらない
    {
        std::vector<float> a, b, c, d;
        { auto e = makeEngine (kit, fs, 1, 1); renderEvents (*e, ev, fs, 1.0, a, b, 512); }
        { auto e = makeEngine (kit, fs, 1, 1); renderEvents (*e, ev, fs, 1.0, c, d, 37); }
        float diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max ({ diff, std::abs (a[i] - c[i]), std::abs (b[i] - d[i]) });
        printf ("     512 と 37 の差の最大 %.2e\n", diff);
        check (diff < 1e-4f, "ブロックの大きさ（512 / 37）で音が変わらない");
    }
    // 3) BLEND 0 は生だけと同じ（サブ・クラップが混ざらない）、キックの低域の分け方は平ら（位相だけ）
    {
        std::vector<Ev> one { { 0.01, 36, 110 } };
        std::vector<float> a, b;
        { auto e = makeEngine (kit, fs, 0, 0); renderEvents (*e, one, fs, 1.5, a, b); }
        auto f = makeEngine (kit, fs, 1, 0);
        std::vector<float> c, d; renderEvents (*f, one, fs, 1.5, c, d);
        auto band = [&] (const std::vector<float>& x, double lo, double hi)
        {
            Biquad h1, h2, l1, l2; h1.highpass (fs, lo, 0.707); h2 = h1; l1.lowpass (fs, hi, 0.707); l2 = l1;
            double s = 0; for (float v : x) { const float y = l2.run (l1.run (h2.run (h1.run (v)))); s += (double) y * y; }
            return 10 * std::log10 (s + 1e-30);
        };
        const double lowA = band (a, 30, 90), lowC = band (c, 30, 90), midA = band (a, 300, 3000), midC = band (c, 300, 3000);
        const double want = kickSound (0).levelDb;
        printf ("     30〜90 Hz: 生 %.1f dB → サブ %.1f dB（差 %.1f、狙い +%.0f）、300 Hz〜3 kHz: 差 %.2f dB\n", lowA, lowC, lowC - lowA, want, midC - midA);
        check (std::abs (lowC - lowA - want) < 3.0, "サブに替えても低音の大きさが狙い（808 Short は +3 dB）±3 dB 以内（自動で合わせている）");
        check (std::abs (midC - midA) < 0.5, "サブに替えても 300 Hz〜3 kHz（生の輪郭）は変わらない");
    }
    // 4) 弱いスネアではクラップが鳴らない
    {
        std::vector<Ev> ghost { { 0.01, 38, 25 } };
        std::vector<float> a, b, c, d;
        { auto e = makeEngine (kit, fs, 0, 0); renderEvents (*e, ghost, fs, 0.8, a, b); }
        { auto f = makeEngine (kit, fs, 0, 1); renderEvents (*f, ghost, fs, 0.8, c, d); }
        float diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, std::abs (a[i] - c[i]));
        check (diff < 1e-6f, "ゴースト（強さ 25）にはクラップが付かない");
    }
    // 5) ハイハット: オープンの後に閉じると止まる
    {
        std::vector<Ev> hh { { 0.01, 46, 100 }, { 0.30, 33, 80 } };
        std::vector<float> a, b; { auto e = makeEngine (kit, fs, 0, 0); renderEvents (*e, hh, fs, 1.2, a, b); }
        float after = 0; for (size_t i = (size_t) (0.42 * fs); i < (size_t) (0.6 * fs); ++i) after = std::max (after, std::abs (a[i]));
        float before = 0; for (size_t i = (size_t) (0.1 * fs); i < (size_t) (0.29 * fs); ++i) before = std::max (before, std::abs (a[i]));
        printf ("     オープン中 %.1f dB、ペダルの 120 ms 後 %.1f dB\n", 20 * std::log10 (before + 1e-9), 20 * std::log10 (after + 1e-9));
        check (after < before * 0.25f, "オープンハイハットがペダルで止まる（−12 dB 以下）");
    }
    // 6) サンプルレート変換（44.1 → 48 kHz）: 1 kHz のひずみ、19 kHz の写り込み、遅れ
    {
        auto run = [] (const std::vector<float>& in, int nOut)
        {
            StreamResampler rs; rs.setup (44100, 48000);
            std::vector<float> outL ((size_t) nOut), outR ((size_t) nOut);
            size_t fed = 0; int done = 0;
            while (done < nOut)
            {
                const int n = std::min (512, nOut - done);
                const int64_t need = rs.needInputUpTo (n);
                while (rs.inputWritten() < need)
                {
                    const int k = (int) std::min<int64_t> (512, need - rs.inputWritten());
                    std::vector<float> a ((size_t) k, 0.0f);
                    for (int i = 0; i < k; ++i) a[(size_t) i] = fed + (size_t) i < in.size() ? in[fed + (size_t) i] : 0.0f;
                    rs.push (a.data(), a.data(), k); fed += (size_t) k;
                }
                rs.produce (outL.data() + done, outR.data() + done, n);
                done += n;
            }
            return outL;
        };
        auto tone = [] (double hz, int n) { std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5f * (float) std::sin (2 * 3.141592653589793 * hz * i / 44100.0); return x; };
        auto fit = [] (const std::vector<float>& y, double hz, int from, int to)   // その周波数の成分の大きさ（48 kHz）
        {
            double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
            for (int i = from; i < to; ++i)
            {
                const double w = 2 * 3.141592653589793 * hz * i / 48000.0, sn = std::sin (w), cs = std::cos (w);
                ss += sn * sn; cc += cs * cs; sc += sn * cs; ys += y[(size_t) i] * sn; yc += y[(size_t) i] * cs;
            }
            const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            return std::sqrt (a * a + b * b);
        };
        auto y1 = run (tone (1000, 50000), 48000);
        double res = 0, sig = 0;
        {
            const double amp = fit (y1, 1000, 4800, 43200);
            // 残り（1 kHz を引いた分）
            double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
            for (int i = 4800; i < 43200; ++i) { const double w = 2 * 3.141592653589793 * 1000 * i / 48000.0; ss += std::sin (w) * std::sin (w); cc += std::cos (w) * std::cos (w); sc += std::sin (w) * std::cos (w); ys += y1[(size_t) i] * std::sin (w); yc += y1[(size_t) i] * std::cos (w); }
            const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            for (int i = 4800; i < 43200; ++i) { const double w = 2 * 3.141592653589793 * 1000 * i / 48000.0; const double e = y1[(size_t) i] - a * std::sin (w) - b * std::cos (w); res += e * e; sig += (a * std::sin (w) + b * std::cos (w)) * (a * std::sin (w) + b * std::cos (w)); }
            printf ("     1 kHz: 大きさ %.4f（入力 0.5）、残り %.1f dB\n", amp, 10 * std::log10 (res / sig));
            check (std::abs (amp - 0.5) < 0.005 && 10 * std::log10 (res / sig) < -80, "44.1→48 kHz: 1 kHz の大きさが同じで、ひずみ・雑音が −80 dB 以下");
        }
        auto y2 = run (tone (19000, 50000), 48000);
        const double a19 = fit (y2, 19000, 4800, 43200), aImg = fit (y2, 48000 - (44100 - 19000), 4800, 43200);
        printf ("     19 kHz: %.1f dB、写り込み（22.9 kHz）%.1f dB\n", 20 * std::log10 (a19 / 0.5), 20 * std::log10 (aImg / 0.5 + 1e-12));
        check (std::abs (20 * std::log10 (a19 / 0.5)) < 0.5 && 20 * std::log10 (aImg / 0.5 + 1e-12) < -70, "19 kHz はそのまま、写り込みは −70 dB 以下");
        std::vector<float> imp (4000, 0.0f); imp[1000] = 1.0f;
        auto y3 = run (imp, 3000);
        int pk = 0; for (int i = 0; i < 3000; ++i) if (std::abs (y3[(size_t) i]) > std::abs (y3[(size_t) pk])) pk = i;
        const double expect = 1000.0 * 48000.0 / 44100.0;   // 変換そのものは遅れない（遅れはノートを H 個あとに置く分だけ。下の 7 で確かめる）
        printf ("     インパルスの山 %d（計算 %.1f）\n", pk, expect);
        check (std::abs (pk - expect) <= 1.0, "変換の時間の位置が計算どおり");
    }
    // 7) 48 kHz のホストでの流れ（中は 44.1 kHz、ノートは eventIndexFor で置く）と、44.1 kHz で直接作った音の大きさの差
    {
        std::vector<float> a, b;
        { auto e = makeEngine (kit, fs, 1, 1); renderEvents (*e, ev, fs, 3.0, a, b); }
        auto e = makeEngine (kit, fs, 1, 1);
        StreamResampler rs; rs.setup (44100, 48000);
        const double host = 48000;
        const size_t N = (size_t) ((ev.back().sec + 3.0) * host);
        std::vector<float> L (N), R (N), tl (Engine::maxBlock), tr (Engine::maxBlock);
        size_t k = 0;
        for (size_t pos = 0; pos < N; pos += 256)
        {
            const int n = (int) std::min<size_t> (256, N - pos);
            const int64_t need = rs.needInputUpTo (n);
            while (rs.inputWritten() < need)
            {
                while (k < ev.size())
                {
                    const int64_t hi = (int64_t) std::llround (ev[k].sec * host);
                    if (hi >= (int64_t) (pos + (size_t) n)) break;
                    if (rs.eventIndexFor (hi) > rs.inputWritten()) break;
                    if (ev[k].note == -4) e->controller (4, ev[k].vel); else e->noteOn (ev[k].note, ev[k].vel);
                    ++k;
                }
                int64_t until = need;
                if (k < ev.size())
                {
                    const int64_t hi = (int64_t) std::llround (ev[k].sec * host);
                    if (hi < (int64_t) (pos + (size_t) n)) until = std::min (until, std::max (rs.inputWritten() + 1, rs.eventIndexFor (hi)));
                }
                const int c = (int) std::min<int64_t> (Engine::maxBlock, until - rs.inputWritten());
                e->render (tl.data(), tr.data(), c);
                rs.push (tl.data(), tr.data(), c);
            }
            rs.produce (L.data() + pos, R.data() + pos, n);
        }
        bool finite = true; for (size_t i = 0; i < N; ++i) finite &= std::isfinite (L[i]) && std::isfinite (R[i]);
        const double l44 = loudness (a, b, 44100), l48 = loudness (L, R, host);
        printf ("     44.1 kHz で直接 %.2f LUFS、48 kHz の流れ %.2f LUFS（差 %.2f）\n", l44, l48, l48 - l44);
        check (finite && std::abs (l48 - l44) < 0.3, "48 kHz のホストでも同じ大きさ（±0.3 dB）・NaN なし");
        // 最初の音の鳴り始め（ピークの 10 %）のずれ = 申告する遅れ
        auto onsetOf = [] (const std::vector<float>& x, double rate)
        {
            float pk = 0; for (size_t i = 0; i < (size_t) (0.5 * rate) && i < x.size(); ++i) pk = std::max (pk, std::abs (x[i]));
            for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) > pk * 0.1f) return (double) i / rate;
            return 0.0;
        };
        StreamResampler tmp; tmp.setup (44100, 48000);
        const double d = (onsetOf (L, host) - onsetOf (a, 44100)) * host;
        printf ("     鳴り始めのずれ %.1f サンプル（申告する遅れ %d）\n", d, tmp.latencyOut());
        check (std::abs (d - tmp.latencyOut()) < 2.0, "48 kHz の遅れが申告どおり（Logic が補正できる）");
    }
    // 8) TUNE（録音の音程）と LENGTH（短くする）
    {
        auto one = [&] (float tune, float len, std::vector<float>& L)
        {
            Settings st; st.tune[GToms] = tune; st.length[GToms] = len;
            analyzeEdm (kit, st);
            auto e = std::make_unique<Engine>(); e->prepare (fs); e->setKit (&kit); e->setSettings (st);
            std::vector<Ev> hit { { 0.01, 48, 110 } };
            std::vector<float> R; renderEvents (*e, hit, fs, 3.0, L, R);
        };
        auto centroid = [&] (const std::vector<float>& x)   // 最初の 0.3 秒の重心（Hz）
        {
            const int N = 8192; double num = 0, den = 0;
            for (int k = 1; k < N / 2; ++k)
            {
                double re = 0, im = 0;
                if (k % 8 != 0) continue;   // 粗く
                for (int i = 0; i < N && i < (int) x.size(); ++i) { const double w = 2 * 3.141592653589793 * k * i / N; re += x[(size_t) i] * std::cos (w); im -= x[(size_t) i] * std::sin (w); }
                const double m = re * re + im * im; num += m * k * fs / N; den += m;
            }
            return num / (den + 1e-30);
        };
        std::vector<float> a, b, c;
        one (0, 1, a); one (12, 1, b); one (0, 0.2f, c);
        bool finite = true; for (float v : b) finite &= std::isfinite (v);
        const double ca = centroid (a), cb = centroid (b);
        auto tailE = [&] (const std::vector<float>& x) { double s = 0; for (size_t i = (size_t) (0.4 * fs); i < (size_t) (1.2 * fs) && i < x.size(); ++i) s += (double) x[i] * x[i]; return 10 * std::log10 (s + 1e-30); };
        printf ("     TUNE +12: 重心 %.0f → %.0f Hz（×%.2f）、LENGTH 20 %%: 0.4〜1.2 秒の残り %.1f dB → %.1f dB\n", ca, cb, cb / ca, tailE (a), tailE (c));
        check (finite && cb / ca > 1.5 && cb / ca < 2.6, "TUNE +12 で音程が上がる（重心が 1.5〜2.6 倍）・NaN なし");
        check (tailE (c) < tailE (a) - 20, "LENGTH 20 % で響きが 20 dB 以上短くなる");
    }
    // 9) キットがなくても EDM の層は鳴る / サイドスティックにクラップは付かない / 鳴っている間に SOUND を替えてもサブはふくらまない / LENGTH 99↔100 %
    {
        Settings st; st.kickBlend = 1; st.snareBlend = 1;
        auto e = std::make_unique<Engine>(); e->prepare (fs); e->setSettings (st);   // キットなし
        std::vector<Ev> hits { { 0.01, 36, 110 }, { 0.5, 38, 110 } };
        std::vector<float> L, R; renderEvents (*e, hits, fs, 0.5, L, R);
        const float pk = peakOf (L, R);
        printf ("     キットなしの EDM のピーク %.1f dBFS\n", 20 * std::log10 (pk + 1e-12));
        check (pk > 0.01f, "キットがなくても EDM の層（サブ・クラップ）は鳴る");
    }
    {
        std::vector<Ev> ss { { 0.01, 37, 110 } };
        std::vector<float> a, b, c, d;
        { auto e = makeEngine (kit, fs, 0, 0); renderEvents (*e, ss, fs, 0.6, a, b); }
        { auto e = makeEngine (kit, fs, 0, 1); renderEvents (*e, ss, fs, 0.6, c, d); }
        float diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, std::abs (a[i] - c[i]));
        check (diff < 1e-6f, "サイドスティック（37）にはクラップが付かない");
    }
    {
        // Sub Thump で叩いて 50 ms 後に 808 Long へ替える → 鳴っているサブは Sub Thump のまま（0.3〜0.6 秒が静か）
        Settings s1; s1.kickBlend = 1; s1.kickSound = 2;
        analyzeEdm (kit, s1);
        auto e = std::make_unique<Engine>(); e->prepare (fs); e->setKit (&kit); e->setSettings (s1);
        std::vector<float> L ((size_t) (1.0 * fs)), R (L.size());
        e->noteOn (36, 110);
        size_t pos = 0;
        while (pos < L.size())
        {
            if (pos >= (size_t) (0.05 * fs)) { Settings s2 = s1; s2.kickSound = 1; e->setSettings (s2); }
            const int n = (int) std::min<size_t> (256, L.size() - pos);
            e->render (L.data() + pos, R.data() + pos, n); pos += (size_t) n;
        }
        std::vector<float> L0 ((size_t) (1.0 * fs)), R0 (L0.size());
        { auto e0 = std::make_unique<Engine>(); e0->prepare (fs); e0->setKit (&kit); e0->setSettings (s1); e0->noteOn (36, 110);
          for (size_t p2 = 0; p2 < L0.size(); p2 += 256) e0->render (L0.data() + p2, R0.data() + p2, (int) std::min<size_t> (256, L0.size() - p2)); }
        float diff = 0; for (size_t i = 0; i < L.size(); ++i) diff = std::max (diff, std::abs (L[i] - L0[i]));
        printf ("     SOUND を途中で替えたときの差の最大 %.2e\n", diff);
        check (diff < 1e-6f, "鳴っている間に SOUND を替えても、鳴っているサブの形は変わらない");
    }
    {
        auto tailAt = [&] (float len)
        {
            Settings st; st.length[GCymbals] = len;
            auto e = std::make_unique<Engine>(); e->prepare (fs); e->setKit (&kit); e->setSettings (st);
            std::vector<Ev> hit { { 0.01, 49, 110 } };
            std::vector<float> L, R; renderEvents (*e, hit, fs, 4.0, L, R);
            double s = 0; for (size_t i = (size_t) (2.8 * fs); i < (size_t) (3.1 * fs); ++i) s += (double) L[i] * L[i];
            return 10 * std::log10 (s + 1e-30);
        };
        const double t99 = tailAt (0.99f), t100 = tailAt (1.0f);
        printf ("     クラッシュの 2.8〜3.1 秒: LENGTH 99 %% %.1f dB、100 %% %.1f dB\n", t99, t100);
        check (std::abs (t100 - t99) < 2.0, "LENGTH 99 % と 100 % の間に段差がない（2 dB 以内）");
    }
    // 10) マルチアウト: 部品ごとに別の出口へ出しても、全部を足すと 1 本で出したのと同じ / 出口ごとに自分の部品だけ / 途中で替えても足した音は同じ
    {
        constexpr int K = Engine::maxOuts;
        auto runMulti = [&] (const std::vector<Ev>& evs, const std::array<int, NumPieces>& direct, const std::array<int, NumPieces>& room,
                             double switchAt, std::vector<std::vector<float>>& outsV)
        {
            Settings st; st.kickBlend = 1; st.snareBlend = 1;
            analyzeEdm (kit, st);
            auto e = std::make_unique<Engine>(); e->prepare (fs); e->setKit (&kit); e->setSettings (st);
            std::array<int, NumPieces> zero {};
            e->setRouting (switchAt > 0 ? zero : direct, switchAt > 0 ? zero : room);
            const size_t N = (size_t) ((evs.back().sec + 2.0) * fs);
            outsV.assign (2 * K, std::vector<float> (N, 0.0f));
            std::vector<float*> ptr (2 * K);
            size_t i = 0, k = 0;
            while (i < N)
            {
                while (k < evs.size() && (size_t) std::llround (evs[k].sec * fs) <= i) { e->noteOn (evs[k].note, evs[k].vel); ++k; }
                if (switchAt > 0 && i >= (size_t) (switchAt * fs)) e->setRouting (direct, room);
                size_t n = std::min<size_t> (256, N - i);
                if (k < evs.size()) n = std::min (n, std::max<size_t> (1, (size_t) std::llround (evs[k].sec * fs) - i));
                for (int c = 0; c < 2 * K; ++c) ptr[(size_t) c] = outsV[(size_t) c].data() + i;
                e->render (ptr.data(), K, (int) n);
                i += n;
            }
        };
        std::array<int, NumPieces> direct {}, room {};
        direct[Kick] = Engine::OutKick; direct[Snare] = Engine::OutSnare;
        for (int p : { (int) TomHi, (int) TomMidHi, (int) TomMidLo, (int) TomLo }) direct[(size_t) p] = Engine::OutToms;
        direct[HiHat] = Engine::OutHiHat;
        for (int p : { (int) CrashL, (int) CrashR, (int) Ride }) direct[(size_t) p] = Engine::OutCymbals;
        room = direct;
        std::vector<float> a, b;
        { auto e = makeEngine (kit, fs, 1, 1); renderEvents (*e, ev, fs, 2.0, a, b); }
        std::vector<std::vector<float>> o;
        runMulti (ev, direct, room, 0, o);
        float d = 0; for (size_t i = 0; i < a.size() && i < o[0].size(); ++i) { float sl = 0, sr = 0; for (int c = 0; c < K; ++c) { sl += o[(size_t) (2 * c)][i]; sr += o[(size_t) (2 * c + 1)][i]; } d = std::max ({ d, std::abs (sl - a[i]), std::abs (sr - b[i]) }); }
        float mainPk = 0; for (size_t i = 0; i < o[0].size(); ++i) mainPk = std::max ({ mainPk, std::abs (o[0][i]), std::abs (o[1][i]) });
        printf ("     全部を別の出口に: 足した音と 1 本の差の最大 %.2e、Main に残った音 %.1f dBFS\n", d, 20 * std::log10 (mainPk + 1e-12));
        check (d < 1e-5f, "マルチアウト: 全部の出口を足すと 1 本で出したのと同じ（二重に足していない・抜けていない）");
        check (mainPk < 1e-7f, "マルチアウト: 全部を分けたら Main は無音");
        // キックだけ・ルームは分ける: スネアを叩いても Kick の出口は無音、キックのルームは Room の出口
        std::array<int, NumPieces> d2 {}, r2 {};
        d2[Kick] = Engine::OutKick;
        for (auto& x : r2) x = Engine::OutRoom;
        r2[Kick] = Engine::OutRoom;
        std::vector<std::vector<float>> o2;
        runMulti ({ { 0.01, 36, 110 }, { 3.0, 38, 110 } }, d2, r2, 0, o2);   // キックが鳴り終わってからスネア
        auto pk = [&] (const std::vector<float>& x, double from, double to) { float m = 0; for (size_t i = (size_t) (from * fs); i < (size_t) (to * fs) && i < x.size(); ++i) m = std::max (m, std::abs (x[i])); return 20 * std::log10 (m + 1e-12); };
        const double kickDuringKick = pk (o2[2 * Engine::OutKick], 0.0, 0.6), mainDuringKick = pk (o2[0], 0.0, 2.9);
        const double kickDuringSnare = pk (o2[2 * Engine::OutKick], 3.0, 3.6), roomDuringSnare = pk (o2[2 * Engine::OutRoom], 3.0, 3.6), mainDuringSnare = pk (o2[0], 3.0, 3.6);
        printf ("     スネアの間: Kick %.1f / Room %.1f / Main %.1f dBFS、キックの間: Kick %.1f / Main %.1f dBFS\n", kickDuringSnare, roomDuringSnare, mainDuringSnare, kickDuringKick, mainDuringKick);
        std::vector<std::vector<float>> o2k;   // キックだけ（スネアなし）と比べて、Kick の出口が全く同じならスネアは入っていない
        runMulti ({ { 0.01, 36, 110 }, { 3.0, 37, 1 } }, d2, r2, 0, o2k);
        float leak = 0; for (size_t i = 0; i < o2[2 * Engine::OutKick].size() && i < o2k[2 * Engine::OutKick].size(); ++i)
            leak = std::max (leak, std::abs (o2[2 * Engine::OutKick][i] - o2k[2 * Engine::OutKick][i]));
        printf ("     Kick の出口へのスネアの漏れ（キックだけとの差の最大）%.2e\n", leak);
        (void) kickDuringSnare;
        check (leak < 1e-7f && roomDuringSnare > -60 && mainDuringSnare > -40, "キックだけ分けた: スネアは Kick の出口に入らず、スネアのルームは Room の出口へ");
        check (kickDuringKick > -30 && mainDuringKick < -150, "キックは Kick の出口だけ（ルームは Room、Main には入らない）");
        // 鳴っている途中で出口を替えても、全部を足した音は 1 本と同じ（10 ms で移すだけ）
        std::vector<std::vector<float>> o3;
        runMulti (ev, direct, room, 1.03, o3);
        float d3 = 0; for (size_t i = 0; i < a.size() && i < o3[0].size(); ++i) { float sl = 0, sr = 0; for (int c = 0; c < K; ++c) { sl += o3[(size_t) (2 * c)][i]; sr += o3[(size_t) (2 * c + 1)][i]; } d3 = std::max ({ d3, std::abs (sl - a[i]), std::abs (sr - b[i]) }); }
        printf ("     鳴っている途中で全部の出口を替えた: 足した音と 1 本の差の最大 %.2e\n", d3);
        check (d3 < 1e-5f, "途中で出口を替えても、足した音は変わらない（15 ms で移す）");
    }
    // 11) ROOM を分けても、キックのルームの低音は BLEND どおり 808 に入れ替わる（Room の出口から生キックの低音が戻らない）
    {
        auto roomLow = [&] (float blend)
        {
            Settings st; st.kickBlend = blend;
            analyzeEdm (kit, st);
            auto e = std::make_unique<Engine>(); e->prepare (fs); e->setKit (&kit); e->setSettings (st);
            std::array<int, NumPieces> d {}, r {};
            for (auto& x : r) x = Engine::OutRoom;
            e->setRouting (d, r);
            constexpr int K = Engine::maxOuts;
            const size_t N = (size_t) (1.5 * fs);
            std::vector<std::vector<float>> o (2 * K, std::vector<float> (N, 0.0f));
            std::vector<float*> ptr (2 * K);
            e->noteOn (36, 110);
            for (size_t i = 0; i < N; i += 256)
            {
                for (int c = 0; c < 2 * K; ++c) ptr[(size_t) c] = o[(size_t) c].data() + i;
                e->render (ptr.data(), K, (int) std::min<size_t> (256, N - i));
            }
            Biquad h1, h2, l1, l2; h1.highpass (fs, 30, 0.707); h2 = h1; l1.lowpass (fs, 60, 0.707); l2 = l1;   // 分け目（100 Hz）より十分下
            double sum = 0; for (float v : o[2 * Engine::OutRoom]) { const float y = l2.run (l1.run (h2.run (h1.run (v)))); sum += (double) y * y; }
            return 10 * std::log10 (sum + 1e-30);
        };
        const double b0 = roomLow (0), b1 = roomLow (1);
        printf ("     Room の出口のキックの 30〜60 Hz: BLEND 0 %% %.1f dB → 100 %% %.1f dB\n", b0, b1);
        check (b1 < b0 - 10, "ROOM を分けても、キックのルームの低音（分け目より下）は BLEND 100 % で 10 dB 以上下がる（分け目がなければ 0 dB。808 に任せる）");
    }
    printf (fails ? "\n%d 個 NG\n" : "\n全部 OK\n", fails);
    return fails ? 1 : 0;
}

int main (int argc, char** argv)
{
    if (argc >= 2 && std::string (argv[1]) == "test") return test (argc >= 3 ? argv[2] : "Studio Kit");
    if (argc >= 3 && std::string (argv[1]) == "demo") return demo (argv[2], argc >= 4 ? argv[3] : "Studio Kit");
    fprintf (stderr, "使い方: render demo <出力フォルダ> [キット名] / render test [キット名]\n");
    return 2;
}
