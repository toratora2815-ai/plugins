#pragma once
#include <JuceHeader.h>
#include "Card.h"

// 左: カードの一覧 / 右: 選んだカード（写真・アプリ・書いていた所・ページ・メモ）
// 裏では、使っている間ときどき今の状態を取り、離れたらカードにして、戻ってきたら見せる
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
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (bool focus)> onShowRequest;
    std::function<void()> onHideRequest;

    void showLatest (bool focus);
    void pauseNow();   // 自分で中断（メモつき）

    // 確認用
    void loadDemo();

private:
    bool live = true;
    std::unique_ptr<juce::PropertiesFile> props;
    juce::Array<Card> cards;
    int selected = -1;
    bool welcome = false;          // 戻ってきたときに出したカードか（見出しが「おかえりなさい」になる）
    double awayMinutes = 0;

    // 状態
    bool away = false;
    juce::String awayWhy;
    juce::Time awayStart;
    bool capturing = false;
    rc::Capture rolling;           // 使っている間に取った、いちばん新しい状態
    juce::Time rollingAt;
    bool rollingShot = false;

    juce::Image shot;
    juce::String shotId;

    juce::TextButton reopenBtn, frontBtn, closeBtn, settingsBtn, permBtn, deleteBtn;
    juce::TextEditor noteBox;

    // クリックできる所（書類・ページ・アプリ）
    struct Link { juce::Rectangle<int> r; int kind; int index; };   // kind 0 = 前のアプリ, 1 = ページ, 2 = ほかのアプリ, 3 = Finder, 4 = 写真
    std::vector<Link> links;
    int hoverLink = -1;

    juce::Rectangle<int> listArea, cardArea, banner;

    void timerCallback() override;
    void reload (const juce::String& selectId = {});
    void select (int i);
    Card* current() { return juce::isPositiveAndBelow (selected, cards.size()) ? &cards.getReference (selected) : nullptr; }

    juce::File rollingShotFile() const { return Card::root().getChildFile ("rolling.jpg"); }
    std::vector<std::string> skipList() const;
    void takeRolling();
    void goAway (const juce::String& why);
    void comeBack();
    void makeCard (const rc::Capture& c, bool shotOk, const juce::String& why, juce::Time when, bool showNow, bool focusNote);
    void openLink (const Link& l);
    void showSettings();
    void updateMenu();
    void registerKeys();

    int idleMinutes() const    { return props->getIntValue ("idleMin", 5); }
    int showAfterMinutes() const { return props->getIntValue ("showMin", 3); }
    bool autoOn() const        { return props->getBoolValue ("auto", true); }
    bool shotsOn() const       { return props->getBoolValue ("shots", true); }
    bool textOn() const        { return props->getBoolValue ("text", true); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
