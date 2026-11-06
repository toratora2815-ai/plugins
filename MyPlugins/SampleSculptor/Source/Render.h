#pragma once
// Sample Sculptor: 読み込んだサンプルを、つまみの設定どおりに作り直す（JUCE なし。tools/test_render.cpp）
//
// 順番:  切り取り（START / END）→ ピッチ（再生速度ごと）→ 形（ATTACK / LENGTH / ROOM の DRY 側）
//        → 逆再生（SOUND）→ 響きを足す（ROOM の WET 側、尾の分だけ長くなる）→ 逆再生（SOUND + ROOM = 吸い込む響き）
//        → DRIVE（4 倍オーバーサンプリング）→ LOW / HIGH CUT → WIDTH → フェード → 音量（NORMALIZE / GAIN）
//        STRETCH（音程そのまま長さだけ）は切り取りのすぐ後
#include "HitEngine.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ss
{
    using Channels = std::vector<std::vector<float>>;

    enum Reverse { revOff = 0, revSound = 1, revAll = 2 };

    struct Settings
    {
        float start = 0.0f, end = 1.0f;          // 元の長さに対する割合
        float fadeInMs = 0.0f, fadeOutMs = 0.0f;
        int reverse = revOff;
        float pitch = 0.0f;                       // 半音
        float stretch = 1.0f;                     // 長さの倍率（音程は変えない）0.25..4
        float lowCutHz = 20.0f, highCutHz = 20000.0f;   // 20 / 20000 = なし
        float width = 1.0f;                       // 0 = モノラル、1 = そのまま、2 = 広く
        float attack = 0.0f, snapMs = 15.0f, length = 0.0f;   // -1..1
        float room = 0.0f, size = 0.4f, tone = 0.5f;          // room -1..1
        float drive = 0.0f;                       // 0..1
        float gainDb = 0.0f;
        bool normalize = true;
    };

    inline float roomWetGain (float room) { return std::pow (std::max (0.0f, room), 1.3f) * 1.9f; }
    inline double roomTailSec (float size) { return 0.12 + 1.8 * std::pow (std::clamp (size, 0.0f, 1.0f), 1.3) + 0.4; }

    // ブラックマン窓の sinc ローパス（cutoff は 0..0.5、サンプル周波数に対して）
    inline std::vector<float> lowpassFir (double cutoff, int taps)
    {
        std::vector<float> h ((size_t) taps);
        const double m = (taps - 1) * 0.5;
        double sum = 0;
        for (int i = 0; i < taps; ++i)
        {
            const double x = i - m;
            const double s = x == 0 ? 2 * cutoff : std::sin (2 * M_PI * cutoff * x) / (M_PI * x);
            const double w = 0.42 - 0.5 * std::cos (2 * M_PI * i / (taps - 1)) + 0.08 * std::cos (4 * M_PI * i / (taps - 1));
            h[(size_t) i] = (float) (s * w);
            sum += s * w;
        }
        for (auto& v : h) v = (float) (v / sum);
        return h;
    }

    // 遅れなし（中心合わせ）でたたみこむ
    inline std::vector<float> filterZeroPhase (const std::vector<float>& x, const std::vector<float>& h)
    {
        const int n = (int) x.size(), t = (int) h.size(), m = t / 2;
        std::vector<float> y ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            double acc = 0;
            const int k0 = std::max (0, i + m - (n - 1)), k1 = std::min (t - 1, i + m);
            for (int k = k0; k <= k1; ++k) acc += h[(size_t) k] * x[(size_t) (i + m - k)];
            y[(size_t) i] = (float) acc;
        }
        return y;
    }

    inline void reverseAll (Channels& c) { for (auto& v : c) std::reverse (v.begin(), v.end()); }

    // ピッチ（速さごと変える。上げると短く、下げると長く）
    inline Channels repitch (const Channels& in, float semis)
    {
        if (std::abs (semis) < 0.001f || in.empty() || in[0].size() < 4) return in;
        const double r = std::pow (2.0, semis / 12.0);
        const size_t n = in[0].size();
        const size_t outN = std::max<size_t> (4, (size_t) std::floor ((double) (n - 1) / r));
        Channels out;
        for (const auto& srcCh : in)
        {
            std::vector<float> src = srcCh;
            if (r > 1.0) src = filterZeroPhase (src, lowpassFir (0.47 / r, 63));   // 上げるときは折り返しを防ぐ
            std::vector<float> y (outN);
            for (size_t i = 0; i < outN; ++i)
            {
                const double p = i * r;
                const long i1 = (long) p;
                const float f = (float) (p - (double) i1);
                auto at = [&] (long k) { return src[(size_t) std::clamp<long> (k, 0, (long) n - 1)]; };
                const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
                // 3 次エルミート補間
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                y[i] = ((c3 * f + c2) * f + c1) * f + y1;
            }
            out.push_back (std::move (y));
        }
        return out;
    }

    // DRIVE: 4 倍に上げて tanh、下げる（遅れなし）
    inline void drive (Channels& c, float amount)
    {
        if (amount <= 0.0001f) return;
        const float g = std::pow (10.0f, amount * 24.0f / 20.0f);
        const float comp = 1.0f / std::sqrt (g);
        const float blend = std::min (1.0f, amount * 4.0f);
        static const std::vector<float> h = lowpassFir (0.115, 95);   // 4 倍の速さで見て 0.46 × 元のナイキスト
        for (auto& x : c)
        {
            const size_t n = x.size();
            std::vector<float> up (n * 4, 0.0f);
            for (size_t i = 0; i < n; ++i) up[i * 4] = x[i] * 4.0f;
            up = filterZeroPhase (up, h);
            for (auto& v : up) { const float s = std::tanh (v * g) * comp; v = v + (s - v) * blend; }
            // 下げる（4 つに 1 つだけ計算）
            const int t = (int) h.size(), m = t / 2;
            const int un = (int) up.size();
            for (size_t i = 0; i < n; ++i)
            {
                const int j = (int) i * 4;
                double acc = 0;
                for (int k = std::max (0, j + m - (un - 1)); k <= std::min (t - 1, j + m); ++k) acc += h[(size_t) k] * up[(size_t) (j + m - k)];
                x[i] = (float) acc;
            }
        }
    }

    // STRETCH: 音程を変えずに長さだけ変える（WSOLA。窓 約 43 ms、半分ずつ重ねる。
    // 次の窓は「前の窓の自然な続き」に一番似た所を ±11 ms から探すので、うなりやブツブツが出にくい）
    inline Channels timeStretch (const Channels& in, double factor, double fs)
    {
        if (std::abs (factor - 1.0) < 0.005 || in.empty() || in[0].size() < 64) return in;
        int N = (int) std::lround (2048.0 * fs / 48000.0);
        N = std::max (256, N & ~7);
        const int Hs = N / 2, tol = N / 4, half = N / 2;
        const double Ha = Hs / factor;
        const long inN = (long) in[0].size();
        const long outN = std::max<long> (16, (long) std::lround ((double) inN * factor));

        // 似た所探しは、左右を足して 4 分の 1 に間引いたもので
        const int D = 4;
        std::vector<float> mono ((size_t) inN, 0.0f);
        for (auto& c : in) for (long i = 0; i < inN; ++i) mono[(size_t) i] += c[(size_t) i];
        std::vector<float> md ((size_t) (inN / D + 1), 0.0f);
        for (long i = 0; i < inN; ++i) md[(size_t) (i / D)] += mono[(size_t) i];
        auto atM = [&] (long i) { return i >= 0 && i < inN ? mono[(size_t) i] : 0.0f; };
        auto atD = [&] (long i) { return i >= 0 && i < (long) md.size() ? md[(size_t) i] : 0.0f; };

        std::vector<float> w ((size_t) N);
        for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * M_PI * i / N);   // 50 % 重ねで足すと 1

        Channels out (in.size(), std::vector<float> ((size_t) outN, 0.0f));
        std::vector<float> wsum ((size_t) outN, 0.0f);
        long prevC = 0;
        for (long k = 0; k * Hs - half < outN; ++k)
        {
            const long oc = k * Hs;                 // 出力の窓の真ん中
            long c = (long) std::lround (k * Ha);   // 入力の窓の真ん中（予定）
            if (k > 0)
            {
                const long nat = prevC + Hs;        // 前の窓の自然な続き
                const long lo = std::max<long> (0, c - tol), hi = std::min<long> (inN - 1, c + tol);
                // 粗く（4 サンプルおき）
                long best = std::clamp (c, lo, hi);
                double bestV = -1e30;
                for (long cand = lo; cand <= hi; cand += D)
                {
                    double v = 0;
                    for (int i = -half / D; i < half / D; ++i) v += (double) atD ((cand / D) + i) * atD ((nat / D) + i);
                    if (v > bestV) { bestV = v; best = cand; }
                }
                // 細かく（±3）
                const long b0 = best;
                bestV = -1e30;
                for (long cand = std::max (lo, b0 - 3); cand <= std::min (hi, b0 + 3); ++cand)
                {
                    double v = 0;
                    for (int i = -half; i < half; i += 2) v += (double) atM (cand + i) * atM (nat + i);
                    if (v > bestV) { bestV = v; best = cand; }
                }
                c = best;
            }
            for (size_t ch = 0; ch < in.size(); ++ch)
            {
                const auto& src = in[ch];
                auto& dst = out[ch];
                for (int i = 0; i < N; ++i)
                {
                    const long o = oc - half + i, s = c - half + i;
                    if (o < 0 || o >= outN) continue;
                    const float v = (s >= 0 && s < inN) ? src[(size_t) s] : 0.0f;
                    dst[(size_t) o] += v * w[(size_t) i];
                    if (ch == 0) wsum[(size_t) o] += w[(size_t) i];
                }
            }
            prevC = c;
        }
        for (auto& ch : out)
            for (long i = 0; i < outN; ++i) ch[(size_t) i] /= std::max (wsum[(size_t) i], 1.0e-3f);
        return out;
    }

    // LOW / HIGH CUT（バターワース 4 次 = 24 dB/oct。前から 1 回だけかけるので、打った瞬間より前に音がにじまない）
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (bool highpass, double fc, double q, double fs)
        {
            const double w0 = 2 * M_PI * fc / fs, cw = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
            if (highpass) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
            else          { b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; }
            b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
        }
        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    inline void cutFilters (Channels& c, float lowHz, float highHz, double fs)
    {
        const bool lo = lowHz > 20.5f, hi = highHz < 19900.0f && highHz < fs * 0.45;
        if (! lo && ! hi) return;
        for (auto& x : c)
        {
            Biquad f[4];
            if (lo) { f[0].set (true, lowHz, 0.5412, fs); f[1].set (true, lowHz, 1.3066, fs); }
            if (hi) { f[2].set (false, highHz, 0.5412, fs); f[3].set (false, highHz, 1.3066, fs); }
            for (auto& v : x)
            {
                float y = v;
                if (lo) y = f[1].process (f[0].process (y));
                if (hi) y = f[3].process (f[2].process (y));
                v = y;
            }
        }
    }

    // WIDTH: 真ん中（M）はそのまま、左右の差（S）だけを 0〜2 倍
    inline void stereoWidth (Channels& c, float width)
    {
        if (c.size() < 2 || std::abs (width - 1.0f) < 0.001f) return;
        for (size_t i = 0; i < c[0].size(); ++i)
        {
            const float m = 0.5f * (c[0][i] + c[1][i]), sd = 0.5f * (c[0][i] - c[1][i]) * width;
            c[0][i] = m + sd; c[1][i] = m - sd;
        }
    }

    inline Channels render (const Channels& src, double fs, const Settings& s)
    {
        if (src.empty() || src[0].empty()) return {};
        const int numCh = (int) std::min<size_t> (2, src.size());
        const size_t n = src[0].size();

        // 1. 切り取り
        size_t a = (size_t) std::clamp<double> (std::floor (s.start * (double) n), 0.0, (double) n - 1);
        size_t b = (size_t) std::clamp<double> (std::ceil (s.end * (double) n), (double) a + 16, (double) n);
        if (b > n) b = n;
        Channels x;
        for (int c = 0; c < numCh; ++c) x.emplace_back (src[(size_t) c].begin() + (long) a, src[(size_t) c].begin() + (long) b);

        // 2. STRETCH（音程そのまま）→ ピッチ（速さごと）
        x = timeStretch (x, std::clamp (s.stretch, 0.25f, 4.0f), fs);
        x = repitch (x, s.pitch);

        // 3. 形（ROOM は DRY 側だけ。WET はあとで）
        if (std::abs (s.attack) > 0.001f || std::abs (s.length) > 0.001f || s.room < -0.001f)
        {
            hs::Engine e;
            hs::Settings h;
            h.attack = s.attack; h.snapMs = s.snapMs; h.length = s.length;
            h.room = std::min (0.0f, s.room); h.size = s.size; h.tone = s.tone; h.mix = 1.0f;
            e.setSettings (h);
            e.prepare (fs);
            float* ptr[2];
            const size_t len = x[0].size();
            for (size_t i = 0; i < len; i += 512)
            {
                for (int c = 0; c < numCh; ++c) ptr[c] = x[(size_t) c].data() + i;
                e.process (ptr, numCh, (int) std::min<size_t> (512, len - i));
            }
        }

        // 4. 逆再生（音だけ）
        if (s.reverse == revSound) reverseAll (x);

        // 5. 響きを足す
        if (s.room > 0.001f)
        {
            const size_t tail = (size_t) (roomTailSec (s.size) * fs);
            for (auto& v : x) v.resize (v.size() + tail, 0.0f);
            hs::Room room;
            room.prepare (fs);
            room.set (s.size, s.tone);
            room.snap();
            const float wet = roomWetGain (s.room);
            const size_t len = x[0].size();
            for (size_t i = 0; i < len; ++i)
            {
                const float l = x[0][i], r = numCh > 1 ? x[1][i] : l;
                float wl = 0, wr = 0;
                room.process (l, r, wl, wr);
                if (numCh > 1) { x[0][i] += wl * wet; x[1][i] += wr * wet; }
                else x[0][i] += 0.5f * (wl + wr) * wet;
            }
            // 尾の終わりの、聞こえない所を切る（-90 dB）
            size_t last = len;
            const float thr = 3.2e-5f;
            while (last > 1) { bool loud = false; for (auto& v : x) loud |= std::abs (v[last - 1]) > thr; if (loud) break; --last; }
            for (auto& v : x) v.resize (std::max<size_t> (last, 16));
        }

        // 6. 逆再生（響きごと = 吸い込む響き）
        if (s.reverse == revAll) reverseAll (x);

        // 7. DRIVE
        drive (x, s.drive);

        // 7b. LOW / HIGH CUT と WIDTH
        cutFilters (x, s.lowCutHz, s.highCutHz, fs);
        stereoWidth (x, s.width);

        // 8. フェード（なめらかな曲線）
        const size_t len = x[0].size();
        auto fade = [&] (float ms, bool in)
        {
            const size_t f = std::min (len, (size_t) (ms * 0.001 * fs));
            for (size_t i = 0; i < f; ++i)
            {
                const float g = (float) std::sin (0.5 * M_PI * (double) i / (double) f);
                for (auto& v : x) v[in ? i : len - 1 - i] *= g * g;
            }
        };
        if (s.fadeInMs > 0.01f) fade (s.fadeInMs, true);
        if (s.fadeOutMs > 0.01f) fade (s.fadeOutMs, false);
        // 端のプチッを防ぐ（最低 0.5 ms）
        fade (0.5f, false);

        // 9. 音量
        float gain = std::pow (10.0f, s.gainDb / 20.0f);
        if (s.normalize)
        {
            float pk = 0;
            for (auto& v : x) for (float y : v) pk = std::max (pk, std::abs (y));
            if (pk > 1.0e-6f) gain *= std::pow (10.0f, -0.3f / 20.0f) / pk;   // 一番大きい所を -0.3 dB に、そこから GAIN
        }
        for (auto& v : x) for (auto& y : v) y *= gain;
        return x;
    }
}
