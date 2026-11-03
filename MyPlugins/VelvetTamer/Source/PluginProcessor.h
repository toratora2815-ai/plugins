#pragma once
#include <JuceHeader.h>
#include "TamerEngine.h"

class VelvetTamerProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    VelvetTamerProcessor();
    ~VelvetTamerProcessor() override = default;

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
    static juce::String weightId (int i) { return "weight" + juce::String (i); }

    // プリセット（画面上のメニューから呼ぶ）
    static juce::StringArray presetNames();
    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;

    // 画面用：いま使っているほうのエンジン（HIGH / LOW LATENCY）
    const vt::TamerEngine& displayEngine() const { return engines[activeLowLatency.load() ? 1 : 0]; }
    bool isLowLatency() const { return activeLowLatency.load(); }
    double getSampleRateForDisplay() const { return currentRate.load(); }

private:
    vt::Settings readSettings() const;
    void handleAsyncUpdate() override;

    // [0] = HIGH（今までどおり）、[1] = LOW LATENCY。両方とも先に準備しておき、切り替えで確保し直さない
    vt::TamerEngine engines[2];
    std::atomic<bool> activeLowLatency { false };
    std::atomic<double> currentRate { 48000.0 };

    std::atomic<float>* weightParams[vt::numWeights] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvetTamerProcessor)
};
