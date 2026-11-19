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

// 響き系の共通エンジンに、エンジンの値（VerbP の並び、vpCount 個）を直接入れて通す
#include "fx/VerbEngine.h"
extern "C" int aiofx_verb_count() { return vpCount; }
extern "C" int aiofx_render_verb (const float* prm, float* io, int n)
{
    static thread_local VerbSpec s {};
    for (int i = 0; i < vpCount; ++i) s.base[i] = prm[i];
    s.nKnobs = 0;
    VerbEngine e (s);
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
    return 0;
}

// 響きの特徴（本物の記録とエンジンの両方を同じ式で測る）。ir: ステレオ交互、n フレーム、ドライを抜いた響きだけ（インパルス 1 が 0 サンプル目）
// out[0] 響きの始まり ms、[1] 響きの量 dB、[2] 最初の 50 ms の割合、[3] 左右の相関（50〜500 ms）、[4..10] T60（125 Hz〜8 kHz のオクターブ）、[11..19] 音色（63 Hz〜16 kHz、量との差 dB）
namespace
{
struct BandPass
{
    Biquad a, b;
    void set (double sr, double f) { setOne (a, sr, f); setOne (b, sr, f); }
    static void setOne (Biquad& q, double sr, double f)
    {
        const double w = 2 * kPiD * std::min (f, 0.45 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * 1.2);
        q.setNorm (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    float process (float x) { return b.process (a.process (x)); }
};
double edcT60 (const std::vector<double>& e, int n, double sr)
{
    const int tailN = std::min (n / 4, (int) (0.5 * sr));
    double floor = 0; for (int i = n - tailN; i < n; ++i) floor += e[(size_t) i]; floor /= std::max (1, tailN);
    std::vector<double> c ((size_t) n);
    double acc = 0;
    for (int i = n - 1; i >= 0; --i) { acc += std::max (0.0, e[(size_t) i] - floor); c[(size_t) i] = acc; }
    if (c[0] <= 0) return std::nan ("");
    const double c0 = c[0];
    for (auto& v : c) v = 10 * std::log10 (v / c0 + 1e-30);
    int i5 = 0; while (i5 < n && c[(size_t) i5] > -5) ++i5;
    for (double lo : { -35.0, -25.0, -15.0 })
    {
        int il = i5; while (il < n && c[(size_t) il] > lo) ++il;
        if (il < n && il > i5 + 10)
        {
            // 直線の当てはめ（最小二乗）
            double sx = 0, sy = 0, sxx = 0, sxy = 0; const int m = il - i5;
            for (int i = i5; i < il; ++i) { const double t = (i - i5) / sr, y = c[(size_t) i]; sx += t; sy += y; sxx += t * t; sxy += t * y; }
            const double k = (m * sxy - sx * sy) / (m * sxx - sx * sx);
            return k < 0 ? -60.0 / k : std::nan ("");
        }
    }
    return std::nan ("");
}
}
extern "C" int aiofx_verb_features (const float* ir, int n, float* out)
{
    const double sr = 48000.0;
    double E = 0; for (int i = 0; i < 2 * n; ++i) E += (double) ir[i] * ir[i];
    // 響きの始まり: 2 ms ごとのエネルギーが、いちばん大きい 2 ms の −20 dB に届いた所
    //   （ドライの通り道のゆっくり減る小さな尾（−40 dB くらい）や、ドライを抜いた残りのとげに引っぱられない）
    int first = 0;
    {
        const int w = 96; std::vector<double> env ((size_t) n, 0.0); double acc = 0, mxe = 0;
        for (int i = 0; i < n; ++i)
        {
            acc += (double) ir[2 * i] * ir[2 * i] + (double) ir[2 * i + 1] * ir[2 * i + 1];
            if (i >= w) acc -= (double) ir[2 * (i - w)] * ir[2 * (i - w)] + (double) ir[2 * (i - w) + 1] * ir[2 * (i - w) + 1];
            env[(size_t) i] = acc; mxe = std::max (mxe, acc);
        }
        while (first < n && env[(size_t) first] < 0.01 * mxe) ++first;
        first = std::max (0, first - w / 2);
    }
    E *= 0.5;
    out[0] = (float) (first / sr * 1000.0);
    out[1] = (float) (10 * std::log10 (E + 1e-30));
    double eEarly = 0; const int e50 = std::min (n, first + (int) (0.05 * sr));
    for (int i = first; i < e50; ++i) eEarly += 0.5 * ((double) ir[2 * i] * ir[2 * i] + (double) ir[2 * i + 1] * ir[2 * i + 1]);
    out[2] = (float) (eEarly / (E + 1e-30));
    {
        const int a = std::min (n, first + (int) (0.05 * sr)), b = std::min (n, first + (int) (0.5 * sr));
        double sl = 0, sr2 = 0, slr = 0, ml = 0, mr = 0; const int m = std::max (1, b - a);
        for (int i = a; i < b; ++i) { ml += ir[2 * i]; mr += ir[2 * i + 1]; }
        ml /= m; mr /= m;
        for (int i = a; i < b; ++i) { const double l = ir[2 * i] - ml, r = ir[2 * i + 1] - mr; sl += l * l; sr2 += r * r; slr += l * r; }
        out[3] = (sl > 0 && sr2 > 0) ? (float) (slr / std::sqrt (sl * sr2)) : 1.0f;
    }
    static const double bands[7] { 125, 250, 500, 1000, 2000, 4000, 8000 };
    static const double tone[9] { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    std::vector<double> e ((size_t) n);
    for (int bI = 0; bI < 7; ++bI)
    {
        BandPass fl, fr; fl.set (sr, bands[bI]); fr.set (sr, bands[bI]);
        for (int i = 0; i < n; ++i) { const double l = fl.process (ir[2 * i]), r = fr.process (ir[2 * i + 1]); e[(size_t) i] = l * l + r * r; }
        out[4 + bI] = (float) edcT60 (e, n, sr);
    }
    for (int bI = 0; bI < 9; ++bI)
    {
        BandPass fl, fr; fl.set (sr, tone[bI]); fr.set (sr, tone[bI]);
        double eb = 0;
        for (int i = 0; i < n; ++i) { const double l = fl.process (ir[2 * i]), r = fr.process (ir[2 * i + 1]); eb += 0.5 * (l * l + r * r); }
        out[11 + bI] = (float) (10 * std::log10 (eb + 1e-30) - out[1]);
    }
    return 20;
}
