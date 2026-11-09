#pragma once
// AMP ページ: アンプのヘッド（5 種類、GAIN / BASS / MIDDLE / TREBLE / PRESENCE / MASTER）、
// キャビネットとマイク（箱の種類、DYNAMIC / CONDENSER / RIBBON、位置 CENTER〜EDGE、距離 CLOSE〜FAR、IR の読み込み）、出力（リバーブ・OUTPUT）。
#include "PluginProcessor.h"

namespace AmpUI
{
    inline juce::String jp (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    const juce::Colour bg { 0xff1d1e23 }, text { 0xffe8e6e1 }, dim { 0xff8b8d96 }, panel { 0xff22242b };

    // each amp has its own face colour (tolex / panel / knobs)
    struct Face { juce::Colour tolex, plate, ink, knob; };
    inline Face face (int model)
    {
        switch (model)
        {
            case 0:  return { juce::Colour (0xff1c1c1e), juce::Colour (0xff2b2c31), juce::Colour (0xffd9dde3), juce::Colour (0xff6fb0e6) };   // American Clean
            case 1:  return { juce::Colour (0xffa88a5c), juce::Colour (0xff3a2f22), juce::Colour (0xfff0e2c4), juce::Colour (0xffe0b25a) };   // Tweed
            case 2:  return { juce::Colour (0xff3d2a24), juce::Colour (0xff8c3b2e), juce::Colour (0xfff2e6d6), juce::Colour (0xffe8c39a) };   // British Chime
            case 3:  return { juce::Colour (0xff111111), juce::Colour (0xffb79a52), juce::Colour (0xff1a1a1a), juce::Colour (0xffd8b25e) };   // British Crunch
            default: return { juce::Colour (0xff141416), juce::Colour (0xff2e3136), juce::Colour (0xffe05a47), juce::Colour (0xffe05a47) };  // Modern High Gain
        }
    }
}

class AmpKnobRow : public juce::Component
{
public:
    AmpKnobRow (juce::AudioProcessorValueTreeState& s, std::initializer_list<std::pair<const char*, const char*>> ks) : state (s)
    {
        for (auto& [id, label] : ks)
        {
            auto k = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
            k->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            k->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            auto* p = s.getParameter (id);
            k->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            addAndMakeVisible (*k);
            atts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *k));
            auto l = std::make_unique<juce::Label> ("", label);
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            addAndMakeVisible (*l);
            knobs.push_back (std::move (k));
            labels.push_back (std::move (l));
        }
    }
    void setColours (juce::Colour ink, juce::Colour knob)
    {
        for (auto& k : knobs) { k->setColour (juce::Slider::rotarySliderFillColourId, knob); k->setColour (juce::Slider::textBoxTextColourId, ink); }
        for (auto& l : labels) l->setColour (juce::Label::textColourId, ink);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int w = r.getWidth() / juce::jmax (1, (int) knobs.size());
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto col = r.removeFromLeft (w);
            labels[i]->setBounds (col.removeFromTop (16));
            knobs[i]->setBounds (col.reduced (4, 0));
        }
    }
private:
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
};

class AmpPage : public juce::Component, private juce::Timer
{
public:
    explicit AmpPage (AIOGuitarProcessor& p)
        : proc (p),
          ampKnobs (p.apvts, { { "ampGain", "GAIN" }, { "ampBass", "BASS" }, { "ampMid", "MIDDLE" }, { "ampTreble", "TREBLE" }, { "ampPresence", "PRESENCE" }, { "ampMaster", "MASTER" } }),
          micKnobs (p.apvts, { { "cabPos", "POSITION" }, { "cabDist", "DISTANCE" } }),
          outKnobs (p.apvts, { { "revMix", "REVERB" }, { "revSize", "SIZE" }, { "outGain", "OUTPUT" } })
    {
        using namespace AmpUI;
        auto setupCombo = [this] (juce::ComboBox& c, const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
        {
            if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) c.addItemList (pc->choices, 1);
            addAndMakeVisible (c);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c);
        };
        setupCombo (modelBox, "ampModel", modelAtt);
        setupCombo (cabBox, "cabType", cabAtt);
        setupCombo (micBox, "cabMicType", micAtt);
        for (auto* t : { &ampOn, &cabOn })
        {
            t->setClickingTogglesState (true);
            t->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2e));
            t->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe05a47));
            t->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible (*t);
        }
        ampOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "ampOn", ampOn);
        cabOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "cabOn", cabOn);
        for (auto* c : { &ampKnobs, &micKnobs, &outKnobs }) addAndMakeVisible (*c);
        micKnobs.setColours (text, juce::Colour (0xff9b7be0));
        outKnobs.setColours (text, juce::Colour (0xff5b8def));

        irButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (jp (u8"キャビネットの IR（WAV / AIFF）"),
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (! fc.getResult().existsAsFile()) return;
                                      irError = proc.loadCabIR (fc.getResult());
                                      refreshIR();
                                  });
        };
        addAndMakeVisible (irButton);
        irLabel.setFont (juce::FontOptions (12.0f));
        irLabel.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (irLabel);
        refreshIR();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace AmpUI;
        const int m = (int) proc.apvts.getRawParameterValue ("ampModel")->load();
        const auto f = face (m);
        // amp head: tolex frame, control plate, name
        auto h = head.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (h.translated (0, 4), 12.0f);
        g.setColour (f.tolex);
        g.fillRoundedRectangle (h, 12.0f);
        auto plate = h.reduced (16.0f, 18.0f);
        g.setColour (f.plate);
        g.fillRoundedRectangle (plate, 6.0f);
        g.setColour (f.ink.withAlpha (0.25f));
        g.drawRoundedRectangle (plate.reduced (3.0f), 4.0f, 1.0f);
        g.setColour (f.ink);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (ampsim::modelNames()[m].toUpperCase(), plate.withTrimmedLeft (18).removeFromTop (44.0f), juce::Justification::centredLeft);
        // power lamp
        const bool on = ampOn.getToggleState();
        auto lamp = juce::Rectangle<float> (plate.getRight() - 30.0f, plate.getY() + 16.0f, 12.0f, 12.0f);
        if (on) { g.setColour (juce::Colour (0xffff3b30).withAlpha (0.35f)); g.fillEllipse (lamp.expanded (5.0f)); }
        g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff4a1612));
        g.fillEllipse (lamp);
        // grille cloth under the head (the cabinet)
        auto cb = cabArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cb.translated (0, 4), 12.0f);
        g.setColour (f.tolex);
        g.fillRoundedRectangle (cb, 12.0f);
        auto grille = cb.reduced (16.0f).withTrimmedRight (cb.getWidth() * 0.46f);
        g.setColour (juce::Colour (0xff3a3833));
        g.fillRoundedRectangle (grille, 6.0f);
        g.setColour (juce::Colour (0xff4a4740));
        for (float y = grille.getY() + 4.0f; y < grille.getBottom(); y += 5.0f) g.drawHorizontalLine ((int) y, grille.getX() + 4.0f, grille.getRight() - 4.0f);
        // the mic: where it points (centre .. edge) and how far
        const float pos = proc.apvts.getRawParameterValue ("cabPos")->load() / 10.0f, dist = proc.apvts.getRawParameterValue ("cabDist")->load() / 10.0f;
        const auto cone = grille.withSizeKeepingCentre (grille.getHeight() * 0.78f, grille.getHeight() * 0.78f);
        g.setColour (juce::Colour (0xff26241f));
        g.fillEllipse (cone);
        g.setColour (juce::Colour (0xff5a564c));
        g.fillEllipse (cone.withSizeKeepingCentre (cone.getWidth() * 0.22f, cone.getHeight() * 0.22f));
        const bool ir = (int) proc.apvts.getRawParameterValue ("cabType")->load() == ampsim::numCabs - 1;
        if (! ir)
        {
            const float mx = cone.getCentreX() + pos * cone.getWidth() * 0.45f, mr = 7.0f + 6.0f * (1.0f - dist);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (mx - mr, cone.getCentreY() - mr + 3.0f, mr * 2.0f, mr * 2.0f);
            g.setColour (juce::Colour (0xffd8d8d8));
            g.fillEllipse (mx - mr, cone.getCentreY() - mr, mr * 2.0f, mr * 2.0f);
            g.setColour (juce::Colour (0xff6a6a6a));
            g.drawEllipse (mx - mr, cone.getCentreY() - mr, mr * 2.0f, mr * 2.0f, 1.5f);
        }
        g.setColour (text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("CABINET & MIC", cabCtl.toFloat().removeFromTop (24.0f), juce::Justification::centredLeft);

        // output
        auto o = outArea.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (o, 10.0f);
        g.setColour (text);
        g.drawText ("OUTPUT", o.reduced (14.0f, 8.0f).removeFromTop (22.0f), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (jp (u8"リバーブは標準 OFF（自作のリバーブを後ろに）"), o.reduced (14.0f, 8.0f).removeFromBottom (18.0f), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        head = r.removeFromTop (250);
        r.removeFromTop (14);
        outArea = r.removeFromRight (300);
        r.removeFromRight (14);
        cabArea = r;

        auto plate = head.reduced (16, 18);
        auto top = plate.removeFromTop (44).withTrimmedRight (50);
        ampOn.setBounds (top.removeFromRight (90).reduced (0, 8));
        top.removeFromRight (10);
        modelBox.setBounds (top.removeFromRight (230).reduced (0, 8));
        ampKnobs.setBounds (plate.reduced (16, 12));

        cabCtl = cabArea.reduced (16).withTrimmedLeft ((int) (cabArea.getWidth() * 0.54f));
        auto c = cabCtl;
        c.removeFromTop (26);
        auto row = c.removeFromTop (28);
        cabOn.setBounds (row.removeFromRight (70));
        row.removeFromRight (6);
        cabBox.setBounds (row);
        c.removeFromTop (6);
        micBox.setBounds (c.removeFromTop (28));
        c.removeFromTop (6);
        auto irRow = c.removeFromBottom (26);
        irButton.setBounds (irRow.removeFromLeft (110));
        irRow.removeFromLeft (8);
        irLabel.setBounds (irRow);
        c.removeFromBottom (6);
        micKnobs.setBounds (c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 130)));

        auto ok = outArea.reduced (14, 34);
        outKnobs.setBounds (ok.withSizeKeepingCentre (ok.getWidth(), juce::jmin (ok.getHeight(), 120)));
    }

private:
    void refreshIR()
    {
        using namespace AmpUI;
        const auto name = proc.getCabIRName();
        irLabel.setText (irError.isNotEmpty() ? irError : (name.isNotEmpty() ? name : jp (u8"IR なし（「Loaded IR」で使います）")), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const int m = (int) proc.apvts.getRawParameterValue ("ampModel")->load();
        const auto key = juce::String (m) + "/" + juce::String (proc.apvts.getRawParameterValue ("cabPos")->load()) + "/"
                       + juce::String (proc.apvts.getRawParameterValue ("cabDist")->load()) + "/" + juce::String (proc.apvts.getRawParameterValue ("cabType")->load())
                       + (ampOn.getToggleState() ? "1" : "0");
        if (key != shown)
        {
            shown = key;
            const auto f = AmpUI::face (m);
            ampKnobs.setColours (f.ink, f.knob);
            repaint();
        }
    }

    AIOGuitarProcessor& proc;
    AmpKnobRow ampKnobs, micKnobs, outKnobs;
    juce::ComboBox modelBox, cabBox, micBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modelAtt, cabAtt, micAtt;
    juce::TextButton ampOn { "AMP ON" }, cabOn { "CAB ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ampOnAtt, cabOnAtt;
    juce::TextButton irButton { "Load IR..." };
    juce::Label irLabel;
    juce::String irError, shown;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> head, cabArea, cabCtl, outArea;
};
