#pragma once
// 画面の 2 つの大きな表示を作る（裏のスレッドで、実際のエンジンにパルスを通して描く）
//   反射の表示：12 個の粒の波形（上 = 左、下 = 右）。色は高音の多さ（明るい = きらきら、暗い = こもる）
//   残響の表示：時間 × 周波数の色の地図（明るいほど大きい）
#include <JuceHeader.h>
#include "RoomEngine.h"
#include "ProfileTable.h"

namespace lumen
{
struct VisualParams
{
    ReflParams refl;
    TailParams tail;
    MixParams mix;
    const float* revColor = nullptr;
    const float* erColor = nullptr;
    std::vector<float> key;   // 変化を見分けるための値の並び（画面側で作る）
    bool operator== (const VisualParams& o) const { return key == o.key && revColor == o.revColor && erColor == o.erColor; }
};

// 色の並び：深い紺 → 青緑 → 琥珀 → 白
inline juce::Colour heatColour (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    struct Stop { float p; juce::uint8 r, g, b; };
    static const Stop s[] = { { 0.00f, 11, 14, 20 }, { 0.22f, 18, 44, 66 }, { 0.45f, 32, 118, 128 },
                              { 0.68f, 216, 150, 74 }, { 0.86f, 246, 206, 140 }, { 1.00f, 255, 248, 232 } };
    for (int i = 0; i < 5; ++i)
        if (v <= s[i + 1].p)
        {
            const float f = (v - s[i].p) / (s[i + 1].p - s[i].p);
            auto mixc = [f] (juce::uint8 a, juce::uint8 b) { return (juce::uint8) juce::roundToInt (a + f * (b - a)); };
            return juce::Colour (mixc (s[i].r, s[i + 1].r), mixc (s[i].g, s[i + 1].g), mixc (s[i].b, s[i + 1].b));
        }
    return juce::Colour (255, 248, 232);
}

class VisualWorker : private juce::Thread
{
public:
    struct Result
    {
        // 反射：0.25 ms ごとの振幅（dB）と明るさ（0..1）
        std::vector<float> erL, erR, erBrightL, erBrightR;
        float erMs = 200.0f;
        std::vector<float> onsetMs; std::vector<bool> onsetCross;
        // 残響：時間 × 周波数の画像
        juce::Image map;
        float mapSeconds = 3.0f, rt = 2.25f;
        std::vector<float> rtCurve;   // 周波数ごとの残響時間（表示用、mapFreqs と同じ並び）
        int serial = 0;
    };
    static constexpr int kRows = 96;
    static float rowFreq (int r) { return 25.0f * std::pow (800.0f, (float) r / (float) (kRows - 1)); }   // 25 Hz .. 20 kHz

    VisualWorker() : juce::Thread ("Lumen visuals") { startThread (juce::Thread::Priority::low); }
    ~VisualWorker() override { stopThread (2000); }

    void request (const VisualParams& p)
    {
        const juce::ScopedLock sl (lock);
        if (havePending && pending == p) return;
        if (haveDone && done == p) return;
        pending = p; havePending = true;
        notify();
    }
    bool fetch (Result& out)
    {
        const juce::ScopedLock sl (lock);
        if (result.serial == lastFetched) return false;
        out = result; lastFetched = result.serial;
        return true;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (200);
            VisualParams p;
            {
                const juce::ScopedLock sl (lock);
                if (! havePending) continue;
                p = pending; havePending = false;
            }
            Result r = compute (p);
            if (threadShouldExit()) return;
            const juce::ScopedLock sl (lock);
            r.serial = ++serial;
            result = std::move (r);
            done = p; haveDone = true;
        }
    }

    Result compute (const VisualParams& vp)
    {
        constexpr double sr = 48000.0;
        Result r;

        // ── 反射 ──
        std::vector<float> own, cross;
        buildReflections (vp.refl, sr, own, cross);
        {
            const int n = (int) own.size();
            RoomEngine e; e.prepare (sr, 512);
            auto m = vp.mix; m.dryWet = 1.0f; m.reflRev = 0.0f; m.masterDb = 0; m.reflTrimDb = 0; m.eqOn = false;
            e.setTail (vp.tail); e.setMix (m); e.setProfiles (nullptr, vp.erColor);
            std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
            for (int s = 0; s < n; s += 512)
            {
                const int k = std::min (512, n - s);
                e.process (L.data() + s, R.data() + s, own.data() + s, cross.data() + s, k);
            }
            const int bin = (int) (sr * 0.00025);
            float lastT = 0;
            for (int i = n - 1; i > 0; --i) if (std::abs (L[(size_t) i]) + std::abs (R[(size_t) i]) > 1e-4f) { lastT = (float) i / (float) sr * 1000.0f; break; }
            r.erMs = juce::jlimit (60.0f, 800.0f, std::ceil ((lastT + 10.0f) / 50.0f) * 50.0f);
            const int bins = (int) (r.erMs * 4.0f);
            for (auto* ch : { &L, &R })
            {
                std::vector<float> amp ((size_t) bins, -120.0f), bright ((size_t) bins, 0.0f);
                for (int b = 0; b < bins; ++b)
                {
                    double e2 = 0, d2 = 0;
                    for (int i = b * bin; i < (b + 1) * bin && i < n; ++i)
                    {
                        const float x = (*ch)[(size_t) i], dx = i > 0 ? x - (*ch)[(size_t) i - 1] : x;
                        e2 += x * x; d2 += dx * dx;
                    }
                    amp[(size_t) b] = 10.0f * std::log10 ((float) (e2 / bin) + 1e-14f);
                    bright[(size_t) b] = e2 > 1e-14 ? (float) std::sqrt (d2 / (4.0 * e2)) : 0.0f;
                }
                if (ch == &L) { r.erL = amp; r.erBrightL = bright; } else { r.erR = amp; r.erBrightR = bright; }
            }
            const float* t = patternTimes (vp.refl.pattern);
            const float a = sizeScale (vp.refl.size);
            for (int k = 0; k < 12; ++k)
            {
                const bool c = (k & 1) != 0;
                r.onsetMs.push_back (4.0f + a * (t[k] - 4.0f) + (c ? vp.refl.xfeedMs : 0.0f));
                r.onsetCross.push_back (c);
            }
        }

        // ── 残響 ──
        {
            const bool inf = vp.tail.timeSec >= 60.0f;
            r.rt = inf ? 99.0f : vp.tail.timeSec;
            const float pre = vp.tail.predelayMs * 0.001f;
            r.mapSeconds = juce::jlimit (0.4f, 10.0f, inf ? 6.0f : pre + vp.tail.timeSec * 1.1f + 0.05f);
            const int n = (int) (r.mapSeconds * sr);
            RoomEngine e; e.prepare (sr, 512);
            auto m = vp.mix; m.dryWet = 1.0f; m.reflRev = 1.0f; m.masterDb = 0; m.revTrimDb = 0; m.eqOn = false;
            auto tp = vp.tail; tp.modDepth = 0.0f;
            if (inf) tp.timeSec = 30.0f;   // 表示では「とても長い」として描く
            e.setTail (tp); e.setMix (m); e.setProfiles (vp.revColor, nullptr);
            std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f), z ((size_t) 512, 0.0f);
            L[0] = R[0] = 1.0f;
            for (int s = 0; s < n && ! threadShouldExit(); s += 512)
            {
                const int k = std::min (512, n - s);
                e.process (L.data() + s, R.data() + s, z.data(), z.data(), k);
            }

            const int order = 11, fftN = 1 << order;
            juce::dsp::FFT fft (order);
            juce::dsp::WindowingFunction<float> win ((size_t) fftN, juce::dsp::WindowingFunction<float>::hann);
            const int cols = 240;
            const float hop = (float) (n - fftN) / (float) (cols - 1);
            std::vector<float> buf ((size_t) fftN * 2);
            std::vector<float> db ((size_t) (cols * kRows), -200.0f);
            float peak = -200.0f;
            for (int c = 0; c < cols && ! threadShouldExit(); ++c)
            {
                const int start = std::max (0, (int) ((float) c * hop));
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < fftN && start + i < n; ++i) buf[(size_t) i] = 0.5f * (L[(size_t) (start + i)] + R[(size_t) (start + i)]);
                win.multiplyWithWindowingTable (buf.data(), (size_t) fftN);
                fft.performFrequencyOnlyForwardTransform (buf.data());
                for (int row = 0; row < kRows; ++row)
                {
                    const float f0 = rowFreq (row) / std::pow (800.0f, 0.5f / (kRows - 1)), f1 = rowFreq (row) * std::pow (800.0f, 0.5f / (kRows - 1));
                    const int b0 = std::max (1, (int) (f0 / sr * fftN)), b1 = std::max (b0, (int) (f1 / sr * fftN));
                    double s = 0; for (int b = b0; b <= b1 && b < fftN / 2; ++b) s += (double) buf[(size_t) b] * buf[(size_t) b];
                    // 帯域の幅で割って「1 Hz あたり」にそろえ、さらに 1 オクターブあたりで見やすく（ピンク寄り）補正
                    const float v = 10.0f * std::log10 ((float) (s / (b1 - b0 + 1)) + 1e-20f) + 3.0f * std::log2 (rowFreq (row) / 1000.0f);
                    db[(size_t) (c * kRows + row)] = v;
                    peak = std::max (peak, v);
                }
            }
            juce::Image img (juce::Image::ARGB, cols, kRows, false);
            for (int c = 0; c < cols; ++c)
                for (int row = 0; row < kRows; ++row)
                {
                    const float v = (db[(size_t) (c * kRows + row)] - peak + 66.0f) / 66.0f;
                    img.setPixelAt (c, kRows - 1 - row, heatColour (std::pow (juce::jlimit (0.0f, 1.0f, v), 1.15f)));
                }
            r.map = img;

            // 周波数ごとの残響時間（-20 dB と -50 dB の通過時刻から）
            r.rtCurve.assign ((size_t) kRows, 0.0f);
            for (int row = 0; row < kRows; ++row)
            {
                float mx = -200; int mi = 0;
                for (int c = 0; c < cols; ++c) if (db[(size_t) (c * kRows + row)] > mx) { mx = db[(size_t) (c * kRows + row)]; mi = c; }
                int c20 = -1, c50 = -1;
                for (int c = mi; c < cols; ++c)
                {
                    const float v = db[(size_t) (c * kRows + row)];
                    if (c20 < 0 && v < mx - 15.0f) c20 = c;
                    if (c50 < 0 && v < mx - 45.0f) { c50 = c; break; }
                }
                r.rtCurve[(size_t) row] = (c20 >= 0 && c50 > c20) ? 2.0f * (float) (c50 - c20) * hop / (float) sr : (inf ? 99.0f : 0.0f);
            }
        }
        return r;
    }

    juce::CriticalSection lock;
    VisualParams pending, done;
    bool havePending = false, haveDone = false;
    Result result;
    int serial = 0, lastFetched = 0;
};
} // namespace lumen
