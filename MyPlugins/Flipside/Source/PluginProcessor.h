#pragma once
#include <JuceHeader.h>
#include "FlipEngine.h"

class FlipsideProcessor : public juce::AudioProcessor
{
public:
    FlipsideProcessor();
    ~FlipsideProcessor() override = default;

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
    static const juce::StringArray& gridNames();

    juce::AudioProcessorValueTreeState apvts;
    fl::Engine engine;

    // 画面用
    std::atomic<bool> synced { false };
    std::atomic<float> sliceSeconds { 0.5f }, bpmShown { 120.0f };

private:
    double sliceBeats (const juce::AudioPlayHead::PositionInfo*) const;

    double fs = 48000.0, lastBpm = 120.0;
    int reportedLatency = -1;
    juce::SmoothedValue<float> outGain, mixSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlipsideProcessor)
};
