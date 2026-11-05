#pragma once
#include <JuceHeader.h>
#include "TamerEngine.h"
#include "PluginLinkJuce.h"

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

    // Plugin Link（同じ LINK 番号の Balance Scope が見つけた「出すぎの帯域」に FOCUS を合わせる）
    plink::Link link { plink::kindVelvetTamer, apvts };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }
    // 画面用：いま実際に使っている FOCUS と、つながって動かされているか
    float focusLowNow() const { return shownLow.load(); }
    float focusHighNow() const { return shownHigh.load(); }
    bool focusLinked() const { return linkFollowing.load(); }
    plink::Peer linkPeer() const { plink::Peer p; link.ep().find (plink::kindBalanceScope, p); return p; }

private:
    vt::Settings readSettings() const;
    void handleAsyncUpdate() override;

    // [0] = HIGH（今までどおり）、[1] = LOW LATENCY。両方とも先に準備しておき、切り替えで確保し直さない
    vt::TamerEngine engines[2];
    std::atomic<bool> activeLowLatency { false };
    std::atomic<double> currentRate { 48000.0 };

    std::atomic<float>* weightParams[vt::numWeights] {};

    // FOCUS の追いかけ（log2 Hz、音の処理のスレッドだけ）
    void applyLinkFocus (vt::Settings& s, int numSamples);
    float curLo = -1.0f, curHi = -1.0f;
    std::atomic<float> shownLow { 300.0f }, shownHigh { 16000.0f };
    std::atomic<bool> linkFollowing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvetTamerProcessor)
};
