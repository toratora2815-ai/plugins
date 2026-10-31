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
    Knob volume, tone, sustain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WoolFuzzEditor)
};
