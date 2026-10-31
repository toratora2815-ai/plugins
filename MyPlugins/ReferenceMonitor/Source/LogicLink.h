#pragma once
// Logic Pro を外から操作する（再生・停止・位置移動）＋ Logic の再生位置を受け取る。
// 仕組み: このアプリが「Reference Monitor」という仮想 MIDI ポートを作り、
//   ・アプリ → Logic : MMC（再生／停止）と Full Frame（位置移動）
//   ・Logic → アプリ : MTC（タイムコード＝今の再生位置）
// Logic 側では「プロジェクト設定 > 同期 > MIDI」で
//   「MMC 入力を受信」をオン、「MTC を送信」の送信先を Reference Monitor にする（プロジェクトごとの設定）
#include <JuceHeader.h>

class LogicLink : private juce::MidiInputCallback
{
public:
    LogicLink()
    {
        output = juce::MidiOutput::createNewDevice ("Reference Monitor");
        input  = juce::MidiInput::createNewDevice ("Reference Monitor", this);
        if (input != nullptr) input->start();
    }

    ~LogicLink() override
    {
        if (input != nullptr) input->stop();
    }

    bool portsOk() const { return output != nullptr && input != nullptr; }

    // ---- 操作 ----
    void play()  { sendMMC (0x02); }
    void stop()  { sendMMC (0x01); }

    // 指定位置（SMPTE の秒）へ移動
    void locate (double seconds)
    {
        if (output == nullptr) return;
        const int rate = rateCode.load();
        seconds = juce::jmax (0.0, seconds);
        const auto t = toHMSF (seconds, rate);
        const int hr = t[0], mn = t[1], sc = t[2], fr = t[3];
        // Full Frame（Logic はこれで位置移動する）
        const juce::uint8 ff[] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, (juce::uint8) ((rate << 5) | hr), (juce::uint8) mn, (juce::uint8) sc, (juce::uint8) fr, 0xF7 };
        output->sendMessageNow (juce::MidiMessage (ff, (int) sizeof (ff)));
        // 念のため MMC Locate も送る
        const juce::uint8 loc[] = { 0xF0, 0x7F, 0x7F, 0x06, 0x44, 0x06, 0x01, (juce::uint8) ((rate << 5) | hr), (juce::uint8) mn, (juce::uint8) sc, (juce::uint8) fr, 0x00, 0xF7 };
        output->sendMessageNow (juce::MidiMessage (loc, (int) sizeof (loc)));
        lastPosition = seconds;
        lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
    }

    void locateAndPlay (double seconds)
    {
        stop();
        locate (seconds);
        play();
    }

    // ---- 状態 ----
    // Logic から MTC が届いたことがあるか（＝同期設定がオン）
    bool hasTimecode() const { return everReceived.load(); }
    // 再生中か（MTC が流れ続けているか）
    bool isPlaying() const { return juce::Time::getMillisecondCounterHiRes() - lastQuarterMs.load() < 300.0; }
    // 今の再生位置（SMPTE の秒）。再生中は最後に受け取ってからの経過分を足す
    double getPosition() const
    {
        double p = lastPosition.load();
        if (isPlaying()) p += (juce::Time::getMillisecondCounterHiRes() - lastUpdateMs.load()) * 0.001;
        return p;
    }

    static juce::String format (double smpteSeconds)
    {
        // Logic は 1 小節目が 01:00:00:00 のことが多いので、1時間未満の部分だけ表示
        const double s = std::fmod (juce::jmax (0.0, smpteSeconds), 3600.0);
        return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2);
    }

    // テスト用に公開
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (m.isQuarterFrame())
        {
            const int piece = m.getQuarterFrameSequenceNumber();
            const int v = m.getQuarterFrameValue();
            qf[piece] = v;
            lastQuarterMs = now;
            everReceived = true;
            if (piece == 7)
            {
                const int rate = (qf[7] >> 1) & 3;
                const int frames = qf[0] | ((qf[1] & 1) << 4);
                const int secs   = qf[2] | ((qf[3] & 3) << 4);
                const int mins   = qf[4] | ((qf[5] & 3) << 4);
                const int hours  = qf[6] | ((qf[7] & 1) << 4);
                rateCode = rate;
                const double fps = fpsFor (rate);
                // 8つ目が届いた時点で、表している時刻から 2 フレーム進んでいる
                lastPosition = hours * 3600.0 + mins * 60.0 + secs + (frames + 2) / fps;
                lastUpdateMs = now;
            }
        }
        else if (m.isFullFrame())
        {
            int h = 0, mi = 0, s = 0, f = 0;
            juce::MidiMessage::SmpteTimecodeType type;
            m.getFullFrameParameters (h, mi, s, f, type);
            rateCode = (int) type;
            lastPosition = h * 3600.0 + mi * 60.0 + s + f / fpsFor ((int) type);
            lastUpdateMs = now;
            everReceived = true;
        }
    }

    // 秒 → 時・分・秒・フレーム
    static std::array<int, 4> toHMSF (double seconds, int rate)
    {
        const int ifps = (int) std::round (fpsFor (rate));
        int total = (int) std::floor (seconds * fpsFor (rate) + 0.5);
        const int fr = total % ifps;  total /= ifps;
        const int sc = total % 60;    total /= 60;
        const int mn = total % 60;    total /= 60;
        return { juce::jmin (23, total), mn, sc, fr };
    }

private:
    static double fpsFor (int code) { return code == 0 ? 24.0 : code == 1 ? 25.0 : code == 2 ? 29.97 : 30.0; }

    void sendMMC (int command)
    {
        if (output == nullptr) return;
        const juce::uint8 d[] = { 0xF0, 0x7F, 0x7F, 0x06, (juce::uint8) command, 0xF7 };
        output->sendMessageNow (juce::MidiMessage (d, (int) sizeof (d)));
    }

    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput> input;
    int qf[8] {};
    std::atomic<int> rateCode { 1 };
    std::atomic<double> lastPosition { 0.0 }, lastUpdateMs { 0.0 }, lastQuarterMs { -1.0e9 };
    std::atomic<bool> everReceived { false };
};
