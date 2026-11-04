#pragma once
#include <JuceHeader.h>
#include "RoomEngine.h"
#include "ProfileTable.h"

class LumenRoomsProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LumenRoomsProcessor();
    ~LumenRoomsProcessor() override;

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
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // 画面に出す反射の形（12 個の粒の時刻と強さ）
    struct ErView { float timeMs[12]; float db[12]; float lenMs[12]; };
    ErView getErView() const;
    float getOutputLevel (int ch) const { return outLevel[ch].load(); }

    static const juce::StringArray& patternNames();
    static const juce::StringArray& proximityNames();
    static const juce::StringArray& slopeNames();
    static const juce::StringArray& noteNames();
    static const juce::StringArray& colorNames();
    const lumen::ProfileTable& getProfiles() const { return profiles; }

    // よその .crp_preset ファイル（JUCE の ValueTree 形式）を読み込む。失敗したら理由を返す
    juce::String importPreset (const juce::File&);
    juce::String getImportedName() const { return importedName; }
    static juce::File defaultPresetFolder();
    static juce::File userPresetFolder();

    // 自分のプリセット（Lumen Rooms 形式 .lrpreset）
    static juce::File myPresetFolder();
    juce::String saveMyPreset (const juce::String& name);
    juce::String loadMyPreset (const juce::File&);

    // 画面の「LIVE」表示用：入力（ドライ）と響き（ウェット）をモノラルで受け渡す
    struct LiveTap
    {
        static constexpr int size = 1 << 16;
        juce::AbstractFifo fifo { size };
        std::vector<float> dry = std::vector<float> ((size_t) size), wet = std::vector<float> ((size_t) size);
        void push (const float* d, const float* w, int n)
        {
            const auto scope = fifo.write (n);
            if (scope.blockSize1 > 0) { std::copy (d, d + scope.blockSize1, dry.begin() + scope.startIndex1); std::copy (w, w + scope.blockSize1, wet.begin() + scope.startIndex1); }
            if (scope.blockSize2 > 0) { std::copy (d + scope.blockSize1, d + n, dry.begin() + scope.startIndex2); std::copy (w + scope.blockSize1, w + n, wet.begin() + scope.startIndex2); }
        }
    } liveTap;
    std::atomic<bool> liveWanted { false };
    int mapMode = 0;   // 0 = SHAPE（設定の形）, 1 = LIVE（実際の音）
    double getSampleRateForView() const { return sr; }

    // 今のつまみの値（画面の表示でも使う）
    lumen::ReflParams readRefl() const;
    lumen::TailParams readTail() const;
    lumen::MixParams readMix() const;

private:
    void timerCallback() override;
    void rebuildReflections();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    lumen::RoomEngine engine;
    lumen::ProfileTable profiles;
    juce::dsp::Convolution convFromL, convFromR;
    juce::AudioBuffer<float> bufL, bufR;
    double sr = 48000.0;
    int maxBlock = 512;

    lumen::ReflParams lastRefl;
    bool haveRefl = false;
    std::atomic<bool> needRebuild { true };
    std::atomic<float> outLevel[2] { { 0.0f }, { 0.0f } };
    int currentProgram = 0;
    juce::String importedName;
    std::atomic<double> hostBpm { 120.0 };
    std::vector<float> liveDryBuf, liveWetBuf;
    void setParam (const char* id, float value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRoomsProcessor)
};
