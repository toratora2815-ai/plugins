#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "MyTremoloDSP.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。Fader Pilot の「歌っているか」を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu が使用中
constexpr uint32_t kMyTremoloLinkKind = 21;

class MyTremoloProcessor : public juce::AudioProcessor
{
public:
    MyTremoloProcessor();
    ~MyTremoloProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "MyTremolo" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kMyTremoloLinkKind, apvts };   // Plugin Link
    plink::VocalDuck duck;                                    // Fader Pilot が歌っている間、揺れを浅く
    myp::GainMatch gainMatch { *this, myPresets };           // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    // 描いたパターンと形（パラメーターではない。セッションに保存し、画面の SAVE で別ファイルにも保存できる）
    mytrem::Pattern getPattern() const { const juce::ScopedLock sl (editLock); return pattern; }
    mytrem::CustomShape getCustomShape() const { const juce::ScopedLock sl (editLock); return customShape; }
    void setPattern (const mytrem::Pattern& p);
    void setCustomShape (const mytrem::CustomShape& c);
    std::atomic<int> editVersion { 0 };   // パターン・形が変わるたびに増える（画面の描き直し用）

    // 画面に描くため
    mytrem::Engine engine;
    mytrem::Knobs currentKnobs() const;
    std::atomic<double> shownBpm { 120 };
    float tapTempo();   // TAP を押した（BPM を返す。2 回目から）
    static juce::String patternToString (const mytrem::Pattern&);
    static bool patternFromString (const juce::String&, mytrem::Pattern&);
    static juce::String shapeToString (const mytrem::CustomShape&);
    static bool shapeFromString (const juce::String&, mytrem::CustomShape&);

    // 内蔵プリセット（つまみ＋形＋パターンを一度に入れる）
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* p[32] {};
    enum { pIn, pOut, pDepth, pGroove, pFeel, pAccent, pWidth, pShape, pRhythm, pRate, pSync, pStyle,
           pThr, pAtt, pRel, pEnvMode, pRateMod, pDepthMod, pLowKeep, pSmooth, pSmoothMode, pGainMatch, pLinkDuck, pNum };

    juce::CriticalSection editLock;
    mytrem::Pattern pattern;
    mytrem::CustomShape customShape;
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyTremoloProcessor)
};
