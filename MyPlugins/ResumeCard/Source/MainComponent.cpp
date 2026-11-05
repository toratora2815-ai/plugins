#include "MainComponent.h"

namespace
{
const juce::Colour bg (0xff0f1115), panel (0xff181b22), panel2 (0xff20242d), line (0xff2c313c),
                   textC (0xffeceef2), dim (0xff8d94a3), accent (0xffffb35c), blue (0xff6fb6ff), green (0xff6ee7a8);

constexpr int W = 900, H = 600;
constexpr double rollingEvery = 90.0;    // 使っている間、この秒ごとに今の状態を取る
constexpr double activeIdle = 45.0;      // 最後の操作からこれ以内なら「使っている」

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void styleButton (juce::TextButton& b, bool primary = false)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? accent : panel2);
    b.setColour (juce::TextButton::textColourOffId, primary ? juce::Colour (0xff2a1700) : textC);
    b.setColour (juce::ComboBox::outlineColourId, line);
}

juce::String ago (juce::Time t)
{
    const double m = (juce::Time::getCurrentTime() - t).inMinutes();
    if (m < 1) return u8 ("たった今");
    if (m < 60) return juce::String ((int) m) + u8 (" 分前");
    if (m < 60 * 24) return juce::String ((int) (m / 60)) + u8 (" 時間 ") + juce::String ((int) m % 60) + u8 (" 分前");
    return juce::String ((int) (m / 60 / 24)) + u8 (" 日前");
}

juce::String home (juce::String p)
{
    return p.replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~");
}

// 書いていた所は、最後の 2 行・160 文字まで
juce::String tail (const juce::String& s)
{
    auto t = s.trim();
    juce::StringArray lines;
    lines.addLines (t);
    lines.removeEmptyStrings();
    while (lines.size() > 2) lines.remove (0);
    t = lines.joinIntoString ("\n");
    if (t.length() > 160) t = u8 ("…") + t.substring (t.length() - 160);
    return t;
}
} // namespace

//==============================================================================
MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Resume Card"; po.folderName = "Resume Card"; po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);

    reopenBtn.setButtonText (u8 ("全部開き直す"));
    styleButton (reopenBtn, true);
    reopenBtn.onClick = [this]
    {
        if (auto* c = current())
        {
            std::vector<rc::AppInfo> apps { c->cap.front };
            for (auto& a : c->cap.others) apps.push_back (a);
            rc::reopen (apps, c->cap.tabs, c->cap.finderPath, c->cap.front.bundleId);
            if (onHideRequest) onHideRequest();
        }
    };
    frontBtn.setButtonText (u8 ("前のアプリだけ"));
    styleButton (frontBtn);
    frontBtn.onClick = [this]
    {
        if (auto* c = current())
        {
            rc::reopen ({ c->cap.front }, {}, {}, c->cap.front.bundleId);
            if (onHideRequest) onHideRequest();
        }
    };
    closeBtn.setButtonText (u8 ("閉じる"));
    styleButton (closeBtn);
    closeBtn.onClick = [this] { if (onHideRequest) onHideRequest(); };
    settingsBtn.setButtonText (u8 ("設定"));
    styleButton (settingsBtn);
    settingsBtn.onClick = [this] { showSettings(); };
    permBtn.setButtonText (u8 ("許可する"));
    styleButton (permBtn, true);
    permBtn.onClick = [this] { rc::accessibilityTrusted (true); };
    deleteBtn.setButtonText (u8 ("このカードを消す"));
    styleButton (deleteBtn);
    deleteBtn.onClick = [this]
    {
        if (auto* c = current()) { CardStore::remove (*c); welcome = false; reload(); }
    };
    for (auto* b : { &reopenBtn, &frontBtn, &closeBtn, &settingsBtn, &permBtn, &deleteBtn }) addAndMakeVisible (b);

    noteBox.setMultiLine (true, true);
    noteBox.setReturnKeyStartsNewLine (true);
    noteBox.setTextToShowWhenEmpty (u8 ("次の自分へのメモ（例: 見積もりの 3 行目から。金額は田中さんに確認）"), dim);
    noteBox.setColour (juce::TextEditor::backgroundColourId, panel2);
    noteBox.setColour (juce::TextEditor::outlineColourId, line);
    noteBox.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    noteBox.setColour (juce::TextEditor::textColourId, textC);
    noteBox.setFont (juce::FontOptions (13.0f));
    noteBox.onTextChange = [this]
    {
        if (auto* c = current()) { c->note = noteBox.getText(); c->save(); }
    };
    addAndMakeVisible (noteBox);

    setSize (W, H);
    reload();

    if (live)
    {
        Card::root().createDirectory();
        rc::watchAwayEvents ([this] (int kind, const std::string& name)
        {
            const juce::String n (name);
            if (kind == 1)
                goAway (n.containsIgnoreCase ("lock") || n.containsIgnoreCase ("Session") ? "lock"
                        : n.containsIgnoreCase ("screensaver") ? "screensaver" : "sleep");
            else comeBack();
        });
        registerKeys();
        updateMenu();
        rc::setStatusIcon (! autoOn());
        startTimer (5000);
        takeRolling();
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    props->saveIfNeeded();
}

//==============================================================================
std::vector<std::string> MainComponent::skipList() const
{
    // 最初から取らないアプリ（パスワードを扱うもの）
    juce::StringArray s;
    s.addLines (props->getValue ("skip", "com.1password\ncom.agilebits\ncom.bitwarden\ncom.apple.keychainaccess\n"
                                         "com.lastpass\ncom.dashlane\ncom.apple.Passwords"));
    s.removeEmptyStrings();
    std::vector<std::string> out;
    for (auto& x : s) out.push_back (x.trim().toStdString());
    return out;
}

void MainComponent::registerKeys()
{
    rc::registerHotKey (0, props->getIntValue ("keyPause", 0), [this] { pauseNow(); });
    rc::registerHotKey (1, props->getIntValue ("keyShow", 1), [this] { showLatest (true); });
}

void MainComponent::updateMenu()
{
    const auto& k = rc::hotKeyChoices();
    auto keyName = [&] (const char* id, int def)
    {
        const int i = props->getIntValue (id, def);
        return juce::isPositiveAndBelow (i, (int) k.size()) && k[(size_t) i].keyCode >= 0 ? juce::String ("   ") + u8 (k[(size_t) i].label) : juce::String();
    };
    std::vector<std::pair<std::string, int>> items {
        { (u8 ("最後のカードを見る") + keyName ("keyShow", 1)).toStdString(), 1 },
        { (u8 ("いま中断する（メモつき）") + keyName ("keyPause", 0)).toStdString(), 2 },
        { "", 0 },
        { (autoOn() ? u8 ("✓ 離れたら自動でカードにする") : u8 ("   離れたら自動でカードにする")).toStdString(), 3 },
        { u8 ("カードの一覧と設定…").toStdString(), 4 },
        { "", 0 },
        { u8 ("Resume Card を終了").toStdString(), 9 } };
    rc::setStatusMenu (items, [this] (int t)
    {
        if (t == 1) showLatest (true);
        else if (t == 2) pauseNow();
        else if (t == 3) { props->setValue ("auto", ! autoOn()); rc::setStatusIcon (! autoOn()); updateMenu(); }
        else if (t == 4) { welcome = false; reload (current() ? current()->id : juce::String()); if (onShowRequest) onShowRequest (true); }
        else if (t == 9) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });
}

//==============================================================================
void MainComponent::timerCallback()
{
    const double idle = rc::idleSeconds();
    const auto now = juce::Time::getCurrentTime();

    // 使っている間は、ときどき今の状態を取っておく（離れた瞬間は画面ロックなどで取れないことがあるため）
    if (autoOn() && ! away && idle < activeIdle && (now - rollingAt).inSeconds() >= rollingEvery)
        takeRolling();

    // しばらく操作がなければ「離れた」
    if (autoOn() && ! away && idle >= idleMinutes() * 60.0) goAway ("idle");
    // 操作が戻ったら「戻った」
    if (away && idle < 3.0) comeBack();

    permBtn.setVisible (! rc::accessibilityTrusted (false));
    if (permBtn.isVisible() != ! banner.isEmpty()) resized();
    if (isShowing()) repaint (listArea);   // 「〜分前」を進める
}

void MainComponent::takeRolling()
{
    if (capturing) return;
    capturing = true;
    const auto file = rollingShotFile();
    const bool wantShot = shotsOn() && rc::screenTrusted();
    rc::capture (wantShot ? file.getFullPathName().toStdString() : std::string(), textOn(), skipList(),
                 [this, wantShot] (rc::Capture c)
    {
        capturing = false;
        if (c.front.bundleId.empty() && c.tabs.empty()) return;
        rolling = c;
        rollingAt = juce::Time::getCurrentTime() - juce::RelativeTime::seconds (c.idleAtCapture);
        rollingShot = wantShot && c.shot;
    });
}

void MainComponent::goAway (const juce::String& why)
{
    if (away) return;
    away = true;
    awayWhy = why;
    // 離れた時刻（操作なしのときは、最後に操作した時刻）
    awayStart = juce::Time::getCurrentTime() - juce::RelativeTime::seconds (why == "idle" ? rc::idleSeconds() : 0.0);

    // 使っている間に取ったものが新しければそれを使う
    if (rollingAt.toMilliseconds() > 0 && (awayStart - rollingAt).inMinutes() < 4.0)
        makeCard (rolling, rollingShot, why, rollingAt, false, false);
    else if (why == "idle")
    {
        // 画面はまだそのまま見えているので、今取る
        rc::capture (shotsOn() && rc::screenTrusted() ? rollingShotFile().getFullPathName().toStdString() : std::string(), textOn(), skipList(),
                     [this, why] (rc::Capture c) { makeCard (c, c.shot, why, awayStart, false, false); });
    }
}

void MainComponent::comeBack()
{
    if (! away) return;
    away = false;
    const double mins = (juce::Time::getCurrentTime() - awayStart).inMinutes();
    rollingAt = {};   // 戻ったらすぐ取り直す
    if (mins >= showAfterMinutes() && cards.size() > 0 && cards[0].leftAt == awayStart)
    {
        awayMinutes = mins;
        welcome = true;
        reload (cards[0].id);
        if (onShowRequest) onShowRequest (false);
    }
}

void MainComponent::makeCard (const rc::Capture& c, bool shotOk, const juce::String& why, juce::Time when, bool showNow, bool focusNote)
{
    if (c.front.bundleId.empty() && c.tabs.empty() && c.others.empty()) return;
    Card card;
    card.when = when;
    card.leftAt = why == "manual" ? juce::Time::getCurrentTime() : awayStart;
    card.id = Card::newId (card.leftAt);
    card.why = why;
    card.cap = c;
    Card::cardsDir().createDirectory();
    if (shotOk && rollingShotFile().existsAsFile())
        card.hasShot = rollingShotFile().copyFileTo (card.shotFile());
    card.save();
    reload (card.id);
    if (showNow)
    {
        welcome = false;
        if (onShowRequest) onShowRequest (true);
        if (focusNote) noteBox.grabKeyboardFocus();
    }
}

void MainComponent::pauseNow()
{
    // いまの状態をすぐ取って、メモを書けるように開く
    const bool wantShot = shotsOn() && rc::screenTrusted();
    rc::capture (wantShot ? rollingShotFile().getFullPathName().toStdString() : std::string(), textOn(), skipList(),
                 [this, wantShot] (rc::Capture c) { makeCard (c, wantShot && c.shot, "manual", juce::Time::getCurrentTime(), true, true); });
}

void MainComponent::showLatest (bool focus)
{
    welcome = false;
    reload();
    if (onShowRequest) onShowRequest (focus);
}

//==============================================================================
void MainComponent::reload (const juce::String& selectId)
{
    cards = CardStore::loadAll();
    int s = 0;
    for (int i = 0; i < cards.size(); ++i) if (cards[i].id == selectId) s = i;
    selected = -1;
    select (cards.isEmpty() ? -1 : s);
}

void MainComponent::select (int i)
{
    selected = i;
    if (i != 0) welcome = false;
    shot = {};
    if (auto* c = current())
    {
        if (c->hasShot) shot = juce::ImageFileFormat::loadFrom (c->shotFile());
        noteBox.setText (c->note, false);
        noteBox.setEnabled (true);
    }
    else { noteBox.setText ({}, false); noteBox.setEnabled (false); }
    for (auto* b : { &reopenBtn, &frontBtn, &deleteBtn }) b->setEnabled (current() != nullptr);
    repaint();
}

void MainComponent::openLink (const Link& l)
{
    auto* c = current();
    if (c == nullptr) return;
    if (l.kind == 0) rc::reopen ({ c->cap.front }, {}, {}, c->cap.front.bundleId);
    else if (l.kind == 1 && l.index < (int) c->cap.tabs.size()) rc::reopen ({}, { c->cap.tabs[(size_t) l.index] }, {}, c->cap.tabs[(size_t) l.index].bundleId);
    else if (l.kind == 2 && l.index < (int) c->cap.others.size()) rc::reopen ({ c->cap.others[(size_t) l.index] }, {}, {}, c->cap.others[(size_t) l.index].bundleId);
    else if (l.kind == 3) rc::reopen ({}, {}, c->cap.finderPath, "com.apple.finder");
    else if (l.kind == 4 && c->hasShot) rc::openFile (c->shotFile().getFullPathName().toStdString());
}

void MainComponent::showSettings()
{
    juce::PopupMenu m;
    m.addItem (1, u8 ("離れたら自動でカードにする"), true, autoOn());
    juce::PopupMenu idle, show, keyP, keyS;
    for (int v : { 2, 3, 5, 10, 15, 30 }) idle.addItem (100 + v, juce::String (v) + u8 (" 分"), true, idleMinutes() == v);
    for (int v : { 1, 3, 5, 10, 30, 60 }) show.addItem (200 + v, juce::String (v) + u8 (" 分以上"), true, showAfterMinutes() == v);
    const auto& k = rc::hotKeyChoices();
    for (int i = 0; i < (int) k.size(); ++i)
    {
        keyP.addItem (300 + i, u8 (k[(size_t) i].label), true, props->getIntValue ("keyPause", 0) == i);
        keyS.addItem (400 + i, u8 (k[(size_t) i].label), true, props->getIntValue ("keyShow", 1) == i);
    }
    m.addSubMenu (u8 ("操作がないとき、離れたとみなすまで"), idle);
    m.addSubMenu (u8 ("戻ったときにカードを出すのは"), show);
    m.addSeparator();
    m.addItem (2, rc::screenTrusted() ? u8 ("前のウィンドウの写真も残す") : u8 ("前のウィンドウの写真も残す（画面収録の許可が要ります）"), true, shotsOn() && rc::screenTrusted());
    m.addItem (3, u8 ("書いていた文（カーソルの前）も残す"), true, textOn());
    m.addSubMenu (u8 ("キー: いま中断する"), keyP);
    m.addSubMenu (u8 ("キー: 最後のカードを見る"), keyS);
    m.addItem (4, u8 ("ログインしたら起動"), true, rc::launchAtLogin());
    m.addSeparator();
    m.addItem (5, u8 ("取らないアプリを編集…（パスワード管理などは最初から入っています）"));
    m.addItem (6, u8 ("カードの入ったフォルダを開く"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsBtn), [this] (int r)
    {
        if (r == 1) { props->setValue ("auto", ! autoOn()); rc::setStatusIcon (! autoOn()); updateMenu(); }
        else if (r == 2)
        {
            if (! rc::screenTrusted()) { props->setValue ("shots", true); rc::requestScreen(); }
            else props->setValue ("shots", ! shotsOn());
        }
        else if (r == 3) props->setValue ("text", ! textOn());
        else if (r == 4) rc::setLaunchAtLogin (! rc::launchAtLogin());
        else if (r == 5)
        {
            auto* aw = new juce::AlertWindow (u8 ("取らないアプリ"),
                                              u8 ("ここに書いたアプリ（バンドル ID の頭）は、カードに入れません。1 行に 1 つ。"),
                                              juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("skip", props->getValue ("skip", juce::String (u8 ("com.1password\ncom.agilebits\ncom.bitwarden\ncom.apple.keychainaccess\ncom.lastpass\ncom.dashlane\ncom.apple.Passwords"))));
            if (auto* ed = aw->getTextEditor ("skip")) { ed->setMultiLine (true); ed->setReturnKeyStartsNewLine (true); ed->setSize (420, 160); }
            aw->addButton ("OK", 1);
            aw->addButton (u8 ("やめる"), 0);
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
            {
                if (res == 1) props->setValue ("skip", aw->getTextEditorContents ("skip"));
            }), true);
        }
        else if (r == 6) Card::cardsDir().revealToUser();
        else if (r >= 400) { props->setValue ("keyShow", r - 400); registerKeys(); updateMenu(); }
        else if (r >= 300) { props->setValue ("keyPause", r - 300); registerKeys(); updateMenu(); }
        else if (r >= 200) props->setValue ("showMin", r - 200);
        else if (r >= 100) props->setValue ("idleMin", r - 100);
        props->saveIfNeeded();
    });
}

//==============================================================================
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (listArea.contains (p))
    {
        const int i = (p.y - listArea.getY() - 40) / 54;
        if (p.y > listArea.getY() + 40 && juce::isPositiveAndBelow (i, cards.size())) select (i);
        return;
    }
    for (auto& l : links) if (l.r.contains (p)) { openLink (l); return; }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < (int) links.size(); ++i) if (links[(size_t) i].r.contains (e.getPosition())) h = i;
    if (h != hoverLink) { hoverLink = h; repaint (cardArea); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseExit (const juce::MouseEvent&) { if (hoverLink >= 0) { hoverLink = -1; repaint (cardArea); } }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    banner = permBtn.isVisible() ? r.removeFromTop (40) : juce::Rectangle<int>();
    if (! banner.isEmpty()) permBtn.setBounds (banner.reduced (12, 7).removeFromRight (110));
    listArea = r.removeFromLeft (250);
    cardArea = r.reduced (18, 14);
    auto bottom = cardArea.removeFromBottom (34);
    reopenBtn.setBounds (bottom.removeFromLeft (140));
    bottom.removeFromLeft (8);
    frontBtn.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (8);
    closeBtn.setBounds (bottom.removeFromLeft (80));
    settingsBtn.setBounds (bottom.removeFromRight (64));
    bottom.removeFromRight (8);
    deleteBtn.setBounds (bottom.removeFromRight (130));
    cardArea.removeFromBottom (10);
    noteBox.setBounds (cardArea.removeFromBottom (58));
    cardArea.removeFromBottom (6);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    links.clear();

    if (! banner.isEmpty())
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRect (banner);
        g.setColour (textC);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (u8 ("アプリのウィンドウ名や書いていた所を取るには「アクセシビリティ」の許可が要ります（許可したら Resume Card を開き直してください）"),
                    banner.reduced (14, 0).withTrimmedRight (120), juce::Justification::centredLeft, true);
    }

    // 左: 一覧
    {
        g.setColour (panel);
        g.fillRect (listArea);
        auto l = listArea.reduced (14, 0);
        g.setColour (textC);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Resume Card", l.removeFromTop (40), juce::Justification::centredLeft);
        if (cards.isEmpty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (u8 ("まだカードはありません。\n\n画面をロックしたり、しばらく離れたりすると、"
                                  "その直前の状態がカードになります。\n\n今すぐ作るなら メニューバーの しおり → 「いま中断する」"),
                              l.removeFromTop (160), juce::Justification::topLeft, 10);
        }
        for (int i = 0; i < cards.size(); ++i)
        {
            auto row = juce::Rectangle<int> (listArea.getX(), listArea.getY() + 40 + i * 54, listArea.getWidth(), 54);
            if (row.getY() > listArea.getBottom()) break;
            if (i == selected) { g.setColour (panel2); g.fillRect (row); g.setColour (accent); g.fillRect (row.withWidth (3)); }
            const auto& c = cards.getReference (i);
            auto t = row.reduced (14, 8);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (c.leftAt.formatted ("%m/%d %H:%M") + "  " + ago (c.leftAt) + u8 ("  ・ ") + c.whyText(), t.removeFromTop (15), juce::Justification::centredLeft, true);
            g.setColour (textC);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto title = js (c.cap.front.window);
            if (title.isEmpty() && ! c.cap.tabs.empty()) title = js (c.cap.tabs[0].title);
            g.drawText (js (c.cap.front.name) + (title.isNotEmpty() ? u8 (" — ") + title : juce::String()), t, juce::Justification::centredLeft, true);
        }
    }

    // 右: カード
    auto* c = current();
    auto a = cardArea;
    if (c == nullptr)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (u8 ("離れる直前に「どこで何をしていたか」を 1 枚のカードに残して、\n戻ってきたときに見せます。"),
                          a, juce::Justification::centred, 3);
        return;
    }

    // 見出し
    g.setColour (textC);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    if (welcome)
        g.drawText (u8 ("おかえりなさい。") + juce::String (juce::roundToInt (awayMinutes)) + u8 (" 分前、ここまでやっていました"), a.removeFromTop (32), juce::Justification::centredLeft, true);
    else
        g.drawText (c->leftAt.formatted ("%m/%d %H:%M") + u8 (" の中断（") + ago (c->leftAt) + u8 ("）"), a.removeFromTop (32), juce::Justification::centredLeft, true);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (c->whyText() + u8 ("で離れる前の状態  ・  青い所はクリックでそこだけ開きます"), a.removeFromTop (20), juce::Justification::centredLeft);
    a.removeFromTop (10);

    // 写真
    auto right = a;
    if (shot.isValid())
    {
        auto pic = a.removeFromLeft (juce::jmin (300, a.getWidth() / 2));
        right = a.withTrimmedLeft (16);
        const float s = juce::jmin ((float) pic.getWidth() / shot.getWidth(), (float) pic.getHeight() / shot.getHeight());
        auto ir = juce::Rectangle<float> (shot.getWidth() * s, shot.getHeight() * s).withPosition (pic.toFloat().getTopLeft());
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (ir.translated (0, 3), 6);
        g.drawImage (shot, ir, juce::RectanglePlacement::stretchToFit);
        g.setColour (hoverLink >= 0 && links.size() > (size_t) hoverLink ? line : line);
        g.drawRoundedRectangle (ir, 6, 1);
        links.push_back ({ ir.toNearestInt(), 4, 0 });
    }

    auto section = [&] (const juce::String& label)
    {
        right.removeFromTop (4);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, right.removeFromTop (16), juce::Justification::centredLeft);
    };
    auto link = [&] (const juce::String& text, int kind, int index, float size = 13.0f, bool bold = false)
    {
        auto r = right.removeFromTop ((int) size + 7);
        const bool hov = hoverLink == (int) links.size();
        g.setColour (hov ? blue.brighter (0.3f) : blue);
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        const int w = juce::jmin (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain), text) + 4);
        g.drawText (text, r.withWidth (w), juce::Justification::centredLeft, true);
        if (hov) g.fillRect (r.getX(), r.getBottom() - 3, w, 1);
        links.push_back ({ r.withWidth (w), kind, index });
    };

    section (u8 ("使っていたアプリ"));
    link (js (c->cap.front.name) + (c->cap.front.window.empty() ? juce::String() : u8 (" — ") + js (c->cap.front.window)), 0, 0, 15.0f, true);
    if (! c->cap.front.doc.empty())
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (home (js (c->cap.front.doc)), right.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    if (! c->cap.typed.empty() || ! c->cap.selected.empty())
    {
        section (c->cap.selected.empty() ? u8 ("最後に書いていた所（カーソルの前）") : u8 ("選んでいた文字"));
        const auto t = c->cap.selected.empty() ? tail (js (c->cap.typed)) : tail (js (c->cap.selected));
        auto box = right.removeFromTop (50);
        g.setColour (panel2);
        g.fillRoundedRectangle (box.toFloat(), 6);
        g.setColour (green);
        g.fillRect (box.withWidth (3));
        g.setColour (textC);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (t + (c->cap.selected.empty() ? juce::String (u8 (" ▍")) : juce::String()), box.reduced (12, 6), juce::Justification::centredLeft, 3, 0.9f);
    }

    if (! c->cap.tabs.empty())
    {
        section (u8 ("開いていたページ"));
        for (int i = 0; i < (int) c->cap.tabs.size() && right.getHeight() > 60; ++i)
        {
            const auto& t = c->cap.tabs[(size_t) i];
            link (js (t.title).isNotEmpty() ? js (t.title) : js (t.url), 1, i, 12.5f);
        }
    }
    if (! c->cap.others.empty() || ! c->cap.finderPath.empty())
    {
        section (u8 ("ほかに開いていたもの"));
        if (! c->cap.finderPath.empty() && right.getHeight() > 30) link (u8 ("Finder — ") + home (js (c->cap.finderPath)), 3, 0, 12.5f);
        for (int i = 0; i < (int) c->cap.others.size() && right.getHeight() > 30; ++i)
        {
            const auto& o = c->cap.others[(size_t) i];
            auto t = js (o.name);
            if (! o.doc.empty()) t << u8 (" — ") << juce::File (js (o.doc)).getFileName();
            else if (! o.window.empty()) t << u8 (" — ") << js (o.window);
            link (t, 2, i, 12.5f);
        }
    }
}

//==============================================================================
void MainComponent::loadDemo()
{
    Card c;
    c.leftAt = juce::Time::getCurrentTime() - juce::RelativeTime::minutes (42);
    c.when = c.leftAt;
    c.id = "demo";
    c.why = "lock";
    c.cap.front = { u8 ("Numbers").toStdString(), "com.apple.iWork.Numbers", "", u8 ("見積もり_11月.numbers").toStdString(),
                    "/Users/me/Documents/見積もり_11月.numbers" };
    c.cap.typed = u8 ("合計は税込みで 128,000 円。納期は 11/20 で調整中、\n田中さんに金額を確認してから送る").toStdString();
    c.cap.tabs = { { "Chrome", "com.google.Chrome", u8 ("受信トレイ - Gmail").toStdString(), "https://mail.google.com/" },
                   { "Chrome", "com.google.Chrome", u8 ("価格表 2026 - Google スプレッドシート").toStdString(), "https://docs.google.com/" } };
    c.cap.others = { { "Logic Pro", "com.apple.logic10", "", "Song_v3 - Tracks", "/Users/me/Music/Logic/Song_v3.logicx" },
                     { "Slack", "com.tinyspeck.slackmacgap", "", u8 ("#営業 - Slack").toStdString(), "" } };
    c.cap.finderPath = "/Users/me/Documents/";
    c.note = u8 ("3 行目の送料を足してから送る");
    cards.clear();
    cards.add (c);
    welcome = true;
    awayMinutes = 42;
    selected = -1;
    select (0);
    noteBox.setText (c.note, false);
}
