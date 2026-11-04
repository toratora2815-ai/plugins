#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

// Gain Stage の音の処理（JUCE を使わない素の C++。単体でテストできる）
//
// 考え方：
//   トラックの入口で音量を「ちょうどいい大きさ」にそろえるだけのプラグイン。
//   LEARN を押すと、音が鳴っている所だけを数秒測って、TARGET になるゲインを 1 回だけ決める。
//   決めたあとは自動では動かない（Fader Pilot のように追いかけない）。
//
// 流れ：極性反転 → 左右入れ替え → モノラル → GAIN（なめらかに変わる） → 出口
//   測るのはゲインをかける前の音（GAIN の値に関係なく同じ結果になるように）
namespace gs
{
    struct Settings
    {
        float gainDb = 0.0f;
        bool polarity = false;   // 左右とも極性を反転
        bool swap = false;       // 左右を入れ替え
        bool mono = false;       // 左右を足して 2 で割る
    };

    class GainEngine
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr double learnSeconds = 5.0;     // 音が鳴っている時間でこれだけ測ったら終わり
        static constexpr double gateDb = -60.0;         // 100 ms のかたまりがこれより小さければ数えない（無音・息）

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            blockLen = std::max (1, (int) std::lround (sr * 0.1));
            gainCoef = 1.0 - std::exp (-1.0 / (0.05 * sr));     // ゲインの変化は約 50 ms でなめらかに
            meterCoef = 1.0 - std::exp (-1.0 / (0.3 * sr));     // メーターの RMS は約 300 ms
            gainCur = std::pow (10.0, settings.gainDb / 20.0);
            reset();
        }

        void reset()
        {
            inMs = outMs = 0.0;
            inPeakHold = outPeakHold = 0.0f;
            blockSum = 0.0; blockPeak = 0.0f; blockCount = 0;
        }

        void setSettings (const Settings& s) { settings = s; }

        // LEARN（メッセージスレッドから）
        void startLearn()
        {
            learnReset.store (true);
            learning.store (true);
            learnDone.store (false);
        }
        // 途中で終わらせる（それまで測った分で決める）
        void finishLearn() { learnStopNow.store (true); }
        bool isLearning() const { return learning.load(); }
        double learnProgress() const { return std::min (1.0, learnActiveSec.load() / learnSeconds); }

        // 測り終わったら true を 1 回だけ返す。rmsDb / peakDb に測った値（ゲインをかける前）
        bool takeLearnResult (float& rmsDb, float& peakDb)
        {
            if (! learnDone.exchange (false)) return false;
            rmsDb = learnRmsDb.load();
            peakDb = learnPeakDb.load();
            return true;
        }

        void process (float* const* ch, int numCh, int numSamples)
        {
            numCh = std::clamp (numCh, 1, maxChannels);
            const double gainTarget = std::pow (10.0, settings.gainDb / 20.0);
            const float pol = settings.polarity ? -1.0f : 1.0f;

            if (learnReset.exchange (false))
            {
                lSum = 0.0; lBlocks = 0; lPeak = 0.0f;
                blockSum = 0.0; blockPeak = 0.0f; blockCount = 0;
                learnActiveSec.store (0.0);
            }
            const bool isLearning = learning.load();

            for (int i = 0; i < numSamples; ++i)
            {
                float l = ch[0][i], r = numCh > 1 ? ch[1][i] : l;

                // 測る（入ってきたまま、ゲインの前）
                const double e = numCh > 1 ? 0.5 * ((double) l * l + (double) r * r) : (double) l * l;
                const float pk = std::max (std::abs (l), std::abs (r));
                inMs += (e - inMs) * meterCoef;
                inPeakHold = std::max (inPeakHold, pk);

                if (isLearning)
                {
                    blockSum += e;
                    blockPeak = std::max (blockPeak, pk);
                    if (++blockCount >= blockLen)
                    {
                        const double ms = blockSum / blockCount;
                        if (10.0 * std::log10 (ms + 1e-20) > gateDb)
                        {
                            lSum += ms;
                            ++lBlocks;
                            lPeak = std::max (lPeak, blockPeak);
                            learnActiveSec.store (lBlocks * 0.1);
                        }
                        blockSum = 0.0; blockPeak = 0.0f; blockCount = 0;
                    }
                }

                // 加工
                if (settings.swap && numCh > 1) std::swap (l, r);
                if (settings.mono && numCh > 1) { const float m = 0.5f * (l + r); l = r = m; }
                gainCur += (gainTarget - gainCur) * gainCoef;
                const float g = (float) gainCur * pol;
                l *= g; r *= g;

                ch[0][i] = l;
                if (numCh > 1) ch[1][i] = r;

                const double eo = numCh > 1 ? 0.5 * ((double) l * l + (double) r * r) : (double) l * l;
                outMs += (eo - outMs) * meterCoef;
                outPeakHold = std::max (outPeakHold, std::max (std::abs (l), std::abs (r)));
            }

            if (isLearning && (lBlocks * 0.1 >= learnSeconds || learnStopNow.exchange (false)))
            {
                learnRmsDb.store (lBlocks > 0 ? (float) (10.0 * std::log10 (lSum / lBlocks)) : -200.0f);
                learnPeakDb.store (lPeak > 0.0f ? 20.0f * std::log10 (lPeak) : -200.0f);
                learnDone.store (lBlocks > 0);   // learning を下ろす前に（画面側が「無音だった」と見間違えないように）
                learning.store (false);
            }

            meterIn.store ((float) (10.0 * std::log10 (inMs + 1e-20)));
            meterOut.store ((float) (10.0 * std::log10 (outMs + 1e-20)));
            peakIn.store (inPeakHold > 0.0f ? 20.0f * std::log10 (inPeakHold) : -200.0f);
            peakOut.store (outPeakHold > 0.0f ? 20.0f * std::log10 (outPeakHold) : -200.0f);
            // ピークはメーター用に少しずつ下げる（約 20 dB/秒）
            const float fall = std::pow (10.0f, -20.0f * (float) numSamples / (float) sr / 20.0f);
            inPeakHold *= fall;
            outPeakHold *= fall;
        }

        // メーター（dBFS。RMS はフルスケールのサイン波で -3 dB）
        float getInRms() const   { return meterIn.load(); }
        float getOutRms() const  { return meterOut.load(); }
        float getInPeak() const  { return peakIn.load(); }
        float getOutPeak() const { return peakOut.load(); }

    private:
        Settings settings;
        double sr = 48000.0, gainCoef = 0.001, meterCoef = 0.0001, gainCur = 1.0;
        double inMs = 0.0, outMs = 0.0;
        float inPeakHold = 0.0f, outPeakHold = 0.0f;

        int blockLen = 4800, blockCount = 0;
        double blockSum = 0.0;
        float blockPeak = 0.0f;
        double lSum = 0.0;
        long lBlocks = 0;
        float lPeak = 0.0f;

        std::atomic<bool> learning { false }, learnReset { false }, learnStopNow { false }, learnDone { false };
        std::atomic<double> learnActiveSec { 0.0 };
        std::atomic<float> learnRmsDb { -200.0f }, learnPeakDb { -200.0f };
        std::atomic<float> meterIn { -200.0f }, meterOut { -200.0f }, peakIn { -200.0f }, peakOut { -200.0f };
    };
}
