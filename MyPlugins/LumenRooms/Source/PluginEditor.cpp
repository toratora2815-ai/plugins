#include "PluginEditor.h"

namespace
{
const juce::Colour ink (0xff0c0f14), base (0xff12161d), card (0xff171c24), cardEdge (0xff252c38), well (0xff0d1117);
const juce::Colour txt (0xffdfe3ea), dim (0xff8a92a2), faint (0xff4b5262);
const juce::Colour teal (0xff4fd1c5), amber (0xfff2a65a), pearl (0xffd9dde6);
constexpr int W = 1180, H = 740;
}
static void drawHint (juce::Graphics&, juce::Rectangle<float>, const juce::String&);

//==============================================================================
LumenLook::LumenLook()
{
    setColour (juce::Slider::textBoxTextColourId, txt);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, txt);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::textColourId, txt);
    setColour (juce::ComboBox::outlineColourId, cardEdge);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, base);
    setColour (juce::PopupMenu::textColourId, txt);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, txt);
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, ink);
    setColour (juce::AlertWindow::backgroundColourId, base);
    setColour (juce::AlertWindow::textColourId, txt);
}

juce::Label* LumenLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.0f));
    l->setColour (juce::Label::textColourId, txt);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::backgroundColourId, well);
    l->setColour (juce::TextEditor::textColourId, txt);
    l->setColour (juce::TextEditor::highlightColourId, amber.withAlpha (0.3f));
    return l;
}

static juce::Colour accentOf (const juce::Component& c, juce::Colour def)
{
    const auto v = c.getProperties()["accent"];
    return v.isVoid() ? def : juce::Colour ((juce::uint32) (juce::int64) v);
}

void LumenLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
    const auto c = area.getCentre();
    const float ang = start + pos * (end - start);
    const auto accent = accentOf (s, amber);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

    // 目盛り
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const float r0 = r + 1.5f, r1 = r + (i % 5 == 0 ? 4.0f : 2.5f);
        g.setColour (faint);
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 1.0f);
    }
    // 溝と値の弧
    const float rt = r - 3.0f;
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, rt, rt, 0, start, end, true);
    g.setColour (well);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (ang - from) > 0.01f)
    {
        val.addCentredArc (c.x, c.y, rt, rt, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (accent.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // つまみの本体
    const float rb = rt - 6.0f;
    juce::ColourGradient body (juce::Colour (0xff2d3441), c.x - rb * 0.4f, c.y - rb * 0.8f, juce::Colour (0xff12161c), c.x + rb * 0.3f, c.y + rb, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - rb, c.y - rb, 2 * rb, 2 * rb);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c.x - rb + 0.5f, c.y - rb + 0.5f, 2 * rb - 1, 2 * rb - 1, 1.0f);
    // 指し示す線
    const float p0 = rb * 0.2f, p1 = rb * 0.84f;
    g.setColour (txt);
    g.drawLine (c.x + p0 * std::sin (ang), c.y - p0 * std::cos (ang), c.x + p1 * std::sin (ang), c.y - p1 * std::cos (ang), 2.2f);
}

void LumenLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto accent = accentOf (b, amber);
    g.setColour (on ? accent : (over ? card.brighter (0.15f) : well));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? accent : cardEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LumenLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? ink : (over ? txt : dim));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LumenLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (well);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path p;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    p.startNewSubPath (cx - 4, cy - 2);
    p.lineTo (cx, cy + 2);
    p.lineTo (cx + 4, cy - 2);
    g.setColour (dim);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

void LumenLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 28, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
LumenRoomsEditor::Knob& LumenRoomsEditor::knob (const char* id, const char* name, Size size, int pg, juce::Colour accent)
{
    auto k = std::make_unique<Knob>();
    k->size = size;
    k->page = pg;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 86, 16);
    k->slider.setLookAndFeel (&look);
    k->slider.getProperties().set ("accent", (juce::int64) accent.getARGB());
    k->slider.setMouseDragSensitivity (size == Size::big ? 260 : 220);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (size == Size::big ? 12.0f : 11.0f, juce::Font::bold));
    k->label.setColour (juce::Label::textColourId, dim);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    if (auto* prm = proc.apvts.getParameter (id))   // ダブルクリックで初期値に戻す
        k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    auto& ref = *k;
    knobs[id] = std::move (k);
    return ref;
}

LumenRoomsEditor::Choice& LumenRoomsEditor::choice (const char* id, const char* name, int pg)
{
    auto c = std::make_unique<Choice>();
    c->page = pg;
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->box.addItemList (prm->choices, 1);
    c->box.setLookAndFeel (&look);
    c->label.setText (name, juce::dontSendNotification);
    c->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    c->label.setColour (juce::Label::textColourId, dim);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    auto& ref = *c;
    choices[id] = std::move (c);
    return ref;
}

LumenRoomsEditor::Toggle& LumenRoomsEditor::toggle (const char* id, const char* text, int pg)
{
    auto t = std::make_unique<Toggle>();
    t->page = pg;
    t->button.setButtonText (text);
    t->button.setClickingTogglesState (true);
    t->button.setLookAndFeel (&look);
    t->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t->button);
    addAndMakeVisible (t->button);
    auto& ref = *t;
    toggles[id] = std::move (t);
    return ref;
}

LumenRoomsEditor::LumenRoomsEditor (LumenRoomsProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    using S = Size;

    // ── MAIN ──
    choice ("pattern", "PATTERN", 0); choice ("proximity", "PROXIMITY", 0); choice ("erSlope", "SLOPE", 0);
    knob ("erSize", "SIZE", S::big, 0, teal); knob ("reflectivity", "REFLECTIVITY", S::mid, 0, teal); knob ("diffusion", "DIFFUSION", S::mid, 0, teal);
    knob ("erWidth", "WIDTH", S::mid, 0, teal); knob ("erRoll", "ROLL-OFF", S::mid, 0, teal); knob ("erTrim", "TRIM", S::mid, 0, teal);

    choice ("revSlope", "SLOPE", 0);
    toggle ("infinite", "FREEZE", 0);
    knob ("time", "TIME", S::big, 0, amber); knob ("bloom", "BLOOM", S::big, 0, amber);
    knob ("predelay", "PRE-DELAY", S::mid, 0, amber); knob ("revWidth", "WIDTH", S::mid, 0, amber);
    knob ("bassMult", "BASS  x", S::mid, 0, amber); knob ("bassFreq", "BASS  Hz", S::mid, 0, amber);
    knob ("trebMult", "TREBLE  x", S::mid, 0, amber); knob ("trebFreq", "TREBLE  Hz", S::mid, 0, amber);
    knob ("revRoll", "ROLL-OFF", S::mid, 0, amber); knob ("lowCut", "LOW CUT", S::mid, 0, amber); knob ("revTrim", "TRIM", S::mid, 0, amber);

    knob ("mix", "DRY / WET", S::big, -1, pearl); knob ("balance", "REFL / REVERB", S::big, -1, pearl); knob ("master", "OUTPUT", S::mid, -1, pearl);
    knobs["balance"]->slider.getProperties().set ("bipolar", true);
    knobs["master"]->slider.getProperties().set ("bipolar", true);

    // ── DETAIL ──
    knob ("modDepth", "REVERB DEPTH", S::mid, 1, amber); knob ("modRate", "REVERB RATE", S::mid, 1, amber);
    knob ("undDepth", "UNDULATION", S::mid, 1, amber); knob ("undShape", "SHAPE", S::mid, 1, amber);
    knob ("erModDepth", "REFL DEPTH", S::mid, 1, teal); knob ("erModRate", "REFL RATE", S::mid, 1, teal);
    knob ("echoLevel", "ECHO", S::mid, 1, amber); knob ("echoTime", "ECHO TIME", S::mid, 1, amber); knob ("echoRoll", "ECHO ROLL-OFF", S::mid, 1, amber);
    choice ("echoNote", "ECHO NOTE", 1); toggle ("echoSync", "SYNC", 1);
    choice ("preNote", "PRE-DELAY NOTE", 1); toggle ("preSync", "SYNC", 1);
    knob ("lowBoost", "LOW BOOST", S::mid, 1, amber); knob ("boostWeight", "WEIGHT", S::mid, 1, amber);
    choice ("revColor", "REVERB COLOR", 1); choice ("erColor", "REFL COLOR", 1);
    knob ("revXfLevel", "REV XFEED", S::mid, 1, amber); knob ("revXfDelay", "DELAY", S::mid, 1, amber); knob ("revXfRoll", "ROLL-OFF", S::mid, 1, amber);
    knob ("erXfLevel", "REFL XFEED", S::mid, 1, teal); knob ("erXfDelay", "DELAY", S::mid, 1, teal); knob ("erXfRoll", "ROLL-OFF", S::mid, 1, teal);

    // ── EQ ──
    toggle ("eqOn", "EQ ON", 2); choice ("eqSlope", "ROLL-OFF SLOPE", 2);
    const std::pair<const char*, const char*> eqNames[] = { { "eqLowCut", "LOW CUT" }, { "eqLsFreq", "LOW Hz" }, { "eqLsGain", "LOW dB" }, { "eqLsQ", "LOW Q" },
                                                            { "eqMidFreq", "MID Hz" }, { "eqMidGain", "MID dB" }, { "eqMidQ", "MID Q" },
                                                            { "eqHsFreq", "HIGH Hz" }, { "eqHsGain", "HIGH dB" }, { "eqHsQ", "HIGH Q" }, { "eqRoll", "ROLL-OFF" } };
    for (auto& n : eqNames) knob (n.first, n.second, S::mid, 2, pearl);
    for (auto id : { "eqLsGain", "eqMidGain", "eqHsGain" }) knobs[id]->slider.getProperties().set ("bipolar", true);

    toggles["eqOn"]->button.getProperties().set ("accent", (juce::int64) pearl.getARGB());

    cards = { { "EARLY REFLECTIONS", teal, {}, 0 }, { "REVERB", amber, {}, 0 }, { "OUTPUT", pearl, {}, -1 },
              { "MODULATION", amber, {}, 1 }, { "ECHO  /  LOW BOOST", amber, {}, 1 }, { "COLOR  /  CROSSFEED", amber, {}, 1 },
              { "POST EQ", pearl, {}, 2 } };

    const char* tabNames[3] = { "MAIN", "DETAIL", "EQ" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
    }

    saveButton.onClick = [this] { showSaveDialog(); };
    addAndMakeVisible (saveButton);
    for (auto* b : { &shapeButton, &liveButton })
    {
        b->setClickingTogglesState (false);
        b->getProperties().set ("accent", (juce::int64) amber.getARGB());
        addAndMakeVisible (b);
    }
    shapeButton.onClick = [this] { setMapMode (0); };
    liveButton.onClick = [this] { setMapMode (1); };
    shapeButton.setTooltip ("Reverb shape from the current settings");
    liveButton.setTooltip ("What is playing now: teal = dry input, warm = reverb");
    liveImage.clear (liveImage.getBounds(), juce::Colour (0xff0b0e14));
    setMapMode (juce::SystemStats::getEnvironmentVariable ("LUMEN_MAP", juce::String (proc.mapMode)).getIntValue() == 1 ? 1 : 0);
    prevButton.onClick = [this] { if (! browser) { browser = std::make_unique<PresetBrowser> (proc, look); } browser->step (-1); };
    nextButton.onClick = [this] { if (! browser) { browser = std::make_unique<PresetBrowser> (proc, look); } browser->step (+1); };
    nameButton.onClick = [this] { toggleBrowser(); };
    nameButton.setTooltip ("Browse presets");
    for (auto* b : { &prevButton, &nextButton, &nameButton }) addAndMakeVisible (b);
    for (auto* b : { &srcAll, &srcRefl, &srcRev })
    {
        b->getProperties().set ("accent", (juce::int64) amber.getARGB());
        addAndMakeVisible (b);
    }
    srcAll.onClick = [this] { proc.liveSource.store (0); };
    srcRefl.onClick = [this] { proc.liveSource.store (1); };
    srcRev.onClick = [this] { proc.liveSource.store (2); };
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the reverb by this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (proc.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! proc.link.on()) return "Plugin Link OFF - same LINK as Fader Pilot to duck the reverb while singing";
        if (! proc.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = proc.duck.nowDb() < -0.1f;
        return lit ? "voice singing: reverb " + juce::String (proc.duck.nowDb(), 1) + " dB" : juce::String ("voice resting: full reverb");
    };

    setSize (W, H + footerH);
    showPage (juce::jlimit (0, 2, juce::SystemStats::getEnvironmentVariable ("LUMEN_PAGE", "0").getIntValue()));
    worker.request (currentVisualParams());
    startTimerHz (30);
    setMapMode (proc.mapMode);   // ALL / REFL / REVERB は LIVE のときだけ出す
    if (juce::SystemStats::getEnvironmentVariable ("LUMEN_BROWSER", "0") == "1") toggleBrowser();   // 確認用
}

LumenRoomsEditor::~LumenRoomsEditor()
{
    stopTimer();
    proc.liveWanted.store (false);
    for (auto& k : knobs) k.second->slider.setLookAndFeel (nullptr);
    for (auto& c : choices) c.second->box.setLookAndFeel (nullptr);
    for (auto& t : toggles) t.second->button.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void LumenRoomsEditor::showPage (int pg)
{
    page = pg;
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == pg, juce::dontSendNotification);
    for (auto& k : knobs) { const bool v = k.second->page == -1 || k.second->page == pg; k.second->slider.setVisible (v); k.second->label.setVisible (v); }
    for (auto& c : choices) { const bool v = c.second->page == pg; c.second->box.setVisible (v); c.second->label.setVisible (v); }
    for (auto& t : toggles) t.second->button.setVisible (t.second->page == pg);
    resized();
    repaint();
}

void LumenRoomsEditor::placeKnob (const char* id, juce::Point<int> c)
{
    auto it = knobs.find (id);
    if (it == knobs.end()) return;
    auto& k = *it->second;
    const int d = k.size == Size::big ? 76 : 56;
    const int w = 96;
    k.label.setBounds (c.x - w / 2, c.y - d / 2 - 17, w, 15);
    k.slider.setBounds (c.x - w / 2, c.y - d / 2, w, d + 18);
}

void LumenRoomsEditor::placeChoice (const char* id, juce::Rectangle<int> r)
{
    auto it = choices.find (id);
    if (it == choices.end()) return;
    it->second->label.setBounds (r.removeFromTop (15));
    it->second->box.setBounds (r.removeFromTop (24));
}

void LumenRoomsEditor::placeRow (juce::Rectangle<int> area, std::initializer_list<const char*> ids)
{
    const int n = (int) ids.size();
    if (n == 0) return;
    const int cw = area.getWidth() / n;
    int i = 0;
    for (auto id : ids)
    {
        placeKnob (id, { area.getX() + cw * i + cw / 2, area.getCentreY() - 4 });
        ++i;
    }
}

juce::Rectangle<int> LumenRoomsEditor::cardContent (const juce::String& title) const
{
    for (auto& c : cards) if (c.title == title) return c.bounds.reduced (14, 10).withTrimmedTop (26);
    return {};
}

void LumenRoomsEditor::resized()
{
    layoutFooter();
    // 上の帯
    const int hy = 15, hh = 28;
    for (int i = 0; i < 3; ++i) tabs[i].setBounds (222 + i * 78, hy, 72, hh);
    prevButton.setBounds (470, hy, 28, hh);
    nameButton.setBounds (502, hy, 244, hh);
    nextButton.setBounds (750, hy, 28, hh);
    saveButton.setBounds (786, hy, 58, hh);
    meterArea = { 862, 8, W - 18 - 862, 44 };
    if (browser != nullptr) browser->setBounds (16, 62, W - 32, 400);

    // 2 つの表示
    erArea = { 16, 66, 500, 228 };
    mapArea = { 528, 66, W - 16 - 528, 228 };
    shapeButton.setBounds (mapArea.getX() + 104, mapArea.getY() + 10, 66, 22);   // 見出し「REVERB」の右
    liveButton.setBounds (mapArea.getX() + 174, mapArea.getY() + 10, 66, 22);
    srcAll.setBounds (mapArea.getRight() - 14 - 3 * 70 - 8, mapArea.getY() + 10, 66, 22);
    srcRefl.setBounds (mapArea.getRight() - 14 - 2 * 70 - 4, mapArea.getY() + 10, 66, 22);
    srcRev.setBounds (mapArea.getRight() - 14 - 70, mapArea.getY() + 10, 70, 22);

    // カード
    const int top = 306, hgt = H - 14 - top;
    const int outX = W - 16 - 160, mainR = outX - 10;
    for (auto& c : cards)
    {
        if (c.title == "OUTPUT") c.bounds = { outX, top, 160, hgt };
        else if (c.title == "EARLY REFLECTIONS") c.bounds = { 16, top, 410, hgt };
        else if (c.title == "REVERB") c.bounds = { 436, top, mainR - 436, hgt };
        else if (c.title == "MODULATION") c.bounds = { 16, top, 300, hgt };
        else if (c.title == "ECHO  /  LOW BOOST") c.bounds = { 326, top, 340, hgt };
        else if (c.title == "COLOR  /  CROSSFEED") c.bounds = { 676, top, mainR - 676, hgt };
        else if (c.title == "POST EQ") c.bounds = { 16, top, mainR - 16, hgt };
    }

    {
        auto r = cardContent ("OUTPUT");
        placeKnob ("mix", { r.getCentreX(), r.getY() + 62 });
        placeKnob ("balance", { r.getCentreX(), r.getY() + 186 });
        placeKnob ("master", { r.getCentreX(), r.getY() + 304 });
    }

    if (page == 0)
    {
        auto r = cardContent ("EARLY REFLECTIONS");
        auto row = r.removeFromTop (40);
        const int cw = row.getWidth() / 3;
        placeChoice ("pattern", row.removeFromLeft (cw).reduced (4, 0));
        placeChoice ("proximity", row.removeFromLeft (cw).reduced (4, 0));
        placeChoice ("erSlope", row.reduced (4, 0));
        r.removeFromTop (28);
        auto big = r.removeFromTop (150);
        placeKnob ("erSize", { big.getX() + big.getWidth() / 4, big.getCentreY() - 4 });
        placeRow (big.withTrimmedLeft (big.getWidth() / 2), { "reflectivity", "diffusion" });
        r.removeFromTop (28);
        placeRow (r.removeFromTop (130), { "erWidth", "erRoll", "erTrim" });

        auto v = cardContent ("REVERB");
        placeRow (v.removeFromTop (150), { "time", "bloom", "predelay", "revWidth" });
        placeRow (v.removeFromTop (112), { "bassMult", "bassFreq", "trebMult", "trebFreq" });
        auto last = v.removeFromTop (118);
        placeRow (last.removeFromLeft (last.getWidth() * 3 / 4), { "revRoll", "lowCut", "revTrim" });
        auto side = last.reduced (6, 14);
        placeChoice ("revSlope", side.removeFromTop (40));
        side.removeFromTop (10);
        toggles["infinite"]->button.setBounds (side.removeFromTop (26));
    }
    else if (page == 1)
    {
        auto m = cardContent ("MODULATION");
        placeRow (m.removeFromTop (124), { "modDepth", "modRate" });
        placeRow (m.removeFromTop (124), { "undDepth", "undShape" });
        placeRow (m.removeFromTop (124), { "erModDepth", "erModRate" });

        auto e = cardContent ("ECHO  /  LOW BOOST");
        placeRow (e.removeFromTop (118), { "echoLevel", "echoTime", "echoRoll" });
        auto r1 = e.removeFromTop (42);
        placeChoice ("echoNote", r1.removeFromLeft (r1.getWidth() - 84).reduced (4, 0));
        toggles["echoSync"]->button.setBounds (r1.reduced (4, 0).withTrimmedTop (15).withHeight (24));
        e.removeFromTop (8);
        auto r2 = e.removeFromTop (42);
        placeChoice ("preNote", r2.removeFromLeft (r2.getWidth() - 84).reduced (4, 0));
        toggles["preSync"]->button.setBounds (r2.reduced (4, 0).withTrimmedTop (15).withHeight (24));
        e.removeFromTop (10);
        placeRow (e.removeFromTop (118), { "lowBoost", "boostWeight" });

        auto c = cardContent ("COLOR  /  CROSSFEED");
        auto cr = c.removeFromTop (40);
        placeChoice ("revColor", cr.removeFromLeft (cr.getWidth() / 2).reduced (4, 0));
        placeChoice ("erColor", cr.reduced (4, 0));
        c.removeFromTop (14);
        placeRow (c.removeFromTop (130), { "revXfLevel", "revXfDelay", "revXfRoll" });
        placeRow (c.removeFromTop (130), { "erXfLevel", "erXfDelay", "erXfRoll" });
    }
    else
    {
        auto q = cardContent ("POST EQ");
        toggles["eqOn"]->button.setBounds (q.getRight() - 80, q.getY() - 28, 80, 22);
        eqArea = q.removeFromTop (130);
        q.removeFromTop (6);
        auto row = q.removeFromTop (40);
        placeChoice ("eqSlope", row.removeFromRight (170).reduced (4, 0));
        placeRowCols (q.removeFromTop (108), { "eqLowCut", "eqLsFreq", "eqLsGain", "eqLsQ", "eqMidFreq", "eqMidGain" }, 6);
        placeRowCols (q.removeFromTop (108), { "eqMidQ", "eqHsFreq", "eqHsGain", "eqHsQ", "eqRoll" }, 6);
    }
}

//==============================================================================
void LumenRoomsEditor::paint (juce::Graphics& g)
{
    g.fillAll (base);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a202a), 0, 0, base, 0, 300, false));
    g.fillRect (0, 0, W, 300);

    // ロゴ
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.setColour (txt);
    g.drawText ("LUMEN", 18, 14, 90, 30, juce::Justification::centredLeft);
    g.setColour (amber);
    g.drawText ("ROOMS", 103, 14, 100, 30, juce::Justification::centredLeft);

    drawMeters (g, meterArea.toFloat());

    for (auto a : { erArea, mapArea })
    {
        g.setColour (card);
        g.fillRoundedRectangle (a.toFloat(), 10.0f);
        g.setColour (cardEdge);
        g.drawRoundedRectangle (a.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
    drawReflections (g, erArea.toFloat());
    drawReverbMap (g, mapArea.toFloat());

    for (auto& c : cards)
        if (c.page == -1 || c.page == page) drawCard (g, c);
    if (page == 2) drawEqCurve (g, eqArea.toFloat());
}

void LumenRoomsEditor::drawCard (juce::Graphics& g, const Card& c)
{
    auto b = c.bounds.toFloat();
    g.setColour (card);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (c.accent);
    g.fillRoundedRectangle (b.getX() + 14, b.getY() + 15, 3, 12, 1.5f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (txt);
    g.drawText (c.title, (int) b.getX() + 23, (int) b.getY() + 10, 300, 22, juce::Justification::centredLeft);
}

static void drawCaption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& left, juce::Colour accent, const juce::String& right)
{
    auto row = r.reduced (14, 10).removeFromTop (22).toNearestInt();
    g.setColour (accent);
    g.fillRoundedRectangle ((float) row.getX(), (float) row.getY() + 5, 3, 12, 1.5f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (txt);
    g.drawText (left, row.withTrimmedLeft (9), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (dim);
    g.drawText (right, row, juce::Justification::centredRight);
}

void LumenRoomsEditor::drawReflections (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto rp = proc.readRefl();
    drawCaption (g, r, "EARLY REFLECTIONS", teal,
                 juce::String (lumen::patternName (rp.pattern)) + "   size " + juce::String (rp.size * 10.0f, 1) + "   diffusion " + juce::String (rp.diffusion, 1));
    auto plot = r.reduced (14, 10).withTrimmedTop (28).withTrimmedBottom (14);
    g.setColour (well);
    g.fillRoundedRectangle (plot, 6.0f);
    plot = plot.reduced (8, 8);
    if (! haveVis || vis.erL.empty()) return;

    const float ms = vis.erMs;
    g.setFont (juce::FontOptions (10.5f));
    const float tick = ms > 400 ? 100.0f : 50.0f;
    for (float t = 0; t <= ms; t += tick)
    {
        const float x = plot.getX() + plot.getWidth() * t / ms;
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (faint);
        g.drawText (juce::String ((int) t) + " ms", (int) x - 22, (int) plot.getBottom() + 7, 44, 12, juce::Justification::centred);
    }
    const float mid = plot.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    float peak = -200.0f;
    for (auto v : vis.erL) peak = juce::jmax (peak, v);
    for (auto v : vis.erR) peak = juce::jmax (peak, v);
    const int bins = (int) vis.erL.size();
    const float bw = plot.getWidth() / (float) bins;
    const float half = plot.getHeight() * 0.5f - 3.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& amp = ch == 0 ? vis.erL : vis.erR;
        const auto& br = ch == 0 ? vis.erBrightL : vis.erBrightR;
        for (int b = 0; b < bins; ++b)
        {
            const float lv = (amp[(size_t) b] - peak + 48.0f) / 48.0f;
            if (lv <= 0.0f) continue;
            const float h = half * std::pow (lv, 1.3f);
            const float bright = juce::jlimit (0.0f, 1.0f, std::pow (br[(size_t) b] * 1.6f, 0.8f));
            const auto col = juce::Colour (0xff1d5f63).interpolatedWith (teal, juce::jmin (1.0f, bright * 1.4f))
                                 .interpolatedWith (juce::Colour (0xfff2fffd), juce::jmax (0.0f, bright - 0.55f) * 1.6f);
            g.setColour (col.withAlpha (0.35f + 0.65f * lv));
            const float x = plot.getX() + bw * (float) b;
            if (ch == 0) g.fillRect (x, mid - h, juce::jmax (1.0f, bw), h);
            else g.fillRect (x, mid, juce::jmax (1.0f, bw), h);
        }
    }
    for (size_t k = 0; k < vis.onsetMs.size(); ++k)
    {
        const float x = plot.getX() + plot.getWidth() * vis.onsetMs[k] / ms;
        if (x > plot.getRight()) continue;
        const bool cross = vis.onsetCross[k];
        g.setColour ((cross ? amber : teal).withAlpha (0.9f));
        const float y = cross ? plot.getBottom() - 2.0f : plot.getY() + 2.0f;
        g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (dim);
    g.drawText ("L", (int) plot.getRight() - 14, (int) plot.getY() + 4, 12, 12, juce::Justification::right);
    g.drawText ("R", (int) plot.getRight() - 14, (int) plot.getBottom() - 16, 12, 12, juce::Justification::right);
    if (hoverTarget == 1 || dragTarget == 1)
        drawHint (g, plot, "drag  left/right = SIZE   up/down = DIFFUSION   double-click = reset");
}

juce::Rectangle<float> LumenRoomsEditor::erPlot() const
{
    return erArea.toFloat().reduced (14, 10).withTrimmedTop (28).withTrimmedBottom (14);
}

juce::Rectangle<float> LumenRoomsEditor::mapPlot() const
{
    return mapArea.toFloat().reduced (14, 10).withTrimmedTop (28).withTrimmedBottom (14).withTrimmedLeft (28);
}

static void drawHint (juce::Graphics& g, juce::Rectangle<float> plot, const juce::String& text)
{
    g.setFont (juce::FontOptions (11.0f));
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.0f)), text) + 16.0f;
    auto b = juce::Rectangle<float> (plot.getX() + 6, plot.getBottom() - 24, w, 18);
    g.setColour (juce::Colour (0xcc0c0f14));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (0xffdfe3ea));
    g.drawText (text, b, juce::Justification::centred);
}

void LumenRoomsEditor::drawReverbMap (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (proc.mapMode == 1) { drawLiveMap (g, r); return; }
    const auto tp = proc.readTail();
    const bool inf = tp.timeSec >= 60.0f;
    drawCaption (g, r, "REVERB", amber,
                 (inf ? juce::String ("freeze") : juce::String (tp.timeSec, 2) + " s") + "   bloom " + juce::String (tp.bloom, 1)
                     + "   pre " + juce::String (tp.predelayMs, 0) + " ms");
    auto plot = r.reduced (14, 10).withTrimmedTop (28).withTrimmedBottom (14).withTrimmedLeft (28);
    g.setColour (well);
    g.fillRoundedRectangle (plot, 6.0f);
    if (! haveVis || ! vis.map.isValid()) return;

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (plot, 6.0f);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (vis.map, plot);
    }
    g.setFont (juce::FontOptions (10.5f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = plot.getBottom() - plot.getHeight() * std::log (f / 25.0f) / std::log (800.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (faint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) plot.getX() - 30, (int) y - 6, 26, 12,
                    juce::Justification::centredRight);
    }
    const float secs = vis.mapSeconds;
    const float step = secs > 6 ? 2.0f : (secs > 2.5f ? 1.0f : (secs > 1.0f ? 0.5f : 0.1f));
    for (float t = step; t < secs - step * 0.3f; t += step)
    {
        const float x = plot.getX() + plot.getWidth() * t / secs;
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (faint);
        g.drawText (juce::String (t, step < 1.0f ? 1 : 0) + " s", (int) x - 20, (int) plot.getBottom() + 7, 40, 12, juce::Justification::centred);
    }
    if (! inf && ! vis.rtCurve.empty())
    {
        juce::Path p;
        bool started = false;
        const float pre = tp.predelayMs * 0.001f;
        for (int row = 0; row < lumen::VisualWorker::kRows; row += 2)
        {
            const float rt = vis.rtCurve[(size_t) row];
            if (rt <= 0.0f) continue;
            const float x = plot.getX() + plot.getWidth() * juce::jmin (1.0f, (pre + rt) / secs);
            const float y = plot.getBottom() - plot.getHeight() * (float) row / (float) (lumen::VisualWorker::kRows - 1);
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (p, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }
    if (hoverTarget == 2 || dragTarget == 2)
        drawHint (g, plot, "drag  left/right = TIME   up/down = BLOOM   double-click = reset");
}

void LumenRoomsEditor::drawEqCurve (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (well);
    g.fillRoundedRectangle (r, 6.0f);
    auto plot = r.reduced (34, 10).withTrimmedBottom (10);
    const auto m = proc.readMix();
    const float sr = 48000.0f;
    lumen::Biquad b[4];
    b[0].hp (m.eqLowCut, 0.7071, sr);
    b[1].lowShelf (m.eqLsF, m.eqLsQ, m.eqLsG, sr);
    b[2].peak (m.eqMidF, m.eqMidQ, m.eqMidG, sr);
    b[3].highShelf (m.eqHsF, m.eqHsQ, m.eqHsG, sr);
    lumen::Rolloff ro;
    ro.set (m.eqRoll, m.eqRollSlope, sr);

    g.setFont (juce::FontOptions (10.5f));
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = plot.getCentreY() - plot.getHeight() * 0.5f * db / 15.0f;
        g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.16f : 0.06f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (faint);
        g.drawText (juce::String ((int) db), (int) plot.getX() - 30, (int) y - 6, 24, 12, juce::Justification::centredRight);
    }
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = plot.getX() + plot.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (faint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 15, (int) plot.getBottom() + 2, 30, 12,
                    juce::Justification::centred);
    }
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
        float db = ro.magDb (f, sr);
        for (auto& q : b) db += q.magDb (f, sr);
        const float x = plot.getX() + plot.getWidth() * (float) i / 200.0f;
        const float y = plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-15.0f, 15.0f, db) / 15.0f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (m.eqOn ? pearl : faint);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
}

void LumenRoomsEditor::drawMeters (juce::Graphics& g, juce::Rectangle<float> r)
{
    // IN と OUT：L/R の 2 本ずつ、-48〜0 dB の目盛りと、大きいほうの数字
    const float gw = (r.getWidth() - 14.0f) / 2.0f;
    for (int grp = 0; grp < 2; ++grp)
    {
        const float* lv = grp == 0 ? meterIn : meter;
        auto a = juce::Rectangle<float> (r.getX() + (float) grp * (gw + 14.0f), r.getY(), gw, r.getHeight());
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (dim);
        g.drawText (grp == 0 ? "IN" : "OUT", a.removeFromLeft (28).toNearestInt(), juce::Justification::centredLeft);
        const float pk = juce::Decibels::gainToDecibels (juce::jmax (lv[0], lv[1]), -96.0f);
        auto num = a.removeFromRight (42);
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (pk > -1.0f ? juce::Colour (0xffff6b5a) : txt);
        g.drawText (pk <= -95.0f ? juce::String ("-inf") : juce::String (pk, 1), num.toNearestInt(), juce::Justification::centredRight);
        a.removeFromRight (6);
        auto bars = a.removeFromTop (22.0f).withTrimmedTop (4.0f);
        const float bh = 6.0f;
        auto toX = [&a] (float db) { return a.getX() + a.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };
        for (int c = 0; c < 2; ++c)
        {
            auto b = juce::Rectangle<float> (a.getX(), bars.getY() + (float) c * (bh + 3.0f), a.getWidth(), bh);
            g.setColour (well);
            g.fillRoundedRectangle (b, 2.0f);
            const float db = juce::Decibels::gainToDecibels (lv[c], -96.0f);
            const float w = toX (db) - a.getX();
            if (w >= 1.0f)
            {
                g.setGradientFill (juce::ColourGradient (teal, b.getX(), 0, db > -6.0f ? juce::Colour (0xffff6b5a) : amber, b.getRight(), 0, false));
                g.fillRoundedRectangle (b.withWidth (w), 2.0f);
            }
        }
        g.setFont (juce::FontOptions (9.5f));
        const float ty = bars.getY() + 2 * (bh + 3.0f);
        for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })
        {
            g.setColour (faint);
            g.drawVerticalLine ((int) toX (db), ty, ty + 3.0f);
        }
        for (float db : { -48.0f, -24.0f, 0.0f })   // 数字は 3 つだけ（重ならないように）
        {
            const float x = toX (db);
            g.drawText (juce::String ((int) db), (int) x - 12, (int) ty + 3, 24, 11,
                        db == 0.0f ? juce::Justification::centredRight : (db == -48.0f ? juce::Justification::centredLeft : juce::Justification::centred));
        }
    }
}

//==============================================================================
lumen::VisualParams LumenRoomsEditor::currentVisualParams() const
{
    lumen::VisualParams v;
    v.refl = proc.readRefl();
    v.tail = proc.readTail();
    v.mix = proc.readMix();
    const auto& pt = proc.getProfiles();
    v.revColor = pt.revGains ((int) proc.apvts.getRawParameterValue ("revColor")->load());
    v.erColor = pt.erGains ((int) proc.apvts.getRawParameterValue ("erColor")->load());
    for (auto* prm : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (id == "mix" || id == "master" || id == "balance" || id.startsWith ("eq")) continue;
            v.key.push_back (rp->getValue());
        }
    return v;
}

void LumenRoomsEditor::timerCallback()
{
    for (int c = 0; c < 2; ++c) { meter[c] = proc.getOutputLevel (c); meterIn[c] = proc.getInputLevel (c); }
    repaint (meterArea.expanded (2));

    const auto nm = currentPresetName();
    if (nameButton.getButtonText() != nm) nameButton.setButtonText (nm);
    const int src = proc.liveSource.load();
    srcAll.setToggleState (src == 0, juce::dontSendNotification);
    srcRefl.setToggleState (src == 1, juce::dontSendNotification);
    srcRev.setToggleState (src == 2, juce::dontSendNotification);

    if (proc.mapMode == 1) pullLive();
    worker.request (currentVisualParams());
    if (worker.fetch (vis))
    {
        haveVis = true;
        repaint (erArea);
        repaint (mapArea);
    }
    if (page == 2) repaint (eqArea);
}

void LumenRoomsEditor::addFolderToMenu (juce::PopupMenu& menu, const juce::File& dir, std::vector<juce::File>& files, int depth)
{
    auto subs = dir.findChildFiles (juce::File::findDirectories, false);
    subs.sort();
    for (auto& d : subs)
    {
        juce::PopupMenu sub;
        if (depth < 3) addFolderToMenu (sub, d, files, depth + 1);
        if (sub.getNumItems() > 0) menu.addSubMenu (d.getFileName(), sub);
    }
    auto ps = dir.findChildFiles (juce::File::findFiles, false, "*.crp_preset");
    ps.sort();
    for (auto& f : ps)
    {
        files.push_back (f);
        menu.addItem ((int) files.size(), f.getFileNameWithoutExtension());
    }
}

void LumenRoomsEditor::showLoadMenu()
{
    auto files = std::make_shared<std::vector<juce::File>>();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&look);
    auto mine = std::make_shared<juce::Array<juce::File>> (LumenRoomsProcessor::myPresetFolder().findChildFiles (juce::File::findFiles, false, "*.lrpreset"));
    mine->sort();
    {
        juce::PopupMenu mp;
        for (int i = 0; i < mine->size(); ++i) mp.addItem (200000 + i, (*mine)[i].getFileNameWithoutExtension());
        if (mine->isEmpty()) mp.addItem (-1, "(none yet - use SAVE)", false);
        menu.addSubMenu ("My Presets", mp);
        menu.addSeparator();
    }
    const auto folder = LumenRoomsProcessor::defaultPresetFolder();
    if (folder.getFileName() == "Factory Presets" || folder.getFileName() == "Presets") addFolderToMenu (menu, folder, *files, 0);
    const juce::File user = LumenRoomsProcessor::userPresetFolder();
    if (user.isDirectory()) { juce::PopupMenu um; addFolderToMenu (um, user, *files, 1); if (um.getNumItems() > 0) menu.addSubMenu ("User Presets", um); }
    menu.addSeparator();
    menu.addItem (100000, "Open file...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [this, files, folder, mine] (int r)
    {
        if (r >= 200000 && r < 200000 + mine->size())
        {
            const auto f = (*mine)[r - 200000];
            const auto err = proc.loadMyPreset (f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "LOAD", err);
            return;
        }
        auto load = [this] (const juce::File& f)
        {
            const auto err = proc.importPreset (f);
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "LOAD", f.getFileName() + "\n" + err);
        };
        if (r == 100000)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load preset", folder, "*.crp_preset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [load] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
        }
        else if (r > 0 && r <= (int) files->size())
            load ((*files)[(size_t) r - 1]);
    });
}

//==============================================================================
void LumenRoomsEditor::showSaveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name for this preset (saved to My Presets)", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&look);
    saveWindow->addTextEditor ("name", proc.getImportedName().isNotEmpty() ? proc.getImportedName() : juce::String ("My Room"));
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            const auto name = saveWindow->getTextEditorContents ("name");
            const auto existing = LumenRoomsProcessor::myPresetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".lrpreset");
            const auto err = proc.saveMyPreset (name);
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SAVE", err);
            juce::ignoreUnused (existing);
        }
        if (saveWindow != nullptr) saveWindow->setLookAndFeel (nullptr);
        saveWindow.reset();
    }), false);
}

void LumenRoomsEditor::setMapMode (int m)
{
    proc.mapMode = m;
    shapeButton.setToggleState (m == 0, juce::dontSendNotification);
    liveButton.setToggleState (m == 1, juce::dontSendNotification);
    if (m == 1)
    {
        // 古いデータを捨ててから受け取り始める
        auto& t = proc.liveTap;
        const auto sc = t.fifo.read (t.fifo.getNumReady());
        juce::ignoreUnused (sc);
    }
    proc.liveWanted.store (m == 1);
    for (auto* b : { &srcAll, &srcRefl, &srcRev }) b->setVisible (m == 1);
    livePaused = false;
    repaint (mapArea);
}

void LumenRoomsEditor::pullLive()
{
    auto& t = proc.liveTap;
    const double sr = juce::jmax (8000.0, proc.getSampleRateForView());
    const int hop = juce::jmax (64, (int) (sr * 6.0 / liveCols));   // 画面の幅で約 6 秒
    const int ready = t.fifo.getNumReady();
    if (ready <= 0) return;
    const auto sc = t.fifo.read (ready);
    if (livePaused) return;   // 止めている間は捨てる
    bool changed = false;
    auto consume = [&] (int start, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ringDry[(size_t) ringPos] = t.dry[(size_t) (start + i)];
            ringWet[(size_t) ringPos] = t.wet[(size_t) (start + i)];
            ringPos = (ringPos + 1) & (liveN - 1);
            if (++sinceHop < hop) continue;
            sinceHop = 0;
            changed = true;
            // 1 列ぶん：ドライとウェットをそれぞれ周波数ごとの大きさに
            float colDb[2][lumen::VisualWorker::kRows];
            std::vector<float> buf ((size_t) liveN * 2);
            for (int which = 0; which < 2; ++which)
            {
                const auto& ring = which == 0 ? ringDry : ringWet;
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int k = 0; k < liveN; ++k) buf[(size_t) k] = ring[(size_t) ((ringPos + k) & (liveN - 1))];
                liveWin.multiplyWithWindowingTable (buf.data(), (size_t) liveN);
                liveFft.performFrequencyOnlyForwardTransform (buf.data());
                for (int row = 0; row < lumen::VisualWorker::kRows; ++row)
                {
                    const float fr = lumen::VisualWorker::rowFreq (row);
                    const float f0 = fr / std::pow (800.0f, 0.5f / (lumen::VisualWorker::kRows - 1)), f1 = fr * std::pow (800.0f, 0.5f / (lumen::VisualWorker::kRows - 1));
                    const int b0 = juce::jmax (1, (int) (f0 / sr * liveN)), b1 = juce::jmax (b0, (int) (f1 / sr * liveN));
                    double s2 = 0;
                    for (int b = b0; b <= b1 && b < liveN / 2; ++b) s2 += (double) buf[(size_t) b] * buf[(size_t) b];
                    colDb[which][row] = 10.0f * std::log10 ((float) (s2 / (b1 - b0 + 1)) + 1e-20f) - 20.0f * std::log10 ((float) liveN / 4.0f)
                                        + 3.0f * std::log2 (fr / 1000.0f);
                }
            }
            liveImage.moveImageSection (0, 0, 1, 0, liveCols - 1, lumen::VisualWorker::kRows);
            for (int row = 0; row < lumen::VisualWorker::kRows; ++row)
            {
                const float vw = juce::jlimit (0.0f, 1.0f, (colDb[1][row] + 96.0f) / 78.0f);
                const float vd = juce::jlimit (0.0f, 1.0f, (colDb[0][row] + 96.0f) / 78.0f);
                auto c = lumen::heatColour (std::pow (vw, 1.15f));
                const float a = juce::jlimit (0.0f, 0.8f, (vd - vw * 0.85f) * 1.6f);   // ドライが響きより大きい所は青緑
                if (a > 0.0f) c = c.interpolatedWith (juce::Colour (0xff6ff2e6).withMultipliedBrightness (0.35f + 0.65f * vd), a);
                liveImage.setPixelAt (liveCols - 1, lumen::VisualWorker::kRows - 1 - row, c);
            }
        }
    };
    if (sc.blockSize1 > 0) consume (sc.startIndex1, sc.blockSize1);
    if (sc.blockSize2 > 0) consume (sc.startIndex2, sc.blockSize2);
    if (changed) repaint (mapArea);
}

void LumenRoomsEditor::drawLiveMap (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawCaption (g, r, "REVERB", amber, "");
    auto plot = mapPlot();
    g.setColour (well);
    g.fillRoundedRectangle (plot, 6.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (plot, 6.0f);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (liveImage, plot);
    }
    g.setFont (juce::FontOptions (10.5f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = plot.getBottom() - plot.getHeight() * std::log (f / 25.0f) / std::log (800.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (faint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) plot.getX() - 30, (int) y - 6, 26, 12,
                    juce::Justification::centredRight);
    }
    for (int sec = 1; sec < 6; ++sec)
    {
        const float x = plot.getRight() - plot.getWidth() * (float) sec / 6.0f;
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (faint);
        g.drawText ("-" + juce::String (sec) + " s", (int) x - 20, (int) plot.getBottom() + 7, 40, 12, juce::Justification::centred);
    }
    g.setColour (faint);
    g.drawText ("now", (int) plot.getRight() - 30, (int) plot.getBottom() + 7, 30, 12, juce::Justification::centredRight);
    const int src = proc.liveSource.load();
    const juce::String what = src == 1 ? "warm = reflections" : (src == 2 ? "warm = reverb tail" : "warm = reflections + reverb");
    if (livePaused) drawHint (g, plot, "PAUSED  -  click to resume");
    else drawHint (g, plot, "teal = dry input   " + what + "   click = pause");
}

//==============================================================================
static int targetAt (juce::Point<float> p, juce::Rectangle<float> er, juce::Rectangle<float> map, int mapMode)
{
    if (er.contains (p)) return 1;
    if (map.contains (p) && mapMode == 0) return 2;
    return 0;
}

void LumenRoomsEditor::mouseMove (const juce::MouseEvent& e)
{
    const int t = targetAt (e.position, erPlot(), mapPlot(), proc.mapMode);
    if (t != hoverTarget)
    {
        hoverTarget = t;
        setMouseCursor (t != 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint (erArea);
        repaint (mapArea);
    }
}

void LumenRoomsEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverTarget != 0) { hoverTarget = 0; setMouseCursor (juce::MouseCursor::NormalCursor); repaint (erArea); repaint (mapArea); }
}

static std::pair<const char*, const char*> dragParams (int target)
{
    return target == 1 ? std::pair<const char*, const char*> { "erSize", "diffusion" } : std::pair<const char*, const char*> { "time", "bloom" };
}

void LumenRoomsEditor::mouseDown (const juce::MouseEvent& e)
{
    if (proc.mapMode == 1 && mapPlot().contains (e.position)) { livePaused = ! livePaused; repaint (mapArea); return; }
    dragTarget = targetAt (e.position, erPlot(), mapPlot(), proc.mapMode);
    if (dragTarget == 0) return;
    const auto ids = dragParams (dragTarget);
    auto* h = proc.apvts.getParameter (ids.first);
    auto* v = proc.apvts.getParameter (ids.second);
    dragStartX = h->getValue();
    dragStartY = v->getValue();
    h->beginChangeGesture();
    v->beginChangeGesture();
}

void LumenRoomsEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget == 0) return;
    const auto ids = dragParams (dragTarget);
    const auto plot = dragTarget == 1 ? erPlot() : mapPlot();
    const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const float dx = (float) e.getDistanceFromDragStartX() / plot.getWidth() * fine;
    const float dy = (float) -e.getDistanceFromDragStartY() / plot.getHeight() * fine;
    proc.apvts.getParameter (ids.first)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartX + dx));
    proc.apvts.getParameter (ids.second)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartY + dy));
}

void LumenRoomsEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragTarget == 0) return;
    const auto ids = dragParams (dragTarget);
    proc.apvts.getParameter (ids.first)->endChangeGesture();
    proc.apvts.getParameter (ids.second)->endChangeGesture();
    dragTarget = 0;
}

void LumenRoomsEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int t = targetAt (e.position, erPlot(), mapPlot(), proc.mapMode);
    if (t == 0) return;
    const auto ids = dragParams (t);
    for (auto id : { ids.first, ids.second })
    {
        auto* p = proc.apvts.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void LumenRoomsEditor::placeRowCols (juce::Rectangle<int> area, std::initializer_list<const char*> ids, int cols)
{
    const int cw = area.getWidth() / juce::jmax (1, cols);
    int i = 0;
    for (auto id : ids) { placeKnob (id, { area.getX() + cw * i + cw / 2, area.getCentreY() - 4 }); ++i; }
}

juce::String LumenRoomsEditor::currentPresetName() const
{
    const auto imp = proc.getImportedName();
    return imp.isNotEmpty() ? imp : proc.getProgramName (proc.getCurrentProgram());
}

void LumenRoomsEditor::toggleBrowser()
{
    if (browser != nullptr && browser->isVisible()) { browser->setVisible (false); return; }
    if (browser == nullptr) browser = std::make_unique<PresetBrowser> (proc, look);
    browser->onClose = [this] { browser->setVisible (false); };
    browser->onLoaded = [this] { repaint(); };
    browser->refresh();
    addAndMakeVisible (*browser);
    browser->setBounds (16, 62, W - 32, 400);
    browser->toFront (true);
    browser->focusSearch();
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void LumenRoomsEditor::layoutFooter()
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
