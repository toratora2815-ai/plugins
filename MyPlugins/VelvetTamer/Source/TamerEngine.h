#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

// Velvet Tamer の音の処理（JUCE を使わない素の C++。単体でテストできる）
//
// 考え方：
//   音を短い区切り（フレーム）ごとに FFT で周波数に分け、
//   「まわりの周波数よりも飛び出ている所」＝ レゾナンス（耳に痛い響き）だけを、その瞬間だけ下げる。
//   まわりとの比べっこなので、音量の大小には左右されない。
//
// 流れ（フレームごと）：
//   窓をかけて FFT → 各周波数の強さ（dB）→ 少しだけ時間方向にならす
//   → まわりの平均（なだらかな「山なみ」）を求める → 山なみより THRESHOLD 以上飛び出た分を DEPTH の割合で下げる
//   → WEIGHT（帯域ごとの効きの強弱）を掛ける → FOCUS（LOW〜HIGH）の外は効かせない → 隣の周波数とならす → ATTACK / RELEASE で時間方向にならす
//   → 逆 FFT して重ね合わせ → MIX / DELTA / OUTPUT
namespace vt
{
    // WEIGHT の点（周波数は固定。上下だけ動かす）
    static constexpr int numWeights = 7;
    static constexpr float weightHz[numWeights] = { 100.0f, 250.0f, 600.0f, 1500.0f, 3500.0f, 7000.0f, 14000.0f };

    struct Settings
    {
        float depth = 0.5f;          // 0..1  下げる強さ
        float sharpness = 0.5f;      // 0..1  1 ほど細いピークだけを狙う
        float selectivity = 0.5f;    // 0..1  1 ほど目立つピークだけを狙う
        float attackMs = 5.0f, releaseMs = 80.0f;
        float lowHz = 300.0f, highHz = 16000.0f;
        float mix = 1.0f, outDb = 0.0f;
        bool delta = false;          // 削った音だけを聴く
        bool link = true;            // 左右で同じだけ下げる
        float weight[numWeights] { 0, 0, 0, 0, 0, 0, 0 };   // -1..+1  -1 でその帯域は効かない、+1 で 2 倍効く
    };

    // 2 のべき乗サイズの複素 FFT（反復型）
    class Fft
    {
    public:
        void prepare (int size)
        {
            n = size;
            tw.resize ((size_t) n / 2);
            for (int i = 0; i < n / 2; ++i)
            {
                const double a = -2.0 * M_PI * i / n;
                tw[(size_t) i] = { (float) std::cos (a), (float) std::sin (a) };
            }
            rev.resize ((size_t) n);
            int bits = 0;
            while ((1 << bits) < n) ++bits;
            for (int i = 0; i < n; ++i)
            {
                int r = 0;
                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b)) r |= 1 << (bits - 1 - b);
                rev[(size_t) i] = r;
            }
        }

        void run (std::complex<float>* x, bool inverse) const
        {
            for (int i = 0; i < n; ++i)
                if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);

            for (int len = 2; len <= n; len <<= 1)
            {
                const int half = len / 2, step = n / len;
                for (int i = 0; i < n; i += len)
                    for (int j = 0; j < half; ++j)
                    {
                        auto w = tw[(size_t) (j * step)];
                        if (inverse) w = std::conj (w);
                        const auto u = x[i + j], v = x[i + j + half] * w;
                        x[i + j] = u + v;
                        x[i + j + half] = u - v;
                    }
            }
        }

    private:
        int n = 0;
        std::vector<std::complex<float>> tw;
        std::vector<int> rev;
    };

    class TamerEngine
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int displayPoints = 256;
        static constexpr float displayLowHz = 20.0f, displayHighHz = 20000.0f;

        TamerEngine()
        {
            for (auto& v : dispSpec) v = -120.0f;
            for (auto& v : dispGr) v = 0.0f;
        }

        void prepare (double sampleRate, int numChannels)
        {
            sr = sampleRate;
            channels = std::clamp (numChannels, 1, maxChannels);
            n = sr <= 50000.0 ? 2048 : (sr <= 100000.0 ? 4096 : 8192);
            hop = n / 8;
            fifoLatency = n - hop;
            latency = n;   // 入口の待ち (n - hop) ＋ 出口の重ね合わせ (hop)
            bins = n / 2 + 1;
            fft.prepare (n);

            // sqrt-Hann を入口と出口の両方にかける（掛けると Hann）。8 倍重ねると合計 4 になる
            win.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * M_PI * i / n));
            olaScale = 1.0f / 4.0f;
            // フルスケールのサイン波が 0 dB になるように（Hann の振幅 = n/4）
            powerNorm = 1.0f / ((float) n * (float) n / 16.0f);

            for (int c = 0; c < maxChannels; ++c)
            {
                inFifo[c].assign ((size_t) n, 0.0f);
                outFifo[c].assign ((size_t) n, 0.0f);
                outAcc[c].assign ((size_t) n * 2, 0.0f);
                spec[c].assign ((size_t) n, {});
                detPow[c].assign ((size_t) bins, 0.0f);
                grDb[c].assign ((size_t) bins, 0.0f);
                dryDelay[c].assign ((size_t) latency + 1, 0.0f);
            }
            rover = fifoLatency;
            dryPos = 0;

            levelDb.assign ((size_t) bins, 0.0f);
            prefix.assign ((size_t) bins + 1, 0.0);
            tmpA.assign ((size_t) bins, 0.0f);
            tmpB.assign ((size_t) bins, 0.0f);
            focus.assign ((size_t) bins, 0.0f);
            lastLow = lastHigh = -1.0f;
            weightCurve.assign ((size_t) bins, 1.0f);
            for (auto& w : lastWeight) w = -99.0f;

            // 画面用：表示の各点がどの周波数ビンの範囲か
            for (int p = 0; p <= displayPoints; ++p)
            {
                const double f = displayLowHz * std::pow (displayHighHz / displayLowHz, (double) p / displayPoints);
                dispEdge[p] = std::clamp ((int) std::lround (f * n / sr), 1, bins - 1);
            }

            mixCur = settings.mix;
            outCur = std::pow (10.0f, settings.outDb / 20.0f);
            smoothCoef = 1.0f - (float) std::exp (-1.0 / (0.02 * sr));
        }

        int getLatency() const { return latency; }
        void setSettings (const Settings& s) { settings = s; }

        // ch[c] をその場で書き換える。numCh は 1 か 2
        void process (float* const* ch, int numCh, int numSamples)
        {
            numCh = std::clamp (numCh, 1, channels);
            const float mixTarget = std::clamp (settings.mix, 0.0f, 1.0f);
            const float outTarget = std::pow (10.0f, settings.outDb / 20.0f);

            for (int i = 0; i < numSamples; ++i)
            {
                mixCur += (mixTarget - mixCur) * smoothCoef;
                outCur += (outTarget - outCur) * smoothCoef;

                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    inFifo[c][(size_t) rover] = x;
                    const float wet = outFifo[c][(size_t) (rover - fifoLatency)];

                    auto& d = dryDelay[c];
                    const float dry = d[(size_t) dryPos];
                    d[(size_t) dryPos] = x;

                    const float mixed = dry + (wet - dry) * mixCur;
                    ch[c][i] = (settings.delta ? dry - mixed : mixed) * outCur;
                }
                if (++dryPos >= latency) dryPos = 0;

                if (++rover >= n)
                {
                    rover = fifoLatency;
                    processFrame (numCh);
                }
            }
        }

        // 画面用（メッセージスレッドから読む）
        void copyDisplay (float* specDb, float* reductionDb) const
        {
            for (int p = 0; p < displayPoints; ++p)
            {
                specDb[p] = dispSpec[p].load (std::memory_order_relaxed);
                reductionDb[p] = dispGr[p].load (std::memory_order_relaxed);
            }
        }
        float getMaxReductionDb() const { return maxReduction.load (std::memory_order_relaxed); }

    private:
        void updateFocus()
        {
            if (settings.lowHz == lastLow && settings.highHz == lastHigh) return;
            lastLow = settings.lowHz;
            lastHigh = settings.highHz;
            const float lo = std::min (lastLow, lastHigh), hi = std::max (lastLow, lastHigh);
            // 帯域の外は半オクターブかけてなめらかに効かなくなる
            for (int k = 0; k < bins; ++k)
            {
                const float f = (float) (k * sr / n);
                float w = 1.0f;
                if (f < lo) w = 1.0f - std::min (1.0f, std::log2 (lo / std::max (f, 1.0f)) * 2.0f);
                else if (f > hi) w = 1.0f - std::min (1.0f, std::log2 (f / hi) * 2.0f);
                focus[(size_t) k] = 0.5f - 0.5f * std::cos ((float) M_PI * w);
            }
        }

        // 点と点の間は、周波数の対数でなめらかにつなぐ（両端の外は端の値のまま）
        void updateWeights()
        {
            if (std::equal (std::begin (lastWeight), std::end (lastWeight), std::begin (settings.weight))) return;
            std::copy (std::begin (settings.weight), std::end (settings.weight), std::begin (lastWeight));

            for (int k = 0; k < bins; ++k)
            {
                const float f = std::max (1.0f, (float) (k * sr / n));
                float w;
                if (f <= weightHz[0]) w = lastWeight[0];
                else if (f >= weightHz[numWeights - 1]) w = lastWeight[numWeights - 1];
                else
                {
                    int i = 0;
                    while (f > weightHz[i + 1]) ++i;
                    const float t = std::log (f / weightHz[i]) / std::log (weightHz[i + 1] / weightHz[i]);
                    const float s2 = 0.5f - 0.5f * std::cos ((float) M_PI * t);
                    w = lastWeight[i] + (lastWeight[i + 1] - lastWeight[i]) * s2;
                }
                weightCurve[(size_t) k] = 1.0f + std::clamp (w, -1.0f, 1.0f);
            }
        }

        // 周波数の対数の幅でならした平均（prefix 和で一気に）
        void logSmooth (const std::vector<float>& src, std::vector<float>& dst, float octaves)
        {
            const double r = std::pow (2.0, octaves * 0.5);
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];
            for (int k = 0; k < bins; ++k)
            {
                const int a = std::clamp ((int) std::floor (k / r), 0, bins - 1);
                const int b = std::clamp ((int) std::ceil (k * r), a, bins - 1);
                dst[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (b - a + 1));
            }
        }

        // det（強さ dB）から目標の下げ量（dB, 0 以下）を tmpB に作る
        void computeTarget()
        {
            // SHARPNESS: 0 → 山なみの幅 2 オクターブ（広いこもりも拾う）、1 → 1/4 オクターブ（細いピークだけ）
            const float width = 2.0f * std::pow (0.125f, settings.sharpness);
            // SELECTIVITY: 0 → 3 dB 飛び出たら反応、1 → 15 dB
            const float thr = 3.0f + 12.0f * settings.selectivity;
            const float knee = 3.0f;
            const float depth = std::clamp (settings.depth, 0.0f, 1.0f);

            // 1 回目の平均 → 平均より大きく飛び出た所を頭打ちにして 2 回目（ピーク自身に平均が引っぱられないように）
            logSmooth (levelDb, tmpA, width);
            for (int k = 0; k < bins; ++k)
                tmpB[(size_t) k] = std::min (levelDb[(size_t) k], tmpA[(size_t) k] + 3.0f);
            logSmooth (tmpB, tmpA, width);

            for (int k = 0; k < bins; ++k)
            {
                float t = 0.0f;
                if (levelDb[(size_t) k] > floorDb)
                {
                    const float over = levelDb[(size_t) k] - tmpA[(size_t) k] - thr;
                    // やわらかい膝：0 付近はゆるやかに効き始める
                    const float e = over <= -knee ? 0.0f
                                  : (over >= knee ? over : (over + knee) * (over + knee) / (4.0f * knee));
                    t = -std::min (maxDepthDb, e * depth * weightCurve[(size_t) k]) * focus[(size_t) k];
                }
                tmpB[(size_t) k] = t;
            }

            // サイン波は窓のせいで両隣のビンにも広がるので、±2 ビンでいちばん深い値をとってから平均する
            for (int k = 0; k < bins; ++k)
            {
                float m = 0.0f;
                for (int j = std::max (0, k - 2); j <= std::min (bins - 1, k + 2); ++j) m = std::min (m, tmpB[(size_t) j]);
                tmpA[(size_t) k] = m;
            }
            for (int k = 0; k < bins; ++k)
            {
                float s = 0.0f;
                int cnt = 0;
                for (int j = std::max (0, k - 2); j <= std::min (bins - 1, k + 2); ++j) { s += tmpA[(size_t) j]; ++cnt; }
                tmpB[(size_t) k] = s / (float) cnt;
            }
        }

        void processFrame (int numCh)
        {
            updateFocus();
            updateWeights();

            for (int c = 0; c < numCh; ++c)
            {
                auto* x = spec[c].data();
                for (int k = 0; k < n; ++k) x[k] = { inFifo[c][(size_t) k] * win[(size_t) k], 0.0f };
                fft.run (x, false);

                // 強さを時間方向に少しならす（約 10 ms）
                const float a = 1.0f - (float) std::exp (-(double) hop / (0.010 * sr));
                for (int k = 0; k < bins; ++k)
                {
                    const float p = std::norm (x[k]) * powerNorm;
                    detPow[c][(size_t) k] += (p - detPow[c][(size_t) k]) * a;
                }
            }

            const float atk = (float) std::exp (-(double) hop / (std::max (0.5f, settings.attackMs) * 0.001 * sr));
            const float rel = (float) std::exp (-(double) hop / (std::max (1.0f, settings.releaseMs) * 0.001 * sr));
            const bool linked = settings.link || numCh == 1;
            const int groups = linked ? 1 : numCh;

            for (int g = 0; g < groups; ++g)
            {
                for (int k = 0; k < bins; ++k)
                {
                    float p = 0.0f;
                    if (linked) { for (int c = 0; c < numCh; ++c) p += detPow[c][(size_t) k]; p /= (float) numCh; }
                    else p = detPow[g][(size_t) k];
                    levelDb[(size_t) k] = 10.0f * std::log10 (p + 1e-14f);
                }
                computeTarget();

                auto& gr = grDb[g];
                for (int k = 0; k < bins; ++k)
                {
                    const float t = tmpB[(size_t) k];
                    const float coef = t < gr[(size_t) k] ? atk : rel;
                    gr[(size_t) k] = t + (gr[(size_t) k] - t) * coef;
                }
                if (g == 0) storeDisplay();
            }

            for (int c = 0; c < numCh; ++c)
            {
                const auto& gr = grDb[linked ? 0 : c];
                auto* x = spec[c].data();
                for (int k = 0; k < bins; ++k)
                {
                    const float gain = std::pow (10.0f, gr[(size_t) k] / 20.0f);
                    x[k] *= gain;
                    if (k > 0 && k < n / 2) x[n - k] *= gain;
                }
                fft.run (x, true);

                auto& acc = outAcc[c];
                const float scale = olaScale / (float) n;
                for (int k = 0; k < n; ++k) acc[(size_t) k] += x[k].real() * win[(size_t) k] * scale;
                for (int k = 0; k < hop; ++k) outFifo[c][(size_t) k] = acc[(size_t) k];
                std::copy (acc.begin() + hop, acc.begin() + n + hop, acc.begin());
                std::copy (inFifo[c].begin() + hop, inFifo[c].end(), inFifo[c].begin());
            }
        }

        void storeDisplay()
        {
            float worst = 0.0f;
            for (int p = 0; p < displayPoints; ++p)
            {
                const int a = dispEdge[p], b = std::max (a, dispEdge[p + 1]);
                float lv = -120.0f, g = 0.0f;
                for (int k = a; k <= b; ++k)
                {
                    lv = std::max (lv, levelDb[(size_t) k]);
                    g = std::min (g, grDb[0][(size_t) k]);
                }
                dispSpec[p].store (lv, std::memory_order_relaxed);
                dispGr[p].store (g, std::memory_order_relaxed);
                worst = std::min (worst, g);
            }
            maxReduction.store (worst, std::memory_order_relaxed);
        }

        static constexpr float floorDb = -90.0f, maxDepthDb = 24.0f;

        Settings settings;
        double sr = 48000.0;
        int channels = 2, n = 2048, hop = 256, fifoLatency = 1792, latency = 2048, bins = 1025;
        int rover = 0, dryPos = 0;
        float olaScale = 0.25f, powerNorm = 1.0f;
        float mixCur = 1.0f, outCur = 1.0f, smoothCoef = 0.001f;
        float lastLow = -1.0f, lastHigh = -1.0f;

        Fft fft;
        std::vector<float> win;
        std::vector<float> inFifo[maxChannels], outFifo[maxChannels], outAcc[maxChannels], dryDelay[maxChannels];
        std::vector<std::complex<float>> spec[maxChannels];
        std::vector<float> detPow[maxChannels], grDb[maxChannels];
        std::vector<float> levelDb, tmpA, tmpB, focus, weightCurve;
        float lastWeight[numWeights] {};
        std::vector<double> prefix;

        int dispEdge[displayPoints + 1] {};
        std::atomic<float> dispSpec[displayPoints], dispGr[displayPoints];
        std::atomic<float> maxReduction { 0.0f };
    };
}
