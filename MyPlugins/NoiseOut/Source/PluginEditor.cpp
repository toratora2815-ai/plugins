#include "PluginEditor.h"

using namespace nzoui;

//==============================================================================
void SpectrumView::paint (juce::Graphics& g)
{
    auto rr = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1a1e23));
    g.fillRoundedRectangle (rr, 6.0f);
    auto r = rr.reduced (10, 8).withTrimmedBottom (14).withTrimmedRight (34);

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
    // 大きさの目盛り: −110..−10 dB（左）。下げ量: 0..−40 dB（右、上から）
    const float lo = -110.0f, hi = -10.0f;
    auto yLevel = [&] (float db) { return r.getBottom() - r.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo); };
    auto yGain = [&] (float db) { return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 40.0f); };
    g.setColour (text.withAlpha (0.35f));
    for (float d : { -10.0f, -20.0f, -30.0f })
    {
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (r.getRight() + 2, yGain (d) - 6, 32, 12), juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.05f));
        g.drawHorizontalLine ((int) yGain (d), r.getX(), r.getRight());
        g.setColour (text.withAlpha (0.35f));
    }

    auto& e = proc.engine;
    // 入口
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
    g.setColour (text.withAlpha (0.55f)); g.drawText ("INPUT", leg.removeFromLeft (52), juce::Justification::centredLeft);
    if (k.noiseOn) { g.setColour (accent); g.drawText (k.noiseMode == 1 && e.hasProfile() ? "LEARNED NOISE" : "NOISE ESTIMATE", leg.removeFromLeft (118), juce::Justification::centredLeft); }
    g.setColour (redCol); g.drawText ("REDUCTION (right scale)", leg.removeFromLeft (170), juce::Justification::centredLeft);
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

    setupToggle (humOnBtn, humOnAtt, "humOn", "HUM", "Hum remover on/off: narrow notches at the mains frequency and its harmonics (+ DC offset removal)", humCol);
    setupBox (humFreqBox, humFreqAtt, "humFreq", "AUTO finds the hum (around 50 or 60 Hz) by itself in 1-2 seconds. Or choose 50 Hz / 60 Hz");
    setupKnob (humRed, "humReduction", "REDUCTION", "How deep the notches cut. 60 dB = the hum is gone; lower = narrower notches", humCol);
    setupKnob (humHarm, "humHarmonics", "HARMONICS", "How many harmonics to remove (1 = only the base). Buzz needs more (12-16)", humCol);

    setupToggle (noiseOnBtn, noiseOnAtt, "noiseOn", "NOISE", "Noise remover on/off: steady hiss, fans, air conditioners", accent);
    setupBox (noiseModeBox, noiseModeAtt, "noiseMode", "ADAPTIVE = follows the quiet parts by itself. LEARNED = uses the noise you taught with LEARN");
    learnBtn.setButtonText ("LEARN");
    learnBtn.setTooltip ("Play a part with only the noise (no voice) and press LEARN. It listens for 3 seconds, then switches to LEARNED. Press again to stop early");
    learnBtn.setColour (juce::TextButton::buttonOnColourId, accent);
    learnBtn.setClickingTogglesState (false);
    learnBtn.onClick = [this] { processor.toggleLearn(); };
    addAndMakeVisible (learnBtn);
    setupKnob (noiseRed, "noiseReduction", "REDUCTION", "How much the noise goes down (dB). Too much sounds watery", accent);
    setupKnob (noiseThr, "noiseThreshold", "THRESHOLD", "+ = more is treated as noise (removes more), - = only clear noise (safer for quiet voice)", accent);

    setupToggle (roomOnBtn, roomOnAtt, "roomOn", "ROOM", "Room echo remover on/off (for voice, narration, single instruments - not for the whole mix)", roomCol);
    setupKnob (roomRed, "roomReduction", "REDUCTION", "How much room echo is removed. Minus = adds room echo back", roomCol);
    setupKnob (roomTail, "roomTail", "TAIL", "How long the room rings. Longer = removes more of the late tail", roomCol);

    setupKnob (output, "output", "OUTPUT", "Output level", text);
    setupToggle (deltaBtn, deltaAtt, "delta", "DELTA", "Listen only to what is being removed. If you hear the voice in it, turn REDUCTION down", redCol);

    for (auto* l : { &humStatus, &noiseStatus, &roomStatus })
    {
        l->setFont (juce::FontOptions (11.0f));
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, text.withAlpha (0.65f));
        addAndMakeVisible (l);
    }

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

    startTimerHz (30);
    setSize (960, 596);
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
    addAndMakeVisible (b);
    attach = std::make_unique<BtnAttachment> (processor.apvts, paramId, b);
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
        juce::String s;
        if (! k.noiseOn) s = "off";
        else if (learning) s = "learning the noise - play a part with no voice";
        else if (k.noiseMode == 1 && ! e.hasProfile()) s = "nothing learned yet - press LEARN (using ADAPTIVE)";
        else if (k.noiseMode == 1) s = "using the learned noise";
        else if (e.adaptFrozen.load()) s = "voice singing (Fader Pilot) - estimate paused";
        else s = "following the noise (" + juce::String (juce::roundToInt (e.noiseFloorDb.load())) + " dB)";
        noiseStatus.setText (s, juce::dontSendNotification);
    }
    roomStatus.setText (k.roomOn ? "removing room echo (" + juce::String (k.roomTail, 1) + " s tail)" : "off", juce::dontSendNotification);
    // 切っているモジュールのつまみは薄く
    for (auto* c : std::initializer_list<juce::Component*> { &humRed.slider, &humHarm.slider, &humFreqBox }) c->setAlpha (k.humOn ? 1.0f : 0.4f);
    for (auto* c : std::initializer_list<juce::Component*> { &noiseRed.slider, &noiseThr.slider, &noiseModeBox, &learnBtn }) c->setAlpha (k.noiseOn ? 1.0f : 0.4f);
    for (auto* c : std::initializer_list<juce::Component*> { &roomRed.slider, &roomTail.slider }) c->setAlpha (k.roomOn ? 1.0f : 0.4f);
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
    g.drawText ("noise / hum / room echo remover", 160, 2, 260, 46, juce::Justification::centredLeft);

    auto section = [&] (juce::Rectangle<int> r, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff1b1f24));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (c.withAlpha (0.5f));
        g.fillRoundedRectangle (r.toFloat().removeFromTop (3.0f), 1.5f);
    };
    section (humArea, humCol);
    section (noiseArea, accent);
    section (roomArea, roomCol);
    section (outArea, text);
}

void NoiseOutEditor::placeKnob (Knob& k, juce::Rectangle<int> col)
{
    k.label.setBounds (col.removeFromTop (16));
    k.slider.setBounds (col.reduced (2, 0));
}

void NoiseOutEditor::resized()
{
    // 下の欄（2 段）
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
    factoryBox.setBounds (getWidth() - 296, 10, 280, 28);

    auto area = getLocalBounds().withTrimmedBottom (footerH).withTrimmedTop (58).reduced (12, 0);
    graphArea = area.removeFromTop (240);
    spectrum.setBounds (graphArea);
    area.removeFromTop (10);
    auto row = area.removeFromTop (204);
    humArea = row.removeFromLeft (290);
    row.removeFromLeft (8);
    outArea = row.removeFromRight (110);
    row.removeFromRight (8);
    roomArea = row.removeFromRight (210);
    row.removeFromRight (8);
    noiseArea = row;

    auto header = [] (juce::Rectangle<int>& r) { auto h = r.reduced (8, 8); return h.removeFromTop (28); };
    // HUM
    {
        auto r = humArea;
        auto h = header (r);
        humOnBtn.setBounds (h.removeFromLeft (70));
        h.removeFromLeft (8);
        humFreqBox.setBounds (h.removeFromLeft (110));
        auto body = humArea.reduced (8, 8).withTrimmedTop (34);
        humStatus.setBounds (body.removeFromBottom (18));
        const int w = body.getWidth() / 2;
        placeKnob (humRed, body.removeFromLeft (w));
        placeKnob (humHarm, body);
    }
    // NOISE
    {
        auto r = noiseArea;
        auto h = header (r);
        noiseOnBtn.setBounds (h.removeFromLeft (76));
        h.removeFromLeft (8);
        noiseModeBox.setBounds (h.removeFromLeft (120));
        h.removeFromLeft (8);
        learnBtn.setBounds (h.removeFromLeft (110));
        auto body = noiseArea.reduced (8, 8).withTrimmedTop (34);
        noiseStatus.setBounds (body.removeFromBottom (18));
        const int w = body.getWidth() / 2;
        placeKnob (noiseRed, body.removeFromLeft (w));
        placeKnob (noiseThr, body);
    }
    // ROOM
    {
        auto r = roomArea;
        auto h = header (r);
        roomOnBtn.setBounds (h.removeFromLeft (76));
        auto body = roomArea.reduced (8, 8).withTrimmedTop (34);
        roomStatus.setBounds (body.removeFromBottom (18));
        const int w = body.getWidth() / 2;
        placeKnob (roomRed, body.removeFromLeft (w));
        placeKnob (roomTail, body);
    }
    // OUTPUT / DELTA
    {
        auto r = outArea.reduced (8, 8);
        deltaBtn.setBounds (r.removeFromTop (28));
        r.removeFromBottom (18);
        placeKnob (output, r);
    }
}
