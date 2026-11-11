#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace nzoui
{
    inline const juce::Colour accent { 0xff7bd6c0 }, humCol { 0xffe8c36a }, roomCol { 0xffa99be8 }, panel { 0xff15181c },
                              text { 0xffe8eef2 }, dim { 0xff262b31 }, redCol { 0xffe07a6a };

    // 上のグラフ: 入口の大きさ（灰）・しきい値（雑音の推定、緑の線）・いま下げている量（上から垂れる赤）・ハムの倍音（黄の縦線）
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (NoiseOutProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        NoiseOutProcessor& proc;
        float xFor (float hz, juce::Rectangle<float> r) const
        {
            return r.getX() + r.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
        }
    };
}

class NoiseOutEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NoiseOutEditor (NoiseOutProcessor&);
    ~NoiseOutEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using BoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<Attachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip, juce::Colour c);
    void setupBox (juce::ComboBox&, std::unique_ptr<BoxAttachment>&, const juce::String& paramId, const juce::String& tip);
    void setupToggle (juce::TextButton&, std::unique_ptr<BtnAttachment>&, const juce::String& paramId, const juce::String& text, const juce::String& tip, juce::Colour c);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void timerCallback() override;

    NoiseOutProcessor& processor;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Knob humRed, humHarm, noiseRed, noiseThr, roomRed, roomTail, output;
    juce::TextButton humOnBtn, noiseOnBtn, roomOnBtn, deltaBtn, learnBtn;
    std::unique_ptr<BtnAttachment> humOnAtt, noiseOnAtt, roomOnAtt, deltaAtt;
    juce::ComboBox humFreqBox, noiseModeBox, factoryBox;
    std::unique_ptr<BoxAttachment> humFreqAtt, noiseModeAtt;
    juce::Label humStatus, noiseStatus, roomStatus;

    nzoui::SpectrumView spectrum { processor };

    plink::LinkBar linkBar { processor.apvts, nzoui::accent, nzoui::panel, nzoui::text };
    myp::PresetBar presetBar { processor.myPresets, nzoui::accent, nzoui::panel, nzoui::text };
    static constexpr int footerH = 72;

    juce::Rectangle<int> humArea, noiseArea, roomArea, outArea, graphArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseOutEditor)
};
