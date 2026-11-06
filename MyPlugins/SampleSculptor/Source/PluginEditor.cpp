#include "PluginEditor.h"
#include <cstring>

namespace
{
    const juce::Colour bg (0xff121214), panel (0xff1c1c21), edge (0xff2f2f37),
                       accent (0xffff8a3d), teal (0xff5fd4c4), violet (0xffa98bff), text (0xffeeeef2);

    void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float v)
    {
        if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
}

//==============================================================================
void SculptLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float start, float end, juce::Slider& s)
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
WaveView::Peaks WaveView::makePeaks (const SampleAudio* a, int width)
{
    Peaks p;
    if (a == nullptr || a->length() == 0 || width <= 0) return p;
    p.lo.assign ((size_t) width, 0.0f);
    p.hi.assign ((size_t) width, 0.0f);
    const size_t n = a->length();
    for (int x = 0; x < width; ++x)
    {
        const size_t i0 = n * (size_t) x / (size_t) width, i1 = std::max (i0 + 1, n * (size_t) (x + 1) / (size_t) width);
        float lo = 0, hi = 0;
        for (const auto& ch : a->ch)
            for (size_t i = i0; i < std::min (i1, n); ++i) { lo = std::min (lo, ch[i]); hi = std::max (hi, ch[i]); }
        p.lo[(size_t) x] = lo; p.hi[(size_t) x] = hi;
    }
    return p;
}

juce::Rectangle<float> WaveView::srcLane() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (22.0f).withHeight (64.0f); }
juce::Rectangle<float> WaveView::outLane() const { auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f); r.removeFromTop (22.0f + 64.0f + 26.0f); return r.withTrimmedBottom (8.0f); }
float WaveView::xToFrac (float x) const { auto l = srcLane(); return juce::jlimit (0.0f, 1.0f, (x - l.getX()) / l.getWidth()); }
float WaveView::fracToX (float f) const { auto l = srcLane(); return l.getX() + f * l.getWidth(); }

void WaveView::refresh()
{
    auto s = proc.getSource();
    auto o = proc.getRendered();
    hasSource = s != nullptr;
    srcPeaks = makePeaks (s.get(), (int) srcLane().getWidth());
    outPeaks = makePeaks (o.get(), (int) outLane().getWidth());
    srcSec = s ? (double) s->length() / s->fs : 0.0;
    outSec = o ? (double) o->length() / o->fs : 0.0;
    repaint();
}

void WaveView::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    if (! hasSource)
    {
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("Drop a sample here  (or click)", getLocalBounds().toFloat().withTrimmedBottom (20), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (text.withAlpha (0.35f));
        g.drawText ("kick, snare, vocal, loop, anything - WAV / AIFF / FLAC / MP3 / M4A", getLocalBounds().toFloat().withTrimmedTop (40), juce::Justification::centred);
        return;
    }
    auto drawPeaks = [&] (const Peaks& p, juce::Rectangle<float> r, juce::Colour c)
    {
        const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
        g.setColour (c);
        for (size_t x = 0; x < p.lo.size(); ++x)
        {
            const float y0 = mid - juce::jlimit (0.0f, 1.0f, p.hi[x]) * half, y1 = mid - juce::jlimit (-1.0f, 0.0f, p.lo[x]) * half;
            g.drawVerticalLine ((int) (r.getX() + (float) x), y0, juce::jmax (y0 + 1.0f, y1));
        }
    };
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

    // 元のファイル
    auto sl = srcLane();
    g.setColour (text.withAlpha (0.5f));
    g.drawText ("ORIGINAL  " + juce::String (srcSec, 2) + " s   -  drag the orange lines to cut the start / end",
                juce::Rectangle<float> (sl.getX(), 4, sl.getWidth(), 16), juce::Justification::centredLeft);
    drawPeaks (srcPeaks, sl, text.withAlpha (0.45f));
    const float st = proc.apvts.getRawParameterValue ("start")->load(), en = proc.apvts.getRawParameterValue ("end")->load();
    g.setColour (bg.withAlpha (0.65f));
    g.fillRect (sl.withRight (fracToX (st)));
    g.fillRect (sl.withLeft (fracToX (juce::jmax (en, st))));
    g.setColour (accent);
    for (float f : { st, en })
    {
        const float x = fracToX (f);
        g.fillRect (x - 1.0f, sl.getY(), 2.0f, sl.getHeight());
        g.fillRect (x - 5.0f, sl.getY(), 10.0f, 6.0f);
    }

    // できた音
    auto ol = outLane();
    g.setColour (accent);
    g.drawText (juce::String (proc.busy.load() ? "RESULT  (working...)" : "RESULT  ") + juce::String (outSec, 2) + " s   -  click to play",
                juce::Rectangle<float> (ol.getX(), ol.getY() - 20, ol.getWidth(), 16), juce::Justification::centredLeft);
    g.setColour (edge);
    g.drawHorizontalLine ((int) ol.getCentreY(), ol.getX(), ol.getRight());
    drawPeaks (outPeaks, ol, accent.withAlpha (0.85f));
    const float pp = proc.playPos.load();
    if (pp >= 0.0f)
    {
        g.setColour (text);
        g.fillRect (ol.getX() + pp * ol.getWidth() - 1.0f, ol.getY(), 2.0f, ol.getHeight());
    }
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    const bool nearMarker = hasSource && srcLane().expanded (0, 6).contains (e.position);
    setMouseCursor (nearMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    dragId = nullptr;
    if (! hasSource) { if (onEmptyClick) onEmptyClick(); return; }
    if (srcLane().expanded (0, 6).contains (e.position))
    {
        const float st = proc.apvts.getRawParameterValue ("start")->load(), en = proc.apvts.getRawParameterValue ("end")->load();
        dragId = std::abs (e.position.x - fracToX (st)) <= std::abs (e.position.x - fracToX (en)) ? "start" : "end";
        if (auto* p = proc.apvts.getParameter (dragId)) p->beginChangeGesture();
        mouseDrag (e);
    }
    else if (outLane().contains (e.position))
        proc.play();
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId == nullptr) return;
    float f = xToFrac (e.position.x);
    const float st = proc.apvts.getRawParameterValue ("start")->load(), en = proc.apvts.getRawParameterValue ("end")->load();
    if (std::strcmp (dragId, "start") == 0) f = juce::jmin (f, en - 0.002f);
    else f = juce::jmax (f, st + 0.002f);
    setParam (proc.apvts, dragId, f);
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragId != nullptr)
        if (auto* p = proc.apvts.getParameter (dragId)) p->endChangeGesture();
    dragId = nullptr;
}

//==============================================================================
void ExportTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (enabled ? accent.withAlpha (0.18f) : panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (enabled ? accent : edge);
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    auto t = r.reduced (14.0f, 4.0f);
    g.setColour (enabled ? text : text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (sub, t, juce::Justification::topLeft);
}

//==============================================================================
SampleSculptorEditor::SampleSculptorEditor (SampleSculptorProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    processor.editorOpen = true;
    wave.onEmptyClick = [this] { openChooser(); };
    addAndMakeVisible (wave);

    setupKnob (fadeIn,  "fadein",  "FADE IN",  false, 0.0, violet);
    setupKnob (fadeOut, "fadeout", "FADE OUT", false, 0.0, violet);
    setupKnob (pitch,   "pitch",   "PITCH",    true, 0.0, violet);
    setupKnob (attack,  "attack",  "ATTACK",   true, 0.0, accent);
    setupKnob (snap,    "snap",    "SNAP",     false, 15.0, accent);
    setupKnob (length,  "length",  "LENGTH",   true, 0.0, accent);
    setupKnob (room,    "room",    "ROOM",     true, 0.0, teal);
    setupKnob (size,    "size",    "SIZE",     false, 40.0, teal);
    setupKnob (tone,    "tone",    "TONE",     false, 50.0, teal);
    setupKnob (drive,   "drive",   "DRIVE",    false, 0.0, text.withAlpha (0.7f));
    setupKnob (gain,    "gain",    "GAIN",     true, 0.0, text.withAlpha (0.7f));
    attack.slider.setTooltip ("left = softer start, right = harder start");
    length.slider.setTooltip ("left = shorter, right = longer tail");
    room.slider.setTooltip ("left = remove the room (dry), right = add a room (wet)");
    pitch.slider.setTooltip ("semitones - changes speed and length together, like a sampler");

    reverseBox.addItemList ({ "REVERSE OFF", juce::String::fromUTF8 ("\xe2\x97\x80 SOUND"), juce::String::fromUTF8 ("\xe2\x97\x80 SOUND + ROOM") }, 1);
    reverseBox.setTooltip ("SOUND = play it backwards (room is added after).  SOUND + ROOM = the room swells in before the hit");
    for (auto* c : { &reverseBox })
    {
        c->setColour (juce::ComboBox::backgroundColourId, edge);
        c->setColour (juce::ComboBox::outlineColourId, edge);
        c->setColour (juce::ComboBox::textColourId, text);
        c->setColour (juce::ComboBox::arrowColourId, violet);
    }
    addAndMakeVisible (reverseBox);
    reverseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "reverse", reverseBox);

    for (auto* b : { &openButton, &playButton, &saveButton, &resetButton })
    {
        b->setColour (juce::TextButton::buttonColourId, edge);
        b->setColour (juce::TextButton::textColourOffId, text);
        addAndMakeVisible (*b);
    }
    playButton.setColour (juce::TextButton::textColourOffId, accent);
    openButton.onClick = [this] { openChooser(); };
    playButton.onClick = [this] { if (processor.playPos.load() >= 0.0f) processor.stop(); else processor.play(); };
    saveButton.onClick = [this]
    {
        auto f = processor.exportWav();
        if (f.existsAsFile()) { status = "Saved " + f.getFileName(); f.revealToUser(); }
        else status = "Nothing to save yet";
        repaint();
    };
    resetButton.setTooltip ("Set every knob back to the start (the sample stays)");
    resetButton.onClick = [this]
    {
        for (auto& id : SampleSculptorProcessor::paramIds())
            if (auto* prm = processor.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->getDefaultValue());
    };

    normalize.setColour (juce::ToggleButton::textColourId, text.withAlpha (0.8f));
    normalize.setColour (juce::ToggleButton::tickColourId, accent);
    normalize.setTooltip ("Make the loudest point -0.3 dB (GAIN is added on top)");
    addAndMakeVisible (normalize);
    normalizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "normalize", normalize);

    exportTile.onDragOut = [this]
    {
        auto f = processor.exportWav();
        if (f.existsAsFile())
        {
            status = "Made " + f.getFileName() + "  (in Music / Sample Sculptor)";
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, &exportTile);
        }
    };
    addAndMakeVisible (exportTile);

    setSize (900, 610);
    wave.refresh();
    startTimerHz (30);
}

SampleSculptorEditor::~SampleSculptorEditor()
{
    processor.editorOpen = false;
    setLookAndFeel (nullptr);
}

void SampleSculptorEditor::setupKnob (Knob& k, const char* id, const char* name, bool bipolar, double def, juce::Colour c)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, text);
    s.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (s);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, s);
    s.setDoubleClickReturnValue (true, def);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
}

void SampleSculptorEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", processor.sourceFile().getParentDirectory(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) processor.loadFile (fc.getResult()); });
}

void SampleSculptorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHot = false;
    repaint();
    if (! files.isEmpty()) processor.loadFile (juce::File (files[0]));
}

void SampleSculptorEditor::timerCallback()
{
    processor.pollMessages();
    const int v = processor.version.load();
    if (v != seenVersion)
    {
        seenVersion = v;
        wave.refresh();
        if (processor.message.isNotEmpty()) status = processor.message;
        auto r = processor.getRendered();
        exportTile.enabled = r != nullptr && r->length() > 0;
        const auto name = processor.sourceFile().getFileNameWithoutExtension();
        exportTile.title = exportTile.enabled ? juce::String::fromUTF8 ("\xe2\x87\xa6  ") + name + " sculpted.wav" : juce::String ("no sample yet");
        exportTile.sub = exportTile.enabled ? "drag this box into Logic (a new file is made each time, in Music / Sample Sculptor)" : "drop a sample above first";
        exportTile.repaint();
        repaint();
    }
    playButton.setButtonText (processor.playPos.load() >= 0.0f ? "STOP" : "PLAY");
    wave.repaint();
}

void SampleSculptorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    resetButton.setBounds (top.removeFromRight (70).reduced (0, 3));
    top.removeFromRight (8);
    normalize.setBounds (top.removeFromRight (110));
    top.removeFromRight (8);
    playButton.setBounds (top.removeFromRight (80).reduced (0, 3));
    top.removeFromRight (8);
    openButton.setBounds (top.removeFromRight (70).reduced (0, 3));
    b.removeFromTop (8);

    auto bottom = b.removeFromBottom (54);
    saveButton.setBounds (bottom.removeFromRight (110).reduced (0, 10));
    bottom.removeFromRight (10);
    exportTile.setBounds (bottom.removeFromLeft (520));
    b.removeFromBottom (12);

    wave.setBounds (b.removeFromTop (240));
    b.removeFromTop (12);

    // EDIT 3 / SHAPE 3 / ROOM 3 / OUT 2
    const int gap = 10;
    const float unit = (float) (b.getWidth() - gap * 3) / 11.0f;
    gEdit = b.removeFromLeft ((int) (unit * 3)); b.removeFromLeft (gap);
    gShape = b.removeFromLeft ((int) (unit * 3)); b.removeFromLeft (gap);
    gRoom = b.removeFromLeft ((int) (unit * 3)); b.removeFromLeft (gap);
    gOut = b;

    auto place = [] (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
    {
        area.removeFromTop (24);
        area = area.reduced (4, 6);
        const int w = area.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto col = area.removeFromLeft (w);
            k->label.setBounds (col.removeFromTop (16));
            k->slider.setBounds (col);
        }
    };
    auto e = gEdit;
    reverseBox.setBounds (e.removeFromTop (26).withTrimmedLeft (54).reduced (6, 2));
    place (gEdit, { &fadeIn, &fadeOut, &pitch });
    place (gShape, { &attack, &snap, &length });
    place (gRoom, { &room, &size, &tone });
    place (gOut, { &drive, &gain });
}

void SampleSculptorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("SAMPLE SCULPTOR", 18, 16, 220, 30, juce::Justification::centredLeft);
    const auto f = processor.sourceFile();
    g.setColour (accent.withAlpha (0.9f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (f.existsAsFile() ? f.getFileName() : juce::String ("drop a sample, shape it, drag it back into Logic"),
                      juce::Rectangle<int> (236, 16, 250, 30), juce::Justification::centredLeft, 1, 0.8f);

    auto group = [&] (juce::Rectangle<int> r, const char* name, juce::Colour c)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (c);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, r.removeFromTop (26).reduced (10, 0), juce::Justification::centredLeft);
    };
    group (gEdit, "EDIT", violet);
    group (gShape, "SHAPE", accent);
    group (gRoom, "ROOM", teal);
    group (gOut, "OUT", text.withAlpha (0.6f));

    if (status.isNotEmpty())
    {
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (status, exportTile.getBounds().withX (exportTile.getRight() + 10).withRight (saveButton.getX() - 10),
                          juce::Justification::centredLeft, 2, 0.8f);
    }
    if (dropHot)
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRect (getLocalBounds());
        g.setColour (accent);
        g.drawRect (getLocalBounds(), 3);
    }
}
