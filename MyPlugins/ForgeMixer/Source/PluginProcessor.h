#pragma once
#include <JuceHeader.h>
#include "ForgeLink.h"

// Forge Mixer: 音は素通し。同じ LINK 番号の Forge Strip を一覧し、音量・パン・幅の指示を出す
class ForgeMixerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ForgeMixerProcessor();
    ~ForgeMixerProcessor() override = default;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto o = l.getMainOutputChannelSet();
        return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && l.getMainInputChannelSet() == o;
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
    void setStateInformation (const void* data, int size) override
    { if (auto x = getXmlFromBinary (data, size)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x)); }

    juce::AudioProcessorValueTreeState apvts;
    plink::Link link { fslink::kindMixer, apvts };

    // 画面から: いま動かしている Strip への指示（token が同じなら上書き）
    struct Command { float token = 0, gainDb = 0, pan = 0, width = 0; double untilMs = 0; };
    void command (float token, float gainDb, float pan, float width);

    // 画面用: 見つかった Strip
    struct Track { float token = 0, gainDb = 0, pan = 0, width = 0, levelDb = -100, low = -100, mid = -100, high = -100; bool mute = false; juce::String name; int slot = -1; };
    std::vector<Track> tracks() const;

private:
    void timerCallback() override;
    juce::CriticalSection lock;
    std::vector<Command> cmds;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeMixerProcessor)
};
