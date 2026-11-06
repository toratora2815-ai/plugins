// 前後比較の解析: Mod Host に入る音（中のプラグインの遅れぶん遅らせた元の音）と出る音を、
// ミッド（L+R）とサイド（L−R）に分けて FFT。帯域（LOW / MID / HIGH）ごとの音量差と広がり（サイドの割合）の差を出す。
// 音のスレッドは push() だけ（リングに書くだけ）。計算は画面のタイマーから update()。
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace mh
{
class Analyzer
{
public:
    static constexpr int order = 12, size = 1 << order;   // 4096 点
    static constexpr int ring = 1 << 14;
    static constexpr int numPoints = 220;                  // グラフの点（20 Hz〜20 kHz の対数）
    static constexpr int numBands = 3;

    struct Band
    {
        float inDb = -120.0f, outDb = -120.0f;      // 帯域の音量（ミッド＋サイド）
        float inWidth = 0.0f, outWidth = 0.0f;      // サイドの割合 0〜1
    };

    Analyzer()
    {
        for (auto& r : rings) r.fill (0.0f);
        for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
        for (auto& s : specIn) s.fill (-120.0f);
        for (auto& s : specOut) s.fill (-120.0f);
    }

    void setSampleRate (double sr) { rate = sr; }

    // 音のスレッドから
    inline void push (float inL, float inR, float outL, float outR) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        rings[0][(size_t) w] = 0.5f * (inL + inR);
        rings[1][(size_t) w] = 0.5f * (inL - inR);
        rings[2][(size_t) w] = 0.5f * (outL + outR);
        rings[3][(size_t) w] = 0.5f * (outL - outR);
        writePos.store ((w + 1) & (ring - 1), std::memory_order_release);
        pushed.fetch_add (1, std::memory_order_relaxed);
    }

    // 画面のスレッドから。f1 / f2 = 帯域の分け目
    void update (float f1, float f2)
    {
        const int now = pushed.load();
        if (now == lastPushed) return;    // 音が流れていない（止まっている）→ 前のまま
        lastPushed = now;

        const int end = writePos.load (std::memory_order_acquire);
        std::array<std::vector<float>, 4> power;
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < size; ++i)
                fftData[(size_t) i] = rings[(size_t) k][(size_t) ((end - size + i) & (ring - 1))] * window[(size_t) i];
            std::fill (fftData.begin() + size, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            power[(size_t) k].resize (size / 2);
            const float norm = 4.0f / size;   // 0 dB のサイン → 1.0
            for (int b = 0; b < size / 2; ++b) { const float m = fftData[(size_t) b] * norm; power[(size_t) k][(size_t) b] = m * m; }
        }

        const double binHz = rate / size;
        // グラフの点（ミッド＋サイド）
        for (int p = 0; p < numPoints; ++p)
        {
            const double f = 20.0 * std::pow (1000.0, (double) p / (numPoints - 1));
            int b0 = (int) std::floor (f * 0.944 / binHz), b1 = (int) std::ceil (f * 1.059 / binHz);
            b0 = juce::jlimit (1, size / 2 - 1, b0); b1 = juce::jlimit (b0, size / 2 - 1, b1);
            double ei = 0, eo = 0;
            for (int b = b0; b <= b1; ++b)
            {
                ei += power[0][(size_t) b] + power[1][(size_t) b];
                eo += power[2][(size_t) b] + power[3][(size_t) b];
            }
            ei /= (b1 - b0 + 1); eo /= (b1 - b0 + 1);
            auto smooth = [] (float& s, double e) { const float d = 10.0f * (float) std::log10 (e + 1.0e-14); s += (d - s) * 0.35f; };
            smooth (specIn[0][(size_t) p], ei);
            smooth (specOut[0][(size_t) p], eo);
        }

        // 帯域ごと（約 1 秒でならす）
        const double edges[numBands + 1] = { 20.0, f1, f2, juce::jmin (20000.0, rate * 0.5) };
        double sumIn = 0;
        for (int b = 1; b < size / 2; ++b) sumIn += power[0][(size_t) b] + power[1][(size_t) b];
        const bool hasSound = sumIn > 1.0e-9;   // 入口がほぼ無音のときは帯域の数字を動かさない
        for (int k = 0; k < numBands; ++k)
        {
            const int b0 = juce::jlimit (1, size / 2 - 1, (int) std::ceil (edges[k] / binHz));
            const int b1 = juce::jlimit (b0, size / 2 - 1, (int) std::floor (edges[k + 1] / binHz));
            double mi = 0, si = 0, mo = 0, so = 0;
            for (int b = b0; b <= b1; ++b)
            {
                mi += power[0][(size_t) b]; si += power[1][(size_t) b];
                mo += power[2][(size_t) b]; so += power[3][(size_t) b];
            }
            if (! hasSound) continue;
            const float a = 0.06f;
            auto& B = bandState[(size_t) k];
            B.mi += (mi - B.mi) * a; B.si += (si - B.si) * a; B.mo += (mo - B.mo) * a; B.so += (so - B.so) * a;
            auto& out = bands[(size_t) k];
            out.inDb = 10.0f * (float) std::log10 (B.mi + B.si + 1.0e-14);
            out.outDb = 10.0f * (float) std::log10 (B.mo + B.so + 1.0e-14);
            out.inWidth = (float) (B.si / (B.mi + B.si + 1.0e-14));
            out.outWidth = (float) (B.so / (B.mo + B.so + 1.0e-14));
        }
    }

    const std::array<float, numPoints>& inCurve() const { return specIn[0]; }
    const std::array<float, numPoints>& outCurve() const { return specOut[0]; }
    const Band& band (int k) const { return bands[(size_t) juce::jlimit (0, numBands - 1, k)]; }
    static float pointHz (int p) { return 20.0f * std::pow (1000.0f, (float) p / (numPoints - 1)); }

private:
    double rate = 48000.0;
    std::array<std::array<float, ring>, 4> rings;
    std::atomic<int> writePos { 0 }, pushed { 0 };
    int lastPushed = -1;

    juce::dsp::FFT fft { order };
    std::array<float, size> window {};
    std::array<float, size * 2> fftData {};
    std::array<std::array<float, numPoints>, 1> specIn {}, specOut {};

    struct BandAcc { double mi = 0, si = 0, mo = 0, so = 0; };
    std::array<BandAcc, numBands> bandState {};
    std::array<Band, numBands> bands {};
};
} // namespace mh
