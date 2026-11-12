#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace nzoui
{
    inline const juce::Colour accent { 0xff7bd6c0 }, humCol { 0xffe8c36a }, roomCol { 0xffa99be8 }, voiceCol { 0xfff0a07a }, panel { 0xff15181c },
                              text { 0xffe8eef2 }, dim { 0xff262b31 }, redCol { 0xffe07a6a };

    // 上のグラフ: 入口の大きさ（灰、左の目盛り）・しきい値（雑音の推定、緑の線）・いま下げている量（上から垂れる赤、右の目盛り）・ハムの倍音（黄の縦線）
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

    // DETAIL ボタン: 押すと段の中身を細かいつまみに入れ替える。中のつまみが初期値から動いていたら点が付いて光る。ダブルクリックで初期値に戻す
    class DetailButton : public juce::TextButton
    {
    public:
        std::function<void()> onDoubleClick;
        bool changed = false;
        juce::Colour dotColour { 0xffffffff };
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            juce::TextButton::paintButton (g, over, down);
            if (changed)
            {
                g.setColour (getToggleState() ? juce::Colours::black : dotColour);
                g.fillEllipse ((float) getWidth() - 11.0f, (float) getHeight() * 0.5f - 3.0f, 6.0f, 6.0f);
            }
        }
    };

    // 段の中のボタンだけ字を小さめに（"nzoSmall" の印があるもの。下の LINK・プリセット欄はそのまま）
    struct Lnf : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
        {
            if (b.getProperties().contains ("nzoSmall"))
                return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.52f), juce::Font::bold));
            return juce::LookAndFeel_V4::getTextButtonFont (b, buttonHeight);
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
    void setupDetail (int section, const juce::String& tip);
    void placeKnob (Knob&, juce::Rectangle<int>, int labelH = 16);
    void setDetailOpen (int section, bool open);
    bool detailChanged (int section) const;
    void resetDetail (int section);
    void timerCallback() override;

    NoiseOutProcessor& processor;
    nzoui::Lnf lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // いつも見えるつまみ
    Knob humRed, humHarm, noiseRed, noiseThr, roomRed, roomTail, mouthSens, breathRed, plosStr, output;
    // DETAIL の中
    Knob humWidth, roomB0, roomB1, roomB2, roomB3, mouthSkew, breathSens, plosSens, plosFreq;
    juce::TextButton humOnBtn, noiseOnBtn, roomOnBtn, deltaBtn, learnBtn, mouthOnBtn, breathOnBtn, plosOnBtn, gentleBtn, musicBtn, dialogueBtn;
    std::unique_ptr<BtnAttachment> humOnAtt, noiseOnAtt, roomOnAtt, deltaAtt, mouthOnAtt, breathOnAtt, plosOnAtt, gentleAtt;
    juce::ComboBox humFreqBox, noiseModeBox, factoryBox;
    std::unique_ptr<BoxAttachment> humFreqAtt, noiseModeAtt;
    juce::Label humStatus, noiseStatus, roomStatus, voiceStatus, voiceTitle, roomHint, detailNoteHum, detailNoteNoise, detailNoteRoom;
    nzoui::DetailButton detailBtn[4];
    bool detailOpen[4] { false, false, false, false };
    // VOICE FIX の様子: 直近 10 秒に直したピチャ音の数（30 Hz で数を控える輪）、息・吹かれの下げ量（少し保持して読みやすく）
    std::array<int, 300> clickRing {};
    int clickTick = 0;
    float breathHold = 0.0f, plosHold = 0.0f;

    nzoui::SpectrumView spectrum { processor };

    plink::LinkBar linkBar { processor.apvts, nzoui::accent, nzoui::panel, nzoui::text };
    myp::PresetBar presetBar { processor.myPresets, nzoui::accent, nzoui::panel, nzoui::text };
    static constexpr int W = 1240, topH = 58, graphH = 220, rowH = 236, footerH = 40;   // 幅 1200 以上なので下の欄は 1 段

    juce::Rectangle<int> humArea, noiseArea, roomArea, voiceArea, outArea, graphArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseOutEditor)
};
