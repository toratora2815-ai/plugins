#include "PluginEditor.h"
#include <complex>

namespace
{
const juce::Colour bg (0xff1b1714), panel (0xff26201b), panelHi (0xff302820), ink (0xffeadfce), dim (0xff9c8f7e),
                   amber (0xffe7a04a), amberDim (0xff7a5426), teal (0xff7fc4b8);

std::complex<double> onePoleLP (double f, double fc, double fs)
{
    const double k = std::tan (juce::MathConstants<double>::pi * std::min (fc, fs * 0.49) / fs);
    const double a = k / (1 + k), b = (1 - k) / (1 + k);
    const auto z1 = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / fs);   // z^-1
    return a * (1.0 + z1) / (1.0 - b * z1);
}
}

// ---------------------------------------------------------------- 見た目
ElderLook::ElderLook()
{
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, amberDim);
    setColour (juce::ComboBox::arrowColourId, amber);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amberDim);
    setColour (juce::PopupMenu::headerTextColourId, amber);
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, bg);
}

void ElderLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float rad = std::min (r.getWidth(), r.getHeight()) / 2.0f;
    const auto c = r.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    // 目盛り
    g.setColour (dim.withAlpha (0.35f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        g.drawLine (c.x + std::sin (a) * (rad + 1), c.y - std::cos (a) * (rad + 1), c.x + std::sin (a) * (rad - 3), c.y - std::cos (a) * (rad - 3), 1.0f);
    }
    juce::Path track; track.addCentredArc (c.x, c.y, rad - 6, rad - 6, 0, a0, a1, true);
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, rad - 6, rad - 6, 0, a0, ang, true);
    g.setColour (s.isEnabled() ? amber : dim); g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // つまみ本体
    const float kr = rad - 11;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a3e33), c.x, c.y - kr, juce::Colour (0xff1e1915), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);
    g.setColour (ink);
    g.drawLine (c.x + std::sin (ang) * kr * 0.25f, c.y - std::cos (ang) * kr * 0.25f, c.x + std::sin (ang) * kr * 0.9f, c.y - std::cos (ang) * kr * 0.9f, 2.2f);
}

void ElderLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? amber : (down ? panelHi.brighter (0.2f) : over ? panelHi.brighter (0.1f) : panelHi));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? amber.brighter (0.3f) : amberDim.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ElderLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (panelHi); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? amber : amberDim); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow; const float ax = (float) w - 16, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (amber); g.fillPath (arrow);
}

// ---------------------------------------------------------------- 減り方のグラフ
void DecayGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15110e)); g.fillRoundedRectangle (r, 6.0f);
    auto plot = r.reduced (34, 14).withTrimmedBottom (8);
    const auto p = proc.currentParams();
    const auto& pr = proc.profileFor (p.mode);
    using elder::Maps;
    const double fs = 48000.0;
    const double rtMid = Maps::decaySec (p.decay) * pr.rtScale;
    const double bm = Maps::bassMult (p.bassMult);
    const double loopSec = pr.loopMs / 1000.0 * Maps::sizeFactor (p.size, pr.sizeMin);
    const double bLen = loopSec / 4.0;
    const double gMid = std::pow (10.0, -3.0 * bLen / rtMid), gLow = std::pow (10.0, -3.0 * bLen / (rtMid * bm));
    const double hs = std::pow (10.0, Maps::highShelfDb (p.highShelf) * pr.hsScale / 80.0);
    const double xo = Maps::bassXoverHz (p.bassXover), hf = Maps::highFreqHz (p.highFreq);
    auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    const double tLo = 0.1, tHi = 100.0;
    auto yOf = [&] (double t) { t = juce::jlimit (tLo, tHi, t); return plot.getBottom() - (float) (std::log (t / tLo) / std::log (tHi / tLo)) * plot.getHeight(); };

    g.setFont (juce::FontOptions (10.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        g.setColour (dim.withAlpha (0.15f)); g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (dim.withAlpha (0.7f));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xOf (f) - 15, (int) plot.getBottom() + 2, 30, 12, juce::Justification::centred);
    }
    for (double t : { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0 })
    {
        g.setColour (dim.withAlpha (0.15f)); g.drawHorizontalLine ((int) yOf (t), plot.getX(), plot.getRight());
        g.setColour (dim.withAlpha (0.7f));
        g.drawText (juce::String (t, t < 1 ? 1 : 0) + "s", 0, (int) yOf (t) - 6, 30, 12, juce::Justification::centredRight);
    }
    juce::Path path;
    if (pr.kind == 1)
    {   // ゲートは長さが決まっている
        const double len = pr.gateMs / 1000.0 * Maps::sizeFactor (p.size, pr.sizeMin);
        path.startNewSubPath (plot.getX(), yOf (len)); path.lineTo (plot.getRight(), yOf (len));
    }
    else
        for (int i = 0; i <= 200; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 200.0);
            const auto L1 = onePoleLP (f, xo, fs), L2 = onePoleLP (f, hf, fs);
            const double branch = std::abs (gLow * L1 + gMid * (1.0 - L1)) * std::abs (L2 + hs * (1.0 - L2));
            const double rt = -60.0 * bLen / (20.0 * std::log10 (std::max (1e-9, branch)));
            const float x = xOf (f), y = yOf (rt);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
    juce::Path fill (path); fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (amber.withAlpha (0.28f), 0, plot.getY(), amber.withAlpha (0.02f), 0, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (amber); g.strokePath (path, juce::PathStrokeType (2.0f));
    g.setColour (ink); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    const juce::String rtText = pr.kind == 1 ? "GATE " + juce::String (pr.gateMs * Maps::sizeFactor (p.size, pr.sizeMin), 0) + " ms"
                                             : "RT " + juce::String (rtMid, rtMid < 10 ? 2 : 1) + " s";
    g.drawText (rtText, plot.toNearestInt().removeFromTop (16).withTrimmedRight (6), juce::Justification::centredRight);
    g.setColour (dim); g.setFont (juce::FontOptions (10.5f));
    g.drawText (juce::CharPointer_UTF8 ("周波数ごとの響きの長さ"), plot.toNearestInt().removeFromTop (16).withTrimmedLeft (6), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- 画面
ElderVerbEditor::ElderVerbEditor (ElderVerbProcessor& p) : AudioProcessorEditor (&p), proc (p), graph (p)
{
    setLookAndFeel (&look);
    const juce::StringArray labels { "MIX", "PRE-DELAY", "DECAY", "SIZE", "ATTACK", "BASS MULT", "BASS XOVER", "HIGH SHELF", "HIGH FREQ",
                                     "EARLY DIFF", "LATE DIFF", "MOD RATE", "MOD DEPTH", "HIGH CUT", "LOW CUT" };
    const auto& ids = ElderVerbProcessor::knobIds();
    for (int i = 0; i < ids.size(); ++i)
    {
        auto k = std::make_unique<Knob>();
        k->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
        k->s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k->s.setMouseDragSensitivity (220);
        k->name.setText (labels[i], juce::dontSendNotification);
        k->name.setJustificationType (juce::Justification::centred);
        k->name.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        k->name.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (k->s); addAndMakeVisible (k->name);
        k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], k->s);
        if (auto* prm = proc.apvts.getParameter (ids[i])) k->s.setDoubleClickReturnValue (true, prm->getDefaultValue());
        k->s.onValueChange = [this] { graph.repaint(); };
        knobs.push_back (std::move (k));
    }

    modeBox.addItemList (ElderVerbProcessor::modeNames(), 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    modeBox.onChange = [this] { graph.repaint(); };

    for (int i = 0; i < 3; ++i)
    {
        colorBtn[i].setButtonText (ElderVerbProcessor::colorNames()[i]);
        colorBtn[i].setClickingTogglesState (false);
        colorBtn[i].onClick = [this, i] {
            if (auto* prm = proc.apvts.getParameter ("color"))
            { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture(); }
        };
        addAndMakeVisible (colorBtn[i]);
    }
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.setButtonText ("SAVE");
    saveBtn.onClick = [this] { savePreset(); };
    auto step = [this] (int d) {
        const auto files = allPresetFiles();
        const auto& built = ElderVerbProcessor::builtinPresets();
        const int total = (int) built.size() + (int) files.size();
        if (total == 0) return;
        presetIndex = ((presetIndex + d) % total + total) % total;
        if (presetIndex < (int) built.size()) proc.applyValues (built[(size_t) presetIndex].v, built[(size_t) presetIndex].name);
        else loadFile (files[(size_t) (presetIndex - (int) built.size())]);
    };
    prevBtn.onClick = [step] { step (-1); };
    nextBtn.onClick = [step] { step (1); };
    for (auto* b : { &presetBtn, &saveBtn, &prevBtn, &nextBtn }) addAndMakeVisible (*b);
    addAndMakeVisible (graph);
    addAndMakeVisible (linkBar);
    addAndMakeVisible (presetBar);
    duckSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    duckSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    duckSlider.setTooltip ("Fader Pilot (same LINK, on the vocal): lower the reverb by this much while the voice is singing");
    linkBar.addAndMakeVisible (duckSlider);
    duckAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "linkDuck", duckSlider);
    linkBar.extra = &duckSlider; linkBar.extraWidth = 150; linkBar.extraLabel = "VOCAL DUCK";
    linkBar.setSources (proc.link, { plink::kindFaderPilot });
    linkBar.status = [this] (bool& lit) -> juce::String
    {
        if (! proc.link.on()) return "Plugin Link OFF - same LINK as Fader Pilot to duck the reverb while singing";
        if (! proc.duck.sawPilot()) return "waiting for Fader Pilot";
        lit = proc.duck.nowDb() < -0.1f;
        return lit ? "voice singing: reverb " + juce::String (proc.duck.nowDb(), 1) + " dB" : juce::String ("voice resting: full reverb");
    };

    setSize (860, 520 + footerH);
    startTimerHz (20);
    timerCallback();
}

ElderVerbEditor::~ElderVerbEditor() { setLookAndFeel (nullptr); }

ElderVerbEditor::Knob& ElderVerbEditor::knob (const juce::String& id)
{
    return *knobs[(size_t) ElderVerbProcessor::knobIds().indexOf (id)];
}

void ElderVerbEditor::timerCallback()
{
    const int c = (int) proc.apvts.getRawParameterValue ("color")->load();
    for (int i = 0; i < 3; ++i) colorBtn[i].setToggleState (i == c, juce::dontSendNotification);
    const auto name = proc.getPresetName();
    if (name != lastPresetName) { lastPresetName = name; presetBtn.setButtonText (name); }
    const bool gate = proc.profileFor ((int) proc.apvts.getRawParameterValue ("mode")->load()).kind == 1;
    knob ("decay").s.setEnabled (! gate);   // ゲートは DECAY が効かない
    for (int ch = 0; ch < 2; ++ch) meter[ch] = std::max (proc.getLevel (ch), meter[ch] * 0.8f);
    repaint (getLocalBounds().removeFromRight (40).removeFromTop (60));
    static int count = 0; if (++count % 10 == 0) graph.repaint();
}

void ElderVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto top = getLocalBounds().withTrimmedBottom (footerH).removeFromTop (56);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c241d), 0, 0, bg, 0, 56, false));
    g.fillRect (top);
    g.setColour (amber); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("ELDER VERB", 18, 10, 200, 26, juce::Justification::centredLeft);
    g.setColour (dim); g.setFont (juce::FontOptions (10.5f));
    g.drawText ("vintage algorithmic reverb", 19, 33, 200, 14, juce::Justification::centredLeft);
    auto section = [&] (juce::Rectangle<int> r, const juce::String& title) {
        g.setColour (panel); g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (amberDim.withAlpha (0.5f)); g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);
        g.setColour (amber.withAlpha (0.85f)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 12, r.getY() + 6, 300, 14, juce::Justification::centredLeft);
    };
    section (secSpace, "SPACE");
    section (secEq, "DECAY EQ");
    section (secTex, "TEXTURE  /  OUTPUT");
    // 出力メーター
    auto m = juce::Rectangle<int> (getWidth() - 34, 12, 20, 34);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto b = m.removeFromLeft (9).toFloat(); m.removeFromLeft (2);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRect (b);
        const float db = juce::Decibels::gainToDecibels (meter[ch], -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -1.0f ? juce::Colours::red : teal);
        g.fillRect (b.removeFromBottom (b.getHeight() * frac));
    }
}

void ElderVerbEditor::resized()
{
    layoutFooter();
    auto r = getLocalBounds().withTrimmedBottom (footerH);
    auto top = r.removeFromTop (56).reduced (12, 12);
    top.removeFromLeft (200);
    top.removeFromRight (36);
    modeBox.setBounds (top.removeFromLeft (180)); top.removeFromLeft (10);
    for (auto& b : colorBtn) { b.setBounds (top.removeFromLeft (50)); top.removeFromLeft (4); }
    top.removeFromLeft (10);
    saveBtn.setBounds (top.removeFromRight (52)); top.removeFromRight (6);
    nextBtn.setBounds (top.removeFromRight (26)); top.removeFromRight (2);
    presetBtn.setBounds (top.removeFromRight (std::max (80, top.getWidth() - 28)));
    prevBtn.setBounds (top.removeFromRight (26));

    r = r.reduced (12, 6);
    auto row1 = r.removeFromTop (148); r.removeFromTop (8);
    auto row2 = r.removeFromTop (148); r.removeFromTop (8);
    auto row3 = r.removeFromTop (148);
    secSpace = row1; secEq = row2; secTex = row3;

    auto place = [this] (juce::Rectangle<int> area, std::initializer_list<const char*> ids) {
        area = area.reduced (8, 0).withTrimmedTop (20);
        const int w = area.getWidth() / (int) ids.size();
        for (auto* id : ids)
        {
            auto cell = area.removeFromLeft (w);
            auto& k = knob (id);
            k.name.setBounds (cell.removeFromTop (14));
            k.s.setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 100), cell.getHeight() - 4));
        }
    };
    place (row1, { "mix", "predelay", "decay", "size", "attack" });
    auto eqKnobs = row2.removeFromLeft (row2.getWidth() * 52 / 100);
    secEq = secEq.withWidth (eqKnobs.getWidth());
    place (eqKnobs, { "bassMult", "bassXover", "highShelf", "highFreq" });
    row2.removeFromLeft (8);
    graph.setBounds (row2);
    place (row3, { "earlyDiff", "lateDiff", "modRate", "modDepth", "highCut", "lowCut" });
}

// ---------------------------------------------------------------- プリセット
std::vector<juce::File> ElderVerbEditor::allPresetFiles() const
{
    std::vector<juce::File> out;
    for (auto& f : ElderVerbProcessor::myPresetFolder().findChildFiles (juce::File::findFiles, true, "*.evpreset")) out.push_back (f);
    auto v = ElderVerbProcessor::vpresetFolder();
    if (v.isDirectory())
    {
        auto fl = v.findChildFiles (juce::File::findFiles, true, "*.vpreset");
        fl.sort();
        for (auto& f : fl) out.push_back (f);
    }
    return out;
}

void ElderVerbEditor::loadFile (const juce::File& f)
{
    if (f.hasFileExtension ("evpreset"))
    {
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr) return;
        std::map<juce::String, float> v;
        for (auto& id : ElderVerbProcessor::knobIds()) if (x->hasAttribute (id)) v[id] = (float) x->getDoubleAttribute (id);
        v["mode"] = (float) x->getIntAttribute ("mode"); v["color"] = (float) x->getIntAttribute ("color");
        proc.applyValues (v, x->getStringAttribute ("presetName", f.getFileNameWithoutExtension()));
    }
    else
    {
        const auto err = proc.importVPreset (f);
        if (err.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Elder Verb", f.getFileName() + ": " + err);
    }
}

void ElderVerbEditor::showPresetMenu()
{
    juce::PopupMenu m;
    const auto& built = ElderVerbProcessor::builtinPresets();
    m.addSectionHeader ("ELDER VERB");
    for (size_t i = 0; i < built.size(); ++i)
        m.addItem (juce::String (built[i].name), [this, i] { presetIndex = (int) i; proc.applyValues (ElderVerbProcessor::builtinPresets()[i].v, ElderVerbProcessor::builtinPresets()[i].name); });

    const auto files = allPresetFiles();
    juce::PopupMenu mine;
    std::map<juce::String, juce::PopupMenu> groups;
    std::vector<juce::String> order;
    const auto vroot = ElderVerbProcessor::vpresetFolder();
    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& f = files[i];
        auto act = [this, f, i, n = built.size()] { presetIndex = (int) (n + i); loadFile (f); };
        if (f.hasFileExtension ("evpreset")) { mine.addItem (f.getFileNameWithoutExtension(), act); continue; }
        const auto rel = f.getParentDirectory().getRelativePathFrom (vroot);
        if (! groups.count (rel)) order.push_back (rel);
        groups[rel].addItem (f.getFileNameWithoutExtension(), act);
    }
    m.addSeparator();
    m.addSubMenu (juce::CharPointer_UTF8 ("自分のプリセット"), mine, mine.getNumItems() > 0);
    if (! order.empty())
    {
        m.addSeparator();
        m.addSectionHeader (juce::CharPointer_UTF8 ("この Mac の VintageVerb のプリセット"));
        for (auto& g : order) m.addSubMenu (g.replace ("/", " / "), groups[g]);
    }
    m.addSeparator();
    m.addItem (juce::CharPointer_UTF8 ("ファイルから読み込む…"), [this] {
        chooser = std::make_unique<juce::FileChooser> ("Preset", ElderVerbProcessor::vpresetFolder(), "*.vpreset;*.evpreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn));
}

void ElderVerbEditor::savePreset()
{
    auto* w = new juce::AlertWindow ("Elder Verb", juce::CharPointer_UTF8 ("プリセットの名前"), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.getPresetName());
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
        if (r == 1)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
            {
                const auto err = proc.saveMyPreset (name);
                if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Elder Verb", err);
            }
        }
    }), true);
}

// 画面のいちばん下（Plugin Link と プリセット・取り消し）。幅が狭いときは 2 段
void ElderVerbEditor::layoutFooter()
{
    auto f = getLocalBounds().removeFromBottom (footerH).reduced (8, 5);
    if (footerH < 50)   // 1 段
    {
        presetBar.setBounds (f.removeFromRight (presetBar.preferredWidth()).withSizeKeepingCentre (presetBar.preferredWidth(), myp::PresetBar::height));
        f.removeFromRight (6);
        linkBar.setBounds (f.withSizeKeepingCentre (f.getWidth(), plink::LinkBar::height));
    }
    else
    {
        linkBar.setBounds (f.removeFromTop (plink::LinkBar::height));
        f.removeFromTop (4);
        presetBar.setBounds (f.removeFromTop (myp::PresetBar::height));
    }
}
