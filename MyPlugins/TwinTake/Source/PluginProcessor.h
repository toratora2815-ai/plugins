#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "TwinTakeDSP.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。LINK 欄だけ。自然な相手がないので何も送らない・受けない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo、22 = My Filter、23 = My Crystal、24 = Noise Out が使用中
constexpr uint32_t kTwinTakeLinkKind = 25;

class TwinTakeProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    TwinTakeProcessor();
    ~TwinTakeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { needReset.store (true); }   // Logic の Reset: 次のブロックの頭で読み出しを空に
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }   // ダブルは原音の 70 ms 後まで

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "TwinTake" };            // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kTwinTakeLinkKind, apvts };    // Plugin Link（LINK 欄だけ）
    myp::GainMatch gainMatch { *this, myPresets };                  // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override;

    int mode() const { return apvts.getRawParameterValue ("mode")->load() > 0.5f ? 1 : 0; }
    int voices() const { const int i = juce::roundToInt (apvts.getRawParameterValue ("voices")->load()); return i >= 2 ? 4 : (i == 1 ? 2 : 1); }

    // 画面用（音のスレッドが書く）
    std::atomic<bool> monoListen { false };              // MONO ボタンを押している間だけ（保存しない）
    std::atomic<bool> monoTrack { false };               // モノのトラック（1 → 1）に挿されている
    std::atomic<float> shownDelayMs[twin::Engine::maxVoices] {}, shownCents[twin::Engine::maxVoices] {};
    std::atomic<int> shownOnsets { 0 };
    std::atomic<float> shownGate { 1.0f };

    // 内蔵プリセット
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float value) override;   // MODE が変わったら遅れの申告を変える
    void updateLatency();
    twin::Params currentParams() const;

    twin::Engine engine;
    // ダブルの選び方の種（トラック名から一度だけ作り、状態に保存する。開いている間の改名では変えない。
    // 開き直したときに保存した名前と違えば = 別のトラックへコピーした → そのトラックの名前から作り直す）
    std::atomic<uint32_t> trackSeed { 0 };
    juce::CriticalSection seedLock;
    juce::String currentTrack, savedTrack;
    bool seedSet = false, justLoaded = false;
    std::atomic<bool> needReset { false };
    int64_t freeRun = 0;   // 止まっている間の位置（再生中は Logic の曲の位置を使う）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwinTakeProcessor)
};
