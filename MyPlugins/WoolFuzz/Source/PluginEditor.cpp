#include "PluginEditor.h"

WoolFuzzEditor::WoolFuzzEditor (WoolFuzzProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (volume,  "volume",  "VOLUME");
    setupKnob (tone,    "tone",    "TONE");
    setupKnob (sustain, "sustain", "SUSTAIN");
    setSize (420, 260);
}

void WoolFuzzEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe8e2d0));
    k.slider.setColour (juce::Slider::thumbColourId, juce::Colours::black);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, juce::Colours::black);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, k.slider);
}

void WoolFuzzEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffb8b2a4));
    g.setColour (juce::Colour (0xff8a1c1c));
    g.fillRect (0, getHeight() - 60, getWidth(), 60);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("WOOL FUZZ", 0, getHeight() - 60, getWidth(), 60, juce::Justification::centred);
}

void WoolFuzzEditor::resized()
{
    auto area = getLocalBounds().withTrimmedBottom (60).reduced (10);
    const int w = area.getWidth() / 3;
    for (auto* k : { &volume, &tone, &sustain })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col.reduced (6));
    }
}
