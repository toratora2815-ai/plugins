#pragma once
// 抑える系（コンプ・リミッター・ゲート）の共通の部品。JUCE なし。
//   Biquad … RBJ の 2 次フィルター（側鎖のハイパス・音色）
//   compGainDb … 静的な曲線（しきい値・比率・ニー）→ 下げる量 dB
//   Ballistics … 下げる量を ATTACK / RELEASE でならす（dB で）
//   Opto … 光センサーふうの 2 段の戻り（速い戻り＋ゆっくりの戻り）
#include "Module.h"
#include "Oversampler.h"

namespace aiofx
{
constexpr double kPiD = 3.14159265358979323846;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = flush (b1 * x - a1 * y + z2);
        z2 = flush (b2 * x - a2 * y);
        return y;
    }
    void setNorm (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void setHP (double sr, double f, double q = 0.70710678)
    {
        const double w = 2 * kPiD * std::min (f, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setLP (double sr, double f, double q = 0.70710678)
    {
        const double w = 2 * kPiD * std::min (f, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setPeak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPiD * std::min (f, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (double sr, double f, double gainDb, double s = 1.0)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPiD * std::min (f, 0.49 * sr) / sr, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt ((A + 1 / A) * (1 / s - 1) + 2), sq = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                 (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void setHighShelf (double sr, double f, double gainDb, double s = 1.0)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPiD * std::min (f, 0.49 * sr) / sr, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt ((A + 1 / A) * (1 / s - 1) + 2), sq = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// 1 次のフィルター
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void reset() { z = 0.0f; }
    void setLP (double sr, double f) { a = (float) std::exp (-2 * kPiD * std::min (f, 0.45 * sr) / sr); }
    inline float lp (float x) { z = flush (x + (z - x) * a); return z; }
    inline float hp (float x) { return x - lp (x); }
};

// 静的な曲線: 入る大きさ（dB）→ 下げる量（dB、0 以下）。knee は幅（dB、0 = 角）
inline float compGainDb (float levelDb, float thrDb, float ratio, float kneeDb)
{
    const float over = levelDb - thrDb;
    const float s = 1.0f / std::max (1.0f, ratio) - 1.0f;   // 傾き（−1..0）
    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + 0.5f * kneeDb;
        return s * t * t / (2.0f * kneeDb);
    }
    return over > 0.0f ? s * over : 0.0f;
}

// 同じ曲線を傾き（1/比率 − 1、−1..0）で渡す形（傾きをなめらかに動かすとき）
inline float compGainDbSlope (float levelDb, float thrDb, float slope, float kneeDb)
{
    const float over = levelDb - thrDb;
    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + 0.5f * kneeDb;
        return slope * t * t / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

// 時定数（秒）→ 1 サンプルの係数
inline float tauCoef (double sr, double sec) { return (float) std::exp (-1.0 / (std::max (1.0e-6, sec) * sr)); }

// 下げる量（dB、0 以下）を ATTACK（深くなるとき）・RELEASE（戻るとき）でならす
struct Ballistics
{
    float g = 0.0f, aA = 0.0f, aR = 0.0f;
    void reset() { g = 0.0f; }
    void set (double sr, double attSec, double relSec) { aA = tauCoef (sr, attSec); aR = tauCoef (sr, relSec); }
    inline float process (float target)
    {
        const float a = target < g ? aA : aR;
        g = target + (g - target) * a;
        return g;
    }
};

// 山の検出（左右の大きいほう）・RMS の検出（2 乗の平均、時定数 tau）
struct RmsDetector
{
    float a = 0.0f, s = 0.0f;
    void set (double sr, double tauSec) { a = tauCoef (sr, tauSec); }
    void reset() { s = 0.0f; }
    inline float process (float x2) { s = flush (x2 + (s - x2) * a); return s; }   // 2 乗の平均を返す
};

// 小さい音のときの dB（log の 0 を避ける）
inline float levelDb (float lin) { return 20.0f * std::log10 (std::max (lin, 1.0e-7f)); }
inline float powerDb (float p)  { return 10.0f * std::log10 (std::max (p, 1.0e-14f)); }

// 区切りの表を直線でつなぐ（x は小さい順）
template <int N>
inline float interpTable (const float (&xs)[N], const float (&ys)[N], float x)
{
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < N; ++i)
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[N - 1];
}
} // namespace aiofx
