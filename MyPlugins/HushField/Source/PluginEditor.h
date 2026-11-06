#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// つまみの見た目（Hush Field 専用）
class HushLook : public juce::LookAndFeel_V4
{
public:
    HushLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// グラフ（スペクトル・削っている量・帯域カーブ。点をドラッグして帯域を決める）
class HushGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HushGraph (HushFieldProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick();                         // 30 Hz で呼ぶ
    float rangeDb = 12.0f;               // 削る量の表示の深さ

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }
    float hzToX (float hz) const;
    float xToHz (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    juce::Point<float> nodePos (int b) const;
    bool isGainShape (int shape) const;
    int hitNode (juce::Point<float>) const;
    float param (int b, const char* what) const;
    void setParam (int b, const char* what, float value);
    void beginGesture (int b);
    void endGesture (int b);
    void showMenu (int b);
    void drawCurve (juce::Graphics&, juce::Rectangle<float>);

    HushFieldProcessor& proc;
    float red[hf::HushEngine::displayPoints] {}, spec[hf::HushEngine::displayPoints] {};
    int hover = -1, drag = -1;
    bool soloHeld = false;
};

class HushFieldEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HushFieldEditor (HushFieldProcessor&);
    ~HushFieldEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    void timerCallback() override;
    void setupKnob (Knob&, const char* id, const juce::String& name, bool linear = false);
    void setupToggle (Toggle&, const char* id, const juce::String& text);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void setChoice (const char* id, int index);
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    HushFieldProcessor& processor;
    HushLook look;
    juce::TooltipWindow tips { this, 600 };

    HushGraph graph { processor };
    Knob depth, detail, attack, release, mix, outTrim, wetTrim, maxCut, link, focus;
    Knob tilt[6];
    Toggle bypass, delta, sidechain, scListen, lowLatency, linearPhase;
    juce::TextButton soft { "soft" }, hard { "hard" }, lr { "L/R" }, ms { "M/S" };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "A>B" };
    juce::ComboBox presetBox, qualityBox, rangeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt;
    juce::Label status;
    myp::PresetBar presetBar;
    plink::LinkBar linkBar;
    Toggle linkFollow;

    juce::Rectangle<int> leftPanel, stereoPanel, tiltPanel, outPanel, topBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HushFieldEditor)
};
