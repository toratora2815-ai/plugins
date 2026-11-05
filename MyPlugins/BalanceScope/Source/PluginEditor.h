#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class BalanceScopeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BalanceScopeEditor (BalanceScopeProcessor&);
    ~BalanceScopeEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int numRefs = 3;   // 参照カーブのスロット（A / B / C）

private:
    void timerCallback() override;
    std::vector<float> toBands (const std::vector<double>& power, double sampleRate) const;
    void drawGraph (juce::Graphics&, juce::Rectangle<float>);
    void drawDiff (juce::Graphics&, juce::Rectangle<float>);
    void drawBroad (juce::Graphics&, juce::Rectangle<float>);
    void drawLoudness (juce::Graphics&, juce::Rectangle<float>);

    // 参照カーブ（プロジェクトに保存される。apvts.state の子 "REFS" に入れる）
    juce::ValueTree refsTree();
    std::vector<float> loadRef (int slot);
    juce::String refInfo (int slot);
    int compareSlot();                  // -1 = 比べない、0..2 = A..C
    void setCompareSlot (int slot);
    void capture();
    void clearRef();
    void updateRefControls();

    BalanceScopeProcessor& processor;
    BalanceScopeProcessor::Snapshot snap;

    std::vector<float> centers;            // 1/6オクターブ中心周波数
    std::vector<float> avgRaw;             // 正規化前（CAPTURE で保存する値）
    std::vector<float> avgDb, liveDb;      // 正規化済みのカーブ
    std::vector<float> refDb;              // 正規化済みの参照カーブ（比べていないときは空）
    float broadDev[4] {}, broadTol[4] {};
    bool hasData = false, diffView = false;
    int shownSlot = -1;

    juce::ComboBox targetBox, compareBox;
    juce::TextButton resetButton { "RESET" }, captureButton { "CAPTURE" }, clearButton { "CLEAR" }, diffButton { "DIFF" };
    juce::Slider tiltSlider, lowSlider;
    juce::Label tiltLabel, lowLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAttachment, lowAttachment;
    juce::Rectangle<int> graphArea, broadArea, loudArea, refInfoArea;
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffff8fb0), juce::Colour (0xff1a1f26), juce::Colour (0xfff2f2f2) };
    juce::String flashText;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceScopeEditor)
};
