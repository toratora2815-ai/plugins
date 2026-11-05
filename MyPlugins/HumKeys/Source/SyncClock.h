#pragma once
#include <JuceHeader.h>
#include <cmath>

// Logic から送られてくる MIDI クロック（テンポと再生位置の信号）を受け取る。
// 送り先は仮想 MIDI「Hum Keys」。Logic の「プロジェクト設定 → 同期 → MIDI」で MIDI クロックの送信先に選ぶ。
//  - テンポ: クロック（1 拍 24 回）の間隔から
//  - 再生位置: スタート / ソングポジションポインタ（16 分音符単位）＋ クロックの数
// 時刻はすべて juce::Time::getMillisecondCounterHiRes() の ms（JUCE が MIDI の時刻もこれにそろえる）
class SyncClock : public juce::MidiInputCallback
{
public:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        const auto* d = m.getRawData();
        if (m.getRawDataSize() < 1) return;
        const double ms = m.getTimeStamp() * 1000.0;
        const juce::SpinLock::ScopedLockType sl (lock);
        switch (d[0])
        {
            case 0xFA: running = true; pos = -1.0 / 24.0; ++segment; break;                         // スタート（頭から）
            case 0xFB: running = true; if (sppSet) pos = sppBeats - 1.0 / 24.0; ++segment; break;   // 続きから
            case 0xFC: running = false; stopCount++; break;                                         // ストップ
            case 0xF2:                                                                               // ソングポジション
                if (m.getRawDataSize() >= 3)
                {
                    sppBeats = (double) ((int) d[1] | ((int) d[2] << 7)) / 4.0;
                    sppSet = true;
                    pos = sppBeats - 1.0 / 24.0;
                    ++segment;
                }
                break;
            case 0xF8:                                                                               // クロック
            {
                if (lastTickMs > 0)
                {
                    const double dt = ms - lastTickMs;
                    if (dt > 2.0 && dt < 250.0) avgTickMs = avgTickMs <= 0 ? dt : avgTickMs * 0.92 + dt * 0.08;
                }
                lastTickMs = ms;
                if (running)
                {
                    pos += 1.0 / 24.0;
                    const int i = head;
                    tMs[(size_t) i] = ms; tPos[(size_t) i] = pos; tSeg[(size_t) i] = segment;
                    head = (head + 1) % N;
                    count = std::min (count + 1, N);
                }
                break;
            }
            default: break;
        }
    }

    // 今のテンポ（クロックが最近来ていれば）。来ていなければ 0
    double bpm() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (avgTickMs <= 0 || juce::Time::getMillisecondCounterHiRes() - lastTickMs > 2000.0) return 0;
        return 60000.0 / (avgTickMs * 24.0);
    }
    bool isRunning() const { const juce::SpinLock::ScopedLockType sl (lock); return running && juce::Time::getMillisecondCounterHiRes() - lastTickMs < 500.0; }
    int stops() const { const juce::SpinLock::ScopedLockType sl (lock); return stopCount; }
    bool everReceived() const { const juce::SpinLock::ScopedLockType sl (lock); return lastTickMs > 0; }

    // ある時刻が曲の何拍目だったか（再生中でなければ NaN）
    double beatAt (double ms) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (count == 0) return std::nan ("");
        const double tick = avgTickMs > 0 ? avgTickMs : 20.0;
        int newer = -1;
        for (int k = 0; k < count; ++k)
        {
            const int i = ((head - 1 - k) % N + N) % N;
            if (tMs[(size_t) i] <= ms)
            {
                if (newer >= 0 && tSeg[(size_t) newer] == tSeg[(size_t) i])
                {
                    const double f = (ms - tMs[(size_t) i]) / std::max (0.001, tMs[(size_t) newer] - tMs[(size_t) i]);
                    return tPos[(size_t) i] + f * (tPos[(size_t) newer] - tPos[(size_t) i]);
                }
                // その区切りの最後のクロックより後：3 クロック分までなら延ばす
                if (ms - tMs[(size_t) i] <= tick * 3.0)
                    return tPos[(size_t) i] + (ms - tMs[(size_t) i]) / (tick * 24.0);
                return std::nan ("");
            }
            newer = i;
        }
        return std::nan ("");
    }

private:
    static constexpr int N = 16384;   // 120 BPM で約 5 分ぶん
    mutable juce::SpinLock lock;
    std::array<double, N> tMs {}, tPos {};
    std::array<int, N> tSeg {};
    int head = 0, count = 0, segment = 0, stopCount = 0;
    bool running = false, sppSet = false;
    double pos = 0, sppBeats = 0, lastTickMs = 0, avgTickMs = 0;
};
