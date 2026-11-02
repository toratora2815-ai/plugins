#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class HorizonSpreadEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HorizonSpreadEditor (HorizonSpreadProcessor&);
    ~HorizonSpreadEditor() override = default;

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
    void showPage (bool shape);
    void placeKnob (Knob&, juce::Rectangle<int>);
    juce::Rectangle<int> tileContent (const Tile&) const;
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<float>);
    juce::String autoStatus() const;

    HorizonSpreadProcessor& processor;

    Knob spread, mix, output;
    Knob wLow, wMid, wHigh, xLow, xHigh;
    Knob air, airFreq, drive, transient, bassMono;
    Toggle widthOn, airOn, driveOn, transOn, guard, monoCheck;
    juce::TextButton autoButton { "AUTO" };
    juce::TextButton shapeTab { "SHAPE" }, createTab { "CREATE" };
    bool shapePage = true;

    Tile tMaster { "MASTER" }, tMeter { "IMAGE" };
    Tile tWidth { "WIDTH" }, tAir { "AIR" }, tDrive { "SIDE DRIVE" }, tTrans { "TRANSIENT" }, tSafety { "SAFETY" };
    Tile tCreate { "CREATE" };
    juce::Rectangle<int> scopeArea, corrArea, statusArea;

    std::vector<float> scopeL, scopeR;
    float scopePeak = 0.1f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonSpreadEditor)
};
