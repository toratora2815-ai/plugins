#pragma once
// Plugin Link の JUCE 側の部品（パラメーター・定期処理・画面の帯）
#include <JuceHeader.h>
#include "PluginLink.h"

namespace plink
{
    inline const char* groupParamId = "linkGroup";

    // 「LINK」パラメーター（OFF / 1 / 2 / 3 / 4）。同じ番号どうしがつながる
    inline std::unique_ptr<juce::RangedAudioParameter> makeGroupParameter (int version)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { groupParamId, version }, "Plugin Link",
                                                             juce::StringArray { "OFF", "1", "2", "3", "4" }, 0);
    }

    // プロセッサーが 1 個持つ。席の維持と、LINK の番号・トラック名を掲示板へ（メッセージスレッド）
    class Link : private juce::Timer
    {
    public:
        Link (Kind k, juce::AudioProcessorValueTreeState& s) : endpoint (k), state (s)
        {
            groupParam = state.getRawParameterValue (groupParamId);
            timerCallback();
            startTimerHz (4);
        }
        ~Link() override { stopTimer(); }

        int group() const { return endpoint.getGroup(); }
        bool on() const { return endpoint.getGroup() > 0 && endpoint.available(); }
        Endpoint& ep() { return endpoint; }
        const Endpoint& ep() const { return endpoint; }

        void setTrackName (const juce::String& n) { endpoint.setName (n.toStdString()); }

        // 画面用：相手の名前（トラック名があれば「種類（トラック）」）
        static juce::String describe (const Peer& p)
        {
            juce::String s (kindName (p.kind));
            if (p.name[0] != 0) s << " (" << juce::String::fromUTF8 (p.name) << ")";
            return s;
        }

    private:
        void timerCallback() override
        {
            endpoint.setGroup (groupParam != nullptr ? juce::roundToInt (groupParam->load()) : 0);
            endpoint.maintain();
        }

        Endpoint endpoint;
        juce::AudioProcessorValueTreeState& state;
        std::atomic<float>* groupParam = nullptr;
    };

    // 画面のいちばん下に置く帯：[LINK ▾]  いまの様子の文  （右に追加のつまみ）
    class LinkBar : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int height = 30;

        LinkBar (juce::AudioProcessorValueTreeState& s, juce::Colour accentColour, juce::Colour panelColour,
                 juce::Colour textColour)
            : accent (accentColour), panel (panelColour), text (textColour)
        {
            box.addItem ("LINK OFF", 1);
            for (int i = 1; i <= numGroups; ++i) box.addItem ("LINK " + juce::String (i), i + 1);
            box.setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.15f));
            box.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.5f));
            box.setColour (juce::ComboBox::textColourId, text);
            box.setColour (juce::ComboBox::arrowColourId, accent);
            box.setTooltip ("Plugin Link: plugins with the same number talk to each other");
            addAndMakeVisible (box);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, groupParamId, box);
            startTimerHz (4);
        }

        // いまの様子を返す関数（4 回/秒 呼ぶ）。lit = 相手とつながって効いているか
        std::function<juce::String (bool& lit)> status;
        juce::Component* extra = nullptr;   // 右端に置く部品（なくてよい）
        int extraWidth = 0;
        juce::String extraLabel;            // 右端の部品の名前（左に小さく出す）

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (panel);
            g.fillRoundedRectangle (r, 5.0f);
            auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) box.getRight() + 14.0f, r.getCentreY() });
            g.setColour (lit ? accent : text.withAlpha (0.25f));
            g.fillEllipse (dot);
            g.setColour (text.withAlpha (lit ? 0.95f : 0.6f));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (line, textArea, juce::Justification::centredLeft, true);
            if (extraLabel.isNotEmpty())
            {
                g.setColour (text.withAlpha (0.7f));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (extraLabel, labelArea, juce::Justification::centredRight);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            box.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (26);
            if (extra != nullptr)
            {
                extra->setBounds (r.removeFromRight (extraWidth));
                labelArea = extraLabel.isNotEmpty() ? r.removeFromRight (90) : juce::Rectangle<int>();
                r.removeFromRight (8);
            }
            textArea = r;
        }

    private:
        void timerCallback() override
        {
            bool l = false;
            auto s = status ? status (l) : juce::String();
            if (s != line || l != lit) { line = s; lit = l; repaint(); }
        }

        juce::Colour accent, panel, text;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        juce::String line;
        bool lit = false;
        juce::Rectangle<int> textArea, labelArea;
    };

    inline juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (f)) + " Hz";
    }
}
