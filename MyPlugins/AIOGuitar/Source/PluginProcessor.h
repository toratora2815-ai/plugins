#pragma once
#include <JuceHeader.h>
#include "Articulations.h"
#include "FXChain.h"
#include "Pedals.h"
#include "KitVoice.h"
#include "Score.h"
#include "PluginLinkJuce.h"
#include "PresetBar.h"

// Plugin Link の種類番号。PluginLink.h の Kind にはまだ足していない
// （Shared を変えると全プラグインのビルドになるので、次に Shared を触るときに kindAIOGuitar = 17 として足す）
constexpr uint32_t kAIOGuitarLinkKind = 17;

class AIOGuitarProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    AIOGuitarProcessor();
    ~AIOGuitarProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AIO Guitar"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- MY GUITAR recordings (message thread) ----
    // Cut a recording into notes/strokes in the background and put them into a slot.
    void loadRecording (int slot, const juce::File& file, std::function<void (juce::String)> onDone);
    juce::String loadRecordingNow (int slot, const juce::File& file);   // synchronous (tests)
    void clearSlot (int slot);
    bool saveKit (const juce::File& file);
    juce::String loadKit (const juce::File& file);
    void setKitName (const juce::String& name);
    std::shared_ptr<const Kit::GuitarKit> getKit() const { return editKit; }
    juce::String getSlotMessage (int slot) const { return slotMessage[slot]; }
    int getKitVersion() const { return kitVersion; }

    float getParamValue (const char* id) const { return get (id); }

    // articulations: currently latched one (for the UI) and click-to-select from the UI
    int  getCurrentArticulation() const { return voiceParams.lastArticulation.load(); }
    void selectArticulation (int a)     { voiceParams.previewArticulation.store (a); }

    // audition a note from the UI (string 1..6, 0 = auto)
    void previewNote (int note, int string, bool on);

    // ---- SCORE (message thread)
    Score::Model& getScore() { return score; }
    void scoreEdited();                 // the UI changed the score: rebuild what the audio thread applies
    int  getScoreVersion() const { return scoreVersion; }
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void startPreview (double fromPpq) { previewFrom.store (fromPpq); previewRequest.store (1); }
    void stopPreview()                 { previewRequest.store (2); }
    std::atomic<double> uiPpq { 0.0 };          // where playback is (host or preview)
    std::atomic<bool>   uiHostPlaying { false }, uiPreview { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool>   fxSleeping { false };   // UI: effects and amp are resting (nothing playing)
    std::atomic<int>    hostNum { 4 }, hostDen { 4 };

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    myp::Presets myPresets { *this, apvts, "AIOGuitar" };
    plink::Link link { (plink::Kind) kAIOGuitarLinkKind, apvts };   // Plugin Link
    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

private:
    // no allocation: linear search with juce::String == const char* (safe on the audio thread)
    std::atomic<float>* raw (const char* id) const
    {
        for (auto& p : params) if (p.first == id) return p.second;
        jassertfalse; return params.front().second;
    }
    float get (const char* id) const { return raw (id)->load(); }
    std::vector<std::pair<juce::String, std::atomic<float>*>> params;
    juce::MidiBuffer sortedMidi;   // reused: keyswitches first
    void readParams();
    void applySlice (int slot, Kit::SliceResult res);
    void publish (std::shared_ptr<Kit::GuitarKit> kit);
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    VoiceParams voiceParams;
    GuitarSynth synth { voiceParams };
    FXChain chain;
    PedalBoard pedals;
    int quietSamples = 0;
    float lastOutPeak = 1.0f;
    FXParams fx;

    // kit hand-over: message thread -> audio thread without locking the audio thread
    std::shared_ptr<Kit::GuitarKit> editKit;                    // message thread
    juce::SpinLock kitLock;
    std::shared_ptr<const Kit::GuitarKit> pendingKit, audioKit, retiredKit;
    bool kitChanged = false;
    bool kitSwapWaiting = false;   // audio thread: fading the old kit's notes out before the swap
    int  kitSwapWait = 0;
    int kitVersion = 0;
    juce::String slotMessage[Kit::numSlots];
    juce::ThreadPool loader { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::SmoothedValue<float> volume { 1.0f };

    // ---- SCORE
    void timerCallback() override;
    void publishTable();
    void renderScoreNotes (juce::MidiBuffer& midi, double startPpq, int n, double ppqPerSample);
    Score::Model score;
    Score::CaptureFifo captureFifo;
    juce::SpinLock tableLock;
    std::shared_ptr<const Score::ApplyTable> pendingTable, audioTable, retiredTable;
    bool tableChanged = false;
    int scoreVersion = 0;
    std::vector<std::map<Score::Key, Score::Override>> undoStack, redoStack;
    // audio thread
    std::atomic<int> previewRequest { 0 };      // 1 start, 2 stop
    std::atomic<double> previewFrom { 0.0 };
    bool previewRunning = false, capturing = false, syncWasPlaying = false;
    double lastBpm = 120.0;
    double previewPpq = 0.0, captureEnd = 0.0, expectedPpq = -1.0, hookPpq0 = 0.0, hookPpqPerSample = 0.0;
    juce::int64 hookClock0 = 0;
    bool hookCapture = false, hookApplyAll = false;
    juce::MidiBuffer scoreMidi;
    juce::int64 expectedSample = -1;
    bool captureLost = false;   // audio thread: an event did not fit in the FIFO during this pass
    void pushCapture (const Score::CaptureEvent& e) { if (! captureFifo.push (e)) captureLost = true; }
    int currentProgram = 0;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIOGuitarProcessor)
};
