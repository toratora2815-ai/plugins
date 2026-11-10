#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace myfiltui
{
    inline const juce::Colour accent { 0xffd9894a }, accent2 { 0xff5fb0c9 }, panel { 0xff1b1918 }, text { 0xfff0e9e2 }, dim { 0xff302b28 }, warm { 0xffe8c36a };

    // フィルターの形（今の周波数で）と、元（MOD の量）のメーター
    class ResponseView : public juce::Component
    {
    public:
        explicit ResponseView (MyFilterProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        MyFilterProcessor& proc;
    };

    // 揺れの形（Custom のときは点を編集。クリックで足す・ドラッグで動かす・option クリックで消す）
    class ShapeEditor : public juce::Component
    {
    public:
        explicit ShapeEditor (MyFilterProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        MyFilterProcessor& proc;
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
        explicit PatternEditor (MyFilterProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        MyFilterProcessor& proc;
        int lastCell = -1; uint8_t paintValue = 1;
        int cellAt (juce::Point<float>) const;
        void setCell (int idx, uint8_t v);
    };
}

class MyFilterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MyFilterEditor (MyFilterProcessor&);
    ~MyFilterEditor() override;

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
    void setupToggle (juce::TextButton&, std::unique_ptr<BtnAttachment>&, const juce::String& paramId, const juce::String& text, const juce::String& tip);
    void timerCallback() override;
    void showTab (int t);
    void layoutModePanel();
    void syncPatternBoxes();
    void saveFile (bool shape);
    void loadFile (bool shape);
    juce::File libFolder (bool shape) const;
    void setParam (const juce::String& id, float plainValue);

    MyFilterProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    // いつも出ている所
    Knob input, mix, output, mod;
    Knob freq1, res1, gain1, freq2, res2, gain2;
    juce::ComboBox shape1Box, poles1Box, shape2Box, poles2Box, filtersBox, routingBox, styleBox, modeBox, factoryBox;
    std::unique_ptr<BoxAttachment> shape1Att, poles1Att, shape2Att, poles2Att, filtersAtt, routingAtt, styleAtt, modeAtt;
    juce::TextButton linkBtn, analogBtn;
    std::unique_ptr<BtnAttachment> linkAtt, analogAtt;

    // 動かし方ごとの所
    Knob lfoRate, groove, tempo, thr, envGain, att, rel, trigLvl, adsrA, adsrD, adsrS, adsrR;
    juce::ComboBox shapeBox, rhythmBox;
    std::unique_ptr<BoxAttachment> shapeAtt, rhythmAtt;
    juce::TextButton syncBtn, tapBtn, triggerBtn;
    std::unique_ptr<BtnAttachment> syncAtt;
    int lastMode = -1;

    // 下のタブ
    juce::TextButton tabBtn[3];
    int tab = 0;
    Knob fmod1, rmod1, lmod1, fmod2, rmod2, lmod2, lrOff, smooth;
    juce::ComboBox lrModeBox, smoothModeBox;
    std::unique_ptr<BoxAttachment> lrModeAtt, smoothModeAtt;

    myfiltui::ResponseView response { processor };
    myfiltui::ShapeEditor shapeEd { processor };
    myfiltui::PatternEditor patternEd { processor };
    juce::TextButton copyToCustomBtn, shapeSaveBtn, shapeLoadBtn, shapeResetBtn;
    juce::ComboBox barsBox, beatsBox, gridBox;
    juce::TextButton patSaveBtn, patLoadBtn, patClearBtn, patFillBtn;
    juce::Label hintLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    // 画面のいちばん下: Plugin Link と GAIN MATCH・プリセット・取り消し（2 段）
    plink::LinkBar linkBar { processor.apvts, myfiltui::accent, myfiltui::panel, myfiltui::text };
    myp::PresetBar presetBar { processor.myPresets, myfiltui::accent, myfiltui::panel, myfiltui::text };
    juce::Slider duckSlider;
    std::unique_ptr<Attachment> duckAtt;
    static constexpr int footerH = 72;

    juce::StringArray linkTouched;   // LINK で相手に beginChangeGesture したもの
    int lastEditVersion = -1;
    float inMeter = -100.0f;
    juce::Rectangle<int> filter1Area, filter2Area, modArea, modePanelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFilterEditor)
};
