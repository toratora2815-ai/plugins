#pragma once
// Vox Chop の音の計算（JUCE を使わない。tools/EngineTest.cpp からもそのまま使う）
//
// 声を「高さ（周期ごとの山＝ピッチマーク）」「響きの山（フォルマント）」「息」に分けて作り直す。
//  - analyse()     … 声の高さを 5 ms ごとに測り（YIN）、周期ごとの山の位置を決める
//  - GrainPlayer   … 山ごとに 2 周期ぶんを切り出した粒を並べ直す（TD-PSOLA）
//                    粒の間隔 = 高さ、粒の中を読む速さ = 響きの山（体の大きさ）
//  - renderVoice() … 全体を後から丸ごと計算（VOICE MAP・BREATH・GRIT・ROBOT・ALIEN・LOW/HIGH CUT）
//  - autoChop()    … 言葉の切れ目・音量のくぼみで切る
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

namespace vox
{
using Buf = std::vector<float>;
constexpr double pi = 3.14159265358979323846;

//==============================================================================
class FFT
{
public:
    explicit FFT (int order) : n (1 << order), rev ((size_t) n), tw ((size_t) n / 2)
    {
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * i / n));
    }
    int size() const { return n; }
    void transform (std::complex<float>* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto u = x[i + j], v = x[i + j + half] * w;
                    x[i + j] = u + v;
                    x[i + j + half] = u - v;
                }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) x[i] *= s;
        }
    }
private:
    int n;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    static Biquad make (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        Biquad q;
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
        return q;
    }
    static Biquad lowpass (double fs, double f, double Q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2 * pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * Q);
        return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Biquad highpass (double fs, double f, double Q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2 * pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * Q);
        return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Biquad highShelf (double fs, double f, double db)
    {
        f = std::min (f, fs * 0.45);
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        return make (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

inline void sanitize (Buf& x)
{
    for (auto& v : x) if (! std::isfinite (v)) v = 0.0f;
}

//==============================================================================
// 声の高さと山の位置
struct Analysis
{
    double fs = 48000.0;
    int hop = 240;                 // 5 ms
    std::vector<float> period;     // hop ごとの周期（サンプル）。0 = 高さのない所（子音・息・無音）
    std::vector<double> marks;     // 周期ごとの山の位置（小さい順）
    float medianF0 = 0.0f;         // 声のある所の高さの真ん中（Hz）。0 = 声がない

    float periodAt (double pos) const
    {
        if (period.empty() || pos < 0) return 0.0f;
        const size_t i = (size_t) (pos / hop);
        return i < period.size() ? period[i] : 0.0f;
    }
    long nearestMark (double pos) const
    {
        if (marks.empty()) return -1;
        auto it = std::lower_bound (marks.begin(), marks.end(), pos);
        long i = (long) (it - marks.begin());
        if (i >= (long) marks.size()) return (long) marks.size() - 1;
        if (i > 0 && pos - marks[(size_t) i - 1] < marks[(size_t) i] - pos) return i - 1;
        return i;
    }
    float voicedShare() const
    {
        if (period.empty()) return 0.0f;
        size_t v = 0;
        for (auto p : period) v += p > 0;
        return (float) v / (float) period.size();
    }
};

inline float medianOf (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

// cancel = 途中でやめる合図（プラグインを外したとき。なくてよい）
inline Analysis analyse (const Buf& x, double fs, const std::atomic<bool>* cancel = nullptr)
{
    Analysis a;
    a.fs = fs;
    a.hop = std::max (1, (int) std::lround (fs * 0.005));
    const size_t n = x.size();
    const size_t frames = (n + (size_t) a.hop - 1) / (size_t) a.hop;
    a.period.assign (frames, 0.0f);
    if (n < (size_t) (fs * 0.05)) return a;

    // 1) 12 kHz くらいへ間引く（先に高い所を落とす）
    const int D = std::max (1, (int) std::lround (fs / 12000.0));
    const double fd = fs / D;
    Buf d ((n + (size_t) D - 1) / (size_t) D);
    {
        auto b1 = Biquad::lowpass (fs, 0.42 * fd, 0.54), b2 = Biquad::lowpass (fs, 0.42 * fd, 1.31);
        for (size_t i = 0; i < n; ++i)
        {
            const float v = b2.process (b1.process (x[i]));
            if (i % (size_t) D == 0) d[i / (size_t) D] = v;
        }
    }

    // 2) フレームごとの音量（20 ms）
    std::vector<double> cx (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) cx[i + 1] = cx[i] + (double) x[i] * x[i];
    std::vector<float> rms (frames);
    float peakRms = 0.0f;
    const long half = (long) (fs * 0.01);
    for (size_t f = 0; f < frames; ++f)
    {
        const long c = (long) (f * (size_t) a.hop) + a.hop / 2;
        const long s0 = std::max (0L, c - half), s1 = std::min ((long) n, c + half);
        rms[f] = s1 > s0 ? (float) std::sqrt ((cx[(size_t) s1] - cx[(size_t) s0]) / (double) (s1 - s0)) : 0.0f;
        peakRms = std::max (peakRms, rms[f]);
    }
    const float gate = std::max (1.0e-4f, peakRms * 0.01f);   // いちばん大きい所から −40 dB

    // 3) YIN（差分関数を FFT で）
    const int W = (int) std::lround (fd * 0.04);
    const int tauMax = (int) (fd / 60.0), tauMin = std::max (2, (int) (fd / 1000.0));
    int order = 1;
    while ((1 << order) < W + tauMax + 1) ++order;
    FFT fft (order);
    const int N = fft.size();
    std::vector<std::complex<float>> A ((size_t) N), B ((size_t) N);
    std::vector<double> cd (d.size() + 1, 0.0);
    for (size_t i = 0; i < d.size(); ++i) cd[i + 1] = cd[i] + (double) d[i] * d[i];
    std::vector<double> diff ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    std::vector<float> f0 (frames, 0.0f);

    for (size_t f = 0; f < frames; ++f)
    {
        if (cancel != nullptr && (f & 255) == 0 && cancel->load()) return a;
        if (rms[f] < gate) continue;
        const long c = ((long) (f * (size_t) a.hop) + a.hop / 2) / D;
        const long s = c - (W + tauMax) / 2;
        if (s < 0 || s + W + tauMax >= (long) d.size()) continue;
        for (int k = 0; k < N; ++k)
        {
            A[(size_t) k] = k < W + tauMax ? d[(size_t) (s + k)] : 0.0f;
            B[(size_t) k] = k < W ? d[(size_t) (s + k)] : 0.0f;
        }
        fft.transform (A.data(), false);
        fft.transform (B.data(), false);
        for (int k = 0; k < N; ++k) A[(size_t) k] *= std::conj (B[(size_t) k]);
        fft.transform (A.data(), true);
        const double E0 = cd[(size_t) (s + W)] - cd[(size_t) s];
        if (E0 < 1e-12) continue;
        double run = 0.0;
        cm[0] = 1.0;
        for (int t = 1; t <= tauMax; ++t)
        {
            const double Et = cd[(size_t) (s + t + W)] - cd[(size_t) (s + t)];
            diff[(size_t) t] = std::max (0.0, E0 + Et - 2.0 * A[(size_t) t].real());
            run += diff[(size_t) t];
            cm[(size_t) t] = run > 0 ? diff[(size_t) t] * t / run : 1.0;
        }
        int best = -1;
        for (int t = tauMin; t < tauMax; ++t)
            if (cm[(size_t) t] < 0.15)
            {
                while (t + 1 < tauMax && cm[(size_t) t + 1] < cm[(size_t) t]) ++t;
                best = t;
                break;
            }
        if (best < 0)
        {
            double m = 1e9;
            for (int t = tauMin; t < tauMax; ++t) if (cm[(size_t) t] < m) { m = cm[(size_t) t]; best = t; }
            if (m > 0.3) continue;   // はっきりした高さがない
        }
        double tau = best;
        if (best > tauMin && best < tauMax - 1)
        {
            const double y0 = cm[(size_t) best - 1], y1 = cm[(size_t) best], y2 = cm[(size_t) best + 1];
            const double den = y0 - 2 * y1 + y2;
            if (std::abs (den) > 1e-12) tau += std::clamp (0.5 * (y0 - y2) / den, -1.0, 1.0);
        }
        f0[f] = (float) (fd / tau);
    }

    // 4) 整える: 1 オクターブの取り違えを直す → 5 点の真ん中 → 短すぎる声・すき間を埋める
    {
        std::vector<float> fixed = f0;
        for (size_t f = 0; f < frames; ++f)
        {
            if (f0[f] <= 0) continue;
            std::vector<float> around;
            for (long k = (long) f - 12; k <= (long) f + 12; ++k)
                if (k >= 0 && k < (long) frames && f0[(size_t) k] > 0) around.push_back (f0[(size_t) k]);
            const float m = medianOf (around);
            const float r = f0[f] / m;
            if (r > 1.8f && r < 2.2f) fixed[f] = f0[f] * 0.5f;
            else if (r > 0.45f && r < 0.55f) fixed[f] = f0[f] * 2.0f;
        }
        for (size_t f = 0; f < frames; ++f)
        {
            if (fixed[f] <= 0) { f0[f] = 0; continue; }
            std::vector<float> w;
            for (long k = (long) f - 2; k <= (long) f + 2; ++k)
                if (k >= 0 && k < (long) frames && fixed[(size_t) k] > 0) w.push_back (fixed[(size_t) k]);
            f0[f] = medianOf (w);
        }
        // 3 フレーム以下のすき間はつなぐ
        for (size_t f = 1; f < frames; ++f)
            if (f0[f] <= 0 && f0[f - 1] > 0)
            {
                size_t e = f;
                while (e < frames && f0[e] <= 0) ++e;
                if (e < frames && e - f <= 3)
                    for (size_t k = f; k < e; ++k)
                    {
                        const float t = (float) (k - f + 1) / (float) (e - f + 1);
                        f0[k] = f0[f - 1] * std::pow (f0[e] / f0[f - 1], t);
                    }
                f = e;
            }
        // 20 ms より短い声は捨てる
        for (size_t f = 0; f < frames;)
        {
            if (f0[f] <= 0) { ++f; continue; }
            size_t e = f;
            while (e < frames && f0[e] > 0) ++e;
            if (e - f < 4) for (size_t k = f; k < e; ++k) f0[k] = 0;
            f = e;
        }
    }
    std::vector<float> voiced;
    for (size_t f = 0; f < frames; ++f)
        if (f0[f] > 0) { a.period[f] = (float) (fs / f0[f]); voiced.push_back (f0[f]); }
    a.medianF0 = medianOf (voiced);

    // 5) 山の位置: 1 kHz より下だけにした波形の、周期ごとのいちばん大きい所
    Buf sm (n);
    {
        auto lp = Biquad::lowpass (fs, 1000.0, 0.7);
        for (size_t i = 0; i < n; ++i) sm[i] = lp.process (x[i]);
    }
    for (size_t f = 0; f < frames;)
    {
        if (a.period[f] <= 0) { ++f; continue; }
        size_t e = f;
        while (e < frames && a.period[e] > 0) ++e;
        const long runStart = (long) (f * (size_t) a.hop), runEnd = std::min ((long) n, (long) (e * (size_t) a.hop));
        float T = a.period[f];
        // 向き（山が上か下か）を最初の周期で決める
        float up = 0, dn = 0;
        for (long i = runStart; i < std::min (runEnd, runStart + (long) T); ++i) { up = std::max (up, sm[(size_t) i]); dn = std::max (dn, -sm[(size_t) i]); }
        const float sign = up >= dn ? 1.0f : -1.0f;
        auto argmax = [&] (long lo, long hi)
        {
            lo = std::max (0L, lo); hi = std::min ((long) n, hi);
            long best = lo; float bv = -1e9f;
            for (long i = lo; i < hi; ++i) if (sign * sm[(size_t) i] > bv) { bv = sign * sm[(size_t) i]; best = i; }
            return best;
        };
        long pos = argmax (runStart, runStart + (long) T);
        while (pos < runEnd)
        {
            a.marks.push_back ((double) pos);
            const float Tn = a.periodAt ((double) pos);
            if (Tn > 0) T = Tn;
            const long pred = pos + (long) std::lround (T);
            long next = argmax (pred - (long) (0.25f * T), pred + (long) (0.25f * T) + 1);
            if (next <= pos + (long) (0.5f * T)) next = pred;
            pos = next;
        }
        f = e;
    }
    return a;
}

//==============================================================================
// 粒を並べ直して鳴らす（TD-PSOLA）。オフラインの計算と、鍵盤で弾くときの両方で使う
struct GrainCtl
{
    float pitchRatio = 1.0f;    // 出口の高さ / 入口の高さ
    float formantRate = 1.0f;   // 粒の中を読む速さ。1 より大きい = 響きの山が上がる（体が小さい）
    float gain = 1.0f;
    float spacingMul = 1.0f;    // 周期のゆらぎ（GRIT）
};

struct Source
{
    const float* data = nullptr;
    long lo = 0, hi = 0;        // 読んでよい範囲（切った 1 片）
    double fade = 0.0;          // 範囲の端で絞る長さ（サンプル）
    float read (double p) const
    {
        if (p < (double) lo || p >= (double) (hi - 1)) return 0.0f;
        const long i = (long) p;
        const float f = (float) (p - (double) i);
        float v = data[i] + (data[i + 1] - data[i]) * f;
        if (fade > 0.0)
        {
            const double e = std::min (p - (double) lo, (double) (hi - 1) - p);
            if (e < fade) v *= (float) (e / fade);
        }
        return v;
    }
};

class GrainPlayer
{
public:
    static constexpr int maxGrains = 64;

    // alignFirst = true: 最初の粒を元の山の位置にそろえる（全体の計算用。鍵盤では鳴り出しを遅らせないので false）
    void reset (double pos, bool alignFirst = false)
    {
        anaPos = pos; t = 0.0; count = 0; planned = false; lastVoiced = ! alignFirst;
        loopOn = false; follow = false; envRef = envOut = 0.0; followGain = 1.0;
    }
    // LOOP: 読む位置を lo..hi の中で回す（粒の予定も回すので、つなぎ目で途切れない）
    void setLoop (double lo, double hi) { loopOn = hi - lo > 2.0; loopLo = lo; loopHi = hi; }
    // 鍵盤で高さを変えたとき、音量を元の声（同じ位置）に合わせる（30 ms でならす）
    void setLevelFollow (bool on) { follow = on; }
    double position() const { return anaPos; }
    bool hasGrains() const { return count > 0; }

    // srcPerOut = 元の音のサンプルレート / 出口のサンプルレート
    // advance   = 出口 1 サンプルで元の音を何サンプル進むか（向き込み。逆再生はマイナス）
    // ctl.control (位置, 周期, 声か) → GrainCtl、ctl.emitted (出口での中心, 間隔, 声か)
    template <typename Ctl>
    void process (const Source& src, const Analysis& an, double srcPerOut, double advance, float* out, int num, Ctl& ctl)
    {
        for (int i = 0; i < num; ++i)
        {
            if (! planned) plan (an, srcPerOut, advance, ctl, t, true);
            while (planned && t >= next.outCenter - next.halfOut)
            {
                if (count < maxGrains) grains[count++] = next;
                const double c = next.outCenter + nextSpacing;
                plan (an, srcPerOut, advance, ctl, c, false);
            }
            double acc = 0.0;
            for (int g = 0; g < count;)
            {
                auto& gr = grains[g];
                const double tau = t - gr.outCenter;
                if (tau >= gr.halfOut) { gr = grains[--count]; continue; }
                if (tau > -gr.halfOut)
                {
                    const double w = 0.5 + 0.5 * std::cos (pi * tau / gr.halfOut);
                    acc += w * gr.gain * src.read (gr.srcCenter + tau * gr.rate);
                }
                ++g;
            }
            if (follow)
            {
                const double outFs = an.fs / srcPerOut;
                const double a = 1.0 - std::exp (-1.0 / (0.03 * outFs)), b = 1.0 - std::exp (-1.0 / (0.02 * outFs));
                const double ref = src.read (anaPos);
                envRef += a * (ref * ref - envRef);
                envOut += a * (acc * acc - envOut);
                if (envRef > 1e-10 && envOut > 1e-10)
                    followGain += b * (std::clamp (std::sqrt (envRef / envOut), 0.5, 2.0) - followGain);
                acc *= followGain;
            }
            out[i] += (float) acc;
            anaPos = wrap (anaPos + advance);
            t += 1.0;
        }
    }

private:
    struct Grain { double srcCenter = 0, outCenter = 0, halfOut = 1, rate = 1; float gain = 0; };
    Grain grains[maxGrains];
    Grain next;
    double nextSpacing = 1.0;
    int count = 0;
    double anaPos = 0.0, t = 0.0;
    bool planned = false, lastVoiced = true;
    bool loopOn = false, follow = false;
    double loopLo = 0.0, loopHi = 0.0, envRef = 0.0, envOut = 0.0, followGain = 1.0;

    double wrap (double x) const
    {
        if (! loopOn) return x;
        const double span = loopHi - loopLo;
        if (x >= loopHi) return loopLo + std::fmod (x - loopLo, span);
        if (x < loopLo) return loopHi - std::fmod (loopLo - x, span);
        return x;
    }

    // 次の粒を決める（中心を centre に置くつもりで。fresh = 鳴り始め）
    template <typename Ctl>
    void plan (const Analysis& an, double srcPerOut, double advance, Ctl& ctl, double centre, bool fresh)
    {
        const int dir = advance < 0 ? -1 : 1;
        auto anaAt = [&] (double outT) { return wrap (anaPos + advance * (outT - t)); };
        if (fresh)
        {
            const float P0 = an.periodAt (anaPos);
            centre = t + (P0 > 0 ? P0 : 0.005 * an.fs) / srcPerOut;   // 鳴り始め: 今から粒が始まる
        }
        double ana = anaAt (centre);
        const float P = an.periodAt (ana);
        const bool voiced = P > 0 && ! an.marks.empty();
        const GrainCtl c = ctl.control (ana, voiced ? P : 0.0f, voiced);
        const double fr = std::max (0.25, (double) c.formantRate);
        Grain g;
        double spacing;
        if (voiced)
        {
            long m = an.nearestMark (ana);
            if (! lastVoiced)
            {
                // 声の出だし: 粒の中心を元の山の位置（の時刻）にそろえる。山がもう過ぎていれば次の山
                while (m >= 0 && m < (long) an.marks.size() && (an.marks[(size_t) m] - ana) * dir < 0) m += dir;
                if (m < 0 || m >= (long) an.marks.size()) m = an.nearestMark (ana);
                else { centre += (an.marks[(size_t) m] - ana) / advance; ana = an.marks[(size_t) m]; }
            }
            double halfSrc = P;   // その場所の本当の山の間隔（ずれずに元とぴったり重なる）
            const long m2 = m + dir;
            if (m2 >= 0 && m2 < (long) an.marks.size())
            {
                const double d = std::abs (an.marks[(size_t) m2] - an.marks[(size_t) m]);
                if (d > 0.6 * P && d < 1.6 * P) halfSrc = d;
            }
            g.srcCenter = an.marks[(size_t) m];
            g.halfOut = std::max (2.0, halfSrc / (srcPerOut * fr));
            spacing = halfSrc / srcPerOut / std::max (0.1, (double) c.pitchRatio) * c.spacingMul;
            g.gain = c.gain * (float) std::sqrt (std::min (4.0, spacing / g.halfOut));
        }
        else
        {
            g.srcCenter = ana;
            g.halfOut = std::max (2.0, 0.005 * an.fs / (srcPerOut * fr));
            spacing = g.halfOut;   // 50% 重ね = 足すとちょうど 1（息・子音はブザーにしない）
            g.gain = c.gain;
        }
        lastVoiced = voiced;
        g.outCenter = centre;
        g.rate = srcPerOut * fr * dir;
        next = g;
        nextSpacing = std::max (1.0, spacing);
        planned = true;
        ctl.emitted (g.outCenter, nextSpacing, voiced);
    }
};

//==============================================================================
// 声の作り直し（全体を後から計算）
struct VoiceSettings
{
    float shift = 0.0f;      // 高さ（半音）
    float formant = 0.0f;    // 響きの山（半音ぶんの倍率。+3 ≒ 1.19 倍 = 体が小さい）
    float breath = 0.0f;     // 0..1 息に置き換える量（1 = ささやき）
    float grit = 0.0f;       // 0..1 かすれ・ボーカルフライ
    float robot = 0.0f;      // 0..1 高さを 1 つに固定
    float alien = 0.0f;      // 0..1 声から離す（響きの山を極端に・ロボット・息・金属の響き）
    float lowCut = 20.0f, highCut = 20000.0f;
    bool isNeutral() const
    {
        return std::abs (shift) < 1e-4f && std::abs (formant) < 1e-4f && breath < 1e-4f && grit < 1e-4f && robot < 1e-4f
            && alien < 1e-4f && lowCut <= 20.5f && highCut >= 19990.0f;
    }
};

// 息の音に置き換える（なめらかにした音色の形 × でたらめな位相）
inline Buf whisperize (const Buf& x, double fs, uint32_t seed, const std::atomic<bool>* cancel = nullptr)
{
    int order = 8;
    while ((1 << order) < fs * 0.021) ++order;
    FFT fft (order);
    const int N = fft.size(), hop = N / 4;
    const size_t n = x.size();
    Buf out (n + (size_t) N, 0.0f), win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * pi * i / N));
    const int B = std::max (2, (int) std::lround (250.0 / (fs / N)));
    std::vector<std::complex<float>> X ((size_t) N);
    std::vector<float> P ((size_t) N / 2 + 1), S ((size_t) N / 2 + 1);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> ph (0.0f, (float) (2 * pi));
    for (long s = -(long) N; s < (long) n; s += hop)
    {
        if (cancel != nullptr && cancel->load()) break;
        for (int k = 0; k < N; ++k)
        {
            const long i = s + k;
            X[(size_t) k] = (i >= 0 && i < (long) n) ? x[(size_t) i] * win[(size_t) k] : 0.0f;
        }
        fft.transform (X.data(), false);
        for (int k = 0; k <= N / 2; ++k) P[(size_t) k] = std::norm (X[(size_t) k]);
        double run = 0.0;
        int cnt = 0;
        for (int k = 0; k <= std::min (B, N / 2); ++k) { run += P[(size_t) k]; ++cnt; }
        for (int k = 0; k <= N / 2; ++k)
        {
            // k-B..k+B の平均（パワー）
            const float hz = (float) (k * fs / N);
            // ささやきは低い所がほとんどない: 500 Hz の 4 次ハイパスの形をかける
            const double r4 = std::pow (hz / 500.0, 4.0);
            S[(size_t) k] = (float) (std::sqrt (std::max (0.0, run) / std::max (1, cnt)) * r4 / std::sqrt (1.0 + r4 * r4));
            const int add = k + B + 1, rem = k - B;
            if (add <= N / 2) { run += P[(size_t) add]; ++cnt; }
            if (rem >= 0) { run -= P[(size_t) rem]; --cnt; }
        }
        X[0] = 0.0f;
        for (int k = 1; k < N / 2; ++k)
        {
            X[(size_t) k] = std::polar (S[(size_t) k], ph (rng));
            X[(size_t) (N - k)] = std::conj (X[(size_t) k]);
        }
        X[(size_t) N / 2] = 0.0f;
        fft.transform (X.data(), true);
        for (int k = 0; k < N; ++k)
        {
            const long i = s + k;
            if (i >= 0 && i < (long) out.size()) out[(size_t) i] += X[(size_t) k].real() * win[(size_t) k] / 1.5f;
        }
    }
    out.resize (n);
    return out;
}

// 耳の感じ方に近い重み（K 特性のおおよそ: 60 Hz より下を落とし、1.7 kHz より上を +4 dB）
inline Buf kWeighted (const Buf& x, double fs)
{
    Buf y = x;
    auto hp = Biquad::highpass (fs, 60.0, 0.5), sh = Biquad::highShelf (fs, 1680.0, 4.0);
    for (auto& v : y) v = sh.process (hp.process (v));
    return y;
}

struct Rendered
{
    Buf audio;
    Analysis an;     // 作り直した後の山の位置（鍵盤で弾くときに使う）
};

inline Rendered renderVoice (const Buf& x, const Analysis& in, const VoiceSettings& s, uint32_t seed = 1, const std::atomic<bool>* cancel = nullptr)
{
    Rendered r;
    const double fs = in.fs;
    const size_t n = x.size();
    r.an.fs = fs;
    r.an.hop = in.hop;
    r.an.period.assign (in.period.size(), 0.0f);
    if (n == 0) return r;
    if (s.isNeutral())
    {
        r.audio = x;
        r.an = in;
        return r;
    }

    const float alien = std::clamp (s.alien, 0.0f, 1.0f);
    const float robot = std::clamp (s.robot + 0.6f * alien, 0.0f, 1.0f);
    const float formantSt = s.formant + (s.formant >= 0.0f ? 1.0f : -1.0f) * alien * 8.0f;
    const float fr = (float) std::pow (2.0, formantSt / 12.0);
    const float grit = std::clamp (s.grit, 0.0f, 1.0f);
    const float breath = std::clamp (s.breath + 0.3f * alien, 0.0f, 1.0f);
    const double f0Robot = (in.medianF0 > 0 ? in.medianF0 : 150.0) * std::pow (2.0, s.shift / 12.0);
    const double shiftRatio = std::pow (2.0, s.shift / 12.0);

    struct Ctl
    {
        double fs, shiftRatio, f0Robot;
        float robot, fr, grit;
        std::mt19937 rng;
        bool alt = false;
        std::vector<std::pair<double, double>> marks;   // 声の粒の (出口での中心, 間隔)
        GrainCtl control (double, float P, bool voiced)
        {
            GrainCtl c;
            c.formantRate = fr;
            if (! voiced) return c;
            const double f0in = fs / P;
            const double lo = std::log (f0in * shiftRatio), lr = std::log (f0Robot);
            c.pitchRatio = (float) (std::exp ((1.0 - robot) * lo + robot * lr) / f0in);
            if (grit > 0.0f)
            {
                std::uniform_real_distribution<float> u (-1.0f, 1.0f);
                alt = ! alt;
                // 長い・短いを交互（半分の高さのうなり）＋ゆらぎ（ジッター・シマー）
                c.spacingMul = (1.0f + grit * 0.10f * u (rng)) * (alt ? 1.0f + 0.2f * grit : 1.0f - 0.2f * grit);
                c.gain = std::max (0.05f, (1.0f + 0.35f * grit * u (rng)) * (alt ? 1.0f : 1.0f - 0.65f * grit));
            }
            return c;
        }
        void emitted (double centre, double spacing, bool voiced) { if (voiced) marks.emplace_back (centre, spacing); }
    } ctl { fs, shiftRatio, f0Robot, robot, fr, grit, std::mt19937 (seed), false, {} };

    Buf y (n, 0.0f);
    {
        // 粒を速く読む（体を小さく）ときは、先に高い所を落とす（折り返し＝高い音が低く化けるのを防ぐ）
        Buf pre;
        const float* data = x.data();
        if (fr > 1.02f)
        {
            pre = x;
            const double fc = 0.7 * fs / (2.0 * fr);
            auto l1 = Biquad::lowpass (fs, fc, 0.5412), l2 = Biquad::lowpass (fs, fc, 1.3066);
            for (auto& v : pre) v = l2.process (l1.process (v));
            data = pre.data();
        }
        GrainPlayer gp;
        gp.reset (0.0, true);
        Source src { data, 0, (long) n, 0.0 };
        const int block = 4096;
        for (size_t p = 0; p < n; p += block)
        {
            if (cancel != nullptr && cancel->load()) return r;
            gp.process (src, in, 1.0, 1.0, y.data() + p, (int) std::min ((size_t) block, n - p), ctl);
        }
    }

    // 金属の響き（ALIEN）
    if (alien > 0.0f)
    {
        const size_t dl = (size_t) std::max (1.0, fs * 0.0013);
        const float fb = 0.45f * alien;
        Buf comb (n, 0.0f);
        for (size_t i = 0; i < n; ++i) comb[i] = y[i] + (i >= dl ? fb * comb[i - dl] : 0.0f);
        const float mix = 0.7f * alien;
        for (size_t i = 0; i < n; ++i) y[i] += mix * (comb[i] * (1.0f - fb) - y[i]);
    }

    // 息（BREATH）
    if (breath > 0.001f)
    {
        const Buf w = whisperize (y, fs, seed * 7919u + 13u, cancel);
        const float dry = std::sqrt (1.0f - breath), wet = std::sqrt (breath);
        for (size_t i = 0; i < n; ++i) y[i] = dry * y[i] + wet * w[i];
        sanitize (y);
    }

    // 響きの山を下げるとこもるので、高い所を少し足す
    if (fr < 0.98f)
    {
        auto sh = Biquad::highShelf (fs, 2500.0, std::min (6.0, (1.0 - fr) * 18.0));
        for (auto& v : y) v = sh.process (v);
    }

    // 音量を元の声に合わせる（40 ms ごと）。粒の重なり方で変わった音量を戻す
    // 比べるのは耳の感じ方に近い K 特性で（低い基音だけで比べると、ささやきなどが小さくなりすぎる）
    {
        const Buf xh = kWeighted (x, fs), yh = kWeighted (y, fs);
        const double maxUp = 3.0 + 3.0 * breath;   // ささやきは K 特性でも小さいので、上げる上限を広げる
        const size_t hop = (size_t) std::max (1.0, fs * 0.01), win = hop * 4;
        const size_t m = n / hop + 1;
        std::vector<float> g (m, 1.0f);
        for (size_t j = 0; j < m; ++j)
        {
            const size_t c = j * hop, a0 = c > win / 2 ? c - win / 2 : 0, a1 = std::min (n, c + win / 2);
            double es = 0, eo = 0;
            for (size_t i = a0; i < a1; ++i) { es += (double) xh[i] * xh[i]; eo += (double) yh[i] * yh[i]; }
            const double k = (double) std::max<size_t> (1, a1 - a0);
            es = std::sqrt (es / k); eo = std::sqrt (eo / k);
            g[j] = (eo > 1e-6 && es > 1e-5) ? (float) std::clamp (es / eo, 0.3, maxUp) : (es <= 1e-5 ? std::min (1.0f, (float) (es / std::max (eo, 1e-9))) : 1.0f);
        }
        std::vector<float> gs (m);
        for (size_t j = 0; j < m; ++j)
        {
            float a = 0; int c = 0;
            for (long k = (long) j - 1; k <= (long) j + 1; ++k) if (k >= 0 && k < (long) m) { a += g[(size_t) k]; ++c; }
            gs[j] = a / (float) c;
        }
        for (size_t i = 0; i < n; ++i)
        {
            const double p = (double) i / (double) hop;
            const size_t j = std::min (m - 1, (size_t) p), j1 = std::min (m - 1, j + 1);
            const float f = (float) (p - (double) j);
            y[i] *= gs[j] + (gs[j1] - gs[j]) * f;
        }
    }

    // LOW CUT / HIGH CUT
    if (s.lowCut > 20.5f)
    {
        auto h1 = Biquad::highpass (fs, s.lowCut, 0.7071);
        for (auto& v : y) v = h1.process (v);
    }
    if (s.highCut < 19990.0f)
    {
        auto l1 = Biquad::lowpass (fs, s.highCut, 0.7071);
        for (auto& v : y) v = l1.process (v);
    }
    sanitize (y);
    float peak = 0.0f;
    for (auto v : y) peak = std::max (peak, std::abs (v));
    if (peak > 0.98f) for (auto& v : y) v *= 0.98f / peak;
    r.audio = std::move (y);

    // 作り直した後の山の位置（息が多いときは声として扱わない）
    if (breath < 0.9f && ! ctl.marks.empty())
    {
        auto& mk = ctl.marks;
        std::sort (mk.begin(), mk.end());
        std::vector<float> f0s;
        for (size_t i = 0; i + 1 < mk.size(); ++i)
        {
            const double a0 = mk[i].first, a1 = mk[i + 1].first, sp = mk[i].second;
            r.an.marks.push_back (a0);
            if (a1 - a0 > 2.5 * sp) continue;   // 声の切れ目
            const double per = a1 - a0;
            for (size_t f = (size_t) std::max (0.0, a0 / in.hop); f < r.an.period.size() && (double) (f * (size_t) in.hop) < a1; ++f)
                r.an.period[f] = (float) per;
            f0s.push_back ((float) (fs / per));
        }
        r.an.marks.push_back (mk.back().first);
        r.an.medianF0 = medianOf (f0s);
    }
    return r;
}

//==============================================================================
// 切り分け
struct Cut { long start = 0, end = 0; };

inline std::vector<float> envelopeDb (const Buf& x, double fs, int hop)
{
    const size_t n = x.size(), frames = n / (size_t) hop + 1;
    std::vector<double> c (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) c[i + 1] = c[i] + (double) x[i] * x[i];
    std::vector<float> e (frames);
    const long half = (long) (fs * 0.01);
    for (size_t f = 0; f < frames; ++f)
    {
        const long m = (long) (f * (size_t) hop) + hop / 2;
        const long a = std::max (0L, m - half), b = std::min ((long) n, m + half);
        const double v = b > a ? (c[(size_t) b] - c[(size_t) a]) / (double) (b - a) : 0.0;
        e[f] = (float) (10.0 * std::log10 (v + 1e-12));
    }
    return e;
}

inline long nearestZeroCross (const Buf& x, long p, long range)
{
    for (long d = 0; d <= range; ++d)
        for (long q : { p - d, p + d })
            if (q > 0 && q < (long) x.size() && ((x[(size_t) q - 1] <= 0) != (x[(size_t) q] <= 0))) return q;
    return p;
}

// sens 0..1（大きいほど細かく切る）
inline std::vector<Cut> autoChop (const Buf& x, double fs, float sens, int maxSlices = 64)
{
    std::vector<Cut> out;
    const size_t n = x.size();
    if (n < 16) return out;
    const int hop = std::max (1, (int) std::lround (fs * 0.005));
    const auto env = envelopeDb (x, fs, hop);
    const long frames = (long) env.size();
    float peak = -200.0f;
    for (auto v : env) peak = std::max (peak, v);
    if (peak < -70.0f) return out;

    for (int attempt = 0; attempt < 12; ++attempt)
    {
        const float s = std::clamp (sens - 0.1f * (float) attempt, 0.0f, 1.0f);
        auto lerp = [s] (float a, float b) { return a + (b - a) * s; };
        const float silThr = std::max (peak - 38.0f, -65.0f);
        const long gapMin = (long) (lerp (0.22f, 0.04f) / 0.005f);
        const float dipDb = lerp (15.0f, 4.0f);
        const long minLen = (long) (lerp (0.28f, 0.07f) / 0.005f);

        // 鳴っている所（短いすき間はつなぐ）
        std::vector<std::pair<long, long>> regions;
        for (long f = 0; f < frames;)
        {
            if (env[(size_t) f] < silThr) { ++f; continue; }
            long e = f;
            while (e < frames && env[(size_t) e] >= silThr) ++e;
            if (! regions.empty() && f - regions.back().second < gapMin) regions.back().second = e;
            else regions.emplace_back (f, e);
            f = e;
        }
        std::vector<std::pair<long, long>> pieces;
        for (auto [a, b] : regions)
        {
            if ((b - a) * 0.005 < 0.03) continue;   // カチッという音だけ
            // くぼみで切る（深い順）
            std::vector<std::pair<float, long>> cand;
            for (long m = a + minLen; m < b - minLen; ++m)
            {
                if (env[(size_t) m] > env[(size_t) m - 1] || env[(size_t) m] > env[(size_t) m + 1]) continue;
                float ml = -200, mr = -200;
                for (long k = std::max (a, m - 40); k <= m; ++k) ml = std::max (ml, env[(size_t) k]);
                for (long k = m; k < std::min (b, m + 40); ++k) mr = std::max (mr, env[(size_t) k]);
                const float depth = std::min (ml, mr) - env[(size_t) m];
                if (depth >= dipDb) cand.emplace_back (depth, m);
            }
            std::sort (cand.begin(), cand.end(), [] (auto& p, auto& q) { return p.first > q.first; });
            std::vector<long> cuts;
            for (auto& [d, m] : cand)
            {
                bool ok = m - a >= minLen && b - m >= minLen;
                for (long c : cuts) ok = ok && std::abs (c - m) >= minLen;
                if (ok) cuts.push_back (m);
            }
            std::sort (cuts.begin(), cuts.end());
            long prev = a;
            for (long c : cuts) { pieces.emplace_back (prev, c); prev = c; }
            pieces.emplace_back (prev, b);
        }
        if ((int) pieces.size() > maxSlices && attempt < 11) continue;

        out.clear();
        const long pre = (long) (fs * 0.01), post = (long) (fs * 0.03), zc = (long) (fs * 0.003);
        for (size_t i = 0; i < pieces.size() && (int) out.size() < maxSlices; ++i)
        {
            Cut c;
            const bool joinedPrev = i > 0 && pieces[i - 1].second == pieces[i].first;
            const bool joinedNext = i + 1 < pieces.size() && pieces[i + 1].first == pieces[i].second;
            c.start = joinedPrev ? nearestZeroCross (x, pieces[i].first * hop + hop / 2, zc) : pieces[i].first * hop - pre;
            c.end = joinedNext ? nearestZeroCross (x, pieces[i].second * hop + hop / 2, zc) : pieces[i].second * hop + post;
            c.start = std::max (c.start, out.empty() ? 0L : out.back().end);
            c.end = std::min (c.end, (long) n);
            if (i + 1 < pieces.size() && ! joinedNext) c.end = std::min (c.end, pieces[i + 1].first * hop - pre);
            if (c.end - c.start > (long) (fs * 0.02)) out.push_back (c);
        }
        break;
    }
    return out;
}

// 拍で切る（step 秒ごと。phase 秒の所に拍の線がある）。ほぼ無音の所は飛ばす
inline std::vector<Cut> gridChop (const Buf& x, double fs, double stepSec, double phaseSec, int maxSlices = 64)
{
    std::vector<Cut> out;
    const long n = (long) x.size();
    const double step = std::max (0.02, stepSec) * fs;
    double p = std::fmod (phaseSec * fs, step);
    if (p > 0) p -= step;   // 最初の線より前の所も 1 片に
    for (; p < n && (int) out.size() < maxSlices; p += step)
    {
        Cut c { std::max (0L, (long) std::lround (p)), std::min (n, (long) std::lround (p + step)) };
        float pk = 0.0f;
        for (long i = c.start; i < c.end; ++i) pk = std::max (pk, std::abs (x[(size_t) i]));
        if (pk > 0.003f && c.end - c.start > 16) out.push_back (c);
    }
    return out;
}
} // namespace vox
