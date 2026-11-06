#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff14171b), panelBg (0xff1b1f24), line (0xff2c333b), text (0xffe6ebef), dim (0xff8a949e);
    const juce::Colour accents[mh::numMods] = { juce::Colour (0xff4fd1e8), juce::Colour (0xffffa94d),
                                                juce::Colour (0xffe36bd8), juce::Colour (0xffa6e35a) };
    const char* typeNames[] = { "LFO", "ENV", "SIDECHAIN", "RANDOM", "STEPS", "LINK" };
    constexpr int topH = 44, panelH = 336, minW = 860;

    juce::Font font (float h, bool bold = false)
    {
        juce::Font f { juce::FontOptions (h) };
        return bold ? f.boldened() : f;
    }

    void styleKnob (juce::Slider& s, juce::Label& l, const juce::String& name, juce::Component& parent)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, text);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, line);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (font (11.0f, true));
        l.setColour (juce::Label::textColourId, dim);
        parent.addAndMakeVisible (s);
        parent.addAndMakeVisible (l);
    }

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252b32));
        b.setColour (juce::TextButton::textColourOffId, text);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
}

ModHostEditor::ModHostEditor (ModHostProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    styleButton (pluginButton);
    pluginButton.onClick = [this] { showPluginMenu(); };
    addAndMakeVisible (pluginButton);

    styleButton (uiButton);
    uiButton.onClick = [this]
    {
        showUI = ! showUI;
        if (showUI) makeHostedEditor(); else { if (hostedEditor) hostedEditor->removeComponentListener (this); hostedEditor.reset(); }
        updateSize();
    };
    addAndMakeVisible (uiButton);

    status.setFont (font (12.0f));
    status.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (status);

    for (int m = 0; m < mh::numMods; ++m)
    {
        tabs.push_back (std::make_unique<Tab> (*this, m));
        addAndMakeVisible (*tabs.back());
    }
    addAndMakeVisible (scope);

    for (auto* b : { &typeBox, &shapeBox, &divBox })
    {
        b->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff252b32));
        b->setColour (juce::ComboBox::textColourId, text);
        b->setColour (juce::ComboBox::outlineColourId, line);
        addAndMakeVisible (*b);
    }
    typeBox.addItemList ({ "LFO", "ENV (input)", "SIDECHAIN", "RANDOM", "STEPS", "LINK (Fader Pilot / Balance Scope)" }, 1);
    shapeBox.addItemList ({ "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE" }, 1);
    for (int i = 0; i < mh::numDivs; ++i) divBox.addItem (mh::divName (i), i + 1);

    for (auto* b : { &syncButton, &biButton })
    {
        styleButton (*b);
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }

    styleKnob (rate, rateL, "RATE", *this);
    styleKnob (attack, attackL, "ATTACK", *this);
    styleKnob (release, releaseL, "RELEASE", *this);
    styleKnob (gain, gainL, "GAIN", *this);
    styleKnob (smooth, smoothL, "SMOOTH", *this);
    styleKnob (phase, phaseL, "PHASE", *this);
    styleKnob (steps, stepsL, "STEPS", *this);

    for (int t = 0; t < mh::numTargets; ++t)
    {
        auto& r = rows[t];
        for (auto* b : { &r.learn, &r.target, &r.clear }) { styleButton (*b); addAndMakeVisible (*b); }
        r.learn.setClickingTogglesState (false);
        r.learn.onClick = [this, t]
        {
            const int slot = sel * mh::numTargets + t;
            if (proc.learning() == slot) proc.stopLearn(); else proc.startLearn (sel, t);
        };
        r.target.onClick = [this, t] { showTargetMenu (t); };
        r.clear.onClick = [this, t] { proc.setTarget (sel, t, -1); };
        r.amount.setSliderStyle (juce::Slider::LinearBar);
        r.amount.setColour (juce::Slider::trackColourId, accents[0].withAlpha (0.5f));
        r.amount.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252b32));
        r.amount.setColour (juce::Slider::textBoxTextColourId, text);
        r.amount.setColour (juce::Slider::textBoxOutlineColourId, line);
        r.amount.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (r.amount);
    }

    proc.addHostListener (this);
    selectMod (0);
    if (proc.hostedPlugin() != nullptr) makeHostedEditor();
    hostedPluginChanged();
    updateSize();
    startTimerHz (30);
}

ModHostEditor::~ModHostEditor()
{
    proc.removeHostListener (this);
    proc.stopLearn();
    if (hostedEditor) hostedEditor->removeComponentListener (this);
    hostedEditor.reset();
}

// ───────────────────────── 中のプラグインの画面 ─────────────────────────

void ModHostEditor::makeHostedEditor()
{
    if (hostedEditor != nullptr) return;
    auto* p = proc.hostedPlugin();
    if (p == nullptr) return;
    juce::AudioProcessorEditor* e = p->createEditorIfNeeded();
    if (e == nullptr) e = new juce::GenericAudioProcessorEditor (*p);
    hostedEditor.reset (e);
    addAndMakeVisible (*hostedEditor);
    hostedEditor->addComponentListener (this);
}

void ModHostEditor::hostedPluginAboutToChange()
{
    if (hostedEditor) hostedEditor->removeComponentListener (this);
    hostedEditor.reset();
}

void ModHostEditor::hostedPluginChanged()
{
    auto* p = proc.hostedPlugin();
    if (p != nullptr && showUI) makeHostedEditor();
    pluginButton.setButtonText (proc.isLoading() ? juce::String ("Loading...")
                                : p != nullptr ? p->getName() + juce::String::fromUTF8 ("  ▾")
                                               : juce::String::fromUTF8 ("CHOOSE A PLUGIN  ▾"));
    uiButton.setButtonText (showUI ? "HIDE UI" : "SHOW UI");
    uiButton.setEnabled (p != nullptr);
    status.setText (proc.lastError().isNotEmpty() ? "Could not open: " + proc.lastError()
                    : p != nullptr ? juce::String (p->getParameters().size()) + " params"
                                   : juce::String ("Load an effect, then LEARN its knobs"), juce::dontSendNotification);
    updateSize();
    repaint();
}

void ModHostEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (&c == hostedEditor.get() && wasResized) updateSize();
}

void ModHostEditor::updateSize()
{
    const int hw = hostedEditor != nullptr ? hostedEditor->getWidth() : 0;
    const int hh = hostedEditor != nullptr ? hostedEditor->getHeight() : 0;
    const int w = juce::jmax (minW, hw);
    const int h = topH + hh + panelH;
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.setSources (proc.link, { plink::kindFaderPilot, plink::kindBalanceScope });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! proc.link.on()) return "Plugin Link OFF - set a MOD to LINK to follow Fader Pilot / Balance Scope";
        const int f = proc.linkSourceNow.load();
        lit = f > 0;
        if (f == 0) return "waiting for Fader Pilot or Balance Scope";
        return juce::String (f == 1 ? "Fader Pilot ride" : "Balance Scope mix level") + " -> LINK = " + juce::String (juce::roundToInt (proc.linkValueNow.load() * 100.0f)) + " %";
    };
    if (getWidth() != w || getHeight() != h + footerH) setSize (w, h + footerH);
    else resized();
}

void ModHostEditor::showPluginMenu()
{
    menuCache = proc.scanEffects();
    juce::PopupMenu menu;
    menu.addItem (1, "(empty)", proc.hostedPlugin() != nullptr);
    menu.addSeparator();

    juce::String curMaker; juce::PopupMenu sub;
    auto flush = [&]
    {
        if (curMaker.isNotEmpty()) menu.addSubMenu (curMaker, sub);
        sub = {};
    };
    for (size_t i = 0; i < menuCache.size(); ++i)
    {
        const auto maker = juce::String::fromUTF8 (menuCache[i].manufacturer.c_str());
        if (maker != curMaker) { flush(); curMaker = maker; }
        sub.addItem ((int) i + 100, juce::String::fromUTF8 (menuCache[i].name.c_str()));
    }
    flush();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pluginButton),
        [safe = juce::Component::SafePointer<ModHostEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) { safe->proc.unloadPlugin(); return; }
            const size_t i = (size_t) (r - 100);
            if (i < safe->menuCache.size()) safe->proc.loadPlugin (safe->menuCache[i]);
        });
}

void ModHostEditor::showTargetMenu (int t)
{
    const int n = proc.numHostedParams();
    if (n == 0) return;
    juce::PopupMenu menu;
    menu.addItem (1, "LEARN (move a knob in the plugin)");
    menu.addItem (2, "(none)", proc.targetOf (sel, t) >= 0);
    menu.addSeparator();
    const int cur = proc.targetOf (sel, t);
    if (n <= 40)
        for (int i = 0; i < n; ++i) menu.addItem (i + 100, proc.paramName (i), true, i == cur);
    else
        for (int g = 0; g < n; g += 32)
        {
            juce::PopupMenu sub;
            for (int i = g; i < juce::jmin (n, g + 32); ++i) sub.addItem (i + 100, proc.paramName (i), true, i == cur);
            menu.addSubMenu (juce::String (g + 1) + " - " + juce::String (juce::jmin (n, g + 32)) + "  " + proc.paramName (g), sub);
        }

    const int m = sel;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rows[t].target),
        [safe = juce::Component::SafePointer<ModHostEditor> (this), m, t] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->proc.startLearn (m, t);
            else if (r == 2) safe->proc.setTarget (m, t, -1);
            else safe->proc.setTarget (m, t, r - 100);
        });
}

// ───────────────────────── MOD の切り替え ─────────────────────────

void ModHostEditor::selectMod (int m)
{
    sel = m;
    auto& s = proc.apvts;
    typeAtt.reset(); shapeAtt.reset(); divAtt.reset(); syncAtt.reset(); biAtt.reset();
    rateAtt.reset(); attackAtt.reset(); releaseAtt.reset(); gainAtt.reset(); smoothAtt.reset(); phaseAtt.reset(); stepsAtt.reset();
    typeAtt    = std::make_unique<CA> (s, ModHostProcessor::mid (m, "type"), typeBox);
    shapeAtt   = std::make_unique<CA> (s, ModHostProcessor::mid (m, "shape"), shapeBox);
    divAtt     = std::make_unique<CA> (s, ModHostProcessor::mid (m, "div"), divBox);
    syncAtt    = std::make_unique<BA> (s, ModHostProcessor::mid (m, "sync"), syncButton);
    biAtt      = std::make_unique<BA> (s, ModHostProcessor::mid (m, "bipolar"), biButton);
    rateAtt    = std::make_unique<SA> (s, ModHostProcessor::mid (m, "rate"), rate);
    attackAtt  = std::make_unique<SA> (s, ModHostProcessor::mid (m, "attack"), attack);
    releaseAtt = std::make_unique<SA> (s, ModHostProcessor::mid (m, "release"), release);
    gainAtt    = std::make_unique<SA> (s, ModHostProcessor::mid (m, "gain"), gain);
    smoothAtt  = std::make_unique<SA> (s, ModHostProcessor::mid (m, "smooth"), smooth);
    phaseAtt   = std::make_unique<SA> (s, ModHostProcessor::mid (m, "phase"), phase);
    stepsAtt   = std::make_unique<SA> (s, ModHostProcessor::mid (m, "steps"), steps);

    const auto ac = accents[m];
    for (auto* k : { &rate, &attack, &release, &gain, &smooth, &phase, &steps })
    {
        k->setColour (juce::Slider::rotarySliderFillColourId, ac);
        k->setColour (juce::Slider::thumbColourId, ac);
    }
    for (auto* b : { &syncButton, &biButton }) b->setColour (juce::TextButton::buttonOnColourId, ac);
    for (int t = 0; t < mh::numTargets; ++t)
    {
        rows[t].att.reset();
        rows[t].att = std::make_unique<SA> (s, ModHostProcessor::amountId (m, t), rows[t].amount);
        rows[t].amount.setColour (juce::Slider::trackColourId, ac.withAlpha (0.45f));
    }
    std::fill (std::begin (scope.history), std::end (scope.history), 0.0f);
    lastType = -1;
    updateVisibility();
    repaint();
}

void ModHostEditor::updateVisibility()
{
    const int type = typeBox.getSelectedId() - 1;
    const bool sync = syncButton.getToggleState();
    const bool timed = type == mh::typeLfo || type == mh::typeRandom || type == mh::typeSteps;
    shapeBox.setVisible (type == mh::typeLfo);
    syncButton.setVisible (timed);
    divBox.setVisible (timed && sync);
    auto show = [] (juce::Slider& s, juce::Label& l, bool v) { s.setVisible (v); l.setVisible (v); };
    show (rate, rateL, timed && ! sync);
    show (phase, phaseL, type == mh::typeLfo);
    show (attack, attackL, type == mh::typeEnv || type == mh::typeSidechain || type == mh::typeLink);
    show (release, releaseL, type == mh::typeEnv || type == mh::typeSidechain || type == mh::typeLink);
    show (gain, gainL, type == mh::typeEnv || type == mh::typeSidechain || type == mh::typeLink);
    show (smooth, smoothL, type == mh::typeRandom || type == mh::typeSteps);
    show (steps, stepsL, type == mh::typeSteps);
    stepsL.setText (sync ? "STEPS" : "STEPS", juce::dontSendNotification);
    lastType = type; lastSync = sync ? 1 : 0;
    resized();
}

// ───────────────────────── 描画 ─────────────────────────

void ModHostEditor::Tab::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = ed.sel == index;
    const auto ac = accents[index];
    g.setColour (on ? ac.withAlpha (0.18f) : juce::Colour (0xff20252b));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? ac : line);
    g.drawRoundedRectangle (r, 5.0f, on ? 1.6f : 1.0f);

    auto inner = r.reduced (10.0f, 4.0f);
    g.setColour (on ? text : dim);
    g.setFont (font (13.0f, true));
    g.drawText ("MOD " + juce::String (index + 1), inner.removeFromLeft (52.0f), juce::Justification::centredLeft);
    const int type = (int) ed.proc.apvts.getRawParameterValue (ModHostProcessor::mid (index, "type"))->load();
    g.setFont (font (11.0f));
    g.drawText (typeNames[juce::jlimit (0, 5, type)], inner.removeFromLeft (76.0f), juce::Justification::centredLeft);

    // 今の値
    auto bar = inner.withSizeKeepingCentre (inner.getWidth(), 6.0f);
    g.setColour (juce::Colour (0xff2a3037));
    g.fillRoundedRectangle (bar, 3.0f);
    const float v = ed.proc.modValue (index);
    const bool bi = ed.proc.modBipolar (index);
    const float x0 = bi ? bar.getCentreX() : bar.getX();
    const float x1 = bi ? bar.getCentreX() + v * bar.getWidth() * 0.5f : bar.getX() + v * bar.getWidth();
    g.setColour (ac);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x0, x1), bar.getY(), std::abs (x1 - x0) + 1.0f, bar.getHeight()), 3.0f);

    // つながっている数
    int n = 0;
    for (int t = 0; t < mh::numTargets; ++t) n += ed.proc.targetOf (index, t) >= 0 ? 1 : 0;
    if (n > 0)
    {
        g.setColour (ac);
        g.setFont (font (10.0f, true));
        g.drawText (juce::String (n) + (n == 1 ? " target" : " targets"), getLocalBounds().reduced (10, 3), juce::Justification::topRight);
    }
}

void ModHostEditor::Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111418));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const int m = ed.sel;
    const auto ac = accents[m];
    const bool bi = ed.proc.modBipolar (m);
    auto plot = r.reduced (8.0f, 10.0f);
    auto yOf = [&] (float v) { const float u = bi ? 0.5f * (v + 1.0f) : v; return plot.getBottom() - u * plot.getHeight(); };

    const int type = ed.typeBox.getSelectedId() - 1;
    if (type == mh::typeSteps)
    {
        const int n = (int) ed.steps.getValue();
        const float w = plot.getWidth() / (float) juce::jmax (1, n);
        for (int k = 0; k < n; ++k)
        {
            const float v = ed.proc.stepValue (m, k);
            const float top = plot.getBottom() - v * plot.getHeight();
            g.setColour (ac.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (plot.getX() + k * w + 1.5f, top, w - 3.0f, plot.getBottom() - top));
        }
        // 今の値
        g.setColour (text.withAlpha (0.8f));
        g.drawHorizontalLine ((int) yOf (ed.proc.modValue (m)), plot.getX(), plot.getRight());
        g.setColour (dim);
        g.setFont (font (10.0f));
        g.drawText ("drag to draw steps", r.reduced (8.0f, 3.0f), juce::Justification::topRight);
        return;
    }

    if (bi)
    {
        g.setColour (line);
        g.drawHorizontalLine ((int) yOf (0.0f), plot.getX(), plot.getRight());
    }
    juce::Path p;
    const int n = (int) std::size (history);
    for (int i = 0; i < n; ++i)
    {
        const float v = history[(write + i) % n];
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        if (i == 0) p.startNewSubPath (x, yOf (v)); else p.lineTo (x, yOf (v));
    }
    g.setColour (ac);
    g.strokePath (p, juce::PathStrokeType (2.0f));
}

void ModHostEditor::Scope::edit (const juce::MouseEvent& e)
{
    if (ed.typeBox.getSelectedId() - 1 != mh::typeSteps) return;
    auto plot = getLocalBounds().toFloat().reduced (8.0f, 10.0f);
    const int n = (int) ed.steps.getValue();
    const int k = juce::jlimit (0, n - 1, (int) ((e.position.x - plot.getX()) / plot.getWidth() * n));
    ed.proc.setStepValue (ed.sel, k, (plot.getBottom() - e.position.y) / plot.getHeight());
    repaint();
}

void ModHostEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (font (17.0f, true));
    g.drawText ("MOD HOST", topBar.withTrimmedLeft (14).withWidth (120), juce::Justification::centredLeft);

    g.setColour (panelBg);
    g.fillRect (panel);
    g.setColour (line);
    g.drawHorizontalLine (panel.getY(), 0.0f, (float) getWidth());

    const auto ac = accents[sel];
    for (int t = 0; t < mh::numTargets; ++t)
    {
        auto b = rows[t].barArea.toFloat();
        if (b.isEmpty()) continue;
        const int idx = proc.targetOf (sel, t);
        auto track = b.withSizeKeepingCentre (b.getWidth(), 8.0f);
        g.setColour (juce::Colour (0xff2a3037));
        g.fillRoundedRectangle (track, 4.0f);
        if (idx < 0) continue;
        const float base = proc.routeBase (sel, t), now = proc.routeNow (sel, t);
        const float xb = track.getX() + base * track.getWidth(), xn = track.getX() + now * track.getWidth();
        g.setColour (ac.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (xb, xn), track.getY(), std::abs (xn - xb) + 1.0f, track.getHeight()));
        g.setColour (text);
        g.fillRect (juce::Rectangle<float> (xb - 1.0f, b.getY() + 3.0f, 2.0f, b.getHeight() - 6.0f));   // 元の位置
        g.setColour (ac);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xn, track.getCentreY() }));    // 今の位置
    }
}

void ModHostEditor::resized()
{
    layoutFooter();
    auto r = getLocalBounds().withTrimmedBottom (footerH);
    topBar = r.removeFromTop (topH);
    panel = r.removeFromBottom (panelH);
    hostArea = r;

    auto tb = topBar.reduced (10, 8);
    tb.removeFromLeft (120);
    pluginButton.setBounds (tb.removeFromLeft (280));
    tb.removeFromLeft (8);
    uiButton.setBounds (tb.removeFromLeft (84));
    tb.removeFromLeft (12);
    status.setBounds (tb);

    if (hostedEditor != nullptr)
        hostedEditor->setTopLeftPosition (hostArea.getX() + (hostArea.getWidth() - hostedEditor->getWidth()) / 2, hostArea.getY());

    auto p = panel.reduced (12, 10);
    auto tabRow = p.removeFromTop (36);
    const int tw = tabRow.getWidth() / mh::numMods;
    for (int m = 0; m < mh::numMods; ++m) tabs[(size_t) m]->setBounds (tabRow.removeFromLeft (tw));
    p.removeFromTop (10);

    auto mid = p.removeFromTop (142);
    auto left = mid.removeFromLeft (470);
    mid.removeFromLeft (12);
    scope.setBounds (mid);

    auto row1 = left.removeFromTop (26);
    typeBox.setBounds (row1.removeFromLeft (124)); row1.removeFromLeft (6);
    if (shapeBox.isVisible()) { shapeBox.setBounds (row1.removeFromLeft (110)); row1.removeFromLeft (6); }
    if (syncButton.isVisible()) { syncButton.setBounds (row1.removeFromLeft (54)); row1.removeFromLeft (6); }
    if (divBox.isVisible()) { divBox.setBounds (row1.removeFromLeft (88)); row1.removeFromLeft (6); }
    biButton.setBounds (row1.removeFromLeft (40));
    biButton.setButtonText (biButton.getToggleState() ? "BI" : "UNI");

    left.removeFromTop (8);
    auto knobs = left;
    using KL = std::pair<juce::Slider*, juce::Label*>;
    const std::array<KL, 7> knobList { KL { &rate, &rateL }, KL { &phase, &phaseL }, KL { &attack, &attackL },
                                       KL { &release, &releaseL }, KL { &gain, &gainL }, KL { &smooth, &smoothL }, KL { &steps, &stepsL } };
    for (auto [s, l] : knobList)
    {
        if (! s->isVisible()) continue;
        auto k = knobs.removeFromLeft (76);
        l->setBounds (k.removeFromTop (16));
        s->setBounds (k);
    }

    p.removeFromTop (10);
    for (int t = 0; t < mh::numTargets; ++t)
    {
        auto row = p.removeFromTop (26);
        p.removeFromTop (4);
        auto& rw = rows[t];
        rw.learn.setBounds (row.removeFromLeft (64)); row.removeFromLeft (6);
        rw.target.setBounds (row.removeFromLeft (250)); row.removeFromLeft (6);
        rw.amount.setBounds (row.removeFromLeft (200)); row.removeFromLeft (12);
        rw.clear.setBounds (row.removeFromRight (28)); row.removeFromRight (10);
        rw.barArea = row;
    }
}

void ModHostEditor::timerCallback()
{
    // 種類・SYNC が変わったら並べ直す
    const int type = typeBox.getSelectedId() - 1;
    const int sync = syncButton.getToggleState() ? 1 : 0;
    if (type != lastType || sync != lastSync) updateVisibility();
    if (biButton.getButtonText() != (biButton.getToggleState() ? "BI" : "UNI")) biButton.setButtonText (biButton.getToggleState() ? "BI" : "UNI");

    scope.history[scope.write] = proc.modValue (sel);
    scope.write = (scope.write + 1) % (int) std::size (scope.history);

    ++blink;
    const bool hasPlugin = proc.hostedPlugin() != nullptr;
    const int learning = proc.learning();
    for (int t = 0; t < mh::numTargets; ++t)
    {
        auto& rw = rows[t];
        const int idx = proc.targetOf (sel, t);
        const bool isLearning = learning == sel * mh::numTargets + t;
        rw.learn.setEnabled (hasPlugin);
        rw.target.setEnabled (hasPlugin);
        rw.learn.setColour (juce::TextButton::buttonColourId,
                            isLearning ? (blink / 8 % 2 ? accents[sel] : accents[sel].darker (0.6f)) : juce::Colour (0xff252b32));
        rw.learn.setColour (juce::TextButton::textColourOffId, isLearning ? juce::Colours::black : text);
        const juce::String name = isLearning ? juce::String ("move a knob in the plugin...")
                                : idx >= 0 ? proc.paramName (idx)
                                           : juce::String::fromUTF8 ("—  choose a knob  ▾");
        if (rw.target.getButtonText() != name) rw.target.setButtonText (name);
        rw.clear.setVisible (idx >= 0);
        rw.amount.setEnabled (idx >= 0);
        rw.amount.setAlpha (idx >= 0 ? 1.0f : 0.4f);
    }

    for (auto& t : tabs) t->repaint();
    scope.repaint();
    repaint (panel);
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void ModHostEditor::layoutFooter()
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
