#pragma once
// 全プラグイン共通: 自分のプリセットの保存・読み込み と 取り消し・やり直し
//
// 使い方（各プラグイン）:
//   プロセッサー:  myp::Presets myPresets { *this, apvts, "WoolFuzz" };   // apvts より後に書く
//   エディター:    myp::PresetBar presetBar { processor.myPresets, accent, panel, text };
//                  addAndMakeVisible (presetBar);  presetBar.setBounds (area.removeFromBottom (myp::PresetBar::height));
//
// プリセット: ~/Library/Audio/Presets/MyPlugins/<名前>/<プリセット名>.mypreset（XML、パラメーターの値だけ）
//   パラメーター以外（Balance Scope の参照カーブ、LEARN の結果など）と LINK の番号は変えない
// 取り消し: 画面でつまみを触った（マウスを押して離した・ホイール・キー入力）前後の値の差を 1 手として覚える。
//   オートメーションの再生による変化は覚えない。プロセッサーが持つので、画面を閉じても残る（100 手まで）
#include <JuceHeader.h>

namespace myp
{
    class Presets
    {
    public:
        Presets (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, const juce::String& pluginFolderName)
            : proc (p), state (s), folderName (pluginFolderName) {}

        juce::File folder() const
        {
            return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Audio/Presets/MyPlugins").getChildFile (folderName);
        }

        juce::Array<juce::File> list() const
        {
            auto files = folder().findChildFiles (juce::File::findFiles, false, "*.mypreset");
            struct ByName { static int compareElements (const juce::File& a, const juce::File& b)
                { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()); } };
            ByName cmp; files.sort (cmp);
            return files;
        }

        juce::String currentName() const { return state.state.getProperty ("mypPreset", "").toString(); }

        bool save (const juce::String& rawName)
        {
            const auto name = juce::File::createLegalFileName (rawName.trim());
            if (name.isEmpty()) return false;
            folder().createDirectory();
            juce::XmlElement xml ("MyPluginsPreset");
            xml.setAttribute ("plugin", folderName);
            xml.setAttribute ("version", 1);
            for (auto* p : proc.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    if (excluded (r->getParameterID())) continue;
                    auto* e = xml.createNewChildElement ("PARAM");
                    e->setAttribute ("id", r->getParameterID());
                    e->setAttribute ("value", (double) r->getValue());
                }
            const bool ok = xml.writeTo (folder().getChildFile (name + ".mypreset"));
            if (ok) state.state.setProperty ("mypPreset", name, nullptr);
            return ok;
        }

        bool load (const juce::File& f)
        {
            auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr || ! xml->hasTagName ("MyPluginsPreset")) return false;
            std::map<juce::String, float> values;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
            Step step;
            for (auto* p : proc.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    if (excluded (r->getParameterID())) continue;
                    auto it = values.find (r->getParameterID());
                    const float v = it != values.end() ? it->second : r->getDefaultValue();   // 古いプリセットにない値は初期値
                    if (std::abs (v - r->getValue()) > 1.0e-6f) step.push_back ({ r, r->getValue(), v });
                }
            apply (step, false);
            pushUndo (std::move (step));
            state.state.setProperty ("mypPreset", f.getFileNameWithoutExtension(), nullptr);
            return true;
        }

        // ---- 取り消し・やり直し ----
        struct Change { juce::RangedAudioParameter* p; float before, after; };
        using Step = std::vector<Change>;

        bool canUndo() const { return ! undoStack.empty(); }
        bool canRedo() const { return ! redoStack.empty(); }
        void undo() { if (canUndo()) { auto s = std::move (undoStack.back()); undoStack.pop_back(); apply (s, true); redoStack.push_back (std::move (s)); } }
        void redo() { if (canRedo()) { auto s = std::move (redoStack.back()); redoStack.pop_back(); apply (s, false); undoStack.push_back (std::move (s)); } }

        // 画面の操作の前後で呼ぶ（PresetBar が自動で呼ぶ）
        void snapshot() { snap.clear(); for (auto* p : proc.getParameters()) snap.push_back (p->getValue()); }
        void commitSinceSnapshot()
        {
            if (snap.empty()) return;
            Step step;
            auto params = proc.getParameters();
            for (int i = 0; i < params.size() && i < (int) snap.size(); ++i)
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
                    if (! excluded (r->getParameterID()) && std::abs (r->getValue() - snap[(size_t) i]) > 1.0e-6f)
                        step.push_back ({ r, snap[(size_t) i], r->getValue() });
            snap.clear();
            if (! step.empty())
            {
                pushUndo (std::move (step));
                if (currentName().isNotEmpty() && ! currentName().endsWith (" *"))
                    state.state.setProperty ("mypPreset", currentName() + " *", nullptr);   // 呼び出した後に変えた印
            }
        }

        static bool excluded (const juce::String& id) { return id == "linkGroup"; }

    private:
        void apply (const Step& s, bool backwards)
        {
            for (const auto& c : s)
            {
                c.p->beginChangeGesture();
                c.p->setValueNotifyingHost (backwards ? c.before : c.after);
                c.p->endChangeGesture();
            }
        }
        void pushUndo (Step s)
        {
            if (s.empty()) return;
            undoStack.push_back (std::move (s));
            if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
            redoStack.clear();
        }

        juce::AudioProcessor& proc;
        juce::AudioProcessorValueTreeState& state;
        juce::String folderName;
        std::vector<Step> undoStack, redoStack;
        std::vector<float> snap;
    };

    // 画面の帯: [↶] [↷]  [◀] [ プリセット名 ▾ ] [▶]  [SAVE]
    class PresetBar : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int height = 28;

        // undoOnly = true: 取り消し・やり直しだけ（自前のプリセット画面があるプラグイン用）
        PresetBar (Presets& p, juce::Colour accentColour, juce::Colour panelColour, juce::Colour textColour, bool undoOnly = false)
            : presets (p), accent (accentColour), panel (panelColour), text (textColour), onlyUndo (undoOnly)
        {
            for (auto* b : { &undoBtn, &redoBtn, &prevBtn, &nextBtn, &nameBtn, &saveBtn })
            {
                b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.12f));
                b->setColour (juce::TextButton::buttonOnColourId, accent);
                b->setColour (juce::TextButton::textColourOffId, text);
                b->setColour (juce::TextButton::textColourOnId, panel);
                b->setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
                addAndMakeVisible (b);
            }
            undoBtn.setTooltip ("Undo (取り消し)");
            redoBtn.setTooltip ("Redo (やり直し)");
            prevBtn.setTooltip ("Previous preset");
            nextBtn.setTooltip ("Next preset");
            nameBtn.setTooltip ("Your presets");
            saveBtn.setTooltip ("Save the current settings as a preset");
            undoBtn.onClick = [this] { presets.undo(); refresh(); };
            redoBtn.onClick = [this] { presets.redo(); refresh(); };
            prevBtn.onClick = [this] { step (-1); };
            nextBtn.onClick = [this] { step (+1); };
            nameBtn.onClick = [this] { showMenu(); };
            saveBtn.onClick = [this] { askSave (presets.currentName().upToLastOccurrenceOf (" *", false, false)); };
            if (onlyUndo) for (auto* b : { &prevBtn, &nextBtn, &nameBtn, &saveBtn }) b->setVisible (false);
            refresh();
            startTimerHz (10);
        }

        // 置くのにちょうどいい幅
        int preferredWidth() const { return onlyUndo ? 120 : 430; }

        ~PresetBar() override
        {
            if (watched != nullptr) watched->removeMouseListener (&watcher);
        }

        // 親（エディター）に付いたら、エディター全体のマウス操作を見る
        void parentHierarchyChanged() override
        {
            auto* top = getTopLevelComponent();
            if (top == watched) return;
            if (watched != nullptr) watched->removeMouseListener (&watcher);
            watched = top;
            if (watched != nullptr && watched != this) watched->addMouseListener (&watcher, true);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            undoBtn.setBounds (r.removeFromLeft (52)); r.removeFromLeft (3);
            redoBtn.setBounds (r.removeFromLeft (52)); r.removeFromLeft (10);
            saveBtn.setBounds (r.removeFromRight (58)); r.removeFromRight (6);
            prevBtn.setBounds (r.removeFromLeft (26)); r.removeFromLeft (3);
            nextBtn.setBounds (r.removeFromRight (26)); r.removeFromRight (3);
            nameBtn.setBounds (r);
        }

    private:
        // ---- 操作の前後をとらえる ----
        struct Watcher : juce::MouseListener
        {
            explicit Watcher (PresetBar& o) : owner (o) {}
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (owner.isOwnButton (e.eventComponent)) return;
                if (! owner.pending) owner.presets.snapshot();
                owner.pending = true; owner.quietTicks = -1;
            }
            void mouseUp (const juce::MouseEvent& e) override
            {
                if (owner.isOwnButton (e.eventComponent)) return;
                owner.quietTicks = 0;       // 少し待ってから（ポップアップで選んだ値が入るのを待つ）
            }
            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails&) override
            {
                if (owner.isOwnButton (e.eventComponent)) return;
                if (! owner.pending) { owner.presets.snapshot(); owner.pending = true; }
                owner.quietTicks = 0;
            }
            PresetBar& owner;
        };
        bool isOwnButton (juce::Component* c) const { return c != nullptr && (c == this || isParentOf (c)); }

        void timerCallback() override
        {
            if (pending && quietTicks >= 0 && ++quietTicks >= 4)   // 0.4 秒動きがなければ 1 手
            {
                presets.commitSinceSnapshot();
                pending = false; quietTicks = -1;
            }
            refresh();
        }

        void refresh()
        {
            undoBtn.setEnabled (presets.canUndo());
            redoBtn.setEnabled (presets.canRedo());
            auto n = presets.currentName();
            nameBtn.setButtonText (n.isEmpty() ? "(no preset)" : n);
        }

        void step (int dir)
        {
            auto files = presets.list();
            if (files.isEmpty()) return;
            const auto cur = presets.currentName().upToLastOccurrenceOf (" *", false, false);
            int idx = -1;
            for (int i = 0; i < files.size(); ++i) if (files[i].getFileNameWithoutExtension() == cur) idx = i;
            idx = idx < 0 ? (dir > 0 ? 0 : files.size() - 1) : (idx + dir + files.size()) % files.size();
            presets.load (files[idx]);
            refresh();
        }

        void showMenu()
        {
            juce::PopupMenu m;
            auto files = presets.list();
            const auto cur = presets.currentName().upToLastOccurrenceOf (" *", false, false);
            if (files.isEmpty()) m.addItem (-1, "(no saved presets yet - press SAVE)", false);
            for (int i = 0; i < files.size(); ++i)
                m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == cur);
            m.addSeparator();
            m.addItem (1000, "Save as...");
            m.addItem (1001, "Delete \"" + cur + "\"", cur.isNotEmpty() && presets.folder().getChildFile (cur + ".mypreset").existsAsFile());
            m.addItem (1002, "Show preset folder in Finder");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameBtn), [this, files, cur] (int r)
            {
                if (r >= 1 && r <= files.size()) presets.load (files[r - 1]);
                else if (r == 1000) askSave (cur);
                else if (r == 1001) presets.folder().getChildFile (cur + ".mypreset").moveToTrash();
                else if (r == 1002) { presets.folder().createDirectory(); presets.folder().revealToUser(); }
                refresh();
            });
        }

        void askSave (const juce::String& suggestion)
        {
            auto* w = new juce::AlertWindow ("Save preset", "Preset name", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", suggestion.isEmpty() ? "My preset" : suggestion);
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<PresetBar> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
            {
                if (safe == nullptr || result != 1) return;
                const auto name = w->getTextEditorContents ("name");
                auto& ps = safe->presets;
                const auto f = ps.folder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".mypreset");
                if (f.existsAsFile())
                {
                    juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save preset",
                        "\"" + name + "\" already exists. Overwrite?", safe.getComponent(),
                        juce::ModalCallbackFunction::create ([safe, name] (int ok) { if (ok && safe != nullptr) { safe->presets.save (name); safe->refresh(); } }));
                    return;
                }
                ps.save (name);
                safe->refresh();
            }), true);
        }

        Presets& presets;
        juce::Colour accent, panel, text;
        juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" },
                         prevBtn { "<" }, nextBtn { ">" }, nameBtn, saveBtn { "SAVE" };
        juce::Component* watched = nullptr;
        Watcher watcher { *this };
        bool pending = false, onlyUndo = false;
        int quietTicks = -1;
    };
}
