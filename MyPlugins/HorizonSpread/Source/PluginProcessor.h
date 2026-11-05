#pragma once
#include <JuceHeader.h>
#include "SpreadEngine.h"
#include "PluginLinkJuce.h"

class HorizonSpreadProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    HorizonSpreadProcessor();
    ~HorizonSpreadProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    // 内蔵プリセット（Logic のプリセットメニューと、画面上のメニューの両方から選べる）
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    hs::SpreadEngine engine;

    // Plugin Link（同じ LINK 番号の Fader Pilot がボーカルを上げている間、広がりを少し抑える）
    plink::Link link { plink::kindHorizonSpread, apvts };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }
    float linkDuckNow() const { return duckShown.load(); }   // 0..1（SPREAD を何割下げているか）
    bool linkHasPilot() const { return pilotSeen.load(); }

private:
    void timerCallback() override;
    hs::ShapeSettings readSettings() const;
    int currentProgram = 0;

    float duckEnv = 0.0f;   // 音の処理のスレッドだけ
    std::atomic<float> duckShown { 0.0f };
    std::atomic<bool> pilotSeen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonSpreadProcessor)
};
