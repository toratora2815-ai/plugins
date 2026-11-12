#include "PluginEditor.h"

using namespace nzoui;

namespace
{
    // 段の色（0 HUM / 1 NOISE / 2 ROOM / 3 VOICE FIX）
    juce::Colour sectionColour (int s) { return s == 0 ? humCol : s == 1 ? accent : s == 2 ? roomCol : voiceCol; }
}

//==============================================================================
void SpectrumView::paint (juce::Graphics& g)
{
    auto rr = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1a1e23));
    g.fillRoundedRectangle (rr, 6.0f);
    auto r = rr.reduced (10, 8).withTrimmedBottom (14).withTrimmedRight (34).withTrimmedLeft (30);

    // 目盛り（周波数）
    g.setFont (juce::FontOptions (10.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (hz, r);
        g.setColour (text.withAlpha (0.07f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (text.withAlpha (0.4f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (x - 20, r.getBottom() + 1, 40, 12), juce::Justification::centred);
    }
    // 大きさの目盛り: −110..−10 dB（左）。下げ量: 0..−40 dB（右、上から垂れる）
    const float lo = -110.0f, hi = -10.0f;
    auto yLevel = [&] (float db) { return r.getBottom() - r.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo); };
    auto yGain = [&] (float db) { return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 40.0f); };
    for (float d : { -10.0f, -20.0f, -30.0f })
    {
        g.setColour (text.withAlpha (0.05f));
        g.drawHorizontalLine ((int) yGain (d), r.getX(), r.getRight());
        g.setColour (redCol.withAlpha (0.55f));
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (r.getRight() + 3, yGain (d) - 6, 30, 12), juce::Justification::centredLeft);
    }
    for (float d : { -20.0f, -40.0f, -60.0f, -80.0f, -100.0f })
    {
        const float y = yLevel (d);
        g.setColour (text.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, r.getX() - 4, r.getX());
        g.setColour (text.withAlpha (0.4f));
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (rr.getX() + 2, y - 6, 32, 12), juce::Justification::centredRight);
    }

    auto& e = proc.engine;
    juce::Path pin, pthr, pgain;
    pgain.startNewSubPath (r.getX(), r.getY());
    for (int i = 0; i < nzo::numDisp; ++i)
    {
        const float x = xFor (nzo::dispHz (i), r);
        const float yi = yLevel (e.dispIn[(size_t) i].load (std::memory_order_relaxed));
        const float yt = yLevel (e.dispThr[(size_t) i].load (std::memory_order_relaxed));
        const float yg = yGain (e.dispGain[(size_t) i].load (std::memory_order_relaxed));
        if (i == 0) { pin.startNewSubPath (x, r.getBottom()); pin.lineTo (x, yi); pthr.startNewSubPath (x, yt); }
        else { pin.lineTo (x, yi); pthr.lineTo (x, yt); }
        pgain.lineTo (x, yg);
    }
    pin.lineTo (r.getRight(), r.getBottom()); pin.closeSubPath();
    pgain.lineTo (r.getRight(), r.getY()); pgain.closeSubPath();
    g.setColour (text.withAlpha (0.13f));
    g.fillPath (pin);
    g.setColour (text.withAlpha (0.3f));
    g.strokePath (pin, juce::PathStrokeType (1.0f));
    const auto k = proc.currentKnobs();
    if (k.noiseOn)
    {
        g.setColour (accent.withAlpha (0.9f));
        g.strokePath (pthr, juce::PathStrokeType (1.6f));
    }
    g.setColour (redCol.withAlpha (0.2f));
    g.fillPath (pgain);
    g.setColour (redCol.withAlpha (0.8f));
    g.strokePath (pgain, juce::PathStrokeType (1.2f));

    // ハムの倍音
    if (k.humOn)
    {
        const bool autoMode = k.humFreq == 0;
        const bool on = ! autoMode || e.humFound.load();
        const float f0 = e.humF0.load();
        g.setColour (humCol.withAlpha (on ? 0.75f : 0.25f));
        for (int h = 1; h <= k.humHarm; ++h)
        {
            const float x = xFor (f0 * (float) h, r);
            if (x > r.getRight()) break;
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
    }

    // 凡例
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto leg = r.reduced (6, 4).removeFromTop (14);
    g.setColour (text.withAlpha (0.55f)); g.drawText ("INPUT dB (left)", leg.removeFromLeft (104), juce::Justification::centredLeft);
    if (k.noiseOn) { g.setColour (accent); g.drawText (k.noiseMode == 1 && e.hasProfile() ? "LEARNED NOISE" : "NOISE ESTIMATE", leg.removeFromLeft (118), juce::Justification::centredLeft); }
    g.setColour (redCol); g.drawText ("REDUCTION dB (right)", leg.removeFromLeft (150), juce::Justification::centredLeft);
    if (k.humOn) { g.setColour (humCol); g.drawText ("HUM", leg.removeFromLeft (40), juce::Justification::centredLeft); }
}

//==============================================================================
NoiseOutEditor::NoiseOutEditor (NoiseOutProcessor& p)
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
    lnf.setColour (juce::Label::textColourId, text);
    setLookAndFeel (&lnf);

    // ---- HUM ----
    setupToggle (humOnBtn, humOnAtt, "humOn", "HUM", "Hum remover on/off: narrow notches at the mains frequency and its harmonics (+ DC offset removal)", humCol);
    setupBox (humFreqBox, humFreqAtt, "humFreq", "AUTO finds the hum (around 50 or 60 Hz) by itself in 1-2 seconds. Or choose 50 Hz / 60 Hz");
    setupKnob (humRed, "humReduction", "REDUCTION", "How deep the notches cut. 60 dB = the hum is gone; lower = narrower notches", humCol);
    setupKnob (humHarm, "humHarmonics", "HARMONICS", "How many harmonics to remove (1 = only the base). Buzz needs more (12-16)", humCol);
    setupKnob (humWidth, "humWidth", "WIDTH (Q)", "Width of each notch (Q 300-2000). Higher Q = narrower (touches the music less, but takes longer to bite). Lower Q = wider (catches a hum that drifts a little, but also cuts the music between the harmonics). Default Q 1000", humCol);

    // ---- NOISE ----
    setupToggle (noiseOnBtn, noiseOnAtt, "noiseOn", "NOISE", "Noise remover on/off: steady hiss, fans, air conditioners", accent);
    auto setOptimize = [this] (int v)
    {
        if (auto* prm = processor.apvts.getParameter ("noiseOptimize"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
            prm->endChangeGesture();
        }
    };
    musicBtn.setButtonText ("MUSIC");
    dialogueBtn.setButtonText ("DIALOGUE");
    musicBtn.setTooltip ("Tuned for music and singing: keeps long notes and reverb tails more natural");
    dialogueBtn.setTooltip ("Tuned for speech: works faster between words (Noise Out 1.0 sound)");
    musicBtn.setConnectedEdges (juce::Button::ConnectedOnRight);
    dialogueBtn.setConnectedEdges (juce::Button::ConnectedOnLeft);
    for (auto* b : { &musicBtn, &dialogueBtn })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->getProperties().set ("nzoSmall", true);
        addAndMakeVisible (b);
    }
    musicBtn.onClick = [setOptimize] { setOptimize (1); };
    dialogueBtn.onClick = [setOptimize] { setOptimize (0); };
    setupBox (noiseModeBox, noiseModeAtt, "noiseMode", "ADAPTIVE = follows the quiet parts by itself. LEARNED = uses the noise you taught with LEARN");
    learnBtn.setButtonText ("LEARN");
    learnBtn.setTooltip ("Play a part with only the noise (no voice) and press LEARN. It listens for 3 seconds, then switches to LEARNED. Press again to stop early");
    learnBtn.setColour (juce::TextButton::buttonOnColourId, accent);
    learnBtn.setClickingTogglesState (false);
    learnBtn.getProperties().set ("nzoSmall", true);
    learnBtn.onClick = [this] { processor.toggleLearn(); };
    addAndMakeVisible (learnBtn);
    setupKnob (noiseRed, "noiseReduction", "REDUCTION", "How much the noise goes down (dB). Too much sounds watery", accent);
    setupKnob (noiseThr, "noiseThreshold", "THRESHOLD", "+ = more is treated as noise (removes more), - = only clear noise (safer for quiet voice)", accent);
    setupToggle (gentleBtn, gentleAtt, "noiseGentle", "GENTLE", "Softer filtering: fewer watery artifacts on music and long notes, removes a little less", accent);

    // ---- ROOM ----
    setupToggle (roomOnBtn, roomOnAtt, "roomOn", "ROOM", "Room echo remover on/off (for voice, narration, single instruments - not for the whole mix)", roomCol);
    setupKnob (roomRed, "roomReduction", "REDUCTION", "How much room echo is removed. Minus = adds room echo back", roomCol);
    setupKnob (roomTail, "roomTail", "TAIL", "How long the room rings. Longer = removes more of the late tail", roomCol);
    setupKnob (roomB0, "roomBandLow", "LOW", "Room echo removal below about 600 Hz (6 = normal, 0 = keep the low echo, 10 = remove much more). Lower it if the voice gets thin", roomCol);
    setupKnob (roomB1, "roomBandLowMid", "LOW-MID", "Room echo removal around 600 Hz - 1.5 kHz (6 = normal). Raise it for a boxy, muddy room", roomCol);
    setupKnob (roomB2, "roomBandHighMid", "HIGH-MID", "Room echo removal around 1.5 - 4 kHz (6 = normal)", roomCol);
    setupKnob (roomB3, "roomBandHigh", "HIGH", "Room echo removal above about 4 kHz (6 = normal). Raise it for a bright, bathroom-like echo", roomCol);

    // ---- VOICE FIX（口のピチャ音・息・吹かれ）----
    setupToggle (mouthOnBtn, mouthOnAtt, "mouthOn", "MOUTH", "Removes short mouth clicks and lip smacks (very short high ticks)", voiceCol);
    setupToggle (breathOnBtn, breathOnAtt, "breathOn", "BREATH", "Turns breaths between phrases down", voiceCol);
    setupToggle (plosOnBtn, plosOnAtt, "plosiveOn", "PLOSIVE", "Removes the low pop of p / b sounds (air hitting the mic)", voiceCol);
    setupKnob (mouthSens, "mouthSens", "SENSITIVITY", "Higher = finds smaller clicks too. Too high may soften consonants like t / k", voiceCol);
    setupKnob (breathRed, "breathReduction", "REDUCTION", "How much the breaths go down (dB)", voiceCol);
    setupKnob (plosStr, "plosiveStrength", "STRENGTH", "How strongly the low pop is cut", voiceCol);
    setupKnob (mouthSkew, "mouthSkew", "MOUTH\nFREQ SKEW", "Which clicks MOUTH looks for: - = lower, duller clicks, + = only high, sharp ticks", voiceCol);
    setupKnob (breathSens, "breathSens", "BREATH\nSENSITIVITY", "Higher = more sounds are treated as breath. Too high may catch soft s / sh sounds", voiceCol);
    setupKnob (plosSens, "plosiveSens", "PLOSIVE\nSENSITIVITY", "Higher = smaller pops are caught too", voiceCol);
    setupKnob (plosFreq, "plosiveFreq", "PLOSIVE\nFREQ LIMIT", "Pops are cut below this frequency. Raise it for a big boomy pop, lower it to keep the low end of the voice", voiceCol);
    for (auto* k : { &mouthSkew, &breathSens, &plosSens, &plosFreq })
        k->label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    voiceTitle.setText ("VOICE FIX", juce::dontSendNotification);
    voiceTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    voiceTitle.setColour (juce::Label::textColourId, voiceCol);
    voiceTitle.setTooltip ("Mouth clicks, breaths and p / b pops - for voice and narration");
    addAndMakeVisible (voiceTitle);

    // ---- OUTPUT / DELTA ----
    setupKnob (output, "output", "OUTPUT", "Output level", text);
    setupToggle (deltaBtn, deltaAtt, "delta", "DELTA", "Listen only to what is being removed. If you hear the voice in it, turn REDUCTION down", redCol);

    // ---- DETAIL ----
    setupDetail (0, "Detail: notch width (Q)");
    setupDetail (1, "Detail: GENTLE filter");
    setupDetail (2, "Detail: room echo strength per frequency band");
    setupDetail (3, "Detail: mouth click frequency, breath / plosive sensitivity, plosive frequency limit");

    for (auto* l : { &humStatus, &noiseStatus, &roomStatus, &voiceStatus })
    {
        l->setFont (juce::FontOptions (11.0f));
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, text.withAlpha (0.65f));
        l->setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (l);
    }
    auto note = [this] (juce::Label& l, const juce::String& s, juce::Colour c)
    {
        l.setText (s, juce::dontSendNotification);
        l.setFont (juce::FontOptions (11.0f));
        l.setJustificationType (juce::Justification::centredLeft);
        l.setColour (juce::Label::textColourId, c.withAlpha (0.8f));
        l.setMinimumHorizontalScale (0.85f);
        addChildComponent (l);
    };
    note (roomHint, "for voice / single instruments (not a full mix)", text.withAlpha (0.7f));
    note (detailNoteRoom, "per band: 6 = normal, 0 = keep, 10 = remove much more", roomCol);
    note (detailNoteHum, "Higher Q = narrower notches, touches the music less. Lower Q = wider, for a hum that drifts (also cuts a little music).", humCol);
    note (detailNoteNoise, "GENTLE = smoother, less watery sound on music and long notes. Removes a little less.", accent);
    for (auto* l : { &detailNoteHum, &detailNoteNoise }) l->setJustificationType (juce::Justification::topLeft);

    factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    for (int i = 0; i < NoiseOutProcessor::numFactoryPresets(); ++i) factoryBox.addItem (NoiseOutProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int id = factoryBox.getSelectedId();
        if (id > 0) processor.loadFactoryPreset (id - 1);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
        factoryBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    };
    addAndMakeVisible (factoryBox);
    addAndMakeVisible (spectrum);

    // 下の欄
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Fader Pilot: noise learning pauses while the voice sings";
        if (! processor.sawPilot.load()) return "waiting for Fader Pilot";
        lit = processor.pilotSinging.load();
        return lit ? "voice singing - noise learning paused" : "Fader Pilot found - learning the noise between phrases";
    };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    clickRing.fill (processor.engine.mouthClicks.load());
    startTimerHz (30);
    setSize (W, topH + graphH + 10 + rowH + 10 + footerH);
}

NoiseOutEditor::~NoiseOutEditor()
{
    setLookAndFeel (nullptr);
}

void NoiseOutEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& tip, juce::Colour c)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 17);
    s.setTooltip (tip);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    addAndMakeVisible (s);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    k.label.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Attachment> (processor.apvts, paramId, s);
    s.updateText();
}

void NoiseOutEditor::setupBox (juce::ComboBox& b, std::unique_ptr<BoxAttachment>& attach, const juce::String& paramId, const juce::String& tip)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        for (int i = 0; i < c->choices.size(); ++i) b.addItem (c->choices[i].toUpperCase(), i + 1);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    attach = std::make_unique<BoxAttachment> (processor.apvts, paramId, b);
}

void NoiseOutEditor::setupToggle (juce::TextButton& b, std::unique_ptr<BtnAttachment>& attach, const juce::String& paramId, const juce::String& t, const juce::String& tip, juce::Colour c)
{
    b.setButtonText (t);
    b.setClickingTogglesState (true);
    b.setTooltip (tip);
    b.setColour (juce::TextButton::buttonOnColourId, c);
    b.getProperties().set ("nzoSmall", true);
    addAndMakeVisible (b);
    attach = std::make_unique<BtnAttachment> (processor.apvts, paramId, b);
}

void NoiseOutEditor::setupDetail (int s, const juce::String& tip)
{
    auto& b = detailBtn[s];
    b.setButtonText ("DETAIL");
    b.setClickingTogglesState (false);
    b.setTooltip (tip + ". A dot = something here is changed from the default. Double-click = back to the default");
    b.setColour (juce::TextButton::buttonOnColourId, sectionColour (s));
    b.getProperties().set ("nzoSmall", true);
    b.dotColour = sectionColour (s);
    b.onClick = [this, s] { setDetailOpen (s, ! detailOpen[s]); };
    b.onDoubleClick = [this, s] { resetDetail (s); };
    addAndMakeVisible (b);
}

void NoiseOutEditor::setDetailOpen (int s, bool open)
{
    detailOpen[s] = open;
    detailBtn[s].setToggleState (open, juce::dontSendNotification);
    resized();
    repaint();
}

bool NoiseOutEditor::detailChanged (int s) const
{
    for (const auto& [id, sec] : NoiseOutProcessor::detailParams())
        if (sec == s)
            if (auto* prm = processor.apvts.getParameter (id))
                if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1.0e-4f) return true;
    return false;
}

void NoiseOutEditor::resetDetail (int s)
{
    for (const auto& [id, sec] : NoiseOutProcessor::detailParams())
        if (sec == s)
            if (auto* prm = processor.apvts.getParameter (id))
                if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1.0e-6f)
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->getDefaultValue());
                    prm->endChangeGesture();
                }
}

void NoiseOutEditor::timerCallback()
{
    const auto k = processor.currentKnobs();
    auto& e = processor.engine;
    // HUM の様子
    {
        juce::String s;
        if (! k.humOn) s = "off";
        else if (k.humFreq != 0) s = "notches at " + juce::String (k.humFreq == 1 ? 50 : 60) + " Hz x " + juce::String (k.humHarm);
        else if (e.humFound.load()) s = "found hum at " + juce::String (e.humF0.load(), 2) + " Hz";
        else s = "searching for hum (none found yet)";
        humStatus.setText (s, juce::dontSendNotification);
    }
    // NOISE の様子
    {
        const bool learning = processor.isLearning();
        learnBtn.setToggleState (learning, juce::dontSendNotification);
        learnBtn.setButtonText (learning ? "LEARNING " + juce::String (juce::roundToInt (e.learnProgress.load() * 100.0f)) + "%" : "LEARN");
        musicBtn.setToggleState (k.noiseMusic, juce::dontSendNotification);
        dialogueBtn.setToggleState (! k.noiseMusic, juce::dontSendNotification);
        juce::String s;
        if (! k.noiseOn) s = "off";
        else if (learning) s = "learning the noise - play a part with no voice";
        else if (k.noiseMode == 1 && ! e.hasProfile()) s = "nothing learned yet - press LEARN (using ADAPTIVE)";
        else if (k.noiseMode == 1) s = "using the learned noise";
        else if (e.adaptFrozen.load()) s = "voice singing (Fader Pilot) - estimate paused";
        else s = "following the noise (" + juce::String (juce::roundToInt (e.noiseFloorDb.load())) + " dB)";
        noiseStatus.setText (s, juce::dontSendNotification);
    }
    roomStatus.setText (k.roomOn ? "removing room echo (" + juce::String (k.roomTail, 2) + " s tail)" : "off", juce::dontSendNotification);
    // VOICE FIX の様子: 直近 10 秒に直したピチャ音の数、息・吹かれをいま下げている量（1.5 秒ほど残して読みやすく）
    {
        const int total = e.mouthClicks.load (std::memory_order_relaxed);
        const int old = clickRing[(size_t) clickTick];
        clickRing[(size_t) clickTick] = total;
        clickTick = (clickTick + 1) % (int) clickRing.size();
        breathHold = std::max (e.breathCutDb.load (std::memory_order_relaxed), breathHold - 0.6f);
        plosHold = std::max (e.plosCutDb.load (std::memory_order_relaxed), plosHold - 0.6f);
        juce::StringArray parts;
        if (k.mouthOn) parts.add (juce::String (std::max (0, total - old)) + " clicks (10 s)");
        auto cut = [] (float db) { const int v = juce::roundToInt (std::max (0.0f, db)); return v == 0 ? juce::String ("0 dB") : "-" + juce::String (v) + " dB"; };
        if (k.breathOn) parts.add ("breath " + cut (breathHold));
        if (k.plosiveOn) parts.add ("pop " + cut (plosHold));
        voiceStatus.setText (parts.isEmpty() ? juce::String ("off") : parts.joinIntoString ("  /  "), juce::dontSendNotification);
    }
    // DETAIL の点（中のつまみが初期値から動いている）
    for (int s = 0; s < 4; ++s)
    {
        auto& b = detailBtn[s];
        const bool ch = detailChanged (s);
        if (b.changed != ch) { b.changed = ch; b.repaint(); }
        b.setColour (juce::TextButton::textColourOffId, ch ? sectionColour (s) : text.withAlpha (0.85f));
    }
    // 切っているモジュールのつまみは薄く
    auto dim = [] (std::initializer_list<juce::Component*> cs, bool on) { for (auto* c : cs) c->setAlpha (on ? 1.0f : 0.4f); };
    dim ({ &humRed.slider, &humHarm.slider, &humWidth.slider, &humFreqBox }, k.humOn);
    dim ({ &noiseRed.slider, &noiseThr.slider, &noiseModeBox, &learnBtn, &musicBtn, &dialogueBtn, &gentleBtn }, k.noiseOn);
    dim ({ &roomRed.slider, &roomTail.slider, &roomB0.slider, &roomB1.slider, &roomB2.slider, &roomB3.slider }, k.roomOn);
    dim ({ &mouthSens.slider, &mouthSkew.slider }, k.mouthOn);
    dim ({ &breathRed.slider, &breathSens.slider }, k.breathOn);
    dim ({ &plosStr.slider, &plosSens.slider, &plosFreq.slider }, k.plosiveOn);
    spectrum.repaint();
}

void NoiseOutEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff1b1f24));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (accent);
    g.fillRect (0, 48, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("NOISE OUT", 16, 0, 200, 48, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("hum / noise / room echo / mouth clicks, breaths, pops", 160, 2, 360, 46, juce::Justification::centredLeft);

    auto section = [&] (juce::Rectangle<int> r, juce::Colour c, bool open)
    {
        g.setColour (juce::Colour (0xff1b1f24));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        if (open)   // DETAIL を開いている段は、薄く色を敷いて分かるように
        {
            g.setColour (c.withAlpha (0.06f));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }
        g.setColour (c.withAlpha (0.5f));
        g.fillRoundedRectangle (r.toFloat().removeFromTop (3.0f), 1.5f);
    };
    section (humArea, humCol, detailOpen[0]);
    section (noiseArea, accent, detailOpen[1]);
    section (roomArea, roomCol, detailOpen[2]);
    section (voiceArea, voiceCol, detailOpen[3]);
    section (outArea, text, false);
}

void NoiseOutEditor::placeKnob (Knob& k, juce::Rectangle<int> col, int labelH)
{
    // 列が細いとつまみ（丸）が小さくなるので、ラベルごと縦の真ん中に寄せる（丸の下に数値の箱 17 px）
    const int sliderH = std::min (col.getHeight() - labelH, col.getWidth() - 4 + 17 + 6);
    auto block = col.withSizeKeepingCentre (col.getWidth(), labelH + sliderH);
    k.label.setBounds (block.removeFromTop (labelH));
    k.slider.setBounds (block.reduced (2, 0));
}

void NoiseOutEditor::resized()
{
    // 下の欄（1 段: 左 LINK、右 GAIN MATCH・プリセット・取り消し）
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
        presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
        f.removeFromRight (6);
        linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));
    }
    factoryBox.setBounds (getWidth() - 296, 10, 280, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (topH).reduced (12, 0);
    graphArea = area.removeFromTop (graphH);
    spectrum.setBounds (graphArea);
    area.removeFromTop (10);
    auto row = area.removeFromTop (rowH);
    humArea = row.removeFromLeft (220);   row.removeFromLeft (8);
    noiseArea = row.removeFromLeft (296); row.removeFromLeft (8);
    roomArea = row.removeFromLeft (268);  row.removeFromLeft (8);
    outArea = row.removeFromRight (100);  row.removeFromRight (8);
    voiceArea = row;

    // 段の中: 見出し 26 / 選ぶ欄 24 / つまみ / 様子 18
    struct Parts { juce::Rectangle<int> head, opts, body, status; };
    auto split = [] (juce::Rectangle<int> a)
    {
        Parts pt;
        auto r = a.reduced (8, 8);
        r.removeFromTop (2);
        pt.head = r.removeFromTop (26);   r.removeFromTop (6);
        pt.opts = r.removeFromTop (24);   r.removeFromTop (6);
        pt.status = r.removeFromBottom (18); r.removeFromBottom (2);
        pt.body = r;
        return pt;
    };
    auto show = [] (std::initializer_list<juce::Component*> cs, bool v) { for (auto* c : cs) c->setVisible (v); };
    auto cols = [] (juce::Rectangle<int> r, int n, int i)
    {
        const int w = r.getWidth() / n;
        return i == n - 1 ? r.withTrimmedLeft (w * i) : juce::Rectangle<int> (r.getX() + w * i, r.getY(), w, r.getHeight());
    };

    // HUM
    {
        auto pt = split (humArea);
        humOnBtn.setBounds (pt.head.removeFromLeft (70));
        detailBtn[0].setBounds (pt.head.removeFromRight (70));
        humFreqBox.setBounds (pt.opts.removeFromLeft (120));
        humStatus.setBounds (pt.status);
        const bool d = detailOpen[0];
        show ({ &humRed.slider, &humRed.label, &humHarm.slider, &humHarm.label }, ! d);
        show ({ &humWidth.slider, &humWidth.label, &detailNoteHum }, d);
        if (! d) { placeKnob (humRed, cols (pt.body, 2, 0)); placeKnob (humHarm, cols (pt.body, 2, 1)); }
        else { placeKnob (humWidth, cols (pt.body, 2, 0)); detailNoteHum.setBounds (cols (pt.body, 2, 1).reduced (4, 18)); }
    }
    // NOISE
    {
        auto pt = split (noiseArea);
        noiseOnBtn.setBounds (pt.head.removeFromLeft (70));
        detailBtn[1].setBounds (pt.head.removeFromRight (70));
        pt.head.removeFromRight (8);
        dialogueBtn.setBounds (pt.head.removeFromRight (70));
        musicBtn.setBounds (pt.head.removeFromRight (56));
        noiseModeBox.setBounds (pt.opts.removeFromLeft (124));
        pt.opts.removeFromLeft (8);
        learnBtn.setBounds (pt.opts.removeFromLeft (124));
        noiseStatus.setBounds (pt.status);
        const bool d = detailOpen[1];
        show ({ &noiseRed.slider, &noiseRed.label, &noiseThr.slider, &noiseThr.label }, ! d);
        show ({ &gentleBtn, &detailNoteNoise }, d);
        if (! d) { placeKnob (noiseRed, cols (pt.body, 2, 0)); placeKnob (noiseThr, cols (pt.body, 2, 1)); }
        else
        {
            auto b = pt.body.reduced (4, 0);
            gentleBtn.setBounds (b.removeFromTop (b.getHeight() / 2 + 6).withSizeKeepingCentre (110, 28));
            detailNoteNoise.setBounds (b);
        }
    }
    // ROOM
    {
        auto pt = split (roomArea);
        roomOnBtn.setBounds (pt.head.removeFromLeft (70));
        detailBtn[2].setBounds (pt.head.removeFromRight (70));
        const bool d = detailOpen[2];
        roomHint.setBounds (pt.opts);
        detailNoteRoom.setBounds (pt.opts);
        roomHint.setVisible (! d);
        detailNoteRoom.setVisible (d);
        roomStatus.setBounds (pt.status);
        show ({ &roomRed.slider, &roomRed.label, &roomTail.slider, &roomTail.label }, ! d);
        show ({ &roomB0.slider, &roomB0.label, &roomB1.slider, &roomB1.label, &roomB2.slider, &roomB2.label, &roomB3.slider, &roomB3.label }, d);
        if (! d) { placeKnob (roomRed, cols (pt.body, 2, 0)); placeKnob (roomTail, cols (pt.body, 2, 1)); }
        else
        {
            Knob* ks[4] { &roomB0, &roomB1, &roomB2, &roomB3 };
            for (int i = 0; i < 4; ++i) placeKnob (*ks[i], cols (pt.body, 4, i));
        }
    }
    // VOICE FIX
    {
        auto pt = split (voiceArea);
        voiceTitle.setBounds (pt.head.removeFromLeft (120));
        detailBtn[3].setBounds (pt.head.removeFromRight (70));
        mouthOnBtn.setBounds (cols (pt.opts, 3, 0).reduced (3, 0));
        breathOnBtn.setBounds (cols (pt.opts, 3, 1).reduced (3, 0));
        plosOnBtn.setBounds (cols (pt.opts, 3, 2).reduced (3, 0));
        voiceStatus.setBounds (pt.status);
        const bool d = detailOpen[3];
        show ({ &mouthSens.slider, &mouthSens.label, &breathRed.slider, &breathRed.label, &plosStr.slider, &plosStr.label }, ! d);
        show ({ &mouthSkew.slider, &mouthSkew.label, &breathSens.slider, &breathSens.label, &plosSens.slider, &plosSens.label, &plosFreq.slider, &plosFreq.label }, d);
        if (! d)
        {
            placeKnob (mouthSens, cols (pt.body, 3, 0));
            placeKnob (breathRed, cols (pt.body, 3, 1));
            placeKnob (plosStr, cols (pt.body, 3, 2));
        }
        else
        {
            Knob* ks[4] { &mouthSkew, &breathSens, &plosSens, &plosFreq };
            for (int i = 0; i < 4; ++i) placeKnob (*ks[i], cols (pt.body, 4, i), 28);
        }
    }
    // OUTPUT / DELTA
    {
        auto pt = split (outArea);
        deltaBtn.setBounds (pt.head);
        placeKnob (output, pt.body);
    }
}
