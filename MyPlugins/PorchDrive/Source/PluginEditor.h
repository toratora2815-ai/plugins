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
    Knob level, tone, gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PorchDriveEditor)
};
