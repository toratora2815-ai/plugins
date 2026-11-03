#pragma once
#include "Actions.h"

// 割り当てと設定。テンキーが押されたら何をするかもここで決める（すべてメインスレッド）
namespace numdeck
{
    struct Slot
    {
        // 空 = なし（普通の数字キーのまま）、一覧の id、"custom" = 覚えさせたキー、"app" = アプリを開く、"url" = ページを開く
        juce::String actionId;
        int keyCode = -1, mods = 0; // custom のとき
        juce::String label;         // custom / app / url のときのタイルの名前
        juce::String path;          // app のとき
        juce::String url;           // url のとき

        bool isUser() const { return actionId == "custom" || actionId == "app" || actionId == "url"; }

        juce::String toString() const
        {
            if (! isUser()) return actionId;
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", actionId);
            if (actionId == "custom") { o->setProperty ("keyCode", keyCode); o->setProperty ("mods", mods); }
            if (actionId == "app")    o->setProperty ("path", path);
            if (actionId == "url")    o->setProperty ("url", url);
            o->setProperty ("label", label);
            return juce::JSON::toString (juce::var (o), true);
        }

        static Slot fromString (const juce::String& s)
        {
            Slot slot;
            if (s.startsWith ("{"))
            {
                const auto v = juce::JSON::parse (s);
                slot.actionId = v.getProperty ("type", "").toString();
                slot.keyCode  = (int) v.getProperty ("keyCode", -1);
                slot.mods     = (int) v.getProperty ("mods", 0);
                slot.label    = v.getProperty ("label", "").toString();
                slot.path     = v.getProperty ("path", "").toString();
                slot.url      = v.getProperty ("url", "").toString();
                if (! slot.isUser()) slot = {};
            }
            else if (s.startsWith ("custom|")) // 1.0.0 の形式
            {
                auto parts = juce::StringArray::fromTokens (s, "|", "");
                if (parts.size() < 3) return slot;
                slot.actionId = "custom";
                slot.keyCode = parts[1].getIntValue();
                slot.mods = parts[2].getIntValue();
                parts.removeRange (0, 3);
                slot.label = parts.joinIntoString ("|");
            }
            else if (findAction (s) != nullptr)
                slot.actionId = s;
            return slot;
        }

        static Slot app (const juce::String& appPath, const juce::String& name = {})
        {
            Slot s; s.actionId = "app"; s.path = appPath; s.label = name; return s;
        }
    };

    // 画面と実行に使う、割り当ての中身
    struct Resolved
    {
        enum Kind { Key, App, Url };
        bool valid = false;
        Kind kind = Key;
        juce::String name, glyph, path, url;
        Cat cat = Cat::Custom;
        Icon icon = Icon::None;
        int keyCode = -1, mods = 0;
        bool repeat = false;
    };

    class Deck
    {
    public:
        static constexpr int kPages = 4;
        static constexpr int kModes = 2;
        enum Mode { ModeLogic = 0, ModeWork };
        enum ToggleKey { ToggleClear = 0, ToggleEquals, ToggleNone };

        bool enabled = true;
        bool logicOnly = true;     // Logic Pro モードだけに効く
        bool alwaysOnTop = false;
        int mode = ModeLogic;      // 画面のクリックでだけ切り替える（テンキーでは変わらない）
        int toggleKey = ToggleClear;
        juce::String windowState;  // ウィンドウの位置と大きさ

        std::function<void()> onStateChanged;     // ON/OFF やページが変わった
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
            for (int m = 0; m < kModes; ++m)
                for (int p = 0; p < kPages; ++p)
                {
                    resetPage (m, p);
                    names[(size_t) m][(size_t) p] = defaultPageName (m, p);
                }
            load();
        }

        static juce::String modeName (int m) { return m == ModeLogic ? "Logic Pro" : jp ("業務"); }

        static juce::String defaultPageName (int m, int p)
        {
            static const char* n[kModes][kPages] = { { "再生・録音", "編集", "ウィンドウ", "自由" },
                                                     { "編集", "ブラウザ", "アプリ", "リンク" } };
            return jp (n[juce::jlimit (0, kModes - 1, m)][juce::jlimit (0, kPages - 1, p)]);
        }

        int page() const { return pages[(size_t) mode]; }
        void setPage (int p) { pages[(size_t) mode] = (p % kPages + kPages) % kPages; save(); }
        void setMode (int m) { mode = juce::jlimit (0, kModes - 1, m); save(); }

        juce::String pageName (int p) const { return names[(size_t) mode][(size_t) p]; }
        void setPageName (int p, const juce::String& n)
        {
            names[(size_t) mode][(size_t) p] = n.trim().isNotEmpty() ? n.trim() : defaultPageName (mode, p);
            save();
        }

        const Slot& slot (int p, int index) const { return slots[(size_t) mode][(size_t) p][(size_t) index]; }
        void setSlot (int p, int index, const Slot& s) { slots[(size_t) mode][(size_t) p][(size_t) index] = s; save(); }

        Resolved resolve (int p, int index) const
        {
            Resolved r;
            const auto& s = slot (p, index);
            if (s.actionId == "custom" && s.keyCode >= 0)
            {
                r.valid = true;
                r.cat = Cat::Custom;
                r.keyCode = s.keyCode; r.mods = s.mods;
                r.name = s.label.isNotEmpty() ? s.label : shortcutText (s.keyCode, s.mods);
                r.glyph = shortcutText (s.keyCode, s.mods);
                r.repeat = true;
            }
            else if (s.actionId == "app" && s.path.isNotEmpty())
            {
                r.valid = true; r.kind = Resolved::App; r.cat = Cat::App; r.path = s.path;
                r.name = s.label.isNotEmpty() ? s.label
                                              : juce::String::fromUTF8 (KeyTap::appDisplayName (s.path.toStdString()).c_str());
                r.glyph = r.name.substring (0, 4).toUpperCase();
            }
            else if (s.actionId == "url" && s.url.isNotEmpty())
            {
                r.valid = true; r.kind = Resolved::Url; r.cat = Cat::Link; r.url = s.url;
                r.name = s.label.isNotEmpty() ? s.label : juce::URL (s.url).getDomain();
                r.glyph = r.name.substring (0, 4).toUpperCase();
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

        static void run (const Resolved& r)
        {
            switch (r.kind)
            {
                case Resolved::Key: KeyTap::sendKey (r.keyCode, r.mods); break;
                case Resolved::App: KeyTap::openApp (r.path.toStdString()); break;
                case Resolved::Url: KeyTap::openURL (r.url.toStdString()); break;
            }
        }

        bool logicOnlyActive() const { return mode == ModeLogic && logicOnly; }

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

            if (logicOnlyActive() && ! KeyTap::isLogicFrontmost())
            {
                if (down && ! repeat && onIgnored) onIgnored();
                return false;
            }

            if (isPageKey)
            {
                if (down && ! repeat) { setPage (page() + (pad == PadPlus ? 1 : -1)); changed(); }
                return true;
            }

            const auto r = resolve (page(), pad - Pad1);
            if (! r.valid) return false;  // 「なし」のキーは普通の数字キーとして通す
            if (down && (! repeat || r.repeat))
            {
                run (r);
                if (onFired) onFired (pad - Pad1);
            }
            return true;
        }

        void resetPage (int m, int p)
        {
            auto& page = slots[(size_t) m][(size_t) p];
            for (auto& s : page) s = {};

            // テンキーの並び（7 8 9 / 4 5 6 / 1 2 3）で、キー 1〜9 の順に書く
            static const char* logic[kPages][9] = {
                { "start",  "record",   "capture",  "rewind",   "play",      "forward", "undo",   "cycle",  "metronome" },
                { "mute",   "solo",     "automation", "join",   "split",     "zoom",    "redo",   "repeat", "delete" },
                { "newAudio", "newInst", "bounce",  "pianoRoll", "save",     "loops",   "mixer",  "editor", "library" },
                { "", "", "", "", "", "", "", "", "" },
            };
            static const char* work[kPages][9] = {
                { "find",   "save",     "screenshot", "cut",    "copy",      "paste",   "undo",   "redo",   "selectAll" },
                { "back",   "address",  "fwd",      "prevTab",  "reload",    "nextTab", "newTab", "closeTab", "reopenTab" },
                { "", "", "", "", "", "", "", "", "" },
                { "", "", "", "", "", "", "", "", "" },
            };
            for (int i = 0; i < 9; ++i)
                page[(size_t) i] = Slot::fromString (m == ModeLogic ? logic[p][i] : work[p][i]);

            if (m == ModeWork && p == 2)
            {
                // アプリのページ: 入っているアプリだけ並べる
                static const char* apps[] = { "/Applications/Slack.app", "/Applications/Google Chrome.app",
                                              "/System/Library/CoreServices/Finder.app", "/Applications/Microsoft Excel.app",
                                              "/Applications/Microsoft Word.app", "/Applications/zoom.us.app",
                                              "/System/Applications/Notes.app", "/System/Applications/Calendar.app",
                                              "/System/Applications/Mail.app" };
                static const int keys[] = { 7, 8, 9, 4, 5, 6, 1, 2, 3 };
                int k = 0;
                for (auto* a : apps)
                    if (juce::File (a).exists() && k < 9)
                        page[(size_t) keys[k++] - 1] = Slot::app (a);
            }
        }

        void resetCurrentPage() { resetPage (mode, page()); save(); }
        void resetCurrentMode() { for (int p = 0; p < kPages; ++p) resetPage (mode, p); save(); }

        void save()
        {
            props->setValue ("enabled", enabled);
            props->setValue ("logicOnly", logicOnly);
            props->setValue ("alwaysOnTop", alwaysOnTop);
            props->setValue ("mode", mode);
            props->setValue ("toggleKey", toggleKey);
            props->setValue ("window", windowState);
            for (int m = 0; m < kModes; ++m)
            {
                props->setValue (prefix (m) + "page", pages[(size_t) m]);
                for (int p = 0; p < kPages; ++p)
                {
                    props->setValue (prefix (m) + "pageName" + juce::String (p), names[(size_t) m][(size_t) p]);
                    for (int i = 0; i < 9; ++i)
                        props->setValue (slotKey (m, p, i), slots[(size_t) m][(size_t) p][(size_t) i].toString());
                }
            }
            props->saveIfNeeded();
        }

    private:
        std::unique_ptr<juce::PropertiesFile> props;
        std::array<std::array<std::array<Slot, 9>, kPages>, kModes> slots;
        std::array<std::array<juce::String, kPages>, kModes> names;
        std::array<int, kModes> pages {};

        // Logic Pro モードは 1.0.0 と同じ名前で保存する
        static juce::String prefix (int m) { return m == ModeLogic ? "" : "work_"; }
        static juce::String slotKey (int m, int p, int i) { return prefix (m) + "slot" + juce::String (p) + "_" + juce::String (i + 1); }

        void changed() { save(); if (onStateChanged) onStateChanged(); }

        void load()
        {
            enabled     = props->getBoolValue ("enabled", true);
            logicOnly   = props->getBoolValue ("logicOnly", true);
            alwaysOnTop = props->getBoolValue ("alwaysOnTop", false);
            mode        = juce::jlimit (0, kModes - 1, props->getIntValue ("mode", ModeLogic));
            toggleKey   = juce::jlimit (0, (int) ToggleNone, props->getIntValue ("toggleKey", ToggleClear));
            windowState = props->getValue ("window");
            for (int m = 0; m < kModes; ++m)
            {
                pages[(size_t) m] = juce::jlimit (0, kPages - 1, props->getIntValue (prefix (m) + "page", 0));
                for (int p = 0; p < kPages; ++p)
                {
                    const auto nk = prefix (m) + "pageName" + juce::String (p);
                    if (props->containsKey (nk)) names[(size_t) m][(size_t) p] = props->getValue (nk);
                    for (int i = 0; i < 9; ++i)
                        if (props->containsKey (slotKey (m, p, i)))
                            slots[(size_t) m][(size_t) p][(size_t) i] = Slot::fromString (props->getValue (slotKey (m, p, i)));
                }
            }
        }
    };
}
