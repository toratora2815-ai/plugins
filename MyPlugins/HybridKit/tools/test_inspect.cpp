// Hybrid Kit — DSP 検査係の追加テスト（JUCE なし、2026-11-12。2 回目の検査で、写しを直した後の本体に合わせた）
//
// エンジン（KitEngine.h / Resampler.h / ExsKit.h）はそのまま使う。
// PluginProcessor.cpp の次の部分は、同じ手順をこのファイルに「写し」として書いて確かめる（本体を変えたら写しも合わせる）:
//   processBlock のイベントの置き方・サンプルレート変換・モノ出力 / キットの受け渡し（pendingKit・toRetire・retiredKit）/ Loader::run
//
// ビルド: clang++ -std=c++17 -O2 -o test_inspect test_inspect.cpp
// 使い方: test_inspect <rates|clicks|handoff|loader|prefetch|zones|edm|cpu|extreme|multi|all>
//   rates    … 44.1 / 48 / 88.2 / 96 / 32 / 176.4 / 192 kHz × ブロック 1 / 37 / 512 / 4096 / 8192、モノ。NaN・大きさ・遅れの申告・ブロックで変わらないか・メモリ確保
//   clicks   … つまみの急な変化（ブロックの頭にイベントがあると 1 サンプルで変わる）、LENGTH 99→100、SOUND の切り替え、キットの切り替え
//   handoff  … キットを A→B→C・合成キット 12 個を続けて替えたとき、消し忘れ・急に切れる音・先読みの途中で消さないか（写し）
//   loader   … 書き出しの待ち・A→B→A・X→Y→X・つなぎ方の計算の途中で選び直したとき（写し）
//   prefetch … 冷えた（メモリにない）キットを実時間で鳴らし、オーディオスレッドの 1 ブロックの時間を測る。先読みの順番の遅れ
//   zones    … 全キットの録音の終わり（切れ目のプチ）と、頭 → 尾のつなぎ目の山・谷・二度鳴り（refineSplices の前 → 後）
//   edm      … 全キットでサブ・EDM スネアの自動の大きさの範囲、サイドスティックにクラップ、BLEND 50 % の低音
//   cpu      … 声を多くした重い設定での CPU（44.1 / 48 / 96 / 192 kHz）
//   multi    … マルチアウト（0.2.0）: 出口の和 = Stereo・つながっていない出口・モノの Main・出口の数の変化・出口の切り替え・リセット・CPU
//   extreme  … つまみを全部端、無音、キットなし、prepare のやり直し
#include "../Source/KitEngine.h"
#include "../Source/Resampler.h"
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mutex>
#include <new>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

using namespace hk;
using Clock = std::chrono::steady_clock;

// ---- メモリ確保の回数（オーディオスレッドの写しの中だけ数える） ----
static std::atomic<bool> gCountAllocs { false };
static bool gMeasureAllocs = false;   // runHost の process の中だけ数える
static std::atomic<long> gAllocs { 0 };
void* operator new (size_t n)
{
    if (gCountAllocs.load (std::memory_order_relaxed)) gAllocs.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }
void operator delete[] (void* p, size_t) noexcept { std::free (p); }

static int gFails = 0;
static void check (bool ok, const std::string& what) { printf ("%s %s\n", ok ? "OK  " : "NG  ", what.c_str()); if (! ok) ++gFails; }
static double dB (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }
static std::string kitPath (const std::string& n) { return dkdKitFolder() + "/" + n + ".exs"; }
static double msSince (Clock::time_point t) { return std::chrono::duration<double, std::milli> (Clock::now() - t).count(); }

static std::unique_ptr<Kit> loadKit (const std::string& name, double* ms = nullptr)
{
    auto k = std::make_unique<Kit>(); std::string err;
    const auto t0 = Clock::now();
    if (! k->loadProducerKit (kitPath (name), err)) { printf ("     %s を読めない: %s\n", name.c_str(), err.c_str()); return nullptr; }
    if (ms != nullptr) *ms = msSince (t0);
    return k;
}

//==============================================================================
// 鳴らす譜面（render.cpp の popGroove と同じ。秒・ステータス・データ）
struct TEv { double sec; uint8_t st, d1, d2; };

static std::vector<TEv> popGroove()
{
    std::vector<TEv> e;
    std::mt19937 r (7);
    std::normal_distribution<double> jt (0.0, 0.0022);
    std::uniform_int_distribution<int> jv (-5, 5);
    const double bpm = 100, step = 60.0 / bpm / 4;
    auto add = [&] (int bar, int st, int note, int vel) { e.push_back ({ (bar * 16 + st) * step + 0.05 + jt (r), 0x90, (uint8_t) note, (uint8_t) std::clamp (vel + jv (r), 1, 127) }); };
    for (int bar = 0; bar < 8; ++bar)
    {
        const bool fill = bar == 7;
        add (bar, 0, 36, 112); add (bar, 6, 36, 96); add (bar, 8, 36, 108);
        if (bar % 2 == 1 && ! fill) add (bar, 11, 36, 92);
        if (bar % 4 == 2) add (bar, 14, 36, 88);
        add (bar, 4, 38, 116);
        if (! fill) add (bar, 12, 38, 118);
        if (bar % 2 == 0) { add (bar, 7, 38, 28); add (bar, 15, 38, 32); }
        if (bar % 4 == 3 && ! fill) add (bar, 10, 38, 26);
        for (int s = 0; s < 16; s += 2)
        {
            if (fill && s >= 8) break;
            if (bar % 2 == 1 && s == 14) { add (bar, 14, 46, 92); continue; }
            add (bar, s, 42, s % 4 == 0 ? 88 : 66);
            if (bar % 4 >= 2 && (s == 2 || s == 10)) add (bar, s + 1, 42, 44);
        }
        if (bar % 2 == 1 && bar < 7) add (bar + 1, 0, 33, 70);
        if (bar == 0 || bar == 4) add (bar, 0, 49, 110);
        if (fill)
        {
            const int sn[] = { 8, 9, 10, 11 };
            for (int k = 0; k < 4; ++k) add (bar, sn[k], 38, 70 + k * 12);
            add (bar, 12, 48, 104); add (bar, 13, 48, 92); add (bar, 14, 47, 108); add (bar, 15, 43, 116);
        }
    }
    e.push_back ({ 8 * 16 * step + 0.05, 0x90, 36, 118 });
    e.push_back ({ 8 * 16 * step + 0.05, 0x90, 57, 118 });
    std::stable_sort (e.begin(), e.end(), [] (const TEv& a, const TEv& b) { return a.sec < b.sec; });
    return e;
}

// 重い譜面（CPU 用）: 140 BPM、16 分のハイハット＋ライド 8 分＋キック 8 分＋スネアとゴースト＋毎小節クラッシュ＋タムの 16 分
static std::vector<TEv> denseGroove (int bars = 8)
{
    std::vector<TEv> e;
    const double step = 60.0 / 140.0 / 4;
    auto add = [&] (int bar, int st, int note, int vel) { e.push_back ({ (bar * 16 + st) * step + 0.02, 0x90, (uint8_t) note, (uint8_t) vel }); };
    for (int bar = 0; bar < bars; ++bar)
    {
        for (int s = 0; s < 16; ++s)
        {
            e.push_back ({ (bar * 16 + s) * step + 0.019, 0xb0, 4, (uint8_t) ((s * 37) % 128) });   // ペダルを動かす
            add (bar, s, s % 4 == 2 ? 46 : 42, s % 2 ? 72 : 100);
            if (s % 2 == 0) { add (bar, s, 36, 115); add (bar, s, 51, 90); }
            if (s == 4 || s == 12) add (bar, s, 38, 120); else if (s % 2) add (bar, s, 38, 34);
        }
        add (bar, 0, bar % 2 ? 57 : 49, 120);
        add (bar, 12, 48, 110); add (bar, 13, 47, 110); add (bar, 14, 45, 110); add (bar, 15, 43, 110);
    }
    std::stable_sort (e.begin(), e.end(), [] (const TEv& a, const TEv& b) { return a.sec < b.sec; });
    return e;
}

//==============================================================================
// 写し: HybridKitProcessor::processBlock の「イベント → 出口の割り当て → 鳴らす → 出口ごとのサンプルレート変換 → 出口」（0.2.0 マルチアウト）
struct HEv { int offset; uint8_t st, d1, d2; };

struct HostSim
{
    static constexpr int K = Engine::maxOuts;
    Engine engine;
    std::array<StreamResampler, K> resamplers;
    std::array<std::vector<float>, 2 * K> ibuf;
    std::vector<float> tmpL, tmpR;
    // ホスト（Logic）が決めるバスの形: getBusCount (false)、各バスの isEnabled・getNumberOfChannels
    int busCount = 1;
    std::array<bool, K> enabled {};
    std::array<int, K> chans {};
    std::array<bool, 6> sep {};   // sepKick / sepSnare / sepToms / sepHiHat / sepCym / sepRoom
    int connected = 0;            // connectedOutCount

    HostSim()
    {
        tmpL.assign ((size_t) Engine::maxBlock, 0.0f); tmpR.assign ((size_t) Engine::maxBlock, 0.0f);
        for (auto& b : ibuf) b.assign ((size_t) Engine::maxBlock, 0.0f);
        engine.prepare (44100.0);
        for (auto& r : resamplers) r.setup (44100.0, 44100.0);
        setLayout (1, false);
    }
    void prepare (double sr) { engine.prepare (44100.0); for (auto& r : resamplers) r.setup (44100.0, sr); }   // prepareToPlay
    int latency() const { return resamplers[0].latencyOut(); }
    // バスの形（Logic の Stereo = 1 本、Multi Output = 7 本）。mainMono = Main がモノ、off = 無効にするバスの番号
    void setLayout (int count, bool mainMono, const std::vector<int>& off = {})
    {
        busCount = std::clamp (count, 1, K);
        for (int b = 0; b < K; ++b) { enabled[(size_t) b] = b < busCount; chans[(size_t) b] = b < busCount ? 2 : 0; }
        if (mainMono) chans[0] = 1;
        for (int b : off) if (b > 0 && b < K) { enabled[(size_t) b] = false; chans[(size_t) b] = 0; }
    }
    // 写し: reset() → 次の processBlock の頭（resetRequested）
    void hostReset() { engine.hardReset(); for (auto& r : resamplers) r.reset(); }

    void process (float* const* buf, int bufChans, int n, const HEv* ev, int ne)
    {
        auto apply = [this] (const HEv& e)
        {
            const int st = e.st & 0xf0;
            if (st == 0x90 && e.d2 > 0) engine.noteOn (e.d1, e.d2);
            else if (st == 0xb0) engine.controller (e.d1, e.d2);
        };
        // ---- 出口（つながっている出口だけ書く） ----
        std::array<float*, 2 * K> host {};
        int conn = 0;
        const int numBuses = std::min (K, busCount);
        for (int b = 0; b < numBuses; ++b)
        {
            if (! enabled[(size_t) b] || chans[(size_t) b] < 1) continue;
            int ch0 = 0;   // getChannelIndexInProcessBlockBuffer（前の有効なバスのチャンネル数の合計）
            for (int q = 0; q < b; ++q) if (enabled[(size_t) q]) ch0 += chans[(size_t) q];
            if (ch0 < 0 || ch0 >= bufChans) continue;
            host[(size_t) (2 * b)] = buf[ch0];
            host[(size_t) (2 * b + 1)] = chans[(size_t) b] > 1 && ch0 + 1 < bufChans ? buf[ch0 + 1] : nullptr;
            ++conn;
        }
        connected = conn;
        {
            auto has = [&] (int o) { return o < numBuses && host[(size_t) (2 * o)] != nullptr; };
            static constexpr int groupOut[] = { Engine::OutKick, Engine::OutSnare, Engine::OutToms, Engine::OutHiHat, Engine::OutCymbals };
            std::array<int, NumPieces> direct {}, room {};
            const bool roomOwn = sep[5] && has (Engine::OutRoom);
            for (int p = 0; p < NumPieces; ++p)
            {
                const int g = groupOf (p);
                const int o = groupOut[g];
                direct[(size_t) p] = sep[(size_t) g] && has (o) ? o : (int) Engine::OutMain;
                room[(size_t) p] = roomOwn ? (int) Engine::OutRoom : direct[(size_t) p];
            }
            engine.setRouting (direct, room);
        }
        std::array<float*, 2 * K> iptr {};
        for (size_t k = 0; k < iptr.size(); ++k) iptr[k] = ibuf[k].data();
        auto writeHost = [&] (int b, int pos, const float* l, const float* r, int c)
        {
            float* hl = host[(size_t) (2 * b)];
            float* hr = host[(size_t) (2 * b + 1)];
            if (hl == nullptr) return;
            if (hr != nullptr) { std::copy (l, l + c, hl + pos); std::copy (r, r + c, hr + pos); }
            else for (int i = 0; i < c; ++i) hl[pos + i] = 0.5f * (l[i] + r[i]);
        };
        auto& rs0 = resamplers[0];
        if (rs0.isBypass())
        {
            int pos = 0, idx = 0;
            while (pos < n)
            {
                while (idx < ne && ev[idx].offset <= pos) apply (ev[idx++]);
                const int end = idx < ne ? std::min (n, ev[idx].offset) : n;
                const int c = std::max (1, std::min (end - pos, Engine::maxBlock));
                engine.render (iptr.data(), K, c);
                for (int b = 0; b < numBuses; ++b) writeHost (b, pos, iptr[(size_t) (2 * b)], iptr[(size_t) (2 * b + 1)], c);
                pos += c;
            }
            while (idx < ne) apply (ev[idx++]);
        }
        else
        {
            const int64_t base = rs0.outputPos();
            const int64_t need = rs0.needInputUpTo (n);
            int idx = 0;
            while (rs0.inputWritten() < need)
            {
                while (idx < ne && rs0.eventIndexFor (base + ev[idx].offset) <= rs0.inputWritten()) apply (ev[idx++]);
                int64_t until = need;
                if (idx < ne) until = std::min (until, rs0.eventIndexFor (base + ev[idx].offset));
                const int c = (int) std::max<int64_t> (1, std::min<int64_t> (Engine::maxBlock, until - rs0.inputWritten()));
                engine.render (iptr.data(), K, c);
                for (int k = 0; k < K; ++k) resamplers[(size_t) k].push (iptr[(size_t) (2 * k)], iptr[(size_t) (2 * k + 1)], c);
            }
            while (idx < ne) apply (ev[idx++]);
            for (int k = 0; k < K; ++k)
            {
                auto& rs = resamplers[(size_t) k];
                float* hl = k < numBuses ? host[(size_t) (2 * k)] : nullptr;
                float* hr = k < numBuses ? host[(size_t) (2 * k + 1)] : nullptr;
                if (hl == nullptr) { rs.skip (n); continue; }
                if (hr != nullptr) rs.produce (hl, hr, n);
                else
                    for (int pos = 0; pos < n; pos += Engine::maxBlock)
                    {
                        const int c = std::min (Engine::maxBlock, n - pos);
                        rs.produce (tmpL.data(), tmpR.data(), c);
                        for (int i = 0; i < c; ++i) hl[pos + i] = 0.5f * (tmpL[(size_t) i] + tmpR[(size_t) i]);
                    }
            }
        }
    }
    // 出口 1 本（Stereo かモノ）で鳴らす（前からのテスト用）
    void process (float* outL, float* outR, int n, const HEv* ev, int ne)
    {
        setLayout (1, outR == nullptr);
        float* b[2] = { outL, outR };
        process (b, outR == nullptr ? 1 : 2, n, ev, ne);
    }
};

// ホストのように、ブロックごとにイベントを渡して鳴らす。aut はブロックの頭でつまみを変える（オートメーション・画面）
using Automation = std::function<void (int64_t blockStart, int n, Settings&)>;
using BlockHook = std::function<void (int64_t blockStart, int n)>;

static void runHost (HostSim& h, const std::vector<TEv>& ev, double sr, int block, double totalSec, const Settings& base,
                     std::vector<float>& L, std::vector<float>& R, bool mono = false, const Automation& aut = {}, const BlockHook& hook = {})
{
    const size_t N = (size_t) (totalSec * sr);
    L.assign (N, 0.0f); R.assign (N, 0.0f);
    size_t k = 0;
    std::vector<HEv> be; be.reserve (4096);
    for (size_t pos = 0; pos < N; pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, N - pos);
        be.clear();
        while (k < ev.size() && (size_t) std::llround (ev[k].sec * sr) < pos + (size_t) n)
        {
            const int64_t at = std::llround (ev[k].sec * sr);
            be.push_back ({ (int) std::clamp<int64_t> (at - (int64_t) pos, 0, n - 1), ev[k].st, ev[k].d1, ev[k].d2 });
            ++k;
        }
        if (hook) hook ((int64_t) pos, n);
        Settings s = base;
        if (aut) aut ((int64_t) pos, n, s);
        h.engine.setSettings (s);
        gCountAllocs = gMeasureAllocs;
        h.process (L.data() + pos, mono ? nullptr : R.data() + pos, n, be.data(), (int) be.size());
        gCountAllocs = false;
    }
}

// BS.1770 の大きさ（render.cpp と同じ）
static double loudness (const std::vector<float>& L, const std::vector<float>& R, double fs)
{
    auto kfilter = [&] (const std::vector<float>& x)
    {
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
            const double v = s - c1 * w1 - c2 * w2;
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

static float peakOf (const std::vector<float>& L, const std::vector<float>& R, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0;
    for (size_t i = from; i < std::min (to, L.size()); ++i) p = std::max ({ p, std::abs (L[i]), std::abs (R[i]) });
    return p;
}
static bool allFinite (const std::vector<float>& L, const std::vector<float>& R)
{
    for (size_t i = 0; i < L.size(); ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) return false;
    return true;
}
static double onsetSamples (const std::vector<float>& x, double rate)
{
    float pk = 0; for (size_t i = 0; i < (size_t) (0.5 * rate) && i < x.size(); ++i) pk = std::max (pk, std::abs (x[i]));
    for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) > pk * 0.1f) return (double) i;
    return 0;
}
static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0; to = std::min (to, x.size()); if (to <= from) return 0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (to - from));
}
// 5 kHz の 4 次ハイパス → 1 ms ごとの強さ。spike = 変えた所の後 spikeMs の最大、ref = 変える前 refMs の最大（健康診断の click と同じ考え）
static double clickScore (const std::vector<float>& x, double fs, size_t at, double refMs = 30, double spikeMs = 4)
{
    Biquad a, b;
    auto mk = [&] (Biquad& q, double Q) { q.highpass (fs, 5000.0, Q); };
    mk (a, 0.5411961); mk (b, 1.3065630);
    const size_t W = (size_t) (0.001 * fs);
    std::vector<double> e;
    double acc = 0; size_t k = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = b.run (a.run (x[i])); acc += v * v;
        if (++k == W) { e.push_back (acc / (double) W); acc = 0; k = 0; }
    }
    const size_t w0 = at / W;
    double ref = 1e-30, spike = 1e-30;
    for (size_t w = (size_t) std::max<long> (0, (long) w0 - (long) refMs); w + 1 < w0 && w < e.size(); ++w) ref = std::max (ref, e[w]);
    for (size_t w = w0; w <= w0 + (size_t) spikeMs && w < e.size(); ++w) spike = std::max (spike, e[w]);
    return 10.0 * std::log10 (spike / ref);
}

//==============================================================================
// rates: サンプルレート × ブロックの大きさ
static void testRates (Kit& kit)
{
    printf ("\n== サンプルレートとブロックの大きさ（processBlock の写し、生＋サブ＋クラップ） ==\n");
    const auto ev = popGroove();
    Settings st; st.kickBlend = 1; st.snareBlend = 1;
    analyzeEdm (kit, st);
    const double total = ev.back().sec + 2.0;

    std::vector<float> rL, rR;
    { auto h = std::make_unique<HostSim>(); h->prepare (44100); h->engine.setKit (&kit); runHost (*h, ev, 44100, 512, total, st, rL, rR); }
    const double refL = loudness (rL, rR, 44100), refOn = onsetSamples (rL, 44100);
    printf ("     基準 44.1 kHz・512: %.2f LUFS、ピーク %.1f dBFS\n", refL, dB (peakOf (rL, rR)));

    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 32000.0, 176400.0, 192000.0 })
    {
        std::vector<float> aL, aR;   // 512 の結果（ブロックの比べ用）
        for (int block : { 512, 1, 37, 4096, 8192 })
        {
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
            std::vector<float> L, R;
            gAllocs = 0; gMeasureAllocs = true;
            const auto t0 = Clock::now();
            runHost (*h, ev, sr, block, total, st, L, R);
            const double ms = msSince (t0);
            gMeasureAllocs = false;
            const long allocs = gAllocs.load();
            const bool fin = allFinite (L, R);
            const double lu = loudness (L, R, sr);
            const double on = onsetSamples (L, sr) - refOn * sr / 44100.0;
            double diff = 0;
            if (block == 512) { aL = L; aR = R; }
            else for (size_t i = 0; i < L.size(); ++i) diff = std::max ({ diff, (double) std::abs (L[i] - aL[i]), (double) std::abs (R[i] - aR[i]) });
            printf ("     %6.1f kHz ブロック %4d: %.2f LUFS（差 %+.2f）ピーク %5.1f dBFS、鳴り始めのずれ %6.1f（申告 %3d）、512 との差 %.1e、確保 %ld 回、%.0f ms\n",
                    sr / 1000, block, lu, lu - refL, dB (peakOf (L, R)), on, h->latency(), diff, allocs, ms);
            const bool latOk = std::abs (on - h->latency()) <= 2.0 * sr / 44100.0 + 1.0;   // 置き方の中の 1 サンプル＋基準（44.1 kHz の整数位置）の 1 サンプル
            check (fin && std::abs (lu - refL) < 0.3 && latOk && allocs == 0,
                   "  " + std::to_string ((int) sr) + " Hz・" + std::to_string (block) + ": NaN なし・大きさ ±0.3 dB・遅れの申告が ±1.5 サンプル・確保なし");
        }
        // モノ（Logic のモノのトラック）
        {
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
            std::vector<float> L, R; runHost (*h, ev, sr, 256, total, st, L, R, true);
            double d = 0; for (size_t i = 0; i < L.size(); ++i) d = std::max (d, (double) std::abs (L[i] - 0.5f * (aL[i] + aR[i])));
            printf ("     %6.1f kHz モノ: (L+R)/2 との差 %.1e\n", sr / 1000, d);
        }
    }
    // 参考: とても大きいブロック（32 kHz で 32768）。変換の輪（32768）より多く要る
    {
        auto h = std::make_unique<HostSim>(); h->prepare (32000); h->engine.setKit (&kit);
        std::vector<float> L, R, L2, R2; runHost (*h, ev, 32000, 32768, total, st, L, R);
        auto h2 = std::make_unique<HostSim>(); h2->prepare (32000); h2->engine.setKit (&kit);
        runHost (*h2, ev, 32000, 512, total, st, L2, R2);
        double d = 0; for (size_t i = 0; i < L.size(); ++i) d = std::max (d, (double) std::abs (L[i] - L2[i]));
        printf ("     参考: 32 kHz・ブロック 32768 と 512 の差 %.2e（Logic はここまで大きいブロックを渡さない）\n", d);
    }
}

//==============================================================================
// clicks
static void testClicks (Kit& kit)
{
    printf ("\n== プチノイズ ==\n");
    // (a) フェーダーを急に変える: ブロックの頭にイベント（ノートオフなど）があると、最初の区切りが 1 サンプルになり、そこで一気に変わる
    //     フロアタム（低く長く鳴る）を叩いて 0.1 秒後に TOMS VOLUME 0 → −60 dB / OUTPUT 0 → −24 dB
    for (int what = 0; what < 2; ++what)
        for (double sr : { 44100.0, 48000.0 })
            for (int block : { 128, 512 })
                for (int variant = 0; variant < 2; ++variant)
                {
                    std::vector<TEv> ev { { 0.05, 0x90, 43, 120 } };
                    const int64_t changeAt = (int64_t) std::llround (0.15 * sr / block) * block;
                    if (variant == 1) ev.push_back ({ (double) (changeAt + 1) / sr, 0x80, 43, 0 });   // その頭にノートオフ（Drummer は毎回送る）
                    Settings st;
                    auto aut = [&] (int64_t pos, int, Settings& s) { if (pos >= changeAt) { if (what == 0) s.vol[GToms] = 0.001f; else s.output = 0.0631f; } };
                    auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
                    std::vector<float> L, R; runHost (*h, ev, sr, block, 0.4, st, L, R, false, aut);
                    const size_t at = (size_t) changeAt + (size_t) h->latency();
                    const double sc = clickScore (L, sr, at);
                    double jump = 0; for (size_t i = at - 8; i < at + 300; ++i) jump = std::max (jump, (double) std::abs (L[i] - L[i - 1]));
                    double norm = 0; for (size_t i = at - 1500; i < at - 8; ++i) norm = std::max (norm, (double) std::abs (L[i] - L[i - 1]));
                    printf ("     %s（%.1f kHz・ブロック %3d・%s）: 5 kHz 以上の山 %+5.1f dB、変えた所の 1 サンプルの段差 %.4f（直前 30 ms の最大 %.4f、%.1f 倍）\n",
                            what == 0 ? "TOMS VOLUME 0→−60 dB" : "OUTPUT 0→−24 dB     ", sr / 1000, block, variant ? "頭にノートオフあり" : "イベントなし      ", sc, jump, norm, jump / norm);
                }
    // (a2) 同じことを、きれいな低音（808 Long のサブだけ。マイクは全部 −inf）で: OUTPUT 0 → −24 dB をキックの 0.2 秒後に
    for (double sr : { 44100.0, 48000.0 })
        for (int block : { 32, 128, 512 })
            for (int variant = 0; variant < 2; ++variant)
            {
                std::vector<TEv> ev { { 0.05, 0x90, 36, 120 } };
                const int64_t changeAt = (int64_t) std::llround (0.25 * sr / block) * block;
                if (variant == 1) ev.push_back ({ (double) (changeAt + 1) / sr, 0x80, 36, 0 });
                Settings st; st.kickBlend = 1; st.kickSound = 1; st.close = st.oh = st.room = st.bleed = 0;
                analyzeEdm (kit, st);
                auto aut = [&] (int64_t pos, int, Settings& s) { if (pos >= changeAt) s.output = 0.0631f; };
                auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
                std::vector<float> L, R; runHost (*h, ev, sr, block, 0.5, st, L, R, false, aut);
                const size_t at = (size_t) changeAt + (size_t) h->latency();
                double jump = 0; for (size_t i = at - 8; i < at + 600; ++i) jump = std::max (jump, (double) std::abs (L[i] - L[i - 1]));
                double norm = 0; for (size_t i = at - 1500; i < at - 8; ++i) norm = std::max (norm, (double) std::abs (L[i] - L[i - 1]));
                printf ("     サブだけ・OUTPUT 0→−24 dB（%.1f kHz・ブロック %3d・%s）: 1 サンプルの段差 %.4f（ふだんの最大 %.4f の %.1f 倍）、5 kHz 以上の山 %+5.1f dB\n",
                        sr / 1000, block, variant ? "頭にノートオフあり" : "イベントなし      ", jump, norm, jump / norm, clickScore (L, sr, at));
            }
    // (b) LENGTH 99 % → 100 %（鳴っているクラッシュ）
    {
        const double sr = 44100;
        std::vector<TEv> ev { { 0.05, 0x90, 49, 110 } };
        const int block = 256;
        const int64_t changeAt = (int64_t) std::llround (3.0 * sr / block) * block;
        Settings st; st.length[GCymbals] = 0.99f;
        auto aut = [&] (int64_t pos, int, Settings& s) { if (pos >= changeAt) s.length[GCymbals] = 1.0f; };
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
        std::vector<float> L, R; runHost (*h, ev, sr, block, 3.5, st, L, R, false, aut);
        const double before = rms (L, (size_t) changeAt - 441, (size_t) changeAt), after = rms (L, (size_t) changeAt + 256, (size_t) changeAt + 256 + 441);
        printf ("     LENGTH 99→100 %%（クラッシュの 2.95 秒後）: %.1f dBFS → %.1f dBFS（%+.1f dB を 1 ブロックで）\n", dB (before), dB (after), dB (after) - dB (before));
        check (std::abs (dB (after) - dB (before)) < 3.0, "LENGTH を 1 % 動かしても鳴っている音が 3 dB 以上跳ねない");
    }
    // (c) キックの SOUND を鳴っている間に替える（Sub Thump → 808 Long）。サブだけ聴く（マイクを全部 −inf）
    {
        const double sr = 44100; const int block = 256;
        std::vector<TEv> ev { { 0.05, 0x90, 36, 115 } };
        Settings st; st.kickBlend = 1; st.kickSound = 2; st.close = st.oh = st.room = st.bleed = 0;
        analyzeEdm (kit, st);
        const int64_t changeAt = (int64_t) std::llround (0.25 * sr / block) * block;
        auto aut = [&] (int64_t pos, int, Settings& s) { if (pos >= changeAt) s.kickSound = 1; };
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
        std::vector<float> L, R; runHost (*h, ev, sr, block, 0.8, st, L, R, false, aut);
        std::vector<float> L0, R0; { auto h0 = std::make_unique<HostSim>(); h0->prepare (sr); h0->engine.setKit (&kit); runHost (*h0, ev, sr, block, 0.8, st, L0, R0); }
        const double before = rms (L, (size_t) changeAt - 441, (size_t) changeAt), after = rms (L, (size_t) changeAt + 300, (size_t) changeAt + 741);
        // 切れる所: 声の長さは鳴らし始めの SOUND（0.25 秒 × 1.3）のまま
        size_t lastLoud = 0; for (size_t i = 0; i < L.size(); ++i) if (std::abs (L[i]) > 1e-4f) lastLoud = i;
        float pkCut = 0; for (size_t i = lastLoud > 600 ? lastLoud - 600 : 0; i < lastLoud; ++i) pkCut = std::max (pkCut, std::abs (L[i]));
        printf ("     キック SOUND を鳴っている間に Sub Thump → 808 Long: サブ %.1f → %.1f dBFS（%+.1f dB）、%.3f 秒で %.1f dBFS から急に止まる（替えないときの同じ所 %.1f dBFS）\n",
                dB (before), dB (after), dB (after) - dB (before), (double) lastLoud / sr, dB (pkCut), dB (rms (L0, (size_t) changeAt + 300, (size_t) changeAt + 741)));
    }
    // (d) キットの切り替え A→B→C（A のクラッシュがまだ鳴っている）
    printf ("     （キットの切り替えのプチは handoff で測る）\n");
}

//==============================================================================
// handoff: キットの受け渡しの写し（processBlock の頭 + タイマー。2026-11-12 の直し: 前のキットは Engine の oldKits に並ぶ、
// 消すのは先読みが「渡した時点の数（fence）」まで触り終えてから）
struct HandoffSim
{
    // 写し: HybridKitProcessor の pendingKit / retiredKit / retiredFence / toRetire / toRetireFence / pendingDeletes
    std::atomic<const Kit*> pending { nullptr }, retired { nullptr };
    std::atomic<uint32_t> retiredFence { 0 };
    const Kit* toRetire = nullptr;
    uint32_t toRetireFence = 0;
    std::vector<std::pair<const Kit*, uint32_t>> pendingDeletes;
    std::vector<const Kit*> deleted;
    std::vector<double> deletedAt;   // 消した時刻（秒、テスト用）
    double nowSec = 0;
    std::function<void (const Kit*)> onDelete;   // 本当に消す（ASan で、消したキットに触っていないかを見る）
    int64_t lastTimer = 0;

    void audioTop (Engine& e, Prefetcher& pf)   // processBlock の頭
    {
        if (auto* k = pending.exchange (nullptr)) e.setKit (k);
        if (toRetire == nullptr) { toRetire = e.takeRetiredKit(); toRetireFence = pf.enqueued(); }
        if (toRetire != nullptr)
        {
            const Kit* expected = nullptr;
            retiredFence.store (toRetireFence);
            if (retired.compare_exchange_strong (expected, toRetire)) toRetire = nullptr;
        }
    }
    void timer (Prefetcher& pf)   // timerCallback（メッセージスレッド、10 Hz）
    {
        if (const Kit* r = retired.exchange (nullptr)) pendingDeletes.push_back ({ r, retiredFence.load() });
        for (auto it = pendingDeletes.begin(); it != pendingDeletes.end();)
        {
            if (Prefetcher::reached (pf.done(), it->second)) { deleted.push_back (it->first); deletedAt.push_back (nowSec); if (onDelete) onDelete (it->first); it = pendingDeletes.erase (it); }
            else ++it;
        }
    }
};

// 小さな合成キット（部品 1 つ・オーバーヘッド 1 本・5 秒の減っていく雑音）。受け渡しの数え方を、たくさんのキットで試す用
static bool writeSynthCaf (const std::string& path, double seconds, uint32_t seed)
{
    FILE* f = fopen (path.c_str(), "wb");
    if (! f) return false;
    auto be16 = [&] (uint16_t v) { uint8_t b[2] = { (uint8_t) (v >> 8), (uint8_t) v }; fwrite (b, 1, 2, f); };
    auto be32 = [&] (uint32_t v) { uint8_t b[4] = { (uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v }; fwrite (b, 1, 4, f); };
    auto be64 = [&] (uint64_t v) { be32 ((uint32_t) (v >> 32)); be32 ((uint32_t) v); };
    const uint64_t N = (uint64_t) (seconds * 44100);
    fwrite ("caff", 1, 4, f); be16 (1); be16 (0);
    fwrite ("desc", 1, 4, f); be64 (32);
    double sr = 44100; uint64_t srb; memcpy (&srb, &sr, 8); be64 (srb);
    fwrite ("lpcm", 1, 4, f); be32 (0); be32 (4); be32 (1); be32 (2); be32 (16);
    fwrite ("data", 1, 4, f); be64 (4 + N * 4); be32 (0);
    uint32_t s = seed | 1u;
    for (uint64_t i = 0; i < N; ++i)
    {
        const double env = 0.5 * std::exp (-(double) i / 44100.0 / 1.2);
        for (int ch = 0; ch < 2; ++ch)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            be16 ((uint16_t) (int16_t) std::lround (env * ((double) (int32_t) s / 2147483648.0) * 32767.0));
        }
    }
    fclose (f);
    return true;
}

static std::unique_ptr<Kit> makeSynthKit (const std::string& cafPath, int piece, int key)
{
    auto k = std::make_unique<Kit>();
    auto c = std::make_unique<MappedCaf>();
    if (! c->open (cafPath)) return nullptr;
    k->name = "synth"; k->sampleRate = c->sampleRate;
    const MappedCaf* f = c.get();
    k->files.push_back (std::move (c));
    Artic a; a.piece = piece; a.name = "all"; a.key = key;
    Layer L; L.velLo = 0; L.velHi = 127; L.onset = 0; L.fx = 0;
    L.mic[OH].file = f; L.mic[OH].channels = 2; L.mic[OH].headStart = 0; L.mic[OH].headFrames = (uint32_t) f->frames; L.mic[OH].gain = 1.0f;
    L.has[OH] = true;
    a.layers.push_back (L);
    k->artics.push_back (a);
    k->attackPool.resize (4096 * 2);
    for (uint32_t i = 0; i < 4096; ++i) for (int ch = 0; ch < 2; ++ch) k->attackPool[i * 2 + (uint32_t) ch] = (int16_t) std::lround (f->at (i, ch) * 32767.0f);
    k->artics[0].layers[0].mic[OH].attack = k->attackPool.data();
    k->artics[0].layers[0].mic[OH].attackFrames = 4096;
    k->present[(size_t) piece] = true; k->micPresent[(size_t) piece][OH] = true;
    for (auto& p : k->pan) p = { 1.0f, 1.0f };
    k->lut.assign ((size_t) NumPieces * 128 * kNumArticKinds, (int16_t) -1);
    k->lut[((size_t) piece * 128 + (size_t) key) * kNumArticKinds] = 0;
    k->fx = std::make_unique<LayerFx[]> (1); k->numFx = 1;
    return k;
}

static void testHandoff (Kit& A, Kit& B, Kit& C)
{
    printf ("\n== キットの受け渡し（pendingKit → setKit → oldKits → takeRetiredKit → retiredKit＋fence → pendingDeletes の写し） ==\n");
    const double sr = 48000; const int block = 256;
    // (1) 本物のキット: A→B、A→B→C（A のクラッシュがまだ鳴っている）
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        Prefetcher pf; pf.start();
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&A); h->engine.setPrefetcher (&pf);
        HandoffSim hs;
        const int64_t tB = (int64_t) (0.5 * sr), tC = (int64_t) (1.0 * sr);
        bool sentB = false, sentC = false;
        size_t switchAt = 0;
        auto hook = [&] (int64_t pos, int)
        {
            if (! sentB && pos >= tB) { hs.pending = &B; sentB = true; }
            if (scenario == 1 && ! sentC && pos >= tC) { hs.pending = &C; sentC = true; switchAt = (size_t) pos; }
            hs.audioTop (h->engine, pf);
            if (pos - hs.lastTimer >= (int64_t) (0.1 * sr)) { hs.lastTimer = pos; hs.timer (pf); }
        };
        std::vector<TEv> ev { { 0.05, 0x90, 49, 120 }, { 0.06, 0x90, 57, 120 } };   // A のクラッシュ 2 枚（長く鳴る）
        std::vector<float> L, R;
        runHost (*h, ev, sr, block, 12.0, Settings {}, L, R, false, {}, hook);
        pf.stop();
        auto name = [&] (const Kit* k) { return k == &A ? "A" : k == &B ? "B" : k == &C ? "C" : "?"; };
        std::string del; for (auto* k : hs.deleted) { del += name (k); del += " "; }
        if (scenario == 0)
        {
            printf ("     A→B（0.5 秒）: 消されたキット = %s\n", del.c_str());
            check (hs.deleted.size() == 1 && hs.deleted[0] == &A, "A→B: A は鳴り終わってから 1 回だけ消される");
        }
        else
        {
            const size_t at = switchAt + (size_t) h->latency();
            const double before = rms (L, at - 480, at), after = rms (L, at + 64, at + 544);
            printf ("     A→B（0.5 秒）→C（1.0 秒）: 消されたキット = %s、C に替えた所の A のクラッシュ %.1f dBFS → %.1f dBFS\n", del.c_str(), dB (before), dB (after));
            const bool aDel = std::count (hs.deleted.begin(), hs.deleted.end(), &A) == 1, bDel = std::count (hs.deleted.begin(), hs.deleted.end(), &B) == 1;
            check (aDel && bDel && hs.deleted.size() == 2, "A→B→C: A も B も鳴り終わってから 1 回ずつ消される（残らない）");
            check (std::abs (dB (before) - dB (after)) < 3.0, "A→B→C: A の鳴っている音が切れない");
        }
    }
    // (2) 合成キット 12 個を 0.3 秒ごとに替える（それぞれ 5 秒鳴る音を 1 つ鳴らす）→ 8 個の列があふれる所まで。実際に消す（ASan で見張る）
    {
        const std::string dir = std::string (getenv ("TMPDIR") ? getenv ("TMPDIR") : "/tmp");
        const int pieces[][2] = { { CrashL, 49 }, { CrashR, 57 }, { Ride, 51 }, { TomHi, 48 }, { TomMidHi, 47 }, { TomMidLo, 45 }, { TomLo, 43 }, { Snare, 38 }, { Kick, 36 } };
        std::vector<std::unique_ptr<Kit>> kits;
        std::vector<int> noteOf;
        for (int i = 0; i < 12; ++i)
        {
            const std::string p = dir + "/hk_synth_" + std::to_string (i) + ".caf";
            if (! writeSynthCaf (p, 5.0, 77u + (uint32_t) i)) { printf ("     合成キットを書けない: %s\n", p.c_str()); return; }
            auto k = makeSynthKit (p, pieces[i % 9][0], pieces[i % 9][1]);
            if (! k) { printf ("     合成キットを開けない\n"); return; }
            kits.push_back (std::move (k)); noteOf.push_back (pieces[i % 9][1]);
        }
        Prefetcher pf; pf.start();
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (kits[0].get()); h->engine.setPrefetcher (&pf);
        HandoffSim hs;
        std::vector<const Kit*> alive; for (auto& k : kits) alive.push_back (k.get());
        hs.onDelete = [&] (const Kit* k)
        {
            for (auto& u : kits) if (u.get() == k) { u.reset(); break; }   // 本当に消す（munmap・メモリを返す）
        };
        std::vector<TEv> ev;
        for (int i = 0; i < 12; ++i) ev.push_back ({ 0.3 * i + 0.02, 0x90, (uint8_t) noteOf[(size_t) i], 110 });
        int next = 1; size_t maxQueue = 0;
        auto hook = [&] (int64_t pos, int)
        {
            hs.nowSec = (double) pos / sr;
            if (next < 12 && pos >= (int64_t) (0.3 * next * sr)) { hs.pending = alive[(size_t) next]; ++next; }
            hs.audioTop (h->engine, pf);
            if (pos - hs.lastTimer >= (int64_t) (0.1 * sr)) { hs.lastTimer = pos; hs.timer (pf); }
            maxQueue = std::max (maxQueue, hs.pendingDeletes.size());
        };
        std::vector<float> L, R;
        runHost (*h, ev, sr, block, 12.0, Settings {}, L, R, false, {}, hook);
        pf.stop();
        int leaked = 0; for (int i = 0; i < 11; ++i) if (std::count (hs.deleted.begin(), hs.deleted.end(), alive[(size_t) i]) != 1) ++leaked;
        const bool lastAlive = std::count (hs.deleted.begin(), hs.deleted.end(), alive[11]) == 0;
        // 切れた音（声を急に止めた）: 0.3 秒ごとの切り替えの直後に、出力が 1 サンプルで大きく下がった所を数える
        int cuts = 0;
        for (int i = 1; i < 12; ++i)
        {
            const size_t at = (size_t) (0.3 * i * sr) + (size_t) h->latency();
            const double b = rms (L, at - 480, at), a = rms (L, at + 64 + 2048, at + 544 + 2048);
            if (dB (b) - dB (a) > 6.0) ++cuts;
        }
        std::string when;
        for (size_t i = 0; i < hs.deleted.size(); ++i)
            for (int k = 0; k < 12; ++k) if (hs.deleted[i] == alive[(size_t) k]) { char b[48]; snprintf (b, sizeof b, "%d@%.2f ", k, hs.deletedAt[i]); when += b; }
        printf ("     消した順（キット番号@秒。各キットの音は鳴らし始めから 5 秒）: %s\n", when.c_str());
        printf ("     合成キット 12 個を 0.3 秒ごと: 消されたキット %zu 個（最後の 1 個は使用中）、消されずに残った %d、消すのを待った列の最大 %zu、切り替えで音が 6 dB 以上急に下がった所 %d\n",
                hs.deleted.size(), leaked, maxQueue, cuts);
        check (leaked == 0 && lastAlive, "合成キット 12 個: 使い終わったキットは全部 1 回ずつ消され、使っているものは消されない");
        for (int i = 0; i < 12; ++i) ::unlink ((dir + "/hk_synth_" + std::to_string (i) + ".caf").c_str());   // 一時ファイルを片付ける（mmap 中でも消してよい）
    }
    // (3) 先読みが重い範囲を触っている途中でキットを「消したい」とき、fence まで待つか（冷えた大きな録音で。子プロセス）
    {
        const std::string f = "/Library/Application Support/Logic/EXS Factory Samples/Drum Kit Designer Consolidated/Drum Kit Designer/Tight Maple Kit HiHat_Stereo_consolidated.caf";
        fflush (stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            MappedCaf* c = new MappedCaf();
            if (! c->open (f)) _exit (3);
            const double mb = (double) c->mapLen / 1048576.0;
            Prefetcher pf; pf.start();
            pf.request (c->base, c->mapLen);                 // 声が先読みを頼んだ
            const uint32_t fence = pf.enqueued();            // このキットを手放した（takeRetiredKit の時の enqueued()）
            const auto t0 = Clock::now();
            int ticks = 0;
            while (! Prefetcher::reached (pf.done(), fence)) { std::this_thread::sleep_for (std::chrono::milliseconds (100)); ++ticks; }   // タイマー 10 Hz
            const double ms = msSince (t0);
            delete c;                                        // ここで消す
            std::this_thread::sleep_for (std::chrono::milliseconds (300));
            pf.stop();
            printf ("     先読みが %.0f MB を触っている間は消さずに待った: %.0f ms（タイマー %d 回）→ その後に消した\n", mb, ms, ticks);
            fflush (stdout);
            _exit (0);
        }
        int status = 0; waitpid (pid, &status, 0);
        if (WIFSIGNALED (status)) printf ("     fence を待って消す → 子プロセスがシグナル %d（%s）で落ちた\n", WTERMSIG (status), strsignal (WTERMSIG (status)));
        else printf ("     fence を待って消す → 子プロセスは終了コード %d（%s）\n", WEXITSTATUS (status), WEXITSTATUS (status) == 0 ? "落ちなかった" : WEXITSTATUS (status) == 3 ? "ファイルなし" : "?");
        check (! WIFSIGNALED (status) && WEXITSTATUS (status) == 0, "先読みの途中では消さず、触り終えてから消す（落ちない）");
    }
}

//==============================================================================
// loader: Loader::run の写し（juce::Thread → std::thread、wait / notify は同じ意味）。2026-11-12 の直しに合わせた
struct LoaderModel
{
    enum { stateIdle = 0, stateLoading, stateReady, stateError };
    std::atomic<int> requestedKit { -1 };
    int loaderKit = -1;
    Kit* publishedKit = nullptr;
    std::atomic<bool> cancelLoad { false }, analysisDirty { false };
    std::atomic<int> loadingKit { -1 };
    std::atomic<uint32_t> loadSerial { 0 };
    uint32_t waitedSerial = 0;   // オーディオスレッドの写し
    bool lastLoadOk = false;
    bool spliceDone = true;
    std::atomic<float> progress { 0.0f };
    std::atomic<int> loadStateValue { stateIdle };
    std::mutex statusLock; std::string statusMessage;
    std::mutex kitsLock; std::vector<std::unique_ptr<Kit>> ownedKits;
    std::atomic<Kit*> pendingKit { nullptr };
    std::atomic<int> loads { 0 };
    // juce::Thread の代わり
    std::atomic<bool> exitFlag { false };
    std::mutex wm; std::condition_variable wcv; bool signalled = false;
    std::thread th;
    void notify() { { std::lock_guard<std::mutex> g (wm); signalled = true; } wcv.notify_all(); }
    void wait (int ms) { std::unique_lock<std::mutex> g (wm); wcv.wait_for (g, std::chrono::milliseconds (ms), [this] { return signalled; }); signalled = false; }
    bool threadShouldExit() const { return exitFlag.load(); }
    void start() { th = std::thread ([this] { run(); }); }
    void stop() { cancelLoad = true; exitFlag = true; notify(); if (th.joinable()) th.join(); }
    std::string status() { std::lock_guard<std::mutex> g (statusLock); return statusMessage; }

    // 写し: requestKit / deleteKit
    void requestKit (int index)
    {
        index = std::clamp (index, 0, (int) producerKitNames().size() - 1);
        if (requestedKit.exchange (index) != index) { cancelLoad.store (true); notify(); }
    }
    void deleteKit (const Kit* k)
    {
        std::unique_ptr<Kit> victim;
        std::lock_guard<std::mutex> g (kitsLock);
        for (auto it = ownedKits.begin(); it != ownedKits.end(); ++it)
            if (it->get() == k) { victim = std::move (*it); ownedKits.erase (it); break; }
    }
    // 写し: Loader::run
    void run()
    {
        auto setStatus = [this] (const std::string& m) { std::lock_guard<std::mutex> g (statusLock); statusMessage = m; };
        while (! threadShouldExit())
        {
            const int want = requestedKit.load();
            if (want >= 0 && want == loaderKit)
            {
                if (loadStateValue.load() == stateLoading)
                {
                    loadingKit.store (-1);
                    setStatus (producerKitNames()[(size_t) want]);
                    loadStateValue.store (lastLoadOk ? stateReady : stateError);
                }
                if (! spliceDone && publishedKit != nullptr)
                {
                    cancelLoad.store (false);
                    refineStarted = Clock::now(); refining = true;
                    spliceDone = refineSplices (*publishedKit, &cancelLoad);
                    refining = false; refineMs = msSince (refineStarted);
                    continue;
                }
            }
            if (want >= 0 && want != loaderKit)
            {
                cancelLoad.store (false);
                progress.store (0.0f);
                loadingKit.store (want);
                loadSerial.fetch_add (1);
                loadStateValue.store (stateLoading);
                const std::string name = producerKitNames()[(size_t) want];
                setStatus (name);
                auto kit = std::make_unique<Kit>();
                std::string err;
                const bool installed = kitInstalled (name);
                ++loads;
                bool ok = installed && kit->loadProducerKit (dkdKitFolder() + "/" + name + ".exs", err, &progress, &cancelLoad);
                if (ok) analyzeEdm (*kit, Settings {}, &cancelLoad);
                if (threadShouldExit()) break;
                if (cancelLoad.load()) { loadingKit.store (-1); continue; }
                if (! ok)
                {
                    loaderKit = want; lastLoadOk = false; loadingKit.store (-1);
                    setStatus (installed ? "Could not load " + name + " (" + err + ")" : "Drum Kit Designer sounds not found: " + name);
                    loadStateValue.store (stateError);
                    continue;
                }
                Kit* raw = kit.get();
                { std::lock_guard<std::mutex> g (kitsLock); ownedKits.push_back (std::move (kit)); }
                if (Kit* prev = pendingKit.exchange (raw)) deleteKit (prev);
                publishedKit = raw;
                loaderKit = want; lastLoadOk = true;
                loadingKit.store (-1);
                setStatus (name);
                loadStateValue.store (stateReady);
                refineStarted = Clock::now(); refining = true;
                spliceDone = false;
                spliceDone = refineSplices (*raw, &cancelLoad);
                refining = false; refineMs = msSince (refineStarted);
                continue;
            }
            if (analysisDirty.exchange (false) && publishedKit != nullptr) analyzeEdm (*publishedKit, Settings {}, nullptr);
            wait (200);
        }
    }
    std::atomic<bool> refining { false };
    Clock::time_point refineStarted;
    double refineMs = 0;

    // 写し: processBlock の書き出し（オフライン）の待ち。待った ms を返す
    double offlineWait()
    {
        const auto t0 = Clock::now();
        const int lk = loadingKit.load();
        const uint32_t serial = loadSerial.load();
        if (lk >= 0 && lk == requestedKit.load() && serial != waitedSerial)
        {
            waitedSerial = serial;
            for (int i = 0; i < 6000 && loadingKit.load() == lk && loadSerial.load() == serial && pendingKit.load() == nullptr; ++i)
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return msSince (t0);
    }
};

static const char* stateName (int s) { return s == 0 ? "Idle" : s == 1 ? "Loading" : s == 2 ? "Ready" : "Error"; }

// 頭 → 尾のつなぎ方が決まった層の数（refineSplices が最後まで行ったか）
static void countRefined (const Kit& kit, int& refined, int& eligible)
{
    refined = 0; eligible = 0;
    for (auto& a : kit.artics)
        for (auto& L : a.layers)
        {
            int ref = -1;
            for (int m = 0; m < NumMics; ++m) if (L.has[(size_t) m] && L.mic[(size_t) m].tailFrames > 0) { ref = m; break; }
            if (ref < 0) continue;
            const uint32_t H = L.mic[(size_t) ref].headFrames, X = std::min<uint32_t> (kSpliceFrames, H / 4), n = X + 220;
            if (H < n + 1764 || L.mic[(size_t) ref].tailFrames < n + 1764) continue;
            ++eligible;
            const LayerFx& f = kit.fx[(size_t) L.fx];
            bool changed = f.spliceSkip.load() != 0 || f.spliceTrim.load() != 0 || f.spliceRho.load() != 0.0f;
            for (int m = 0; m < NumMics; ++m) changed = changed || f.tailGain[(size_t) m].load() != 1.0f;
            refined += changed ? 1 : 0;
        }
}

static void testLoader (bool /*waitFull*/)
{
    printf ("\n== 読み込み係（Loader::run の写し。2026-11-12 の直し） ==\n");
    auto waitState = [] (LoaderModel& m, int st, double maxSec)
    {
        const auto t0 = Clock::now();
        while (m.loadStateValue.load() != st && msSince (t0) < maxSec * 1000) std::this_thread::sleep_for (std::chrono::milliseconds (2));
        return msSince (t0);
    };
    auto waitIdle = [] (LoaderModel& m, double maxSec)   // 読み込みもつなぎ方の計算も終わるまで
    {
        const auto t0 = Clock::now();
        while ((m.loadStateValue.load() == LoaderModel::stateLoading || m.refining.load()) && msSince (t0) < maxSec * 1000) std::this_thread::sleep_for (std::chrono::milliseconds (5));
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        while (m.refining.load() && msSince (t0) < maxSec * 1000) std::this_thread::sleep_for (std::chrono::milliseconds (5));
    };
    // 0) ふつうに読む＋書き出しの待ち（1 ブロック目だけ待つ、2 ブロック目からは待たない）
    {
        LoaderModel m; m.start();
        m.requestKit (0);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
        double first = 0, rest = 0;
        for (int b = 0; b < 40; ++b) { const double w = m.offlineWait(); if (b == 0) first = w; else rest += w; if (Kit* k = m.pendingKit.exchange (nullptr)) (void) k; }
        waitIdle (m, 60);
        int refined = 0, eligible = 0; if (m.publishedKit) countRefined (*m.publishedKit, refined, eligible);
        printf ("     Studio Kit を書き出し中に読む: 1 ブロック目の待ち %.0f ms、2〜40 ブロック目の待ちの合計 %.0f ms、状態 %s、つなぎ方の計算 %.0f ms（%d / %d 層）\n",
                first, rest, stateName (m.loadStateValue.load()), m.refineMs, refined, eligible);
        check (m.loadStateValue.load() == LoaderModel::stateReady && rest < 50 && first < 30000, "書き出し: 読み込みを 1 回だけ待つ（ブロックごとに待たない）");
        m.stop();
    }
    // 1) A → B → A（B を読んでいる間に A に戻す）
    {
        LoaderModel m; m.start();
        m.requestKit (0);
        waitState (m, LoaderModel::stateReady, 60);
        m.pendingKit.exchange (nullptr);
        waitIdle (m, 60);
        m.requestKit (1);
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
        m.requestKit (0);
        std::this_thread::sleep_for (std::chrono::milliseconds (1500));
        const int st = m.loadStateValue.load();
        double waited = 0; for (int b = 0; b < 20; ++b) waited += m.offlineWait();
        printf ("     Studio → Modern Maple → 0.15 秒で Studio → 1.5 秒後: 状態 %s、表示「%s」、受け渡し待ち %s、続けて書き出したときの 20 ブロックの待ち %.0f ms\n",
                stateName (st), m.status().c_str(), m.pendingKit.load() ? "あり" : "なし", waited);
        check (st == LoaderModel::stateReady && waited < 50, "A→B→A: 「読み込み中」のまま止まらず、書き出しも待たない");
        m.stop();
    }
    // 2) X → Y → X を数 ms のうちに（前は「中止」で止まった）
    {
        LoaderModel m; m.start();
        m.requestKit (0);
        waitState (m, LoaderModel::stateReady, 60);
        m.pendingKit.exchange (nullptr);
        waitIdle (m, 60);
        m.requestKit (2);
        std::this_thread::sleep_for (std::chrono::milliseconds (120));
        m.requestKit (3);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
        m.requestKit (2);
        waitIdle (m, 60);
        const int st = m.loadStateValue.load();
        printf ("     Studio → Stadium → Tight Maple → Stadium（0.12 秒・0.001 秒）: 状態 %s、表示「%s」、最後に出たキット %s、読んだ回数 %d\n",
                stateName (st), m.status().c_str(), m.publishedKit ? m.publishedKit->name.c_str() : "なし", m.loads.load());
        check (st == LoaderModel::stateReady && m.publishedKit && m.publishedKit->name == "Stadium Kit", "X→Y→X: 最後に選んだキットが読める");
        m.stop();
    }
    // 3) 読み終えた直後（つなぎ方の計算の途中）に別のキットを選んで、すぐ戻す
    {
        LoaderModel m; m.start();
        m.requestKit (4);
        waitState (m, LoaderModel::stateReady, 60);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));   // つなぎ方の計算の途中
        const bool wasRefining = m.refining.load();
        m.requestKit (5);
        std::this_thread::sleep_for (std::chrono::milliseconds (3));
        m.requestKit (4);
        waitIdle (m, 60);
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        int refined = 0, eligible = 0; if (m.publishedKit) countRefined (*m.publishedKit, refined, eligible);
        printf ("     Birch Kit のつなぎ方の計算中（%s）に Classic Sixties → 3 ms で Birch: 状態 %s、つなぎ方が決まった層 %d / %d、読んだ回数 %d\n",
                wasRefining ? "計算中だった" : "もう終わっていた", stateName (m.loadStateValue.load()), refined, eligible, m.loads.load());
        if (wasRefining) check (refined >= eligible * 9 / 10, "つなぎ方の計算を途中でやめても、同じキットに戻ったら最後まで計算する");
        m.stop();
    }
    // 4) 閉じる（デストラクター）の速さ: 読んでいる途中で閉じる
    {
        LoaderModel m; m.start();
        m.requestKit (6);
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        const auto t0 = Clock::now();
        m.stop();
        printf ("     読み込みの途中で閉じる: %.0f ms で止まった（stopThread の待ちは 8000 ms）\n", msSince (t0));
    }
}

//==============================================================================
// prefetch: 冷えたキットでのディスク待ち
static uint64_t pageins()
{
    task_events_info_data_t info; mach_msg_type_number_t cnt = TASK_EVENTS_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_EVENTS_INFO, (task_info_t) &info, &cnt) != KERN_SUCCESS) return 0;
    return (uint64_t) info.pageins;
}
static double residentFraction (const MicPart& p)
{
    if (p.file == nullptr || p.headFrames <= p.attackFrames) return 1.0;
    const long ps = getpagesize();
    const uint8_t* a = p.file->bytesAt (p.headStart + p.attackFrames);
    const size_t n = (size_t) (p.headFrames - p.attackFrames) * (size_t) p.channels * 2;
    const uintptr_t s = (uintptr_t) a & ~(uintptr_t) (ps - 1), e = ((uintptr_t) a + n + (uintptr_t) ps - 1) & ~(uintptr_t) (ps - 1);
    std::vector<char> v ((e - s) / (uintptr_t) ps);
    if (mincore ((void*) s, e - s, v.data()) != 0) return -1;
    size_t r = 0; for (char c : v) r += c & 1;
    return v.empty() ? 1.0 : (double) r / (double) v.size();
}

static double threadCpuMs()
{
    thread_basic_info_data_t info; mach_msg_type_number_t cnt = THREAD_BASIC_INFO_COUNT;
    mach_port_t t = mach_thread_self();
    thread_info (t, THREAD_BASIC_INFO, (thread_info_t) &info, &cnt);
    mach_port_deallocate (mach_task_self(), t);
    return (info.user_time.seconds + info.system_time.seconds) * 1000.0 + (info.user_time.microseconds + info.system_time.microseconds) / 1000.0;
}
static bool makeRealtime (double periodMs, double computeMs)   // Logic のオーディオスレッドと同じ種類の優先度
{
    mach_timebase_info_data_t tb; mach_timebase_info (&tb);
    auto toAbs = [&] (double ms) { return (uint32_t) (ms * 1e6 * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t p;
    p.period = toAbs (periodMs); p.computation = toAbs (computeMs); p.constraint = toAbs (periodMs); p.preemptible = 1;
    mach_port_t t = mach_thread_self();
    const kern_return_t r = thread_policy_set (t, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    mach_port_deallocate (mach_task_self(), t);
    return r == KERN_SUCCESS;
}

static void realtimeRun (Kit& kit, bool usePrefetcher, const std::string& label, float tuneAll, int loops = 1)
{
    Prefetcher pf; if (usePrefetcher) pf.start();
    auto h = std::make_unique<HostSim>(); h->prepare (44100); h->engine.setKit (&kit);
    h->engine.setPrefetcher (usePrefetcher ? &pf : nullptr);
    Settings st; for (auto& t : st.tune) t = tuneAll;
    std::vector<TEv> ev;
    { const auto one = popGroove(); const double len = 8 * 16 * (60.0 / 100 / 4); for (int l = 0; l < loops; ++l) for (auto e : one) { e.sec += l * len; ev.push_back (e); } }
    const double sr = 44100; const int block = 128;
    const double total = ev.back().sec + 1.0;
    const size_t N = (size_t) (total * sr);
    std::vector<float> L (N), R (N);
    std::vector<double> wall, off; wall.reserve (N / block + 1); off.reserve (N / block + 1);
    uint64_t pinsInRender = 0; bool rt = false;
    std::thread th ([&]
    {
        rt = makeRealtime (block * 1000.0 / sr, 1.0);
        size_t k = 0;
        const auto start = Clock::now();
        std::vector<HEv> be;
        for (size_t pos = 0, bi = 0; pos < N; pos += block, ++bi)
        {
            std::this_thread::sleep_until (start + std::chrono::microseconds ((int64_t) (bi * block * 1e6 / sr)));
            const int n = (int) std::min<size_t> (block, N - pos);
            be.clear();
            while (k < ev.size() && (size_t) std::llround (ev[k].sec * sr) < pos + (size_t) n)
            { be.push_back ({ (int) std::clamp<int64_t> (std::llround (ev[k].sec * sr) - (int64_t) pos, 0, n - 1), ev[k].st, ev[k].d1, ev[k].d2 }); ++k; }
            h->engine.setSettings (st);
            const uint64_t p0 = pageins();
            const double c0 = threadCpuMs();
            const auto t0 = Clock::now();
            h->process (L.data() + pos, R.data() + pos, n, be.data(), (int) be.size());
            const double w = msSince (t0);
            wall.push_back (w); off.push_back (std::max (0.0, w - (threadCpuMs() - c0)));
            pinsInRender += pageins() - p0;
        }
    });
    th.join();
    pf.stop();
    std::vector<double> s = wall; std::sort (s.begin(), s.end());
    const double budget = block * 1000.0 / sr;
    size_t over1 = 0, overB = 0, waits = 0; double maxOff = 0;
    for (size_t i = 0; i < wall.size(); ++i) { over1 += wall[i] > 1.0; overB += wall[i] > budget; waits += off[i] > 0.3; maxOff = std::max (maxOff, off[i]); }
    printf ("     %s%s: 1 ブロック（%d サンプル = %.1f ms）の処理 中央 %.3f / 99.9 %% %.2f / 最大 %.2f ms、1 ms 超え %zu、持ち時間超え %zu（全 %zu）、止まって待った（0.3 ms 超え）%zu 回・最長 %.2f ms、処理中のページ読み込み（プロセス全体）%llu\n",
            label.c_str(), rt ? "" : "（実時間の優先度にできなかった）", block, budget, s[s.size() / 2], s[(size_t) (s.size() * 0.999)], s.back(), over1, overB, s.size(), waits, maxOff, (unsigned long long) pinsInRender);
}

// キットの録音がメモリにどれだけ載っているか（ページに触らずに）
static double kitResidency (const std::string& name)
{
    ExsInstrument exs; if (! exs.load (kitPath (name))) return -1;
    size_t pages = 0, res = 0; const long ps = getpagesize(); std::vector<char> vec;
    for (auto& smp : exs.samples)
    {
        const std::string p = smp.folder + "/" + smp.file;
        const int fd = ::open (p.c_str(), O_RDONLY); if (fd < 0) continue;
        struct stat st {}; fstat (fd, &st);
        void* m = mmap (nullptr, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); ::close (fd);
        if (m == MAP_FAILED) continue;
        vec.resize (((size_t) st.st_size + ps - 1) / ps);
        if (mincore (m, (size_t) st.st_size, vec.data()) == 0) for (char c : vec) { ++pages; res += c & 1; }
        munmap (m, (size_t) st.st_size);
    }
    return pages ? (double) res / pages : -1;
}
static std::vector<std::string> coldKits()
{
    std::vector<std::pair<double, std::string>> v;
    for (auto& n : producerKitNames()) v.push_back ({ kitResidency (n), n });
    std::sort (v.begin(), v.end());
    std::vector<std::string> out;
    for (auto& p : v) { printf ("       %-20s メモリに %.1f %%\n", p.second.c_str(), p.first * 100); out.push_back (p.second); }
    return out;
}

static void testPrefetch()
{
    printf ("\n== ディスク待ち（mmap の先読み。冷えたキットで） ==\n");
    const auto cold = coldKits();
    // (a) 1 打ぶんの先読みの手間: 先読みは「マイク 1 の頭の残り → マイク 1 の尾 → マイク 2 の頭の残り → …」の順。最後のマイクの頭が届くまでの時間
    {
        double ms = 0; auto kit = loadKit (cold[0], &ms);
        if (kit)
        {
            printf ("     %s を冷えた状態から読む: %.0f ms（叩いた瞬間の写し %.1f MB）\n", cold[0].c_str(), ms, kit->attackPool.size() * 2 / 1048576.0);
            double worst = 0, sumAll = 0; int notes = 0; std::string worstName;
            const int keys[] = { 36, 38, 42, 46, 49, 57, 51, 48, 43 };
            for (int key : keys)
                for (auto& a : kit->artics)
                {
                    if (a.key != key || a.layers.empty()) continue;
                    const Layer& Lr = a.layers[a.layers.size() / 2];
                    double res = 0; int nm = 0;
                    for (int m = 0; m < NumMics; ++m) if (Lr.has[(size_t) m]) { res += residentFraction (Lr.mic[(size_t) m]); ++nm; }
                    const auto t0 = Clock::now();
                    double lastHeadDone = 0;
                    volatile uint8_t sink = 0;
                    for (int m = 0; m < NumMics; ++m)
                    {
                        if (! Lr.has[(size_t) m]) continue;
                        const MicPart& p = Lr.mic[(size_t) m];
                        auto touch = [&] (const uint8_t* q, size_t n) { uint8_t acc = 0; for (size_t i = 0; i < n; i += 4096) acc ^= q[i]; acc ^= q[n - 1]; sink = acc; };
                        if (p.headFrames > p.attackFrames) touch (p.file->bytesAt (p.headStart + p.attackFrames), (size_t) (p.headFrames - p.attackFrames) * (size_t) p.channels * 2);
                        lastHeadDone = msSince (t0);
                        if (p.tailFrames > 0) touch (p.file->bytesAt (p.tailStart), (size_t) p.tailFrames * (size_t) p.channels * 2);
                    }
                    const double all = msSince (t0);
                    (void) sink;
                    sumAll += all; ++notes;
                    if (lastHeadDone > worst) { worst = lastHeadDone; worstName = std::string (pieceName (a.piece)) + " " + a.name; }
                    printf ("       %-8s %-6s 層 %zu/%zu: 先読み前にメモリにあった %3.0f %%、最後のマイクの頭が届くまで %6.1f ms、全部 %6.1f ms\n",
                            pieceName (a.piece), a.name.c_str(), a.layers.size() / 2 + 1, a.layers.size(), 100.0 * res / std::max (1, nm), lastHeadDone, all);
                    break;
                }
            printf ("     最後のマイクの頭が届くまで最大 %.1f ms（%s）。写しの長さは 92.9 ms（TUNE +12 なら 46.4 ms）。1 打の先読み平均 %.1f ms\n", worst, worstName.c_str(), sumAll / std::max (1, notes));
        }
    }
    // (b) 実時間で鳴らす（先読みあり。冷えた状態から → 同じキットで 2 回目）
    {
        auto kit = loadKit (cold[1]);
        if (kit) { analyzeEdm (*kit, Settings {}); realtimeRun (*kit, true, cold[1] + "・先読みあり・冷えた状態から", 0); realtimeRun (*kit, true, cold[1] + "・先読みあり・2 回目", 0); }
    }
    {
        auto kit = loadKit (cold[2]);
        if (kit) { analyzeEdm (*kit, Settings {}); realtimeRun (*kit, true, cold[2] + "・先読みあり・TUNE +12・冷えた状態から", 12); }
    }
    // (c) 先読みなし（比べる用）
    {
        auto kit = loadKit (cold[3]);
        if (kit) { analyzeEdm (*kit, Settings {}); realtimeRun (*kit, false, cold[3] + "・先読みなし（比べる用）・冷えた状態から", 0); }
    }
    // (d) 長く鳴らす（ページがキャッシュから追い出されても大丈夫か。8 小節 × 4 回 ≒ 77 秒）
    {
        auto kit = loadKit (cold[4]);
        if (kit) { analyzeEdm (*kit, Settings {}); realtimeRun (*kit, true, cold[4] + "・先読みあり・77 秒続けて", 0, 4); }
    }
    // (e) 先読みの途中で mmap を閉じると（キットを消すと）どうなるか（子プロセスで）
    {
        const std::string f = "/Library/Application Support/Logic/EXS Factory Samples/Drum Kit Designer Consolidated/Drum Kit Designer/Tight Maple Kit HiHat_Stereo_consolidated.caf";
        fflush (stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            MappedCaf* c = new MappedCaf();
            if (! c->open (f)) { struct stat st {}; if (::stat (f.c_str(), &st) != 0) _exit (3); _exit (4); }
            Prefetcher pf; pf.start();
            pf.request (c->base, c->mapLen);    // 大きな範囲を先読みに渡す（数百 MB）
            std::this_thread::sleep_for (std::chrono::milliseconds (3));
            delete c;                           // キットを消す（munmap）
            std::this_thread::sleep_for (std::chrono::milliseconds (1500));
            pf.stop();
            _exit (0);
        }
        int status = 0; waitpid (pid, &status, 0);
        if (WIFSIGNALED (status)) printf ("     参考（fence を待たずに消すと＝直す前の流れ）: 先読みの途中で munmap → 子プロセスがシグナル %d（%s）で落ちる。今の流れは handoff の (3) で確かめる\n", WTERMSIG (status), strsignal (WTERMSIG (status)));
        else printf ("     参考（fence を待たずに消すと）: 子プロセスは終了コード %d（%s）\n", WEXITSTATUS (status), WEXITSTATUS (status) == 3 ? "ファイルなし" : WEXITSTATUS (status) == 4 ? "開けない" : "落ちなかった");
    }
}

//==============================================================================
// zones: 全キットの録音の終わりとつなぎ目（2026-11-12 の直し: refineSplices の前と後で比べる）
struct SpliceStats { size_t layers = 0, dip6 = 0, bump3 = 0, reHit = 0; double worstDip = 0, worstBump = 0, worstRe = 0; std::string worstDipWhat, worstBumpWhat, worstReWhat; };

// 層のつなぎ目を、全マイクを既定のバランスで混ぜた音で測る（refined = refineSplices の結果を使う）
static SpliceStats measureSplices (const Kit& kit, bool refined, uint32_t shiftEarlier = 0)
{
    SpliceStats st;
    for (auto& a : kit.artics)
        for (auto& Lr : a.layers)
        {
            int ref = -1;
            for (int m = 0; m < NumMics; ++m) if (Lr.has[(size_t) m] && Lr.mic[(size_t) m].tailFrames > 0) { ref = m; break; }
            if (ref < 0) continue;
            const LayerFx& f = kit.fx[(size_t) Lr.fx];
            SpliceParams sp;
            std::array<float, NumMics> tg; tg.fill (1.0f);
            if (refined)
            {
                sp.skip = f.spliceSkip.load(); sp.trim = f.spliceTrim.load(); sp.rho = f.spliceRho.load();
                for (int m = 0; m < NumMics; ++m) tg[(size_t) m] = f.tailGain[(size_t) m].load();
            }
            const uint32_t hf0 = Lr.mic[(size_t) ref].headFrames;
            const uint32_t H0 = hf0 - std::min (sp.trim, hf0 / 2);
            const uint32_t X = std::min<uint32_t> (kSpliceFrames, H0 / 4);
            if (H0 < 8820 + shiftEarlier || X < 200) continue;
            const uint32_t H = H0 - shiftEarlier;   // shiftEarlier > 0: 頭の中のつなぎ目のない所で同じ測り方（比べる用）
            const uint64_t xs = (uint64_t) H - X;
            auto mixRms = [&] (uint64_t from, uint64_t to)
            {
                double s2 = 0; int c = 0;
                for (uint64_t i = from; i < to; ++i)
                {
                    float sl = 0, sr = 0;
                    for (int m = 0; m < NumMics; ++m)
                    {
                        if (! Lr.has[(size_t) m]) continue;
                        const MicPart& p = Lr.mic[(size_t) m]; float l, r;
                        if (! p.fetch (i, l, r, sp, tg[(size_t) m])) continue;
                        if (p.channels == 1 && m != RoomB) { sl += l * p.gain * kit.pan[(size_t) a.piece][0]; sr += r * p.gain * kit.pan[(size_t) a.piece][1]; }
                        else { sl += l * p.gain; sr += r * p.gain; }
                    }
                    s2 += 0.5 * ((double) sl * sl + (double) sr * sr); ++c;
                }
                return c ? std::sqrt (s2 / c) : 0.0;
            };
            const double pre = mixRms (xs - 2 * X, xs), post = mixRms (H, H + 2 * X);
            if (pre <= 0 || post <= 0) continue;
            const double refLvl = dB (std::sqrt (0.5 * (pre * pre + post * post)));
            double lo = 1e9, hi = -1e9;
            for (uint64_t w = xs; w + 110 <= H; w += 110) { const double v = dB (mixRms (w, w + 220)) - refLvl; lo = std::min (lo, v); hi = std::max (hi, v); }
            const std::string what = std::string (pieceName (a.piece)) + " " + a.name + " 強さ " + std::to_string (Lr.velLo) + "-" + std::to_string (Lr.velHi);
            ++st.layers;
            if (lo < -6) ++st.dip6;
            if (hi > 3) ++st.bump3;
            if (lo < st.worstDip) { st.worstDip = lo; st.worstDipWhat = what; }
            if (hi > st.worstBump) { st.worstBump = hi; st.worstBumpWhat = what; }
            // 二度鳴り: 尾に替わった後の 100 ms が、つなぎ目の前の 100 ms より大きい
            const double before = mixRms (xs > 4410 ? xs - 4410 : 0, xs), after = mixRms (H, H + 4410);
            if (before > 0 && after > 0)
            {
                const double rise = dB (after) - dB (before);
                if (rise > 6) ++st.reHit;
                if (rise > st.worstRe) { st.worstRe = rise; st.worstReWhat = what; }
            }
        }
    return st;
}

static void testZones()
{
    printf ("\n== 録音の終わり（プチ）と頭 → 尾のつなぎ目（全キット、全マイクを混ぜた音。refineSplices の前 → 後） ==\n");
    size_t totDipB = 0, totDipA = 0, totBumpB = 0, totBumpA = 0, totLayers = 0, totDipC = 0, totBumpC = 0;
    for (auto& name : producerKitNames())
    {
        double ms = 0; auto kit = loadKit (name, &ms);
        if (! kit) continue;
        // 録音の終わり（尾があれば尾の終わり、なければ頭の終わり）
        size_t abrupt = 0, parts = 0;
        for (auto& a : kit->artics) for (auto& Lr : a.layers) for (int m = 0; m < NumMics; ++m)
        {
            if (! Lr.has[(size_t) m]) continue;
            const MicPart& p = Lr.mic[(size_t) m];
            ++parts;
            float pk = 0; for (uint32_t i = 0; i < p.attackFrames * (uint32_t) p.channels; ++i) pk = std::max (pk, std::abs ((float) p.attack[i] / 32768.0f));
            if (pk <= 0) continue;
            float endPk = 0, l, r;
            if (p.tailFrames > 0) { for (uint32_t i = p.tailFrames > 32 ? p.tailFrames - 32 : 0; i < p.tailFrames; ++i) { p.rawTail (i, l, r); endPk = std::max ({ endPk, std::abs (l), std::abs (r) }); } }
            else { for (uint32_t i = p.headFrames > 32 ? p.headFrames - 32 : 0; i < p.headFrames; ++i) { p.raw (i, l, r); endPk = std::max ({ endPk, std::abs (l), std::abs (r) }); } }
            if (dB (endPk) - dB (pk) > -60) ++abrupt;
        }
        const SpliceStats b = measureSplices (*kit, false);
        const SpliceStats ctl = measureSplices (*kit, false, 2205);   // つなぎ目より 50 ms 前（頭だけ）
        const auto t0 = Clock::now();
        refineSplices (*kit);
        const double rms_ = msSince (t0);
        const SpliceStats a = measureSplices (*kit, true);
        printf ("     %-20s 読み %5.0f ms、写し %5.1f MB、終わりが −60 dB より大きい %zu / %zu | つなぎ方の計算 %.0f ms | 層 %zu: −6 dB より深い谷 %zu → %zu（最悪 %.1f → %.1f dB）、+3 dB より高い山 %zu → %zu（最大 %+.1f → %+.1f dB）、二度鳴り %zu → %zu（最大 %+.1f → %+.1f dB）\n",
                name.c_str(), ms, kit->attackPool.size() * 2 / 1048576.0, abrupt, parts, rms_, a.layers, b.dip6, a.dip6, b.worstDip, a.worstDip, b.bump3, a.bump3, b.worstBump, a.worstBump, b.reHit, a.reHit, b.worstRe, a.worstRe);
        printf ("       比べる用（つなぎ目のない頭の中、同じ測り方）: −6 dB より深い谷 %zu（最悪 %.1f dB）、+3 dB より高い山 %zu（最大 %+.1f dB）\n", ctl.dip6, ctl.worstDip, ctl.bump3, ctl.worstBump);
        totDipC += ctl.dip6; totBumpC += ctl.bump3;
        if (a.worstDip < -9) printf ("       残った深い谷: %s（%.1f dB）\n", a.worstDipWhat.c_str(), a.worstDip);
        if (a.worstBump > 4.5) printf ("       残った高い山: %s（%+.1f dB）\n", a.worstBumpWhat.c_str(), a.worstBump);
        totDipB += b.dip6; totDipA += a.dip6; totBumpB += b.bump3; totBumpA += a.bump3; totLayers += a.layers;
        check (abrupt == 0, "  " + name + ": 録音の終わりで切れない（−60 dB 以下で終わる）");
        check (a.reHit < 10 && a.worstRe < 12.0, "  " + name + ": つなぎ方を整えた後も二度鳴りしない");
        // つなぎ目のない所（響きそのもののゆれ）の 1.5 倍か、整える前の、どちらか大きい方を超えない
        check (a.dip6 <= std::max (b.dip6, ctl.dip6 * 3 / 2 + 5) && a.bump3 <= std::max (b.bump3, ctl.bump3 * 3 / 2 + 5),
               "  " + name + ": 整えた後のつなぎ目の谷・山が、響きそのもののゆれ程度（つなぎ目のない所の 1.5 倍以内か、整える前以下）");
    }
    printf ("     全キットの合計: 層 %zu、−6 dB より深い谷 %zu → %zu（つなぎ目のない所 %zu）、+3 dB より高い山 %zu → %zu（つなぎ目のない所 %zu）\n", totLayers, totDipB, totDipA, totDipC, totBumpB, totBumpA, totBumpC);
}

//==============================================================================
// multi: マルチアウト（0.2.0）。出口ごとの和 = Stereo、つながっていない出口・モノの Main、出口の数の変化、切り替えのプチ、リセット、CPU
// 出口を 2*K 本のチャンネルとして持つ（バス b → [2b] [2b+1]。モノの Main は [0] だけ。無効なバスは 0 のまま）
static void runHostMulti (HostSim& h, const std::vector<TEv>& ev, double sr, int block, double totalSec, const Settings& base,
                          std::vector<std::vector<float>>& ch, const BlockHook& hook = {})
{
    constexpr int K = HostSim::K;
    const size_t N = (size_t) (totalSec * sr);
    ch.assign (2 * K, std::vector<float> (N, 0.0f));
    size_t k = 0;
    std::vector<HEv> be; be.reserve (4096);
    for (size_t pos = 0; pos < N; pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, N - pos);
        be.clear();
        while (k < ev.size() && (size_t) std::llround (ev[k].sec * sr) < pos + (size_t) n)
        {
            const int64_t at = std::llround (ev[k].sec * sr);
            be.push_back ({ (int) std::clamp<int64_t> (at - (int64_t) pos, 0, n - 1), ev[k].st, ev[k].d1, ev[k].d2 });
            ++k;
        }
        if (hook) hook ((int64_t) pos, n);
        h.engine.setSettings (base);
        std::array<float*, 2 * K> buf {};
        int bc = 0;
        for (int b = 0; b < h.busCount; ++b)
            if (h.enabled[(size_t) b])
                for (int c = 0; c < h.chans[(size_t) b]; ++c) buf[(size_t) bc++] = ch[(size_t) (2 * b + c)].data() + pos;
        gCountAllocs = gMeasureAllocs;
        h.process (buf.data(), bc, n, be.data(), (int) be.size());
        gCountAllocs = false;
    }
}

static double maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    double m = 0; for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i) m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}

static void testMulti (Kit& kit)
{
    constexpr int K = HostSim::K;
    static const char* outN[] = { "Main", "Kick", "Snare", "Room", "Toms", "Hi-Hat", "Cym" };
    printf ("\n== マルチアウト（0.2.0、processBlock の写し）: 出口の和 = Stereo・つながっていない出口・モノの Main・出口の数の変化・切り替え・リセット・CPU ==\n");
    const auto ev = popGroove();
    Settings st; st.kickBlend = 1; st.snareBlend = 1;
    analyzeEdm (kit, st);
    const double total = ev.back().sec + 2.0;

    // (1) 出口の和 = Stereo（1 本）。ブロックの大きさは同じにして比べる（変換ありだとブロックの頭のイベントが中の 1 サンプルずれうるため）
    struct Case { const char* name; std::array<bool, 6> sep; std::vector<int> off; bool mono; int block; };
    const Case cases[] = {
        { "スイッチ全部 OFF（7 本つなぐ）", { 0, 0, 0, 0, 0, 0 }, {}, false, 256 },
        { "キックだけ分ける", { 1, 0, 0, 0, 0, 0 }, {}, false, 37 },
        { "ルームだけ分ける", { 0, 0, 0, 0, 0, 1 }, {}, false, 512 },
        { "全部分ける（5 組＋ルーム）", { 1, 1, 1, 1, 1, 1 }, {}, false, 4096 },
        { "全部分ける・Snare と Hi-Hat の出口が無効", { 1, 1, 1, 1, 1, 1 }, { 2, 5 }, false, 256 },
        { "全部分ける・Main がモノ", { 1, 1, 1, 1, 1, 1 }, {}, true, 256 },
    };
    for (double sr : { 44100.0, 48000.0 })
        for (auto& c : cases)
        {
            std::vector<float> rL, rR;
            { auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit); runHost (*h, ev, sr, c.block, total, st, rL, rR); }
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
            h->setLayout (K, c.mono, c.off);
            h->sep = c.sep;
            std::vector<std::vector<float>> o;
            gAllocs = 0; gMeasureAllocs = true;
            runHostMulti (*h, ev, sr, c.block, total, st, o);
            gMeasureAllocs = false;
            const long allocs = gAllocs.load();
            double maxDiff = 0; bool fin = true;
            for (size_t i = 0; i < rL.size(); ++i)
            {
                if (c.mono)
                {
                    double m = o[0][i]; for (int b = 1; b < K; ++b) m += 0.5 * ((double) o[(size_t) (2 * b)][i] + o[(size_t) (2 * b + 1)][i]);
                    maxDiff = std::max (maxDiff, std::abs (m - 0.5 * ((double) rL[i] + rR[i])));
                }
                else
                {
                    double sl = 0, sr2 = 0; for (int b = 0; b < K; ++b) { sl += o[(size_t) (2 * b)][i]; sr2 += o[(size_t) (2 * b + 1)][i]; }
                    maxDiff = std::max ({ maxDiff, std::abs (sl - rL[i]), std::abs (sr2 - rR[i]) });
                }
                for (int q = 0; q < 2 * K; ++q) fin = fin && std::isfinite (o[(size_t) q][i]);
            }
            std::string pk;
            for (int b = 0; b < K; ++b)
            {
                const float p = peakOf (o[(size_t) (2 * b)], o[(size_t) (2 * b + 1)]);
                char t[48]; snprintf (t, sizeof t, "%s %s ", outN[b], p > 0 ? std::to_string ((int) std::lround (dB (p))).c_str() : "無音"); pk += t;
            }
            printf ("     %.1f kHz・ブロック %4d・%s: 出口の和と Stereo の差の最大 %.1e、つながった出口 %d、確保 %ld 回 | ピーク dBFS: %s\n", sr / 1000, c.block, c.name, maxDiff, h->connected, allocs, pk.c_str());
            check (fin && maxDiff < 1e-5 && allocs == 0, std::string ("  ") + c.name + ": 出口の和が Stereo と同じ（二重・抜けなし）・NaN なし・確保なし");
            if (c.sep[0] && c.sep[1] && c.sep[2] && c.sep[3] && c.sep[4] && c.sep[5] && c.off.empty() && ! c.mono)
                check (peakOf (o[0], o[1]) == 0.0f, "  全部分けたら Main は無音");
        }

    // (2) 出口の数が変わる（Logic の Stereo ⇄ Multi Output。Logic は止めて初期化し直す＝prepare）: 1 本 → 7 本（全部分ける）→ 1 本
    for (double sr : { 44100.0, 48000.0 })
    {
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
        auto r = std::make_unique<HostSim>(); r->prepare (sr); r->engine.setKit (&kit);   // 同じ所で prepare する Stereo（比べる用）
        h->sep = { 1, 1, 1, 1, 1, 1 };
        const int64_t t1 = (int64_t) (5.0 * sr), t2 = (int64_t) (11.0 * sr);
        bool d1 = false, d2 = false;
        int connA = 0, connB = 0, connC = 0;
        auto hookH = [&] (int64_t pos, int)
        {
            if (! d1 && pos >= t1) { d1 = true; connA = h->connected; h->setLayout (K, false); h->prepare (sr); }
            if (! d2 && pos >= t2) { d2 = true; connB = h->connected; h->setLayout (1, false); h->prepare (sr); }
        };
        std::vector<std::vector<float>> o;
        runHostMulti (*h, ev, sr, 256, total, st, o, [&] (int64_t pos, int n) { hookH (pos, n); });
        connC = h->connected;
        bool r1 = false, r2 = false;
        std::vector<std::vector<float>> ro;
        runHostMulti (*r, ev, sr, 256, total, st, ro, [&] (int64_t pos, int) { if (! r1 && pos >= t1) { r1 = true; r->prepare (sr); } if (! r2 && pos >= t2) { r2 = true; r->prepare (sr); } });
        double maxDiff = 0; bool fin = true;
        for (size_t i = 0; i < o[0].size(); ++i)
        {
            double sl = 0, sr2 = 0; for (int b = 0; b < K; ++b) { sl += o[(size_t) (2 * b)][i]; sr2 += o[(size_t) (2 * b + 1)][i]; }
            maxDiff = std::max ({ maxDiff, std::abs (sl - ro[0][i]), std::abs (sr2 - ro[1][i]) });
            for (int q = 0; q < 2 * K; ++q) fin = fin && std::isfinite (o[(size_t) q][i]);
        }
        const float kickPk = peakOf (std::vector<float> (o[2].begin() + t1, o[2].begin() + t2), std::vector<float> (o[3].begin() + t1, o[3].begin() + t2));
        printf ("     %.1f kHz: 1 本 → 7 本（5 秒）→ 1 本（11 秒）、それぞれ prepare し直し: 出口の和と Stereo の差の最大 %.1e、7 本の間の Kick の出口 %.1f dBFS、最後のつながった出口 %d（%d→%d→%d）\n",
                sr / 1000, maxDiff, dB (kickPk), connC, connA, connB, connC);
        check (fin && maxDiff < 1e-5 && kickPk > 0, "  出口の数が 1 ↔ 7 に変わっても、和は Stereo と同じ・NaN なし");
    }

    // (3) 出口のスイッチを鳴っている途中で替える（低い正弦波のサブだけ＝段差が見えやすい。808 Long、マイクは全部 −inf）
    for (double sr : { 44100.0, 48000.0 })
        for (int variant = 0; variant < 2; ++variant)
        {
            Settings s2; s2.kickBlend = 1; s2.kickSound = 1; s2.close = s2.oh = s2.room = s2.bleed = 0;
            analyzeEdm (kit, s2);
            std::vector<TEv> one { { 0.05, 0x90, 36, 120 } };
            const int block = 128;
            const int64_t tOn = (int64_t) std::llround (0.25 * sr / block) * block;
            const int64_t tOff = variant == 0 ? (int64_t) std::llround (0.50 * sr / block) * block : tOn + 2 * block;   // 1: 15 ms 以内にもう一度
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
            h->setLayout (K, false);
            std::vector<std::vector<float>> o;
            runHostMulti (*h, one, sr, block, 0.8, s2, o, [&] (int64_t pos, int) { h->sep[0] = pos >= tOn && pos < tOff; });
            std::vector<float> rL, rR;
            { auto r = std::make_unique<HostSim>(); r->prepare (sr); r->engine.setKit (&kit); runHost (*r, one, sr, block, 0.8, s2, rL, rR); }
            double maxDiff = 0; for (size_t i = 0; i < rL.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (o[0][i] + o[2][i] - rL[i]));
            const size_t lat = (size_t) h->latency();
            const double normal = maxStep (rL, (size_t) tOn - 2000, (size_t) tOn);
            const double stepMainOn = maxStep (o[0], (size_t) tOn + lat - 4, (size_t) tOn + lat + 800), stepKickOn = maxStep (o[2], (size_t) tOn + lat - 4, (size_t) tOn + lat + 800);
            const double stepMainOff = maxStep (o[0], (size_t) tOff + lat - 4, (size_t) tOff + lat + 800), stepKickOff = maxStep (o[2], (size_t) tOff + lat - 4, (size_t) tOff + lat + 800);
            printf ("     %.1f kHz・%s: Main / Kick の出口の 1 サンプルの段差 ON の所 %.4f / %.4f、OFF の所 %.4f / %.4f（ふだんの最大 %.4f）、Main＋Kick と Stereo の差 %.1e\n",
                    sr / 1000, variant == 0 ? "キックの出口を ON → 0.25 秒後に OFF" : "ON → 5.8 ms 後に OFF（15 ms の移しの途中）", stepMainOn, stepKickOn, stepMainOff, stepKickOff, normal, maxDiff);
            if (variant == 0) check (std::max ({ stepMainOn, stepKickOn, stepMainOff, stepKickOff }) < normal * 3.0 && maxDiff < 1e-5, "  出口を替えても、それぞれの出口にプチが出ない（15 ms で移す）");
            else check (std::max ({ stepMainOff, stepKickOff }) < normal * 3.0, "  移している途中でもう一度替えても、出口にプチが出ない");
        }

    // (4) Logic のリセット（reset → hardReset）の直後の一打: キックを分けているのに、頭が Main に出ないか
    for (double sr : { 44100.0, 48000.0 })
    {
        std::vector<TEv> evs { { 0.05, 0x90, 36, 120 }, { 1.0 + 0.0, 0x90, 36, 120 } };   // 2 打目はリセットの直後（ブロックの頭）
        const int block = 256;
        const int64_t tReset = (int64_t) std::llround (1.0 * sr / block) * block;
        evs[1].sec = (double) tReset / sr;
        for (int doReset = 0; doReset < 2; ++doReset)
        {
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit);
            h->setLayout (K, false); h->sep = { 1, 0, 0, 0, 0, 0 };
            std::vector<std::vector<float>> o;
            runHostMulti (*h, evs, sr, block, 1.5, st, o, [&] (int64_t pos, int) { if (doReset && pos == tReset) h->hostReset(); });
            const size_t a = (size_t) tReset + (size_t) h->latency(), b = a + (size_t) (0.02 * sr);
            const double main = rms (o[0], a, b) + rms (o[1], a, b), kick = rms (o[2], a, b) + rms (o[3], a, b);
            printf ("     %.1f kHz・%s: 2 打目の最初の 20 ms、Main %.1f dBFS / Kick の出口 %.1f dBFS（Main に漏れた分 %+.1f dB）\n",
                    sr / 1000, doReset ? "リセットの直後" : "リセットなし", dB (main / 2), dB (kick / 2), dB (main) - dB (kick));
            if (doReset) check (dB (main) - dB (kick) < -40.0, "  リセットの直後でも、分けた部品の頭が Main に漏れない");
        }
    }

    // (5) CPU: 重い譜面、7 本つないで全部分ける vs 1 本
    {
        Settings s3; s3.kickBlend = 1; s3.snareBlend = 1; for (auto& t : s3.tune) t = 0.5f; for (auto& l : s3.length) l = 0.6f;
        analyzeEdm (kit, s3);
        const auto dev = denseGroove (8);
        const double tot = dev.back().sec + 2.0;
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            double ms1 = 0, ms7 = 0;
            { auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit); std::vector<float> L, R; const auto t0 = Clock::now(); runHost (*h, dev, sr, 256, tot, s3, L, R); ms1 = msSince (t0); }
            { auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (&kit); h->setLayout (K, false); h->sep = { 1, 1, 1, 1, 1, 1 };
              std::vector<std::vector<float>> o; const auto t0 = Clock::now(); runHostMulti (*h, dev, sr, 256, tot, s3, o); ms7 = msSince (t0); }
            printf ("     CPU %.1f kHz: 1 本 %.1f %% / 7 本全部分ける %.1f %%（1 コアに対して、%.1f 秒の音）\n", sr / 1000, 100.0 * ms1 / (tot * 1000), 100.0 * ms7 / (tot * 1000), tot);
        }
    }
}

//==============================================================================
// edm: 自動の大きさ
static void testEdm()
{
    printf ("\n== EDM の層の自動の大きさ（全キット） ==\n");
    for (auto& name : producerKitNames())
    {
        auto kit = loadKit (name);
        if (! kit) continue;
        float sgMin = 1e9f, sgMax = 0, edMin = 1e9f, edMax = 0, sdMin = 1e9f, sdMax = 0;
        const auto t0 = Clock::now(); int runs = 0;
        for (int ks = 0; ks < kNumKickSounds; ++ks)
            for (int note : { 0, 11 })
                for (float tune : { -12.0f, 12.0f })
                {
                    Settings st; st.kickSound = ks; st.kickNote = note; st.snareSound = ks; st.snareTune = tune;
                    analyzeEdm (*kit, st); ++runs;
                    for (auto& a : kit->artics)
                        for (auto& Lr : a.layers)
                        {
                            const LayerFx& f = kit->fx[(size_t) Lr.fx];
                            if (a.piece == Kick) { sgMin = std::min (sgMin, std::abs (f.subGain.load())); sgMax = std::max (sgMax, std::abs (f.subGain.load())); sdMin = std::min (sdMin, f.subDelay.load()); sdMax = std::max (sdMax, f.subDelay.load()); }
                            if (a.piece == Snare && articKind (a.name) == 0) { edMin = std::min (edMin, f.edmGain.load()); edMax = std::max (edMax, f.edmGain.load()); }
                        }
                }
        const double per = msSince (t0) / runs;
        // いちばん強いキック・スネア（127）を BLEND 1 と 0 で比べる
        Settings st; st.kickBlend = 1; st.snareBlend = 1; analyzeEdm (*kit, st);
        auto one = [&] (int note, int vel, float kb, float sb, std::vector<float>& L, std::vector<float>& R)
        {
            Settings s = st; s.kickBlend = kb; s.snareBlend = sb;
            auto h = std::make_unique<HostSim>(); h->prepare (44100); h->engine.setKit (kit.get());
            runHost (*h, { { 0.01, 0x90, (uint8_t) note, (uint8_t) vel } }, 44100, 256, 1.2, s, L, R);
        };
        std::vector<float> a, b, c, d;
        one (36, 127, 0, 0, a, b); const float k0 = peakOf (a, b); one (36, 127, 1, 0, c, d); const float k1 = peakOf (c, d);
        one (38, 127, 0, 0, a, b); const float s0 = peakOf (a, b); one (38, 127, 0, 1, c, d); const float s1 = peakOf (c, d);
        printf ("     %-20s サブの大きさ %.3f〜%.3f、サブの開始 %.0f〜%.0f サンプル、EDM スネア %.3f〜%.3f（1 回の計算 %.0f ms）| 127 のキック %.1f → %.1f dBFS、スネア %.1f → %.1f dBFS\n",
                name.c_str(), sgMin, sgMax, sdMin, sdMax, edMin, edMax, per, dB (k0), dB (k1), dB (s0), dB (s1));
        check (std::isfinite (sgMax) && std::isfinite (edMax) && dB (k1) - dB (k0) < 9 && dB (s1) - dB (s0) < 9, "  " + name + ": BLEND 100 % でピークが 9 dB 以上増えない・NaN なし");
    }
    // サイドスティック（37）にもクラップが付く / BLEND 50 % の低音
    {
        auto kit = loadKit ("Studio Kit");
        if (! kit) return;
        Settings st; analyzeEdm (*kit, st);
        auto band = [] (const std::vector<float>& x, double lo, double hi)
        {
            Biquad h1, h2, l1, l2; h1.highpass (44100, lo, 0.707); h2 = h1; l1.lowpass (44100, hi, 0.707); l2 = l1;
            double s = 0; for (float v : x) { const float y = l2.run (l1.run (h2.run (h1.run (v)))); s += (double) y * y; }
            return 10 * std::log10 (s + 1e-30);
        };
        auto one = [&] (int note, int vel, float kb, float sb)
        {
            Settings s = st; s.kickBlend = kb; s.snareBlend = sb;
            auto h = std::make_unique<HostSim>(); h->prepare (44100); h->engine.setKit (kit.get());
            std::vector<float> L, R; runHost (*h, { { 0.01, 0x90, (uint8_t) note, (uint8_t) vel } }, 44100, 256, 1.0, s, L, R);
            return L;
        };
        const auto ss0 = one (37, 100, 0, 0), ss1 = one (37, 100, 0, 1);
        printf ("     サイドスティック（37・強さ 100）に SNARE BLEND 100 %%: 400 Hz〜8 kHz %+.1f dB（クラップが付く）\n", band (ss1, 400, 8000) - band (ss0, 400, 8000));
        const auto k0 = one (36, 110, 0, 0), k5 = one (36, 110, 0.5f, 0), k1 = one (36, 110, 1, 0);
        printf ("     キックの 30〜90 Hz: BLEND 0 %% %.1f dB / 50 %% %.1f dB / 100 %% %.1f dB（50 %% の山 %+.1f dB）\n",
                band (k0, 30, 90), band (k5, 30, 90), band (k1, 30, 90), band (k5, 30, 90) - std::max (band (k0, 30, 90), band (k1, 30, 90)));
    }
}

//==============================================================================
// cpu
static void testCpu()
{
    printf ("\n== CPU（重い譜面・全部の TUNE +0.5 半音＝補間あり・LENGTH 60 %%・BLEND 100 %%） ==\n");
    auto kit = loadKit ("Studio Kit");
    if (! kit) return;
    Settings st; st.kickBlend = 1; st.snareBlend = 1;
    for (auto& t : st.tune) t = 0.5f;
    for (auto& l : st.length) l = 0.6f;
    analyzeEdm (*kit, st);
    const auto ev = denseGroove (8);
    const double total = ev.back().sec + 2.0;
    int maxVoices = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            if (pass == 0 && sr != 44100.0) continue;
            auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (kit.get());
            std::vector<float> L, R;
            auto hook = [&] (int64_t, int) { maxVoices = std::max (maxVoices, h->engine.activeVoices()); };
            const auto t0 = Clock::now();
            runHost (*h, ev, sr, 256, total, st, L, R, false, {}, hook);
            const double ms = msSince (t0);
            printf ("     %s %.1f kHz: %.1f 秒の音を %.0f ms で（1 コアの %.1f %%）、同時に鳴った声 最大 %d\n",
                    pass == 0 ? "1 回目（ディスクから）" : "2 回目（メモリに載った後）", sr / 1000, total, ms, 100.0 * ms / (total * 1000.0), maxVoices);
        }
    // 同じ設定で TUNE 0・LENGTH 100 %（補間なし）
    {
        Settings s2 = st; for (auto& t : s2.tune) t = 0; for (auto& l : s2.length) l = 1;
        auto h = std::make_unique<HostSim>(); h->prepare (48000); h->engine.setKit (kit.get());
        std::vector<float> L, R;
        const auto t0 = Clock::now(); runHost (*h, ev, 48000, 256, total, s2, L, R); const double ms = msSince (t0);
        printf ("     TUNE 0・LENGTH 100 %%（補間なし）48 kHz: 1 コアの %.1f %%\n", 100.0 * ms / (total * 1000.0));
    }
}

//==============================================================================
// extreme
static void testExtreme (Kit& kit)
{
    printf ("\n== つまみを端に・無音・キットなし・prepare のやり直し ==\n");
    std::vector<TEv> all;   // 全部の部品を同時に 127 で
    for (int n : { 36, 38, 42, 49, 57, 51, 48, 47, 45, 43 }) all.push_back ({ 0.02, 0x90, (uint8_t) n, 127 });
    auto go = [&] (const Settings& s, const std::vector<TEv>& ev, double sr, std::vector<float>& L, std::vector<float>& R, const Kit* k, double sec = 4.0)
    {
        analyzeEdm (kit, s);
        auto h = std::make_unique<HostSim>(); h->prepare (sr); h->engine.setKit (k);
        runHost (*h, ev, sr, 512, sec, s, L, R);
    };
    std::vector<float> L, R;
    { Settings s; go (s, all, 48000, L, R, &kit); printf ("     既定のつまみ・全部の部品を同時に 127: ピーク %.1f dBFS\n", dB (peakOf (L, R))); }
    {
        Settings s; s.close = s.oh = s.room = s.bleed = s.output = 3.98f; for (auto& v : s.vol) v = 3.98f; s.kickBlend = s.snareBlend = 1;
        go (s, all, 48000, L, R, &kit); printf ("     全部 +12 dB・BLEND 100 %%・全部 127: ピーク %.1f dBFS、NaN %s\n", dB (peakOf (L, R)), allFinite (L, R) ? "なし" : "あり");
    }
    for (float tune : { -12.0f, 12.0f })
    {
        Settings s; for (auto& t : s.tune) t = tune; s.snareTune = tune; s.kickBlend = s.snareBlend = 1; s.kickNote = tune > 0 ? 11 : 0; s.kickSound = tune > 0 ? 2 : 1;
        go (s, popGroove(), 96000, L, R, &kit, 22);
        float tail = peakOf (L, R, L.size() - 48000);
        printf ("     TUNE %+.0f（全部・EDM も）96 kHz: ピーク %.1f dBFS、NaN %s、最後の 0.5 秒 %.1f dBFS\n", tune, dB (peakOf (L, R)), allFinite (L, R) ? "なし" : "あり", dB (tail));
        check (allFinite (L, R), "TUNE " + std::to_string ((int) tune) + ": NaN なし");
    }
    {
        Settings s; for (auto& l : s.length) l = 0; s.close = s.oh = s.room = s.bleed = 0;
        go (s, popGroove(), 44100, L, R, &kit, 22); printf ("     LENGTH 0 %%・マイク全部 −inf: ピーク %.1f dBFS、NaN %s\n", dB (peakOf (L, R)), allFinite (L, R) ? "なし" : "あり");
    }
    { Settings s; s.kickBlend = s.snareBlend = 1; go (s, {}, 48000, L, R, &kit, 2); printf ("     何も鳴らさない: ピーク %s\n", peakOf (L, R) == 0 ? "ちょうど 0" : std::to_string (peakOf (L, R)).c_str()); check (peakOf (L, R) == 0, "無音のときは 0"); }
    { Settings s; s.kickBlend = s.snareBlend = 1; go (s, popGroove(), 48000, L, R, nullptr, 22); printf ("     キットなし（Mac に録音がない）: EDM だけ ピーク %.1f dBFS、NaN %s\n", dB (peakOf (L, R)), allFinite (L, R) ? "なし" : "あり"); check (allFinite (L, R), "キットなしでも NaN なし"); check (peakOf (L, R) > 0.001f, "キットなしでも EDM の層は鳴る（KitEngine.h の noteOn の説明どおり）"); }
    // prepare（prepareToPlay）を鳴っている途中で何度も（サンプルレートも変える）
    {
        auto h = std::make_unique<HostSim>(); h->engine.setKit (&kit);
        Settings s; s.kickBlend = s.snareBlend = 1;
        const auto ev = popGroove();
        bool fin = true; float pk = 0;
        const double rates[] = { 48000, 96000, 44100, 192000, 88200 };
        for (int i = 0; i < 5; ++i)
        {
            h->prepare (rates[i]);
            std::vector<float> L2, R2; runHost (*h, ev, rates[i], 333, 3.0, s, L2, R2);
            fin = fin && allFinite (L2, R2); pk = std::max (pk, peakOf (L2, R2));
        }
        printf ("     prepareToPlay を 5 回（48→96→44.1→192→88.2 kHz）: NaN %s、ピーク %.1f dBFS\n", fin ? "なし" : "あり", dB (pk));
        check (fin, "prepare のやり直しで NaN なし");
    }
}

int main (int argc, char** argv)
{
    const std::string what = argc >= 2 ? argv[1] : "all";
    const bool full = argc >= 3 && std::string (argv[2]) == "full";
    if (what == "loader") { testLoader (full); }
    else if (what == "prefetch") testPrefetch();
    else if (what == "zones") testZones();
    else if (what == "edm") testEdm();
    else if (what == "cpu") testCpu();
    else if (what == "multi") { auto kit = loadKit ("Studio Kit"); if (kit) testMulti (*kit); }
    else
    {
        auto kit = loadKit ("Studio Kit");
        if (! kit) return 1;
        if (what == "rates" || what == "all") testRates (*kit);
        if (what == "clicks" || what == "all") testClicks (*kit);
        if (what == "extreme" || what == "all") testExtreme (*kit);
        if (what == "handoff" || what == "all")
        {
            auto b = loadKit ("Modern Maple Kit"), c = loadKit ("Stadium Kit");
            if (b && c) testHandoff (*kit, *b, *c);
        }
        if (what == "all") { testMulti (*kit); testLoader (false); testZones(); testEdm(); testCpu(); testPrefetch(); }
    }
    printf (gFails ? "\n%d 個 NG\n" : "\n全部 OK\n", gFails);
    return gFails ? 1 : 0;
}
