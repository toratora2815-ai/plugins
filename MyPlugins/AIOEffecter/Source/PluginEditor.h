#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class AIOEffecterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AIOEffecterEditor (AIOEffecterProcessor&);
    ~AIOEffecterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static inline const juce::Colour accent { 0xff4fb3a5 }, panel { 0xff16191b }, text { 0xffe4ece9 }, rowCol { 0xff1f2427 };

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // 1 段（スロット）
    struct Row : public juce::Component
    {
        Row (AIOEffecterEditor& e, int slot);
        void paint (juce::Graphics&) override;
        void resized() override;
        void syncType();          // 部品が変わったら名前・つまみの数・値の文字を変える
        void mouseDown (const juce::MouseEvent&) override { owner.setHintSlot (index); }

        AIOEffecterEditor& owner;
        int index;
        int shownType = -1;
        float meter = -1.0f;
        juce::TextButton up { "^" }, down { "v" }, onBtn { "ON" };
        juce::ComboBox type;
        std::unique_ptr<BtnAttachment> onAtt;
        juce::Slider knob[aiofx::kMaxKnobs];
        juce::Label name[aiofx::kMaxKnobs];
        std::unique_ptr<Attachment> att[aiofx::kMaxKnobs];
    };

    void timerCallback() override;
    void setHintSlot (int s) { hintSlot = s; updateHint(); }
    void updateHint();

    AIOEffecterProcessor& processor;
    juce::LookAndFeel_V4 lnf;
    std::vector<std::unique_ptr<Row>> rows;
    juce::ComboBox racks;
    juce::Label hint;
    int hintSlot = 0;

    // 画面のいちばん下: Plugin Link と GAIN MATCH・プリセット・取り消し（2 段）
    plink::LinkBar linkBar { processor.apvts, accent, panel, text };
    myp::PresetBar presetBar { processor.myPresets, accent, panel, text };
    juce::TextButton matchBtn;
    std::unique_ptr<BtnAttachment> matchAtt;
    static constexpr int footerH = 72, headerH = 48, rowH = 74;
    void layoutFooter();

    float meterDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIOEffecterEditor)
};
