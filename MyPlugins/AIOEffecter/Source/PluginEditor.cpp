#include "PluginEditor.h"

namespace
{
    // 部品の選び方: ComboBox の id = type + 1（1 = 空き）
    void fillTypeBox (juce::ComboBox& b)
    {
        b.addItem ("-  empty  -", 1);
        juce::String lastCat;
        for (int t = 1; t <= aiofx::numTypes(); ++t)
        {
            const auto* inf = aiofx::infoFor (t);
            if (lastCat != inf->category) { b.addSectionHeading (inf->category); lastCat = inf->category; }
            b.addItem (inf->name, t + 1);
        }
    }
}

// ================= 1 段 =================
AIOEffecterEditor::Row::Row (AIOEffecterEditor& e, int slot) : owner (e), index (slot)
{
    auto& p = owner.processor;
    for (auto* b : { &up, &down })
    {
        b->setTooltip ("Move this slot up / down");
        addAndMakeVisible (*b);
    }
    up.onClick = [this] { owner.processor.moveSlot (index, index - 1); };
    down.onClick = [this] { owner.processor.moveSlot (index, index + 1); };
    up.setEnabled (slot > 0);
    down.setEnabled (slot < aiofx::kSlots - 1);

    onBtn.setClickingTogglesState (true);
    onBtn.setTooltip ("Turn this slot on / off");
    addAndMakeVisible (onBtn);
    onAtt = std::make_unique<BtnAttachment> (p.apvts, AIOEffecterProcessor::onId (slot), onBtn);

    fillTypeBox (type);
    type.setTooltip ("Choose the part for this slot");
    type.onChange = [this]
    {
        const int t = juce::jmax (0, type.getSelectedId() - 1);
        if (t != owner.processor.slotType (index)) owner.processor.setSlotType (index, t);
        owner.setHintSlot (index);
    };
    addAndMakeVisible (type);

    for (int k = 0; k < aiofx::kMaxKnobs; ++k)
    {
        auto& s = knob[k];
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
        s.setColour (juce::Slider::textBoxTextColourId, owner.text.withAlpha (0.85f));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.textFromValueFunction = [this, k] (double v) -> juce::String
        {
            if (const auto* inf = aiofx::infoFor (owner.processor.slotType (index)))
                if (k < (int) inf->knobs.size())
                {
                    const auto& ki = inf->knobs[(size_t) k];
                    if (ki.ctext != nullptr)
                    {
                        float all[aiofx::kMaxKnobs];
                        for (int j = 0; j < aiofx::kMaxKnobs; ++j)
                            all[j] = owner.processor.apvts.getRawParameterValue (AIOEffecterProcessor::knobId (index, j))->load();
                        all[k] = (float) v;
                        return juce::String (ki.ctext ((float) v, all));
                    }
                    if (ki.text != nullptr) return juce::String (ki.text ((float) v));
                }
            return juce::String (v, 2);
        };
        s.onDragStart = [this] { owner.setHintSlot (index); };
        addChildComponent (s);
        att[k] = std::make_unique<Attachment> (p.apvts, AIOEffecterProcessor::knobId (slot, k), s);
        name[k].setJustificationType (juce::Justification::centred);
        name[k].setFont (juce::FontOptions (11.0f, juce::Font::bold));
        name[k].setColour (juce::Label::textColourId, owner.text.withAlpha (0.8f));
        name[k].setInterceptsMouseClicks (false, false);
        addChildComponent (name[k]);
    }
    syncType();
}

void AIOEffecterEditor::Row::syncType()
{
    const int t = owner.processor.slotType (index);
    if (t == shownType) return;
    shownType = t;
    type.setSelectedId (t + 1, juce::dontSendNotification);
    const auto* inf = aiofx::infoFor (t);
    const int nk = inf ? (int) inf->knobs.size() : 0;
    for (int k = 0; k < aiofx::kMaxKnobs; ++k)
    {
        const bool vis = k < nk;
        knob[k].setVisible (vis);
        name[k].setVisible (vis);
        if (vis)
        {
            name[k].setText (inf->knobs[(size_t) k].name, juce::dontSendNotification);
            const int steps = inf->knobs[(size_t) k].steps;
            knob[k].setRange (0.0, 1.0, steps > 1 ? 1.0 / (steps - 1) : 0.0);
            knob[k].setDoubleClickReturnValue (true, inf->knobs[(size_t) k].def);
            knob[k].updateText();
        }
    }
    onBtn.setEnabled (t != 0);
    repaint();
}

void AIOEffecterEditor::Row::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
    g.setColour (owner.rowCol);
    g.fillRoundedRectangle (r, 6.0f);
    const bool on = onBtn.getToggleState() && shownType > 0;
    g.setColour (on ? owner.accent.withAlpha (0.9f) : juce::Colour (0xff3a4044));
    g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
    // 段の番号
    g.setColour (owner.text.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (index + 1), 10, 4, 20, 14, juce::Justification::centredLeft);
    // 動きのメーター（ゲートの開き・下げ量など）
    if (meter >= 0.0f && on)
    {
        auto m = juce::Rectangle<float> (300.0f, 12.0f, 6.0f, (float) getHeight() - 24.0f);
        g.setColour (juce::Colour (0xff0d0f10));
        g.fillRect (m);
        g.setColour (owner.accent);
        g.fillRect (m.withTrimmedTop (m.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, meter))));
    }
}

void AIOEffecterEditor::Row::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    r.removeFromLeft (16);
    auto arrows = r.removeFromLeft (24);
    up.setBounds (arrows.removeFromTop (r.getHeight() / 2).reduced (0, 2));
    down.setBounds (arrows.reduced (0, 2));
    r.removeFromLeft (6);
    onBtn.setBounds (r.removeFromLeft (44).withSizeKeepingCentre (44, 28));
    r.removeFromLeft (8);
    type.setBounds (r.removeFromLeft (180).withSizeKeepingCentre (180, 28));
    r.removeFromLeft (20);   // メーター
    const int kw = juce::jmin (78, r.getWidth() / aiofx::kMaxKnobs);
    for (int k = 0; k < aiofx::kMaxKnobs; ++k)
    {
        auto c = r.removeFromLeft (kw);
        name[k].setBounds (c.removeFromTop (13));
        knob[k].setBounds (c);
    }
}

// ================= 画面全体 =================
AIOEffecterEditor::AIOEffecterEditor (AIOEffecterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff343b3f));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3033));
    lnf.setColour (juce::TextButton::buttonOnColourId, accent);
    lnf.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.8f));
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a3033));
    lnf.setColour (juce::ComboBox::textColourId, text);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4246));
    lnf.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff22282b));
    lnf.setColour (juce::PopupMenu::textColourId, text);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    lnf.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    lnf.setColour (juce::PopupMenu::headerTextColourId, accent);
    setLookAndFeel (&lnf);

    for (int s = 0; s < aiofx::kSlots; ++s)
    {
        rows.push_back (std::make_unique<Row> (*this, s));
        addAndMakeVisible (*rows.back());
    }

    racks.setTextWhenNothingSelected ("RACKS");
    racks.addItemList (AIOEffecterProcessor::rackPresetNames(), 1);
    racks.setTooltip ("Built-in racks (replace all slots)");
    racks.onChange = [this]
    {
        const int i = racks.getSelectedId() - 1;
        if (i >= 0) processor.loadRackPreset (i);
        racks.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (racks);

    hint.setColour (juce::Label::textColourId, text.withAlpha (0.65f));
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    matchBtn.setButtonText ("MATCH");
    matchBtn.setClickingTogglesState (true);
    matchBtn.setTooltip ("Gain Stage (same LINK, before this plugin): keep the output at Gain Stage's TARGET level");
    linkBar.addAndMakeVisible (matchBtn);
    matchAtt = std::make_unique<BtnAttachment> (processor.apvts, "linkMatch", matchBtn);
    linkBar.extra = &matchBtn; linkBar.extraWidth = 64;
    linkBar.setSources (processor.link, { plink::kindGainStage });
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (processor.apvts.getRawParameterValue (myp::GainMatch::paramId)->load() > 0.5f)
            return "GAIN MATCH is on - LEVEL MATCH paused";
        if (! processor.link.on()) return "LINK OFF - same LINK as Gain Stage to match levels";
        if (! processor.match.sawStage()) return "waiting for Gain Stage (RMS mode)";
        lit = true;
        const float t = processor.match.trimDb();
        return "matched to TARGET (" + juce::String (t > 0.05f ? "+" : "") + juce::String (t, 1) + " dB)";
    };

    updateHint();
    startTimerHz (20);
    setSize (1240, headerH + aiofx::kSlots * rowH + 26 + footerH + 8);
}

AIOEffecterEditor::~AIOEffecterEditor()
{
    setLookAndFeel (nullptr);
}

void AIOEffecterEditor::updateHint()
{
    const auto* inf = aiofx::infoFor (processor.slotType (hintSlot));
    hint.setText (inf ? juce::String (hintSlot + 1) + "  " + inf->name + " - " + inf->hint
                      : juce::String ("Pick a part for each slot (top to bottom = signal order). Arrows move a slot."),
                  juce::dontSendNotification);
}

void AIOEffecterEditor::timerCallback()
{
    for (auto& r : rows)
    {
        r->syncType();
        const float m = processor.slotMeter (r->index);
        if (std::abs (m - r->meter) > 0.01f) { r->meter = m; r->repaint (300, 0, 10, rowH); }
    }
    updateHint();
    const float db = processor.outPeakDb.load();
    if (std::abs (db - meterDb) > 0.2f) { meterDb = db; repaint (getWidth() - 220, 10, 200, 28); }
}

void AIOEffecterEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (juce::Colour (0xff1c2124));
    g.fillRect (0, 0, getWidth(), headerH);
    g.setColour (accent);
    g.fillRect (0, headerH, getWidth(), 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("AIO EFFECTER", 16, 0, 300, headerH, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("signal flows top to bottom", 220, 0, 200, headerH, juce::Justification::centredLeft);
    // 出口のメーター（横）
    auto m = juce::Rectangle<int> (getWidth() - 220, 18, 170, 12);
    g.setColour (juce::Colour (0xff0d0f10));
    g.fillRect (m);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb + 60.0f) / 60.0f);
    g.setColour (meterDb > -0.5f ? juce::Colour (0xffe04a3a) : accent);
    g.fillRect (m.withWidth ((int) ((float) m.getWidth() * norm)));
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("OUT", getWidth() - 44, 16, 30, 16, juce::Justification::centredLeft);
}

void AIOEffecterEditor::resized()
{
    layoutFooter();
    racks.setBounds (getWidth() - 420, 10, 180, 28);
    auto area = getLocalBounds().withTrimmedTop (headerH + 4).withTrimmedBottom (footerH + 4);
    for (auto& r : rows) r->setBounds (area.removeFromTop (rowH).reduced (6, 0));
    hint.setBounds (area.reduced (14, 2));
}

// 画面のいちばん下（2 段: 上に LINK 欄、下に GAIN MATCH・プリセット・取り消し）
void AIOEffecterEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
    f.removeFromTop (4);
    presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
}
