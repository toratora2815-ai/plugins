// render.cpp — AIO Effecter の部品 1 個に音を通す（JUCE なし）。Guitar Rig の記録と比べて合わせ込むための道具
//   clang++ -O2 -std=c++17 -I ../Source tools/render.cpp -o render
//   ./render <部品 id> in.f32 out.f32 [k0=値 k1=値 ...] [F.名前=値 ...]
//   in / out は 48 kHz ステレオ交互の float。k = つまみ（0..1、Guitar Rig と同じ尺度）、F = TubeComp の係数
#include "fx/Registry.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

using namespace aiofx;

static void setFit (const std::string& name, float v)
{
    auto& F = TubeComp::fit();
    std::map<std::string, float*> m {
        { "drv0", &F.drv0 }, { "drv1", &F.drv1 }, { "satCeil", &F.satCeil }, { "out0", &F.out0 },
        { "thr0", &F.thr0 }, { "thr1", &F.thr1 }, { "rat1", &F.rat1 }, { "ratP", &F.ratP }, { "knee1", &F.knee1 },
        { "att0", &F.att0 }, { "att1", &F.att1 }, { "rel0", &F.rel0 }, { "rel1", &F.rel1 }, { "dyn1", &F.dyn1 },
        { "detRms", &F.detRms }, { "satSoft", &F.satSoft }, { "satMix", &F.satMix } };
    auto& N = NoiseReduction::fit();
    m["scHz"] = &N.scHz; m["ldDb"] = &N.ldDb; m["ldPow"] = &N.ldPow; m["fcPow"] = &N.fcPow;
    auto it = m.find (name);
    if (it != m.end()) { *it->second = v; return; }
    if (float* q = findFit (name)) { *q = v; return; }
    std::cerr << "unknown fit " << name << "\n"; std::exit (2);
}

int main (int argc, char** argv)
{
    if (argc < 4) { std::cerr << "usage: render <id> in.f32 out.f32 [k0=v ...] [F.name=v ...]\n"; return 1; }
    const int type = typeForId (argv[1]);
    if (type == 0) { std::cerr << "unknown module " << argv[1] << "\n"; return 1; }
    std::ifstream f (argv[2], std::ios::binary | std::ios::ate);
    const size_t bytes = (size_t) f.tellg(); f.seekg (0);
    std::vector<float> io (bytes / sizeof (float));
    f.read ((char*) io.data(), (std::streamsize) bytes);
    const size_t n = io.size() / 2;
    std::vector<std::pair<int, float>> knobs;
    for (int a = 4; a < argc; ++a)
    {
        std::string s = argv[a];
        const auto eq = s.find ('=');
        if (s.rfind ("F.", 0) == 0) { setFit (s.substr (2, eq - 2), std::stof (s.substr (eq + 1))); continue; }
        if (s[0] == 'k') knobs.push_back ({ std::stoi (s.substr (1, eq - 1)), std::stof (s.substr (eq + 1)) });
    }
    auto m = makeType (type);
    const int block = 256;
    m->prepare (48000.0, block);
    const auto& inf = *infoFor (type);
    for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, inf.knobs[(size_t) k].def);
    for (auto& kv : knobs) m->setKnob (kv.first, kv.second);
    m->prepare (48000.0, block);   // 係数を初期値のまま確定（なめらかさの途中から始めない）
    for (auto& kv : knobs) m->setKnob (kv.first, kv.second);
    m->reset();
    std::vector<float> L (block), R (block);
    for (size_t off = 0; off < n; off += block)
    {
        const int b = (int) std::min ((size_t) block, n - off);
        for (int i = 0; i < b; ++i) { L[(size_t) i] = io[2 * (off + i)]; R[(size_t) i] = io[2 * (off + i) + 1]; }
        m->process (L.data(), R.data(), b);
        for (int i = 0; i < b; ++i) { io[2 * (off + i)] = L[(size_t) i]; io[2 * (off + i) + 1] = R[(size_t) i]; }
    }
    std::ofstream o (argv[3], std::ios::binary);
    o.write ((const char*) io.data(), (std::streamsize) (io.size() * sizeof (float)));
    return 0;
}
