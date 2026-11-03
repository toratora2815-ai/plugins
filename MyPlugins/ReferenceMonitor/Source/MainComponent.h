#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Capture.h"
#include "MusicControl.h"
#include "Library.h"
#include "LogicLink.h"

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
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
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
    juce::String outputUID() const;
    void refreshOutputs();
    void refreshLiveApps();
    void saveSettings();
    void loadSettings();
    void noFocus (juce::Component&);
    void setListenAs (int idx);

    std::vector<float> toBands (const std::vector<double>& power, double sampleRate) const;
    void updateSpectra();
    void drawTimeline (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);

    // ---- 中身 ----
    Engine engine;
    CaptureEngine capture { engine };
    MusicControl music;
    LogicLink logic;
    MusicControl::State musicState;
    Library library;

    // ---- 画面部品 ----
    juce::ListBox refList { "refs", this };
    juce::TextButton addButton { "+ ADD" }, removeButton { "REMOVE" };
    juce::ComboBox liveAppBox, refBox;   // refBox = 参照曲の選択（0 番目がライブ）
    Seg abSeg, levelSeg, bandSeg, stereoSeg, outSeg, listenSeg;
    juce::TextButton playButton { "PLAY" }, loopButton { "LOOP" }, restartButton { "CUE ON SWITCH" }, addCueButton { "+ CUE" }, setMixButton { "SET MIX" };
    juce::OwnedArray<CueButton> cueButtons;
    juce::String cueSignature;
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
    std::vector<double> snapBuf;
    juce::Rectangle<int> cueRow, headerArea, statusArea, refLabel, liveLabel, hintArea, timelineArea, specArea, meterArea, labelsRow1, labelsRow2;
    juce::Rectangle<int> levelLabel, bandLabel, stereoLabel, trimLabel, outLabel, volLabel, listenLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
