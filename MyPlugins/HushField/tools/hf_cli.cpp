// 測定用: hf_cli in.f32 out.f32 [名前=値 ...]
//   in/out は 48 kHz ステレオ float32（インターリーブ）。名前は soothe3 の測定と同じ 0..1 の値（Depth=0.5 → 5.0）
//   T.xxx=値 で Tuning を上書き（合わせ込み用）
#include "../Source/HushEngine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage\n"); return 1; }
    FILE* f = std::fopen (argv[1], "rb"); if (! f) return 1;
    std::fseek (f, 0, SEEK_END); long bytes = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    std::vector<float> in ((size_t) bytes / 4); std::fread (in.data(), 4, in.size(), f); std::fclose (f);
    const int n = (int) in.size() / 2;
    hf::Settings s;
    // 既定の帯域（soothe3 の初期状態と同じ並び）
    s.bands[0] = { true, true, hf::LowCut, 100.0f, 0.7f, 0.0f, 5.0f, 0.5f };
    s.bands[1] = { true, true, hf::Bell, 4000.0f, 0.7f, 2.5f, 5.0f, 0.5f };
    s.bands[2] = { true, true, hf::HighCut, 16000.0f, 0.7f, 0.0f, 5.0f, 0.5f };
    auto& T = hf::tuning();
    std::map<std::string, float*> tm {
        {"fineOct0",&T.fineOct0},{"fineOct10",&T.fineOct10},{"coarseOct",&T.coarseOct},{"coarseMul",&T.coarseMul},{"emph",&T.emph},{"lvlK",&T.lvlK},{"lvlRef",&T.lvlRef},
        {"thr0",&T.thr0},{"thr1",&T.thr1},{"thrHard",&T.thrHard},{"slope",&T.slope},{"gamma",&T.gamma},
        {"hardMul",&T.hardMul},{"knee",&T.knee},{"detSlope",&T.detSlope},{"hardDet",&T.hardDet},{"sat0",&T.sat0},{"satDet",&T.satDet},{"atkBase",&T.atkBase},{"atkK",&T.atkK},{"relBase",&T.relBase},{"relK",&T.relK}};
    for (int a = 3; a < argc; ++a)
    {
        std::string kv = argv[a]; auto eq = kv.find ('='); std::string k = kv.substr (0, eq); float v = std::stof (kv.substr (eq + 1));
        if (k.rfind ("T.", 0) == 0) { auto it = tm.find (k.substr (2)); if (it == tm.end()) { std::fprintf (stderr, "no tuning %s\n", k.c_str()); return 2; } *it->second = v; continue; }
        if (k == "Mode") s.hard = v > 0.5f;
        else if (k == "Depth") s.depth = v * 10;
        else if (k == "Detail") s.detail = v * 10;
        else if (k == "Attack") s.attack = v * 10;
        else if (k == "Release") s.release = v * 10;
        else if (k == "Mix") s.mix = v;
        else if (k == "Max cut") s.maxCutDb = v >= 0.999f ? 1000.0f : v * 21.6f;
        else if (k == "Low latency") s.lowLatency = v > 0.5f;
        else if (k == "Linear phase") s.linearPhase = v > 0.5f;
        else if (k == "Quality") s.quality = (int) v;
        else if (k == "Stereo mode") s.midSide = v > 0.5f;
        else if (k == "Stereo link") s.stereoLink = v;
        else if (k == "Delta") s.delta = v > 0.5f;
        else if (k.rfind ("Band ", 0) == 0)
        {
            int b = k[5] - '1'; std::string what = k.substr (7);
            auto& bd = s.bands[b];
            if (what == "enabled") { bd.enabled = v > 0.5f; if (bd.enabled) bd.used = true; }
            else if (what == "shape") { bd.shape = (int) v; bd.used = true; }
            else if (what == "frequency") bd.hz = hf::posToHz (v);
            else if (what == "depth") bd.gain = (v - 0.5f) * 20.0f;
            else if (what == "slope") bd.slope = v * 10.0f;
            else if (what == "q") { const float qs[] = {0.1f,0.1f,0.2f,0.3f,0.4f,0.7f,1.2f,1.9f,3.3f,5.7f,10.0f}; float p = v * 10; int i = std::min (9, (int) p); bd.q = qs[i] + (p - i) * (qs[i + 1] - qs[i]); }
            else if (what == "focus") bd.focus = v;
        }
        else { std::fprintf (stderr, "unknown %s\n", k.c_str()); return 3; }
    }
    hf::HushEngine e; e.prepare (48000.0, 512);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i) { l[(size_t) i] = in[(size_t) (2 * i)]; r[(size_t) i] = in[(size_t) (2 * i + 1)]; }
    for (int pos = 0; pos < n; pos += 512)
    {
        int m = std::min (512, n - pos);
        float* io[2] { l.data() + pos, r.data() + pos };
        e.process (io, 2, nullptr, 0, m, s);
    }
    std::vector<float> out ((size_t) n * 2);
    for (int i = 0; i < n; ++i) { out[(size_t) (2 * i)] = l[(size_t) i]; out[(size_t) (2 * i + 1)] = r[(size_t) i]; }
    FILE* o = std::fopen (argv[2], "wb"); std::fwrite (out.data(), 4, out.size(), o); std::fclose (o);
    return 0;
}
