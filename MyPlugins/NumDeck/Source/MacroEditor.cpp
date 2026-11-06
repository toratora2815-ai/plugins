#include "MacroEditor.h"

using namespace numdeck;

namespace
{
    const juce::Colour bg (0xff0d1218), card (0xff1c2530), line (0xff2a3644), text (0xffe8eef5),
                       sub (0xff8796a8), accent (0xffffb84d), learnCol (0xffff6fae);
    constexpr int rowH = 34;

    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Colour kindColour (const Step& s)
    {
        if (s.kind == "wait") return sub;
        if (s.kind == "app")  return catColour (Cat::App);
        if (s.kind == "url")  return catColour (Cat::Link);
        if (s.kind == "key")  return catColour (Cat::Custom);
        if (auto* a = findAction (s.actionId)) return catColour (a->cat);
        return sub;
    }

    juce::String kindTag (const Step& s)
    {
        if (s.kind == "wait") return jp ("待つ");
        if (s.kind == "app")  return jp ("アプリ");
        if (s.kind == "url")  return jp ("リンク");
        if (s.kind == "key")  return jp ("キー");
        return jp ("操作");
    }
}

MacroEditor::MacroEditor (const Slot& current, Host h) : host (std::move (h))
{
    if (current.actionId == "macro") steps = current.steps;

    nameLabel.setText (jp ("マクロの名前（タイルに出る）"), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, sub);
    nameLabel.setFont (font (12.0f));
    addAndMakeVisible (nameLabel);
    nameBox.setText (current.actionId == "macro" ? current.label : juce::String());
    nameBox.setTextToShowWhenEmpty (jp ("例：研修の準備"), sub.withAlpha (0.6f));
    nameBox.setColour (juce::TextEditor::backgroundColourId, card);
    nameBox.setColour (juce::TextEditor::outlineColourId, line);
    nameBox.setColour (juce::TextEditor::textColourId, text);
    nameBox.setFont (font (15.0f, true));
    nameBox.applyFontToAllText (font (15.0f, true));
    addAndMakeVisible (nameBox);

    auto setup = [this] (juce::TextButton& b, const char* t, std::function<void()> fn)
    {
        b.setButtonText (jp (t));
        b.setColour (juce::TextButton::buttonColourId, card);
        b.setColour (juce::TextButton::textColourOffId, text);
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    };
    setup (addAction,  "+ 操作",            [this] { showAddActionMenu(); });
    setup (addKey,     "+ キーを覚えさせる", [this] { toggleLearning(); });
    setup (addWaitBtn, "+ 待つ",            [this] { addWait(); });
    setup (addApp,     "+ アプリ",          [this] { showAddAppMenu(); });
    setup (addLinkBtn, "+ リンク",          [this] { addLink(); });
    setup (testBtn,    "試す",              [this]
    {
        stopLearning();
        if (! steps.empty() && host.test) host.test (steps);
    });
    setup (saveBtn,    "保存",              [this]
    {
        stopLearning();
        Slot s;
        s.actionId = "macro";
        s.label = nameBox.getText().trim();
        s.steps = steps;
        if (host.save) host.save (s);
        if (host.close) host.close();
    });
    setup (cancelBtn,  "キャンセル",        [this] { stopLearning(); if (host.close) host.close(); });
    saveBtn.setColour (juce::TextButton::buttonColourId, accent);
    saveBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

    setSize (620, 560);
    startTimerHz (30);
}

MacroEditor::~MacroEditor()
{
    stopLearning();
}

void MacroEditor::timerCallback()
{
    if (learning) { learnPulse += 0.08f; repaint (listArea); }
}

void MacroEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    nameLabel.setBounds (b.removeFromTop (18));
    nameBox.setBounds (b.removeFromTop (34));
    b.removeFromTop (10);

    auto bottom = b.removeFromBottom (36);
    saveBtn.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    cancelBtn.setBounds (bottom.removeFromRight (100));
    testBtn.setBounds (bottom.removeFromLeft (100));
    b.removeFromBottom (10);

    auto adds = b.removeFromBottom (34);
    const int w = (adds.getWidth() - 4 * 6) / 5;
    for (auto* btn : { &addAction, &addKey, &addWaitBtn, &addApp, &addLinkBtn })
    {
        btn->setBounds (adds.removeFromLeft (btn == &addKey ? w + 40 : w - 10));
        adds.removeFromLeft (6);
    }
    b.removeFromBottom (8);
    listArea = b;
}

juce::Rectangle<int> MacroEditor::rowBounds (int row) const
{
    return { listArea.getX() + 6, listArea.getY() + 6 + row * rowH - scroll, listArea.getWidth() - 12, rowH - 4 };
}

int MacroEditor::rowAt (juce::Point<int> p) const
{
    if (! listArea.contains (p)) return -1;
    const int r = (p.y - listArea.getY() - 6 + scroll) / rowH;
    return r >= 0 && r < (int) steps.size() ? r : -1;
}

void MacroEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto la = listArea.toFloat();
    g.setColour (card.darker (0.3f));
    g.fillRoundedRectangle (la, 8.0f);
    g.setColour (learning ? learnCol.withAlpha (0.5f + 0.5f * std::sin (learnPulse)) : line);
    g.drawRoundedRectangle (la.reduced (0.5f), 8.0f, learning ? 2.0f : 1.0f);

    g.saveState();
    g.reduceClipRegion (listArea.reduced (2));

    if (steps.empty() && ! learning)
    {
        g.setColour (sub);
        g.setFont (font (14.0f));
        g.drawFittedText (jp ("下のボタンで手順を足してください。\n"
                              "上から順番に実行されます（キーとキーの間は自動で 80 ms あきます）。\n\n"
                              "行はドラッグで並べ替え、右クリックで直す・消す。"),
                          listArea.reduced (20), juce::Justification::centred, 6);
    }

    for (int i = 0; i < (int) steps.size(); ++i)
    {
        auto r = rowBounds (i).toFloat();
        if (dragging && i == pressRow) { g.setColour (accent.withAlpha (0.15f)); g.fillRoundedRectangle (r, 6.0f); continue; }
        const auto& s = steps[(size_t) i];
        const auto c = kindColour (s);
        g.setColour (i == selected ? card.brighter (0.15f) : card);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (c.withAlpha (0.9f));
        g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);

        auto in = r.reduced (12.0f, 0.0f);
        g.setColour (sub);
        g.setFont (font (12.0f));
        g.drawText (juce::String (i + 1) + ".", in.removeFromLeft (26.0f), juce::Justification::centredLeft);
        auto tag = in.removeFromLeft (52.0f).withSizeKeepingCentre (46.0f, 20.0f);
        g.setColour (c.withAlpha (0.18f));
        g.fillRoundedRectangle (tag, 5.0f);
        g.setColour (c);
        g.setFont (font (11.0f, true));
        g.drawText (kindTag (s), tag, juce::Justification::centred);
        in.removeFromLeft (8.0f);
        g.setColour (text);
        g.setFont (font (14.0f));
        g.drawFittedText (s.describe(), in.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }

    if (learning)
    {
        auto r = rowBounds ((int) steps.size()).toFloat();
        g.setColour (learnCol.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (learnCol);
        g.setFont (font (13.0f, true));
        g.drawText (jp ("足したいショートカットを押してください（続けて押せば続けて足されます。Esc で終わり）"),
                    r.reduced (12.0f, 0.0f), juce::Justification::centredLeft);
    }

    if (dragging && dropRow >= 0)
    {
        const int y = rowBounds (dropRow).getY() - 3;
        g.setColour (accent);
        g.fillRect (listArea.getX() + 6, y, listArea.getWidth() - 12, 2);
    }
    g.restoreState();
}

void MacroEditor::mouseDown (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    selected = r;
    pressRow = r;
    dragging = false;
    if (r >= 0 && e.mods.isPopupMenu()) { pressRow = -1; showRowMenu (r); }
    repaint();
}

void MacroEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (pressRow < 0) return;
    if (! dragging && e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const int y = e.getPosition().y - listArea.getY() - 6 + scroll + rowH / 2;
    dropRow = juce::jlimit (0, (int) steps.size(), y / rowH);
    repaint();
}

void MacroEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging && pressRow >= 0 && dropRow >= 0)
    {
        auto s = steps[(size_t) pressRow];
        steps.erase (steps.begin() + pressRow);
        const int to = dropRow > pressRow ? dropRow - 1 : dropRow;
        steps.insert (steps.begin() + juce::jlimit (0, (int) steps.size(), to), s);
        selected = to;
    }
    dragging = false;
    pressRow = dropRow = -1;
    repaint();
}

void MacroEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r >= 0) editStep (r);
}

void MacroEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int maxScroll = juce::jmax (0, 12 + ((int) steps.size() + 1) * rowH - listArea.getHeight());
    scroll = juce::jlimit (0, maxScroll, scroll - (int) (w.deltaY * 200.0f));
    repaint();
}

void MacroEditor::addStep (const Step& s, bool withWaitAfter)
{
    const int at = selected >= 0 && selected < (int) steps.size() ? selected + 1 : (int) steps.size();
    steps.insert (steps.begin() + at, s);
    selected = at;
    if (withWaitAfter)
    {
        // アプリやページは開くまで少しかかるので、後ろに「待つ」を入れておく（あとで消せる）
        Step w; w.kind = "wait"; w.ms = 1000;
        steps.insert (steps.begin() + at + 1, w);
        selected = at + 1;
    }
    // 下まで足したら見えるようにスクロール
    const int bottom = 6 + (selected + 2) * rowH;
    if (bottom - scroll > listArea.getHeight()) scroll = bottom - listArea.getHeight();
    repaint();
}

void MacroEditor::showAddActionMenu()
{
    juce::PopupMenu m;
    std::map<int, juce::String> ids;
    int id = 1;
    const std::vector<Cat> order = host.mode == Deck::ModeLogic
        ? std::vector<Cat> { Cat::Transport, Cat::Edit, Cat::Track, Cat::Window, Cat::File, Cat::RefMon, Cat::IdeaCatcher, Cat::Aside, Cat::General, Cat::Browser }
        : std::vector<Cat> { Cat::General, Cat::Browser, Cat::Edit, Cat::File, Cat::RefMon, Cat::IdeaCatcher, Cat::Aside };
    for (auto cat : order)
    {
        juce::PopupMenu sub;
        for (auto& a : catalog())
        {
            if (a.cat != cat) continue;
            sub.addItem (id, a.remote != nullptr ? jp (a.name) : jp (a.name) + "    " + shortcutText (a.keyCode, a.mods));
            ids[id++] = a.id;
        }
        m.addSubMenu (catName (cat), sub);
    }
    juce::Component::SafePointer<MacroEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addAction), [safe, ids] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (auto it = ids.find (r); it != ids.end())
        {
            Step s; s.kind = "action"; s.actionId = it->second;
            safe->addStep (s);
        }
    });
}

void MacroEditor::showAddAppMenu()
{
    juce::PopupMenu m;
    static const char* featured[] = {
        "/Applications/Google Chrome.app", "/Applications/Slack.app", "/System/Library/CoreServices/Finder.app",
        "/Applications/Microsoft Excel.app", "/Applications/Microsoft Word.app", "/Applications/Microsoft PowerPoint.app",
        "/Applications/zoom.us.app", "/Applications/Logic Pro.app", "/System/Applications/Notes.app",
        "/System/Applications/Calendar.app", "/System/Applications/Mail.app" };
    std::map<int, juce::String> paths;
    int id = 1;
    for (auto* p : featured)
        if (juce::File (p).exists())
        {
            m.addItem (id, juce::String::fromUTF8 (KeyTap::appDisplayName (p).c_str()));
            paths[id++] = p;
        }
    m.addSeparator();
    m.addItem (999, jp ("ほかのアプリを選ぶ\xe2\x80\xa6"));
    juce::Component::SafePointer<MacroEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addApp), [safe, paths] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 999)
        {
            safe->chooser = std::make_unique<juce::FileChooser> (jp ("開くアプリを選んでください"), juce::File ("/Applications"), "*.app");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            if (safe == nullptr || fc.getResult() == juce::File()) return;
                                            Step s; s.kind = "app"; s.path = fc.getResult().getFullPathName();
                                            safe->addStep (s, true);
                                        });
            return;
        }
        if (auto it = paths.find (r); it != paths.end())
        {
            Step s; s.kind = "app"; s.path = it->second;
            safe->addStep (s, true);
        }
    });
}

void MacroEditor::addWait()
{
    juce::Component::SafePointer<MacroEditor> safe (this);
    askNumber (jp ("待つ時間（ミリ秒。1000 = 1 秒）"), 500, [safe] (int ms)
    {
        if (safe == nullptr) return;
        Step s; s.kind = "wait"; s.ms = juce::jlimit (10, 60000, ms);
        safe->addStep (s);
    });
}

void MacroEditor::addLink()
{
    juce::PopupMenu m;
    m.addItem (1, jp ("Chrome で今開いているページ"));
    m.addItem (2, jp ("アドレス（URL）を入力\xe2\x80\xa6"));
    juce::Component::SafePointer<MacroEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addLinkBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        juce::String url = "https://", label;
        if (r == 1)
        {
            std::string u, t;
            if (! KeyTap::chromeFrontPage (u, t))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, jp ("Chrome のページが取れませんでした"),
                    jp ("Chrome で登録したいページを開いてから、もう一度選んでください。"));
                return;
            }
            url = juce::String::fromUTF8 (u.c_str());
            label = juce::String::fromUTF8 (t.c_str()).upToFirstOccurrenceOf (" - ", false, false).trim();
        }
        safe->askLink (url, label, [safe] (juce::String u, juce::String l)
        {
            if (safe == nullptr) return;
            Step s; s.kind = "url"; s.url = u; s.label = l;
            safe->addStep (s, true);
            safe->showPlaceMenu (safe->selected - 1);   // すぐに「どのアカウントで・どこに」を選べるように
        });
    });
}

// 行を直す（種類に合わせて）
void MacroEditor::editStep (int row)
{
    if (row < 0 || row >= (int) steps.size()) return;
    auto& s = steps[(size_t) row];
    juce::Component::SafePointer<MacroEditor> safe (this);
    if (s.kind == "wait")
        askNumber (jp ("待つ時間（ミリ秒）"), s.ms, [safe, row] (int ms)
        {
            if (safe != nullptr && row < (int) safe->steps.size()) { safe->steps[(size_t) row].ms = juce::jlimit (10, 60000, ms); safe->repaint(); }
        });
    else if (s.kind == "url")
        showPlaceMenu (row);
    else
        showRowMenu (row);
}

void MacroEditor::showRowMenu (int row)
{
    if (row < 0 || row >= (int) steps.size()) return;
    const auto& s = steps[(size_t) row];
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (row + 1) + ". " + s.describe());
    if (s.kind == "wait") m.addItem (1, jp ("時間を変える\xe2\x80\xa6"));
    if (s.kind == "url")
    {
        m.addItem (2, jp ("アカウントと開く場所\xe2\x80\xa6"));
        m.addItem (3, jp ("アドレスと名前を変える\xe2\x80\xa6"));
    }
    m.addItem (4, jp ("上へ"), row > 0);
    m.addItem (5, jp ("下へ"), row < (int) steps.size() - 1);
    m.addItem (6, jp ("複製"));
    m.addSeparator();
    m.addItem (7, jp ("消す"));
    juce::Component::SafePointer<MacroEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (rowBounds (row))), [safe, row] (int r)
    {
        if (safe == nullptr || r == 0 || row >= (int) safe->steps.size()) return;
        auto& st = safe->steps;
        if (r == 1) { safe->editStep (row); return; }
        if (r == 2) { safe->showPlaceMenu (row); return; }
        if (r == 3)
        {
            safe->askLink (st[(size_t) row].url, st[(size_t) row].label, [safe, row] (juce::String u, juce::String l)
            {
                if (safe == nullptr || row >= (int) safe->steps.size()) return;
                safe->steps[(size_t) row].url = u; safe->steps[(size_t) row].label = l; safe->repaint();
            });
            return;
        }
        if (r == 4) { std::swap (st[(size_t) row], st[(size_t) row - 1]); safe->selected = row - 1; }
        if (r == 5) { std::swap (st[(size_t) row], st[(size_t) row + 1]); safe->selected = row + 1; }
        if (r == 6) { st.insert (st.begin() + row + 1, st[(size_t) row]); safe->selected = row + 1; }
        if (r == 7) { st.erase (st.begin() + row); safe->selected = juce::jmin (row, (int) st.size() - 1); }
        safe->repaint();
    });
}

// リンクの行: どの Chrome アカウントで、どの画面のどこに開くか
void MacroEditor::showPlaceMenu (int row)
{
    if (row < 0 || row >= (int) steps.size() || steps[(size_t) row].kind != "url") return;
    const auto place = steps[(size_t) row].place;

    juce::PopupMenu m;
    m.addSectionHeader (jp ("このページを開く Chrome のアカウント"));
    const auto profiles = KeyTap::chromeProfiles();
    m.addItem (100, jp ("設定のアカウントのまま"), true, place.profile.isEmpty());
    for (size_t i = 0; i < profiles.size(); ++i)
        m.addItem (101 + (int) i, juce::String::fromUTF8 (profiles[i].second.c_str()), true,
                   place.profile == juce::String::fromUTF8 (profiles[i].first.c_str()));

    m.addSectionHeader (jp ("開く場所"));
    m.addItem (200, jp ("Chrome にまかせる（置き直さない）"), true, ! place.placed());
    const auto screens = KeyTap::screenNames();
    static const char* regions[] = { "full", "left", "right", "top", "bottom" };
    std::vector<std::pair<juce::String, juce::String>> placeIds;   // id - 200 - 1 → (screen, region)
    auto addScreen = [&] (const juce::String& screenName, const juce::String& shown)
    {
        juce::PopupMenu sub;
        bool any = false;
        for (auto* rg : regions)
        {
            const bool sel = place.placed() && place.screen == screenName && place.region == rg;
            any = any || sel;
            placeIds.push_back ({ screenName, rg });
            sub.addItem (200 + (int) placeIds.size(), LinkPlace::regionName (rg), true, sel);
        }
        m.addSubMenu (shown, sub, true, nullptr, any);
    };
    for (size_t i = 0; i < screens.size(); ++i)
    {
        const auto n = juce::String::fromUTF8 (screens[i].c_str());
        addScreen (n, n + (i == 0 ? jp ("（メイン）") : juce::String()));
    }
    addScreen ({}, jp ("マウスのある画面"));

    juce::Component::SafePointer<MacroEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (rowBounds (row))),
                     [safe, row, profiles, placeIds] (int r)
    {
        if (safe == nullptr || r == 0 || row >= (int) safe->steps.size()) return;
        auto& p = safe->steps[(size_t) row].place;
        if (r == 100) { p.profile = {}; p.profileName = {}; }
        else if (r > 100 && r < 200)
        {
            const auto& pr = profiles[(size_t) (r - 101)];
            p.profile = juce::String::fromUTF8 (pr.first.c_str());
            p.profileName = juce::String::fromUTF8 (pr.second.c_str());
        }
        else if (r == 200) { p.region = {}; p.screen = {}; }
        else if (r > 200 && r - 201 < (int) placeIds.size())
        {
            p.screen = placeIds[(size_t) (r - 201)].first;
            p.region = placeIds[(size_t) (r - 201)].second;
        }
        safe->repaint();
        // アカウントを選んだら、続けて場所も選べるように（場所を選んだら終わり）
        if (r >= 100 && r < 200) safe->showPlaceMenu (row);
    });
}

void MacroEditor::toggleLearning()
{
    if (learning) { stopLearning(); return; }
    if (! host.learn) return;
    learning = true;
    addKey.setButtonText (jp ("覚えさせるのをやめる"));
    addKey.setColour (juce::TextButton::buttonColourId, learnCol);
    addKey.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    juce::Component::SafePointer<MacroEditor> safe (this);
    host.learn ([safe] (int code, int mods)
    {
        if (safe == nullptr) return;
        if (code == 53 && mods == 0) { safe->stopLearning(); return; }   // Esc で終わり
        Step s; s.kind = "key"; s.keyCode = code; s.mods = mods;
        safe->addStep (s);
    });
    repaint();
}

void MacroEditor::stopLearning()
{
    if (! learning) return;
    learning = false;
    if (host.learn) host.learn (nullptr);
    addKey.setButtonText (jp ("+ キーを覚えさせる"));
    addKey.setColour (juce::TextButton::buttonColourId, card);
    addKey.setColour (juce::TextButton::textColourOffId, text);
    repaint();
}

void MacroEditor::askNumber (const juce::String& title, int value, std::function<void (int)> done)
{
    input = std::make_unique<juce::AlertWindow> (title, juce::String(), juce::MessageBoxIconType::NoIcon);
    input->addTextEditor ("v", juce::String (value));
    input->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    input->addButton (jp ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MacroEditor> safe (this);
    input->enterModalState (true, juce::ModalCallbackFunction::create ([safe, done] (int r)
    {
        if (safe == nullptr || safe->input == nullptr) return;
        const int v = safe->input->getTextEditorContents ("v").getIntValue();
        safe->input->setVisible (false);
        if (r == 1 && done) done (v);
    }), false);
}

void MacroEditor::askLink (const juce::String& url, const juce::String& label, std::function<void (juce::String, juce::String)> done)
{
    input = std::make_unique<juce::AlertWindow> (jp ("ページ"), juce::String(), juce::MessageBoxIconType::NoIcon);
    input->addTextEditor ("u", url, jp ("アドレス（URL）"));
    input->addTextEditor ("l", label, jp ("名前（一覧に出る）"));
    input->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    input->addButton (jp ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MacroEditor> safe (this);
    input->enterModalState (true, juce::ModalCallbackFunction::create ([safe, done] (int r)
    {
        if (safe == nullptr || safe->input == nullptr) return;
        auto u = safe->input->getTextEditorContents ("u").trim();
        const auto l = safe->input->getTextEditorContents ("l").trim();
        safe->input->setVisible (false);
        if (r != 1 || u.isEmpty() || u == "https://") return;
        if (! u.containsChar (':')) u = "https://" + u;
        if (done) done (u, l);
    }), false);
}
