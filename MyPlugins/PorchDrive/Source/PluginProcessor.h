#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, double fs) { a = std::exp (-juce::MathConstants<float>::twoPi * fc / (float) fs); }
    float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

class PorchDriveProcessor : public juce::AudioProcessor
{
public:
    PorchDriveProcessor();
    ~PorchDriveProcessor() override = default;

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

    myp::Presets myPresets { *this, apvts, "PorchDrive" };   // 自分のプリセット・取り消し

    plink::Link link { plink::kindPorchDrive, apvts };   // Plugin Link
    plink::LevelMatch match;   // 出口を Gain Stage の TARGET にそろえる

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* gainParam  = nullptr;
    std::atomic<float>* toneParam  = nullptr;
    std::atomic<float>* levelParam = nullptr;

    juce::SmoothedValue<float> g1, g2, toneGain, outGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    static constexpr int maxCh = 2;
    OnePole inHP[maxCh], dc1[maxCh], dc2[maxCh], lp1[maxCh], lp2[maxCh];
    OnePole toneSplit[maxCh], fizzLP[maxCh];

    // オーバーサンプリング側で使うゲインの一時バッファ
    std::vector<float> g1Buf, g2Buf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PorchDriveProcessor)
};
