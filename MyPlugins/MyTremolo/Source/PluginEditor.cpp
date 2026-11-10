#include "PluginEditor.h"

using namespace mytremui;

//==============================================================================
// 揺れ方の表示
void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111415));
    g.fillRoundedRectangle (r, 6.0f);
    auto a = r.reduced (10.0f, 12.0f);

    const auto k = proc.currentKnobs();
    const auto pt = proc.getPattern();
    const double len = k.rhythm == mytrem::kRhythmPattern ? pt.lengthBeats() : std::max (4.0, mytrem::rhythmBeats (k.rhythm));
    const int beats = (int) std::lround (len);

    // 拍の線
    for (int b = 0; b <= beats; ++b)
    {
        const float x = a.getX() + a.getWidth() * (float) b / (float) len;
        const bool bar = b % (k.rhythm == mytrem::kRhythmPattern ? pt.beatsPerBar : 4) == 0;
        g.setColour (bar ? text.withAlpha (0.25f) : text.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
    }
    g.setColour (text.withAlpha (0.08f));
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());

    mytrem::EventList ev; ev.build (pt);
    float ctab[mytrem::kShapeLen];
    proc.getCustomShape().render (ctab, mytrem::kShapeLen, k.smoothing, k.smoothMode);
    juce::Path path;
    const int n = juce::jmax (64, (int) a.getWidth());
    for (int i = 0; i <= n; ++i)
    {
        const double b = len * i / n;
        const float v = juce::jlimit (0.0f, 1.0f, mytrem::previewGain (b, k, ev, pt, ctab));
        const float x = a.getX() + a.getWidth() * (float) i / (float) n;
        const float y = a.getBottom() - a.getHeight() * v;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    juce::Path fill (path);
    fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
    g.setColour (accent.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // 再生位置
    const float ph = proc.engine.shownPhase.load();
    const float px = a.getX() + a.getWidth() * ph;
    g.setColour (warm.withAlpha (0.8f));
    g.drawLine (px, a.getY(), px, a.getBottom(), 1.5f);

    g.setColour (text.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f));
    const juce::String info = juce::String (proc.shownBpm.load(), 1) + " BPM  " + (k.sync ? "SYNC" : "FREE") + "   "
                            + (k.rhythm == mytrem::kRhythmPattern ? juce::String ("PATTERN") : juce::String (mytrem::rhythmName (k.rhythm)))
                            + " / " + juce::String (k.shape < mytrem::kNumShapes ? mytrem::shapeName (k.shape) : "Custom");
    g.drawText (info, getLocalBounds().reduced (10, 2).removeFromTop (14), juce::Justification::topRight);
}

//==============================================================================
// 形の編集
bool ShapeEditor::editable() const { return proc.currentKnobs().shape == mytrem::kNumShapes; }

juce::Point<float> ShapeEditor::toScreen (float x, float y) const
{
    auto a = getLocalBounds().toFloat().reduced (12.0f, 14.0f);
    return { a.getX() + a.getWidth() * x, a.getBottom() - a.getHeight() * y };
}
juce::Point<float> ShapeEditor::fromScreen (juce::Point<float> p) const
{
    auto a = getLocalBounds().toFloat().reduced (12.0f, 14.0f);
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
}

void ShapeEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111415));
    g.fillRoundedRectangle (r, 6.0f);
    const auto k = proc.currentKnobs();
    float tab[mytrem::kShapeLen];
    const bool custom = k.shape == mytrem::kNumShapes;
    const auto cs = dragIdx >= 0 ? work : proc.getCustomShape();
    if (custom)
        cs.render (tab, mytrem::kShapeLen, proc.apvts.getRawParameterValue ("smoothing")->load() / 100.0f,
                   (int) proc.apvts.getRawParameterValue ("smoothMode")->load());
    else
        std::copy (mytrem::kShapes[k.shape], mytrem::kShapes[k.shape] + mytrem::kShapeLen, tab);

    g.setColour (text.withAlpha (0.08f));
    for (int i = 1; i < 4; ++i) { auto p0 = toScreen (i * 0.25f, 0), p1 = toScreen (i * 0.25f, 1); g.drawLine (p0.x, p0.y, p1.x, p1.y); }
    juce::Path path;
    for (int i = 0; i <= mytrem::kShapeLen; ++i)
    {
        auto p = toScreen ((float) i / mytrem::kShapeLen, tab[i % mytrem::kShapeLen]);
        if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
    }
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    if (custom)
    {
        for (int i = 0; i < cs.n; ++i)
        {
            auto p = toScreen (cs.x[(size_t) i], cs.y[(size_t) i]);
            g.setColour (warm);
            g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
        }
        g.setColour (text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("click = add point   drag = move   option-click = delete", getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);
    }
    else
    {
        g.setColour (text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("choose SHAPE: Custom (or press EDIT) to draw your own", getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);
    }
}

void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! editable()) return;
    work = proc.getCustomShape();
    const auto pos = e.position;
    int hit = -1;
    for (int i = 0; i < work.n; ++i)
        if (toScreen (work.x[(size_t) i], work.y[(size_t) i]).getDistanceFrom (pos) < 8.0f) hit = i;
    if (e.mods.isAltDown())
    {
        if (hit > 0 && hit < work.n - 1)
        {
            for (int i = hit; i < work.n - 1; ++i) { work.x[(size_t) i] = work.x[(size_t) i + 1]; work.y[(size_t) i] = work.y[(size_t) i + 1]; }
            --work.n;
            proc.setCustomShape (work);
        }
        dragIdx = -1;
        repaint();
        return;
    }
    if (hit < 0 && work.n < mytrem::CustomShape::kMaxPoints)
    {
        const auto v = fromScreen (pos);
        int at = 1;
        while (at < work.n && work.x[(size_t) at] < v.x) ++at;
        for (int i = work.n; i > at; --i) { work.x[(size_t) i] = work.x[(size_t) i - 1]; work.y[(size_t) i] = work.y[(size_t) i - 1]; }
        work.x[(size_t) at] = v.x; work.y[(size_t) at] = v.y; ++work.n;
        hit = at;
        proc.setCustomShape (work);
    }
    dragIdx = hit;
    repaint();
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0) return;
    auto v = fromScreen (e.position);
    if (dragIdx == 0) v.x = 0.0f;
    else if (dragIdx == work.n - 1) v.x = 1.0f;
    else v.x = juce::jlimit (work.x[(size_t) dragIdx - 1] + 0.002f, work.x[(size_t) dragIdx + 1] - 0.002f, v.x);
    work.x[(size_t) dragIdx] = v.x; work.y[(size_t) dragIdx] = v.y;
    proc.setCustomShape (work);
    repaint();
}

void ShapeEditor::mouseUp (const juce::MouseEvent&) { dragIdx = -1; repaint(); }

//==============================================================================
// リズムの升目
int PatternEditor::cellAt (juce::Point<float> p) const
{
    const auto pt = proc.getPattern();
    const int n = pt.numCells();
    auto a = getLocalBounds().toFloat().reduced (8.0f, 10.0f);
    if (! a.contains (p)) return -1;
    const int rows = pt.bars;
    const int perRow = n / rows;
    const int row = juce::jlimit (0, rows - 1, (int) ((p.y - a.getY()) / (a.getHeight() / rows)));
    const int col = juce::jlimit (0, perRow - 1, (int) ((p.x - a.getX()) / (a.getWidth() / perRow)));
    return row * perRow + col;
}

void PatternEditor::setCell (int idx, uint8_t v)
{
    auto pt = proc.getPattern();
    if (idx < 0 || idx >= pt.numCells()) return;
    if (v == 2 && (idx == 0 || pt.cell[(size_t) idx - 1] == 0)) v = 1;   // 前に何もなければ「続き」にはできない
    if (pt.cell[(size_t) idx] == v) return;
    pt.cell[(size_t) idx] = v;
    // 休みにしたら、後ろの「続き」も休みに
    if (v == 0) for (int j = idx + 1; j < pt.numCells() && pt.cell[(size_t) j] == 2; ++j) pt.cell[(size_t) j] = 0;
    proc.setPattern (pt);
    repaint();
}

void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position);
    if (c < 0) return;
    // 升目を触ったら、RHYTHM を Pattern にする（そうしないと描いても音が変わらない）
    if (proc.currentKnobs().rhythm != mytrem::kRhythmPattern)
        if (auto* prm = proc.apvts.getParameter ("rhythm")) { prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); prm->endChangeGesture(); }
    const auto pt = proc.getPattern();
    const uint8_t cur = pt.cell[(size_t) c];
    if (e.mods.isShiftDown() || e.mods.isPopupMenu()) paintValue = cur == 2 ? 1 : 2;
    else paintValue = cur == 0 ? 1 : 0;
    lastCell = c;
    setCell (c, paintValue);
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position);
    if (c < 0 || c == lastCell) return;
    lastCell = c;
    setCell (c, paintValue);
}

void PatternEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111415));
    g.fillRoundedRectangle (r, 6.0f);
    const auto pt = proc.getPattern();
    const int n = pt.numCells();
    auto a = r.reduced (8.0f, 10.0f);
    const int rows = pt.bars, perRow = n / rows;
    const float cw = a.getWidth() / perRow, ch = a.getHeight() / rows;
    const double gb = mytrem::gridBeats (pt.grid);
    for (int i = 0; i < n; ++i)
    {
        const int row = i / perRow, col = i % perRow;
        auto cell = juce::Rectangle<float> (a.getX() + col * cw, a.getY() + row * ch, cw, ch).reduced (1.0f, 3.0f);
        const double beatPos = col * gb;
        const bool onBeat = std::abs (beatPos - std::round (beatPos)) < 1e-6;
        const uint8_t v = pt.cell[(size_t) i];
        if (v == 0) g.setColour (onBeat ? dim.brighter (0.25f) : dim);
        else if (v == 1) g.setColour (accent);
        else g.setColour (accent.withAlpha (0.55f));
        if (v == 2) cell = cell.withTrimmedLeft (-2.0f);   // 前とつながって見えるように
        g.fillRoundedRectangle (cell, 2.0f);
    }
    const float ph = proc.engine.shownPhase.load();
    if (proc.currentKnobs().rhythm == mytrem::kRhythmPattern)
    {
        const float pos = ph * n;
        const int row = juce::jlimit (0, rows - 1, (int) pos / perRow);
        const float x = a.getX() + (pos - row * perRow) * cw;
        g.setColour (warm.withAlpha (0.9f));
        g.drawLine (x, a.getY() + row * ch, x, a.getY() + (row + 1) * ch, 2.0f);
    }
}

//==============================================================================
MyTremoloEditor::MyTremoloEditor (MyTremoloProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff333a3b));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff283031));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff232a2b));
    lnf.setColour (juce::ComboBox::textColourId, text);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4344));
    lnf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1e2324));
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
    setLookAndFeel (&lnf);

    setupKnob (input, "input", "INPUT", "Level into the tremolo (drives the STYLE saturation harder)");
    setupKnob (depth, "depth", "DEPTH", "How deep the volume moves. 100 % = fully off at the bottom of the wave");
    setupKnob (groove, "groove", "GROOVE", "Left = shuffle, right = swing (stretches every first of two beats)");
    setupKnob (feel, "feel", "FEEL", "Left = rush ahead of the beat, right = drag behind the beat");
    setupKnob (accentK, "accent", "ACCENT", "Right = only the 1 (every 2 beats) pulses, left = the 1 drops out");
    setupKnob (rate, "rate", "RATE", "Own tempo in BPM (used when SYNC is off). TAP to set it by tapping");
    setupKnob (width, "width", "WIDTH", "Moves the right side ahead of the left. 100 % = opposite = auto-pan");
    setupKnob (lowKeep, "lowKeep", "LOW KEEP", "Lows below this frequency are not moved (keeps bass and kick steady)");
    setupKnob (output, "output", "OUTPUT", "Level after the tremolo (into the STYLE output stage)");
    setupKnob (thr, "threshold", "THRESHOLD", "Input level where the envelope starts to act (the ring shows the input)");
    setupKnob (att, "attack", "ATTACK", "How fast RATE MOD / DEPTH MOD reach their full amount");
    setupKnob (rel, "release", "RELEASE", "How fast they go back after the input drops below THRESHOLD");
    setupKnob (rateMod, "rateMod", "RATE MOD", "Louder input = faster (+) or slower (-) in octaves. +4 and up = ring-mod tones");
    setupKnob (depthMod, "depthMod", "DEPTH MOD", "+ = deeper when loud (shallow when quiet), - = shallower when loud");
    setupKnob (smooth, "smoothing", "SMOOTH", "0 = steps between points, 100 = fully curved");

    setupBox (styleBox, styleAtt, "style", "Saturation colour. Digital = clean with a hard ceiling");
    setupBox (rhythmBox, rhythmAtt, "rhythm", "Note length of one wave. Pattern = your own rhythm (RHYTHM tab)");
    setupBox (shapeBox, shapeAtt, "shape", "Wave shape. Custom = your own (SHAPE tab)");
    setupBox (envModeBox, envModeAtt, "envMode", "Env = more input, more change. Gate = full change once above THRESHOLD");
    setupBox (smoothModeBox, smoothModeAtt, "smoothMode", "Curve between points");

    syncBtn.setButtonText ("SYNC");
    syncBtn.setClickingTogglesState (true);
    syncBtn.setTooltip ("On = follow Logic's tempo and bar position. Off = run at RATE");
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<BtnAttachment> (processor.apvts, "sync", syncBtn);
    tapBtn.setButtonText ("TAP");
    tapBtn.setTooltip ("Tap the beat to set RATE (turns SYNC off)");
    tapBtn.onClick = [this]
    {
        if (auto* s = processor.apvts.getParameter ("sync")) if (s->getValue() > 0.5f) { s->beginChangeGesture(); s->setValueNotifyingHost (0.0f); s->endChangeGesture(); }
        processor.tapTempo();
    };
    addAndMakeVisible (tapBtn);

    factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    for (int i = 0; i < MyTremoloProcessor::numFactoryPresets(); ++i) factoryBox.addItem (MyTremoloProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int i = factoryBox.getSelectedId() - 1;
        if (i >= 0) processor.loadFactoryPreset (i);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
        factoryBox.setText ("FACTORY PRESETS", juce::dontSendNotification);
    };
    addAndMakeVisible (factoryBox);

    const char* tabs[3] = { "SHAPE", "RHYTHM", "ENVELOPE" };
    for (int i = 0; i < 3; ++i)
    {
        tabBtn[i].setButtonText (tabs[i]);
        tabBtn[i].setClickingTogglesState (false);
        tabBtn[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabBtn[i]);
    }

    addAndMakeVisible (wave);
    addChildComponent (shapeEd);
    addChildComponent (patternEd);

    copyToCustomBtn.setButtonText ("EDIT");
    copyToCustomBtn.setTooltip ("Copy the current shape into Custom so you can change its points");
    copyToCustomBtn.onClick = [this]
    {
        const auto k = processor.currentKnobs();
        if (k.shape < mytrem::kNumShapes)
        {
            mytrem::CustomShape c; c.n = 17;
            for (int i = 0; i < c.n; ++i)
            {
                const float x = (float) i / 16.0f;
                c.x[(size_t) i] = x;
                c.y[(size_t) i] = mytrem::kShapes[k.shape][juce::jlimit (0, mytrem::kShapeLen - 1, (int) std::lround (x * mytrem::kShapeLen) % mytrem::kShapeLen)];
            }
            processor.setCustomShape (c);
            if (auto* s = processor.apvts.getParameter ("smoothMode")) { s->beginChangeGesture(); s->setValueNotifyingHost (s->convertTo0to1 (0.0f)); s->endChangeGesture(); }
        }
        if (auto* s = processor.apvts.getParameter ("shape")) { s->beginChangeGesture(); s->setValueNotifyingHost (1.0f); s->endChangeGesture(); }
    };
    shapeSaveBtn.setButtonText ("SAVE");  shapeSaveBtn.onClick = [this] { saveFile (true); };
    shapeLoadBtn.setButtonText ("LOAD");  shapeLoadBtn.onClick = [this] { loadFile (true); };
    shapeResetBtn.setButtonText ("RESET"); shapeResetBtn.setTooltip ("Custom back to a plain sine (3 points)");
    shapeResetBtn.onClick = [this] { mytrem::CustomShape c; c.setSine(); processor.setCustomShape (c); };
    for (auto* b : { &copyToCustomBtn, &shapeSaveBtn, &shapeLoadBtn, &shapeResetBtn }) addChildComponent (b);

    for (int i = 1; i <= 4; ++i) barsBox.addItem (juce::String (i) + (i == 1 ? " bar" : " bars"), i);
    for (int i = 2; i <= 7; ++i) beatsBox.addItem (juce::String (i) + " beats", i);
    for (int i = 0; i < mytrem::kNumGrids; ++i) gridBox.addItem (juce::String ("grid ") + mytrem::gridName (i), i + 1);
    auto patChange = [this]
    {
        auto pt = processor.getPattern();
        const int oldCells = pt.numCells();
        const double oldGrid = mytrem::gridBeats (pt.grid);
        auto old = pt;
        pt.bars = juce::jmax (1, barsBox.getSelectedId());
        pt.beatsPerBar = juce::jmax (2, beatsBox.getSelectedId());
        pt.grid = juce::jmax (0, gridBox.getSelectedId() - 1);
        // 升目が変わったら、同じ時刻に近い升目に写す
        pt.cell.fill (0);
        const double g = mytrem::gridBeats (pt.grid);
        for (int i = 0; i < oldCells; ++i)
            if (old.cell[(size_t) i] != 0)
            {
                const int j = (int) std::lround (i * oldGrid / g);
                if (j < pt.numCells()) pt.cell[(size_t) j] = (uint8_t) juce::jmax ((int) pt.cell[(size_t) j], (int) old.cell[(size_t) i] == 1 ? 1 : 2);
            }
        for (int j = 0; j < pt.numCells(); ++j) if (pt.cell[(size_t) j] == 2 && (j == 0 || pt.cell[(size_t) j - 1] == 0)) pt.cell[(size_t) j] = 1;
        processor.setPattern (pt);
    };
    barsBox.onChange = patChange; beatsBox.onChange = patChange; gridBox.onChange = patChange;
    patSaveBtn.setButtonText ("SAVE"); patSaveBtn.onClick = [this] { saveFile (false); };
    patLoadBtn.setButtonText ("LOAD"); patLoadBtn.onClick = [this] { loadFile (false); };
    patClearBtn.setButtonText ("CLEAR");
    patClearBtn.onClick = [this] { auto pt = processor.getPattern(); pt.cell.fill (0); processor.setPattern (pt); };
    patFillBtn.setButtonText ("FILL");
    patFillBtn.onClick = [this] { auto pt = processor.getPattern(); for (int i = 0; i < pt.numCells(); ++i) pt.cell[(size_t) i] = 1; processor.setPattern (pt); };
    for (auto* c : std::initializer_list<juce::Component*> { &barsBox, &beatsBox, &gridBox, &patSaveBtn, &patLoadBtn, &patClearBtn, &patFillBtn }) addChildComponent (c);

    hintLabel.setColour (juce::Label::textColourId, text.withAlpha (0.55f));
    hintLabel.setFont (juce::FontOptions (11.0f));
    addChildComponent (hintLabel);

    // 下の欄
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): make the tremolo this much shallower while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<Attachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Fader Pilot to calm the tremolo while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing - tremolo shallower" : "Fader Pilot found - voice not singing";
    };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    syncPatternBoxes();
    showTab (0);
    startTimerHz (30);
    setSize (1060, 560 + footerH);
}

MyTremoloEditor::~MyTremoloEditor()
{
    setLookAndFeel (nullptr);
}

void MyTremoloEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 18);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, k.slider);
    if (paramId == "attack" || paramId == "release")
        k.slider.textFromValueFunction = [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 2) + " s" : juce::String (v, v < 10 ? 1 : 0) + " ms"; };
    if (paramId == "rateMod")
        k.slider.textFromValueFunction = [] (double v) { return (v > 0.005 ? "+" : "") + juce::String (v, 2) + " oct"; };
    if (paramId == "threshold")
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    if (paramId == "rate")
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
    k.slider.updateText();
}

void MyTremoloEditor::setupBox (juce::ComboBox& b, std::unique_ptr<BoxAttachment>& att, const juce::String& paramId, const juce::String& tip)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        b.addItemList (c->choices, 1);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<BoxAttachment> (processor.apvts, paramId, b);
}

void MyTremoloEditor::showTab (int t)
{
    tab = t;
    for (int i = 0; i < 3; ++i) tabBtn[i].setToggleState (i == t, juce::dontSendNotification);
    const bool sh = t == 0, rh = t == 1, en = t == 2;
    shapeEd.setVisible (sh);
    for (auto* c : std::initializer_list<juce::Component*> { &copyToCustomBtn, &shapeSaveBtn, &shapeLoadBtn, &shapeResetBtn, &smooth.slider, &smooth.label, &smoothModeBox }) c->setVisible (sh);
    patternEd.setVisible (rh);
    for (auto* c : std::initializer_list<juce::Component*> { &barsBox, &beatsBox, &gridBox, &patSaveBtn, &patLoadBtn, &patClearBtn, &patFillBtn }) c->setVisible (rh);
    for (auto* k : { &thr, &att, &rel, &rateMod, &depthMod }) { k->slider.setVisible (en); k->label.setVisible (en); }
    envModeBox.setVisible (en);
    hintLabel.setVisible (rh);
    hintLabel.setText (rh ? "click = play / rest   shift-click or right-click = hold the previous one longer   (RHYTHM switches to Pattern)" : "",
                       juce::dontSendNotification);
    resized();
    repaint();
}

void MyTremoloEditor::syncPatternBoxes()
{
    const auto pt = processor.getPattern();
    barsBox.setSelectedId (pt.bars, juce::dontSendNotification);
    beatsBox.setSelectedId (pt.beatsPerBar, juce::dontSendNotification);
    gridBox.setSelectedId (pt.grid + 1, juce::dontSendNotification);
}

juce::File MyTremoloEditor::libFolder (bool shape) const
{
    auto f = processor.myPresets.folder().getChildFile (shape ? "Shapes" : "Rhythms");
    f.createDirectory();
    return f;
}

void MyTremoloEditor::saveFile (bool shape)
{
    chooser = std::make_unique<juce::FileChooser> (shape ? "Save shape" : "Save rhythm", libFolder (shape), shape ? "*.myshape" : "*.myrhythm");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, shape] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension (shape ? "myshape" : "myrhythm");
            f.replaceWithText (shape ? MyTremoloProcessor::shapeToString (processor.getCustomShape())
                                     : MyTremoloProcessor::patternToString (processor.getPattern()));
        });
}

void MyTremoloEditor::loadFile (bool shape)
{
    chooser = std::make_unique<juce::FileChooser> (shape ? "Load shape" : "Load rhythm", libFolder (shape), shape ? "*.myshape" : "*.myrhythm");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, shape] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const auto s = f.loadFileAsString();
            if (shape)
            {
                mytrem::CustomShape c;
                if (MyTremoloProcessor::shapeFromString (s, c))
                {
                    processor.setCustomShape (c);
                    if (auto* prm = processor.apvts.getParameter ("shape")) { prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); prm->endChangeGesture(); }
                }
            }
            else
            {
                mytrem::Pattern pt;
                if (MyTremoloProcessor::patternFromString (s, pt))
                {
                    processor.setPattern (pt);
                    if (auto* prm = processor.apvts.getParameter ("rhythm")) { prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); prm->endChangeGesture(); }
                }
            }
        });
}

void MyTremoloEditor::timerCallback()
{
    if (processor.editVersion.load() != lastEditVersion)
    {
        lastEditVersion = processor.editVersion.load();
        syncPatternBoxes();
        shapeEd.repaint();
        patternEd.repaint();
    }
    wave.repaint();
    if (tab == 0) shapeEd.repaint();
    if (tab == 1) patternEd.repaint();
    const float in = processor.engine.shownInDb.load();
    inMeter = in > inMeter ? in : inMeter - 1.5f;
    envMeter = processor.engine.shownEnv.load();
    if (tab == 2) repaint (thr.slider.getBounds().expanded (6));
    rate.slider.setAlpha (processor.apvts.getRawParameterValue ("sync")->load() > 0.5f ? 0.45f : 1.0f);
}

void MyTremoloEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff202627));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (accent);
    g.fillRect (0, 48, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MY TREMOLO", 16, 0, 260, 48, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("STYLE", styleBox.getX() - 50, 0, 44, 48, juce::Justification::centredRight);
    g.drawText ("RHYTHM", rhythmBox.getX(), rhythmBox.getY() - 15, 100, 14, juce::Justification::centredLeft);
    g.drawText ("SHAPE", shapeBox.getX(), shapeBox.getY() - 15, 100, 14, juce::Justification::centredLeft);

    if (tab == 2)
    {
        // THRESHOLD のまわりに入力の大きさ（赤 = しきい値を超えている）
        auto b = thr.slider.getBounds().toFloat().withTrimmedBottom (20).reduced (2);
        const float sz = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.withSizeKeepingCentre (sz, sz);
        const float norm = juce::jlimit (0.0f, 1.0f, (inMeter + 80.0f) / 80.0f);
        const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
        juce::Path arc; arc.addCentredArc (c.getCentreX(), c.getCentreY(), sz / 2 + 3, sz / 2 + 3, 0, a0, a0 + (a1 - a0) * norm, true);
        const float thrDb = processor.apvts.getRawParameterValue ("threshold")->load();
        g.setColour (inMeter > thrDb ? juce::Colour (0xffe0584a) : text.withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (3.0f));
        // エンベロープの量
        auto m = juce::Rectangle<float> ((float) depthMod.slider.getRight() + 16, (float) thr.slider.getY() + 8, 12, (float) thr.slider.getHeight() - 16);
        g.setColour (juce::Colour (0xff0f1213)); g.fillRect (m);
        g.setColour (warm); g.fillRect (m.withTrimmedTop (m.getHeight() * (1.0f - envMeter)));
        g.setColour (text.withAlpha (0.5f));
        g.drawText ("ENV", (int) m.getX() - 10, (int) m.getBottom() + 2, 32, 12, juce::Justification::centred);
    }
}

void MyTremoloEditor::resized()
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
    styleBox.setBounds (factoryBox.getX() - 180, 10, 150, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (56).reduced (12, 4);
    // つまみの段
    auto row = area.removeFromTop (138);
    Knob* ks[9] = { &input, &depth, &groove, &feel, &accentK, &rate, &width, &lowKeep, &output };
    const int w = row.getWidth() / 9;
    for (int i = 0; i < 9; ++i)
    {
        auto col = row.removeFromLeft (w);
        ks[i]->label.setBounds (col.removeFromTop (18));
        if (ks[i] == &rate)
        {
            auto btns = col.removeFromBottom (24);
            syncBtn.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (3, 1));
            tapBtn.setBounds (btns.reduced (3, 1));
        }
        ks[i]->slider.setBounds (col.reduced (4, 0));
    }
    area.removeFromTop (20);
    // 選ぶ段
    auto sel = area.removeFromTop (30);
    rhythmBox.setBounds (sel.removeFromLeft (150));
    sel.removeFromLeft (10);
    shapeBox.setBounds (sel.removeFromLeft (170));
    sel.removeFromLeft (20);
    for (int i = 2; i >= 0; --i) { tabBtn[i].setBounds (sel.removeFromRight (100).reduced (2, 0)); }
    area.removeFromTop (8);

    // 下半分: 左に揺れ方の表示、右にタブの中身
    auto left = area.removeFromLeft (area.getWidth() * 44 / 100);
    wave.setBounds (left.reduced (0, 2));
    area.removeFromLeft (12);
    auto right = area;

    // SHAPE タブ
    {
        auto r = right;
        auto tools = r.removeFromRight (150);
        shapeEd.setBounds (r.reduced (0, 2));
        tools.removeFromLeft (10);
        copyToCustomBtn.setBounds (tools.removeFromTop (26)); tools.removeFromTop (4);
        auto sl = tools.removeFromTop (26);
        shapeSaveBtn.setBounds (sl.removeFromLeft (sl.getWidth() / 2).reduced (0, 0).withTrimmedRight (2));
        shapeLoadBtn.setBounds (sl.withTrimmedLeft (2));
        tools.removeFromTop (4);
        shapeResetBtn.setBounds (tools.removeFromTop (26));
        tools.removeFromTop (6);
        smoothModeBox.setBounds (tools.removeFromBottom (26));
        tools.removeFromBottom (2);
        smooth.label.setBounds (tools.removeFromTop (16));
        smooth.slider.setBounds (tools);
    }
    // RHYTHM タブ
    {
        auto r = right;
        auto top = r.removeFromTop (28);
        const int bw = top.getWidth() / 7;
        barsBox.setBounds (top.removeFromLeft (bw).reduced (2, 0));
        beatsBox.setBounds (top.removeFromLeft (bw).reduced (2, 0));
        gridBox.setBounds (top.removeFromLeft (bw + 10).reduced (2, 0));
        patFillBtn.setBounds (top.removeFromLeft (bw - 3).reduced (2, 0));
        patClearBtn.setBounds (top.removeFromLeft (bw - 3).reduced (2, 0));
        patSaveBtn.setBounds (top.removeFromLeft (bw - 2).reduced (2, 0));
        patLoadBtn.setBounds (top.reduced (2, 0));
        hintLabel.setBounds (r.removeFromBottom (16));
        r.removeFromTop (6);
        patternEd.setBounds (r);
    }
    // ENVELOPE タブ
    {
        auto r = right.withTrimmedRight (40);
        auto modeRow = r.removeFromBottom (30);
        envModeBox.setBounds (modeRow.withSizeKeepingCentre (160, 26));
        Knob* es[5] = { &thr, &att, &rel, &rateMod, &depthMod };
        const int ew = r.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
        {
            auto col = r.removeFromLeft (ew);
            es[i]->label.setBounds (col.removeFromTop (18));
            es[i]->slider.setBounds (col.reduced (4, 10));
        }
    }
}
