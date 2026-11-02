#pragma once
#include <JuceHeader.h>

// SHAPE と CREATE の両方で使う小さな部品
namespace hs
{
    inline float coefFor (double ms, double sr)
    {
        return ms <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr));
    }

    // RBJ 型のバイクアッド。音声スレッドでメモリ確保しないよう自前で持つ
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0; }

        void setHighShelf (double sr, double freq, double gainDb)
        {
            freq = juce::jlimit (20.0, sr * 0.45, freq);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
            const double cs = std::cos (w0), sn = std::sin (w0);
            const double alpha = sn / std::sqrt (2.0);
            const double sA = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1) - (A - 1) * cs + sA;
            b0 = (float) (A * ((A + 1) + (A - 1) * cs + sA) / a0);
            b1 = (float) (-2 * A * ((A - 1) + (A + 1) * cs) / a0);
            b2 = (float) (A * ((A + 1) + (A - 1) * cs - sA) / a0);
            a1 = (float) (2 * ((A - 1) - (A + 1) * cs) / a0);
            a2 = (float) (((A + 1) - (A - 1) * cs - sA) / a0);
        }

        // 中心で 0 dB になるバンドパス
        void setBandPass (double sr, double freq, double q)
        {
            freq = juce::jlimit (20.0, sr * 0.45, freq);
            const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (float) (alpha / a0);
            b1 = 0.0f;
            b2 = (float) (-alpha / a0);
            a1 = (float) (-2.0 * std::cos (w0) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct OnePoleHP
    {
        float c = 0, xPrev = 0, yPrev = 0;
        void setCutoff (double f, double sr) { c = (float) std::exp (-juce::MathConstants<double>::twoPi * f / sr); }
        void reset() { xPrev = yPrev = 0; }
        float process (float x) { const float y = c * (yPrev + x - xPrev); xPrev = x; yPrev = y; return y; }
    };

    struct OnePoleLP
    {
        float c = 0, y = 0;
        void setCutoff (double f, double sr) { c = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * f / sr); }
        void reset() { y = 0; }
        float process (float x) { y += c * (x - y); return y; }
    };

    struct SimpleDelay
    {
        std::vector<float> buf;
        int pos = 0;
        void setLength (int n) { buf.assign ((size_t) juce::jmax (1, n), 0.0f); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        float push (float x)
        {
            const float y = buf[(size_t) pos];
            buf[(size_t) pos] = x;
            if (++pos >= (int) buf.size()) pos = 0;
            return y;
        }
    };

    // 小数サンプルの遅れを読めるディレイ（3次エルミート補間）
    // read(d) は「これから書く1サンプル」から数えて d サンプル前の値（d >= 2）
    struct FracDelay
    {
        std::vector<float> buf;
        int mask = 0, w = 0;

        void setMaxSamples (int n)
        {
            int size = 1;
            while (size < n + 8) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

        float at (int ago) const { return buf[(size_t) ((w - ago) & mask)]; }

        float read (float d) const
        {
            d = juce::jlimit (2.0f, (float) (mask - 4), d);
            const int i = (int) d;
            const float t = d - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }
    };
}
