#pragma once
// VOLUME PEDAL … 足で踏むボリューム（Guitar Rig の Volume Pedal を測って合わせ込み、2026-11-19。測った値と 0.01 dB で同じ）
//   PEDAL: 踏み込み 0..1。音量（dB）は 3 点を通る 2 次式（放物線）:
//     一番下げ = MIN（−80 + 80 × MIN dB）、半分 = HALF（−40 + 40 × HALF dB）、一番上げ = 0 dB
//     （初期値 MIN 0.5・HALF 0.5 で −40 → −20 → 0 dB の直線）
//   VOLUME: ±20 dB（初期値 0.425 = −3 dB）
// 踏み込みは 15 ms でなめらかに（オートメーション・エクスプレッションペダルで動かしてもプチッとしない）
#include "Module.h"

namespace aiofx
{
struct VolumePedal final : Module
{
    static float minDb (float v)  { return -80.0f + 80.0f * v; }
    static float halfDb (float v) { return -40.0f + 40.0f * v; }
    static float pedalDb (float p, float mn, float hf)
    {
        // (0, MIN)・(0.5, HALF)・(1, 0 dB) を通る放物線
        const float m = minDb (mn), h = halfDb (hf);
        return m * (p - 0.5f) * (p - 1.0f) * 2.0f + h * p * (p - 1.0f) * -4.0f;
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "volpedal", "VOLUME PEDAL", "DYNAMICS",
            "Foot volume pedal. Automate PEDAL (or map an expression pedal) for swells. MIN = level at heel down, HALF = level half way.",
            {
                { "PEDAL",  1.0f,   0, nullptr, [] (float v, const float* all) { return fmt (pedalDb (v, all[2], all[3]), 1, " dB"); } },
                { "VOLUME", 0.425f, 0, [] (float v) { return fmt (-20.0f + 40.0f * v, 1, " dB"); } },
                { "MIN",    0.5f,   0, [] (float v) { return fmt (minDb (v), 1, " dB"); } },
                { "HALF",   0.5f,   0, [] (float v) { return fmt (halfDb (v), 1, " dB"); } },
            } };
        return i;
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; g.prepare (sr, 0.015);
        g.set (target(), true);
    }
    void reset() override { g.set (target(), true); }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 4 || same (k[i], v)) return;
        k[i] = v; g.set (target());
    }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s) { const float m = g.next(); L[s] *= m; R[s] *= m; }
    }
    float meter() const override { return std::clamp (1.0f - g.cur, 0.0f, 1.0f); }

private:
    float target() const { return dbToGain (pedalDb (k[0], k[2], k[3]) + (-20.0f + 40.0f * k[1])); }
    double sr = 48000.0;
    float k[4] { 1.0f, 0.425f, 0.5f, 0.5f };
    Smooth g;
};

} // namespace aiofx
