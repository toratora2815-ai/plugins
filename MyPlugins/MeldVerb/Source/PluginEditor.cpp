#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0f1218), panel (0xff171c25), edge (0xff283040), text (0xffe9eef6), dim (0xff8a94a6);
    const juce::Colour colE (0xfff2b35b), colP (0xff5bc8f2), colH (0xffa98bff), accent (0xff7ee0b0), warn (0xffff8f7a);
    const juce::Colour engineCol[3] = { colE, colP, colH };
    const char* engineName[3] = { "EARLY", "PLATE", "HALL" };
    const char* lvlId[3] = { "lvlE", "lvlP", "lvlH" };

    float xForHz (float hz, juce::Rectangle<float> r) { return r.getX() + r.getWidth() * (std::log10 (hz / 20.0f) / 3.0f); }
    float hzForX (float x, juce::Rectangle<float> r) { return 20.0f * std::pow (10.0f, 3.0f * (x - r.getX()) / r.getWidth()); }

    void sectionBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        g.setColour (panel); g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (edge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (c); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 6).removeFromTop (16), juce::Justification::topLeft);
        }
    }
}

MeldVerbEditor::MeldVerbEditor (MeldVerbProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    processor.editorOpen.store (true);

    // 3 つの量（パッドの下の横スライダー）
    for (int i = 0; i < 3; ++i)
    {
        auto& k = knob (lvlId[i], engineName[i], engineCol[i]);
        k.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        k.slider.setColour (juce::Slider::trackColourId, engineCol[i].withAlpha (0.8f));
        k.slider.setColour (juce::Slider::backgroundColourId, edge);
        k.slider.textFromValueFunction = [] (double v) { return v <= -69.9 ? juce::String ("OFF") : juce::String (v, 1) + " dB"; };
        k.label.setJustificationType (juce::Justification::centredLeft);
        k.slider.onValueChange = [this] { pad.repaint(); decay.repaint(); };
    }
    addAndMakeVisible (pad);

    // 3 つの響きのつまみ
    knob ("eTime", "TIME", colE); knob ("eSize", "SIZE", colE); knob ("eDiff", "DIFFUSION", colE); knob ("eAngle", "ANGLE", colE); knob ("eLp", "LOWPASS", colE);
    for (int t = 0; t < 2; ++t)
    {
        const juce::String a = t == 0 ? "p" : "h"; const auto c = t == 0 ? colP : colH;
        knob (a + "Time", "TIME", c); knob (a + "Size", "SIZE", c); knob (a + "Diff", "DIFFUSION", c); knob (a + "Attack", "ATTACK", c);
        knob (a + "Lp", "LOWPASS", c); knob (a + "Damp", "DAMPING", c); knob (a + "Bal", "BALANCE", c); knob (a + "Cross", "CROSSOVER", c);
        toggle (a + "Sync", "SYNC", c);
        combo (a + "Note");
        knobById[a + "Bal"]->slider.textFromValueFunction = [] (double v)
        {
            if (std::abs (v - 0.5) < 0.01) return juce::String ("EVEN");
            return v < 0.5 ? "HI " + juce::String (juce::roundToInt ((0.5 - v) * 200)) + "% shorter" : "LO " + juce::String (juce::roundToInt ((v - 0.5) * 200)) + "% shorter";
        };
        knobById[a + "Bal"]->slider.updateText();
        knobById[a + "Bal"]->slider.setTooltip ("Below 0.5 the highs above CROSSOVER fade sooner, above 0.5 the lows below CROSSOVER fade sooner");
        knobById[a + "Damp"]->slider.setTooltip ("How deep the LOWPASS cuts into the tail. Higher keeps more top end");
        knobById[a + "Attack"]->slider.setTooltip ("How long the reverb takes to swell up");
    }
    knobById["eAngle"]->slider.setTooltip ("0: reflections from the source side, loud first. 40: even. 100: from the far side, building up");
    knobById["eLp"]->slider.setTooltip ("Dulls the reflections that arrive on the opposite side (head shadow)");
    knobById["eSize"]->slider.setTooltip ("How long each reflection smears. Small = tight clicks, large = joined");

    const char* tabNames[3] = { "EARLY", "PLATE", "HALL" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].setClickingTogglesState (false);
        tabs[i].setColour (juce::TextButton::buttonColourId, edge);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, engineCol[i]);
        tabs[i].setColour (juce::TextButton::textColourOffId, dim);
        tabs[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        tabs[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabs[i]);
    }
    addAndMakeVisible (decay);

    // 右の列
    knob ("predelay", "PRE-DELAY", accent); toggle ("pdSync", "SYNC", accent); combo ("pdNote");
    knob ("modRate", "RATE", accent); knob ("modDepth", "DEPTH", accent); combo ("modType");
    toggle ("smoothOn", "SMOOTHING", accent); knob ("smoothAmt", "AMOUNT", accent);
    toggleById["smoothOn"]->button.setTooltip ("Gives fresh notes and hits a little more reverb, sustained parts less, and quiet parts after loud ones much less - so the tail stays clear");
    knob ("mix", "DRY/WET", text, true); knob ("output", "OUTPUT", text, true);

    // EQ
    const char* eqNames[2] = { "PRE EQ", "REVERB EQ" };
    for (int i = 0; i < 2; ++i)
    {
        eqTabs[i].setButtonText (eqNames[i]);
        eqTabs[i].setColour (juce::TextButton::buttonColourId, edge);
        eqTabs[i].setColour (juce::TextButton::buttonOnColourId, accent);
        eqTabs[i].setColour (juce::TextButton::textColourOffId, dim);
        eqTabs[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        eqTabs[i].onClick = [this, i] { showEqTab (i); };
        addAndMakeVisible (eqTabs[i]);
    }
    for (int rev = 0; rev < 2; ++rev)
    {
        const juce::String a = rev ? "rev" : "pre";
        toggle (a + "On", "ON", accent);
        knob (a + "LowGain", "GAIN", accent); knob (a + "LowFreq", "FREQ", accent); knob (a + "LowSlope", "SLOPE", accent); combo (a + "LowShape");
        knob (a + "BellGain", "GAIN", accent); knob (a + "BellFreq", "FREQ", accent); knob (a + "BellQ", "Q", accent);
        knob (a + "HighGain", "GAIN", accent); knob (a + "HighFreq", "FREQ", accent); knob (a + "HighSlope", "SLOPE", accent); combo (a + "HighShape");
        for (auto k : { "LowGain", "LowFreq", "LowSlope", "BellGain", "BellFreq", "BellQ", "HighGain", "HighFreq", "HighSlope" })
            knobById[a + k]->slider.onValueChange = [this] { eqView.repaint(); };
    }
    knob ("unmaskAmt", "AMOUNT", accent);
    knobById["unmaskAmt"]->slider.onDragEnd = [this] { processor.applyUnmaskAmount(); };
    knobById["unmaskAmt"]->slider.setTooltip ("How much of the learned UNMASK cut to use");
    addAndMakeVisible (eqView);

    for (auto* b : { &learnButton, &unmaskButton })
    {
        b->setColour (juce::TextButton::buttonColourId, edge);
        b->setColour (juce::TextButton::textColourOffId, accent);
        addAndMakeVisible (*b);
    }
    learnButton.setTooltip ("Play the track for 5 seconds: finds the harsh resonance in the input and cuts it before the reverb (Pre EQ bell), plus a low cut for rumble");
    unmaskButton.setTooltip ("Play the track for 5 seconds: compares the dry sound with the reverb and turns the reverb down where it hides the dry sound (Reverb EQ)");
    learnButton.onClick = [this]
    {
        if (processor.learning() != MeldVerbProcessor::LearnMode::none) processor.cancelLearn();
        else { processor.startLearn (MeldVerbProcessor::LearnMode::pre); showEqTab (0); }
    };
    unmaskButton.onClick = [this]
    {
        if (processor.learning() != MeldVerbProcessor::LearnMode::none) processor.cancelLearn();
        else { processor.startLearn (MeldVerbProcessor::LearnMode::unmask); showEqTab (1); }
    };
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    presetBox.addItemList (MeldVerbProcessor::presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESET");
    presetBox.setColour (juce::ComboBox::backgroundColourId, edge);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, accent);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) processor.applyPreset (presetBox.getSelectedId() - 1); repaint(); };
    addAndMakeVisible (presetBox);

    showTab (2);
    showEqTab (1);
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the reverb by this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "Plugin Link OFF - same LINK as Fader Pilot to duck the reverb while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing: reverb " + juce::String (processor.duck.nowDb(), 1) + " dB" : juce::String ("voice resting: full reverb");
    };
    setSize (1040, 730 + footerH);
    startTimerHz (30);
}

MeldVerbEditor::~MeldVerbEditor()
{
    processor.editorOpen.store (false);
}

MeldVerbEditor::Knob& MeldVerbEditor::knob (const juce::String& id, const juce::String& name, juce::Colour c, bool big)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 80 : 70, 16);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    k->slider.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
    k->slider.setColour (juce::Slider::thumbColourId, text);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
    k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k->slider);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (big ? 13.0f : 11.0f, juce::Font::bold));
    k->label.setColour (juce::Label::textColourId, c);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k->slider);
    k->slider.setDoubleClickReturnValue (true, processor.apvts.getParameter (id)->convertFrom0to1 (processor.apvts.getParameter (id)->getDefaultValue()));
    knobById[id] = k.get();
    knobs.push_back (std::move (k));
    return *knobs.back();
}

MeldVerbEditor::Toggle& MeldVerbEditor::toggle (const juce::String& id, const juce::String& name, juce::Colour c)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (name);
    t->button.setClickingTogglesState (true);
    t->button.setColour (juce::TextButton::buttonColourId, edge);
    t->button.setColour (juce::TextButton::buttonOnColourId, c);
    t->button.setColour (juce::TextButton::textColourOffId, dim);
    t->button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t->button);
    t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t->button);
    t->button.onClick = [this] { decay.repaint(); eqView.repaint(); };
    toggleById[id] = t.get();
    toggles.push_back (std::move (t));
    return *toggles.back();
}

MeldVerbEditor::Combo& MeldVerbEditor::combo (const juce::String& id)
{
    auto c = std::make_unique<Combo>();
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        c->box.addItemList (prm->choices, 1);
    c->box.setColour (juce::ComboBox::backgroundColourId, edge);
    c->box.setColour (juce::ComboBox::outlineColourId, edge);
    c->box.setColour (juce::ComboBox::textColourId, text);
    c->box.setColour (juce::ComboBox::arrowColourId, dim);
    addAndMakeVisible (c->box);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, c->box);
    c->box.onChange = [this] { eqView.repaint(); decay.repaint(); };
    comboById[id] = c.get();
    combos.push_back (std::move (c));
    return *combos.back();
}

void MeldVerbEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (15));
    k.slider.setBounds (r);
}

void MeldVerbEditor::showTab (int t)
{
    tab = t;
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == t, juce::dontSendNotification);
    auto vis = [&] (const juce::String& id, bool v)
    {
        if (auto it = knobById.find (id); it != knobById.end()) { it->second->slider.setVisible (v); it->second->label.setVisible (v); }
        if (auto it = toggleById.find (id); it != toggleById.end()) it->second->button.setVisible (v);
        if (auto it = comboById.find (id); it != comboById.end()) it->second->box.setVisible (v);
    };
    for (auto id : { "eTime", "eSize", "eDiff", "eAngle", "eLp" }) vis (id, t == 0);
    for (int e = 0; e < 2; ++e)
    {
        const juce::String a = e == 0 ? "p" : "h";
        for (auto k : { "Time", "Size", "Diff", "Attack", "Lp", "Damp", "Bal", "Cross", "Sync", "Note" }) vis (a + k, t == e + 1);
    }
    decay.repaint();
    repaint();
}

void MeldVerbEditor::showEqTab (int t)
{
    eqTab = t;
    for (int i = 0; i < 2; ++i) eqTabs[i].setToggleState (i == t, juce::dontSendNotification);
    for (int rev = 0; rev < 2; ++rev)
    {
        const juce::String a = rev ? "rev" : "pre";
        const bool v = rev == t;
        for (auto k : { "LowGain", "LowFreq", "LowSlope", "BellGain", "BellFreq", "BellQ", "HighGain", "HighFreq", "HighSlope" })
        { knobById[a + k]->slider.setVisible (v); knobById[a + k]->label.setVisible (v); }
        comboById[a + "LowShape"]->box.setVisible (v); comboById[a + "HighShape"]->box.setVisible (v);
        toggleById[a + "On"]->button.setVisible (v);
    }
    knobById["unmaskAmt"]->slider.setVisible (t == 1); knobById["unmaskAmt"]->label.setVisible (t == 1);
    eqView.repaint();
    repaint();
}

//==============================================================================
void MeldVerbEditor::resized()
{
    layoutFooter();
    auto r = getLocalBounds().withTrimmedBottom (footerH).reduced (12);
    headerArea = r.removeFromTop (40);
    r.removeFromTop (8);
    auto top = r.removeFromTop (430);
    r.removeFromTop (10);
    eqArea = r;

    leftArea = top.removeFromLeft (300);
    top.removeFromLeft (12);
    rightArea = top.removeFromRight (296);
    top.removeFromRight (12);
    centreArea = top;

    // ヘッダー
    {
        auto h = headerArea;
        meterArea = h.removeFromRight (150).reduced (0, 8);
        h.removeFromRight (10);
        presetBox.setBounds (h.removeFromRight (180).reduced (0, 6));
    }
    // 左: パッドと量
    {
        auto a = leftArea.reduced (10);
        a.removeFromTop (20);
        pad.setBounds (a.removeFromTop (280));
        a.removeFromTop (8);
        for (int i = 0; i < 3; ++i)
        {
            auto row = a.removeFromTop (34);
            auto& k = *knobById[lvlId[i]];
            k.label.setBounds (row.removeFromLeft (56));
            k.slider.setBounds (row);
        }
    }
    // 真ん中: タブ、残響の形、つまみ
    {
        auto a = centreArea.reduced (10);
        auto tabRow = a.removeFromTop (26);
        const int tw = tabRow.getWidth() / 3;
        for (int i = 0; i < 3; ++i) tabs[i].setBounds (tabRow.removeFromLeft (tw).reduced (2, 0));
        a.removeFromTop (8);
        decay.setBounds (a.removeFromTop (150));
        a.removeFromTop (6);
        auto row1 = a.removeFromTop (110), row2 = a;
        // EARLY（1 段）
        {
            auto r1 = row1; const int w = r1.getWidth() / 5;
            for (auto id : { "eTime", "eSize", "eDiff", "eAngle", "eLp" }) placeKnob (*knobById[id], r1.removeFromLeft (w).reduced (2));
        }
        for (int e = 0; e < 2; ++e)
        {
            const juce::String p = e == 0 ? "p" : "h";
            auto r1 = row1; const int w = r1.getWidth() / 4;
            auto timeCell = r1.removeFromLeft (w).reduced (2);
            placeKnob (*knobById[p + "Time"], timeCell);
            for (auto k : { "Size", "Diff", "Attack" }) placeKnob (*knobById[p + k], r1.removeFromLeft (w).reduced (2));
            auto r2 = row2;
            auto syncCol = r2.removeFromLeft (w).reduced (4, 10);
            toggleById[p + "Sync"]->button.setBounds (syncCol.removeFromTop (24));
            syncCol.removeFromTop (6);
            comboById[p + "Note"]->box.setBounds (syncCol.removeFromTop (24));
            const int w2 = r2.getWidth() / 4;
            for (auto k : { "Lp", "Damp", "Bal", "Cross" }) placeKnob (*knobById[p + k], r2.removeFromLeft (w2).reduced (2));
        }
    }
    // 右の列
    {
        auto a = rightArea.reduced (10);
        a.removeFromTop (20);
        auto pre = a.removeFromTop (96);
        placeKnob (*knobById["predelay"], pre.removeFromLeft (90));
        pre.removeFromLeft (8);
        auto pc = pre.reduced (0, 18);
        toggleById["pdSync"]->button.setBounds (pc.removeFromTop (24).removeFromLeft (80));
        pc.removeFromTop (6);
        comboById["pdNote"]->box.setBounds (pc.removeFromTop (24).removeFromLeft (110));
        a.removeFromTop (22);
        auto mod = a.removeFromTop (96);
        placeKnob (*knobById["modRate"], mod.removeFromLeft (90));
        placeKnob (*knobById["modDepth"], mod.removeFromLeft (90));
        comboById["modType"]->box.setBounds (mod.reduced (4, 34));
        a.removeFromTop (22);
        auto sm = a.removeFromTop (96);
        auto smL = sm.removeFromLeft (110).reduced (0, 16);
        toggleById["smoothOn"]->button.setBounds (smL.removeFromTop (26));
        smoothMeterArea = smL.withTrimmedTop (8).withHeight (30);
        placeKnob (*knobById["smoothAmt"], sm.removeFromLeft (90));
        a.removeFromTop (8);
        auto mo = a;
        placeKnob (*knobById["mix"], mo.removeFromLeft (mo.getWidth() / 2).reduced (6, 0));
        placeKnob (*knobById["output"], mo.reduced (6, 0));
    }
    // EQ
    {
        auto a = eqArea.reduced (10);
        auto left = a.removeFromLeft (130);
        for (int i = 0; i < 2; ++i) { eqTabs[i].setBounds (left.removeFromTop (26)); left.removeFromTop (6); }
        left.removeFromTop (4);
        auto onRow = left.removeFromTop (24);
        toggleById["preOn"]->button.setBounds (onRow); toggleById["revOn"]->button.setBounds (onRow);
        left.removeFromTop (10);
        learnButton.setBounds (left.removeFromTop (26)); left.removeFromTop (6);
        unmaskButton.setBounds (left.removeFromTop (26));
        a.removeFromLeft (10);
        auto graph = a.removeFromLeft (390);
        statusLabel.setBounds (graph.removeFromBottom (18));
        eqView.setBounds (graph);
        a.removeFromLeft (10);
        // LOW / BELL / HIGH の 3 列 + UNMASK AMOUNT
        auto amt = a.removeFromRight (78);
        placeKnob (*knobById["unmaskAmt"], amt.withTrimmedTop (30).withHeight (96));
        const int cw = a.getWidth() / 3;
        for (int rev = 0; rev < 2; ++rev)
        {
            const juce::String p = rev ? "rev" : "pre";
            auto cols = a;
            auto lo = cols.removeFromLeft (cw).withTrimmedTop (16), be = cols.removeFromLeft (cw).withTrimmedTop (16), hi = cols.withTrimmedTop (16);
            auto place3 = [&] (juce::Rectangle<int> c, const char* k1, const char* k2, const char* k3, const char* shape)
            {
                auto top2 = c.removeFromTop (92); const int kw = top2.getWidth() / 2;
                placeKnob (*knobById[p + k1], top2.removeFromLeft (kw).reduced (1));
                placeKnob (*knobById[p + k2], top2.reduced (1));
                auto bot = c;
                placeKnob (*knobById[p + k3], bot.removeFromLeft (kw).withHeight (86).reduced (1));
                if (shape != nullptr) comboById[p + shape]->box.setBounds (bot.reduced (2, 30).withHeight (24));
            };
            place3 (lo, "LowGain", "LowFreq", "LowSlope", "LowShape");
            place3 (be, "BellGain", "BellFreq", "BellQ", nullptr);
            place3 (hi, "HighGain", "HighFreq", "HighSlope", "HighShape");
        }
    }
}

void MeldVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // ヘッダー
    {
        auto h = headerArea;
        g.setColour (text); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("MELD VERB", h.removeFromLeft (170), juce::Justification::centredLeft);
        g.setColour (dim); g.setFont (juce::FontOptions (12.0f));
        g.drawText ("early  /  plate  /  hall  blend reverb", h.removeFromLeft (300), juce::Justification::centredLeft);
        // IN / OUT メーター
        auto m = meterArea.toFloat();
        for (int i = 0; i < 2; ++i)
        {
            auto row = m.removeFromTop (m.getHeight() / (2 - i));
            g.setColour (dim); g.setFont (juce::FontOptions (10.0f));
            g.drawText (i == 0 ? "IN" : "OUT", row.removeFromLeft (28), juce::Justification::centredLeft);
            auto bar = row.reduced (0, 3);
            g.setColour (edge); g.fillRoundedRectangle (bar, 3.0f);
            for (int c = 0; c < 2; ++c)
            {
                const float lv = shownLevels[i * 2 + c];
                const float db = juce::Decibels::gainToDecibels (lv, -60.0f);
                const float w = bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
                auto sub = bar.withHeight (bar.getHeight() / 2).withY (bar.getY() + c * bar.getHeight() / 2).withWidth (w);
                g.setColour (db > -0.5f ? warn : accent.withAlpha (0.85f)); g.fillRect (sub);
            }
        }
    }
    sectionBox (g, leftArea, "BLEND", text);
    sectionBox (g, centreArea, {}, text);
    sectionBox (g, rightArea, "PRE-DELAY", accent);
    {
        auto a = rightArea.reduced (10);
        g.setColour (accent); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("MODULATION", a.withY (a.getY() + 20 + 96 + 4).withHeight (16), juce::Justification::topLeft);
        g.drawText ("SMOOTHING", a.withY (a.getY() + 20 + 96 + 22 + 96 + 4).withHeight (16), juce::Justification::topLeft);
    }
    sectionBox (g, eqArea, {}, accent);
    // EQ の 3 列の見出し
    {
        auto a = eqArea.reduced (10);
        a.removeFromLeft (130 + 10 + 390 + 10);
        a.removeFromRight (78);
        const int cw = a.getWidth() / 3;
        g.setColour (dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("LOW", a.removeFromLeft (cw).removeFromTop (14), juce::Justification::centred);
        g.drawText ("BELL", a.removeFromLeft (cw).removeFromTop (14), juce::Justification::centred);
        g.drawText ("HIGH", a.removeFromTop (14), juce::Justification::centred);
        if (eqTab == 1)
        {
            auto amt = eqArea.reduced (10).removeFromRight (78);
            g.drawText ("UNMASK", amt.removeFromTop (14), juce::Justification::centred);
        }
    }
    // SMOOTHING の今の量
    if (! smoothMeterArea.isEmpty())
    {
        auto m = smoothMeterArea.toFloat();
        g.setColour (edge); g.fillRoundedRectangle (m.withHeight (8), 3.0f);
        const float zeroX = m.getX() + m.getWidth() * (12.0f / 14.0f);   // -12..+2 dB
        const float x = m.getX() + m.getWidth() * juce::jlimit (0.0f, 1.0f, (shownSmooth + 12.0f) / 14.0f);
        g.setColour (param ("smoothOn") > 0.5f ? accent : dim.withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x, zeroX), m.getY(), std::max (x, zeroX), m.getY() + 8));
        g.setColour (dim); g.setFont (juce::FontOptions (10.0f));
        g.drawText ("send " + juce::String (shownSmooth, 1) + " dB", m.withTrimmedTop (10), juce::Justification::centredLeft);
    }
}

//==============================================================================
juce::Point<float> MeldVerbEditor::BlendPad::corner (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (26, 22);
    if (i == 0) return { r.getCentreX(), r.getY() };
    if (i == 1) return { r.getX(), r.getBottom() };
    return { r.getRight(), r.getBottom() };
}

void MeldVerbEditor::BlendPad::paint (juce::Graphics& g)
{
    juce::Path tri;
    tri.startNewSubPath (corner (0)); tri.lineTo (corner (1)); tri.lineTo (corner (2)); tri.closeSubPath();
    g.setColour (edge.withAlpha (0.6f)); g.fillPath (tri);
    g.setColour (edge.brighter (0.3f)); g.strokePath (tri, juce::PathStrokeType (1.2f));
    // 今の 3 つの量から、点の位置（重み）
    float w[3], sum = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float db = ed.param (lvlId[i]);
        w[i] = db <= -69.9f ? 0.0f : std::pow (10.0f, (db + 3.0f) / 10.0f) / 3.0f;
        sum += w[i];
    }
    juce::Point<float> pt = (corner (0) + corner (1) + corner (2)) / 3.0f;
    if (sum > 1e-6f) pt = (corner (0) * w[0] + corner (1) * w[1] + corner (2) * w[2]) / sum;
    for (int i = 0; i < 3; ++i)
    {
        const auto c = corner (i);
        const float db = ed.param (lvlId[i]);
        const float a = db <= -69.9f ? 0.15f : juce::jlimit (0.15f, 1.0f, 1.0f + db / 30.0f);
        g.setColour (engineCol[i].withAlpha (a)); g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (c));
        g.setColour (engineCol[i]); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto lab = juce::Rectangle<float> (80, 14).withCentre (c + juce::Point<float> (0, i == 0 ? -14.0f : 15.0f));
        g.drawText (engineName[i], lab, juce::Justification::centred);
        g.setColour (engineCol[i].withAlpha (0.25f * a)); g.drawLine ({ pt, c }, 2.0f);
    }
    g.setColour (text); g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (pt));
    g.setColour (bg); g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (pt));
}

void MeldVerbEditor::BlendPad::setFrom (juce::Point<float> q)
{
    // 三角形の中の重心座標（はみ出したら中へ）
    const auto a = corner (0), b = corner (1), c = corner (2);
    const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    float w0 = ((b.y - c.y) * (q.x - c.x) + (c.x - b.x) * (q.y - c.y)) / det;
    float w1 = ((c.y - a.y) * (q.x - c.x) + (a.x - c.x) * (q.y - c.y)) / det;
    float w2 = 1.0f - w0 - w1;
    w0 = std::max (0.0f, w0); w1 = std::max (0.0f, w1); w2 = std::max (0.0f, w2);
    const float s = w0 + w1 + w2; w0 /= s; w1 /= s; w2 /= s;
    auto toDb = [] (float w) { return w < 0.005f ? -70.0f : juce::jlimit (-70.0f, 0.0f, 10.0f * std::log10 (3.0f * w) - 3.0f); };
    ed.setLevelsFromPad (toDb (w0), toDb (w1), toDb (w2));
}

void MeldVerbEditor::BlendPad::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    for (auto id : lvlId) ed.processor.apvts.getParameter (id)->beginChangeGesture();
    setFrom (e.position);
}
void MeldVerbEditor::BlendPad::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void MeldVerbEditor::BlendPad::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    for (auto id : lvlId) ed.processor.apvts.getParameter (id)->endChangeGesture();
}

void MeldVerbEditor::setLevelsFromPad (float dbE, float dbP, float dbH)
{
    const float v[3] = { dbE, dbP, dbH };
    for (int i = 0; i < 3; ++i)
    {
        auto* prm = processor.apvts.getParameter (lvlId[i]);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v[i]));
    }
    pad.repaint(); decay.repaint();
}

//==============================================================================
void MeldVerbEditor::DecayView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (bg.darker (0.2f)); g.fillRoundedRectangle (r, 6.0f);
    const auto q = ed.processor.readParams();
    auto plot = r.reduced (8, 10).withTrimmedBottom (12);
    const float rtP = 1.32f * q.plate.timeSec, rtH = 1.25f * q.hall.timeSec;
    // 横軸: 選んでいる響きが -60 dB になるくらいまで
    float span = ed.tab == 0 ? std::max (0.12f, (q.early.timeMs + 80.0f) / 1000.0f) : (ed.tab == 1 ? rtP : rtH) * 1.1f + q.predelayMs / 1000.0f;
    span = juce::jlimit (0.1f, 30.0f, span);
    auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * t / span; };
    auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 66.0f); };
    // 目盛り
    g.setColour (edge);
    for (int db = 0; db >= -60; db -= 20) g.drawHorizontalLine ((int) yOf ((float) db), plot.getX(), plot.getRight());
    g.setColour (dim); g.setFont (juce::FontOptions (10.0f));
    for (int k = 0; k <= 4; ++k)
    {
        const float t = span * (float) k / 4.0f;
        g.drawText (t < 1.0f ? juce::String (juce::roundToInt (t * 1000)) + " ms" : juce::String (t, 2) + " s",
                    juce::Rectangle<float> (60, 12).withCentre ({ xOf (t), plot.getBottom() + 8 }), juce::Justification::centred);
    }
    const float pre = q.predelayMs / 1000.0f;
    // PLATE / HALL: 立ち上がり（ATTACK）と減衰（中域の残響時間）
    for (int e = 2; e >= 1; --e)
    {
        const auto& t = e == 1 ? q.plate : q.hall;
        const float lvl = q.levelDb[e]; if (lvl <= -69.9f) continue;
        const float rt = e == 1 ? rtP : rtH, att = t.attackMs / 1000.0f;
        juce::Path pth; bool first = true;
        for (int i = 0; i <= 160; ++i)
        {
            const float tt = span * (float) i / 160.0f, rel = tt - pre;
            if (rel < 0) continue;
            float db = lvl - 60.0f * rel / rt;
            if (att > 0.001f && rel < att) db -= (e == 2 ? 14.0f : 6.0f) * (1.0f - rel / att);
            const juce::Point<float> pt (xOf (tt), yOf (db));
            if (first) { pth.startNewSubPath (xOf (pre), yOf (-66)); pth.lineTo (pt); first = false; } else pth.lineTo (pt);
        }
        if (first) continue;
        pth.lineTo (xOf (span), yOf (-66)); pth.closeSubPath();
        const bool sel = ed.tab == e;
        g.setColour (engineCol[e].withAlpha (sel ? 0.35f : 0.12f)); g.fillPath (pth);
        g.setColour (engineCol[e].withAlpha (sel ? 1.0f : 0.4f)); g.strokePath (pth, juce::PathStrokeType (sel ? 1.6f : 1.0f));
    }
    // EARLY: 跳ね返りの棒
    if (q.levelDb[0] > -69.9f)
    {
        if (! ed.earlyCacheOk || ! (ed.earlyCacheKey == q.early)) { ed.earlyCache = ed.processor.earlyView(); ed.earlyCacheKey = q.early; ed.earlyCacheOk = true; }
        const auto& v = ed.earlyCache;
        const bool sel = ed.tab == 0;
        for (int k = 0; k < v.n; ++k)
        {
            const float t = pre + v.ms[k] / 1000.0f; if (t > span) break;
            const float db = q.levelDb[0] + v.db[k];
            const float x = xOf (t);
            g.setColour (colE.interpolatedWith (colE.darker (0.6f), v.side[k]).withAlpha (sel ? 1.0f : 0.45f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x - 1.5f, yOf (db), x + 1.5f, yOf (-66)));
        }
    }
    g.setColour (dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    juce::String info;
    if (ed.tab == 0) info << "EARLY  " << juce::String (q.early.timeMs, 1) << " ms";
    else { const auto& t = ed.tab == 1 ? q.plate : q.hall; info << (ed.tab == 1 ? "PLATE  " : "HALL  ") << "decay " << juce::String ((ed.tab == 1 ? rtP : rtH), 2) << " s (mid)"; if (ed.param (ed.tab == 1 ? "pSync" : "hSync") > 0.5f) info << "  synced " << juce::String (t.timeSec, 2) << " s"; }
    g.drawText (info, r.reduced (10, 6), juce::Justification::topRight);
}

//==============================================================================
void MeldVerbEditor::EqView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (bg.darker (0.2f)); g.fillRoundedRectangle (r, 6.0f);
    auto plot = r.reduced (6, 8);
    const bool rev = ed.eqTab == 1;
    // 目盛り
    g.setColour (edge);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine ((int) xForHz (hz, plot), plot.getY(), plot.getBottom());
    auto yDb = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.2f, 1.2f, db / 18.0f); };
    g.drawHorizontalLine ((int) yDb (0), plot.getX(), plot.getRight());
    g.setColour (dim); g.setFont (juce::FontOptions (9.0f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (30, 10).withCentre ({ xForHz (hz, plot), plot.getBottom() - 5 }), juce::Justification::centred);
    // スペクトル（入力＝灰、響き＝色）
    float mx = -120;
    for (int b = 0; b < MeldVerbProcessor::specBands; ++b) mx = std::max (mx, std::max (ed.dryDb[b], ed.wetDb[b]));
    auto ySpec = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (mx + 6.0f - db) / 66.0f); };
    for (int which = 0; which < 2; ++which)
    {
        juce::Path pth;
        for (int b = 0; b < MeldVerbProcessor::specBands; ++b)
        {
            const float hz = assist::Spectrum::centre (b); if (hz > 20000) break;
            const float db = which == 0 ? ed.dryDb[b] : ed.wetDb[b];
            const juce::Point<float> pt (xForHz (hz, plot), ySpec (db));
            if (b == 0) pth.startNewSubPath (pt); else pth.lineTo (pt);
        }
        g.setColour (which == 0 ? dim.withAlpha (0.55f) : colP.withAlpha (0.55f));
        g.strokePath (pth, juce::PathStrokeType (1.2f));
    }
    // EQ のカーブ
    const auto q = ed.processor.readParams();
    meld::TriEQ eq; const double sr = ed.processor.getRate() > 0 ? ed.processor.getRate() : 48000.0;
    eq.setup (rev ? q.revEq : q.preEq, sr);
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 200.0f;
        const float hz = hzForX (x, plot);
        const float db = (float) eq.magDb (std::min (hz, (float) (0.49 * sr)), sr);
        if (i == 0) curve.startNewSubPath (x, yDb (db)); else curve.lineTo (x, yDb (db));
    }
    const bool on = rev ? q.revEq.on : q.preEq.on;
    g.setColour (on ? accent : dim.withAlpha (0.5f)); g.strokePath (curve, juce::PathStrokeType (2.0f));
    g.setColour (dim); g.setFont (juce::FontOptions (10.0f));
    g.drawText (rev ? "reverb EQ  (grey = dry, blue = reverb)" : "pre EQ  (grey = input)", plot.reduced (4, 2), juce::Justification::topLeft);
    // LEARN の進み具合
    const auto lm = ed.processor.learning();
    if (lm != MeldVerbProcessor::LearnMode::none)
    {
        auto bar = plot.removeFromBottom (5);
        g.setColour (edge); g.fillRect (bar);
        g.setColour (accent); g.fillRect (bar.withWidth (bar.getWidth() * ed.processor.learnProgress()));
    }
}

//==============================================================================
void MeldVerbEditor::timerCallback()
{
    shownSmooth = shownSmooth * 0.7f + 0.3f * processor.getSmoothDb();
    for (int i = 0; i < 4; ++i) shownLevels[i] = std::max (processor.getLevel (i), shownLevels[i] * 0.85f);
    processor.getSpectra (dryDb, wetDb);
    const bool learning = processor.learning() != MeldVerbProcessor::LearnMode::none;
    learnButton.setButtonText (learning && processor.learning() == MeldVerbProcessor::LearnMode::pre ? "STOP" : "LEARN");
    unmaskButton.setButtonText (learning && processor.learning() == MeldVerbProcessor::LearnMode::unmask ? "STOP" : "UNMASK");
    statusLabel.setText (processor.lastLearnMessage(), juce::dontSendNotification);
    eqView.repaint();
    repaint (meterArea.expanded (2));
    repaint (smoothMeterArea.expanded (2));
    if (! pad.dragging) pad.repaint();
    decay.repaint();
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void MeldVerbEditor::layoutFooter()
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
