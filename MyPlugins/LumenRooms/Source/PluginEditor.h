#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Visuals.h"

class LumenLook : public juce::LookAndFeel_V4
{
public:
    LumenLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.5f, juce::Font::bold); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class LumenRoomsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenRoomsEditor (LumenRoomsProcessor&);
    ~LumenRoomsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Size { big, mid };
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        Size size = Size::mid;
        int page = 0;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
        int page = 0;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
        int page = 0;
    };
    struct Card { juce::String title; juce::Colour accent; juce::Rectangle<int> bounds; int page; };

    Knob& knob (const char* id, const char* name, Size, int page, juce::Colour accent);
    Choice& choice (const char* id, const char* name, int page);
    Toggle& toggle (const char* id, const char* text, int page);
    void placeKnob (const char* id, juce::Point<int> centre);
    void placeChoice (const char* id, juce::Rectangle<int>);
    void placeRow (juce::Rectangle<int> area, std::initializer_list<const char*> ids);
    juce::Rectangle<int> cardContent (const juce::String& title) const;
    void showPage (int);
    void timerCallback() override;
    lumen::VisualParams currentVisualParams() const;

    void drawCard (juce::Graphics&, const Card&);
    void drawReflections (juce::Graphics&, juce::Rectangle<float>);
    void drawReverbMap (juce::Graphics&, juce::Rectangle<float>);
    void drawEqCurve (juce::Graphics&, juce::Rectangle<float>);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    void showLoadMenu();
    void addFolderToMenu (juce::PopupMenu&, const juce::File&, std::vector<juce::File>&, int depth);

    LumenRoomsProcessor& proc;
    LumenLook look;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::map<juce::String, std::unique_ptr<Choice>> choices;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::vector<Card> cards;
    juce::TextButton tabs[3];
    juce::TextButton loadButton { "LOAD" };
    juce::ComboBox presetBox;
    juce::Label importedLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    int page = 0;

    juce::Rectangle<int> erArea, mapArea, eqArea, meterArea;
    lumen::VisualWorker worker;
    lumen::VisualWorker::Result vis;
    bool haveVis = false;
    float meter[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRoomsEditor)
};
