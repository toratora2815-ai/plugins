#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0f1316), panel (0xff182026), edge (0xff26323b),
                       sky (0xff7cc7ff), amber (0xffffc86b), mint (0xff74e0b5), coral (0xffff7a6b),
                       text (0xffe8eef2);

    constexpr float levelTop = 0.0f, levelBottom = -60.0f;   // 音量の表示範囲
    constexpr float gainRange = 15.0f;                        // フェーダーの表示範囲（±）
}

FaderPilotEditor::FaderPilotEditor (FaderPilotProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (target, "target", "TARGET", true);
    setupKnob (floorKnob, "floor", "FLOOR");
    setupKnob (range, "range", "RANGE", true);
    setupKnob (speed, "speed", "SPEED");
    setupKnob (offset, "offset", "OFFSET");
    setupKnob (output, "output", "OUTPUT");

    setupToggle (autoRide, "auto", "AUTO RIDE", sky);
    setupToggle (write, "write", "WRITE", coral);
    setupToggle (follow, "follow", "FOLLOW MUSIC", mint);
    autoRide.button.setTooltip ("ON: ride automatically. OFF: use the RIDE fader / automation as it is");
    write.button.setTooltip ("While playing with AUTO RIDE on, send the fader moves to the Ride parameter (record with Latch / Touch)");
    follow.button.setTooltip ("Aim at the sidechain (music) level + OFFSET instead of TARGET");

    presetBox.addItemList (FaderPilotProcessor::presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESET");
    presetBox.setColour (juce::ComboBox::backgroundColourId, edge);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, sky);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0) processor.applyPreset (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    fader.setColour (juce::Slider::trackColourId, amber.withAlpha (0.6f));
    fader.setColour (juce::Slider::backgroundColourId, edge);
    fader.setColour (juce::Slider::thumbColourId, text);
    fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    fader.setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);
    faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "ride", fader);

    linkBar.setSources (processor.link, { plink::kindBalanceScope });
    linkBar.status = [this] (bool& lit)
    {
        auto& lk = processor.link;
        if (! lk.on()) return juce::String ("Plugin Link OFF  -  same LINK as Horizon Spread (narrow width) / Balance Scope on the music bus (FOLLOW MUSIC)");
        const int n = lk.ep().countPeers();
        if (n == 0) return "LINK " + juce::String (lk.group()) + ": no other plugin on this LINK yet";
        lit = processor.engine.isVoiceActive();
        const float r = processor.engine.getGainDb();
        juce::String t = "Sending fader " + juce::String ((r > 0.05f ? "+" : "") + juce::String (r, 1)) + " dB"
                       + (lit ? " (vocal active)" : " (vocal quiet)");
        plink::Peer bsPeer;
        if (lk.ep().find (plink::kindBalanceScope, bsPeer))
            t << "   |   music from " << plink::Link::describe (bsPeer) << ": " << juce::String (bsPeer.v[plink::bs::musicDb], 1) << " dB"
              << (processor.engine.isMusicFromLink() ? " (used)" : (processor.apvts.getRawParameterValue ("follow")->load() > 0.5f ? " (sidechain wins)" : ""));
        return t;
    };
    addAndMakeVisible (linkBar);

    points.reserve (shownPoints);
    setSize (940, 618);
    startTimerHz (30);
}

void FaderPilotEditor::setupKnob (Knob& k, const char* id, const char* name, bool big)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 76, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, big ? sky : sky.withAlpha (0.6f));
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (big ? 15.0f : 12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, big ? sky : text.withAlpha (0.75f));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
}

void FaderPilotEditor::setupToggle (Toggle& t, const char* id, const juce::String& txt, juce::Colour on)
{
    t.button.setButtonText (txt);
    t.button.setClickingTogglesState (true);
    t.button.setColour (juce::TextButton::buttonColourId, edge);
    t.button.setColour (juce::TextButton::buttonOnColourId, on);
    t.button.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.6f));
    t.button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

juce::Rectangle<int> FaderPilotEditor::tileContent (const Tile& t) const
{
    return t.bounds.reduced (8).withTrimmedTop (24);
}

void FaderPilotEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (18));
    k.slider.setBounds (r.reduced (2));
}

void FaderPilotEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    linkBar.setBounds (area.removeFromBottom (plink::LinkBar::height));
    area.removeFromBottom (8);
    auto header = area.removeFromTop (44);
    autoRide.button.setBounds (header.removeFromRight (110).reduced (2, 8));
    header.removeFromRight (6);
    write.button.setBounds (header.removeFromRight (80).reduced (2, 8));
    header.removeFromRight (6);
    follow.button.setBounds (header.removeFromRight (120).reduced (2, 8));
    header.removeFromRight (6);
    presetBox.setBounds (header.removeFromRight (190).reduced (2, 8));
    area.removeFromTop (8);

    const int gap = 12;
    auto bottom = area.removeFromBottom (180);
    area.removeFromBottom (gap);
    tFader.bounds = area.removeFromRight (130);
    area.removeFromRight (gap);
    tGraph.bounds = area;
    graphArea = tileContent (tGraph);
    faderArea = tileContent (tFader);
    fader.setBounds (faderArea.reduced (30, 0).withTrimmedTop (20));   // 上の 20px は今の位置の数字

    // 下段：LEVEL 2、MOTION 2、MUSIC 1、OUTPUT 1
    const float unit = (float) (bottom.getWidth() - 3 * gap) / 6.0f;
    tLevel.bounds = bottom.removeFromLeft ((int) (unit * 2));  bottom.removeFromLeft (gap);
    tMotion.bounds = bottom.removeFromLeft ((int) (unit * 2)); bottom.removeFromLeft (gap);
    tMusic.bounds = bottom.removeFromLeft ((int) unit);        bottom.removeFromLeft (gap);
    tOut.bounds = bottom;

    auto pair = [this] (const Tile& tile, Knob& a, Knob& b)
    {
        auto r = tileContent (tile);
        placeKnob (a, r.removeFromLeft (r.getWidth() / 2));
        placeKnob (b, r);
    };
    pair (tLevel, floorKnob, target);   // 量のつまみ（TARGET / RANGE）は右側
    pair (tMotion, speed, range);
    placeKnob (offset, tileContent (tMusic));
    placeKnob (output, tileContent (tOut));
}

void FaderPilotEditor::timerCallback()
{
    fp::RiderEngine::Point buf[256];
    int n;
    while ((n = processor.engine.readHistory (buf, 256, readPos)) > 0)
        for (int i = 0; i < n; ++i)
        {
            if ((int) points.size() >= shownPoints) points.erase (points.begin());
            points.push_back (buf[i]);
        }

    liveGain = processor.engine.getGainDb();
    musicDb = processor.engine.getMusicDb();

    const bool isAuto = param ("auto") > 0.5f;
    fader.setEnabled (! isAuto);
    write.button.setEnabled (isAuto);
    offset.slider.setEnabled (param ("follow") > 0.5f);
    target.slider.setEnabled (param ("follow") < 0.5f);
    repaint();
}

void FaderPilotEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("FADER PILOT", 18, 14, 200, 36, juce::Justification::centredLeft);
    g.setColour (sky.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("vocal level rider", 172, 14, 160, 36, juce::Justification::centredLeft);

    for (auto* t : { &tGraph, &tFader, &tLevel, &tMotion, &tMusic, &tOut })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (t->bounds.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (t->bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (t->title, t->bounds.reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);
    }

    // 状態（グラフのタイトルの右）
    {
        auto info = tGraph.bounds.reduced (12, 8).removeFromTop (22).withTrimmedLeft (130);
        const bool isAuto = param ("auto") > 0.5f, isFollow = param ("follow") > 0.5f;
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

        juce::String mode;
        juce::Colour c = text.withAlpha (0.5f);
        if (! isAuto)                      { mode = "MANUAL  (RIDE fader / automation)"; }
        else if (processor.isWriting())    { mode = "WRITING  to Ride automation"; c = coral; }
        else                               { mode = "AUTO RIDING"; c = sky; }
        if (isAuto && param ("write") > 0.5f && ! processor.isHostPlaying())
            mode << "   (WRITE: waiting for playback)";
        g.setColour (c);
        g.drawText (mode, info, juce::Justification::centredLeft);

        if (isFollow)
        {
            const bool viaLink = processor.engine.isMusicFromLink();
            const bool ok = (processor.sidechainConnected() || viaLink) && musicDb > -60.0f;
            g.setColour (ok ? mint : coral);
            g.drawText (ok ? juce::String (viaLink ? "MUSIC (LINK)  " : "MUSIC  ") + juce::String (musicDb, 1) + " dB  ->  aim "
                                 + juce::String (musicDb + param ("offset"), 1) + " dB"
                           : "MUSIC: no sidechain / Balance Scope signal",
                        info, juce::Justification::centredRight);
        }
    }

    drawGraph (g, graphArea.toFloat());
}

void FaderPilotEditor::drawGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    auto plot = r.reduced (34.0f, 8.0f);

    auto yLevel = [&] (float db) { return juce::jmap (juce::jlimit (levelBottom, levelTop, db), levelBottom, levelTop, plot.getBottom(), plot.getY()); };
    auto yGain  = [&] (float db) { return plot.getCentreY() - juce::jlimit (-gainRange, gainRange, db) / gainRange * plot.getHeight() * 0.5f; };

    // 目盛り：左が音量、右がフェーダー
    g.setFont (juce::FontOptions (10.0f));
    for (float d = levelTop; d >= levelBottom; d -= 12.0f)
    {
        const float y = yLevel (d);
        g.setColour (edge);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (text.withAlpha (0.4f));
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (r.getX(), y - 6, 30, 12), juce::Justification::centredRight);
    }
    for (float d : { -10.0f, 0.0f, 10.0f })
    {
        g.setColour (amber.withAlpha (0.5f));
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), juce::Rectangle<float> (plot.getRight() + 4, yGain (d) - 6, 28, 12),
                    juce::Justification::centredLeft);
    }

    const int n = (int) points.size();
    if (n < 2) return;
    const float dx = plot.getWidth() / (float) (shownPoints - 1);
    const float x0 = plot.getRight() - dx * (float) (n - 1);
    auto xAt = [&] (int i) { return x0 + dx * (float) i; };

    // 小さすぎて動かしていない所（息・無音）を暗くする
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    for (int i = 0; i < n; ++i)
        if (! points[(size_t) i].active)
            g.fillRect (xAt (i) - dx * 0.5f, plot.getY(), dx + 0.5f, plot.getHeight());

    auto line = [&] (auto value, juce::Colour c, float w)
    {
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float y = value (points[(size_t) i]);
            if (i == 0) p.startNewSubPath (xAt (i), y); else p.lineTo (xAt (i), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w));
    };

    // 目標
    line ([&] (const fp::RiderEngine::Point& pt) { return yLevel (pt.targetDb); }, mint.withAlpha (0.7f), 1.0f);
    // 入ってきた声（灰）と、フェーダー後の声（白）
    line ([&] (const fp::RiderEngine::Point& pt) { return yLevel (pt.voiceDb); }, text.withAlpha (0.3f), 1.2f);
    line ([&] (const fp::RiderEngine::Point& pt) { return yLevel (pt.outDb); }, text.withAlpha (0.9f), 1.6f);
    // フェーダーの動き（黄、真ん中が 0 dB）
    g.setColour (amber.withAlpha (0.25f));
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    line ([&] (const fp::RiderEngine::Point& pt) { return yGain (pt.gainDb); }, amber, 2.0f);

    // 凡例
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto legend = plot.withHeight (14.0f).translated (6.0f, 4.0f);
    for (auto [name, c] : { std::pair<const char*, juce::Colour> { "IN", text.withAlpha (0.45f) }, { "OUT", text },
                            { "TARGET", mint }, { "FADER", amber } })
    {
        g.setColour (c);
        g.drawText (name, legend.removeFromLeft (56.0f), juce::Justification::centredLeft);
    }
}

// AUTO のときは、フェーダーの横に「今どこにいるか」の印を出す
void FaderPilotEditor::paintOverChildren (juce::Graphics& g)
{
    const auto track = fader.getBounds().toFloat().withTrimmedBottom (24.0f).reduced (0.0f, 10.0f);
    const float y = track.getBottom() - (liveGain + gainRange) / (2.0f * gainRange) * track.getHeight();
    const float yc = juce::jlimit (track.getY(), track.getBottom(), y);

    if (param ("auto") > 0.5f)
    {
        juce::Path tri;
        tri.addTriangle (track.getX() - 14.0f, yc - 6.0f, track.getX() - 14.0f, yc + 6.0f, track.getX() - 4.0f, yc);
        g.setColour (amber);
        g.fillPath (tri);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ((liveGain > 0.05f ? "+" : "") + juce::String (liveGain, 1) + " dB",
                    faderArea.withHeight (18).translated (0, -2), juce::Justification::centred);
    }
}
