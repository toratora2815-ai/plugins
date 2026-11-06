#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class FaderPilotEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FaderPilotEditor (FaderPilotProcessor&);
    ~FaderPilotEditor() override = default;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
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
    void setupToggle (Toggle&, const char* id, const juce::String& text, juce::Colour on);
    void placeKnob (Knob&, juce::Rectangle<int>);
    juce::Rectangle<int> tileContent (const Tile&) const;
    void drawGraph (juce::Graphics&, juce::Rectangle<float>);
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    FaderPilotProcessor& processor;

    Knob target, floorKnob, range, speed, offset, output;
    Toggle autoRide, write, follow;
    juce::ComboBox presetBox;
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffffc86b), juce::Colour (0xff182026), juce::Colour (0xffe8eef2) };
    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xffffc86b), juce::Colour (0xff182026), juce::Colour (0xffe8eef2) };
    juce::Slider fader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment;

    Tile tGraph { "LEVEL HISTORY" }, tFader { "RIDE" }, tLevel { "LEVEL" }, tMotion { "MOTION" },
         tMusic { "MUSIC" }, tOut { "OUTPUT" };
    juce::Rectangle<int> graphArea, faderArea;

    static constexpr int shownPoints = 600;   // 6 秒分（10 ms ごと）
    std::vector<fp::RiderEngine::Point> points;
    int readPos = 0;
    float liveGain = 0.0f, musicDb = -120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderPilotEditor)
};
