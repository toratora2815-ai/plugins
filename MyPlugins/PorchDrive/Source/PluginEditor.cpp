#include "PluginEditor.h"

PorchDriveEditor::PorchDriveEditor (PorchDriveProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (level, "level", "LEVEL");
    setupKnob (tone,  "tone",  "TONE");
    setupKnob (gain,  "gain",  "GAIN");
    setSize (420, 260);
}

void PorchDriveEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::white);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff10284a));
    k.slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, k.slider);
}

void PorchDriveEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2f6fb3));
    g.setColour (juce::Colour (0xff10284a));
    g.fillRect (0, getHeight() - 60, getWidth(), 60);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PORCH DRIVE", 0, getHeight() - 60, getWidth(), 60, juce::Justification::centred);
}

void PorchDriveEditor::resized()
{
    auto area = getLocalBounds().withTrimmedBottom (60).reduced (10);
    const int w = area.getWidth() / 3;
    for (auto* k : { &level, &tone, &gain })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col.reduced (6));
    }
}
