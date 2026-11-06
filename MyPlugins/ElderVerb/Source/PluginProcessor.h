#pragma once
#include <JuceHeader.h>
#include "ElderEngine.h"

class ElderVerbProcessor : public juce::AudioProcessor
{
public:
    ElderVerbProcessor();
    ~ElderVerbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    static const juce::StringArray& modeNames();
    static const juce::StringArray& colorNames();
    // つまみの ID（0..1 の値。表示だけ秒・Hz にする）
    static const juce::StringArray& knobIds();

    // プリセット
    struct Preset { juce::String name; std::map<juce::String, float> v; };
    static const std::vector<Preset>& builtinPresets();
    void applyValues (const std::map<juce::String, float>& v, const juce::String& name);
    // 本物の VintageVerb 形式（.vpreset、XML の属性に 0..1 の値）を読む。失敗したら理由を返す
    juce::String importVPreset (const juce::File&);
    static juce::File vpresetFolder();
    static juce::File myPresetFolder();
    juce::String saveMyPreset (const juce::String& name);
    juce::String getPresetName() const { return presetName; }

    float getLevel (int ch) const { return outLevel[ch].load(); }
    elder::Params currentParams() const;
    const elder::Profile& profileFor (int mode) const { return engine.profiles[juce::jlimit (0, elder::kNumModes - 1, mode)]; }

private:
    elder::Engine engine;
    elder::Params lastParams; bool firstBlock = true; int lastMode = -1;
    juce::SmoothedValue<float> dryGain, wetGain;
    juce::AudioBuffer<float> wetBuf;
    std::atomic<float> outLevel[2] { 0.0f, 0.0f };
    juce::String presetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElderVerbProcessor)
};
