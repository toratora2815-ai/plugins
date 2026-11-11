#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace rrui
{
    inline const juce::Colour bg { 0xff1d1b18 }, panel { 0xff26231f }, tile { 0xff2e2a25 }, accent { 0xffe2a75a }, accent2 { 0xff79b8a9 },
                              text { 0xfff0e7d8 }, dim { 0xff8a8174 }, off { 0xff4a453e };
}

class RetroRetroEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RetroRetroEditor (RetroRetroProcessor&);
    ~RetroRetroEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // 1 つのつまみ（回すつまみ・選択・ON/OFF のどれか）
    struct Ctrl
    {
        juce::String id; rrp::Kind kind; int module = 0;
        std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::ComboBox> box; std::unique_ptr<juce::TextButton> btn;
        juce::Label label;
        std::unique_ptr<SAtt> sAtt; std::unique_ptr<CAtt> cAtt; std::unique_ptr<BAtt> bAtt;
        juce::Component* comp() { return slider ? (juce::Component*) slider.get() : box ? (juce::Component*) box.get() : (juce::Component*) btn.get(); }
    };
    std::unique_ptr<Ctrl> makeCtrl (const juce::String& id, const juce::String& labelText, bool big = false);
    void placeCtrl (Ctrl&, juce::Rectangle<int>);

    void timerCallback() override;
    void setView (int v);           // 0 = MAIN、1 = DETAIL
    void setDetailModule (int m);

    RetroRetroProcessor& processor;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // MAIN（表）
    struct Tile { int module; std::unique_ptr<Ctrl> on, amount, sub1, sub2; juce::Rectangle<int> area; };
    std::vector<Tile> tiles;
    std::unique_ptr<Ctrl> magnitude, tone, toneMode, compress, outWidth, mix, outGain;

    // DETAIL（モジュールごとの全部のつまみ）
    std::vector<std::unique_ptr<Ctrl>> detail;
    juce::OwnedArray<juce::TextButton> moduleTabs;
    int detailModule = 1;

    juce::TextButton mainBtn { "MAIN" }, detailBtn { "DETAIL" };
    juce::ComboBox factoryBox;
    int view = 0, clearHold = 0;
    float inMeter = -100.0f, outMeter = -100.0f;

    plink::LinkBar linkBar { processor.apvts, rrui::accent, rrui::panel, rrui::text };
    myp::PresetBar presetBar { processor.myPresets, rrui::accent, rrui::panel, rrui::text };
    juce::Slider duckSlider;
    std::unique_ptr<SAtt> duckAtt;

    static constexpr int W = 1120, topH = 50, mainH = 340, footerH = 72;
    juce::Rectangle<int> masterArea, detailArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroRetroEditor)
};
