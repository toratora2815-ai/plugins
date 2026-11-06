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

    // 画面のいちばん下: Plugin Link と プリセット・取り消し

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xff8fb8ff), juce::Colour (0xff20243a), juce::Colour (0xffe6e9f5) };

    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xff8fb8ff), juce::Colour (0xff20243a), juce::Colour (0xffe6e9f5) };

    static constexpr int footerH = 72;

    void layoutFooter();
    juce::Slider duckSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAtt;
    std::array<Knob, 9> knobs;
    juce::TextButton freezeButton { "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaEditor)
};
