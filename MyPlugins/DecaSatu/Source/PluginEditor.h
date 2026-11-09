#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class DecaSatuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecaSatuEditor (DecaSatuProcessor&);
    ~DecaSatuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<Attachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, bool big = false);
    void setupToggle (juce::TextButton&, std::unique_ptr<BtnAttachment>&, const juce::String& paramId, const juce::String& text, const juce::String& tip);
    void timerCallback() override;
    void syncStyleButtons();

    DecaSatuProcessor& processor;

    static inline const juce::Colour accent { 0xffd9823b }, panel { 0xff1d1b19 }, text { 0xffefe6d8 };

    juce::LookAndFeel_V4 lnf;

    juce::TextButton styleBtn[5];
    juce::Label styleHint;
    juce::TextButton punishBtn, thumpBtn, steepBtn;
    std::unique_ptr<BtnAttachment> punishAtt, thumpAtt, steepAtt;
    Knob drive, lowCut, tone, highCut, mix, output;

    // 画面のいちばん下: Plugin Link と GAIN MATCH・プリセット・取り消し（2 段）
    plink::LinkBar linkBar { processor.apvts, accent, panel, text };
    myp::PresetBar presetBar { processor.myPresets, accent, panel, text };
    juce::TextButton matchBtn;
    std::unique_ptr<BtnAttachment> matchAtt;
    static constexpr int footerH = 72;
    void layoutFooter();

    float meterDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecaSatuEditor)
};
