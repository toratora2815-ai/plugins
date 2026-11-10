#pragma once
// My Tremolo の「色付け」（ANALOG STYLE）と 4 倍オーバーサンプリング。JUCE なし。
// 並び: 入口 → [前の強調 → コンプ → 1 段目の頭打ち] → 揺れ（VCA）→ OUTPUT → [2 段目の頭打ち → 強調を戻す → 低域カット → 補正] → 出口
// 数値は build/tremlab の測定から当てはめた（TremoloStyleTables.h）
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

namespace mytrem
{
    constexpr double kPi = 3.14159265358979323846;

    //==============================================================================
    // 2 倍のハーフバンド FIR（直線位相）。2 段重ねて 4 倍にする
    struct Halfband
    {
        std::vector<float> h;        // 全タップ（奇数）
        int n = 0;
        // 上げ: 入力 1 → 出力 2。下げ: 入力 2 → 出力 1
        std::vector<float> bufUp, bufDn;
        int posUp = 0, posDn = 0;

        void design (int taps, double transition)
        {
            n = taps;
            h.assign ((size_t) n, 0.0f);
            const int m = (n - 1) / 2;
            const double beta = 0.1102 * (80.0 - 8.7);   // 約 80 dB のカイザー窓
            auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
            const double fc = 0.25 + transition * 0.0;   // 半分の帯域（正規化 0.25 = fs/4）
            double sum = 0;
            for (int i = 0; i < n; ++i)
            {
                const int k = i - m;
                const double sinc = k == 0 ? 2 * fc : std::sin (2 * kPi * fc * k) / (kPi * k);
                const double r = (double) k / m;
                const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
                h[(size_t) i] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& v : h) v = (float) (v / sum);
            bufUp.assign ((size_t) n * 2, 0.0f);
            bufDn.assign ((size_t) n * 2, 0.0f);
            posUp = posDn = 0;
        }
        void reset() { std::fill (bufUp.begin(), bufUp.end(), 0.0f); std::fill (bufDn.begin(), bufDn.end(), 0.0f); posUp = posDn = 0; }

        // 0 を挟んで 2 倍にし、FIR を通す（利得 2 を掛ける）
        inline void up (float x, float& y0, float& y1)
        {
            push (bufUp, posUp, x); y0 = 2.0f * conv (bufUp, posUp);
            push (bufUp, posUp, 0.0f); y1 = 2.0f * conv (bufUp, posUp);
        }
        inline float down (float x0, float x1)
        {
            push (bufDn, posDn, x0);
            const float y = conv (bufDn, posDn);
            push (bufDn, posDn, x1);
            return y;
        }
        int latencyAtHighRate() const { return n - 1; }   // 上げ＋下げで (n-1)/2 × 2

    private:
        inline void push (std::vector<float>& b, int& p, float x)
        {
            p = (p == 0 ? n : p) - 1;
            b[(size_t) p] = x; b[(size_t) (p + n)] = x;
        }
        inline float conv (const std::vector<float>& b, int p) const
        {
            const float* s = b.data() + p;
            float acc = 0.0f;
            for (int i = 0; i < n; ++i) acc += h[(size_t) i] * s[i];
            return acc;
        }
    };

    //==============================================================================
    // 色付けの数値（1 種類分）
    struct StyleParams
    {
        float preDb = 0;                     // 入口の倍率
        float emphHz = 3000, emphDb = 0;     // 1 段目の前の高域の持ち上げ（2 段目の後で戻す）
        // コンプ（1 段目の前）
        float compThrDb = 0, compRatio = 1, compKneeDb = 6, compAttMs = 1, compRelMs = 50;
        float scHpHz = 20, scShelfHz = 2000, scShelfDb = 0;   // コンプの耳の帯域
        float compMakeupDb = 0;
        // 頭打ち: y = s·tanh((u + b)/s) − s·tanh(b/s)、u = x + a·x²/(1+x²)
        float s1 = 100, a1 = 0, b1 = 0;
        float s2 = 100, a2 = 0, b2 = 0;
        float hpHz = 13.5f, hpQ = 0.75f;     // 出口の低域カット（2 次。hpQ < 0.1 なら 1 次）
        float trimDb = 0;                    // 最後の倍率
        float lowShelfHz = 200, lowShelfDb = 0;   // 出口の低域の小さな傾き
        float compRms = 0;                   // 1 = コンプの耳を RMS（2 乗の平均）にする、0 = ピーク
        float compDb = 0;                    // 1 = 縮める量（dB）そのものを ATTACK / RELEASE でならす（耳は瞬時値）、2 = 縮める時だけ倍率でならす
        float compGainMs = 0;                // > 0 = 縮める量をさらに 1 次でならす（ms）
        float compFb = 0;                    // 1 = 耳を VCA の後ろに置く（フィードバック型）
        float compRmsMs = 0;                 // compDb のとき、耳を 2 乗の平均にする時定数（ms、0 = 瞬時値）
        int digital = 0;                    // 1 = 何も足さず、出口だけ −0.1 dB で硬くクリップ
    };

    //==============================================================================
    // 1 次の棚（高域）: H = (1 + g·s/w) / (1 + s/w) の双一次変換。g は直線倍率
    struct Shelf1
    {
        double b0 = 1, b1 = 0, a1 = 0, z = 0;
        void setHigh (double fs, double hz, double db)
        {
            const double g = std::pow (10.0, db / 20.0);
            const double k = std::tan (kPi * std::min (hz, fs * 0.45) / fs);
            // H(s) = (s·g + w)/(s + w)
            const double a0 = 1 + k;
            b0 = ((g + k) / a0); b1 = ((k - g) / a0); a1 = ((k - 1) / a0);
        }
        // setHigh のちょうど逆（持ち上げた分を戻す）: H = (s + w) / (g·s + w)
        void setHighInverse (double fs, double hz, double db)
        {
            const double g = std::pow (10.0, db / 20.0);
            const double k = std::tan (kPi * std::min (hz, fs * 0.45) / fs);
            const double a0 = g + k;
            b0 = ((1 + k) / a0); b1 = ((k - 1) / a0); a1 = ((k - g) / a0);
        }
        void setLow (double fs, double hz, double db)
        {
            const double g = std::pow (10.0, db / 20.0);
            const double k = std::tan (kPi * std::min (hz, fs * 0.45) / fs);
            // H(s) = (s + g·w)/(s + w)
            const double a0 = 1 + k;
            b0 = ((1 + g * k) / a0); b1 = ((g * k - 1) / a0); a1 = ((k - 1) / a0);
        }
        void setHighpass (double fs, double hz)
        {
            const double k = std::tan (kPi * std::min (hz, fs * 0.45) / fs);
            const double a0 = 1 + k;
            b0 = (1 / a0); b1 = (-1 / a0); a1 = ((k - 1) / a0);
        }
        void reset() { z = 0; }
        inline float process (float xf)   // 転置直接形 II（係数・状態は double。4 倍の速さでも極が 1 に丸まらないように）
        {
            const double x = xf, y = b0 * x + z;
            z = b1 * x - a1 * y;
            return (float) y;
        }
    };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setHighpass (double fs, double hz, double q)
        {
            const double w = 2 * kPi * hz / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al;
            b0 = ((1 + c) / 2 / a0); b1 = (-(1 + c) / a0); b2 = b0;
            a1 = (-2 * c / a0); a2 = ((1 - al) / a0);
        }
        void setLowpass (double fs, double hz, double q)
        {
            const double w = 2 * kPi * hz / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al;
            b0 = ((1 - c) / 2 / a0); b1 = ((1 - c) / a0); b2 = b0;
            a1 = (-2 * c / a0); a2 = ((1 - al) / a0);
        }
        void reset() { z1 = z2 = 0; }
        inline float process (float xf)
        {
            const double x = xf, y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    inline float shaper (float x, float s, float a, float b, float off)
    {
        const float u = x + a * (x * x) / (1.0f + x * x);
        return s * std::tanh ((u + b) / s) - off;
    }

    //==============================================================================
    // 1 チャンネル分の色付け。4 倍の速さで回す
    struct StyleChannel
    {
        StyleParams p;
        double fsHi = 192000;
        Shelf1 emph, deemph, scShelf, scHp, lowShelf, hp1;
        Biquad hp;
        float pre = 1, makeup = 1, trim = 1, off1 = 0, off2 = 0;
        float env = 0, attC = 0, relC = 0, gainC = 0, rmsC = 0, msS = 0, grS = 0, grS2 = 0, fbPrev = 0;
        float compGain = 1;

        void prepare (double fsHigh, const StyleParams& sp)
        {
            fsHi = fsHigh; p = sp;
            emph.setHigh (fsHi, p.emphHz, p.emphDb);
            deemph.setHighInverse (fsHi, p.emphHz, p.emphDb);
            scShelf.setHigh (fsHi, p.scShelfHz, p.scShelfDb);
            scHp.setHighpass (fsHi, p.scHpHz);
            lowShelf.setLow (fsHi, p.lowShelfHz, p.lowShelfDb);
            hp.setHighpass (fsHi, p.hpHz, std::max (0.1f, p.hpQ));
            hp1.setHighpass (fsHi, p.hpHz);
            pre = std::pow (10.0f, p.preDb / 20.0f);
            makeup = std::pow (10.0f, p.compMakeupDb / 20.0f);
            trim = std::pow (10.0f, p.trimDb / 20.0f);
            off1 = p.s1 * std::tanh (p.b1 / p.s1);
            off2 = p.s2 * std::tanh (p.b2 / p.s2);
            attC = (float) std::exp (-1.0 / (std::max (0.01f, p.compAttMs) * 1e-3 * fsHi));
            relC = (float) std::exp (-1.0 / (std::max (0.01f, p.compRelMs) * 1e-3 * fsHi));
            gainC = p.compGainMs > 0.001f ? (float) std::exp (-1.0 / (p.compGainMs * 1e-3 * fsHi)) : 0.0f;
            rmsC = p.compRmsMs > 0.001f ? (float) std::exp (-1.0 / (p.compRmsMs * 1e-3 * fsHi)) : 0.0f;
            reset();
        }
        void reset()
        {
            emph.reset(); deemph.reset(); scShelf.reset(); scHp.reset(); lowShelf.reset(); hp.reset(); hp1.reset();
            env = 0; compGain = 1; msS = 0; grS = 0; grS2 = 0; fbPrev = 0;
        }
        inline float gainComputer (float ldb) const   // 入る音量（dB）→ 縮める量（dB、ニーつき）
        {
            const float over = ldb - p.compThrDb;
            const float k = std::max (1e-3f, p.compKneeDb);   // ニー 0 でも 0/0 にならないように
            if (2 * over < -k) return 0.0f;
            if (2 * over > k) return over * (1 - 1 / p.compRatio);
            const float t = over + k / 2;
            return (1 - 1 / p.compRatio) * t * t / (2 * k);
        }

        // 1 段目（揺れの前）
        inline float front (float x)
        {
            if (p.digital) return x;
            x *= pre;
            x = emph.process (x);
            if (p.compRatio > 1.001f)
            {
                const float scv = scShelf.process (scHp.process (p.compFb > 0.5f ? fbPrev : x));
                float gr;
                if (p.compDb > 0.5f)
                {
                    // 瞬時の音量 → 縮める量 → その量を ATTACK（増える時）/ RELEASE（減る時）でならす
                    float ldb;
                    if (p.compRms > 0.5f) { msS = rmsC * msS + (1 - rmsC) * 2.0f * scv * scv; ldb = 10.0f * std::log10 (msS + 1e-18f); }
                    else ldb = 20.0f * std::log10 (std::abs (scv) + 1e-9f);
                    const float gi = gainComputer (ldb);
                    if (p.compDb > 1.5f)
                    {
                        // 2 = 縮める時は倍率（直線）でならし、戻す時は dB でならす（Pump の段の応答がこの形: 縮み始めがゆっくりで後半が速く、戻りは dB でまっすぐ）
                        if (gi > grS)
                        {
                            const float gNow = std::pow (10.0f, -grS / 20.0f), gTo = std::pow (10.0f, -gi / 20.0f);
                            grS = -20.0f * std::log10 (std::max (1e-9f, attC * gNow + (1 - attC) * gTo));
                        }
                        else grS = relC * grS + (1 - relC) * gi;
                    }
                    else grS = gi > grS ? attC * grS + (1 - attC) * gi : relC * grS + (1 - relC) * gi;
                    gr = grS;
                }
                else
                {
                    const float sc = p.compRms > 0.5f ? 2.0f * scv * scv : std::abs (scv);   // RMS は正弦波のピークにそろえる
                    env = sc > env ? attC * env + (1 - attC) * sc : relC * env + (1 - relC) * sc;
                    gr = gainComputer (p.compRms > 0.5f ? 10.0f * std::log10 (env + 1e-18f) : 20.0f * std::log10 (env + 1e-9f));
                }
                if (gainC > 0) { grS2 = gainC * grS2 + (1 - gainC) * gr; gr = grS2; }
                const float g = std::pow (10.0f, -gr / 20.0f);
                fbPrev = x * g;
                compGain = g * makeup;
                x *= compGain;
            }
            return shaper (x, p.s1, p.a1, p.b1, off1);
        }
        // 2 段目（揺れと OUTPUT の後）
        inline float back (float x)
        {
            if (p.digital) return std::max (-0.988f, std::min (0.988f, x));
            x = shaper (x, p.s2, p.a2, p.b2, off2);
            x = deemph.process (x);
            x = lowShelf.process (x);
            x = p.hpQ < 0.1f ? hp1.process (x) : hp.process (x);   // hpQ < 0.1 = 1 次（Grit の止まった後の戻り方がこの形）
            return x * trim;
        }
    };

    //==============================================================================
    // 4 倍オーバーサンプラー（1 チャンネル）
    struct Oversampler4
    {
        Halfband a, b;   // a: 1→2、b: 2→4
        Oversampler4() { prepare(); }
        void prepare() { a.design (47, 0); b.design (21, 0); }
        void reset() { a.reset(); b.reset(); }
        // 遅れ（元の速さのサンプル数）: (47−1)/2 + (21−1)/4 = 23 + 5
        static constexpr double latency() { return 28.0; }
        inline void up (float x, float* o4)
        {
            float u0, u1; a.up (x, u0, u1);
            b.up (u0, o4[0], o4[1]);
            b.up (u1, o4[2], o4[3]);
        }
        inline float down (const float* o4)
        {
            const float d0 = b.down (o4[0], o4[1]);
            const float d1 = b.down (o4[2], o4[3]);
            return a.down (d0, d1);
        }
    };
}
