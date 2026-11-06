#include "PluginEditor.h"

namespace
{
    const char* ids[]   = { "mix", "decay", "size", "predelay", "damping", "mod", "shimmer", "width", "lowcut" };
    const char* names[] = { "MIX", "DECAY", "SIZE", "PRE-DELAY", "DAMPING", "MOD", "SHIMMER", "WIDTH", "LOW CUT" };
    const juce::Colour accent (0xff8fb8ff), bgTop (0xff0d1020), bgBottom (0xff1c1433);
}

NebulaEditor::NebulaEditor (NebulaProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2f4a));
        k.slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.8f));
        addAndMakeVisible (k.slider);

        k.label.setText (names[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        k.label.setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, ids[i], k.slider);
    }

    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2f4a));
    freezeButton.setColour (juce::TextButton::buttonOnColourId, accent);
    freezeButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "freeze", freezeButton);
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the reverb by this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "Plugin Link OFF - same LINK as Fader Pilot to duck the reverb while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing: reverb " + juce::String (processor.duck.nowDb(), 1) + " dB" : juce::String ("voice resting: full reverb");
    };

    setSize (640, 380 + footerH);
}

void NebulaEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0, 0, bgBottom, 0, (float) getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("NEBULA", 20, 10, 300, 40, juce::Justification::centredLeft);
    g.setColour (accent.withAlpha (0.7f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("cinematic reverb", 140, 10, 200, 40, juce::Justification::centredLeft);
}

void NebulaEditor::resized()
{
    layoutFooter();
    auto area = getLocalBounds().withTrimmedBottom (footerH).reduced (14);
    auto header = area.removeFromTop (46);
    freezeButton.setBounds (header.removeFromRight (110).reduced (4, 8));

    const int rowH = area.getHeight() / 2;
    auto row1 = area.removeFromTop (rowH);
    auto row2 = area;
    const int w = row1.getWidth() / 5;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& row = i < 5 ? row1 : row2;
        auto col = row.removeFromLeft (w);
        knobs[i].label.setBounds (col.removeFromTop (20));
        knobs[i].slider.setBounds (col.reduced (4));
    }
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void NebulaEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    if (footerH < 50)   // 1 段
    {
        presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
        f.removeFromRight (6);
        linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));
    }
    else
    {
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
}
