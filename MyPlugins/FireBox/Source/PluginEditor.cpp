#include "PluginEditor.h"

FireBoxEditor::FireBoxEditor (FireBoxProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    setupKnob (kIn,     "in",       "IN",        {},            "Input gain before everything (drives the saturation, compression and clipping harder)");
    setupKnob (kBass,   "bass",     "BASS",      "bassOn",      "Low-end saturation and a lift around 54 Hz (the EQ starts above about 22%)");
    setupKnob (kThick,  "thick",    "THICK",     "thickOn",     "Broad saturation with mid-range emphasis; also widens the side signal");
    setupKnob (kBody,   "body",     "BODY",      "bodyOn",      "Slow upward compression: lifts quiet parts over about a second");
    setupKnob (kComp,   "comp",     "COMP",      "compOn",      "COMP: very fast 3.5:1 compressor with automatic makeup. OTT: 3-band up/down compression");
    setupKnob (kSc,     "softclip", "SOFT CLIP", "softclipOn",  "Saturating soft clipper");
    setupKnob (kCrunch, "crunch",   "CRUNCH",    "crunchOn",    "Fuzz that crushes the signal");
    setupKnob (kMix,    "mix",      "MIX",       {},            "Dry / processed blend (parallel processing)");
    setupKnob (kLoud,   "loud",     "LOUD",      "loudOn",      "Drive into the peak limiter (ceiling 0 dBFS)");
    setupKnob (kOut,    "out",      "OUT",       {},            "Output level, after the limiter");

    setupSwitch (ottBtn,    ottAtt,    "ott",        "OTT",    "Switch the COMP knob between the compressor and the 3-band OTT");
    setupSwitch (unityBtn,  unityAtt,  "unity",      "UNITY",  "Keep the output loudness equal to the input (4 s average)");
    setupSwitch (liveBtn,   liveAtt,   "live",       "LIVE",   "Lower latency (shorter look-ahead for COMP / OTT and LOUD)");
    setupSwitch (osBtn,     osAtt,     "oversample", "192k",   "Process at about 192 kHz");
    setupSwitch (bypassBtn, bypassAtt, "bypass",     "BYPASS", "Latency-compensated bypass");

    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    matchBtn.setButtonText ("MATCH");
    matchBtn.setClickingTogglesState (true);
    matchBtn.setTooltip ("Gain Stage (same LINK, before this plugin): keep the output at Gain Stage's TARGET level");
    linkBar.addAndMakeVisible (matchBtn);
    matchAtt = std::make_unique<BA> (processor.apvts, "linkMatch", matchBtn);
    linkBar.extra = &matchBtn; linkBar.extraWidth = 64;
    linkBar.setSources (processor.link, { plink::kindGainStage });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! processor.link.on()) return "LINK OFF - same LINK as Gain Stage to match levels";
        if (! processor.match.sawStage()) return "waiting for Gain Stage (RMS mode)";
        lit = true;
        const float t = processor.match.trimDb();
        return "matched to TARGET (" + juce::String (t > 0.05f ? "+" : "") + juce::String (t, 1) + " dB)";
    };

    setSize (980, 300 + footerH);
    startTimerHz (20);
}

FireBoxEditor::~FireBoxEditor() { setLookAndFeel (nullptr); }

void FireBoxEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, const juce::String& onId, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, fbui::text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);
    k.att = std::make_unique<SA> (processor.apvts, id, k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, fbui::text);
    addAndMakeVisible (k.label);
    k.onId = onId;
    if (onId.isNotEmpty())
    {
        k.onBtn.setButtonText ("ON");
        k.onBtn.setClickingTogglesState (true);
        k.onBtn.setTooltip ("Turn " + name + " on / off");
        addAndMakeVisible (k.onBtn);
        k.onAtt = std::make_unique<BA> (processor.apvts, onId, k.onBtn);
    }
}

void FireBoxEditor::setupSwitch (juce::TextButton& b, std::unique_ptr<BA>& att, const juce::String& id, const juce::String& t, const juce::String& tip)
{
    b.setButtonText (t); b.setClickingTogglesState (true); b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<BA> (processor.apvts, id, b);
}

void FireBoxEditor::timerCallback()
{
    inL = processor.inMeter.lufs.load(); outL = processor.outMeter.lufs.load();
    inP = processor.inMeter.peak.load(); outP = processor.outMeter.peak.load();
    for (Knob* k : { &kBass, &kThick, &kBody, &kComp, &kSc, &kCrunch, &kLoud })
        k->slider.setEnabled (k->onBtn.getToggleState());
    kComp.label.setText (ottBtn.getToggleState() ? "OTT" : "COMP", juce::dontSendNotification);
    repaint (0, 0, getWidth(), 50);
}

void FireBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (fbui::bg);
    auto top = getLocalBounds().removeFromTop (50);
    g.setColour (fbui::panel); g.fillRect (top);
    g.setColour (fbui::ember); g.fillRect (top.removeFromBottom (2));

    // タイトル（炎の三角形 + 名前）
    juce::Path flame; flame.startNewSubPath (22, 38); flame.quadraticTo (14, 24, 24, 10); flame.quadraticTo (26, 22, 32, 18); flame.quadraticTo (38, 30, 30, 38); flame.closeSubPath();
    g.setColour (fbui::ember); g.fillPath (flame);
    g.setColour (fbui::text); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("FIRE BOX", 44, 8, 160, 32, juce::Justification::centredLeft);

    // メーター（短期ラウドネスとピーク）
    auto meter = [&] (int x, const char* name, float lufs, float pk)
    {
        g.setColour (fbui::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, x, 8, 34, 14, juce::Justification::centredLeft);
        g.setColour (fbui::text); g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (lufs > -99 ? juce::String (lufs, 1) + " LUFS" : juce::String ("-- LUFS"), x, 22, 110, 20, juce::Justification::centredLeft);
        const float pdb = juce::Decibels::gainToDecibels (pk, -60.0f);
        const int w = (int) juce::jmap (juce::jlimit (-60.0f, 3.0f, pdb), -60.0f, 3.0f, 0.0f, 70.0f);
        g.setColour (fbui::ash); g.fillRect (x + 36, 11, 70, 6);
        g.setColour (pdb > -0.1f ? juce::Colours::red : fbui::ember); g.fillRect (x + 36, 11, w, 6);
    };
    meter (230, "IN", inL, inP);
    meter (360, "OUT", outL, outP);

    // 処理の流れの区切り
    g.setColour (fbui::panel);
    const int w = getWidth() / 10;
    for (int i : { 1, 4, 7 }) g.fillRect (i * w, 60, 1, getHeight() - footerH - 70);
}

void FireBoxEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (50).reduced (10, 11);
    for (auto* b : { &bypassBtn, &osBtn, &liveBtn, &unityBtn })
    {
        b->setBounds (top.removeFromRight (70));
        top.removeFromRight (6);
    }

    // 画面のいちばん下（Plugin Link と プリセット・取り消し）を 2 段で
    auto f = r.removeFromBottom (footerH).reduced (8, 5);
    linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
    f.removeFromTop (4);
    presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));

    auto main = r.reduced (4, 10);
    const int w = main.getWidth() / 10;
    for (Knob* k : { &kIn, &kBass, &kThick, &kBody, &kComp, &kSc, &kCrunch, &kMix, &kLoud, &kOut })
    {
        auto col = main.removeFromLeft (w).reduced (4, 0);
        auto btnRow = col.removeFromTop (24);
        if (k->onId.isNotEmpty()) k->onBtn.setBounds (btnRow.withSizeKeepingCentre (44, 20));
        if (k == &kComp) { k->onBtn.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2).withSizeKeepingCentre (40, 20)); ottBtn.setBounds (btnRow.withSizeKeepingCentre (44, 20)); }
        col.removeFromTop (6);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col.removeFromTop (juce::jmin (col.getHeight(), w + 14)));
    }
}
