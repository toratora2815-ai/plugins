#pragma once
#include "Actions.h"

// 割り当てと設定。テンキーが押されたら何をするかもここで決める（すべてメインスレッド）
namespace numdeck
{
    struct Slot
    {
        juce::String actionId;      // 空 = なし（普通の数字キーのまま）、"custom" = 自分で覚えさせたキー
        int keyCode = -1, mods = 0; // custom のとき
        juce::String label;         // custom のとき

        juce::String toString() const
        {
            if (actionId != "custom") return actionId;
            return "custom|" + juce::String (keyCode) + "|" + juce::String (mods) + "|" + label;
        }

        static Slot fromString (const juce::String& s)
        {
            Slot slot;
            if (! s.startsWith ("custom|")) { slot.actionId = findAction (s) != nullptr ? s : juce::String(); return slot; }
            auto parts = juce::StringArray::fromTokens (s, "|", "");
            if (parts.size() < 3) return slot;
            slot.actionId = "custom";
            slot.keyCode = parts[1].getIntValue();
            slot.mods = parts[2].getIntValue();
            parts.removeRange (0, 3);
            slot.label = parts.joinIntoString ("|");
            return slot;
        }
    };

    // 画面と送信に使う、割り当ての中身
    struct Resolved
    {
        bool valid = false;
        juce::String name, glyph;
        Cat cat = Cat::Custom;
        Icon icon = Icon::None;
        int keyCode = -1, mods = 0;
        bool repeat = false;
    };

    class Deck
    {
    public:
        static constexpr int kPages = 3;
        enum ToggleKey { ToggleClear = 0, ToggleEquals, ToggleNone };

        bool enabled = true;
        bool logicOnly = true;
        bool alwaysOnTop = false;
        int page = 0;
        int toggleKey = ToggleClear;
        std::array<std::array<Slot, 9>, kPages> slots;

        std::function<void()> onStateChanged;   // ON/OFF やページが変わった
        std::function<void (int index)> onFired;  // 0..8 のタイルが動いた
        std::function<void()> onIgnored;          // Logic が前面じゃないので送らなかった

        Deck()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Num Deck";
            o.folderName = "Num Deck";
            o.filenameSuffix = "settings";
            o.osxLibrarySubFolder = "Application Support";
            props = std::make_unique<juce::PropertiesFile> (o);
            resetAll();
            load();
        }

        static juce::String pageName (int p)
        {
            static const char* names[kPages] = { "再生・録音", "編集", "ウィンドウ" };
            return jp (names[juce::jlimit (0, kPages - 1, p)]);
        }

        Resolved resolve (int p, int index) const
        {
            Resolved r;
            const auto& s = slots[(size_t) p][(size_t) index];
            if (s.actionId == "custom" && s.keyCode >= 0)
            {
                r.valid = true;
                r.cat = Cat::Custom;
                r.keyCode = s.keyCode; r.mods = s.mods;
                r.name = s.label.isNotEmpty() ? s.label : shortcutText (s.keyCode, s.mods);
                r.glyph = shortcutText (s.keyCode, s.mods);
                r.repeat = true;
            }
            else if (auto* a = findAction (s.actionId))
            {
                r.valid = true;
                r.name = jp (a->name); r.glyph = a->glyph;
                r.cat = a->cat; r.icon = a->icon;
                r.keyCode = a->keyCode; r.mods = a->mods; r.repeat = a->repeat;
            }
            return r;
        }

        // テンキー（修飾キーなし）が押された。true = そのキーを他のアプリに渡さない
        bool handlePadKey (int pad, bool down, bool repeat)
        {
            const bool isToggle = (toggleKey == ToggleClear && pad == PadClear)
                               || (toggleKey == ToggleEquals && pad == PadEquals);
            if (isToggle)
            {
                if (down && ! repeat) { enabled = ! enabled; changed(); }
                return true;
            }
            if (! enabled) return false;

            const bool isPageKey = pad == PadPlus || pad == PadMinus;
            const bool isSlot = pad >= Pad1 && pad <= Pad9;
            if (! isPageKey && ! isSlot) return false;

            if (logicOnly && ! KeyTap::isLogicFrontmost())
            {
                if (down && ! repeat && onIgnored) onIgnored();
                return false;
            }

            if (isPageKey)
            {
                if (down && ! repeat) { page = (page + (pad == PadPlus ? 1 : kPages - 1)) % kPages; changed(); }
                return true;
            }

            const auto r = resolve (page, pad - Pad1);
            if (! r.valid) return false;  // 「なし」のキーは普通の数字キーとして通す
            if (down && (! repeat || r.repeat))
            {
                KeyTap::sendKey (r.keyCode, r.mods);
                if (onFired) onFired (pad - Pad1);
            }
            return true;
        }

        void fire (int index)
        {
            const auto r = resolve (page, index);
            if (r.valid) KeyTap::sendKey (r.keyCode, r.mods);
        }

        void setSlot (int p, int index, const Slot& s) { slots[(size_t) p][(size_t) index] = s; save(); }

        void resetPage (int p)
        {
            // テンキーの並び（7 8 9 / 4 5 6 / 1 2 3）で、キー 1〜9 の順に書く
            static const char* defaults[kPages][9] = {
                { "start",  "record",   "capture",  "rewind",   "play",      "forward", "undo",   "cycle",  "metronome" },
                { "mute",   "solo",     "automation", "join",   "split",     "zoom",    "redo",   "repeat", "delete" },
                { "newAudio", "newInst", "bounce",  "pianoRoll", "save",     "loops",   "mixer",  "editor", "library" },
            };
            for (int i = 0; i < 9; ++i)
                slots[(size_t) p][(size_t) i] = Slot::fromString (defaults[p][i]);
        }

        void resetAll() { for (int p = 0; p < kPages; ++p) resetPage (p); }

        void save()
        {
            props->setValue ("enabled", enabled);
            props->setValue ("logicOnly", logicOnly);
            props->setValue ("alwaysOnTop", alwaysOnTop);
            props->setValue ("page", page);
            props->setValue ("toggleKey", toggleKey);
            for (int p = 0; p < kPages; ++p)
                for (int i = 0; i < 9; ++i)
                    props->setValue ("slot" + juce::String (p) + "_" + juce::String (i + 1), slots[(size_t) p][(size_t) i].toString());
            props->saveIfNeeded();
        }

    private:
        std::unique_ptr<juce::PropertiesFile> props;

        void changed() { save(); if (onStateChanged) onStateChanged(); }

        void load()
        {
            enabled     = props->getBoolValue ("enabled", true);
            logicOnly   = props->getBoolValue ("logicOnly", true);
            alwaysOnTop = props->getBoolValue ("alwaysOnTop", false);
            page        = juce::jlimit (0, kPages - 1, props->getIntValue ("page", 0));
            toggleKey   = juce::jlimit (0, (int) ToggleNone, props->getIntValue ("toggleKey", ToggleClear));
            for (int p = 0; p < kPages; ++p)
                for (int i = 0; i < 9; ++i)
                {
                    const auto key = "slot" + juce::String (p) + "_" + juce::String (i + 1);
                    if (props->containsKey (key))
                        slots[(size_t) p][(size_t) i] = Slot::fromString (props->getValue (key));
                }
        }
    };
}
