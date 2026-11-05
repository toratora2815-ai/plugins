#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

// Fader Pilot の音の処理（JUCE を使わない素の C++。単体でテストできる）
//
// 考え方：
//   コンプのように「大きい所を潰す」のではなく、人がフェーダーを上げ下げするのと同じように、
//   音全体の音量（ゲイン）だけをゆっくり動かして、声の大きさを TARGET に近づける。
//   入ってきた音を測って動かす（出口を測らない）ので、行ったり来たりの発振はしない。
//
// 流れ：
//   左右を足す → 声の帯域だけ（120 Hz〜8 kHz）を取り出す → 約 40 ms の RMS で「今の声の大きさ」
//   → 目標との差を RANGE の範囲に収める（FLOOR より小さい音＝息や無音のときは動かさない）
//   → SPEED の速さでフェーダーを動かす → OUTPUT
//   FOLLOW MUSIC のときは、サイドチェーン（オケ）の約 400 ms の RMS ＋ OFFSET が目標になる
namespace fp
{
    struct Settings
    {
        float targetDb = -18.0f;     // 揃えたい声の大きさ（RMS dBFS）
        float rangeDb = 6.0f;        // フェーダーを動かす最大量（±）
        float speedMs = 250.0f;      // 動きの速さ（目標の約 63% まで動く時間）
        float floorDb = -50.0f;      // これより小さい音は無視（動かさない）
        bool follow = false;         // オケの音量を目標にする
        float offsetDb = 0.0f;       // FOLLOW のとき、オケより何 dB 大きくするか
        bool autoRide = true;        // false のときは manualDb をそのまま使う
        float manualDb = 0.0f;       // AUTO OFF のときのフェーダー（オートメーション）
        float outDb = 0.0f;
    };

    // 2 次のフィルター（RBJ の式）
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void set (bool highPass, double f, double sr, double q = 0.7071)
        {
            const double w = 2.0 * M_PI * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            const double n = 1.0 / (1.0 + a);
            if (highPass) { b0 = (1 + c) * 0.5 * n; b1 = -(1 + c) * n; }
            else          { b0 = (1 - c) * 0.5 * n; b1 = (1 - c) * n; }
            b2 = b0;
            a1 = -2.0 * c * n;
            a2 = (1.0 - a) * n;
        }
        void reset() { z1 = z2 = 0; }
        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    class RiderEngine
    {
    public:
        static constexpr int maxChannels = 2;

        // 画面用の履歴（約 10 ms ごとに 1 点）
        struct Point { float voiceDb, outDb, gainDb, targetDb; bool active; };
        static constexpr int historySize = 1024;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            voiceHp.set (true, 120.0, sr);
            voiceLp.set (false, 8000.0, sr);
            musicHp.set (true, 60.0, sr);
            voiceCoef = coefFor (0.040);
            musicCoef = coefFor (0.400);
            paramCoef = coefFor (0.020);
            linkCoef = coefFor (0.050);   // Balance Scope 側で 0.4 秒ならしてあるので、ここはブロックの段差を消すだけ
            pointEvery = std::max (1, (int) std::lround (sr * 0.010));
            reset();
        }

        void reset()
        {
            voiceHp.reset(); voiceLp.reset(); musicHp.reset();
            voiceMs = 0.0; musicMs = 0.0; linkMs = 0.0;
            gainDb = settings.autoRide ? 0.0f : settings.manualDb;
            manualCur = settings.manualDb;
            outCur = std::pow (10.0f, settings.outDb / 20.0f);
            ptVoice = ptOut = 0.0; ptCount = 0; ptActive = false;
        }

        void setSettings (const Settings& s) { settings = s; }

        // Plugin Link で受け取ったオケの音量（Balance Scope から。ok = 新しい値が来ている）。
        // サイドチェーンに音が来ていないときだけ、こちらを FOLLOW MUSIC に使う
        void setLinkMusic (bool ok, float db) { linkOk = ok; linkTargetMs = ok ? std::pow (10.0, db / 10.0) : 0.0; }
        bool isMusicFromLink() const { return currentFromLink.load (std::memory_order_relaxed); }

        // ch はその場で書き換える。sc はサイドチェーン（なければ nullptr / 0）。読むだけ
        void process (float* const* ch, int numCh, int numSamples, const float* const* sc = nullptr, int numSc = 0)
        {
            numCh = std::clamp (numCh, 1, maxChannels);
            numSc = sc != nullptr ? std::clamp (numSc, 0, maxChannels) : 0;

            const float range = std::max (0.0f, settings.rangeDb);
            const double speedCoef = coefFor (std::max (5.0f, settings.speedMs) * 0.001);
            const float outTarget = std::pow (10.0f, settings.outDb / 20.0f);

            for (int i = 0; i < numSamples; ++i)
            {
                // 声の大きさ
                float x = 0.0f;
                for (int c = 0; c < numCh; ++c) x += ch[c][i];
                x /= (float) numCh;
                const float v = voiceLp.process (voiceHp.process (x));
                voiceMs += ((double) v * v - voiceMs) * voiceCoef;

                // オケの大きさ（サイドチェーン）
                if (numSc > 0)
                {
                    float m = 0.0f;
                    for (int c = 0; c < numSc; ++c) m += sc[c][i];
                    m = musicHp.process (m / (float) numSc);
                    musicMs += ((double) m * m - musicMs) * musicCoef;
                }
                else musicMs += (0.0 - musicMs) * musicCoef;
                linkMs += (linkTargetMs - linkMs) * linkCoef;

                const float voiceDb = toDb (voiceMs);
                const bool scHas = numSc > 0 && toDb (musicMs) > -60.0f;
                fromLink = ! scHas && linkOk;
                const float musicDb = toDb (fromLink ? linkMs : musicMs);
                const bool musicOk = musicDb > -60.0f;
                const float target = settings.follow ? (musicOk ? musicDb + settings.offsetDb : lastTarget)
                                                     : settings.targetDb;
                lastTarget = target;
                const bool active = voiceDb > settings.floorDb && (! settings.follow || musicOk);
                lastActive = active;

                manualCur += (settings.manualDb - manualCur) * (float) paramCoef;
                if (settings.autoRide)
                {
                    // 小さい音（息・無音）やオケが止まっている間は、今の位置で止めておく
                    if (active)
                    {
                        const float want = std::clamp (target - voiceDb, -range, range);
                        gainDb += (float) ((want - gainDb) * speedCoef);
                    }
                    gainDb = std::clamp (gainDb, -range, range);   // RANGE を狭めたとき
                }
                else gainDb += (manualCur - gainDb) * (float) paramCoef;   // AUTO を切ったときに段差が出ないように

                outCur += (outTarget - outCur) * (float) paramCoef;
                const float g = std::pow (10.0f, gainDb / 20.0f) * outCur;
                for (int c = 0; c < numCh; ++c) ch[c][i] *= g;

                // 画面用の点
                ptVoice += voiceMs;
                ptOut += voiceMs * (double) g * g;
                ptActive = ptActive || active;
                if (++ptCount >= pointEvery)
                {
                    pushPoint ({ toDb (ptVoice / ptCount), toDb (ptOut / ptCount), gainDb, target, ptActive });
                    ptVoice = ptOut = 0.0; ptCount = 0; ptActive = false;
                }
            }
            currentGain.store (gainDb, std::memory_order_relaxed);
            currentActive.store (lastActive, std::memory_order_relaxed);
            currentMusic.store (fromLink ? toDb (linkMs) : (numSc > 0 ? musicDb() : -120.0f), std::memory_order_relaxed);
            currentFromLink.store (fromLink, std::memory_order_relaxed);
        }

        // 今のフェーダー位置（dB）。オートメーションへの書き込みと画面で使う
        float getGainDb() const { return currentGain.load (std::memory_order_relaxed); }
        float getMusicDb() const { return currentMusic.load (std::memory_order_relaxed); }
        bool isVoiceActive() const { return currentActive.load (std::memory_order_relaxed); }   // 声が FLOOR より大きいか

        // 画面用：新しい点を古い順に dst へ。読んだ数を返す（readPos は呼ぶ側が持つ）
        int readHistory (Point* dst, int maxPoints, int& readPos) const
        {
            const int w = writePos.load (std::memory_order_acquire);
            if (w - readPos > historySize) readPos = w - historySize;
            int n = 0;
            while (readPos < w && n < maxPoints)
                dst[n++] = history[(size_t) (readPos++ % historySize)];
            return n;
        }

    private:
        double coefFor (double seconds) const { return 1.0 - std::exp (-1.0 / (seconds * sr)); }
        static float toDb (double ms) { return (float) (10.0 * std::log10 (ms + 1.0e-12)); }
        float musicDb() const { return toDb (musicMs); }

        void pushPoint (const Point& p)
        {
            const int w = writePos.load (std::memory_order_relaxed);
            history[(size_t) (w % historySize)] = p;
            writePos.store (w + 1, std::memory_order_release);
        }

        Settings settings;
        double sr = 48000.0;
        Biquad voiceHp, voiceLp, musicHp;
        double voiceMs = 0.0, musicMs = 0.0, voiceCoef = 0.0, musicCoef = 0.0, paramCoef = 0.0;
        float gainDb = 0.0f, manualCur = 0.0f, outCur = 1.0f, lastTarget = -18.0f;
        int pointEvery = 480;

        double ptVoice = 0.0, ptOut = 0.0;
        int ptCount = 0;
        bool ptActive = false;

        Point history[historySize] {};
        std::atomic<int> writePos { 0 };
        std::atomic<float> currentGain { 0.0f }, currentMusic { -120.0f };
        std::atomic<bool> currentActive { false }, currentFromLink { false };
        bool linkOk = false, fromLink = false;
        double linkMs = 0.0, linkTargetMs = 0.0, linkCoef = 0.0;
        bool lastActive = false;
    };
}
