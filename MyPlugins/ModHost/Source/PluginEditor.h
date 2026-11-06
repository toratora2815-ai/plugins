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
    void showPage (int);
    void layoutBands (juce::Rectangle<int>);
    void layoutMorph (juce::Rectangle<int>);
    void paintBandsInfo (juce::Graphics&);
    void paintMorphInfo (juce::Graphics&);

    // BANDS ページのグラフ: 入口（灰色）と出口（色）のスペクトル、帯域の分け目（ドラッグ）、帯域クリックで SEND の ON/OFF
    struct BandGraph : juce::Component
    {
        ModHostEditor& ed;
        explicit BandGraph (ModHostEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        float xOf (float hz) const;
        float hzOf (float x) const;
        int dragging = -1;   // 0 = SPLIT LOW、1 = SPLIT HIGH
    };

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

    // 画面のいちばん下: Plugin Link と プリセット・取り消し

    plink::LinkBar linkBar { proc.apvts, juce::Colour (0xff4fd1e8), juce::Colour (0xff252b32), juce::Colour (0xffe6eaee) };

    myp::PresetBar presetBar { proc.myPresets, juce::Colour (0xff4fd1e8), juce::Colour (0xff252b32), juce::Colour (0xffe6eaee) };

    static constexpr int footerH = 40;

    void layoutFooter();
    int sel = 0;

    juce::TextButton pluginButton, uiButton;
    juce::Label status;
    bool showUI = true;
    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;

    // ページ（MOD / BANDS / MORPH）
    int page = 0;
    juce::TextButton pageButtons[3];
    std::vector<juce::Component*> modComps, bandComps, morphComps;

    // BANDS
    BandGraph graph { *this };
    juce::Slider drive, mixK, outK, sendS[3];
    juce::Label driveL, mixL, outL;
    juce::TextButton autoButton { "AUTO GAIN" }, bandsButton { "BANDS" };
    std::unique_ptr<SA> driveAtt, mixAtt, outAtt, sendAtt[3];
    std::unique_ptr<BA> autoAtt, bandsAtt;
    juce::Rectangle<int> bandInfo, autoInfoArea;

    // MORPH
    juce::TextButton storeA { "STORE A" }, storeB { "STORE B" }, clearMorphButton { "CLEAR" };
    juce::Slider morphS;
    std::unique_ptr<SA> morphAtt;
    juce::Rectangle<int> morphInfo;

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
