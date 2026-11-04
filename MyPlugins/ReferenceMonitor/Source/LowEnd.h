#pragma once
// LOW 表示用: 低域（20〜250 Hz）だけを細かく測るスペクトラム。
// 400 Hz より上を切ってから 約 6 kHz に間引き、4096 点の FFT で 約 1.5 Hz きざみに見る（直近 約 2 秒の平均）。
// ミッド（L+R）とサイド（L−R）の両方を測る。無音のあいだは前の形を保つ
#include <JuceHeader.h>
#include "ListenSim.h"

class LowAnalyser
{
public:
    static constexpr int order = 12;
    static constexpr int size = 1 << order;   // 4096
    static constexpr int hop = size / 4;
    static constexpr int numBins = size / 2 + 1;
    static constexpr int kStages = 4;          // 4 段 = 48 dB/oct（間引きの前に高い音を切る）

    // 表示の周波数（20〜250 Hz を 1/24 オクターブごと）
    static const std::vector<float>& centres()
    {
        static const std::vector<float> c = []
        {
            std::vector<float> v;
            for (float f = 20.0f; f <= 251.0f; f *= std::pow (2.0f, 1.0f / 24.0f)) v.push_back (f);
            return v;
        }();
        return c;
    }

    LowAnalyser()
    {
        window.resize (size);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
        ring.assign (size, 0.0f);
        sideRing.assign (size, 0.0f);
        fftData.assign (size * 2, 0.0f);
        sideData.assign (size * 2, 0.0f);
        power.assign (numBins, 0.0);
        sidePower.assign (numBins, 0.0);
    }

    void prepare (double sampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        decim = juce::jmax (1, juce::roundToInt (sampleRate / 6000.0));
        lowRate = sampleRate / decim;
        for (int s = 0; s < kStages; ++s)
        {
            midLp[s]  = refdsp::designBiquad (0, sampleRate, 400.0, 0.70710678, 0.0);
            sideLp[s] = midLp[s];
        }
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (sideRing.begin(), sideRing.end(), 0.0f);
        std::fill (power.begin(), power.end(), 0.0);
        std::fill (sidePower.begin(), sidePower.end(), 0.0);
        pos = sinceFFT = decimCount = 0;
        hasData = false;
    }

    void requestReset() noexcept { resetFlag = true; }

    void push (const float* L, const float* R, int n) noexcept
    {
        if (resetFlag.exchange (false))
        {
            const juce::SpinLock::ScopedTryLockType sl (lock);
            if (sl.isLocked())
            {
                std::fill (power.begin(), power.end(), 0.0);
                std::fill (sidePower.begin(), sidePower.end(), 0.0);
                hasData = false;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            double m = 0.5 * ((double) L[i] + (double) R[i]);
            double s = 0.5 * ((double) L[i] - (double) R[i]);
            for (int k = 0; k < kStages; ++k) { m = midLp[k].process (m); s = sideLp[k].process (s); }
            if (++decimCount < decim) continue;
            decimCount = 0;
            ring[(size_t) pos] = (float) m;
            sideRing[(size_t) pos] = (float) s;
            pos = (pos + 1) & (size - 1);
            if (++sinceFFT >= hop) { sinceFFT = 0; analyse(); }
        }
    }

    // UI 用。データがあれば true。sampleRate には間引いたあとのレートが入る
    bool getSnapshot (std::vector<double>& mid, std::vector<double>& side, double& sampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        mid = power;
        side = sidePower;
        sampleRate = lowRate;
        return hasData;
    }

    // パワーを centres() ごとの dB に（TONE の表示と同じ目盛り: 1/24 オクターブの中のエネルギー）
    static std::vector<float> toBands (const std::vector<double>& p, double rate)
    {
        const auto& c = centres();
        std::vector<float> out (c.size(), -300.0f);
        if ((int) p.size() < 4) return out;
        const double bw = rate / size;
        const int maxBin = (int) p.size() - 1;
        const double norm = 20.0 * std::log10 (size / 4.0);
        for (size_t k = 0; k < c.size(); ++k)
        {
            const double fc = c[k];
            const double lo = fc * std::pow (2.0, -1.0 / 48.0), hi = fc * std::pow (2.0, 1.0 / 48.0);
            const int b0 = (int) std::ceil (lo / bw), b1 = std::min (maxBin, (int) std::floor (hi / bw));
            double density = 0.0;
            if (b1 >= b0 && b0 >= 1)
            {
                for (int b = b0; b <= b1; ++b) density += p[(size_t) b];
                density /= (b1 - b0 + 1);
            }
            else
            {
                const double x = fc / bw;
                const int i0 = juce::jlimit (1, maxBin - 1, (int) x);
                const double fr = juce::jlimit (0.0, 1.0, x - i0);
                density = p[(size_t) i0] * (1.0 - fr) + p[(size_t) i0 + 1] * fr;
            }
            out[k] = (float) (10.0 * std::log10 (density * (hi - lo) / bw + 1.0e-30) - norm);
        }
        return out;
    }

    // f0〜f1 Hz の中で、サイドの割合 S / (M + S)（パワー）。音がほとんどないときは -1
    static float sideRatio (const std::vector<double>& mid, const std::vector<double>& side, double rate, double f0, double f1)
    {
        const int n = (int) juce::jmin (mid.size(), side.size());
        if (n < 4) return -1.0f;
        const double bw = rate / size;
        double m = 0.0, s = 0.0;
        for (int i = juce::jmax (1, (int) std::ceil (f0 / bw)); i <= juce::jmin (n - 1, (int) std::floor (f1 / bw)); ++i)
        { m += mid[(size_t) i]; s += side[(size_t) i]; }
        const double norm = (size / 4.0) * (size / 4.0);
        if ((m + s) / norm < 1.0e-9) return -1.0f;   // 約 -90 dB 未満
        return (float) (s / (m + s));
    }

private:
    void analyse() noexcept
    {
        double ms = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float x = ring[(size_t) ((pos + i) & (size - 1))];
            const float y = sideRing[(size_t) ((pos + i) & (size - 1))];
            ms += (double) x * x + (double) y * y;
            fftData[(size_t) i] = x * window[(size_t) i];
            sideData[(size_t) i] = y * window[(size_t) i];
        }
        ms /= size;
        if (ms < 1.0e-9) return;   // 低域に音がない（約 -90dBFS 未満）ときは形を保つ

        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        std::fill (sideData.begin() + size, sideData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        fft.performFrequencyOnlyForwardTransform (sideData.data(), true);

        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;
        const double a = hasData ? std::exp (-(double) hop / (lowRate * 2.0)) : 0.0;
        for (int b = 0; b < numBins; ++b)
        {
            const double p = (double) fftData[(size_t) b] * fftData[(size_t) b];
            const double q = (double) sideData[(size_t) b] * sideData[(size_t) b];
            power[(size_t) b] = power[(size_t) b] * a + p * (1.0 - a);
            sidePower[(size_t) b] = sidePower[(size_t) b] * a + q * (1.0 - a);
        }
        hasData = true;
    }

    juce::dsp::FFT fft { order };
    refdsp::Biquad midLp[kStages], sideLp[kStages];
    std::vector<float> window, ring, sideRing, fftData, sideData;
    std::vector<double> power, sidePower;
    int pos = 0, sinceFFT = 0, decim = 8, decimCount = 0;
    double lowRate = 6000.0;
    bool hasData = false;
    std::atomic<bool> resetFlag { false };
    juce::SpinLock lock;
};
