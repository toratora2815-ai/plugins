#pragma once
#include <JuceHeader.h>
#include "Deck.h"

// マクロを作る・直す画面（タイルのメニュー「マクロを作る / 直す…」で開く別ウィンドウ）
//   ・手順は 1 行ずつ。行をドラッグで並べ替え、右クリック（またはダブルクリック）で直す・消す
//   ・「キーを覚えさせる」を押している間は、押したショートカットが 1 行ずつ下に足されていく（Esc で終わり）
//   ・「試す」で、その場で 1 回実行してみる
class MacroEditor : public juce::Component, private juce::Timer
{
public:
    struct Host
    {
        // 覚えさせる。fn に (keyCode, mods) が届く。nullptr で終わり
        std::function<void (std::function<void (int, int)>)> learn;
        std::function<void (const numdeck::Slot&)> save;          // 保存（タイルに入れる）
        std::function<void (const std::vector<numdeck::Step>&)> test;
        std::function<void()> close;
        int mode = 0;   // 今のモード（一覧の操作の並びを合わせる）
    };

    MacroEditor (const numdeck::Slot& current, Host host);
    ~MacroEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void addStep (const numdeck::Step&, bool withWaitAfter = false);
    void showAddActionMenu();
    void showAddAppMenu();
    void addLink();
    void addWait();
    void editStep (int row);
    void showRowMenu (int row);
    void toggleLearning();
    void stopLearning();
    void showPlaceMenu (int row);
    int rowAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int row) const;
    void askNumber (const juce::String& title, int value, std::function<void (int)> done);
    void askLink (const juce::String& url, const juce::String& label, std::function<void (juce::String, juce::String)> done);

    Host host;
    std::vector<numdeck::Step> steps;
    juce::TextEditor nameBox;
    juce::Label nameLabel;
    juce::TextButton addAction, addKey, addWaitBtn, addApp, addLinkBtn, testBtn, saveBtn, cancelBtn;
    juce::Rectangle<int> listArea;
    int selected = -1, pressRow = -1, dropRow = -1, scroll = 0;
    bool dragging = false, learning = false;
    float learnPulse = 0.0f;

    std::unique_ptr<juce::AlertWindow> input;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroEditor)
};
