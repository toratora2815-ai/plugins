#pragma once
// つまみ → エンジンの値の表（抑える系・響き系の共通エンジンで使う）。JUCE なし
#include "Module.h"

namespace aiofx
{
// つまみ 1 本の表。連続のつまみは測った点（0, 0.25, 0.5, 0.75, 1 と初期値）、選ぶつまみは選択肢の数だけ
struct DynKnobTable
{
    int points = 0;              // 表の点の数（0 = このつまみは何も変えない）
    bool steps = false;          // true = 選ぶつまみ（点の間をつながない。値 = 番号 / (点の数 − 1)）
    int nParams = 0;
    static constexpr int kMaxParams = 20, kMaxPoints = 14;   // 0.3.0 で 10 → 20（響き系の MODE は多くの値を変える）
    int params[kMaxParams] {};   // 変えるエンジンの値（DynP）
    float xs[kMaxPoints] {};     // 点のつまみの位置（連続のとき。小さい順）
    float vals[kMaxPoints][kMaxParams] {};   // [点][値]（絶対値。初期値の点との差を足す）
};

// 表の値を、つまみの値 v（0..1）で読む
inline float dynKnobValue (const DynKnobTable& t, int j, float v)
{
    if (t.points <= 1) return t.vals[0][j];
    v = std::clamp (v, 0.0f, 1.0f);
    if (t.steps)
    {
        const int i = std::clamp ((int) std::lround (v * (float) (t.points - 1)), 0, t.points - 1);
        return t.vals[i][j];
    }
    if (v <= t.xs[0]) return t.vals[0][j];
    for (int i = 1; i < t.points; ++i)
        if (v <= t.xs[i])
        {
            const float f = (v - t.xs[i - 1]) / std::max (1.0e-6f, t.xs[i] - t.xs[i - 1]);
            return t.vals[i - 1][j] + (t.vals[i][j] - t.vals[i - 1][j]) * f;
        }
    return t.vals[t.points - 1][j];
}

using KnobTable = DynKnobTable;
inline float knobValue (const KnobTable& t, int j, float v) { return dynKnobValue (t, j, v); }
} // namespace aiofx
