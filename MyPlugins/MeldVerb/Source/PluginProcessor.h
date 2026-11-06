#pragma once
#include <JuceHeader.h>
#include "MeldEngine.h"
#include "Assist.h"

class MeldVerbProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MeldVerbProcessor();
    ~MeldVerbProcessor() override;

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

    // プリセット
    static juce::StringArray presetNames();
    void applyPreset (int index);

    // つまみの値 → エンジンの設定（画面の表示でも使う）
    meld::Params readParams() const;
    static const juce::StringArray& noteNames();        // 残響時間のテンポ同期
    static const juce::StringArray& preNoteNames();     // プリディレイのテンポ同期
    static double noteBeats (int i);
    static double preNoteBeats (int i);
    double getBpm() const { return hostBpm.load(); }

    // 画面表示用
    float getSmoothDb() const { return smoothDb.load(); }
    float getLevel (int i) const { return levels[i].load(); }   // 0 in L, 1 in R, 2 out L, 3 out R
    double getRate() const { return sr; }
    meld::Early::View earlyView() const;
    double eqMagDb (bool reverb, double f) const;

    // LEARN（PRE EQ の耳に痛い響きを探す）と UNMASK（元の音を響きから浮かせる）
    enum class LearnMode { none, pre, unmask };
    void startLearn (LearnMode m);
    void cancelLearn();
    LearnMode learning() const { return learnMode.load(); }
    float learnProgress() const { return learnProg.load(); }
    juce::String lastLearnMessage() const { const juce::ScopedLock l (msgLock); return learnMsg; }
    void applyUnmaskAmount();   // UNMASK の量のつまみを動かしたとき、覚えた形を量に合わせて入れ直す

    // 画面のスペクトル表示（1/6 オクターブ、dB）
    static constexpr int specBands = assist::Spectrum::numBands;
    void getSpectra (float* dryDb, float* wetDb) const;
    std::atomic<bool> editorOpen { false };

private:
    void timerCallback() override;
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    static const juce::StringArray& paramOrder();
    std::vector<std::atomic<float>*> ptrs;
    void setParam (const juce::String& id, float value);
    void finishLearn();

    meld::Engine engine;
    double sr = 48000.0;
    int maxBlock = 512;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<float> smoothDb { 0.0f };
    std::atomic<float> levels[4] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } };
    std::vector<float> dryBuf, wetBuf;

    // 音の処理 → タイマーへ受け渡す（ドライと響き、モノラル）
    static constexpr int fifoSize = 1 << 16;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoDry = std::vector<float> ((size_t) fifoSize), fifoWet = std::vector<float> ((size_t) fifoSize);

    assist::Spectrum liveDry, liveWet;       // 画面用（なめらかに動く）
    assist::Accumulator accDry, accWet;      // LEARN 用（ためていく）
    std::vector<float> chunkDry, chunkWet;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuf;
    mutable juce::CriticalSection specLock, msgLock;
    float shownDry[specBands] {}, shownWet[specBands] {};
    std::atomic<LearnMode> learnMode { LearnMode::none };
    std::atomic<float> learnProg { 0.0f };
    double learnSeconds = 0;
    juce::String learnMsg;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeldVerbProcessor)
};
