#pragma once
#include <JuceHeader.h>
#include "Library.h"

// 口マネを録る → 手元の音から似ているものを並べる → クリックで試聴、ドラッグで Logic へ
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // 確認用（--snapshot）: wav を口マネとして探す
    void searchWithFile (const juce::File& wav);
    void waitForLibrary();

    struct Hit { int index; float dist; };

private:
    bool live = true;
    std::unique_ptr<juce::PropertiesFile> props;
    Library library;
    std::shared_ptr<const Library::Entries> lib = std::make_shared<Library::Entries>();
    int libGen = -1;

    // 音
    juce::AudioDeviceManager devices;
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::CriticalSection audioLock;
    double deviceFs = 48000;
    int deviceBlock = 512;
    bool micOk = false;

    // 録音（音のスレッドが書く）
    std::vector<float> rec;
    std::atomic<int> recN { 0 };
    std::atomic<int> lastLoud { -1 };
    std::atomic<bool> recording { false };
    std::atomic<float> level { 0 };
    double recStartMs = 0;
    std::vector<float> query;   // 最後の口マネ（声の波形）
    double queryFs = 48000;
    std::vector<float> previewVoice;   // 自分の声を聴き直すとき
    std::atomic<int> voicePos { -1 };

    // 探した結果
    enum QueryKind { None, Voice, Example };
    QueryKind qkind = None;
    sketch::Features qf;
    juce::String exampleName;
    std::vector<Hit> hits;
    int kindFilter = 0;
    int playing = -1, hover = -1;

    juce::TextButton folderBtn, voiceBtn, deviceBtn;
    juce::OwnedArray<juce::TextButton> kindBtns;
    juce::TextEditor searchBox;
    juce::String message;

    class ResultList;
    std::unique_ptr<ResultList> list;
    juce::Viewport viewport;

    juce::Rectangle<int> header, recArea, recCircle, wave, filters, footer;

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void timerCallback() override;

    void openDevice (bool withInput);
    void toggleRecord();
    void startRecording();
    void stopRecording();
    void runQuery();
    void searchLike (int entryIndex);
    void play (int entryIndex);
    void stopPlayback();
    void showFolderMenu();
    void showRowMenu (int row);
    void startDrag (int row);
    juce::StringArray folderList() const;
    void setFolders (const juce::StringArray&);
    juce::String statusText() const;

    friend class ResultList;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
