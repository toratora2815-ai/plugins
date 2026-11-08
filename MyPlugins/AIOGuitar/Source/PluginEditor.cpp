#include "PluginEditor.h"

namespace Pal
{
    const juce::Colour bg       { 0xff15161a };
    const juce::Colour panel    { 0xff22242b };
    const juce::Colour panelHi  { 0xff2c2f38 };
    const juce::Colour text     { 0xffe8e6e1 };
    const juce::Colour dim      { 0xff8b8d96 };
    const juce::Colour guitar   { 0xffe8a33d };
    const juce::Colour gate     { 0xff9aa5b1 };
    const juce::Colour comp     { 0xff3db8c8 };
    const juce::Colour drive    { 0xff4cc46a };
    const juce::Colour amp      { 0xffe05a47 };
    const juce::Colour cab      { 0xff9b7be0 };
    const juce::Colour reverb   { 0xff5b8def };
    const juce::Colour out      { 0xffd8d8d8 };
}

//==============================================================================
AIOLookAndFeel::AIOLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Pal::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Pal::text);
    setColour (juce::ComboBox::backgroundColourId, Pal::bg);
    setColour (juce::ComboBox::outlineColourId, Pal::panelHi);
    setColour (juce::ComboBox::textColourId, Pal::text);
    setColour (juce::ComboBox::arrowColourId, Pal::dim);
    setColour (juce::PopupMenu::backgroundColourId, Pal::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::panelHi);
    setColour (juce::PopupMenu::textColourId, Pal::text);
}

void AIOLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float arcR = radius - 3.0f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Pal::bg);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (s.isEnabled() ? colour : Pal::dim);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - 7.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d47), centre.x, centre.y - knobR,
                                             juce::Colour (0xff1b1c21), centre.x, centre.y + knobR, false));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobR + 3.0f, 3.0f, knobR * 0.55f, 1.5f);
    g.setColour (Pal::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void AIOLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto colour = b.findColour (juce::ToggleButton::tickColourId);
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? colour.withAlpha (0.18f) : Pal::bg);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? colour : (highlighted ? Pal::dim : Pal::panelHi));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);

    const float d = 8.0f;
    auto led = juce::Rectangle<float> (r.getX() + 8.0f, r.getCentreY() - d * 0.5f, d, d);
    g.setColour (on ? colour : juce::Colour (0xff3a3c44));
    g.fillEllipse (led);
    if (on)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.0f));
    }

    g.setColour (on ? Pal::text : Pal::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    const auto txt = b.getButtonText().isNotEmpty() ? b.getButtonText() : juce::String (on ? "ON" : "OFF");
    g.drawText (txt, r.withTrimmedLeft (20.0f), juce::Justification::centred);
}

//==============================================================================
Section::Section (juce::AudioProcessorValueTreeState& s, juce::String t, juce::Colour c)
    : state (s), title (std::move (t)), colour (c) {}

static std::unique_ptr<juce::Slider> makeKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::Colour colour)
{
    auto k = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
    k->setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->setColour (juce::Slider::textBoxBackgroundColourId, Pal::bg);
    k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->setDoubleClickReturnValue (true, state.getParameter (id)->convertFrom0to1 (state.getParameter (id)->getDefaultValue()));
    return k;
}

static std::unique_ptr<juce::Label> makeLabel (const juce::String& text)
{
    auto l = std::make_unique<juce::Label> ("", text);
    l->setJustificationType (juce::Justification::centred);
    l->setFont (juce::FontOptions (12.0f));
    l->setColour (juce::Label::textColourId, Pal::dim);
    return l;
}

void Section::addKnob (const juce::String& id, const juce::String& labelText)
{
    auto k = makeKnob (state, id, colour);
    addAndMakeVisible (*k);
    knobAtt.push_back (std::make_unique<SA> (state, id, *k));

    auto l = makeLabel (labelText);
    addAndMakeVisible (*l);

    knobs.push_back (std::move (k));
    labels.push_back (std::move (l));
}

void Section::addToggle (const juce::String& id)
{
    toggle = std::make_unique<juce::ToggleButton>();
    toggle->setColour (juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible (*toggle);
    toggleAtt = std::make_unique<BA> (state, id, *toggle);
}

void Section::addCombo (const juce::String& id)
{
    combo = std::make_unique<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        combo->addItemList (p->choices, 1);
    addAndMakeVisible (*combo);
    comboAtt = std::make_unique<CA> (state, id, *combo);
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Pal::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 10.0f, 4.0f, 16.0f, 2.0f);

    g.setColour (Pal::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, juce::Rectangle<float> (22.0f, 6.0f, r.getWidth() - 30.0f, 24.0f),
                juce::Justification::centredLeft);
}

void Section::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto header = r.removeFromTop (26);

    if (toggle)
        toggle->setBounds (header.removeFromRight (64).reduced (0, 1));

    if (combo)
    {
        const int titleW = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (15.0f, juce::Font::bold), title) + 26;
        header.removeFromLeft (titleW);
        header.removeFromRight (6);
        if (header.getWidth() >= 110)
            combo->setBounds (header.reduced (0, 1));
        else
            combo->setBounds (r.removeFromTop (26).reduced (2, 2));
    }

    r.removeFromTop (4);
    if (knobs.empty()) return;
    const int w = r.getWidth() / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto col = r.removeFromLeft (w);
        labels[i]->setBounds (col.removeFromTop (16));
        knobs[i]->setBounds (col.reduced (2, 0));
    }
}

//==============================================================================
static juce::String jp (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }

static void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, const juce::String& title)
{
    g.setColour (Pal::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 10.0f, 4.0f, 16.0f, 2.0f);
    g.setColour (Pal::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 6.0f, r.getWidth() - 30.0f, 24.0f),
                juce::Justification::centredLeft);
}

//==============================================================================
ArticulationPanel::ArticulationPanel (AIOGuitarProcessor& p, juce::Colour c) : proc (p), colour (c)
{
    addKnob ("artBendStart", "Bend Start");
    addKnob ("artBendTime", "Bend Time");
    addKnob ("artHold", "Bend Hold");
    addKnob ("slideRange", "Slide");
    addKnob ("vibRate", "Vib Rate");
    addKnob ("vibDelay", "Vib Delay");
    addKnob ("bendUp", "Wheel Up");
    addKnob ("bendDown", "Wheel Down");
    startTimerHz (15);
}

void ArticulationPanel::addKnob (const juce::String& id, const juce::String& text)
{
    auto k = makeKnob (proc.apvts, id, colour);
    addAndMakeVisible (*k);
    knobAtt.push_back (std::make_unique<SA> (proc.apvts, id, *k));
    auto l = makeLabel (text);
    addAndMakeVisible (*l);
    knobs.push_back (std::move (k));
    labels.push_back (std::move (l));
}

juce::Rectangle<int> ArticulationPanel::tileBounds (int i) const
{
    const int cols = 6, rows = 2;
    const int w = tilesArea.getWidth() / cols, h = tilesArea.getHeight() / rows;
    return { tilesArea.getX() + (i % cols) * w, tilesArea.getY() + (i / cols) * h, w, h };
}

void ArticulationPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintPanel (g, r, colour, "ARTICULATIONS");
    g.setColour (Pal::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (jp (u8"ピアノロールで音を選んで「アーティキュレーション」→ AIO Guitar、またはキースイッチ C0〜B0 を書く。クリックで次の音に試せる"),
                juce::Rectangle<float> (160.0f, 6.0f, r.getWidth() - 170.0f, 24.0f), juce::Justification::centredLeft);

    const int current = proc.getCurrentArticulation();
    for (int i = 0; i < numArticulations; ++i)
    {
        auto t = tileBounds (i).toFloat().reduced (3.0f);
        const bool on = i == current;
        g.setColour (on ? colour.withAlpha (0.22f) : Pal::bg);
        g.fillRoundedRectangle (t, 6.0f);
        g.setColour (on ? colour : Pal::panelHi);
        g.drawRoundedRectangle (t, 6.0f, on ? 1.6f : 1.0f);

        g.setColour (on ? colour : Pal::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::MidiMessage::getMidiNoteName (kKeyswitchLow + i, true, true, 3),
                    t.reduced (7.0f, 5.0f).removeFromTop (14.0f), juce::Justification::topLeft);
        g.setColour (on ? Pal::text : Pal::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (articulationName (i), t.reduced (6.0f, 4.0f).withTrimmedTop (16).toNearestInt(),
                          juce::Justification::centred, 2);
    }

    g.setColour (Pal::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("ARTICULATION SETTINGS", artArea.withHeight (14).toFloat(), juce::Justification::centredLeft);
    g.drawText ("PITCH WHEEL", wheelArea.withHeight (14).toFloat(), juce::Justification::centredLeft);
}

void ArticulationPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromTop (28);
    tilesArea = r.removeFromLeft (r.getWidth() * 50 / 100);
    r.removeFromLeft (14);

    artArea = r.removeFromLeft (r.getWidth() * 3 / 4);
    r.removeFromLeft (8);
    wheelArea = r;

    auto place = [&] (juce::Rectangle<int>& area, size_t i, int w, int h)
    {
        auto col = area.removeFromLeft (w).removeFromTop (h);
        labels[i]->setBounds (col.removeFromTop (14));
        knobs[i]->setBounds (col.reduced (2, 0));
    };
    auto a = artArea.withTrimmedTop (16);
    const int w = a.getWidth() / 3, h = a.getHeight() / 2;
    auto row1 = a.removeFromTop (h), row2 = a;
    for (size_t i = 0; i < 3; ++i) place (row1, i, w, h);
    for (size_t i = 3; i < 6; ++i) place (row2, i, w, h);

    auto b = wheelArea.withTrimmedTop (16);
    auto b1 = b.removeFromTop (h), b2 = b;
    place (b1, 6, b1.getWidth(), h);
    place (b2, 7, b2.getWidth(), h);
}

void ArticulationPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numArticulations; ++i)
        if (tileBounds (i).contains (e.getPosition()))
        {
            proc.selectArticulation (i);
            return;
        }
}

void ArticulationPanel::timerCallback()
{
    const int c = proc.getCurrentArticulation();
    if (c != shown) { shown = c; repaint (tilesArea); }
}

//==============================================================================
SlotCard::SlotCard (AIOGuitarProcessor& p, int s, juce::String t, juce::String h, juce::Colour c)
    : proc (p), slot (s), title (std::move (t)), hint (std::move (h)), colour (c)
{
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Recording (" + juce::String (Kit::slotName (slot)) + ")",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
    };
    clearButton.onClick = [this] { proc.clearSlot (slot); };
    startTimerHz (5);
}

void SlotCard::load (const juce::File& f)
{
    busy = true;
    status = "Analysing " + f.getFileName() + " ...";
    repaint();
    juce::Component::SafePointer<SlotCard> safe (this);
    proc.loadRecording (slot, f, [safe] (juce::String) { if (safe != nullptr) { safe->busy = false; safe->shownVersion = -1; } });
}

bool SlotCard::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a")) return true;
    return false;
}

void SlotCard::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a")) { load (juce::File (f)); break; }
    repaint();
}

void SlotCard::timerCallback()
{
    if (busy || shownVersion == proc.getKitVersion()) return;
    shownVersion = proc.getKitVersion();
    auto kit = proc.getKit();
    count = kit ? kit->count (slot) : 0;
    status = proc.getSlotMessage (slot);
    if (status.isEmpty()) status = count > 0 ? juce::String (count) + (slot == Kit::slotBrush ? " strokes" : " notes") : juce::String();
    clearButton.setEnabled (count > 0);
    repaint();
}

void SlotCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintPanel (g, r, colour, title);
    if (dragOver)
    {
        g.setColour (colour.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.0f);
    }

    auto body = getLocalBounds().reduced (12, 8).withTrimmedTop (24).withTrimmedBottom (30);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Pal::dim);
    g.drawFittedText (hint, body.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);

    // LED + status
    auto st = body.removeFromBottom (30);
    const bool has = count > 0;
    g.setColour (busy ? Pal::guitar : (has ? colour : juce::Colour (0xff3a3c44)));
    g.fillEllipse ((float) st.getX(), (float) st.getY() + 4.0f, 9.0f, 9.0f);
    g.setColour (has || busy ? Pal::text : Pal::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (busy ? status : (status.isNotEmpty() ? status : jp (u8"ここにファイルをドラッグ")),
                      st.withTrimmedLeft (16), juce::Justification::topLeft, 2);
}

void SlotCard::resized()
{
    auto r = getLocalBounds().reduced (12, 8).removeFromBottom (26);
    clearButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (6);
    loadButton.setBounds (r);
}

//==============================================================================
KitCard::KitCard (AIOGuitarProcessor& p, juce::Colour c) : proc (p), colour (c)
{
    name.setEditable (true);
    name.setColour (juce::Label::backgroundColourId, Pal::bg);
    name.setColour (juce::Label::outlineColourId, Pal::panelHi);
    name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    name.onTextChange = [this] { proc.setKitName (name.getText().trim().isEmpty() ? "My Guitar" : name.getText().trim()); };
    addAndMakeVisible (name);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (openButton);
    if (AIOGuitarProcessor::isStandaloneApp())
    {
        guideButton.setColour (juce::TextButton::buttonColourId, Pal::guitar.withAlpha (0.35f));
        guideButton.setTooltip (jp (u8"ギターをつないで、1 音ずつ案内を見ながら録る（アプリだけ）"));
        guideButton.onClick = [this] { if (onGuide) onGuide(); };
        addAndMakeVisible (guideButton);
    }

    saveButton.onClick = [this]
    {
        auto kit = proc.getKit();
        if (kit == nullptr) { message = jp (u8"まだ録音が入っていません"); repaint(); return; }
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("AIO Guitar");
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save guitar", dir.getChildFile (juce::File::createLegalFileName (kit->name) + ".aioguitar"), "*.aioguitar");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  message = proc.saveKit (f.withFileExtension ("aioguitar")) ? "Saved " + f.getFileName() : juce::String ("Could not save.");
                                  repaint();
                              });
    };
    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open guitar", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("AIO Guitar"), "*.aioguitar");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) { message = proc.loadKit (fc.getResult()); repaint(); }
                              });
    };
    startTimerHz (5);
}

void KitCard::timerCallback()
{
    if (shownVersion == proc.getKitVersion()) return;
    shownVersion = proc.getKitVersion();
    auto kit = proc.getKit();
    if (! name.isBeingEdited()) name.setText (kit ? kit->name : juce::String ("My Guitar"), juce::dontSendNotification);
    saveButton.setEnabled (kit != nullptr);
    repaint();
}

void KitCard::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat(), colour, "GUITAR");
    g.setColour (Pal::dim);
    g.setFont (juce::FontOptions (12.0f));
    auto r = getLocalBounds().reduced (12, 8);
    if (! guideButton.isVisible() || message.isNotEmpty())
        g.drawFittedText (message.isNotEmpty() ? message : jp (u8"録音一式を 1 つのファイルに保存・読み込み（Logic のプロジェクトにも自動で入ります）"),
                          r.withTrimmedTop (62).withHeight (34), juce::Justification::topLeft, 2);
}

void KitCard::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (28);
    name.setBounds (r.removeFromTop (26));
    auto b = r.removeFromBottom (26);
    if (guideButton.isVisible()) { r.removeFromBottom (6); guideButton.setBounds (r.removeFromBottom (28)); }
    saveButton.setBounds (b.removeFromLeft (b.getWidth() / 2 - 3));
    b.removeFromLeft (6);
    openButton.setBounds (b);
}

//==============================================================================
StringGrid::StringGrid (AIOGuitarProcessor& p) : proc (p) { startTimerHz (5); }

void StringGrid::timerCallback()
{
    if (shownVersion != proc.getKitVersion()) { shownVersion = proc.getKitVersion(); rebuild(); repaint(); }
}

void StringGrid::rebuild()
{
    for (auto& row : cells) for (auto& c : row) c = {};
    auto kit = proc.getKit();
    summary = {};
    if (kit == nullptr || ! kit->hasNotes()) return;

    float maxCents = 0, lo = 1e9f, hi = -1e9f;
    int n = 0, noString = 0;
    for (auto& s : kit->samples)
    {
        if (s.slot == Kit::slotBrush) continue;
        ++n;
        maxCents = juce::jmax (maxCents, std::abs (s.cents));
        if (s.rawRms > 0 && s.slot == Kit::slotSoft) { const float db = juce::Decibels::gainToDecibels (s.rawRms); lo = juce::jmin (lo, db); hi = juce::jmax (hi, db); }
        if (s.string >= 1 && s.fret >= 0 && s.fret < frets)
        {
            auto& c = cells[s.string][s.fret];
            const int layer = s.slot == Kit::slotSoft ? 0 : (s.slot == Kit::slotHard ? 1 : 2);
            c.rec[layer] = true;
            c.any = true;
            c.cents = s.cents;
        }
        else ++noString;
    }

    // where every note comes from (soft layer, else hard, else mute)
    const int layer = kit->count (Kit::slotSoft) > 0 ? Kit::slotSoft : (kit->count (Kit::slotHard) > 0 ? Kit::slotHard : Kit::slotMute);
    for (int s = 1; s <= 6; ++s)
        for (int f = 0; f < frets; ++f)
        {
            int cand[8];
            const int note = kOpenString[s] + f;
            if (Kit::candidates (*kit, layer, note, s, cand, 8) > 0)
            {
                const auto& src = kit->samples[(size_t) cand[0]];
                cells[s][f].srcString = src.string;
                cells[s][f].srcFret = src.fret;
                cells[s][f].shift = note - src.rootNote;
            }
        }

    summary = juce::String (n) + " notes  |  " + jp (u8"音程の補正 最大 ") + juce::String (juce::roundToInt (maxCents)) + jp (u8" セント")
            + (hi > lo ? "  |  " + jp (u8"弱いピッキングの音量差 ") + juce::String (hi - lo, 1) + jp (u8" dB → そろえた") : juce::String());
    if (noString > 0) summary << "  |  " << noString << jp (u8" 音は弦が分からず（録る順番を確認）");
}

juce::Rectangle<float> StringGrid::board() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (44.0f).withTrimmedLeft (34.0f).withTrimmedBottom (14.0f);
}

juce::Rectangle<float> StringGrid::cellRect (int string, int fret) const
{
    auto b = board();
    const float w = b.getWidth() / frets, h = b.getHeight() / 6.0f;
    return { b.getX() + fret * w, b.getY() + (string - 1) * h, w, h };
}

juce::Point<int> StringGrid::cellAt (juce::Point<float> p) const
{
    auto b = board();
    if (! b.contains (p)) return {};
    const int f = juce::jlimit (0, frets - 1, (int) ((p.x - b.getX()) / (b.getWidth() / frets)));
    const int s = juce::jlimit (1, 6, 1 + (int) ((p.y - b.getY()) / (b.getHeight() / 6.0f)));
    return { f, s };
}

juce::String StringGrid::describe (int s, int f) const
{
    const auto& c = cells[s][f];
    const int note = kOpenString[s] + f;
    juce::String t = juce::String (s) + jp (u8"弦 ") + juce::String (f) + "F  (" + juce::MidiMessage::getMidiNoteName (note, true, true, 3) + ")  ";
    if (c.any)
    {
        t << jp (u8"録音あり");
        if (c.rec[0]) t << " soft";
        if (c.rec[1]) t << " hard";
        if (c.rec[2]) t << " mute";
        t << jp (u8"、音程を ") << juce::String (-c.cents, 0) << jp (u8" セント直した");
    }
    else if (c.srcFret >= 0 || c.srcString > 0)
    {
        t << "<- " << (c.srcString > 0 ? juce::String (c.srcString) + jp (u8"弦 ") : juce::String()) << juce::String (c.srcFret) << jp (u8"F の録音を ")
          << (c.shift > 0 ? "+" : "") << juce::String (c.shift) << jp (u8" 半音");
    }
    else t << jp (u8"録音なし");
    return t + jp (u8"  （クリックで試聴）");
}

void StringGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintPanel (g, r, Pal::guitar, "STRINGS & FRETS");
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (hover.y > 0 ? Pal::text : Pal::dim);
    g.drawText (hover.y > 0 ? describe (hover.y, hover.x)
                            : (summary.isNotEmpty() ? summary : jp (u8"録音を入れると、録った場所（●）と、ほかの音をどの録音から作るかがここに出ます")),
                juce::Rectangle<float> (180.0f, 6.0f, r.getWidth() - 190.0f, 24.0f), juce::Justification::centredLeft);

    auto b = board();
    // fret numbers + inlays
    g.setFont (juce::FontOptions (10.0f));
    for (int f = 0; f < frets; ++f)
    {
        auto cr = cellRect (1, f);
        g.setColour (Pal::dim);
        g.drawText (juce::String (f), juce::Rectangle<float> (cr.getX(), b.getY() - 14.0f, cr.getWidth(), 12.0f), juce::Justification::centred);
        if (f == 3 || f == 5 || f == 7 || f == 9 || f == 15 || f == 17 || f == 19 || f == 21 || f == 12)
        {
            g.setColour (Pal::panelHi);
            g.fillEllipse (cr.getCentreX() - 3.0f, b.getBottom() + 4.0f, 6.0f, 6.0f);
        }
    }
    for (int s = 1; s <= 6; ++s)
    {
        auto lr = cellRect (s, 0);
        g.setColour (Pal::dim);
        g.drawText (juce::String (s) + " " + juce::MidiMessage::getMidiNoteName (kOpenString[s], false, false, 3),
                    juce::Rectangle<float> (b.getX() - 34.0f, lr.getY(), 30.0f, lr.getHeight()), juce::Justification::centredRight);
    }

    // cells: shade = how far the note is from its recording
    for (int s = 1; s <= 6; ++s)
        for (int f = 0; f < frets; ++f)
        {
            auto cr = cellRect (s, f).reduced (1.0f);
            const auto& c = cells[s][f];
            juce::Colour fill = Pal::bg;
            if (c.any) fill = Pal::guitar.withAlpha (0.25f);
            else if (c.srcFret >= 0) fill = Pal::guitar.withAlpha (juce::jmax (0.03f, 0.16f - 0.04f * (float) std::abs (c.shift)));
            if (hover == juce::Point<int> (f, s)) fill = fill.brighter (0.4f);
            g.setColour (fill);
            g.fillRect (cr);
            if (s == hlString && f == hlFret)
            {
                g.setColour ((hlRec ? juce::Colour (0xffff3b30) : Pal::guitar).withAlpha (0.5f));
                g.fillRect (cr);
                g.setColour (hlRec ? juce::Colour (0xffff3b30) : Pal::text);
                g.drawRect (cr.expanded (1.5f), 2.0f);
            }

            const float d = juce::jmin (cr.getWidth(), cr.getHeight()) * 0.42f;
            const juce::Colour lc[3] { Pal::guitar, Pal::amp, Pal::reverb };
            int k = 0;
            for (int L = 0; L < 3; ++L)
                if (c.rec[L])
                {
                    g.setColour (lc[L]);
                    g.fillEllipse (cr.getX() + 3.0f + (float) k * (d * 0.75f), cr.getCentreY() - d * 0.5f, d, d);
                    ++k;
                }
        }
    // nut + strings
    g.setColour (Pal::text.withAlpha (0.7f));
    g.fillRect (cellRect (1, 1).getX() - 1.5f, b.getY(), 3.0f, b.getHeight());
    for (int s = 1; s <= 6; ++s)
    {
        auto cr = cellRect (s, 0);
        g.setColour (Pal::dim.withAlpha (0.5f));
        g.fillRect (b.getX(), cr.getCentreY() - 0.5f * (0.6f + 0.25f * (float) s), b.getWidth(), 0.6f + 0.25f * (float) s);
    }

    // legend
    g.setFont (juce::FontOptions (11.0f));
    auto lg = juce::Rectangle<float> (b.getX(), b.getBottom() + 1.0f, b.getWidth(), 12.0f);
    const juce::Colour lc[3] { Pal::guitar, Pal::amp, Pal::reverb };
    const char* ln[3] { "soft", "hard", "mute" };
    float x = lg.getRight() - 180.0f;
    for (int L = 0; L < 3; ++L)
    {
        g.setColour (lc[L]);
        g.fillEllipse (x, lg.getY() + 2.0f, 8.0f, 8.0f);
        g.setColour (Pal::dim);
        g.drawText (ln[L], juce::Rectangle<float> (x + 11.0f, lg.getY(), 45.0f, 12.0f), juce::Justification::centredLeft);
        x += 60.0f;
    }
}

void StringGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto c = cellAt (e.position);
    if (c != hover) { hover = c; repaint(); }
}

void StringGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto c = cellAt (e.position);
    if (c.y < 1) return;
    if (e.mods.isPopupMenu() && onRedo) { onRedo (c.y, c.x); return; }
    pressed = c;
    pressedNote = kOpenString[c.y] + c.x;
    proc.previewNote (pressedNote, c.y, true);
}

void StringGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressedNote >= 0) proc.previewNote (pressedNote, pressed.y, false);
    pressedNote = -1;
}

//==============================================================================
PlaySettings::PlaySettings (AIOGuitarProcessor& p, juce::Colour c) : colour (c)
{
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("stringMode")))
        stringMode.addItemList (ch->choices, 1);
    addAndMakeVisible (stringMode);
    stringModeAtt = std::make_unique<CA> (p.apvts, "stringMode", stringMode);
    stringLabel.setFont (juce::FontOptions (12.0f));
    stringLabel.setColour (juce::Label::textColourId, Pal::dim);
    addAndMakeVisible (stringLabel);
    for (auto* t : { &tune, &level }) { t->setColour (juce::ToggleButton::tickColourId, colour); addAndMakeVisible (*t); }
    tuneAtt = std::make_unique<BA> (p.apvts, "kitTune", tune);
    levelAtt = std::make_unique<BA> (p.apvts, "kitLevel", level);
    tune.setTooltip (jp (u8"録音の音程のずれを 1 音ずつ測って直す"));
    level.setTooltip (jp (u8"録音ごとの音量差をそろえる"));

    strum.setColour (juce::ToggleButton::tickColourId, colour);
    strum.setTooltip (jp (u8"和音を弦の順にずらして鳴らす（表拍はダウン、裏はアップ。SCORE で和音ごとに変えられる）"));
    addAndMakeVisible (strum);
    strumAtt = std::make_unique<BA> (p.apvts, "strumOn", strum);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("upStrings")))
        upStrings.addItemList (ch->choices, 1);
    addAndMakeVisible (upStrings);
    upAtt = std::make_unique<CA> (p.apvts, "upStrings", upStrings);
    upStrings.setTooltip (jp (u8"アップストロークで鳴らす弦の数（上から）。外した低い弦は前の音が鳴ったまま"));
    upLabel.setFont (juce::FontOptions (12.0f));
    upLabel.setColour (juce::Label::textColourId, Pal::dim);
    addAndMakeVisible (upLabel);

    const std::pair<const char*, const char*> ks[] { { "strumSpeed", "Strum Speed" }, { "kitVelSens", "Velocity" }, { "kitHuman", "Humanize" },
                                                     { "gtMute", "Palm Mute" }, { "gtRelease", "Release" }, { "gtVolume", "Volume" } };
    for (auto& [id, text] : ks)
    {
        auto k = makeKnob (p.apvts, id, colour);
        addAndMakeVisible (*k);
        knobAtt.push_back (std::make_unique<SA> (p.apvts, id, *k));
        auto l = makeLabel (text);
        addAndMakeVisible (*l);
        knobs.push_back (std::move (k));
        labels.push_back (std::move (l));
    }
}

void PlaySettings::paint (juce::Graphics& g) { paintPanel (g, getLocalBounds().toFloat(), colour, "PLAYING"); }

void PlaySettings::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    r.removeFromTop (28);
    auto left = r.removeFromLeft (190);
    stringLabel.setBounds (left.removeFromTop (16));
    stringMode.setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    auto row = left.removeFromTop (24);
    tune.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
    row.removeFromLeft (4);
    level.setBounds (row);
    r.removeFromLeft (14);
    auto mid = r.removeFromLeft (150);
    strum.setBounds (mid.removeFromTop (26).withWidth (110));
    mid.removeFromTop (6);
    upLabel.setBounds (mid.removeFromTop (16));
    upStrings.setBounds (mid.removeFromTop (24));
    r.removeFromLeft (8);
    const int w = r.getWidth() / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto col = r.removeFromLeft (w);
        labels[i]->setBounds (col.removeFromTop (14));
        knobs[i]->setBounds (col.reduced (2, 0));
    }
}

//==============================================================================
MyGuitarPage::MyGuitarPage (AIOGuitarProcessor& p)
    : soft  (p, Kit::slotSoft,  "SOFT PICKING", jp (u8"弱く弾いた単音。カポ 0・3・6・9・12（1〜3 弦は 15 も）で、6 弦→1 弦、各弦ダウン→アップ。低い弦は 6〜8 秒伸ばす"), Pal::guitar),
      hard  (p, Kit::slotHard,  "HARD PICKING", jp (u8"強く弾いた単音（あとからで OK）。録り方は SOFT と同じ。強い打ち込み（ベロシティ 80 以上）で鳴る"), Pal::amp),
      mute  (p, Kit::slotMute,  "PALM MUTE",    jp (u8"ブリッジミュートの単音。6・5・4 弦を 0・3・6・9 で、6→4 弦、各弦ダウン→アップ"), Pal::reverb),
      brush (p, Kit::slotBrush, "BRUSHING",     jp (u8"全弦ミュートのストローク。ダウン、アップ…と交互に 8 回ほど、少し間を空けて。C1 = ダウン、C#1 = アップ"), Pal::cab),
      kit (p, Pal::guitar), grid (p), settings (p, Pal::guitar), articulations (p, Pal::drive), guide (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &soft, &hard, &mute, &brush, &kit, &grid, &settings, &articulations })
        addAndMakeVisible (*c);
    addChildComponent (guide);
    kit.onGuide = [this] { showGuide (true); };
    guide.onClose = [this] { showGuide (false); };
    guide.onHighlight = [this] (int s, int f, bool rec) { grid.setHighlight (s, f, rec); };
    if (AIOGuitarProcessor::isStandaloneApp())
        grid.onRedo = [this] (int s, int f)
        {
            juce::PopupMenu m;
            m.addSectionHeader (juce::String (s) + jp (u8"弦 ") + juce::String (f) + jp (u8"F を録る（ダウンとアップ）"));
            m.addItem (1, "SOFT PICKING");
            m.addItem (2, "HARD PICKING");
            m.addItem (3, "PALM MUTE");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, s, f] (int r)
            {
                if (r <= 0) return;
                showGuide (true);
                guide.startSingle (r - 1, s, f);
            });
        };
}

void MyGuitarPage::showGuide (bool show)
{
    guideShown = show;
    if (! show) guide.stop();
    guide.setVisible (show);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &soft, &hard, &mute, &brush, &kit, &settings, &articulations })
        c->setVisible (! show);
    resized();
}

void MyGuitarPage::resized()
{
    auto r = getLocalBounds();
    const int gap = 8;
    auto row1 = r.removeFromTop (170); r.removeFromTop (gap);
    const int kitW = 230;
    kit.setBounds (row1.removeFromRight (kitW)); row1.removeFromRight (gap);
    const int w = (row1.getWidth() - 3 * gap) / 4;
    for (auto* c : { &soft, &hard, &mute, &brush }) { c->setBounds (row1.removeFromLeft (w)); row1.removeFromLeft (gap); }
    grid.setBounds (r.removeFromTop (214)); r.removeFromTop (gap);
    settings.setBounds (r.removeFromTop (118)); r.removeFromTop (gap);
    articulations.setBounds (r);
    if (guideShown)   // the guide on top, the strings & frets table below it
    {
        auto all = getLocalBounds();
        grid.setBounds (all.removeFromBottom (240));
        all.removeFromBottom (gap);
        guide.setBounds (all);
    }
}

//==============================================================================
AIOGuitarEditor::AIOGuitarEditor (AIOGuitarProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      myGuitarPage (p), scorePage (p), effectsPage (p),
      amp    (p.apvts, "AMP",          Pal::amp),
      cab    (p.apvts, "CAB",          Pal::cab),
      reverb (p.apvts, "REVERB / OUT", Pal::reverb)
{
    setLookAndFeel (&lnf);

    amp.addToggle ("ampOn");
    amp.addCombo ("ampChannel");
    amp.addKnob ("ampGain", "Gain");
    amp.addKnob ("ampBass", "Bass");
    amp.addKnob ("ampMid", "Middle");
    amp.addKnob ("ampTreble", "Treble");
    amp.addKnob ("ampPresence", "Presence");
    amp.addKnob ("ampMaster", "Master");

    cab.addToggle ("cabOn");
    cab.addCombo ("cabType");
    cab.addKnob ("cabMic", "Mic Tone");

    reverb.addKnob ("revMix", "Mix");
    reverb.addKnob ("revSize", "Size");
    reverb.addKnob ("outGain", "Output");

    for (auto* c : { &amp, &cab, &reverb }) ampPage.addAndMakeVisible (*c);
    ampNote.setText (jp (u8"仮の画面です。アンプ・キャビ・マイクの種類と位置まで選べる画面に作り直します（AmpliTube・Amp Designer を測って合わせ込み中）"), juce::dontSendNotification);
    ampNote.setColour (juce::Label::textColourId, Pal::dim);
    ampNote.setFont (juce::FontOptions (13.0f));
    ampPage.addAndMakeVisible (ampNote);

    addAndMakeVisible (myGuitarPage);
    addChildComponent (scorePage);
    addChildComponent (effectsPage);
    addChildComponent (ampPage);

    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setClickingTogglesState (false);
        tabs[i].setColour (juce::TextButton::buttonColourId, Pal::panel);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, Pal::guitar);
        tabs[i].setColour (juce::TextButton::textColourOffId, Pal::dim);
        tabs[i].setColour (juce::TextButton::textColourOnId, Pal::bg);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
    }

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTooltip (jp (u8"内蔵の音作り（EFFECTS と AMP だけが変わる。録音・演奏・譜面の設定はそのまま）"));
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    linkBar.status = [this] (bool& lit) -> juce::String { lit = proc.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };

    showPage (0);
    setSize (1100, 860);
    startTimerHz (4);
}

AIOGuitarEditor::~AIOGuitarEditor()
{
    setLookAndFeel (nullptr);
}

void AIOGuitarEditor::showPage (int i)
{
    page = i;
    myGuitarPage.setVisible (i == 0);
    scorePage.setVisible (i == 1);
    effectsPage.setVisible (i == 2);
    ampPage.setVisible (i == 3);
    for (int k = 0; k < 4; ++k) tabs[k].setToggleState (k == i, juce::dontSendNotification);
    if (i == 1) scorePage.grabKeyboardFocus();
}

void AIOGuitarEditor::timerCallback()
{
    const int id = proc.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void AIOGuitarEditor::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg);
    g.setColour (Pal::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("AIO GUITAR", 18, 10, 200, 32, juce::Justification::centredLeft);
    g.setColour (Pal::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("my guitar  >  score  >  effects  >  amp", 180, 10, 240, 32, juce::Justification::centredLeft);
}

void AIOGuitarEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (52).reduced (12, 10);
    presetBox.setBounds (top.removeFromRight (210));
    top.removeFromRight (16);
    for (int i = 3; i >= 0; --i) { tabs[i].setBounds (top.removeFromRight (110)); top.removeFromRight (4); }

    // 画面のいちばん下（Plugin Link と プリセット・取り消し）
    auto f = r.removeFromBottom (40).reduced (12, 0);
    presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
    f.removeFromRight (10);
    linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));

    auto page = r.reduced (12, 0).withTrimmedBottom (6);
    myGuitarPage.setBounds (page);
    scorePage.setBounds (page);
    effectsPage.setBounds (page);
    ampPage.setBounds (page);

    {
        auto a = ampPage.getLocalBounds();
        ampNote.setBounds (a.removeFromTop (28));
        auto row = a.removeFromTop (200);
        amp.setBounds (row); a.removeFromTop (8);
        auto row2 = a.removeFromTop (200);
        reverb.setBounds (row2.removeFromRight (360)); row2.removeFromRight (8);
        cab.setBounds (row2);
    }
}
