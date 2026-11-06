#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"

// 1次フィルター（ローパス／ハイパス兼用）
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, double fs) { a = std::exp (-juce::MathConstants<float>::twoPi * fc / (float) fs); }
    float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

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

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* toneParam    = nullptr;
    std::atomic<float>* volumeParam  = nullptr;

    juce::SmoothedValue<float> driveGain, toneMix, outGain;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    static constexpr int maxCh = 2;
    OnePole inHP[maxCh];                      // 入力の低域カット
    OnePole stage1LP[maxCh], stage2LP[maxCh]; // 段間のハイ落とし
    OnePole dcBlock1[maxCh], dcBlock2[maxCh]; // 非対称クリップ後のDCカット
    OnePole toneLP[maxCh], toneHP[maxCh];     // トーン回路

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WoolFuzzProcessor)
};
