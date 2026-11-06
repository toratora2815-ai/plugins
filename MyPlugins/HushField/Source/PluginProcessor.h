#pragma once
#include <JuceHeader.h>
#include "HushEngine.h"

class HushFieldProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    HushFieldProcessor();
    ~HushFieldProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    // 帯域 b（0..7）のパラメーター ID。what = used / on / shape / freq / q / gain / slope / focus
    static juce::String bandId (int b, const char* what) { return "b" + juce::String (b + 1) + what; }

    // プリセット
    static juce::StringArray presetNames();
    void applyPreset (int index);

    // A / B 比べ（今の設定と、もう一つの設定を入れ替える）
    bool isOnB() const { return onB; }
    void switchAB (bool toB);
    void copyAtoB();

    juce::AudioProcessorValueTreeState apvts;

    const hf::HushEngine& engine() const { return eng; }
    hf::Settings currentSettings() const { return readSettings(); }
    bool sidechainConnected() const { return scConnected.load(); }

private:
    hf::Settings readSettings() const;
    void handleAsyncUpdate() override;

    hf::HushEngine eng;
    std::atomic<int> reportedLatency { -1 }, wantLatency { 0 };
    std::atomic<bool> scConnected { false };

    juce::ValueTree otherState;   // A/B のもう片方
    bool onB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HushFieldProcessor)
};
