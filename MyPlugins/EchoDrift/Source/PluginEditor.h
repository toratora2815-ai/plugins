#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class EchoDriftEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EchoDriftEditor (EchoDriftProcessor&);
    ~EchoDriftEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // エコーの並び（上 = 左、下 = 右。高さ = 1 kHz での大きさ）と、スタイルの音色カーブ
    struct EchoView : public juce::Component
    {
        explicit EchoView (EchoDriftEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        EchoDriftEditor& ed;
    };

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Combo
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    Knob& knob (const juce::String& id, const juce::String& name, juce::Colour c, bool big = false);
    Combo& combo (const juce::String& id);
    void placeKnob (Knob& k, juce::Rectangle<int> r);
    void timerCallback() override;
    void updateVisibility();
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);

    EchoDriftProcessor& processor;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffffb454), juce::Colour (0xff17140f), juce::Colour (0xffefe6d8) };
    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xffffb454), juce::Colour (0xff17140f), juce::Colour (0xffefe6d8) };
    static constexpr int footerH = 72;   // 2 段（LINK 欄 / GAIN MATCH・プリセット欄）
    void layoutFooter();
    juce::Slider duckSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAtt;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::map<juce::String, Knob*> knobById;
    std::map<juce::String, Combo*> comboById;

    EchoView view { *this };
    juce::TextButton modeButtons[4];
    juce::TextButton primeButton { "PRIME" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> primeAtt;
    juce::ComboBox presetBox;
    juce::TextButton stylePrev { "<" }, styleNext { ">" };
    juce::Rectangle<int> headerArea, echoArea, viewArea, toneArea, meterArea;
    float shownLevels[4] {};
    int lastMode = -1, lastSync[3] { -1, -1, -1 };
    juce::TooltipWindow tooltips { this, 600 };

    friend struct EchoView;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoDriftEditor)
};
