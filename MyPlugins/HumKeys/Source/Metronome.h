#pragma once
#include <JuceHeader.h>
#include <cmath>

// メトロノーム（クリック）とカウントイン。Mac の標準の出力（ヘッドホン推奨）から鳴らす。
// 鳴らしている「拍の目盛り」を覚えていて、テイクの音が何拍目だったかを出せる（Logic と同期していないときの拍）。
//  - 拍は 4 分音符 = 1（Logic の MIDI クロックと同じ数え方）
//  - カウントインの小節は負の拍、そのあとの 1 小節目の頭が 0 拍
//  - 時刻はすべて juce::Time::getMillisecondCounterHiRes() の ms
class Metronome : private juce::AudioIODeviceCallback
{
public:
    ~Metronome() override { close(); }

    void setSignature (int num, int den)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sigNum = juce::jlimit (1, 16, num); sigDen = den == 8 ? 8 : 4;
    }
    double barQuarters() const { const juce::SpinLock::ScopedLockType sl (lock); return sigNum * 4.0 / sigDen; }

    void setBpm (double b)
    {
        b = juce::jlimit (30.0, 300.0, b);
        const juce::SpinLock::ScopedLockType sl (lock);
        if (std::abs (b - bpm) < 1e-6) return;
        if (alive)
        {
            // 今の拍の位置はそのままで、テンポだけ変える
            const double now = juce::Time::getMillisecondCounterHiRes();
            anchorBeat = beatAtLocked (now);
            anchorMs = now;
        }
        bpm = b;
    }

    // 目盛りを始める。countInBars 小節のカウントのあと 0 拍目
    void start (bool audibleClick, int countInBars)
    {
        open();
        const juce::SpinLock::ScopedLockType sl (lock);
        const double lead = 120.0;   // 少し先から（出力の準備ぶん）
        anchorMs = juce::Time::getMillisecondCounterHiRes() + lead;
        anchorBeat = -countInBars * (sigNum * 4.0 / sigDen);
        startBeat = anchorBeat;
        audible = audibleClick;
        alive = true;
        lastUnit = std::numeric_limits<long>::min();
    }
    void setAudible (bool a)
    {
        if (a) open();
        const juce::SpinLock::ScopedLockType sl (lock);
        audible = a;
    }
    void setMuted (bool m) { muted = m; }   // Logic の再生中（Logic のクリックを使う）
    void stop()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        alive = false; audible = false;
    }
    void close()
    {
        stop();
        if (isOpen) { out.removeAudioCallback (this); out.closeAudioDevice(); isOpen = false; }
    }

    bool isAlive() const { const juce::SpinLock::ScopedLockType sl (lock); return alive; }
    bool isAudible() const { const juce::SpinLock::ScopedLockType sl (lock); return alive && audible; }
    double beatAt (double ms) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return alive ? beatAtLocked (ms) : std::nan ("");
    }
    double nowBeat() const { return beatAt (juce::Time::getMillisecondCounterHiRes()); }
    float volume = 0.5f;

private:
    juce::AudioDeviceManager out;
    bool isOpen = false;
    mutable juce::SpinLock lock;
    int sigNum = 4, sigDen = 4;
    double bpm = 120, anchorMs = 0, anchorBeat = 0, startBeat = 0;
    bool alive = false, audible = false;
    std::atomic<bool> muted { false };
    long lastUnit = 0;
    double sr = 48000;
    int latency = 0;
    double clickAge = 1e9, clickFreq = 1500, clickAmp = 0, clickPh = 0;

    double beatAtLocked (double ms) const { return anchorBeat + (ms - anchorMs) * bpm / 60000.0; }

    void open()
    {
        if (isOpen) return;
        out.initialise (0, 2, nullptr, true);
        out.addAudioCallback (this);
        isOpen = true;
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        sr = d->getCurrentSampleRate();
        latency = d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples();
    }
    void audioDeviceStopped() override {}

    // 6/8・12/8 は付点 4 分（8 分 3 つ）ごと、ほかの /8 は 8 分ごと、/4 は 4 分ごとに鳴らす
    static double unitOf (int num, int den) { return den == 8 ? ((num % 3 == 0 && num > 3) ? 1.5 : 0.5) : 1.0; }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outs, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) juce::FloatVectorOperations::clear (outs[c], n);
        double aMs, aBeat, b, barQ, unit, sBeat; bool on, live;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            aMs = anchorMs; aBeat = anchorBeat; b = bpm; live = alive; sBeat = startBeat;
            on = alive && audible;
            barQ = sigNum * 4.0 / sigDen; unit = unitOf (sigNum, sigDen);
        }
        if (! live && clickAge > 1.0) return;
        // このブロックの最初のサンプルが耳に届く時刻
        const double t0 = juce::Time::getMillisecondCounterHiRes() + (double) latency / sr * 1000.0;
        const double beatsPerSample = b / 60.0 / sr;
        double beat = aBeat + (t0 - aMs) * b / 60000.0;
        const float vol = volume;
        for (int i = 0; i < n; ++i, beat += beatsPerSample)
        {
            if (live)
            {
                const long u = (long) std::floor (beat / unit + 1e-9);
                if (u != lastUnit)
                {
                    const bool first = lastUnit == std::numeric_limits<long>::min();
                    lastUnit = u;
                    const double ub = (double) u * unit;
                    const bool counting = ub < -1e-9;   // カウントインの小節（負の拍）は ON でなくても鳴らす
                    if (! first && ub >= sBeat - 1e-9 && (on || counting) && ! muted.load())
                    {
                        const double inBar = std::fmod (std::fmod (ub, barQ) + barQ, barQ);
                        const bool accent = inBar < 1e-6 || barQ - inBar < 1e-6;
                        clickAge = 0; clickPh = 0;
                        clickFreq = accent ? 2000.0 : 1400.0;
                        clickAmp = accent ? 1.0 : 0.6;
                        if (counting) clickAmp *= 1.1;
                    }
                }
            }
            if (clickAge < 0.06)
            {
                clickPh += clickFreq / sr;
                const double env = std::exp (-clickAge / 0.012) * std::min (1.0, clickAge * sr / 8.0);
                const float s = (float) (std::sin (2 * M_PI * clickPh) * env * clickAmp * vol * 0.6);
                for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) outs[c][i] = s;
            }
            clickAge += 1.0 / sr;
        }
    }
};
