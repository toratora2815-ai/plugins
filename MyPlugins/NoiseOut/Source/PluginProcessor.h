#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "NoiseOutDSP.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。Fader Pilot の「歌っているか」を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo、22 = My Filter、23 = My Crystal が使用中
constexpr uint32_t kNoiseOutLinkKind = 24;

class NoiseOutProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    NoiseOutProcessor();
    ~NoiseOutProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { needReset.store (true); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }   // HUM の細いノッチは止めた後もしばらく鳴き残る
    // DETAIL（折りたたみ）に入っているパラメーターと、その段（0 HUM / 1 NOISE / 2 ROOM / 3 VOICE）
    static const std::vector<std::pair<juce::String, int>>& detailParams();

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "NoiseOut" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kNoiseOutLinkKind, apvts };   // Plugin Link
    myp::GainMatch gainMatch { *this, myPresets, { "delta" } };   // GAIN MATCH（DELTA は聴くだけなので測り直さない）

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    nzo::Engine engine;
    nzo::Knobs currentKnobs() const;

    // LEARN（画面のボタン）: 押すと雑音の形を覚え始め、鳴っている所を 3 秒聴いたら自動で終わって LEARNED になる。もう一度押すとそこで終わる。
    // 押した回数を音のスレッドへ渡す（状態のフラグにしないので、途中で止まっても次の 1 回で必ず始まる）
    void toggleLearn() { if (engine.learning.load()) ++learnStopSeq; else ++learnStartSeq; }
    bool isLearning() const { return engine.learning.load(); }
    void forgetProfile();

    // Plugin Link の様子（画面用）
    std::atomic<bool> sawPilot { false }, pilotSinging { false };

    // 内蔵プリセット
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void saveExtraState (juce::ValueTree& st);
    void loadExtraState (const juce::ValueTree& st);

    enum { pHumOn, pHumFreq, pHumRed, pHumHarm, pNoiseOn, pNoiseMode, pNoiseRed, pNoiseThr, pRoomOn, pRoomRed, pRoomTail, pOut, pDelta,
           pNoiseOpt, pNoiseGentle, pRoomB0, pRoomB1, pRoomB2, pRoomB3, pHumWidth,
           pMouthOn, pMouthSens, pMouthSkew, pBreathOn, pBreathRed, pBreathSens, pPlosOn, pPlosStr, pPlosSens, pPlosFreq, pNum };
    std::atomic<float>* p[pNum] {};
    std::atomic<bool> needReset { false };
    std::atomic<int> learnStartSeq { 0 }, learnStopSeq { 0 };
    int seenProfileVersion = 0;
    juce::CriticalSection stateLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseOutProcessor)
};
