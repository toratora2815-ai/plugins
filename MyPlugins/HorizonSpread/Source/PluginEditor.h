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
    struct Choice
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
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
    void setupMode (Toggle&, const char* id, const juce::String& offText, const juce::String& onText);
    void setupChoice (Choice&, const char* id);
    void placeHeader (const Tile&, Toggle& on, juce::Component* mode = nullptr, int modeWidth = 66);
    void drawBandMeter (juce::Graphics&, juce::Rectangle<float>);
    void refreshPresetBox();
    void showPage (bool shape);
    void placeKnob (Knob&, juce::Rectangle<int>);
    juce::Rectangle<int> tileContent (const Tile&) const;
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<float>);
    juce::String autoStatus() const;

    HorizonSpreadProcessor& processor;

    Knob spread, mix, output;
    Knob wLow, wMid, wHigh, xLow, xHigh;
    Knob air, airFreq, drive, transient, dynamic, bassMono;
    Toggle widthOn, airOn, driveOn, transOn, guard, monoCheck, sideSolo, autoGain;

    Knob shiftTime, shiftAmt, dblAmt, detune, bandsAmt, bandsCount, hazeAmt, hazeSize, motionRate, motionDepth;
    Toggle shiftOn, doubleOn, bandsOn, hazeOn, motionOn, bandsMode, hazeMode;
    Choice shiftMode, motionSync;
    juce::ComboBox presetBox;
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffffb86b), juce::Colour (0xff161c24), juce::Colour (0xffe9eef3) };
    juce::Slider duckSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAttachment;
    int shownProgram = -1;
    juce::TextButton autoButton { "AUTO" };
    juce::TextButton shapeTab { "SHAPE" }, createTab { "CREATE" };
    bool shapePage = true;

    Tile tMaster { "MASTER" }, tMeter { "IMAGE" };
    Tile tWidth { "WIDTH" }, tAir { "AIR" }, tDrive { "SIDE DRIVE" }, tTrans { "DYNAMICS" }, tSafety { "SAFETY" };
    Tile tShift { "SHIFT" }, tDouble { "DOUBLE" }, tBands { "BANDS" }, tHaze { "HAZE" }, tMotion { "MOTION" };
    juce::Rectangle<int> scopeArea, corrArea, statusArea, bandArea;

    std::vector<float> scopeL, scopeR;
    float scopePeak = 0.1f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonSpreadEditor)
};
