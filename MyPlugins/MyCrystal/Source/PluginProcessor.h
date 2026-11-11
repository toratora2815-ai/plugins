#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "MyCrystalDSP.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。Fader Pilot の「歌っているか」を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo、22 = My Filter が使用中
constexpr uint32_t kMyCrystalLinkKind = 23;

class MyCrystalProcessor : public juce::AudioProcessor
{
public:
    MyCrystalProcessor();
    ~MyCrystalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { needReset.store (true); }   // Logic の Reset: 次のブロックの頭で輪を空に
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }   // RECYCLE 100 % ではいつまでも鳴る

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "MyCrystal" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kMyCrystalLinkKind, apvts };   // Plugin Link
    plink::VocalDuck duck;                                    // Fader Pilot が歌っている間、ウェットを下げる
    myp::GainMatch gainMatch { *this, myPresets };           // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    mycry::Engine engine;
    mycry::Knobs currentKnobs() const;
    std::atomic<double> shownBpm { 120 };

    // 内蔵プリセット
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

    // つまみの値 ⇄ 0..1（本物と同じ「指数」の曲がり方）
    static juce::NormalisableRange<float> expRange (float maxValue, float k);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    enum { pIn, pOut, pMix, pAnalog, pPitch, pDir, pSplice, pSpliceNote, pDelay, pDelayNote, pSync, pRecycle,
           pThr, pGateDuck, pPitchOff, pSpliceOff, pDelayOff, pSmooth, pLowCut, pHighCut, pAtt, pRel,
           pDuckMode, pFbMode, pLinkDuck, pNum };
    std::atomic<float>* p[pNum] {};
    std::atomic<bool> needReset { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyCrystalProcessor)
};
