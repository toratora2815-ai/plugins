#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NebulaEditor : public juce::AudioProcessorEditor
{
public:
    explicit NebulaEditor (NebulaProcessor&);
    ~NebulaEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    NebulaProcessor& processor;
    std::array<Knob, 9> knobs;
    juce::TextButton freezeButton { "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaEditor)
};
