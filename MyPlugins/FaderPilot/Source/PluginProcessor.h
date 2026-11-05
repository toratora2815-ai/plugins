#pragma once
#include <JuceHeader.h>
#include "RiderEngine.h"
#include "PluginLinkJuce.h"

class FaderPilotProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FaderPilotProcessor();
    ~FaderPilotProcessor() override;

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

    // プリセット（画面上のメニューから呼ぶ）
    static juce::StringArray presetNames();
    void applyPreset (int index);

    bool isHostPlaying() const { return playing.load(); }
    bool isWriting() const { return writing; }
    bool sidechainConnected() const { return scConnected.load(); }

    juce::AudioProcessorValueTreeState apvts;
    fp::RiderEngine engine;

    // Plugin Link（同じ LINK 番号の Horizon Spread へフェーダーの位置を送る）
    plink::Link link { plink::kindFaderPilot, apvts };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }

private:
    fp::Settings readSettings() const;
    void timerCallback() override;   // フェーダーの動きを「RIDE」パラメーターに書く（メッセージスレッド）

    std::atomic<bool> playing { false }, scConnected { false };
    bool writing = false;            // ホストに「触っている」と伝えている間 true

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderPilotProcessor)
};
