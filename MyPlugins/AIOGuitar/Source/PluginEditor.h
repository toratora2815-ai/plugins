#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ScorePage.h"
#include "EffectsPage.h"

//==============================================================================
class AIOLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AIOLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
// A pedal / amp panel: title, optional on-switch, optional selector, row of knobs.
// (EFFECTS / AMP pages use these until they are rebuilt.)
class Section : public juce::Component
{
public:
    Section (juce::AudioProcessorValueTreeState& s, juce::String title, juce::Colour colour);

    void addKnob   (const juce::String& paramID, const juce::String& label);
    void addToggle (const juce::String& paramID);
    void addCombo  (const juce::String& paramID);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::AudioProcessorValueTreeState& state;
    juce::String title;
    juce::Colour colour;

    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>>  labels;
    std::vector<std::unique_ptr<SA>>           knobAtt;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<BA> toggleAtt;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<CA> comboAtt;
};

//==============================================================================
// Keyswitch map (click a tile to pick the articulation for the next notes) + articulation settings.
class ArticulationPanel : public juce::Component, private juce::Timer
{
public:
    ArticulationPanel (AIOGuitarProcessor&, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    void timerCallback() override;
    juce::Rectangle<int> tileBounds (int index) const;
    void addKnob (const juce::String& id, const juce::String& text);

    AIOGuitarProcessor& proc;
    juce::Colour colour;
    juce::Rectangle<int> tilesArea, artArea, wheelArea;
    int shown = -1;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<SA>> knobAtt;
};

//==============================================================================
// One recording slot (soft / hard / palm mute / brushing): load or drop a file.
class SlotCard : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    SlotCard (AIOGuitarProcessor&, int slot, juce::String title, juce::String hint, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void timerCallback() override;
    void load (const juce::File&);

    AIOGuitarProcessor& proc;
    int slot;
    juce::String title, hint;
    juce::Colour colour;
    juce::TextButton loadButton { "Load..." }, clearButton { "Clear" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false, dragOver = false;
    int shownVersion = -1, count = 0;
    juce::String status;
};

// Guitar name + save / open the whole guitar (.aioguitar)
class KitCard : public juce::Component, private juce::Timer
{
public:
    KitCard (AIOGuitarProcessor&, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    AIOGuitarProcessor& proc;
    juce::Colour colour;
    juce::Label name;
    juce::TextButton saveButton { "Save Guitar..." }, openButton { "Open Guitar..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    int shownVersion = -1;
};

// 6 strings x frets: which notes were recorded (and how much they were corrected),
// which recording every other note comes from. Click = listen.
class StringGrid : public juce::Component, private juce::Timer
{
public:
    explicit StringGrid (AIOGuitarProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int frets = 23;   // 0 .. 22

private:
    struct Cell { bool rec[3] {}; float cents = 0; int srcString = 0, srcFret = -1, shift = 0; bool any = false; };
    void timerCallback() override;
    void rebuild();
    juce::Rectangle<float> board() const;
    juce::Rectangle<float> cellRect (int string, int fret) const;
    juce::Point<int> cellAt (juce::Point<float>) const;   // x = fret, y = string (0 = none)
    juce::String describe (int string, int fret) const;

    AIOGuitarProcessor& proc;
    Cell cells[7][frets];
    juce::String summary;
    juce::Point<int> hover, pressed;
    int pressedNote = -1, shownVersion = -1;
};

class PlaySettings : public juce::Component
{
public:
    PlaySettings (AIOGuitarProcessor&, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    juce::Colour colour;
    juce::ComboBox stringMode;
    juce::ToggleButton tune { "Pitch Correct" }, level { "Level Match" };
    std::unique_ptr<CA> stringModeAtt;
    std::unique_ptr<BA> tuneAtt, levelAtt;
    juce::Label stringLabel { {}, "String choice" };
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<SA>> knobAtt;
};

class MyGuitarPage : public juce::Component
{
public:
    explicit MyGuitarPage (AIOGuitarProcessor&);
    void resized() override;
private:
    SlotCard soft, hard, mute, brush;
    KitCard kit;
    StringGrid grid;
    PlaySettings settings;
    ArticulationPanel articulations;
};

//==============================================================================
class AIOGuitarEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AIOGuitarEditor (AIOGuitarProcessor&);
    ~AIOGuitarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int);

    AIOGuitarProcessor& proc;
    AIOLookAndFeel lnf;

    juce::TextButton tabs[4] { juce::TextButton ("MY GUITAR"), juce::TextButton ("SCORE"), juce::TextButton ("EFFECTS"), juce::TextButton ("AMP") };
    juce::Component ampPage;
    MyGuitarPage myGuitarPage;
    ScorePage scorePage;
    EffectsPage effectsPage;
    Section amp, cab, reverb;
    juce::Label ampNote;
    juce::ComboBox presetBox;
    int page = 0;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し
    plink::LinkBar linkBar { proc.apvts, juce::Colour (0xffe8a33d), juce::Colour (0xff22242b), juce::Colour (0xffe8e6e1) };
    myp::PresetBar presetBar { proc.myPresets, juce::Colour (0xffe8a33d), juce::Colour (0xff22242b), juce::Colour (0xffe8e6e1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIOGuitarEditor)
};
