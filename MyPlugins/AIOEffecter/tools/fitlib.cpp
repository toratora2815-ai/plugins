// fitlib.cpp — 合わせ込み用の共有ライブラリ（Python の ctypes から呼ぶ）。部品 1 個に音を通す
//   clang++ -O2 -std=c++17 -shared -fPIC -I ../Source tools/fitlib.cpp -o libaiofx.dylib
//   ただし係数（FitAdd）は全部の呼び出しで共有。並列で呼ぶときは同じ係数で
#include "fx/Registry.h"
#include <cstring>
#include <mutex>

using namespace aiofx;

extern "C" {
// 係数を書き換える。見つからなければ 0
int aiofx_set_fit (const char* name, float v)
{
    if (float* p = findFit (name)) { *p = v; return 1; }
    return 0;
}
float aiofx_get_fit (const char* name)
{
    if (float* p = findFit (name)) return *p;
    return -1.0e30f;
}
int aiofx_num_fit() { return (int) fitEntries().size(); }
const char* aiofx_fit_name (int i) { return (i >= 0 && i < (int) fitEntries().size()) ? fitEntries()[(size_t) i].name : ""; }

// io: n サンプルのステレオ交互（その場で書き換え）。kIdx / kVal: つまみ。初期値から始めて、渡したつまみだけ変える
int aiofx_render (const char* id, float* io, int n, const int* kIdx, const float* kVal, int nk, double tempo)
{
    const int type = typeForId (id);
    if (type == 0) return -1;
    auto m = makeType (type);
    const int block = 256;
    const auto& inf = *infoFor (type);
    for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, inf.knobs[(size_t) k].def);
    for (int i = 0; i < nk; ++i) m->setKnob (kIdx[i], kVal[i]);
    m->prepare (48000.0, block);
    for (int i = 0; i < nk; ++i) m->setKnob (kIdx[i], kVal[i]);
    m->setTempo (tempo);
    m->reset();
    float L[block], R[block];
    for (int off = 0; off < n; off += block)
    {
        const int b = std::min (block, n - off);
        for (int i = 0; i < b; ++i) { L[i] = io[2 * (off + i)]; R[i] = io[2 * (off + i) + 1]; }
        m->process (L, R, b);
        for (int i = 0; i < b; ++i) { io[2 * (off + i)] = L[i]; io[2 * (off + i) + 1] = R[i]; }
    }
    return m->latency();
}
}

// 抑える系の共通エンジンに、エンジンの値（DynP の並び、dpCount 個）を直接入れて通す（部品の表を作る前の合わせ込み用）
#include "fx/DynEngine.h"
extern "C" int aiofx_dyn_count() { return dpCount; }
extern "C" int aiofx_render_dyn (const float* prm, float* io, int n)
{
    DynSpec s {};
    for (int i = 0; i < dpCount; ++i) s.base[i] = prm[i];
    s.nKnobs = 0;
    DynEngine e (s);
    const int block = 256;
    e.prepare (48000.0, block);
    e.reset();
    float L[block], R[block];
    for (int off = 0; off < n; off += block)
    {
        const int b = std::min (block, n - off);
        for (int i = 0; i < b; ++i) { L[i] = io[2 * (off + i)]; R[i] = io[2 * (off + i) + 1]; }
        e.process (L, R, b);
        for (int i = 0; i < b; ++i) { io[2 * (off + i)] = L[i]; io[2 * (off + i) + 1] = R[i]; }
    }
    return e.latency();
}
