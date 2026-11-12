#include "PluginEditor.h"

//==============================================================================
// キットの絵
KitView::KitView (HybridKitProcessor& p, juce::Colour accentColour, juce::Colour textColour)
    : proc (p), accent (accentColour), text (textColour)
{
    setRepaintsOnMouseActivity (false);
}

std::vector<KitView::Shape> KitView::shapes() const
{
    const float W = (float) getWidth(), H = (float) getHeight();
    auto at = [&] (float cx, float cy, float w, float h) { return juce::Rectangle<float> (W * (cx - w * 0.5f), H * (cy - h * 0.5f), W * w, H * h); };
    // 奥から手前の順（当たり判定は逆から）
    std::vector<Shape> s {
        { hk::CrashL,   at (0.17f, 0.16f, 0.27f, 0.10f), true,  49 },
        { hk::CrashR,   at (0.83f, 0.14f, 0.27f, 0.10f), true,  57 },
        { hk::Ride,     at (0.84f, 0.42f, 0.29f, 0.11f), true,  51 },
        { hk::TomHi,    at (0.38f, 0.33f, 0.14f, 0.17f), false, 48 },
        { hk::TomMidHi, at (0.55f, 0.31f, 0.145f, 0.18f), false, 47 },
        { hk::TomMidLo, at (0.665f, 0.47f, 0.15f, 0.19f), false, 45 },
        { hk::TomLo,    at (0.74f, 0.72f, 0.19f, 0.25f), false, 43 },
        { hk::HiHat,    at (0.11f, 0.47f, 0.19f, 0.075f), true, 42 },
        { hk::Snare,    at (0.25f, 0.69f, 0.17f, 0.15f), false, 38 },
        { hk::Kick,     at (0.48f, 0.70f, 0.25f, 0.44f), false, 36 } };
    return s;
}

void KitView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0f1114));
    g.fillRoundedRectangle (b, 8.0f);
    // 床の敷物
    g.setColour (juce::Colour (0xff1c1f25));
    g.fillEllipse (b.getWidth() * 0.06f, b.getHeight() * 0.62f, b.getWidth() * 0.88f, b.getHeight() * 0.36f);

    static const char* names[] = { "Kick", "Snare", "Tom 1", "Tom 2", "Tom 3", "Floor Tom", "Hi-Hat", "Crash L", "Crash R", "Ride" };
    const bool loaded = proc.loadState() == HybridKitProcessor::stateReady;
    for (const auto& s : shapes())
    {
        const bool present = ! loaded || proc.piecePresent (s.piece);
        if (! present) continue;
        const bool sel = hk::groupOf (s.piece) == selected;
        const float gl = glow[(size_t) s.piece];
        const auto r = s.r;
        if (s.cymbal)
        {
            juce::ColourGradient cg (juce::Colour (0xffe2c068), r.getCentreX(), r.getCentreY(), juce::Colour (0xff7a5a22), r.getRight(), r.getBottom(), true);
            g.setGradientFill (cg);
            g.fillEllipse (r);
            g.setColour (juce::Colour (0x40000000));
            for (float k = 0.25f; k < 1.0f; k += 0.25f) g.drawEllipse (r.withSizeKeepingCentre (r.getWidth() * k, r.getHeight() * k), 1.0f);
            g.setColour (juce::Colour (0xfff1d98a));
            g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.16f, r.getHeight() * 0.32f));
            if (s.piece == hk::HiHat)   // 下のシンバル
            {
                g.setColour (juce::Colour (0xff6d5020));
                g.drawEllipse (r.translated (0, r.getHeight() * 0.35f), 1.5f);
            }
        }
        else if (s.piece == hk::Kick)
        {
            const auto c = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
            g.setColour (juce::Colour (0xff6b3f22));
            g.fillEllipse (c);
            juce::ColourGradient hg (juce::Colour (0xfff4ecd8), c.getCentreX(), c.getY(), juce::Colour (0xffc9bea4), c.getCentreX(), c.getBottom(), false);
            g.setGradientFill (hg);
            g.fillEllipse (c.reduced (c.getWidth() * 0.06f));
            g.setColour (juce::Colour (0xff3a2a1c));
            g.setFont (juce::FontOptions (c.getHeight() * 0.13f, juce::Font::bold));
            g.drawText ("HK", c, juce::Justification::centred);
        }
        else
        {
            const auto shell = r.withTrimmedTop (r.getHeight() * 0.25f);
            juce::ColourGradient wg (juce::Colour (0xffa8643a), shell.getX(), shell.getY(), juce::Colour (0xff5a321c), shell.getRight(), shell.getY(), false);
            wg.addColour (0.45, juce::Colour (0xffd48c55));
            g.setGradientFill (wg);
            g.fillRoundedRectangle (shell, 4.0f);
            g.setColour (juce::Colour (0xffb9b9b9));
            g.drawRoundedRectangle (shell, 4.0f, 1.0f);
            const auto head = r.withHeight (r.getHeight() * 0.5f);
            juce::ColourGradient hg (juce::Colour (0xfff4ecd8), head.getCentreX(), head.getY(), juce::Colour (0xffc9bea4), head.getCentreX(), head.getBottom(), false);
            g.setGradientFill (hg);
            g.fillEllipse (head);
            g.setColour (juce::Colour (0xff8c8c8c));
            g.drawEllipse (head, 1.2f);
        }
        if (gl > 0.01f)
        {
            g.setColour (accent.withAlpha (juce::jlimit (0.0f, 0.65f, gl * 0.65f)));
            g.fillEllipse (s.piece == hk::Kick ? r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())) : r);
        }
        if (sel)
        {
            g.setColour (accent);
            if (s.piece == hk::Kick) g.drawEllipse (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())).expanded (2), 2.5f);
            else if (s.cymbal) g.drawEllipse (r.expanded (2), 2.5f);
            else g.drawRoundedRectangle (r.expanded (2), 6.0f, 2.5f);
        }
        g.setColour (text.withAlpha (sel ? 0.95f : 0.6f));
        g.setFont (juce::FontOptions (12.0f, sel ? juce::Font::bold : juce::Font::plain));
        g.drawText (names[s.piece], juce::Rectangle<float> (r.getX() - 20, r.getBottom() + 1, r.getWidth() + 40, 15), juce::Justification::centred);
    }
    g.setColour (text.withAlpha (0.35f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("click = play   shift = soft   alt (hi-hat) = open", b.reduced (10, 6), juce::Justification::bottomRight);
}

void KitView::mouseDown (const juce::MouseEvent& e)
{
    const auto list = shapes();
    const bool loaded = proc.loadState() == HybridKitProcessor::stateReady;
    for (auto it = list.rbegin(); it != list.rend(); ++it)
    {
        if (loaded && ! proc.piecePresent (it->piece)) continue;
        auto r = it->r;
        if (it->piece == hk::Kick) r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        const float dx = (e.position.x - r.getCentreX()) / (r.getWidth() * 0.5f), dy = (e.position.y - r.getCentreY()) / (r.getHeight() * 0.5f);
        const bool hit = (it->cymbal || it->piece == hk::Kick) ? dx * dx + dy * dy <= 1.0f : r.contains (e.position);
        if (! hit) continue;
        if (onSelect) onSelect (hk::groupOf (it->piece));
        int note = it->note;
        if (it->piece == hk::HiHat && e.mods.isAltDown()) note = 46;
        proc.audition (note, e.mods.isShiftDown() ? 55 : 105);
        return;
    }
}

void KitView::tick()
{
    bool dirty = false;
    for (int p = 0; p < hk::NumPieces; ++p)
    {
        const float v = proc.hitFlash (p);
        if (v > 0.0f) { glow[(size_t) p] = juce::jmax (glow[(size_t) p], v); proc.clearFlash (p, 0.0f); dirty = true; }
        if (glow[(size_t) p] > 0.01f) { glow[(size_t) p] *= 0.82f; dirty = true; }
        else glow[(size_t) p] = 0.0f;
    }
    if (dirty) repaint();
}

//==============================================================================
HybridKitEditor::HybridKitEditor (HybridKitProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    kitBox.addItemList (HybridKitProcessor::kitNames(), 1);
    kitBox.setTooltip ("Drum Kit Designer kit (read from Logic's sound library on this Mac)");
    addAndMakeVisible (kitBox);
    kitAtt = std::make_unique<ComboAtt> (processor.apvts, "kit", kitBox);

    factoryBox.setTextWhenNothingSelected ("Presets");
    const auto names = HybridKitProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i) factoryBox.addItem (names[i], i + 1);
    factoryBox.onChange = [this] { const int id = factoryBox.getSelectedId(); if (id > 0) processor.applyPreset (id - 1); };
    addAndMakeVisible (factoryBox);

    for (auto* c : { &kitBox, &factoryBox, &soundBox, &noteBox })
    {
        c->setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.12f));
        c->setColour (juce::ComboBox::textColourId, text);
        c->setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
        c->setColour (juce::ComboBox::arrowColourId, accent);
    }

    kitView.onSelect = [this] (int g) { selectGroup (g); };
    addAndMakeVisible (kitView);

    setupKnob (volK, "VOLUME");
    setupKnob (tuneK, "TUNE");
    setupKnob (lenK, "LENGTH");
    setupKnob (blendK, "BLEND");
    setupKnob (edmTuneK, "LAYER TUNE");
    volK.slider.setTooltip ("Level of this piece (all mics)");
    tuneK.slider.setTooltip ("Pitch of the recorded drum, in semitones (all mics move together)");
    lenK.slider.setTooltip ("100 % = natural ring. Lower = shorter (like dampening)");
    blendK.slider.setTooltip ("0 % = live drum only. Kick: the low end below 100 Hz moves to the 808 sub (the attack stays live). Snare: adds the EDM layer on hard hits (not on ghost notes)");
    edmTuneK.slider.setTooltip ("Pitch of the EDM snare layer, in semitones");

    for (auto* l : { &soundLabel, &noteLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (*l);
    }
    soundLabel.setText ("SOUND", juce::dontSendNotification);
    noteLabel.setText ("NOTE", juce::dontSendNotification);
    for (int i = 0; i < hk::kNumKickNotes; ++i) noteBox.addItem (hk::kickNoteName (i), i + 1);
    noteBox.setTooltip ("Pitch of the 808 sub. Match the key of the song");
    soundBox.setTooltip ("Which EDM layer to add");
    addAndMakeVisible (soundBox);
    addAndMakeVisible (noteBox);
    edmHint.setText ("EDM layer: Kick and Snare", juce::dontSendNotification);
    edmHint.setJustificationType (juce::Justification::centred);
    edmHint.setFont (juce::FontOptions (13.0f));
    edmHint.setColour (juce::Label::textColourId, text.withAlpha (0.45f));
    addAndMakeVisible (edmHint);

    setupKnob (closeF, "CLOSE", true);
    setupKnob (ohF, "OH", true);
    setupKnob (roomF, "ROOM", true);
    setupKnob (bleedF, "BLEED", true);
    setupKnob (outF, "OUTPUT", true);
    closeF.att = std::make_unique<SliderAtt> (processor.apvts, "close", closeF.slider);
    ohF.att = std::make_unique<SliderAtt> (processor.apvts, "oh", ohF.slider);
    roomF.att = std::make_unique<SliderAtt> (processor.apvts, "room", roomF.slider);
    bleedF.att = std::make_unique<SliderAtt> (processor.apvts, "bleed", bleedF.slider);
    outF.att = std::make_unique<SliderAtt> (processor.apvts, "output", outF.slider);
    closeF.slider.setTooltip ("Close mics (kick in/out, snare top/bottom, toms, hi-hat)");
    ohF.slider.setTooltip ("Overhead mics (the whole kit from above)");
    roomF.slider.setTooltip ("Room mics A and B (the sound of the room)");
    bleedF.slider.setTooltip ("Bleed: each drum leaking into the other close mics");
    outF.slider.setTooltip ("Output level");

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.status = [this] (bool& lit) -> juce::String { lit = processor.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };

    selectGroup (hk::GKick);
    setSize (1040, topH + 530 + footerH);
    startTimerHz (30);
}

void HybridKitEditor::setupKnob (Knob& k, const juce::String& name, bool vertical)
{
    k.slider.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.25f));
    k.slider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.8f));
    k.slider.setColour (juce::Slider::backgroundColourId, panel.brighter (0.25f));
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
}

void HybridKitEditor::selectGroup (int g)
{
    group = juce::jlimit (0, hk::NumGroups - 1, g);
    kitView.setSelectedGroup (group);
    const juce::String id (HybridKitProcessor::groupId (group));
    volK.att.reset(); tuneK.att.reset(); lenK.att.reset();
    volK.att = std::make_unique<SliderAtt> (processor.apvts, id + "Vol", volK.slider);
    tuneK.att = std::make_unique<SliderAtt> (processor.apvts, id + "Tune", tuneK.slider);
    lenK.att = std::make_unique<SliderAtt> (processor.apvts, id + "Len", lenK.slider);

    const bool kick = group == hk::GKick, snare = group == hk::GSnare;
    soundAtt.reset(); noteAtt.reset(); blendK.att.reset(); edmTuneK.att.reset();
    soundBox.clear (juce::dontSendNotification);
    if (kick)
    {
        for (int i = 0; i < hk::kNumKickSounds; ++i) soundBox.addItem (hk::kickSound (i).name, i + 1);
        soundAtt = std::make_unique<ComboAtt> (processor.apvts, "kickSound", soundBox);
        noteAtt = std::make_unique<ComboAtt> (processor.apvts, "kickNote", noteBox);
        blendK.att = std::make_unique<SliderAtt> (processor.apvts, "kickBlend", blendK.slider);
    }
    else if (snare)
    {
        for (int i = 0; i < hk::kNumSnareSounds; ++i) soundBox.addItem (hk::snareSoundName (i), i + 1);
        soundAtt = std::make_unique<ComboAtt> (processor.apvts, "snareSound", soundBox);
        blendK.att = std::make_unique<SliderAtt> (processor.apvts, "snareBlend", blendK.slider);
        edmTuneK.att = std::make_unique<SliderAtt> (processor.apvts, "snareEdmTune", edmTuneK.slider);
    }
    const bool edm = kick || snare;
    for (juce::Component* c : { (juce::Component*) &blendK.slider, (juce::Component*) &blendK.label, (juce::Component*) &soundBox, (juce::Component*) &soundLabel })
        c->setVisible (edm);
    noteBox.setVisible (kick); noteLabel.setVisible (kick);
    edmTuneK.slider.setVisible (snare); edmTuneK.label.setVisible (snare);
    edmHint.setVisible (! edm);
    repaint();
}

void HybridKitEditor::timerCallback()
{
    kitView.tick();
    const int st = processor.loadState();
    const float pr = processor.loadProgress();
    if (st != shownState || (st == HybridKitProcessor::stateLoading && std::abs (pr - shownProgress) > 0.01f))
    {
        shownState = st; shownProgress = pr;
        repaint (statusArea);
        kitView.repaint();
    }
}

void HybridKitEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel.darker (0.35f));
    // 上の帯
    auto top = getLocalBounds().removeFromTop (topH).reduced (12, 0);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("HYBRID KIT", top.removeFromLeft (150), juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("live + EDM drums", top.removeFromLeft (110), juce::Justification::centredLeft);

    // 状態（読み込み中・準備 OK・見つからない）
    {
        auto s = statusArea.toFloat();
        const int st = processor.loadState();
        juce::String msg = processor.statusText();
        if (st == HybridKitProcessor::stateLoading)
        {
            const float pr = processor.loadProgress();
            g.setColour (panel.brighter (0.15f));
            g.fillRoundedRectangle (s.withTrimmedTop (s.getHeight() - 5), 2.0f);
            g.setColour (wood);
            g.fillRoundedRectangle (s.withTrimmedTop (s.getHeight() - 5).withWidth (s.getWidth() * pr), 2.0f);
            msg = "Loading " + msg + "  " + juce::String (juce::roundToInt (pr * 100)) + " %";
            g.setColour (text.withAlpha (0.8f));
        }
        else if (st == HybridKitProcessor::stateError) g.setColour (juce::Colour (0xffff7a59));
        else { g.setColour (accent.withAlpha (0.85f)); msg = msg.isEmpty() ? juce::String() : "Ready: " + msg; }
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (msg, statusArea.withTrimmedBottom (6), juce::Justification::centredLeft, 2);
    }

    auto panelBox = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (text.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, r.reduced (12, 6).removeFromTop (16), juce::Justification::topLeft);
    };
    panelBox (piecePanel, juce::String (HybridKitProcessor::groupName (group)) + "  (live)");
    panelBox (edmPanel, "EDM LAYER");
    panelBox (mixerPanel, "MIXER");
    g.setColour (text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Plays Logic's Drum Kit Designer recordings on this Mac. Use it on a Drummer track as is.",
                      mixerPanel.reduced (12, 8).removeFromBottom (32), juce::Justification::bottomLeft, 2);
}

void HybridKitEditor::resized()
{
    auto area = getLocalBounds();
    auto footer = area.removeFromBottom (footerH).reduced (8, 5);
    presetBar.setBounds (footer.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
    footer.removeFromRight (8);
    linkBar.setBounds (footer.withSizeKeepingCentre (footer.getWidth(), plink::LinkBar::height));

    auto top = area.removeFromTop (topH).reduced (12, 9);
    top.removeFromLeft (270);
    kitBox.setBounds (top.removeFromLeft (210));
    top.removeFromLeft (10);
    factoryBox.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (14);
    statusArea = top;

    area.reduce (10, 4);
    auto left = area.removeFromLeft (660);
    area.removeFromLeft (10);
    mixerPanel = area.withTrimmedBottom (4);

    kitView.setBounds (left.removeFromTop (330));
    left.removeFromTop (8);
    auto bottom = left.withTrimmedBottom (4);
    piecePanel = bottom.removeFromLeft (310);
    bottom.removeFromLeft (8);
    edmPanel = bottom;

    auto pp = piecePanel.reduced (10, 8);
    pp.removeFromTop (20);
    const int kw = pp.getWidth() / 3;
    for (auto* k : { &volK, &tuneK, &lenK })
    {
        auto col = pp.removeFromLeft (kw);
        k->label.setBounds (col.removeFromTop (18));
        k->slider.setBounds (col.reduced (4, 2));
    }

    auto ep = edmPanel.reduced (10, 8);
    ep.removeFromTop (20);
    edmHint.setBounds (ep);
    auto bcol = ep.removeFromLeft (100);
    blendK.label.setBounds (bcol.removeFromTop (18));
    blendK.slider.setBounds (bcol.reduced (4, 2));
    auto scol = ep.removeFromLeft (150).reduced (6, 0);
    soundLabel.setBounds (scol.removeFromTop (18));
    soundBox.setBounds (scol.removeFromTop (28).reduced (0, 2));
    scol.removeFromTop (12);
    noteLabel.setBounds (scol.removeFromTop (18));
    noteBox.setBounds (scol.removeFromTop (28).reduced (0, 2));
    auto tcol = ep;
    edmTuneK.label.setBounds (tcol.removeFromTop (18));
    edmTuneK.slider.setBounds (tcol.reduced (4, 2));

    auto mp = mixerPanel.reduced (12, 8);
    mp.removeFromTop (22);
    mp.removeFromBottom (36);
    const int fw = mp.getWidth() / 5;
    for (auto* f : { &closeF, &ohF, &roomF, &bleedF, &outF })
    {
        auto col = mp.removeFromLeft (fw);
        f->label.setBounds (col.removeFromTop (18));
        f->slider.setBounds (col.reduced (6, 2));
    }
}
