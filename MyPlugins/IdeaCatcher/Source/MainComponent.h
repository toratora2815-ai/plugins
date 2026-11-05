#pragma once
#include <JuceHeader.h>
#include "IdeaAnalysis.h"

// 録音 → 解析 → Logic 用の MIDI を作る、の 1 画面
class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void toggleRecord();
    void openLastInLogic();
    void showMe (bool focus);
    void command (const juce::String& c);

    // 確認用
    bool loadForSnapshot (const juce::File& wav);
    juce::File midiFile() const { return cur.mid; }

    std::function<void (bool floating, bool focus)> onShowRequest;

    static juce::File ideasRoot();

private:
    enum State { Idle, Starting, Recording, Analysing, Done };
    State state = Idle;
    bool live = true;

    std::unique_ptr<juce::PropertiesFile> props;
    juce::AudioDeviceManager devices;

    // 録音（音の処理のスレッドが書く）
    std::vector<float> rec;
    std::atomic<int64_t> recN { 0 }, lastLoud { -1 };
    std::atomic<float> level { 0 };
    std::atomic<bool> recording { false };
    double recFs = 48000;
    double recStartMs = 0;
    float meter = 0;

    struct Idea
    {
        bool valid = false;
        juce::File folder, wav, mid;
        idea::Result r;
        idea::MidiOptions o;
    } cur;

    juce::String message;   // 下に出す一言
    juce::Array<juce::File> recent;
    int hoverRecent = -1;
    bool dragStarted = false;

    juce::TextButton kindBtn, keyDownBtn, keyUpBtn, modeBtn, bpmMinus, bpmPlus, bpmHalf, bpmDouble, quantBtn,
                     logicBtn, finderBtn, hotkeyBtn, inputBtn;
    juce::ToggleButton autoOpenBtn, autoStopBtn, loginBtn;

    juce::Rectangle<int> header, recArea, recCircle, card, roll, actions, dragMidi, dragWav, recentArea, footer;

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void timerCallback() override;

    void startRecording();
    void openDeviceAndRecord();
    void stopRecording();
    void finishAnalysis (idea::Result r, std::vector<float> samples, double fs);
    void loadIdea (const juce::File& folder);
    void applyResult (const juce::File& folder, idea::Result r, const juce::var& saved);

    void writeMidi();
    void saveInfo();
    void refreshRecent();
    void refreshButtons();
    void refreshMenu();
    void setState (State s);
    void editOptions (std::function<void (idea::MidiOptions&)> f);
    void registerHotKey();
    void showInputSettings();

    juce::String keyLabel() const;
    juce::String hotKeyLabel() const;
    static juce::String shortKey (int key, bool minor);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
