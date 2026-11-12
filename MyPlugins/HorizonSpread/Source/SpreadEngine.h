#pragma once
#include "DspParts.h"
#include "CreateEngine.h"

// Horizon Spread の音の処理（パラメーターや画面とは切り離してあるので、単体でテストできる）
//
// 基本の考え方：
//   L/R を Mid（真ん中）と Side（左右の差）に分け、Mid は触らずに Side だけを加工する。
//   こうするとモノラルにしたとき Side は打ち消し合って消え、元の音がそのまま残る。
//
// 流れ：
//   入力 → CREATE（生み出す側、CreateEngine.h）→ M/S → Side を4帯域に分ける（BASS MONO / LOW / MID / HIGH）→ 帯域ごとの幅
//        → AIR（Side の高域シェルフ）→ TRANSIENT（アタックで Side を増減）
//        → SIDE DRIVE（Side の倍音、4倍オーバーサンプリング）→ DYNAMIC → GUARD → MIX
//        → AUTO GAIN（元の音と同じ音量に戻す）→ OUTPUT → SIDE SOLO / MONO CHECK → 出力
//   MIX と GUARD の基準になる「元の音」は、CREATE を通す前の入力
namespace hs
{
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
        float dynamic = 0.0f;        // 0..1  静かなところは広く、大きいところは少し狭く

        float bassMonoHz = 20.0f;    // 20 以下 = OFF
        bool  guard = true;
        bool  mono = false;
        bool  sideSolo = false;
        bool  autoGain = true;

        CreateSettings create;
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
            for (auto* f : { &xoB, &xoL, &xoH, &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH, &anML, &anMH,
                             &apDMB, &apDML, &apDMH, &apDSB, &apDSL, &apDSH, &mtML, &mtMH, &mtSL, &mtSH })
            {
                f->prepare (spec);
                f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            }
            for (auto* f : { &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH, &apDMB, &apDML, &apDMH, &apDSB, &apDSL, &apDSH })
                f->setType (juce::dsp::LinkwitzRileyFilterType::allpass);
            create.prepare (sr);

            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            oversampling->initProcessing ((size_t) blockMax);
            latency = (int) std::round (oversampling->getLatencyInSamples());

            for (auto* d : { &dlM, &dlMd, &dlSd, &dlSw })
                d->setLength (latency);

            for (auto* b : { &bufM, &bufMd, &bufSd, &bufSw, &bufX, &bufE })
                b->assign ((size_t) blockMax, 0.0f);

            driveHP.setCutoff (200.0, sr);
            residueDC.setCutoff (25.0, sr);

            // スムージング時間
            for (auto* s : { &sW, &sWL, &sWM, &sWH, &sBass, &sAirDb, &sDrive, &sTrans, &sGuardOn, &sMix, &sOut, &sMono,
                             &sSolo, &sAutoGain, &sDyn })
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
            cAgEnv = coefFor (400.0, sr); cAgGain = coefFor (300.0, sr);
            cDynA = coefFor (20.0, sr); cDynR = coefFor (250.0, sr); cDynLong = coefFor (3000.0, sr);

            first = true;
            reset();
        }

        void reset()
        {
            for (auto* f : { &xoB, &xoL, &xoH, &apS0L, &apS0H, &apS1H, &apMB, &apML, &apMH, &anML, &anMH,
                             &apDMB, &apDML, &apDMH, &apDSB, &apDSL, &apDSH, &mtML, &mtMH, &mtSL, &mtSH })
                f->reset();
            if (oversampling) oversampling->reset();
            for (auto* d : { &dlM, &dlMd, &dlSd, &dlSw }) d->reset();
            create.reset();
            air.reset(); driveHP.reset(); residueDC.reset();
            envFast = envSlow = 0; gEnvM = gEnvS = gEnvSd = 0; gGuard = 1;
            mLR = mLL = mRR = 0; osEnv = 0; dEnvIn = dEnvOut = 0;
            agIn = agOut = 0; agGain = 1; dynShort = dynLong = 0;
            for (int b = 0; b < 3; ++b) bandM[b] = bandS[b] = 0;
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
            set (sSolo, s.sideSolo ? 1.0f : 0.0f);
            set (sAutoGain, s.autoGain ? 1.0f : 0.0f);
            set (sDyn, s.transOn ? s.dynamic * sp : 0.0f);
            create.setSettings (s.create, sp);
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
        float getAutoGainDb() const { return autoGainDb.load(); }
        // 帯域ごとの広さ（Side と Mid の音量差、dB）。0 = LOW, 1 = MID, 2 = HIGH
        float getBandWidthDb (int band) const { return bandWidthDb[band].load(); }
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
                for (auto* f : { &xoB, &apMB, &apDMB, &apDSB }) f->setCutoffFrequency (fB);
                lastFB = fB;
            }
            if (std::abs (fL - lastFL) > 1.0e-3f * fL)
            {
                for (auto* f : { &xoL, &apS0L, &apML, &anML, &apDML, &apDSL, &mtML, &mtSL }) f->setCutoffFrequency (fL);
                lastFL = fL;
            }
            if (std::abs (fH - lastFH) > 1.0e-3f * fH)
            {
                for (auto* f : { &xoH, &apS0H, &apS1H, &apMH, &anMH, &apDMH, &apDSH, &mtMH, &mtSH }) f->setCutoffFrequency (fH);
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
                    // 元の音（MIX と GUARD の基準）。下の帯域分けと同じ位相回転を通しておく
                    const float mIn = 0.5f * (L[i] + R[i]);
                    const float sIn = 0.5f * (L[i] - R[i]);
                    const float mDry = apDMH.processSample (0, apDML.processSample (0, apDMB.processSample (0, mIn)));
                    const float sDry = apDSH.processSample (0, apDSL.processSample (0, apDSB.processSample (0, sIn)));

                    // CREATE：Mid から新しい広がりを作る
                    float m, s;
                    create.process (L[i], R[i], m, s);

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
                    bufMd[(size_t) i] = mDry;
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
                const float md = dlMd.push (bufMd[(size_t) i]);
                const float sd = dlSd.push (bufSd[(size_t) i]);
                const float swd = dlSw.push (bufSw[(size_t) i]);
                float s = swd + 0.8f * sDrive.getNextValue() * residueDC.process (bufE[(size_t) i]);

                // DRIVE で潰れた分の音量を戻す（Side の音量は保ったまま倍音だけ足す）
                dEnvIn  = swd * swd + cDriveEnv * (dEnvIn - swd * swd);
                dEnvOut = s * s     + cDriveEnv * (dEnvOut - s * s);
                if (std::abs (dEnvIn - dEnvOut) > 0.0f)
                    s *= juce::jlimit (0.5f, 2.0f, std::sqrt ((dEnvIn + 1.0e-12f) / (dEnvOut + 1.0e-12f)));

                // DYNAMIC：元の音が普段より静かなら Side を最大 +40%、大きければ最大 -50%
                {
                    const float e = md * md + sd * sd;
                    dynShort = e + (e > dynShort ? cDynA : cDynR) * (dynShort - e);
                    dynLong  = e + cDynLong * (dynLong - e);
                    const float dyn = sDyn.getNextValue();
                    if (dyn > 1.0e-6f && dynLong > 1.0e-9f)
                    {
                        const float diffDb = 10.0f * std::log10 ((dynShort + 1.0e-12f) / dynLong);
                        const float k = diffDb < 0.0f ? 0.4f * juce::jmin (1.0f, -diffDb / 12.0f)
                                                      : -0.5f * juce::jmin (1.0f, diffDb / 6.0f);
                        s *= 1.0f + dyn * k;
                    }
                }

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
                const float mOutV = mix * m + (1.0f - mix) * md;

                // AUTO GAIN：広げて大きくなった分を、元の音と同じ音量に戻す（±6 dB まで、ゆっくり）
                const float eIn = md * md + sd * sd, eOut = mOutV * mOutV + sOutV * sOutV;
                agIn  = eIn  + cAgEnv * (agIn - eIn);
                agOut = eOut + cAgEnv * (agOut - eOut);
                if (agIn > 1.0e-8f && agOut > 1.0e-8f)
                {
                    const float target = juce::jlimit (0.5f, 2.0f, std::sqrt (agIn / agOut));
                    agGain = target + cAgGain * (agGain - target);
                }
                const float agOn = sAutoGain.getNextValue();
                const float ag = 1.0f + agOn * (agGain - 1.0f);

                // 帯域ごとの広さメーター
                {
                    float m0, mr, m1, m2, s0, sr0, s1, s2;
                    mtML.processSample (0, mOutV, m0, mr); mtMH.processSample (0, mr, m1, m2);
                    mtSL.processSample (0, sOutV, s0, sr0); mtSH.processSample (0, sr0, s1, s2);
                    const float mb[3] { m0, m1, m2 }, sb[3] { s0, s1, s2 };
                    for (int b = 0; b < 3; ++b)
                    {
                        bandM[b] = mb[b] * mb[b] + cMeter * (bandM[b] - mb[b] * mb[b]);
                        bandS[b] = sb[b] * sb[b] + cMeter * (bandS[b] - sb[b] * sb[b]);
                    }
                }

                // GAIN MATCH が ON の間は、OUTPUT でも音量を変えない（ほかのプラグインの GAIN MATCH と同じ「入れても・つまみをいじっても音量が変わらない」。2026-11-12）
                const float outV = sOut.getNextValue();
                const float g = (outV + agOn * (1.0f - outV)) * ag;
                float l = (mOutV + sOutV) * g;
                float r = (mOutV - sOutV) * g;
                const float solo = sSolo.getNextValue();
                l += solo * (sOutV * g - l);
                r += solo * (sOutV * g - r);
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
            autoGainDb = juce::Decibels::gainToDecibels (1.0f + sAutoGain.getCurrentValue() * (agGain - 1.0f));
            for (int b = 0; b < 3; ++b)
                bandWidthDb[b] = bandM[b] + bandS[b] > 1.0e-9f
                                   ? 10.0f * std::log10 ((bandS[b] + 1.0e-12f) / (bandM[b] + 1.0e-12f)) : -60.0f;
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
        LR apDMB, apDML, apDMH;           // 元の音（Mid）の位相合わせ
        LR apDSB, apDSL, apDSH;           // 元の音（Side）の位相合わせ
        LR mtML, mtMH, mtSL, mtSH;        // 帯域ごとの広さメーター

        CreateEngine create;

        Biquad air;
        OnePoleHP driveHP, residueDC;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        SimpleDelay dlM, dlMd, dlSd, dlSw;
        std::vector<float> bufM, bufMd, bufSd, bufSw, bufX, bufE;

        juce::SmoothedValue<float> sW, sWL, sWM, sWH, sBass, sAirDb, sDrive, sTrans, sGuardOn, sMix, sOut, sMono,
                                   sSolo, sAutoGain, sDyn;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sFB, sFL, sFH, sAirF;
        float lastFB = -1, lastFL = -1, lastFH = -1, lastAirF = -1, lastAirDb = -1;

        float envFast = 0, envSlow = 0, cFastA = 0, cFastR = 0, cSlowA = 0, cSlowR = 0;
        float gEnvM = 0, gEnvS = 0, gEnvSd = 0, gGuard = 1, cGuardEnv = 0, cGuardAtt = 0, cGuardRel = 0;
        float osEnv = 0, cOsEnvA = 0, cOsEnvR = 0;
        float dEnvIn = 0, dEnvOut = 0, cDriveEnv = 0;
        float mLR = 0, mLL = 0, mRR = 0, cMeter = 0;
        float agIn = 0, agOut = 0, agGain = 1, cAgEnv = 0, cAgGain = 0;
        float dynShort = 0, dynLong = 0, cDynA = 0, cDynR = 0, cDynLong = 0;
        float bandM[3] {}, bandS[3] {};

        std::atomic<float> correlation { 1.0f }, guardDb { 0.0f }, autoGainDb { 0.0f };
        std::atomic<float> bandWidthDb[3] { -60.0f, -60.0f, -60.0f };
        std::array<float, scopeSize> scopeL {}, scopeR {};
        std::atomic<int> scopeWrite { 0 };

        std::atomic<bool> autoRequest { false };
        bool autoActive = false;
        double autoSumM[3] {}, autoSumS[3] {};
        juce::int64 autoCount = 0;
    };
}
