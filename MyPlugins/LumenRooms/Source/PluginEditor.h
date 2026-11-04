#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Visuals.h"
#include "PresetBrowser.h"

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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

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
    void showSaveDialog();
    void setMapMode (int);
    void pullLive();
    void drawLiveMap (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<float> erPlot() const;
    juce::Rectangle<float> mapPlot() const;
    int dragTarget = 0;   // 0 = なし, 1 = 反射の表示, 2 = 残響の表示
    float dragStartX = 0, dragStartY = 0;
    int hoverTarget = 0;
    void addFolderToMenu (juce::PopupMenu&, const juce::File&, std::vector<juce::File>&, int depth);

    LumenRoomsProcessor& proc;
    LumenLook look;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::map<juce::String, std::unique_ptr<Choice>> choices;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::vector<Card> cards;
    juce::TextButton tabs[3];
    juce::TextButton loadButton { "LOAD" }, saveButton { "SAVE" };
    juce::TextButton shapeButton { "SHAPE" }, liveButton { "LIVE" };
    juce::TextButton srcAll { "ALL" }, srcRefl { "REFL" }, srcRev { "REVERB" };
    juce::TextButton prevButton { "<" }, nextButton { ">" }, nameButton;
    std::unique_ptr<PresetBrowser> browser;
    bool livePaused = false;
    void toggleBrowser();
    juce::String currentPresetName() const;
    void placeRowCols (juce::Rectangle<int> area, std::initializer_list<const char*> ids, int cols);
    std::unique_ptr<juce::AlertWindow> saveWindow;
    // LIVE 表示
    static constexpr int liveOrder = 11, liveN = 1 << liveOrder, liveCols = 260;
    juce::dsp::FFT liveFft { liveOrder };
    juce::dsp::WindowingFunction<float> liveWin { (size_t) liveN, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ringDry = std::vector<float> ((size_t) liveN, 0.0f), ringWet = std::vector<float> ((size_t) liveN, 0.0f);
    int ringPos = 0, sinceHop = 0;
    juce::Image liveImage { juce::Image::ARGB, liveCols, lumen::VisualWorker::kRows, true };
    juce::ComboBox presetBox;
    juce::Label importedLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    int page = 0;

    juce::Rectangle<int> erArea, mapArea, eqArea, meterArea;
    lumen::VisualWorker worker;
    lumen::VisualWorker::Result vis;
    bool haveVis = false;
    float meter[2] {}, meterIn[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRoomsEditor)
};
