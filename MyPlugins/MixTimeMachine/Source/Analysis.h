#pragma once
// バウンスの解析（JUCE 非依存なので、単体でテストできる）
//  - ファイル名から「どの曲か」を決める
//  - 曲全体の音量・ピーク・ダイナミクス・広がり・音色（1/3 オクターブの平均スペクトル）
//  - 2つのバウンスの頭のずれ（書き出し範囲が違っても同じ位置を比べられるように）
#include "LoudnessMeter.h"
#include <complex>
#include <regex>
#include <string>
#include <vector>

namespace mtm
{

// ---------------------------------------------------------------------------
// 曲の名前

inline std::string normaliseName (std::string s)
{
    // 全角スペース → 半角、連続スペースを1つに、前後の空白・区切りを取る
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (i + 2 < s.size() && (unsigned char) s[i] == 0xE3 && (unsigned char) s[i + 1] == 0x80 && (unsigned char) s[i + 2] == 0x80)
        {
            out += ' '; i += 2; continue;
        }
        out += s[i];
    }
    out = std::regex_replace (out, std::regex ("\\s+"), " ");
    out = std::regex_replace (out, std::regex ("^[\\s_\\-]+|[\\s_\\-]+$"), "");
    return out;
}

// 版を表す飾りを後ろから取る（"_3"・"demo 7"・"dem9"・"mix2"・"v3"・"master" など）。
// "DNA 2" の "2" のような、曲名の一部かもしれない数字だけのものは取らない
inline std::string stripVersionSuffix (std::string s)
{
    static const std::regex logicCounter ("_\\d+$");
    static const std::regex versionWord ("[\\s_\\-]*\\(?\\b(demo|dem|mix|mixdown|master|mastered|ver|version|v|take|rev|bounce|final|test|draft)\\s*\\.?\\d*\\)?$",
                                         std::regex::icase);
    static const std::regex copyWord ("[\\s_\\-]*(copy|コピー)\\s*\\d*$|\\s*\\(\\d+\\)$", std::regex::icase);
    for (int guard = 0; guard < 6; ++guard)
    {
        std::string t = std::regex_replace (s, logicCounter, "");
        t = std::regex_replace (t, versionWord, "");
        t = std::regex_replace (t, copyWord, "");
        t = normaliseName (t);
        if (t == s || t.empty()) break;
        s = t;
    }
    return s;
}

inline bool isDigitAt (const std::string& s, size_t i) { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }

// ファイル名（拡張子なし）→ 曲の名前。
// projects に Logic のプロジェクト名があれば、ファイル名の頭がいちばん長く一致するプロジェクトにまとめる
// （Logic のバウンスは、最初はプロジェクト名と同じ名前になるため）。
// それ以外は版の飾りを取った名前。さらに「既にある曲名 + 数字」は、その曲にまとめる
inline std::string songKey (const std::string& fileStem, const std::vector<std::string>& projects,
                            const std::vector<std::string>& knownSongs = {})
{
    const std::string name = normaliseName (fileStem);
    std::string best;
    for (auto p : projects)
    {
        p = normaliseName (p);
        if (p.empty() || p.size() <= best.size() || name.compare (0, p.size(), p) != 0) continue;
        if (isDigitAt (name, p.size()) && isDigitAt (p, p.size() - 1)) continue;   // "DNA 2" と "DNA 20" は別
        best = p;
    }
    if (! best.empty()) return best;

    std::string key = stripVersionSuffix (name);
    static const std::regex trailingNumber ("^(.*\\S)\\s+\\d+$");
    std::smatch m;
    if (std::regex_match (key, m, trailingNumber))
        for (auto& k : knownSongs)
            if (k == m[1].str()) return k;
    return key.empty() ? name : key;
}

// ---------------------------------------------------------------------------
// FFT（2 のべき乗の長さ）

inline void fft (std::vector<std::complex<double>>& a)
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
        const double ang = -2.0 * refdsp::kPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 曲全体の解析

constexpr int kNumBands = 31;   // 20Hz〜20kHz の 1/3 オクターブ

inline float bandCentre (int b) { return 20.0f * std::pow (2.0f, (float) b / 3.0f); }

struct Analysis
{
    float lufs = -200.0f;          // 曲全体（ゲート付き）
    float peakDb = -120.0f;        // サンプルピーク
    float plr = 0.0f;              // ピーク − 音量（小さいほど潰れている）
    float loudRange = 0.0f;        // 3秒値の大きい所と小さい所の差（LRA に近い）
    float sideRatio = 0.0f;        // 0〜1、サイドの割合（広がり）
    float lengthSec = 0.0f;
    std::array<float, kNumBands> bands {};   // 平均パワー（dB、曲の音量と無関係な絶対値）
    bool ok = false;
};

inline Analysis analyse (const float* L, const float* R, int n, double fs)
{
    Analysis a;
    if (n < 16 || fs <= 0) return a;
    a.lengthSec = (float) (n / fs);

    refdsp::LoudnessMeter m;
    m.prepare (fs);
    double sLL = 0, sRR = 0, sLR = 0;
    std::vector<float> shortTerms;
    const int blk = (int) (fs * 0.1);
    for (int pos = 0; pos < n; pos += blk)
    {
        const int k = std::min (blk, n - pos);
        m.process (L + pos, R + pos, k);
        for (int i = pos; i < pos + k; ++i) { sLL += (double) L[i] * L[i]; sRR += (double) R[i] * R[i]; sLR += (double) L[i] * R[i]; }
        if (pos >= (int) (fs * 3.0) && m.shortTerm.load() > -70.0f) shortTerms.push_back (m.shortTerm.load());
    }
    a.lufs = m.gatedSeconds.load() > 0.0f ? m.integrated.load() : -200.0f;
    a.peakDb = m.peakMax.load();
    a.plr = a.lufs > -100.0f ? a.peakDb - a.lufs : 0.0f;
    if (sLL + sRR > 1.0e-9)
    {
        const double mid = (sLL + sRR + 2.0 * sLR) * 0.25, side = (sLL + sRR - 2.0 * sLR) * 0.25;
        a.sideRatio = (float) (side / (mid + side + 1.0e-20));
    }
    if (shortTerms.size() > 10 && a.lufs > -100.0f)
    {
        // EBU の LRA と同じ考え方: 絶対 -70・相対 -20 でゲートして、10%〜95% の幅
        std::vector<float> g;
        for (float v : shortTerms) if (v > a.lufs - 20.0f) g.push_back (v);
        if (g.size() > 5)
        {
            std::sort (g.begin(), g.end());
            a.loudRange = g[(size_t) (g.size() * 0.95) - 1] - g[(size_t) (g.size() * 0.10)];
        }
    }

    // 平均スペクトル（ハン窓 8192 点、半分ずつ重ねる。曲が長いときは間引く）
    constexpr int N = 8192;
    std::vector<double> win (N), power (N / 2 + 1, 0.0);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * refdsp::kPi * i / N);
    const int frames = std::max (1, (n - N) / (N / 2));
    const int stride = std::max (1, frames / 400);
    std::vector<std::complex<double>> buf (N);
    int used = 0;
    for (int f = 0; f < frames; f += stride)
    {
        const int start = f * (N / 2);
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            const double x = idx < n ? 0.5 * ((double) L[idx] + R[idx]) : 0.0;
            buf[(size_t) i] = { x * win[(size_t) i], 0.0 };
        }
        fft (buf);
        for (int k = 0; k <= N / 2; ++k) power[(size_t) k] += std::norm (buf[(size_t) k]);
        ++used;
    }
    for (int b = 0; b < kNumBands; ++b)
    {
        const double lo = bandCentre (b) / std::pow (2.0, 1.0 / 6.0), hi = bandCentre (b) * std::pow (2.0, 1.0 / 6.0);
        double e = 0.0; int cnt = 0;
        for (int k = std::max (1, (int) std::floor (lo * N / fs)); k <= std::min (N / 2, (int) std::ceil (hi * N / fs)); ++k)
        {
            const double fk = k * fs / N;
            if (fk < lo || fk >= hi) continue;
            e += power[(size_t) k]; ++cnt;
        }
        if (cnt == 0)   // 低い帯域で bin が無いときは、いちばん近い bin
        {
            const int k = std::max (1, (int) std::lround (bandCentre (b) * N / fs));
            e = power[(size_t) std::min (k, N / 2)]; cnt = 1;
        }
        // 帯域の幅に比例する合計パワー（ピンクノイズで平ら）
        const double total = e / std::max (1, used);
        a.bands[(size_t) b] = (float) (10.0 * std::log10 (total + 1.0e-20));
    }
    a.ok = true;
    return a;
}

// 2つの音色の差（B − A）。音量の差は除いて「音色だけ」にする（中域 200Hz〜5kHz の平均差を 0 に合わせる）
inline std::array<float, kNumBands> toneDiff (const Analysis& a, const Analysis& b)
{
    std::array<float, kNumBands> d {};
    double mean = 0; int cnt = 0;
    for (int i = 0; i < kNumBands; ++i)
    {
        d[(size_t) i] = b.bands[(size_t) i] - a.bands[(size_t) i];
        if (bandCentre (i) >= 200.0f && bandCentre (i) <= 5000.0f) { mean += d[(size_t) i]; ++cnt; }
    }
    mean /= std::max (1, cnt);
    for (auto& v : d) v -= (float) mean;
    return d;
}

// ---------------------------------------------------------------------------
// 頭のずれを探す。B の位置 = A の位置 + 戻り値（秒）。見つからないときは confidence が低い

struct Offset { double seconds = 0.0; double confidence = 0.0; };

inline std::vector<float> onsetEnvelope (const float* L, const float* R, int n, double fs, double rate, double maxSec)
{
    const int hop = std::max (1, (int) std::lround (fs / rate));
    const int count = std::min (n, (int) (maxSec * fs)) / hop;
    std::vector<float> env ((size_t) std::max (0, count));
    float prev = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        double e = 0.0;
        for (int j = i * hop; j < (i + 1) * hop; ++j) e += std::abs (L[j]) + std::abs (R[j]);
        const float lv = (float) std::log10 (e / hop + 1.0e-5);
        env[(size_t) i] = std::max (0.0f, lv - prev);   // 立ち上がりだけ
        prev = lv;
    }
    // 平均を引いて大きさをそろえる
    double mean = 0; for (float v : env) mean += v;
    mean /= std::max<size_t> (1, env.size());
    double ss = 0; for (auto& v : env) { v -= (float) mean; ss += (double) v * v; }
    const float norm = (float) (1.0 / std::sqrt (ss + 1.0e-20));
    for (auto& v : env) v *= norm;
    return env;
}

inline double xcorrAt (const std::vector<float>& a, const std::vector<float>& b, int lag)
{
    // b[i + lag] と a[i] を比べる
    double s = 0.0;
    const int n = (int) a.size(), m = (int) b.size();
    for (int i = std::max (0, -lag); i < n && i + lag < m; ++i) s += (double) a[(size_t) i] * b[(size_t) (i + lag)];
    return s;
}

inline Offset findOffset (const float* aL, const float* aR, int an, double afs,
                          const float* bL, const float* bR, int bn, double bfs,
                          double maxShiftSec = 20.0, double windowSec = 90.0)
{
    Offset out;
    // 1段目: 100Hz の粗い包絡で ±maxShift を全部見る
    auto ea = onsetEnvelope (aL, aR, an, afs, 100.0, windowSec);
    auto eb = onsetEnvelope (bL, bR, bn, bfs, 100.0, windowSec + maxShiftSec);
    if (ea.size() < 200 || eb.size() < 200) return out;
    const int maxLag = (int) (maxShiftSec * 100.0);
    double best = -1e9, second = -1e9; int bestLag = 0;
    std::vector<double> sc ((size_t) (2 * maxLag + 1));
    for (int lag = -maxLag; lag <= maxLag; ++lag) sc[(size_t) (lag + maxLag)] = xcorrAt (ea, eb, lag);
    for (int lag = -maxLag; lag <= maxLag; ++lag)
        if (sc[(size_t) (lag + maxLag)] > best) { best = sc[(size_t) (lag + maxLag)]; bestLag = lag; }
    for (int lag = -maxLag; lag <= maxLag; ++lag)
        if (std::abs (lag - bestLag) > 20) second = std::max (second, sc[(size_t) (lag + maxLag)]);
    // 2段目: 1kHz の細かい包絡で ±15ms
    auto fa = onsetEnvelope (aL, aR, an, afs, 1000.0, std::min (windowSec, 60.0));
    auto fb = onsetEnvelope (bL, bR, bn, bfs, 1000.0, std::min (windowSec, 60.0) + maxShiftSec);
    double fBest = -1e9; int fLag = bestLag * 10;
    for (int lag = bestLag * 10 - 15; lag <= bestLag * 10 + 15; ++lag)
    {
        const double s = xcorrAt (fa, fb, lag);
        if (s > fBest) { fBest = s; fLag = lag; }
    }
    out.seconds = fLag / 1000.0;

    // 3段目: サンプルレートが同じなら、波形そのもので ±10ms をサンプル単位で合わせる（切替で位相がずれないように）
    if (std::abs (afs - bfs) < 1.0)
    {
        const int base = (int) std::lround (out.seconds * afs);
        const int len = (int) (afs * 3.0), range = (int) (afs * 0.010);
        // A の中で一番大きい 3 秒（最初の windowSec 秒から）を使う
        int startA = 0; double bestE = -1.0;
        for (int s = 0; s + len < std::min (an, (int) (windowSec * afs)); s += len / 2)
        {
            double e = 0; for (int i = s; i < s + len; i += 16) e += std::abs (aL[i]) + std::abs (aR[i]);
            if (e > bestE) { bestE = e; startA = s; }
        }
        double wBest = -1e30; int wLag = base;
        for (int lag = base - range; lag <= base + range; ++lag)
        {
            if (startA + lag < 0 || startA + len + lag >= bn) continue;
            double acc = 0.0;
            for (int i = startA; i < startA + len; ++i)
                acc += (double) (aL[i] + aR[i]) * (bL[i + lag] + bR[i + lag]);
            if (acc > wBest) { wBest = acc; wLag = lag; }
        }
        out.seconds = wLag / afs;
    }
    // 確かさ: いちばん良い所が、ほかの所よりどれだけ飛び抜けているか
    out.confidence = best <= 0 ? 0.0 : std::max (0.0, std::min (1.0, (best - std::max (0.0, second)) / best * 2.0));
    return out;
}

} // namespace mtm
