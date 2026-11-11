// テスト・当てはめ用の C の入口（build/rclab から ctypes で使う）。プラグインには入らない
#include "../Source/RetroEngine.h"
using namespace rr;
static Knobs neutralKnobs()
{
    Knobs k; k.noiseAmt = k.wobAmt = k.distAmt = k.digAmt = k.spaceAmt = k.lossAmt = k.echoAmt = 0; k.eqOn = false; k.compress = 0; return k;
}
// 名前=値 の並び（"distAmt=0.5,distType=3"）でつまみを変える
static void applyKV (Knobs& k, const char* kv);
extern "C" {
// x: 左右交互、n フレーム。y も同じ。neutral=1 なら本物の測定と同じ「全部 0・EQ 切・コンプ 0」から
int rr_render (const float* x, float* y, int n, double sr, const char* kv, int neutral, double bpm)
{
    static Engine* e = nullptr; if (! e) e = new Engine();
    e->prepare (sr, 512);
    Knobs k = neutral ? neutralKnobs() : Knobs();
    applyKV (k, kv);
    Transport tr; tr.bpm = bpm; tr.playing = true;
    std::vector<float> L (512), R (512);
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        for (int i = 0; i < m; ++i) { L[(size_t) i] = x[2 * (pos + i)]; R[(size_t) i] = x[2 * (pos + i) + 1]; }
        float* io[2] = { L.data(), R.data() };
        e->process (io, 2, m, k, tr);
        for (int i = 0; i < m; ++i) { y[2 * (pos + i)] = L[(size_t) i]; y[2 * (pos + i) + 1] = R[(size_t) i]; }
    }
    return e->latency();
}
// 歪みの数値を差し替える（当てはめ用）
void rr_set_dist (int type, const float* v)
{
    auto& m = const_cast<DistModel&> (distModels()[(size_t) type]);
    std::memcpy (&m, v, sizeof (DistModel));
}
void rr_set_space_trim (int type, float g) { const_cast<std::array<float, 10>&> (Space::spaceGainTrim())[(size_t) type] = g; }
}
#include <string>
#include <map>
#include <functional>
static void applyKV (Knobs& k, const char* kvs)
{
    std::map<std::string, std::function<void (float)>> m {
#define F(n) { #n, [&] (float v) { k.n = v; } },
#define I(n) { #n, [&] (float v) { k.n = (int) v; } },
#define B(n) { #n, [&] (float v) { k.n = v > 0.5f; } },
        F(magnitude) F(inGain) F(inWidth) F(outWidth) F(outGain) F(mix) I(varispeed) I(resync) F(varispeedMix) B(eqOn) F(cutLo) F(cutHi) B(cutLoHard) B(cutHiHard) F(tone) I(toneMode) F(compress)
        B(noiseOn) F(noiseAmt) I(noiseType) F(noiseTone) F(noisePrePost) F(noiseFollow) F(noiseDuck) F(noiseFlux) F(noiseLo) F(noiseHi)
        B(wobOn) F(wobAmt) F(wowFlutter) F(wowRate) I(wowNote) B(wowSync) B(wowStereo) F(flutterRate) I(flutterNote) B(flutterSync) F(wobMorph) F(wobWarp) F(wobMix) F(wobFlux) F(wobLo) F(wobHi)
        B(distOn) F(distAmt) I(distType) F(distOverload) F(distMix) F(distFlux) F(distLo) F(distHi) B(distCut)
        B(digOn) F(digAmt) F(digRateBits) F(digSmooth) F(digSputter) F(digDrive) F(digMix) F(digFlux) F(digLo) F(digHi) B(digCut)
        B(spaceOn) F(spaceAmt) I(spaceType) F(spaceDecay) F(spacePan) F(spaceWidth) F(spacePre) F(spaceDuck) F(spaceFlux) F(spaceLo) F(spaceHi)
        B(lossOn) F(lossAmt) F(lossXfade) F(lossDrop) B(wearStereo) F(magRate) I(magNote) B(magSync) F(magMorph) F(magWarp) F(magVolFilter) I(codecDropMode) F(codecStretch) F(codecBlur) F(codecMix) B(codecCut) F(lossFlux) F(lossLo) F(lossHi)
        B(echoOn) F(echoAmt) B(echoSolo) B(echoClear) F(echoFb) F(echoTimeL) F(echoTimeR) I(echoNoteL) I(echoNoteR) B(echoSyncL) B(echoSyncR) B(echoLink) F(echoScrub) F(echoWidth) F(echoPingPong) F(echoDuck)
        F(grainAmt) F(grainPitch) B(grainRev) B(grainInv) F(echoFlux) F(echoLo) F(echoHi) F(echoWob) F(echoDist) F(echoDig) F(echoSpace) F(echoLoss)
    };
    std::string s (kvs ? kvs : "");
    size_t p = 0;
    while (p < s.size())
    {
        size_t q = s.find (',', p); if (q == std::string::npos) q = s.size();
        auto item = s.substr (p, q - p); auto eq = item.find ('=');
        if (eq != std::string::npos) { auto it = m.find (item.substr (0, eq)); if (it != m.end()) it->second (std::stof (item.substr (eq + 1))); else fprintf (stderr, "unknown %s\n", item.c_str()); }
        p = q + 1;
    }
}
extern "C" {
// DISTORT だけを通す（当てはめ用。左右同じ）
void rr_dist (const float* x, float* y, int n, double sr, int type, float amt)
{
    static Distort* d = nullptr; if (! d) d = new Distort();
    d->prepare (sr); d->reset();
    Knobs k; k.distType = type; k.distOverload = 0.0f; k.distFlux = 0.0f; k.distLo = 0; k.distHi = 1; k.distCut = false; k.distMix = 1;
    std::vector<float> env ((size_t) 512, 0.5f), L (512), R (512);
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        for (int i = 0; i < m; ++i) L[(size_t) i] = R[(size_t) i] = x[pos + i];
        float* io[2] = { L.data(), R.data() };
        d->process (io, 1, m, k, amt, env.data());
        for (int i = 0; i < m; ++i) y[pos + i] = L[(size_t) i];
    }
}
}
extern "C" {
// 途中でつまみを変える（switchAt サンプル目から kv2）。ブロックの大きさ bs
int rr_render2 (const float* x, float* y, int n, double sr, const char* kv1, const char* kv2, int switchAt, int bs)
{
    static Engine* e = nullptr; if (! e) e = new Engine();
    e->prepare (sr, bs);
    Knobs k1 = Knobs(), k2 = Knobs(); applyKV (k1, kv1); applyKV (k2, kv2);
    Transport tr; tr.bpm = 120; tr.playing = true;
    std::vector<float> L ((size_t) bs), R ((size_t) bs);
    for (int pos = 0; pos < n; pos += bs)
    {
        const int m = std::min (bs, n - pos);
        for (int i = 0; i < m; ++i) { L[(size_t) i] = x[2 * (pos + i)]; R[(size_t) i] = x[2 * (pos + i) + 1]; }
        float* io[2] = { L.data(), R.data() };
        e->process (io, 2, m, pos >= switchAt ? k2 : k1, tr);
        for (int i = 0; i < m; ++i) { y[2 * (pos + i)] = L[(size_t) i]; y[2 * (pos + i) + 1] = R[(size_t) i]; }
    }
    return e->latency();
}
}
extern "C" {
// 3 段階（at1 から kv2、at2 から kv3）
int rr_render3 (const float* x, float* y, int n, double sr, const char* kv1, const char* kv2, const char* kv3, int at1, int at2, int bs)
{
    static Engine* e = nullptr; if (! e) e = new Engine();
    e->prepare (sr, bs);
    Knobs k1, k2, k3; applyKV (k1, kv1); applyKV (k2, kv2); applyKV (k3, kv3);
    Transport tr; tr.bpm = 120; tr.playing = true;
    std::vector<float> L ((size_t) bs), R ((size_t) bs);
    for (int pos = 0; pos < n; pos += bs)
    {
        const int m = std::min (bs, n - pos);
        for (int i = 0; i < m; ++i) { L[(size_t) i] = x[2 * (pos + i)]; R[(size_t) i] = x[2 * (pos + i) + 1]; }
        float* io[2] = { L.data(), R.data() };
        e->process (io, 2, m, pos >= at2 ? k3 : pos >= at1 ? k2 : k1, tr);
        for (int i = 0; i < m; ++i) { y[2 * (pos + i)] = L[(size_t) i]; y[2 * (pos + i) + 1] = R[(size_t) i]; }
    }
    return e->latency();
}
}
