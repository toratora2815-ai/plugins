#pragma once
#include <cmath>
#include <algorithm>

// 目標カーブ（1/6オクターブ帯域のエネルギーで見たとき、ピンクノイズ＝フラット）
// 数値は一般的な市販曲の傾向をもとにした目安で、iZotopeのデータではありません。
struct TargetCurve
{
    const char* name;
    float tiltPerOct;   // 1kHzを基準にした傾き（dB/oct）
    float lowShelf;     // 120Hz以下の持ち上げ量（dB）
    float tolerance;    // 許容幅（±dB）
};

inline const TargetCurve kTargets[] = {
    { "Modern (Pop / Rock)",    -1.5f,  0.0f, 3.0f },
    { "Bass-Heavy (Hip-Hop / EDM)", -2.0f, 3.0f, 3.0f },
    { "Acoustic / Jazz",        -1.2f, -2.0f, 3.5f },
    { "Cinematic / Orchestral", -2.5f,  0.0f, 4.0f },
};
inline constexpr int kNumTargets = 4;

inline float targetDb (const TargetCurve& t, float f)
{
    const float shelf = t.lowShelf / (1.0f + std::pow (f / 120.0f, 4.0f));
    return t.tiltPerOct * std::log2 (f / 1000.0f) + shelf;
}

inline float targetTol (const TargetCurve& t, float f)
{
    return t.tolerance
         + 2.0f * std::max (0.0f, std::log2 (60.0f / f))
         + 1.5f * std::max (0.0f, std::log2 (f / 8000.0f));
}

// 4つの大きな帯域
struct BroadBand { const char* name; float lo, hi; };
inline const BroadBand kBroadBands[] = {
    { "LOW",      20.0f,   150.0f },
    { "LOW-MID",  150.0f,  1200.0f },
    { "HIGH-MID", 1200.0f, 6000.0f },
    { "HIGH",     6000.0f, 20000.0f },
};
