// test_inspect.cpp — AIO Effecter の DSP 検査（DSP 検査係。1 回目 2026-11-19、2 回目・3 回目 2026-11-19 で更新）。JUCE なし・ビルドには入らない
//   clang++ -std=c++17 -O2 -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_inspect.cpp -o build/aiofx/ti
//   ./ti <mode>
//     info    … 画面のつまみと表（DynTables.h）の数・初期値の食い違い、値の文字
//     fuzz N  … 0.2.0 の部品: 全部のつまみ 0 / 1 / 初期値 / ランダム N 通り × 入力 6 種: NaN・山・+24 dBFS 超え・残り
//     fuzzall N … 全部の部品（0.1.0 の 7 個も）× レート 44.1〜192 k × ブロック 1〜2048 × 入力 9 種、途中でつまみも動かす（ASan で動かす）
//     click   … つまみを一気に / 0.3 秒で動かしたときのプチッ（220 Hz、ふだん 1〜3、10 以上はプチッ）
//     os / alias / mix … 4 倍の特性・折り返し（半端な周波数で。倍音と重なる折り返しは数えない）・MIX の途中のくし形
//     max     … MAXIMIZER: 遅れ（位相で）・天井（本体）・最後の clamp の前の山と clamp にかかった割合（写し MaxCopy。本体と 1 ビットも違わないかを毎回確かめる）・歪み・折り返し・つまみの切り替え
//     lat     … 全部の部品の遅れの申告と実際（1 kHz の位相。100 Hz は棚の位相が混じるので目安）
//     trans   … TRANSIENT: 頭の持ち上げ・SUSTAIN の床。trans2 … CLIP の ON/OFF（小さい音で 1 ビットも変わらないか）・折り返し・遅れ・SUSTAIN の静的な分
//     rack    … ラックでの MAXIMIZER の ON/OFF・入れ替え・遅れの合計
//     rack2   … 全部の部品の OFF → ON → OFF（抜け・プチッ）、遅れの違う部品の選び直し、8 段の遅れの合計と実際、遅れのない部品の古い響き
//     gate2   … CHANNEL DYN のゲート（HOLD・バタつき・閉じている間の切り替え）、2 段の戻りの割合の切り替え
//     onset   … 無音のあといきなり始まる打音で、コンプの頭がどれだけ跳ねるか（本物の Guitar Rig も跳ねる。比べるのは build/aiofx の記録で）
//     block / reset / rates / cpu / cpu2 / pedal
//     engine / lookmix / esteps … エンジンを表を通さず直接（飽和の形・ランダム・先読み・段の切り替え）
//     targets … 本人が名前を挙げた大きく上げる設定の音量
//     rackreset … ホストの reset のあと遅れのある段のドライに前の音が残るか
//     resetleak … 音が流れている最中の reset で、遅れのある部品やフィルターを通らない音（高い所）が漏れないか（DSP 検査 0.8.0）
//     maxlow  … 3 回目: MAXIMIZER の低音（34 Hz より下で窓の最大の列が溢れると歪みが段で増える。C1 と C#1 を比べる）
//     swapin  … 3 回目: 再生中に部品を選び直して MAXIMIZER などが入る瞬間のプチッ
//   表（DynTables.h）を作り直したら、ASan でも動かす（配列の外に書いていないか）:
//   clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -I MyPlugins/AIOEffecter/Source MyPlugins/AIOEffecter/tools/test_inspect.cpp -o /tmp/ti && /tmp/ti info && /tmp/ti fuzzall 24
#include "fx/Registry.h"
#include "fx/Rack.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include <cmath>

using namespace aiofx;

struct Sig
{
    std::vector<float> L, R;
    size_t size() const { return L.size(); }
};

inline Sig zeros (double sr, double secs)
{
    Sig s; s.L.assign ((size_t) (sr * secs), 0.0f); s.R = s.L; return s;
}
inline Sig impulse (double sr, double secs, float amp = 1.0f)
{
    Sig s = zeros (sr, secs); s.L[0] = s.R[0] = amp; return s;
}
inline Sig sine (double sr, double secs, double hz, float amp)
{
    Sig s = zeros (sr, secs);
    for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = amp * (float) std::sin (2.0 * M_PI * hz * (double) i / sr);
    return s;
}
inline Sig noise (double sr, double secs, float amp, uint32_t seed = 1)
{
    Sig s = zeros (sr, secs);
    std::mt19937 g (seed); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (size_t i = 0; i < s.size(); ++i) { s.L[i] = amp * d (g); s.R[i] = amp * d (g); }
    return s;
}
inline void append (Sig& a, const Sig& b)
{
    a.L.insert (a.L.end(), b.L.begin(), b.L.end());
    a.R.insert (a.R.end(), b.R.begin(), b.R.end());
}

// render.cpp と同じ作り方（なめらかさを初期値のまま確定してから）
inline std::unique_ptr<Module> makeSnapped (const char* id, double sr, int block, const std::vector<float>& kv)
{
    const int t = typeForId (id);
    auto m = makeType (t);
    const auto& inf = *infoFor (t);
    m->prepare (sr, block);
    for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, k < (int) kv.size() ? kv[(size_t) k] : inf.knobs[(size_t) k].def);
    m->prepare (sr, block);
    for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, k < (int) kv.size() ? kv[(size_t) k] : inf.knobs[(size_t) k].def);
    m->reset();
    return m;
}
inline std::vector<float> defaults (const char* id)
{
    std::vector<float> v;
    for (auto& k : infoFor (typeForId (id))->knobs) v.push_back (k.def);
    return v;
}

inline void runModule (Module& m, Sig& s, int block)
{
    for (size_t off = 0; off < s.size(); off += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, s.size() - off);
        m.process (s.L.data() + off, s.R.data() + off, n);
    }
}

struct Stats { float peak = 0; long bad = 0; double rms = 0; };
inline Stats stats (const Sig& s, size_t from = 0, size_t to = (size_t) -1)
{
    Stats st; double e = 0; size_t n = 0;
    to = std::min (to, s.size());
    for (size_t i = from; i < to; ++i)
        for (float x : { s.L[i], s.R[i] })
        {
            if (! std::isfinite (x)) { ++st.bad; continue; }
            st.peak = std::max (st.peak, std::abs (x)); e += (double) x * x; ++n;
        }
    st.rms = n ? std::sqrt (e / (double) n) : 0.0;
    return st;
}
inline double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-30)); }

// 残響時間: Schroeder の逆積分で −5 → −35 dB（T30）× 2
inline double rt60 (const Sig& s, double sr, size_t from = 0)
{
    std::vector<double> e (s.size() - from);
    double acc = 0;
    for (size_t i = s.size(); i-- > from;) { acc += (double) s.L[i] * s.L[i] + (double) s.R[i] * s.R[i]; e[i - from] = acc; }
    if (acc <= 0) return 0;
    const double t0 = e[0];
    long i5 = -1, i35 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double d = 10.0 * std::log10 (e[i] / t0 + 1e-300);
        if (i5 < 0 && d <= -5.0) i5 = (long) i;
        if (i35 < 0 && d <= -35.0) { i35 = (long) i; break; }
    }
    if (i5 < 0 || i35 < 0) return -1;   // 35 dB 下がらない
    return 2.0 * (double) (i35 - i5) / sr;
}

inline double nowSec()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

static const char* kNew[] = { "touchflt", "peakflt" };   // 0.9.0 の音量で動くフィルター（前の版の部品は kAll に）

#include <cstring>
#include <complex>
#include <map>
#include <functional>

using Want = std::array<SlotKnobs, kSlots>;

static Sig pluckSeq (double sr, double secs, float peak)
{
    Sig s = zeros (sr, secs);
    const double f0s[4] { 110.0, 146.8, 196.0, 164.8 };
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / sr; const int k = (int) (t / 0.5) % 4; const double tt = std::fmod (t, 0.5);
        double v = 0;
        for (int h = 1; h <= 8; ++h) v += std::sin (2 * M_PI * f0s[k] * h * tt) / h;
        s.L[i] = s.R[i] = (float) (peak * 0.55 * v * std::exp (-tt * 4.0));
    }
    return s;
}

static const DynSpec* specFor (const std::string& id)
{
    if (id == "tubepush") return &specSupercharger();
    if (id == "tubepushxl") return &specSuperchargerGT();
    if (id == "fetcomp") return &specVC76();
    if (id == "optocomp") return &specVC2A();
    if (id == "vcacomp") return &specVC160();
    if (id == "buscomp") return &specSolidBuscomp();
    if (id == "chandyn") return &specSolidDynamics();
    if (id == "quickcomp") return &specFastComp();
    if (id == "pedalcomp") return &specStompCompressor();
    return nullptr;
}

static std::vector<float> randomKnobs (const char* id, std::mt19937& g, bool respectSteps)
{
    const auto& inf = *infoFor (typeForId (id));
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<float> v;
    for (auto& k : inf.knobs)
    {
        float x = u (g);
        const int pick = (int) (u (g) * 6);   // 端をよく選ぶ
        if (pick == 0) x = 0.0f; else if (pick == 1) x = 1.0f;
        if (respectSteps && k.steps > 1) x = std::round (x * (float) (k.steps - 1)) / (float) (k.steps - 1);
        v.push_back (x);
    }
    return v;
}

static std::string kvStr (const std::vector<float>& kv)
{
    std::string s = "{";
    for (size_t i = 0; i < kv.size(); ++i) { char b[16]; std::snprintf (b, sizeof b, "%s%.2f", i ? "," : "", kv[i]); s += b; }
    return s + "}";
}

// 220 Hz のきれいなサインでのプチッの目安（前回の検査と同じ式）
static float cleanliness (const std::vector<float>& x, size_t a, size_t b, double sr, double f = 220.0)
{
    const double w = 2 * M_PI * f / sr; const size_t win = (size_t) (0.005 * sr), ctx = (size_t) (0.02 * sr);
    float worst = 0;
    for (size_t s = std::max (a, ctx + 2); s + win < std::min (b, x.size() - ctx); s += win)
    {
        float d2 = 0, pk = 0;
        for (size_t i = s; i < s + win; ++i) d2 = std::max (d2, std::abs (x[i] - 2 * x[i - 1] + x[i - 2]));
        for (size_t i = s - ctx; i < s + win + ctx; ++i) pk = std::max (pk, std::abs (x[i]));
        if (pk > 1e-4f) worst = std::max (worst, (float) (d2 / (pk * w * w)));
    }
    return worst;
}

// 1 Hz 刻みの DFT の 1 点（ハン窓）
static double dftMag (const std::vector<float>& x, size_t off, size_t N, double f, double sr)
{
    std::complex<double> acc = 0; const double w = 2 * M_PI * f / sr;
    for (size_t i = 0; i < N; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) N);
        acc += (double) x[off + i] * win * std::polar (1.0, -w * (double) i);
    }
    return std::abs (acc);
}
// 正弦波 f0 を通した出力の「倍音」と「折り返し」の最大（基音に対する dB）
// 正弦波 f0 を通した出力の「倍音」と「折り返し」の最大（基音に対する dB）。
// 折り返しが帯域内の倍音（か基音）から 30 Hz 以内に落ちるときは区別できないので数えない（f0 は 4999 / 7001 Hz など半端な値で）
static void aliasReport (const std::vector<float>& y, size_t off, double sr, double f0, double& harm, double& alias, double& aliasF)
{
    const size_t N = (size_t) sr;
    const double fund = dftMag (y, off, N, f0, sr);
    harm = 0; alias = 0; aliasF = 0;
    std::vector<double> inband; for (int n = 1; n * f0 < sr / 2; ++n) inband.push_back (n * f0);
    for (int n = 2; n <= 40; ++n)
    {
        double f = std::fmod ((double) n * f0, sr); if (f > sr / 2) f = sr - f;
        if (f < 20 || f > sr / 2 - 20) continue;
        const double mg = dftMag (y, off, N, f, sr) / std::max (1e-30, fund);
        if ((double) n * f0 > sr / 2)
        {
            bool clash = false; for (double h : inband) if (std::abs (h - f) < 30.0) clash = true;
            if (clash) continue;
            if (mg > alias) { alias = mg; aliasF = f; }
        }
        else harm = std::max (harm, mg);
    }
}


// ============================================================================
static void modeInfo()
{
    std::printf ("== 部品の情報と表の食い違い ==\n");
    for (const char* id : kNew)
    {
        const auto& inf = *infoFor (typeForId (id));
        auto* sp = specFor (id);
        std::printf ("  %-10s つまみ %2zu", id, inf.knobs.size());
        if (sp)
        {
            int withTable = 0; for (int k = 0; k < sp->nKnobs; ++k) if (sp->knobs[k].points > 0) ++withTable;
            std::printf ("  表の nKnobs %2d（表あり %d 本）", sp->nKnobs, withTable);
            if (sp->nKnobs != (int) inf.knobs.size()) std::printf ("  ★数が違う");
            for (int k = 0; k < std::min (sp->nKnobs, (int) inf.knobs.size()); ++k)
                if (std::abs (sp->knobDef[k] - inf.knobs[(size_t) k].def) > 1e-3f)
                    std::printf ("  ★初期値違い k%d(%s): 表 %.3f / 画面 %.3f", k, inf.knobs[(size_t) k].name, sp->knobDef[k], inf.knobs[(size_t) k].def);
        }
        std::printf ("\n");
        // 値の文字
        for (size_t k = 0; k < inf.knobs.size(); ++k)
        {
            const auto& ki = inf.knobs[k];
            float all[kMaxKnobs]; for (int j = 0; j < kMaxKnobs; ++j) all[j] = j < (int) inf.knobs.size() ? inf.knobs[(size_t) j].def : 0.5f;
            auto tx = [&] (float v) { return ki.text ? ki.text (v) : ki.ctext ? ki.ctext (v, all) : fmt (v, 2, ""); };
            std::printf ("      %-11s 0 → %-10s 0.5 → %-10s 1 → %-10s\n", ki.name, tx (0.0f).c_str(), tx (0.5f).c_str(), tx (1.0f).c_str());
        }
    }
}

// ============================================================================
struct FuzzWorst { float peak = 0; std::string at; long bad = 0; std::string badAt; float tail = 0; std::string tailAt; long over16 = 0; std::string overAt; float dc = 0; };

static void runFuzzOne (const char* id, const std::vector<float>& kv, int inputKind, double sr, int B, FuzzWorst& w, const std::string& label)
{
    auto m = makeSnapped (id, sr, B, kv);
    Sig s;
    switch (inputKind)
    {
        case 0: s = sine (sr, 1.5, 100, 4.0f); break;        // +12 dBFS 100 Hz
        case 1: s = pluckSeq (sr, 2.0, 0.5f); break;          // −6 dBFS ギターふう
        case 2: s = noise (sr, 1.5, 0.25f); break;            // −12 dBFS ノイズ
        case 3: s = impulse (sr, 0.5, 1.0f); break;           // インパルス
        case 4: s = sine (sr, 1.5, 7000, 1.0f); break;        // 0 dBFS 7 kHz
        default: s = zeros (sr, 1.0); break;                  // 無音
    }
    const size_t inLen = s.size();
    append (s, zeros (sr, 4.0));
    runModule (*m, s, B);
    const int lat = m->latency();
    auto st = stats (s);
    long over = 0; for (size_t i = 0; i < s.size(); ++i) if (std::abs (s.L[i]) > 16.0f || std::abs (s.R[i]) > 16.0f) ++over;
    auto tl = stats (s, s.size() - (size_t) sr);
    double dc = 0; for (size_t i = s.size() - (size_t) sr; i < s.size(); ++i) dc += s.L[i]; dc /= sr;
    (void) inLen; (void) lat;
    if (st.peak > w.peak) { w.peak = st.peak; w.at = label; }
    if (st.bad > w.bad) { w.bad = st.bad; w.badAt = label; }
    if (tl.peak > w.tail) { w.tail = tl.peak; w.tailAt = label; }
    if (over > w.over16) { w.over16 = over; w.overAt = label; }
    w.dc = std::max (w.dc, (float) std::abs (dc));
}

static void modeFuzz (int nRandom)
{
    const double sr = 48000; const int B = 256;
    static const char* inName[] = { "+12dBFS 100Hz", "pluck -6", "noise -12", "impulse", "0dBFS 7kHz", "silence" };
    std::printf ("== 全部のつまみ 0 / 1 / 初期値 / ランダム %d 通り × 入力 6 種（入力のあと無音 4 秒）==\n", nRandom);
    std::printf ("   山 = 出口の最大（dBFS）、NaN = 壊れた数の数、>16 = ラックの頭打ち（+24 dBFS）にかかったサンプル数、残り = 最後の 1 秒の最大\n");
    for (const char* id : kNew)
    {
        FuzzWorst w;
        std::mt19937 g (1234);
        std::vector<std::vector<float>> sets;
        auto d = defaults (id); sets.push_back (d);
        sets.push_back (std::vector<float> (d.size(), 0.0f));
        sets.push_back (std::vector<float> (d.size(), 1.0f));
        for (int r = 0; r < nRandom; ++r) sets.push_back (randomKnobs (id, g, r % 3 != 0));
        for (size_t si = 0; si < sets.size(); ++si)
            for (int in = 0; in < 6; ++in)
                runFuzzOne (id, sets[si], in, sr, B, w, std::string (inName[in]) + " " + kvStr (sets[si]));
        std::printf ("  %-10s 山 %+6.1f dBFS [%s]\n", id, db (w.peak), w.at.c_str());
        std::printf ("  %-10s NaN %ld%s%s  >16 %ld%s%s  残り %.2e [%s]  直流 %.1e\n", "", w.bad, w.bad ? " " : "", w.badAt.c_str(), w.over16, w.over16 ? " " : "", w.overAt.c_str(), w.tail, w.tailAt.c_str(), w.dc);
    }
}

// ============================================================================
// つまみを一気に（ブロックの境目で）/ 0.3 秒かけて動かしたときのプチッ
static void clickOne (const char* id, int k, float a, float b, float& jump, float& drag, float& base, float amp = 0.25f, std::vector<float> kvBase = {})
{
    const double sr = 48000; const int B = 256;
    jump = drag = base = 0;
    for (int mode = 0; mode < 2; ++mode)
    {
        auto kv = kvBase.empty() ? defaults (id) : kvBase;
        if (kvBase.empty() && std::strcmp (id, "peakflt") == 0) { kv[0] = 0.35f; kv[3] = 0.75f; }   // MIX 0（初期値）は素通しなので、内蔵ラック「Peak Push」の設定から（DSP 検査 0.9.0）
        kv[(size_t) k] = a;
        auto m = makeSnapped (id, sr, B, kv);
        Sig s = sine (sr, 2.0, 220, amp);
        for (size_t i = 0; i < (size_t) (0.1 * sr); ++i) { s.L[i] *= (float) i / (float) (0.1 * sr); s.R[i] = s.L[i]; }
        const size_t t1 = (size_t) (0.9 * sr) / B * B, t2 = t1 + (mode ? (size_t) (0.3 * sr) / B * B : 0);
        for (size_t off = 0; off < s.size(); off += B)
        {
            if (mode == 0 && off == t1) m->setKnob (k, b);
            if (mode == 1 && off >= t1 && off <= t2) m->setKnob (k, a + (b - a) * (float) (off - t1) / (float) std::max<size_t> (1, t2 - t1));
            m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off));
        }
        const int lat = m->latency();
        base = std::max (base, cleanliness (s.L, (size_t) (0.5 * sr) + lat, t1 + lat, sr));
        base = std::max (base, cleanliness (s.L, t2 + (size_t) (0.5 * sr) + lat, s.size(), sr));
        const float c = cleanliness (s.L, t1 + lat, t2 + (size_t) (0.1 * sr) + lat, sr);
        (mode ? drag : jump) = std::max (mode ? drag : jump, c);
    }
}

static void modeClick()
{
    std::printf ("== つまみのプチッ（220 Hz −12 dBFS のサイン。数字 = 一気に / 0.3 秒かけて。[] = 動かす前後のふだんの値）==\n");
    std::printf ("   前回の検査の目安: ふだん 1〜3、プチッは 10 以上\n");
    for (const char* id : kNew)
    {
        const auto& inf = *infoFor (typeForId (id));
        std::printf ("  %-10s\n", id);
        for (int k = 0; k < (int) inf.knobs.size(); ++k)
        {
            const auto& ki = inf.knobs[(size_t) k];
            std::vector<std::pair<float, float>> moves;
            if (ki.steps > 1) { for (int i = 0; i < ki.steps; ++i) for (int j = 0; j < ki.steps; ++j) if (i != j) moves.push_back ({ (float) i / (ki.steps - 1), (float) j / (ki.steps - 1) }); }
            else moves = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.2f, 0.8f }, { 0.8f, 0.2f } };
            float J = 0, D = 0, Bs = 0; std::string worstMove;
            for (auto mv : moves)
            {
                float j, d, b; clickOne (id, k, mv.first, mv.second, j, d, b);
                if (j > J) { J = j; char t[32]; std::snprintf (t, sizeof t, "%.2f→%.2f", mv.first, mv.second); worstMove = t; }
                D = std::max (D, d); Bs = std::max (Bs, b);
            }
            std::printf ("      %-11s %7.1f / %6.1f  [%4.1f]  いちばん悪い動き %s%s\n", ki.name, J, D, Bs, worstMove.c_str(), J > 10 && J > 3 * Bs ? "  ★" : "");
        }
    }
}

// ============================================================================
static void modeOS()
{
    std::printf ("== 4 倍オーバーサンプリング（Oversampler4）==\n");
    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("  %.0f Hz: 上げ下げを通しただけの特性（大きさ dB / 遅れ サンプル）\n   ", sr);
        for (double f : { 20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
        {
            Oversampler4 o; o.reset();
            const size_t N = (size_t) sr; std::vector<float> x (2 * N), y (2 * N);
            for (size_t i = 0; i < 2 * N; ++i) { x[i] = (float) std::sin (2 * M_PI * f * (double) i / sr); float u[4]; o.up (x[i], u); y[i] = o.down (u); }
            // 位相: 後半 1 秒で x と y の相関から
            std::complex<double> X = 0, Y = 0;
            for (size_t i = N; i < 2 * N; ++i) { auto e = std::polar (1.0, -2 * M_PI * f * (double) i / sr); X += (double) x[i] * e; Y += (double) y[i] * e; }
            const double mag = std::abs (Y) / std::abs (X);
            double ph = std::arg (Y / X); if (ph > 0) ph -= 2 * M_PI;
            const double delay = -ph / (2 * M_PI * f) * sr;
            std::printf (" %5.0fHz %+5.2fdB %4.1f", f, db (mag), delay);
        }
        std::printf ("\n");
    }
    // 上げたあとの像（4 倍の中で 元の fs/2 より上に出る成分）
    {
        const double sr = 48000; const double sr4 = 4 * sr;
        std::printf ("  48 kHz: 上げたあとの像（元の Nyquist より上の成分の最大、dB）:");
        for (double f : { 1000.0, 10000.0, 18000.0, 20000.0, 22000.0 })
        {
            Oversampler4 o; o.reset();
            const size_t N = (size_t) sr; std::vector<float> u4 (8 * N);
            for (size_t i = 0; i < 2 * N; ++i) o.up ((float) std::sin (2 * M_PI * f * (double) i / sr), &u4[4 * i]);
            const size_t N4 = 4 * N, off = 4 * N;
            const double fund = dftMag (u4, off, N4, f, sr4);
            double worst = 0;
            // 像は 48k−f, 48k+f, 96k−f
            for (double im : { sr - f, sr + f, 2 * sr - f }) worst = std::max (worst, dftMag (u4, off, N4, im, sr4) / fund);
            std::printf ("  %.0fHz %+.0f", f, db (worst));
        }
        std::printf ("\n");
    }
}

// ============================================================================
// 折り返し（歪ませる部品）
static void modeAlias()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== 折り返し（4999 / 7001 Hz の正弦波、後半 1 秒の DFT。基音に対する dB）==\n");
    struct Case { const char* id; std::vector<std::pair<int, float>> kv; float amp; const char* name; };
    std::vector<Case> cases {
        { "transient", { { 0, 0.5f }, { 1, 0.5f }, { 2, 1.0f }, { 3, 1.0f } }, 1.0f, "TRANSIENT CLIP ON・GAIN +6・0 dBFS" },
        { "transient", { { 0, 0.5f }, { 1, 0.5f }, { 2, 1.0f }, { 3, 1.0f } }, 2.0f, "TRANSIENT CLIP ON・GAIN +6・+6 dBFS" },
        { "tubepush",  { { 3, 1.0f } }, 0.5f, "TUBE PUSH DIRT ON・−6 dBFS" },
        { "tubepush",  { { 3, 1.0f }, { 0, 1.0f }, { 1, 1.0f } }, 1.0f, "TUBE PUSH DIRT ON・INPUT 1・COMPRESS 1・0 dBFS" },
        { "tubepush",  { { 0, 1.0f }, { 1, 1.0f } }, 1.0f, "TUBE PUSH INPUT 1・COMPRESS 1・0 dBFS" },
        { "tubepushxl", { { 2, 1.0f } }, 0.5f, "TUBE PUSH XL SATURATION 1・−6 dBFS" },
        { "fetcomp",   { }, 0.5f, "FET COMP 初期値・−6 dBFS" },
        { "fetcomp",   { { 4, 0.6f } }, 0.5f, "FET COMP OUTPUT 0.6・−6 dBFS" },
        { "optocomp",  { { 0, 1.0f } }, 0.5f, "OPTO COMP GAIN 1・−6 dBFS" },
        { "maximizer", { { 3, 0.0f } }, 0.5f, "MAXIMIZER CHARACTER 0・−6 dBFS（初期の押し込み）" },
        { "maximizer", { { 3, 0.0f }, { 1, 0.0f } }, 0.5f, "MAXIMIZER CHARACTER 0・THRESHOLD 0・−6 dBFS" },
        { "maximizer", { { 1, 0.0f } }, 0.5f, "MAXIMIZER CHARACTER 3.55（初期）・THRESHOLD 0・−6 dBFS" },
    };
    for (auto& c : cases)
        for (double f0 : { 4999.0, 7001.0 })
        {
            auto kv = defaults (c.id); for (auto& p : c.kv) kv[(size_t) p.first] = p.second;
            auto m = makeSnapped (c.id, sr, B, kv);
            Sig s = sine (sr, 2.5, f0, c.amp);
            runModule (*m, s, B);
            double h, a, af; aliasReport (s.L, (size_t) (1.2 * sr), sr, f0, h, a, af);
            std::printf ("  %-55s %4.0f Hz: 出口 %+5.1f dBFS、倍音の最大 %+6.1f dB、折り返し %+6.1f dB（%5.0f Hz）\n", c.name, f0, db (stats (s, (size_t) sr).peak), db (h), db (a), af);
        }
}

// ============================================================================
// MIX の途中でくし形になるか（小さいインパルスで周波数特性）
static void mixResponse (const char* id, std::vector<std::pair<int, float>> kvSet, const char* name, float level = 0.001f)
{
    const double sr = 48000; const int B = 256;
    auto kv = defaults (id); for (auto& p : kvSet) kv[(size_t) p.first] = p.second;
    auto m = makeSnapped (id, sr, B, kv);
    const size_t N = 16384;
    Sig s = zeros (sr, (double) N / sr + 0.5);
    s.L[100] = s.R[100] = level;
    runModule (*m, s, B);
    // 応答（100 サンプル目から N）
    std::vector<double> magDb;
    double lo = 1e9, hi = -1e9, loF = 0;
    std::vector<double> fs;
    for (double f = 30; f < 20000; f *= 1.02) fs.push_back (f);
    for (double f : fs)
    {
        std::complex<double> acc = 0;
        for (size_t i = 0; i < N; ++i) acc += (double) s.L[100 + i] * std::polar (1.0, -2 * M_PI * f * (double) i / sr);
        const double d = db (std::abs (acc) / level);
        magDb.push_back (d);
        if (d < lo) { lo = d; loF = f; }
        hi = std::max (hi, d);
    }
    // 「くし」: 隣り合う点との差の最大（1 オクターブの中の谷の深さ）
    double notch = 0, notchF = 0;
    for (size_t i = 6; i + 6 < magDb.size(); ++i)
    {
        const double around = std::max (magDb[i - 6], magDb[i + 6]);
        if (around - magDb[i] > notch) { notch = around - magDb[i]; notchF = fs[i]; }
    }
    std::printf ("  %-48s 30 Hz〜20 kHz: 最小 %+6.1f dB（%5.0f Hz）最大 %+6.1f dB、いちばん深い谷 %.1f dB（%5.0f Hz）\n", name, lo, loF, hi, notch, notchF);
}
static void modeMix()
{
    std::printf ("== MIX / DRY の途中の値でくし形になるか（−60 dBFS のインパルス = 圧縮しない大きさ）==\n");
    mixResponse ("tubepush", { { 4, 0.0f } }, "TUBE PUSH MIX 0（全部加工）");
    mixResponse ("tubepush", { { 4, 0.5f } }, "TUBE PUSH MIX 0.5");
    mixResponse ("tubepush", { { 4, 0.75f } }, "TUBE PUSH MIX 0.75");
    mixResponse ("tubepush", { { 4, 1.0f } }, "TUBE PUSH MIX 1（ほぼドライ）");
    mixResponse ("tubepush", { { 4, 0.5f }, { 3, 1.0f } }, "TUBE PUSH MIX 0.5・DIRT ON");
    mixResponse ("tubepushxl", { { 9, 0.5f } }, "TUBE PUSH XL MIX 0.5");
    mixResponse ("tubepushxl", { { 9, 0.75f } }, "TUBE PUSH XL MIX 0.75");
    mixResponse ("fetcomp", { }, "FET COMP 初期値（DRY 100 %）");
    mixResponse ("fetcomp", { { 5, 0.5f } }, "FET COMP DRY 0.5");
    mixResponse ("optocomp", { { 3, 0.75f } }, "OPTO COMP DRY 0.75");
    mixResponse ("vcacomp", { }, "VCA COMP 初期値（DRY 0.53）");
    mixResponse ("vcacomp", { { 3, 0.75f } }, "VCA COMP DRY 0.75");
}

// ============================================================================
// MAXIMIZER の写し（fx/Maximizer.h の struct をそのまま写し、名前を変えて clamp の前の山と本体の段の山を記録する）。
// 3 回目（2026-11-19）: 本体は「本体の段（荒さありは 4 倍のクリップ）→ 1.5 ms の短い先読み → 念のための clamp」。
//   写しは窓の最大の列を 1 つ大きく（W = look + 3 / L + 3）した「お勧めの直し」の形。本体がまだ + 2 なら、低音（34 Hz より下）の行だけ
//   「★写しが古い」が出る（列が溢れて窓の最大を見誤るため。ほかの行は 1 ビットも同じ）。本体を直したら全部の行がそろう
struct MaxCopyCore final : Module
{
    static constexpr double kLookSec = 353.0 / 48000.0;
    static float inDb (float v)   { return -20.0f + 40.0f * v; }
    static float thrDb (float v)  { return -20.0f + 20.0f * v; }
    static float ceilDb (float v) { return -20.0f + 20.0f * v; }
    static float charVal (float v) { return 10.0f * v; }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "maximizer", "MAXIMIZER", "DYNAMICS",
            "Loudness maximizer: lower THRESHOLD to push it louder, CEILING is the highest peak. Low CHARACTER = harder, crunchier. Adds 9 ms of delay (reported to Logic).",
            {
                { "INPUT",     0.5f,   0, [] (float v) { return fmt (inDb (v), 1, " dB"); } },
                { "THRESHOLD", 0.745f, 0, [] (float v) { return fmt (thrDb (v), 1, " dB"); } },
                { "CEILING",   0.95f,  0, [] (float v) { return fmt (ceilDb (v), 1, " dB"); } },
                { "CHARACTER", 0.355f, 0, [] (float v) { return fmt (charVal (v), 1, ""); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        look = std::max (1, (int) std::lround (kLookSec * sr));
        W = look + 3;   // 3 回目の検査のお勧め（本体は + 2 で、低音で窓の最大を見誤る）
        buf.assign ((size_t) (look + 1) * 2, 0.0f);
        win.assign ((size_t) W, 0.0f);
        dq.assign ((size_t) W, 0);
        box.assign ((size_t) look, 1.0);
        drive.prepare (sr, 0.02); ceil.prepare (sr, 0.02); crunch.prepare (sr, 0.02); crunchMix.prepare (sr, 0.02);
        safe.prepare (sr, 0.0015);
        recalc (true);
        reset();
    }
    void reset() override
    {
        std::fill (buf.begin(), buf.end(), 0.0f); pos = 0;
        std::fill (win.begin(), win.end(), 0.0f); head = tail = 0; t = 0;
        std::fill (box.begin(), box.end(), 1.0); boxSum = (double) look; boxPos = 0;
        g = 1.0f; gr = 1.0f;
        for (auto& o : os) o.reset();
        for (auto& a : d4) for (auto& v : a) v = 0.0f;
        d4p = 0;
        safe.reset();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 4 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    int latency() const override { return look + 4 + safe.L; }   // 先読み 353 ＋ 4 倍のクリップ 4 ＋ 天井を守る先読み 72（48 kHz）
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float d = drive.next(), c = ceil.next(), cr = crunch.next(), cm = crunchMix.next();
            const float head0 = dbToGain (6.0f * cr);   // 荒さの分だけリミッターの天井を上げる
            const float l = L[s] * d, r = R[s] * d;
            const float pk = std::max (std::abs (l), std::abs (r));
            // 窓 [t − look, t] の最大（出している音も入る）: 単調な列で O(1)
            win[(size_t) (t % W)] = pk;
            while (head != tail && win[(size_t) (dq[(size_t) ((tail - 1 + W) % W)] % W)] <= pk) tail = (tail - 1 + W) % W;
            dq[(size_t) tail] = t; tail = (tail + 1) % W;
            while (head != tail && dq[(size_t) head] < t - look) head = (head + 1) % W;
            const float wmax = win[(size_t) (dq[(size_t) head] % W)];
            ++t;
            const double target = wmax > head0 ? (double) head0 / wmax : 1.0;
            // 箱形の平均（look サンプル）: 山が出ていく時点では、平均した下げ量が必ずその山に足りている
            boxSum += target - box[(size_t) boxPos];
            box[(size_t) boxPos] = target;
            if (++boxPos >= look) boxPos = 0;
            const float avg = (float) (boxSum / look);
            g = avg < g ? avg : avg + (g - avg) * relA;   // 戻りだけ CHARACTER の速さで
            // 音は先読みの分だけ遅らせる
            float* b = buf.data();
            const int rp = (pos + 1) % (look + 1);
            b[2 * pos] = l; b[2 * pos + 1] = r;
            float ol = b[2 * rp] * g, orr = b[2 * rp + 1] * g;
            pos = rp;
            limStage = std::max (limStage, std::max (std::abs (ol), std::abs (orr)) / head0);
            // 荒さ: 荒さなしの道 = 4 サンプルの整数の遅れ、荒さありの道 = 4 倍のやわらかいクリップ（フィルターはいつも回す）。20 ms で入れ替え
            {
                const float dl = d4[0][d4p], dr = d4[1][d4p];
                d4[0][d4p] = ol; d4[1][d4p] = orr; d4p = (d4p + 1) & 3;
                const float cl = clip4 (os[0], ol, 1.0f), cr2 = clip4 (os[1], orr, 1.0f);
                ol = dl + cm * (cl - dl); orr = dr + cm * (cr2 - dr);
            }
            // 天井を守る短い先読み（荒さありで 4 倍から戻した波形の山が天井を超える分。荒さなしでは何もしない）
            safe.process (ol, orr);
            { const float pa = std::max (std::abs (ol), std::abs (orr)); preClamp = std::max (preClamp, pa); ++total; if (pa > 1.0001f) ++hits; }
            // 念のための頭打ち（上の先読みで、ふだんは届かない）
            ol = std::clamp (ol, -1.0f, 1.0f); orr = std::clamp (orr, -1.0f, 1.0f);
            L[s] = ol * c; R[s] = orr * c;
            gr = g;
        }
        boxSum = 0.0; for (double v : box) boxSum += v;   // 小数の誤差がたまらないよう、ブロックごとに数え直す
        safe.recount();
    }
    float limStage = 0, preClamp = 0; long hits = 0, total = 0;
    float meter() const override { return std::clamp (-gainToDb (gr) / 24.0f, 0.0f, 1.0f); }

private:
    // 短い先読みのリミッター（窓の最大 → 箱形の平均 → 戻り 2 ms。天井 1.0）
    struct ShortLim
    {
        int L = 72, W = 74, pos = 0, head = 0, tail = 0, bp = 0;
        std::vector<float> bufL, bufR, win;
        std::vector<int64_t> dq;
        std::vector<double> box;
        double boxSum = 72.0;
        int64_t t = 0;
        float g = 1.0f, relA = 0.0f;
        void prepare (double sr, double sec)
        {
            L = std::max (1, (int) std::lround (sec * sr)); W = L + 3;
            bufL.assign ((size_t) L + 1, 0.0f); bufR = bufL;
            win.assign ((size_t) W, 0.0f); dq.assign ((size_t) W, 0); box.assign ((size_t) L, 1.0);
            relA = tauCoef (sr, 0.002);
            reset();
        }
        void reset()
        {
            std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f);
            std::fill (win.begin(), win.end(), 0.0f); std::fill (box.begin(), box.end(), 1.0);
            boxSum = (double) L; pos = head = tail = bp = 0; t = 0; g = 1.0f;
        }
        inline void process (float& l, float& r)
        {
            const float pk = std::max (std::abs (l), std::abs (r));
            win[(size_t) (t % W)] = pk;
            while (head != tail && win[(size_t) (dq[(size_t) ((tail - 1 + W) % W)] % W)] <= pk) tail = (tail - 1 + W) % W;
            dq[(size_t) tail] = t; tail = (tail + 1) % W;
            while (head != tail && dq[(size_t) head] < t - L) head = (head + 1) % W;
            const float wmax = win[(size_t) (dq[(size_t) head] % W)];
            ++t;
            const double target = wmax > 1.0f ? 1.0 / wmax : 1.0;
            boxSum += target - box[(size_t) bp]; box[(size_t) bp] = target;
            if (++bp >= L) bp = 0;
            const float avg = (float) (boxSum / L);
            g = avg < g ? avg : avg + (g - avg) * relA;
            const int rp = (pos + 1) % (L + 1);
            bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
            l = bufL[(size_t) rp] * g; r = bufR[(size_t) rp] * g;
            pos = rp;
        }
        void recount() { boxSum = 0.0; for (double v : box) boxSum += v; }
    };

    static float clip4 (Oversampler4& o, float x, float amt)
    {
        float u[4]; o.up (x, u);
        if (amt > 1.0e-4f)
            for (auto& v : u) v += amt * (v / std::pow (1.0f + std::pow (std::abs (v) * (1.0f / 0.94f), 8.0f), 0.125f) - v);   // 天井 0.94（−0.5 dB）
        return o.down (u);
    }
    void recalc (bool snap)
    {
        drive.set (dbToGain (inDb (k[0]) - thrDb (k[1])), snap);
        ceil.set (dbToGain (ceilDb (k[2])), snap);
        const float c = charVal (k[3]);
        crunch.set (std::clamp (1.0f - c / 2.5f, 0.0f, 1.0f), snap);
        crunchMix.set (c < 2.5f ? 1.0f : 0.0f, snap);
        relA = tauCoef (sr, 0.002);   // 戻り 2 ms（速く戻すと元の速さで波形に掛け算することになり折り返しが出る。荒さは 4 倍のクリップで作る）
    }
    double sr = 48000.0;
    float k[4] { 0.5f, 0.745f, 0.95f, 0.355f };
    int look = 353, pos = 0, W = 355, head = 0, tail = 0, boxPos = 0;
    std::vector<float> buf, win;
    std::vector<int64_t> dq;
    std::vector<double> box;
    double boxSum = 353.0;
    int64_t t = 0;
    float g = 1.0f, gr = 1.0f, relA = 0.0f;
    Smooth drive, ceil, crunch, crunchMix;
    Oversampler4 os[2];
    float d4[2][4] {};
    int d4p = 0;
    ShortLim safe;
};

struct MaxCopy
{
    MaxCopyCore c;
    float preClean = 0, preCrunch = 0, limStage = 0; long clampHits = 0, total = 0;
    void prepare (double sr, const std::vector<float>& kv)
    {
        c.prepare (sr, 256); for (int k = 0; k < 4; ++k) c.setKnob (k, kv[(size_t) k]);
        c.prepare (sr, 256); for (int k = 0; k < 4; ++k) c.setKnob (k, kv[(size_t) k]); c.reset();
    }
    void process (float* L, float* R, int n)
    {
        c.process (L, R, n);
        preClean = preCrunch = c.preClamp; limStage = c.limStage; clampHits = c.hits; total = c.total;
    }
};

// 正弦波 f の出口と入口の位相の差から遅れ（サンプル）。hint の ± 半周期に入れる
static double phaseDelayHint (const std::vector<float>& x, const std::vector<float>& y, double f, double sr, size_t from, size_t to, double hint)
{
    std::complex<double> X = 0, Y = 0;
    for (size_t i = from; i < to; ++i) { auto e = std::polar (1.0, -2 * M_PI * f * (double) i / sr); X += (double) x[i] * e; Y += (double) y[i] * e; }
    double d = -std::arg (Y / X) / (2 * M_PI * f) * sr;
    const double per = sr / f;
    while (d < hint - per / 2) d += per;
    while (d > hint + per / 2) d -= per;
    return d;
}
// 8 kHz より上（4 次の HP）の山（[a, b) の範囲。フィルターは 0 から回す）
static float hfBurst (const std::vector<float>& y, size_t a, size_t b, double sr)
{
    Biquad h1, h2; h1.setHP (sr, 8000.0, 0.54); h2.setHP (sr, 8000.0, 1.31);
    float pk = 0;
    for (size_t i = 0; i < b; ++i) { const float v = h2.process (h1.process (y[i])); if (i >= a) pk = std::max (pk, std::abs (v)); }
    return pk;
}
static Sig drumish (double sr, double secs, float amp, uint32_t seed = 5)
{
    Sig s = zeros (sr, secs); std::mt19937 g (seed); std::normal_distribution<float> n (0, 1);
    for (size_t i = 0; i < s.size(); ++i) { const double t = std::fmod ((double) i / sr, 0.25); const float e = (float) std::exp (-t * 30); s.L[i] = s.R[i] = amp * (0.9f * e * (float) std::sin (2 * M_PI * 60 * t) + 0.3f * e * n (g)); }
    return s;
}
static Sig squareish (double sr, double secs, double f, float amp)
{
    Sig s = zeros (sr, secs);
    for (size_t i = 0; i < s.size(); ++i) { double v = 0; for (int h = 1; h < 40; h += 2) if (h * f < sr / 2) v += std::sin (2 * M_PI * f * h * (double) i / sr) / h; s.L[i] = s.R[i] = (float) (amp * v * 4 / M_PI); }
    return s;
}
static Sig fs4sine (double sr, double secs, float amp)   // fs/4 の正弦波を 45° ずらす: サンプルの山は 0.707、間の山は 1（インターサンプルピーク）
{
    Sig s = zeros (sr, secs);
    for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = amp * (float) std::sin (M_PI / 2 * (double) i + M_PI / 4);
    return s;
}
// 音楽っぽい素材: キック・スネア・ハイハット・ベース（のこぎり）・和音。山を peak に
static Sig musicish (double sr, double secs, float peak, uint32_t seed = 21)
{
    Sig s = zeros (sr, secs); std::mt19937 g (seed); std::normal_distribution<float> n (0, 1);
    Biquad hh; hh.setHP (sr, 7000.0, 0.7); Biquad sn; sn.setPeak (sr, 1800.0, 0.7, 6.0);
    Sig ch = pluckSeq (sr, secs, 0.3f); double bassPh = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / sr; const double beat = std::fmod (t, 0.5), half = std::fmod (t + 0.25, 0.5), q = std::fmod (t, 0.125);
        float v = 0;
        v += (float) (0.9 * std::exp (-beat * 18) * std::sin (2 * M_PI * (50 * beat + 80 * (1 - std::exp (-beat * 30)) / 30)));
        v += 0.5f * (float) std::exp (-half * 25) * sn.process (n (g)) * 0.6f;
        v += 0.25f * (float) std::exp (-q * 60) * hh.process (n (g));
        bassPh += 55.0 / sr; if (bassPh >= 1) bassPh -= 1; v += 0.25f * (float) (2 * bassPh - 1);
        v += ch.L[i]; s.L[i] = s.R[i] = v;
    }
    float pk = stats (s).peak; for (size_t i = 0; i < s.size(); ++i) { s.L[i] *= peak / pk; s.R[i] = s.L[i]; }
    return s;
}

// 808 ふう: 60 Hz → fEnd に下がる・頭 amp、2 秒ごと（3 回目）
static Sig k808 (double sr, double secs, float amp, double fEnd)
{
    Sig x = zeros (sr, secs); double ph = 0;
    for (size_t i = 0; i < x.size(); ++i) { const double t = std::fmod ((double) i / sr, 2.0); if (t < 1.0 / sr) ph = 0; const double f = fEnd + (60 - fEnd) * std::exp (-t * 25); ph += f / sr; x.L[i] = x.R[i] = amp * (float) (std::exp (-t * 1.5) * std::sin (2 * M_PI * ph)); }
    return x;
}
template <typename M> static void runAny (M& m, Sig& s, int block)
{
    for (size_t off = 0; off < s.size(); off += (size_t) block)
        m.process (s.L.data() + off, s.R.data() + off, (int) std::min ((size_t) block, s.size() - off));
}

static void modeMax()
{
    std::printf ("== MAXIMIZER ==\n");
    // 1. 遅れ（位相で。インパルスの山の位置は 4 倍のフィルターの形で 1 サンプル後ろに出るので使わない）
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const std::vector<float> kv { 0.5f, 1.0f, 1.0f, 0.355f };   // 押し込み 0 dB・天井 0 dB = 線形
        auto m = makeSnapped ("maximizer", sr, 256, kv), m2 = makeSnapped ("maximizer", sr, 256, kv);
        Sig in = sine (sr, 1.5, 100, 0.25f), s = in; runModule (*m, s, 256);
        Sig in2 = sine (sr, 1.5, 1000, 0.25f), s2 = in2; runModule (*m2, s2, 256);
        const int lat = m->latency();
        const double d1 = phaseDelayHint (in.L, s.L, 100, sr, (size_t) (0.7 * sr), (size_t) (1.4 * sr), lat), d2 = phaseDelayHint (in2.L, s2.L, 1000, sr, (size_t) (0.7 * sr), (size_t) (1.4 * sr), lat);
        std::printf ("  %6.0f Hz: 申告 %4d、実際 100 Hz %.2f / 1 kHz %.2f %s\n", sr, lat, d1, d2, std::abs (d1 - lat) > 0.5 || std::abs (d2 - lat) > 0.5 ? "★ずれ" : "");
    }
    // 2. 天井（本体）と、最後の clamp の前の山（写し）
    struct In { const char* name; std::function<Sig (double)> make; };
    std::vector<In> ins {
        { "music 0", [] (double sr) { return musicish (sr, 4.0, 1.0f); } },
        { "music -6", [] (double sr) { return musicish (sr, 4.0, 0.5f); } },
        { "drum-ish 0", [] (double sr) { return drumish (sr, 2.0, 1.0f); } },
        { "pluck +6", [] (double sr) { return pluckSeq (sr, 2.0, 2.0f); } },
        { "1k sine -6", [] (double sr) { return sine (sr, 2.0, 1000, 0.5f); } },
        { "40Hz +12", [] (double sr) { return sine (sr, 2.0, 40, 4.0f); } },
        { "noise 0", [] (double sr) { return noise (sr, 2.0, 1.0f); } },
        { "square 100", [] (double sr) { return squareish (sr, 2.0, 100, 1.0f); } },
        { "fs/4 +6", [] (double sr) { return fs4sine (sr, 2.0, 2.0f); } },
        { "15k +6", [] (double sr) { return sine (sr, 2.0, 15000, 2.0f); } },
        { "30Hz -6", [] (double sr) { return sine (sr, 2.0, 30, 0.5f); } },          // 3 回目: 34 Hz より下（窓の最大の列が溢れる所）
        { "808→31Hz 0", [] (double sr) { return k808 (sr, 4.0, 1.0f, 31.0); } },
    };
    std::printf ("  天井（48 kHz・CEILING −1 dB）: 出口 ÷ 天井（1 以下が正しい）/ clamp の前の山 ÷ 天井 / clamp にかかったサンプルの割合 / リミッターの段の山 ÷ その天井\n");
    for (float chr : { 0.355f, 1.0f, 0.0f, 0.2f })
        for (float thr : { 0.745f, 0.0f })
        {
            std::printf ("   CHARACTER %4.1f・THRESHOLD %5.1f dB（%s）\n", 10 * chr, -20 + 20 * thr, 10 * chr < 2.5f ? "荒さあり: clamp なし" : "荒さなし: 最後に clamp");
            for (auto& in : ins)
            {
                const double sr = 48000;
                const std::vector<float> kv { 0.5f, thr, 0.95f, chr };
                auto m = makeSnapped ("maximizer", sr, 256, kv);
                MaxCopy mc; mc.prepare (sr, kv);
                Sig s = in.make (sr), s2 = s; runModule (*m, s, 256); runAny (mc, s2, 256);
                float d = 0; for (size_t i = 0; i < s.size(); ++i) d = std::max (d, std::abs (s.L[i] - s2.L[i]));
                const float c = dbToGain (-1.0f);
                std::printf ("    %-11s 出口/天井 %.4f（%+5.2f dB）  clamp 前 %.4f（%+5.2f dB）  clamp %7.3f %%  リミッター段 %.6f%s\n", in.name, stats (s).peak / c, db (stats (s).peak / c),
                             10 * chr < 2.5f ? mc.preCrunch : mc.preClean, db (10 * chr < 2.5f ? mc.preCrunch : mc.preClean), 100.0 * (double) mc.clampHits / (double) std::max (1L, mc.total), mc.limStage,
                             d > 0 ? "  ★写しと本体が違う（低音の行だけなら本体の列が短い = 3 回目の指摘。本体を直してもまだ出るなら写しを Maximizer.h に合わせる）" : "");
            }
        }
    // 3. 歪み（1 kHz 0 dBFS）・折り返し（荒さありで 4999 / 7001 / 9973 Hz）・低音の歪み（荒さなし）
    std::printf ("  1 kHz −6 dBFS の倍音と、4999 / 7001 / 9973 Hz −6 dBFS の折り返し\n");
    for (float thr : { 0.745f, 0.0f })
        for (float chr : { 0.0f, 0.1f, 0.2f, 0.355f })
        {
            const double sr = 48000;
            std::printf ("    THRESHOLD %5.1f CHARACTER %4.1f:", -20 + 20 * thr, 10 * chr);
            { auto m = makeSnapped ("maximizer", sr, 256, { 0.5f, thr, 0.95f, chr }); Sig s = sine (sr, 2.5, 997, 0.5f); runModule (*m, s, 256); double h, a, af; aliasReport (s.L, (size_t) (1.2 * sr), sr, 997, h, a, af); std::printf (" 997 Hz 倍音 %+6.1f dB", db (h)); }
            for (double f0 : { 4999.0, 7001.0, 9973.0 })
            { auto m = makeSnapped ("maximizer", sr, 256, { 0.5f, thr, 0.95f, chr }); Sig s = sine (sr, 2.5, f0, 0.5f); runModule (*m, s, 256); double h, a, af; aliasReport (s.L, (size_t) (1.2 * sr), sr, f0, h, a, af); std::printf ("  %4.0f Hz 折り返し %+6.1f dB", f0, db (a)); }
            std::printf ("\n");
        }
    std::printf ("  荒さなし（CHARACTER 3.55）の低音の歪み（THD、−6 dBFS の正弦波）\n");
    for (float thr : { 0.745f, 0.0f })
    {
        std::printf ("    THRESHOLD %5.1f:", -20 + 20 * thr);
        for (double f0 : { 40.0, 60.0, 100.0, 1000.0 })
        {
            const double sr = 48000;
            auto m = makeSnapped ("maximizer", sr, 256, { 0.5f, thr, 0.95f, 0.355f });
            Sig s = sine (sr, 2.5, f0, 0.5f); runModule (*m, s, 256);
            const size_t N = (size_t) sr, off = (size_t) (1.2 * sr);
            const double fund = dftMag (s.L, off, N, f0, sr); double e = 0;
            for (int n = 2; n <= 20; ++n) { const double v = dftMag (s.L, off, N, n * f0, sr) / fund; e += v * v; }
            std::printf ("  %4.0f Hz %+6.1f dB", f0, db (std::sqrt (e)));
        }
        std::printf ("\n");
    }
    // 4. つまみを一気に（荒さの入り切り・押し込み・天井）: 8 kHz より上の山（切り替え中 / 前 / 後）
    std::printf ("  つまみを一気に動かしたとき（220 Hz）: 8 kHz より上の山 切り替え中 / 前 / 後、プチッの目安\n");
    struct Mv { int k; float a, b; const char* name; };
    for (Mv mv : { Mv { 3, 0.26f, 0.24f, "CHARACTER 2.6→2.4" }, Mv { 3, 0.24f, 0.26f, "CHARACTER 2.4→2.6" }, Mv { 3, 1.0f, 0.0f, "CHARACTER 10→0" },
                   Mv { 1, 1.0f, 0.0f, "THRESHOLD 0→−20" }, Mv { 2, 1.0f, 0.0f, "CEILING 0→−20" }, Mv { 0, 0.0f, 1.0f, "INPUT −20→+20" } })
        for (float amp : { 0.25f, 1.0f })
        {
            const double sr = 48000; const int B = 256;
            std::vector<float> kv { 0.5f, 0.745f, 0.95f, 0.355f }; kv[(size_t) mv.k] = mv.a;
            auto m = makeSnapped ("maximizer", sr, B, kv);
            Sig s = sine (sr, 2.0, 220, amp);
            const size_t t1 = (size_t) (1.0 * sr) / B * B;
            for (size_t off = 0; off < s.size(); off += B) { if (off == t1) m->setKnob (mv.k, mv.b); m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off)); }
            std::printf ("    %-20s %+5.1f dBFS: %.5f / %.5f / %.5f  プチッ %.1f\n", mv.name, db (amp), hfBurst (s.L, t1, t1 + (size_t) (0.15 * sr), sr), hfBurst (s.L, (size_t) (0.5 * sr), t1, sr),
                         hfBurst (s.L, t1 + (size_t) (0.3 * sr), (size_t) (1.9 * sr), sr), cleanliness (s.L, t1, t1 + (size_t) (0.15 * sr), sr));
        }
}

// ============================================================================
// ラック: MAXIMIZER の段の ON/OFF・入れ替え・遅れ
static Want emptyWant() { Want w {}; for (auto& s : w) { s.type = 0; s.on = true; for (auto& k : s.k) k = 0.5f; } return w; }
static void setSlot (Want& w, int slot, const char* id, std::vector<std::pair<int, float>> kv = {}, bool on = true)
{
    auto& s = w[(size_t) slot];
    s.type = id ? typeForId (id) : 0; s.on = on;
    if (! id) return;
    const auto& inf = *infoFor (s.type);
    for (int k = 0; k < kMaxKnobs; ++k) s.k[k] = k < (int) inf.knobs.size() ? inf.knobs[(size_t) k].def : 0.5f;
    for (auto& p : kv) s.k[p.first] = p.second;
}

static void modeRack()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== ラックの遅れ（MAXIMIZER）==\n");
    // A. 押し込み 0 dB・天井 0 dB の MAXIMIZER を ON / OFF（0.5 秒ごと）。出口は「入口を latency() だけ遅らせたもの」とほぼ同じはず（4 倍のフィルターの位相の分だけ高い音で少しずれる）
    {
        Want w = emptyWant(); setSlot (w, 2, "maximizer", { { 1, 1.0f }, { 2, 1.0f } });
        Rack r; r.prepare (sr, B, w);
        Sig in = sine (sr, 4.0, 220, 0.25f), s = in;
        float worst = 0; size_t worstAt = 0;
        double minRms = 1e9; size_t minAt = 0;
        for (size_t off = 0; off < s.size(); off += B)
        {
            w[2].on = ((off / (size_t) (0.5 * sr)) % 2) == 0;
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        }
        const size_t lt = (size_t) r.latency();
        for (size_t i = lt; i < s.size(); ++i) { const float e = std::abs (s.L[i] - in.L[i - lt]); if (e > worst) { worst = e; worstAt = i; } }
        for (size_t i = 2000; i + 480 < s.size(); i += 48) { double e = 0; for (size_t j = i; j < i + 480; ++j) e += (double) s.L[j] * s.L[j]; e = std::sqrt (e / 480); if (e < minRms) { minRms = e; minAt = i; } }
        std::printf ("  A. 押し込みなしの MAXIMIZER を 0.5 秒ごとに ON/OFF: 入口を latency 分遅らせたものとの差の最大 %.3f（%.3f s）、10 ms の rms の最小 %.1f dB（%.3f s、ふだん %.1f dB）、latency %d\n",
                     worst, worstAt / sr, db (minRms), minAt / sr, db (0.25 / std::sqrt (2.0)), r.latency());
        std::printf ("     プチッの目安 %.1f（ふだん %.1f）\n", cleanliness (s.L, (size_t) (0.4 * sr), s.size(), sr), cleanliness (s.L, (size_t) (0.1 * sr), (size_t) (0.45 * sr), sr));
    }
    // B. 部品の入れ替え: 空き → MAXIMIZER → 空き（service をオーディオと同じスレッドで呼ぶ）
    {
        Want w = emptyWant();
        Rack r; r.prepare (sr, B, w);
        Sig in = sine (sr, 3.0, 220, 0.25f), s = in;
        std::vector<int> lats;
        for (size_t off = 0; off < s.size(); off += B)
        {
            if (off == (size_t) (1.0 * sr) / B * B) { setSlot (w, 0, "maximizer", { { 1, 1.0f }, { 2, 1.0f } }); r.service (w); }
            if (off == (size_t) (2.0 * sr) / B * B) { setSlot (w, 0, nullptr); r.service (w); }
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
            lats.push_back (r.latency());
            if ((off / B) % 40 == 0) r.service (w);
        }
        int mx = 0; for (int l : lats) mx = std::max (mx, l);
        std::printf ("  B. 空き → MAXIMIZER（1 s）→ 空き（2 s）: latency の最大 %d、最後 %d。プチッの目安 1 s 付近 %.1f、2 s 付近 %.1f（ふだん %.1f）\n",
                     mx, lats.back(), cleanliness (s.L, (size_t) (0.98 * sr), (size_t) (1.2 * sr), sr), cleanliness (s.L, (size_t) (1.98 * sr), (size_t) (2.2 * sr), sr),
                     cleanliness (s.L, (size_t) (0.3 * sr), (size_t) (0.9 * sr), sr));
        // 出口の rms の谷（ダブり・抜け）
        double minRms = 1e9; size_t minAt = 0;
        for (size_t i = 2000; i + 480 < s.size(); i += 48) { double e = 0; for (size_t j = i; j < i + 480; ++j) e += (double) s.L[j] * s.L[j]; e = std::sqrt (e / 480); if (e < minRms) { minRms = e; minAt = i; } }
        std::printf ("     10 ms の rms の最小 %.1f dB（%.3f s）\n", db (minRms), minAt / sr);
    }
    // C. MAXIMIZER 2 個（1 個 OFF）＋ほか: 遅れの合計
    {
        Want w = emptyWant();
        setSlot (w, 0, "tubepush"); setSlot (w, 1, "maximizer"); setSlot (w, 3, "transient"); setSlot (w, 5, "maximizer", {}, false);
        Rack r; r.prepare (sr, B, w);
        std::printf ("  C. MAXIMIZER 2 個（1 個は OFF）: latency %d（MAXIMIZER 429 × 2 + TUBE PUSH 8 + TRANSIENT 4 = 870 のはず、48 kHz）\n", r.latency());
        Sig s = impulse (sr, 0.5, 0.1f); for (size_t off = 0; off < s.size(); off += B) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        size_t at = 0; float pk = 0; for (size_t i = 0; i < s.size(); ++i) if (std::abs (s.L[i]) > pk) { pk = std::abs (s.L[i]); at = i; }
        std::printf ("     インパルスの山 %zu サンプル目\n", at);
    }
    // D. 遅れのある段を OFF → ON にしたとき、部品の中が空（reset）なので 7.35 ms 音が抜けるか
    {
        Want w = emptyWant(); setSlot (w, 0, "maximizer", { { 1, 1.0f }, { 2, 1.0f } }, false);
        Rack r; r.prepare (sr, B, w);
        Sig in = sine (sr, 2.0, 220, 0.25f), s = in;
        const size_t tOn = (size_t) (1.0 * sr) / B * B;
        for (size_t off = 0; off < s.size(); off += B) { w[0].on = off >= tOn; r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0); }
        double worstDip = 0; size_t at = 0;
        for (size_t i = tOn; i < tOn + (size_t) (0.04 * sr); ++i)
        {
            const double env = 0.25;   // 期待の山
            // 1 周期ごとの山
            if (i + 218 >= s.size()) break;
            float pk = 0; for (size_t j = i; j < i + 218; ++j) pk = std::max (pk, std::abs (s.L[j]));
            const double d = db (pk / env); if (d < worstDip) { worstDip = d; at = i; }
        }
        std::printf ("  D. OFF → ON（押し込みなし）: ON の直後 40 ms の 1 周期ごとの山の最小 %.1f dB（%.1f ms 後）。プチッの目安 %.1f\n", worstDip, (at - tOn) * 1000.0 / sr,
                     cleanliness (s.L, tOn - 2000, tOn + (size_t) (0.1 * sr), sr));
    }
}

// ============================================================================
static void modeBlock()
{
    std::printf ("== ブロックの大きさ（1・3・64・2048）と 256 の差（つまみは止めたまま）==\n");
    const double sr = 48000;
    for (const char* id : kNew)
    {
        std::mt19937 g (7);
        float worst = 0;
        for (int r = 0; r < 6; ++r)
        {
            auto kv = r == 0 ? defaults (id) : randomKnobs (id, g, true);
            Sig ref = pluckSeq (sr, 1.5, 0.5f);
            { auto m = makeSnapped (id, sr, 256, kv); runModule (*m, ref, 256); }
            for (int B : { 1, 3, 64, 2048 })
            {
                Sig s = pluckSeq (sr, 1.5, 0.5f);
                auto m = makeSnapped (id, sr, B, kv); runModule (*m, s, B);
                for (size_t i = 0; i < s.size(); ++i) worst = std::max (worst, std::abs (s.L[i] - ref.L[i]));
            }
        }
        std::printf ("  %-10s 差の最大 %.2e\n", id, worst);
    }
}

static void modeReset()
{
    std::printf ("== reset 後の残り（大きい音 1 秒 → reset → 無音 1 秒の最大）と、新品との差（reset → ギターふう）==\n");
    const double sr = 48000; const int B = 256;
    for (const char* id : kNew)
    {
        std::mt19937 g (9);
        float resid = 0, diff = 0;
        for (int r = 0; r < 8; ++r)
        {
            auto kv = r == 0 ? defaults (id) : randomKnobs (id, g, true);
            auto m = makeSnapped (id, sr, B, kv);
            Sig a = pluckSeq (sr, 1.0, 2.0f); runModule (*m, a, B);
            m->reset();
            Sig z = zeros (sr, 1.0); runModule (*m, z, B);
            resid = std::max (resid, stats (z).peak);
            m->reset();
            Sig p = pluckSeq (sr, 1.0, 0.5f), q = p; runModule (*m, p, B);
            auto f = makeSnapped (id, sr, B, kv); runModule (*f, q, B);
            for (size_t i = 0; i < p.size(); ++i) diff = std::max (diff, std::abs (p.L[i] - q.L[i]));
        }
        std::printf ("  %-10s 残り %.2e、新品との差 %.2e\n", id, resid, diff);
    }
}

static void modeRates()
{
    std::printf ("== サンプルレート（44.1 / 96 / 192 kHz）: 初期値・全部 0・全部 1・ランダム 20 通り ==\n");
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        std::printf (" %.0f Hz\n", sr);
        for (const char* id : kNew)
        {
            std::mt19937 g (3);
            float pk = 0; long bad = 0; float tail = 0; int lat = 0;
            for (int r = 0; r < 23; ++r)
            {
                auto d = defaults (id);
                auto kv = r == 0 ? d : r == 1 ? std::vector<float> (d.size(), 0.0f) : r == 2 ? std::vector<float> (d.size(), 1.0f) : randomKnobs (id, g, true);
                auto m = makeSnapped (id, sr, 512, kv);
                Sig s = pluckSeq (sr, 1.0, 1.0f); append (s, noise (sr, 0.5, 0.25f)); append (s, zeros (sr, 3.0));
                runModule (*m, s, 512);
                auto st = stats (s); pk = std::max (pk, st.peak); bad += st.bad;
                tail = std::max (tail, stats (s, s.size() - (size_t) (0.5 * sr)).peak);
                lat = std::max (lat, m->latency());
            }
            std::printf ("  %-10s 山 %+6.1f dBFS、NaN %ld、残り %.1e、遅れ %d\n", id, db (pk), bad, tail, lat);
        }
    }
}

static void setFZ() { uint64_t f; asm volatile ("mrs %0, fpcr" : "=r"(f)); f |= (1ull << 24); asm volatile ("msr fpcr, %0" :: "r"(f)); }

static void modeCpu()
{
    setFZ();
    std::printf ("== CPU（1 コアに対する %%、ブロック 256、5 回のうち最速。この Mac は別の計算で混んでいる）==\n");
    for (double sr : { 48000.0, 96000.0 })
    {
        std::printf (" %.0f Hz:\n", sr);
        for (const char* id : kNew)
        {
            auto run = [&] (const std::vector<float>& kv) {
                double best = 1e9;
                for (int rep = 0; rep < 5; ++rep)
                {
                    auto m = makeSnapped (id, sr, 256, kv);
                    Sig s = noise (sr, 2.0, 0.1f); append (s, zeros (sr, 1.0));
                    const double t0 = nowSec(); runModule (*m, s, 256); best = std::min (best, nowSec() - t0);
                }
                return best / 3.0 * 100.0;
            };
            auto d = defaults (id);
            std::vector<float> heavy = d;
            if (! std::strcmp (id, "tubepush")) heavy[3] = 1.0f;          // DIRT
            if (! std::strcmp (id, "tubepushxl")) heavy[2] = 1.0f;        // SATURATION
            if (! std::strcmp (id, "transient")) heavy[3] = 1.0f;         // CLIP
            std::printf ("  %-10s 初期値 %.2f %%、重い設定 %.2f %%\n", id, run (d), run (heavy));
        }
        // 8 段
        for (const char* id : { "tubepush", "tubepushxl", "fetcomp", "transient", "maximizer" })
        {
            Want w = emptyWant();
            for (int i = 0; i < 8; ++i) setSlot (w, i, id, ! std::strcmp (id, "transient") ? std::vector<std::pair<int, float>> { { 3, 1.0f } } : std::vector<std::pair<int, float>> {});
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                Rack r; r.prepare (sr, 256, w);
                Sig s = noise (sr, 2.0, 0.05f);
                const double t0 = nowSec();
                for (size_t off = 0; off < s.size(); off += 256) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> (256, s.size() - off), w, 120.0);
                best = std::min (best, nowSec() - t0);
            }
            std::printf ("  8 段すべて %-10s %.1f %%\n", id, best / 2.0 * 100);
        }
    }
}

static void modePedal()
{
    std::printf ("== VOLUME PEDAL: MIN × HALF ごとに PEDAL 0..1 の中でいちばん大きい dB（VOLUME 0 dB のとき）==\n        ");
    for (float hf : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) std::printf ("  HALF %.2f      ", hf);
    std::printf ("\n");
    for (float mn : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        std::printf ("  MIN %.2f", mn);
        for (float hf : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            float best = -1e9, at = 0;
            for (int i = 0; i <= 1000; ++i) { const float p = i / 1000.0f; const float d = VolumePedal::pedalDb (p, mn, hf); if (d > best) { best = d; at = p; } }
            std::printf ("  %+6.2f (@%.2f)", best, at);
        }
        std::printf ("\n");
    }
    // 実際に通して: VOLUME 1・MIN 0・HALF 1・PEDAL 0.75
    {
        auto m = makeSnapped ("volpedal", 48000, 256, { 0.75f, 1.0f, 0.0f, 1.0f });
        Sig s = sine (48000, 0.5, 1000, 0.5f); runModule (*m, s, 256);
        std::printf ("  PEDAL 0.75・VOLUME 1・MIN 0・HALF 1 で −6 dBFS を入れると 出口 %+.1f dBFS\n", db (stats (s, 12000).peak));
    }
    // PEDAL を 0 → 1 を 50 ms で（エクスプレッションペダルを速く踏む）
    {
        const double sr = 48000; const int B = 64;
        auto m = makeSnapped ("volpedal", sr, B, { 0.0f, 0.425f, 0.5f, 0.5f });
        Sig s = sine (sr, 1.0, 220, 0.25f);
        for (size_t off = 0; off < s.size(); off += B)
        {
            const double t = (double) off / sr;
            if (t > 0.3 && t < 0.35) m->setKnob (0, (float) ((t - 0.3) / 0.05));
            if (t >= 0.35) m->setKnob (0, 1.0f);
            m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off));
        }
        std::printf ("  PEDAL 0 → 1 を 50 ms（ブロック 64）: プチッの目安 %.1f\n", cleanliness (s.L, (size_t) (0.25 * sr), (size_t) (0.5 * sr), sr));
    }
}

static void modeTrans()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== TRANSIENT ==\n");
    // 頭の持ち上げの最大
    for (float clip : { 0.0f, 1.0f })
        for (float gain : { 0.56f, 1.0f })
        {
            auto m = makeSnapped ("transient", sr, B, { 1.0f, 0.5f, gain, clip, 1.0f });
            Sig s = pluckSeq (sr, 2.0, 0.5f), in = s; runModule (*m, s, B);
            std::printf ("  ATTACK 100 %%・GAIN %+.1f dB・CLIP %s・ギターふう −6 dBFS: 出口の山 %+.1f dBFS（入口 %+.1f）\n", Transient::gainDb (gain), clip > 0.5f ? "ON " : "OFF", db (stats (s).peak), db (stats (in).peak));
        }
    {
        auto m = makeSnapped ("transient", sr, B, { 1.0f, 0.5f, 1.0f, 0.0f, 1.0f });
        Sig s = impulse (sr, 0.2, 0.0f); for (size_t i = 0; i < (size_t) (0.1 * sr); ++i) s.L[i] = s.R[i] = 0; // 無音
        Sig k = zeros (sr, 1.0); for (size_t i = 24000; i < k.size(); ++i) { k.L[i] = k.R[i] = (float) (0.9 * std::sin (2 * M_PI * 80 * (double) (i - 24000) / sr)); }
        runModule (*m, k, B);
        std::printf ("  ATTACK 100 %%・GAIN +6・CLIP OFF・無音から 0.9 の 80 Hz がいきなり: 出口の山 %+.1f dBFS\n", db (stats (k).peak));
    }
    // SUSTAIN 100 %: 音が終わったあとの −80 dBFS のノイズ（録音の床）がどれだけ上がるか
    {
        auto m = makeSnapped ("transient", sr, B, { 0.5f, 1.0f, 0.5f, 1.0f, 1.0f });
        Sig s = pluckSeq (sr, 1.0, 0.5f);
        for (size_t i = (size_t) (0.5 * sr); i < s.size(); ++i) s.L[i] = s.R[i] = 0;
        Sig n = noise (sr, 4.0, 0.0001f); append (s, n);
        Sig in = s; runModule (*m, s, B);
        for (double t0 : { 0.6, 1.0, 2.0, 3.0, 4.0 })
        {
            const size_t a = (size_t) (t0 * sr), b = a + (size_t) (0.1 * sr);
            std::printf ("  SUSTAIN 100 %%: %.1f s（音が止まって %.1f s）の −80 dBFS の床 → %+.1f dB\n", t0, t0 - 0.5, db (stats (s, a, b).rms / stats (in, a, b).rms));
        }
    }
}

// ============================================================================
// エンジンを直接（表の値の範囲が広がったときに壊れないか）
static DynSpec rawSpec (const float* base)
{
    DynSpec s {}; for (int i = 0; i < dpCount; ++i) s.base[i] = base[i]; s.nKnobs = 0; return s;
}
static void modeEngine()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== エンジンの飽和の形 shape(): 押し込み d（dB）× 偏り b で、0 dBFS / +6 dBFS の 100 Hz を入れた出口の山（dBFS）==\n");
    std::printf ("            ");
    for (float b : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, -1.0f }) std::printf ("  b=%+.2f        ", b);
    std::printf ("\n");
    for (float dDb : { -20.0f, 0.0f, 5.77f, 10.0f, 15.0f, 20.0f, 30.0f })
    {
        std::printf ("  d %+5.1f dB", dDb);
        for (float b : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, -1.0f })
        {
            float out[2];
            for (int a = 0; a < 2; ++a)
            {
                float base[dpCount]; std::copy (dynDefaults(), dynDefaults() + dpCount, base);
                base[dpSat2] = dDb; base[dpBias2] = b;
                DynSpec sp = rawSpec (base); DynEngine e (sp); e.prepare (sr, B);
                Sig s = sine (sr, 1.0, 100, a ? 2.0f : 1.0f); runModule (e, s, B);
                out[a] = stats (s, (size_t) (0.5 * sr)).peak;
                if (stats (s).bad) out[a] = -1;
            }
            std::printf ("  %+6.1f/%+6.1f", db (out[0]), db (out[1]));
        }
        std::printf ("\n");
    }
    // 広い範囲のランダム（つまみの表がどんな値でも）
    std::printf ("== エンジンの値をランダムに（広め 3000 通り）: NaN・+24 dBFS 超え・1000 超え（ラックが部品を reset する大きさ）==\n");
    {
        std::mt19937 g (42); std::uniform_real_distribution<float> u (0, 1);
        auto R = [&] (float a, float b) { return a + (b - a) * u (g); };
        long nBad = 0, n16 = 0, n1000 = 0, nTail = 0; std::string ex16, ex1000, exBad, exTail;
        for (int it = 0; it < 3000; ++it)
        {
            float b[dpCount];
            b[dpPre] = R (-30, 30); b[dpThr] = R (-70, 10); b[dpRatio] = R (0, 7); b[dpKnee] = R (0, 60); b[dpAtt] = R (-5, 0); b[dpRel] = R (-4, 1);
            b[dpMakeup] = R (-60, 45); b[dpDry] = R (-0.1f, 1.1f); b[dpWet] = R (-0.1f, 1.1f); b[dpSat1] = R (-60, 25); b[dpBias1] = R (-1, 1);
            b[dpSat2] = R (-60, 25); b[dpBias2] = R (-1, 1); b[dpScHp] = R (0, 3.5f); b[dpRms] = R (-6, 0); b[dpLow] = R (-15, 15); b[dpHigh] = R (-15, 15);
            b[dpRange] = R (0, 100); b[dpExpThr] = R (-120, -10); b[dpExpRatio] = R (0, 4); b[dpExpRange] = R (0, 100); b[dpGAtt] = R (-4, 0); b[dpGRel] = R (-4, 1);
            b[dpRel2] = R (-2, 1.5f); b[dpRel2Frac] = R (0, 1); b[dpLook] = R (0, 20); b[dpOut] = R (-20, 20);
            DynSpec sp = rawSpec (b); DynEngine e (sp); e.prepare (sr, B);
            Sig s = pluckSeq (sr, 1.0, 0.5f); append (s, sine (sr, 0.5, 100, 2.0f)); append (s, zeros (sr, 3.0));
            runModule (e, s, B);
            auto st = stats (s);
            char desc[200]; std::snprintf (desc, sizeof desc, "makeup %.0f sat2 %.1f b2 %.2f sat1 %.1f b1 %.2f out %.0f", b[dpMakeup], b[dpSat2], b[dpBias2], b[dpSat1], b[dpBias1], b[dpOut]);
            if (st.bad) { ++nBad; exBad = desc; }
            if (st.peak > 16) { ++n16; ex16 = desc; }
            if (st.peak > 1000) { ++n1000; ex1000 = desc; }
            if (stats (s, s.size() - (size_t) (0.5 * sr)).peak > 1e-4f) { ++nTail; exTail = desc; }
        }
        std::printf ("  NaN あり %ld 通り %s\n  +24 dBFS 超え %ld 通り（例 %s）\n  1000 超え %ld 通り（例 %s）\n  3 秒後も −80 dBFS より大きい %ld 通り %s\n",
                     nBad, exBad.c_str(), n16, ex16.c_str(), n1000, ex1000.c_str(), nTail, exTail.c_str());
    }
    // 先読み dpLook: 申告の遅れと実際、ドライとのずれ（飽和があるとき・MIX 50 %）
    std::printf ("== 先読み（dpLook）==\n");
    for (float look : { 0.0f, 5.0f, 20.0f, 24.0f, 25.0f, 26.0f, 30.0f, 45.0f })
    {
        float b[dpCount]; std::copy (dynDefaults(), dynDefaults() + dpCount, b);
        b[dpLook] = look;
        DynSpec sp = rawSpec (b); DynEngine e (sp); e.prepare (sr, B);
        Sig s = impulse (sr, 0.2, 0.01f); runModule (e, s, B);
        size_t at = 0; float pk = 0; for (size_t i = 0; i < s.size(); ++i) if (std::abs (s.L[i]) > pk) { pk = std::abs (s.L[i]); at = i; }
        std::printf ("  dpLook %4.1f ms: 申告 %4d、実際 %4zu（大きさ %.4f）%s\n", look, e.latency(), at, pk, (int) at == e.latency() ? "" : "  ★ずれ");
    }
}

// 先読み＋飽和＋ MIX 50 % のくし形
static void modeLookMix()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== 先読み＋飽和（4 倍）＋ドライ 50 %% のくし形（−60 dBFS インパルスの特性）==\n");
    for (float look : { 0.0f, 2.0f })
    {
        float b[dpCount]; std::copy (dynDefaults(), dynDefaults() + dpCount, b);
        b[dpSat1] = 0.0f; b[dpSat2] = 0.0f; b[dpDry] = 0.5f; b[dpWet] = 0.5f; b[dpLook] = look;
        DynSpec sp = rawSpec (b); DynEngine e (sp); e.prepare (sr, B);
        const size_t N = 16384; Sig s = zeros (sr, (double) N / sr + 0.5); s.L[100] = s.R[100] = 0.001f;
        runModule (e, s, B);
        double lo = 1e9, loF = 0;
        for (double f = 30; f < 20000; f *= 1.02)
        {
            std::complex<double> acc = 0;
            for (size_t i = 0; i < N; ++i) acc += (double) s.L[100 + i] * std::polar (1.0, -2 * M_PI * f * (double) i / sr);
            const double d = db (std::abs (acc) / 0.001);
            if (d < lo) { lo = d; loF = f; }
        }
        std::printf ("  dpLook %.0f ms（飽和 2 段あり・ドライ 0.5 ＋ウェット 0.5）: 最小 %+.1f dB（%.0f Hz）\n", look, lo, loF);
    }
}

// 選ぶつまみ・切り替えでエンジンの値が段で飛ぶとき（表の形を問わず）
static void modeEngineSteps()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== エンジンの値を段で切り替えたときのプチッ（220 Hz −12 dBFS、圧縮がかかる設定）==\n");
    struct P { int idx; float a, b; const char* name; };
    std::vector<P> ps {
        { dpRms, -5.0f, -1.5f, "山 ↔ RMS（dpRms）" }, { dpScHp, 0.0f, 3.0f, "側鎖 HP OFF ↔ 1 kHz" }, { dpAtt, -4.5f, -1.0f, "ATTACK 0.03 ↔ 100 ms" },
        { dpRel, -2.0f, 0.5f, "RELEASE 10 ms ↔ 3 s" }, { dpRatio, 1.0f, 4.0f, "比率 2 ↔ 16" }, { dpRange, 100.0f, 3.0f, "下げる上限 100 ↔ 3 dB" },
        { dpSat1, -60.0f, 12.0f, "前の飽和 なし ↔ +12 dB" }, { dpBias1, 0.0f, 0.5f, "前の偏り 0 ↔ 0.5（押し込み +6 dB）" },
        { dpExpRatio, 0.0f, 2.0f, "ゲート なし ↔ 1:4" }, { dpRel2Frac, 0.0f, 0.8f, "2 段の戻り 0 ↔ 0.8" }, { dpLook, 0.0f, 3.0f, "先読み 0 ↔ 3 ms（表には入らない。参考）" },
        { dpThr, -30.0f, -10.0f, "しきい値 −30 ↔ −10（なめらか）" }, { dpMakeup, 0.0f, 12.0f, "MAKEUP 0 ↔ 12（なめらか）" },
    };
    for (auto& p : ps)
    {
        float worst = 0, base = 0;
        for (int dir = 0; dir < 2; ++dir)
        {
            DynSpec sp {}; std::copy (dynDefaults(), dynDefaults() + dpCount, sp.base);
            sp.base[dpThr] = -30; sp.base[dpRatio] = 2; sp.base[dpAtt] = -2.5f; sp.base[dpRel] = -1;
            if (p.idx == dpBias1) sp.base[dpSat1] = 6.0f;
            if (p.idx == dpExpRatio) { sp.base[dpExpThr] = -20; sp.base[dpExpRange] = 40; }
            if (p.idx == dpRel2Frac) sp.base[dpRel2] = 0.5f;
            sp.nKnobs = 1; sp.knobDef[0] = 0;
            auto& t = sp.knobs[0]; t.points = 2; t.steps = true; t.nParams = 1; t.params[0] = p.idx; t.xs[0] = 0; t.xs[1] = 1;
            t.vals[0][0] = dir ? p.b : p.a; t.vals[1][0] = dir ? p.a : p.b;
            sp.base[p.idx] = t.vals[0][0];
            DynEngine e (sp); e.prepare (sr, B); e.setKnob (0, 0.0f);
            Sig s = sine (sr, 2.0, 220, 0.25f);
            for (size_t i = 0; i < (size_t) (0.1 * sr); ++i) { s.L[i] *= (float) i / (float) (0.1 * sr); s.R[i] = s.L[i]; }
            const size_t t1 = (size_t) (1.0 * sr) / B * B;
            for (size_t off = 0; off < s.size(); off += B) { if (off == t1) e.setKnob (0, 1.0f); e.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off)); }
            base = std::max (base, cleanliness (s.L, (size_t) (0.5 * sr), t1, sr));
            base = std::max (base, cleanliness (s.L, t1 + (size_t) (0.5 * sr), s.size(), sr));
            worst = std::max (worst, cleanliness (s.L, t1, t1 + (size_t) (0.1 * sr), sr));
        }
        std::printf ("  %-34s 切り替え %7.1f（ふだん %.1f）%s\n", p.name, worst, base, worst > 10 && worst > 3 * base ? "  ★" : "");
    }
}

static void modeTargets()
{
    const double sr = 48000; const int B = 256;
    struct C { const char* id; std::vector<std::pair<int, float>> kv; const char* name; };
    std::vector<C> cs {
        { "tubepush", {}, "TUBE PUSH 初期値" },
        { "tubepush", { { 0, 1.0f }, { 1, 1.0f } }, "TUBE PUSH INPUT 1・COMPRESS 1" },
        { "tubepush", { { 1, 1.0f } }, "TUBE PUSH COMPRESS 1" },
        { "tubepush", { { 0, 1.0f } }, "TUBE PUSH INPUT 1" },
        { "tubepush", { { 0, 0.0f }, { 1, 1.0f } }, "TUBE PUSH INPUT 0・COMPRESS 1" },
        { "tubepush", { { 0, 1.0f }, { 1, 1.0f }, { 3, 1.0f }, { 5, 1.0f } }, "TUBE PUSH INPUT 1・COMPRESS 1・DIRT・OUTPUT 1" },
        { "tubepushxl", {}, "TUBE PUSH XL 初期値" },
        { "tubepushxl", { { 0, 1.0f }, { 4, 1.0f } }, "TUBE PUSH XL INPUT 1・COMPRESS 1" },
        { "tubepushxl", { { 2, 1.0f }, { 10, 1.0f } }, "TUBE PUSH XL SATURATION 1・OUTPUT 1" },
        { "fetcomp", {}, "FET COMP 初期値" },
        { "fetcomp", { { 4, 1.0f } }, "FET COMP OUTPUT 1" },
        { "fetcomp", { { 4, 1.0f }, { 5, 0.0f } }, "FET COMP OUTPUT 1・DRY 0" },
        { "fetcomp", { { 0, 0.0f } }, "FET COMP INPUT 0" },
        { "optocomp", {}, "OPTO COMP 初期値" },
        { "optocomp", { { 0, 1.0f } }, "OPTO COMP GAIN 1" },
        { "optocomp", { { 0, 1.0f }, { 1, 0.0f } }, "OPTO COMP GAIN 1・PEAK RED 0" },
        { "vcacomp", {}, "VCA COMP 初期値" },
        { "vcacomp", { { 0, 1.0f }, { 2, 1.0f } }, "VCA COMP THRESHOLD 1・OUTPUT 1" },
        { "chandyn", { { 11, 1.0f } }, "CHANNEL DYN WET 1" },
        { "transient", { { 0, 1.0f }, { 3, 0.0f } }, "TRANSIENT ATTACK 100 %・CLIP OFF" },
        { "volpedal", { { 1, 1.0f } }, "VOLUME PEDAL VOLUME 1" },
    };
    std::printf ("  %-48s %-26s %-26s\n", "", "ギターふう −12 dBFS（山/rms）", "ギターふう 0 dBFS（山/rms）");
    for (auto& c : cs)
    {
        std::printf ("  %-48s", c.name);
        for (float pk : { 0.25f, 1.0f })
        {
            auto kv = defaults (c.id); for (auto& p : c.kv) kv[(size_t) p.first] = p.second;
            auto m = makeSnapped (c.id, sr, B, kv);
            Sig s = pluckSeq (sr, 4.0, pk), in = s; runModule (*m, s, B);
            const int lat = m->latency();
            auto so = stats (s, (size_t) sr), si = stats (in, (size_t) sr);
            (void) lat;
            std::printf ("  %+6.1f / %+6.1f dBFS（%+5.1f dB）", db (so.peak), db (so.rms), db (so.rms / si.rms));
        }
        std::printf ("\n");
    }
}

static void modeRackReset()
{
    const double sr = 48000; const int B = 256;
    for (int on = 0; on < 2; ++on)
    {
        Want w {}; for (auto& s : w) { s.type = 0; s.on = true; for (auto& k : s.k) k = 0.5f; }
        w[0].type = typeForId ("maximizer"); w[0].on = on; const auto& inf = *infoFor (w[0].type); for (int k = 0; k < kMaxKnobs; ++k) w[0].k[k] = k < (int) inf.knobs.size() ? inf.knobs[(size_t) k].def : 0.5f;
        Rack r; r.prepare (sr, B, w);
        Sig s = sine (sr, 1.0, 220, 0.5f);
        for (size_t off = 0; off < s.size(); off += B) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        r.resetAll();
        Sig z = zeros (sr, 0.1);
        for (size_t off = 0; off < z.size(); off += B) r.process (z.L.data() + off, z.R.data() + off, (int) std::min<size_t> ((size_t) B, z.size() - off), w, 120.0);
        size_t last = 0; for (size_t i = 0; i < z.size(); ++i) if (std::abs (z.L[i]) > 1e-6f) last = i;
        std::printf ("  MAXIMIZER の段 %s: reset のあと無音を入れたら、出口の最大 %+.1f dBFS（%zu サンプル目まで）\n", on ? "ON " : "OFF", db (stats (z).peak), last);
    }
}

// 音が流れている最中のホストの reset で、部品を通らない音が漏れないか（DSP 検査 0.8.0: 遅れのある MORPH FILTER がコンプ用の「ドライ → 15 ms で部品の音へ」に入って、
// LP 194 Hz なのに 2 kHz より上が −104 → −18 dBFS に跳ねた）。ホワイトノイズ −12 dBFS を流したまま 1 秒で reset、2 kHz より上の 1 ms ごとの強さを reset の前と後 30 ms で比べる
static void modeResetLeak()
{
    const double sr = 48000; const int B = 256;
    struct Case { const char* id; const char* what; std::vector<std::pair<int, float>> knobs; };
    const std::vector<Case> cases = {
        { "morphflt", "LP 194 Hz 24 dB", { { 0, 0.25f }, { 2, 1.0f } } }, { "morphflt", "LP 52 Hz 24 dB", { { 0, 0.0f }, { 2, 1.0f } } },
        { "howlwah", "WAH 0", { { 1, 0.0f } } }, { "vintwah", "WAH 0", { { 1, 0.0f } } }, { "customwah", "初期値", {} }, { "vowelwah", "初期値", {} },
        { "tapewow", "初期値", {} }, { "transient", "初期値", {} }, { "fetcomp", "初期値", {} }, { "maximizer", "初期値", {} } };
    // 2 kHz の 2 次の HP（RBJ）を 2 段
    auto hp = [&] (const std::vector<float>& x) {
        const double w0 = 2.0 * M_PI * 2000.0 / sr, al = std::sin (w0) / (2.0 * 0.7071), cs = std::cos (w0);
        const double b0 = (1 + cs) / 2, b1 = -(1 + cs), b2 = (1 + cs) / 2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
        std::vector<double> y (x.begin(), x.end());
        for (int st = 0; st < 2; ++st)
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y) { const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = in; y2 = y1; y1 = o; v = o; }
        }
        return y;
    };
    std::printf ("== 音が流れている最中の reset: 2 kHz より上の 1 ms ごとの強さの最大（dBFS）、reset の前（ふだん）→ 後 30 ms（★ = 6 dB より上に跳ね、しかも −60 dBFS より上）==\n");
    for (auto& c : cases)
    {
        Want w {}; for (auto& sl : w) { sl.type = 0; sl.on = true; for (auto& k : sl.k) k = 0.5f; }
        w[0].type = typeForId (c.id); const auto& inf = *infoFor (w[0].type);
        for (int k = 0; k < kMaxKnobs; ++k) w[0].k[k] = k < (int) inf.knobs.size() ? inf.knobs[(size_t) k].def : 0.5f;
        for (auto& kv : c.knobs) w[0].k[kv.first] = kv.second;
        Rack r; r.prepare (sr, B, w);
        Sig s = noise (sr, 1.5, 0.25f);
        const size_t t0 = (size_t) (1.0 * sr) / B * B;
        for (size_t off = 0; off < s.size(); off += B)
        {
            if (off == t0) r.resetAll();
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        }
        const auto y = hp (s.L);
        const size_t win = (size_t) (0.001 * sr);
        auto maxDb = [&] (size_t a, size_t b) { double mx = 1e-30; for (size_t i = a; i + win <= b; i += win) { double e = 0; for (size_t j = i; j < i + win; ++j) e += y[j] * y[j]; mx = std::max (mx, e / (double) win); } return 10.0 * std::log10 (mx); };
        const double before = maxDb ((size_t) (0.5 * sr), t0), after = maxDb (t0, t0 + (size_t) (0.03 * sr));
        std::printf ("  %-10s %-16s %7.1f → %7.1f%s\n", c.id, c.what, before, after, (after > before + 6.0 && after > -60.0) ? "  ★" : "");
    }
}

static const char* kAll[] = { "gate", "nred", "tubecomp", "limit", "studiovb", "spring", "twindly",
                              "tubepush", "tubepushxl", "fetcomp", "optocomp", "vcacomp", "transient", "buscomp", "chandyn", "quickcomp", "pedalcomp", "volpedal", "maximizer",
                              "spacevb", "classicvb", "shaperoom", "pickroom", "clubverb", "digihall", "digihallxl", "frostverb", "eightverb",
                              "warpdly", "bbdecho", "widedly", "clubdly", "styledly", "tapeheads",
                              "shelfeq", "consoleeq", "parameq", "graphiceq", "scoopeq", "tremolo", "ringmod", "flanger",
                              "electricfl", "multichorus", "vibeensemble", "scannervibe",
                              "classicph", "sweepph", "tapewow", "rotary",
                              "morphflt", "howlwah", "vintwah", "customwah", "vowelwah",
                              "touchflt", "peakflt" };


// ============================================================================
// 全部の部品（0.1.0 の 7 個も）: つまみ 0 / 1 / 0.5 / 初期値 / ランダム、途中でつまみを動かす、いろいろなレート・ブロック
static void modeFuzzAll (int nRandom)
{
    static const char* inName[] = { "+12dBFS 100Hz", "pluck -6", "noise -12", "impulse", "0dBFS 7kHz", "silence", "+24dBFS noise", "tiny 1e-30", "DC 0.5" };
    std::printf ("== 全部の部品: つまみ 0 / 1 / 0.5 / 初期値 / ランダム %d 通り × 入力 9 種 × レート・ブロック（途中でつまみも動かす）==\n", nRandom);
    for (const char* id : kAll)
    {
        if (const char* only = std::getenv ("FUZZ_ONLY")) if (std::strcmp (only, id) != 0) continue;   // FUZZ_ONLY=部品 で 1 つだけ
        std::mt19937 g (777);
        const auto& inf = *infoFor (typeForId (id));
        std::vector<std::vector<float>> sets;
        auto d = defaults (id); sets.push_back (d);
        sets.push_back (std::vector<float> (d.size(), 0.0f));
        sets.push_back (std::vector<float> (d.size(), 1.0f));
        sets.push_back (std::vector<float> (d.size(), 0.5f));
        for (int r = 0; r < nRandom; ++r) sets.push_back (randomKnobs (id, g, r % 3 != 0));
        float worstPk = 0; std::string pkAt; long bad = 0; std::string badAt; long over16 = 0; std::string overAt; float tail = 0; std::string tailAt; long big1000 = 0;
        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
        const int blocks[] = { 1, 7, 64, 256, 2048 };
        int run = 0;
        for (size_t si = 0; si < sets.size(); ++si)
            for (int in = 0; in < 9; ++in, ++run)
            {
                const double sr = rates[run % 4] == 192000.0 && (run % 3) ? 48000.0 : rates[run % 4];
                const int B = blocks[run % 5];
                auto m = makeSnapped (id, sr, std::max (B, 16), sets[si]);
                Sig s;
                switch (in)
                {
                    case 0: s = sine (sr, 1.0, 100, 4.0f); break;
                    case 1: s = pluckSeq (sr, 1.0, 0.5f); break;
                    case 2: s = noise (sr, 1.0, 0.25f); break;
                    case 3: s = impulse (sr, 0.3, 1.0f); break;
                    case 4: s = sine (sr, 1.0, 7000, 1.0f); break;
                    case 5: s = zeros (sr, 0.5); break;
                    case 6: s = noise (sr, 1.0, 16.0f, 5); break;
                    case 7: s = noise (sr, 1.0, 1e-30f, 6); break;
                    default: { s = zeros (sr, 1.0); for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = 0.5f; } break;
                }
                const size_t inLen = s.size();
                const double tailSec = (std::string (id) == "studiovb" || std::string (id) == "spring" || std::string (id) == "twindly") ? 8.0 : 4.0;
                append (s, zeros (sr, tailSec));
                // 途中でつまみを動かす（入力の間だけ、0.1 秒ごと。半分は一気に、半分はランダムな値へ）
                std::vector<float> kv = sets[si];
                std::uniform_real_distribution<float> u (0.0f, 1.0f);
                const size_t step = (size_t) (0.1 * sr);
                size_t nextMove = step;
                for (size_t off = 0; off < s.size(); off += (size_t) B)
                {
                    const int n = (int) std::min ((size_t) B, s.size() - off);
                    if (off >= nextMove && off < inLen && (si % 2 == 1))
                    {
                        nextMove += step;
                        const int k = (int) (u (g) * (float) inf.knobs.size()) % (int) inf.knobs.size();
                        kv[(size_t) k] = u (g) < 0.3f ? (u (g) < 0.5f ? 0.0f : 1.0f) : u (g);
                        m->setKnob (k, kv[(size_t) k]);
                    }
                    if (off >= inLen && off < inLen + (size_t) B && (si % 2 == 1))
                        for (int k = 0; k < (int) inf.knobs.size(); ++k) m->setKnob (k, sets[si][(size_t) k]);   // 余韻は最初の設定で
                    m->process (s.L.data() + off, s.R.data() + off, n);
                    if (std::getenv ("FUZZ_DEBUG") != nullptr)
                    {
                        static bool shown = false;
                        for (int i = 0; i < n && ! shown; ++i)
                            if (! std::isfinite (s.L[off + (size_t) i]) || ! std::isfinite (s.R[off + (size_t) i]))
                            {
                                shown = true;
                                std::printf ("    [FUZZ_DEBUG] 最初の NaN: 入力 %s sr %.0f B %d、%.4f 秒目。そのときのつまみ {", inName[in], sr, B, (double) (off + (size_t) i) / sr);
                                for (float x : kv) std::printf ("%.4f,", x);
                                std::printf ("}\n");
                            }
                    }
                }
                auto st = stats (s);
                long ov = 0, b1000 = 0;
                for (size_t i = 0; i < s.size(); ++i) { const float a = std::max (std::abs (s.L[i]), std::abs (s.R[i])); if (a > 16.0f) ++ov; if (a > 1000.0f) ++b1000; }
                auto tl = stats (s, s.size() - (size_t) (0.5 * sr));
                char lab[512]; std::snprintf (lab, sizeof lab, "%s sr %.0f B %d %s", inName[in], sr, B, kvStr (sets[si]).c_str());
                if (st.peak > worstPk) { worstPk = st.peak; pkAt = lab; }
                if (st.bad) { bad += st.bad; badAt = lab; }
                if (ov > over16) { over16 = ov; overAt = lab; }
                big1000 += b1000;
                if (in != 8 && tl.peak > tail) { tail = tl.peak; tailAt = lab; }
            }
        std::printf ("  %-10s 山 %+6.1f dBFS [%s]\n", id, db (worstPk), pkAt.c_str());
        std::printf ("  %-10s NaN %ld %s | >16 最大 %ld サンプル %s | >1000 %ld | 最後 0.5 秒の最大 %.1e [%s]\n", "", bad, badAt.c_str(), over16, overAt.c_str(), big1000, tail, tailAt.c_str());
    }
}

// ============================================================================
// 遅れ: 申告と実際（小さい正弦波の位相）
static void modeLat()
{
    std::printf ("== 遅れの申告と実際（−40 dBFS の正弦波の位相の遅れ、サンプル）==\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf (" %.0f Hz\n", sr);
        for (const char* id : kAll)
        {
            auto kv = defaults (id);
            if (! std::strcmp (id, "maximizer")) { kv[1] = 1.0f; kv[2] = 1.0f; }   // 押し込みなし（線形）
            auto m = makeSnapped (id, sr, 256, kv);
            Sig in = sine (sr, 1.5, 100, 0.01f); Sig s2 = sine (sr, 1.5, 1000, 0.01f);
            Sig a = in, b = s2; runModule (*m, a, 256);
            auto m2 = makeSnapped (id, sr, 256, kv); runModule (*m2, b, 256);
            const size_t from = (size_t) (0.7 * sr), to = (size_t) (1.5 * sr) - 1;
            const int lat = m->latency();
            const double d1 = phaseDelayHint (in.L, a.L, 100, sr, from, to, lat), d2 = phaseDelayHint (s2.L, b.L, 1000, sr, from, to, lat);
            if (lat > 0 || std::abs (d1) > 0.6 || std::abs (d2) > 0.6)
                std::printf ("  %-10s 申告 %4d、実際 100 Hz %7.2f / 1 kHz %7.2f %s\n", id, lat, d1, d2, (! std::strcmp (id, "studiovb") || ! std::strcmp (id, "spring") || ! std::strcmp (id, "twindly")) ? "  （響きの位相。遅れではない）" : std::abs (d2 - lat) > 0.6 ? "  ★1 kHz で 0.6 サンプルより大きいずれ" : "");
        }
    }
}


static void modeTrans2()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== TRANSIENT: CLIP の ON/OFF ==\n");
    // 1. 小さい音（頭打ちにかからない）で CLIP を入れ替えても、音が 1 ビットも変わらないか（いつも 4 倍を通す）
    {
        std::vector<float> kv = defaults ("transient");
        Sig a = pluckSeq (sr, 2.0, 0.1f), b = a;
        auto m1 = makeSnapped ("transient", sr, B, kv);
        auto m2 = makeSnapped ("transient", sr, B, kv);
        for (size_t off = 0; off < a.size(); off += B)
        {
            const bool flip = ((off / (size_t) (0.25 * sr)) % 2) == 1;
            m2->setKnob (3, flip ? 0.0f : 1.0f);
            m1->process (a.L.data() + off, a.R.data() + off, (int) std::min<size_t> ((size_t) B, a.size() - off));
            m2->process (b.L.data() + off, b.R.data() + off, (int) std::min<size_t> ((size_t) B, b.size() - off));
        }
        float d = 0; for (size_t i = 0; i < a.size(); ++i) d = std::max (d, std::abs (a.L[i] - b.L[i]));
        std::printf ("  −20 dBFS のギターふうで 0.25 秒ごとに CLIP ON/OFF: いつも ON との差の最大 %.2e（%.1f dB）\n", d, db (d));
    }
    // 2. 頭打ちにかかる大きさで切り替え: 8 kHz より上の山（切り替えの前後のふだんの値と比べる）
    for (float amp : { 0.5f, 1.0f, 2.0f })
        for (int dir = 0; dir < 2; ++dir)
        {
            auto kv = defaults ("transient"); kv[2] = 1.0f; kv[3] = dir ? 0.0f : 1.0f;
            auto m = makeSnapped ("transient", sr, B, kv);
            Sig s = sine (sr, 2.0, 220, amp);
            const size_t t1 = (size_t) (1.0 * sr) / B * B;
            for (size_t off = 0; off < s.size(); off += B) { if (off == t1) m->setKnob (3, dir ? 1.0f : 0.0f); m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off)); }
            const float before = hfBurst (s.L, (size_t) (0.5 * sr), t1, sr), after = hfBurst (s.L, t1 + (size_t) (0.3 * sr), (size_t) (1.9 * sr), sr), at = hfBurst (s.L, t1, t1 + (size_t) (0.1 * sr), sr);
            std::printf ("  220 Hz %+5.1f dBFS・GAIN +5.8: CLIP %s: 8 kHz より上の山 切り替え中 %.4f / 前 %.4f / 後 %.4f %s\n", db (amp), dir ? "OFF→ON" : "ON→OFF", at, before, after, at > 1.5f * std::max (before, after) + 1e-4f ? "★" : "");
        }
    // 3. CLIP OFF で線形か（−20 dBFS 1 kHz の倍音）
    {
        auto kv = defaults ("transient"); kv[3] = 0.0f; kv[0] = 0.5f; kv[1] = 0.5f;
        auto m = makeSnapped ("transient", sr, B, kv);
        Sig s = sine (sr, 2.5, 1000, 0.1f); runModule (*m, s, B);
        double h, a, af; aliasReport (s.L, (size_t) (1.2 * sr), sr, 1000, h, a, af);
        std::printf ("  CLIP OFF・ATTACK/SUSTAIN 0 %%・1 kHz −20 dBFS: 倍音の最大 %+.1f dB\n", db (h));
    }
    // 4. CLIP ON の折り返し（大きい音・高い音）
    for (double f0 : { 2999.0, 4999.0, 7001.0, 9973.0 })
        for (float amp : { 1.0f, 4.0f })
        {
            auto kv = defaults ("transient"); kv[2] = 1.0f; kv[3] = 1.0f; kv[0] = 0.5f; kv[1] = 0.5f;
            auto m = makeSnapped ("transient", sr, B, kv);
            Sig s = sine (sr, 2.5, f0, amp); runModule (*m, s, B);
            double h, a, af; aliasReport (s.L, (size_t) (1.2 * sr), sr, f0, h, a, af);
            std::printf ("  CLIP ON・GAIN +5.8・%5.0f Hz %+5.1f dBFS: 出口の山 %+5.1f dBFS、倍音 %+6.1f dB、折り返し %+6.1f dB（%5.0f Hz）\n", f0, db (amp), db (stats (s, (size_t) sr).peak), db (h), db (a), af);
        }
    // 5. 遅れ: 位相
    for (float clip : { 0.0f, 1.0f })
    {
        auto kv = defaults ("transient"); kv[3] = clip; kv[0] = 0.5f; kv[1] = 0.5f;
        auto m = makeSnapped ("transient", sr, B, kv);
        Sig in = sine (sr, 1.5, 100, 0.01f), s = in; runModule (*m, s, B);
        std::printf ("  CLIP %s: 申告 %d、実際 100 Hz %.2f サンプル\n", clip > 0.5f ? "ON " : "OFF", m->latency(), phaseDelayHint (in.L, s.L, 100, sr, (size_t) (0.7 * sr), (size_t) (1.4 * sr), 4));
    }
    // 6. SUSTAIN の静的な分: 音のない所の −80 / −60 dBFS の床がどれだけ上がる / 下がるか（SUSTAIN 0 / 0.36 / 1）
    for (float sus : { 0.0f, 0.36f, 0.5f, 1.0f })
        for (float fl : { 0.0001f, 0.001f })
        {
            auto kv = defaults ("transient"); kv[1] = sus;
            auto m = makeSnapped ("transient", sr, B, kv);
            Sig s = noise (sr, 3.0, fl, 11), in = s; runModule (*m, s, B);
            std::printf ("  SUSTAIN %3.0f %%（%+4.0f %%）・ノイズだけ %+5.0f dBFS: 2〜3 秒目の音量の変化 %+5.1f dB\n", sus * 100, (sus - 0.5f) * 200, db (fl), db (stats (s, (size_t) (2 * sr)).rms / stats (in, (size_t) (2 * sr)).rms));
        }
}


// ============================================================================
// ラック（2 回目）: OFF → ON / ON → OFF の抜け・プチッ、部品の入れ替え、遅れの合計と実際
static Want want1 (const char* id, std::vector<std::pair<int, float>> kv = {}, bool on = true, int slot = 0)
{
    Want w = emptyWant(); setSlot (w, slot, id, kv, on); return w;
}
static void modeRack2()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== ラック: 部品ごとの OFF → ON → OFF（0.5 秒ごと）。220 Hz −12 dBFS。1 周期ごとの山の最小（ON にした直後 40 ms、ふだんとの差 dB）とプチッの目安 ==\n");
    for (const char* id : kAll)
    {
        Want w = want1 (id, {}, false);
        if (! std::strcmp (id, "maximizer")) { w[0].k[1] = 1.0f; w[0].k[2] = 1.0f; }
        Rack r; r.prepare (sr, B, w);
        Sig in = sine (sr, 3.0, 220, 0.25f), s = in;
        for (size_t i = 0; i < (size_t) (0.05 * sr); ++i) { s.L[i] *= (float) i / (float) (0.05 * sr); s.R[i] = s.L[i]; }
        std::vector<size_t> sw;
        bool prevOn = false;
        for (size_t off = 0; off < s.size(); off += B)
        {
            const bool on = ((off / (size_t) (0.5 * sr)) % 2) == 1;
            if (on != prevOn) sw.push_back (off);
            prevOn = on; w[0].on = on;
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        }
        // ON の間の安定した山
        float onPk = 0; for (size_t i = (size_t) (0.85 * sr); i < (size_t) (0.98 * sr); ++i) onPk = std::max (onPk, std::abs (s.L[i]));
        float offPk = 0; for (size_t i = (size_t) (0.3 * sr); i < (size_t) (0.45 * sr); ++i) offPk = std::max (offPk, std::abs (s.L[i]));
        double worstDip = 0; float worstClick = 0;
        for (size_t t : sw)
        {
            for (size_t i = t; i + 218 < std::min (s.size(), t + (size_t) (0.04 * sr)); i += 48)
            {
                float pk = 0; for (size_t j = i; j < i + 218; ++j) pk = std::max (pk, std::abs (s.L[j]));
                worstDip = std::min (worstDip, db (pk / std::min (onPk, offPk)));
            }
            worstClick = std::max (worstClick, cleanliness (s.L, t, t + (size_t) (0.06 * sr), sr));
        }
        const float base = cleanliness (s.L, (size_t) (0.6 * sr), (size_t) (0.95 * sr), sr);
        std::printf ("  %-10s 遅れ %4d  OFF の山 %+5.1f / ON の山 %+5.1f dBFS、切り替え直後の谷 %+5.1f dB、プチッ %5.1f（ON のふだん %4.1f）%s\n", id, r.latency(), db (offPk), db (onPk), worstDip, worstClick, base,
                     worstDip < -3.0 || (worstClick > 10 && worstClick > 3 * base) ? "  ★" : "");
    }
    // 入れ替え: いろいろな遅れの部品を 0.4 秒ごとに選び直す（service はタイマーの代わりに 50 ms ごと）
    std::printf ("== ラック: 部品の選び直し（slot 1）MAXIMIZER → TRANSIENT → FET COMP → 空き → TUBE PUSH XL → MAXIMIZER → VCA COMP ==\n");
    {
        const char* seq[] = { "maximizer", "transient", "fetcomp", nullptr, "tubepushxl", "maximizer", "vcacomp" };
        Want w = emptyWant(); setSlot (w, 0, seq[0], { { 1, 1.0f }, { 2, 1.0f } });
        setSlot (w, 3, "tubepush");   // 後ろにもう 1 つ（遅れの合計を見る）
        Rack r; r.prepare (sr, B, w);
        Sig s = sine (sr, 3.2, 220, 0.25f);
        int idx = 0; std::vector<std::pair<double, int>> latLog; int lastLat = -1;
        for (size_t off = 0; off < s.size(); off += B)
        {
            const int want = (int) (off / (size_t) (0.4 * sr));
            if (want != idx && want < 7) { idx = want; setSlot (w, 0, seq[idx], seq[idx] && ! std::strcmp (seq[idx], "maximizer") ? std::vector<std::pair<int, float>> { { 1, 1.0f }, { 2, 1.0f } } : std::vector<std::pair<int, float>> {}); }
            if ((off / B) % 9 == 0) r.service (w);
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
            if (r.latency() != lastLat) { lastLat = r.latency(); latLog.push_back ({ (double) off / sr, lastLat }); }
        }
        std::printf ("  遅れの変化: "); for (auto& p : latLog) std::printf ("%.2f s→%d  ", p.first, p.second); std::printf ("\n");
        float worst = 0; double worstT = 0;
        for (int i = 1; i < 8; ++i)
        {
            const size_t t = (size_t) (0.4 * sr * i);
            const float c = cleanliness (s.L, t > (size_t) (0.02 * sr) ? t - (size_t) (0.02 * sr) : 0, std::min (s.size(), t + (size_t) (0.25 * sr)), sr);
            if (c > worst) { worst = c; worstT = (double) t / sr; }
        }
        std::printf ("  プチッの目安の最大 %.1f（%.2f s 付近、ふだん %.1f）\n", worst, worstT, cleanliness (s.L, (size_t) (0.1 * sr), (size_t) (0.38 * sr), sr));
    }
    // 遅れの合計と実際（8 段いろいろ、低い音で線形の範囲）
    std::printf ("== ラック: 遅れの合計の申告と実際（−40 dBFS の正弦波）==\n");
    for (double sr2 : { 44100.0, 48000.0, 96000.0 })
    {
        Want w = emptyWant();
        setSlot (w, 0, "tubepush"); setSlot (w, 1, "fetcomp"); setSlot (w, 2, "transient"); setSlot (w, 3, "maximizer", { { 1, 1.0f }, { 2, 1.0f } });
        setSlot (w, 4, "optocomp"); setSlot (w, 5, "pedalcomp", {}, false); setSlot (w, 6, "buscomp"); setSlot (w, 7, "maximizer", { { 1, 1.0f }, { 2, 1.0f } }, false);
        Rack r; r.prepare (sr2, 512, w);
        Sig in = sine (sr2, 2.0, 100, 0.01f), s = in; for (size_t off = 0; off < s.size(); off += 512) r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> (512, s.size() - off), w, 120.0);
        Sig in2 = sine (sr2, 2.0, 1000, 0.01f), s2 = in2; Rack r2; r2.prepare (sr2, 512, w); for (size_t off = 0; off < s2.size(); off += 512) r2.process (s2.L.data() + off, s2.R.data() + off, (int) std::min<size_t> (512, s2.size() - off), w, 120.0);
        const int lat = r.latency();
        std::printf ("  %.0f Hz: 8 段（2 段は OFF）申告 %d、実際 100 Hz %.2f / 1 kHz %.2f\n", sr2, lat, phaseDelayHint (in.L, s.L, 100, sr2, (size_t) (1.0 * sr2), (size_t) (1.9 * sr2), lat), phaseDelayHint (in2.L, s2.L, 1000, sr2, (size_t) (1.0 * sr2), (size_t) (1.9 * sr2), lat));
    }
    // 0.1.0 の部品（遅れなし）の OFF → ON: 古い響きを出さない（reset）か
    std::printf ("== ラック: 遅れのない部品の OFF → ON で古い響きが出ないか（音 → OFF → 無音 → ON）==\n");
    for (const char* id : { "studiovb", "spring", "twindly", "vcacomp", "chandyn" })
    {
        Want w = want1 (id);
        Rack r; r.prepare (sr, B, w);
        Sig s = pluckSeq (sr, 1.0, 0.5f); append (s, zeros (sr, 1.5));
        const size_t tOff = (size_t) (1.0 * sr) / B * B, tOn = (size_t) (1.5 * sr) / B * B;
        for (size_t off = 0; off < s.size(); off += B) { w[0].on = ! (off >= tOff && off < tOn); r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0); }
        std::printf ("  %-10s ON に戻した後の無音の最大 %+.1f dBFS\n", id, db (stats (s, tOn, s.size()).peak));
    }
}

// ============================================================================
// CHANNEL DYN のゲート（HOLD・閉じ方・OFF の切り替え）と 2 段の戻り
static void modeGate2()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== CHANNEL DYN のゲート ==\n");
    const auto& sp = specSolidDynamics();
    for (float rel : { 0.0f, 0.5f, 1.0f })
    {
        float hold = dynKnobValue (sp.knobs[10], 1, rel) - dynKnobValue (sp.knobs[10], 1, sp.knobDef[10]) + sp.base[dpGHold];
        std::printf ("  GATE REL %.1f: HOLD %.0f ms・閉じる速さ %.0f ms\n", rel, 1000 * std::pow (10.0f, std::clamp (hold, -4.0f, 1.0f)),
                     1000 * std::pow (10.0f, dynKnobValue (sp.knobs[10], 0, rel) - dynKnobValue (sp.knobs[10], 0, sp.knobDef[10]) + sp.base[dpGRel]));
    }
    // 1. HOLD: −20 dBFS 1 kHz 0.3 s → 無音（−70 dBFS のノイズ）X ms → −20 dBFS。間のノイズが通るか（ゲートが開いたまま）
    for (float rel : { 0.0f, 1.0f })
        for (double gap : { 0.05, 0.1, 0.3, 1.0 })
        {
            auto kv = defaults ("chandyn"); kv[6] = 1.0f; kv[7] = 0.75f; kv[10] = rel;
            auto m = makeSnapped ("chandyn", sr, B, kv);
            Sig s = sine (sr, 0.3, 1000, 0.1f); append (s, noise (sr, gap, 0.0003f, 3)); append (s, sine (sr, 0.3, 1000, 0.1f));
            Sig nz = noise (sr, 0.3 + gap + 0.3, 0.0003f, 3);
            runModule (*m, s, B);
            // 間の後半 20 ms の音量（ノイズの −70 dB に対して）
            const size_t a = (size_t) ((0.3 + gap - 0.02) * sr), b = (size_t) ((0.3 + gap) * sr);
            std::printf ("  GATE REL %.1f・間 %4.0f ms: 間の終わりの音量 %+6.1f dBFS（入口のノイズ −74 dBFS 前後。開いていれば ≒ 同じ＋WET の分）\n", rel, gap * 1000, db (stats (s, a, b).rms));
        }
    // 2. 減っていく音でゲートがバタバタしないか（閾値をゆっくり横切る 100 Hz、開閉の回数）
    for (float gatk : { 0.0f, 1.0f })
    {
        auto kv = defaults ("chandyn"); kv[6] = 1.0f; kv[7] = 0.75f; kv[9] = gatk;
        auto m = makeSnapped ("chandyn", sr, B, kv);
        Sig s = zeros (sr, 6.0); std::mt19937 g (2); std::normal_distribution<float> n (0, 1);
        for (size_t i = 0; i < s.size(); ++i) { const double t = (double) i / sr; const float env = (float) (0.3 * std::exp (-t / 1.0)); s.L[i] = s.R[i] = env * (float) std::sin (2 * M_PI * 100 * t) + 0.0005f * n (g); }
        Sig in = s; runModule (*m, s, B);
        // 10 ms ごとの出口 / 入口
        int flips = 0; int state = 1; double tClose = -1;
        for (size_t i = 0; i + 480 < s.size(); i += 480)
        {
            const double ratio = db (stats (s, i, i + 480).rms / std::max (1e-12, stats (in, i, i + 480).rms));
            const int st = ratio > -20 ? 1 : 0;
            if (st != state) { ++flips; state = st; if (! st && tClose < 0) tClose = (double) i / sr; }
        }
        std::printf ("  GATE ATK %s: ゆっくり減る 100 Hz で開閉の回数 %d（最初に閉じたのは %.2f s）\n", gatk > 0.5f ? "B" : "A", flips, tClose);
    }
    // 3. ゲートが閉じている間にゲートを切る（RANGE 1 → 0、EXP MODE GATE → EXP、GATE THR を下げる）。8 kHz より上の山
    struct Mv { int k; float a, b; const char* name; };
    for (float gatk : { 0.0f, 1.0f })
        for (Mv mv : { Mv { 6, 1.0f, 0.0f, "RANGE 1→0" }, Mv { 8, 0.0f, 1.0f, "EXP MODE GATE→EXP" }, Mv { 8, 1.0f, 0.0f, "EXP MODE EXP→GATE" }, Mv { 7, 0.75f, 0.0f, "GATE THR 0.75→0" }, Mv { 10, 0.0f, 1.0f, "GATE REL 0→1" } })
        {
            auto kv = defaults ("chandyn"); kv[6] = 1.0f; kv[7] = 0.75f; kv[9] = gatk; kv[(size_t) mv.k] = mv.a;
            auto m = makeSnapped ("chandyn", sr, B, kv);
            Sig s = sine (sr, 2.0, 220, 0.01f);   // −40 dBFS（しきい値 −30.7 より下 = 閉じている）
            const size_t t1 = (size_t) (1.0 * sr) / B * B;
            for (size_t off = 0; off < s.size(); off += B) { if (off == t1) m->setKnob (mv.k, mv.b); m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off)); }
            const float before = hfBurst (s.L, (size_t) (0.5 * sr), t1, sr), after = hfBurst (s.L, t1 + (size_t) (0.4 * sr), (size_t) (1.95 * sr), sr), at = hfBurst (s.L, t1, t1 + (size_t) (0.2 * sr), sr);
            const float lvB = (float) stats (s, (size_t) (0.8 * sr), t1).rms, lvA = (float) stats (s, (size_t) (1.7 * sr), (size_t) (1.95 * sr)).rms;
            float c = 0; float pkA = 0; for (size_t i = t1 + (size_t) (0.4 * sr); i < (size_t) (1.95 * sr); ++i) pkA = std::max (pkA, std::abs (s.L[i]));
            c = 0; for (size_t i = t1; i < t1 + (size_t) (0.2 * sr); ++i) c = std::max (c, std::abs (s.L[i] - 2 * s.L[i - 1] + s.L[i - 2]));
            const double w = 2 * M_PI * 220 / sr;
            std::printf ("  GATE ATK %s %-18s 音量 %+6.1f → %+6.1f dBFS、8 kHz より上の山 %.2e（後のふだん %.2e）、2 階差分 / (後の山 w²) %.1f %s\n", gatk > 0.5f ? "B" : "A", mv.name, db (lvB), db (lvA), at, after,
                         c / (pkA * w * w + 1e-12), at > 3 * std::max (before, after) + 1e-6f && c / (pkA * w * w + 1e-12) > 10 ? "★" : "");
        }
    // 4. 2 段の戻り: 切り替えのプチッ（PEDAL COMP RELEASE FAST ↔ SLOW、BUS COMP RELEASE の段）。大きい音のあとの戻りの途中で切り替え
    std::printf ("== 2 段の戻りの割合を切り替える（大きい音のあと戻っている途中）==\n");
    struct C { const char* id; int k; float a, b; const char* name; };
    for (C c : { C { "pedalcomp", 3, 0.0f, 1.0f, "PEDAL COMP RELEASE FAST→SLOW" }, C { "pedalcomp", 3, 1.0f, 0.0f, "PEDAL COMP RELEASE SLOW→FAST" },
                 C { "buscomp", 3, 0.4f, 1.0f, "BUS COMP RELEASE 0.4→1" }, C { "buscomp", 3, 1.0f, 0.0f, "BUS COMP RELEASE 1→0" },
                 C { "optocomp", 2, 0.0f, 1.0f, "OPTO COMP MODE COMP→LIMIT" } })
    {
        auto kv = defaults (c.id); kv[(size_t) c.k] = c.a;
        auto m = makeSnapped (c.id, sr, B, kv);
        Sig s = sine (sr, 1.0, 220, 0.5f); append (s, sine (sr, 2.0, 220, 0.05f));
        const size_t t1 = (size_t) (1.3 * sr) / B * B;
        for (size_t off = 0; off < s.size(); off += B) { if (off == t1) m->setKnob (c.k, c.b); m->process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off)); }
        const float a1 = (float) stats (s, t1 - (size_t) (0.02 * sr), t1).rms, a2 = (float) stats (s, t1 + (size_t) (0.1 * sr), t1 + (size_t) (0.12 * sr)).rms;
        std::printf ("  %-32s 切り替え前 %+6.1f → 100 ms 後 %+6.1f dBFS、プチッの目安 %.1f（ふだん %.1f）\n", c.name, db (a1), db (a2), cleanliness (s.L, t1, t1 + (size_t) (0.1 * sr), sr), cleanliness (s.L, (size_t) (2.3 * sr), (size_t) (2.9 * sr), sr));
    }
}

// ============================================================================
static void modeCpu2()
{
    setFZ();
    std::printf ("== CPU（ラック 8 段、1 コアに対する %%、ブロック 256、3 回のうち最速）==\n");
    struct Cfg { const char* id; std::vector<std::pair<int, float>> kv; const char* name; };
    std::vector<Cfg> cfgs {
        { "fetcomp", {}, "FET COMP 初期値" }, { "tubepushxl", { { 2, 1.0f } }, "TUBE PUSH XL SATURATION 1" },
        { "pedalcomp", {}, "PEDAL COMP" }, { "transient", { { 3, 1.0f } }, "TRANSIENT CLIP ON" }, { "transient", { { 3, 0.0f } }, "TRANSIENT CLIP OFF" },
        { "maximizer", { { 3, 0.0f } }, "MAXIMIZER CHARACTER 0" }, { "maximizer", {}, "MAXIMIZER 初期値" }, { "chandyn", { { 6, 1.0f } }, "CHANNEL DYN RANGE 1" },
        { "maximizer", {}, "MAXIMIZER 全部 OFF（回し続け）" },
    };
    for (double sr : { 48000.0, 96000.0 })
        for (auto& c : cfgs)
        {
            Want w = emptyWant();
            const bool off = std::string (c.name).find ("OFF（") != std::string::npos;
            for (int i = 0; i < 8; ++i) setSlot (w, i, c.id, c.kv, ! off);
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                Rack r; r.prepare (sr, 256, w);
                Sig s = noise (sr, 2.0, 0.05f);
                const double t0 = nowSec();
                for (size_t o = 0; o < s.size(); o += 256) r.process (s.L.data() + o, s.R.data() + o, (int) std::min<size_t> (256, s.size() - o), w, 120.0);
                best = std::min (best, nowSec() - t0);
            }
            std::printf ("  %6.0f Hz  8 段すべて %-32s %5.1f %%\n", sr, c.name, best / 2.0 * 100);
        }
}



// ============================================================================
// 無音のあと、いきなり始まる打音（キックのサンプルのように 1 サンプル目から大きい）でコンプの頭がどれだけ跳ねるか。
// 本物（Guitar Rig）も跳ねる（2 回目の検査: Supercharger COMPRESS 1 で −20 dBFS の正弦波の頭が +2 dBFS、本体 +4）。ここでは本体の数字だけ
static void modeOnset()
{
    const double sr = 48000; const int B = 256;
    auto hit = [] (double sr, float amp)
    {
        Sig s = zeros (sr, 0.5), h = zeros (sr, 0.5);
        std::mt19937 g (3); std::normal_distribution<float> n (0, 1);
        for (size_t i = 0; i < h.size(); ++i) { const double t = (double) i / sr; const float e = (float) std::exp (-t * 20); h.L[i] = h.R[i] = amp * e * (float) (0.8 * std::cos (2 * M_PI * 60 * t) + 0.2 * std::exp (-t * 300) * n (g)); }
        append (s, h); return s;
    };
    struct C { const char* id; std::vector<std::pair<int, float>> kv; const char* name; };
    std::vector<C> cs {
        { "tubepush", {}, "TUBE PUSH 初期値" }, { "tubepush", { { 1, 1.0f } }, "TUBE PUSH COMPRESS 1" }, { "tubepush", { { 0, 0.0f }, { 1, 1.0f } }, "TUBE PUSH INPUT 0・COMPRESS 1" },
        { "tubepushxl", { { 4, 1.0f } }, "TUBE PUSH XL COMPRESS 1" }, { "tubepushxl", { { 4, 1.0f }, { 5, 1.0f } }, "TUBE PUSH XL COMPRESS 1・ATTACK 1" },
        { "fetcomp", {}, "FET COMP 初期値" }, { "fetcomp", { { 2, 0.0f }, { 4, 0.6f } }, "FET COMP ATTACK 0・OUTPUT 0.6" },
        { "optocomp", {}, "OPTO COMP 初期値" }, { "vcacomp", {}, "VCA COMP 初期値" }, { "buscomp", {}, "BUS COMP 初期値" }, { "chandyn", {}, "CHANNEL DYN 初期値" },
        { "quickcomp", {}, "QUICK COMP 初期値" }, { "pedalcomp", {}, "PEDAL COMP 初期値" }, { "transient", { { 0, 1.0f }, { 3, 0.0f } }, "TRANSIENT ATTACK 100 %・CLIP OFF" },
    };
    std::printf ("無音 0.5 秒 → いきなり始まる打音: 出口の山（始まって 3 ms）/ 30〜200 ms の山（dBFS）\n");
    for (auto& c : cs)
    {
        std::printf ("  %-36s", c.name);
        for (float amp : { 0.25f, 1.0f })
        {
            auto kv = defaults (c.id); for (auto& p : c.kv) kv[(size_t) p.first] = p.second;
            auto m = makeSnapped (c.id, sr, B, kv);
            Sig s = hit (sr, amp), in = s; runModule (*m, s, B);
            const size_t on = (size_t) (0.5 * sr), lat = (size_t) m->latency();
            std::printf ("  入口 %+5.1f: 頭 %+6.1f / 後 %+6.1f", db (stats (in).peak), db (stats (s, on, on + lat + (size_t) (0.003 * sr)).peak), db (stats (s, on + (size_t) (0.03 * sr), on + (size_t) (0.2 * sr)).peak));
        }
        std::printf ("\n");
    }
}


// ============================================================================
// 3 回目: MAXIMIZER の低音。窓の最大の列が溢れると 34 Hz より下で歪みが段で増える（C1 と C#1 の差）
static void modeMaxLow()
{
    const double sr = 48000;
    std::printf ("== MAXIMIZER の低音: 0 dBFS の正弦波・CHARACTER 3.55・CEILING −1。倍音（2〜15 次、基音に対する dB）と、写し（直した形）の本体の段の山÷天井 ==\n");
    std::printf ("   直した本体なら 30.9 / 32.7 Hz も 34.6 Hz と同じくらい（−29〜−32 dB）。今の本体（列 + 2）は THRESHOLD −6 で 32.7 Hz が約 −20 dB に跳ねる\n");
    for (double f : { 27.5, 30.9, 32.7, 34.6, 36.7, 41.2 })
    {
        std::printf ("  %5.1f Hz:", f);
        for (float thrDb : { -3.0f, -6.0f, -12.0f })
        {
            const std::vector<float> kv { 0.5f, (thrDb + 20.0f) / 20.0f, 0.95f, 0.355f };
            auto m = makeSnapped ("maximizer", sr, 256, kv);
            MaxCopy mc; mc.prepare (sr, kv);
            Sig s = sine (sr, 2.0, f, 1.0f), s2 = s; runModule (*m, s, 256); runAny (mc, s2, 256);
            const size_t N = (size_t) sr, off = (size_t) (0.8 * sr);
            const double fund = dftMag (s.L, off, N, f, sr); double e = 0;
            for (int n = 2; n <= 15; ++n) { const double v = dftMag (s.L, off, N, n * f, sr) / fund; e += v * v; }
            float d = 0; for (size_t i = 0; i < s.size(); ++i) d = std::max (d, std::abs (s.L[i] - s2.L[i]));
            std::printf ("  TH %+3.0f: %+6.1f dB（写しの段 %+4.1f dB）%s", thrDb, db (std::sqrt (e)), db (mc.limStage), d > 0 ? "★本体は列が短い" : "");
        }
        std::printf ("\n");
    }
}

// 3 回目: 再生中に部品を選び直して、遅れのある部品（MAXIMIZER）が入る瞬間のプチッ（220 Hz −12 dBFS）
static void modeSwapIn()
{
    const double sr = 48000; const int B = 256;
    std::printf ("== 再生中の選び直し: プチッの目安（ふだん 1.0、10 以上はプチッ）と隣のサンプルとの差の最大（ふだん 0.0072）==\n");
    struct Case { const char* from; const char* to; const char* name; };
    for (Case c : { Case { nullptr, "maximizer", "空き → MAXIMIZER" }, Case { "tubepushxl", "maximizer", "TUBE PUSH XL → MAXIMIZER" },
                    Case { "maximizer", "fetcomp", "MAXIMIZER → FET COMP" }, Case { nullptr, "tubepushxl", "空き → TUBE PUSH XL" },
                    Case { nullptr, "limit", "空き → LIMITER" }, Case { nullptr, "studiovb", "空き → ROOM VERB" } })
    {
        Want w = emptyWant(); setSlot (w, 0, c.from);
        Rack r; r.prepare (sr, B, w);
        Sig s = sine (sr, 2.0, 220, 0.25f);
        const size_t tSw = (size_t) (1.0 * sr) / B * B;
        for (size_t off = 0; off < s.size(); off += B)
        {
            if (off == tSw) setSlot (w, 0, c.to);
            if ((off / B) % 4 == 0) r.service (w);
            r.process (s.L.data() + off, s.R.data() + off, (int) std::min<size_t> ((size_t) B, s.size() - off), w, 120.0);
        }
        float jump = 0; size_t at = tSw;
        for (size_t i = tSw; i < tSw + (size_t) (0.3 * sr); ++i) { const float d = std::abs (s.L[i] - s.L[i - 1]); if (d > jump) { jump = d; at = i; } }
        const float cl = cleanliness (s.L, tSw - 2000, tSw + (size_t) (0.3 * sr), sr);
        std::printf ("  %-26s プチッ %6.1f  差の最大 %.4f（%.1f ms 後）%s\n", c.name, cl, jump, (double) (at - tSw) * 1000.0 / sr, cl > 10 ? "  ★" : "");
    }
}


int main (int argc, char** argv)
{
    const std::string m = argc > 1 ? argv[1] : "info";
    if (m == "info") modeInfo();
    else if (m == "fuzz") modeFuzz (argc > 2 ? std::atoi (argv[2]) : 60);
    else if (m == "fuzzall") modeFuzzAll (argc > 2 ? std::atoi (argv[2]) : 20);
    else if (m == "click") modeClick();
    else if (m == "click1") { const char* only = argv[2]; const auto& inf = *infoFor (typeForId (only)); for (int k = 0; k < (int) inf.knobs.size(); ++k) { const auto& ki = inf.knobs[(size_t) k]; std::vector<std::pair<float, float>> mv; if (ki.steps > 1) { for (int i = 0; i < ki.steps; ++i) for (int j = 0; j < ki.steps; ++j) if (i != j) mv.push_back ({ (float) i / (ki.steps - 1), (float) j / (ki.steps - 1) }); } else mv = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.2f, 0.8f }, { 0.8f, 0.2f } }; float J = 0, D = 0, Bs = 0; for (auto x : mv) { float j, d, b; clickOne (only, k, x.first, x.second, j, d, b); J = std::max (J, j); D = std::max (D, d); Bs = std::max (Bs, b); } std::printf ("  %-10s %-11s %7.1f / %6.1f [%4.1f]\n", only, ki.name, J, D, Bs); } }
    else if (m == "os") { modeOS(); }
    else if (m == "alias") modeAlias();
    else if (m == "mix") modeMix();
    else if (m == "max") modeMax();
    else if (m == "lat") modeLat();
    else if (m == "rack") modeRack();
    else if (m == "rack2") modeRack2();
    else if (m == "gate2") modeGate2();
    else if (m == "onset") modeOnset();
    else if (m == "block") modeBlock();
    else if (m == "reset") modeReset();
    else if (m == "rates") modeRates();
    else if (m == "cpu") modeCpu();
    else if (m == "cpu2") modeCpu2();
    else if (m == "pedal") modePedal();
    else if (m == "trans") modeTrans();
    else if (m == "trans2") modeTrans2();
    else if (m == "engine") modeEngine();
    else if (m == "lookmix") modeLookMix();
    else if (m == "esteps") modeEngineSteps();
    else if (m == "targets") modeTargets();
    else if (m == "rackreset") modeRackReset();
    else if (m == "resetleak") modeResetLeak();
    else if (m == "maxlow") modeMaxLow();
    else if (m == "swapin") modeSwapIn();
    else { std::printf ("知らないモード: %s（先頭のコメントを見てください）\n", m.c_str()); return 1; }
    return 0;
}
