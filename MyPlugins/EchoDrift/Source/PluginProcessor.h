#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "EchoEngine.h"

class EchoDriftProcessor : public juce::AudioProcessor
{
public:
    EchoDriftProcessor();
    ~EchoDriftProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;
    myp::Presets myPresets { *this, apvts, "EchoDrift" };   // 自分のプリセット・取り消し
    plink::Link link { plink::kindEchoDrift, apvts };      // Plugin Link
    plink::VocalDuck duck;                                   // Fader Pilot が歌っている間エコーを下げる
    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

    // 内蔵プリセット
    static juce::StringArray presetNames();
    void applyPreset (int index);

    // 選択肢の名前
    static juce::StringArray styleNames();
    static const juce::StringArray& noteNames();      // ECHO 1 / 2（1/1〜1/64）
    static const juce::StringArray& rhythmNoteNames(); // RHYTHM（1/2〜1/64）

    // つまみ → エンジンの設定（テンポ同期もここで ms にする。画面の表示にも使う）
    echo::Params readParams() const;
    double getBpm() const { return hostBpm.load(); }
    double getRate() const { return sr; }

    // 画面用
    float getLevel (int i) const { return levels[i].load(); }   // 0 in L 1 in R 2 out L 3 out R
    std::atomic<bool> editorOpen { false };

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float value);
    static const juce::StringArray& paramOrder();
    std::vector<std::atomic<float>*> ptrs;

    echo::Engine engine;
    double sr = 48000.0;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<float> levels[4] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoDriftProcessor)
};
