#include "PluginEditor.h"

namespace
{
    // STYLE ごとのひとこと（画面の下の小さい字）
    const char* styleText[5] = {
        "A - round and even, slightly soft top",
        "E - bright and open, uneven crunchy clipping",
        "N - darker top, thick and forward mids",
        "T - low-mid weight, gentler drive range, uneven clipping",
        "P - tight lows, punchy low-mids, gritty",
    };
}

DecaSatuEditor::DecaSatuEditor (DecaSatuProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3632));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c2925));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.8f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setLookAndFeel (&lnf);

    const char* names[5] = { "A", "E", "N", "T", "P" };
    for (int i = 0; i < 5; ++i)
    {
        auto& b = styleBtn[i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (false);
        b.setTooltip (styleText[i]);
        b.onClick = [this, i]
        {
            if (auto* prm = processor.apvts.getParameter ("style"))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
            syncStyleButtons();
        };
        addAndMakeVisible (b);
    }
    styleHint.setColour (juce::Label::textColourId, text.withAlpha (0.65f));
    styleHint.setFont (juce::FontOptions (12.0f));
    styleHint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (styleHint);

    setupToggle (punishBtn, punishAtt, "punish", "PUNISH", "+16 dB more drive into the saturation");
    setupToggle (thumpBtn, thumpAtt, "thump", "THUMP", "Adds a resonant bump at the LOW CUT frequency");
    setupToggle (steepBtn, steepAtt, "steep", "STEEP", "HIGH CUT slope: off = gentle, on = steep");

    setupKnob (drive, "drive", "DRIVE", true);
    setupKnob (lowCut, "lowCut", "LOW CUT");
    setupKnob (tone, "tone", "TONE");
    setupKnob (highCut, "highCut", "HIGH CUT");
    setupKnob (mix, "mix", "MIX");
    setupKnob (output, "output", "OUTPUT");

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    matchBtn.setButtonText ("MATCH");
    matchBtn.setClickingTogglesState (true);
    matchBtn.setTooltip ("Gain Stage (same LINK, before this plugin): keep the output at Gain Stage's TARGET level");
    linkBar.addAndMakeVisible (matchBtn);
    matchAtt = std::make_unique<BtnAttachment> (processor.apvts, "linkMatch", matchBtn);
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

    syncStyleButtons();
    startTimerHz (20);
    setSize (760, 330 + footerH);
}

DecaSatuEditor::~DecaSatuEditor()
{
    setLookAndFeel (nullptr);
}

void DecaSatuEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, bool big)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, big ? accent : accent.withAlpha (0.85f));
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (big ? 17.0f : 13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, k.slider);
    if (paramId == "lowCut" || paramId == "highCut")
        k.slider.textFromValueFunction = [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"; };
    if (paramId == "drive")
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1); };
    k.slider.updateText();
}

void DecaSatuEditor::setupToggle (juce::TextButton& b, std::unique_ptr<BtnAttachment>& att, const juce::String& paramId,
                                  const juce::String& t, const juce::String& tip)
{
    b.setButtonText (t);
    b.setClickingTogglesState (true);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<BtnAttachment> (processor.apvts, paramId, b);
}

void DecaSatuEditor::syncStyleButtons()
{
    const int s = juce::jlimit (0, 4, (int) processor.apvts.getRawParameterValue ("style")->load());
    for (int i = 0; i < 5; ++i) styleBtn[i].setToggleState (i == s, juce::dontSendNotification);
    styleHint.setText (styleText[s], juce::dontSendNotification);
}

void DecaSatuEditor::timerCallback()
{
    syncStyleButtons();
    const float db = processor.outPeakDb.load();
    if (std::abs (db - meterDb) > 0.2f) { meterDb = db; repaint (getWidth() - 40, 60, 30, 200); }
}

void DecaSatuEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    // 上の帯と名前
    g.setColour (juce::Colour (0xff272420));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (accent);
    g.fillRect (0, 48, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("DECA SATU", 16, 0, 300, 48, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("STYLE", 300, 0, 60, 48, juce::Justification::centredRight);

    // 出口のメーター（右端）
    auto m = juce::Rectangle<int> (getWidth() - 34, 66, 14, 190);
    g.setColour (juce::Colour (0xff0f0e0d));
    g.fillRect (m);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb + 48.0f) / 48.0f);
    auto lit = m.withTrimmedTop ((int) (m.getHeight() * (1.0f - norm)));
    g.setColour (meterDb > -0.5f ? juce::Colour (0xffe04a3a) : accent);
    g.fillRect (lit);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("OUT", m.getX() - 8, m.getBottom() + 2, 30, 14, juce::Justification::centred);
}

void DecaSatuEditor::resized()
{
    layoutFooter();
    // 上の帯: STYLE ボタンと PUNISH
    {
        int x = 368;
        for (auto& b : styleBtn) { b.setBounds (x, 9, 40, 30); x += 44; }
        punishBtn.setBounds (getWidth() - 110, 9, 90, 30);
    }
    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (56).withTrimmedRight (44).reduced (12, 6);
    styleHint.setBounds (area.removeFromBottom (20));
    // 左に大きい DRIVE、右に 5 つ
    auto left = area.removeFromLeft (220);
    drive.label.setBounds (left.removeFromTop (24));
    drive.slider.setBounds (left.reduced (6));
    area.removeFromLeft (8);
    auto toggles = area.removeFromBottom (34);
    const int w = area.getWidth() / 5;
    Knob* ks[5] = { &lowCut, &tone, &highCut, &mix, &output };
    for (int i = 0; i < 5; ++i)
    {
        auto col = area.removeFromLeft (w);
        ks[i]->label.setBounds (col.removeFromTop (20));
        ks[i]->slider.setBounds (col.reduced (4));
    }
    thumpBtn.setBounds (toggles.getX() + (w - 80) / 2, toggles.getY() + 2, 80, 26);
    steepBtn.setBounds (toggles.getX() + 2 * w + (w - 80) / 2, toggles.getY() + 2, 80, 26);
}

// 画面のいちばん下（2 段: 上に LINK 欄、下に GAIN MATCH・プリセット・取り消し）
void DecaSatuEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
    f.removeFromTop (4);
    presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
}
