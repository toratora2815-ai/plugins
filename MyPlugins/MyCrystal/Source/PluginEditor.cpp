#include "PluginEditor.h"

using namespace mycryui;

//==============================================================================
void GrainView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1c2126));
    g.fillRoundedRectangle (r, 6.0f);
    const auto k = proc.currentKnobs();
    auto inner = r.reduced (12, 8);
    auto title = inner.removeFromTop (16);
    g.setColour (accent);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("GRAINS", title, juce::Justification::topLeft);

    // 上の説明: 何をしているか
    const double bpm = juce::jmax (20.0, proc.shownBpm.load());
    juce::String sTxt, dTxt;
    if (k.sync)
    {
        sTxt = juce::String (mycry::spliceNoteName (k.spliceNote)) + " (" + juce::String (juce::roundToInt (mycry::spliceNoteBeats (k.spliceNote) * 60000.0 / bpm)) + " ms)";
        dTxt = juce::String (mycry::delayNoteName (k.delayNote));
    }
    else { sTxt = juce::String (juce::roundToInt (k.spliceMs)) + " ms"; dTxt = juce::String (juce::roundToInt (k.delayMs)) + " ms"; }
    const int c = juce::roundToInt (k.pitchCents);
    juce::String pTxt = c == 0 ? "no pitch shift" : ((c > 0 ? "+" : "") + (c % 100 == 0 ? juce::String (c / 100) + " semitones" : juce::String (c) + " cents"));
    g.setColour (text.withAlpha (0.75f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ((k.reverse ? "REVERSE  |  " : "FORWARD  |  ") + pTxt + "  |  splice " + sTxt + "  |  delay " + dTxt, title.withTrimmedLeft (64), juce::Justification::topLeft);

    // 2 本の帯（左・右）: 0 から右へ時間（遅れ）。窓 = 粒が読む範囲
    const float loMs = proc.engine.shownLoMs.load(), winMs = proc.engine.shownWindowMs.load();
    const float winR = winMs * juce::jmax (0.01f, 1.0f - k.spliceOffPct / 100.0f);
    const float delR = (k.sync ? (float) (mycry::delayNoteBeats (k.delayNote) * 60000.0 / bpm) : k.delayMs) * juce::jmax (0.0f, 1.0f - k.delayOffPct / 100.0f);
    const float loR = juce::jmax (9.75f, delR);
    const float total = juce::jmax (50.0f, juce::jmax (loMs + winMs, loR + winR) * 1.12f);
    inner.removeFromTop (6);
    const float laneH = (inner.getHeight() - 22) / 2.0f;
    for (int lane = 0; lane < 2; ++lane)
    {
        auto lr = juce::Rectangle<float> (inner.getX() + 18, inner.getY() + lane * (laneH + 4), inner.getWidth() - 18, laneH - 4);
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (lane == 0 ? "L" : "R", juce::Rectangle<float> (inner.getX(), lr.getY(), 16, lr.getHeight()), juce::Justification::centred);
        g.setColour (dim);
        g.fillRoundedRectangle (lr, 3.0f);
        const float lo = lane == 0 ? loMs : loR, w = lane == 0 ? winMs : winR;
        const float x0 = lr.getX() + lr.getWidth() * lo / total, x1 = lr.getX() + lr.getWidth() * (lo + w) / total;
        const auto col = lane == 0 ? accent : accent2;
        g.setColour (col.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (x0, lr.getY(), juce::jmax (2.0f, x1 - x0), lr.getHeight()));
        // 今の位置
        const float pos = proc.engine.shownPos[lane].load();
        const float xp = x0 + (x1 - x0) * pos;
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (xp - 1.5f, lr.getY() - 2, 3.0f, lr.getHeight() + 4));
    }
    // 目盛り
    auto axis = juce::Rectangle<float> (inner.getX() + 18, inner.getBottom() - 16, inner.getWidth() - 18, 14);
    g.setColour (text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("0", axis, juce::Justification::centredLeft);
    g.drawText (total >= 1000.0f ? juce::String (total / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (total)) + " ms", axis, juce::Justification::centredRight);
    g.drawText ("time back from now  ->", axis, juce::Justification::centred);
}

//==============================================================================
MyCrystalEditor::MyCrystalEditor (MyCrystalProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff323942));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262c33));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20252b));
    lnf.setColour (juce::ComboBox::textColourId, text);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a424b));
    lnf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2126));
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
    lnf.setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff20252b));
    lnf.setColour (juce::TooltipWindow::textColourId, text);
    setLookAndFeel (&lnf);

    pitch.sliderPtr = std::make_unique<SemitoneSlider>();
    setupKnob (input, "input", "INPUT", "Level into the effect only (the dry sound is not changed). Drives the ANALOG color harder");
    setupKnob (pitch, "pitch", "PITCH", "Pitch of the grains, +-3 octaves. Steps in semitones; hold shift for cents");
    setupKnob (splice, "splice", "SPLICE", "Length of each grabbed slice. Short = metallic, long = clear echoes. Also roughly the echo time");
    setupKnob (spliceNote, "spliceNote", "SPLICE", "Length of each slice in notes (SYNC on). The grains restart on this grid");
    setupKnob (delay, "delay", "DELAY", "Extra time before the grains play");
    setupKnob (delayNote, "delayNote", "DELAY", "Extra time before the grains play, in notes (SYNC on)");
    setupKnob (recycle, "recycle", "RECYCLE", "Feeds the result back in. With PITCH set, every repeat climbs (or falls) further");
    setupKnob (mix, "mix", "MIX", "Dry vs. effect. On a send (Aux) use 100 %");
    setupKnob (output, "output", "OUTPUT", "Level of the effect (into the ANALOG output stage). The dry sound is not changed");
    setupKnob (thr, "threshold", "THRESHOLD", "Level for GATE / DUCK. The ring shows the input; red = above");
    setupKnob (gateDuck, "gateDuck", "GATE / DUCK", "Right = DUCK: the effect goes down while you play/sing (above THRESHOLD). Left = GATE: the effect only comes in when you play loud. Middle = off");
    setupKnob (pitchOff, "pitchOffset", "PITCH OFFSET", "Left goes down and right goes up by this much (stereo detune)");
    setupKnob (spliceOff, "spliceOffset", "SPLICE OFFSET", "Right slices are this much shorter than the left");
    setupKnob (delayOff, "delayOffset", "DELAY OFFSET", "Right delay is this much shorter than the left");
    setupKnob (smooth, "smoothing", "SMOOTHING", "Crossfade between slices. Higher = washy and smooth (up to half a slice)");
    setupKnob (lowCut, "lowCut", "LOW CUT", "Removes lows from the effect (12 dB/oct). Inside the RECYCLE loop: every repeat gets thinner");
    setupKnob (highCut, "highCut", "HIGH CUT", "Removes highs from the effect (12 dB/oct). Inside the RECYCLE loop: every repeat gets darker");
    setupKnob (att, "attack", "ATTACK", "How fast GATE / DUCK acts after the input goes above THRESHOLD");
    setupKnob (rel, "release", "RELEASE", "How fast GATE / DUCK returns after the input falls below THRESHOLD");
    pitch.slider().setDoubleClickReturnValue (true, 0.0);
    gateDuck.slider().setDoubleClickReturnValue (true, 0.0);
    spliceNote.slider().textFromValueFunction = [] (double v) { return juce::String (mycry::spliceNoteName ((int) std::lround (v))); };
    delayNote.slider().textFromValueFunction = [] (double v) { return juce::String (mycry::delayNoteName ((int) std::lround (v))); };
    spliceNote.slider().updateText(); delayNote.slider().updateText();

    setupBox (duckModeBox, duckModeAtt, "duckMode", "What GATE / DUCK turns down: the effect you hear, only the RECYCLE repeats, or both");
    setupBox (fbModeBox, fbModeAtt, "fbMode", "Mixed = both sides fed back together, Dual = left and right separate, Ping-Pong = repeats bounce left and right");
    for (auto* l : { &duckModeLabel, &fbModeLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }
    duckModeLabel.setText ("DUCK MODE", juce::dontSendNotification);
    fbModeLabel.setText ("FEEDBACK", juce::dontSendNotification);

    setupToggle (dirBtn, dirAtt, "direction", "REVERSE", "On = the slices play backwards. Off = forwards");
    dirBtn.onStateChange = [this] { dirBtn.setButtonText (dirBtn.getToggleState() ? "REVERSE" : "FORWARD"); };
    dirBtn.setButtonText (dirBtn.getToggleState() ? "REVERSE" : "FORWARD");
    setupToggle (syncBtn, syncAtt, "sync", "SYNC", "On = SPLICE and DELAY in notes, following the Logic tempo");
    setupToggle (analogBtn, analogAtt, "analog", "ANALOG", "On = soft analog color on the effect. Off = DIGITAL (clean, hard limit at -0.1 dB)");
    tweakBtn.setButtonText ("TWEAK");
    tweakBtn.setClickingTogglesState (true);
    tweakBtn.setTooltip ("Show the extra controls (stereo offsets, smoothing, filters, gate timing, feedback mode)");
    tweakBtn.onClick = [this] { setTweakOpen (tweakBtn.getToggleState()); };
    addAndMakeVisible (tweakBtn);

    factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    for (int i = 0; i < MyCrystalProcessor::numFactoryPresets(); ++i) factoryBox.addItem (MyCrystalProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int id = factoryBox.getSelectedId();
        if (id > 0) processor.loadFactoryPreset (id - 1);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
        factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    };
    addAndMakeVisible (factoryBox);
    addAndMakeVisible (grainView);

    // 下の欄
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): turn the effect down this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<Attachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 160; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Fader Pilot to lower the effect while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing - effect lowered" : "Fader Pilot found - voice not singing";
    };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    tweakOpen = (bool) processor.apvts.state.getProperty ("tweakOpen", false);
    tweakBtn.setToggleState (tweakOpen, juce::dontSendNotification);
    startTimerHz (30);
    setSize (1000, mainH + (tweakOpen ? tweakH : 0) + footerH);
}

MyCrystalEditor::~MyCrystalEditor()
{
    setLookAndFeel (nullptr);
}

void MyCrystalEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& tip)
{
    auto& s = k.slider();
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 112, 17);
    s.setTooltip (tip);
    addAndMakeVisible (s);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, s);
    s.updateText();
}

void MyCrystalEditor::setupBox (juce::ComboBox& b, std::unique_ptr<BoxAttachment>& attach, const juce::String& paramId, const juce::String& tip)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        b.addItemList (c->choices, 1);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    attach = std::make_unique<BoxAttachment> (processor.apvts, paramId, b);
}

void MyCrystalEditor::setupToggle (juce::TextButton& b, std::unique_ptr<BtnAttachment>& attach, const juce::String& paramId, const juce::String& t, const juce::String& tip)
{
    b.setButtonText (t);
    b.setClickingTogglesState (true);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    attach = std::make_unique<BtnAttachment> (processor.apvts, paramId, b);
}

void MyCrystalEditor::setTweakOpen (bool open)
{
    tweakOpen = open;
    processor.apvts.state.setProperty ("tweakOpen", open, nullptr);
    setSize (getWidth(), mainH + (open ? tweakH : 0) + footerH);
}

void MyCrystalEditor::timerCallback()
{
    const auto k = processor.currentKnobs();
    for (auto* kk : { &splice, &delay }) { kk->slider().setVisible (! k.sync); kk->label.setVisible (! k.sync); }
    for (auto* kk : { &spliceNote, &delayNote }) { kk->slider().setVisible (k.sync); kk->label.setVisible (k.sync); }
    for (auto* kk : { &pitchOff, &spliceOff, &delayOff, &smooth, &lowCut, &highCut, &att, &rel })
    { kk->slider().setVisible (tweakOpen); kk->label.setVisible (tweakOpen); }
    for (auto* c : std::initializer_list<juce::Component*> { &duckModeBox, &fbModeBox, &duckModeLabel, &fbModeLabel }) c->setVisible (tweakOpen);
    // GATE / DUCK が切りの間は、THRESHOLD・ATTACK・RELEASE・DUCK MODE を薄く
    const bool gd = std::abs (k.gateDuckDb) >= 0.25f;
    for (auto* c : std::initializer_list<juce::Component*> { &thr.slider(), &att.slider(), &rel.slider(), &duckModeBox }) c->setAlpha (gd ? 1.0f : 0.4f);
    const float in = processor.engine.shownInDb.load();
    inMeter = in > inMeter ? in : inMeter - 1.5f;
    grainView.repaint();
    repaint (lowerArea);
}

void MyCrystalEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff1d2227));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (accent);
    g.fillRect (0, 48, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MY CRYSTAL", 16, 0, 220, 48, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("granular pitch echo", 178, 2, 200, 46, juce::Justification::centredLeft);

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff1b1f24));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        if (title.isNotEmpty())
        {
            g.setColour (c);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 4).removeFromTop (14), juce::Justification::topLeft);
        }
    };
    section (mainArea, "", accent);
    section (lowerArea.withTrimmedLeft (grainArea.getWidth() + 10), "GATE / DUCK", warm);
    if (tweakOpen) section (tweakArea, "TWEAK", accent2);

    // THRESHOLD のまわりに入力の大きさ（赤 = 越えている）と、今どれだけ下げているか
    if (thr.slider().isVisible())
    {
        auto b = thr.slider().getBounds().toFloat().withTrimmedBottom (18).reduced (2);
        const float sz = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.withSizeKeepingCentre (sz, sz);
        const float norm = juce::jlimit (0.0f, 1.0f, (inMeter + 60.0f) / 60.0f);
        const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
        juce::Path arc; arc.addCentredArc (c.getCentreX(), c.getCentreY(), sz / 2 + 3, sz / 2 + 3, 0, a0, a0 + (a1 - a0) * norm, true);
        g.setColour (processor.engine.shownAbove.load() ? juce::Colour (0xffe0584a) : text.withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (3.0f));
        const float red = processor.engine.shownGateDb.load();
        g.setColour (red < -0.5f ? warm : text.withAlpha (0.4f));
        g.setFont (juce::FontOptions (11.0f));
        auto gb = gateDuck.slider().getBounds();
        g.drawText (red < -0.5f ? "effect " + juce::String (red, 1) + " dB" : "effect not lowered",
                    gb.getX() - 20, gb.getBottom() + 2, gb.getWidth() + 40, 14, juce::Justification::centred);
    }
}

void MyCrystalEditor::placeKnob (Knob& k, juce::Rectangle<int> col)
{
    k.label.setBounds (col.removeFromTop (16));
    k.slider().setBounds (col.reduced (2, 0));
}

void MyCrystalEditor::resized()
{
    // 下の欄（2 段）
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
    // 上の帯
    factoryBox.setBounds (getWidth() - 236, 10, 220, 28);
    tweakBtn.setBounds (factoryBox.getX() - 82, 10, 72, 28);
    analogBtn.setBounds (tweakBtn.getX() - 90, 10, 82, 28);
    syncBtn.setBounds (analogBtn.getX() - 72, 10, 64, 28);
    dirBtn.setBounds (syncBtn.getX() - 98, 10, 90, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (58).reduced (12, 0);
    // 1 段目: INPUT | PITCH | SPLICE | DELAY | RECYCLE | MIX | OUTPUT
    mainArea = area.removeFromTop (186);
    {
        auto r = mainArea.reduced (8, 10);
        const int w = r.getWidth() / 7;
        Knob* ks[] = { &input, &pitch, &splice, &delay, &recycle, &mix, &output };
        for (int i = 0; i < 7; ++i)
        {
            auto col = r.removeFromLeft (w);
            placeKnob (*ks[i], col);
            if (ks[i] == &splice) placeKnob (spliceNote, col);
            if (ks[i] == &delay) placeKnob (delayNote, col);
        }
    }
    area.removeFromTop (10);
    // 2 段目: 粒の様子 | THRESHOLD・GATE / DUCK
    lowerArea = area.removeFromTop (152);
    {
        auto r = lowerArea;
        grainArea = r.removeFromLeft (juce::roundToInt (r.getWidth() * 0.66f));
        grainView.setBounds (grainArea);
        r.removeFromLeft (10);
        r = r.reduced (10, 22);
        r.removeFromBottom (12);
        const int w = r.getWidth() / 2;
        placeKnob (thr, r.removeFromLeft (w));
        placeKnob (gateDuck, r);
    }
    // TWEAK
    area.removeFromTop (10);
    tweakArea = area.removeFromTop (tweakH - 10);
    {
        auto r = tweakArea.reduced (8, 6);
        r.removeFromTop (14);
        const int w = r.getWidth() / 10;
        Knob* ks[] = { &pitchOff, &spliceOff, &delayOff, &smooth, &lowCut, &highCut, &att, &rel };
        for (auto* kk : ks) placeKnob (*kk, r.removeFromLeft (w));
        auto c1 = r.removeFromLeft (w).reduced (4, 0), c2 = r.reduced (4, 0);
        duckModeLabel.setBounds (c1.removeFromTop (16)); duckModeBox.setBounds (c1.removeFromTop (28).translated (0, 24));
        fbModeLabel.setBounds (c2.removeFromTop (16)); fbModeBox.setBounds (c2.removeFromTop (28).translated (0, 24));
    }
}
