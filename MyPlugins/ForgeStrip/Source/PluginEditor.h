#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ForgeLook : public juce::LookAndFeel_V4
{
public:
    ForgeLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// モジュールの並び（クリック = 選ぶ、左の丸 = オン/オフ、ドラッグ = 並べ替え）
class ChainBar : public juce::Component, private juce::Timer
{
public:
    explicit ChainBar (ForgeStripProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;   // モジュール番号、-1 = MASTER
    int selected = 1;
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> chip (int slot) const;
    int slotAt (juce::Point<float>) const;
    ForgeStripProcessor& proc;
    int dragFrom = -1, dragOver = -1; bool dragging = false; juce::Point<float> downPos;
};

// EQ のグラフ（点をドラッグ = 周波数と量、ホイール = Q、ダブルクリック = オン/オフ）
class EqGraph : public juce::Component, private juce::Timer
{
public:
    explicit EqGraph (ForgeStripProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void (int)> onSelect;
    int selected = 0;
private:
    void timerCallback() override { repaint(); }
    float hzToX (float) const; float xToHz (float) const; float dbToY (float) const; float yToDb (float) const;
    float val (int b, const char* w) const;
    void setVal (int b, const char* w, float v);
    juce::Point<float> node (int b) const;
    ForgeStripProcessor& proc;
    int drag = -1;
};

// 1 ページ（選んだモジュールのつまみ）。中身は作り直す
class ModulePage : public juce::Component, private juce::Timer
{
public:
    ModulePage (ForgeStripProcessor& p, int module);
    ~ModulePage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void build();
    juce::Slider& knob (const juce::String& id, const juce::String& label, juce::Rectangle<int> r);
    juce::Button& toggle (const juce::String& id, const juce::String& text, juce::Rectangle<int> r);
    juce::ComboBox& combo (const juce::String& id, juce::Rectangle<int> r, const juce::String& label = {});
    void caption (const juce::String& text, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft);
    void buildBands (const std::vector<std::pair<const char*, const char*>>& knobs, const std::vector<std::pair<const char*, const char*>>& toggles,
                     const std::vector<std::pair<const char*, const char*>>& combos, juce::Rectangle<int> area);
    void buildEqBand();

    ForgeStripProcessor& proc;
    const int mod;
    std::vector<std::unique_ptr<juce::Component>> comps;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::unique_ptr<EqGraph> eqGraph;
    std::vector<std::unique_ptr<juce::Component>> eqBandComps;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> eqS;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> eqB;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> eqC;
    juce::Label* meter[3] {};
    juce::Rectangle<int> bandCols[3];
    int shownBands = -1, shownMode = -1;
    juce::Rectangle<int> eqBandArea;
};

class ForgeStripEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ForgeStripEditor (ForgeStripProcessor&);
    ~ForgeStripEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void showPage (int module);
    ForgeStripProcessor& processor;
    ForgeLook look;
    juce::TooltipWindow tips { this, 600 };
    ChainBar chain { processor };
    std::unique_ptr<ModulePage> page;
    juce::Label status;
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffffb454), juce::Colour (0xff1d1d22), juce::Colour (0xffe8e4dc) };
    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xffffb454), juce::Colour (0xff1d1d22), juce::Colour (0xffe8e4dc) };
    int current = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeStripEditor)
};
