#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PorchDriveEditor : public juce::AudioProcessorEditor
{
public:
    explicit PorchDriveEditor (PorchDriveProcessor&);
    ~PorchDriveEditor() override = default;

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

    PorchDriveProcessor& processor;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xff8fd0ff), juce::Colour (0xff10284a), juce::Colour (0xffe8f0ff) };

    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xff8fd0ff), juce::Colour (0xff10284a), juce::Colour (0xffe8f0ff) };

    static constexpr int footerH = 72;

    void layoutFooter();
    juce::TextButton matchBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAtt;
    Knob level, tone, gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PorchDriveEditor)
};
