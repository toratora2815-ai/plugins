#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "DecaSatuDSP.h"   // 音の部分（JUCE なし。後で Forge Strip のモジュールにも使えるように分けてある）

// Plugin Link の種類（PluginLink.h には未登録。Gain Stage の LEVEL MATCH を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient が使用中
constexpr uint32_t kDecaSatuLinkKind = 20;

class DecaSatuProcessor : public juce::AudioProcessor
{
public:
    DecaSatuProcessor();
    ~DecaSatuProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 5.0; }   // 大きい音が止まったあとの低い揺れが消えるまで

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "DecaSatu" };   // 自分のプリセット・取り消し

    plink::Link link { (plink::Kind) kDecaSatuLinkKind, apvts };   // Plugin Link
    plink::LevelMatch match;   // 出口を Gain Stage の TARGET にそろえる
    myp::GainMatch gainMatch { *this, myPresets };   // GAIN MATCH（本物の AUTO の曲線もこの中で使う）

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    // 画面の表示用: 今の出口の音量（dB、ピーク）
    std::atomic<float> outPeakDb { -100.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* pStyle = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pPunish = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pThump = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pSteep = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pGainMatch = nullptr;
    std::atomic<float>* pLinkMatch = nullptr;

    decasatu::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecaSatuProcessor)
};
