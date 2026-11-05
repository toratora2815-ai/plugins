#pragma once
#include <JuceHeader.h>
#include "PatternFinder.h"

// 操作を見て、同じ流れの 3 回目で知らせる → 「自動にする」で、キー 1 つでくり返せるようにする
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

    std::function<void()> onShowRequest, onHideRequest;
    void loadDemo();

    struct Flow
    {
        juce::String name;
        std::vector<third::Step> steps;
        int count = 0;
        int slot = 0;            // ⌃⌥ + この数字でくり返す（0 = なし）
        juce::Time when;
        std::string signature;
    };

private:
    bool live = true;
    std::unique_ptr<juce::PropertiesFile> props;
    std::vector<third::Step> steps;
    std::set<std::string> skip;        // もう知らせた（今日）＋ 出さない（ずっと）
    std::set<std::string> dismissed;   // 出さない（ずっと）
    std::vector<Flow> found, routines;
    double lastStepT = 0;
    bool evaluated = true;
    int todayCount = 0;
    int runIndex = -1, runStep = -1;
    juce::String runState, message;

    juce::TextButton watchBtn, settingsBtn, inputBtn, axBtn;
    juce::Rectangle<int> header, banner, statsArea, body;
    struct Hit { juce::Rectangle<int> r; int action; int index; };   // action: 1 = 自動にする, 2 = 出さない, 3 = 実行, 4 = 消す, 5 = キー
    std::vector<Hit> hits;
    int hover = -1;

    class Hud;
    std::unique_ptr<Hud> hud;

    void timerCallback() override;
    void onStep (const third::Step& s);
    void check (bool waitIfLonger);
    void report (const third::Found& f);
    void automate (int foundIndex);
    void dismiss (int foundIndex, bool forever);
    void runRoutine (int i);
    void registerKeys();
    void setWatching (bool on);
    void updateMenu();
    void updateIcon();
    void showSettings();
    void save();
    void load();
    std::vector<std::string> skipBundles() const;
    static juce::String nameFor (const std::vector<third::Step>&);

    bool wantWatch() const { return props->getBoolValue ("watch", true); }
    int minCount() const   { return props->getIntValue ("count", 3); }

    friend class Hud;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
