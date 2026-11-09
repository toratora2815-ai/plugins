#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class DrumTransientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DrumTransientEditor (DrumTransientProcessor&);
    ~DrumTransientEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label, hint;
        std::unique_ptr<SliderAtt> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& hint);
    void setupCombo (juce::ComboBox&, juce::Label&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items, std::unique_ptr<ComboAtt>&);
    void timerCallback() override;
    void layoutFooter();

    DrumTransientProcessor& processor;

    const juce::Colour accent { 0xffff8a3d }, panel { 0xff1c1d22 }, text { 0xffeeeae4 };

    plink::LinkBar linkBar { processor.apvts, accent, panel, text };
    myp::PresetBar presetBar { processor.myPresets, accent, panel, text };
    static constexpr int footerH = 72, topH = 46, meterW = 300;

    Knob push, punch, body, clip, release;
    juce::ComboBox lowKeep, ceiling, factory;
    juce::Label lowKeepLabel, ceilingLabel;
    std::unique_ptr<ComboAtt> lowKeepAtt, ceilingAtt;
    juce::TextButton linkBtn { "LINK" }, deltaBtn { "DELTA" }, learnBtn { "LEARN" };
    int learnShowT = 0;   // LEARN の結果を出しておく残り（タイマーの回数）
    void drawHistory (juce::Graphics&, juce::Rectangle<int>);
    std::unique_ptr<ButtonAtt> linkAtt, deltaAtt;

    float showAtk = 0, showBody = 0, holdAtk = 0, holdBody = 0;
    int holdAtkT = 0, holdBodyT = 0;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumTransientEditor)
};
