#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace twinui
{
    inline const juce::Colour accent { 0xffe8a87c }, accent2 { 0xff7cc4e8 }, panel { 0xff17191d }, text { 0xffeef0f2 }, dim { 0xff262a30 }, warm { 0xffe8c36a };

    // TAKES: 各ダブルが、原音からどれだけ遅れて（GUITAR は前後に）・何セントずれているか。音ごとに選び直すので、点が動く
    class TakesView : public juce::Component
    {
    public:
        explicit TakesView (TwinTakeProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void update();   // 画面のタイマーから
    private:
        TwinTakeProcessor& proc;
        struct Dot { float ms = 0, cents = 0; };
        static constexpr int histLen = 10;
        Dot hist[twin::Engine::maxVoices][histLen] {};
        int histCount = 0, lastOnsets = -1, lastMode = -1, lastVoices = -1;
    };
}

class TwinTakeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TwinTakeEditor (TwinTakeProcessor&);
    ~TwinTakeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<Attachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void setChoice (const juce::String& paramId, int index);
    void timerCallback() override;

    TwinTakeProcessor& processor;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Knob dbl, loose, width, lowKeep;
    juce::TextButton modeBtn[2], voicesBtn[3], monoBtn;
    juce::Label voicesLabel, takeLabel;
    juce::Slider takeSlider;
    std::unique_ptr<Attachment> takeAtt;
    juce::ComboBox factoryBox;

    twinui::TakesView takesView { processor };

    plink::LinkBar linkBar { processor.apvts, twinui::accent, twinui::panel, twinui::text };
    myp::PresetBar presetBar { processor.myPresets, twinui::accent, twinui::panel, twinui::text };
    static constexpr int footerH = 72, topH = 50;

    juce::Rectangle<int> knobArea, sideArea, takesArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwinTakeEditor)
};
