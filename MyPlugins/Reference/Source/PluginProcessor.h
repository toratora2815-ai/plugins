#pragma once
#include <JuceHeader.h>
#include "LoudnessMeter.h"
#include "Spectrum.h"
#include "FilePlayer.h"
#include "LiveInput.h"

class ReferenceProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ReferenceProcessor();
    ~ReferenceProcessor() override;

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
    // 停止中も処理を止めないでほしいので「余韻が無限」と伝える
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI から（メッセージスレッド）----
    void loadFile (const juce::File& f);
    void setLiveDevice (const juce::String& name);
    void retryLive() { liveFailed = false; }
    void resetMeters() { mixMeter.requestReset(); refMeter.requestReset(); }
    juce::StringArray getLiveDeviceNames() { return live.getDeviceNames(); }

    juce::String getFileStatus() const { return fileStatus; }
    juce::String getLiveDeviceName() const { return liveDeviceName; }
    juce::String getLiveError() const { return liveError; }
    bool isLoading() const { return loading.load(); }

    juce::AudioProcessorValueTreeState apvts;
    refdsp::LoudnessMeter mixMeter, refMeter;
    SpectrumAnalyser mixSpec, refSpec;
    FilePlayer player;
    LiveInput live;

    std::atomic<float> mixGainDb { 0.0f }, refGainDb { 0.0f };   // いま掛けているゲイン
    std::atomic<bool> hostPlaying { false };

    enum Source { fileSource = 0, liveSource = 1 };
    enum Level  { levelOff = 0, levelMatch = 1, levelStream = 2 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (float* L, float* R, int n, bool mono);
    void timerCallback() override;

    std::atomic<float>* pAb = nullptr;
    std::atomic<float>* pSource = nullptr;
    std::atomic<float>* pLevel = nullptr;
    std::atomic<float>* pBand = nullptr;
    std::atomic<float>* pStereo = nullptr;
    std::atomic<float>* pTrim = nullptr;
    std::atomic<float>* pLoop = nullptr;
    std::atomic<float>* pFollow = nullptr;

    static constexpr int kChunk = 1024;
    juce::AudioBuffer<float> refBuf { 2, kChunk };
    double sr = 48000.0;
    int blockSize = 512;

    juce::LinearSmoothedValue<float> abSmooth, wSt, wMono, wSide, wFull, wLow, wMid, wHigh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> mixGain { 1.0f }, refGain { 1.0f };
    juce::dsp::LinkwitzRileyFilter<float> xoLow, xoHigh;
    float heldMatchDb = 0.0f, heldMixStreamDb = 0.0f, heldRefStreamDb = 0.0f;
    bool refWasSilent = false;
    int lastSource = -1;

    // ファイル読み込み・ライブ入力の管理（メッセージスレッド）
    juce::ThreadPool loaderPool { 1 };
    juce::CriticalSection pendingLock;
    std::unique_ptr<RefAudio> pendingAudio;
    juce::String pendingError;
    std::atomic<bool> loading { false }, stateRestored { false };
    juce::String filePath, fileStatus, liveDeviceName, liveError;
    bool liveFailed = false;
    int autoScanCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceProcessor)
};
