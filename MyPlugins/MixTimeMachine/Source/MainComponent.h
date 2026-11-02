#pragma once
#include <JuceHeader.h>
#include <set>
#include "Library.h"
#include "Player.h"

// ListBox の中身を関数で渡すための小さな入れ物
struct ListModel : public juce::ListBoxModel
{
    std::function<int()> rows;
    std::function<void (int, juce::Graphics&, int, int, bool)> paintRow;
    std::function<void (int, const juce::MouseEvent&)> clicked;
    int getNumRows() override { return rows ? rows() : 0; }
    void paintListBoxItem (int r, juce::Graphics& g, int w, int h, bool sel) override { if (paintRow) paintRow (r, g, w, h, sel); }
    void listBoxItemClicked (int r, const juce::MouseEvent& e) override { if (clicked) clicked (r, e); }
};

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
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
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;

    // ---- フォルダの見張り ----
    void scanFolders();
    void refreshBounceDirs();
    void handleFile (const juce::File&, bool baseline);
    Version& addVersion (const juce::File&, bool archive);
    void queueAnalysis (const Version&, bool archive);
    void pickUpResults();
    void foldersMenu();
    void addRoot (const juce::File&);
    void regroupSongs();

    // ---- 曲と版 ----
    void selectSong (const juce::String& song);
    void assign (int slot, const juce::String& versionId);
    void requestTrack (const juce::String& versionId);
    void updateGainsAndOffset();
    void versionMenu (Version&);
    void songMenu (const juce::String& song);
    void editNote (const juce::String& versionId);
    void moveToSong (const juce::String& versionId);
    void renameSong (const juce::String& song);
    void removeVersion (const juce::String& versionId, bool trashArchive);
    std::vector<Version*> currentVersions();
    Version* slotVersion (int slot);

    // ---- 再生 ----
    void setWhich (int w);
    void togglePlay();
    void updateLoop();
    double timeAtX (int x) const;
    int xAtTime (double t) const;

    // ---- ブラインドテスト ----
    void startBlind();
    void stopBlind();
    void blindPick (int xy);
    juce::String slotLabel (int slot);

    void flash (const juce::String&);
    void saveSettings();
    void refreshOutputs();
    void noFocus (juce::Component&);

    void drawTimeline (juce::Graphics&, juce::Rectangle<float>);
    void drawCompare (juce::Graphics&, juce::Rectangle<float>);

    // ---- 中身 ----
    Library library;
    Player player;
    juce::AudioDeviceManager deviceManager;
    juce::ThreadPool analysisPool { 1 }, loadPool { 1 };

    struct Result { juce::String id, archivePath, error; mtm::Analysis an; };
    juce::CriticalSection resultLock;
    std::vector<Result> results;
    std::vector<std::shared_ptr<const Track>> loadedTracks;
    struct OffsetResult { juce::String a, b; mtm::Offset off; };
    std::vector<OffsetResult> offsetResults;

    std::map<juce::String, std::shared_ptr<const Track>> cache;
    std::set<juce::String> loading;
    std::map<juce::String, mtm::Offset> offsets;   // "A|B" → ずれ
    std::set<juce::String> offsetsRunning;
    std::vector<std::shared_ptr<const Track>> trash;   // 再生から外したもの（メッセージスレッドで捨てる）

    struct Pending { juce::int64 time = 0, size = 0; };
    std::map<juce::String, Pending> pending;
    juce::Array<juce::File> bounceDirs;
    double nextDirScan = 0.0;
    int tick = 0, analysingCount = 0;

    // ---- 状態 ----
    juce::String selectedSong, selectedVersion;
    juce::String slotId[2];
    bool matchLevel = true, align = true, loopOn = false, keepCopies = true;
    double loopS = 0.0, loopE = 0.0;
    bool draggingLoop = false;
    juce::String flashMessage;
    double flashUntil = 0.0;

    int uiWhich = 0;                 // 画面上で選んでいる方（0 = A / X、1 = B / Y）
    bool blind = false;
    int blindRound = 0;
    bool xIsA = true;
    std::vector<int> blindChoices;   // 選んだ方（0 = A, 1 = B）
    static constexpr int kBlindRounds = 5;
    juce::Random rng;

    // ---- 画面部品 ----
    ListModel songModel, versionModel;
    juce::ListBox songList { "songs", &songModel }, versionList { "versions", &versionModel };
    juce::TextButton slotA, slotB;
    juce::TextButton playButton { "PLAY" }, loopButton { "LOOP" }, matchButton { "MATCH LEVEL" }, alignButton { "ALIGN" },
                     blindButton { "BLIND TEST" }, pickX { "I PREFER X" }, pickY { "I PREFER Y" },
                     foldersButton { "FOLDERS..." }, keepButton { "KEEP COPIES" }, noteButton { "NOTE..." }, keeperButton { "KEEPER" };
    juce::Slider volumeSlider;
    juce::ComboBox outputBox;
    juce::StringArray outputNames;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, timelineArea, compareArea, statusArea, songsLabel, versionsLabel, volLabel, outLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
