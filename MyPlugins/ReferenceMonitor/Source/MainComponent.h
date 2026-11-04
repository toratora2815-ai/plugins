#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Capture.h"
#include "MusicControl.h"
#include "Library.h"
#include "LogicLink.h"
#include "RemoteCommands.h"

// 横並びの切替ボタン
class Seg : public juce::Component
{
public:
    Seg (const juce::StringArray& labels, std::vector<juce::Colour> onColours, std::function<void (int)> cb);
    void setIndex (int idx);
    int getIndex() const { return index; }
    void resized() override;
    juce::OwnedArray<juce::TextButton> buttons;

private:
    std::function<void (int)> onSelect;
    int index = -1;
};

// 右クリックでメニューを出せるボタン（キュー用）
class CueButton : public juce::TextButton
{
public:
    std::function<void()> onRightClick, onDoubleClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && onDoubleClick) onDoubleClick();
    }
};

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ListBoxModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // ウィンドウの位置と大きさ（次に開いたとき、同じ場所に出す）
    juce::String savedWindowState() const { return library.settings.getWithDefault ("window", "").toString(); }
    void saveWindowState (const juce::String& st)
    {
        if (st == savedWindowState()) return;
        library.settings.set ("window", st);
        library.save();
    }

    // テスト用
    Engine& getEngine() { return engine; }
    void selectItem (int row);

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void timerCallback() override;
    void manageCapture();
    void pollMusic();
    void pickUpLoadedAudio();
    void loadFileAsync (const juce::String& path);
    void addFiles (const juce::StringArray& paths);
    void removeSelected();
    void refreshRefBox();

    bool isLive() const { return selectedRow == 0; }
    std::vector<Cue>* currentCues();
    double currentPosition() const;
    double currentLength() const;
    void setAB (bool ref);
    void jumpToCue (int idx, bool play);
    void seekTo (double seconds);
    void addCue();
    void cueMenu (int idx);
    void renameCue (int idx);
    void updateLoop();
    void explainLoop();
    void checkLogicLoop();
    double timeAtX (int x) const;
    int cueNearX (int x);
    void rebuildCueButtons();
    void togglePlay();
    void refPlay();
    void refPause();
    bool refIsPlaying() const;
    void setMixPosition (int idx, bool clear);
    void flash (const juce::String& msg);
    juce::String outputUID();
    bool outputFallback = false;
    double syncCheckAt = 0.0;     // PLAY (Logic) を押したあと、Logic が答えたかを見る時刻   // 選んだ出力先がつながっていないので、Mac の出力先で鳴らしている
    void refreshOutputs();
    void refreshLiveApps();
    void saveSettings();
    void loadSettings();
    void noFocus (juce::Component&);
    void setListenAs (int idx);
    void remoteCommand (const juce::String& command);   // Num Deck などからの操作

    std::vector<float> toBands (const std::vector<double>& power, double sampleRate) const;
    void updateSpectra();
    void drawTimeline (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    void drawBands (juce::Graphics&, juce::Rectangle<float>, bool punch);   // WIDTH / PUNCH 表示
    void setView (int mode);   // 0 TONE / 1 WIDTH / 2 PUNCH
    // MORE（LISTEN AS と配信サービスの音量）
    void showMoreMenu();
    void updateMoreButton();
    // AVG（何曲かのリファレンスの平均の音色と比べる）
    void avgMenu();
    void addCurrentToAvg();
    void rebuildAvg();
    void saveAvg();
    void loadAvg();
    std::vector<float> fileCurve (const RefAudio& a) const;
    std::vector<float> normalisedCurve (std::vector<float> v) const;
    float curveLevel (const std::vector<float>& v) const;
    void updateLiveShape();
    void takeSnapshot();
    void clearSnapshot();
    // 音色の差 a − b（音量の差は除く、となりと少しならす）
    std::vector<float> toneDiff (const std::vector<float>& a, const std::vector<float>& b) const;
    // 低・中・高の広がり（サイドの割合 0〜1）。測れないとき -1
    static std::array<float, 3> bandWidths (const std::vector<double>& mid, const std::vector<double>& side, double sampleRate);

    // ---- 中身 ----
    Engine engine;
    CaptureEngine capture { engine };
    MusicControl music;
    LogicLink logic;
    MusicControl::State musicState;
    Library library;

    std::unique_ptr<RemoteCommands> remote;

    // ---- 画面部品 ----
    juce::ListBox refList { "refs", this };
    juce::TextButton addButton { "+ ADD" }, removeButton { "REMOVE" };
    juce::ComboBox liveAppBox, refBox;   // refBox = 参照曲の選択（0 番目がライブ）
    Seg abSeg, levelSeg, bandSeg, stereoSeg, outSeg, listenSeg, viewSeg;
    juce::TextButton playButton { "PLAY" }, loopButton { "LOOP" }, restartButton { "CUE ON SWITCH" }, addCueButton { "+ CUE" }, setMixButton { "SET MIX" };
    juce::OwnedArray<CueButton> cueButtons;
    juce::String cueSignature;
    CueButton snapButton;   // SNAP（右クリックで消す）
    CueButton avgButton;    // AVG（右クリックでメニュー）
    juce::TextButton moreButton { "MORE" };
    juce::TextButton resetButton { "RESET METERS" }, dimButton { "DIM" }, muteButton { "MUTE" },
                     pinButton { "PIN" }, bypassButton { "BYPASS" };
    juce::Slider trimSlider, volumeSlider;
    juce::ComboBox outABox, outBBox;
    juce::StringArray outUIDs, liveBundles;
    std::unique_ptr<juce::FileChooser> chooser;

    // ---- 状態 ----
    int selectedRow = 0;          // 0 = ライブ、1〜 = ファイル
    int activeCue = -1;
    bool loopOn = false, restartOnSwitch = true, bypass = false;
    juce::String liveBundle { "com.apple.Music" };
    juce::String mixBundle { "com.apple.logic10" };
    juce::String outA, outB;
    juce::String captureError, statusMix, statusRef, statusHint;
    juce::Colour statusMixColour, statusRefColour;
    double retryAt = 0.0, mixSilentSince = 0.0;
    int tick = 0;
    juce::String loadingPath, flashMessage;
    double flashUntil = 0.0, logicLoopCooldown = 0.0;
    int draggingCue = -1;

    juce::ThreadPool loader { 1 };
    juce::CriticalSection pendingLock;
    std::unique_ptr<RefAudio> pendingAudio;
    juce::String pendingError;
    bool playAfterLoad = false;

    // 表示
    std::vector<float> centers, mixDb, refDb, diffDb;
    bool mixHas = false, refHas = false;
    float displayTop = -20.0f;
    std::vector<double> snapBuf, sideBuf;
    int viewMode = 0;                              // 0 TONE / 1 WIDTH / 2 PUNCH
    std::array<float, 3> mixPunchV { -1, -1, -1 }, refPunchV { -1, -1, -1 }, snapPunch { -1, -1, -1 };
    struct AvgCurve { juce::String name; std::vector<float> db; };
    std::vector<AvgCurve> avgCurves;
    std::vector<float> avgDb, mixLong, refLong;    // 平均の目標、長め（約 20 秒）にならした自分の曲・参照曲
    double mixLongCount = 0, refLongCount = 0;
    juce::String refLongKey;
    bool avgOn = false;
    std::map<juce::String, std::vector<float>> liveShapes;   // ミュージックの曲ごとの音量の形（再生した所だけ）
    std::vector<float> mixSlow;                    // ゆっくり平均したミックスの音色（SNAP で覚える元）
    std::array<float, 3> mixWidth { -1, -1, -1 }, refWidth { -1, -1, -1 }, snapWidth { -1, -1, -1 };
    std::vector<float> snapDb, snapDiffDb;         // SNAP したときのミックスの音色と、その REF との差
    bool snapHas = false;
    double snapTime = 0.0;
    juce::Rectangle<int> specHeader, legendArea;
    juce::Rectangle<int> cueRow, headerArea, statusArea, refLabel, liveLabel, hintArea, timelineArea, specArea, meterArea, labelsRow1, labelsRow2;
    juce::Rectangle<int> levelLabel, bandLabel, stereoLabel, trimLabel, outLabel, volLabel, listenLabel, moreLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
