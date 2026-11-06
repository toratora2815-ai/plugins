#pragma once
#include <JuceHeader.h>
#include "dsp/Strip.h"
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

class ForgeStripProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
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

    // 並び順（メッセージスレッドで変える）
    void getOrder (int* o) const;
    void setOrder (const int* o);

    const fs::Strip& strip() const { return eng; }
    fs::StripSettings currentSettings() const { return readSettings(); }
    bool sidechainConnected() const { return scConnected.load(); }

    // 入出力のメーター（dB）
    std::atomic<float> inLevel { -100.0f }, outLevel { -100.0f };

private:
    fs::StripSettings readSettings() const;
    void handleAsyncUpdate() override;
    float p (const std::string& id) const;
    std::unordered_map<std::string, std::atomic<float>*> params;

    fs::Strip eng;
    std::atomic<int> reportedLatency { -1 }, wantLatency { 0 };
    std::atomic<bool> scConnected { false };
    mutable juce::SpinLock orderLock;
    int order[fs::numModules];
    juce::AudioBuffer<float> scBuf, monoBuf;
    double sr = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeStripProcessor)
};
