#pragma once
// スペクトラム解析（直近 約1秒の平均パワー）。無音のあいだは前の形を保つ
#include <JuceHeader.h>

class SpectrumAnalyser
{
public:
    static constexpr int order = 12;
    static constexpr int size = 1 << order;   // 4096
    static constexpr int hop = size / 2;
    static constexpr int numBins = size / 2 + 1;

    SpectrumAnalyser()
    {
        window.resize (size);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
        ring.assign (size, 0.0f);
        fftData.assign (size * 2, 0.0f);
        power.assign (numBins, 0.0);
    }

    void prepare (double sampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sr = sampleRate;
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (power.begin(), power.end(), 0.0);
        pos = sinceFFT = 0;
        hasData = false;
    }

    void push (const float* L, const float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) pos] = 0.5f * (L[i] + R[i]);
            pos = (pos + 1) & (size - 1);
            if (++sinceFFT >= hop) { sinceFFT = 0; analyse(); }
        }
    }

    // UI 用。データがあれば true
    bool getSnapshot (std::vector<double>& out, double& sampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        out = power;
        sampleRate = sr;
        return hasData;
    }

private:
    void analyse() noexcept
    {
        double ms = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float x = ring[(size_t) ((pos + i) & (size - 1))];
            ms += (double) x * x;
            fftData[(size_t) i] = x * window[(size_t) i];
        }
        ms /= size;
        if (ms < 1.0e-8) return;   // 約 -80dBFS 未満は無視（形を保つ）

        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;
        const double a = hasData ? std::exp (-(double) hop / (sr * 1.0)) : 0.0;
        for (int b = 0; b < numBins; ++b)
        {
            const double p = (double) fftData[(size_t) b] * fftData[(size_t) b];
            power[(size_t) b] = power[(size_t) b] * a + p * (1.0 - a);
        }
        hasData = true;
    }

    juce::dsp::FFT fft { order };
    std::vector<float> window, ring, fftData;
    std::vector<double> power;
    int pos = 0, sinceFFT = 0;
    double sr = 48000.0;
    bool hasData = false;
    juce::SpinLock lock;
};
