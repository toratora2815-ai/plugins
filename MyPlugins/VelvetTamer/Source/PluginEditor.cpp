#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff120f16), panel (0xff1b1621), edge (0xff2c2433),
                       rose (0xffe58fb4), plum (0xffb59cff), gold (0xffffd27a), text (0xffefe9f3);

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

    presetBox.addItemList (VelvetTamerProcessor::presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESET");
    presetBox.setColour (juce::ComboBox::backgroundColourId, edge);
    presetBox.setColour (juce::ComboBox::outlineColourId, edge);
    presetBox.setColour (juce::ComboBox::textColourId, text);
    presetBox.setColour (juce::ComboBox::arrowColourId, rose);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0) processor.applyPreset (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

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
    header.removeFromRight (6);
    presetBox.setBounds (header.removeFromRight (200).reduced (2, 8));
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

float VelvetTamerEditor::xToFreq (float x, juce::Rectangle<float> r) const
{
    const float lo = vt::TamerEngine::displayLowHz, hi = vt::TamerEngine::displayHighHz;
    return lo * std::pow (hi / lo, (x - r.getX()) / r.getWidth());
}

// WEIGHT：0 が真ん中、+100% が上、-100% が下（上下に 15% ずつ余白）
float VelvetTamerEditor::weightToY (float w, juce::Rectangle<float> r) const
{
    return r.getCentreY() - w * r.getHeight() * 0.35f;
}

float VelvetTamerEditor::yToWeight (float y, juce::Rectangle<float> r) const
{
    return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - y) / (r.getHeight() * 0.35f));
}

juce::Rectangle<float> VelvetTamerEditor::plotArea() const
{
    return displayArea.toFloat().reduced (4.0f).withTrimmedBottom (16.0f);
}

int VelvetTamerEditor::findGrab (juce::Point<float> p) const
{
    const auto plot = plotArea();
    if (! plot.expanded (6.0f).contains (p)) return grabNone;

    for (int i = 0; i < vt::numWeights; ++i)
    {
        const juce::Point<float> node (freqToX (vt::weightHz[i], plot),
                                       weightToY (param (VelvetTamerProcessor::weightId (i)) * 0.01f, plot));
        if (node.getDistanceFrom (p) < 10.0f) return i;
    }

    const float lo = param ("low"), hi = param ("high");
    const float x0 = freqToX (juce::jmin (lo, hi), plot), x1 = freqToX (juce::jmax (lo, hi), plot);
    if (std::abs (p.x - x0) < 6.0f) return lo <= hi ? grabLow : grabHigh;
    if (std::abs (p.x - x1) < 6.0f) return lo <= hi ? grabHigh : grabLow;
    if (p.x > x0 && p.x < x1) return grabBand;
    return grabNone;
}

std::vector<juce::RangedAudioParameter*> VelvetTamerEditor::paramsFor (int g) const
{
    auto& s = processor.apvts;
    if (g >= 0 && g < vt::numWeights) return { s.getParameter (VelvetTamerProcessor::weightId (g)) };
    if (g == grabLow)  return { s.getParameter ("low") };
    if (g == grabHigh) return { s.getParameter ("high") };
    if (g == grabBand) return { s.getParameter ("low"), s.getParameter ("high") };
    return {};
}

void VelvetTamerEditor::setParam (const juce::String& id, float value)
{
    if (auto* p = processor.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void VelvetTamerEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = findGrab (e.position);
    if (h != hover) { hover = h; repaint (tDisplay.bounds); }
    setMouseCursor (h == grabNone ? juce::MouseCursor::NormalCursor
                  : (h < vt::numWeights ? juce::MouseCursor::UpDownResizeCursor
                  : (h == grabBand ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor)));
}

void VelvetTamerEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover != grabNone) { hover = grabNone; repaint (tDisplay.bounds); }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void VelvetTamerEditor::mouseDown (const juce::MouseEvent& e)
{
    grab = findGrab (e.position);
    if (grab == grabNone) return;
    for (auto* p : paramsFor (grab)) p->beginChangeGesture();
    grabLowHz = param ("low");
    grabHighHz = param ("high");
    grabFreq = xToFreq (e.position.x, plotArea());
}

void VelvetTamerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (grab == grabNone) return;
    const auto plot = plotArea();
    const float f = xToFreq (e.position.x, plot);

    if (grab < vt::numWeights)
        setParam (VelvetTamerProcessor::weightId (grab), yToWeight (e.position.y, plot) * 100.0f);
    else if (grab == grabLow)
        setParam ("low", f);
    else if (grab == grabHigh)
        setParam ("high", f);
    else
    {
        // 幅はそのまま、帯域ごと横に動かす（端に当たったら止める）
        auto* lowP = processor.apvts.getParameter ("low");
        auto* highP = processor.apvts.getParameter ("high");
        const auto& lr = lowP->getNormalisableRange();
        const auto& hr = highP->getNormalisableRange();
        const float ratio = juce::jlimit (lr.start / grabLowHz, lr.end / grabLowHz, f / grabFreq);
        const float r2 = juce::jlimit (hr.start / grabHighHz, hr.end / grabHighHz, ratio);
        setParam ("low", grabLowHz * r2);
        setParam ("high", grabHighHz * r2);
    }
    repaint (tDisplay.bounds);
}

void VelvetTamerEditor::mouseUp (const juce::MouseEvent&)
{
    for (auto* p : paramsFor (grab)) p->endChangeGesture();
    grab = grabNone;
}

// ダブルクリックで初期値に戻す
void VelvetTamerEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (auto* p : paramsFor (findGrab (e.position)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
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
    juce::ignoreUnused (r);
    auto plot = plotArea();

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
        const float lo = param ("low"), hi = param ("high");
        const float x0 = juce::jlimit (plot.getX(), plot.getRight(), freqToX (juce::jmin (lo, hi), plot));
        const float x1 = juce::jlimit (plot.getX(), plot.getRight(), freqToX (juce::jmax (lo, hi), plot));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (plot.withRight (x0));
        g.fillRect (plot.withLeft (x1));
        const bool loFirst = lo <= hi;
        auto edgeLine = [&] (float x, int which)
        {
            const bool hot = hover == which || grab == which || hover == grabBand || grab == grabBand;
            g.setColour (plum.withAlpha (hot ? 1.0f : 0.5f));
            g.fillRect (x - (hot ? 1.0f : 0.5f), plot.getY(), hot ? 2.0f : 1.0f, plot.getHeight());
            // つまみ（つかめる目印）
            g.fillRoundedRectangle (x - 4.0f, plot.getCentreY() - 14.0f, 8.0f, 28.0f, 3.0f);
        };
        edgeLine (x0, loFirst ? grabLow : grabHigh);
        edgeLine (x1, loFirst ? grabHigh : grabLow);
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

    // WEIGHT のカーブと点
    {
        float w[vt::numWeights];
        for (int i = 0; i < vt::numWeights; ++i) w[i] = param (VelvetTamerProcessor::weightId (i)) * 0.01f;

        g.setColour (gold.withAlpha (0.25f));
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

        juce::Path curve;
        for (int px = 0; px <= (int) plot.getWidth(); px += 2)
        {
            const float x = plot.getX() + (float) px;
            const float f = xToFreq (x, plot);
            float v;
            if (f <= vt::weightHz[0]) v = w[0];
            else if (f >= vt::weightHz[vt::numWeights - 1]) v = w[vt::numWeights - 1];
            else
            {
                int i = 0;
                while (f > vt::weightHz[i + 1]) ++i;
                const float t = std::log (f / vt::weightHz[i]) / std::log (vt::weightHz[i + 1] / vt::weightHz[i]);
                v = w[i] + (w[i + 1] - w[i]) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t));
            }
            const float y = weightToY (v, plot);
            if (px == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (gold.withAlpha (0.8f));
        g.strokePath (curve, juce::PathStrokeType (1.4f));

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int i = 0; i < vt::numWeights; ++i)
        {
            const juce::Point<float> c (freqToX (vt::weightHz[i], plot), weightToY (w[i], plot));
            const bool hot = hover == i || grab == i;
            const float rad = hot ? 6.5f : 5.0f;
            g.setColour (bg);
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
            g.setColour (gold);
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, 1.6f);
            if (hot)
            {
                const int pctVal = juce::roundToInt (w[i] * 100.0f);
                g.drawText ((pctVal > 0 ? "+" : "") + juce::String (pctVal) + "%",
                            juce::Rectangle<float> (c.x - 30, c.y - 24, 60, 14), juce::Justification::centred);
            }
        }
    }
}
