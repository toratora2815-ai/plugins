#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0e1217), panel (0xff161c24), edge (0xff232c37),
                       sky (0xff6fc3ff), dusk (0xffffb86b), text (0xffe9eef3),
                       danger (0xffff6b6b), ok (0xff5fd18a);
}

HorizonSpreadEditor::HorizonSpreadEditor (HorizonSpreadProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (spread, "spread", "SPREAD", true);
    setupKnob (mix, "mix", "MIX");
    setupKnob (output, "output", "OUTPUT");

    setupKnob (wLow, "widthLow", "LOW");
    setupKnob (wMid, "widthMid", "MID");
    setupKnob (wHigh, "widthHigh", "HIGH");
    setupKnob (xLow, "xoverLow", "LOW | MID");
    setupKnob (xHigh, "xoverHigh", "MID | HIGH");

    setupKnob (air, "air", "GAIN");
    setupKnob (airFreq, "airFreq", "FREQ");
    setupKnob (drive, "drive", "AMOUNT");
    setupKnob (transient, "transient", "TRANSIENT");
    setupKnob (dynamic, "dynamic", "DYNAMIC");
    setupKnob (bassMono, "bassMono", "BASS MONO");

    setupToggle (widthOn, "widthOn", "ON");
    setupToggle (airOn, "airOn", "ON");
    setupToggle (driveOn, "driveOn", "ON");
    setupToggle (transOn, "transOn", "ON");
    setupToggle (guard, "guard", "GUARD");
    setupToggle (monoCheck, "monoCheck", "MONO CHECK");
    setupToggle (sideSolo, "sideSolo", "SIDE SOLO");
    setupToggle (autoGain, "autoGain", "AUTO GAIN");
    monoCheck.button.setTooltip ("Hold to listen in mono (turn off before bouncing)");
    sideSolo.button.setTooltip ("Listen to the width (Side) only (turn off before bouncing)");
    autoGain.button.setTooltip ("Keep the loudness the same as the original, so wider does not just mean louder");

    setupKnob (shiftTime, "shiftTime", "TIME");
    setupKnob (shiftAmt, "shiftAmount", "AMOUNT");
    setupKnob (dblAmt, "doubleAmount", "AMOUNT");
    setupKnob (detune, "detune", "DETUNE");
    setupKnob (bandsAmt, "bandsAmount", "AMOUNT");
    setupKnob (bandsCount, "bandsCount", "BANDS");
    setupKnob (hazeAmt, "hazeAmount", "AMOUNT");
    setupKnob (hazeSize, "hazeSize", "SIZE");
    setupKnob (motionRate, "motionRate", "RATE");
    setupKnob (motionDepth, "motionDepth", "DEPTH");

    setupToggle (shiftOn, "shiftOn", "ON");
    setupToggle (doubleOn, "doubleOn", "ON");
    setupToggle (bandsOn, "bandsOn", "ON");
    setupToggle (hazeOn, "hazeOn", "ON");
    setupToggle (motionOn, "motionOn", "ON");
    setupChoice (shiftMode, "shiftMode");
    setupChoice (motionSync, "motionSync");
    setupMode (bandsMode, "bandsMode", "ALT", "TILT");
    setupMode (hazeMode, "hazeMode", "CLOUD", "SMOOTH");

    autoButton.setColour (juce::TextButton::buttonColourId, edge);
    autoButton.setColour (juce::TextButton::textColourOffId, sky);
    autoButton.setTooltip ("Play the song for 6 seconds and WIDTH is set automatically");
    autoButton.onClick = [this] { processor.engine.startAuto(); };
    addAndMakeVisible (autoButton);

    for (auto* t : { &shapeTab, &createTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (1001);
        t->setColour (juce::TextButton::buttonColourId, panel);
        t->setColour (juce::TextButton::buttonOnColourId, sky);
        t->setColour (juce::TextButton::textColourOffId, text.withAlpha (0.7f));
        t->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (t);
    }
    shapeTab.setToggleState (true, juce::dontSendNotification);
    shapeTab.onClick  = [this] { showPage (true); };
    createTab.onClick = [this] { showPage (false); };

    presetBox.setColour (juce::ComboBox::backgroundColourId, panel);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, sky);
    presetBox.setTextWhenNothingSelected ("PRESET");
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != processor.getCurrentProgram())
        {
            processor.setCurrentProgram (idx);
            shownProgram = idx;
        }
    };
    addAndMakeVisible (presetBox);
    refreshPresetBox();

    scopeL.resize (1024);
    scopeR.resize (1024);

    // Plugin Link の帯（いちばん下）。右端に VOCAL DUCK の量
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setColour (juce::Slider::trackColourId, dusk.withAlpha (0.7f));
    duckSlider.setColour (juce::Slider::backgroundColourId, edge);
    duckSlider.setColour (juce::Slider::thumbColourId, text);
    duckSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    duckSlider.setTooltip ("VOCAL DUCK: how much SPREAD narrows while Fader Pilot pushes the vocal up");
    duckAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "linkDuck", duckSlider);
    linkBar.addAndMakeVisible (duckSlider);
    linkBar.extra = &duckSlider;
    linkBar.extraWidth = 200;
    linkBar.extraLabel = "VOCAL DUCK";
    linkBar.status = [this] (bool& lit)
    {
        auto& lk = processor.link;
        if (! lk.on()) return juce::String ("Plugin Link OFF  -  set the same LINK number on Fader Pilot to narrow the width while the vocal is pushed up");
        if (! processor.linkHasPilot()) return "LINK " + juce::String (lk.group()) + ": waiting for Fader Pilot on this LINK";
        const float d = processor.linkDuckNow();
        lit = d > 0.005f;
        return lit ? "Vocal pushed up by Fader Pilot  ->  SPREAD -" + juce::String (juce::roundToInt (d * 100.0f)) + " %"
                   : juce::String ("Following Fader Pilot  (no narrowing right now)");
    };
    addAndMakeVisible (linkBar);

    setSize (1180, 638);
    showPage (true);
    startTimerHz (30);
}

void HorizonSpreadEditor::setupKnob (Knob& k, const char* id, const char* name, bool big)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 76, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, big ? dusk : sky);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (big ? 15.0f : 12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, big ? dusk : sky);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
}

void HorizonSpreadEditor::setupToggle (Toggle& t, const char* id, const juce::String& txt)
{
    t.button.setButtonText (txt);
    t.button.setClickingTogglesState (true);
    t.button.setColour (juce::TextButton::buttonColourId, edge);
    t.button.setColour (juce::TextButton::buttonOnColourId, sky);
    t.button.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.6f));
    t.button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

// 2択のモード切り替え（押すたびに文字が入れ替わる）
void HorizonSpreadEditor::setupMode (Toggle& t, const char* id, const juce::String& offText, const juce::String& onText)
{
    setupToggle (t, id, offText);
    t.button.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    t.button.setColour (juce::TextButton::buttonOnColourId, dusk);
    auto* b = &t.button;
    b->onStateChange = [b, offText, onText]
    {
        const auto txt = b->getToggleState() ? onText : offText;
        if (b->getButtonText() != txt) b->setButtonText (txt);
    };
    b->onStateChange();
}

void HorizonSpreadEditor::setupChoice (Choice& c, const char* id)
{
    c.box.setColour (juce::ComboBox::backgroundColourId, edge);
    c.box.setColour (juce::ComboBox::outlineColourId, edge);
    c.box.setColour (juce::ComboBox::textColourId, text.withAlpha (0.85f));
    c.box.setColour (juce::ComboBox::arrowColourId, sky);
    c.box.setJustificationType (juce::Justification::centred);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        c.box.addItemList (p->choices, 1);
    addAndMakeVisible (c.box);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, c.box);
}

void HorizonSpreadEditor::placeHeader (const Tile& tile, Toggle& on, juce::Component* mode, int modeWidth)
{
    auto hdr = tile.bounds.reduced (8).removeFromTop (22);
    on.button.setBounds (hdr.removeFromRight (40));
    if (mode != nullptr)
    {
        hdr.removeFromRight (6);
        mode->setBounds (hdr.removeFromRight (modeWidth));
    }
}

// Logic のメニューからプリセットが変わったときも、画面のメニューを合わせる
void HorizonSpreadEditor::refreshPresetBox()
{
    const int idx = processor.getCurrentProgram();
    if (idx != shownProgram)
    {
        shownProgram = idx;
        presetBox.setSelectedItemIndex (idx, juce::dontSendNotification);
    }
}

void HorizonSpreadEditor::showPage (bool shape)
{
    shapePage = shape;
    for (auto* k : { &wLow, &wMid, &wHigh, &xLow, &xHigh, &air, &airFreq, &drive, &transient, &dynamic })
    {
        k->slider.setVisible (shape);
        k->label.setVisible (shape);
    }
    for (auto* t : { &widthOn, &airOn, &driveOn, &transOn })
        t->button.setVisible (shape);
    autoButton.setVisible (shape);

    for (auto* k : { &shiftTime, &shiftAmt, &dblAmt, &detune, &bandsAmt, &bandsCount, &hazeAmt, &hazeSize, &motionRate, &motionDepth })
    {
        k->slider.setVisible (! shape);
        k->label.setVisible (! shape);
    }
    for (auto* t : { &shiftOn, &doubleOn, &bandsOn, &hazeOn, &motionOn, &bandsMode, &hazeMode })
        t->button.setVisible (! shape);
    shiftMode.box.setVisible (! shape);
    motionSync.box.setVisible (! shape);
    repaint();
}

juce::Rectangle<int> HorizonSpreadEditor::tileContent (const Tile& t) const
{
    return t.bounds.reduced (8).withTrimmedTop (24);
}

void HorizonSpreadEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    if (&k != &spread && r.getHeight() > 150) r = r.withSizeKeepingCentre (r.getWidth(), 150);
    k.label.setBounds (r.removeFromTop (18));
    k.slider.setBounds (r.reduced (2));
}

void HorizonSpreadEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    linkBar.setBounds (area.removeFromBottom (plink::LinkBar::height));
    area.removeFromBottom (8);
    auto header = area.removeFromTop (44);
    monoCheck.button.setBounds (header.removeFromRight (110).reduced (2, 8));
    sideSolo.button.setBounds (header.removeFromRight (100).reduced (2, 8));
    header.removeFromRight (8);
    presetBox.setBounds (header.removeFromRight (190).reduced (2, 8));
    header.removeFromLeft (406);
    shapeTab.setBounds (header.removeFromLeft (100).reduced (2, 8));
    createTab.setBounds (header.removeFromLeft (100).reduced (2, 8));
    area.removeFromTop (8);

    const int gap = 12;
    auto left = area.removeFromLeft (200);
    area.removeFromLeft (gap);
    tSafety.bounds = left.removeFromBottom (150);
    left.removeFromBottom (gap);
    tMaster.bounds = left;
    tMeter.bounds = area.removeFromRight (300);
    area.removeFromRight (gap);

    // 左：SPREAD / MIX / OUTPUT
    {
        autoGain.button.setBounds (tMaster.bounds.reduced (8).removeFromTop (22).removeFromRight (84));
        auto c = tileContent (tMaster);
        auto bigArea = c.removeFromTop (c.getHeight() - 112);
        placeKnob (spread, bigArea.reduced (10, 0));
        c.removeFromTop (6);
        auto row = c;
        placeKnob (mix, row.removeFromLeft (row.getWidth() / 2));
        placeKnob (output, row);
    }

    // 右：イメージ表示
    {
        auto c = tileContent (tMeter);
        scopeArea = c.removeFromTop (c.getWidth());
        c.removeFromTop (10);
        corrArea = c.removeFromTop (44);
        statusArea = c.removeFromTop (20);
        c.removeFromTop (6);
        bandArea = c;
    }

    // 中央：CREATE ページ（上段 SHIFT / DOUBLE / BANDS、下段 HAZE / MOTION）
    {
        auto c = area;
        auto top = c.removeFromTop (c.getHeight() * 46 / 100);
        c.removeFromTop (gap);
        const int w3 = (top.getWidth() - 2 * gap) / 3;
        tShift.bounds = top.removeFromLeft (w3);  top.removeFromLeft (gap);
        tDouble.bounds = top.removeFromLeft (w3); top.removeFromLeft (gap);
        tBands.bounds = top;
        tHaze.bounds = c.removeFromLeft ((c.getWidth() - gap) / 2); c.removeFromLeft (gap);
        tMotion.bounds = c;

        auto pair = [this] (const Tile& tile, Knob& a, Knob& b)
        {
            auto r = tileContent (tile);
            placeKnob (a, r.removeFromLeft (r.getWidth() / 2));
            placeKnob (b, r);
        };
        // 量（AMOUNT / DEPTH）のつまみは、どのタイルでも右側
        pair (tShift, shiftTime, shiftAmt);
        pair (tDouble, detune, dblAmt);
        pair (tBands, bandsCount, bandsAmt);
        pair (tHaze, hazeSize, hazeAmt);
        pair (tMotion, motionRate, motionDepth);
        placeHeader (tShift, shiftOn, &shiftMode.box, 92);
        placeHeader (tDouble, doubleOn);
        placeHeader (tBands, bandsOn, &bandsMode.button);
        placeHeader (tHaze, hazeOn, &hazeMode.button);
        placeHeader (tMotion, motionOn, &motionSync.box, 84);
    }

    // 中央：SHAPE ページ
    auto rowTop = area.removeFromTop (area.getHeight() * 46 / 100);
    area.removeFromTop (gap);
    tWidth.bounds = rowTop;
    {
        auto c = tileContent (tWidth);
        auto foot = c.removeFromBottom (20);
        juce::ignoreUnused (foot);
        const int w = c.getWidth() / 5;
        placeKnob (wLow, c.removeFromLeft (w));
        placeKnob (wMid, c.removeFromLeft (w));
        placeKnob (wHigh, c.removeFromLeft (w));
        placeKnob (xLow, c.removeFromLeft (w));
        placeKnob (xHigh, c);
        auto hdr = tWidth.bounds.reduced (8).removeFromTop (22);
        widthOn.button.setBounds (hdr.removeFromRight (40));
        hdr.removeFromRight (6);
        autoButton.setBounds (hdr.removeFromRight (64));
    }

    const float unit = (float) (area.getWidth() - 2 * gap) / 5.0f;   // AIR 1.8、DRIVE 1.3、DYNAMICS 1.9
    tAir.bounds = area.removeFromLeft ((int) (unit * 1.8f));     area.removeFromLeft (gap);
    tDrive.bounds = area.removeFromLeft ((int) (unit * 1.3f));   area.removeFromLeft (gap);
    tTrans.bounds = area;

    {
        auto c = tileContent (tAir);
        placeKnob (airFreq, c.removeFromLeft (c.getWidth() / 2));
        placeKnob (air, c);
        airOn.button.setBounds (tAir.bounds.reduced (8).removeFromTop (22).removeFromRight (40));
    }
    placeKnob (drive, tileContent (tDrive));
    driveOn.button.setBounds (tDrive.bounds.reduced (8).removeFromTop (22).removeFromRight (40));
    {
        auto c = tileContent (tTrans);
        placeKnob (transient, c.removeFromLeft (c.getWidth() / 2));
        placeKnob (dynamic, c);
    }
    transOn.button.setBounds (tTrans.bounds.reduced (8).removeFromTop (22).removeFromRight (40));
    {
        auto c = tileContent (tSafety);
        placeKnob (bassMono, c.removeFromLeft (c.getWidth() / 2));
        guard.button.setBounds (c.withSizeKeepingCentre (juce::jmin (90, c.getWidth() - 8), 30));
    }
}

void HorizonSpreadEditor::timerCallback()
{
    processor.engine.copyScope (scopeL.data(), scopeR.data(), (int) scopeL.size());
    float pk = 0.0f;
    for (size_t i = 0; i < scopeL.size(); ++i)
        pk = juce::jmax (pk, std::abs (scopeL[i]), std::abs (scopeR[i]));
    scopePeak = juce::jmax (0.05f, pk > scopePeak ? pk : scopePeak * 0.97f);
    repaint (tMeter.bounds);
    refreshPresetBox();
    const bool synced = motionSync.box.getSelectedItemIndex() > 0;
    motionRate.slider.setEnabled (! synced);
    motionRate.slider.setAlpha (synced ? 0.35f : 1.0f);
    repaint (tWidth.bounds.withTrimmedTop (tWidth.bounds.getHeight() - 30));
}

juce::String HorizonSpreadEditor::autoStatus() const
{
    using E = hs::SpreadEngine;
    switch (processor.engine.autoState.load())
    {
        case E::autoListening:
            return "AUTO: listening  " + juce::String (processor.engine.autoProgress.load(), 1)
                 + " / " + juce::String (E::autoSeconds, 0) + " s   (play the main part of the song)";
        case E::autoReady:
        case E::autoApplied:
            return "AUTO: done  -  LOW " + juce::String (juce::roundToInt (processor.engine.autoResult[0].load() * 100)) + "%"
                 + "   MID " + juce::String (juce::roundToInt (processor.engine.autoResult[1].load() * 100)) + "%"
                 + "   HIGH " + juce::String (juce::roundToInt (processor.engine.autoResult[2].load() * 100)) + "%";
        case E::autoMonoSource:
            return "AUTO: this sound is almost mono - use the CREATE page to make width";
        default:
            return "AUTO: press and play 6 s of the song to set WIDTH automatically";
    }
}

void HorizonSpreadEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("HORIZON SPREAD", 18, 14, 260, 36, juce::Justification::centredLeft);
    g.setColour (dusk.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("stereo spread toolkit", 214, 14, 200, 36, juce::Justification::centredLeft);

    std::vector<Tile*> tiles { &tMaster, &tMeter, &tSafety };
    if (shapePage) for (auto* t : { &tWidth, &tAir, &tDrive, &tTrans }) tiles.push_back (t);
    else for (auto* t : { &tShift, &tDouble, &tBands, &tHaze, &tMotion }) tiles.push_back (t);

    for (auto* t : tiles)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (t->bounds.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (t->bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (t->title, t->bounds.reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);
    }

    if (shapePage)
    {
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.0f));
        auto foot = tileContent (tWidth).removeFromBottom (20);
        g.drawText (autoStatus(), foot, juce::Justification::centredLeft);
    }

    drawScope (g, scopeArea.toFloat());
    drawCorrelation (g, corrArea.toFloat());

    const float gr = processor.engine.getGuardReductionDb();
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (gr < -0.1f ? dusk : text.withAlpha (0.4f));
    g.drawText (gr < -0.1f ? "GUARD  " + juce::String (gr, 1) + " dB" : "GUARD  idle",
                statusArea, juce::Justification::centredLeft);
    const float ag = processor.engine.getAutoGainDb();
    const bool agOn = processor.apvts.getRawParameterValue ("autoGain")->load() > 0.5f;
    g.setColour (agOn ? sky.withAlpha (0.8f) : text.withAlpha (0.4f));
    g.drawText (agOn ? "AUTO GAIN  " + juce::String (ag, 1) + " dB" : "AUTO GAIN  off",
                statusArea, juce::Justification::centredRight);
    drawBandMeter (g, bandArea.toFloat());
}

void HorizonSpreadEditor::drawScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.46f;

    g.setColour (edge);
    g.drawLine (c.x, r.getY() + 6, c.x, r.getBottom() - 6, 1.0f);
    g.drawLine (c.x - rad * 0.7071f, c.y - rad * 0.7071f, c.x + rad * 0.7071f, c.y + rad * 0.7071f, 1.0f);
    g.drawLine (c.x - rad * 0.7071f, c.y + rad * 0.7071f, c.x + rad * 0.7071f, c.y - rad * 0.7071f, 1.0f);
    g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, 1.0f);

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.4f));
    g.drawText ("L", (int) (c.x - rad * 0.75f) - 14, (int) (c.y - rad * 0.75f) - 14, 14, 14, juce::Justification::centred);
    g.drawText ("R", (int) (c.x + rad * 0.75f), (int) (c.y - rad * 0.75f) - 14, 14, 14, juce::Justification::centred);
    g.drawText ("M", (int) c.x + 4, (int) r.getY() + 4, 14, 14, juce::Justification::centred);

    const float scale = rad / (scopePeak * 1.1f);
    g.setColour (sky.withAlpha (0.45f));
    for (size_t i = 0; i < scopeL.size(); ++i)
    {
        const float side = (scopeR[i] - scopeL[i]) * 0.7071f;
        const float mid  = (scopeL[i] + scopeR[i]) * 0.7071f;
        const float x = c.x + juce::jlimit (-rad, rad, side * scale);
        const float y = c.y - juce::jlimit (-rad, rad, mid * scale);
        g.fillRect (x, y, 1.6f, 1.6f);
    }
}

void HorizonSpreadEditor::drawCorrelation (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float corr = processor.engine.getCorrelation();
    auto label = r.removeFromTop (16);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.6f));
    g.drawText ("CORRELATION", label, juce::Justification::centredLeft);
    g.drawText (juce::String (corr, 2), label, juce::Justification::centredRight);

    auto bar = r.removeFromTop (14);
    g.setColour (bg);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (danger.withAlpha (0.25f));
    g.fillRect (bar.withWidth (bar.getWidth() * 0.5f));
    g.setColour (edge);
    g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());

    const float x = bar.getX() + (corr + 1.0f) * 0.5f * bar.getWidth();
    g.setColour (corr < 0.0f ? danger : (corr < 0.2f ? dusk : ok));
    g.fillRoundedRectangle (x - 3.0f, bar.getY() - 2.0f, 6.0f, bar.getHeight() + 4.0f, 2.0f);

    g.setColour (text.withAlpha (0.4f));
    g.drawText ("-1", r.removeFromTop (14).withWidth (20), juce::Justification::centredLeft);
    g.drawText ("+1", juce::Rectangle<float> (bar.getRight() - 20, bar.getBottom(), 20, 14), juce::Justification::centredRight);
}

// 帯域ごとの広さ（Side が Mid より何 dB 小さいか）。白い線は AUTO の目安（市販曲によくある広さ）
void HorizonSpreadEditor::drawBandMeter (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.6f));
    g.drawText ("WIDTH BY BAND", r.removeFromTop (16), juce::Justification::centredLeft);

    const char* names[] { "LOW", "MID", "HIGH" };
    constexpr float lo = -30.0f, hi = 3.0f;
    for (int b = 0; b < 3; ++b)
    {
        auto row = r.removeFromTop (22).reduced (0, 4);
        g.setColour (text.withAlpha (0.6f));
        g.drawText (names[b], row.removeFromLeft (36), juce::Justification::centredLeft);
        auto value = row.removeFromRight (52);
        g.setColour (bg);
        g.fillRoundedRectangle (row, 3.0f);

        const float db = processor.engine.getBandWidthDb (b);
        const float x = juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo));
        g.setColour (db > -1.0f ? danger : sky.withAlpha (0.8f));
        g.fillRoundedRectangle (row.withWidth (row.getWidth() * x), 3.0f);

        const float tx = row.getX() + row.getWidth() * (hs::SpreadEngine::autoTargetDb[b] - lo) / (hi - lo);
        g.setColour (text.withAlpha (0.8f));
        g.drawVerticalLine ((int) tx, row.getY() - 2.0f, row.getBottom() + 2.0f);

        g.setColour (text.withAlpha (0.6f));
        g.drawText (db <= -59.0f ? juce::String ("-") : juce::String (db, 1) + " dB", value, juce::Justification::centredRight);
    }
    g.setColour (text.withAlpha (0.35f));
    g.drawText ("narrow  <---  |  target  --->  wide", r.removeFromTop (16), juce::Justification::centredLeft);
}
