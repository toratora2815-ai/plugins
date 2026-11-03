#pragma once
#include <JuceHeader.h>
#include "Deck.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshStatus();
    void applyOnTop();
    void showTileMenu (int index);
    void showSettingsMenu();
    void startLearning (int page, int index);
    void renameSlot (int page, int index);
    void chooseOtherApp (int page, int index);
    void registerChromePage (int page, int index);
    void enterUrl (int page, int index);
    void askText (const juce::String& title, const juce::String& message,
                  const std::vector<std::pair<juce::String, juce::String>>& fields,
                  std::function<void (const juce::StringArray&)> done);
    juce::Image appIcon (const juce::String& path);
    juce::Colour modeAccent() const;

    void paintHeader (juce::Graphics&);
    void paintTile (juce::Graphics&, int index);
    void paintFooter (juce::Graphics&);
    void paintBanner (juce::Graphics&);
    void paintLearning (juce::Graphics&);

    numdeck::KeyTap tap;
    numdeck::Deck deck;

    // 画面の状態
    bool permitted = false, tapRunning = false, logicRunning = false, logicFront = false;
    std::array<float, 9> flash {};
    int hoverTile = -1;
    int learnPage = -1, learnIndex = -1;
    float ignoredHint = 0.0f;
    int tick = 0;
    bool onTopApplied = false;

    // クリックできる場所
    juce::Rectangle<int> headerArea, statusPill, modeTabs[numdeck::Deck::kModes], pageTabs[numdeck::Deck::kPages], prevPage, nextPage,
                         gridArea, tiles[9], footerArea, logicOnlyPill, onTopPill, settingsPill,
                         bannerArea, bannerButton, learnCancel;

    std::unique_ptr<juce::AlertWindow> inputWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, juce::Image> iconCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
