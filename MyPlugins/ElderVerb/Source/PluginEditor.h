#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ElderLook : public juce::LookAndFeel_V4
{
public:
    ElderLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (14.0f, juce::Font::bold); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.5f, juce::Font::bold); }
};

// 周波数ごとの残響の長さ（設定から計算して描く）
class DecayGraph : public juce::Component
{
public:
    explicit DecayGraph (ElderVerbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    ElderVerbProcessor& proc;
};

class ElderVerbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ElderVerbEditor (ElderVerbProcessor&);
    ~ElderVerbEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void savePreset();
    void loadFile (const juce::File&);

    ElderVerbProcessor& proc;
    ElderLook look;

    struct Knob
    {
        juce::Slider s; juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::vector<std::unique_ptr<Knob>> knobs;   // ElderVerbProcessor::knobIds() の順
    Knob& knob (const juce::String& id);

    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    juce::TextButton colorBtn[3];
    juce::TextButton presetBtn, saveBtn, prevBtn { "<" }, nextBtn { ">" };
    DecayGraph graph;
    std::unique_ptr<juce::FileChooser> chooser;
    float meter[2] {};
    juce::String lastPresetName;
    int presetIndex = 0;
    std::vector<juce::File> allPresetFiles() const;
    juce::Rectangle<int> secSpace, secEq, secTex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElderVerbEditor)
};
