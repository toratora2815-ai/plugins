#include "PluginEditor.h"

namespace col
{
    const juce::Colour bg      (0xff12151a);
    const juce::Colour panel   (0xff1a1f26);
    const juce::Colour edge    (0xff2a313b);
    const juce::Colour text    (0xffdfe6ee);
    const juce::Colour dim     (0xff8793a1);
    const juce::Colour accent  (0xff7fe0c2);   // ミント：カーブ・つまみ
    const juce::Colour cut     (0xffff8a70);   // コーラル：削っている量
    const juce::Colour spec    (0xff5f7891);
    const juce::Colour nodeOff (0xff58626e);
}

// =====================================================================
HushLook::HushLook()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, col::bg);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::bg);
    setColour (juce::ComboBox::outlineColourId, col::edge);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::dim);
    setColour (juce::TextButton::textColourOnId, col::bg);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::edge);
}

void HushLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const float thick = juce::jmax (2.5f, rad * 0.12f);
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, rad - thick, rad - thick, 0.0f, start, end, true);
    g.setColour (col::edge);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // 中央が 0 のつまみ（範囲が負〜正）は真ん中から
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    val.addCentredArc (c.x, c.y, rad - thick, rad - thick, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (s.isEnabled() ? col::accent : col::dim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // 本体
    const float inner = rad - thick * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b333e), c.x, c.y - inner, juce::Colour (0xff171b21), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);
    const float ang = to - juce::MathConstants<float>::halfPi;
    g.setColour (col::text);
    g.drawLine (c.x + std::cos (ang) * inner * 0.35f, c.y + std::sin (ang) * inner * 0.35f,
                c.x + std::cos (ang) * inner * 0.85f, c.y + std::sin (ang) * inner * 0.85f, juce::jmax (1.5f, thick * 0.6f));
}

void HushLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                 juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto bar = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    g.setColour (col::edge);
    g.fillRoundedRectangle (bar, 2.0f);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zx = bipolar ? bar.getCentreX() : bar.getX();
    g.setColour (col::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zx, pos), bar.getY(), std::abs (pos - zx), bar.getHeight()), 2.0f);
    g.setColour (col::text);
    g.fillEllipse (pos - 5.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void HushLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? col::accent : (over ? col::edge.brighter (0.15f) : col::panel.brighter (0.06f)));
    if (down) g.setColour (col::accent.darker (0.3f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::edge);
    if (! on) g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font HushLook::getTextButtonFont (juce::TextButton&, int h) { return juce::FontOptions (juce::jmin (14.0f, h * 0.55f), juce::Font::bold); }
juce::Font HushLook::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.0f); }
juce::Font HushLook::getPopupMenuFont() { return juce::FontOptions (14.0f); }
juce::Label* HushLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.5f));
    l->setColour (juce::Label::backgroundColourId, col::bg);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

// =====================================================================
HushGraph::HushGraph (HushFieldProcessor& p) : proc (p) {}

float HushGraph::hzToX (float hz) const { auto r = plot(); return r.getX() + hf::hzToPos (juce::jlimit (20.0f, 20000.0f, hz)) * r.getWidth(); }
float HushGraph::xToHz (float x) const { auto r = plot(); return hf::posToHz (juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
// カーブ: 0 の線はグラフの上から 42%、±10 で ±32%
float HushGraph::gainToY (float g) const { auto r = plot(); return r.getY() + r.getHeight() * (0.42f - g * 0.032f); }
float HushGraph::yToGain (float y) const { auto r = plot(); return juce::jlimit (-10.0f, 10.0f, (0.42f - (y - r.getY()) / r.getHeight()) / 0.032f); }
float HushGraph::param (int b, const char* what) const { return proc.apvts.getRawParameterValue (HushFieldProcessor::bandId (b, what))->load(); }
void HushGraph::setParam (int b, const char* what, float value)
{
    if (auto* p = proc.apvts.getParameter (HushFieldProcessor::bandId (b, what)))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}
void HushGraph::beginGesture (int b)
{
    for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (HushFieldProcessor::bandId (b, w))) p->beginChangeGesture();
}
void HushGraph::endGesture (int b)
{
    for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (HushFieldProcessor::bandId (b, w))) p->endChangeGesture();
}
bool HushGraph::isGainShape (int shape) const { return shape != hf::LowCut && shape != hf::HighCut && shape != hf::BandReject; }

juce::Point<float> HushGraph::nodePos (int b) const
{
    const int shape = juce::roundToInt (param (b, "shape"));
    return { hzToX (param (b, "freq")), gainToY (isGainShape (shape) ? param (b, "gain") : 0.0f) };
}

int HushGraph::hitNode (juce::Point<float> p) const
{
    int best = -1; float bd = 11.0f;
    for (int b = 0; b < hf::numBands; ++b)
    {
        if (param (b, "used") < 0.5f) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

void HushGraph::tick()
{
    const auto& e = proc.engine();
    for (int i = 0; i < hf::HushEngine::displayPoints; ++i)
    {
        const float r = e.shownReduction (i);
        red[i] = juce::jmax (r, red[i] * 0.86f);       // 落ちるときはゆっくり
        const float s = e.shownSpectrum (i);
        spec[i] += (s - spec[i]) * 0.35f;
    }
    repaint();
}

void HushGraph::paint (juce::Graphics& g)
{
    auto r = plot();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 6.0f);

    // 目盛り
    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f })
    {
        const float x = hzToX (f);
        g.setColour (col::edge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (col::dim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    (int) x + 3, (int) r.getBottom() - 16, 40, 14, juce::Justification::left);
    }
    // 削る量の目盛り（上から下へ）
    for (int i = 1; i <= 3; ++i)
    {
        const float db = rangeDb * i / 4.0f;
        const float y = r.getY() + r.getHeight() * 0.9f * (db / rangeDb);
        g.setColour (col::edge.withAlpha (0.35f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (col::cut.withAlpha (0.6f));
        g.drawText ("-" + juce::String (db, db < 10 ? 1 : 0), (int) r.getRight() - 40, (int) y - 13, 36, 12, juce::Justification::right);
    }

    // スペクトル（高域を +4.5 dB/oct 持ち上げて平らに見せる）
    const int N = hf::HushEngine::displayPoints;
    {
        juce::Path p;
        for (int i = 0; i < N; ++i)
        {
            const float hz = hf::HushEngine::displayHz (i);
            const float v = spec[i] + 4.5f * std::log2 (hz / 1000.0f);
            const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (v + 110.0f) / 100.0f);
            const float x = r.getX() + (i + 0.5f) / N * r.getWidth();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setColour (col::spec.withAlpha (0.18f)); g.fillPath (fill);
        g.setColour (col::spec.withAlpha (0.7f)); g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // 削っている量（上からぶら下がる）
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        for (int i = 0; i < N; ++i)
        {
            const float x = r.getX() + (i + 0.5f) / N * r.getWidth();
            const float y = r.getY() + r.getHeight() * 0.9f * juce::jlimit (0.0f, 1.0f, red[i] / rangeDb);
            p.lineTo (x, y);
        }
        p.lineTo (r.getRight(), r.getY()); p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (col::cut.withAlpha (0.75f), 0, r.getY(), col::cut.withAlpha (0.35f), 0, r.getY() + r.getHeight() * 0.5f, false));
        g.fillPath (p);
    }

    drawCurve (g, r);

    // 点
    for (int b = 0; b < hf::numBands; ++b)
    {
        if (param (b, "used") < 0.5f) continue;
        const auto c = nodePos (b);
        const bool on = param (b, "on") > 0.5f;
        const float rad = (b == hover || b == drag) ? 8.0f : 6.5f;
        g.setColour (on ? col::accent : col::nodeOff);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
        g.setColour (col::bg);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (c.x - rad, c.y - rad, rad * 2, rad * 2), juce::Justification::centred);
    }
    // 説明
    const int show = drag >= 0 ? drag : hover;
    if (show >= 0)
    {
        static const char* names[] = { "Low cut", "Low shelf", "Bell", "Bandpass", "Band reject", "Tilt", "High shelf", "High cut" };
        const int shape = juce::roundToInt (param (show, "shape"));
        const float f = param (show, "freq");
        juce::String t = juce::String (show + 1) + "  " + names[shape] + "  "
                       + (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
        if (isGainShape (shape)) t << "  " << (param (show, "gain") > 0 ? "+" : "") << juce::String (param (show, "gain"), 1);
        if (shape == hf::LowCut || shape == hf::HighCut) t << "  slope " << juce::String (param (show, "slope"), 1);
        else t << "  Q " << juce::String (param (show, "q"), 2);
        if (param (show, "on") < 0.5f) t << "  (off)";
        g.setColour (col::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (t, r.reduced (10.0f, 6.0f).removeFromBottom (36.0f).removeFromTop (16.0f), juce::Justification::left);
    }
    else
    {
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (juce::CharPointer_UTF8 ("ダブルクリックで帯域を追加 / ドラッグで移動 / ホイールで幅 / 右クリックで形・ソロ / ⌥ を押しながらで その帯域だけ聴く"),
                    r.reduced (10.0f, 6.0f).removeFromBottom (36.0f).removeFromTop (16.0f), juce::Justification::left);
    }
}

void HushGraph::drawCurve (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto s = proc.currentSettings();
    juce::Path p;
    const int steps = (int) r.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + (float) i;
        const float hz = xToHz (x);
        float c = 0.0f;
        for (int b = 0; b < hf::numBands; ++b)
            if (s.bands[b].used && s.bands[b].enabled) c += hf::bandResponse (s.bands[b], hz);
        const float y = juce::jmin (r.getBottom(), gainToY (juce::jmax (c, -14.0f)));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    // 0 の線
    g.setColour (col::accent.withAlpha (0.18f));
    g.drawHorizontalLine ((int) gainToY (0.0f), r.getX(), r.getRight());
    g.setColour (col::accent.withAlpha (0.25f));
    g.drawHorizontalLine ((int) gainToY (-10.0f), r.getX(), r.getRight());
    juce::Path fill (p);
    fill.lineTo (r.getRight(), gainToY (-10.0f)); fill.lineTo (r.getX(), gainToY (-10.0f)); fill.closeSubPath();
    g.setColour (col::accent.withAlpha (0.07f)); g.fillPath (fill);
    g.setColour (col::accent.withAlpha (0.9f));
    g.strokePath (p, juce::PathStrokeType (2.0f));
}

void HushGraph::mouseMove (const juce::MouseEvent& e) { const int h = hitNode (e.position); if (h != hover) { hover = h; repaint(); } }
void HushGraph::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

void HushGraph::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (e.mods.isPopupMenu()) { if (h >= 0) showMenu (h); return; }
    drag = h;
    if (drag >= 0)
    {
        beginGesture (drag);
        if (e.mods.isAltDown())
        {
            soloHeld = true;
            if (auto* p = proc.apvts.getParameter ("bandSolo")) p->setValueNotifyingHost (p->convertTo0to1 ((float) (drag + 1)));
        }
    }
}

void HushGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    setParam (drag, "freq", xToHz (e.position.x));
    const int shape = juce::roundToInt (param (drag, "shape"));
    if (isGainShape (shape)) setParam (drag, "gain", yToGain (e.position.y));
}

void HushGraph::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) endGesture (drag);
    if (soloHeld)
    {
        soloHeld = false;
        if (auto* p = proc.apvts.getParameter ("bandSolo")) p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    }
    drag = -1;
}

void HushGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 0) { setParam (h, "on", param (h, "on") > 0.5f ? 0.0f : 1.0f); return; }   // 点のダブルクリック = ON/OFF
    for (int b = 0; b < hf::numBands; ++b)
        if (param (b, "used") < 0.5f)
        {
            setParam (b, "shape", (float) hf::Bell);
            setParam (b, "freq", xToHz (e.position.x));
            setParam (b, "gain", std::round (yToGain (e.position.y) * 2.0f) / 2.0f);
            setParam (b, "q", 0.7f); setParam (b, "slope", 5.0f); setParam (b, "focus", 50.0f);
            setParam (b, "on", 1.0f);
            setParam (b, "used", 1.0f);
            return;
        }
}

void HushGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int h = hitNode (e.position);
    if (h < 0) return;
    const int shape = juce::roundToInt (param (h, "shape"));
    const float d = w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    if (shape == hf::LowCut || shape == hf::HighCut) setParam (h, "slope", juce::jlimit (0.0f, 10.0f, param (h, "slope") + d * 3.0f));
    else setParam (h, "q", juce::jlimit (0.1f, 10.0f, param (h, "q") * std::exp (d * 0.8f)));
}

void HushGraph::showMenu (int b)
{
    juce::PopupMenu m, shapes, focusMenu;
    static const char* names[] = { "Low cut", "Low shelf", "Bell", "Bandpass", "Band reject", "Tilt", "High shelf", "High cut" };
    const int shape = juce::roundToInt (param (b, "shape"));
    for (int i = 0; i < hf::numShapes; ++i) shapes.addItem (100 + i, names[i], true, i == shape);
    const float fo = param (b, "focus");
    focusMenu.addItem (200, "L / Mid only", true, fo < 1.0f);
    focusMenu.addItem (201, "even", true, std::abs (fo - 50.0f) < 1.0f);
    focusMenu.addItem (202, "R / Side only", true, fo > 99.0f);
    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    m.addItem (1, "Enabled", true, param (b, "on") > 0.5f);
    m.addItem (2, "Solo (listen to this band)", true, juce::roundToInt (proc.apvts.getRawParameterValue ("bandSolo")->load()) == b + 1);
    m.addSubMenu ("Stereo focus", focusMenu);
    m.addSeparator();
    m.addItem (3, "Remove band");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, b] (int r)
    {
        if (r >= 100 && r < 100 + hf::numShapes) setParam (b, "shape", (float) (r - 100));
        else if (r == 1) setParam (b, "on", param (b, "on") > 0.5f ? 0.0f : 1.0f);
        else if (r == 2)
        {
            auto* p = proc.apvts.getParameter ("bandSolo");
            const bool already = juce::roundToInt (proc.apvts.getRawParameterValue ("bandSolo")->load()) == b + 1;
            p->setValueNotifyingHost (p->convertTo0to1 (already ? 0.0f : (float) (b + 1)));
        }
        else if (r >= 200 && r <= 202) setParam (b, "focus", r == 200 ? 0.0f : (r == 201 ? 50.0f : 100.0f));
        else if (r == 3) setParam (b, "used", 0.0f);
        repaint();
    });
}

// =====================================================================
HushFieldEditor::HushFieldEditor (HushFieldProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetBar (p.myPresets, col::accent, col::panel, col::text),
      linkBar (p.apvts, col::accent, col::panel, col::text)
{
    setLookAndFeel (&look);
    addAndMakeVisible (graph);

    setupKnob (depth, "depth", "DEPTH");
    setupKnob (detail, "detail", "DETAIL");
    setupKnob (attack, "attack", "ATTACK");
    setupKnob (release, "release", "RELEASE");
    setupKnob (mix, "mix", "MIX", true);
    setupKnob (outTrim, "outTrim", "OUT", true);
    setupKnob (wetTrim, "wetTrim", "WET", true);
    setupKnob (maxCut, "maxCut", "MAX CUT");
    setupKnob (link, "stereoLink", "LINK");
    setupKnob (focus, "stereoFocus", "FOCUS");
    const char* tiltIds[6] = { "detailTiltLow", "detailTiltHigh", "attackTiltLow", "attackTiltHigh", "releaseTiltLow", "releaseTiltHigh" };
    const char* tiltNames[6] = { "DET LO", "DET HI", "ATK LO", "ATK HI", "REL LO", "REL HI" };
    for (int i = 0; i < 6; ++i) setupKnob (tilt[i], tiltIds[i], tiltNames[i]);

    depth.slider.setTooltip (juce::CharPointer_UTF8 ("どれだけ深く削るか（5 から上で急に効く）"));
    detail.slider.setTooltip (juce::CharPointer_UTF8 ("どれだけ細い山を「山」とみなすか。上げるほど細い響きだけを深く削る"));
    attack.slider.setTooltip (juce::CharPointer_UTF8 ("削り始める速さ（0 でほぼ即、10 で約 1 秒）"));
    release.slider.setTooltip (juce::CharPointer_UTF8 ("元に戻る速さ（0 で約 0.1 秒、10 で約 1 秒）"));
    maxCut.slider.setTooltip (juce::CharPointer_UTF8 ("これより深くは削らない（いちばん右で off）"));

    setupToggle (bypass, "bypass", "BYPASS");
    setupToggle (delta, "delta", "DELTA");
    setupToggle (sidechain, "sidechain", "SC");
    setupToggle (scListen, "scListen", "SC LISTEN");
    setupToggle (lowLatency, "lowLatency", "LOW LATENCY");
    setupToggle (linearPhase, "linearPhase", "LINEAR PHASE");
    delta.button.setTooltip (juce::CharPointer_UTF8 ("削った音だけを聴く"));
    sidechain.button.setTooltip (juce::CharPointer_UTF8 ("どこを削るかを、サイドチェーンの音で決める"));

    for (auto* b : { &soft, &hard, &lr, &ms, &aBtn, &bBtn, &copyBtn }) { addAndMakeVisible (b); b->setClickingTogglesState (false); }
    soft.onClick = [this] { setChoice ("mode", 0); };
    hard.onClick = [this] { setChoice ("mode", 1); };
    lr.onClick = [this] { setChoice ("stereoMode", 0); };
    ms.onClick = [this] { setChoice ("stereoMode", 1); };
    aBtn.onClick = [this] { processor.switchAB (false); };
    bBtn.onClick = [this] { processor.switchAB (true); };
    copyBtn.onClick = [this] { processor.copyAtoB(); };
    copyBtn.setTooltip (juce::CharPointer_UTF8 ("今の設定を、もう片方（A/B）へ写す"));

    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("Presets");
    const auto names = HushFieldProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i) presetBox.addItem (names[i], i + 1);
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0) processor.applyPreset (id - 1); };

    addAndMakeVisible (qualityBox);
    qualityBox.addItemList ({ "normal", "high", "ultra" }, 1);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "quality", qualityBox);
    qualityBox.setTooltip (juce::CharPointer_UTF8 ("解析の細かさ（high / ultra は CPU を多く使う）"));

    addAndMakeVisible (rangeBox);
    rangeBox.addItemList ({ "6 dB", "12 dB", "24 dB", "48 dB" }, 1);
    rangeBox.setSelectedId (2, juce::dontSendNotification);
    rangeBox.onChange = [this] { const float r[] = { 6.0f, 12.0f, 24.0f, 48.0f }; graph.rangeDb = r[juce::jlimit (0, 3, rangeBox.getSelectedId() - 1)]; };
    rangeBox.setTooltip (juce::CharPointer_UTF8 ("削る量の表示の深さ"));

    addAndMakeVisible (status);
    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, col::cut);
    status.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (linkBar);
    setupToggle (linkFollow, "linkFollow", "FOLLOW");
    linkFollow.button.setTooltip (juce::CharPointer_UTF8 ("Balance Scope が見つけた「出すぎの帯域」を、自動で強めに処理する"));
    linkBar.addAndMakeVisible (linkFollow.button);
    linkBar.extra = &linkFollow.button;
    linkBar.extraWidth = 80;
    linkBar.setSources (processor.link, { plink::kindBalanceScope });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "Plugin Link OFF  -  same LINK number as Balance Scope to connect";
        juce::StringArray parts;
        for (int i = 0; i < 2; ++i)
            if (processor.linkBoostNow (i) > 0.05f)
                parts.add ("+" + juce::String (processor.linkBoostNow (i), 1) + " at " + plink::hzText (processor.linkHzNow (i)));
        lit = ! parts.isEmpty();
        plink::Peer peer;
        if (! processor.link.ep().find (plink::kindBalanceScope, peer)) return "waiting for Balance Scope";
        return lit ? "Balance Scope: excess -> " + parts.joinIntoString (", ") : juce::String ("Balance Scope: nothing sticking out");
    };

    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); },
                             [this] { return processor.gainMatch.available(); }, "GAIN MATCH: Hush Field does not change the loudness (measured after you turn it on or move a knob, locked after 10 s of playing). Paused while DELTA / SC LISTEN / SOLO / BYPASS");
    setResizable (true, true);
    setResizeLimits (900, 640, 1800, 1240);
    setSize (1040, 700);
    startTimerHz (30);
}

HushFieldEditor::~HushFieldEditor() { setLookAndFeel (nullptr); }

void HushFieldEditor::setupKnob (Knob& k, const char* id, const juce::String& name, bool linear)
{
    k.slider.setSliderStyle (linear ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (linear ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow, false, linear ? 64 : 64, 18);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, col::dim);
    k.label.setJustificationType (linear ? juce::Justification::centredLeft : juce::Justification::centred);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
}

void HushFieldEditor::setupToggle (Toggle& t, const char* id, const juce::String& text)
{
    t.button.setButtonText (text);
    t.button.setClickingTogglesState (true);
    addAndMakeVisible (t.button);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

void HushFieldEditor::setChoice (const char* id, int index)
{
    if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
}

void HushFieldEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (16));
    k.slider.setBounds (r);
}

void HushFieldEditor::timerCallback()
{
    graph.tick();
    soft.setToggleState (param ("mode") < 0.5f, juce::dontSendNotification);
    hard.setToggleState (param ("mode") > 0.5f, juce::dontSendNotification);
    lr.setToggleState (param ("stereoMode") < 0.5f, juce::dontSendNotification);
    ms.setToggleState (param ("stereoMode") > 0.5f, juce::dontSendNotification);
    aBtn.setToggleState (! processor.isOnB(), juce::dontSendNotification);
    bBtn.setToggleState (processor.isOnB(), juce::dontSendNotification);
    const bool ll = param ("lowLatency") > 0.5f;
    linearPhase.button.setEnabled (! ll);
    qualityBox.setEnabled (! ll);
    juce::String st;
    if (param ("sidechain") > 0.5f && ! processor.sidechainConnected())
        st = juce::CharPointer_UTF8 ("SC: サイドチェーンが来ていません（Logic のプラグイン右上で選ぶ）");
    else if (juce::roundToInt (param ("bandSolo")) > 0)
        st = "SOLO band " + juce::String (juce::roundToInt (param ("bandSolo")));
    else if (param ("delta") > 0.5f) st = juce::CharPointer_UTF8 ("DELTA: 削った音だけ");
    status.setText (st, juce::dontSendNotification);
}

void HushFieldEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        if (title.isNotEmpty())
        {
            g.setColour (col::dim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 6).removeFromTop (14), juce::Justification::left);
        }
    };
    drawPanel (leftPanel, {});
    drawPanel (stereoPanel, "STEREO");
    drawPanel (tiltPanel, "TILT  (low / high)");
    drawPanel (outPanel, "OUTPUT");

    g.setColour (col::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("hush field", topBar.withWidth (200).withTrimmedLeft (14), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v" JucePlugin_VersionString, topBar.withWidth (200).withTrimmedLeft (138), juce::Justification::centredLeft);
}

void HushFieldEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    {
        // 2 段: 下 = GAIN MATCH・プリセット欄、その上 = LINK 欄
        presetBar.setBounds (area.removeFromBottom (myp::PresetBar::height).removeFromRight (presetBar.preferredWidth()));
        area.removeFromBottom (4);
        linkBar.setBounds (area.removeFromBottom (plink::LinkBar::height));
        area.removeFromBottom (8);
    }
    topBar = area.removeFromTop (38);
    {
        auto t = topBar.withTrimmedLeft (200);
        presetBox.setBounds (t.removeFromLeft (300).reduced (0, 6));
        t.removeFromLeft (12);
        aBtn.setBounds (t.removeFromLeft (34).reduced (0, 6)); t.removeFromLeft (4);
        bBtn.setBounds (t.removeFromLeft (34).reduced (0, 6)); t.removeFromLeft (4);
        copyBtn.setBounds (t.removeFromLeft (46).reduced (0, 6));
        t.removeFromLeft (16);
        sidechain.button.setBounds (t.removeFromLeft (44).reduced (0, 6)); t.removeFromLeft (4);
        scListen.button.setBounds (t.removeFromLeft (84).reduced (0, 6));
        status.setBounds (t.reduced (6, 6));
    }
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (150);
    leftPanel = area.removeFromLeft (200);
    area.removeFromLeft (8);
    auto graphArea = area;
    graph.setBounds (graphArea);
    rangeBox.setBounds (graphArea.getRight() - 84, graphArea.getY() + 6, 76, 22);

    {
        auto l = leftPanel.reduced (12);
        auto modeRow = l.removeFromTop (28);
        soft.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2).reduced (2, 0));
        hard.setBounds (modeRow.reduced (2, 0));
        l.removeFromTop (8);
        const int bigH = juce::jmin (180, l.getHeight() / 2);
        placeKnob (depth, l.removeFromTop (bigH));
        l.removeFromTop (6);
        const int midH = (l.getHeight() - 6) / 2;
        placeKnob (detail, l.removeFromTop (midH).withSizeKeepingCentre (110, midH));
        l.removeFromTop (6);
        auto row = l;
        placeKnob (attack, row.removeFromLeft (row.getWidth() / 2));
        placeKnob (release, row);
    }

    bottom.removeFromTop (8);
    stereoPanel = bottom.removeFromLeft (290);
    bottom.removeFromLeft (8);
    outPanel = bottom.removeFromRight (360);
    bottom.removeFromRight (8);
    tiltPanel = bottom;
    {
        auto s = stereoPanel.reduced (10).withTrimmedTop (18);
        auto btns = s.removeFromLeft (56);
        lr.setBounds (btns.removeFromTop (30).reduced (0, 2));
        ms.setBounds (btns.removeFromTop (30).reduced (0, 2));
        s.removeFromLeft (6);
        const int w = s.getWidth() / 3;
        placeKnob (link, s.removeFromLeft (w));
        placeKnob (focus, s.removeFromLeft (w));
        placeKnob (maxCut, s);
    }
    {
        auto t = tiltPanel.reduced (10).withTrimmedTop (18);
        const int w = t.getWidth() / 6;
        for (int i = 0; i < 6; ++i) placeKnob (tilt[i], i == 5 ? t : t.removeFromLeft (w));
    }
    {
        auto o = outPanel.reduced (10).withTrimmedTop (18);
        // 左: MIX / WET / OUT、右: ボタン 2 列
        auto right = o.removeFromRight (150);
        o.removeFromRight (8);
        auto row = [&] (Knob& k) { auto r = o.removeFromTop (juce::jmax (22, o.getHeight() / 3)); k.label.setBounds (r.removeFromLeft (40)); k.slider.setBounds (r); };
        row (mix); row (wetTrim); row (outTrim);
        const int bh = (right.getHeight() - 9) / 4;
        auto two = right.removeFromTop (bh);
        bypass.button.setBounds (two.removeFromLeft (two.getWidth() / 2).reduced (2, 0));
        delta.button.setBounds (two.reduced (2, 0));
        right.removeFromTop (3);
        qualityBox.setBounds (right.removeFromTop (bh).reduced (2, 0));
        right.removeFromTop (3);
        lowLatency.button.setBounds (right.removeFromTop (bh).reduced (2, 0));
        right.removeFromTop (3);
        linearPhase.button.setBounds (right.removeFromTop (bh).reduced (2, 0));
    }
}
