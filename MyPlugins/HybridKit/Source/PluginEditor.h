#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// キットの絵（客席から見た形）。叩くと光る。クリックで部品を選んで試し打ち（shift = 弱く、ハイハットは alt でオープン）
class KitView : public juce::Component
{
public:
    KitView (HybridKitProcessor& p, juce::Colour accentColour, juce::Colour textColour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelectedGroup (int g) { selected = g; repaint(); }
    void tick();
    std::function<void (int group)> onSelect;

private:
    struct Shape { int piece; juce::Rectangle<float> r; bool cymbal; int note; };
    std::vector<Shape> shapes() const;
    HybridKitProcessor& proc;
    juce::Colour accent, text;
    int selected = hk::GKick;
    std::array<float, hk::NumPieces> glow {};
};

class HybridKitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HybridKitEditor (HybridKitProcessor&);
    ~HybridKitEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void setupKnob (Knob&, const juce::String& name, bool vertical = false);
    void selectGroup (int g);
    void timerCallback() override;

    HybridKitProcessor& processor;
    const juce::Colour accent { 0xff3fd0c0 }, wood { 0xffd99a5b }, panel { 0xff16181c }, text { 0xffeceae6 };

    juce::ComboBox kitBox, factoryBox;
    std::unique_ptr<ComboAtt> kitAtt;
    KitView kitView { processor, accent, text };

    int group = hk::GKick;
    Knob volK, tuneK, lenK;                 // 選んだ部品の VOLUME / TUNE / LENGTH
    Knob blendK, edmTuneK;                  // EDM の層
    juce::ComboBox soundBox, noteBox;
    juce::Label soundLabel, noteLabel, edmHint;
    std::unique_ptr<ComboAtt> soundAtt, noteAtt;
    Knob closeF, ohF, roomF, bleedF, outF;  // ミキサー
    std::array<juce::TextButton, 6> sepBtn;  // 別の出口へ（KICK / SNARE / TOMS / HI-HAT / CYMBALS / ROOM）
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 6> sepAtt;
    juce::Rectangle<int> outsArea;
    int shownOuts = -1;

    plink::LinkBar linkBar { processor.apvts, accent, panel, text };
    myp::PresetBar presetBar { processor.myPresets, accent, panel, text };
    static constexpr int footerH = 40, topH = 48;
    juce::Rectangle<int> piecePanel, edmPanel, mixerPanel, statusArea;
    int shownState = -1; float shownProgress = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridKitEditor)
};
