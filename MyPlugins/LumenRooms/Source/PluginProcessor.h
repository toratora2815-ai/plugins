#pragma once
#include <JuceHeader.h>
#include "RoomEngine.h"
#include "ProfileTable.h"

class LumenRoomsProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LumenRoomsProcessor();
    ~LumenRoomsProcessor() override;

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
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // 画面に出す反射の形（12 個の粒の時刻と強さ）
    struct ErView { float timeMs[12]; float db[12]; float lenMs[12]; };
    ErView getErView() const;
    float getOutputLevel (int ch) const { return outLevel[ch].load(); }

    static const juce::StringArray& patternNames();
    static const juce::StringArray& proximityNames();
    static const juce::StringArray& slopeNames();
    static const juce::StringArray& noteNames();
    static const juce::StringArray& colorNames();
    const lumen::ProfileTable& getProfiles() const { return profiles; }

    // よその .crp_preset ファイル（JUCE の ValueTree 形式）を読み込む。失敗したら理由を返す
    juce::String importPreset (const juce::File&);
    juce::String getImportedName() const { return importedName; }
    static juce::File defaultPresetFolder();
    static juce::File userPresetFolder();

private:
    void timerCallback() override;
    lumen::ReflParams readRefl() const;
    lumen::TailParams readTail() const;
    lumen::MixParams readMix() const;
    void rebuildReflections();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    lumen::RoomEngine engine;
    lumen::ProfileTable profiles;
    juce::dsp::Convolution convFromL, convFromR;
    juce::AudioBuffer<float> bufL, bufR;
    double sr = 48000.0;
    int maxBlock = 512;

    lumen::ReflParams lastRefl;
    bool haveRefl = false;
    std::atomic<bool> needRebuild { true };
    std::atomic<float> outLevel[2] { { 0.0f }, { 0.0f } };
    int currentProgram = 0;
    juce::String importedName;
    std::atomic<double> hostBpm { 120.0 };
    void setParam (const char* id, float value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRoomsProcessor)
};
