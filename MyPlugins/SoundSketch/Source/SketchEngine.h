#pragma once
// 口マネで音を探すための「音の特徴」と「似ている度」（JUCE なし。単体テストできる）
//
// 1 つの音から 48 個の数字（特徴）を作る:
//   - 音量の形（鳴り始めから約 1 秒、40 ms ごと）
//   - 音の明るさ（スペクトルの重心）の動き
//   - ノイズっぽさ（スペクトルの平らさ）
//   - 帯域のバランス、長さ、立ち上がりの速さ、明るさが上がるか下がるか
// 口マネの声は本物の音より明るさの幅がせまい（キックほど低くもハットほど高くも出せない）ので、
// 探すときは声の明るさを広げてから比べる（voiceToLibrary）。
#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace sketch
{
constexpr double kFs = 22050.0;      // 解析はこのサンプリング周波数で
constexpr int kN = 1024, kHop = 256; // FFT の長さと間隔（約 11.6 ms）
constexpr int kEnv = 24, kCen = 8, kFlat = 4, kBands = 8;
constexpr int kDims = kEnv + kCen + kFlat + kBands + 4;
constexpr double kMaxRead = 6.0;     // 解析に使うのは頭から 6 秒まで

struct Features
{
    float v[kDims] {};
    float* env()   { return v; }
    float* cen()   { return v + kEnv; }
    float* flat()  { return v + kEnv + kCen; }
    float* bands() { return v + kEnv + kCen + kFlat; }
    const float* env() const   { return v; }
    const float* cen() const   { return v + kEnv; }
    const float* flat() const  { return v + kEnv + kCen; }
    const float* bands() const { return v + kEnv + kCen + kFlat; }
    // 後ろの 4 つ
    float& logLen()  { return v[kDims - 4]; }   // log2(鳴っている長さ 秒)
    float& attack()  { return v[kDims - 3]; }   // log2(いちばん大きくなるまで 秒)
    float& cenMean() { return v[kDims - 2]; }   // 最初の 300 ms の明るさ（log2 Hz）
    float& cenSlope(){ return v[kDims - 1]; }   // 明るさの上がり下がり（オクターブ）
    float logLen() const  { return v[kDims - 4]; }
    float attack() const  { return v[kDims - 3]; }
    float cenMean() const { return v[kDims - 2]; }
    float cenSlope() const{ return v[kDims - 1]; }
    bool valid = false;
    float seconds = 0;   // 鳴っている長さ（秒）
};

//==============================================================================
namespace detail
{
inline void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * 3.14159265358979f / (float) len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], t = a[i + k + len / 2] * w;
                a[i + k] = u + t;
                a[i + k + len / 2] = u - t;
                w *= wl;
            }
        }
    }
}

// 22050 Hz に直す（区間の平均を取ってから線形補間。特徴を取るにはこれで十分）
inline std::vector<float> resample (const float* x, int n, double fs)
{
    if (n <= 0) return {};
    const double r = fs / kFs;
    const int m = (int) std::floor ((n - 1) / r) + 1;
    std::vector<float> out ((size_t) std::max (1, m));
    if (r <= 1.0001)
    {
        for (int i = 0; i < m; ++i)
        {
            const double p = i * r;
            const int a = (int) p; const double f = p - a;
            const float b = a + 1 < n ? x[a + 1] : x[a];
            out[(size_t) i] = (float) (x[a] * (1.0 - f) + b * f);
        }
        return out;
    }
    // 先に積分しておいて、[p - r/2, p + r/2] の平均を取る
    std::vector<double> cs ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + x[i];
    for (int i = 0; i < m; ++i)
    {
        const double c = i * r;
        const int a = std::max (0, (int) std::floor (c - r * 0.5)), b = std::min (n, (int) std::ceil (c + r * 0.5));
        out[(size_t) i] = b > a ? (float) ((cs[(size_t) b] - cs[(size_t) a]) / (b - a)) : x[std::min (n - 1, (int) c)];
    }
    return out;
}

inline float log2f_ (double x) { return (float) std::log2 (std::max (1e-9, x)); }
} // namespace detail

//==============================================================================
// x: モノラル（何チャンネルかあるときは平均したもの）、fs: その周波数
inline Features analyse (const float* x, int n, double fs)
{
    Features F;
    auto s = detail::resample (x, std::min (n, (int) (fs * kMaxRead)), fs);
    const int frames = (int) s.size() >= kN ? 1 + ((int) s.size() - kN) / kHop : 0;
    if (frames < 3) { s.resize (kN * 3, 0.0f); }
    const int nf = std::max (frames, 3);

    // 1. フレームごとの音量
    std::vector<float> db ((size_t) nf);
    float peak = -200;
    int peakF = 0;
    for (int f = 0; f < nf; ++f)
    {
        double e = 0;
        const int o = f * kHop;
        for (int i = 0; i < kN && o + i < (int) s.size(); ++i) e += (double) s[(size_t) (o + i)] * s[(size_t) (o + i)];
        db[(size_t) f] = (float) (10.0 * std::log10 (e / kN + 1e-12));
        if (db[(size_t) f] > peak) { peak = db[(size_t) f]; peakF = f; }
    }
    if (! std::isfinite (peak) || peak < -75) return F;   // ほとんど無音（か、壊れたデータ）

    // 2. 鳴り始め（いちばん大きい所から 30 dB 下を最初に超えた所の 1 つ前）と、鳴り終わり
    int on = 0;
    while (on < peakF && db[(size_t) on] < peak - 30) ++on;
    on = std::max (0, on - 1);
    int last = peakF;
    for (int f = peakF; f < nf; ++f) if (db[(size_t) f] > peak - 30) last = f;
    const double frameSec = kHop / kFs;
    F.seconds = (float) ((last - on + 1) * frameSec);
    F.logLen() = detail::log2f_ (F.seconds + 0.02);
    F.attack() = detail::log2f_ ((peakF - on) * frameSec + 0.005);

    // 3. 鳴り始めから約 1.2 秒ぶんのスペクトル
    const int K = std::min (nf - on, (int) std::ceil (1.2 / frameSec));
    std::vector<float> win (kN);
    for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * i / (kN - 1));
    const double binHz = kFs / kN;
    static const double edges[kBands + 1] { 40, 80, 160, 320, 640, 1280, 2560, 5120, 11000 };
    std::vector<float> cen ((size_t) K), flat ((size_t) K), pw ((size_t) K), rel ((size_t) K);
    std::vector<std::array<float, kBands>> bands ((size_t) K);
    std::vector<std::complex<float>> buf (kN);
    for (int k = 0; k < K; ++k)
    {
        const int o = (on + k) * kHop;
        for (int i = 0; i < kN; ++i)
        {
            const int p = o + i;
            buf[(size_t) i] = { p < (int) s.size() ? s[(size_t) p] * win[(size_t) i] : 0.0f, 0.0f };
        }
        detail::fft (buf);
        double sum = 0, wsum = 0, logSum = 0; int cnt = 0;
        double be[kBands] {};
        for (int b = 1; b < kN / 2; ++b)
        {
            const double hz = b * binHz;
            const double p = (double) std::norm (buf[(size_t) b]) + 1e-12;
            sum += p; wsum += p * hz;
            if (hz >= 100 && hz <= 10000) { logSum += std::log (p); ++cnt; }
            for (int j = 0; j < kBands; ++j) if (hz >= edges[j] && hz < edges[j + 1]) { be[j] += p; break; }
        }
        double am = 0; for (int b = 1; b < kN / 2; ++b) { const double hz = b * binHz; if (hz >= 100 && hz <= 10000) am += (double) std::norm (buf[(size_t) b]) + 1e-12; }
        am /= std::max (1, cnt);
        cen[(size_t) k] = detail::log2f_ (wsum / sum);
        flat[(size_t) k] = (float) std::clamp (std::exp (logSum / std::max (1, cnt)) / am, 0.0, 1.0);
        pw[(size_t) k] = (float) sum;
        rel[(size_t) k] = db[(size_t) std::min (nf - 1, on + k)] - peak;
        for (int j = 0; j < kBands; ++j) bands[(size_t) k][(size_t) j] = (float) (10.0 * std::log10 (be[j] + 1e-12));
    }

    // 範囲 [t0, t1) 秒の、音の大きさで重みを付けた平均（静かなフレームは数えない）
    auto avg = [&] (const std::vector<float>& v, double t0, double t1, float fallback)
    {
        double a = 0, w = 0;
        for (int k = (int) (t0 / frameSec); k < std::min (K, (int) std::ceil (t1 / frameSec)); ++k)
        {
            if (k < 0 || rel[(size_t) k] < -40) continue;
            const double ww = std::pow (10.0, rel[(size_t) k] / 10.0);
            a += v[(size_t) k] * ww; w += ww;
        }
        return w > 0 ? (float) (a / w) : fallback;
    };

    // 音量の形（40 ms ごと、ピークを 0、-50 dB を -1）
    for (int i = 0; i < kEnv; ++i)
    {
        double m = -200;
        for (int k = (int) (i * 0.04 / frameSec); k < std::min (K, (int) ((i + 1) * 0.04 / frameSec) + 1); ++k)
            m = std::max (m, (double) rel[(size_t) k]);
        F.env()[i] = (float) (std::clamp (m, -50.0, 0.0) / 50.0);
    }
    F.cenMean() = avg (cen, 0.0, 0.3, 10.0f);
    for (int i = 0; i < kCen; ++i)
        F.cen()[i] = avg (cen, i * 0.08, (i + 1) * 0.08, i > 0 ? F.cen()[i - 1] : F.cenMean());
    F.cenSlope() = avg (cen, 0.2, 0.45, F.cenMean()) - avg (cen, 0.0, 0.1, F.cenMean());
    static const double fs0[kFlat] { 0.0, 0.05, 0.15, 0.35 }, fs1[kFlat] { 0.05, 0.15, 0.35, 0.7 };
    for (int i = 0; i < kFlat; ++i) F.flat()[i] = avg (flat, fs0[i], fs1[i], i > 0 ? F.flat()[i - 1] : 0.0f);
    double bm = 0;
    for (int j = 0; j < kBands; ++j)
    {
        std::vector<float> col ((size_t) K);
        for (int k = 0; k < K; ++k) col[(size_t) k] = bands[(size_t) k][(size_t) j];
        F.bands()[j] = avg (col, 0.0, 0.3, -120.0f);
        bm += F.bands()[j];
    }
    bm /= kBands;
    for (int j = 0; j < kBands; ++j) F.bands()[j] = (float) std::clamp ((F.bands()[j] - bm) / 20.0, -3.0, 3.0);
    for (float x : F.v) if (! std::isfinite (x)) return Features {};
    F.valid = true;
    return F;
}

//==============================================================================
// 声の明るさを、本物の音の幅に広げる（log2 Hz で 1 次式）
//  声: 約 300 Hz〜7 kHz  →  音: 約 100 Hz〜10 kHz
inline Features voiceToLibrary (Features q)
{
    constexpr float a = 1.46f, b = -5.37f;
    for (int i = 0; i < kCen; ++i) q.cen()[i] = a * q.cen()[i] + b;
    q.cenMean() = a * q.cenMean() + b;
    q.cenSlope() *= a;
    return q;
}

struct Weights
{
    float env = 3.0f, cenShape = 1.0f, cenMean = 0.7f, flat = 4.0f, bands = 0.35f, len = 0.6f, attack = 0.25f, slope = 0.5f;
};

// 小さいほど似ている（0 = 同じ）
inline float distance (const Features& q, const Features& s, const Weights& w = {})
{
    if (! q.valid || ! s.valid) return 1e9f;
    auto msq = [] (const float* a, const float* b, int n) { float d = 0; for (int i = 0; i < n; ++i) d += (a[i] - b[i]) * (a[i] - b[i]); return d / n; };
    float d = w.env * msq (q.env(), s.env(), kEnv);
    float cs = 0;
    for (int i = 0; i < kCen; ++i)
    {
        const float x = (q.cen()[i] - q.cenMean()) - (s.cen()[i] - s.cenMean());
        cs += x * x;
    }
    d += w.cenShape * cs / kCen;
    const float cm = q.cenMean() - s.cenMean();
    d += w.cenMean * cm * cm;
    d += w.flat * msq (q.flat(), s.flat(), kFlat);
    d += w.bands * msq (q.bands(), s.bands(), kBands);
    d += w.len * (q.logLen() - s.logLen()) * (q.logLen() - s.logLen());
    d += w.attack * (q.attack() - s.attack()) * (q.attack() - s.attack());
    d += w.slope * (q.cenSlope() - s.cenSlope()) * (q.cenSlope() - s.cenSlope());
    return d;
}

// 画面に出す「似ている度」（0〜100）
inline int matchPercent (float d) { return (int) std::lround (100.0 * std::exp (-0.5 * d)); }

//==============================================================================
// ファイルの場所と名前から、音の種類を当てる
enum Kind { kAll = 0, kKick, kSnare, kHat, kClap, kPerc, kBass, kFx, kVox, kLoop, kNumKinds };

inline const char* kindLabel (int k)
{
    static const char* n[] { "すべて", "キック", "スネア", "ハット", "クラップ", "パーカス", "ベース", "FX", "声", "ループ" };
    return k >= 0 && k < kNumKinds ? n[k] : "";
}

inline bool hasWord (const std::string& s, const char* w)
{
    // 英数字の切れ目で始まる所だけ（"hat" は "what" に当たらない。"hihat" は別に書く）
    const std::string word (w);
    for (size_t p = s.find (word); p != std::string::npos; p = s.find (word, p + 1))
    {
        const bool startOk = p == 0 || ! std::isalpha ((unsigned char) s[p - 1]);
        if (startOk) return true;
    }
    return false;
}

// path は小文字にしたもの。kinds のビットを返す（1 << Kind）
inline uint32_t guessKinds (const std::string& lowerPath, float seconds)
{
    const auto slash = lowerPath.find_last_of ('/');
    const std::string name = slash == std::string::npos ? lowerPath : lowerPath.substr (slash + 1);
    auto any = [&] (std::initializer_list<const char*> ws, const std::string& in)
    { for (auto* w : ws) if (hasWord (in, w)) return true; return false; };
    uint32_t k = 0;
    // 名前で決まればそれを優先、なければフォルダの名前も見る
    auto pick = [&] (const std::string& in)
    {
        uint32_t r = 0;
        if (any ({ "kick", "kik", "kck", "bd_", "bassdrum" }, in)) r |= 1u << kKick;
        if (any ({ "snare", "snr", "rim", "sd_" }, in)) r |= 1u << kSnare;
        if (any ({ "hat", "hihat", "hi-hat", "hh_", "hh ", "cymbal", "ride", "crash", "open_hh", "closed_hh" }, in)) r |= 1u << kHat;
        if (any ({ "clap", "snap" }, in)) r |= 1u << kClap;
        if (any ({ "perc", "tom", "shaker", "conga", "bongo", "tamb", "cowbell", "clave", "block" }, in)) r |= 1u << kPerc;
        if (any ({ "bass", "808", "sub", "reese" }, in) && ! hasWord (in, "bassdrum")) r |= 1u << kBass;
        if (any ({ "fx", "sfx", "riser", "impact", "sweep", "whoosh", "foley", "noise", "glitch", "downlifter", "uplifter" }, in)) r |= 1u << kFx;
        if (any ({ "vox", "vocal", "voice", "adlib", "chant" }, in)) r |= 1u << kVox;
        return r;
    };
    k = pick (name);
    if (k == 0) k = pick (lowerPath);
    if (hasWord (lowerPath, "loop") || seconds > 4.0f) k |= 1u << kLoop;
    return k;
}
} // namespace sketch
