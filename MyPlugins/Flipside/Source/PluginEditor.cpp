#include "PluginEditor.h"
#include <thread>

namespace
{
    const juce::Colour bg (0xff111318), panel (0xff1b1e26), edge (0xff2d3240),
                       violet (0xffa98bff), pink (0xffff7ab6), text (0xffeceef4);
}

//==============================================================================
void SlotStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int pattern = (int) proc.apvts.getRawParameterValue ("pattern")->load();
    // 聞こえている枠 = 入力の今の枠の 1 つ前（Logic が遅れを補正するので）
    const int64_t heard = proc.engine.curIndex.load() - 1;
    const float ph = proc.engine.curPhase.load();
    const int64_t base = heard >= 0 ? heard - (heard % 8) : 0;
    const float w = r.getWidth() / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        auto s = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (3.0f, 0.0f);
        const int64_t idx = base + i;
        const bool rev = fl::Engine::reversedSlot (pattern, idx);
        g.setColour (rev ? violet.withAlpha (0.25f) : edge.withAlpha (0.6f));
        g.fillRoundedRectangle (s, 5.0f);
        if (idx == heard)
        {
            g.setColour ((rev ? violet : text).withAlpha (0.55f));
            if (rev) g.fillRoundedRectangle (s.withTrimmedLeft (s.getWidth() * ph), 5.0f);   // 逆向きは右から左へ
            else     g.fillRoundedRectangle (s.withWidth (s.getWidth() * ph), 5.0f);
            g.setColour (text);
            g.drawRoundedRectangle (s, 5.0f, 1.5f);
        }
        g.setColour (rev ? text : text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (rev ? juce::String::fromUTF8 ("\xe2\x97\x80 REV") : juce::String::fromUTF8 ("\xe2\x96\xb6"), s, juce::Justification::centred);
    }
}

void FileTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (hot ? pink.withAlpha (0.25f) : (isResult ? violet.withAlpha (0.18f) : panel.brighter (0.06f)));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (hot ? pink : (isResult ? violet : edge.brighter (0.3f)));
    const float dash[] = { 6.0f, 4.0f };
    juce::Path p; p.addRoundedRectangle (r, 8.0f);
    if (isResult) g.strokePath (p, juce::PathStrokeType (1.5f));
    else
    {
        juce::Path d; juce::PathStrokeType (1.5f).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);
    }
    auto t = r.reduced (12.0f, 6.0f);
    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawFittedText (title, t.removeFromTop (t.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
    g.setColour (text.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (sub, t.toNearestInt(), juce::Justification::centredTop, 2, 0.7f);
}

//==============================================================================
FlipsideEditor::FlipsideEditor (FlipsideProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setupCombo (gridBox, gridLabel, "GRID");
    gridBox.addItemList (FlipsideProcessor::gridNames(), 1);
    gridAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "grid", gridBox);
    setupCombo (patternBox, patternLabel, "REVERSE");
    patternBox.addItemList ({ "EVERY SLICE", "EVERY 2ND", "EVERY 4TH", "OFF" }, 1);
    patternAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "pattern", patternBox);

    setupKnob (fade, "fade", "FADE", 6.0);
    setupKnob (mix, "mix", "MIX", 100.0);
    setupKnob (output, "output", "OUTPUT", 0.0);
    addAndMakeVisible (strip);

    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, text.withAlpha (0.6f));
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    dropZone.title = "Drop an audio file here  (or click to choose)";
    dropZone.sub = "vocals, samples, loops, whole songs - saved reversed as WAV in Music / Flipside";
    dropZone.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file to reverse", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) startReverse (fc.getResult()); });
    };
    addAndMakeVisible (dropZone);

    resultTile.isResult = true;
    resultTile.onDragOut = [this]
    {
        if (lastOut.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ lastOut.getFullPathName() }, false, &resultTile);
    };
    resultTile.onClick = [this] { if (lastOut.existsAsFile()) lastOut.revealToUser(); };
    addChildComponent (resultTile);

    trim.setToggleState ((bool) processor.apvts.state.getProperty ("trim", true), juce::dontSendNotification);
    trim.setColour (juce::ToggleButton::textColourId, text.withAlpha (0.8f));
    trim.setColour (juce::ToggleButton::tickColourId, violet);
    trim.setTooltip ("Cut the silence at both ends, so the reversed sound starts right away");
    trim.onClick = [this] { processor.apvts.state.setProperty ("trim", trim.getToggleState(), nullptr); };
    addAndMakeVisible (trim);

    finder.setColour (juce::TextButton::buttonColourId, edge);
    finder.setColour (juce::TextButton::textColourOffId, text);
    finder.onClick = [this] { if (lastOut.existsAsFile()) lastOut.revealToUser(); else { fl::outputFolder().createDirectory(); fl::outputFolder().revealToUser(); } };
    addAndMakeVisible (finder);

    lastOut = juce::File (processor.apvts.state.getProperty ("lastFile", "").toString());
    if (lastOut.existsAsFile()) showResult();

    setSize (700, 500);
    startTimerHz (30);
}

FlipsideEditor::~FlipsideEditor() = default;   // 作業中に閉じても、結果は SafePointer で捨てられる

void FlipsideEditor::setupKnob (Knob& k, const char* id, const char* name, double def)
{
    auto& s = k.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, violet);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
    s.setColour (juce::Slider::thumbColourId, text);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (s);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, s);
    s.setDoubleClickReturnValue (true, def);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);
}

void FlipsideEditor::setupCombo (juce::ComboBox& b, juce::Label& l, const char* name)
{
    b.setColour (juce::ComboBox::backgroundColourId, edge);
    b.setColour (juce::ComboBox::outlineColourId, edge);
    b.setColour (juce::ComboBox::textColourId, text);
    b.setColour (juce::ComboBox::arrowColourId, violet);
    addAndMakeVisible (b);
    l.setText (name, juce::dontSendNotification);
    l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (l);
}

void FlipsideEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropZone.hot = false;
    dropZone.repaint();
    if (! files.isEmpty()) startReverse (juce::File (files[0]));
}

void FlipsideEditor::startReverse (const juce::File& f)
{
    if (working.exchange (true)) return;
    dropZone.title = "Reversing " + f.getFileName() + " ...";
    dropZone.repaint();
    const bool doTrim = trim.getToggleState();
    juce::Component::SafePointer<FlipsideEditor> safe (this);
    std::thread ([safe, f, doTrim]
    {
        auto r = fl::reverseFile (f, doTrim);
        juce::MessageManager::callAsync ([safe, r]
        {
            if (safe == nullptr) return;
            safe->working = false;
            safe->dropZone.title = "Drop an audio file here  (or click to choose)";
            if (r.ok)
            {
                safe->lastOut = r.out;
                safe->processor.apvts.state.setProperty ("lastFile", r.out.getFullPathName(), nullptr);
                safe->resultTile.sub = r.message + "   -   drag this box into Logic  /  click = show in Finder";
                safe->showResult();
            }
            else
                safe->dropZone.sub = r.message;
            safe->dropZone.repaint();
        });
    }).detach();
}

void FlipsideEditor::showResult()
{
    resultTile.title = juce::String::fromUTF8 ("\xe2\x97\x80  ") + lastOut.getFileName();
    if (resultTile.sub.isEmpty()) resultTile.sub = "drag this box into Logic  /  click = show in Finder";
    resultTile.setVisible (true);
    resultTile.repaint();
}

void FlipsideEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    b.removeFromTop (40);

    livePanel = b.removeFromTop (250);
    b.removeFromTop (12);
    filePanel = b;

    auto lp = livePanel.reduced (12);
    lp.removeFromTop (22);
    auto row = lp.removeFromTop (120);
    auto combos = row.removeFromLeft (200);
    gridLabel.setBounds (combos.removeFromTop (18));
    gridBox.setBounds (combos.removeFromTop (28));
    combos.removeFromTop (12);
    patternLabel.setBounds (combos.removeFromTop (18));
    patternBox.setBounds (combos.removeFromTop (28));
    row.removeFromLeft (16);
    const int kw = row.getWidth() / 3;
    for (auto* k : { &fade, &mix, &output })
    {
        auto col = row.removeFromLeft (kw);
        k->label.setBounds (col.removeFromTop (18));
        k->slider.setBounds (col);
    }
    lp.removeFromTop (10);
    strip.setBounds (lp.removeFromTop (44));
    lp.removeFromTop (6);
    status.setBounds (lp.removeFromTop (20));

    auto fp = filePanel.reduced (12);
    fp.removeFromTop (22);
    auto bottom = fp.removeFromBottom (28);
    trim.setBounds (bottom.removeFromLeft (160));
    finder.setBounds (bottom.removeFromRight (150));
    fp.removeFromBottom (8);
    const int half = (fp.getWidth() - 10) / 2;
    dropZone.setBounds (fp.removeFromLeft (half));
    fp.removeFromLeft (10);
    resultTile.setBounds (fp);
}

void FlipsideEditor::timerCallback()
{
    strip.repaint();
    const float sec = processor.sliceSeconds.load();
    const auto grid = FlipsideProcessor::gridNames()[(int) processor.apvts.getRawParameterValue ("grid")->load()];
    juce::String s;
    if (processor.synced.load())
        s << "Following Logic: " << grid << " = " << juce::String (sec, 2) << " s at " << juce::String (processor.bpmShown.load(), 1)
          << " BPM.  Delay " << juce::String (sec, 2) << " s - Logic lines it up automatically";
    else
        s << "Logic stopped: slicing freely (" << grid << " = " << juce::String (sec, 2) << " s at "
          << juce::String (processor.bpmShown.load(), 1) << " BPM).  Press play to lock to the bars";
    status.setText (s, juce::dontSendNotification);
}

void FlipsideEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("FLIPSIDE", 18, 14, 160, 30, juce::Justification::centredLeft);
    g.setColour (violet);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("reverse anything - live on the grid, or whole files", 136, 14, 360, 30, juce::Justification::centredLeft);

    auto box = [&] (juce::Rectangle<int> r, const char* name, juce::Colour c)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (c);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, r.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    };
    box (livePanel, "LIVE REVERSE  (on this track, in time with the song)", violet);
    box (filePanel, "FILE REVERSE  (make a reversed copy of an audio file)", pink);
}
