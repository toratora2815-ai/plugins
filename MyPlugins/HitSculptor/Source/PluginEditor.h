#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// つまみ（真ん中が 0 のものは、真ん中から左右へ色をつける）
class HitLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
};

// 最後の 1 打（灰色 = 元の音、色 = 出てくる音、dB 目盛り）
class HitDisplay : public juce::Component
{
public:
    explicit HitDisplay (hs::Engine& e) : engine (e) {}
    void paint (juce::Graphics&) override;
private:
    hs::Engine& engine;
};

class HitSculptorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HitSculptorEditor (HitSculptorProcessor&);
    ~HitSculptorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label, hint;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* id, const char* name, const char* hint, bool bipolar, double def);
    void refreshPresetName();

    HitSculptorProcessor& processor;
    HitLook look;
    HitDisplay display { processor.engine };
    Knob attack, snap, length, room, size, tone, drive, mix, output;
    juce::ComboBox presetBox;
    juce::Rectangle<int> groupShape, groupRoom, groupOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitSculptorEditor)
};
