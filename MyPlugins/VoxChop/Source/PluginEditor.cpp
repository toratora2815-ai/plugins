#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff141218), panel (0xff1d1b22), edge (0xff302c38),
                       accent (0xffff7a59), violet (0xffb18cff), teal (0xff5fd4c4), blue (0xff7fb8ff), pink (0xffff8fc8), text (0xffeeeaf2);

    void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float v)
    {
        if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    float param (juce::AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }
    juce::String noteName (int i) { return juce::MidiMessage::getMidiNoteName (VoxChopProcessor::firstNote + i, true, true, 3); }
}

//==============================================================================
VoxLook::VoxLook()
{
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.4f));
    setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId, text);
}

void VoxLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.14f);
    const float arcR = radius - lineW * 0.5f;
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
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (v, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float bodyR = arcR - lineW * 1.4f;
    g.setColour (panel.brighter (0.15f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    juce::Path needle;
    needle.addRoundedRectangle (-1.5f, -bodyR, 3.0f, bodyR * 0.55f, 1.5f);
    g.setColour (text);
    g.fillPath (needle, juce::AffineTransform::rotation (to).translated (c.x, c.y));
}

//==============================================================================
WaveEditor::Peaks WaveEditor::makePeaks (const vox::Buf* a, int width)
{
    Peaks p;
    if (a == nullptr || a->empty() || width <= 0) return p;
    p.lo.assign ((size_t) width, 0.0f);
    p.hi.assign ((size_t) width, 0.0f);
    const size_t n = a->size();
    for (int x = 0; x < width; ++x)
    {
        const size_t i0 = n * (size_t) x / (size_t) width, i1 = std::max (i0 + 1, n * (size_t) (x + 1) / (size_t) width);
        float lo = 0, hi = 0;
        for (size_t i = i0; i < std::min (i1, n); ++i) { lo = std::min (lo, (*a)[i]); hi = std::max (hi, (*a)[i]); }
        p.lo[(size_t) x] = lo;
        p.hi[(size_t) x] = hi;
    }
    return p;
}

void WaveEditor::refresh()
{
    auto s = proc.getSource();
    auto v = proc.getVoice();
    length = s != nullptr ? (long) s->audio.size() : 0;
    fs = s != nullptr ? s->fs : 48000.0;
    const int w = (int) lane().getWidth();
    // 波形の形は、声が変わったときだけ作り直す（切れ目を動かしている間は作り直さない）
    const int av = proc.audioVersion.load();
    if (av != shownAudio || w != shownWidth)
    {
        srcPeaks = makePeaks (s != nullptr ? &s->audio : nullptr, w);
        outPeaks = makePeaks (v != nullptr ? &v->audio : nullptr, w);
        shownAudio = av;
        shownWidth = w;
    }
    repaint();
}

bool WaveEditor::whole() const { return (int) param (proc.apvts, "mode") == 1; }
double WaveEditor::xToSample (float x) const { auto l = lane(); return juce::jlimit (0.0, (double) length, (double) (x - l.getX()) / l.getWidth() * (double) length); }
float WaveEditor::sampleToX (double s) const { auto l = lane(); return l.getX() + (float) (s / juce::jmax (1.0, (double) length)) * l.getWidth(); }

int WaveEditor::sliceAt (float x) const
{
    auto sl = proc.getSlices();
    if (sl == nullptr) return -1;
    const double s = xToSample (x);
    for (int i = 0; i < (int) sl->size(); ++i)
        if (s >= (double) (*sl)[(size_t) i].start && s < (double) (*sl)[(size_t) i].end) return i;
    return -1;
}

bool WaveEditor::edgeAt (float x, int& slice, bool& isEnd) const
{
    auto sl = proc.getSlices();
    if (sl == nullptr || whole()) return false;
    float best = 6.0f;
    slice = -1;
    for (int i = 0; i < (int) sl->size(); ++i)
        for (bool e : { false, true })
        {
            const float d = std::abs (x - sampleToX ((double) (e ? (*sl)[(size_t) i].end : (*sl)[(size_t) i].start)));
            if (d < best) { best = d; slice = i; isEnd = e; }
        }
    return slice >= 0;
}

void WaveEditor::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    if (length == 0)
    {
        g.setColour (text.withAlpha (0.55f));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Drop a vocal here  (or click)", getLocalBounds().toFloat().withTrimmedBottom (30), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (text.withAlpha (0.4f));
        g.drawText ("or press CAPTURE and play the vocal track (Side Chain menu at the top of this window)  -  WAV / AIFF / FLAC / MP3 / M4A",
                    getLocalBounds().toFloat().withTrimmedTop (36), juce::Justification::centred);
        return;
    }
    const auto l = lane();
    const bool isWhole = whole();
    auto sl = proc.getSlices();
    const int sel = proc.selected();
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

    if (! isWhole && sl != nullptr)
    {
        for (int i = 0; i < (int) sl->size(); ++i)
        {
            const auto& s = (*sl)[(size_t) i];
            const float x0 = sampleToX ((double) s.start), x1 = sampleToX ((double) s.end);
            const auto band = juce::Rectangle<float> (x0, l.getY(), juce::jmax (1.0f, x1 - x0), l.getHeight());
            g.setColour (i == sel ? accent.withAlpha (0.22f) : (i % 2 ? violet.withAlpha (0.07f) : teal.withAlpha (0.07f)));
            g.fillRect (band);
            g.setColour (i == sel ? accent : text.withAlpha (0.55f));
            if (x1 - x0 > 18.0f)
                g.drawText (juce::String (i + 1) + (x1 - x0 > 46.0f ? "  " + noteName (i) : juce::String()),
                            juce::Rectangle<float> (x0 + 3, 3, juce::jmax (10.0f, x1 - x0 - 3), 16), juce::Justification::centredLeft, false);
        }
    }
    auto drawPeaks = [&] (const Peaks& p, juce::Colour c)
    {
        const float mid = l.getCentreY(), half = l.getHeight() * 0.5f;
        g.setColour (c);
        for (size_t x = 0; x < p.lo.size(); ++x)
        {
            const float y0 = mid - juce::jlimit (0.0f, 1.0f, p.hi[x]) * half, y1 = mid - juce::jlimit (-1.0f, 0.0f, p.lo[x]) * half;
            g.drawVerticalLine ((int) (l.getX() + (float) x), y0, juce::jmax (y0 + 1.0f, y1));
        }
    };
    drawPeaks (srcPeaks, text.withAlpha (0.22f));
    drawPeaks (outPeaks, accent.withAlpha (0.8f));

    if (! isWhole && sl != nullptr)
        for (int i = 0; i < (int) sl->size(); ++i)
            for (long p : { (*sl)[(size_t) i].start, (*sl)[(size_t) i].end })
            {
                g.setColour (i == sel ? accent : text.withAlpha (0.45f));
                g.fillRect (sampleToX ((double) p) - 0.75f, l.getY(), 1.5f, l.getHeight());
            }
    if (isWhole)
    {
        g.setColour (accent);
        g.drawText ("WHOLE  -  click = play the whole voice, drag it into Logic", juce::Rectangle<float> (l.getX(), 3, l.getWidth(), 16), juce::Justification::centredLeft);
    }
    const double pp = proc.playPos.load();
    if (pp >= 0.0)
    {
        g.setColour (text);
        g.fillRect (sampleToX (pp) - 1.0f, l.getY(), 2.0f, l.getHeight());
    }
}

void WaveEditor::mouseMove (const juce::MouseEvent& e)
{
    int s; bool en;
    if (length > 0 && edgeAt (e.position.x, s, en)) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (length > 0 && (whole() || sliceAt (e.position.x) >= 0)) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WaveEditor::mouseDown (const juce::MouseEvent& e)
{
    dragSlice = -1;
    pressed = draggedOut = false;
    if (length == 0) { if (onEmptyClick) onEmptyClick(); return; }
    if (whole()) { pressed = true; pressSlice = -1; return; }

    const int i = sliceAt (e.position.x);
    if (e.mods.isPopupMenu())
    {
        const double at = xToSample (e.position.x);
        auto cur = proc.getSlices();
        const int count = cur != nullptr ? (int) cur->size() : 0;
        juce::PopupMenu m;
        m.addItem (1, "Split here", i >= 0);
        m.addItem (2, "Delete this slice", i >= 0);
        m.addItem (3, "Join with the next slice", i >= 0 && i + 1 < count);
        m.addSeparator();
        m.addItem (4, "Chop again (AUTO / GRID)");
        juce::Component::SafePointer<WaveEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
            [safe, i, at] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& p = safe->proc;
                if (r == 4) { p.rechop(); return; }
                auto now = p.getSlices();
                if (now == nullptr || i < 0 || i >= (int) now->size()) return;
                VoxSlices c = *now;
                if (r == 1)
                {
                    const long cut = (long) at, minLen = (long) (0.03 * safe->fs);
                    if (cut - c[(size_t) i].start < minLen || c[(size_t) i].end - cut < minLen || (int) c.size() >= VoxChopProcessor::maxSlices) return;
                    VoxSlice b = c[(size_t) i];
                    b.start = cut;
                    c[(size_t) i].end = cut;
                    c.insert (c.begin() + i + 1, b);
                }
                else if (r == 2) c.erase (c.begin() + i);
                else if (r == 3 && i + 1 < (int) c.size()) { c[(size_t) i].end = c[(size_t) i + 1].end; c.erase (c.begin() + i + 1); }
                p.setSlices (std::move (c), true);
            });
        return;
    }
    int s; bool en;
    if (edgeAt (e.position.x, s, en)) { dragSlice = s; dragEnd = en; return; }
    if (i >= 0)
    {
        proc.select (i);
        proc.preview (i);
        pressed = true;
        pressSlice = i;
        repaint();
    }
    else proc.stopPreview();
}

void WaveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlice >= 0)
    {
        auto cur = proc.getSlices();
        if (cur == nullptr || dragSlice >= (int) cur->size()) return;
        VoxSlices c = *cur;
        const long p = (long) xToSample (e.position.x), minLen = (long) (0.03 * fs);
        auto& s = c[(size_t) dragSlice];
        if (dragEnd)
        {
            VoxSlice* nx = dragSlice + 1 < (int) c.size() ? &c[(size_t) dragSlice + 1] : nullptr;
            const bool touch = nx != nullptr && nx->start == s.end;
            const long hi = nx == nullptr ? length : (touch ? nx->end - minLen : nx->start);
            const long v = juce::jlimit (s.start + minLen, juce::jmax (s.start + minLen, hi), p);
            if (touch) nx->start = v;
            s.end = v;
        }
        else
        {
            VoxSlice* pv = dragSlice > 0 ? &c[(size_t) dragSlice - 1] : nullptr;
            const bool touch = pv != nullptr && pv->end == s.start;
            const long lo = pv == nullptr ? 0 : (touch ? pv->start + minLen : pv->end);
            const long v = juce::jlimit (juce::jmin (lo, s.end - minLen), s.end - minLen, p);
            if (touch) pv->end = v;
            s.start = v;
        }
        proc.setSlices (std::move (c), true);
        return;
    }
    if (pressed && ! draggedOut && e.getDistanceFromDragStart() > 8 && onDragOut)
    {
        draggedOut = true;
        onDragOut (pressSlice);
    }
}

void WaveEditor::mouseUp (const juce::MouseEvent&)
{
    if (whole() && pressed && ! draggedOut) proc.preview (-1);
    pressed = false;
    dragSlice = -1;
}

void WaveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (whole() || length == 0) return;
    const int i = sliceAt (e.position.x);
    auto cur = proc.getSlices();
    if (cur == nullptr || i < 0 || (int) cur->size() >= VoxChopProcessor::maxSlices) return;
    VoxSlices c = *cur;
    const long cut = (long) xToSample (e.position.x), minLen = (long) (0.03 * fs);
    if (cut - c[(size_t) i].start < minLen || c[(size_t) i].end - cut < minLen) return;
    VoxSlice b = c[(size_t) i];
    b.start = cut;
    c[(size_t) i].end = cut;
    c.insert (c.begin() + i + 1, b);
    proc.stopPreview();
    proc.setSlices (std::move (c), true);
}

//==============================================================================
float VoiceMap::srcF0() const { const float f = proc.sourceF0(); return f > 0.0f ? f : 120.0f; }
static constexpr float mapLoHz = 50.0f, mapHiHz = 800.0f, mapLoBody = -12.0f, mapHiBody = 15.0f;
float VoiceMap::hzToX (float hz) const { auto p = plot(); return p.getX() + p.getWidth() * (std::log2 (hz / mapLoHz) / std::log2 (mapHiHz / mapLoHz)); }
float VoiceMap::xToHz (float x) const { auto p = plot(); return mapLoHz * std::pow (mapHiHz / mapLoHz, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
float VoiceMap::bodyToY (float b) const { auto p = plot(); return p.getBottom() - p.getHeight() * (b - mapLoBody) / (mapHiBody - mapLoBody); }
float VoiceMap::yToBody (float y) const { auto p = plot(); return mapLoBody + (mapHiBody - mapLoBody) * juce::jlimit (0.0f, 1.0f, (p.getBottom() - y) / p.getHeight()); }

void VoiceMap::setPoint (float hz, float bodyAbs)
{
    const float f0 = srcF0();
    setParam (proc.apvts, "shift", juce::jlimit (-24.0f, 24.0f, 12.0f * std::log2 (hz / f0)));
    setParam (proc.apvts, "formant", juce::jlimit (-12.0f, 12.0f, bodyAbs - VoxChopProcessor::bodyOf (f0)));
}

void VoiceMap::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    const auto p = plot();
    g.setColour (bg.withAlpha (0.6f));
    g.fillRect (p);

    // 男性・女性の声の高さの範囲
    auto band = [&] (float lo, float hi, juce::Colour c, const char* name, float labelY)
    {
        g.setColour (c.withAlpha (0.09f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (hzToX (lo), p.getY(), hzToX (hi), p.getBottom()));
        g.setColour (c.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (name, juce::Rectangle<float> (hzToX (lo) + 4, labelY, hzToX (hi) - hzToX (lo) - 4, 14), juce::Justification::centredLeft);
    };
    band (85.0f, 180.0f, blue, "MALE VOICES", p.getBottom() - 16);
    band (165.0f, 255.0f, pink, "FEMALE VOICES", p.getBottom() - 30);

    g.setFont (juce::FontOptions (10.0f));
    for (float hz : { 60.0f, 100.0f, 150.0f, 200.0f, 300.0f, 400.0f, 600.0f })
    {
        g.setColour (edge);
        g.drawVerticalLine ((int) hzToX (hz), p.getY(), p.getBottom());
        g.setColour (text.withAlpha (0.45f));
        g.drawText (juce::String ((int) hz) + " Hz", juce::Rectangle<float> (hzToX (hz) - 30, p.getBottom() + 2, 60, 14), juce::Justification::centred);
    }
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("LOWER  \xe2\x86\x90  PITCH  \xe2\x86\x92  HIGHER"), juce::Rectangle<float> (p.getX(), p.getBottom() + 12, p.getWidth(), 14), juce::Justification::centred);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, p.getX() - 22, p.getCentreY()));
        g.drawText (juce::String::fromUTF8 ("BIGGER BODY  \xe2\x86\x90  BODY  \xe2\x86\x92  SMALLER BODY"),
                    juce::Rectangle<float> (p.getX() - 22 - p.getHeight() / 2, p.getCentreY() - 7, p.getHeight(), 14), juce::Justification::centred);
    }

    const float f0 = srcF0(), srcBody = VoxChopProcessor::bodyOf (f0);
    const float shift = param (proc.apvts, "shift"), formant = param (proc.apvts, "formant");
    const float curHz = f0 * std::pow (2.0f, shift / 12.0f), curBody = srcBody + formant;

    // 目印
    for (auto& lm : VoxChopProcessor::landmarks())
    {
        const float x = hzToX (lm.hz), y = bodyToY (lm.body);
        g.setColour (text.withAlpha (0.75f));
        g.drawEllipse (x - 6, y - 6, 12, 12, 1.5f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (lm.name, juce::Rectangle<float> (x + 9, y - 8, 80, 16), juce::Justification::centredLeft);
    }
    // 元の声
    const float yx = hzToX (f0), yy = bodyToY (srcBody);
    g.setColour (teal);
    g.fillEllipse (yx - 5, yy - 5, 10, 10);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (proc.sourceF0() > 0 ? "YOU (" + juce::String (juce::roundToInt (f0)) + " Hz)" : juce::String ("YOU (no voice yet)"),
                juce::Rectangle<float> (yx - 60, yy + 7, 120, 14), juce::Justification::centred);
    // 今の点
    const float cx = hzToX (juce::jlimit (mapLoHz, mapHiHz, curHz)), cy = bodyToY (juce::jlimit (mapLoBody, mapHiBody, curBody));
    juce::Path line;
    line.startNewSubPath (yx, yy);
    line.lineTo (cx, cy);
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (line, line, dashes, 2);
    g.setColour (accent.withAlpha (0.7f));
    g.fillPath (line);
    g.setColour (accent);
    g.fillEllipse (cx - 9, cy - 9, 18, 18);
    g.setColour (bg);
    g.fillEllipse (cx - 3, cy - 3, 6, 6);

    g.setColour (text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("VOICE MAP   pitch " + juce::String (shift >= 0 ? "+" : "") + juce::String (shift, 1) + " st (" + juce::String (juce::roundToInt (curHz)) + " Hz)"
                + "    body " + juce::String::fromUTF8 ("\xc3\x97") + juce::String (std::pow (2.0f, formant / 12.0f), 2),
                juce::Rectangle<float> (p.getX(), 4, p.getWidth(), 18), juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("drag the dot, or click a name  -  double-click = back to YOU", juce::Rectangle<float> (p.getX(), 4, p.getWidth(), 18), juce::Justification::centredRight);
    if (proc.busy.load())
    {
        g.setColour (accent);
        g.drawText ("working...", p.reduced (8).removeFromTop (16), juce::Justification::topRight);
    }
}

void VoiceMap::mouseDown (const juce::MouseEvent& e)
{
    for (auto& lm : VoxChopProcessor::landmarks())
        if (e.position.getDistanceFrom ({ hzToX (lm.hz), bodyToY (lm.body) }) < 13.0f)
        {
            for (auto* id : { "shift", "formant" }) if (auto* prm = proc.apvts.getParameter (id)) prm->beginChangeGesture();
            setPoint (lm.hz, lm.body);
            for (auto* id : { "shift", "formant" }) if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
            proc.previewWhenReady();
            repaint();
            return;
        }
    dragging = true;
    for (auto* id : { "shift", "formant" }) if (auto* prm = proc.apvts.getParameter (id)) prm->beginChangeGesture();
    mouseDrag (e);
}

void VoiceMap::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    setPoint (xToHz (e.position.x), yToBody (e.position.y));
    repaint();
}

void VoiceMap::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    for (auto* id : { "shift", "formant" }) if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
    proc.previewWhenReady();
}

void VoiceMap::mouseDoubleClick (const juce::MouseEvent&)
{
    dragging = false;
    for (auto* id : { "shift", "formant" })
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f));
            prm->endChangeGesture();
        }
    proc.previewWhenReady();
    repaint();
}

//==============================================================================
juce::Rectangle<float> PadGrid::cell (int i) const
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth() / 8.0f, h = b.getHeight() / 8.0f;
    return { b.getX() + (float) (i % 8) * w, b.getY() + (float) (i / 8) * h, w, h };
}

void PadGrid::paint (juce::Graphics& g)
{
    auto sl = proc.getSlices();
    const int count = sl != nullptr ? (int) sl->size() : 0, sel = proc.selected(), playing = proc.playSlice.load();
    const bool melodic = (int) param (proc.apvts, "keyMode") == 1;
    for (int i = 0; i < 64; ++i)
    {
        const auto r = cell (i).reduced (3.0f);
        const bool has = i < count;
        g.setColour (! has ? panel : (i == playing ? accent.withAlpha (0.65f) : (i == sel ? accent.withAlpha (0.3f) : edge)));
        g.fillRoundedRectangle (r, 6.0f);
        if (i == sel && has) { g.setColour (accent); g.drawRoundedRectangle (r, 6.0f, 2.0f); }
        g.setColour (has ? text : text.withAlpha (0.2f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), r.reduced (6, 4), juce::Justification::topLeft);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (text.withAlpha (has ? 0.6f : 0.2f));
        g.drawText (melodic ? juce::String() : noteName (i), r.reduced (6, 4), juce::Justification::bottomRight);
        if (has)
        {
            const auto& s = (*sl)[(size_t) i];
            juce::String flags;
            if (s.reverse) flags << "R ";
            if (s.loop) flags << "L ";
            if (s.oneShot) flags << "1 ";
            if (s.choke) flags << "C ";
            if (std::abs (s.pitch) > 0.05f) flags << (s.pitch > 0 ? "+" : "") << juce::String (s.pitch, 1);
            g.setColour (violet);
            g.drawText (flags, r.reduced (6, 4), juce::Justification::bottomLeft);
        }
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    auto sl = proc.getSlices();
    for (int i = 0; i < 64; ++i)
        if (cell (i).contains (e.position) && sl != nullptr && i < (int) sl->size())
        {
            proc.select (i);
            proc.preview (i);
            if (onPick) onPick (i);
            repaint();
            return;
        }
}

//==============================================================================
void ExportTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (enabled ? accent.withAlpha (0.16f) : panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (enabled ? accent : edge);
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    auto t = r.reduced (12.0f, 4.0f);
    g.setColour (enabled ? text : text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (title, t.removeFromTop (t.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (sub, t.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
}

//==============================================================================
VoxChopEditor::VoxChopEditor (VoxChopProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    const char* names[] = { "SOURCE", "VOICE", "KEYS" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (11);
        tabs[i].onClick = [this, i] { showPage (i); };
        styleButton (tabs[i], text);
        addAndMakeVisible (tabs[i]);
    }

    // SOURCE
    wave.onEmptyClick = [this] { openChooser(); };
    wave.onDragOut = [this] (int i)
    {
        if (processor.renderPending()) { status = "Still working - try again in a moment"; repaint(); return; }
        if (i < 0) { auto f = processor.exportWhole(); if (f.existsAsFile()) dragFiles ({ f.getFullPathName() }, wave, "the whole voice"); }
        else dragOutSlice (i);
    };
    addChildComponent (wave);
    for (auto* b : { &openButton, &captureButton, &rechopButton }) { styleButton (*b, text); addChildComponent (*b); }
    openButton.setTooltip ("Open a vocal file (or drop one on the window)");
    openButton.onClick = [this] { openChooser(); };
    captureButton.setTooltip ("Record the vocal track into Vox Chop: choose the vocal track in the Side Chain menu at the top of this window, "
                              "press CAPTURE, then play in Logic. It stops when you stop playing");
    captureButton.onClick = [this]
    {
        if (processor.capturing() || processor.captureArmed()) processor.armCapture (false);
        else
        {
            processor.armCapture (true);
            status = processor.sidechainConnected() ? "Armed - play in Logic. It records until you stop"
                                                    : "Armed - choose the vocal track in Side Chain (top of this window), then play in Logic";
        }
    };
    rechopButton.setTooltip ("Chop again with the current AUTO / GRID settings (your hand-made cuts are replaced)");
    rechopButton.onClick = [this] { processor.rechop(); };
    setupBox (modeBox, "mode", "CHOP = cut the voice and play the pieces on keys.  WHOLE = change the whole voice and drag it back into Logic");
    setupBox (chopByBox, "chopBy", "AUTO = cut at the gaps between words.  GRID = cut on the beat");
    setupBox (gridBox, "grid", "GRID length (uses the tempo when you captured, or Logic's tempo)");
    setupKnob (sens, "sens", "SENS", violet, "AUTO: higher = more, smaller pieces", false);
    sens.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    sens.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    for (auto* t : { &tileSlice, &tileAll, &tileMidi, &tileWhole }) addChildComponent (*t);
    tileSlice.onDragOut = [this] { dragOutSlice (processor.selected()); };
    tileAll.onDragOut = [this]
    {
        if (processor.renderPending()) { status = "Still working - try again in a moment"; repaint(); return; }
        auto files = processor.exportAllSlices();
        juce::StringArray paths;
        for (auto& f : files) paths.add (f.getFullPathName());
        if (! paths.isEmpty()) dragFiles (paths, tileAll, juce::String (paths.size()) + " slices");
    };
    tileMidi.onDragOut = [this] { auto f = processor.exportMidi(); if (f.existsAsFile()) dragFiles ({ f.getFullPathName() }, tileMidi, f.getFileName()); };
    tileWhole.onDragOut = [this]
    {
        if (processor.renderPending()) { status = "Still working - try again in a moment"; repaint(); return; }
        auto f = processor.exportWhole();
        if (f.existsAsFile()) dragFiles ({ f.getFullPathName() }, tileWhole, "the whole voice");
    };
    for (auto* t : { &tileSlice, &tileAll, &tileMidi, &tileWhole })
        t->onClick = [this] { status = "Drag the box into Logic (files are saved in Music / Vox Chop)"; repaint(); };

    // VOICE
    addChildComponent (map);
    setupKnob (breath, "breath", "BREATH", teal, "Replace the voice with breath. 100 % = whisper", true);
    setupKnob (grit, "grit", "GRIT", accent, "Creaky, rough voice (vocal fry / growl) - not a normal distortion", true);
    setupKnob (robot, "robot", "ROBOT", blue, "Flatten the melody to one note", true);
    setupKnob (alien, "alien", "ALIEN", violet, "Move away from a human voice (extreme body, robot, breath, metal)", true);
    setupKnob (lowCut, "lowcut", "LOW CUT", text.withAlpha (0.7f), "Cut the lows (radio / phone sounds)", true);
    setupKnob (highCut, "highcut", "HIGH CUT", text.withAlpha (0.7f), "Cut the highs", true);
    for (int i = 0; i < (int) VoxChopProcessor::voicePresets().size(); ++i)
    {
        const auto& vp = VoxChopProcessor::voicePresets()[(size_t) i];
        auto* b = presetTiles.add (new juce::TextButton (vp.name));
        b->setTooltip (vp.hint);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (22);
        styleButton (*b, text);
        b->onClick = [this, i] { processor.applyVoicePreset (i); processor.previewWhenReady(); };
        addChildComponent (b);
    }
    styleButton (hearButton, accent);
    hearButton.setTooltip ("Play the selected slice (or the whole voice) with the new voice");
    hearButton.onClick = [this] { processor.preview ((int) param (processor.apvts, "mode") == 1 ? -1 : processor.selected()); };
    addChildComponent (hearButton);

    // KEYS
    pads.onPick = [this] (int) { updateSliceControls(); };
    addChildComponent (pads);
    for (auto* s : { &slicePitch, &sliceGain })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxTextColourId, text);
        s->getProperties().set ("bipolar", true);
        addChildComponent (*s);
    }
    slicePitch.setRange (-24.0, 24.0, 0.1);
    slicePitch.setDoubleClickReturnValue (true, 0.0);
    slicePitch.setTextValueSuffix (" st");
    slicePitch.setColour (juce::Slider::rotarySliderFillColourId, violet);
    slicePitch.setTooltip ("Pitch of this slice (semitones)");
    slicePitch.onValueChange = [this] { editSelected ([v = (float) slicePitch.getValue()] (VoxSlice& s) { s.pitch = v; }); };
    slicePitch.onDragEnd = [this] { processor.preview (processor.selected()); };
    sliceGain.setRange (-24.0, 12.0, 0.1);
    sliceGain.setDoubleClickReturnValue (true, 0.0);
    sliceGain.setTextValueSuffix (" dB");
    sliceGain.setColour (juce::Slider::rotarySliderFillColourId, teal);
    sliceGain.onValueChange = [this] { editSelected ([v = (float) sliceGain.getValue()] (VoxSlice& s) { s.gainDb = v; }); };
    for (auto* l : { &slicePitchLabel, &sliceGainLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, text);
        addChildComponent (*l);
    }
    slicePitchLabel.setText ("PITCH", juce::dontSendNotification);
    sliceGainLabel.setText ("GAIN", juce::dontSendNotification);
    auto toggle = [this] (juce::ToggleButton& t, const juce::String& tip, std::function<void (VoxSlice&, bool)> fn)
    {
        t.setColour (juce::ToggleButton::textColourId, text);
        t.setColour (juce::ToggleButton::tickColourId, accent);
        t.setTooltip (tip);
        t.onClick = [this, &t, fn] { const bool on = t.getToggleState(); editSelected ([fn, on] (VoxSlice& s) { fn (s, on); }); };
        addChildComponent (t);
    };
    toggle (revToggle, "Play this slice backwards", [] (VoxSlice& s, bool on) { s.reverse = on; });
    toggle (loopToggle, "Repeat this slice while the key is held (good for long vowels)", [] (VoxSlice& s, bool on) { s.loop = on; });
    toggle (oneShotToggle, "Play to the end even if you let go of the key", [] (VoxSlice& s, bool on) { s.oneShot = on; });
    toggle (chokeToggle, "A CHOKE slice stops the other CHOKE slices (no overlapping voices)", [] (VoxSlice& s, bool on) { s.choke = on; });
    setupBox (keyModeBox, "keyMode", "SLICES = one slice per key from C1.  MELODIC = the selected slice on every key (C3 = as is) - play chords for harmonies");
    keepToggle.setColour (juce::ToggleButton::textColourId, text);
    keepToggle.setColour (juce::ToggleButton::tickColourId, accent);
    keepToggle.setTooltip ("ON = higher keys keep the same voice (body) and length.  OFF = like speeding up a tape (chipmunk / slowed)");
    keepAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "keep", keepToggle);
    addChildComponent (keepToggle);
    setupKnob (attack, "attack", "ATTACK", blue, "Fade-in when a key starts", false);
    setupKnob (decay, "decay", "DECAY", blue, "", false);
    setupKnob (sustain, "sustain", "SUSTAIN", blue, "", false);
    setupKnob (release, "release", "RELEASE", blue, "Fade-out after you let go of the key", false);
    setupKnob (velocity, "velocity", "VELOCITY", teal, "How much the key velocity changes the level", false);
    setupKnob (level, "level", "LEVEL", text.withAlpha (0.7f), "Output level", false);
    level.slider.getProperties().set ("bipolar", true);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.status = [this] (bool& lit) -> juce::String { lit = processor.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };

    setSize (1100, 640 + footerH);
    tabs[0].setToggleState (true, juce::dontSendNotification);
    showPage (0);
    wave.refresh();
    startTimerHz (30);
}

VoxChopEditor::~VoxChopEditor()
{
    setLookAndFeel (nullptr);
}

void VoxChopEditor::styleButton (juce::Button& b, juce::Colour textColour)
{
    b.setColour (juce::TextButton::buttonColourId, edge);
    b.setColour (juce::TextButton::buttonOnColourId, accent);
    b.setColour (juce::TextButton::textColourOffId, textColour);
    b.setColour (juce::TextButton::textColourOnId, bg);
}

void VoxChopEditor::setupKnob (Knob& k, const char* id, const char* name, juce::Colour c, const juce::String& tip, bool previewOnRelease)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.setColour (juce::Slider::trackColourId, c);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, text);
    s.setTooltip (tip);
    addChildComponent (s);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, s);
    if (auto* prm = processor.apvts.getParameter (id)) s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    if (previewOnRelease) s.onDragEnd = [this] { processor.previewWhenReady(); };
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addChildComponent (k.label);
}

void VoxChopEditor::setupBox (Box& b, const char* id, const juce::String& tip)
{
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        b.box.addItemList (prm->choices, 1);
    b.box.setTooltip (tip);
    b.box.setColour (juce::ComboBox::backgroundColourId, edge);
    b.box.setColour (juce::ComboBox::outlineColourId, edge);
    b.box.setColour (juce::ComboBox::textColourId, text);
    b.box.setColour (juce::ComboBox::arrowColourId, accent);
    addChildComponent (b.box);
    b.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, b.box);
}

void VoxChopEditor::showPage (int p)
{
    page = p;
    const bool s0 = p == 0, s1 = p == 1, s2 = p == 2;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &wave, &openButton, &captureButton, &rechopButton, &modeBox.box, &chopByBox.box, &gridBox.box,
                                                                        &sens.slider, &sens.label, &tileSlice, &tileAll, &tileMidi, &tileWhole })
        c->setVisible (s0);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &map, &hearButton, &breath.slider, &breath.label, &grit.slider, &grit.label, &robot.slider, &robot.label,
                                                                        &alien.slider, &alien.label, &lowCut.slider, &lowCut.label, &highCut.slider, &highCut.label })
        c->setVisible (s1);
    for (auto* b : presetTiles) b->setVisible (s1);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pads, &slicePitch, &sliceGain, &slicePitchLabel, &sliceGainLabel, &revToggle, &loopToggle, &oneShotToggle,
                                                                        &chokeToggle, &keyModeBox.box, &keepToggle, &attack.slider, &attack.label, &decay.slider, &decay.label,
                                                                        &sustain.slider, &sustain.label, &release.slider, &release.label, &velocity.slider, &velocity.label,
                                                                        &level.slider, &level.label })
        c->setVisible (s2);
    updateSliceControls();
    resized();
    repaint();
}

void VoxChopEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a vocal", processor.sourceFile().getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) processor.loadFile (fc.getResult()); });
}

void VoxChopEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHot = false;
    repaint();
    if (! files.isEmpty()) processor.loadFile (juce::File (files[0]));
}

void VoxChopEditor::dragFiles (const juce::StringArray& paths, juce::Component& from, const juce::String& what)
{
    status = "Dragging " + what + "  (saved in Music / Vox Chop)";
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, &from);
}

void VoxChopEditor::dragOutSlice (int index)
{
    if (processor.renderPending()) { status = "Still working - try again in a moment"; repaint(); return; }
    auto f = processor.exportSlice (index);
    if (f.existsAsFile()) dragFiles ({ f.getFullPathName() }, page == 0 ? (juce::Component&) wave : (juce::Component&) tileSlice, "slice " + juce::String (index + 1));
}

void VoxChopEditor::editSelected (std::function<void (VoxSlice&)> fn)
{
    auto cur = processor.getSlices();
    const int i = processor.selected();
    if (cur == nullptr || i < 0 || i >= (int) cur->size()) return;
    VoxSlices c = *cur;
    fn (c[(size_t) i]);
    processor.setSlices (std::move (c), false);
}

void VoxChopEditor::updateSliceControls()
{
    auto sl = processor.getSlices();
    const int i = processor.selected();
    const bool has = sl != nullptr && i >= 0 && i < (int) sl->size();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &slicePitch, &sliceGain, &revToggle, &loopToggle, &oneShotToggle, &chokeToggle })
        c->setEnabled (has);
    if (! has) return;
    const auto& s = (*sl)[(size_t) i];
    slicePitch.setValue (s.pitch, juce::dontSendNotification);
    sliceGain.setValue (s.gainDb, juce::dontSendNotification);
    revToggle.setToggleState (s.reverse, juce::dontSendNotification);
    loopToggle.setToggleState (s.loop, juce::dontSendNotification);
    oneShotToggle.setToggleState (s.oneShot, juce::dontSendNotification);
    chokeToggle.setToggleState (s.choke, juce::dontSendNotification);
}

void VoxChopEditor::updateTiles()
{
    auto sl = processor.getSlices();
    auto src = processor.getSource();
    const bool whole = (int) param (processor.apvts, "mode") == 1;
    const int n = sl != nullptr ? (int) sl->size() : 0, sel = processor.selected();
    const bool chop = ! whole && n > 0;
    const juce::String arrow = juce::String::fromUTF8 ("\xe2\x87\xa6  ");
    tileSlice.enabled = chop && sel < n;
    tileSlice.title = arrow + (tileSlice.enabled ? "SLICE " + juce::String (sel + 1) + "  (" + noteName (sel) + ")" : juce::String ("SLICE"));
    tileSlice.sub = "drag this one slice into Logic (also: drag a slice in the waveform)";
    tileAll.enabled = chop;
    tileAll.title = arrow + "ALL " + juce::String (n) + " SLICES";
    tileAll.sub = "one audio file per slice";
    tileMidi.enabled = chop && src != nullptr;
    const int bar = src != nullptr ? (int) std::floor (src->startPpq / 4.0 + 1e-9) + 1 : 1;
    tileMidi.title = arrow + "MIDI  (original order)";
    tileMidi.sub = "drop it on this track at bar " + juce::String (bar) + " - the chops replay your phrase. Then rearrange the notes";
    tileWhole.enabled = src != nullptr;
    tileWhole.title = arrow + "WHOLE VOICE";
    tileWhole.sub = "the whole voice with the new VOICE settings, as one file";
    for (auto* t : { &tileSlice, &tileAll, &tileMidi, &tileWhole }) t->repaint();
}

void VoxChopEditor::timerCallback()
{
    if (auto m = processor.takeMessage(); m.isNotEmpty()) { status = m; repaint(); }
    const int v = processor.version.load();
    if (v != seenVersion)
    {
        seenVersion = v;
        wave.refresh();
        updateTiles();
        updateSliceControls();
        repaint();
    }
    // CAPTURE ボタン
    juce::String capText = "CAPTURE";
    juce::Colour capCol = edge;
    if (processor.capturing()) { capText = juce::String::fromUTF8 ("\xe2\x97\x8f REC ") + juce::String (processor.capturedSeconds(), 1) + " s"; capCol = juce::Colour (0xffe0403a); }
    else if (processor.captureArmed()) { capText = "ARMED - PLAY"; capCol = accent.darker (0.3f); }
    if (captureButton.getButtonText() != capText) captureButton.setButtonText (capText);
    captureButton.setColour (juce::TextButton::buttonColourId, capCol);
    const bool grid = (int) param (processor.apvts, "chopBy") == 1;
    gridBox.box.setEnabled (grid);
    sens.slider.setEnabled (! grid);
    if (page == 0) { wave.repaint(); updateTiles(); }
    if (page == 1) map.repaint();
    if (page == 2) pads.repaint();
    repaint (0, 0, getWidth(), 56);
    repaint (content.withTrimmedTop (content.getHeight() - 26));
}

void VoxChopEditor::resized()
{
    layoutFooter();
    auto top = juce::Rectangle<int> (0, 0, getWidth(), 52).reduced (16, 10);
    top.removeFromLeft (170);
    for (auto& t : tabs) { t.setBounds (top.removeFromLeft (100).reduced (2, 2)); top.removeFromLeft (4); }
    content = juce::Rectangle<int> (16, 58, getWidth() - 32, 640 - 58 - 10);
    auto b = content;
    b.removeFromBottom (26);   // 下の 1 行は説明

    if (page == 0)
    {
        auto row = b.removeFromTop (32);
        openButton.setBounds (row.removeFromLeft (70).reduced (0, 2)); row.removeFromLeft (8);
        captureButton.setBounds (row.removeFromLeft (130).reduced (0, 2)); row.removeFromLeft (16);
        modeBox.box.setBounds (row.removeFromLeft (100).reduced (0, 3)); row.removeFromLeft (16);
        chopByBox.box.setBounds (row.removeFromLeft (90).reduced (0, 3)); row.removeFromLeft (8);
        gridBox.box.setBounds (row.removeFromLeft (80).reduced (0, 3)); row.removeFromLeft (16);
        sens.label.setBounds (row.removeFromLeft (42));
        sens.slider.setBounds (row.removeFromLeft (200));
        rechopButton.setBounds (row.removeFromRight (90).reduced (0, 2));
        b.removeFromTop (10);
        auto tiles = b.removeFromBottom (58);
        b.removeFromBottom (10);
        wave.setBounds (b);
        const int tw = (tiles.getWidth() - 3 * 10) / 4;
        for (auto* t : { &tileSlice, &tileAll, &tileMidi, &tileWhole }) { t->setBounds (tiles.removeFromLeft (tw)); tiles.removeFromLeft (10); }
    }
    else if (page == 1)
    {
        map.setBounds (b.removeFromLeft (560));
        b.removeFromLeft (16);
        gMacro = b.removeFromTop (150);
        auto m = gMacro.withTrimmedTop (24).reduced (6, 4);
        const int kw = m.getWidth() / 6;
        for (auto* k : { &breath, &grit, &robot, &alien, &lowCut, &highCut })
        {
            auto col = m.removeFromLeft (kw);
            k->label.setBounds (col.removeFromTop (16));
            k->slider.setBounds (col);
        }
        b.removeFromTop (10);
        gPresets = b;
        auto pr = gPresets.withTrimmedTop (26).reduced (8, 6);
        auto hear = pr.removeFromBottom (34);
        hearButton.setBounds (hear.withSizeKeepingCentre (160, 30));
        pr.removeFromBottom (6);
        const int cols = 4, rows = (presetTiles.size() + cols - 1) / cols;
        const int cw = pr.getWidth() / cols, ch = juce::jmin (34, pr.getHeight() / juce::jmax (1, rows));
        for (int i = 0; i < presetTiles.size(); ++i)
            presetTiles[i]->setBounds (juce::Rectangle<int> (pr.getX() + (i % cols) * cw, pr.getY() + (i / cols) * ch, cw, ch).reduced (3));
    }
    else
    {
        const int side = juce::jmin (b.getHeight(), 480);
        pads.setBounds (b.removeFromLeft (side).withHeight (side));
        b.removeFromLeft (16);
        gSlice = b.removeFromTop (170);
        auto s = gSlice.withTrimmedTop (26).reduced (8, 4);
        auto knobs = s.removeFromLeft (200);
        auto c1 = knobs.removeFromLeft (100), c2 = knobs;
        slicePitchLabel.setBounds (c1.removeFromTop (16)); slicePitch.setBounds (c1);
        sliceGainLabel.setBounds (c2.removeFromTop (16)); sliceGain.setBounds (c2);
        s.removeFromLeft (16);
        const int th = s.getHeight() / 4;
        for (auto* t : { &revToggle, &loopToggle, &oneShotToggle, &chokeToggle }) t->setBounds (s.removeFromTop (th).withWidth (160));
        b.removeFromTop (10);
        gGlobal = b.removeFromTop (130);
        auto gg = gGlobal.withTrimmedTop (26).reduced (8, 4);
        auto left = gg.removeFromLeft (190);
        keyModeBox.box.setBounds (left.removeFromTop (30).reduced (0, 2));
        left.removeFromTop (8);
        keepToggle.setBounds (left.removeFromTop (28));
        const int kw2 = gg.getWidth() / 2;
        for (auto* k : { &velocity, &level })
        {
            auto col = gg.removeFromLeft (kw2);
            k->label.setBounds (col.removeFromTop (16));
            k->slider.setBounds (col);
        }
        b.removeFromTop (10);
        gEnv = b;
        auto e = gEnv.withTrimmedTop (26).reduced (8, 4);
        const int kw3 = e.getWidth() / 4;
        for (auto* k : { &attack, &decay, &sustain, &release })
        {
            auto col = e.removeFromLeft (kw3);
            k->label.setBounds (col.removeFromTop (16));
            k->slider.setBounds (col.withSizeKeepingCentre (col.getWidth(), juce::jmin (col.getHeight(), 110)));
        }
    }
}

void VoxChopEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOX CHOP", 18, 12, 160, 30, juce::Justification::centredLeft);

    // 右上: 今の声
    auto src = processor.getSource();
    juce::String info;
    if (src != nullptr)
    {
        info = src->name + "   " + juce::String ((double) src->audio.size() / src->fs, 1) + " s";
        if (src->an.medianF0 > 0) info << "   voice " << juce::roundToInt (src->an.medianF0) << " Hz";
        if (auto sl = processor.getSlices()) info << "   " << (int) sl->size() << " slices";
    }
    else info = "no voice yet";
    if (processor.busy.load()) info = "working...   " + info;
    g.setColour (accent.withAlpha (0.9f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (info, juce::Rectangle<int> (500, 12, getWidth() - 516, 28), juce::Justification::centredRight, 1, 0.8f);

    auto group = [&] (juce::Rectangle<int> r, const juce::String& name, juce::Colour c)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (c);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, r.removeFromTop (26).reduced (10, 0), juce::Justification::centredLeft);
    };
    juce::String hint;
    if (page == 0)
    {
        hint = (int) param (processor.apvts, "mode") == 1
             ? "WHOLE: set the voice on the VOICE page, then drag WHOLE VOICE into Logic.  Keys play the whole voice (C3 = as is)"
             : "Click a slice = play.  Drag its edge = move the cut.  Double-click = split.  Right-click = delete / join.  Drag a slice = into Logic";
        if (! processor.sidechainConnected() && (processor.captureArmed() || processor.capturing()))
            hint = "CAPTURE: choose the vocal track in the Side Chain menu at the top of this window, then play in Logic";
    }
    else if (page == 1)
    {
        group (gMacro, "CHARACTER", violet);
        group (gPresets, "VOICES  (click to try - it plays the selected slice)", accent);
        hint = "The whole voice is calculated again after each change (a moment).  Small moves sound the most natural";
    }
    else
    {
        const int sel = processor.selected();
        group (gSlice, "SLICE " + juce::String (sel + 1) + "  (" + noteName (sel) + ")", accent);
        group (gGlobal, "KEYS", teal);
        group (gEnv, "ENVELOPE", blue);
        hint = (int) param (processor.apvts, "keyMode") == 1
             ? "MELODIC: the selected slice is on every key (C3 = as is). Play chords for harmonies"
             : "SLICES: slice 1 is C1, slice 2 is C#1 ... Rearrange the notes in Logic's piano roll";
    }
    g.setColour (text.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.5f));
    const auto line = content.withTrimmedTop (content.getHeight() - 22);
    g.drawFittedText (status.isNotEmpty() ? status + "     " + hint : hint, line, juce::Justification::centredLeft, 1, 0.8f);

    if (dropHot)
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRect (getLocalBounds().withTrimmedBottom (footerH));
        g.setColour (accent);
        g.drawRect (getLocalBounds().withTrimmedBottom (footerH), 3);
    }
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）
void VoxChopEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
    f.removeFromRight (6);
    linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));
}
