#pragma once
// プリセットブラウザ：検索・お気に入り（★）・前へ／次へ
//   並び：My Presets → Lumen（内蔵）→ 参考リバーブのプリセット（フォルダ順）
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    enum Kind { builtin = 0, mine = 1, reference = 2 };
    struct Entry
    {
        juce::String key, category, name;
        Kind kind = builtin;
        juce::File file;
        int program = -1;
    };

    PresetBrowser (LumenRoomsProcessor& p, juce::LookAndFeel& lf) : proc (p)
    {
        setLookAndFeel (&lf);
        search.setTextToShowWhenEmpty ("Search presets (name or folder)", juce::Colour (0xff6b7282));
        search.setFont (juce::FontOptions (14.0f));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1117));
        search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff252c38));
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xfff2a65a));
        search.setColour (juce::TextEditor::textColourId, juce::Colour (0xffdfe3ea));
        search.onTextChange = [this] { applyFilter(); };
        search.onReturnKey = [this] { if (! shown.empty()) load (*shown.front()); };
        search.onEscapeKey = [this] { if (onClose) onClose(); };
        addAndMakeVisible (search);

        favOnly.setButtonText (juce::String::fromUTF8 ("\xe2\x98\x85 ONLY"));
        favOnly.setClickingTogglesState (true);
        favOnly.onClick = [this] { applyFilter(); };
        addAndMakeVisible (favOnly);

        openFile.setButtonText ("OPEN FILE...");
        openFile.onClick = [this] { openFromFile(); };
        addAndMakeVisible (openFile);

        close.setButtonText ("CLOSE");
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (close);

        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0d1117));
        list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff252c38));
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        loadFavourites();
        refresh();
    }
    ~PresetBrowser() override { list.setModel (nullptr); setLookAndFeel (nullptr); }

    std::function<void()> onClose, onLoaded;

    void refresh()
    {
        all.clear();
        auto mineFiles = LumenRoomsProcessor::myPresetFolder().findChildFiles (juce::File::findFiles, false, "*.lrpreset");
        mineFiles.sort();
        for (auto& f : mineFiles) all.push_back ({ "my:" + f.getFullPathName(), "My Presets", f.getFileNameWithoutExtension(), mine, f, -1 });
        for (int i = 0; i < proc.getNumPrograms(); ++i) all.push_back ({ "builtin:" + juce::String (i), "Lumen", proc.getProgramName (i), builtin, {}, i });
        const auto root = LumenRoomsProcessor::defaultPresetFolder();
        if (root.getFileName() == "Presets" || root.getFileName() == "Factory Presets")
        {
            auto files = root.findChildFiles (juce::File::findFiles, true, "*.crp_preset");
            std::vector<Entry> refs;
            for (auto& f : files)
            {
                auto rel = f.getParentDirectory().getRelativePathFrom (root);
                if (rel == ".") rel = "Factory";
                refs.push_back ({ "ref:" + f.getFullPathName(), rel, f.getFileNameWithoutExtension(), reference, f, -1 });
            }
            std::sort (refs.begin(), refs.end(), [] (const Entry& a, const Entry& b)
                       { return a.category != b.category ? a.category.compareNatural (b.category) < 0 : a.name.compareNatural (b.name) < 0; });
            for (auto& e : refs) all.push_back (e);
        }
        const auto userRef = LumenRoomsProcessor::userPresetFolder();
        for (auto& f : userRef.findChildFiles (juce::File::findFiles, true, "*.crp_preset"))
            all.push_back ({ "ref:" + f.getFullPathName(), "User Presets", f.getFileNameWithoutExtension(), reference, f, -1 });
        applyFilter();
    }

    // 前へ／次へ（★ ONLY が押されていればお気に入りの中で）
    void step (int dir)
    {
        std::vector<const Entry*> seq;
        for (auto& e : all) if (! favOnly.getToggleState() || favs.contains (e.key)) seq.push_back (&e);
        if (seq.empty()) return;
        const auto cur = proc.getPresetKey();
        int idx = -1;
        for (int i = 0; i < (int) seq.size(); ++i) if (seq[(size_t) i]->key == cur) idx = i;
        const int n = (int) seq.size();
        const int next = idx < 0 ? (dir > 0 ? 0 : n - 1) : ((idx + dir) % n + n) % n;
        load (*seq[(size_t) next]);
    }

    void focusSearch() { search.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141922));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (juce::Colour (0xfff2a65a).withAlpha (0.6f));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (juce::Colour (0xffdfe3ea));
        g.drawText ("PRESETS", 18, 12, 120, 22, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (juce::Colour (0xff8a92a2));
        g.drawText (juce::String ((int) shown.size()) + " / " + juce::String ((int) all.size()) + "   click = load   "
                        + juce::String::fromUTF8 ("\xe2\x98\x85") + " = favourite",
                    getWidth() - 360, getHeight() - 24, 344, 18, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto top = r.removeFromTop (28);
        top.removeFromLeft (90);
        close.setBounds (top.removeFromRight (74));
        top.removeFromRight (8);
        openFile.setBounds (top.removeFromRight (110));
        top.removeFromRight (8);
        favOnly.setBounds (top.removeFromRight (84));
        top.removeFromRight (12);
        search.setBounds (top);
        r.removeFromTop (8);
        r.removeFromBottom (20);
        list.setBounds (r);
    }

private:
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& e = *shown[(size_t) row];
        const bool current = e.key == proc.getPresetKey();
        if (current) { g.setColour (juce::Colour (0xfff2a65a).withAlpha (0.16f)); g.fillRect (0, 0, w, h); }
        else if (row % 2) { g.setColour (juce::Colours::white.withAlpha (0.02f)); g.fillRect (0, 0, w, h); }
        const bool fav = favs.contains (e.key);
        g.setFont (juce::FontOptions (15.0f));
        g.setColour (fav ? juce::Colour (0xfff2a65a) : juce::Colour (0xff3a4150));
        g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"), 6, 0, 22, h, juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (e.kind == mine ? juce::Colour (0xff4fd1c5) : juce::Colour (0xff6b7282));
        g.drawText (e.category, 34, 0, 170, h, juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (current ? juce::Colour (0xfff2a65a) : juce::Colour (0xffdfe3ea));
        g.drawText (e.name, 214, 0, w - 224, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& ev) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& e = *shown[(size_t) row];
        if (ev.x < 30) { toggleFavourite (e.key); return; }
        load (e);
    }

    void returnKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, (int) shown.size())) load (*shown[(size_t) row]);
    }

    void applyFilter()
    {
        shown.clear();
        auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
        words.removeEmptyStrings();
        for (auto& e : all)
        {
            if (favOnly.getToggleState() && ! favs.contains (e.key)) continue;
            const auto hay = (e.category + " " + e.name).toLowerCase();
            bool ok = true;
            for (auto& w : words) if (! hay.contains (w)) { ok = false; break; }
            if (ok) shown.push_back (&e);
        }
        list.updateContent();
        list.repaint();
        repaint();
    }

    void load (const Entry& e)
    {
        juce::String err;
        if (e.kind == builtin) proc.setCurrentProgram (e.program);
        else if (e.kind == mine) err = proc.loadMyPreset (e.file);
        else err = proc.importPreset (e.file);
        if (err.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "LOAD", e.name + "\n" + err);
            return;
        }
        proc.setPresetKey (e.key);
        list.repaint();
        if (onLoaded) onLoaded();
    }

    void openFromFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load preset", LumenRoomsProcessor::defaultPresetFolder(), "*.crp_preset;*.lrpreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const bool isMine = f.hasFileExtension ("lrpreset");
            load ({ (isMine ? "my:" : "ref:") + f.getFullPathName(), "File", f.getFileNameWithoutExtension(), isMine ? mine : reference, f, -1 });
        });
    }

    static juce::File favouritesFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MyPlugins/LumenRooms/favourites.txt");
    }
    void loadFavourites()
    {
        favs.clear();
        favs.addLines (favouritesFile().loadFileAsString());
        favs.removeEmptyStrings();
    }
    void toggleFavourite (const juce::String& key)
    {
        if (favs.contains (key)) favs.removeString (key); else favs.add (key);
        favouritesFile().getParentDirectory().createDirectory();
        favouritesFile().replaceWithText (favs.joinIntoString ("\n"));
        applyFilter();
    }

    LumenRoomsProcessor& proc;
    juce::TextEditor search;
    juce::TextButton favOnly, openFile, close;
    juce::ListBox list;
    std::vector<Entry> all;
    std::vector<const Entry*> shown;
    juce::StringArray favs;
    std::unique_ptr<juce::FileChooser> chooser;
};
