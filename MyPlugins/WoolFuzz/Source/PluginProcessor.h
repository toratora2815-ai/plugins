#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "WoolFuzzDSP.h"   // 音の部分（AIO Guitar の EFFECTS も同じものを使う）

class WoolFuzzProcessor : public juce::AudioProcessor
{
public:
    WoolFuzzProcessor();
    ~WoolFuzzProcessor() override = default;

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

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "WoolFuzz" };   // 自分のプリセット・取り消し

    plink::Link link { plink::kindWoolFuzz, apvts };   // Plugin Link
    plink::LevelMatch match;   // 出口を Gain Stage の TARGET にそろえる
    myp::GainMatch gainMatch { *this, myPresets };   // GAIN MATCH（入れても音量が変わらない）

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* toneParam    = nullptr;
    std::atomic<float>* volumeParam  = nullptr;

    pedal::WoolFuzzDSP dsp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WoolFuzzProcessor)
};
