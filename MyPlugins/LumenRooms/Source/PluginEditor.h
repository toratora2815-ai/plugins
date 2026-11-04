#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class LumenLook : public juce::LookAndFeel_V4
{
public:
    LumenLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
};

class LumenRoomsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenRoomsEditor (LumenRoomsProcessor&);
    ~LumenRoomsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        bool big = false;
    };
    struct Choice
    {
        juce::String id;
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Panel
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        int page = 0;
        std::vector<Knob*> knobs;
        std::vector<Choice*> choices;
        std::vector<juce::Button*> toggles;
        int columns = 3;
        juce::Rectangle<int> extra;   // 絵を描く場所など
        int extraHeight = 0;
    };

    void timerCallback() override;
    Knob* knob (const char* id, const char* name, bool big = false);
    Choice* choice (const char* id, const char* name);
    void layoutPanel (Panel&);
    void showPage (int);
    void drawReflections (juce::Graphics&, juce::Rectangle<float>);
    void drawDecay (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>);

    LumenRoomsProcessor& proc;
    LumenLook look;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<Panel> panels;
    Toggle freeze, eqOn, echoSync, preSync;
    juce::TextButton loadButton { "LOAD" };
    juce::Label importedLabel;
    void showLoadMenu();
    void addFolderToMenu (juce::PopupMenu&, const juce::File&, std::vector<juce::File>&, int depth);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton tabs[3];
    juce::ComboBox presetBox;
    int page = 0;
    juce::Rectangle<int> meterArea;
    float meter[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRoomsEditor)
};
