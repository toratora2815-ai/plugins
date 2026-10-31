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

private:
    void timerCallback() override;
    std::vector<float> toBands (const std::vector<double>& power, double sampleRate) const;
    void drawGraph (juce::Graphics&, juce::Rectangle<float>);
    void drawBroad (juce::Graphics&, juce::Rectangle<float>);

    BalanceScopeProcessor& processor;
    BalanceScopeProcessor::Snapshot snap;

    std::vector<float> centers;            // 1/6オクターブ中心周波数
    std::vector<float> avgDb, liveDb;      // 正規化済みのカーブ
    float broadDev[4] {}, broadTol[4] {};
    bool hasData = false;

    juce::ComboBox targetBox;
    juce::TextButton resetButton { "RESET" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
    juce::Rectangle<int> graphArea, broadArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceScopeEditor)
};
