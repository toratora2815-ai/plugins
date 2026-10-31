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

    setSize (640, 380);
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
    auto area = getLocalBounds().reduced (14);
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
