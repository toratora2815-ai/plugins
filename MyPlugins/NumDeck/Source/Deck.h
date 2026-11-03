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
        enum Kind { Key, App, Url, Remote };
        bool valid = false;
        Kind kind = Key;
        juce::String name, glyph, path, url, remote;
        Cat cat = Cat::Custom;
        Icon icon = Icon::None;
        int keyCode = -1, mods = 0;
        bool repeat = false;
        bool toLogic = false;   // Logic Pro モードのキー: 前面のアプリに関係なく Logic に送る
    };

    class Deck
    {
    public:
        static constexpr int kPages = 4;
        static constexpr int kSlots = 11;  // キー 1〜9、キー 0（添字 9）、キー .（添字 10）
        static constexpr int kModes = 2;
        enum Mode { ModeLogic = 0, ModeWork };
        enum ToggleKey { ToggleClear = 0, ToggleEquals, ToggleNone };

        bool enabled = true;
        bool logicOnly = true;     // Logic Pro モードだけに効く
        bool alwaysOnTop = false;
        int mode = ModeLogic;      // 画面のクリックでだけ切り替える（テンキーでは変わらない）
        int toggleKey = ToggleClear;
        int modeKey = 115;  // モード（Logic Pro / 業務）を切り替えるキー。最初は Home。-1 = なし
        juce::String windowState;  // ウィンドウの位置と大きさ
        juce::String chromeProfile, chromeProfileName;  // リンクを開く Chrome のアカウント（空 = いつもの）

        std::function<void()> onStateChanged;     // ON/OFF やページが変わった
        std::function<void (int index)> onFired;  // 0..8 = キー 1〜9、9 = キー 0、10 = キー . のタイルが動いた
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
            static const char* n[kModes][kPages] = { { "再生・録音", "編集", "ウィンドウ", "リファレンス" },
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
                if (a->remote != nullptr) { r.kind = Resolved::Remote; r.remote = a->remote; }
            }
            r.toLogic = mode == ModeLogic && r.kind == Resolved::Key;
            return r;
        }

        void run (const Resolved& r) const
        {
            switch (r.kind)
            {
                case Resolved::Key:
                    if (r.toLogic) KeyTap::sendKeyToLogic (r.keyCode, r.mods);
                    else           KeyTap::sendKey (r.keyCode, r.mods);
                    break;
                case Resolved::App: KeyTap::openApp (r.path.toStdString()); break;
                case Resolved::Url: KeyTap::openURL (r.url.toStdString(), chromeProfile.toStdString()); break;
                case Resolved::Remote: KeyTap::sendRemote ("referencemonitor", r.remote.toStdString()); break;
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
            const bool isSlot = (pad >= Pad0 && pad <= Pad9) || pad == PadDot;
            if (! isPageKey && ! isSlot) return false;

            // Reference Monitor を前に出して聴き比べている間も、Logic と同じに扱う
            if (logicOnlyActive() && ! KeyTap::isLogicFrontmost() && ! KeyTap::isRefMonitorFrontmost())
            {
                if (down && ! repeat && onIgnored) onIgnored();
                return false;
            }

            if (isPageKey)
            {
                if (down && ! repeat) { setPage (page() + (pad == PadPlus ? 1 : -1)); changed(); }
                return true;
            }

            const int index = pad == Pad0 ? 9 : pad == PadDot ? 10 : pad - Pad1;
            const auto r = resolve (page(), index);
            if (! r.valid) return false;  // 「なし」のキーは普通の数字キーとして通す
            if (down && (! repeat || r.repeat))
            {
                run (r);
                if (onFired) onFired (index);
            }
            return true;
        }

        // テンキー以外も含む全部のキー。モード切替のキーなら切り替えて true（そのキーは他のアプリに渡さない）
        bool handleAnyKey (int keyCode, bool down, bool repeat)
        {
            if (modeKey < 0 || keyCode != modeKey) return false;
            if (down && ! repeat) { setMode (mode == ModeLogic ? ModeWork : ModeLogic); changed(); }
            return true;
        }

        void resetPage (int m, int p)
        {
            auto& page = slots[(size_t) m][(size_t) p];
            for (auto& s : page) s = {};

            // キー 1〜9、キー 0、キー . の順に書く（テンキーの並びは 7 8 9 / 4 5 6 / 1 2 3 / 0 .）
            static const char* logic[kPages][kSlots] = {
                { "freeze", "record",   "rmOut",    "rewind",   "play",      "forward", "undo",   "cycle",  "metronome", "inputMon", "lowLatency" },
                { "mute",   "solo",     "automation", "join",   "split",     "zoom",    "redo",   "repeat", "delete",    "", "" },
                { "newAudio", "newInst", "bounce",  "pianoRoll", "save",     "loops",   "mixer",  "editor", "library",   "", "" },
                { "rmPlay", "rmMono",   "rmListen", "rmCue1",   "rmCue2",    "rmCue3",  "rmAB",   "rmMix",  "rmRef",     "", "" },
            };
            static const char* work[kPages][kSlots] = {
                { "find",   "save",     "screenshot", "cut",    "copy",      "paste",   "undo",   "redo",   "selectAll", "", "" },
                { "back",   "address",  "fwd",      "prevTab",  "reload",    "nextTab", "newTab", "closeTab", "reopenTab", "", "" },
                { "", "", "", "", "", "", "", "", "", "", "" },
                { "", "", "", "", "", "", "", "", "", "", "" },
            };
            for (int i = 0; i < kSlots; ++i)
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
            props->setValue ("modeKey", modeKey);
            props->setValue ("window", windowState);
            props->setValue ("chromeProfile", chromeProfile);
            props->setValue ("chromeProfileName", chromeProfileName);
            for (int m = 0; m < kModes; ++m)
            {
                props->setValue (prefix (m) + "page", pages[(size_t) m]);
                for (int p = 0; p < kPages; ++p)
                {
                    props->setValue (prefix (m) + "pageName" + juce::String (p), names[(size_t) m][(size_t) p]);
                    for (int i = 0; i < kSlots; ++i)
                        props->setValue (slotKey (m, p, i), slots[(size_t) m][(size_t) p][(size_t) i].toString());
                }
            }
            props->saveIfNeeded();
        }

    private:
        std::unique_ptr<juce::PropertiesFile> props;
        std::array<std::array<std::array<Slot, kSlots>, kPages>, kModes> slots;
        std::array<std::array<juce::String, kPages>, kModes> names;
        std::array<int, kModes> pages {};

        // Logic Pro モードは 1.0.0 と同じ名前で保存する
        static juce::String prefix (int m) { return m == ModeLogic ? "" : "work_"; }
        static juce::String slotKey (int m, int p, int i) { return prefix (m) + "slot" + juce::String (p) + "_" + (i == 10 ? juce::String ("dot") : juce::String ((i + 1) % 10)); }

        void changed() { save(); if (onStateChanged) onStateChanged(); }

        void load()
        {
            enabled     = props->getBoolValue ("enabled", true);
            logicOnly   = props->getBoolValue ("logicOnly", true);
            alwaysOnTop = props->getBoolValue ("alwaysOnTop", false);
            mode        = juce::jlimit (0, kModes - 1, props->getIntValue ("mode", ModeLogic));
            toggleKey   = juce::jlimit (0, (int) ToggleNone, props->getIntValue ("toggleKey", ToggleClear));
            windowState = props->getValue ("window");
            modeKey     = props->getIntValue ("modeKey", 115);
            chromeProfile     = props->getValue ("chromeProfile");
            chromeProfileName = props->getValue ("chromeProfileName");
            for (int m = 0; m < kModes; ++m)
            {
                pages[(size_t) m] = juce::jlimit (0, kPages - 1, props->getIntValue (prefix (m) + "page", 0));
                for (int p = 0; p < kPages; ++p)
                {
                    const auto nk = prefix (m) + "pageName" + juce::String (p);
                    if (props->containsKey (nk)) names[(size_t) m][(size_t) p] = props->getValue (nk);
                    for (int i = 0; i < kSlots; ++i)
                        if (props->containsKey (slotKey (m, p, i)))
                            slots[(size_t) m][(size_t) p][(size_t) i] = Slot::fromString (props->getValue (slotKey (m, p, i)));
                }
            }

            // 1.3.0: Logic Pro モードの 4 ページ目（空いていた「自由」）に Reference Monitor の操作を入れる
            if (! props->getBoolValue ("refMonPage", false))
            {
                auto& p4 = slots[(size_t) ModeLogic][3];
                if (std::all_of (p4.begin(), p4.end(), [] (const Slot& s) { return s.actionId.isEmpty(); }))
                    resetPage (ModeLogic, 3);
                if (names[(size_t) ModeLogic][3] == jp ("自由"))
                    names[(size_t) ModeLogic][3] = defaultPageName (ModeLogic, 3);
                props->setValue ("refMonPage", true);
                save();
            }
            // 1.3.1: 1 ページ目の「キャプチャ録音」を Reference Monitor の出力 A/B 切替に
            if (! props->getBoolValue ("outSwitch", false))
            {
                auto& s3 = slots[(size_t) ModeLogic][0][2];
                if (s3.actionId == "capture") { s3 = Slot(); s3.actionId = "rmOut"; }
                props->setValue ("outSwitch", true);
                save();
            }
            // 1.3.2: 1 ページ目の「先頭へ」（Return で足りる）をトラックのフリーズに
            if (! props->getBoolValue ("freezeKey", false))
            {
                auto& s1 = slots[(size_t) ModeLogic][0][0];
                if (s1.actionId == "start") { s1 = Slot(); s1.actionId = "freeze"; }
                props->setValue ("freezeKey", true);
                save();
            }
        }
    };
}
