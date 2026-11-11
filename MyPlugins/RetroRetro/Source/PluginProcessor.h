#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "RetroEngine.h"   // 音の部分（JUCE なし）

// Plugin Link の種類（PluginLink.h には未登録。Fader Pilot の「歌っているか」を受けるだけなので Shared は変えない）
// 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo、22 = My Filter、23 = My Crystal、24 = Noise Out、25 = Twin Take が使用中
constexpr uint32_t kRetroRetroLinkKind = 26;

namespace rrp
{
    // つまみの表示のしかた
    enum class Show { pct, db36, hz, wowHz, flutHz, magHz, echoMs, bipolar, decay, pre, pitch, scrub, width, none };
    enum class Kind { flt, choice, toggle };
    struct Def
    {
        const char* id; const char* name; Kind kind; float def; Show show; int module;   // module: 0 MASTER, 1 NOISE, 2 WOBBLE, 3 DISTORT, 4 DIGITAL, 5 SPACE, 6 LOSS, 7 ECHO
        juce::StringArray choices;
    };
    const std::vector<Def>& defs();
    constexpr int kNumModules = 8;
    const char* moduleName (int m);
}

class RetroRetroProcessor : public juce::AudioProcessor
{
public:
    RetroRetroProcessor();
    ~RetroRetroProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 30.0; }   // ECHO のフィードバック 100 % は鳴り続ける

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "RetroRetro" };   // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kRetroRetroLinkKind, apvts };   // Plugin Link
    plink::VocalDuck duck;                                    // Fader Pilot が歌っている間、ノイズを下げる
    myp::GainMatch gainMatch { *this, myPresets };           // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    rr::Engine engine;
    rr::Knobs currentKnobs() const;

    // 内蔵プリセット
    static int numFactoryPresets();
    static juce::String factoryPresetName (int i);
    void loadFactoryPreset (int i);

    std::atomic<float> meterIn { -100.0f }, meterOut { -100.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::vector<std::atomic<float>*> raw;
    std::atomic<float>* linkDuckDb = nullptr;
    std::atomic<bool> needReset { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroRetroProcessor)
};
