#pragma once
#include <JuceHeader.h>
#include "HitEngine.h"

class HitSculptorProcessor : public juce::AudioProcessor
{
public:
    HitSculptorProcessor();
    ~HitSculptorProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // 内蔵プリセット
    struct Preset { const char* name; float attack, snap, length, room, size, tone, drive, mix, output; };
    static const std::vector<Preset>& presets();
    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;
    hs::Engine engine;

private:
    hs::Settings readSettings() const;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::SmoothedValue<float> driveAmt, outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitSculptorProcessor)
};
