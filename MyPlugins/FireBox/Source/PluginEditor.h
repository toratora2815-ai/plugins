#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace fbui
{
    const juce::Colour bg (0xff1b1714), panel (0xff2a221d), ember (0xffe8742a), ash (0xff6b5d52), text (0xfff3e7d9), dim (0xff9c8b7c);

    struct Look : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
            const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
            const bool on = s.isEnabled();
            juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
            g.setColour (ash.withAlpha (0.6f)); g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float ang = a0 + pos * (a1 - a0);
            juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
            g.setColour (on ? ember : ash); g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (panel.brighter (0.15f)); g.fillEllipse (cx - rad * 0.68f, cy - rad * 0.68f, rad * 1.36f, rad * 1.36f);
            juce::Path p; p.addRoundedRectangle (-1.5f, -rad * 0.66f, 3.0f, rad * 0.36f, 1.5f);
            g.setColour (on ? text : dim); g.fillPath (p, juce::AffineTransform::rotation (ang).translated (cx, cy));
        }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (1.0f);
            const bool on = b.getToggleState();
            g.setColour (on ? ember.withAlpha (0.9f) : panel.brighter (over ? 0.25f : 0.12f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? ember.brighter (0.3f) : ash); g.drawRoundedRectangle (r, 4.0f, 1.0f);
            juce::ignoreUnused (down);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour (b.getToggleState() ? bg : text);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
        }
    };
}

class FireBoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FireBoxEditor (FireBoxProcessor&);
    ~FireBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider; juce::Label label; juce::TextButton onBtn;
        std::unique_ptr<SA> att; std::unique_ptr<BA> onAtt; juce::String onId;
    };
    void setupKnob (Knob&, const juce::String& id, const juce::String& name, const juce::String& onId, const juce::String& tip);
    void setupSwitch (juce::TextButton&, std::unique_ptr<BA>&, const juce::String& id, const juce::String& text, const juce::String& tip);
    void timerCallback() override;

    FireBoxProcessor& processor;
    fbui::Look look;

    Knob kIn, kBass, kThick, kBody, kComp, kSc, kCrunch, kMix, kLoud, kOut;
    juce::TextButton ottBtn, unityBtn, liveBtn, osBtn, bypassBtn;
    std::unique_ptr<BA> ottAtt, unityAtt, liveAtt, osAtt, bypassAtt;
    float inL = -100, outL = -100, inP = 0, outP = 0;

    // 画面のいちばん下: Plugin Link と プリセット・取り消し
    plink::LinkBar linkBar { processor.apvts, fbui::ember, fbui::panel, fbui::text };
    myp::PresetBar presetBar { processor.myPresets, fbui::ember, fbui::panel, fbui::text };
    juce::TextButton matchBtn;
    std::unique_ptr<BA> matchAtt;
    static constexpr int footerH = 72;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FireBoxEditor)
};
