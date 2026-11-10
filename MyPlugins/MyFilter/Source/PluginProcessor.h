#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "MyFilterDSP.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。Fader Pilot の「歌っているか」を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo が使用中
constexpr uint32_t kMyFilterLinkKind = 22;

class MyFilterProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    MyFilterProcessor();
    ~MyFilterProcessor() override;

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
    double getTailLengthSeconds() const override { return 5.0; }   // RES 0.95 で響きが −60 dB まで 1.8 秒（DSP 検査係）

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "MyFilter" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kMyFilterLinkKind, apvts };   // Plugin Link
    plink::VocalDuck duck;                                   // Fader Pilot が歌っている間、MOD を浅く
    myp::GainMatch gainMatch { *this, myPresets };          // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    // 描いたパターンと形（パラメーターではない。セッションに保存し、画面の SAVE で別ファイルにも保存できる）
    mytrem::Pattern getPattern() const { const juce::ScopedLock sl (editLock); return pattern; }
    mytrem::CustomShape getCustomShape() const { const juce::ScopedLock sl (editLock); return customShape; }
    void setPattern (const mytrem::Pattern& p);
    void setCustomShape (const mytrem::CustomShape& c);
    std::atomic<int> editVersion { 0 };

    myfilt::Engine engine;
    myfilt::Knobs currentKnobs() const;
    std::atomic<double> shownBpm { 120 };
    std::atomic<bool> monoToStereo { false };
    float tapTempo();
    std::atomic<int> linkGesture { 0 };   // 画面で FREQ・RES・GAIN をつかんでいる間だけ LINK で相手も動かす（プリセット・取り消しでは動かさない）

    static juce::String patternToString (const mytrem::Pattern&);
    static bool patternFromString (const juce::String&, mytrem::Pattern&);
    static juce::String shapeToString (const mytrem::CustomShape&);
    static bool shapeFromString (const juce::String&, mytrem::CustomShape&);

    // 内蔵プリセット
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;   // LINK（1 と 2 のつまみを一緒に動かす）

    enum { pIn, pOut, pMix, pAnalog, pStyle, pFilters, pRouting, pFLink,
           pFreq1, pRes1, pShape1, pPoles1, pGain1, pFMod1, pRMod1, pLMod1,
           pFreq2, pRes2, pShape2, pPoles2, pGain2, pFMod2, pRMod2, pLMod2,
           pMod, pMode, pLfoRate, pLfoShape, pRhythm, pTempo, pSync, pGroove,
           pThr, pEnvGain, pAtt, pRel, pTrigLvl, pTrigger, pA, pD, pS, pR,
           pLrOff, pLrMode, pSmooth, pSmoothMode, pGainMatch, pLinkDuck, pNum };
    std::atomic<float>* p[pNum] {};
    std::map<juce::String, float> lastNorm;   // LINK 用: 前の値
    bool linking = false;

    juce::CriticalSection editLock;
    mytrem::Pattern pattern;
    mytrem::CustomShape customShape;
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFilterProcessor)
};
