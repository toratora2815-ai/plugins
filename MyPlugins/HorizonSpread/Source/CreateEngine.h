#pragma once
#include "DspParts.h"

// Horizon Spread の「生み出す」側（CREATE ページ）
//
// 基本の考え方は SHAPE と同じ：Mid から新しい Side を作って足すだけなので、
// モノラルにすると足した分は消えて元の音に戻る。
// 例外は SHIFT の CLASSIC モードだけ（右を本当に遅らせるので、モノラルでは少し音色が変わる）。
//
// 流れ：SHIFT → DOUBLE → BANDS → HAZE で Side を足す → MOTION で Side の広がりを揺らす
//      → ここで出来た L/R を SHAPE 側が「最初からあった広がり」として整える
namespace hs
{
    struct CreateSettings
    {
        bool  shiftOn = true;
        float shiftMs = 8.0f, shiftAmt = 0.0f;      // ms, 0..1
        bool  shiftClassic = false;

        bool  doubleOn = true;
        float doubleAmt = 0.0f, detune = 8.0f;      // 0..1, セント

        bool  bandsOn = true;
        float bandsAmt = 0.0f;                      // 0..1
        int   bands = 8;                            // 4..12
        bool  bandsTilt = false;

        bool  hazeOn = true;
        float hazeAmt = 0.0f, hazeSize = 0.5f;      // 0..1
        bool  hazeSmooth = false;

        bool  motionOn = true;
        float motionRate = 0.25f, motionDepth = 0.0f;   // Hz, 0..1
    };

    class CreateEngine
    {
    public:
        static constexpr int maxBands = 12;
        static constexpr int maxTaps = 128;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            const int maxDelay = (int) std::ceil (0.064 * sr);
            for (auto* d : { &dlMid, &dlR, &dlHaze })
                d->setMaxSamples (maxDelay);
            for (auto& c : apL) c.setMaxSamples (maxDelay);
            for (auto& c : apR) c.setMaxSamples (maxDelay);

            hazeHP.setCutoff (120.0, sr);
            tiltLP.setCutoff (1000.0, sr);

            for (auto* s : { &sShiftAmt, &sClassic, &sDoubleAmt, &sBandsAmt, &sTilt,
                             &sHazeAmt, &sSmooth, &sMotionDepth })
                s->reset (sr, 0.05);
            for (auto* s : { &sShiftMs, &sDetune, &sHazeSize, &sMotionRate })
                s->reset (sr, 0.2);

            bandCount = -1;
            velvetSize = -1.0f;
            first = true;
            reset();
        }

        void reset()
        {
            for (auto* d : { &dlMid, &dlR, &dlHaze }) d->reset();
            for (auto& c : apL) c.reset();
            for (auto& c : apR) c.reset();
            for (auto& b : bp) b.reset();
            hazeHP.reset(); tiltLP.reset();
            shUp = 0.0f; shDown = 0.5f;
            wobble1 = 0.0; wobble2 = 0.37; motionPhase = 0.0;
            mz1 = mz2 = 0.0f;
        }

        void setSettings (const CreateSettings& c, float spread)
        {
            const float sp = juce::jlimit (0.0f, 1.0f, spread);
            auto set = [this] (juce::SmoothedValue<float>& v, float target)
            {
                if (first) v.setCurrentAndTargetValue (target); else v.setTargetValue (target);
            };

            set (sShiftAmt, c.shiftOn ? c.shiftAmt * sp : 0.0f);
            set (sShiftMs, c.shiftMs);
            set (sClassic, c.shiftClassic ? 1.0f : 0.0f);
            set (sDoubleAmt, c.doubleOn ? c.doubleAmt * sp : 0.0f);
            set (sDetune, c.detune);
            set (sBandsAmt, c.bandsOn ? c.bandsAmt * sp : 0.0f);
            set (sTilt, c.bandsTilt ? 1.0f : 0.0f);
            set (sHazeAmt, c.hazeOn ? c.hazeAmt * sp : 0.0f);
            set (sHazeSize, c.hazeSize);
            set (sSmooth, c.hazeSmooth ? 1.0f : 0.0f);
            set (sMotionDepth, c.motionOn ? c.motionDepth * sp : 0.0f);
            set (sMotionRate, c.motionRate);

            if (c.bands != bandCount)
                setupBands (juce::jlimit (4, maxBands, c.bands));
            first = false;
        }

        // 1サンプル処理。入力 L/R から、作った後の Mid / Side を返す
        void process (float l, float r, float& mOut, float& sOut)
        {
            // ---- SHIFT（CLASSIC）：右を本当に遅らせる ----
            const float shiftSamples = sShiftMs.getNextValue() * 0.001f * (float) sr;
            const float shiftAmt = sShiftAmt.getNextValue();
            const float classic = sClassic.getNextValue();
            const float rDelayed = dlR.read (shiftSamples);
            dlR.push (r);
            r += classic * shiftAmt * (rDelayed - r);

            const float m = 0.5f * (l + r);
            float s = 0.5f * (l - r);
            const float mDelayed = dlMid.read (shiftSamples);
            dlMid.push (m);

            // ---- SHIFT（SAFE）：遅らせた Mid を Side にだけ足す ----
            s += (1.0f - classic) * shiftAmt * mDelayed;

            // ---- DOUBLE：少し上と少し下にピッチをずらした2人を、左右逆向きに足す ----
            const float dAmt = sDoubleAmt.getNextValue();
            const float cents = sDetune.getNextValue();
            if (dAmt > 1.0e-6f)
            {
                wobble1 += 0.17 / sr; if (wobble1 >= 1.0) wobble1 -= 1.0;
                wobble2 += 0.23 / sr; if (wobble2 >= 1.0) wobble2 -= 1.0;
                const float ms = 0.001f * (float) sr;
                const float base1 = (10.0f + 1.5f * (float) std::sin (juce::MathConstants<double>::twoPi * wobble1)) * ms;
                const float base2 = (11.0f + 1.5f * (float) std::sin (juce::MathConstants<double>::twoPi * wobble2)) * ms;
                const float ratio = std::exp2 (cents / 1200.0f);
                const float up   = shifter (shUp, ratio, base1);
                const float down = shifter (shDown, 1.0f / ratio, base2);
                s += dAmt * 0.5f * (up - down);
            }

            // ---- BANDS：細かい帯域を交互に左右へ（ALT）／左を明るく右を暗く（TILT）----
            const float bAmt = sBandsAmt.getNextValue();
            const float tilt = sTilt.getNextValue();
            {
                float alt = 0.0f;
                for (int i = 0; i < bandCount; ++i)
                    alt += ((i & 1) ? -1.0f : 1.0f) * bp[(size_t) i].process (m);
                const float tlt = m - 2.0f * tiltLP.process (m);   // 低域 -1 … 高域 +1
                s += bAmt * 0.6f * (alt + tilt * (tlt - alt));
            }

            // ---- HAZE：左右で違う拡散をかけた差を Side に（CLOUD）／ベルベットノイズで無相関化（SMOOTH）----
            const float hAmt = sHazeAmt.getNextValue();
            const float size = sHazeSize.getNextValue();
            const float smooth = sSmooth.getNextValue();
            const float hIn = hazeHP.process (m);
            dlHaze.push (hIn);
            if (hAmt > 1.0e-6f)
            {
                float cloud = 0.0f, velvet = 0.0f;
                if (smooth < 0.999f)
                {
                    const float scale = (0.4f + 2.2f * size) * 0.001f * (float) sr;
                    float xl = hIn, xr = hIn;
                    for (int k = 0; k < 4; ++k)
                    {
                        xl = apL[(size_t) k].process (xl, apTimesL[k] * scale);
                        xr = apR[(size_t) k].process (xr, apTimesR[k] * scale);
                    }
                    cloud = 0.7f * (xl - xr);
                }
                if (smooth > 0.001f)
                {
                    if (std::abs (size - velvetSize) > 0.01f)
                        setupVelvet (size);
                    float vl = 0.0f, vr = 0.0f;
                    for (int k = 0; k < tapCount; ++k)
                    {
                        vl += tapGainL[(size_t) k] * dlHaze.at (tapPosL[(size_t) k]);
                        vr += tapGainR[(size_t) k] * dlHaze.at (tapPosR[(size_t) k]);
                    }
                    velvet = 0.7f * (vl - vr);
                }
                s += hAmt * (cloud + smooth * (velvet - cloud));
            }

            // ---- MOTION：広がりの量と、広がる帯域をゆっくり揺らす ----
            const float depth = sMotionDepth.getNextValue();
            const float rate = sMotionRate.getNextValue();
            if (depth > 1.0e-6f)
            {
                motionPhase += rate / sr;
                if (motionPhase >= 1.0) motionPhase -= 1.0;
                const double ph = juce::MathConstants<double>::twoPi * motionPhase;
                const float gain = 1.0f + depth * 0.5f * (float) std::sin (ph);
                const double fc = 700.0 * std::exp2 (1.7 * std::cos (ph));
                const float t = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                const float y1 = a * s + mz1;  mz1 = s - a * y1;
                const float y2 = a * y1 + mz2; mz2 = y1 - a * y2;
                s = gain * (s + depth * 0.35f * (y2 - s));
            }

            mOut = m;
            sOut = s;
        }

    private:
        // ディレイを使ったピッチシフター（2つの読み出しを sin² で交互に）
        float shifter (float& phase, float ratio, float baseDelay)
        {
            const float window = 0.030f * (float) sr;
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);
            float p2 = phase + 0.5f;
            if (p2 >= 1.0f) p2 -= 1.0f;
            const float g1 = std::sin (juce::MathConstants<float>::pi * phase);
            const float g2 = std::sin (juce::MathConstants<float>::pi * p2);
            return g1 * g1 * dlMid.read (baseDelay + phase * window)
                 + g2 * g2 * dlMid.read (baseDelay + p2 * window);
        }

        void setupBands (int n)
        {
            // 200 Hz〜8 kHz を対数で n 等分
            const double ratio = 40.0;
            const double bwOct = std::log2 (ratio) / n;
            const double q = std::sqrt (std::exp2 (bwOct)) / (std::exp2 (bwOct) - 1.0);
            for (int i = 0; i < n; ++i)
                bp[(size_t) i].setBandPass (sr, 200.0 * std::pow (ratio, (i + 0.5) / n), q);
            bandCount = n;
        }

        // 左右別々の、まばらで減衰するランダムなパルス列（ベルベットノイズ）
        void setupVelvet (float size)
        {
            velvetSize = size;
            const double lengthMs = 8.0 + 32.0 * size;
            const double spacing = sr / 1500.0;
            tapCount = juce::jlimit (4, maxTaps, (int) (lengthMs * 0.001 * sr / spacing));
            auto fill = [&] (std::array<int, maxTaps>& pos, std::array<float, maxTaps>& gain, juce::int64 seed)
            {
                juce::Random rnd (seed);
                double energy = 0.0;
                for (int k = 0; k < tapCount; ++k)
                {
                    pos[(size_t) k] = 1 + (int) ((k + rnd.nextFloat() * 0.999f) * spacing);
                    const float decay = std::pow (0.1f, (float) k / (float) tapCount);   // 終わりで -20 dB
                    gain[(size_t) k] = (rnd.nextBool() ? 1.0f : -1.0f) * decay;
                    energy += decay * decay;
                }
                const float norm = (float) (1.0 / std::sqrt (energy));
                for (int k = 0; k < tapCount; ++k) gain[(size_t) k] *= norm;
            };
            fill (tapPosL, tapGainL, 0x5eed1);
            fill (tapPosR, tapGainR, 0x5eed2);
        }

        // 小数の遅れに対応したシュレーダー型オールパス
        struct Allpass
        {
            FracDelay d;
            void setMaxSamples (int n) { d.setMaxSamples (n); }
            void reset() { d.reset(); }
            float process (float x, float delay)
            {
                constexpr float g = 0.62f;
                const float del = d.read (delay);
                const float w = x + g * del;
                d.push (w);
                return del - g * w;
            }
        };

        static constexpr float apTimesL[4] { 3.1f, 5.3f, 8.9f, 14.2f };   // ms（SIZE 50% で約1.5倍）
        static constexpr float apTimesR[4] { 3.7f, 6.1f, 9.7f, 12.9f };

        double sr = 48000.0;
        bool first = true;

        FracDelay dlMid, dlR, dlHaze;
        float shUp = 0.0f, shDown = 0.5f;
        double wobble1 = 0.0, wobble2 = 0.37, motionPhase = 0.0;

        std::array<Biquad, maxBands> bp;
        int bandCount = -1;
        OnePoleLP tiltLP;

        OnePoleHP hazeHP;
        std::array<Allpass, 4> apL, apR;
        std::array<int, maxTaps> tapPosL {}, tapPosR {};
        std::array<float, maxTaps> tapGainL {}, tapGainR {};
        int tapCount = 0;
        float velvetSize = -1.0f;

        float mz1 = 0.0f, mz2 = 0.0f;

        juce::SmoothedValue<float> sShiftAmt, sClassic, sDoubleAmt, sBandsAmt, sTilt, sHazeAmt, sSmooth, sMotionDepth;
        juce::SmoothedValue<float> sShiftMs, sDetune, sHazeSize, sMotionRate;
    };
}
