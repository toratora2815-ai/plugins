#pragma once
// EFFECTS ページ: IN → [COTTON COMP / DRIVE A / DRIVE B / AMP / TREM を好きな順に] → CAB（いつも最後）→ OUT。
// DRIVE A / B（0.6.0）は「機種 ▾」で PORCH DRIVE / WOOL FUZZ / HEARTH DRIVE / BRASS BOOST / BARN DIST から選ぶ（DriveSlot.h）。
// TREM（0.4.0）は My Tremolo の小型版。つまみは DEPTH / RATE / SHAPE と小さい SYNC だけ（細かく作るときは後ろに My Tremolo）。
// 箱をドラッグするか ◀ ▶ で並べ替え。AMP の箱は小さく、押すと AMP ページ（つまみはそちら）。CAB の箱も押すと AMP ページ。
// ドライブ・ファズは単体プラグインと同じ音。フットスイッチで ON/OFF（OFF は計算しない）。
#include "PluginProcessor.h"
#include "AmpPage.h"

namespace FxUI
{
    inline juce::String jp (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    // ◀ ▶ (move one place)
    inline void styleArrow (juce::TextButton& b, juce::Colour ink)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.18f));
        b.setColour (juce::TextButton::textColourOffId, ink);
        b.setTooltip (jp (u8"1 つずらす（箱をドラッグしても入れ替えられます）"));
    }

    // six dots: "you can grab this"
    inline void drawGrip (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        g.setColour (ink.withAlpha (0.45f));
        const float cx = r.getCentreX(), y = r.getY() + 10.0f;
        for (int row = 0; row < 2; ++row)
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (cx + k * 7.0f - 1.6f, y + row * 6.0f - 1.6f, 3.2f, 3.2f);
    }
}

// one block of the chain that can be moved (a pedal or the amp)
class MovableBlock : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MovableBlock (int blockId) : block (blockId)
    {
        left.onClick  = [this] { if (onMove) onMove (block, -1); };
        right.onClick = [this] { if (onMove) onMove (block, +1); };
        addAndMakeVisible (left);
        addAndMakeVisible (right);
    }
    const int block;
    std::function<void (int block, int step)> onMove;
    void setArrowsEnabled (bool canLeft, bool canRight) { left.setEnabled (canLeft); right.setEnabled (canRight); }

protected:
    void placeArrows (juce::Rectangle<int> row)   // in the two bottom corners
    {
        left.setBounds (row.removeFromLeft (30).withSizeKeepingCentre (30, 28));
        right.setBounds (row.removeFromRight (30).withSizeKeepingCentre (30, 28));
    }
    juce::TextButton left { juce::String (juce::CharPointer_UTF8 (u8"◀")) }, right { juce::String (juce::CharPointer_UTF8 (u8"▶")) };
};

class PedalPanel : public MovableBlock
{
public:
    struct Look { juce::Colour body, ink, knob; };

    PedalPanel (juce::AudioProcessorValueTreeState& s, int blockId, juce::String nameIn, juce::String subIn, const char* onId,
                std::initializer_list<std::pair<const char*, const char*>> knobsIn, Look lookIn)
        : MovableBlock (blockId), name (std::move (nameIn)), sub (std::move (subIn)), look (lookIn)
    {
        for (auto& [id, label] : knobsIn)
        {
            auto k = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            k->setColour (juce::Slider::rotarySliderFillColourId, look.knob);
            k->setColour (juce::Slider::textBoxTextColourId, look.ink);
            k->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            k->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            auto* p = s.getParameter (id);
            k->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            addAndMakeVisible (*k);
            atts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *k));
            auto l = std::make_unique<juce::Label> ("", label);
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, look.ink);
            l->setInterceptsMouseClicks (false, false);   // the label area drags the pedal
            addAndMakeVisible (*l);
            knobs.push_back (std::move (k));
            labels.push_back (std::move (l));
        }
        footswitch.setClickingTogglesState (true);
        footswitch.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2e));
        footswitch.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a3a40));
        footswitch.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9a9aa2));
        footswitch.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        footswitch.onStateChange = [this] { repaint(); };
        addAndMakeVisible (footswitch);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, onId, footswitch);
        FxUI::styleArrow (left, look.ink); FxUI::styleArrow (right, look.ink);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0, 4), 14.0f);
        g.setColour (look.body);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (look.ink.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (5.0f), 10.0f, 1.2f);
        FxUI::drawGrip (g, r, look.ink);

        // name plate
        g.setColour (look.ink);
        g.setFont (juce::FontOptions (getWidth() < 240 ? 21.0f : 26.0f, juce::Font::bold));
        g.drawFittedText (name, plate.toNearestInt().withTrimmedTop (18).removeFromTop (30), juce::Justification::centred, 1);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (sub, plate.withTrimmedTop (48.0f).removeFromTop (18.0f), juce::Justification::centred);

        // LED
        const bool on = footswitch.getToggleState();
        auto led = juce::Rectangle<float> (plate.getCentreX() - 6.0f, plate.getY(), 12.0f, 12.0f);
        if (on)
        {
            g.setColour (juce::Colour (0xffff3b30).withAlpha (0.35f));
            g.fillEllipse (led.expanded (5.0f));
        }
        g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff4a1612));
        g.fillEllipse (led);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 22);
        auto bottom = r.removeFromBottom (54);
        footswitch.setBounds (bottom.withSizeKeepingCentre (juce::jmin (120, bottom.getWidth() - 70), 46));
        placeArrows (bottom);
        const int n = (int) knobs.size();
        if (getWidth() < 240 && n == 3)   // narrow: two knobs on top, the third under them (like many real pedals)
        {
            auto top = r.removeFromTop (juce::jmin (230, r.getHeight() - 90));
            auto row1 = top.removeFromTop (top.getHeight() / 2), row2 = top;
            const int w = row1.getWidth() / 2;
            place (0, row1.removeFromLeft (w)); place (1, row1);
            place (2, row2.withSizeKeepingCentre (w, row2.getHeight()));
        }
        else
        {
            auto top = r.removeFromTop (juce::jmin (130, r.getHeight() / 2 - 30));
            const int w = top.getWidth() / juce::jmax (1, n);
            for (int i = 0; i < n; ++i) place (i, top.removeFromLeft (w));
        }
        plate = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), juce::jmin (90.0f, (float) r.getHeight()));
    }

protected:
    void place (int i, juce::Rectangle<int> col)
    {
        labels[(size_t) i]->setBounds (col.removeFromBottom (18));
        knobs[(size_t) i]->setBounds (col.reduced (2, 0));
    }

    juce::String name, sub;
    Look look;
    juce::Rectangle<float> plate;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
    juce::TextButton footswitch { "ON / OFF" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
};

// DRIVE A / B（0.6.0）: 「機種 ▾」で 5 機種から選ぶ。つまみは箱ごとに同じパラメーター（GAIN / TONE / LEVEL）のままで、
// 名前・箱の上の並び・色だけが機種に合わせて変わる（PORCH DRIVE は前と同じ LEVEL / TONE / GAIN の並び）
class DrivePanel : public PedalPanel, private juce::Timer
{
public:
    struct Face { const char* name; const char* sub; const char* desc; Look look; const char* labels[3]; int order[3]; };

    static const Face& face (int m)
    {
        static const Face f[pedal::DriveSlotDSP::numModels] {
            { "PORCH DRIVE", "overdrive", u8"素直に広く歪むオーバードライブ（単体の Porch Drive と同じ音）",
              { juce::Colour (0xff2f6fb3), juce::Colour (0xff10284a), juce::Colour (0xff8fd0ff) }, { "GAIN", "TONE", "LEVEL" }, { 2, 1, 0 } },
            { "WOOL FUZZ", "fuzz", u8"太く長く伸びるファズ（単体の Wool Fuzz と同じ音）",
              { juce::Colour (0xffb8b2a4), juce::Colour (0xff8a1c1c), juce::Colour (0xff8a1c1c) }, { "SUSTAIN", "TONE", "VOLUME" }, { 0, 1, 2 } },
            { "HEARTH DRIVE", "overdrive", u8"中域が前に出て低音が締まるオーバードライブ。歪んだアンプの前で刻みを締める・リードを前に出す",
              { juce::Colour (0xffc8643b), juce::Colour (0xfffbefe0), juce::Colour (0xffffd9b8) }, { "DRIVE", "TONE", "LEVEL" }, { 0, 1, 2 } },
            { "BRASS BOOST", "treble booster", u8"高音を持ち上げて押し出すブースター。RANGE 右で高音だけ、左で全体。クランチのアンプの前でリードが伸びる",
              { juce::Colour (0xffc9a640), juce::Colour (0xff2a2010), juce::Colour (0xff6b4e12) }, { "BOOST", "RANGE", "LEVEL" }, { 0, 1, 2 } },
            { "BARN DIST", "distortion", u8"オーバードライブとファズの間の、硬く荒いディストーション。GAIN 0 はほぼ素通し",
              { juce::Colour (0xff8e2b22), juce::Colour (0xfff3e6d8), juce::Colour (0xffffc9a8) }, { "DIST", "TONE", "LEVEL" }, { 0, 1, 2 } },
        };
        return f[juce::jlimit (0, pedal::DriveSlotDSP::numModels - 1, m)];
    }

    DrivePanel (juce::AudioProcessorValueTreeState& s, int blockId, juce::String slotNameIn, const char* onId,
                const char* gainId, const char* toneId, const char* levelId, const char* modelIdIn)
        : PedalPanel (s, blockId, "", "", onId, { { gainId, "GAIN" }, { toneId, "TONE" }, { levelId, "LEVEL" } }, face (0).look),
          state (s), slotName (std::move (slotNameIn)), modelId (modelIdIn)
    {
        pick.setButtonText (FxUI::jp (u8"機種 ▾"));
        pick.setTooltip (FxUI::jp (u8"この箱のペダルを替える（つまみの値はそのまま）"));
        pick.onClick = [this] { showModels(); };
        addAndMakeVisible (pick);
        applyFace (model());
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        PedalPanel::paint (g);
        g.setColour (look.ink.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (slotName, getLocalBounds().reduced (16, 8).removeFromTop (14), juce::Justification::centredLeft);
    }

    void resized() override
    {
        PedalPanel::resized();   // knob i at place i (gain, tone, level)
        juce::Rectangle<int> kb[3], lb[3];
        for (int i = 0; i < 3; ++i) { kb[i] = knobs[(size_t) i]->getBounds(); lb[i] = labels[(size_t) i]->getBounds(); }
        const auto& f = face (shown);
        for (int p = 0; p < 3; ++p)   // place p shows knob order[p]
        {
            const int k = f.order[p];
            knobs[(size_t) k]->setBounds (kb[p]);
            labels[(size_t) k]->setBounds (lb[p]);
        }
        auto p = plate.toNearestInt();
        pick.setBounds (p.withSizeKeepingCentre (juce::jmin (110, p.getWidth() - 20), 22).withY (p.getY() + 50));
    }

private:
    int model() const { return juce::jlimit (0, pedal::DriveSlotDSP::numModels - 1, (int) state.getRawParameterValue (modelId)->load()); }

    void applyFace (int m)
    {
        shown = m;
        const auto& f = face (m);
        name = f.name; sub = {};
        look = f.look;
        for (int i = 0; i < 3; ++i)
        {
            knobs[(size_t) i]->setColour (juce::Slider::rotarySliderFillColourId, look.knob);
            knobs[(size_t) i]->setColour (juce::Slider::textBoxTextColourId, look.ink);
            labels[(size_t) i]->setText (f.labels[i], juce::dontSendNotification);
            labels[(size_t) i]->setColour (juce::Label::textColourId, look.ink);
        }
        FxUI::styleArrow (left, look.ink); FxUI::styleArrow (right, look.ink);
        pick.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.22f));
        pick.setColour (juce::TextButton::textColourOffId, look.ink);
        setTooltip (FxUI::jp (f.desc));
        if (getWidth() > 0) resized();
        repaint();
    }

    void showModels()
    {
        juce::PopupMenu menu;
        const int cur = model();
        for (int m = 0; m < pedal::DriveSlotDSP::numModels; ++m)
        {
            const auto& f = face (m);
            menu.addItem (m + 1, juce::String (f.name) + juce::String::fromUTF8 (u8"  —  ") + FxUI::jp (f.desc), true, m == cur);
        }
        juce::Component::SafePointer<DrivePanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pick), [safe] (int r)
        {
            if (safe != nullptr && r > 0) safe->choose (r - 1);
        });
    }

    void choose (int m)
    {
        if (auto* p = state.getParameter (modelId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
            p->endChangeGesture();
        }
        applyFace (m);
    }

    void timerCallback() override { if (model() != shown) applyFace (model()); }   // a preset, undo or Logic changed it

    juce::AudioProcessorValueTreeState& state;
    juce::String slotName;
    const char* modelId;
    int shown = -1;
    juce::TextButton pick;
};

// TREM: DEPTH / RATE / SHAPE。RATE は SYNC 中は音符の長さ（trRate）、SYNC OFF・単体アプリは Hz（trHz）に入れ替わる
class TremPanel : public PedalPanel, private juce::Timer
{
public:
    TremPanel (juce::AudioProcessorValueTreeState& s, int blockId, Look lookIn)
        : PedalPanel (s, blockId, "TREM", "tremolo", "trOn", { { "trDepth", "DEPTH" }, { "trRate", "RATE" }, { "trShape", "SHAPE" } }, lookIn),
          state (s)
    {
        knobs[2]->textFromValueFunction = [] (double v) { return juce::String (pedal::TremDSP::shapeName ((int) std::lround (v))); };
        knobs[1]->textFromValueFunction = [] (double v) { return juce::String (pedal::TremDSP::rateName ((int) std::lround (v))); };
        knobs[0]->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
        for (auto& k : knobs) k->updateText();

        hz.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        hz.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        hz.setColour (juce::Slider::rotarySliderFillColourId, look.knob);
        hz.setColour (juce::Slider::textBoxTextColourId, look.ink);
        hz.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        hz.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        hz.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        hz.setDoubleClickReturnValue (true, 5.5);
        addChildComponent (hz);
        hzAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, "trHz", hz);
        hz.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " Hz"; };
        hz.updateText();

        sync.setButtonText ("SYNC");
        sync.setClickingTogglesState (true);
        sync.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.25f));
        sync.setColour (juce::TextButton::buttonOnColourId, look.knob);
        sync.setColour (juce::TextButton::textColourOffId, look.ink);
        sync.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        sync.setTooltip (FxUI::jp (u8"ON = Logic のテンポと拍に合わせる（RATE は音符の長さ）、OFF = RATE を Hz で"));
        addAndMakeVisible (sync);
        syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, "trSync", sync);
        if (AIOGuitarProcessor::isStandaloneApp())   // 単体アプリには Logic のテンポがない: いつも Hz
        {
            sync.setEnabled (false);
            sync.setTooltip (FxUI::jp (u8"単体アプリでは RATE は Hz だけ（Logic のテンポがないため）"));
        }
        setTooltip (FxUI::jp (u8"トレモロ（My Tremolo の小型版）。AMP の後ろ = 本物のアンプのトレモロ、前 = ペダル（歪みの量も揺れる）"));
        swapRate();
        startTimerHz (8);
    }

    void resized() override
    {
        PedalPanel::resized();
        hz.setBounds (knobs[1]->getBounds());
        auto p = plate.toNearestInt();
        sync.setBounds (p.withSizeKeepingCentre (64, 22).withY (p.getBottom() - 24));
    }

private:
    bool hzMode() const { return AIOGuitarProcessor::isStandaloneApp() || state.getRawParameterValue ("trSync")->load() < 0.5f; }
    void swapRate()
    {
        const bool h = hzMode();
        hz.setVisible (h);
        knobs[1]->setVisible (! h);
    }
    void timerCallback() override { if (hz.isVisible() != hzMode()) swapRate(); }

    juce::AudioProcessorValueTreeState& state;
    juce::Slider hz;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hzAtt;
    juce::TextButton sync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
};

// the amp in the chain: a small head. Its knobs are on the AMP page (clicking the head goes there)
class AmpBlock : public MovableBlock
{
public:
    explicit AmpBlock (AIOGuitarProcessor& p) : MovableBlock (rig::blkAmp), proc (p)
    {
        onButton.setClickingTogglesState (true);
        onButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2e));
        onButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe05a47));
        onButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        onButton.onStateChange = [this] { repaint(); };
        addAndMakeVisible (onButton);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "ampOn", onButton);
        edit.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.3f));
        edit.onClick = [this] { if (onEdit) onEdit(); };
        edit.setTooltip (FxUI::jp (u8"AMP ページで機種・つまみを調整"));
        addAndMakeVisible (edit);
        setTooltip (FxUI::jp (u8"アンプ。押すと AMP ページ、ドラッグで位置を変える"));
    }
    std::function<void()> onEdit;

    // what the head shows (repaint when it changes)
    juce::String key() const
    {
        juce::String k (model());
        for (auto* id : { "ampGain", "ampBass", "ampMid", "ampTreble", "ampPresence", "ampMaster" })
            k << "/" << juce::String (v (id), 1);
        return k;
    }

    void paint (juce::Graphics& g) override
    {
        const auto f = AmpUI::face (model());
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
        g.setColour (f.tolex);
        g.fillRoundedRectangle (r, 12.0f);
        FxUI::drawGrip (g, r, f.ink == juce::Colour (0xff1a1a1a) ? juce::Colour (0xffe8e6e1) : f.ink);
        auto pl = r.reduced (12.0f).withTrimmedTop (16.0f).withTrimmedBottom (108.0f);
        g.setColour (f.plate);
        g.fillRoundedRectangle (pl, 6.0f);
        g.setColour (f.ink.withAlpha (0.25f));
        g.drawRoundedRectangle (pl.reduced (3.0f), 4.0f, 1.0f);

        auto t = pl.reduced (12.0f, 10.0f);
        g.setColour (f.ink);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("AMP", t.removeFromTop (18.0f), juce::Justification::centredLeft);
        // power lamp
        const bool on = onButton.getToggleState();
        auto lamp = juce::Rectangle<float> (pl.getRight() - 24.0f, pl.getY() + 12.0f, 10.0f, 10.0f);
        if (on) { g.setColour (juce::Colour (0xffff3b30).withAlpha (0.35f)); g.fillEllipse (lamp.expanded (4.0f)); }
        g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff4a1612));
        g.fillEllipse (lamp);
        g.setColour (f.ink);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawFittedText (ampsim::modelNames()[model()].toUpperCase(), t.removeFromTop (52.0f).toNearestInt(), juce::Justification::centredLeft, 2);
        t.removeFromTop (8.0f);
        // the knobs as numbers (set on the AMP page)
        g.setFont (juce::FontOptions (12.0f));
        const std::pair<const char*, const char*> rows[] { { "GAIN", "ampGain" }, { "BASS", "ampBass" }, { "MIDDLE", "ampMid" },
                                                           { "TREBLE", "ampTreble" }, { "PRESENCE", "ampPresence" }, { "MASTER", "ampMaster" } };
        for (auto& [label, id] : rows)
        {
            auto line = t.removeFromTop (17.0f);
            if (line.getBottom() > pl.getBottom() - 6.0f) break;
            g.setColour (f.ink.withAlpha (0.75f));
            g.drawText (label, line, juce::Justification::centredLeft);
            g.setColour (f.ink);
            g.drawText (juce::String (v (id), 1), line, juce::Justification::centredRight);
        }
        if (! on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (pl, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (FxUI::jp (u8"AMP OFF（素通り）"), pl, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 22);
        auto bottom = r.removeFromBottom (54);
        onButton.setBounds (bottom.withSizeKeepingCentre (juce::jmin (120, bottom.getWidth() - 70), 46));
        placeArrows (bottom);
        r.removeFromBottom (8);
        edit.setBounds (r.removeFromBottom (32).reduced (4, 0));
    }

private:
    int model() const { return juce::jlimit (0, ampsim::numModels - 1, (int) proc.apvts.getRawParameterValue ("ampModel")->load()); }
    float v (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    AIOGuitarProcessor& proc;
    juce::TextButton onButton { "AMP ON" };
    juce::TextButton edit { juce::String (juce::CharPointer_UTF8 (u8"AMP 画面で調整 ▸")) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
};

class EffectsPage : public juce::Component, private juce::Timer
{
public:
    explicit EffectsPage (AIOGuitarProcessor& p)
        : proc (p),
          comp (p.apvts, rig::blkComp, "COTTON COMP", "compressor", "ccOn", { { "ccComp", "COMP" }, { "ccLevel", "LEVEL" }, { "ccBlend", "BLEND" } },
                { juce::Colour (0xffe9e4d8), juce::Colour (0xff2d5c58), juce::Colour (0xff3a8a80) }),
          drive (p.apvts, rig::blkDrive, "DRIVE A", "pdOn", "pdGain", "pdTone", "pdLevel", "pdModel"),
          fuzz (p.apvts, rig::blkFuzz, "DRIVE B", "wfOn", "wfSustain", "wfTone", "wfVolume", "wfModel"),
          ampBlock (p),
          trem (p.apvts, rig::blkTrem, { juce::Colour (0xff24504b), juce::Colour (0xffe8f3f1), juce::Colour (0xff7fd6c8) })
    {
        for (auto* b : blocks())
        {
            addAndMakeVisible (*b);
            b->addMouseListener (this, false);   // drag the block by its body (the knobs keep their own drags)
            b->setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            b->onMove = [this] (int block, int step)
            {
                const auto o = currentOrder();
                setOrder (rig::moved (o, block, rig::posOf (o, block) + step));
            };
        }
        ampBlock.onEdit = [this] { if (onEditAmp) onEditAmp(); };
        startTimerHz (10);
    }

    std::function<void()> onEditAmp;   // set by the editor: show the AMP page

    void paint (juce::Graphics& g) override
    {
        using FxUI::jp;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1d1e23));
        g.fillRoundedRectangle (r.withTrimmedTop (34.0f), 12.0f);
        g.setColour (juce::Colour (0xff8b8d96));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (jp (u8"箱をドラッグか ◀ ▶ で順番を入れ替え。AMP の箱を押すと AMP ページ。CAB（スピーカー）はいつも最後。TREM は AMP の後ろが本物のアンプのトレモロ"),
                    r.removeFromTop (30.0f).withTrimmedLeft (4.0f).withTrimmedRight (150.0f), juce::Justification::centredLeft);
        if (proc.fxSleeping.load())
        {
            g.setColour (juce::Colour (0xff4cc46a));
            g.drawText (jp (u8"休止中（弾くと戻ります）"), getLocalBounds().removeFromTop (30).withTrimmedRight (6), juce::Justification::centredRight);
        }

        // the signal line, with an arrow between the blocks
        const float y = (float) row.getCentreY();
        g.setColour (juce::Colour (0xff3a3c44));
        g.fillRect (juce::Rectangle<float> ((float) row.getX() + 30.0f, y - 2.0f, (float) row.getWidth() - 60.0f, 4.0f));
        g.setColour (juce::Colour (0xff8b8d96));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("IN", juce::Rectangle<int> (row.getX(), row.getCentreY() - 12, kInW, 24), juce::Justification::centred);
        g.drawText ("OUT", juce::Rectangle<int> (row.getRight() - kOutW, row.getCentreY() - 12, kOutW, 24), juce::Justification::centred);
        for (int k = 0; k <= rig::numBlocks; ++k)   // between the blocks, and before the CAB
        {
            const float x = k < rig::numBlocks ? (float) slots[(size_t) k].getX() - kGap * 0.5f : (float) cabBox.getX() - kGap * 0.5f;
            juce::Path a; a.addTriangle (x - 5.0f, y - 7.0f, x - 5.0f, y + 7.0f, x + 6.0f, y);
            g.fillPath (a);
        }

        // pedals after the amp: "AMP の後" (between the amp and the speaker)
        const auto o = currentOrder();
        const int ampAt = rig::posOf (o, rig::blkAmp);
        for (int k = ampAt + 1; k < rig::numBlocks; ++k)
        {
            auto s = slots[(size_t) k];
            g.setColour (juce::Colour (0xffe05a47).withAlpha (0.8f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (jp (u8"AMP の後"), s.withY (s.getBottom() + 6).withHeight (18), juce::Justification::centred);
        }

        paintCab (g);
    }

    void resized() override { layout(); }

    // ---- drag to reorder
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBlock = nullptr; dragging = false;
        cabPressed = e.eventComponent == this && cabBox.contains (e.getPosition());
        for (auto* b : blocks())
            if (e.eventComponent == b) { dragBlock = b; dragStart = b->getPosition(); }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBlock == nullptr) return;
        const int dx = e.getDistanceFromDragStartX();
        if (! dragging && std::abs (dx) < 6) return;
        dragging = true;
        dragBlock->toFront (false);
        const int x = juce::jlimit (slots[0].getX(), slots[(size_t) rig::numBlocks - 1].getX(), dragStart.x + dx);
        dragBlock->setTopLeftPosition (x, slots[0].getY());
        // where it would land: the slot nearest to its centre; the others make room
        const auto preview = rig::moved (currentOrder(), dragBlock->block, slotNearest (x + slots[0].getWidth() / 2));
        placeBlocks (preview, dragBlock);
        previewOrder = preview;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (cabPressed)   // the CAB box: its settings are on the AMP page
        {
            cabPressed = false;
            if (e.eventComponent == this && cabBox.contains (e.getPosition()) && onEditAmp) onEditAmp();
            return;
        }
        if (dragBlock == nullptr) return;
        if (dragging) setOrder (previewOrder);
        else if (dragBlock == &ampBlock && onEditAmp) onEditAmp();   // a click on the amp (no drag): its page
        dragBlock = nullptr; dragging = false;
        layout();
    }

private:
    static constexpr int kInW = 34, kOutW = 40, kGap = 18, kCabW = 120;

    std::array<MovableBlock*, rig::numBlocks> blocks() { return { &comp, &drive, &fuzz, &ampBlock, &trem }; }
    MovableBlock* blockFor (int id) { return blocks()[(size_t) juce::jlimit (0, rig::numBlocks - 1, id)]; }

    rig::Order currentOrder() const { return rig::orderAt ((int) proc.apvts.getRawParameterValue ("fxChain")->load()); }

    void setOrder (const rig::Order& o)
    {
        if (auto* p = proc.apvts.getParameter ("fxChain"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) rig::indexOf (o)));
            p->endChangeGesture();
        }
        layout();
        repaint();
    }

    int slotNearest (int centreX) const
    {
        int best = 0;
        for (int k = 1; k < rig::numBlocks; ++k)
            if (std::abs (slots[(size_t) k].getCentreX() - centreX) < std::abs (slots[(size_t) best].getCentreX() - centreX)) best = k;
        return best;
    }

    void layout()
    {
        auto r = getLocalBounds().withTrimmedTop (50).reduced (16, 12);
        r.removeFromBottom (26);   // room for "AMP の後"
        const int h = juce::jmin (470, r.getHeight());
        row = r.withSizeKeepingCentre (r.getWidth(), h);
        auto inner = row.withTrimmedLeft (kInW + kGap).withTrimmedRight (kOutW + kGap);
        cabBox = inner.removeFromRight (kCabW).withSizeKeepingCentre (kCabW, juce::jmin (220, h));
        inner.removeFromRight (kGap);
        const int w = (inner.getWidth() - kGap * (rig::numBlocks - 1)) / rig::numBlocks;
        for (int k = 0; k < rig::numBlocks; ++k)
        {
            slots[(size_t) k] = inner.removeFromLeft (w);
            inner.removeFromLeft (kGap);
        }
        if (! dragging) placeBlocks (currentOrder(), nullptr);
        shownOrder = (int) proc.apvts.getRawParameterValue ("fxChain")->load();
    }

    void placeBlocks (const rig::Order& o, MovableBlock* skip)
    {
        for (int k = 0; k < rig::numBlocks; ++k)
        {
            auto* b = blockFor (o[(size_t) k]);
            b->setArrowsEnabled (k > 0, k < rig::numBlocks - 1);
            if (b != skip) b->setBounds (slots[(size_t) k]);
        }
        repaint();
    }

    void paintCab (juce::Graphics& g)
    {
        using FxUI::jp;
        auto c = cabBox.toFloat();
        const bool on = proc.apvts.getRawParameterValue ("cabOn")->load() > 0.5f;
        const int type = juce::jlimit (0, ampsim::numCabs - 1, (int) proc.apvts.getRawParameterValue ("cabType")->load());
        const int mic = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("cabMicType")->load());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (c.translated (0, 4), 10.0f);
        g.setColour (juce::Colour (0xff2a2b30));
        g.fillRoundedRectangle (c, 10.0f);
        auto grille = c.reduced (10.0f).removeFromTop (c.getHeight() * 0.38f);
        g.setColour (juce::Colour (0xff3a3833));
        g.fillRoundedRectangle (grille, 5.0f);
        g.setColour (juce::Colour (0xff4a4740));
        for (float yy = grille.getY() + 4.0f; yy < grille.getBottom(); yy += 5.0f) g.drawHorizontalLine ((int) yy, grille.getX() + 4.0f, grille.getRight() - 4.0f);
        auto t = c.reduced (10.0f).withTrimmedTop (c.getHeight() * 0.38f + 8.0f);
        g.setColour (juce::Colour (0xffe8e6e1));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (on ? "CAB" : "CAB OFF", t.removeFromTop (20.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colour (0xffb8b8c0));
        const auto irName = proc.getCabIRName();
        g.drawFittedText (type == ampsim::numCabs - 1 ? (irName.isNotEmpty() ? irName : juce::String ("IR")) : ampsim::cabNames()[type],
                          t.removeFromTop (30.0f).toNearestInt(), juce::Justification::centred, 2);
        if (type != ampsim::numCabs - 1) g.drawText (ampsim::micNames()[mic], t.removeFromTop (16.0f), juce::Justification::centred);
        g.setColour (juce::Colour (0xff8b8d96));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (jp (u8"いつも最後"), c.removeFromBottom (20.0f), juce::Justification::centred);
    }

    void timerCallback() override
    {
        const int o = (int) proc.apvts.getRawParameterValue ("fxChain")->load();
        if (o != shownOrder && ! dragging) { layout(); repaint(); }
        const bool sl = proc.fxSleeping.load();
        if (sl != shownSleep) { shownSleep = sl; repaint(); }
        const auto k = ampBlock.key();
        if (k != shownAmp) { shownAmp = k; ampBlock.repaint(); }
        const auto ck = juce::String (proc.apvts.getRawParameterValue ("cabOn")->load()) + "/" + juce::String (proc.apvts.getRawParameterValue ("cabType")->load())
                      + "/" + juce::String (proc.apvts.getRawParameterValue ("cabMicType")->load()) + "/" + proc.getCabIRName();
        if (ck != shownCab) { shownCab = ck; repaint (cabBox.expanded (6)); }
    }

    AIOGuitarProcessor& proc;
    PedalPanel comp;
    DrivePanel drive, fuzz;
    AmpBlock ampBlock;
    TremPanel trem;
    std::array<juce::Rectangle<int>, rig::numBlocks> slots;
    juce::Rectangle<int> row, cabBox;
    MovableBlock* dragBlock = nullptr;
    juce::Point<int> dragStart;
    bool dragging = false, cabPressed = false;
    rig::Order previewOrder { 0, 1, 2, 3, 4 };
    int shownOrder = -1;
    bool shownSleep = false;
    juce::String shownAmp, shownCab;
};
