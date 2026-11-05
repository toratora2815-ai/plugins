#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GainStageEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GainStageEditor (GainStageProcessor&);
    ~GainStageEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void timerCallback() override;
    void setupToggle (Toggle&, const char* id, const juce::String& text);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, float rmsDb, float peakDb);

    GainStageProcessor& processor;

    juce::Slider gain, target;
    juce::Label gainLabel, targetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt, targetAtt;
    juce::ComboBox measureBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> measureAtt;
    juce::TextButton learnButton;
    Toggle polarity, mono, swap;

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffb8e86b), juce::Colour (0xff191e22), juce::Colour (0xffe9eef1) };

    juce::Rectangle<int> meterIn, meterOut, infoArea;
    float inRms = -200, inPeak = -200, outRms = -200, outPeak = -200;
    juce::String flashText;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainStageEditor)
};
