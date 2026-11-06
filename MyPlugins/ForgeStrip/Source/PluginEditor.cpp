#include "PluginEditor.h"

namespace col
{
    const juce::Colour bg      (0xff141417);
    const juce::Colour panel   (0xff1d1d22);
    const juce::Colour edge    (0xff2f2f37);
    const juce::Colour text    (0xffe8e4dc);
    const juce::Colour dim     (0xff8e8a83);
    const juce::Colour accent  (0xffffb454);   // 琥珀
    const juce::Colour accent2 (0xff6fd3c8);   // 青緑（量・メーター）
    const juce::Colour off     (0xff4a4a52);
}

// =====================================================================
ForgeLook::ForgeLook()
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

void ForgeLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const float thick = juce::jmax (2.5f, rad * 0.13f);
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, rad - thick, rad - thick, 0.0f, start, end, true);
    g.setColour (col::edge);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    val.addCentredArc (c.x, c.y, rad - thick, rad - thick, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (s.isEnabled() ? col::accent : col::dim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float inner = rad - thick * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34343c), c.x, c.y - inner, juce::Colour (0xff1a1a1f), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);
    const float ang = to - juce::MathConstants<float>::halfPi;
    g.setColour (col::text);
    g.drawLine (c.x + std::cos (ang) * inner * 0.3f, c.y + std::sin (ang) * inner * 0.3f,
                c.x + std::cos (ang) * inner * 0.85f, c.y + std::sin (ang) * inner * 0.85f, juce::jmax (1.5f, thick * 0.55f));
}

void ForgeLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? col::accent : (over ? col::edge.brighter (0.15f) : col::panel.brighter (0.05f)));
    if (down) g.setColour (col::accent.darker (0.3f));
    g.fillRoundedRectangle (r, 4.0f);
    if (! on) { g.setColour (col::edge); g.drawRoundedRectangle (r, 4.0f, 1.0f); }
}
juce::Font ForgeLook::getTextButtonFont (juce::TextButton&, int h) { return juce::FontOptions (juce::jmin (13.0f, h * 0.55f), juce::Font::bold); }
juce::Font ForgeLook::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.0f); }
juce::Font ForgeLook::getPopupMenuFont() { return juce::FontOptions (14.0f); }
juce::Label* ForgeLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.0f));
    l->setColour (juce::Label::backgroundColourId, col::bg);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

// =====================================================================
ChainBar::ChainBar (ForgeStripProcessor& p) : proc (p) { startTimerHz (10); }

juce::Rectangle<float> ChainBar::chip (int slot) const
{
    const float w = (float) getWidth() / (float) (fs::numModules + 1);
    return { slot * w + 2.0f, 2.0f, w - 4.0f, (float) getHeight() - 4.0f };
}

int ChainBar::slotAt (juce::Point<float> pt) const
{
    for (int i = 0; i <= fs::numModules; ++i) if (chip (i).contains (pt)) return i;
    return -1;
}

void ChainBar::paint (juce::Graphics& g)
{
    int o[fs::numModules]; proc.getOrder (o);
    for (int i = 0; i <= fs::numModules; ++i)
    {
        auto r = chip (i);
        const bool master = i == fs::numModules;
        const int m = master ? -1 : o[i];
        const bool on = master ? true : proc.apvts.getRawParameterValue (fsid::J (fsid::id (m, "on")))->load() > 0.5f;
        const bool sel = (master && selected == -1) || (! master && selected == m);
        g.setColour (sel ? col::panel.brighter (0.12f) : col::panel);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (sel ? col::accent : col::edge);
        g.drawRoundedRectangle (r, 5.0f, sel ? 1.6f : 1.0f);
        if (dragging && i == dragOver) { g.setColour (col::accent2); g.drawRoundedRectangle (r.reduced (2), 4.0f, 2.0f); }
        if (! master)
        {
            // 電源の丸
            auto led = juce::Rectangle<float> (r.getX() + 7, r.getCentreY() - 5, 10, 10);
            g.setColour (on ? col::accent : col::off);
            g.fillEllipse (led);
            g.setColour (on ? col::text : col::dim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawFittedText (fs::moduleName (m), r.withTrimmedLeft (20).toNearestInt(), juce::Justification::centred, 1);
            // つながりの矢印
            g.setColour (col::dim.withAlpha (0.5f));
            g.drawText (">", juce::Rectangle<float> (r.getRight() - 2, r.getY(), 6, r.getHeight()).toNearestInt(), juce::Justification::centred);
        }
        else
        {
            g.setColour (col::text);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawFittedText ("MASTER", r.toNearestInt(), juce::Justification::centred, 1);
        }
    }
}

void ChainBar::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position; dragging = false;
    dragFrom = slotAt (e.position);
}

void ChainBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0 || dragFrom == fs::numModules) return;
    if (e.position.getDistanceFrom (downPos) > 6.0f) dragging = true;
    if (dragging) { dragOver = juce::jmin (slotAt (e.position), fs::numModules - 1); repaint(); }
}

void ChainBar::mouseUp (const juce::MouseEvent& e)
{
    int o[fs::numModules]; proc.getOrder (o);
    if (dragging)
    {
        if (dragOver >= 0 && dragOver != dragFrom)
        {
            const int m = o[dragFrom];
            std::vector<int> v (o, o + fs::numModules);
            v.erase (v.begin() + dragFrom);
            v.insert (v.begin() + dragOver, m);
            for (int i = 0; i < fs::numModules; ++i) o[i] = v[(size_t) i];
            proc.setOrder (o);
        }
        dragging = false; dragOver = -1; repaint();
        return;
    }
    const int s = slotAt (e.position);
    if (s < 0) return;
    if (s == fs::numModules) { selected = -1; if (onSelect) onSelect (-1); repaint(); return; }
    const int m = o[s];
    auto r = chip (s);
    if (e.position.x < r.getX() + 22)
    {
        if (auto* p = proc.apvts.getParameter (fsid::J (fsid::id (m, "on"))))
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
        }
    }
    selected = m;
    if (onSelect) onSelect (m);
    repaint();
}

// =====================================================================
EqGraph::EqGraph (ForgeStripProcessor& p) : proc (p) { startTimerHz (25); }

float EqGraph::hzToX (float hz) const { return (float) getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float EqGraph::xToHz (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
float EqGraph::dbToY (float d) const { return (float) getHeight() * (0.5f - d / 40.0f); }
float EqGraph::yToDb (float y) const { return juce::jlimit (-30.0f, 15.0f, (0.5f - y / (float) getHeight()) * 40.0f); }
float EqGraph::val (int b, const char* w) const { return proc.apvts.getRawParameterValue (fsid::J (fsid::eqId (b, w)))->load(); }
void EqGraph::setVal (int b, const char* w, float v)
{
    if (auto* p = proc.apvts.getParameter (fsid::J (fsid::eqId (b, w)))) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
juce::Point<float> EqGraph::node (int b) const
{
    const int sh = (int) val (b, "shape");
    return { hzToX (val (b, "freq")), fs::eqShapeHasGain (sh) ? dbToY (val (b, "gain")) : dbToY (0.0f) };
}

void EqGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg); g.fillRoundedRectangle (r, 6.0f);
    g.setFont (juce::FontOptions (10.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = hzToX (f);
        g.setColour (col::edge); g.drawVerticalLine ((int) x, 0, r.getBottom());
        g.setColour (col::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x + 2, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);
    }
    for (float d : { -24.0f, -12.0f, 0.0f, 12.0f })
    {
        const float y = dbToY (d);
        g.setColour (d == 0 ? col::edge.brighter (0.3f) : col::edge); g.drawHorizontalLine ((int) y, 0, r.getRight());
        g.setColour (col::dim); g.drawText (juce::String ((int) d), 3, (int) y - 12, 30, 12, juce::Justification::left);
    }
    // 合計のカーブ（設定から計算）
    const auto st = proc.currentSettings();
    fs::EqModule tmp; tmp.prepare (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0); tmp.set (st.eq);
    juce::Path p;
    for (int x = 0; x <= getWidth(); x += 2)
    {
        const float y = dbToY (tmp.responseDb (xToHz ((float) x)));
        if (x == 0) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
    }
    const bool on = st.on[fs::ModEq];
    g.setColour ((on ? col::accent : col::dim).withAlpha (0.9f));
    g.strokePath (p, juce::PathStrokeType (2.0f));
    // 点
    for (int b = 0; b < fs::EqSettings::numBands; ++b)
    {
        const auto pt = node (b);
        const bool bOn = val (b, "on") > 0.5f;
        const float rad = b == selected ? 8.0f : 6.0f;
        g.setColour (bOn ? (b == selected ? col::accent : col::accent2) : col::off);
        g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
        g.setColour (col::bg);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2).toNearestInt(), juce::Justification::centred);
        if (bOn && st.eq.band[b].dynamic)
        {
            const float cur = proc.strip().eqModule().currentGain (b);
            g.setColour (col::accent2);
            g.drawLine (pt.x, pt.y, pt.x, dbToY (cur), 2.0f);
        }
    }
    g.setColour (col::edge); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    drag = -1; float best = 14.0f;
    for (int b = 0; b < fs::EqSettings::numBands; ++b)
    {
        const float d = node (b).getDistanceFrom (e.position);
        if (d < best) { best = d; drag = b; }
    }
    if (drag >= 0)
    {
        selected = drag; if (onSelect) onSelect (drag);
        for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (fsid::J (fsid::eqId (drag, w)))) p->beginChangeGesture();
        if (val (drag, "on") < 0.5f) setVal (drag, "on", 1.0f);
    }
}
void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    setVal (drag, "freq", xToHz (e.position.x));
    if (fs::eqShapeHasGain ((int) val (drag, "shape"))) setVal (drag, "gain", yToDb (e.position.y));
}
void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (fsid::J (fsid::eqId (drag, w)))) p->endChangeGesture();
    drag = -1;
}
void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int b = 0; b < fs::EqSettings::numBands; ++b)
        if (node (b).getDistanceFrom (e.position) < 10.0f) { setVal (b, "on", val (b, "on") > 0.5f ? 0.0f : 1.0f); return; }
}
void EqGraph::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float q = val (selected, "q");
    setVal (selected, "q", juce::jlimit (0.1f, 40.0f, q * std::pow (1.15f, w.deltaY * 4.0f)));
}

// =====================================================================
ModulePage::ModulePage (ForgeStripProcessor& p, int module) : proc (p), mod (module) { build(); startTimerHz (15); }
ModulePage::~ModulePage() { stopTimer(); }

void ModulePage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (r, 8.0f);
    if ((mod == fs::ModSculptor || mod == fs::ModUnmask) && ! bandCols[0].isEmpty())
    {
        // 今かけている量（30 帯域）
        auto a = bandCols[0].toFloat();
        g.setColour (col::bg); g.fillRoundedRectangle (a, 6.0f);
        const auto& gains = mod == fs::ModSculptor ? proc.strip().sculptorModule().currentGainsDb() : proc.strip().unmaskModule().currentGainsDb();
        const float range = mod == fs::ModSculptor ? 24.0f : 18.0f;
        const float zeroY = mod == fs::ModSculptor ? a.getCentreY() : a.getY() + 14.0f;
        g.setColour (col::edge); g.drawHorizontalLine ((int) zeroY, a.getX(), a.getRight());
        const int nb = (int) gains.size();
        const float bw = a.getWidth() / (float) juce::jmax (1, nb);
        for (int k = 0; k < nb; ++k)
        {
            const float v = juce::jlimit (-range, range, gains[(size_t) k]);
            const float h = v / range * (a.getHeight() * (mod == fs::ModSculptor ? 0.45f : 0.85f));
            auto bar = juce::Rectangle<float> (a.getX() + k * bw + 1, h > 0 ? zeroY - h : zeroY, bw - 2, std::abs (h));
            g.setColour (v > 0 ? col::accent : col::accent2);
            g.fillRect (bar);
            if (k % 3 == 0)
            {
                const float f = fs::toneBandHz (k);
                g.setColour (col::dim); g.setFont (juce::FontOptions (10.0f));
                g.drawText (f >= 1000 ? juce::String (f / 1000.0f, 1) + "k" : juce::String ((int) f), (int) (a.getX() + k * bw), (int) a.getBottom() - 13, 40, 12, juce::Justification::left);
            }
        }
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f));
        g.drawText (mod == fs::ModSculptor ? juce::String::fromUTF8 ("今の量（上 = 持ち上げ、下 = 下げ、±24 dB）")
                                           : juce::String::fromUTF8 ("今下げている量（0〜18 dB）。サイドチェーンにぶつかる相手を入れる"),
                    a.reduced (8, 2).toNearestInt(), juce::Justification::topRight);
        return;
    }
    for (int b = 0; b < 3; ++b)
        if (! bandCols[b].isEmpty())
        {
            g.setColour (col::bg.withAlpha (0.6f)); g.fillRoundedRectangle (bandCols[b].toFloat(), 6.0f);
            g.setColour (col::edge); g.drawRoundedRectangle (bandCols[b].toFloat(), 6.0f, 1.0f);
        }
}

void ModulePage::resized() {}

juce::Slider& ModulePage::knob (const juce::String& id, const juce::String& label, juce::Rectangle<int> r)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 16);
    s->setBounds (r.withTrimmedTop (14));
    addAndMakeVisible (*s);
    sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *s));
    caption (label, r.withHeight (14), juce::Justification::centred);
    auto& ref = *s; comps.push_back (std::move (s)); return ref;
}

juce::Button& ModulePage::toggle (const juce::String& id, const juce::String& text, juce::Rectangle<int> r)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->setClickingTogglesState (true);
    b->setBounds (r);
    addAndMakeVisible (*b);
    bAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *b));
    auto& ref = *b; comps.push_back (std::move (b)); return ref;
}

juce::ComboBox& ModulePage::combo (const juce::String& id, juce::Rectangle<int> r, const juce::String& label)
{
    if (label.isNotEmpty()) { caption (label, r.removeFromTop (14)); }
    auto c = std::make_unique<juce::ComboBox>();
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->addItemList (prm->choices, 1);
    c->setBounds (r);
    addAndMakeVisible (*c);
    cAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c));
    auto& ref = *c; comps.push_back (std::move (c)); return ref;
}

void ModulePage::caption (const juce::String& text, juce::Rectangle<int> r, juce::Justification j)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (juce::FontOptions (11.5f, juce::Font::bold));
    l->setColour (juce::Label::textColourId, col::dim);
    l->setJustificationType (j);
    l->setBounds (r);
    l->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*l);
    comps.push_back (std::move (l));
}

void ModulePage::build()
{
    // 添付（attachment）を先に外してから部品を消す
    sAtt.clear(); bAtt.clear(); cAtt.clear(); eqS.clear(); eqB.clear(); eqC.clear();
    eqBandComps.clear(); eqGraph.reset(); comps.clear();
    for (auto& m : meter) m = nullptr;
    for (auto& c : bandCols) c = {};
    const int W = 1036, H = 470;
    setSize (W, H);
    auto area = juce::Rectangle<int> (0, 0, W, H).reduced (14, 10);
    auto head = area.removeFromTop (34);

    if (mod < 0)
    {
        caption ("MASTER", head.removeFromLeft (200));
        auto row = area.removeFromTop (110);
        const int kw = 92;
        knob ("inGain", "INPUT", row.removeFromLeft (kw));
        knob ("outGain", "OUTPUT", row.removeFromLeft (kw));
        knob ("pan", "PAN", row.removeFromLeft (kw));
        knob ("width", "WIDTH", row.removeFromLeft (kw));
        knob ("delayL", "DELAY L", row.removeFromLeft (kw));
        knob ("delayR", "DELAY R", row.removeFromLeft (kw));
        row.removeFromLeft (20);
        auto tg = row.removeFromLeft (130);
        toggle ("mono", "MONO", tg.removeFromTop (26).reduced (2));
        toggle ("swap", "SWAP L/R", tg.removeFromTop (26).reduced (2));
        toggle ("invert", "INVERT", tg.removeFromTop (26).reduced (2));
        toggle ("bypass", "BYPASS ALL", tg.removeFromTop (26).reduced (2));
        area.removeFromTop (16);
        caption ("LIMITER (always last)", area.removeFromTop (18));
        auto lr = area.removeFromTop (100);
        toggle ("lim_on", "LIMITER", lr.removeFromLeft (110).withSizeKeepingCentre (100, 28));
        combo ("lim_mode", lr.removeFromLeft (150).withSizeKeepingCentre (140, 42), "MODE");
        combo ("lim_style", lr.removeFromLeft (150).withSizeKeepingCentre (140, 42), "STYLE");
        knob ("lim_ceiling", "CEILING", lr.removeFromLeft (kw));
        area.removeFromTop (10);
        caption (juce::String::fromUTF8 ("各モジュールの上の丸でオン/オフ、名前をドラッグで順番を入れ替え。サイドチェーンは Logic のプラグイン右上の「サイドチェーン」で選ぶ"),
                 area.removeFromTop (20));
        return;
    }

    // 見出し: 名前 / オン / ミックス
    caption (fs::moduleName (mod), head.removeFromLeft (150));
    toggle (fsid::J (fsid::id (mod, "on")), "ON", head.removeFromLeft (60).reduced (4, 4));
    {
        auto mixArea = head.removeFromRight (230);
        caption ("MIX", mixArea.removeFromLeft (40));
        auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        s->setBounds (mixArea.reduced (2, 6));
        addAndMakeVisible (*s);
        sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, fsid::J (fsid::id (mod, "mix")), *s));
        comps.push_back (std::move (s));
    }
    area.removeFromTop (4);

    using KV = std::vector<std::pair<const char*, const char*>>;
    auto globalsRow = [&] () { auto r = area.removeFromTop (96); return r; };
    switch (mod)
    {
        case fs::ModEq:
        {
            eqGraph = std::make_unique<EqGraph> (proc);
            eqGraph->setBounds (area.removeFromTop (300));
            addAndMakeVisible (*eqGraph);
            eqGraph->onSelect = [this] (int) { buildEqBand(); };
            area.removeFromTop (8);
            eqBandArea = area;
            buildEqBand();
            break;
        }
        case fs::ModComp1:
        case fs::ModComp2:
        {
            auto g = globalsRow();
            combo (fsid::J (fsid::id (mod, "detect")), g.removeFromLeft (120).withSizeKeepingCentre (110, 44), "DETECT");
            auto t = g.removeFromLeft (130);
            toggle (fsid::J (fsid::id (mod, "autogain")), "AUTO GAIN", t.removeFromTop (30).reduced (2));
            toggle (fsid::J (fsid::id (mod, "autorel")), "AUTO REL", t.removeFromTop (30).reduced (2));
            toggle (fsid::J (fsid::id (mod, "vintage")), "VINTAGE", t.removeFromTop (30).reduced (2));
            knob (fsid::J (fsid::id (mod, "gain")), "GAIN", g.removeFromLeft (84));
            knob (fsid::J (fsid::id (mod, "hpf")), "SC HPF", g.removeFromLeft (84));
            knob (fsid::J (fsid::id (mod, "lpf")), "SC LPF", g.removeFromLeft (84));
            toggle (fsid::J (fsid::id (mod, "ext")), "EXT SC", g.removeFromLeft (100).withSizeKeepingCentre (90, 28));
            buildBands (KV { { "thr", "THRESH" }, { "ratio", "RATIO" }, { "att", "ATTACK" }, { "rel", "RELEASE" }, { "knee", "KNEE" }, { "gain", "GAIN" }, { "mix", "MIX" } },
                        KV { { "byp", "BYPASS" } }, KV {}, area);
            break;
        }
        case fs::ModExciter:
        {
            auto g = globalsRow();
            combo (fsid::J (fsid::id (mod, "fmode")), g.removeFromLeft (120).withSizeKeepingCentre (110, 44), "HARMONICS");
            toggle (fsid::J (fsid::id (mod, "tame")), "TAME", g.removeFromLeft (90).withSizeKeepingCentre (80, 28));
            knob (fsid::J (fsid::id (mod, "tone")), "TONE", g.removeFromLeft (84));
            knob (fsid::J (fsid::id (mod, "postf")), "POST FREQ", g.removeFromLeft (84));
            knob (fsid::J (fsid::id (mod, "postg")), "POST GAIN", g.removeFromLeft (84));
            caption (juce::String::fromUTF8 ("X: 左 = 角ばった / 右 = なめらか　Y: 下 = 奇数倍音 / 上 = 偶数倍音"), g.reduced (10, 30));
            buildBands (KV { { "drive", "DRIVE" }, { "x", "X" }, { "y", "Y" }, { "blend", "BLEND" } }, KV { { "byp", "BYPASS" }, { "trash", "TRASH" } }, KV {}, area);
            break;
        }
        case fs::ModTransient:
        {
            auto g = globalsRow();
            combo (fsid::J (fsid::id (mod, "mode")), g.removeFromLeft (140).withSizeKeepingCentre (130, 44), "MODE");
            buildBands (KV { { "att", "ATTACK" }, { "sus", "SUSTAIN" } }, KV { { "byp", "BYPASS" } }, KV { { "contour", "CONTOUR" } }, area);
            break;
        }
        case fs::ModGate:
        {
            auto g = globalsRow();
            knob (fsid::J (fsid::id (mod, "hpf")), "SC HPF", g.removeFromLeft (84));
            knob (fsid::J (fsid::id (mod, "lpf")), "SC LPF", g.removeFromLeft (84));
            toggle (fsid::J (fsid::id (mod, "ext")), "EXT SC", g.removeFromLeft (100).withSizeKeepingCentre (90, 28));
            buildBands (KV { { "open", "THRESH" }, { "close", "HYST" }, { "ratio", "RATIO" }, { "att", "ATTACK" }, { "rel", "RELEASE" }, { "hold", "HOLD" } },
                        KV { { "byp", "BYPASS" } }, KV {}, area);
            break;
        }
        case fs::ModDensity:
            area.removeFromTop (60);
            buildBands (KV { { "range", "RANGE" }, { "ratio", "RATIO" }, { "thr", "THRESH" }, { "speed", "SPEED" } }, KV { { "byp", "BYPASS" } }, KV {}, area);
            break;
        case fs::ModClipper:
            area.removeFromTop (60);
            buildBands (KV { { "thr", "THRESH" }, { "boost", "SOFT BOOST" } }, KV { { "byp", "BYPASS" } }, KV {}, area);
            break;
        case fs::ModPhase:
        {
            auto r = area.removeFromTop (130);
            knob (fsid::J (fsid::id (mod, "rot")), "ROTATION", r.removeFromLeft (120));
            knob (fsid::J (fsid::id (mod, "delay")), "DELAY", r.removeFromLeft (120));
            break;
        }
        case fs::ModSculptor:
        {
            auto r = area.removeFromTop (130);
            combo (fsid::J (fsid::id (mod, "target")), r.removeFromLeft (220).withSizeKeepingCentre (200, 46), "TARGET");
            knob (fsid::J (fsid::id (mod, "amount")), "AMOUNT", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "tone")), "TONE", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "speed")), "SPEED", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "lo")), "LOW", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "hi")), "HIGH", r.removeFromLeft (110));
            area.removeFromTop (10);
            bandCols[0] = area.removeFromTop (180);   // 量の表示
            break;
        }
        case fs::ModUnmask:
        {
            auto r = area.removeFromTop (130);
            knob (fsid::J (fsid::id (mod, "amount")), "AMOUNT", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "sens")), "SENSITIVITY", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "att")), "ATTACK", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "rel")), "RELEASE", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "lo")), "LOW", r.removeFromLeft (110));
            knob (fsid::J (fsid::id (mod, "hi")), "HIGH", r.removeFromLeft (110));
            area.removeFromTop (10);
            bandCols[0] = area.removeFromTop (180);
            break;
        }
        default: break;
    }
}

void ModulePage::buildBands (const std::vector<std::pair<const char*, const char*>>& knobs, const std::vector<std::pair<const char*, const char*>>& toggles,
                             const std::vector<std::pair<const char*, const char*>>& combos, juce::Rectangle<int> area)
{
    const int nb = (int) proc.apvts.getRawParameterValue (fsid::J (fsid::id (mod, "bands")))->load() + 1;
    shownBands = nb;
    auto top = area.removeFromTop (60);
    combo (fsid::J (fsid::id (mod, "bands")), top.removeFromLeft (90).withSizeKeepingCentre (80, 44), "BANDS");
    if (nb >= 2) knob (fsid::J (fsid::id (mod, "x1")), "SPLIT 1", top.removeFromLeft (80).withTrimmedBottom (-30));
    if (nb >= 3) knob (fsid::J (fsid::id (mod, "x2")), "SPLIT 2", top.removeFromLeft (80).withTrimmedBottom (-30));
    area.removeFromTop (6);
    const int colW = (area.getWidth() - 2 * 10) / 3;
    static const char* names[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        auto c = juce::Rectangle<int> (area.getX() + b * (colW + 10), area.getY(), colW, area.getHeight());
        if (b >= nb) continue;
        bandCols[b] = c;
        auto in = c.reduced (8, 6);
        auto hdr = in.removeFromTop (24);
        caption (nb == 1 ? juce::String ("FULL BAND") : juce::String (names[nb == 2 && b == 1 ? 2 : b]), hdr.removeFromLeft (90));
        for (auto& t : toggles) toggle (fsid::J (fsid::bandId (mod, b, t.first)), t.second, hdr.removeFromLeft (76).reduced (2, 1));
        for (auto& cb : combos) combo (fsid::J (fsid::bandId (mod, b, cb.first)), hdr.removeFromLeft (100).reduced (2, 1));
        auto lab = std::make_unique<juce::Label>();
        lab->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        lab->setColour (juce::Label::textColourId, col::accent2);
        lab->setJustificationType (juce::Justification::centredRight);
        lab->setBounds (hdr);
        addAndMakeVisible (*lab);
        meter[b] = lab.get();
        comps.push_back (std::move (lab));
        // つまみを 4 列で並べる
        const int perRow = knobs.size() > 4 ? 4 : (int) knobs.size();
        const int kw = in.getWidth() / juce::jmax (1, perRow), kh = 92;
        for (size_t k = 0; k < knobs.size(); ++k)
        {
            const int row = (int) k / perRow, cl = (int) k % perRow;
            knob (fsid::J (fsid::bandId (mod, b, knobs[k].first)), knobs[k].second, { in.getX() + cl * kw, in.getY() + 4 + row * kh, kw, kh });
        }
    }
}

void ModulePage::buildEqBand()
{
    // 選んだ帯域のつまみだけ作り直す（先に添付を外してから部品を消す）
    eqS.clear(); eqB.clear(); eqC.clear();
    eqBandComps.clear();
    const int b = eqGraph != nullptr ? eqGraph->selected : 0;
    auto area = eqBandArea;
    auto& sA = eqS; auto& bA = eqB; auto& cA = eqC;
    auto addK = [&] (const char* w, const juce::String& label, juce::Rectangle<int> r)
    {
        auto l = std::make_unique<juce::Label> (juce::String(), label);
        l->setFont (juce::FontOptions (11.5f, juce::Font::bold)); l->setColour (juce::Label::textColourId, col::dim);
        l->setJustificationType (juce::Justification::centred); l->setBounds (r.withHeight (14)); addAndMakeVisible (*l);
        auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 16);
        s->setBounds (r.withTrimmedTop (14)); addAndMakeVisible (*s);
        sA.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, fsid::J (fsid::eqId (b, w)), *s));
        eqBandComps.push_back (std::move (l)); eqBandComps.push_back (std::move (s));
    };
    auto hdr = std::make_unique<juce::Label> (juce::String(), "BAND " + juce::String (b + 1));
    hdr->setFont (juce::FontOptions (13.0f, juce::Font::bold)); hdr->setColour (juce::Label::textColourId, col::accent);
    hdr->setBounds (area.removeFromLeft (80).withHeight (30)); addAndMakeVisible (*hdr); eqBandComps.push_back (std::move (hdr));
    auto onB = std::make_unique<juce::TextButton> ("ON"); onB->setClickingTogglesState (true);
    onB->setBounds (juce::Rectangle<int> (area.getX(), area.getY(), 60, 28)); addAndMakeVisible (*onB);
    bA.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, fsid::J (fsid::eqId (b, "on")), *onB));
    eqBandComps.push_back (std::move (onB));
    auto shape = std::make_unique<juce::ComboBox>();
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (fsid::J (fsid::eqId (b, "shape"))))) shape->addItemList (prm->choices, 1);
    shape->setBounds (juce::Rectangle<int> (area.getX(), area.getY() + 36, 170, 26)); addAndMakeVisible (*shape);
    cA.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, fsid::J (fsid::eqId (b, "shape")), *shape));
    eqBandComps.push_back (std::move (shape));
    area.removeFromLeft (190);
    addK ("freq", "FREQ", area.removeFromLeft (90).withHeight (110));
    addK ("gain", "GAIN", area.removeFromLeft (90).withHeight (110));
    addK ("q", "Q", area.removeFromLeft (90).withHeight (110));
    area.removeFromLeft (30);
    auto dyn = std::make_unique<juce::TextButton> ("DYNAMIC"); dyn->setClickingTogglesState (true);
    dyn->setBounds (juce::Rectangle<int> (area.getX(), area.getY(), 100, 28)); addAndMakeVisible (*dyn);
    bA.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, fsid::J (fsid::eqId (b, "dyn")), *dyn));
    eqBandComps.push_back (std::move (dyn));
    auto dir = std::make_unique<juce::ComboBox>();
    dir->addItemList ({ "Down", "Up" }, 1);
    dir->setBounds (juce::Rectangle<int> (area.getX(), area.getY() + 36, 100, 26)); addAndMakeVisible (*dir);
    cA.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, fsid::J (fsid::eqId (b, "dir")), *dir));
    eqBandComps.push_back (std::move (dir));
    area.removeFromLeft (110);
    addK ("thr", "THRESH", area.removeFromLeft (90).withHeight (110));
    area.removeFromLeft (20);
    auto tip = std::make_unique<juce::Label> (juce::String(), juce::String::fromUTF8 ("点をドラッグ = 周波数と量\nホイール = Q、ダブルクリック = オン/オフ"));
    tip->setFont (juce::FontOptions (12.0f)); tip->setColour (juce::Label::textColourId, col::dim);
    tip->setBounds (area.withHeight (60)); addAndMakeVisible (*tip); eqBandComps.push_back (std::move (tip));
}

void ModulePage::timerCallback()
{
    // 帯域の数が変わったら作り直す
    if (fsid::multiband (mod))
    {
        const int nb = (int) proc.apvts.getRawParameterValue (fsid::J (fsid::id (mod, "bands")))->load() + 1;
        if (nb != shownBands) { build(); repaint(); return; }
    }
    const auto& st = proc.strip();
    for (int b = 0; b < 3; ++b)
    {
        if (meter[b] == nullptr) continue;
        float v = 0; juce::String unit = " dB";
        switch (mod)
        {
            case fs::ModComp1: v = -st.compModule (0).gainReduction (b); break;
            case fs::ModComp2: v = -st.compModule (1).gainReduction (b); break;
            case fs::ModGate: v = -std::min (60.0f, st.gateModule().gainReduction (b)); break;
            case fs::ModDensity: v = st.densityModule().boost (b); break;
            case fs::ModClipper: v = -st.clipperModule().reduction (b); break;
            case fs::ModTransient: v = st.transientModule().currentGain (b); break;
            default: break;
        }
        meter[b]->setText ((v > 0.05f ? "+" : "") + juce::String (v, 1) + unit, juce::dontSendNotification);
    }
    if (mod == fs::ModSculptor || mod == fs::ModUnmask) repaint (bandCols[0]);
}

// =====================================================================
ForgeStripEditor::ForgeStripEditor (ForgeStripProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (chain);
    chain.onSelect = [this] (int m) { showPage (m); };
    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (status);
    setSize (1060, 590);
    showPage (fs::ModEq);
    startTimerHz (10);
}

ForgeStripEditor::~ForgeStripEditor() { setLookAndFeel (nullptr); }

void ForgeStripEditor::showPage (int module)
{
    current = module;
    chain.selected = module;
    page = std::make_unique<ModulePage> (processor, module);
    addAndMakeVisible (*page);
    resized();
    repaint();
}

void ForgeStripEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FORGE STRIP", 14, 8, 200, 26, juce::Justification::centredLeft);
    // 入出力メーター
    auto meterBar = [&] (juce::Rectangle<float> r, float db, const juce::String& label)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r, 3.0f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -0.5f ? juce::Colour (0xffff6b5a) : col::accent2);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * t), 3.0f);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, r.translated (-34, 0).withWidth (30).toNearestInt(), juce::Justification::centredRight);
    };
    meterBar ({ 700, 10, 140, 9 }, processor.inLevel.load(), "IN");
    meterBar ({ 700, 23, 140, 9 }, processor.outLevel.load(), "OUT");
    g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("並び: 左 → 右の順に通る"), 230, 12, 300, 16, juce::Justification::centredLeft);
}

void ForgeStripEditor::resized()
{
    chain.setBounds (12, 42, getWidth() - 24, 40);
    if (page != nullptr) page->setBounds (12, 90, getWidth() - 24, 470);
    status.setBounds (14, getHeight() - 26, getWidth() - 28, 20);
}

void ForgeStripEditor::timerCallback()
{
    const auto st = processor.currentSettings();
    const int lat = processor.strip().latencyFor (st);
    juce::String s = juce::String::fromUTF8 ("遅れ ") + juce::String (lat) + juce::String::fromUTF8 (" サンプル（")
                   + juce::String (lat * 1000.0 / juce::jmax (1.0, processor.getSampleRate()), 1) + " ms)";
    s += processor.sidechainConnected() ? juce::String::fromUTF8 ("　サイドチェーン: つながっている") : juce::String::fromUTF8 ("　サイドチェーン: なし");
    status.setText (s, juce::dontSendNotification);
    repaint (0, 0, getWidth(), 40);
}
