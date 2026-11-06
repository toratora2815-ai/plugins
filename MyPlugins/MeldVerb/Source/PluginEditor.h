#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MeldVerbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MeldVerbEditor (MeldVerbProcessor&);
    ~MeldVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // 3 つの響きを混ぜる三角形のパッド
    struct BlendPad : public juce::Component
    {
        explicit BlendPad (MeldVerbEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::Point<float> corner (int i) const;
        void setFrom (juce::Point<float>);
        MeldVerbEditor& ed; bool dragging = false;
    };
    // 残響の形（時間と音量）
    struct DecayView : public juce::Component
    {
        explicit DecayView (MeldVerbEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        MeldVerbEditor& ed;
    };
    // EQ のカーブと、入力（ドライ）・響きのスペクトル
    struct EqView : public juce::Component
    {
        explicit EqView (MeldVerbEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        MeldVerbEditor& ed;
    };

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
    struct Combo
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    Knob& knob (const juce::String& id, const juce::String& name, juce::Colour c, bool big = false);
    Toggle& toggle (const juce::String& id, const juce::String& name, juce::Colour c);
    Combo& combo (const juce::String& id);
    void placeKnob (Knob& k, juce::Rectangle<int> r);
    void showTab (int t);
    void showEqTab (int t);
    void timerCallback() override;
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    void setLevelsFromPad (float dbE, float dbP, float dbH);

    MeldVerbProcessor& processor;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xfff2b35b), juce::Colour (0xff181c24), juce::Colour (0xffe6e9ef) };

    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xfff2b35b), juce::Colour (0xff181c24), juce::Colour (0xffe6e9ef) };

    static constexpr int footerH = 40;

    void layoutFooter();
    juce::Slider duckSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAtt;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Combo>> combos;
    std::map<juce::String, Knob*> knobById;
    std::map<juce::String, Toggle*> toggleById;
    std::map<juce::String, Combo*> comboById;

    BlendPad pad { *this };
    DecayView decay { *this };
    EqView eqView { *this };
    juce::TextButton tabs[4], eqTabs[2];
    int tab = 2, eqTab = 1;
    juce::ComboBox presetBox;
    juce::TextButton learnButton { "LEARN" }, unmaskButton { "UNMASK" };
    juce::Label statusLabel;
    juce::Rectangle<int> headerArea, leftArea, centreArea, rightArea, eqArea, meterArea, smoothMeterArea;
    juce::Rectangle<int> shapeRow[3];
    float shownSmooth = 0, shownLevels[4] {};
    float dryDb[MeldVerbProcessor::specBands] {}, wetDb[MeldVerbProcessor::specBands] {};
    meld::Early::View earlyCache; meld::EarlyParams earlyCacheKey; bool earlyCacheOk = false;
    juce::TooltipWindow tooltips { this, 600 };

    friend struct BlendPad; friend struct DecayView; friend struct EqView;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeldVerbEditor)
};
