#pragma once
#include <JuceHeader.h>
#include "VoiceEngine.h"
#include "SyncClock.h"
#include "Preview.h"
#include "Metronome.h"

class RemoteCommands;
class HistoryPanel;
class EditPanel;

// マイクの音 → VoiceEngine → 仮想 MIDI「Hum Keys」 → Logic
class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Num Deck などからの命令（"toggle" / "melody" / "drums" / "new" / "preview" / "mini" / "pin" など）
    void handleCommand (const juce::String& cmd);

    // 確認用（--snapshot）
    void showSnapshot (int which);
    void writeTakeForSnapshot (const juce::File& f) const { writeTakeMidi (f); }

    juce::String savedWindowState() const;
    void saveWindowState (const juce::String&);
    void applySavedViewState();   // ウィンドウができたあとに MINI / PIN を戻す

    // 履歴の画面から使う
    static juce::File takeFolder();
    Preview preview;
    void closeHistory();

    // ---- テイク（手直しの画面からも使う） ----
    struct TNote { double s, e; int n, ch, vel; double bs, be; bool drum; };
    // 書き出す形（拍。ファイルの頭 = 0、クオンタイズ済み）。src = takeNotes の番号
    struct ENote { double bs, be; int n, ch, vel; bool drum; size_t src; double shift; };
    // start = ファイルの頭が曲の何拍目か、t0 = 拍が分からないときの基準（秒）
    std::vector<ENote> exportNotes (double* start = nullptr, double* t0 = nullptr) const;
    double takeBpm() const;
    double barQuarters() const;
    std::vector<TNote> takeNotes;
    void beginEdit();               // 手直しの前に呼ぶ（元に戻す用に覚える）
    void editDone();                // 手直しのあとに呼ぶ（保存し直す）
    bool undoEdit();
    void revertEdits();
    bool takeIsEdited() const { return takeLocked; }
    void previewTake();
    void closeEditor();
    int drumNoteAt (int cls) const { return engine.p.drumNote[juce::jlimit (0, 5, cls)].load(); }
    int drumClassOf (int note) const;

private:
    // ---- 音 ----
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void timerCallback() override;

    struct Sink : hum::MidiSink
    {
        std::unique_ptr<juce::MidiOutput> port;
        void send (uint8_t s, uint8_t a, uint8_t b) override
        {
            if (port != nullptr) port->sendMessageNow (juce::MidiMessage ((int) s, (int) a, (int) b));
        }
    };

    hum::Engine engine;
    Sink sink;
    juce::AudioDeviceManager devices;
    std::vector<float> mono;
    std::atomic<float> inPeak { 0 };
    int inputLatency = 0;

    // ---- Logic との同期（MIDI クロック） ----
    SyncClock sync;
    std::unique_ptr<juce::MidiInput> clockIn;
    std::atomic<double> mapHostMs { 0 };     // この時刻（ms）に
    std::atomic<int64_t> mapClock { 0 };     // このサンプル番号の音がマイクに入った
    int lastStops = 0;
    double hostMsOf (int64_t sample) const;

    std::unique_ptr<RemoteCommands> remote;

    // ---- 設定 ----
    std::unique_ptr<juce::PropertiesFile> props;
    void loadSettings();
    void saveSettings();
    void openAudioSettings();
    void startAudio();

    // ---- 画面 ----
    struct HistFrame { float pitch, level; int note; int hit = -1; };
    std::vector<HistFrame> hist;   // 輪（最新 5 秒）
    int histPos = 0;
    float viewCenter = 60, meterDb = -100, peakHoldDb = -100;
    std::array<float, 6> padFlash {};
    std::array<int, 6> padVel {};
    int lastHitCls = -1;

    juce::Rectangle<int> displayArea, meterArea, rollArea, bigArea;
    std::array<juce::Rectangle<int>, 6> padAreas;

    juce::TextButton onButton { "ON" }, melodyTab { "MELODY" }, drumsTab { "DRUMS" }, autoTab { "AUTO" },
                     audioButton, panicButton { "PANIC" }, miniButton { "MINI" }, pinButton { "PIN" },
                     autoGateButton, autoKeyButton { "AUTO" };
    juce::Label statusLabel;

    // MELODY
    juce::ComboBox keyBox, scaleBox, octaveBox, chanBox;
    juce::Slider glueSlider, holdSlider;
    juce::TextButton velButton, bendButton, exprButton;
    // DRUMS
    juce::Slider sensSlider;
    std::array<juce::ComboBox, 6> drumNoteBox;
    std::array<juce::TextButton, 6> learnButton, resetButton;

    struct Caption { juce::Component* c; juce::String text; };
    std::vector<Caption> captions;
    void caption (juce::Component& c, const juce::String& t) { captions.push_back ({ &c, t }); }

    // ---- 自動（GATE / キーとオクターブ） ----
    double autoGateUntil = 0;
    std::vector<float> autoGateLevels;
    std::vector<float> voiced;      // 最近 20 秒ぶんの歌った高さ（輪）
    size_t voicedPos = 0;
    bool autoKeyListening = false;
    static constexpr size_t voicedNeed = 2200;   // 声のある所 約 6 秒
    void startAutoGate();
    void finishAutoGate();
    void autoKey();
    void applyAutoKey();
    juce::String flashMsg;
    double flashUntil = 0;
    void flash (const juce::String& s, double secs = 6.0);

    // ---- MINI / PIN ----
    bool mini = false, pinned = false;
    juce::Rectangle<int> fullBounds;
    void setMini (bool m);
    void setPinned (bool p);

    // ---- 録り逃し防止（テイク） ----
    std::vector<hum::CapEvent> take;
    std::vector<double> takeBeats;   // 各イベントが曲の何拍目だったか（拍が分からなければ NaN）
    enum { srcNone = 0, srcLogic = 1, srcClick = 2 };
    int takeSource = srcNone;        // 拍をどこから取ったか（Logic の MIDI クロック / このアプリのクリック）
    bool takeSaved = false, takeLocked = false;   // takeLocked = 手直し済み（もう録った音から作り直さない）
    std::vector<std::vector<TNote>> undoStack;
    int64_t takeLastT = 0;
    double takeRate = 48000;
    juce::File takeFile;
    juce::Rectangle<int> takeArea, dragArea, playArea;
    bool dragStarted = false;
    juce::Slider tempoSlider;
    juce::TextButton folderButton, newTakeButton, historyButton, editButton;
    void pullCapture();
    void finishTake();
    void newTake();
    void rebuildTakeNotes();
    bool takeHasBeats() const;      // 拍が分かっている（Logic かクリック）
    bool takeSynced() const;        // Logic の小節が分かっている
    int takeBar() const;            // 同期しているとき、テイクが始まる小節（1 から）
    juce::File writeTakeMidi (const juce::File& dest) const;
    void startDragOut();
    void resaveIfNeeded();

    // ---- 拍子・クリック・カウントイン・クオンタイズ ----
    Metronome metro;
    juce::ComboBox sigBox, quantBox, strengthBox;
    juce::TextButton clickButton { "CLICK" }, countButton;
    bool clickOn = false;
    int sigNum = 4, sigDen = 4;
    int quantize = 0;               // 0 = OFF, 1 = 1/4, 2 = 1/8, 3 = 1/16, 4 = 1/8T, 5 = 1/16T
    float quantStrength = 1.0f;
    double quantUnit() const;
    void setClick (bool on);
    void countIn();
    void applySignature();

    std::unique_ptr<EditPanel> editor;
    void openEditor();

    std::unique_ptr<HistoryPanel> history;
    void openHistory();

    void refreshButtons();
    void setMode (int m);
    juce::String midiStatus;
    bool snapshotMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
