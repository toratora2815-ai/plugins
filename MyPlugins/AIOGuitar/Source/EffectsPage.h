#pragma once
// EFFECTS ページ: ペダルボード。今は Porch Drive（ドライブ）と Wool Fuzz（ファズ）。
// 中身は単体プラグインと同じ音。⇄ で順番を入れ替え、フットスイッチで ON/OFF（OFF は計算しない）。
#include "PluginProcessor.h"

class PedalPanel : public juce::Component
{
public:
    struct Look { juce::Colour body, ink, knob; };

    PedalPanel (juce::AudioProcessorValueTreeState& s, juce::String nameIn, juce::String subIn, const char* onId,
                std::initializer_list<std::pair<const char*, const char*>> knobsIn, Look lookIn)
        : state (s), name (std::move (nameIn)), sub (std::move (subIn)), look (lookIn)
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

        // name plate
        g.setColour (look.ink);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (name, r.withTrimmedTop (r.getHeight() * 0.50f).withHeight (32.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (sub, r.withTrimmedTop (r.getHeight() * 0.50f + 30.0f).withHeight (18.0f), juce::Justification::centred);

        // LED
        const bool on = footswitch.getToggleState();
        auto led = juce::Rectangle<float> (r.getCentreX() - 6.0f, r.getY() + r.getHeight() * 0.50f - 22.0f, 12.0f, 12.0f);
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
        auto r = getLocalBounds().reduced (18, 18);
        auto top = r.removeFromTop (juce::jmin (130, r.getHeight() / 2 - 30));
        const int w = top.getWidth() / juce::jmax (1, (int) knobs.size());
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto col = top.removeFromLeft (w);
            labels[i]->setBounds (col.removeFromBottom (18));
            knobs[i]->setBounds (col.reduced (4, 0));
        }
        footswitch.setBounds (r.removeFromBottom (54).withSizeKeepingCentre (130, 46));
    }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String name, sub;
    Look look;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
    juce::TextButton footswitch { "ON / OFF" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
};

class EffectsPage : public juce::Component, private juce::Timer
{
public:
    explicit EffectsPage (AIOGuitarProcessor& p)
        : proc (p),
          drive (p.apvts, "PORCH DRIVE", "overdrive", "pdOn", { { "pdLevel", "LEVEL" }, { "pdTone", "TONE" }, { "pdGain", "GAIN" } },
                 { juce::Colour (0xff2f6fb3), juce::Colour (0xff10284a), juce::Colour (0xff8fd0ff) }),
          fuzz (p.apvts, "WOOL FUZZ", "fuzz", "wfOn", { { "wfSustain", "SUSTAIN" }, { "wfTone", "TONE" }, { "wfVolume", "VOLUME" } },
                { juce::Colour (0xffb8b2a4), juce::Colour (0xff8a1c1c), juce::Colour (0xff8a1c1c) })
    {
        addAndMakeVisible (drive);
        addAndMakeVisible (fuzz);
        swap.setTooltip (juce::String (juce::CharPointer_UTF8 (u8"ペダルの順番を入れ替える")));
        swap.onClick = [this]
        {
            if (auto* o = proc.apvts.getParameter ("pedalOrder"))
                o->setValueNotifyingHost (o->getValue() > 0.5f ? 0.0f : 1.0f);
        };
        addAndMakeVisible (swap);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1d1e23));
        g.fillRoundedRectangle (r.withTrimmedTop (34.0f), 12.0f);
        g.setColour (juce::Colour (0xff8b8d96));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (u8"単体の Porch Drive・Wool Fuzz と同じ音。OFF のペダルは計算しません。音が鳴っていない間は EFFECTS と AMP を休ませます")),
                    r.removeFromTop (30.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        if (proc.fxSleeping.load())
        {
            g.setColour (juce::Colour (0xff4cc46a));
            g.drawText (juce::String (juce::CharPointer_UTF8 (u8"休止中（弾くと戻ります）")), getLocalBounds().removeFromTop (30).withTrimmedRight (6), juce::Justification::centredRight);
        }
        // signal flow
        auto flow = board.toFloat();
        g.setColour (juce::Colour (0xff8b8d96));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("IN", flow.removeFromLeft (50.0f), juce::Justification::centred);
        g.drawText ("AMP >", flow.removeFromRight (70.0f), juce::Justification::centred);
        g.setColour (juce::Colour (0xff3a3c44));
        g.fillRect (board.toFloat().withSizeKeepingCentre ((float) board.getWidth() - 100.0f, 4.0f));
    }

    void resized() override { layout(); }

private:
    void layout()
    {
        auto r = getLocalBounds().withTrimmedTop (50).reduced (20, 20);
        board = r;
        r.removeFromLeft (50); r.removeFromRight (70);
        const int pedalW = juce::jmin (300, (r.getWidth() - 90) / 2), pedalH = juce::jmin (440, r.getHeight());
        auto row = r.withSizeKeepingCentre (pedalW * 2 + 90, pedalH);
        auto first = row.removeFromLeft (pedalW), mid = row.removeFromLeft (90), second = row;
        const bool fuzzFirst = proc.apvts.getRawParameterValue ("pedalOrder")->load() > 0.5f;
        (fuzzFirst ? fuzz : drive).setBounds (first);
        (fuzzFirst ? drive : fuzz).setBounds (second);
        swap.setBounds (mid.withSizeKeepingCentre (56, 34));
        shownOrder = fuzzFirst;
    }

    void timerCallback() override
    {
        const bool fuzzFirst = proc.apvts.getRawParameterValue ("pedalOrder")->load() > 0.5f;
        if (fuzzFirst != shownOrder) { layout(); repaint(); }
        const bool sl = proc.fxSleeping.load();
        if (sl != shownSleep) { shownSleep = sl; repaint(); }
    }

    AIOGuitarProcessor& proc;
    PedalPanel drive, fuzz;
    juce::TextButton swap { juce::String (juce::CharPointer_UTF8 (u8"⇄")) };
    juce::Rectangle<int> board;
    bool shownOrder = false, shownSleep = false;
};
