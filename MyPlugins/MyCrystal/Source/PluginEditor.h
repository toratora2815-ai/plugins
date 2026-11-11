#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace mycryui
{
    inline const juce::Colour accent { 0xff7fc8e8 }, accent2 { 0xffc49be8 }, panel { 0xff16191d }, text { 0xffe8eef2 }, dim { 0xff262b31 }, warm { 0xffe8c36a };

    // 粒の様子: 遅れの窓（SPLICE の幅、DELAY の分だけ右へ）と、今読んでいる所（左・右）
    class GrainView : public juce::Component
    {
    public:
        explicit GrainView (MyCrystalProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        MyCrystalProcessor& proc;
    };

    // PITCH: ふだんは半音ごとに止まる。shift を押しながらだとセント単位
    class SemitoneSlider : public juce::Slider
    {
    public:
        double snapValue (double v, DragMode) override
        {
            if (juce::ModifierKeys::currentModifiers.isShiftDown()) return v;
            return std::round (v / 100.0) * 100.0;
        }
    };
}

class MyCrystalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MyCrystalEditor (MyCrystalProcessor&);
    ~MyCrystalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using BoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        std::unique_ptr<juce::Slider> sliderPtr { std::make_unique<juce::Slider>() };
        juce::Slider& slider() { return *sliderPtr; }
        juce::Label label;
        std::unique_ptr<Attachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void setupBox (juce::ComboBox&, std::unique_ptr<BoxAttachment>&, const juce::String& paramId, const juce::String& tip);
    void setupToggle (juce::TextButton&, std::unique_ptr<BtnAttachment>&, const juce::String& paramId, const juce::String& text, const juce::String& tip);
    void timerCallback() override;
    void setTweakOpen (bool open);
    void placeKnob (Knob&, juce::Rectangle<int>);

    MyCrystalProcessor& processor;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Knob input, pitch, splice, spliceNote, delay, delayNote, recycle, mix, output;
    Knob thr, gateDuck;
    Knob pitchOff, spliceOff, delayOff, smooth, lowCut, highCut, att, rel;
    juce::ComboBox duckModeBox, fbModeBox, factoryBox;
    std::unique_ptr<BoxAttachment> duckModeAtt, fbModeAtt;
    juce::TextButton dirBtn, syncBtn, analogBtn, tweakBtn;
    std::unique_ptr<BtnAttachment> dirAtt, syncAtt, analogAtt;
    juce::Label duckModeLabel, fbModeLabel;

    mycryui::GrainView grainView { processor };

    plink::LinkBar linkBar { processor.apvts, mycryui::accent, mycryui::panel, mycryui::text };
    myp::PresetBar presetBar { processor.myPresets, mycryui::accent, mycryui::panel, mycryui::text };
    juce::Slider duckSlider;
    std::unique_ptr<Attachment> duckAtt;
    static constexpr int footerH = 72, mainH = 418, tweakH = 150;

    bool tweakOpen = false;
    float inMeter = -100.0f;
    juce::Rectangle<int> mainArea, lowerArea, tweakArea, grainArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyCrystalEditor)
};
