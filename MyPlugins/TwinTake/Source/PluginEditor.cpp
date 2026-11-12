#include "PluginEditor.h"

using namespace twinui;

//==============================================================================
void TakesView::update()
{
    const int on = proc.shownOnsets.load(), m = proc.mode(), nv = m == 1 ? 1 : proc.voices();
    if (m != lastMode || nv != lastVoices) { histCount = 0; lastMode = m; lastVoices = nv; }
    if (on != lastOnsets)
    {
        lastOnsets = on;
        for (int v = 0; v < twin::Engine::maxVoices; ++v)
        {
            for (int i = histLen - 1; i > 0; --i) hist[v][i] = hist[v][i - 1];
            float ms = proc.shownDelayMs[v].load();
            if (m == 1) ms -= (float) twin::Engine::guitarLatencyMs;   // GUITAR: 原音（40 ms 遅らせた）からの前後
            hist[v][0] = { ms, proc.shownCents[v].load() };
        }
        histCount = juce::jmin (histLen, histCount + 1);
    }
    repaint();
}

void TakesView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1c1f24));
    g.fillRoundedRectangle (r, 6.0f);
    auto inner = r.reduced (12, 8);
    auto title = inner.removeFromTop (16);
    g.setColour (accent);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("TAKES", title, juce::Justification::topLeft);
    const int m = proc.mode(), nv = m == 1 ? 1 : proc.voices();
    g.setColour (text.withAlpha (0.65f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (m == 1 ? "original on the left, the double on the right - a new timing and tuning on every attack"
                       : "every new note: each double picks a new timing (late), tuning (cents), tone and level",
                title.withTrimmedLeft (60), juce::Justification::topLeft);

    // 横軸: VOCAL は原音から 0〜50 ms 後ろ、GUITAR は原音の前後 −30〜+30 ms
    const float lo = m == 1 ? -30.0f : -5.0f, hi = m == 1 ? 30.0f : 50.0f;
    inner.removeFromTop (6);
    auto axis = inner.removeFromBottom (14);
    auto plot = inner.withTrimmedLeft (30);
    auto xOf = [&] (float ms) { return plot.getX() + plot.getWidth() * (juce::jlimit (lo, hi, ms) - lo) / (hi - lo); };

    // 原音の線（DOUBLE ONLY の間は薄く = 原音は出ていない）
    const float x0 = xOf (0.0f);
    const bool dOnly = proc.doubleOnly();
    const float origA = dOnly ? 0.18f : 0.55f;
    g.setColour (text.withAlpha (origA));
    g.fillRect (juce::Rectangle<float> (x0 - 1.0f, plot.getY(), 2.0f, plot.getHeight()));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (dOnly ? "ORIGINAL (muted)" : "ORIGINAL", juce::Rectangle<float> (x0 - 60, axis.getY(), 120, 14), juce::Justification::centred);
    if (m == 0)
    {
        // 12 ms より近いと、くし形の音色（フランジャーのような音）になるので使わない
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (x0 + 1.0f, plot.getY(), xOf (12.0f) - x0 - 1.0f, plot.getHeight()));
    }
    else
    {
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (xOf (-10.0f), plot.getY(), xOf (10.0f) - xOf (-10.0f), plot.getHeight()));
        g.setColour (text.withAlpha (origA));
        g.fillRect (juce::Rectangle<float> (x0 - 1.0f, plot.getY(), 2.0f, plot.getHeight()));
    }
    g.setColour (text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (juce::roundToInt (lo)) + " ms", axis, juce::Justification::centredLeft);
    g.drawText ("+" + juce::String (juce::roundToInt (hi)) + " ms", axis, juce::Justification::centredRight);

    static const char* names1[] = { "C" };
    static const char* names2[] = { "L", "R" };
    static const char* names4[] = { "L", "R", "L2", "R2" };
    const char** names = m == 1 ? names2 + 1 : (nv == 1 ? names1 : (nv == 2 ? names2 : names4));
    const float laneH = plot.getHeight() / (float) nv;
    for (int v = 0; v < nv; ++v)
    {
        auto lane = juce::Rectangle<float> (plot.getX(), plot.getY() + (float) v * laneH, plot.getWidth(), laneH);
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (names[v], juce::Rectangle<float> (inner.getX(), lane.getY(), 26, lane.getHeight()), juce::Justification::centred);
        const auto col = (v % 2 == 0) ? accent : accent2;
        const float cy = lane.getCentreY();
        for (int i = histCount - 1; i >= 0; --i)
        {
            const auto& d = hist[v][i];
            // 縦の位置で音程（上 = 高め、±9 セントで端）
            const float y = cy - (lane.getHeight() * 0.38f) * juce::jlimit (-1.0f, 1.0f, d.cents / 9.0f);
            const float a = i == 0 ? 1.0f : 0.45f * (1.0f - (float) i / histLen);
            const float sz = i == 0 ? 9.0f : 6.0f;
            g.setColour (col.withAlpha (a));
            g.fillEllipse (xOf (d.ms) - sz / 2, y - sz / 2, sz, sz);
            if (i == 0)
            {
                g.setColour (text.withAlpha (0.8f));
                g.setFont (juce::FontOptions (10.5f));
                const juce::String t = (d.ms >= 0 ? "+" : "") + juce::String (d.ms, 1) + " ms  " + (d.cents >= 0 ? "+" : "") + juce::String (d.cents, 1) + " c";
                g.drawText (t, juce::Rectangle<float> (xOf (d.ms) + 8, y - 8, 120, 16), juce::Justification::centredLeft);
            }
        }
    }
    if (m == 0 && proc.shownGate.load() < 0.97f)
    {
        g.setColour (warm.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("s / t / breaths are not doubled", title, juce::Justification::topRight);
    }
}

//==============================================================================
TwinTakeEditor::TwinTakeEditor (TwinTakeProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff323840));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262b31));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20242a));
    lnf.setColour (juce::ComboBox::textColourId, text);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4149));
    lnf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2025));
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
    lnf.setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff20242a));
    lnf.setColour (juce::TooltipWindow::textColourId, text);
    setLookAndFeel (&lnf);

    setupKnob (dbl, "double", "DOUBLE", "Level of each double compared to the original. The original is never changed. Far left = OFF");
    setupKnob (loose, "loose", "LOOSE", "TIGHT <-> LOOSE: how different each take is (timing, a few cents of tuning, tone and level all together)");
    setupKnob (width, "width", "WIDTH", "How far left and right the doubles sit (GUITAR: the original goes left, the double right)");
    setupKnob (lowKeep, "lowKeep", "LOW KEEP", "Below this the sound is not doubled (keeps lows tight and mono-safe). GUITAR: the lows of the original stay in the middle. Far left = OFF");
    dbl.slider.setDoubleClickReturnValue (true, -6.0);

    const char* modeNames[] = { "VOCAL", "GUITAR" };
    const char* modeTips[] = { "VOCAL: original in the middle, doubles on the sides, always a little late. No latency. Consonants and breaths are not doubled",
                               "GUITAR: original on the left, the double on the right, a bit before or after. Adds 40 ms latency (Logic compensates)" };
    for (int i = 0; i < 2; ++i)
    {
        modeBtn[i].setButtonText (modeNames[i]);
        modeBtn[i].setTooltip (modeTips[i]);
        modeBtn[i].setClickingTogglesState (false);
        modeBtn[i].onClick = [this, i] { setChoice ("mode", i); };
        addAndMakeVisible (modeBtn[i]);
    }
    const char* vNames[] = { "1", "2", "4" };
    for (int i = 0; i < 3; ++i)
    {
        voicesBtn[i].setButtonText (vNames[i]);
        voicesBtn[i].setTooltip ("Number of doubles (VOCAL). 1 = one in the middle, 2 = left and right, 4 = a stack (each is a bit quieter)");
        voicesBtn[i].onClick = [this, i] { setChoice ("voices", i); };
        addAndMakeVisible (voicesBtn[i]);
    }
    for (auto* l : { &voicesLabel, &takeLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }
    voicesLabel.setText ("VOICES", juce::dontSendNotification);
    takeLabel.setText ("TAKE", juce::dontSendNotification);
    takeSlider.setSliderStyle (juce::Slider::IncDecButtons);
    takeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 26);
    takeSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    takeSlider.setTooltip ("Pick a different performance (1 to 8). Same take = the same doubles every time you play or bounce");
    addAndMakeVisible (takeSlider);
    takeAtt = std::make_unique<Attachment> (processor.apvts, "take", takeSlider);

    doubleOnlyBtn.setButtonText ("DOUBLE ONLY");
    doubleOnlyBtn.setClickingTogglesState (true);
    doubleOnlyBtn.setTooltip ("Mute the original and output only the doubles - for an AUX send (post-fader, DOUBLE at 0 dB, set the amount with the AUX fader). "
                              "Saved with the song, not changed by presets or UNDO");
    doubleOnlyBtn.setColour (juce::TextButton::buttonOnColourId, warm);
    addAndMakeVisible (doubleOnlyBtn);
    doubleOnlyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "doubleOnly", doubleOnlyBtn);

    monoBtn.setButtonText ("MONO");
    monoBtn.setTooltip ("Hold to listen in mono (phone speaker check). Not saved and not bounced");
    monoBtn.onStateChange = [this] { processor.monoListen.store (monoBtn.isDown()); };
    addAndMakeVisible (monoBtn);

    factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    for (int i = 0; i < TwinTakeProcessor::numFactoryPresets(); ++i) factoryBox.addItem (TwinTakeProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int id = factoryBox.getSelectedId();
        if (id > 0) processor.loadFactoryPreset (id - 1);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
        factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    };
    addAndMakeVisible (factoryBox);
    addAndMakeVisible (takesView);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.status = [this] (bool& lit) -> juce::String { lit = processor.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    startTimerHz (30);
    setSize (900, topH + 300 + footerH);
}

TwinTakeEditor::~TwinTakeEditor()
{
    processor.monoListen.store (false);
    setLookAndFeel (nullptr);
}

void TwinTakeEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& tip)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    s.setTooltip (tip);
    addAndMakeVisible (s);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, s);
    s.updateText();
}

void TwinTakeEditor::setChoice (const juce::String& paramId, int index)
{
    if (auto* prm = processor.apvts.getParameter (paramId))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

void TwinTakeEditor::timerCallback()
{
    const int m = processor.mode(), nv = processor.voices();
    for (int i = 0; i < 2; ++i) modeBtn[i].setToggleState (m == i, juce::dontSendNotification);
    const int vi = nv == 4 ? 2 : (nv == 2 ? 1 : 0);
    for (int i = 0; i < 3; ++i)
    {
        voicesBtn[i].setToggleState (vi == i, juce::dontSendNotification);
        voicesBtn[i].setEnabled (m == 0);
        voicesBtn[i].setAlpha (m == 0 ? 1.0f : 0.35f);
    }
    voicesLabel.setAlpha (m == 0 ? 1.0f : 0.35f);
    takesView.update();
    repaint (0, 0, getWidth(), topH);
}

void TwinTakeEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff1d2025));
    g.fillRect (0, 0, getWidth(), topH - 2);
    g.setColour (accent);
    g.fillRect (0, topH - 2, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("TWIN TAKE", 16, 0, 200, topH - 2, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("double tracker", 170, 2, 120, topH - 4, juce::Justification::centredLeft);
    if (processor.doubleOnly())
    {   // 挿したまま ON にして「歌が消えた」と思わないように
        g.setColour (warm);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DOUBLE ONLY: original muted (for an AUX send)", 270, 2, 330, topH - 4, juce::Justification::centredLeft);
    }
    else if (processor.monoTrack.load())
    {
        g.setColour (warm);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MONO TRACK: insert as \"Mono -> Stereo\" so the doubles can spread", 270, 2, 330, topH - 4, juce::Justification::centredLeft);
    }

    auto section = [&] (juce::Rectangle<int> r)
    {
        g.setColour (juce::Colour (0xff1b1e23));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    };
    section (knobArea);
    section (sideArea);
}

void TwinTakeEditor::placeKnob (Knob& k, juce::Rectangle<int> col)
{
    k.label.setBounds (col.removeFromTop (18));
    k.slider.setBounds (col.reduced (4, 0));
}

void TwinTakeEditor::resized()
{
    // 下の欄（2 段）
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
    // 上の帯: MODE | MONO | 内蔵プリセット
    factoryBox.setBounds (getWidth() - 216, 11, 200, 28);
    monoBtn.setBounds (factoryBox.getX() - 78, 11, 68, 28);
    modeBtn[1].setBounds (monoBtn.getX() - 98, 11, 88, 28);
    modeBtn[0].setBounds (modeBtn[1].getX() - 86, 11, 86, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (topH + 10).reduced (12, 0);
    auto row = area.removeFromTop (168);
    sideArea = row.removeFromRight (220);
    row.removeFromRight (10);
    knobArea = row;
    {
        auto r = knobArea.reduced (8, 10);
        const int w = r.getWidth() / 4;
        placeKnob (dbl, r.removeFromLeft (w));
        placeKnob (loose, r.removeFromLeft (w));
        placeKnob (width, r.removeFromLeft (w));
        placeKnob (lowKeep, r);
    }
    {
        auto r = sideArea.reduced (14, 10);
        voicesLabel.setBounds (r.removeFromTop (16));
        auto vb = r.removeFromTop (30);
        const int bw = vb.getWidth() / 3;
        for (int i = 0; i < 3; ++i) voicesBtn[i].setBounds (vb.removeFromLeft (bw).reduced (3, 2));
        r.removeFromTop (6);
        takeLabel.setBounds (r.removeFromTop (16));
        takeSlider.setBounds (r.removeFromTop (30).reduced (20, 2));
        r.removeFromTop (10);
        doubleOnlyBtn.setBounds (r.removeFromTop (28).reduced (3, 0));
    }
    area.removeFromTop (10);
    takesArea = area.removeFromTop (112);
    takesView.setBounds (takesArea);
}
