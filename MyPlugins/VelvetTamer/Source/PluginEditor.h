#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VelvetTamerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VelvetTamerEditor (VelvetTamerProcessor&);
    ~VelvetTamerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    struct Tile
    {
        explicit Tile (const juce::String& t) : title (t) {}
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* id, const char* name, bool big = false);
    void setupToggle (Toggle&, const char* id, const juce::String& text);
    void placeKnob (Knob&, juce::Rectangle<int>);
    juce::Rectangle<int> tileContent (const Tile&) const;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>);
    float freqToX (float hz, juce::Rectangle<float> r) const;

    VelvetTamerProcessor& processor;

    Knob depth, sharpness, selectivity, attack, release, low, high, mix, output;
    Toggle delta, link;

    Tile tDisplay { "RESONANCE" }, tAmount { "AMOUNT" }, tDetect { "DETECT" }, tTime { "TIME" },
         tFocus { "FOCUS" }, tOut { "OUTPUT" };
    juce::Rectangle<int> displayArea;

    float spec[vt::TamerEngine::displayPoints] {}, gr[vt::TamerEngine::displayPoints] {};
    float shownSpec[vt::TamerEngine::displayPoints] {}, shownGr[vt::TamerEngine::displayPoints] {};
    float shownMax = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvetTamerEditor)
};
