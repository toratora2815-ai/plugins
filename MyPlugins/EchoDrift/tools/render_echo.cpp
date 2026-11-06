// Echo Drift のエンジンに WAV を通して書き出す（比べる用）。
//   render_echo in.wav out.wav 余韻秒 番号=値 ...
// 番号と値は、測った市販ディレイの AU と同じ（0..1 の正規化値、選択肢は番号そのまま）なので、
// 同じ測定スクリプトで両方を鳴らせる:
//   7 bypass 8 input 9 output 10 mix 14 mode 20 echo1 time 26 echo2 time 27 feedback 31 prime
//   33 groove 34 feel 35 saturation 36 low cut 37 high cut 39 rhythm mode 44 rhythm time
//   51 repeats 52 decay 64 style 124 tempo
// ビルド: c++ -O2 -std=c++17 -I../Source render_echo.cpp -o render_echo
#include "EchoEngine.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

static bool readWav (const char* path, std::vector<float>& L, std::vector<float>& R, int& sr)
{
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    std::vector<unsigned char> b; unsigned char tmp[65536]; size_t n;
    while ((n = std::fread (tmp, 1, sizeof (tmp), f)) > 0) b.insert (b.end(), tmp, tmp + n);
    std::fclose (f);
    auto u16 = [&] (size_t i) { return (int) (b[i] | (b[i + 1] << 8)); };
    auto u32 = [&] (size_t i) { return (uint32_t) (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | ((uint32_t) b[i + 3] << 24)); };
    int nc = 2, fmt = 3, bits = 32; size_t i = 12;
    while (i + 8 <= b.size())
    {
        const std::string id ((const char*) &b[i], 4); const uint32_t sz = u32 (i + 4);
        if (id == "fmt ") { fmt = u16 (i + 8); nc = u16 (i + 10); sr = (int) u32 (i + 12); bits = u16 (i + 22); }
        if (id == "data")
        {
            const size_t frames = sz / (size_t) (nc * bits / 8);
            L.resize (frames); R.resize (frames);
            for (size_t k = 0; k < frames; ++k)
                for (int c = 0; c < nc && c < 2; ++c)
                {
                    float v;
                    const size_t p = i + 8 + (k * nc + c) * (bits / 8);
                    if (fmt == 3) std::memcpy (&v, &b[p], 4);
                    else v = (float) (int16_t) u16 (p) / 32768.0f;
                    (c == 0 ? L : R)[k] = v;
                    if (nc == 1) R[k] = v;
                }
            return true;
        }
        i += 8 + sz;
    }
    return false;
}

static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path, "wb");
    const uint32_t n = (uint32_t) L.size(), bytes = n * 8;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (3); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (uint32_t k = 0; k < n; ++k) { std::fwrite (&L[k], 4, 1, f); std::fwrite (&R[k], 4, 1, f); }
    std::fclose (f);
}

static double timeMs (double x) { return 0.100279 * (std::exp (11.9149 * x) - 1.0); }

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: render_echo in.wav out.wav tail id=value...\n"); return 1; }
    std::vector<float> L, R; int sr = 48000;
    if (! readWav (argv[1], L, R, sr)) { std::fprintf (stderr, "read error\n"); return 1; }
    const double tail = std::atof (argv[3]);
    std::map<int, double> v { { 8, 0.5 }, { 9, 0.5 }, { 10, 0.5 }, { 14, 0 }, { 20, 0.6565 }, { 26, 0.6565 }, { 27, 0 }, { 31, 0 }, { 33, 0.5 }, { 34, 0.5 },
                              { 35, 0 }, { 36, 0 }, { 37, 0 }, { 39, 0 }, { 44, 0.5655 }, { 51, 7.0 / 15 }, { 52, 0 }, { 64, 1 }, { 124, 90.0 / 210 } };
    for (int a = 4; a < argc; ++a)
    {
        const std::string s (argv[a]); const auto e = s.find ('=');
        if (e != std::string::npos) v[std::atoi (s.substr (0, e).c_str())] = std::atof (s.substr (e + 1).c_str());
    }
    const char* wob = std::getenv ("WOBBLE");
    echo::Params p;
    const double bpm = 30 + 210 * v[124];
    p.mode = (int) v[14];
    p.time1Ms = timeMs (v[20]); p.time2Ms = timeMs (v[26]);
    p.feedback = (float) (v[27] * 1.25);
    p.mix = (float) v[10];
    p.inputDb = (float) ((v[8] - 0.5) * 48); p.outputDb = (float) ((v[9] - 0.5) * 48);
    p.saturationDb = (float) (v[35] * 24);
    p.lowCut = (float) v[36]; p.highCut = (float) v[37];
    p.groove = (float) ((v[33] - 0.5) * 0.5);
    p.feelMs = (v[34] - 0.5) * 0.5 * 60000.0 / bpm;
    p.prime = v[31] > 0.5;
    p.style = (int) v[64];
    // RHYTHM の時間（0..1 → 0..30000 ms。参考と同じ形の指数）
    p.rhythmMs = 0.0946 * (std::exp (12.67 * v[44]) - 1.0);
    p.repeats = 1 + (int) std::lround (v[51] * 15);
    p.decayDb = (float) (v[52] * 40);
    p.loopBeatsMs = 2 * 60000.0 / bpm;
    if (wob) p.wobble = (float) std::atof (wob);

    echo::Engine eng;
    eng.prepare (sr);
    eng.set (p);
    eng.reset();
    const size_t total = L.size() + (size_t) (tail * sr);
    L.resize (total, 0.0f); R.resize (total, 0.0f);
    for (size_t i = 0; i < total; i += 512)
    {
        const int n = (int) std::min<size_t> (512, total - i);
        eng.process (L.data() + i, R.data() + i, n);
    }
    writeWav (argv[2], L, R, sr);
    return 0;
}
