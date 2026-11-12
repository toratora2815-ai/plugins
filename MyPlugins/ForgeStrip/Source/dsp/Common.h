// Forge Strip: 共通の部品（JUCE なし）
#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <complex>
#include <cstring>

namespace fs
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }
template <typename T> inline T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }

// 時定数 ms → 1 サンプルごとの係数（0 ms なら即）
inline float coefMs (float ms, double sr) { return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr)); }

// ---------------------------------------------------------------------
// 双二次フィルター（Direct Form II transposed）
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    inline float process (float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
    // アナログの試作 H(s) = (B0 s^2 + B1 s + B2) / (A0 s^2 + A1 s + A2) を、w0（rad/s 正規化 = 1）で
    // 双一次変換（fc でプリワープ）する。s は fc で正規化済み
    void fromAnalog (double B0, double B1, double B2, double A0, double A1, double A2, double fc, double sr)
    {
        fc = std::min (fc, sr * 0.49);
        const double K = 1.0 / std::tan (kPi * fc / sr);   // s = K (1 - z^-1) / (1 + z^-1)
        const double K2 = K * K;
        const double a0 = A0 * K2 + A1 * K + A2;
        b0 = (B0 * K2 + B1 * K + B2) / a0;
        b1 = (2.0 * (B2 - B0 * K2)) / a0;
        b2 = (B0 * K2 - B1 * K + B2) / a0;
        a1 = (2.0 * (A2 - A0 * K2)) / a0;
        a2 = (A0 * K2 - A1 * K + A2) / a0;
    }
    // 1 次: H(s) = (B0 s + B1) / (A0 s + A1)
    void fromAnalog1 (double B0, double B1, double A0, double A1, double fc, double sr)
    {
        fc = std::min (fc, sr * 0.49);
        const double K = 1.0 / std::tan (kPi * fc / sr);
        const double a0 = A0 * K + A1;
        b0 = (B0 * K + B1) / a0;
        b1 = (B1 - B0 * K) / a0;
        b2 = 0;
        a1 = (A1 - A0 * K) / a0;
        a2 = 0;
    }
    // 周波数 f での複素応答
    std::complex<double> response (double f, double sr) const
    {
        const double w = 2.0 * kPi * f / sr;
        const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
        return (b0 + b1 * z1c + b2 * z2c) / (1.0 + a1 * z1c + a2 * z2c);
    }
    void lowpass (double fc, double q, double sr) { fromAnalog (0, 0, 1, 1, 1.0 / q, 1, fc, sr); }
    // アナログの形に合わせた（高域がつぶれない）2 次ローパス・ハイパス（Vicanek の方法）
    void matchedPoles (double w0, double q)
    {
        const double zeta = 1.0 / (2.0 * q);
        const double e = std::exp (-zeta * w0);
        a1 = zeta <= 1.0 ? -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * w0) : -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * w0);
        a2 = e * e;
    }
    void lowpassMatched (double fc, double q, double sr)
    {
        const double w0 = 2.0 * kPi * std::min (fc, sr * 0.49) / sr;
        matchedPoles (w0, q);
        const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
        const double s2 = std::sin (w0 / 2) * std::sin (w0 / 2), p0 = 1 - s2, p1 = s2, p2 = 4 * p0 * p1;
        const double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * q * q;
        const double B0 = A0, B1 = std::max (0.0, (R1 - B0 * p0) / p1);
        b0 = 0.5 * (std::sqrt (B0) + std::sqrt (B1)); b1 = std::sqrt (B0) - b0; b2 = 0;
    }
    void highpassMatched (double fc, double q, double sr)
    {
        const double w0 = 2.0 * kPi * std::min (fc, sr * 0.49) / sr;
        matchedPoles (w0, q);
        const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
        const double s2 = std::sin (w0 / 2) * std::sin (w0 / 2), p0 = 1 - s2, p1 = s2, p2 = 4 * p0 * p1;
        b0 = q * std::sqrt (A0 * p0 + A1 * p1 + A2 * p2) / (4 * p1); b1 = -2 * b0; b2 = b0;
    }
    // 1 次（極は e^{-w0}、ナイキストでの大きさをアナログに合わせる）
    void lowpass1Matched (double fc, double sr)
    {
        const double w0 = 2.0 * kPi * fc / sr, p = std::exp (-w0), r = (sr * 0.5) / fc, gN = 1.0 / std::sqrt (1 + r * r);
        const double sum = 1 - p, diff = gN * (1 + p);
        b0 = 0.5 * (sum + diff); b1 = 0.5 * (sum - diff); b2 = 0; a1 = -p; a2 = 0;
    }
    void highpass1Matched (double fc, double sr)
    {
        const double w0 = 2.0 * kPi * fc / sr, p = std::exp (-w0), r = (sr * 0.5) / fc, gN = r / std::sqrt (1 + r * r);
        b0 = gN * (1 + p) / 2; b1 = -b0; b2 = 0; a1 = -p; a2 = 0;
    }
    void highpass (double fc, double q, double sr) { fromAnalog (1, 0, 0, 1, 1.0 / q, 1, fc, sr); }
    void lowpass1 (double fc, double sr) { fromAnalog1 (0, 1, 1, 1, fc, sr); }
    void highpass1 (double fc, double sr) { fromAnalog1 (1, 0, 1, 1, fc, sr); }
    void allpass (double fc, double q, double sr) { fromAnalog (1, -1.0 / q, 1, 1, 1.0 / q, 1, fc, sr); }
    void bandpass (double fc, double q, double sr) { fromAnalog (0, 1.0 / q, 0, 1, 1.0 / q, 1, fc, sr); }
    void peak (double fc, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        fromAnalog (1, A / q, 1, 1, 1.0 / (A * q), 1, fc, sr);
    }
    // RBJ のシェルフ。gainDb は全体の上げ下げ量（真ん中＝fc でその半分）
    void lowShelf (double fc, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        fromAnalog (A, A * sA / q, A * A, A, sA / q, 1, fc, sr);
    }
    void highShelf (double fc, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        fromAnalog (A * A, A * sA / q, A, 1, sA / q, A, fc, sr);
    }
    // 1 次のシェルフ（fc で半分の量）
    void lowShelf1 (double fc, double gainDb, double sr)
    {
        const double g = std::pow (10.0, gainDb / 20.0), a = std::sqrt (g);
        fromAnalog1 (1, a, 1, 1.0 / a, fc, sr);
    }
    void highShelf1 (double fc, double gainDb, double sr)
    {
        const double g = std::pow (10.0, gainDb / 20.0), a = std::sqrt (g);
        fromAnalog1 (a, 1, 1.0 / a, 1, fc, sr);
    }
};

// ---------------------------------------------------------------------
// 3 帯域までの分割（Linkwitz-Riley 4 次。足すと元に戻る＝全域通過）
struct Crossover3
{
    int bands = 1;
    Biquad lp1[2], hp1[2], lp2[2], hp2[2], ap[2];   // ap: 低い帯域の位相をそろえる（2 本目の分割点の全域通過）
    float f1 = 200, f2 = 6000;
    double sr = 48000;
    void prepare (double s) { sr = s; set (bands, f1, f2, true); }
    void reset() { for (int i = 0; i < 2; ++i) { lp1[i].reset(); hp1[i].reset(); lp2[i].reset(); hp2[i].reset(); ap[i].reset(); } }
    void set (int nb, float a, float b, bool force = false)
    {
        if (! force && nb == bands && a == f1 && b == f2) return;
        bands = nb; f1 = a; f2 = std::max (b, a * 1.05f);
        const double q = 0.70710678;
        for (int i = 0; i < 2; ++i)
        {
            lp1[i].lowpass (f1, q, sr); hp1[i].highpass (f1, q, sr);
            lp2[i].lowpass (f2, q, sr); hp2[i].highpass (f2, q, sr);
        }
        // LR4 の全域通過 = 2 次の全域通過（Q 0.5... LR4 の和は 2 次全域通過 Q = 0.7071）
        for (int i = 0; i < 2; ++i) ap[i].allpass (f2, q, sr);
    }
    // in → out[0..bands-1]
    inline void split (float x, int ch, float* out)
    {
        if (bands <= 1) { out[0] = x; return; }
        float lo = lp1[1].process (lp1[0].process (x, ch), ch);
        float hi = hp1[1].process (hp1[0].process (x, ch), ch);
        if (bands == 2) { out[0] = lo; out[1] = hi; return; }
        // 低い帯域にも f2 の全域通過をかけて、上 2 つの和と位相をそろえる（LR4 の和 = 2 次全域通過 Q 0.7071 が 1 つ）
        lo = ap[0].process (lo, ch);
        out[0] = lo;
        out[1] = lp2[1].process (lp2[0].process (hi, ch), ch);
        out[2] = hp2[1].process (hp2[0].process (hi, ch), ch);
    }
};

// ---------------------------------------------------------------------
// 直近 W サンプルの最大値（単調キュー）
struct SlidingMax
{
    std::vector<float> val; std::vector<long> idx;
    int head = 0, tail = 0, cap = 0; long n = 0; int window = 1;
    void prepare (int w) { window = std::max (1, w); cap = window + 2; val.assign ((size_t) cap, 0); idx.assign ((size_t) cap, 0); head = tail = 0; n = 0; }
    void reset() { head = tail = 0; n = 0; }
    inline float push (float v)
    {
        while (tail != head && val[(size_t) ((tail - 1 + cap) % cap)] <= v) tail = (tail - 1 + cap) % cap;
        val[(size_t) tail] = v; idx[(size_t) tail] = n; tail = (tail + 1) % cap;
        while (idx[(size_t) head] <= n - window) head = (head + 1) % cap;
        ++n;
        return val[(size_t) head];
    }
};

// ---------------------------------------------------------------------
// 4 倍オーバーサンプリング（直線位相の FIR。遅れ = latency サンプルを申告する）
struct Oversampler4
{
    static constexpr int factor = 4, taps = 97;              // 4 倍の速さで 97 タップ → 片道 48（= 元の 12 サンプル）
    static constexpr int latency = (taps - 1) / factor;      // 上げ＋下げで元の 24 サンプル
    float h[taps] {};
    float upHist[2][taps / factor + 2] {};   // 元の速さの入力の履歴
    float dnHist[2][taps] {};                // 4 倍の速さの出力の履歴
    int dnPos[2] {};
    void prepare (double)
    {
        // カイザー窓の sinc（通過 0.43 × 元の fs まで、元の Nyquist で大きく下げる）
        const double fc = 0.115, beta = 8.0;
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        double sum = 0;
        for (int i = 0; i < taps; ++i)
        {
            const double m = i - (taps - 1) * 0.5;
            const double sinc = m == 0 ? 2 * fc : std::sin (2 * kPi * fc * m) / (kPi * m);
            const double r = m / ((taps - 1) * 0.5);
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
            h[i] = (float) (sinc * w); sum += h[i];
        }
        for (int i = 0; i < taps; ++i) h[i] = (float) (h[i] / sum);
        reset();
    }
    void reset() { std::memset (upHist, 0, sizeof (upHist)); std::memset (dnHist, 0, sizeof (dnHist)); dnPos[0] = dnPos[1] = 0; }
    template <typename F> inline float process (float x, int ch, F&& f)
    {
        constexpr int H = taps / factor + 2;
        float* uh = upHist[ch];
        for (int i = H - 1; i > 0; --i) uh[i] = uh[i - 1];
        uh[0] = x;
        float y = 0;
        for (int k = 0; k < factor; ++k)
        {
            // 0 を詰めて 4 倍にした列を h でたたみこむ（位相 k のタップだけ）
            float u = 0;
            for (int j = 0; k + j * factor < taps; ++j) u += h[k + j * factor] * uh[j];
            u *= (float) factor;
            const float v = f (u);
            float* dh = dnHist[ch];
            dh[dnPos[ch]] = v;
            if (k == 0)   // 4n の位置で間引く → ちょうど 24 サンプル遅れ
            {
                float acc = 0; int p = dnPos[ch];
                for (int t = 0; t < taps; ++t) { acc += h[t] * dh[p]; if (--p < 0) p = taps - 1; }
                y = acc;
            }
            if (++dnPos[ch] >= taps) dnPos[ch] = 0;
        }
        return y;
    }
};

// 単純な遅延線（整数サンプル）
struct Delay
{
    std::vector<float> buf; int pos = 0, len = 1;
    void prepare (int maxLen) { buf.assign ((size_t) std::max (1, maxLen + 1), 0.0f); pos = 0; len = (int) buf.size(); }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline float process (float x, int d)
    {
        buf[(size_t) pos] = x;
        int r = pos - d; if (r < 0) r += len;
        const float y = buf[(size_t) r];
        if (++pos >= len) pos = 0;
        return y;
    }
    // 小数の遅れ（直線補間）。遅れをなめらかに動かすとき用（d は 0 〜 len - 2）
    inline float processFrac (float x, float d)
    {
        buf[(size_t) pos] = x;
        const int di = (int) d; const float f = d - (float) di;
        int r0 = pos - di; if (r0 < 0) r0 += len;
        int r1 = r0 - 1; if (r1 < 0) r1 += len;
        const float y = buf[(size_t) r0] + f * (buf[(size_t) r1] - buf[(size_t) r0]);
        if (++pos >= len) pos = 0;
        return y;
    }
};
} // namespace fs
