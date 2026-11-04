#include "PluginEditor.h"

namespace
{
const juce::Colour bg0 (0xff12141a), bg1 (0xff1b1e27), panelC (0xff20242f), edge (0xff2e3442);
const juce::Colour txt (0xffd9dde6), dim (0xff8c93a3), amber (0xffe9a54b), teal (0xff57c4c0);
}

//==============================================================================
LumenLook::LumenLook()
{
    setColour (juce::Slider::textBoxTextColourId, txt);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, txt);
    setColour (juce::ComboBox::backgroundColourId, bg1);
    setColour (juce::ComboBox::textColourId, txt);
    setColour (juce::ComboBox::outlineColourId, edge);
    setColour (juce::ComboBox::arrowColourId, amber);
    setColour (juce::PopupMenu::backgroundColourId, bg1);
    setColour (juce::PopupMenu::textColourId, txt);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.25f));
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, bg0);
}

void LumenLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float ang = start + pos * (end - start);
    const auto accent = s.getProperties().getWithDefault ("teal", false) ? teal : amber;

    g.setColour (bg0);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, end, true);
    val.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, ang, true);
    g.setColour (edge);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (bg1.brighter (0.15f));
    g.fillEllipse (c.x - r * 0.68f, c.y - r * 0.68f, r * 1.36f, r * 1.36f);
    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -r * 0.66f, 3.0f, r * 0.34f, 1.5f);
    g.setColour (txt);
    g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void LumenLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? amber : (over ? bg1.brighter (0.1f) : bg1));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? amber : edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void LumenLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (bg1);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path p;
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (amber);
    g.fillPath (p);
}

//==============================================================================
LumenRoomsEditor::Knob* LumenRoomsEditor::knob (const char* id, const char* name, bool big)
{
    auto k = std::make_unique<Knob>();
    k->id = id;
    k->big = big;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    k->slider.setLookAndFeel (&look);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k->label.setColour (juce::Label::textColourId, dim);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
    return knobs.back().get();
}

LumenRoomsEditor::Choice* LumenRoomsEditor::choice (const char* id, const char* name)
{
    auto c = std::make_unique<Choice>();
    c->id = id;
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->box.addItemList (prm->choices, 1);
    c->box.setLookAndFeel (&look);
    c->label.setText (name, juce::dontSendNotification);
    c->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    c->label.setColour (juce::Label::textColourId, dim);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    choices.push_back (std::move (c));
    return choices.back().get();
}

LumenRoomsEditor::LumenRoomsEditor (LumenRoomsProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);

    // ── MAIN ──
    Panel refl { "EARLY REFLECTIONS" };
    refl.page = 0; refl.columns = 3; refl.extraHeight = 96;
    refl.choices = { choice ("pattern", "PATTERN"), choice ("proximity", "PROXIMITY"), choice ("erSlope", "ROLL-OFF SLOPE") };
    refl.knobs = { knob ("erSize", "SIZE"), knob ("reflectivity", "REFLECTIVITY"), knob ("diffusion", "DIFFUSION"),
                   knob ("erWidth", "WIDTH"), knob ("erRoll", "ROLL-OFF"), knob ("erTrim", "TRIM") };
    for (auto* k : refl.knobs) k->slider.getProperties().set ("teal", true);
    panels.push_back (refl);

    Panel rev { "REVERB" };
    rev.page = 0; rev.columns = 4; rev.extraHeight = 96;
    rev.choices = { choice ("revSlope", "ROLL-OFF SLOPE") };
    rev.knobs = { knob ("time", "TIME"), knob ("bloom", "BLOOM"), knob ("predelay", "PRE-DELAY"), knob ("revWidth", "WIDTH"),
                  knob ("bassMult", "BASS MULT"), knob ("bassFreq", "BASS FREQ"), knob ("trebMult", "TREBLE MULT"), knob ("trebFreq", "TREBLE FREQ"),
                  knob ("revRoll", "ROLL-OFF"), knob ("lowCut", "LOW CUT"), knob ("revTrim", "TRIM") };
    panels.push_back (rev);

    Panel out { "OUTPUT" };
    out.page = -1; out.columns = 1;
    out.knobs = { knob ("mix", "DRY / WET", true), knob ("balance", "REFL / REVERB", true), knob ("master", "OUTPUT") };
    panels.push_back (out);

    // ── DETAIL ──
    Panel mod { "MODULATION" };
    mod.page = 1; mod.columns = 2;
    mod.knobs = { knob ("modDepth", "REVERB DEPTH"), knob ("modRate", "REVERB RATE"), knob ("undDepth", "UNDULATION"),
                  knob ("undShape", "UNDUL. SHAPE"), knob ("erModDepth", "REFL DEPTH"), knob ("erModRate", "REFL RATE") };
    panels.push_back (mod);

    Panel echo { "ECHO  /  LOW BOOST" };
    echo.page = 1; echo.columns = 2;
    echo.knobs = { knob ("echoLevel", "ECHO LEVEL"), knob ("echoTime", "ECHO TIME"), knob ("echoRoll", "ECHO ROLL-OFF"),
                   knob ("lowBoost", "LOW BOOST"), knob ("boostWeight", "BOOST WEIGHT") };
    panels.push_back (echo);

    Panel xf { "CROSSFEED" };
    xf.page = 1; xf.columns = 3;
    xf.knobs = { knob ("revXfLevel", "REVERB LEVEL"), knob ("revXfDelay", "REVERB DELAY"), knob ("revXfRoll", "REVERB ROLL-OFF"),
                 knob ("erXfLevel", "REFL LEVEL"), knob ("erXfDelay", "REFL DELAY"), knob ("erXfRoll", "REFL ROLL-OFF") };
    for (int i = 3; i < 6; ++i) xf.knobs[(size_t) i]->slider.getProperties().set ("teal", true);
    panels.push_back (xf);

    // ── EQ ──
    Panel eq { "POST EQ" };
    eq.page = 2; eq.columns = 4; eq.extraHeight = 120;
    eq.choices = { choice ("eqSlope", "ROLL-OFF SLOPE") };
    eq.knobs = { knob ("eqLowCut", "LOW CUT"), knob ("eqLsFreq", "LOW FREQ"), knob ("eqLsGain", "LOW GAIN"), knob ("eqLsQ", "LOW Q"),
                 knob ("eqRoll", "ROLL-OFF"), knob ("eqMidFreq", "MID FREQ"), knob ("eqMidGain", "MID GAIN"), knob ("eqMidQ", "MID Q"),
                 nullptr, knob ("eqHsFreq", "HIGH FREQ"), knob ("eqHsGain", "HIGH GAIN"), knob ("eqHsQ", "HIGH Q") };
    panels.push_back (eq);

    freeze.button.setButtonText ("FREEZE");
    freeze.button.setClickingTogglesState (true);
    freeze.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "infinite", freeze.button);
    addAndMakeVisible (freeze.button);
    eqOn.button.setButtonText ("EQ ON");
    eqOn.button.setClickingTogglesState (true);
    eqOn.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "eqOn", eqOn.button);
    addAndMakeVisible (eqOn.button);

    const char* tabNames[3] = { "MAIN", "DETAIL", "EQ" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].setClickingTogglesState (false);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
    }

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setSize (1060, 640);
    showPage (0);
    startTimerHz (30);
}

LumenRoomsEditor::~LumenRoomsEditor()
{
    for (auto& k : knobs) k->slider.setLookAndFeel (nullptr);
    for (auto& c : choices) c->box.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void LumenRoomsEditor::showPage (int pg)
{
    page = pg;
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == pg, juce::dontSendNotification);
    for (auto& pn : panels)
    {
        const bool vis = pn.page == -1 || pn.page == pg;
        for (auto* k : pn.knobs) if (k) { k->slider.setVisible (vis); k->label.setVisible (vis); }
        for (auto* c : pn.choices) { c->box.setVisible (vis); c->label.setVisible (vis); }
    }
    freeze.button.setVisible (pg == 0);
    eqOn.button.setVisible (pg == 2);
    resized();
    repaint();
}

void LumenRoomsEditor::layoutPanel (Panel& pn)
{
    auto r = pn.bounds.reduced (12, 8);
    r.removeFromTop (24);   // タイトル
    if (pn.extraHeight > 0) { pn.extra = r.removeFromTop (pn.extraHeight); r.removeFromTop (6); }
    if (! pn.choices.empty())
    {
        auto row = r.removeFromTop (44);
        const int w = row.getWidth() / juce::jmax (1, (int) pn.choices.size());
        for (auto* c : pn.choices)
        {
            auto cell = row.removeFromLeft (w).reduced (4, 0);
            c->label.setBounds (cell.removeFromTop (16));
            c->box.setBounds (cell.removeFromTop (24));
        }
        r.removeFromTop (6);
    }
    const int cols = juce::jmax (1, pn.columns);
    const int cellW = r.getWidth() / cols;
    int i = 0;
    int y = r.getY();
    int rowH = 0;
    for (auto* k : pn.knobs)
    {
        const int col = i % cols;
        if (col == 0 && i > 0) { y += rowH; rowH = 0; }
        ++i;
        if (k == nullptr) continue;
        const int h = k->big ? 132 : 96;
        rowH = juce::jmax (rowH, h);
        juce::Rectangle<int> cell (r.getX() + col * cellW, y, cellW, h);
        k->label.setBounds (cell.removeFromTop (15));
        k->slider.setBounds (cell.reduced (k->big ? 10 : 6, 0));
    }
}

void LumenRoomsEditor::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop (58).reduced (16, 12);
    head.removeFromLeft (220);
    presetBox.setBounds (head.removeFromRight (200));
    head.removeFromRight (16);
    for (int i = 2; i >= 0; --i) { tabs[i].setBounds (head.removeFromRight (84)); head.removeFromRight (6); }

    area.reduce (12, 4);
    area.removeFromBottom (8);
    auto outArea = area.removeFromRight (176);
    for (auto& pn : panels)
        if (pn.title == "OUTPUT") pn.bounds = outArea;
    area.removeFromRight (10);

    auto setB = [this] (const juce::String& title, juce::Rectangle<int> b) { for (auto& pn : panels) if (pn.title == title) pn.bounds = b; };
    if (page == 0)
    {
        auto left = area.removeFromLeft (360);
        area.removeFromLeft (10);
        setB ("EARLY REFLECTIONS", left);
        setB ("REVERB", area);
    }
    else if (page == 1)
    {
        auto a = area;
        auto top = a.removeFromTop (a.getHeight() / 2 + 40);
        auto modB = top.removeFromLeft (top.getWidth() / 2 - 5);
        top.removeFromLeft (10);
        setB ("MODULATION", modB);
        setB ("ECHO  /  LOW BOOST", top);
        a.removeFromTop (10);
        setB ("CROSSFEED", a);
    }
    else
        setB ("POST EQ", area);

    for (auto& pn : panels)
        if (pn.page == -1 || pn.page == page) layoutPanel (pn);

    for (auto& pn : panels)
    {
        if (pn.title == "REVERB" && page == 0)
        {
            auto b = pn.bounds.reduced (12, 8);
            freeze.button.setBounds (b.getRight() - 86, b.getY() + 54 + 96 + 6 + 16, 86, 24);
        }
        if (pn.title == "POST EQ" && page == 2)
        {
            auto b = pn.bounds.reduced (12, 8);
            eqOn.button.setBounds (b.getRight() - 86, b.getY(), 86, 22);
        }
        if (pn.title == "OUTPUT")
            meterArea = pn.bounds.reduced (24, 12).withTrimmedTop (24 + 132 * 2 + 96 + 8);
    }
}

void LumenRoomsEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bg1, 0, 0, bg0, 0, (float) getHeight(), false));
    g.fillAll();

    g.setColour (txt);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("LUMEN", 18, 14, 90, 30, juce::Justification::centredLeft);
    g.setColour (amber);
    g.drawText ("ROOMS", 100, 14, 100, 30, juce::Justification::centredLeft);

    for (auto& pn : panels)
    {
        if (! (pn.page == -1 || pn.page == page)) continue;
        auto b = pn.bounds.toFloat();
        g.setColour (panelC);
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (pn.title, pn.bounds.reduced (14, 8).removeFromTop (20), juce::Justification::centredLeft);

        if (pn.title == "EARLY REFLECTIONS") drawReflections (g, pn.extra.toFloat());
        if (pn.title == "REVERB") drawDecay (g, pn.extra.toFloat());
        if (pn.title == "POST EQ")
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (12.5f));
            g.drawFittedText ("Shapes the whole wet signal (reflections + reverb) after mixing.\n"
                              "Turn on with EQ ON. Dry signal is never touched.",
                              pn.extra.reduced (4), juce::Justification::topLeft, 3);
        }
    }
    if (! meterArea.isEmpty()) drawMeter (g, meterArea.toFloat());
}

void LumenRoomsEditor::drawReflections (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg0);
    g.fillRoundedRectangle (r, 5.0f);
    const auto v = proc.getErView();
    float maxT = 120.0f;
    for (int k = 0; k < 12; ++k) maxT = juce::jmax (maxT, v.timeMs[k] + v.lenMs[k]);
    maxT = std::ceil (maxT / 50.0f) * 50.0f;
    auto plot = r.reduced (8, 8);
    plot.removeFromBottom (12);
    g.setFont (juce::FontOptions (10.0f));
    for (float t = 0; t <= maxT; t += 50.0f)
    {
        const float x = plot.getX() + plot.getWidth() * t / maxT;
        g.setColour (edge);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (dim);
        g.drawText (juce::String ((int) t) + "ms", (int) x - 20, (int) plot.getBottom() + 1, 40, 11, juce::Justification::centred);
    }
    for (int k = 0; k < 12; ++k)
    {
        const float x0 = plot.getX() + plot.getWidth() * v.timeMs[k] / maxT;
        const float w = juce::jmax (2.0f, plot.getWidth() * v.lenMs[k] / maxT);
        const float hNorm = juce::jlimit (0.0f, 1.0f, (v.db[k] + 70.0f) / 60.0f);
        const float h = plot.getHeight() * hNorm;
        const bool cross = (k & 1) != 0;
        juce::ColourGradient grad ((cross ? amber : teal).withAlpha (0.95f), x0, plot.getBottom() - h,
                                   (cross ? amber : teal).withAlpha (0.15f), x0 + w, plot.getBottom() - h, false);
        g.setGradientFill (grad);
        g.fillRect (x0, plot.getBottom() - h, w, h);
    }
    g.setColour (dim);
    g.drawText ("same side", plot.removeFromTop (12).removeFromRight (120), juce::Justification::centredRight);
    g.setColour (teal);
    g.fillRect (plot.getRight() - 132, plot.getY() - 9, 8.0f, 6.0f);
}

void LumenRoomsEditor::drawDecay (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg0);
    g.fillRoundedRectangle (r, 5.0f);
    auto plot = r.reduced (10, 8);
    plot.removeFromBottom (12);
    const bool inf = proc.apvts.getRawParameterValue ("infinite")->load() > 0.5f;
    const float T = proc.apvts.getRawParameterValue ("time")->load();
    const float bm = proc.apvts.getRawParameterValue ("bassMult")->load(), bf = proc.apvts.getRawParameterValue ("bassFreq")->load();
    const float tm = proc.apvts.getRawParameterValue ("trebMult")->load(), tf = proc.apvts.getRawParameterValue ("trebFreq")->load();
    auto rtAt = [&] (float f)
    {
        const float lb = 1.0f / (1.0f + std::pow (f / bf, 2.0f));
        const float hx = std::log2 (f / tf);
        const float hb = juce::jlimit (0.0f, 1.0f, 0.5f + 0.22f * hx);
        return T * (1.0f + (bm - 1.0f) * lb) * (1.0f + (tm - 1.0f) * hb);
    };
    float maxRT = 0;
    for (int i = 0; i <= 60; ++i) maxRT = juce::jmax (maxRT, rtAt (20.0f * std::pow (1000.0f, i / 60.0f)));
    maxRT = juce::jmax (1.0f, maxRT * 1.15f);
    juce::Path p;
    for (int i = 0; i <= 120; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / 120.0f);
        const float x = plot.getX() + plot.getWidth() * (float) i / 120.0f;
        const float y = plot.getBottom() - plot.getHeight() * (inf ? 0.9f : rtAt (f) / maxRT);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (amber.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (amber);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = plot.getX() + plot.getWidth() * std::log10 (f / 20.0f) / 3.0f;
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 15, (int) plot.getBottom() + 1, 30, 11,
                    juce::Justification::centred);
    }
    g.setColour (txt);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (inf ? "FREEZE" : ("decay  " + juce::String (T, 2) + " s"), plot.toNearestInt().removeFromTop (16), juce::Justification::topRight);
}

void LumenRoomsEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto bars = r.withSizeKeepingCentre (40, r.getHeight());
    for (int c = 0; c < 2; ++c)
    {
        auto b = bars.removeFromLeft (16);
        bars.removeFromLeft (8);
        g.setColour (bg0);
        g.fillRoundedRectangle (b, 3.0f);
        const float db = juce::Decibels::gainToDecibels (meter[c], -60.0f);
        const float h = b.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -1.0f ? juce::Colours::red : amber);
        g.fillRoundedRectangle (b.withTop (b.getBottom() - h), 3.0f);
    }
}

void LumenRoomsEditor::timerCallback()
{
    for (int c = 0; c < 2; ++c) meter[c] = proc.getOutputLevel (c);
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    repaint();
}
