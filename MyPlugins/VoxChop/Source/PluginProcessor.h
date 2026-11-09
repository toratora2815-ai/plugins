#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "VoxEngine.h"

// Plugin Link の種類番号。PluginLink.h の Kind にはまだ足していない
// （Shared を変えると全プラグインのビルドになるので、次に Shared を触るときに kindVoxChop = 18 として足す）
constexpr uint32_t kVoxChopLinkKind = 18;

// 読み込んだ声（モノラル）
struct VoxSource
{
    vox::Buf audio;
    double fs = 48000.0;
    vox::Analysis an;
    juce::String name;
    double startPpq = 0.0;     // 頭が曲のどの拍か（CAPTURE のとき。ファイルは 0）
    double bpm = 0.0;          // CAPTURE したときのテンポ。0 = Logic の今のテンポを使う
};

// 声を作り直した後の音
struct VoxVoice
{
    vox::Buf audio;
    vox::Analysis an;
    double fs = 48000.0;
};

// 切った 1 片と、その設定
struct VoxSlice
{
    long start = 0, end = 0;
    float pitch = 0.0f;       // 半音
    float gainDb = 0.0f;
    bool reverse = false, loop = false, oneShot = false, choke = false;
};
using VoxSlices = std::vector<VoxSlice>;

class VoxChopProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::Thread
{
public:
    VoxChopProcessor();
    ~VoxChopProcessor() override;

    static constexpr int maxSlices = 64;
    static constexpr int firstNote = 36;          // C1 から上へ並べる
    static constexpr double maxCaptureSeconds = 300.0;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // バイパス中: サイドチェーンの声を出さない。離した鍵盤がわからなくなるので、鳴っている声は消す
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.clear(); for (auto& v : voices) if (v.active) endVoice (v); }
    void reset() override { killAll = true; }   // 次のブロックの頭で全部の声を消す
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    myp::Presets presets { *this, apvts, "VoxChop" };                 // 自分のプリセット・取り消し
    plink::Link link { (plink::Kind) kVoxChopLinkKind, apvts };       // Plugin Link（LINK 欄だけ）
    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

    //== 画面から ==
    void loadFile (const juce::File&);
    void armCapture (bool on);                    // CAPTURE: 次に再生した所をサイドチェーンから取り込む
    bool captureArmed() const { return capArmed.load(); }
    bool capturing() const { return capRecording.load(); }
    double capturedSeconds() const { return capLen.load() / juce::jmax (1.0, hostFs); }
    bool sidechainConnected() const { return sideActive.load(); }
    void rechop();                                // 今の SENS・GRID で切り直す
    void setSlices (VoxSlices, bool manualEdit);  // 手で直したとき
    void preview (int sliceIndex);                // -1 = 全体（WHOLE）
    void stopPreview() { previewReq = -100; }
    void previewWhenReady() { previewAfterRender = true; }   // 声を計算し直したら、選んでいる片を鳴らす

    std::shared_ptr<const VoxSource> getSource() const { const juce::SpinLock::ScopedLockType l (lock); return source; }
    std::shared_ptr<const VoxVoice>  getVoice() const  { const juce::SpinLock::ScopedLockType l (lock); return voice; }
    std::shared_ptr<const VoxSlices> getSlices() const { const juce::SpinLock::ScopedLockType l (lock); return slices; }
    int selected() const { return selectedSlice.load(); }
    void select (int i) { selectedSlice = i; }

    // 書き出し（~/Music/Vox Chop）。失敗なら空
    juce::File exportSlice (int index);
    juce::Array<juce::File> exportAllSlices();
    juce::File exportWhole();
    juce::File exportMidi();

    // 声の目印（VOICE MAP）
    struct Landmark { const char* name; float hz; float body; };
    static const std::vector<Landmark>& landmarks();
    static float bodyOf (float f0);               // 声の高さから体の大きさを見積もる（男性 0、女性 +3）
    float sourceF0() const;                       // 読み込んだ声の高さの真ん中（なければ 0）

    // 内蔵の声プリセット（VOICE ページのタイル）
    struct VoicePreset { const char* name; const char* hint; int landmark; float shift, formant, breath, grit, robot, alien, lowCut, highCut; };
    static const std::vector<VoicePreset>& voicePresets();
    void applyVoicePreset (int index);

    std::atomic<int> version { 0 };               // 音や切れ目が変わるたびに増える（画面が描き直す）
    std::atomic<int> audioVersion { 0 };          // 声（元・作り直し）が変わったときだけ増える（波形の形を作り直す）
    std::atomic<bool> busy { false };
    std::atomic<double> playPos { -1.0 };         // いちばん新しく鳴らした所（元の音のサンプル）、止まっているとき -1
    std::atomic<int> playSlice { -1 };
    juce::String takeMessage();                   // 読めなかったとき等（画面が 1 回だけ出す）
    juce::File sourceFile() const { const juce::ScopedLock sl (loadLock); return curFile; }
    bool renderPending() const { return busy.load() || needRender.load(); }
    vox::VoiceSettings voiceSettings() const;

    static const juce::StringArray& voiceIds();   // 変えると全体を計算し直すもの
    static const juce::StringArray& chopIds();    // 変えると切り直すもの

private:
    void parameterChanged (const juce::String&, float) override;
    void run() override;
    struct LoadRequest
    {
        juce::File file;
        bool restore = false;                     // 保存から戻す（切れ目と拍の位置も戻す）
        VoxSlices slices;
        double ppq = 0.0, bpm = 0.0;
    };
    void startLoaded (std::shared_ptr<VoxSource>, const VoxSlices* keepSlices);
    void waitForWork();                           // オフライン書き出しのとき、裏の仕事が終わるのを待つ
    void doChop();
    void doRender();
    static juce::File outputFolder();
    juce::File uniqueFile (const juce::String& name, const juce::String& ext);
    vox::Buf renderSliceOffline (const VoxVoice&, const VoxSlice&, bool keep) const;
    double gridStepSeconds (const VoxSource&) const;

    mutable juce::SpinLock lock;
    std::shared_ptr<const VoxSource> source;
    std::shared_ptr<const VoxVoice> voice;
    std::shared_ptr<const VoxSlices> slices;
    std::atomic<int> selectedSlice { 0 };

    // 裏の仕事
    std::atomic<bool> needRender { false }, needChop { false }, restoring { false }, manualSlices { false };
    std::atomic<double> lastHostBpm { 120.0 };
    std::atomic<juce::int64> lastVoiceChange { 0 };
    std::atomic<bool> cancelWork { false };       // プラグインを外すとき、計算を途中でやめる
    mutable juce::CriticalSection loadLock;
    std::unique_ptr<LoadRequest> pendingLoad;
    std::unique_ptr<LoadRequest> unresolved;      // 保存から戻すファイルが見つからなかった（見つかるまで、保存のときはこれをそのまま書く）
    juce::File curFile;                           // 今の声のファイル（保存用）
    double curPpq = 0.0, curBpm = 0.0;
    juce::String pendingMessage;

    // CAPTURE（オーディオスレッドで書く）
    std::vector<float> capBuf;
    juce::CriticalSection capLock;                // capBuf を作る・捨てる（メッセージと裏のスレッド）
    std::atomic<bool> capArmed { false }, capRecording { false }, capDone { false }, capStopReq { false }, sideActive { false };
    int sideHold = 0;
    std::atomic<long> capLen { 0 };
    double capStartPpq = 0.0, capBpm = 120.0;

    // オーディオスレッドで手放した声・切れ目を、裏のスレッドで捨てる（オーディオで大きなメモリを解放しない）
    struct Graveyard
    {
        static constexpr int N = 64;
        std::shared_ptr<const void> slot[N];
        std::atomic<int> full[N] {};
        template <typename T> void bury (std::shared_ptr<T>& p)   // オーディオスレッドから
        {
            if (p == nullptr) return;
            for (int i = 0; i < N; ++i)
                if (full[i].load (std::memory_order_acquire) == 0)
                {
                    slot[i] = std::move (p);
                    full[i].store (1, std::memory_order_release);
                    return;
                }
            p.reset();
        }
        void clear()                                                // 裏のスレッドから
        {
            for (int i = 0; i < N; ++i)
                if (full[i].load (std::memory_order_acquire) == 1)
                {
                    slot[i].reset();
                    full[i].store (0, std::memory_order_release);
                }
        }
    } graveyard;
    std::shared_ptr<const VoxVoice> audioVoice;   // オーディオスレッドの手元（ロックが取れないときもこれで鳴らす）
    std::shared_ptr<const VoxSlices> audioSlices;

    // 鍵盤
    struct Voice
    {
        std::shared_ptr<const VoxVoice> buf;      // 鳴らし始めた声（入れ替わっても鳴り終わるまで持つ）
        bool active = false, released = false, fast = false;
        int note = -1, slice = -1;
        long from = 0, to = 0;
        bool reverse = false, loop = false, oneShot = false, choke = false;
        double ratio = 1.0;
        float gain = 1.0f, env = 0.0f;
        int stage = 0;                            // 0 アタック 1 ディケイ 2 サステイン 3 リリース
        juce::uint64 age = 0;
        vox::GrainPlayer gp;
    };
    static constexpr int numVoices = 20, maxAudible = 16;   // 16 声 + 奪われた声が消えるまでの 4 つ
    Voice voices[numVoices];
    juce::uint64 ageCounter = 0;
    std::vector<float> scratch;
    double hostFs = 48000.0;
    std::atomic<int> previewReq { -1000 };
    std::atomic<bool> killAll { false };
    std::atomic<bool> previewAfterRender { false };        // -1000 = なし、-100 = 止める、-1 = 全体、0.. = 片
    juce::SmoothedValue<float> levelSm;

    void startVoice (int note, float velocity, const VoxSlices&, int sliceIndex, double semitones, bool forceOneShot);
    void renderVoices (float* out, int n);
    void endVoice (Voice& v) { v.active = false; graveyard.bury (v.buf); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxChopProcessor)
};
