#pragma once
// 歌った音程の集まりから、キー（メジャー／マイナー）とちょうどいいオクターブを当てる。JUCE なし。
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hum
{

struct KeyGuess
{
    bool ok = false;
    int key = 0;          // 0 = C
    bool minor = false;
    float confidence = 0; // 相関（-1..1）。0.6 以上ならかなり確か
    int octave = 0;       // 出す音の真ん中が C3〜C4 あたりに来る OCTAVE（-2..+2）
    float median = 0;     // 歌った高さの真ん中（MIDI 番号）
};

// pitches = 声のあったフレームの音程（MIDI 番号の小数）。フレームは等間隔なので、数がそのまま長さの重み
inline KeyGuess guessKey (const std::vector<float>& pitches, float targetCenter = 66.0f)
{
    KeyGuess g;
    if (pitches.size() < 200) return g;   // 約 0.5 秒ぶん未満は当てにならない

    // Krumhansl–Kessler の調性プロファイル（どの音がどれくらい「そのキーらしい」か）
    static const double majorP[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    std::array<double, 12> h {};
    for (float m : pitches) h[(size_t) ((((int) std::lround (m)) % 12 + 12) % 12)] += 1.0;

    auto corr = [&] (const double* prof, int key)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[(size_t) i]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = h[(size_t) ((i + key) % 12)] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2;
    for (int k = 0; k < 12; ++k)
    {
        const double a = corr (majorP, k), b = corr (minorP, k);
        if (a > best) { best = a; g.key = k; g.minor = false; }
        if (b > best) { best = b; g.key = k; g.minor = true; }
    }
    g.confidence = (float) best;

    std::vector<float> v (pitches);
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    g.median = v[v.size() / 2];
    int bestOct = 0; float bestD = 1e9f;
    for (int o = -2; o <= 2; ++o)
    {
        const float d = std::abs (g.median + 12.0f * (float) o - targetCenter);
        if (d < bestD) { bestD = d; bestOct = o; }
    }
    g.octave = bestOct;
    g.ok = true;
    return g;
}

} // namespace hum
