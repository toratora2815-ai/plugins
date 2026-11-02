#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff120f16), panel (0xff1b1621), edge (0xff2c2433),
                       rose (0xffe58fb4), plum (0xffb59cff), text (0xffefe9f3);

    constexpr int points = vt::TamerEngine::displayPoints;
    constexpr float specTop = 0.0f, specBottom = -90.0f;   // 入力スペクトルの表示範囲
    constexpr float grRange = 24.0f;                        // 下げた量の表示範囲（上から下へ）
}

VelvetTamerEditor::VelvetTamerEditor (VelvetTamerProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (depth, "depth", "DEPTH", true);
    setupKnob (sharpness, "sharpness", "SHARPNESS");
    setupKnob (selectivity, "selectivity", "SELECTIVITY");
    setupKnob (attack, "attack", "ATTACK");
    setupKnob (release, "release", "RELEASE");
    setupKnob (low, "low", "LOW");
    setupKnob (high, "high", "HIGH");
    setupKnob (mix, "mix", "MIX");
    setupKnob (output, "output", "OUTPUT");

    setupToggle (delta, "delta", "DELTA");
    setupToggle (link, "link", "LINK");
    delta.button.setTooltip ("Listen only to what is being removed");
    link.button.setTooltip ("Reduce both channels by the same amount");

    for (int i = 0; i < points; ++i) { shownSpec[i] = specBottom; shownGr[i] = 0.0f; }

    setSize (980, 600);
    startTimerHz (30);
}

void VelvetTamerEditor::setupKnob (Knob& k, const char* id, const char* name, bool big)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 76, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, big ? rose : plum);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, edge);
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (big ? 15.0f : 12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, big ? rose : plum);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
}

void VelvetTamerEditor::setupToggle (Toggle& t, const char* id, const juce::String& txt)
{
    t.button.setButtonText (txt);
    t.button.setClickingTogglesState (true);
    t.button.setColour (juce::TextButton::buttonColourId, edge);
    t.button.setColour (juce::TextButton::buttonOnColourId, rose);
    t.button.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.6f));
    t.button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

juce::Rectangle<int> VelvetTamerEditor::tileContent (const Tile& t) const
{
    return t.bounds.reduced (8).withTrimmedTop (24);
}

void VelvetTamerEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (18));
    k.slider.setBounds (r.reduced (2));
}

void VelvetTamerEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto header = area.removeFromTop (44);
    link.button.setBounds (header.removeFromRight (90).reduced (2, 8));
    header.removeFromRight (6);
    delta.button.setBounds (header.removeFromRight (90).reduced (2, 8));
    area.removeFromTop (8);

    const int gap = 12;
    auto bottom = area.removeFromBottom (190);
    area.removeFromBottom (gap);
    tDisplay.bounds = area;
    displayArea = tileContent (tDisplay);

    // 下段：AMOUNT 1、ほか 2 つずつ
    const float unit = (float) (bottom.getWidth() - 4 * gap) / 9.0f;
    tAmount.bounds = bottom.removeFromLeft ((int) unit);      bottom.removeFromLeft (gap);
    tDetect.bounds = bottom.removeFromLeft ((int) (unit * 2)); bottom.removeFromLeft (gap);
    tTime.bounds = bottom.removeFromLeft ((int) (unit * 2));   bottom.removeFromLeft (gap);
    tFocus.bounds = bottom.removeFromLeft ((int) (unit * 2));  bottom.removeFromLeft (gap);
    tOut.bounds = bottom;

    placeKnob (depth, tileContent (tAmount));
    auto pair = [this] (const Tile& tile, Knob& a, Knob& b)
    {
        auto r = tileContent (tile);
        placeKnob (a, r.removeFromLeft (r.getWidth() / 2));
        placeKnob (b, r);
    };
    pair (tDetect, sharpness, selectivity);
    pair (tTime, attack, release);
    pair (tFocus, low, high);
    pair (tOut, mix, output);
}

void VelvetTamerEditor::timerCallback()
{
    processor.engine.copyDisplay (spec, gr);
    for (int i = 0; i < points; ++i)
    {
        // 上がるときはすぐ、下がるときはゆっくり（見やすさのため）
        shownSpec[i] = spec[i] > shownSpec[i] ? spec[i] : shownSpec[i] + (spec[i] - shownSpec[i]) * 0.15f;
        shownGr[i] = gr[i] < shownGr[i] ? gr[i] : shownGr[i] + (gr[i] - shownGr[i]) * 0.2f;
    }
    const float m = processor.engine.getMaxReductionDb();
    shownMax = m < shownMax ? m : shownMax + (m - shownMax) * 0.1f;
    repaint (tDisplay.bounds);
}

float VelvetTamerEditor::freqToX (float hz, juce::Rectangle<float> r) const
{
    const float lo = vt::TamerEngine::displayLowHz, hi = vt::TamerEngine::displayHighHz;
    return r.getX() + r.getWidth() * std::log (hz / lo) / std::log (hi / lo);
}

void VelvetTamerEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VELVET TAMER", 18, 14, 220, 36, juce::Justification::centredLeft);
    g.setColour (rose.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("resonance suppressor", 196, 14, 200, 36, juce::Justification::centredLeft);

    for (auto* t : { &tDisplay, &tAmount, &tDetect, &tTime, &tFocus, &tOut })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (t->bounds.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (t->bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (t->title, t->bounds.reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);
    }

    // いちばん深く下げている量
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (shownMax < -0.5f ? rose : text.withAlpha (0.4f));
    g.drawText ("MAX  " + juce::String (shownMax, 1) + " dB",
                tDisplay.bounds.reduced (12, 8).removeFromTop (22), juce::Justification::centredRight);

    drawDisplay (g, displayArea.toFloat());
}

void VelvetTamerEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    auto plot = r.reduced (4.0f).withTrimmedBottom (16.0f);

    // 周波数の目盛り
    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f, plot);
        g.setColour (edge);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (text.withAlpha (0.4f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), juce::Justification::centred);
    }
    // 下げた量の目盛り（右端）
    for (float d : { 6.0f, 12.0f, 18.0f })
    {
        const float y = plot.getY() + plot.getHeight() * d / grRange;
        g.setColour (edge.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (rose.withAlpha (0.45f));
        g.drawText ("-" + juce::String ((int) d), juce::Rectangle<float> (plot.getRight() - 26, y - 12, 24, 12),
                    juce::Justification::centredRight);
    }

    // FOCUS の外を暗くする
    {
        const float lo = processor.apvts.getRawParameterValue ("low")->load();
        const float hi = processor.apvts.getRawParameterValue ("high")->load();
        const float x0 = juce::jlimit (plot.getX(), plot.getRight(), freqToX (juce::jmin (lo, hi), plot));
        const float x1 = juce::jlimit (plot.getX(), plot.getRight(), freqToX (juce::jmax (lo, hi), plot));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (plot.withRight (x0));
        g.fillRect (plot.withLeft (x1));
        g.setColour (plum.withAlpha (0.5f));
        g.drawVerticalLine ((int) x0, plot.getY(), plot.getBottom());
        g.drawVerticalLine ((int) x1, plot.getY(), plot.getBottom());
    }

    auto xAt = [&] (int i) { return plot.getX() + plot.getWidth() * ((float) i + 0.5f) / (float) points; };

    // 入力のスペクトル（下から）
    juce::Path specPath;
    specPath.startNewSubPath (plot.getX(), plot.getBottom());
    for (int i = 0; i < points; ++i)
    {
        const float v = juce::jlimit (specBottom, specTop, shownSpec[i]);
        specPath.lineTo (xAt (i), juce::jmap (v, specBottom, specTop, plot.getBottom(), plot.getY()));
    }
    specPath.lineTo (plot.getRight(), plot.getBottom());
    specPath.closeSubPath();
    g.setColour (plum.withAlpha (0.18f));
    g.fillPath (specPath);
    g.setColour (plum.withAlpha (0.7f));
    g.strokePath (specPath, juce::PathStrokeType (1.2f));

    // 下げている量（上から垂れ下がる）
    juce::Path grPath;
    grPath.startNewSubPath (plot.getX(), plot.getY());
    for (int i = 0; i < points; ++i)
    {
        const float v = juce::jlimit (0.0f, grRange, -shownGr[i]);
        grPath.lineTo (xAt (i), plot.getY() + plot.getHeight() * v / grRange);
    }
    grPath.lineTo (plot.getRight(), plot.getY());
    grPath.closeSubPath();
    g.setColour (rose.withAlpha (0.35f));
    g.fillPath (grPath);
    g.setColour (rose);
    g.strokePath (grPath, juce::PathStrokeType (1.6f));
}
