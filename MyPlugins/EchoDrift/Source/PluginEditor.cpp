#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff120f0b), panel (0xff1c1813), edge (0xff342c22), text (0xffefe6d8), dim (0xff9a8e7d);
    const juce::Colour amber (0xffffb454), teal (0xff5fd0c0), rose (0xffff7f6e), sand (0xffd9c29a);

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

    // スタイルの説明（画面の右上に出す）
    juce::String styleBlurb (int i)
    {
        static const char* b[] = {
            "Clean wide reel: flat, open top, soft tape compression",
            "Studio reel: a little darker each repeat, gentle saturation",
            "Cartridge echo: mid-forward, dark top, lazy random pitch drift",
            "Roller-head tape: thin lows, fast flutter, separate L/R wobble",
            "Tube-driven reel: honky mids, fizzy grit, busy flutter",
            "Worn cassette: thin, bright, wavering, squashed",
            "Bucket brigade with swirl: warm, dark repeats, slow chorus",
            "Dark bucket brigade: muffled repeats that keep falling off",
            "Oil-can drum: boxy band, smeared and gurgly",
            "Spinning drum: bright, deep fast wobble, L/R apart",
            "Telephone line: narrow band, crunchy",
            "AM radio: band-limited, warm, slightly gritty",
            "FM radio: smooth and slightly scooped",
            "Shortwave: thin, phasey, smeared",
            "Transmitter: hard mid push, compressed",
            "Digital: clean, exact repeats",
            "Analog delay: soft top, slow gentle movement",
            "Digital chorus: clean with very slow drifting pitch",
            "Analog chorus: warm, rolling chorus wobble",
            "Pedal chorus: deep, opposite L/R sweep",
            "Vibrato: fast wide pitch wobble",
            "Saturated: thick, fuzzy tape",
            "Fat: dark, round, slow random drift",
            "Distressed: pushed and crunchy",
            "Limited: compressed, even repeats",
            "Distorted: heavy grit, very dark top",
            "Squawk: nasal band push, squashed",
            "Ambient: dark and slightly smeared",
            "Diffused: smeared like a small room",
            "Splattered: blurred, splashy repeats",
            "Verbed: echoes that bloom into reverb" };
        return (i >= 0 && i < (int) (sizeof (b) / sizeof (b[0]))) ? juce::String (b[i]) : juce::String();
    }

    juce::String msText (double ms)
    {
        if (ms >= 1000.0) return juce::String (ms / 1000.0, ms >= 10000 ? 1 : 2) + " s";
        return juce::String (ms, ms < 10 ? 2 : (ms < 100 ? 1 : 0)) + " ms";
    }
}

EchoDriftEditor::EchoDriftEditor (EchoDriftProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    processor.editorOpen.store (true);

    // 時間（ECHO 1 / ECHO 2 / RHYTHM）
    for (auto id : { "time1", "time2" })
    {
        auto& k = knob (id, juce::String (id) == "time1" ? "ECHO 1" : "ECHO 2", juce::String (id) == "time1" ? amber : teal, true);
        k.slider.textFromValueFunction = [] (double v) { return msText (v); };
        k.slider.updateText();
    }
    knob ("rTime", "RHYTHM", rose, true).slider.textFromValueFunction = [] (double v) { return msText (v); };
    for (auto id : { "sync1", "note1", "sync2", "note2", "rSync", "rNote" }) combo (id);
    knob ("repeats", "REPEATS", rose);
    knob ("decay", "DECAY", rose);
    knob ("feedback", "FEEDBACK", amber, true);
    knob ("mix", "MIX", amber, true);

    knob ("saturation", "SATURATION", rose);
    knob ("lowCut", "LOW CUT", sand);
    knob ("highCut", "HIGH CUT", sand);
    knob ("wobble", "WOBBLE", teal);
    knob ("groove", "GROOVE", teal);
    knob ("feel", "FEEL", teal);
    knob ("input", "INPUT", dim);
    knob ("output", "OUTPUT", dim);

    knobById["feedback"]->slider.setTooltip ("How much of each echo comes back. Above 100% it runs away, held back by the style's saturation");
    knobById["mix"]->slider.setTooltip ("Dry / echo balance (equal power)");
    knobById["saturation"]->slider.setTooltip ("Drives the echo path harder. Each style breaks up in its own way");
    knobById["lowCut"]->slider.setTooltip ("Thins the echoes (on the way out)");
    knobById["highCut"]->slider.setTooltip ("Darkens the echoes a little more every repeat (inside the loop)");
    knobById["wobble"]->slider.setTooltip ("Scales the style's own wow and flutter. 100% = as the style is, 0% = steady");
    knobById["groove"]->slider.setTooltip ("Swing: pushes every other echo late (+) or early (-), as a share of the echo time");
    knobById["feel"]->slider.setTooltip ("Moves all echoes later (+) or earlier (-), as a share of one beat");
    knobById["repeats"]->slider.setTooltip ("How many taps in the rhythm (the whole pattern repeats every 2 beats with FEEDBACK)");
    knobById["decay"]->slider.setTooltip ("How much quieter the last tap is than the first");
    knobById["time1"]->slider.setTooltip ("Echo time (left in Dual, first bounce in Ping-Pong)");
    knobById["time2"]->slider.setTooltip ("Right echo time in Dual, second bounce in Ping-Pong");

    // モード
    const char* modeNames[4] = { "SINGLE", "DUAL", "PING-PONG", "RHYTHM" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (modeNames[i]);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, edge);
        b.setColour (juce::TextButton::buttonOnColourId, amber);
        b.setColour (juce::TextButton::textColourOffId, dim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { setParam ("mode", (float) i); };
        addAndMakeVisible (b);
    }
    modeButtons[0].setTooltip ("One echo time, both sides");
    modeButtons[1].setTooltip ("Separate echo times for left and right");
    modeButtons[2].setTooltip ("Left and right summed, bouncing left (ECHO 1) then right (ECHO 2)");
    modeButtons[3].setTooltip ("A row of taps (RHYTHM, REPEATS, DECAY) repeating every 2 beats");

    primeButton.setClickingTogglesState (true);
    primeButton.setColour (juce::TextButton::buttonColourId, edge);
    primeButton.setColour (juce::TextButton::buttonOnColourId, teal);
    primeButton.setColour (juce::TextButton::textColourOffId, dim);
    primeButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    primeButton.setTooltip ("Round echo times to a prime number of milliseconds (repeats line up less, sounds smoother)");
    addAndMakeVisible (primeButton);
    primeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "prime", primeButton);

    // スタイル
    auto& st = combo ("style");
    st.box.setColour (juce::ComboBox::textColourId, amber);
    st.box.setJustificationType (juce::Justification::centred);
    for (auto* b : { &stylePrev, &styleNext })
    {
        b->setColour (juce::TextButton::buttonColourId, edge);
        b->setColour (juce::TextButton::textColourOffId, text);
        addAndMakeVisible (*b);
    }
    stylePrev.onClick = [this] { const int s = (int) param ("style"); setParam ("style", (float) ((s + echo::numProfiles() - 1) % echo::numProfiles())); };
    styleNext.onClick = [this] { const int s = (int) param ("style"); setParam ("style", (float) ((s + 1) % echo::numProfiles())); };

    presetBox.addItemList (EchoDriftProcessor::presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESET");
    presetBox.setColour (juce::ComboBox::backgroundColourId, edge);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, amber);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) processor.applyPreset (presetBox.getSelectedId() - 1); repaint(); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (view);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the echoes by this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "Plugin Link OFF - same LINK as Fader Pilot to duck the echoes while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing: echoes " + juce::String (processor.duck.nowDb(), 1) + " dB" : juce::String ("voice resting: full echoes");
    };

    setSize (1000, 600 + footerH);
    updateVisibility();
    startTimerHz (30);
}

EchoDriftEditor::~EchoDriftEditor()
{
    processor.editorOpen.store (false);
}

void EchoDriftEditor::setParam (const juce::String& id, float value)
{
    if (auto* prm = processor.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

EchoDriftEditor::Knob& EchoDriftEditor::knob (const juce::String& id, const juce::String& name, juce::Colour c, bool big)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 84 : 70, 16);
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
    k->slider.onValueChange = [this] { view.repaint(); };
    knobById[id] = k.get();
    knobs.push_back (std::move (k));
    return *knobs.back();
}

EchoDriftEditor::Combo& EchoDriftEditor::combo (const juce::String& id)
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
    c->box.onChange = [this] { updateVisibility(); view.repaint(); repaint(); };
    comboById[id] = c.get();
    combos.push_back (std::move (c));
    return *combos.back();
}

void EchoDriftEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (15));
    k.slider.setBounds (r);
}

// モードと同期の選び方に合わせて、出すつまみを変える
void EchoDriftEditor::updateVisibility()
{
    const int mode = (int) param ("mode");
    const int s1 = (int) param ("sync1"), s2 = (int) param ("sync2"), sr = (int) param ("rSync");
    if (mode == lastMode && s1 == lastSync[0] && s2 == lastSync[1] && sr == lastSync[2]) return;
    lastMode = mode; lastSync[0] = s1; lastSync[1] = s2; lastSync[2] = sr;
    for (int i = 0; i < 4; ++i) modeButtons[i].setToggleState (i == mode, juce::dontSendNotification);
    const bool rhythm = mode == 3, two = mode == 1 || mode == 2;
    auto showK = [&] (const char* id, bool v) { knobById[id]->slider.setVisible (v); knobById[id]->label.setVisible (v); };
    auto showC = [&] (const char* id, bool v) { comboById[id]->box.setVisible (v); };
    showK ("time1", ! rhythm && s1 == 0);
    showC ("sync1", ! rhythm); showC ("note1", ! rhythm && s1 != 0);
    showK ("time2", two && s2 == 0);
    showC ("sync2", two); showC ("note2", two && s2 != 0);
    showK ("rTime", rhythm && sr == 0);
    showC ("rSync", rhythm); showC ("rNote", rhythm && sr != 0);
    showK ("repeats", rhythm); showK ("decay", rhythm);
    resized();
    repaint();
}

//==============================================================================
void EchoDriftEditor::resized()
{
    layoutFooter();
    auto r = getLocalBounds().withTrimmedBottom (footerH).reduced (12);
    headerArea = r.removeFromTop (44);
    {
        auto h = headerArea;
        h.removeFromLeft (190);   // タイトル
        presetBox.setBounds (h.removeFromRight (180).reduced (0, 8));
        h.removeFromRight (14);
        auto modes = h.removeFromRight (360).reduced (0, 8);
        const int w = modes.getWidth() / 4;
        for (int i = 0; i < 4; ++i) modeButtons[i].setBounds (modes.removeFromLeft (w).reduced (2, 0));
        h.removeFromRight (14);
        auto st = h.reduced (0, 6);
        stylePrev.setBounds (st.removeFromLeft (28));
        styleNext.setBounds (st.removeFromRight (28));
        comboById["style"]->box.setBounds (st.reduced (4, 0));
    }
    r.removeFromTop (10);
    auto top = r.removeFromTop (330);
    echoArea = top.removeFromLeft (440);
    top.removeFromLeft (10);
    viewArea = top;
    view.setBounds (viewArea.reduced (10).withTrimmedTop (18));

    // ECHO の欄: 上に時間 2 つ（RHYTHM のときは RHYTHM / REPEATS / DECAY）、下に FEEDBACK と MIX
    {
        auto e = echoArea.reduced (10).withTrimmedTop (20);
        auto row1 = e.removeFromTop (150);
        auto cell = [&] (juce::Rectangle<int>& row, int w) { return row.removeFromLeft (w); };
        auto a = cell (row1, row1.getWidth() / 2), b = row1;
        auto timeCell = [&] (juce::Rectangle<int> c, const char* tId, const char* sId, const char* nId)
        {
            auto bottom = c.removeFromBottom (24).reduced (6, 0);
            comboById[sId]->box.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 0));
            if (nId) comboById[nId]->box.setBounds (bottom.reduced (2, 0));
            placeKnob (*knobById[tId], c.withSizeKeepingCentre (130, c.getHeight()));
        };
        timeCell (a, "time1", "sync1", "note1");
        timeCell (b, "time2", "sync2", "note2");
        // RHYTHM は ECHO 1 の場所
        timeCell (a, "rTime", "rSync", "rNote");
        comboById["note1"]->box.setBounds (comboById["note1"]->box.getBounds());
        placeKnob (*knobById["repeats"], b.removeFromLeft (b.getWidth() / 2).withSizeKeepingCentre (90, 110));
        placeKnob (*knobById["decay"], b.withSizeKeepingCentre (90, 110));
        e.removeFromTop (8);
        auto row2 = e;
        placeKnob (*knobById["feedback"], row2.removeFromLeft (row2.getWidth() / 2).withSizeKeepingCentre (130, row2.getHeight()));
        placeKnob (*knobById["mix"], row2.withSizeKeepingCentre (130, row2.getHeight()));
    }

    r.removeFromTop (10);
    toneArea = r;
    {
        auto t = toneArea.reduced (10).withTrimmedTop (18);
        meterArea = t.removeFromRight (70);
        auto pb = t.removeFromRight (80);
        primeButton.setBounds (pb.withSizeKeepingCentre (64, 28));
        const char* ids[8] = { "saturation", "lowCut", "highCut", "wobble", "groove", "feel", "input", "output" };
        const int w = t.getWidth() / 8;
        for (auto id : ids) placeKnob (*knobById[id], t.removeFromLeft (w).withSizeKeepingCentre (w - 6, std::min (t.getHeight(), 120)));
    }
}

void EchoDriftEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
    f.removeFromRight (6);
    linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));
}

void EchoDriftEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // タイトル
    g.setColour (amber); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("ECHO DRIFT", headerArea.withWidth (190), juce::Justification::centredLeft);
    g.setColour (dim); g.setFont (juce::FontOptions (10.0f));
    g.drawText ("tape / bucket / radio echoes", headerArea.withWidth (190).translated (0, 16), juce::Justification::centredLeft);

    sectionBox (g, echoArea, "ECHO", amber);
    sectionBox (g, viewArea, "", amber);
    sectionBox (g, toneArea, "TONE / MOTION", sand);

    // 表示欄の見出し: スタイルの説明
    {
        const int st = (int) param ("style");
        auto t = viewArea.reduced (10, 6).removeFromTop (16);
        g.setColour (amber); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (echo::profiles()[juce::jlimit (0, echo::numProfiles() - 1, st)].name).toUpperCase(), t, juce::Justification::topLeft);
        g.setColour (dim); g.setFont (juce::FontOptions (11.0f));
        g.drawText (styleBlurb (st), t, juce::Justification::topRight);
    }

    // メーター（IN / OUT）
    {
        auto m = meterArea.reduced (4, 6);
        const char* nm[2] = { "IN", "OUT" };
        for (int s = 0; s < 2; ++s)
        {
            auto col = m.removeFromLeft (m.getWidth() / (2 - s));
            g.setColour (dim); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (nm[s], col.removeFromBottom (14), juce::Justification::centred);
            for (int c = 0; c < 2; ++c)
            {
                auto bar = col.removeFromLeft (col.getWidth() / (2 - c)).reduced (3, 0).toFloat();
                g.setColour (edge); g.fillRoundedRectangle (bar, 2.0f);
                const float db = juce::Decibels::gainToDecibels (shownLevels[s * 2 + c], -60.0f);
                const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                auto fill = bar.withTrimmedTop (bar.getHeight() * (1.0f - frac));
                g.setColour (db > -1.0f ? rose : (s == 0 ? sand : amber)); g.fillRoundedRectangle (fill, 2.0f);
            }
        }
    }
}

void EchoDriftEditor::timerCallback()
{
    for (int i = 0; i < 4; ++i) shownLevels[i] = std::max (processor.getLevel (i), shownLevels[i] * 0.85f);
    updateVisibility();
    repaint (meterArea.expanded (2));
    // テンポ同期のときは、テンポが変わると並びも変わる
    static double lastBpm = 0;
    if (std::abs (processor.getBpm() - lastBpm) > 0.01) { lastBpm = processor.getBpm(); view.repaint(); }
}

//==============================================================================
void EchoDriftEditor::EchoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto q = ed.processor.readParams();
    const auto& P = echo::profiles()[juce::jlimit (0, echo::numProfiles() - 1, q.style)];
    const double fs = 48000.0;
    echo::Chain color, loop;
    color.set (P.color, P.nColor, P.colorGain, fs);
    loop.set (P.loop, P.nLoop, P.loopGain, fs);
    const double loopDb = loop.magDb (1000.0, fs) + (q.highCut > 0 ? 10 * std::log10 (1.0 / (1.0 + std::pow (1000.0 / echo::highCutHz (q.highCut), 2.0))) : 0.0);
    const double firstDb = color.magDb (1000.0, fs) + loop.magDb (1000.0, fs);
    const double fbDb = q.feedback > 0.0005f ? 20.0 * std::log10 (q.feedback) : -200.0;

    struct Tap { double ms; double db; int ch; };
    std::vector<Tap> taps;
    const double T1 = q.prime ? echo::primeMs (q.time1Ms) : q.time1Ms;
    const double T2 = q.prime ? echo::primeMs (q.time2Ms) : q.time2Ms;
    if (q.mode == 0 || q.mode == 1)
    {
        for (int c = 0; c < 2; ++c)
        {
            const double T = (q.mode == 1 && c == 1) ? T2 : T1;
            for (int k = 1; k <= 64; ++k)
            {
                const bool odd = (k % 2) == 1;
                const double ms = k * T + P.off[c * 2 + (odd ? 0 : 1)] + (odd ? q.groove * T : 0.0) + q.feelMs;
                const double db = firstDb + (k - 1) * (fbDb + loopDb);
                if (db < -60.0) break;
                taps.push_back ({ std::max (0.0, ms), db, c });
            }
        }
    }
    else if (q.mode == 2)
    {
        for (int k = 0; k < 64; ++k)
        {
            const double db = firstDb - 3.0 + k * (fbDb + 2 * loopDb);
            if (db < -60.0) break;
            taps.push_back ({ T1 + k * (T1 + T2) + q.feelMs, db, 0 });
            taps.push_back ({ T1 + T2 + k * (T1 + T2) + q.feelMs, db + loopDb, 1 });
        }
    }
    else
    {
        const int n = std::max (1, q.repeats);
        double s2 = 0; std::vector<double> gdb ((size_t) n);
        for (int k = 0; k < n; ++k) { gdb[(size_t) k] = -(n > 1 ? q.decayDb * k / (n - 1) : 0.0); s2 += std::pow (10.0, gdb[(size_t) k] / 10.0); }
        const double norm = 10.0 * std::log10 (std::min (1.0, 2.0 / s2));
        for (int round = 0; round < 32; ++round)
        {
            const double base = round * q.loopBeatsMs, rdb = round * (fbDb + loopDb);
            if (firstDb + rdb + norm < -60.0) break;
            for (int k = (round == 0 ? 1 : 0); k < n; ++k)
                for (int c = 0; c < 2; ++c) taps.push_back ({ base + k * q.rhythmMs + q.feelMs, firstDb + rdb + norm + gdb[(size_t) k], c });
        }
    }

    // 横軸: いちばん長いもの（最大 12 s）
    double span = 200.0;
    for (auto& t : taps) span = std::max (span, t.ms * 1.08);
    span = std::min (span, 12000.0);

    auto plot = r.withTrimmedBottom (18);
    const float mid = plot.getCentreY();
    g.setColour (edge);
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    // 目盛り
    g.setFont (juce::FontOptions (10.0f));
    const double steps[] = { 50, 100, 250, 500, 1000, 2000, 5000 };
    double step = 5000; for (double s : steps) if (span / s <= 8) { step = s; break; }
    for (double t = step; t < span; t += step)
    {
        const float x = plot.getX() + (float) (t / span) * plot.getWidth();
        g.setColour (edge.withAlpha (0.6f)); g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (dim); g.drawText (msText (t), juce::Rectangle<float> (x - 30, plot.getBottom() + 2, 60, 14), juce::Justification::centred);
    }
    g.setColour (dim);
    g.drawText ("L", plot.withHeight (14).translated (2, 0), juce::Justification::topLeft);
    g.drawText ("R", plot.withTrimmedTop (plot.getHeight() - 14).translated (2, 0), juce::Justification::bottomLeft);
    // 元の音
    g.setColour (text.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY() + 6, 2.0f, plot.getHeight() - 12));
    for (auto& t : taps)
    {
        if (t.ms > span) continue;
        const float x = plot.getX() + (float) (t.ms / span) * plot.getWidth();
        const float h = (float) juce::jlimit (0.0, 1.0, (t.db + 60.0) / 60.0) * (plot.getHeight() / 2 - 8);
        const auto c = t.ch == 0 ? amber : teal;
        g.setColour (c.withAlpha (0.9f));
        if (t.ch == 0) g.fillRoundedRectangle (x - 1.5f, mid - h, 3.0f, h, 1.5f);
        else g.fillRoundedRectangle (x - 1.5f, mid, 3.0f, h, 1.5f);
    }

    // 右上に、1 回目のエコーの音色（青緑の線は 1 回くり返すたびの変化）
    auto inset = juce::Rectangle<float> (r.getRight() - 210, r.getY() + 4, 206, 74);
    g.setColour (bg.withAlpha (0.85f)); g.fillRoundedRectangle (inset, 5.0f);
    g.setColour (edge); g.drawRoundedRectangle (inset, 5.0f, 1.0f);
    auto ir = inset.reduced (6, 6);
    auto pathFor = [&] (bool first)
    {
        juce::Path p;
        for (int i = 0; i <= 80; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 80.0);
            double db = first ? color.magDb (f, fs) + loop.magDb (f, fs) : loop.magDb (f, fs) - loop.magDb (1000.0, fs);
            if (first) db -= firstDb;
            db = juce::jlimit (-30.0, 12.0, db);
            const float x = ir.getX() + ir.getWidth() * (float) i / 80.0f;
            const float y = ir.getY() + ir.getHeight() * (float) ((12.0 - db) / 42.0);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (edge); g.drawHorizontalLine ((int) (ir.getY() + ir.getHeight() * 12.0f / 42.0f), ir.getX(), ir.getRight());
    g.setColour (teal.withAlpha (0.8f)); g.strokePath (pathFor (false), juce::PathStrokeType (1.2f));
    g.setColour (amber); g.strokePath (pathFor (true), juce::PathStrokeType (1.6f));
    g.setColour (dim); g.setFont (juce::FontOptions (9.0f));
    g.drawText ("20 Hz", ir.withHeight (10).withY (ir.getBottom() - 10), juce::Justification::left);
    g.drawText ("20 kHz", ir.withHeight (10).withY (ir.getBottom() - 10), juce::Justification::right);
}
