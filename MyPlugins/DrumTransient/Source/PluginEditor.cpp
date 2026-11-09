#include "PluginEditor.h"

DrumTransientEditor::DrumTransientEditor (DrumTransientProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (push,    "push",    "PUSH",    "how hard to drive");
    setupKnob (punch,   "punch",   "PUNCH",   "keep the attack");
    setupKnob (body,    "body",    "BODY",    "- tight / + room");
    setupKnob (clip,    "clip",    "CLIP / LIMIT", "cut or smooth");
    setupKnob (release, "release", "RELEASE", "tight / smooth");
    setupCombo (lowKeep, lowKeepLabel, "lowKeep", "LOW KEEP", DrumTransientProcessor::lowKeepNames(), lowKeepAtt);
    setupCombo (ceiling, ceilingLabel, "ceiling", "CEILING", DrumTransientProcessor::ceilingNames(), ceilingAtt);
    lowKeep.setTooltip ("Below this frequency nothing is clipped (keeps the kick's fundamental clean)");
    ceiling.setTooltip ("Output never goes above this level");

    for (auto* b : { &linkBtn, &deltaBtn })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setColour (juce::TextButton::textColourOffId, text);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (*b);
    }
    linkBtn.setTooltip ("Same gain on left and right (keeps the snare in the centre)");
    deltaBtn.setTooltip ("Listen only to what is being removed");
    linkAtt = std::make_unique<ButtonAtt> (processor.apvts, "stereoLink", linkBtn);
    deltaAtt = std::make_unique<ButtonAtt> (processor.apvts, "delta", deltaBtn);

    learnBtn.setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
    learnBtn.setColour (juce::TextButton::textColourOffId, text);
    learnBtn.setTooltip ("Play the drums and press: listens for 5 s, then sets PUSH so the ATTACK meter peaks reach the preset's target");
    learnBtn.onClick = [this] { processor.toggleLearn(); learnShowT = 0; };
    addAndMakeVisible (learnBtn);

    factory.setTextWhenNothingSelected ("Presets");
    const auto names = DrumTransientProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i) factory.addItem (names[i], i + 1);
    factory.onChange = [this] { const int id = factory.getSelectedId(); if (id > 0) processor.applyPreset (id - 1); };
    addAndMakeVisible (factory);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });
    linkBar.status = [this] (bool& lit) -> juce::String { lit = processor.link.on(); return lit ? juce::String ("on this LINK") : juce::String ("Plugin Link OFF"); };

    setSize (1000, topH + 230 + footerH);
    startTimerHz (30);
}

void DrumTransientEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& hint)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.2f));
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (k.label);

    k.hint.setText (hint, juce::dontSendNotification);
    k.hint.setJustificationType (juce::Justification::centred);
    k.hint.setFont (juce::FontOptions (11.0f));
    k.hint.setColour (juce::Label::textColourId, text.withAlpha (0.5f));
    addAndMakeVisible (k.hint);

    k.attachment = std::make_unique<SliderAtt> (processor.apvts, paramId, k.slider);
}

void DrumTransientEditor::setupCombo (juce::ComboBox& c, juce::Label& l, const juce::String& paramId, const juce::String& name,
                                      const juce::StringArray& items, std::unique_ptr<ComboAtt>& att)
{
    c.addItemList (items, 1);
    addAndMakeVisible (c);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (l);
    att = std::make_unique<ComboAtt> (processor.apvts, paramId, c);
}

void DrumTransientEditor::timerCallback()
{
    auto follow = [] (float& shown, float& hold, int& holdT, float v)
    {
        shown = v > shown ? v : shown * 0.85f + v * 0.15f;
        if (v >= hold) { hold = v; holdT = 45; } else if (--holdT < 0) hold = juce::jmax (0.0f, hold - 0.3f);
    };
    follow (showAtk, holdAtk, holdAtkT, processor.dsp.meterAttack.load());
    follow (showBody, holdBody, holdBodyT, processor.dsp.meterBody.load());
    repaint (meterArea);

    // LEARN の表示
    juce::String t = "LEARN";
    switch (processor.learnState.load())
    {
        case DrumTransientProcessor::learnListening: t = "LISTEN " + juce::String (juce::roundToInt (processor.learnProgress.load() * 100.0f)) + "%"; break;
        case DrumTransientProcessor::learnAnalyzing: t = "..."; break;
        case DrumTransientProcessor::learnDone:
        case DrumTransientProcessor::learnFailed:
            if (learnShowT == 0) learnShowT = 120;   // 4 秒出す
            t = processor.learnState.load() == DrumTransientProcessor::learnDone ? "PUSH " + juce::String (processor.learnResult.load(), 1) + " dB" : juce::String ("TOO QUIET");
            if (--learnShowT <= 0) { processor.learnState.store (DrumTransientProcessor::learnIdle); learnShowT = 0; }
            break;
        default: if (processor.learnIsBusy()) t = "...";   // 前の計算が終わるまで（押しても始まらない）
                 break;
    }
    const bool active = processor.learnState.load() != DrumTransientProcessor::learnIdle;
    learnBtn.setButtonText (t);
    learnBtn.setColour (juce::TextButton::buttonColourId, active ? accent : panel.brighter (0.15f));
    learnBtn.setColour (juce::TextButton::textColourOffId, active ? juce::Colours::black : text);
}

void DrumTransientEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto top = getLocalBounds().removeFromTop (topH);
    g.setColour (panel.brighter (0.06f));
    g.fillRect (top);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("DRUM TRANSIENT", top.reduced (14, 0), juce::Justification::centredLeft);

    drawHistory (g, meterArea.reduced (6, 4));
}

// 削った量を右から左へ流す（約 3 秒）。真ん中に入口（PUSH 後）の波形を薄く。上から: BODY（胴を下げた量）の青い帯、
// その下に続けて ATTACK（アタックを切った量）のオレンジのトゲ（＝上端からの長さが、合わせて削った量）
void DrumTransientEditor::drawHistory (juce::Graphics& g, juce::Rectangle<int> r)
{
    const juce::Colour blue (0xff6fb4ff);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto head = r.removeFromTop (16);
    g.setColour (accent);
    g.drawText ("ATTACK -" + juce::String (holdAtk, 1), head.removeFromLeft (head.getWidth() / 2), juce::Justification::centredLeft);
    g.setColour (blue);
    g.drawText ("BODY -" + juce::String (holdBody, 1), head, juce::Justification::centredRight);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (r);
    const float range = 18.0f;   // 縦: 上端 0 dB、下端 -18 dB
    const auto rr = r.toFloat();
    auto yOf = [&] (float dB) { return rr.getY() + juce::jlimit (0.0f, 1.0f, dB / range) * rr.getHeight(); };

    auto& e = processor.dsp;
    const int W = r.getWidth(), cols = 600;   // 5 ms × 600 = 3 秒
    const int end = e.histWrite.load (std::memory_order_acquire);
    auto at = [&] (std::atomic<float>* h, int back) { return h[(end - 1 - back + 4 * dt::Engine::histSize) % dt::Engine::histSize].load (std::memory_order_relaxed); };
    std::vector<float> va ((size_t) W), vb ((size_t) W), vi ((size_t) W);
    for (int x = 0; x < W; ++x)
    {
        // x = 0 が一番古い。この 1 ピクセルにかかる列の最大値（一瞬のトゲが平均で消えないように）
        const int c0 = cols - 1 - (x * cols) / W, c1 = juce::jmax (cols - 1 - ((x + 1) * cols) / W, 0);
        float a = 0, b = 0, in = -100;
        for (int c = c1; c <= c0; ++c) { a = juce::jmax (a, at (e.histAtk, c)); b = juce::jmax (b, at (e.histBody, c)); in = juce::jmax (in, at (e.histIn, c)); }
        va[(size_t) x] = a; vb[(size_t) x] = b; vi[(size_t) x] = in;
    }

    // 入口の波形（天井から -36 〜 +12 dB を、真ん中から上下に）
    const float cy = rr.getCentreY(), half = rr.getHeight() * 0.5f;
    juce::Path wave;
    wave.startNewSubPath (rr.getX(), cy);
    for (int x = 0; x < W; ++x) wave.lineTo (rr.getX() + (float) x + 0.5f, cy - half * juce::jlimit (0.0f, 1.0f, (vi[(size_t) x] + 36.0f) / 48.0f));
    for (int x = W - 1; x >= 0; --x) wave.lineTo (rr.getX() + (float) x + 0.5f, cy + half * juce::jlimit (0.0f, 1.0f, (vi[(size_t) x] + 36.0f) / 48.0f));
    wave.closeSubPath();
    g.setColour (text.withAlpha (0.16f));
    g.fillPath (wave);

    g.setColour (text.withAlpha (0.12f));
    for (int d = 6; d < 18; d += 6) g.drawHorizontalLine ((int) yOf ((float) d), rr.getX(), rr.getRight());

    // BODY の帯（なめらか）
    juce::Path band;
    band.startNewSubPath (rr.getX(), rr.getY());
    for (int x = 0; x < W; ++x) band.lineTo (rr.getX() + (float) x + 0.5f, yOf (vb[(size_t) x]));
    band.lineTo (rr.getRight(), rr.getY());
    band.closeSubPath();
    g.setColour (blue.withAlpha (0.7f));
    g.fillPath (band);

    // ATTACK のトゲ（帯の下に続けて）。切っていない所は何も描かない
    g.setColour (accent);
    for (int x = 0; x < W; ++x)
        if (va[(size_t) x] > 0.05f)
            g.drawVerticalLine (r.getX() + x, yOf (vb[(size_t) x]), juce::jmax (yOf (vb[(size_t) x]) + 1.0f, yOf (vb[(size_t) x] + va[(size_t) x])));

    g.setColour (text.withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("-6", juce::Rectangle<float> (rr.getX() + 2, yOf (6.0f) - 12, 30, 12), juce::Justification::centredLeft);
    g.drawText ("-12", juce::Rectangle<float> (rr.getX() + 2, yOf (12.0f) - 12, 30, 12), juce::Justification::centredLeft);
}

void DrumTransientEditor::resized()
{
    layoutFooter();
    auto area = getLocalBounds().withTrimmedBottom (footerH);
    auto top = area.removeFromTop (topH).reduced (10, 8);
    top.removeFromLeft (220);
    deltaBtn.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    linkBtn.setBounds (top.removeFromRight (64));
    top.removeFromRight (14);
    factory.setBounds (top.removeFromRight (200));
    top.removeFromRight (14);
    learnBtn.setBounds (top.removeFromRight (110));

    area = area.reduced (10, 8);
    meterArea = area.removeFromRight (meterW);
    auto combos = area.removeFromRight (120);
    combos.removeFromTop (20);
    lowKeepLabel.setBounds (combos.removeFromTop (20));
    lowKeep.setBounds (combos.removeFromTop (26).reduced (6, 0));
    combos.removeFromTop (30);
    ceilingLabel.setBounds (combos.removeFromTop (20));
    ceiling.setBounds (combos.removeFromTop (26).reduced (6, 0));

    const int w = area.getWidth() / 5;
    for (auto* k : { &push, &punch, &body, &clip, &release })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->hint.setBounds (col.removeFromBottom (16));
        k->slider.setBounds (col.reduced (4));
    }
}

// 画面のいちばん下（Plugin Link と GAIN MATCH・プリセット・取り消し）。2 段
void DrumTransientEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
    f.removeFromTop (4);
    presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
}
