#pragma once
#include <JuceHeader.h>
#include "GainEngine.h"
#include "PluginLinkJuce.h"

class GainStageProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GainStageProcessor();
    ~GainStageProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // LEARN（画面のボタンから）。もう一度押すと、それまでに測った分で決める
    void toggleLearn();
    // 最後に LEARN した結果の説明（プロジェクトに保存される）。音が来なかったときは flashMessage
    juce::String learnInfo() const { return apvts.state.getProperty ("learnInfo", "").toString(); }
    juce::String takeFlash() { auto s = flash; flash = {}; return s; }

    juce::AudioProcessorValueTreeState apvts;
    gs::GainEngine engine;

    // Plugin Link（同じ LINK 番号の Balance Scope へ、入口で足した GAIN と TARGET を送る）
    plink::Link link { plink::kindGainStage, apvts };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }

private:
    void timerCallback() override;   // LEARN の結果を GAIN に入れる（メッセージスレッド）
    bool wasLearning = false;
    std::atomic<bool> hasLearned { false };
    juce::String flash;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainStageProcessor)
};
