// SketchEngine の確認用（JUCE なし）
//   clang++ -std=c++17 -O2 -I../Source test_engine.cpp -o /tmp/test_engine && /tmp/test_engine [wav フォルダ]
// 1. 作った「本物の音」と「口マネの声」で、口マネから正しい種類が 1 位に来るか
// 2. フォルダを渡すと、その中の wav を読んで、口マネごとの上位 5 個を出す
#include "SketchEngine.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <random>
#include <sys/stat.h>

using namespace sketch;
constexpr double FS = 44100;
std::mt19937 rng (1);
float noise() { return std::uniform_real_distribution<float> (-1, 1) (rng); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bp (double f, double q) { const double w = 2 * M_PI * f / FS, al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos (w) / a0; a2 = (1 - al) / a0; }
    void lp (double f, double q) { const double w = 2 * M_PI * f / FS, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp (double f, double q) { const double w = 2 * M_PI * f / FS, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    float run (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};

using Gen = std::function<float (double t)>;
std::vector<float> make (double secs, Gen g, double pre = 0.05)
{
    std::vector<float> v ((size_t) ((secs + pre + 0.1) * FS), 0.0f);
    for (size_t i = (size_t) (pre * FS); i < v.size(); ++i) v[i] = g ((double) i / FS - pre);
    return v;
}

// 声っぽい音（のこぎり波を声の帯域に丸める）
std::vector<float> voiced (double secs, std::function<double (double)> f0, std::function<double (double)> amp, double formant, double noiseAmt = 0.03)
{
    Biquad lp; lp.lp (formant, 0.9);
    Biquad f2; f2.bp (formant * 2.5, 2.0);
    double ph = 0;
    return make (secs, [=] (double t) mutable {
        ph += f0 (t) / FS; ph -= std::floor (ph);
        const float saw = (float) (2 * ph - 1) + noiseAmt * noise();
        return (float) (amp (t) * (lp.run (saw) + 0.4f * f2.run (saw)));
    });
}
std::vector<float> noiseBand (double secs, double f, double q, std::function<double (double)> amp, std::function<double (double)> fMove = nullptr)
{
    Biquad b; b.bp (f, q);
    int cnt = 0;
    return make (secs, [=] (double t) mutable {
        if (fMove && (cnt++ % 64) == 0) b.bp (fMove (t), q);
        return (float) (amp (t) * 4.0 * b.run (noise()));
    });
}
auto dec = [] (double tau, double att = 0.002) { return [=] (double t) { return t < 0 ? 0.0 : std::min (1.0, t / att) * std::exp (-t / tau); }; };

struct Item { std::string name; Features f; };

Features feat (const std::vector<float>& v) { return analyse (v.data(), (int) v.size(), FS); }

int main (int argc, char** argv)
{
    // ---- 本物の音（の代わり）
    std::vector<Item> lib;
    {
        double ph = 0;
        lib.push_back ({ "kick", feat (make (0.6, [&] (double t) { ph += (50 + 130 * std::exp (-t / 0.03)) / FS; return (float) (std::sin (2 * M_PI * ph) * dec (0.25) (t)); })) });
        Biquad hp1, hp2; hp1.hp (7000, 0.7); hp2.hp (7000, 0.7);
        lib.push_back ({ "hat closed", feat (make (0.15, [&] (double t) { return (float) (hp2.run (hp1.run (noise())) * dec (0.035) (t)); })) });
        Biquad hp3, hp4; hp3.hp (6000, 0.7); hp4.hp (6000, 0.7);
        lib.push_back ({ "hat open", feat (make (0.8, [&] (double t) { return (float) (hp4.run (hp3.run (noise())) * dec (0.3) (t)); })) });
        Biquad sb; sb.bp (2500, 0.6);
        lib.push_back ({ "snare", feat (make (0.4, [&] (double t) { return (float) (0.7 * sb.run (noise()) * 3 * dec (0.11) (t) + 0.5 * std::sin (2 * M_PI * 190 * t) * dec (0.05) (t)); })) });
        Biquad cb; cb.bp (1400, 1.5);
        lib.push_back ({ "clap", feat (make (0.35, [&] (double t) { const double e = t < 0.03 ? (std::fmod (t, 0.01) < 0.004 ? 1.0 : 0.2) : std::exp (-(t - 0.03) / 0.1); return (float) (cb.run (noise()) * 4 * e); })) });
        Biquad bl; bl.lp (400, 0.8); double bph = 0;
        lib.push_back ({ "bass", feat (make (1.3, [&] (double t) { bph += 55 / FS; bph -= std::floor (bph); return (float) (bl.run ((float) (2 * bph - 1)) * (t < 1.1 ? std::min (1.0, t / 0.01) : std::exp (-(t - 1.1) / 0.05))); })) });
        lib.push_back ({ "riser", feat (noiseBand (2.0, 800, 3, [] (double t) { return 0.2 + 0.4 * t; }, [] (double t) { return 800 * std::pow (2.0, t * 1.7); })) });
        double tph = 0;
        lib.push_back ({ "tom", feat (make (0.5, [&] (double t) { tph += (110 + 60 * std::exp (-t / 0.05)) / FS; return (float) (std::sin (2 * M_PI * tph) * dec (0.18) (t)); })) });
    }

    // ---- 口マネ
    struct Q { const char* say; const char* want; std::vector<float> v; };
    std::vector<Q> qs;
    qs.push_back ({ "ドゥン", "kick", voiced (0.4, [] (double t) { return 150 - 50 * std::min (1.0, t / 0.15); }, dec (0.15, 0.008), 700) });
    qs.push_back ({ "ツッ", "hat closed", noiseBand (0.12, 6000, 1.2, dec (0.04, 0.003)) });
    qs.push_back ({ "ツー", "hat open", noiseBand (0.7, 6000, 1.2, dec (0.3, 0.004)) });
    qs.push_back ({ "パッ", "snare", [] { auto a = noiseBand (0.3, 2200, 0.8, dec (0.1, 0.003));
                                         auto b = voiced (0.3, [] (double) { return 180.0; }, dec (0.05, 0.004), 1200);
                                         for (size_t i = 0; i < a.size(); ++i) a[i] += 0.5f * b[i]; return a; }() });
    qs.push_back ({ "ブーーン", "bass", voiced (1.3, [] (double) { return 95.0; }, [] (double t) { return t < 1.1 ? std::min (1.0, t / 0.02) : std::exp (-(t - 1.1) / 0.06); }, 500) });
    qs.push_back ({ "シューッ↑", "riser", noiseBand (1.8, 1500, 2.5, [] (double t) { return 0.2 + 0.4 * t; }, [] (double t) { return 1500 * std::pow (2.0, t * 1.6); }) });

    int ok = 0;
    for (auto& q : qs)
    {
        const auto f = voiceToLibrary (feat (q.v));
        std::vector<std::pair<float, std::string>> r;
        for (auto& it : lib) r.push_back ({ distance (f, it.f), it.name });
        std::sort (r.begin(), r.end());
        const bool hit = r[0].second == q.want;
        ok += hit;
        std::printf ("%-12s want %-11s -> %s", q.say, q.want, hit ? "OK " : "NG ");
        for (int i = 0; i < 3; ++i) std::printf (" %s(%d%%)", r[(size_t) i].second.c_str(), matchPercent (r[(size_t) i].first));
        std::printf ("\n");
    }
    std::printf ("synthetic: %d / %d\n", ok, (int) qs.size());

    // ---- 種類の当て方
    struct K { const char* p; float s; uint32_t want; };
    const K ks[] { { "/packs/x/one_shots/kicks/fss_kick_feel.wav", 0.5f, 1u << kKick },
                   { "/packs/x/mo_wc_vocal_adlib_what.wav", 0.6f, 1u << kVox },
                   { "/packs/x/hh_closed_01.wav", 0.1f, 1u << kHat },
                   { "/packs/x/drum_loop_120.wav", 8.0f, 1u << kLoop } };
    int kok = 0;
    for (auto& k : ks) { const auto g = guessKinds (k.p, k.s); const bool h = (g & k.want) != 0 && ! (k.want != (1u << kHat) && (g & (1u << kHat))); kok += h; if (! h) std::printf ("kind NG: %s -> %x\n", k.p, g); }
    std::printf ("kinds: %d / 4\n", kok);

    // ---- 本物のフォルダ
    if (argc > 1)
    {
        std::vector<Item> real;
        std::function<void (const std::string&)> walk = [&] (const std::string& d) {
            DIR* dir = opendir (d.c_str()); if (! dir) return;
            while (auto* e = readdir (dir))
            {
                if (e->d_name[0] == '.') continue;
                const std::string p = d + "/" + e->d_name;
                struct stat st; if (stat (p.c_str(), &st) != 0) continue;
                if (S_ISDIR (st.st_mode)) { walk (p); continue; }
                if (p.size() < 4 || strcasecmp (p.c_str() + p.size() - 4, ".wav") != 0) continue;
                std::ifstream in (p, std::ios::binary); std::vector<char> b ((std::istreambuf_iterator<char> (in)), {});
                if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) != 0) continue;
                int ch = 0, bits = 0, fmt = 0; double sr = 0; size_t pos = 12; const char* data = nullptr; size_t dlen = 0;
                while (pos + 8 <= b.size())
                {
                    uint32_t len; std::memcpy (&len, &b[pos + 4], 4);
                    if (! std::memcmp (&b[pos], "fmt ", 4)) { uint16_t f, c; uint32_t s; uint16_t bb; std::memcpy (&f, &b[pos + 8], 2); std::memcpy (&c, &b[pos + 10], 2); std::memcpy (&s, &b[pos + 12], 4); std::memcpy (&bb, &b[pos + 22], 2); fmt = f; ch = c; sr = s; bits = bb; if (fmt == 0xFFFE && len >= 26) { uint16_t sub; std::memcpy (&sub, &b[pos + 32], 2); fmt = sub; } }
                    if (! std::memcmp (&b[pos], "data", 4)) { data = &b[pos + 8]; dlen = std::min<size_t> (len, b.size() - pos - 8); break; }
                    pos += 8 + len + (len & 1);
                }
                if (! data || ch < 1 || (bits != 16 && bits != 24 && bits != 32)) continue;
                const int bps = bits / 8; const size_t n = dlen / (size_t) (bps * ch);
                std::vector<float> m (n, 0.0f);
                for (size_t i = 0; i < n; ++i) for (int c = 0; c < ch; ++c)
                {
                    const char* q = data + (i * ch + c) * bps; float v = 0;
                    if (bits == 16) { int16_t s; std::memcpy (&s, q, 2); v = s / 32768.f; }
                    else if (bits == 24) { int s = (int) ((uint8_t) q[0] | ((uint8_t) q[1] << 8) | ((int8_t) q[2] << 16)); v = s / 8388608.f; }
                    else if (fmt == 3) std::memcpy (&v, q, 4);
                    else { int32_t s; std::memcpy (&s, q, 4); v = s / 2147483648.f; }
                    m[i] += v / ch;
                }
                auto f = analyse (m.data(), (int) n, sr);
                if (f.valid) real.push_back ({ p.substr (p.find_last_of ('/') + 1), f });
            }
            closedir (dir);
        };
        walk (argv[1]);
        std::printf ("\nreal library: %d files\n", (int) real.size());
        for (auto& q : qs)
        {
            const auto f = voiceToLibrary (feat (q.v));
            std::vector<std::pair<float, std::string>> r;
            for (auto& it : real) r.push_back ({ distance (f, it.f), it.name });
            std::sort (r.begin(), r.end());
            std::printf ("%s:\n", q.say);
            for (int i = 0; i < 5 && i < (int) r.size(); ++i) std::printf ("   %3d%%  %s\n", matchPercent (r[(size_t) i].first), r[(size_t) i].second.c_str());
        }
    }
    return ok == (int) qs.size() && kok == 4 ? 0 : 1;
}
