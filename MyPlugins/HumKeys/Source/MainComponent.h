#pragma once
#include <JuceHeader.h>
#include "VoiceEngine.h"

// マイクの音 → VoiceEngine → 仮想 MIDI「Hum Keys」 → Logic
class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void showModeForSnapshot (int m);
    juce::String savedWindowState() const;
    void saveWindowState (const juce::String&);

private:
    // ---- 音 ----
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void timerCallback() override;

    struct Sink : hum::MidiSink
    {
        std::unique_ptr<juce::MidiOutput> port;
        void send (uint8_t s, uint8_t a, uint8_t b) override
        {
            if (port != nullptr) port->sendMessageNow (juce::MidiMessage ((int) s, (int) a, (int) b));
        }
    };

    hum::Engine engine;
    Sink sink;
    juce::AudioDeviceManager devices;
    std::vector<float> mono;
    std::atomic<float> inPeak { 0 };

    // ---- 設定 ----
    std::unique_ptr<juce::PropertiesFile> props;
    void loadSettings();
    void saveSettings();
    void openAudioSettings();
    void startAudio();

    // ---- 画面 ----
    struct HistFrame { float pitch, level; int note; };
    std::vector<HistFrame> hist;   // 輪（最新 5 秒）
    int histPos = 0;
    float viewCenter = 60, meterDb = -100, peakHoldDb = -100;
    std::array<float, 3> padFlash {};
    std::array<int, 3> padVel {};
    int lastShownNote = -1;

    juce::Rectangle<int> displayArea, meterArea, rollArea;
    std::array<juce::Rectangle<int>, 3> padAreas;

    juce::TextButton onButton { "ON" }, melodyTab { "MELODY" }, drumsTab { "DRUMS" },
                     audioButton, panicButton { "PANIC" };
    juce::Label statusLabel;

    // MELODY
    juce::ComboBox keyBox, scaleBox, octaveBox, chanBox;
    juce::Slider glueSlider, holdSlider;
    juce::TextButton velButton, bendButton;
    // DRUMS
    juce::Slider sensSlider;
    std::array<juce::ComboBox, 3> drumNoteBox;
    std::array<juce::TextButton, 3> learnButton, resetButton;

    struct Caption { juce::Component* c; juce::String text; };
    std::vector<Caption> captions;
    void caption (juce::Component& c, const juce::String& t) { captions.push_back ({ &c, t }); }

    void refreshButtons();
    void setMode (int m);
    juce::String midiStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
