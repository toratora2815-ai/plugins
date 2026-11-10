#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace mytremui
{
    inline const juce::Colour accent { 0xff4fb3a5 }, panel { 0xff191c1d }, text { 0xffe6efee }, dim { 0xff2a3031 }, warm { 0xffe0a458 };

    // 今の設定での揺れ方（1 小節か、リズム・パターンの長さ分）と再生位置
    class WaveView : public juce::Component
    {
    public:
        explicit WaveView (MyTremoloProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        MyTremoloProcessor& proc;
    };

    // 形の表示と、Custom のときの点の編集（クリックで足す・ドラッグで動かす・option クリックで消す）
    class ShapeEditor : public juce::Component
    {
    public:
        explicit ShapeEditor (MyTremoloProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        MyTremoloProcessor& proc;
        int dragIdx = -1;
        mytrem::CustomShape work;
        juce::Point<float> toScreen (float x, float y) const;
        juce::Point<float> fromScreen (juce::Point<float>) const;
        bool editable() const;
    };

    // リズムの升目（クリック: 鳴らす / 休み、shift か右クリック: 前の 1 回を伸ばす）
    class PatternEditor : public juce::Component
    {
    public:
        explicit PatternEditor (MyTremoloProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        MyTremoloProcessor& proc;
        int lastCell = -1; uint8_t paintValue = 1;
        int cellAt (juce::Point<float>) const;
        void setCell (int idx, uint8_t v);
    };
}

class MyTremoloEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MyTremoloEditor (MyTremoloProcessor&);
    ~MyTremoloEditor() override;

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

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void setupBox (juce::ComboBox&, std::unique_ptr<BoxAttachment>&, const juce::String& paramId, const juce::String& tip);
    void timerCallback() override;
    void showTab (int t);
    void syncPatternBoxes();
    void saveFile (bool shape);
    void loadFile (bool shape);
    juce::File libFolder (bool shape) const;

    MyTremoloProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    Knob input, depth, groove, feel, accentK, rate, width, lowKeep, output;
    Knob thr, att, rel, rateMod, depthMod, smooth;
    juce::ComboBox styleBox, rhythmBox, shapeBox, envModeBox, smoothModeBox, factoryBox;
    std::unique_ptr<BoxAttachment> styleAtt, rhythmAtt, shapeAtt, envModeAtt, smoothModeAtt;
    juce::TextButton syncBtn, tapBtn;
    std::unique_ptr<BtnAttachment> syncAtt;

    juce::TextButton tabBtn[3];
    int tab = 0;

    mytremui::WaveView wave { processor };
    mytremui::ShapeEditor shapeEd { processor };
    mytremui::PatternEditor patternEd { processor };
    juce::TextButton copyToCustomBtn, shapeSaveBtn, shapeLoadBtn, shapeResetBtn;
    juce::ComboBox barsBox, beatsBox, gridBox;
    juce::TextButton patSaveBtn, patLoadBtn, patClearBtn, patFillBtn;
    juce::Label hintLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    // 画面のいちばん下: Plugin Link と GAIN MATCH・プリセット・取り消し（2 段）
    plink::LinkBar linkBar { processor.apvts, mytremui::accent, mytremui::panel, mytremui::text };
    myp::PresetBar presetBar { processor.myPresets, mytremui::accent, mytremui::panel, mytremui::text };
    juce::Slider duckSlider;
    std::unique_ptr<Attachment> duckAtt;
    static constexpr int footerH = 72;

    int lastEditVersion = -1;
    float inMeter = -100.0f, envMeter = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyTremoloEditor)
};
