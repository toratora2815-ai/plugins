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
    // 相手選び（FROM）は apvts.state の "linkFrom" に「種類|トラック名」で保存する（例 "1|Mix Bus"）。
    // トラック名がない相手は「種類|#席の番号」（そのときだけ有効）
    class Link : private juce::Timer
    {
    public:
        static constexpr const char* fromProperty = "linkFrom";

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

        static juce::String keyFor (const Peer& p)
        {
            return juce::String ((int) p.kind) + "|"
                 + (p.name[0] != 0 ? juce::String::fromUTF8 (p.name) : "#" + juce::String (p.slotIndex));
        }

        // 選んである相手（空 = おまかせ）
        juce::String chosenKey() const { return state.state.getProperty (fromProperty, "").toString(); }
        void choose (const juce::String& key)
        {
            state.state.setProperty (fromProperty, key, nullptr);
            timerCallback();
        }
        // 選んである相手がいま見つかっているか（選んでいないときは true）
        bool chosenPresent() const { return chosenKey().isEmpty() || resolved; }

    private:
        void timerCallback() override
        {
            endpoint.setGroup (groupParam != nullptr ? juce::roundToInt (groupParam->load()) : 0);
            endpoint.maintain();

            // 選んである相手を席の持ち主の番号に直して、音の処理側（find）へ伝える
            const auto key = chosenKey();
            const int kind = key.upToFirstOccurrenceOf ("|", false, false).getIntValue();
            for (int k = 0; k < (int) numKinds; ++k) if (k != kind) endpoint.setPreferred ((Kind) k, 0);
            resolved = false;
            if (key.isEmpty() || kind <= 0 || kind >= (int) numKinds) return;
            const Kind kk = (Kind) kind;
            Peer peers[numSlots];
            const int n = endpoint.list (&kk, 1, peers, numSlots);
            uint64_t owner = 0;
            for (int i = 0; i < n && owner == 0; ++i)
                if (keyFor (peers[i]) == key) owner = peers[i].owner;
            endpoint.setPreferred (kk, owner);
            resolved = owner != 0;
        }

        Endpoint endpoint;
        juce::AudioProcessorValueTreeState& state;
        std::atomic<float>* groupParam = nullptr;
        bool resolved = false;
    };

    // 画面のいちばん下に置く帯：[LINK ▾] [FROM ▾]  いまの様子の文  （右に追加のつまみ）
    // FROM は setSources() で受け取る相手の種類を渡したときだけ出る
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

            from.setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.15f));
            from.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.35f));
            from.setColour (juce::ComboBox::textColourId, text);
            from.setColour (juce::ComboBox::arrowColourId, accent);
            from.setTooltip ("Which plugin to listen to (AUTO = the one that sent most recently)");
            from.onChange = [this] { fromChanged(); };
            addChildComponent (from);
            startTimerHz (4);
        }

        // 相手選び（FROM）を出す。kinds = 受け取る相手の種類
        void setSources (Link& l, std::vector<Kind> kinds)
        {
            link = &l;
            sourceKinds = std::move (kinds);
            from.setVisible (! sourceKinds.empty());
            refreshFrom();
            resized();
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
            auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ dotX, r.getCentreY() });
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
            if (from.isVisible()) { r.removeFromLeft (6); from.setBounds (r.removeFromLeft (190)); }
            dotX = (float) r.getX() + 14.0f;
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
            if (from.isVisible() && ! from.isPopupActive()) refreshFrom();
        }

        // FROM の中身：AUTO、いま同じ LINK にいる相手、（選んであるのにいない相手）
        void refreshFrom()
        {
            if (link == nullptr) return;
            Peer peers[numSlots];
            const int n = link->ep().list (sourceKinds.data(), (int) sourceKinds.size(), peers, numSlots);
            juce::StringArray keys, labels;
            for (int i = 0; i < n; ++i)
            {
                keys.add (Link::keyFor (peers[i]));
                labels.add (peers[i].name[0] != 0 ? Link::describe (peers[i])
                                                  : juce::String (kindName (peers[i].kind)) + " #" + juce::String (peers[i].slotIndex + 1));
            }
            const auto chosen = link->chosenKey();
            if (chosen.isNotEmpty() && ! keys.contains (chosen))
            {
                keys.add (chosen);
                labels.add (chosen.fromFirstOccurrenceOf ("|", false, false) + "  (not here)");
            }
            if (keys == fromKeys && labels == fromLabels && chosen == fromChosen) return;
            fromKeys = keys; fromLabels = labels; fromChosen = chosen;
            from.clear (juce::dontSendNotification);
            from.addItem ("FROM: AUTO", 1);
            for (int i = 0; i < keys.size(); ++i) from.addItem ("FROM: " + labels[i], i + 2);
            const int idx = chosen.isEmpty() ? 0 : keys.indexOf (chosen) + 1;
            from.setSelectedItemIndex (idx, juce::dontSendNotification);
        }

        void fromChanged()
        {
            if (link == nullptr) return;
            const int id = from.getSelectedId();
            link->choose (id <= 1 ? juce::String() : fromKeys[id - 2]);
            fromChosen = link->chosenKey();
        }

        juce::Colour accent, panel, text;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        juce::String line;
        bool lit = false;
        juce::Rectangle<int> textArea, labelArea;
        float dotX = 140.0f;

        juce::ComboBox from;
        Link* link = nullptr;
        std::vector<Kind> sourceKinds;
        juce::StringArray fromKeys, fromLabels;
        juce::String fromChosen = "?";
    };

    inline juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (f)) + " Hz";
    }
}
