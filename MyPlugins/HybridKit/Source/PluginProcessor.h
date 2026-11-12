#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "KitEngine.h"
#include "Resampler.h"

// Plugin Link の種類番号（PluginLink.h には未登録。Shared を変えると全部ビルドになるので、Vox Chop などと同じく番号だけ使う）
constexpr uint32_t kHybridKitLinkKind = 27;

// Hybrid Kit: Logic の Drum Kit Designer（Producer Kit）の録音を鳴らし、キック・スネアに EDM の層を重ねる AU 音源。
// GAIN MATCH は付けない（音源なので。CLAUDE.md の決まり）
class HybridKitProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    HybridKitProcessor();
    ~HybridKitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { resetRequested.store (true); }   // 次の processBlock で全部止める

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 内蔵プリセット
    static juce::StringArray presetNames();
    void applyPreset (int index);

    static juce::StringArray kitNames();
    static const char* groupId (int g);       // kick / snare / toms / hihat / cym
    static const char* groupName (int g);     // 画面の表示

    juce::AudioProcessorValueTreeState apvts;
    myp::Presets myPresets { *this, apvts, "HybridKit" };               // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kHybridKitLinkKind, apvts };         // Plugin Link（LINK 欄だけ）
    void updateTrackProperties (const TrackProperties& tp) override
    {
        if (tp.name.has_value()) { link.setTrackName (*tp.name); trackName = *tp.name; }
    }

    // ---- 画面から ----
    void audition (int note, int velocity);                 // 画面のキットを叩いた（メッセージスレッド）
    float hitFlash (int piece) const { return engine.hitFlash[(size_t) juce::jlimit (0, hk::NumPieces - 1, piece)].load(); }
    void clearFlash (int piece, float v) { engine.hitFlash[(size_t) juce::jlimit (0, hk::NumPieces - 1, piece)].store (v); }
    enum LoadState { stateIdle = 0, stateLoading, stateReady, stateError };
    int loadState() const { return loadStateValue.load(); }
    float loadProgress() const { return progress.load(); }
    juce::String statusText() const;
    bool piecePresent (int piece) const { return (presentMask.load() >> piece) & 1u; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    hk::Settings readSettings() const;
    void timerCallback() override;
    void requestKit (int index);
    void flushMidiLog();

    // ---- キットを読む裏のスレッド ----
    struct Loader : juce::Thread
    {
        HybridKitProcessor& owner;
        explicit Loader (HybridKitProcessor& o) : juce::Thread ("HybridKit loader"), owner (o) {}
        void run() override;
    };
    Loader loader { *this };
    std::atomic<int> requestedKit { -1 };      // メッセージスレッドが入れる
    int loaderKit = -1;                         // 裏のスレッドだけが触る（最後に出したキットの番号）
    hk::Kit* publishedKit = nullptr;            // 裏のスレッドだけが触る（最後に出したキット）
    std::atomic<bool> cancelLoad { false }, analysisDirty { false };
    std::atomic<int> loadingKit { -1 };          // 裏のスレッドがいま読んでいるキット（−1 = 読んでいない）
    std::atomic<uint32_t> loadSerial { 0 };      // 読み始めるたびに増える
    uint32_t waitedSerial = 0;                   // オーディオスレッドだけ（書き出しで待つのは 1 回の読み込みにつき 1 回）
    bool lastLoadOk = false;                     // 裏のスレッドだけ
    bool spliceDone = true;                      // 裏のスレッドだけ（出したキットの頭 → 尾のつなぎ方を最後まで決めたか）
    std::atomic<float> progress { 0.0f };
    std::atomic<int> loadStateValue { stateIdle };
    std::atomic<uint32_t> presentMask { 0x3ffu };
    juce::CriticalSection statusLock;
    juce::String statusMessage;
    std::mutex kitsLock;
    std::vector<std::unique_ptr<hk::Kit>> ownedKits;
    std::atomic<hk::Kit*> pendingKit { nullptr };        // 裏のスレッド → オーディオスレッド
    std::atomic<const hk::Kit*> retiredKit { nullptr };  // オーディオスレッド → メッセージスレッド（もう鳴っていない）
    std::atomic<uint32_t> retiredFence { 0 };            // そのときまでに先読みへ渡した数（先読みがそこまで触り終えたら消してよい）
    const hk::Kit* toRetire = nullptr;                   // オーディオスレッドだけ
    uint32_t toRetireFence = 0;
    std::vector<std::pair<const hk::Kit*, uint32_t>> pendingDeletes;   // メッセージスレッドだけ
    std::atomic<bool> resetRequested { false };
    void deleteKit (const hk::Kit* k);
    int lastKitParam = -1;
    std::array<int, 4> lastAnalysis { -1, -1, -1, -1 };

    // ---- 音 ----
    hk::Engine engine;
    hk::StreamResampler resampler;
    hk::Prefetcher prefetcher;
    static constexpr double internalRate = 44100.0;   // Drum Kit Designer の録音
    std::vector<float> tmpL, tmpR;

    struct Ev { int offset; uint8_t st, d1, d2; };
    std::array<Ev, 2048> events {};
    juce::AbstractFifo auditionFifo { 128 };
    std::array<std::pair<int, int>, 128> auditionBuf {};

    // ---- 届いた MIDI の記録（Drummer が送ってくるノートを確かめるため。最初の 20 分だけ） ----
    struct LogEv { double sec, ppq; uint8_t st, d1, d2; };
    juce::AbstractFifo logFifo { 4096 };
    std::array<LogEv, 4096> logBuf {};
    std::atomic<bool> logEnabled { true };
    int64_t samplesSeen = 0;
    double hostRate = 44100.0;
    juce::String trackName;
    std::unique_ptr<juce::FileOutputStream> logStream;
    int logLines = 0;
    juce::uint32 createdMs = juce::Time::getMillisecondCounter();

    std::atomic<float>* pKit = nullptr;
    std::atomic<float>* pClose = nullptr;
    std::atomic<float>* pOH = nullptr;
    std::atomic<float>* pRoom = nullptr;
    std::atomic<float>* pBleed = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::array<std::atomic<float>*, hk::NumGroups> pVol {}, pTune {}, pLen {};
    std::atomic<float>* pKickBlend = nullptr;
    std::atomic<float>* pKickSound = nullptr;
    std::atomic<float>* pKickNote = nullptr;
    std::atomic<float>* pSnareBlend = nullptr;
    std::atomic<float>* pSnareSound = nullptr;
    std::atomic<float>* pSnareEdmTune = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridKitProcessor)
};
