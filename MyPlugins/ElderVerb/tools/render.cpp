// テスト用：Elder Verb のエンジンだけで WAV を書き出す（JUCE なし）
// 使い方: render in.wav out.wav 余韻秒 Name=値 ... （値は 0..1。p_<項目>=値 でモードの性格を上書き）
// 名前は Mix PreDelay Decay Size Attack BassMult BassXover HighShelf HighFreq EarlyDiffusion LateDiffusion
//        ModRate ModDepth HighCut LowCut ColorMode ReverbMode（比べる相手と同じ 0..1 の値）
#include "../Source/ElderEngine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
using namespace elder;

static bool readWav (const char* path, std::vector<float>& L, std::vector<float>& R, int& sr)
{
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    char id[4]; uint32_t sz; std::fseek (f, 12, SEEK_SET); int ch = 2, bits = 32, fmt = 3;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (! std::memcmp (id, "fmt ", 4)) { uint16_t fm, c; uint32_t rate; std::fread (&fm, 2, 1, f); std::fread (&c, 2, 1, f); std::fread (&rate, 4, 1, f);
            std::fseek (f, 6, SEEK_CUR); uint16_t b; std::fread (&b, 2, 1, f); fmt = fm; ch = c; sr = (int) rate; bits = b; std::fseek (f, (long) sz - 16, SEEK_CUR); }
        else if (! std::memcmp (id, "data", 4))
        {
            const size_t n = sz / (size_t) (bits / 8) / (size_t) ch; L.resize (n); R.resize (n);
            for (size_t i = 0; i < n; ++i) for (int c = 0; c < ch; ++c)
            {
                float v = 0;
                if (fmt == 3 || fmt == 0xFFFE) { if (bits == 32) std::fread (&v, 4, 1, f); else { double d; std::fread (&d, 8, 1, f); v = (float) d; } }
                else { int16_t s; std::fread (&s, 2, 1, f); v = s / 32768.0f; }
                if (c == 0) L[i] = v; if (c == 1 || ch == 1) R[i] = v;
            }
            break;
        }
        else std::fseek (f, (long) (sz + (sz & 1)), SEEK_CUR);
    }
    std::fclose (f); return true;
}
static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path, "wb"); uint32_t n = (uint32_t) L.size(), data = n * 8;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (3); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "render in.wav out.wav tail key=val...\n"); return 1; }
    std::vector<float> L, R; int sr = 48000;
    if (! readWav (argv[1], L, R, sr)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
    const size_t tail = (size_t) (std::atof (argv[3]) * sr);
    L.resize (L.size() + tail, 0.0f); R.resize (R.size() + tail, 0.0f);

    Params p; std::map<std::string, float> prof;
    for (int i = 4; i < argc; ++i)
    {
        std::string a = argv[i]; auto q = a.find ('='); if (q == std::string::npos) continue;
        const std::string k = a.substr (0, q); const float v = (float) std::atof (a.substr (q + 1).c_str());
        if (k == "Mix") p.mix = v; else if (k == "PreDelay") p.predelay = v; else if (k == "Decay") p.decay = v; else if (k == "Size") p.size = v;
        else if (k == "Attack") p.attack = v; else if (k == "BassMult") p.bassMult = v; else if (k == "BassXover") p.bassXover = v;
        else if (k == "HighShelf") p.highShelf = v; else if (k == "HighFreq") p.highFreq = v; else if (k == "EarlyDiffusion") p.earlyDiff = v;
        else if (k == "LateDiffusion") p.lateDiff = v; else if (k == "ModRate") p.modRate = v; else if (k == "ModDepth") p.modDepth = v;
        else if (k == "HighCut") p.highCut = v; else if (k == "LowCut") p.lowCut = v;
        else if (k == "ColorMode") p.color = v < 0.6666f ? 0 : (v < 0.9999f ? 1 : 2);
        else if (k == "ReverbMode") p.mode = std::clamp ((int) std::lround (v * 24) - 1, 0, kNumModes - 1);
        else if (k.rfind ("p_", 0) == 0) prof[k.substr (2)] = v;
    }
    Engine e;
    for (auto& [k, v] : prof) if (! e.setProfileField (p.mode, k.c_str(), v)) { std::fprintf (stderr, "unknown profile field %s\n", k.c_str()); return 1; }
    e.prepare (sr);
    e.setParams (p);
    std::vector<float> oL (L.size()), oR (L.size());
    const int B = 512;
    for (size_t i = 0; i < L.size(); i += B)
    {
        const int n = (int) std::min ((size_t) B, L.size() - i);
        e.process (L.data() + i, R.data() + i, oL.data() + i, oR.data() + i, n);
    }
    writeWav (argv[2], oL, oR, sr);
    return 0;
}
