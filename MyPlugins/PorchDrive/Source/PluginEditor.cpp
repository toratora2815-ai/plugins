#include "PluginEditor.h"

PorchDriveEditor::PorchDriveEditor (PorchDriveProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (level, "level", "LEVEL");
    setupKnob (tone,  "tone",  "TONE");
    setupKnob (gain,  "gain",  "GAIN");
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    matchBtn.setButtonText ("MATCH");
    matchBtn.setClickingTogglesState (true);
    matchBtn.setTooltip ("Gain Stage (same LINK, before this plugin): keep the output at Gain Stage's TARGET level");
    linkBar.addAndMakeVisible (matchBtn);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "linkMatch", matchBtn);
    linkBar.extra = &matchBtn; linkBar.extraWidth = 64;
    linkBar.setSources (processor.link, { plink::kindGainStage });
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (processor.apvts.getRawParameterValue (myp::GainMatch::paramId)->load() > 0.5f)
            return "GAIN MATCH is on - LEVEL MATCH paused";
        if (! processor.link.on()) return "LINK OFF - same LINK as Gain Stage to match levels";
        if (! processor.match.sawStage()) return "waiting for Gain Stage (RMS mode)";
        lit = true;
        const float t = processor.match.trimDb();
        return "matched to TARGET (" + juce::String (t > 0.05f ? "+" : "") + juce::String (t, 1) + " dB)";
    };
    setSize (420, 260 + footerH);
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
    g.fillRect (0, getHeight() - footerH - 60, getWidth(), 60);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PORCH DRIVE", 0, getHeight() - footerH - 60, getWidth(), 60, juce::Justification::centred);
}

void PorchDriveEditor::resized()
{
    layoutFooter();
    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedBottom (60).reduced (10);
    const int w = area.getWidth() / 3;
    for (auto* k : { &level, &tone, &gain })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col.reduced (6));
    }
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void PorchDriveEditor::layoutFooter()
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
