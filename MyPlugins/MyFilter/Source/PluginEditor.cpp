#include "PluginEditor.h"

using namespace myfiltui;

//==============================================================================
// フィルターの形の表示（今の周波数で）。右の細い棒 = 元（MOD の量）
void ResponseView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff121110));
    g.fillRoundedRectangle (r, 6.0f);
    auto meter = r.removeFromRight (22.0f).reduced (6.0f, 12.0f);
    auto a = r.reduced (10.0f, 12.0f);

    const auto k = proc.currentKnobs();
    const float lo = 20.0f, hi = 20000.0f, dbTop = 30.0f, dbBot = -42.0f;
    auto xOf = [&] (float hz) { return a.getX() + a.getWidth() * std::log (hz / lo) / std::log (hi / lo); };
    auto yOf = [&] (float db) { return a.getY() + a.getHeight() * (dbTop - juce::jlimit (dbBot, dbTop, db)) / (dbTop - dbBot); };

    g.setFont (juce::FontOptions (10.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (hz);
        g.setColour (text.withAlpha (0.07f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (text.withAlpha (0.35f));
        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), (int) x - 15, (int) a.getBottom() - 12, 30, 12, juce::Justification::centred);
    }
    for (float db : { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f, -36.0f })
    {
        g.setColour (text.withAlpha (db == 0.0f ? 0.18f : 0.06f));
        g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
    }

    const int nF = k.filters;
    const bool par = k.parallel;
    const float hzNow[2] { proc.engine.shownHz1.load(), proc.engine.shownHz2.load() };
    const float resNow[2] { proc.engine.shownRes1.load(), proc.engine.shownRes2.load() };
    auto curve = [&] (bool live) -> juce::Path
    {
        juce::Path path;
        const int n = juce::jmax (100, (int) a.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const float hz = lo * std::pow (hi / lo, (float) i / (float) n);
            double total = 0.0;
            double lin[2] {};
            for (int f = 0; f < nF; ++f)
            {
                const float fc = live ? hzNow[f] : myfilt::freqFromNorm (k.freq[f]);
                const float rs = live ? resNow[f] : k.res[f];
                const float db = myfilt::Engine::responseDb (hz, fc, rs, k.shape[f], k.poles[f], 192000.0) + k.gainDb[f];
                lin[f] = std::pow (10.0, db / 20.0);
            }
            if (nF == 1) total = lin[0];
            else if (par && ! proc.monoToStereo.load()) total = lin[0] + lin[1];   // 位相は無視した目安
            else if (proc.monoToStereo.load()) total = lin[0];
            else total = lin[0] * lin[1];
            const float y = yOf ((float) (20.0 * std::log10 (std::max (1e-6, total))));
            const float x = a.getX() + a.getWidth() * (float) i / (float) n;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        return path;
    };
    if (k.mod > 0.001f)
    {
        g.setColour (text.withAlpha (0.25f));
        g.strokePath (curve (false), juce::PathStrokeType (1.2f));
    }
    auto live = curve (true);
    juce::Path fill (live);
    fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
    g.setColour (accent.withAlpha (0.16f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (live, juce::PathStrokeType (2.0f));

    // 元のメーター
    g.setColour (juce::Colour (0xff0b0a0a));
    g.fillRect (meter);
    const float m = juce::jlimit (0.0f, 1.0f, proc.engine.shownMod.load());
    g.setColour (warm);
    g.fillRect (meter.withTrimmedTop (meter.getHeight() * (1.0f - m)));
    g.setColour (text.withAlpha (0.45f));
    g.drawText ("MOD", (int) meter.getX() - 8, (int) meter.getBottom() + 1, (int) meter.getWidth() + 16, 11, juce::Justification::centred);

    g.setColour (text.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f));
    juce::String info = "F1 " + juce::String (juce::roundToInt (hzNow[0])) + " Hz";
    if (nF > 1) info << "   F2 " << juce::roundToInt (hzNow[1]) << " Hz";
    info << "   " << myfilt::modeName (k.mode);
    if (k.mode == myfilt::kRhythm || k.mode == myfilt::kRandom) info << "  " << juce::String (proc.shownBpm.load(), 1) << " BPM";
    g.drawText (info, getLocalBounds().reduced (12, 2).removeFromTop (14), juce::Justification::topLeft);
}

//==============================================================================
// 形の編集（My Tremolo と同じ）
bool ShapeEditor::editable() const { return proc.currentKnobs().shapeIdx == mytrem::kNumShapes; }

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
    g.setColour (juce::Colour (0xff121110));
    g.fillRoundedRectangle (r, 6.0f);
    const auto k = proc.currentKnobs();
    float tab[mytrem::kShapeLen];
    const bool custom = k.shapeIdx == mytrem::kNumShapes;
    const auto cs = dragIdx >= 0 ? work : proc.getCustomShape();
    if (custom) cs.render (tab, mytrem::kShapeLen, k.smoothing, k.smoothMode);
    else std::copy (mytrem::kShapes[k.shapeIdx], mytrem::kShapes[k.shapeIdx] + mytrem::kShapeLen, tab);

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

    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    if (custom)
    {
        for (int i = 0; i < cs.n; ++i)
        {
            auto p = toScreen (cs.x[(size_t) i], cs.y[(size_t) i]);
            g.setColour (warm);
            g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
        }
        g.setColour (text.withAlpha (0.45f));
        g.drawText ("click = add point   drag = move   option-click = delete", getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);
    }
    else
        g.drawText ("choose SHAPE: Custom (or press EDIT) to draw your own", getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);
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
// リズムの升目（My Tremolo と同じ）
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
    if (v == 2 && (idx == 0 || pt.cell[(size_t) idx - 1] == 0)) v = 1;
    if (pt.cell[(size_t) idx] == v) return;
    pt.cell[(size_t) idx] = v;
    if (v == 0) for (int j = idx + 1; j < pt.numCells() && pt.cell[(size_t) j] == 2; ++j) pt.cell[(size_t) j] = 0;
    proc.setPattern (pt);
    repaint();
}

void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position);
    if (c < 0) return;
    // 升目を触ったら、RHYTHM を Pattern に（そうしないと描いても動きが変わらない）
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
    g.setColour (juce::Colour (0xff121110));
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
        if (v == 2) cell = cell.withTrimmedLeft (-2.0f);
        g.fillRoundedRectangle (cell, 2.0f);
    }
    if (proc.currentKnobs().rhythm == mytrem::kRhythmPattern)
    {
        const float pos = proc.engine.shownPhase.load() * n;
        const int row = juce::jlimit (0, rows - 1, (int) pos / perRow);
        const float x = a.getX() + (pos - row * perRow) * cw;
        g.setColour (warm.withAlpha (0.9f));
        g.drawLine (x, a.getY() + row * ch, x, a.getY() + (row + 1) * ch, 2.0f);
    }
}

//==============================================================================
MyFilterEditor::MyFilterEditor (MyFilterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3532));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e2926));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff27231f));
    lnf.setColour (juce::ComboBox::textColourId, text);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff433d38));
    lnf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff221f1d));
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
    setLookAndFeel (&lnf);

    setupKnob (input, "input", "INPUT", "Level into the filter (drives the ANALOG saturation harder)");
    setupKnob (mix, "mix", "MIX", "Filtered vs. dry sound. 100 % = only the filtered sound");
    setupKnob (output, "output", "OUTPUT", "Level after the filter (into the ANALOG output stage)");
    setupKnob (mod, "mod", "MOD", "How far the modulation moves the filter (x FREQ MOD in the TWEAK tab)");
    setupKnob (freq1, "freq1", "FREQ", "Cutoff / center frequency of filter 1");
    setupKnob (res1, "res1", "RES", "Resonance. Near the top it starts to sing on its own - watch your speakers");
    setupKnob (gain1, "gain1", "GAIN", "Level of filter 1");
    setupKnob (freq2, "freq2", "FREQ", "Cutoff / center frequency of filter 2");
    setupKnob (res2, "res2", "RES", "Resonance of filter 2");
    setupKnob (gain2, "gain2", "GAIN", "Level of filter 2");
    // LINK: つかんでいる間だけ相手のつまみも動かす。相手にも「触っている」ことを Logic に伝える（Touch の書き込み用）
    for (auto* kk : { &freq1, &res1, &gain1, &freq2, &res2, &gain2 })
    {
        const juce::String id = kk == &freq1 ? "freq1" : kk == &res1 ? "res1" : kk == &gain1 ? "gain1" : kk == &freq2 ? "freq2" : kk == &res2 ? "res2" : "gain2";
        const juce::String other = id.dropLastCharacters (1) + (id.endsWith ("1") ? "2" : "1");
        kk->slider.onDragStart = [this, other]
        {
            ++processor.linkGesture;
            if (processor.apvts.getRawParameterValue ("filterLink")->load() > 0.5f && processor.apvts.getRawParameterValue ("filters")->load() > 0.5f)
                if (auto* o = processor.apvts.getParameter (other)) { o->beginChangeGesture(); linkTouched.addIfNotAlreadyThere (other); }
        };
        kk->slider.onDragEnd = [this, other]
        {
            if (linkTouched.contains (other)) { if (auto* o = processor.apvts.getParameter (other)) o->endChangeGesture(); linkTouched.removeString (other); }
            processor.linkGesture = juce::jmax (0, processor.linkGesture.load() - 1);
        };
    }

    setupBox (shape1Box, shape1Att, "shape1", "Lowpass / Bandpass / Highpass / Notch");
    setupBox (poles1Box, poles1Att, "poles1", "Slope: 2 pole = 12 dB/oct ... 8 pole = 48 dB/oct");
    setupBox (shape2Box, shape2Att, "shape2", "Lowpass / Bandpass / Highpass / Notch");
    setupBox (poles2Box, poles2Att, "poles2", "Slope: 2 pole = 12 dB/oct ... 8 pole = 48 dB/oct");
    setupBox (filtersBox, filtersAtt, "filters", "One filter, or two filters (in series or parallel)");
    setupBox (routingBox, routingAtt, "routing", "Serial = filter 1 into filter 2. Parallel = both side by side, added");
    setupBox (styleBox, styleAtt, "style", "Analog style (saturation colour)");
    setupBox (modeBox, modeAtt, "modMode", "What moves the filter");
    setupToggle (linkBtn, linkAtt, "filterLink", "LINK", "Move FREQ, RES and GAIN of both filters together");
    setupToggle (analogBtn, analogAtt, "analog", "ANALOG", "On = analog saturation (STYLE). Off = digital, clean with a hard ceiling");

    // 動かし方ごと
    setupKnob (lfoRate, "lfoRate", "RATE", "LFO speed (0.01 - 100 Hz). Very fast = ring-mod like tones");
    setupKnob (groove, "groove", "GROOVE", "Left = shuffle, right = swing");
    setupKnob (tempo, "tempo", "TEMPO", "Own tempo (used when SYNC is off). TAP to set it by tapping");
    setupKnob (thr, "threshold", "THRESHOLD", "Input level where the envelope starts to open the filter (the ring shows the input)");
    setupKnob (envGain, "envGain", "GAIN", "Sensitivity. High = opens fully just above THRESHOLD (gate-like)");
    setupKnob (att, "attack", "ATTACK", "How fast the filter opens");
    setupKnob (rel, "release", "RELEASE", "How fast it closes again");
    setupKnob (trigLvl, "trigLevel", "TRIGGER", "Input level that triggers (the ring shows the input). All the way up = only the button");
    setupKnob (adsrA, "adsrA", "A", "Attack time");
    setupKnob (adsrD, "adsrD", "D", "Decay time (to SUSTAIN)");
    setupKnob (adsrS, "adsrS", "S", "Sustain level while held / above the trigger");
    setupKnob (adsrR, "adsrR", "R", "Release time");
    setupBox (shapeBox, shapeAtt, "lfoShape", "Wave shape. Custom = your own (SHAPE tab)");
    setupBox (rhythmBox, rhythmAtt, "rhythm", "Note length of one wave. Pattern = your own rhythm (RHYTHM tab)");
    syncBtn.setButtonText ("SYNC");
    syncBtn.setClickingTogglesState (true);
    syncBtn.setTooltip ("On = follow Logic's tempo and bar position. Off = run at TEMPO");
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<BtnAttachment> (processor.apvts, "sync", syncBtn);
    tapBtn.setButtonText ("TAP");
    tapBtn.setTooltip ("Tap the beat to set TEMPO (turns SYNC off)");
    tapBtn.onClick = [this]
    {
        if (auto* s = processor.apvts.getParameter ("sync")) if (s->getValue() > 0.5f) { s->beginChangeGesture(); s->setValueNotifyingHost (0.0f); s->endChangeGesture(); }
        processor.tapTempo();
    };
    addAndMakeVisible (tapBtn);
    triggerBtn.setButtonText ("TRIGGER");
    triggerBtn.setTooltip ("Hold to trigger by hand");
    triggerBtn.onStateChange = [this]
    {
        const bool down = triggerBtn.isDown();
        if (auto* t = processor.apvts.getParameter ("trigger"))
            if ((t->getValue() > 0.5f) != down) { t->beginChangeGesture(); t->setValueNotifyingHost (down ? 1.0f : 0.0f); t->endChangeGesture(); }
    };
    addAndMakeVisible (triggerBtn);

    // TWEAK
    setupKnob (fmod1, "freqMod1", "FREQ MOD 1", "How many octaves MOD 100 % moves filter 1 (minus = downward)");
    setupKnob (rmod1, "resMod1", "RES MOD 1", "Modulation adds (+) or takes away (-) resonance of filter 1");
    setupKnob (lmod1, "levelMod1", "LEVEL MOD 1", "+ = filter 1 gets quieter when the modulation is low, - = when it is high");
    setupKnob (fmod2, "freqMod2", "FREQ MOD 2", "How many octaves MOD 100 % moves filter 2");
    setupKnob (rmod2, "resMod2", "RES MOD 2", "Modulation adds (+) or takes away (-) resonance of filter 2");
    setupKnob (lmod2, "levelMod2", "LEVEL MOD 2", "+ = filter 2 gets quieter when the modulation is low, - = when it is high");
    setupKnob (lrOff, "lrOffset", "L/R OFFSET", "Mono-to-stereo only: right side frequency higher (+) / lower (-)");
    setupKnob (smooth, "smoothing", "SMOOTH", "Custom shape / RANDOM / STEP: 0 = jumps, 100 = fully smooth");
    setupBox (lrModeBox, lrModeAtt, "lrMode", "Mono-to-stereo only: Flipped = right moves opposite to left");
    setupBox (smoothModeBox, smoothModeAtt, "smoothMode", "Curve of the smoothing");

    factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    for (int i = 0; i < MyFilterProcessor::numFactoryPresets(); ++i) factoryBox.addItem (MyFilterProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int i = factoryBox.getSelectedId() - 1;
        if (i >= 0) processor.loadFactoryPreset (i);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
        factoryBox.setText ("FACTORY PRESETS", juce::dontSendNotification);
    };
    addAndMakeVisible (factoryBox);

    const char* tabs[3] = { "TWEAK", "SHAPE", "RHYTHM" };
    for (int i = 0; i < 3; ++i)
    {
        tabBtn[i].setButtonText (tabs[i]);
        tabBtn[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabBtn[i]);
    }
    addAndMakeVisible (response);
    addChildComponent (shapeEd);
    addChildComponent (patternEd);

    copyToCustomBtn.setButtonText ("EDIT");
    copyToCustomBtn.setTooltip ("Copy the current shape into Custom so you can change its points");
    copyToCustomBtn.onClick = [this]
    {
        const auto k = processor.currentKnobs();
        if (k.shapeIdx < mytrem::kNumShapes)
        {
            mytrem::CustomShape c; c.n = 17;
            for (int i = 0; i < c.n; ++i)
            {
                const float x = (float) i / 16.0f;
                c.x[(size_t) i] = x;
                c.y[(size_t) i] = mytrem::kShapes[k.shapeIdx][juce::jlimit (0, mytrem::kShapeLen - 1, (int) std::lround (x * mytrem::kShapeLen) % mytrem::kShapeLen)];
            }
            processor.setCustomShape (c);
            setParam ("smoothMode", 0.0f);
        }
        setParam ("lfoShape", (float) mytrem::kNumShapes);
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
        pt.cell.fill (0);
        const double gg = mytrem::gridBeats (pt.grid);
        for (int i = 0; i < oldCells; ++i)
            if (old.cell[(size_t) i] != 0)
            {
                const int j = (int) std::lround (i * oldGrid / gg);
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
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): make the filter movement this much smaller while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<Attachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Fader Pilot to calm the filter movement while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing - MOD smaller" : "Fader Pilot found - voice not singing";
    };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    syncPatternBoxes();
    showTab (0);
    startTimerHz (30);
    setSize (1180, 600 + footerH);
}

MyFilterEditor::~MyFilterEditor()
{
    // つまみをつかんだまま閉じた時の後片付け（LINK の「つかんでいる」が残ると、プリセットで相手が二重に動く）
    for (auto& id : linkTouched) if (auto* o = processor.apvts.getParameter (id)) o->endChangeGesture();
    linkTouched.clear();
    processor.linkGesture = 0;
    setLookAndFeel (nullptr);
}

void MyFilterEditor::setParam (const juce::String& id, float plainValue)
{
    if (auto* prm = processor.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
        prm->endChangeGesture();
    }
}

void MyFilterEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 17);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, k.slider);
    if (paramId == "threshold" || paramId == "trigLevel")
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    if (paramId == "tempo")
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
    k.slider.updateText();
}

void MyFilterEditor::setupBox (juce::ComboBox& b, std::unique_ptr<BoxAttachment>& att, const juce::String& paramId, const juce::String& tip)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        b.addItemList (c->choices, 1);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<BoxAttachment> (processor.apvts, paramId, b);
}

void MyFilterEditor::setupToggle (juce::TextButton& b, std::unique_ptr<BtnAttachment>& att, const juce::String& paramId, const juce::String& t, const juce::String& tip)
{
    b.setButtonText (t);
    b.setClickingTogglesState (true);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<BtnAttachment> (processor.apvts, paramId, b);
}

void MyFilterEditor::showTab (int t)
{
    tab = t;
    for (int i = 0; i < 3; ++i) tabBtn[i].setToggleState (i == t, juce::dontSendNotification);
    const bool tw = t == 0, sh = t == 1, rh = t == 2;
    for (auto* k : { &fmod1, &rmod1, &lmod1, &fmod2, &rmod2, &lmod2, &lrOff }) { k->slider.setVisible (tw); k->label.setVisible (tw); }
    lrModeBox.setVisible (tw);
    shapeEd.setVisible (sh);
    for (auto* c : std::initializer_list<juce::Component*> { &copyToCustomBtn, &shapeSaveBtn, &shapeLoadBtn, &shapeResetBtn }) c->setVisible (sh);
    smooth.slider.setVisible (sh || tw); smooth.label.setVisible (sh || tw); smoothModeBox.setVisible (sh || tw);
    patternEd.setVisible (rh);
    for (auto* c : std::initializer_list<juce::Component*> { &barsBox, &beatsBox, &gridBox, &patSaveBtn, &patLoadBtn, &patClearBtn, &patFillBtn }) c->setVisible (rh);
    hintLabel.setVisible (rh);
    hintLabel.setText (rh ? "click = play / rest   shift-click or right-click = hold the previous one longer   (RHYTHM switches to Pattern)" : "",
                       juce::dontSendNotification);
    resized();
    repaint();
}

void MyFilterEditor::syncPatternBoxes()
{
    const auto pt = processor.getPattern();
    barsBox.setSelectedId (pt.bars, juce::dontSendNotification);
    beatsBox.setSelectedId (pt.beatsPerBar, juce::dontSendNotification);
    gridBox.setSelectedId (pt.grid + 1, juce::dontSendNotification);
}

juce::File MyFilterEditor::libFolder (bool shape) const
{
    auto f = processor.myPresets.folder().getChildFile (shape ? "Shapes" : "Rhythms");
    f.createDirectory();
    return f;
}

void MyFilterEditor::saveFile (bool shape)
{
    chooser = std::make_unique<juce::FileChooser> (shape ? "Save shape" : "Save rhythm", libFolder (shape), shape ? "*.myshape" : "*.myrhythm");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, shape] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension (shape ? "myshape" : "myrhythm");
            f.replaceWithText (shape ? MyFilterProcessor::shapeToString (processor.getCustomShape())
                                     : MyFilterProcessor::patternToString (processor.getPattern()));
        });
}

void MyFilterEditor::loadFile (bool shape)
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
                if (MyFilterProcessor::shapeFromString (s, c)) { processor.setCustomShape (c); setParam ("lfoShape", (float) mytrem::kNumShapes); }
            }
            else
            {
                mytrem::Pattern pt;
                if (MyFilterProcessor::patternFromString (s, pt)) { processor.setPattern (pt); setParam ("rhythm", (float) mytrem::kRhythmPattern); }
            }
        });
}

void MyFilterEditor::timerCallback()
{
    if (processor.editVersion.load() != lastEditVersion)
    {
        lastEditVersion = processor.editVersion.load();
        syncPatternBoxes();
        shapeEd.repaint();
        patternEd.repaint();
    }
    const auto k = processor.currentKnobs();
    if (k.mode != lastMode) { lastMode = k.mode; layoutModePanel(); repaint(); }
    response.repaint();
    if (tab == 1) shapeEd.repaint();
    if (tab == 2) patternEd.repaint();
    const float in = processor.engine.shownInDb.load();
    inMeter = in > inMeter ? in : inMeter - 1.5f;
    repaint (modePanelArea);

    // 2 つ目のフィルターを使わない間は薄く
    const bool two = k.filters > 1;
    for (auto* c : std::initializer_list<juce::Component*> { &freq2.slider, &freq2.label, &res2.slider, &res2.label, &gain2.slider, &gain2.label,
                                                             &shape2Box, &poles2Box, &routingBox, &linkBtn, &fmod2.slider, &rmod2.slider, &lmod2.slider })
        c->setAlpha (two ? 1.0f : 0.35f);
    tempo.slider.setAlpha (k.sync ? 0.45f : 1.0f);
    const bool m2s = processor.monoToStereo.load();
    lrOff.slider.setAlpha (m2s ? 1.0f : 0.35f); lrModeBox.setAlpha (m2s ? 1.0f : 0.35f);
    styleBox.setAlpha (k.style > 0 ? 1.0f : 0.35f);
}

void MyFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff24201d));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (accent);
    g.fillRect (0, 48, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MY FILTER", 16, 0, 220, 48, juce::Justification::centredLeft);

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff221f1c));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (c);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.reduced (10, 4).removeFromTop (14), juce::Justification::topLeft);
    };
    section (filter1Area, "FILTER 1", accent);
    section (filter2Area, processor.currentKnobs().filters > 1 ? "FILTER 2" : "FILTER 2  (choose 2 Filters to use)", accent2);
    section (modArea, "MODULATION", warm);
    section (modePanelArea, juce::String (myfilt::modeName (processor.currentKnobs().mode)) + " MODE", warm);

    // しきい値・きっかけのまわりに入力の大きさ（赤 = 超えている）
    const int mode = processor.currentKnobs().mode;
    Knob* ringKnob = mode == myfilt::kEnvelope ? &thr : ((mode == myfilt::kStep || mode == myfilt::kADSR) ? &trigLvl : nullptr);
    if (ringKnob != nullptr && ringKnob->slider.isVisible())
    {
        auto b = ringKnob->slider.getBounds().toFloat().withTrimmedBottom (18).reduced (2);
        const float sz = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.withSizeKeepingCentre (sz, sz);
        const float norm = juce::jlimit (0.0f, 1.0f, (inMeter + 60.0f) / 60.0f);
        const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
        juce::Path arc; arc.addCentredArc (c.getCentreX(), c.getCentreY(), sz / 2 + 3, sz / 2 + 3, 0, a0, a0 + (a1 - a0) * norm, true);
        const float lv = (float) ringKnob->slider.getValue();
        g.setColour (inMeter > lv ? juce::Colour (0xffe0584a) : text.withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (3.0f));
    }
}

void MyFilterEditor::resized()
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
    styleBox.setBounds (factoryBox.getX() - 128, 10, 116, 28);
    analogBtn.setBounds (styleBox.getX() - 88, 10, 82, 28);
    filtersBox.setBounds (240, 10, 120, 28);
    routingBox.setBounds (366, 10, 110, 28);
    linkBtn.setBounds (482, 10, 70, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (58).reduced (12, 0);
    // 1 段目: INPUT | FILTER 1 | MODULATION | FILTER 2 | MIX | OUTPUT
    auto row = area.removeFromTop (178);
    auto knobAt = [] (Knob& k, juce::Rectangle<int> col)
    {
        k.label.setBounds (col.removeFromTop (16));
        k.slider.setBounds (col.reduced (2, 0));
    };
    {
        auto c = row.removeFromLeft (96); c.removeFromTop (22); knobAt (input, c.removeFromTop (118));
    }
    row.removeFromLeft (6);
    filter1Area = row.removeFromLeft (330);
    row.removeFromLeft (6);
    modArea = row.removeFromLeft (170);
    row.removeFromLeft (6);
    filter2Area = row.removeFromLeft (330);
    row.removeFromLeft (6);
    {
        auto c = row.removeFromLeft (row.getWidth() / 2); c.removeFromTop (22); knobAt (mix, c.removeFromTop (118));
        auto d = row; d.removeFromTop (22); knobAt (output, d.removeFromTop (118));
    }
    auto filterLayout = [&] (juce::Rectangle<int> r, Knob& fq, Knob& rs, Knob& gn, juce::ComboBox& sh, juce::ComboBox& pl)
    {
        r = r.reduced (6, 0); r.removeFromTop (20);
        auto boxes = r.removeFromBottom (34).reduced (2, 4);
        sh.setBounds (boxes.removeFromLeft (boxes.getWidth() / 2).reduced (2, 0));
        pl.setBounds (boxes.reduced (2, 0));
        const int w = r.getWidth() / 3;
        knobAt (fq, r.removeFromLeft (w)); knobAt (rs, r.removeFromLeft (w)); knobAt (gn, r);
    };
    filterLayout (filter1Area, freq1, res1, gain1, shape1Box, poles1Box);
    filterLayout (filter2Area, freq2, res2, gain2, shape2Box, poles2Box);
    {
        auto r = modArea.reduced (8, 0); r.removeFromTop (20);
        modeBox.setBounds (r.removeFromBottom (34).reduced (2, 4));
        knobAt (mod, r.withSizeKeepingCentre (110, r.getHeight()));
    }

    area.removeFromTop (8);
    // 2 段目: 動かし方ごとのつまみ
    modePanelArea = area.removeFromTop (128);
    layoutModePanel();
    area.removeFromTop (8);

    // 下半分: 左にフィルターの形、右にタブ
    auto left = area.removeFromLeft (area.getWidth() * 47 / 100);
    response.setBounds (left.reduced (0, 2));
    area.removeFromLeft (12);
    auto tabsRow = area.removeFromTop (28);
    for (int i = 0; i < 3; ++i) tabBtn[i].setBounds (tabsRow.removeFromLeft (100).reduced (2, 0));
    area.removeFromTop (6);
    auto right = area;

    // TWEAK
    {
        auto r = right;
        auto top = r.removeFromTop (r.getHeight() / 2);
        const int w = top.getWidth() / 4;
        knobAt (fmod1, top.removeFromLeft (w)); knobAt (rmod1, top.removeFromLeft (w)); knobAt (lmod1, top.removeFromLeft (w));
        {
            auto c = top; c.removeFromTop (16);
            lrModeBox.setBounds (c.removeFromBottom (26).reduced (4, 0));
            lrOff.label.setBounds (top.removeFromTop (16));
            lrOff.slider.setBounds (c.reduced (2, 0));
        }
        knobAt (fmod2, r.removeFromLeft (w)); knobAt (rmod2, r.removeFromLeft (w)); knobAt (lmod2, r.removeFromLeft (w));
        if (tab == 0)
        {
            auto c = r; smoothModeBox.setBounds (c.removeFromBottom (26).reduced (4, 0));
            smooth.label.setBounds (c.removeFromTop (16)); smooth.slider.setBounds (c.reduced (2, 0));
        }
    }
    // SHAPE
    {
        auto r = right;
        auto tools = r.removeFromRight (140);
        shapeEd.setBounds (r.reduced (0, 2));
        tools.removeFromLeft (10);
        copyToCustomBtn.setBounds (tools.removeFromTop (26)); tools.removeFromTop (4);
        auto sl = tools.removeFromTop (26);
        shapeSaveBtn.setBounds (sl.removeFromLeft (sl.getWidth() / 2).withTrimmedRight (2));
        shapeLoadBtn.setBounds (sl.withTrimmedLeft (2));
        tools.removeFromTop (4);
        shapeResetBtn.setBounds (tools.removeFromTop (26));
        tools.removeFromTop (6);
        if (tab == 1)
        {
            smoothModeBox.setBounds (tools.removeFromBottom (26));
            tools.removeFromBottom (2);
            smooth.label.setBounds (tools.removeFromTop (16));
            smooth.slider.setBounds (tools);
        }
    }
    // RHYTHM
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
}

// 動かし方ごとのつまみを並べ替える（モードが変わった時）
void MyFilterEditor::layoutModePanel()
{
    Knob* all[] = { &lfoRate, &groove, &tempo, &thr, &envGain, &att, &rel, &trigLvl, &adsrA, &adsrD, &adsrS, &adsrR };
    for (auto* k : all) { k->slider.setVisible (false); k->label.setVisible (false); }
    for (auto* c : std::initializer_list<juce::Component*> { &shapeBox, &rhythmBox, &syncBtn, &tapBtn, &triggerBtn }) c->setVisible (false);

    auto r = modePanelArea.reduced (14, 0);
    r.removeFromTop (20);
    r = r.withTrimmedBottom (4);
    const int kw = 104;
    auto knob = [&] (Knob& k)
    {
        auto c = r.removeFromLeft (kw);
        k.label.setBounds (c.removeFromTop (16)); k.slider.setBounds (c.reduced (2, 0));
        k.slider.setVisible (true); k.label.setVisible (true);
        r.removeFromLeft (8);
    };
    auto box = [&] (juce::ComboBox& b, const juce::String&)
    {
        auto c = r.removeFromLeft (150);
        b.setBounds (c.withSizeKeepingCentre (146, 28).translated (0, 8));
        b.setVisible (true);
        r.removeFromLeft (10);
    };
    auto tempoGroup = [&]
    {
        auto c = r.removeFromLeft (kw);
        tempo.label.setBounds (c.removeFromTop (16));
        auto btns = c.removeFromBottom (24);
        syncBtn.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 1));
        tapBtn.setBounds (btns.reduced (2, 1));
        tempo.slider.setBounds (c.reduced (2, 0));
        for (juce::Component* x : std::initializer_list<juce::Component*> { &tempo.slider, &tempo.label, &syncBtn, &tapBtn }) x->setVisible (true);
        r.removeFromLeft (8);
    };
    auto trigButton = [&]
    {
        auto c = r.removeFromLeft (120);
        triggerBtn.setBounds (c.withSizeKeepingCentre (110, 34).translated (0, 6));
        triggerBtn.setVisible (true);
        r.removeFromLeft (8);
    };
    switch (processor.currentKnobs().mode)
    {
        case myfilt::kLFO:      knob (lfoRate); box (shapeBox, "SHAPE"); break;
        case myfilt::kRhythm:   box (rhythmBox, "RHYTHM"); box (shapeBox, "SHAPE"); knob (groove); tempoGroup(); break;
        case myfilt::kEnvelope: knob (thr); knob (envGain); knob (att); knob (rel); break;
        case myfilt::kRandom:   box (rhythmBox, "RHYTHM"); knob (groove); tempoGroup(); break;
        case myfilt::kStep:     knob (trigLvl); trigButton(); break;
        default:                knob (trigLvl); knob (adsrA); knob (adsrD); knob (adsrS); knob (adsrR); trigButton(); break;
    }
}
