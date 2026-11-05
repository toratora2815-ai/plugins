#pragma once
#include <JuceHeader.h>
#include "TaskParser.h"

// ひとりごとを聞いて、「やること」だけをリマインダー（時刻のある打ち合わせなどはカレンダー）に入れる
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit MainComponent (bool live = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    std::function<void()> onShowRequest;

    // 聞こえた文を処理する（確認用にも使う）
    void heard (const juce::String& text, bool forced);
    void addDemo();

    struct Entry
    {
        juce::Time when;
        juce::String heardText, title, dueText;
        bool event = false;
        juce::String id;           // リマインダー / 予定の識別子
        int status = 0;            // 0 = 入れている途中, 1 = 入れた, 2 = 取り消した, 3 = 失敗
        juce::String error;
        aside::Due due;
    };

private:
    bool live = true;
    std::unique_ptr<juce::PropertiesFile> props;
    juce::Array<Entry> entries;
    juce::String partial;          // いま聞こえている途中の文
    juce::String state = "off";
    float level = 0;
    bool forceNext = false;        // 次の一言は、きっかけの言葉がなくてもメモ
    double forceUntil = 0;
    bool pausedForApp = false;
    int hoverUndo = -1;

    juce::TextButton onBtn, settingsBtn, remindersBtn;
    juce::Rectangle<int> header, live_, listArea;
    std::vector<std::pair<juce::Rectangle<int>, int>> undoAreas;

    class Hud;
    std::unique_ptr<Hud> hud;

    void timerCallback() override;
    void setListening (bool on);
    void startEngine();
    void updateMenu();
    void updateIcon();
    void registerKey();
    void memoNext();
    void undo (int index);
    void showSettings();
    void save();
    void load();
    juce::String stateText() const;

    bool wantOn() const          { return props->getBoolValue ("on", true); }
    bool pauseInLogic() const    { return props->getBoolValue ("pauseLogic", true); }
    bool everything() const      { return props->getBoolValue ("everything", false); }
    juce::String listName() const { return props->getValue ("list", juce::String::fromUTF8 ("ひとりごと")); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
