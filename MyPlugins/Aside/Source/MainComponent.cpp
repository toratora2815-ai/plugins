#include "MainComponent.h"
#include "AsideMac.h"

namespace
{
const juce::Colour bg (0xff101214), panel (0xff191c20), panel2 (0xff22262c), line (0xff2e333a),
                   textC (0xffecedef), dim (0xff8f96a0), accent (0xffb59cff), green (0xff6ee7a8), amber (0xffffc24b), red (0xffff6b6b);

constexpr int W = 640, H = 560, rowH = 58;

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void styleButton (juce::TextButton& b, bool primary = false)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? accent : panel2);
    b.setColour (juce::TextButton::textColourOffId, primary ? juce::Colour (0xff1b1036) : textC);
    b.setColour (juce::ComboBox::outlineColourId, line);
}

juce::File dataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Aside");
}
} // namespace

//==============================================================================
// 入れたことを右上に数秒だけ出す（取り消しボタンつき）
class MainComponent::Hud : public juce::Component, private juce::Timer
{
public:
    explicit Hud (MainComponent& o) : owner (o) { setSize (360, 74); }

    // 一言メモで聞いている間: 「どうぞ」と聞こえている文字
    void showListening()
    {
        index = -2;
        place();
        stopTimer();
    }

    void show (int entryIndex)
    {
        index = entryIndex;
        place();
        startTimer (7000);
    }

    void place()
    {
        if (! isOnDesktop())
        {
            addToDesktop (juce::ComponentPeer::windowHasDropShadow);
            if (auto* p = getPeer()) aside::mac::makeHud (p->getNativeHandle(), false);
        }
        int x = 0, y = 0;
        aside::mac::hudPosition (getWidth(), x, y);
        setTopLeftPosition (x, y);
        setVisible (true);
        if (auto* p = getPeer()) aside::mac::makeHud (p->getNativeHandle(), false);
        repaint();
    }

    bool listeningShown() const { return isVisible() && index == -2; }
    void hideNow() { stopTimer(); setVisible (false); }

    void paint (juce::Graphics& g) override
    {
        if (index == -2)
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (panel.withAlpha (0.97f));
            g.fillRoundedRectangle (r, 12);
            g.setColour (amber.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 12, 1.5f);
            auto b = getLocalBounds().reduced (14, 10);
            g.setColour (amber);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String::fromUTF8 ("● 聞いています（言い終わったら自動で閉じます）"), b.removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (textC);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawFittedText (owner.partial.isNotEmpty() ? juce::String::fromUTF8 ("「") + owner.partial + juce::String::fromUTF8 ("」")
                                                         : juce::String::fromUTF8 ("どうぞ。やることを一言"),
                              b, juce::Justification::centredLeft, 2);
            return;
        }
        if (! juce::isPositiveAndBelow (index, owner.entries.size())) return;
        const auto& e = owner.entries.getReference (index);
        auto r = getLocalBounds().toFloat();
        g.setColour (panel.withAlpha (0.97f));
        g.fillRoundedRectangle (r, 12);
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12, 1);
        auto b = getLocalBounds().reduced (14, 10);
        undoBtn = b.removeFromRight (74).withSizeKeepingCentre (70, 28);
        g.setColour (e.status == 2 ? dim : textC);
        g.setFont (juce::FontOptions (11.0f));
        const auto where = e.event ? u8 ("カレンダーに入れました") : u8 ("リマインダーに入れました");
        g.setColour (e.status == 3 ? red : e.status == 2 ? dim : green);
        g.drawText (e.status == 2 ? u8 ("取り消しました") : e.status == 3 ? u8 ("入れられませんでした") : e.status == 0 ? u8 ("入れています…") : where,
                    b.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (textC);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (e.title, b.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (e.dueText.isNotEmpty() ? e.dueText : u8 ("日時なし"), b, juce::Justification::centredLeft, true);
        if (e.status == 1)
        {
            g.setColour (panel2);
            g.fillRoundedRectangle (undoBtn.toFloat(), 6);
            g.setColour (textC);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (u8 ("取り消し"), undoBtn, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (index == -2) { owner.command ("cancel"); return; }
        if (undoBtn.contains (e.getPosition())) { owner.undo (index); repaint(); startTimer (1500); }
        else { setVisible (false); if (owner.onShowRequest) owner.onShowRequest(); }
    }

private:
    MainComponent& owner;
    int index = -1;
    juce::Rectangle<int> undoBtn;
    void timerCallback() override { stopTimer(); setVisible (false); }
};

//==============================================================================
MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Aside"; po.folderName = "Aside"; po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);
    hud = std::make_unique<Hud> (*this);

    onBtn.onClick = [this]
    {
        if (! alwaysListen()) { memoNext(); return; }
        props->setValue ("on", ! wantOn());
        setListening (wantOn());
    };
    styleButton (onBtn, true);
    settingsBtn.setButtonText (u8 ("設定"));
    styleButton (settingsBtn);
    settingsBtn.onClick = [this] { showSettings(); };
    remindersBtn.setButtonText (u8 ("リマインダーを開く"));
    styleButton (remindersBtn);
    remindersBtn.onClick = [] { aside::mac::openReminders(); };
    for (auto* b : { &onBtn, &settingsBtn, &remindersBtn }) addAndMakeVisible (b);

    setSize (W, H);
    if (live)
    {
        load();
        registerKey();
        aside::mac::listenCommands ([this] (const std::string& c) { command (js (c)); });
        setListening (wantOn());
        startTimerHz (10);
    }
    onBtn.setButtonText (! alwaysListen() ? u8 ("一言メモ") : wantOn() ? u8 ("聞くのを止める") : u8 ("聞く"));
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (live) { aside::mac::stopListening(); save(); }
    props->saveIfNeeded();
}

//==============================================================================
void MainComponent::startEngine()
{
    aside::mac::startListening (
        [this] (const std::string& text, bool fin)
        {
            if (! fin)
            {
                partial = js (text);
                // 話している間は待つ
                if (forceNext) forceUntil = std::max (forceUntil, juce::Time::getMillisecondCounterHiRes() + 3000.0);
                repaint (live_);
                if (hud->listeningShown()) hud->repaint();
                return;
            }
            partial = {};
            const bool forced = forceNext && juce::Time::getMillisecondCounterHiRes() < forceUntil;
            if (oneShot) { oneShot = false; forceNext = false; hud->hideNow(); aside::mac::stopListening(); state = "off"; updateMenu(); }
            heard (js (text), forced || everything());
            if (forced) { forceNext = false; updateIcon(); }
            repaint();
        },
        [this] (float l) { level = std::max (l, level * 0.8f); },
        [this] (const std::string& s) { state = js (s); updateIcon(); updateMenu(); repaint(); });
}

void MainComponent::setListening (bool on)
{
    oneShot = false;
    onBtn.setButtonText (! alwaysListen() ? u8 ("一言メモ") : on ? u8 ("聞くのを止める") : u8 ("聞く"));
    if (on) { pausedForApp = false; startEngine(); }
    else { aside::mac::stopListening(); state = "off"; }
    updateIcon();
    updateMenu();
    repaint();
}

void MainComponent::updateIcon()
{
    if (! live) return;
    const bool on = aside::mac::listening();
    if (! alwaysListen()) { aside::mac::setStatusIcon (on ? 3 : 1); return; }   // ボタンのときだけ聞く形
    aside::mac::setStatusIcon (! on ? 0 : forceNext ? 3 : level > 0.35f ? 2 : 1);
}

void MainComponent::registerKey()
{
    aside::mac::registerHotKey (props->getIntValue ("key", 0), [this] { memoNext(); });
}

void MainComponent::command (const juce::String& c)
{
    if (c == "memo") memoNext();
    else if (c == "undo") undo (0);
    else if (c == "show") { if (onShowRequest) onShowRequest(); }
    else if (c == "cancel" && oneShot) { oneShot = false; forceNext = false; aside::mac::stopListening(); state = "off"; hud->hideNow(); updateIcon(); updateMenu(); repaint(); }
}

void MainComponent::memoNext()
{
    if (oneShot) { command ("cancel"); return; }   // もう一度押したらやめる
    if (! aside::mac::listening())
    {
        if (alwaysListen()) { props->setValue ("on", true); setListening (true); }
        else { oneShot = true; partial = {}; startEngine(); hud->showListening(); }
    }
    forceNext = true;
    forceUntil = juce::Time::getMillisecondCounterHiRes() + 12000.0;
    updateIcon();
    repaint();
}

void MainComponent::updateMenu()
{
    if (! live) return;
    const auto& k = aside::mac::hotKeyChoices();
    const int ki = props->getIntValue ("key", 0);
    const auto keyName = juce::isPositiveAndBelow (ki, (int) k.size()) && k[(size_t) ki].keyCode >= 0 ? juce::String ("   ") + u8 (k[(size_t) ki].label) : juce::String();
    std::vector<std::pair<std::string, int>> items {
        { stateText().toStdString(), -1 },
        { "", 0 },
        { (u8 ("一言メモ（押してから話す）") + keyName).toStdString(), 2 } };
    if (alwaysListen()) items.push_back ({ (wantOn() ? u8 ("聞くのを止める") : u8 ("聞く")).toStdString(), 1 });
    if (! entries.isEmpty() && entries.getReference (0).status == 1)
        items.push_back ({ (u8 ("取り消す: ") + entries.getReference (0).title).toStdString(), 3 });
    items.push_back ({ "", 0 });
    items.push_back ({ u8 ("聞き取った一覧と設定…").toStdString(), 4 });
    items.push_back ({ u8 ("リマインダーを開く").toStdString(), 5 });
    items.push_back ({ "", 0 });
    items.push_back ({ u8 ("Aside を終了").toStdString(), 9 });
    aside::mac::setStatusMenu (items, [this] (int t)
    {
        if (t == 1) { props->setValue ("on", ! wantOn()); setListening (wantOn()); }
        else if (t == 2) memoNext();
        else if (t == 3) undo (0);
        else if (t == 4) { if (onShowRequest) onShowRequest(); }
        else if (t == 5) aside::mac::openReminders();
        else if (t == 9) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });
}

juce::String MainComponent::stateText() const
{
    if (oneShot && state == "listening") return u8 ("一言メモ: 聞いています");
    if (! alwaysListen() && (state == "off" || state == "listening")) return u8 ("一言メモ・Num Deck のボタンを押したときだけ聞きます（ふだんマイクは閉じています）");
    if (pausedForApp) return u8 ("Logic が前にあるので休んでいます");
    if (state == "listening") return u8 ("聞いています（Mac の中だけで文字にしています）");
    if (state == "denied-mic") return u8 ("マイクの許可がありません");
    if (state == "denied-speech") return u8 ("音声認識の許可がありません");
    if (state == "no-ondevice") return u8 ("この Mac では、Mac の中だけで文字にする音声認識が使えません");
    if (state.startsWith ("error:")) return u8 ("マイクを開けませんでした: ") + state.fromFirstOccurrenceOf (":", false, false);
    return u8 ("止めています");
}

//==============================================================================
void MainComponent::heard (const juce::String& text, bool forced)
{
    const auto now = aside::nowLocal();
    auto t = aside::parse (text.toStdString(), now);
    if (! t.ok && forced) t = aside::parse ((text + u8 ("、メモして")).toStdString(), now);
    if (! t.ok) return;

    Entry e;
    e.when = juce::Time::getCurrentTime();
    e.heardText = text;
    e.title = js (t.title);
    e.due = t.due;
    e.dueText = js (aside::dueText (t.due, now));
    e.event = t.isEvent;
    entries.insert (0, e);
    while (entries.size() > 200) entries.removeLast();
    repaint();
    if (! live) return;

    aside::mac::Item it;
    it.title = t.title;
    it.notes = (u8 ("ひとりごと:「") + text + u8 ("」（Aside が ") + e.when.formatted ("%m/%d %H:%M") + u8 (" に聞き取り）")).toStdString();
    it.hasDate = t.due.hasDate; it.hasTime = t.due.hasTime;
    it.y = t.due.y; it.m = t.due.m; it.d = t.due.d; it.hh = t.due.hh; it.mm = t.due.mm;
    it.event = t.isEvent;
    it.listName = listName().toStdString();
    const auto when = e.when;
    aside::mac::addItem (it, [this, when] (const std::string& id, const std::string& err)
    {
        for (int i = 0; i < entries.size(); ++i)
        {
            auto& x = entries.getReference (i);
            if (x.when != when) continue;
            x.id = js (id);
            x.status = err.empty() ? 1 : 3;
            if (err == "reminders-denied") x.error = u8 ("リマインダーの許可がありません（システム設定 → プライバシーとセキュリティ → リマインダー）");
            else if (err == "calendar-denied") x.error = u8 ("カレンダーの許可がありません（システム設定 → プライバシーとセキュリティ → カレンダー）");
            else x.error = js (err);
            hud->show (i);
            break;
        }
        save();
        updateMenu();
        repaint();
    });
    hud->show (0);
}

void MainComponent::undo (int i)
{
    if (! juce::isPositiveAndBelow (i, entries.size())) return;
    auto& e = entries.getReference (i);
    if (e.status != 1) return;
    aside::mac::removeItem (e.id.toStdString(), e.event);
    e.status = 2;
    save();
    updateMenu();
    repaint();
}

void MainComponent::timerCallback()
{
    // Logic が前にある間は聞かない（歌や録音の邪魔をしないため）
    // 一言メモ: 話し始めないまま 12 秒たったら閉じる
    if (oneShot && juce::Time::getMillisecondCounterHiRes() > forceUntil) command ("cancel");

    if (wantOn() && pauseInLogic())
    {
        const bool logicFront = aside::mac::frontBundle() == "com.apple.logic10";
        if (logicFront && aside::mac::listening()) { aside::mac::stopListening(); pausedForApp = true; updateMenu(); repaint(); }
        else if (! logicFront && pausedForApp) { pausedForApp = false; startEngine(); }
    }
    if (forceNext && juce::Time::getMillisecondCounterHiRes() > forceUntil) { forceNext = false; repaint(); }
    level *= 0.85f;
    static int n = 0;
    if (++n % 3 == 0) updateIcon();
    if (isShowing()) repaint (live_);
}

//==============================================================================
void MainComponent::showSettings()
{
    juce::PopupMenu m, keys;
    m.addItem (6, u8 ("いつも聞く（ボタンを押さなくても、きっかけの言葉で入れる）"), true, alwaysListen());
    m.addItem (1, u8 ("いつも聞くとき: Logic が前にあるときは聞かない"), alwaysListen(), pauseInLogic());
    m.addItem (2, u8 ("きっかけの言葉がなくても、聞こえた文を全部メモする"), true, everything());
    const auto& k = aside::mac::hotKeyChoices();
    for (int i = 0; i < (int) k.size(); ++i) keys.addItem (100 + i, u8 (k[(size_t) i].label), true, props->getIntValue ("key", 0) == i);
    m.addSubMenu (u8 ("キー: 一言メモ"), keys);
    m.addItem (3, u8 ("リマインダーのリスト名: ") + listName() + u8 ("…"));
    m.addItem (4, u8 ("ログインしたら起動"), true, aside::mac::launchAtLogin());
    m.addSeparator();
    m.addItem (5, u8 ("聞き取った一覧を消す"), ! entries.isEmpty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsBtn), [this] (int r)
    {
        if (r == 1) props->setValue ("pauseLogic", ! pauseInLogic());
        else if (r == 2) props->setValue ("everything", ! everything());
        else if (r == 3)
        {
            auto* aw = new juce::AlertWindow (u8 ("リマインダーのリスト名"), u8 ("やることを入れるリスト（なければ作ります）"), juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", listName());
            aw->addButton ("OK", 1);
            aw->addButton (u8 ("やめる"), 0);
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
            {
                const auto n = aw->getTextEditorContents ("name").trim();
                if (res == 1 && n.isNotEmpty()) { props->setValue ("list", n); repaint(); }
            }), true);
        }
        else if (r == 4) aside::mac::setLaunchAtLogin (! aside::mac::launchAtLogin());
        else if (r == 5) { entries.clear(); save(); updateMenu(); repaint(); }
        else if (r == 6) { props->setValue ("always", ! alwaysListen()); props->setValue ("on", true); setListening (wantOn()); }
        else if (r >= 100) { props->setValue ("key", r - 100); registerKey(); updateMenu(); }
        props->saveIfNeeded();
    });
}

void MainComponent::save()
{
    juce::Array<juce::var> a;
    for (auto& e : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("when", e.when.toMilliseconds());
        o->setProperty ("heard", e.heardText);
        o->setProperty ("title", e.title);
        o->setProperty ("due", e.dueText);
        o->setProperty ("event", e.event);
        o->setProperty ("id", e.id);
        o->setProperty ("status", e.status);
        o->setProperty ("error", e.error);
        a.add (o);
    }
    dataDir().createDirectory();
    dataDir().getChildFile ("history.json").replaceWithText (juce::JSON::toString (a));
}

void MainComponent::load()
{
    const auto v = juce::JSON::parse (dataDir().getChildFile ("history.json"));
    if (auto* a = v.getArray())
        for (auto& x : *a)
        {
            Entry e;
            e.when = juce::Time ((juce::int64) x["when"]);
            e.heardText = x["heard"].toString();
            e.title = x["title"].toString();
            e.dueText = x["due"].toString();
            e.event = (bool) x["event"];
            e.id = x["id"].toString();
            e.status = (int) x["status"];
            e.error = x["error"].toString();
            if (e.status == 0) e.status = 3;
            entries.add (e);
        }
}

//==============================================================================
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    for (auto& [r, i] : undoAreas) if (r.contains (e.getPosition())) { undo (i); return; }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (auto& [r, i] : undoAreas) if (r.contains (e.getPosition())) h = i;
    if (h != hoverUndo) { hoverUndo = h; repaint (listArea); }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (64);
    {
        auto h = header.reduced (18, 16);
        settingsBtn.setBounds (h.removeFromRight (64));
        h.removeFromRight (8);
        remindersBtn.setBounds (h.removeFromRight (140));
        h.removeFromRight (8);
        onBtn.setBounds (h.removeFromRight (120));
    }
    live_ = r.removeFromTop (96).reduced (18, 6);
    listArea = r.reduced (18, 8);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    undoAreas.clear();

    {
        auto h = header.reduced (18, 0);
        g.setColour (textC);
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        g.drawText ("Aside", h.removeFromLeft (76), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (u8 ("ひとりごと秘書"), h.removeFromLeft (100), juce::Justification::centredLeft);
    }

    // いま
    {
        g.setColour (panel);
        g.fillRoundedRectangle (live_.toFloat(), 10);
        auto a = live_.reduced (14, 10);
        const bool on = live ? aside::mac::listening() : true;
        auto top = a.removeFromTop (20);
        g.setColour (on ? (forceNext ? amber : green) : dim);
        g.fillEllipse (top.removeFromLeft (12).withSizeKeepingCentre (9, 9).toFloat());
        top.removeFromLeft (6);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (forceNext ? u8 ("一言メモ: どうぞ") : (live ? stateText() : u8 ("聞いています（Mac の中だけで文字にしています）")),
                    top, juce::Justification::centredLeft, true);
        // 声のメーター
        auto meter = a.removeFromRight (8);
        g.setColour (panel2);
        g.fillRoundedRectangle (meter.toFloat(), 3);
        const float lv = juce::jlimit (0.0f, 1.0f, level);
        g.setColour (accent);
        g.fillRoundedRectangle (meter.toFloat().removeFromBottom (meter.getHeight() * lv), 3);
        a.removeFromRight (10);
        a.removeFromTop (6);
        g.setFont (juce::FontOptions (14.0f));
        if (partial.isNotEmpty()) { g.setColour (textC); g.drawFittedText (u8 ("「") + partial + u8 ("」"), a, juce::Justification::topLeft, 2); }
        else
        {
            g.setColour (dim);
            g.drawFittedText (alwaysListen()
                                  ? u8 ("「あとで請求書出さなきゃ」「明日の3時までに見積もり送らないと」のように、"
                                        "つぶやくだけでリマインダーに入ります。きっかけの言葉がないおしゃべりは何もしません。")
                                  : u8 ("Num Deck の「一言メモ」ボタン（または ⌃⌥M）を押してから、「明日の3時までに見積もり送る」のように一言。"
                                        "言い終わるとリマインダーに入り、マイクは閉じます。"),
                              a, juce::Justification::topLeft, 3);
        }
    }

    // 一覧
    auto l = listArea;
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (u8 ("入れたもの（新しい順）  ・  リスト「") + listName() + u8 ("」"), l.removeFromTop (20), juce::Justification::centredLeft);
    if (entries.isEmpty())
    {
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (u8 ("まだありません。\n\n"
                              "きっかけの言葉: あとで / 〜しなきゃ / 〜しないと / 忘れないように / メモして / リマインド など\n"
                              "日時もわかります: 今日・明日・明後日・金曜・来週の月曜・20日・11月20日・3時・夕方・30分後\n"
                              "打ち合わせ・会議・予約などで時刻があれば、カレンダーの予定になります"),
                          l.removeFromTop (130), juce::Justification::topLeft, 8);
        return;
    }
    for (int i = 0; i < entries.size() && l.getHeight() >= rowH; ++i)
    {
        const auto& e = entries.getReference (i);
        auto row = l.removeFromTop (rowH);
        g.setColour (i % 2 ? bg : panel.withAlpha (0.6f));
        g.fillRect (row);
        auto r = row.reduced (10, 6);
        if (e.status == 1)
        {
            auto u = r.removeFromRight (74).withSizeKeepingCentre (70, 26);
            undoAreas.push_back ({ u, i });
            g.setColour (hoverUndo == i ? line.brighter (0.2f) : panel2);
            g.fillRoundedRectangle (u.toFloat(), 6);
            g.setColour (textC);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (u8 ("取り消し"), u, juce::Justification::centred);
        }
        auto top = r.removeFromTop (22);
        g.setColour (e.status == 2 ? dim : textC);
        g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        const auto badge = e.event ? u8 ("予定") : u8 ("やること");
        g.drawText (e.title, top.removeFromLeft (juce::jmin (top.getWidth() - 170, (int) juce::GlyphArrangement::getStringWidth (juce::FontOptions (14.5f, juce::Font::bold), e.title) + 10)),
                    juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (e.status == 3 ? red : e.status == 2 ? dim : e.event ? amber : green);
        g.drawText ((e.status == 2 ? u8 ("取り消し済み") : e.status == 3 ? u8 ("失敗") : badge) + (e.dueText.isNotEmpty() ? u8 ("  ") + e.dueText : juce::String()),
                    top, juce::Justification::centredLeft, true);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (e.when.formatted ("%m/%d %H:%M") + u8 ("  「") + e.heardText + u8 ("」") + (e.error.isNotEmpty() ? u8 ("  ") + e.error : juce::String()),
                    r, juce::Justification::centredLeft, true);
    }
}

void MainComponent::addDemo()
{
    for (auto* s : { "金曜の夜8時に田中さんと打ち合わせ、忘れないように", "明日の3時までに見積もり送らないと",
                     "今日は天気がいいな", "あ、あとで請求書出さなきゃ" })
        heard (u8 (s), false);
    for (auto& e : entries) e.status = 1;
    entries.getReference (2).status = 2;
    partial = u8 ("そういえば牛乳買わ");
}
