#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class WoolFuzzEditor : public juce::AudioProcessorEditor
{
public:
    explicit WoolFuzzEditor (WoolFuzzProcessor&);
    ~WoolFuzzEditor() override = default;

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

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    WoolFuzzProcessor& processor;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffc0392b), juce::Colour (0xff2a2620), juce::Colour (0xffe8e2d0) };

    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xffc0392b), juce::Colour (0xff2a2620), juce::Colour (0xffe8e2d0) };

    static constexpr int footerH = 72;

    void layoutFooter();
    juce::TextButton matchBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAtt;
    Knob volume, tone, sustain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WoolFuzzEditor)
};
