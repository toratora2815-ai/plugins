#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ModHostEditor : public juce::AudioProcessorEditor,
                      private juce::Timer,
                      private ModHostProcessor::Listener,
                      private juce::ComponentListener
{
public:
    explicit ModHostEditor (ModHostProcessor&);
    ~ModHostEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void hostedPluginAboutToChange() override;
    void hostedPluginChanged() override;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    void showPluginMenu();
    void showTargetMenu (int t);
    void selectMod (int m);
    void updateVisibility();
    void makeHostedEditor();
    void updateSize();

    // MOD の切り替えタブ（今の値を小さな棒で）
    struct Tab : juce::Component
    {
        ModHostEditor& ed; int index;
        Tab (ModHostEditor& e, int i) : ed (e), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { ed.selectMod (index); }
    };
    // 動きの表示（STEPS のときは段の値をドラッグで書く）
    struct Scope : juce::Component
    {
        ModHostEditor& ed;
        explicit Scope (ModHostEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override { edit (e); }
        void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
        void edit (const juce::MouseEvent&);
        float history[240] {}; int write = 0;
    };
    // つなぐ先の 1 行
    struct Row
    {
        juce::TextButton learn { "LEARN" }, target, clear { "X" };
        juce::Slider amount;
        std::unique_ptr<SA> att;
        juce::Rectangle<int> barArea;
    };

    ModHostProcessor& proc;
    int sel = 0;

    juce::TextButton pluginButton, uiButton;
    juce::Label status;
    bool showUI = true;
    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;

    std::vector<std::unique_ptr<Tab>> tabs;
    Scope scope { *this };

    juce::ComboBox typeBox, shapeBox, divBox;
    juce::TextButton syncButton { "SYNC" }, biButton { "BI" };
    juce::Slider rate, attack, release, gain, smooth, phase, steps;
    juce::Label rateL, attackL, releaseL, gainL, smoothL, phaseL, stepsL;
    std::unique_ptr<CA> typeAtt, shapeAtt, divAtt;
    std::unique_ptr<BA> syncAtt, biAtt;
    std::unique_ptr<SA> rateAtt, attackAtt, releaseAtt, gainAtt, smoothAtt, phaseAtt, stepsAtt;
    Row rows[mh::numTargets];

    juce::Rectangle<int> topBar, hostArea, panel;
    int lastType = -1, lastSync = -1, blink = 0;
    std::vector<mh::AuEntry> menuCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModHostEditor)
};
