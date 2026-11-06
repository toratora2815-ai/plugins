#pragma once
#include <JuceHeader.h>
#include "dsp/Strip.h"
#include "ForgeLink.h"
#include "PresetBar.h"
#include <unordered_map>
#include <string>

// モジュールの ID の頭（パラメーター ID は "<頭>_..."）
namespace fsid
{
    inline const char* prefix (int m)
    {
        static const char* p[] = { "sc", "eq", "c1", "ex", "ts", "c2", "gt", "dn", "cl", "ph", "um" };
        return p[m];
    }
    // ID は短いので std::string の中に収まり、組み立ててもメモリを確保しない（音の処理中に使える）
    inline std::string id (int m, const char* what) { std::string s (prefix (m)); s += '_'; s += what; return s; }
    inline std::string bandId (int m, int b, const char* what)
    { std::string s (prefix (m)); s += "_b"; s += (char) ('1' + b); s += '_'; s += what; return s; }
    inline std::string eqId (int b, const char* what)
    { std::string s ("eq_b"); if (b >= 9) { s += '1'; s += (char) ('0' + (b + 1 - 10)); } else s += (char) ('1' + b); s += '_'; s += what; return s; }
    inline juce::String J (const std::string& s) { return juce::String (s); }
    // 帯域分けのあるモジュールか
    inline bool multiband (int m)
    {
        return m == fs::ModComp1 || m == fs::ModComp2 || m == fs::ModExciter || m == fs::ModTransient
            || m == fs::ModGate || m == fs::ModDensity || m == fs::ModClipper;
    }
}

class ForgeStripProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
{
public:
    ForgeStripProcessor();
    ~ForgeStripProcessor() override = default;

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    myp::Presets myPresets { *this, apvts, "ForgeStrip" };

    // 並び順（パラメーター *_pos から作る。setOrder はメッセージスレッドで）
    void getOrder (int* o) const;
    void setOrder (const int* o);

    const fs::Strip& strip() const { return eng; }
    fs::StripSettings currentSettings() const { return readSettings(); }
    bool sidechainConnected() const { return scConnected.load(); }

    // 入出力のメーター（dB）
    std::atomic<float> inLevel { -100.0f }, outLevel { -100.0f };
    std::atomic<float> bandLevel[3] { { -100.0f }, { -100.0f }, { -100.0f } };

    // Plugin Link（Forge Mixer から音量・パン・幅を動かせる）
    plink::Link link { fslink::kindStrip, apvts };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }
    bool mixerConnected() const { return mixerSeen.load(); }

private:
    fs::StripSettings readSettings() const;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void setParam (const char* id, float v);
    float token = 1.0f;
    std::atomic<bool> mixerSeen { false };
    fs::Biquad meterLp, meterHp;
    float p (const std::string& id) const;
    std::unordered_map<std::string, std::atomic<float>*> params;

    fs::Strip eng;
    std::atomic<int> reportedLatency { -1 }, wantLatency { 0 };
    std::atomic<bool> scConnected { false };
    std::atomic<float>* posValue[fs::numModules] {};
    juce::RangedAudioParameter* posParam[fs::numModules] {};
    juce::AudioBuffer<float> scBuf, monoBuf;
    double sr = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeStripProcessor)
};
