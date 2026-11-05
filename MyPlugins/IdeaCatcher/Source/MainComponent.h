#pragma once
#include <JuceHeader.h>
#include "IdeaAnalysis.h"

// 画面の上のほうに出る小さな表示（録音中・解析中・できたとき）。マウスは通す
class Hud : public juce::Component
{
public:
    juce::String big, small;
    juce::Colour accent { 0xffff4d5e };
    float meter = -1;   // < 0 = 出さない
    void paint (juce::Graphics&) override;
};

// 録音 → 解析 → Logic 用の MIDI を作る、の 1 画面
class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void toggleRecord();
    void openLastInLogic();
    void showMe (bool focus);
    void command (const juce::String& c);

    // 確認用
    bool loadForSnapshot (const juce::File& wav);
    juce::File midiFile() const { return cur.mid; }

    std::function<void (bool floating, bool focus)> onShowRequest;
    std::function<bool()> isWindowVisible;

    static juce::File ideasRoot();

private:
    enum State { Idle, Starting, Recording, Analysing, Done };
    enum Mode { Normal, Overdub, Catch };
    State state = Idle;
    bool live = true;

    std::unique_ptr<juce::PropertiesFile> props;
    juce::AudioDeviceManager devices;
    bool devicesInited = false;

    // 音の処理のスレッドが書くもの
    std::vector<float> ring;                 // さかのぼり用（約 32 秒、いつも書き続ける）
    std::atomic<int64_t> ringW { 0 };        // これまでに入ってきたサンプル数（絶対位置）
    std::vector<float> rec;                  // 録音（最長 5 分）
    std::atomic<int64_t> recN { 0 }, lastLoud { -1 }, copyPos { 0 };
    std::atomic<float> level { 0 };
    std::atomic<bool> recording { false };
    // クリック（カウントイン）と、重ね録りの再生
    std::atomic<int64_t> clickStart { -1 };
    std::atomic<double> clickPeriod { 24000 };
    std::atomic<int> clickBeats { 4 };       // < 0 = 鳴らし続ける
    std::vector<float> play;
    std::atomic<int64_t> playStart { -1 };
    std::atomic<bool> playing { false };

    double recFs = 48000;
    float meter = 0;

    // 録音 1 回ぶんの情報
    struct Session
    {
        Mode mode = Normal;
        bool countIn = false;
        double bpm = 100;
        int64_t from0 = 0;        // rec[0] の絶対位置
        int64_t gridAbs = -1;     // 1 小節目の頭（聞こえた位置の絶対位置）
        int latency = 0;          // 出力＋入力の遅れ（サンプル）
        std::vector<double> taps; // 秒（rec の頭から）
    } ses;

    struct Idea
    {
        bool valid = false;
        juce::File folder, mid;
        std::vector<idea::Layer> layers;
        juce::Array<juce::File> wavs;
        std::vector<int> snapKey, snapMinor;   // ノートを合わせたキー（読み直しで同じ結果にする）
        idea::MidiOptions o;
    } cur;

    juce::String message;   // 下に出す一言
    juce::Array<juce::File> recent;
    int hoverRecent = -1;
    bool dragStarted = false;

    std::unique_ptr<Hud> hud;
    double hudUntil = 0;

    juce::TextButton kindBtn, keyDownBtn, keyUpBtn, modeBtn, bpmMinus, bpmPlus, bpmHalf, bpmDouble, quantBtn,
                     logicBtn, dubBtn, finderBtn, hotkeyBtn, inputBtn, countBtn, moreBtn, tapBtn;
    juce::ToggleButton autoOpenBtn, autoStopBtn, loginBtn, prerollBtn;

    juce::Rectangle<int> header, recArea, recCircle, card, roll, actions, dragMidi, dragWav, recentArea, footer;

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void timerCallback() override;

    void startRecording (Mode m);
    bool ensureDevice();
    void beginCapture (Mode m);
    int64_t findPreStart (int64_t S) const;
    void stopRecording();
    void finishAnalysis (idea::Result r, std::vector<float> samples, double fs, Mode m);
    void loadIdea (const juce::File& folder);
    void applyIdea (Idea next);
    void tap();
    void removeLastLayer();
    void buildPlayback();
    void setPreroll (bool on);

    void writeMidi();
    void saveInfo();
    juce::File writeLogicVoice();
    void refreshRecent();
    void refreshButtons();
    void refreshMenu();
    void setState (State s);
    void editOptions (std::function<void (idea::MidiOptions&)> f);
    void registerHotKey();
    void showInputSettings();
    void showCountMenu();
    void showMoreMenu();
    void showDubMenu();

    void updateHud();
    void flashHud (const juce::String& big, const juce::String& small, juce::Colour c, double seconds = 3.5);
    void report (const juce::String& text, bool important);   // 画面の一言＋（画面が出ていなければ）HUD

    bool hasMelodyLayer() const;
    double countBpm() const;
    juce::String keyLabel() const;
    juce::String hotKeyLabel() const;
    static juce::String shortKey (int key, bool minor);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
