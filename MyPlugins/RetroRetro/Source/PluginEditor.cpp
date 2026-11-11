#include "PluginEditor.h"

namespace
{
    const rrp::Def* findDef (const juce::String& id)
    {
        for (auto& d : rrp::defs()) if (id == d.id) return &d;
        return nullptr;
    }
    // モジュールごとの説明（ツールチップ）
    const char* moduleTip (int m)
    {
        static const char* t[] = {
            "MASTER: input / output, EQ, compressor, varispeed",
            "NOISE: adds hiss, crackle, hum (generated, 10 types). Follow = noise rides the input level, Duck = noise gets out of the way",
            "WOBBLE: tape speed wobble. Wow = slow pitch drift, Flutter = fast shimmer",
            "DISTORT: 9 saturation characters (tube, transformer, tape, preamp, pocket recorder...)",
            "DIGITAL: lowers sample rate and bit depth (old samplers, games)",
            "SPACE: 10 rooms, plates and springs. Above 50 % the dry sound goes down",
            "LOSS: Magnetic = worn tape (level / tone wavering, dropouts), Codec = low-bitrate audio",
            "ECHO: tape-style delay; the repeats can wobble, distort, crush, diffuse and wear",
        };
        return t[juce::jlimit (0, 7, m)];
    }
}

RetroRetroEditor::RetroRetroEditor (RetroRetroProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColourScheme ({ rrui::bg, rrui::panel, rrui::tile, rrui::dim, rrui::text, rrui::accent, rrui::bg, rrui::accent, rrui::text });
    lnf.setColour (juce::Slider::rotarySliderFillColourId, rrui::accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, rrui::off);
    lnf.setColour (juce::Slider::thumbColourId, rrui::text);
    lnf.setColour (juce::ComboBox::backgroundColourId, rrui::panel);
    lnf.setColour (juce::ComboBox::outlineColourId, rrui::off);
    lnf.setColour (juce::PopupMenu::backgroundColourId, rrui::panel);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, rrui::accent.withAlpha (0.4f));
    setLookAndFeel (&lnf);

    // MAIN の 7 枚
    auto addTile = [this] (int m, const char* onId, const char* amtId, const char* s1, const char* s1Label, const char* s2, const char* s2Label)
    {
        Tile t; t.module = m;
        t.on = makeCtrl (onId, rrp::moduleName (m));
        t.amount = makeCtrl (amtId, "", true);
        if (s1) t.sub1 = makeCtrl (s1, s1Label);
        if (s2) t.sub2 = makeCtrl (s2, s2Label);
        t.on->btn->setTooltip (moduleTip (m));
        tiles.push_back (std::move (t));
    };
    addTile (1, "noiseOn", "noiseAmt",  "noiseType", "TYPE",        "noiseTone",  "TONE");
    addTile (2, "wobOn",   "wobAmt",    "wowFlutter", "WOW<>FLUTTER", "wowStereo", "STEREO");
    addTile (3, "distOn",  "distAmt",   "distType",  "TYPE",        "distMix",    "MIX");
    addTile (4, "digOn",   "digAmt",    "digRateBits", "RATE<>BITS", "digSputter", "SPUTTER");
    addTile (5, "spaceOn", "spaceAmt",  "spaceType", "TYPE",        "spaceDecay", "DECAY");
    addTile (6, "lossOn",  "lossAmt",   "lossXfade", "MAG<>CODEC",  "lossDrop",   "DROPOUTS");
    addTile (7, "echoOn",  "echoAmt",   "echoNoteL", "TIME",        "echoFb",     "FEEDBACK");

    magnitude = makeCtrl ("magnitude", "MAGNITUDE", true);
    magnitude->slider->setTooltip ("MAGNITUDE: turns every module up or down together (it multiplies all the amounts)");
    tone = makeCtrl ("tone", "TONE"); toneMode = makeCtrl ("toneMode", "");
    compress = makeCtrl ("compress", "COMPRESS"); outWidth = makeCtrl ("outWidth", "WIDTH");
    mix = makeCtrl ("mix", "MIX"); outGain = makeCtrl ("outGain", "OUT");

    // DETAIL: モジュールごとに全部
    for (auto& d : rrp::defs())
    {
        juce::String lab (d.name);
        const juce::String mn (rrp::moduleName (d.module));
        if (d.module > 0 && lab.startsWithIgnoreCase (mn.substring (0, 1) + mn.substring (1).toLowerCase() + " ")) lab = lab.fromFirstOccurrenceOf (" ", false, false);
        auto c = makeCtrl (d.id, lab.toUpperCase());
        c->module = d.module;
        detail.push_back (std::move (c));
    }
    for (int m = 0; m < rrp::kNumModules; ++m)
    {
        auto* b = moduleTabs.add (new juce::TextButton (rrp::moduleName (m)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, rrui::panel);
        b->setColour (juce::TextButton::buttonOnColourId, rrui::accent);
        b->setColour (juce::TextButton::textColourOnId, rrui::bg);
        b->onClick = [this, m] { setDetailModule (m); };
        b->setTooltip (moduleTip (m));
        addChildComponent (b);
    }

    for (auto* b : { &mainBtn, &detailBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, rrui::panel);
        b->setColour (juce::TextButton::buttonOnColourId, rrui::accent);
        b->setColour (juce::TextButton::textColourOnId, rrui::bg);
        addAndMakeVisible (*b);
    }
    mainBtn.onClick = [this] { setView (0); };
    detailBtn.onClick = [this] { setView (1); };
    detailBtn.setTooltip ("All the knobs of one module");

    factoryBox.setTextWhenNothingSelected ("Factory presets");
    for (int i = 0; i < RetroRetroProcessor::numFactoryPresets(); ++i) factoryBox.addItem (RetroRetroProcessor::factoryPresetName (i), i + 1);
    factoryBox.onChange = [this]
    {
        const int i = factoryBox.getSelectedId() - 1;
        if (i >= 0) processor.loadFactoryPreset (i);
        factoryBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (factoryBox);

    // 下の欄
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the NOISE this much while the voice is singing (hiss only in the gaps)");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<SAtt> (processor.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 170; linkBar.extraLabel = "NOISE DUCK";
    linkBar.setSources (processor.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Fader Pilot to lower the noise while singing";
        if (! processor.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = processor.duck.nowDb() < -0.1f;
        return lit ? "voice singing - noise lowered" : "Fader Pilot found - voice not singing";
    };
    presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); });

    view = (int) processor.apvts.state.getProperty ("rrView", 0);
    detailModule = juce::jlimit (0, rrp::kNumModules - 1, (int) processor.apvts.state.getProperty ("rrModule", 1));
    setSize (W, topH + mainH + footerH);
    setView (view);
    startTimerHz (20);
}

RetroRetroEditor::~RetroRetroEditor()
{
    setLookAndFeel (nullptr);
}

std::unique_ptr<RetroRetroEditor::Ctrl> RetroRetroEditor::makeCtrl (const juce::String& id, const juce::String& labelText, bool big)
{
    auto c = std::make_unique<Ctrl>();
    c->id = id;
    const auto* d = findDef (id);
    c->kind = d ? d->kind : rrp::Kind::flt;
    c->module = d ? d->module : 0;
    if (c->kind == rrp::Kind::flt)
    {
        c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        c->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 76 : 70, 16);
        c->slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        c->slider->setColour (juce::Slider::textBoxTextColourId, rrui::text);
        c->slider->setColour (juce::Slider::rotarySliderFillColourId, big ? rrui::accent : rrui::accent2);
        c->slider->setDoubleClickReturnValue (true, d ? d->def : 0.5);
        c->sAtt = std::make_unique<SAtt> (processor.apvts, id, *c->slider);
        if (d) c->slider->setDoubleClickReturnValue (true, d->def);
        addChildComponent (*c->slider);
    }
    else if (c->kind == rrp::Kind::choice)
    {
        c->box = std::make_unique<juce::ComboBox>();
        if (d) c->box->addItemList (d->choices, 1);
        c->cAtt = std::make_unique<CAtt> (processor.apvts, id, *c->box);
        addChildComponent (*c->box);
    }
    else
    {
        c->btn = std::make_unique<juce::TextButton> (labelText.isNotEmpty() ? labelText : juce::String ("ON"));
        c->btn->setClickingTogglesState (true);
        c->btn->setColour (juce::TextButton::buttonColourId, rrui::off);
        c->btn->setColour (juce::TextButton::buttonOnColourId, rrui::accent);
        c->btn->setColour (juce::TextButton::textColourOnId, rrui::bg);
        c->btn->setColour (juce::TextButton::textColourOffId, rrui::text);
        c->bAtt = std::make_unique<BAtt> (processor.apvts, id, *c->btn);
        addChildComponent (*c->btn);
    }
    c->label.setText (c->kind == rrp::Kind::toggle ? juce::String() : labelText, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    c->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    c->label.setColour (juce::Label::textColourId, rrui::dim);
    addChildComponent (c->label);
    return c;
}

void RetroRetroEditor::placeCtrl (Ctrl& c, juce::Rectangle<int> r)
{
    c.comp()->setVisible (true);
    if (c.kind == rrp::Kind::toggle) { c.label.setVisible (false); c.btn->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 110), 24)); return; }
    if (c.label.getText().isNotEmpty()) { c.label.setVisible (true); c.label.setBounds (r.removeFromTop (14)); }
    if (c.kind == rrp::Kind::choice) c.box->setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
    else c.slider->setBounds (r);
}

void RetroRetroEditor::setView (int v)
{
    view = v;
    processor.apvts.state.setProperty ("rrView", v, nullptr);
    mainBtn.setToggleState (v == 0, juce::dontSendNotification);
    detailBtn.setToggleState (v == 1, juce::dontSendNotification);
    resized();
    repaint();
}

void RetroRetroEditor::setDetailModule (int m)
{
    detailModule = m;
    processor.apvts.state.setProperty ("rrModule", m, nullptr);
    resized();
    repaint();
}

void RetroRetroEditor::timerCallback()
{
    // ECHO CLEAR は押したら次のタイマーで OFF に戻す（音の処理は押した瞬間だけ消す）
    if (auto* p = processor.apvts.getParameter ("echoClear"))
        if (p->getValue() > 0.5f) { if (++clearHold > 2) { p->beginChangeGesture(); p->setValueNotifyingHost (0.0f); p->endChangeGesture(); clearHold = 0; } }
    const float i = processor.meterIn.load(), o = processor.meterOut.load();
    inMeter = juce::jmax (i, inMeter - 1.5f); outMeter = juce::jmax (o, outMeter - 1.5f);
    // オフのモジュールは薄く
    for (auto& t : tiles)
    {
        const bool on = t.on->btn->getToggleState();
        for (auto* c : { t.amount.get(), t.sub1.get(), t.sub2.get() }) if (c) c->comp()->setAlpha (on ? 1.0f : 0.4f);
    }
    repaint (getWidth() - 200, 0, 200, topH);
}

void RetroRetroEditor::paint (juce::Graphics& g)
{
    g.fillAll (rrui::bg);
    // 上の帯
    g.setColour (rrui::panel); g.fillRect (0, 0, getWidth(), topH);
    g.setColour (rrui::accent); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("RETRO RETRO", 16, 0, 220, topH, juce::Justification::centredLeft);
    g.setColour (rrui::dim); g.setFont (juce::FontOptions (11.0f));
    g.drawText ("noise / wobble / distort / digital / space / loss / echo", 196, 0, 330, topH, juce::Justification::centredLeft);
    // メーター（入口・出口）
    auto meter = [&g] (juce::Rectangle<int> r, float dbv, const char* lab)
    {
        g.setColour (rrui::dim); g.setFont (juce::FontOptions (10.0f)); g.drawText (lab, r.removeFromLeft (24), juce::Justification::centredLeft);
        g.setColour (rrui::off); g.fillRect (r);
        const float f = juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f);
        g.setColour (dbv > -1.0f ? juce::Colours::orangered : rrui::accent2); g.fillRect (r.withWidth ((int) (r.getWidth() * f)));
    };
    meter ({ getWidth() - 180, 12, 160, 10 }, inMeter, "IN");
    meter ({ getWidth() - 180, 28, 160, 10 }, outMeter, "OUT");

    if (view == 0)
    {
        for (auto& t : tiles)
        {
            g.setColour (rrui::tile); g.fillRoundedRectangle (t.area.toFloat(), 8.0f);
        }
        g.setColour (rrui::panel.brighter (0.05f)); g.fillRoundedRectangle (masterArea.toFloat(), 8.0f);
        g.setColour (rrui::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MASTER", masterArea.withHeight (22).translated (0, 4), juce::Justification::centred);
    }
    else
    {
        g.setColour (rrui::tile); g.fillRoundedRectangle (detailArea.toFloat(), 8.0f);
        g.setColour (rrui::dim); g.setFont (juce::FontOptions (11.0f));
        g.drawText (moduleTip (detailModule), detailArea.withHeight (20).translated (12, 4), juce::Justification::centredLeft);
    }
}

void RetroRetroEditor::resized()
{
    // 下の欄（2 段）
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
    // 上の帯
    mainBtn.setBounds (540, 11, 70, 28);
    detailBtn.setBounds (614, 11, 70, 28);
    factoryBox.setBounds (700, 11, 220, 28);

    // いったん全部隠す
    auto hide = [] (Ctrl* c) { if (! c) return; c->comp()->setVisible (false); c->label.setVisible (false); };
    for (auto& t : tiles) { hide (t.on.get()); hide (t.amount.get()); hide (t.sub1.get()); hide (t.sub2.get()); }
    for (auto* c : { magnitude.get(), tone.get(), toneMode.get(), compress.get(), outWidth.get(), mix.get(), outGain.get() }) hide (c);
    for (auto& c : detail) hide (c.get());
    for (auto* b : moduleTabs) b->setVisible (false);

    auto area = getLocalBounds().withTrimmedTop (topH).withTrimmedBottom (footerH).reduced (10, 8);
    if (view == 0)
    {
        masterArea = area.removeFromRight (230);
        area.removeFromRight (8);
        const int tw = (area.getWidth() - 6 * 6) / 7;
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            auto& t = tiles[i];
            t.area = area.removeFromLeft (tw); area.removeFromLeft (6);
            auto r = t.area.reduced (6, 6);
            placeCtrl (*t.on, r.removeFromTop (26));
            r.removeFromTop (4);
            placeCtrl (*t.amount, r.removeFromTop (128));
            r.removeFromTop (6);
            if (t.sub1) { placeCtrl (*t.sub1, r.removeFromTop (t.sub1->kind == rrp::Kind::flt ? 84 : 42)); r.removeFromTop (4); }
            if (t.sub2) placeCtrl (*t.sub2, r.removeFromTop (t.sub2->kind == rrp::Kind::flt ? 84 : 30));
        }
        auto m = masterArea.reduced (8, 8); m.removeFromTop (20);
        placeCtrl (*magnitude, m.removeFromTop (126));
        m.removeFromTop (4);
        auto row1 = m.removeFromTop (84);
        const int cw = row1.getWidth() / 3;
        placeCtrl (*tone, row1.removeFromLeft (cw)); placeCtrl (*compress, row1.removeFromLeft (cw)); placeCtrl (*outWidth, row1);
        auto row2 = m.removeFromTop (84);
        placeCtrl (*toneMode, row2.removeFromLeft (cw).withSizeKeepingCentre (cw - 4, 40)); placeCtrl (*mix, row2.removeFromLeft (cw)); placeCtrl (*outGain, row2);
        toneMode->box->setTooltip ("TONE shape: Tilt / Mid / Filter 1 (radio) / Filter 2 (phone)");
    }
    else
    {
        auto tabs = area.removeFromTop (28);
        const int bw = tabs.getWidth() / rrp::kNumModules;
        for (int i = 0; i < moduleTabs.size(); ++i)
        {
            auto* b = moduleTabs[i];
            b->setVisible (true);
            b->setToggleState (i == detailModule, juce::dontSendNotification);
            b->setBounds (tabs.removeFromLeft (bw).reduced (2, 0));
        }
        area.removeFromTop (6);
        detailArea = area;
        auto g = area.reduced (8, 0); g.removeFromTop (24);
        const int cols = 11, cw = g.getWidth() / cols, rh = (g.getHeight()) / 3;
        int idx = 0;
        for (auto& c : detail)
        {
            if (c->module != detailModule) continue;
            const int col = idx % cols, row = idx / cols;
            if (row >= 3) break;
            placeCtrl (*c, { g.getX() + col * cw, g.getY() + row * rh, cw - 4, rh - 6 });
            ++idx;
        }
    }
}
