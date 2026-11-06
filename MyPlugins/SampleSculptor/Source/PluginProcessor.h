#pragma once
#include <JuceHeader.h>
#include "Render.h"

// 読み込んだ音・作り直した音
struct SampleAudio
{
    ss::Channels ch;
    double fs = 48000.0;
    size_t length() const { return ch.empty() ? 0 : ch[0].size(); }
};

class SampleSculptorProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::Thread
{
public:
    SampleSculptorProcessor();
    ~SampleSculptorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& paramIds();

    // 画面から
    void loadFile (const juce::File&);            // 裏で読む
    void play()  { playRequest = 1; }
    void stop()  { playRequest = 2; }
    juce::File exportWav();                       // 今の音を WAV に（~/Music/Sample Sculptor）。失敗なら空
    std::shared_ptr<const SampleAudio> getSource() const   { const juce::SpinLock::ScopedLockType l (lock); return source; }
    std::shared_ptr<const SampleAudio> getRendered() const { const juce::SpinLock::ScopedLockType l (lock); return rendered; }
    juce::File sourceFile() const { return juce::File (apvts.state.getProperty ("file", "").toString()); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> version { 0 };               // 作り直すたびに増える（画面が描き直す）
    std::atomic<bool> busy { false }, editorOpen { false };
    std::atomic<float> playPos { -1.0f };         // 0..1、止まっているとき -1
    juce::String message;                         // 読めなかったとき等（メッセージスレッドだけ）
    void pollMessages();                          // 裏で読み終えた結果を state へ（メッセージスレッドから呼ぶ）

private:
    void parameterChanged (const juce::String&, float) override;
    void run() override;                          // 読み込みと作り直し（裏のスレッド）
    ss::Settings readSettings() const;
    static juce::File outputFolder();

    mutable juce::SpinLock lock;
    std::shared_ptr<const SampleAudio> source, rendered;

    std::atomic<bool> dirty { false }, playAfter { false };
    std::atomic<juce::int64> lastChangeMs { 0 };
    juce::CriticalSection loadLock;
    juce::File pendingLoad;
    juce::String loadedPath, loadError;

    // 試し聴き（1 声）
    double hostFs = 48000.0, pos = 0.0, rate = 1.0;
    float voiceGain = 1.0f, declick = 0.0f;
    bool playing = false;
    std::atomic<int> playRequest { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSculptorProcessor)
};
