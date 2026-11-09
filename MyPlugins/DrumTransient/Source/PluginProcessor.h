#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "DrumTransientDSP.h"

// Plugin Link の種類番号（PluginLink.h には未登録。Shared を変えると全部ビルドになるので、Vox Chop と同じく番号だけ使う）
constexpr uint32_t kDrumTransientLinkKind = 19;

class DrumTransientProcessor : public juce::AudioProcessor
{
public:
    DrumTransientProcessor();
    ~DrumTransientProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 内蔵プリセット
    static juce::StringArray presetNames();
    void applyPreset (int index);

    static juce::StringArray lowKeepNames()  { return { "OFF", "60 Hz", "90 Hz", "120 Hz", "160 Hz" }; }
    static juce::StringArray ceilingNames()  { return { "-0.1 dB", "-0.3 dB", "-1.0 dB" }; }

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "DrumTransient" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kDrumTransientLinkKind, apvts };   // Plugin Link（LINK 欄だけ）
    myp::GainMatch gainMatch { *this, myPresets, { "delta" } };   // GAIN MATCH（DELTA は聴くだけなので測り直さない）

    dt::Engine dsp;

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dt::Params readParams() const;

    std::atomic<float>* pPush = nullptr;
    std::atomic<float>* pPunch = nullptr;
    std::atomic<float>* pBody = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pLowKeep = nullptr;
    std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pGainMatch = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumTransientProcessor)
};
