#pragma once
#include <cmath>
#include <algorithm>

// RBJ cookbook biquad (Transposed Direct Form II), up to 2 channels.
struct Biquad
{
    enum Type { LowPass, HighPass, Peak, LowShelf, HighShelf, BandPass };

    // double: at 4x of a 192 kHz project a 20 Hz high-pass pole sits at 0.9998 (float rounds it onto 1.0)
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    void setBypass() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void set (Type type, double fs, double f0, double q = 0.7071, double gainDb = 0.0) noexcept
    {
        f0 = std::clamp (f0, 10.0, fs * 0.45);
        q  = std::max (q, 0.05);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * 3.14159265358979323846 * f0 / fs;
        const double c  = std::cos (w), s = std::sin (w);
        const double al = s / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

        switch (type)
        {
            case LowPass:
                B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2;
                A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case HighPass:
                B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2;
                A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case BandPass:
                B0 = al; B1 = 0; B2 = -al;
                A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case Peak:
                B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A;
                A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            case LowShelf:
            {
                const double sq = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * c + sq);
                B1 = 2 * A * ((A - 1) - (A + 1) * c);
                B2 = A * ((A + 1) - (A - 1) * c - sq);
                A0 = (A + 1) + (A - 1) * c + sq;
                A1 = -2 * ((A - 1) + (A + 1) * c);
                A2 = (A + 1) + (A - 1) * c - sq; break;
            }
            case HighShelf:
            {
                const double sq = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * c + sq);
                B1 = -2 * A * ((A - 1) + (A + 1) * c);
                B2 = A * ((A + 1) + (A - 1) * c - sq);
                A0 = (A + 1) - (A - 1) * c + sq;
                A1 = 2 * ((A - 1) - (A + 1) * c);
                A2 = (A + 1) - (A - 1) * c - sq; break;
            }
        }

        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0;
        a1 = A1 / A0; a2 = A2 / A0;
    }

    inline float process (float xIn, int ch) noexcept
    {
        const double x = xIn;
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
};

// Simple one-pole DC blocker
struct DCBlocker
{
    float x1[2] {}, y1[2] {};
    float r = 0.995f;
    void prepare (double fs) { r = (float) std::exp (-2.0 * 3.14159265358979323846 * 15.0 / fs); }
    void reset() { x1[0] = x1[1] = y1[0] = y1[1] = 0; }
    inline float process (float x, int ch) noexcept
    {
        const float y = x - x1[ch] + r * y1[ch];
        x1[ch] = x; y1[ch] = y;
        return y;
    }
};
