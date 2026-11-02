#pragma once
#include <JuceHeader.h>

// Horizon Spread の音の処理（パラメーターや画面とは切り離してあるので、単体でテストできる）
//
// 基本の考え方：
//   L/R を Mid（真ん中）と Side（左右の差）に分け、Mid は触らずに Side だけを加工する。
//   こうするとモノラルにしたとき Side は打ち消し合って消え、元の音がそのまま残る。
//
// 流れ（第1段階＝整える側）：
//   入力 → M/S → Side を4帯域に分ける（BASS MONO / LOW / MID / HIGH）→ 帯域ごとの幅
//        → AIR（Side の高域シェルフ）→ TRANSIENT（アタックで Side を増減）
//        → SIDE DRIVE（Side の倍音、4倍オーバーサンプリング）→ GUARD → MIX → 出力
namespace hs
{
    inline float coefFor (double ms, double sr)
    {
        return ms <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr));
    }

    // RBJ 型のバイクアッド（ハイシェルフ用）。音声スレッドでメモリ確保しないよう自前で持つ
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

    struct ShapeSettings
    {
        float spread = 1.0f;         // 0..1  全体の効き
        float mix = 1.0f;            // 0..1
        float outDb = 0.0f;

        bool  widthOn = true;
        float wLow = 1.0f, wMid = 1.0f, wHigh = 1.0f;   // 0..2（1 = 原音）
        float xLow = 250.0f, xHigh = 4000.0f;

        bool  airOn = true;
        float airDb = 0.0f, airFreq = 10000.0f;

        bool  driveOn = true;
        float drive = 0.0f;          // 0..1

        bool  transOn = true;
        float transient = 0.0f;      // -1..1

        float bassMonoHz = 20.0f;    // 20 以下 = OFF
        bool  guard = true;
        bool  mono = false;
    };

    class SpreadEngine
    {
    public:
        static constexpr int scopeSize = 4096;
        static constexpr float autoSeconds = 6.0f;
        enum AutoState { autoIdle = 0, autoListening = 1, autoReady = 2, autoApplied = 3, autoMonoSource = 4 };

        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            blockMax = juce::jmax (16, maxBlock);

            juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockMax, 1 };
            for (auto* f : { &xoB, &xoL, &xoH, &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH, &anML, &anMH })
            {
                f->prepare (spec);
                f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            }
            for (auto* f : { &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH })
                f->setType (juce::dsp::LinkwitzRileyFilterType::allpass);

            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            oversampling->initProcessing ((size_t) blockMax);
            latency = (int) std::round (oversampling->getLatencyInSamples());

            for (auto* d : { &dlM, &dlSd, &dlSw })
                d->setLength (latency);

            for (auto* b : { &bufM, &bufSd, &bufSw, &bufX, &bufE })
                b->assign ((size_t) blockMax, 0.0f);

            driveHP.setCutoff (200.0, sr);
            residueDC.setCutoff (25.0, sr);

            // スムージング時間
            for (auto* s : { &sW, &sWL, &sWM, &sWH, &sBass, &sAirDb, &sDrive, &sTrans, &sGuardOn, &sMix, &sOut, &sMono })
                s->reset (sr, 0.05);
            for (auto* s : { &sFB, &sFL, &sFH, &sAirF })
                s->reset (sr, 0.08);

            cFastA = coefFor (0.2, sr);  cFastR = coefFor (25.0, sr);
            cSlowA = coefFor (25.0, sr); cSlowR = coefFor (250.0, sr);
            cGuardEnv = coefFor (150.0, sr);
            cGuardAtt = coefFor (10.0, sr); cGuardRel = coefFor (300.0, sr);
            cMeter = coefFor (300.0, sr);
            cDriveEnv = coefFor (80.0, sr);
            cOsEnvA = coefFor (2.0, sr * 4.0); cOsEnvR = coefFor (50.0, sr * 4.0);

            first = true;
            reset();
        }

        void reset()
        {
            for (auto* f : { &xoB, &xoL, &xoH, &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH, &anML, &anMH })
                f->reset();
            if (oversampling) oversampling->reset();
            for (auto* d : { &dlM, &dlSd, &dlSw }) d->reset();
            air.reset(); driveHP.reset(); residueDC.reset();
            envFast = envSlow = 0; gEnvM = gEnvS = gEnvSd = 0; gGuard = 1;
            mLR = mLL = mRR = 0; osEnv = 0; dEnvIn = dEnvOut = 0;
            lastFB = lastFL = lastFH = lastAirF = lastAirDb = -1.0f;
        }

        int getLatency() const { return latency; }

        void setSettings (const ShapeSettings& s)
        {
            settings = s;
            const float sp = juce::jlimit (0.0f, 1.0f, s.spread);
            auto set = [this] (juce::SmoothedValue<float>& v, float target)
            {
                if (first) v.setCurrentAndTargetValue (target); else v.setTargetValue (target);
            };
            auto setM = [this] (juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>& v, float target)
            {
                if (first) v.setCurrentAndTargetValue (target); else v.setTargetValue (target);
            };

            set (sW, s.widthOn ? sp : 0.0f);
            set (sWL, s.wLow); set (sWM, s.wMid); set (sWH, s.wHigh);
            set (sBass, s.bassMonoHz > 20.5f ? 1.0f : 0.0f);
            setM (sFB, juce::jlimit (20.0f, 300.0f, s.bassMonoHz));
            setM (sFL, s.xLow);
            setM (sFH, s.xHigh);
            set (sAirDb, s.airOn ? s.airDb * sp : 0.0f);
            setM (sAirF, s.airFreq);
            set (sDrive, s.driveOn ? s.drive * sp : 0.0f);
            set (sTrans, s.transOn ? s.transient * sp : 0.0f);
            set (sGuardOn, s.guard ? 1.0f : 0.0f);
            set (sMix, juce::jlimit (0.0f, 1.0f, s.mix));
            set (sOut, juce::Decibels::decibelsToGain (s.outDb));
            set (sMono, s.mono ? 1.0f : 0.0f);
            first = false;
        }

        // ステレオ（L/R 同じ長さ）をその場で処理
        void process (float* L, float* R, int numSamples)
        {
            for (int start = 0; start < numSamples; start += blockMax)
                processChunk (L + start, R + start, juce::jmin (blockMax, numSamples - start));
        }

        // ---- メーター（画面から読む） ----
        float getCorrelation() const { return correlation.load(); }
        float getGuardReductionDb() const { return guardDb.load(); }
        void copyScope (float* l, float* r, int n) const
        {
            const int w = scopeWrite.load();
            for (int i = 0; i < n; ++i)
            {
                const int idx = (w - n + i + scopeSize * 4) % scopeSize;
                l[i] = scopeL[(size_t) idx];
                r[i] = scopeR[(size_t) idx];
            }
        }

        // ---- AUTO ----
        void startAuto() { autoRequest = true; autoState = autoListening; autoProgress = 0.0f; }
        std::atomic<int> autoState { autoIdle };
        std::atomic<float> autoProgress { 0.0f };       // 秒
        std::atomic<float> autoResult[3] { 1.0f, 1.0f, 1.0f };

        // 帯域ごとの Side/Mid 比の目標（一般的な市販曲の目安、dB）
        static constexpr float autoTargetDb[3] = { -16.0f, -9.0f, -6.0f };
        static constexpr float autoMin[3] = { 0.5f, 0.6f, 0.6f };
        static constexpr float autoMax[3] = { 1.2f, 1.6f, 1.7f };

    private:
        void updateFilters (float fB, float fL, float fH)
        {
            fL = juce::jmax (fL, fB * 1.25f);
            fH = juce::jmax (fH, fL * 1.5f);
            fH = juce::jmin (fH, (float) sr * 0.4f);
            if (std::abs (fB - lastFB) > 1.0e-3f * fB)
            {
                xoB.setCutoffFrequency (fB); apMB.setCutoffFrequency (fB);
                lastFB = fB;
            }
            if (std::abs (fL - lastFL) > 1.0e-3f * fL)
            {
                for (auto* f : { &xoL, &apS0L, &apML, &anML }) f->setCutoffFrequency (fL);
                lastFL = fL;
            }
            if (std::abs (fH - lastFH) > 1.0e-3f * fH)
            {
                for (auto* f : { &xoH, &apS0H, &apS1H, &apMH, &anMH }) f->setCutoffFrequency (fH);
                lastFH = fH;
            }
        }

        void processChunk (float* L, float* R, int n)
        {
            if (autoRequest.exchange (false))
            {
                for (int b = 0; b < 3; ++b) autoSumM[b] = autoSumS[b] = 0.0;
                autoCount = 0;
                autoActive = true;
            }

            // ===== 1段目：帯域分け・幅・AIR・TRANSIENT =====
            constexpr int sub = 16;
            for (int i0 = 0; i0 < n; i0 += sub)
            {
                const int len = juce::jmin (sub, n - i0);
                const float fB = sFB.getNextValue(), fL = sFL.getNextValue(), fH = sFH.getNextValue();
                sFB.skip (len - 1); sFL.skip (len - 1); sFH.skip (len - 1);
                updateFilters (fB, fL, fH);

                const float airF = sAirF.getNextValue(); sAirF.skip (len - 1);
                const float airDb = sAirDb.getNextValue(); sAirDb.skip (len - 1);
                if (std::abs (airF - lastAirF) > 1.0e-3f * airF || std::abs (airDb - lastAirDb) > 1.0e-3f)
                {
                    air.setHighShelf (sr, airF, airDb);
                    lastAirF = airF; lastAirDb = airDb;
                }

                for (int i = i0; i < i0 + len; ++i)
                {
                    const float m = 0.5f * (L[i] + R[i]);
                    const float s = 0.5f * (L[i] - R[i]);

                    // Side を4帯域に（LR4 クロスオーバー、足すと元の Side のオールパス版）
                    float s0, r1, s1, r2, s2, s3;
                    xoB.processSample (0, s, s0, r1);
                    xoL.processSample (0, r1, s1, r2);
                    xoH.processSample (0, r2, s2, s3);
                    s0 = apS0H.processSample (0, apS0L.processSample (0, s0));
                    s1 = apS1H.processSample (0, s1);

                    // Mid も同じオールパスを通して Side と位相を揃える
                    const float mAp = apMH.processSample (0, apML.processSample (0, apMB.processSample (0, m)));

                    const float w = sW.getNextValue();
                    const float gL = 1.0f + w * (sWL.getNextValue() - 1.0f);
                    const float gM = 1.0f + w * (sWM.getNextValue() - 1.0f);
                    const float gH = 1.0f + w * (sWH.getNextValue() - 1.0f);
                    const float keepBass = 1.0f - sBass.getNextValue();

                    const float sDry = keepBass * s0 + s1 + s2 + s3;
                    float sw = keepBass * gL * s0 + gL * s1 + gM * s2 + gH * s3;

                    sw = air.process (sw);

                    // TRANSIENT：速い包絡と遅い包絡の差でアタックを検出
                    const float lvl = std::abs (m) + std::abs (s);
                    envFast = lvl + (lvl > envFast ? cFastA : cFastR) * (envFast - lvl);
                    envSlow = lvl + (lvl > envSlow ? cSlowA : cSlowR) * (envSlow - lvl);
                    const float ratioDb = 20.0f * std::log10 ((envFast + 1.0e-6f) / (envSlow + 1.0e-6f));
                    const float tr = juce::jlimit (0.0f, 1.0f, (ratioDb - 2.0f) / 8.0f);
                    lastTransient = tr;
                    sw *= juce::jmax (0.0f, 1.0f + sTrans.getNextValue() * tr);

                    bufM[(size_t) i] = mAp;
                    bufSd[(size_t) i] = sDry;
                    bufSw[(size_t) i] = sw;
                    bufX[(size_t) i] = driveHP.process (sw);

                    if (autoActive)
                    {
                        float mLo, mRest, mMid, mHi;
                        anML.processSample (0, m, mLo, mRest);
                        anMH.processSample (0, mRest, mMid, mHi);
                        if (m * m + s * s > 1.0e-7f)
                        {
                            const float sLo = s0 + s1;
                            autoSumM[0] += mLo * mLo;  autoSumS[0] += sLo * sLo;
                            autoSumM[1] += mMid * mMid; autoSumS[1] += s2 * s2;
                            autoSumM[2] += mHi * mHi;  autoSumS[2] += s3 * s3;
                            ++autoCount;
                        }
                    }
                }
            }

            if (autoActive)
            {
                autoProgress = (float) (autoCount / sr);
                if (autoCount >= (juce::int64) (autoSeconds * sr))
                {
                    autoActive = false;
                    double totS = 0.0, totM = 0.0;
                    for (int b = 0; b < 3; ++b) { totS += autoSumS[b]; totM += autoSumM[b]; }
                    if (totS < totM * 1.0e-4)   // Side が Mid より 40dB 以上小さい＝ほぼモノラル
                    {
                        autoState = autoMonoSource;
                    }
                    else
                    {
                        for (int b = 0; b < 3; ++b)
                        {
                            const double ratioDb = 10.0 * std::log10 ((autoSumS[b] + 1.0e-12) / (autoSumM[b] + 1.0e-12));
                            const double w = std::pow (10.0, (autoTargetDb[b] - ratioDb) / 20.0);
                            autoResult[b] = (float) juce::jlimit ((double) autoMin[b], (double) autoMax[b], w);
                        }
                        autoState = autoReady;
                    }
                }
            }

            // ===== 2段目：SIDE DRIVE（4倍オーバーサンプリングで歪みの“差分”だけ作る）=====
            {
                const float drv = sDrive.getTargetValue();
                const float k = 1.0f + 2.5f * drv;
                constexpr float bias = 0.3f;
                const float tb = std::tanh (bias);
                const float norm = 1.0f / (k * (1.0f - tb * tb));

                float* xp = bufX.data();
                juce::dsp::AudioBlock<float> inBlock (&xp, 1, (size_t) n);
                auto up = oversampling->processSamplesUp (inBlock);
                auto* u = up.getChannelPointer (0);
                for (size_t j = 0; j < up.getNumSamples(); ++j)
                {
                    const float x = u[j];
                    const float x2 = x * x;
                    osEnv = x2 + (x2 > osEnv ? cOsEnvA : cOsEnvR) * (osEnv - x2);
                    const float r = std::sqrt (osEnv) + 1.0e-3f;
                    const float v = x / r;
                    const float y = (std::tanh (k * v + bias) - tb) * norm;
                    u[j] = r * (y - v);
                }
                float* ep = bufE.data();
                juce::dsp::AudioBlock<float> outBlock (&ep, 1, (size_t) n);
                oversampling->processSamplesDown (outBlock);
            }

            // ===== 3段目：遅れを揃えて合成・GUARD・MIX・出力 =====
            float guardMin = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float m  = dlM.push (bufM[(size_t) i]);
                const float sd = dlSd.push (bufSd[(size_t) i]);
                const float swd = dlSw.push (bufSw[(size_t) i]);
                float s = swd + 0.8f * sDrive.getNextValue() * residueDC.process (bufE[(size_t) i]);

                // DRIVE で潰れた分の音量を戻す（Side の音量は保ったまま倍音だけ足す）
                dEnvIn  = swd * swd + cDriveEnv * (dEnvIn - swd * swd);
                dEnvOut = s * s     + cDriveEnv * (dEnvOut - s * s);
                if (std::abs (dEnvIn - dEnvOut) > 0.0f)
                    s *= juce::jlimit (0.5f, 2.0f, std::sqrt ((dEnvIn + 1.0e-12f) / (dEnvOut + 1.0e-12f)));

                // GUARD：Side が Mid に迫りすぎたら（相関 0.1 付近）Side を抑える。元の広さよりは狭めない
                gEnvM  = m * m   + cGuardEnv * (gEnvM - m * m);
                gEnvS  = s * s   + cGuardEnv * (gEnvS - s * s);
                gEnvSd = sd * sd + cGuardEnv * (gEnvSd - sd * sd);
                float gT = 1.0f;
                if (gEnvS > 1.0e-10f)
                {
                    const float limit = juce::jmax (0.81f * gEnvM, gEnvSd);
                    if (gEnvS > limit) gT = std::sqrt (limit / gEnvS);
                }
                gT = 1.0f + sGuardOn.getNextValue() * (gT - 1.0f);
                gGuard = gT + (gT < gGuard ? cGuardAtt : cGuardRel) * (gGuard - gT);
                s *= gGuard;
                guardMin = juce::jmin (guardMin, gGuard);

                const float mix = sMix.getNextValue();
                const float sOutV = mix * s + (1.0f - mix) * sd;

                const float g = sOut.getNextValue();
                float l = (m + sOutV) * g;
                float r = (m - sOutV) * g;
                const float mono = sMono.getNextValue();
                const float c = 0.5f * (l + r);
                l += mono * (c - l);
                r += mono * (c - r);
                L[i] = l; R[i] = r;

                mLR = l * r + cMeter * (mLR - l * r);
                mLL = l * l + cMeter * (mLL - l * l);
                mRR = r * r + cMeter * (mRR - r * r);

                const int w = scopeWrite.load (std::memory_order_relaxed);
                scopeL[(size_t) w] = l; scopeR[(size_t) w] = r;
                scopeWrite.store ((w + 1) % scopeSize, std::memory_order_relaxed);
            }

            const float e = mLL + mRR;
            correlation = e > 1.0e-9f ? juce::jlimit (-1.0f, 1.0f, mLR / std::sqrt (mLL * mRR + 1.0e-20f)) : 1.0f;
            guardDb = juce::Decibels::gainToDecibels (guardMin, -60.0f);
        }

    public:
        float lastTransient = 0.0f;   // テスト用

    private:
        double sr = 48000.0;
        int blockMax = 512, latency = 0;
        bool first = true;
        ShapeSettings settings;

        using LR = juce::dsp::LinkwitzRileyFilter<float>;
        LR xoB, xoL, xoH;                 // Side の帯域分け
        LR apS0L, apS0H, apS1H;           // 帯域の位相合わせ
        LR apMB, apML, apMH;              // Mid の位相合わせ
        LR anML, anMH;                    // AUTO 用の Mid 帯域分け

        Biquad air;
        OnePoleHP driveHP, residueDC;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        SimpleDelay dlM, dlSd, dlSw;
        std::vector<float> bufM, bufSd, bufSw, bufX, bufE;

        juce::SmoothedValue<float> sW, sWL, sWM, sWH, sBass, sAirDb, sDrive, sTrans, sGuardOn, sMix, sOut, sMono;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sFB, sFL, sFH, sAirF;
        float lastFB = -1, lastFL = -1, lastFH = -1, lastAirF = -1, lastAirDb = -1;

        float envFast = 0, envSlow = 0, cFastA = 0, cFastR = 0, cSlowA = 0, cSlowR = 0;
        float gEnvM = 0, gEnvS = 0, gEnvSd = 0, gGuard = 1, cGuardEnv = 0, cGuardAtt = 0, cGuardRel = 0;
        float osEnv = 0, cOsEnvA = 0, cOsEnvR = 0;
        float dEnvIn = 0, dEnvOut = 0, cDriveEnv = 0;
        float mLR = 0, mLL = 0, mRR = 0, cMeter = 0;

        std::atomic<float> correlation { 1.0f }, guardDb { 0.0f };
        std::array<float, scopeSize> scopeL {}, scopeR {};
        std::atomic<int> scopeWrite { 0 };

        std::atomic<bool> autoRequest { false };
        bool autoActive = false;
        double autoSumM[3] {}, autoSumS[3] {};
        juce::int64 autoCount = 0;
    };
}
