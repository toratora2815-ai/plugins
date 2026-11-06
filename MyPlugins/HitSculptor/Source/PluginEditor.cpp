#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff121214), panel (0xff1c1c20), edge (0xff2e2e35),
                       accent (0xffff8a3d), teal (0xff5fd4c4), text (0xffeeeef2);
}

//==============================================================================
void HitLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                float start, float end, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.13f);
    const float arcR = radius - lineW * 0.5f;
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (edge);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    if (std::abs (to - from) > 0.001f)
    {
        juce::Path v;
        v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (fill);
        g.strokePath (v, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // 本体と指し針
    const float bodyR = arcR - lineW * 1.4f;
    g.setColour (panel.brighter (0.15f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    juce::Path needle;
    needle.addRoundedRectangle (-1.5f, -bodyR, 3.0f, bodyR * 0.55f, 1.5f);
    g.setColour (text);
    g.fillPath (needle, juce::AffineTransform::rotation (to).translated (c.x, c.y));
    if (bipolar)
    {
        g.setColour (text.withAlpha (0.35f));
        const float a = (start + end) * 0.5f;
        g.fillEllipse (c.x + (arcR + lineW) * std::sin (a) - 1.5f, c.y - (arcR + lineW) * std::cos (a) - 1.5f, 3.0f, 3.0f);
    }
}

//==============================================================================
void HitDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 8.0f);
    auto plot = r.reduced (36.0f, 14.0f).withTrimmedBottom (8.0f);
    plot.removeFromLeft (-8.0f);

    constexpr float topDb = 6.0f, botDb = -60.0f;
    auto yOf = [&] (float a)
    {
        const float d = juce::jlimit (botDb, topDb, hs::ampDb (a));
        return juce::jmap (d, botDb, topDb, plot.getBottom(), plot.getY());
    };
    // 目盛り
    g.setFont (juce::FontOptions (10.0f));
    for (float d : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = juce::jmap (d, botDb, topDb, plot.getBottom(), plot.getY());
        g.setColour (edge);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (text.withAlpha (0.35f));
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (r.getX() + 4, y - 6, 26, 12), juce::Justification::centredRight);
    }
    for (int ms = 100; ms < 1000; ms += 100)
    {
        const float x = plot.getX() + plot.getWidth() * (float) ms / 1000.0f;
        g.setColour (edge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        if (ms % 200 == 0)
        {
            g.setColour (text.withAlpha (0.3f));
            g.drawText (juce::String (ms) + " ms", juce::Rectangle<float> (x - 30, plot.getBottom() + 1, 60, 12), juce::Justification::centred);
        }
    }

    const int n = juce::jmin (engine.capPos.load (std::memory_order_acquire), hs::Engine::dispLen);
    if (n <= 1)
    {
        g.setColour (text.withAlpha (0.4f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Play a kick or snare - each hit is drawn here (grey = original, orange = result)", plot, juce::Justification::centred);
        return;
    }
    auto pathOf = [&] (std::array<std::atomic<float>, hs::Engine::dispLen>& a)
    {
        juce::Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < n; ++i)
            p.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) hs::Engine::dispLen, yOf (a[(size_t) i].load (std::memory_order_relaxed)));
        p.lineTo (plot.getX() + plot.getWidth() * (float) (n - 1) / (float) hs::Engine::dispLen, plot.getBottom());
        p.closeSubPath();
        return p;
    };
    const auto pin = pathOf (engine.dispIn), pout = pathOf (engine.dispOut);
    g.setColour (text.withAlpha (0.13f));
    g.fillPath (pin);
    g.setColour (text.withAlpha (0.35f));
    g.strokePath (pin, juce::PathStrokeType (1.0f));
    g.setColour (accent.withAlpha (0.28f));
    g.fillPath (pout);
    g.setColour (accent);
    g.strokePath (pout, juce::PathStrokeType (1.6f));

    // 今のゲイン
    const float gdb = engine.lastGainDb.load();
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (gdb > 0.5f ? accent : (gdb < -0.5f ? teal : text.withAlpha (0.5f)));
    g.drawText ("GAIN " + juce::String (gdb > 0 ? "+" : "") + juce::String (juce::jmax (-60.0f, gdb), 1) + " dB",
                plot.removeFromTop (14).removeFromRight (120), juce::Justification::centredRight);
}

//==============================================================================
HitSculptorEditor::HitSculptorEditor (HitSculptorProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (display);

    setupKnob (attack, "attack", "ATTACK", "SOFT  |  HARD", true, 0.0);
    setupKnob (snap,   "snap",   "SNAP",   "how long the hit is", false, 15.0);
    setupKnob (length, "length", "LENGTH", "SHORT  |  LONG", true, 0.0);
    setupKnob (room,   "room",   "ROOM",   "DRY  |  WET", true, 0.0);
    setupKnob (size,   "size",   "SIZE",   "added room size", false, 40.0);
    setupKnob (tone,   "tone",   "TONE",   "dark  |  bright", false, 50.0);
    setupKnob (drive,  "drive",  "DRIVE",  "soft clip", false, 0.0);
    setupKnob (mix,    "mix",    "MIX",    "parallel", false, 100.0);
    setupKnob (output, "output", "OUTPUT", "level", false, 0.0);
    for (auto* k : { &room, &size, &tone }) k->slider.setColour (juce::Slider::rotarySliderFillColourId, teal);

    presetBox.setTextWhenNothingSelected ("PRESET");
    const auto& ps = HitSculptorProcessor::presets();
    for (int i = 0; i < (int) ps.size(); ++i) presetBox.addItem (ps[(size_t) i].name, i + 1);
    presetBox.setColour (juce::ComboBox::backgroundColourId, edge);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, accent);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) processor.applyPreset (id - 1);
    };
    addAndMakeVisible (presetBox);
    refreshPresetName();

    setSize (760, 500);
    startTimerHz (30);
}

HitSculptorEditor::~HitSculptorEditor() { setLookAndFeel (nullptr); }

void HitSculptorEditor::setupKnob (Knob& k, const char* id, const char* name, const char* hint, bool bipolar, double def)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, text);
    s.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (s);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, s);
    s.setDoubleClickReturnValue (true, def);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
    k.hint.setText (hint, juce::dontSendNotification);
    k.hint.setJustificationType (juce::Justification::centred);
    k.hint.setFont (juce::FontOptions (10.0f));
    k.hint.setColour (juce::Label::textColourId, text.withAlpha (0.4f));
    addAndMakeVisible (k.hint);
}

void HitSculptorEditor::refreshPresetName()
{
    const auto name = processor.apvts.state.getProperty ("preset", "").toString();
    const auto& ps = HitSculptorProcessor::presets();
    for (int i = 0; i < (int) ps.size(); ++i)
        if (name == ps[(size_t) i].name) { presetBox.setSelectedId (i + 1, juce::dontSendNotification); return; }
}

void HitSculptorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    presetBox.setBounds (top.removeFromRight (190).reduced (0, 3));
    b.removeFromTop (8);
    display.setBounds (b.removeFromTop (190));
    b.removeFromTop (14);

    // 3 つのまとまり: SHAPE（3）/ ROOM（3）/ OUT（3）
    const int gap = 12;
    const int gw = (b.getWidth() - gap * 2) / 3;
    groupShape = b.removeFromLeft (gw); b.removeFromLeft (gap);
    groupRoom = b.removeFromLeft (gw);  b.removeFromLeft (gap);
    groupOut = b;

    auto place = [] (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
    {
        area.removeFromTop (22);   // まとまりの名前
        area = area.reduced (4, 4);
        const int w = area.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto col = area.removeFromLeft (w);
            k->label.setBounds (col.removeFromTop (18));
            k->hint.setBounds (col.removeFromBottom (14));
            k->slider.setBounds (col.reduced (2, 0));
        }
    };
    place (groupShape, { &attack, &snap, &length });
    place (groupRoom, { &room, &size, &tone });
    place (groupOut, { &drive, &mix, &output });
}

void HitSculptorEditor::timerCallback()
{
    display.repaint();
}

void HitSculptorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("HIT SCULPTOR", 18, 16, 220, 30, juce::Justification::centredLeft);
    g.setColour (accent.withAlpha (0.85f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("shape every drum hit: attack, length, room", 196, 16, 320, 30, juce::Justification::centredLeft);

    auto group = [&] (juce::Rectangle<int> r, const char* name, juce::Colour c)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (c);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, r.removeFromTop (22).reduced (10, 0), juce::Justification::centredLeft);
    };
    group (groupShape, "SHAPE", accent);
    group (groupRoom, "ROOM", teal);
    group (groupOut, "OUT", text.withAlpha (0.6f));
}
