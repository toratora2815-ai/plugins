#include "PluginEditor.h"

DrumTransientEditor::DrumTransientEditor (DrumTransientProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (push,    "push",    "PUSH",    "how hard to drive");
    setupKnob (punch,   "punch",   "PUNCH",   "keep the attack");
    setupKnob (body,    "body",    "BODY",    "- tight / + room");
    setupKnob (clip,    "clip",    "CLIP / LIMIT", "cut or smooth");
    setupKnob (release, "release", "RELEASE", "tight / smooth");
    setupCombo (lowKeep, lowKeepLabel, "lowKeep", "LOW KEEP", DrumTransientProcessor::lowKeepNames(), lowKeepAtt);
    setupCombo (ceiling, ceilingLabel, "ceiling", "CEILING", DrumTransientProcessor::ceilingNames(), ceilingAtt);
    lowKeep.setTooltip ("Below this frequency nothing is clipped (keeps the kick's fundamental clean)");
    ceiling.setTooltip ("Output never goes above this level");

    for (auto* b : { &linkBtn, &deltaBtn })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setColour (juce::TextButton::textColourOffId, text);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (*b);
    }
    linkBtn.setTooltip ("Same gain on left and right (keeps the snare in the centre)");
    deltaBtn.setTooltip ("Listen only to what is being removed");
    linkAtt = std::make_unique<ButtonAtt> (processor.apvts, "stereoLink", linkBtn);
    deltaAtt = std::make_unique<ButtonAtt> (processor.apvts, "delta", deltaBtn);

    factory.setTextWhenNothingSelected ("Presets");
    const auto names = DrumTransientProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i) factory.addItem (names[i], i + 1);
    factory.onChange = [this] { const int id = factory.getSelectedId(); if (id > 0) processor.applyPreset (id - 1); };
    addAndMakeVisible (factory);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });
    linkBar.status = [this] (bool& lit) -> juce::String { lit = processor.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };

    setSize (820, topH + 230 + footerH);
    startTimerHz (30);
}

void DrumTransientEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& hint)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.2f));
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);

    k.hint.setText (hint, juce::dontSendNotification);
    k.hint.setJustificationType (juce::Justification::centred);
    k.hint.setFont (juce::FontOptions (11.0f));
    k.hint.setColour (juce::Label::textColourId, text.withAlpha (0.5f));
    addAndMakeVisible (k.hint);

    k.attachment = std::make_unique<SliderAtt> (processor.apvts, paramId, k.slider);
}

void DrumTransientEditor::setupCombo (juce::ComboBox& c, juce::Label& l, const juce::String& paramId, const juce::String& name,
                                      const juce::StringArray& items, std::unique_ptr<ComboAtt>& att)
{
    c.addItemList (items, 1);
    addAndMakeVisible (c);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (l);
    att = std::make_unique<ComboAtt> (processor.apvts, paramId, c);
}

void DrumTransientEditor::timerCallback()
{
    auto follow = [] (float& shown, float& hold, int& holdT, float v)
    {
        shown = v > shown ? v : shown * 0.85f + v * 0.15f;
        if (v >= hold) { hold = v; holdT = 45; } else if (--holdT < 0) hold = juce::jmax (0.0f, hold - 0.3f);
    };
    follow (showAtk, holdAtk, holdAtkT, processor.dsp.meterAttack.load());
    follow (showBody, holdBody, holdBodyT, processor.dsp.meterBody.load());
    repaint (meterArea);
}

void DrumTransientEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto top = getLocalBounds().removeFromTop (topH);
    g.setColour (panel.brighter (0.06f));
    g.fillRect (top);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("DRUM TRANSIENT", top.reduced (14, 0), juce::Justification::centredLeft);

    // GR メーター 2 本（アタックと胴）
    auto m = meterArea.reduced (8, 6);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const int bw = (m.getWidth() - 10) / 2;
    auto drawBar = [&] (juce::Rectangle<int> r, float db, float hold, const juce::String& name, juce::Colour col)
    {
        g.setColour (text.withAlpha (0.8f));
        g.drawText (name, r.removeFromTop (16), juce::Justification::centred);
        auto valR = r.removeFromBottom (16);
        g.drawText ("-" + juce::String (db, 1), valR, juce::Justification::centred);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (r);
        const float range = 20.0f;
        const int h = (int) std::round (r.getHeight() * juce::jlimit (0.0f, 1.0f, db / range));
        g.setColour (col);
        g.fillRect (r.withHeight (h));
        const int hy = r.getY() + (int) std::round (r.getHeight() * juce::jlimit (0.0f, 1.0f, hold / range));
        g.setColour (text);
        g.fillRect (r.getX(), hy - 1, r.getWidth(), 2);
        g.setColour (text.withAlpha (0.15f));
        for (int d = 5; d < 20; d += 5)
        {
            const int y = r.getY() + r.getHeight() * d / 20;
            g.drawHorizontalLine (y, (float) r.getX(), (float) r.getRight());
        }
    };
    drawBar (m.removeFromLeft (bw), showAtk, holdAtk, "ATTACK", accent);
    m.removeFromLeft (10);
    drawBar (m.removeFromLeft (bw), showBody, holdBody, "BODY", juce::Colour (0xff6fb4ff));
}

void DrumTransientEditor::resized()
{
    layoutFooter();
    auto area = getLocalBounds().withTrimmedBottom (footerH);
    auto top = area.removeFromTop (topH).reduced (10, 8);
    top.removeFromLeft (220);
    deltaBtn.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    linkBtn.setBounds (top.removeFromRight (64));
    top.removeFromRight (14);
    factory.setBounds (top.removeFromRight (200));

    area = area.reduced (10, 8);
    meterArea = area.removeFromRight (meterW);
    auto combos = area.removeFromRight (120);
    combos.removeFromTop (20);
    lowKeepLabel.setBounds (combos.removeFromTop (20));
    lowKeep.setBounds (combos.removeFromTop (26).reduced (6, 0));
    combos.removeFromTop (30);
    ceilingLabel.setBounds (combos.removeFromTop (20));
    ceiling.setBounds (combos.removeFromTop (26).reduced (6, 0));

    const int w = area.getWidth() / 5;
    for (auto* k : { &push, &punch, &body, &clip, &release })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->hint.setBounds (col.removeFromBottom (16));
        k->slider.setBounds (col.reduced (4));
    }
}

// 画面のいちばん下（Plugin Link と GAIN MATCH・プリセット・取り消し）。2 段
void DrumTransientEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
    f.removeFromTop (4);
    presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
}
