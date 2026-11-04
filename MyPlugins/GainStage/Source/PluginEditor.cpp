#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff101315), panel (0xff191e22), edge (0xff283038),
                       lime (0xffb8e86b), amber (0xffffc35c), red (0xffff6b5c), text (0xffe9eef1);
    constexpr float meterTop = 0.0f, meterBottom = -60.0f;
}

GainStageEditor::GainStageEditor (GainStageProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    auto knob = [this] (juce::Slider& s, juce::Label& l, const char* name, bool big)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        s.setColour (juce::Slider::rotarySliderFillColourId, big ? lime : lime.withAlpha (0.6f));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
        s.setColour (juce::Slider::thumbColourId, text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, text);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (big ? 16.0f : 12.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, big ? lime : text.withAlpha (0.75f));
        addAndMakeVisible (l);
    };
    knob (gain, gainLabel, "GAIN", true);
    knob (target, targetLabel, "TARGET", false);
    gain.setDoubleClickReturnValue (true, 0.0);
    target.setDoubleClickReturnValue (true, -18.0);
    gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "gain", gain);
    targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "target", target);

    measureBox.addItem ("RMS", 1);
    measureBox.addItem ("PEAK", 2);
    measureBox.setColour (juce::ComboBox::backgroundColourId, edge);
    measureBox.setColour (juce::ComboBox::outlineColourId, edge);
    measureBox.setColour (juce::ComboBox::textColourId, text);
    addAndMakeVisible (measureBox);
    measureAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "measure", measureBox);

    learnButton.setButtonText ("LEARN");
    learnButton.setColour (juce::TextButton::buttonColourId, edge);
    learnButton.setColour (juce::TextButton::textColourOffId, lime);
    learnButton.setTooltip ("Play the track, press LEARN: the gain is set so the level hits TARGET. Press again to finish early");
    learnButton.onClick = [this] { processor.toggleLearn(); };
    addAndMakeVisible (learnButton);

    setupToggle (polarity, "polarity", juce::String::fromUTF8 ("\xc3\x98  POLARITY"));
    setupToggle (mono, "mono", "MONO");
    setupToggle (swap, "swap", "SWAP L/R");

    setSize (600, 400);
    startTimerHz (30);
}

void GainStageEditor::setupToggle (Toggle& t, const char* id, const juce::String& txt)
{
    t.button.setButtonText (txt);
    t.button.setClickingTogglesState (true);
    t.button.setColour (juce::TextButton::buttonColourId, edge);
    t.button.setColour (juce::TextButton::buttonOnColourId, amber);
    t.button.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.6f));
    t.button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

void GainStageEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    b.removeFromTop (40);   // タイトル

    auto bottom = b.removeFromBottom (36);
    const int tw = (bottom.getWidth() - 16) / 3;
    polarity.button.setBounds (bottom.removeFromLeft (tw)); bottom.removeFromLeft (8);
    mono.button.setBounds (bottom.removeFromLeft (tw));     bottom.removeFromLeft (8);
    swap.button.setBounds (bottom);
    b.removeFromBottom (10);
    infoArea = b.removeFromBottom (36);
    b.removeFromBottom (6);

    meterIn = b.removeFromLeft (70);
    b.removeFromLeft (10);
    meterOut = b.removeFromRight (70);
    b.removeFromRight (10);

    // 真ん中：GAIN（大）、その下に LEARN、右に TARGET と RMS/PEAK
    auto right = b.removeFromRight (130);
    targetLabel.setBounds (right.removeFromTop (20));
    target.setBounds (right.removeFromTop (120));
    right.removeFromTop (8);
    measureBox.setBounds (right.removeFromTop (28).reduced (10, 0));

    auto centre = b;
    learnButton.setBounds (centre.removeFromBottom (44).reduced (20, 2));
    centre.removeFromBottom (6);
    gainLabel.setBounds (centre.removeFromTop (22));
    gain.setBounds (centre);
}

void GainStageEditor::timerCallback()
{
    auto& e = processor.engine;
    auto follow = [] (float& shown, float now, float fall) { shown = now > shown ? now : juce::jmax (now, shown - fall); };
    follow (inRms, e.getInRms(), 0.6f);
    follow (outRms, e.getOutRms(), 0.6f);
    inPeak = e.getInPeak();
    outPeak = e.getOutPeak();

    const bool learning = e.isLearning();
    learnButton.setButtonText (learning ? "LEARNING  " + juce::String (e.learnProgress() * 5.0, 1) + " / 5.0 s   (press to finish)"
                                        : juce::String ("LEARN"));
    learnButton.setColour (juce::TextButton::buttonColourId, learning ? lime : edge);
    learnButton.setColour (juce::TextButton::textColourOffId, learning ? juce::Colours::black : lime);

    const auto f = processor.takeFlash();
    if (f.isNotEmpty()) { flashText = f; flashTicks = 120; }
    if (flashTicks > 0) --flashTicks;
    repaint();
}

void GainStageEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, float rmsDb, float peakDb)
{
    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title, r.removeFromTop (18), juce::Justification::centred);
    auto nums = r.removeFromBottom (34);
    auto bar = r.reduced (18, 4);

    g.setColour (panel);
    g.fillRoundedRectangle (bar, 4.0f);
    auto yOf = [&] (float db) { return juce::jmap (juce::jlimit (meterBottom, meterTop, db), meterBottom, meterTop, bar.getBottom(), bar.getY()); };

    // TARGET の線
    const float t = processor.apvts.getRawParameterValue ("target")->load();
    const bool peakMode = processor.apvts.getRawParameterValue ("measure")->load() > 0.5f;

    const float yr = yOf (rmsDb);
    g.setColour (lime.withAlpha (0.85f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getX() + 2, yr, bar.getRight() - 2, bar.getBottom()));
    const float yp = yOf (peakDb);
    g.setColour (peakDb > -0.5f ? red : amber);
    g.fillRect (bar.getX() + 2, yp - 1.0f, bar.getWidth() - 4, 2.0f);

    g.setColour (text.withAlpha (0.9f));
    const float yt = yOf (t);
    g.fillRect (bar.getX() - 6, yt - 1.0f, bar.getWidth() + 12, 2.0f);

    g.setFont (juce::FontOptions (10.0f));
    g.setColour (text.withAlpha (0.4f));
    for (float d : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (bar.getRight() + 2, yOf (d) - 6, 18, 12), juce::Justification::centredLeft);

    auto fmt = [] (float v) { return v > -100.0f ? juce::String (v, 1) : juce::String ("--"); };
    g.setFont (juce::FontOptions (11.0f, ! peakMode));
    g.setColour (lime);
    g.drawText ("RMS " + fmt (rmsDb), nums.removeFromTop (16), juce::Justification::centred);
    g.setFont (juce::FontOptions (11.0f, peakMode));
    g.setColour (amber);
    g.drawText ("PK " + fmt (peakDb), nums, juce::Justification::centred);
}

void GainStageEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("GAIN STAGE", 18, 12, 200, 34, juce::Justification::centredLeft);
    g.setColour (lime.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("set the input level in one click", 170, 12, 260, 34, juce::Justification::centredLeft);

    drawMeter (g, meterIn.toFloat(), "IN", inRms, inPeak);
    drawMeter (g, meterOut.toFloat(), "OUT", outRms, outPeak);

    // 最後の LEARN の結果
    auto r = infoArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setFont (juce::FontOptions (12.0f));
    if (flashTicks > 0) { g.setColour (red); g.drawText (flashText, r.reduced (12, 0), juce::Justification::centredLeft); }
    else
    {
        const auto info = processor.learnInfo();
        g.setColour (info.isEmpty() ? text.withAlpha (0.45f) : text.withAlpha (0.85f));
        g.drawFittedText (info.isEmpty() ? juce::String ("Play the track and press LEARN.  White line = TARGET")
                                         : "LEARNED:  " + info,
                          r.reduced (12, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
}
