#include "MainComponent.h"
#include "ThirdMac.h"

namespace
{
const juce::Colour bg (0xff0f1216), panel (0xff181c22), panel2 (0xff212630), line (0xff2d333e),
                   textC (0xffeceef2), dim (0xff8c94a3), accent (0xffff9f43), green (0xff6ee7a8), blue (0xff7cb8ff), red (0xffff6b6b);

constexpr int W = 720, H = 620;
constexpr double pauseSeconds = 5.0;   // 手がこれだけ止まったら、流れが終わったとみてもう一度探す

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void styleButton (juce::TextButton& b, bool primary = false)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? accent : panel2);
    b.setColour (juce::TextButton::textColourOffId, primary ? juce::Colour (0xff2a1400) : textC);
    b.setColour (juce::ComboBox::outlineColourId, line);
}

juce::File dataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Third Time");
}

// 手順の札を折り返して並べる。描いた高さを返す
int drawChips (juce::Graphics& g, const std::vector<third::Step>& steps, juce::Rectangle<int> area, int highlight, bool draw = true)
{
    int x = area.getX(), y = area.getY();
    const int h = 22;
    const juce::FontOptions f (11.5f);
    for (int i = 0; i < (int) steps.size(); ++i)
    {
        const auto& s = steps[(size_t) i];
        const auto t = js (s.label);
        const int w = juce::jmin (area.getWidth(), (int) juce::GlyphArrangement::getStringWidth (f, t) + 16);
        if (x + w > area.getRight() && x > area.getX()) { x = area.getX(); y += h + 6; }
        if (draw)
        {
            const auto c = s.kind == third::App ? blue : s.kind == third::Key ? accent : s.kind == third::Type ? dim : green;
            g.setColour (i == highlight ? c.withAlpha (0.45f) : c.withAlpha (0.14f));
            g.fillRoundedRectangle ((float) x, (float) y, (float) w, (float) h, 5);
            g.setColour (i == highlight ? textC : c.brighter (0.2f));
            g.setFont (f);
            g.drawText (t, x, y, w, h, juce::Justification::centred, true);
        }
        x += w + 6;
    }
    return y + h - area.getY();
}
} // namespace

//==============================================================================
// 右上の知らせ: 「この流れ、今日 3 回目です」/ くり返し中
class MainComponent::Hud : public juce::Component, private juce::Timer
{
public:
    explicit Hud (MainComponent& o) : owner (o) {}

    void showFound (int index)
    {
        mode = 0; idx = index;
        const auto& f = owner.found[(size_t) index];
        juce::Image tmp (juce::Image::ARGB, 1, 1, true);
        juce::Graphics g (tmp);
        const int chipsH = drawChips (g, f.steps, { 0, 0, 372, 400 }, -1, false);
        setSize (400, 88 + chipsH + 44);
        place();
        startTimer (25000);
    }

    void showRun (int routine, int step, const juce::String& state)
    {
        mode = 1; idx = routine; runStep = step; runState = state;
        setSize (400, state == "wait" ? 128 : 92);
        place();
        stopTimer();
    }

    void hideSoon (int ms) { startTimer (ms); }

    void paint (juce::Graphics& g) override
    {
        buttons.clear();
        auto r = getLocalBounds().toFloat();
        g.setColour (panel.withAlpha (0.98f));
        g.fillRoundedRectangle (r, 12);
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12, 1);
        auto b = getLocalBounds().reduced (14, 12);
        if (mode == 0 && juce::isPositiveAndBelow (idx, (int) owner.found.size()))
        {
            const auto& f = owner.found[(size_t) idx];
            g.setColour (accent);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (u8 ("「3回目」を見つけました"), b.removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (textC);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (u8 ("この流れ、今日 ") + juce::String (f.count) + u8 (" 回目です。自動にしますか？"), b.removeFromTop (26), juce::Justification::centredLeft, true);
            b.removeFromTop (6);
            const int ch = drawChips (g, f.steps, b, -1);
            b.removeFromTop (ch + 12);
            auto row = b.removeFromTop (30);
            addButton (g, row.removeFromLeft (120), u8 ("自動にする"), 1, true);
            row.removeFromLeft (8);
            addButton (g, row.removeFromLeft (90), u8 ("あとで"), 6, false);
            row.removeFromLeft (8);
            addButton (g, row.removeFromLeft (110), u8 ("もう出さない"), 2, false);
        }
        else if (mode == 1 && juce::isPositiveAndBelow (idx, (int) owner.routines.size()))
        {
            const auto& rt = owner.routines[(size_t) idx];
            g.setColour (green);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (u8 ("くり返し中: ") + rt.name, b.removeFromTop (16), juce::Justification::centredLeft, true);
            g.setColour (textC);
            g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
            const auto& s = juce::isPositiveAndBelow (runStep, (int) rt.steps.size()) ? rt.steps[(size_t) runStep] : third::Step {};
            g.drawText (juce::String (runStep + 1) + " / " + juce::String ((int) rt.steps.size()) + u8 ("  ") + js (s.label),
                        b.removeFromTop (24), juce::Justification::centredLeft, true);
            if (runState == "wait")
            {
                g.setColour (accent);
                g.setFont (juce::FontOptions (12.5f));
                g.drawText (u8 ("ここは自分で入力してください。終わったら ⌃⌥T（または「続ける」）"), b.removeFromTop (22), juce::Justification::centredLeft, true);
                b.removeFromTop (4);
                auto row = b.removeFromTop (30);
                addButton (g, row.removeFromLeft (100), u8 ("続ける"), 7, true);
                row.removeFromLeft (8);
                addButton (g, row.removeFromLeft (90), u8 ("やめる"), 8, false);
            }
            else
            {
                b.removeFromTop (4);
                addButton (g, b.removeFromTop (28).removeFromLeft (90), u8 ("やめる"), 8, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& [r, a] : buttons)
        {
            if (! r.contains (e.getPosition())) continue;
            if (a == 1) { owner.automate (idx); setVisible (false); }
            else if (a == 2) { owner.dismiss (idx, true); setVisible (false); }
            else if (a == 6) setVisible (false);
            else if (a == 7) third::mac::continueRun();
            else if (a == 8) third::mac::cancelRun();
            return;
        }
    }

private:
    MainComponent& owner;
    int mode = 0, idx = -1, runStep = -1;
    juce::String runState;
    std::vector<std::pair<juce::Rectangle<int>, int>> buttons;

    void addButton (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, int action, bool primary)
    {
        g.setColour (primary ? accent : panel2);
        g.fillRoundedRectangle (r.toFloat(), 6);
        g.setColour (primary ? juce::Colour (0xff2a1400) : textC);
        g.setFont (juce::FontOptions (12.5f, primary ? juce::Font::bold : juce::Font::plain));
        g.drawText (t, r, juce::Justification::centred);
        buttons.push_back ({ r, action });
    }

    void place()
    {
        if (! isOnDesktop()) addToDesktop (juce::ComponentPeer::windowHasDropShadow);
        int x = 0, y = 0;
        third::mac::hudPosition (getWidth(), x, y);
        setTopLeftPosition (x, y);
        setVisible (true);
        if (auto* p = getPeer()) third::mac::makeHud (p->getNativeHandle());
        repaint();
    }

    void timerCallback() override { stopTimer(); setVisible (false); }
};

//==============================================================================
MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Third Time"; po.folderName = "Third Time"; po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);
    hud = std::make_unique<Hud> (*this);

    watchBtn.onClick = [this] { props->setValue ("watch", ! wantWatch()); setWatching (wantWatch()); };
    styleButton (watchBtn, true);
    settingsBtn.setButtonText (u8 ("設定"));
    styleButton (settingsBtn);
    settingsBtn.onClick = [this] { showSettings(); };
    inputBtn.setButtonText (u8 ("入力監視を許可"));
    styleButton (inputBtn, true);
    inputBtn.onClick = [] { if (! third::mac::inputMonitoringTrusted (true)) third::mac::openPrivacy ("ListenEvent"); };
    axBtn.setButtonText (u8 ("アクセシビリティを許可"));
    styleButton (axBtn, true);
    axBtn.onClick = [] { third::mac::accessibilityTrusted (true); };
    for (auto* b : { &watchBtn, &settingsBtn, &inputBtn, &axBtn }) addAndMakeVisible (b);
    watchBtn.setButtonText (wantWatch() ? u8 ("見張るのを止める") : u8 ("見張る"));

    setSize (W, H);
    if (live)
    {
        load();
        registerKeys();
        setWatching (wantWatch());
        startTimer (1000);
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (live) { third::mac::stopWatching(); save(); }
    props->saveIfNeeded();
}

//==============================================================================
std::vector<std::string> MainComponent::skipBundles() const
{
    juce::StringArray s;
    s.addLines (props->getValue ("skip", "com.1password\ncom.agilebits\ncom.bitwarden\ncom.apple.keychainaccess\n"
                                         "com.lastpass\ncom.dashlane\ncom.apple.Passwords"));
    s.removeEmptyStrings();
    std::vector<std::string> out;
    for (auto& x : s) out.push_back (x.trim().toStdString());
    return out;
}

void MainComponent::setWatching (bool on)
{
    watchBtn.setButtonText (on ? u8 ("見張るのを止める") : u8 ("見張る"));
    message = {};
    if (on)
    {
        if (! third::mac::startWatching ([this] (const third::Step& s) { onStep (s); }, skipBundles()))
            message = u8 ("操作を見られません。「入力監視を許可」を押して、システム設定で Third Time を ON にしてから開き直してください");
    }
    else third::mac::stopWatching();
    updateIcon();
    updateMenu();
    resized();
    repaint();
}

void MainComponent::registerKeys()
{
    third::mac::registerHotKey (0, third::mac::keyCodeT(), third::mac::modsCtrlOpt(), [] { third::mac::continueRun(); });
    for (int k = 1; k <= 9; ++k) third::mac::registerHotKey (k, -1, 0, nullptr);
    for (int i = 0; i < (int) routines.size(); ++i)
        if (routines[(size_t) i].slot > 0)
            third::mac::registerHotKey (routines[(size_t) i].slot, third::mac::keyCodeForDigit (routines[(size_t) i].slot),
                                        third::mac::modsCtrlOpt(), [this, i] { runRoutine (i); });
}

void MainComponent::updateIcon()
{
    if (! live) return;
    third::mac::setStatusIcon (third::mac::running() ? 3 : ! third::mac::watching() ? 0 : ! found.empty() ? 2 : 1);
}

void MainComponent::updateMenu()
{
    if (! live) return;
    std::vector<std::pair<std::string, int>> items;
    items.push_back ({ (third::mac::watching() ? u8 ("見張っています（今日の操作 ") + juce::String (todayCount) + u8 (" 個）") : u8 ("止めています")).toStdString(), -1 });
    items.push_back ({ "", 0 });
    for (int i = 0; i < (int) routines.size() && i < 12; ++i)
        items.push_back ({ (u8 ("▶ ") + routines[(size_t) i].name + (routines[(size_t) i].slot > 0 ? u8 ("   ⌃⌥") + juce::String (routines[(size_t) i].slot) : juce::String())).toStdString(), 100 + i });
    if (! routines.empty()) items.push_back ({ "", 0 });
    items.push_back ({ (third::mac::watching() ? u8 ("見張るのを止める") : u8 ("見張る")).toStdString(), 1 });
    items.push_back ({ u8 ("見つけた流れと設定…").toStdString(), 2 });
    items.push_back ({ "", 0 });
    items.push_back ({ u8 ("Third Time を終了").toStdString(), 9 });
    third::mac::setStatusMenu (items, [this] (int t)
    {
        if (t == 1) { props->setValue ("watch", ! wantWatch()); setWatching (wantWatch()); }
        else if (t == 2) { if (onShowRequest) onShowRequest(); }
        else if (t == 9) juce::JUCEApplication::getInstance()->systemRequestedQuit();
        else if (t >= 100) runRoutine (t - 100);
    });
}

//==============================================================================
void MainComponent::onStep (const third::Step& s)
{
    third::push (steps, s);
    ++todayCount;
    lastStepT = s.t;
    evaluated = false;
    check (true);
}

void MainComponent::check (bool waitIfLonger)
{
    third::Rules r;
    r.minCount = minCount();
    const auto f = third::findRepeat (steps, skip, r, waitIfLonger);
    evaluated = true;
    if (f.ok) report (f);
}

void MainComponent::report (const third::Found& f)
{
    skip.insert (third::skipKey (f.signature));
    Flow fl;
    fl.steps.assign (steps.begin() + f.start, steps.begin() + f.start + f.length);
    fl.count = f.count;
    fl.when = juce::Time::getCurrentTime();
    fl.signature = f.signature;
    fl.name = nameFor (fl.steps);
    found.insert (found.begin(), fl);
    if (found.size() > 20) found.pop_back();
    hud->showFound (0);
    updateIcon();
    repaint();
}

juce::String MainComponent::nameFor (const std::vector<third::Step>& st)
{
    juce::StringArray parts;
    for (auto& s : st)
    {
        if (! s.strong) continue;
        auto l = js (s.label);
        if (s.kind == third::App) l = l.upToFirstOccurrenceOf (u8 (" を前に"), false, false);
        else if (l.contains (u8 ("（"))) l = l.upToFirstOccurrenceOf (u8 ("（"), false, false);
        if (parts.isEmpty() || parts[parts.size() - 1] != l) parts.add (l);
        if (parts.size() >= 4) break;
    }
    return parts.joinIntoString (u8 (" → "));
}

void MainComponent::automate (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) found.size())) return;
    Flow r = found[(size_t) i];
    // 最初が「アプリを前に」でなければ足す（どのアプリでくり返すかをはっきりさせる）
    if (! r.steps.empty() && r.steps.front().kind != third::App)
    {
        third::Step a;
        a.kind = third::App;
        a.bundle = r.steps.front().bundle;
        a.key = "app:" + a.bundle;
        a.label = third::mac::appName (a.bundle) + " を前に";
        r.steps.insert (r.steps.begin(), a);
    }
    // あいている ⌃⌥1〜9
    std::set<int> used;
    for (auto& x : routines) used.insert (x.slot);
    for (int k = 1; k <= 9; ++k) if (! used.count (k)) { r.slot = k; break; }
    routines.push_back (r);
    found.erase (found.begin() + i);
    registerKeys();
    save();
    updateMenu();
    updateIcon();
    repaint();
    if (onShowRequest) onShowRequest();
}

void MainComponent::dismiss (int i, bool forever)
{
    if (! juce::isPositiveAndBelow (i, (int) found.size())) return;
    if (forever) dismissed.insert (third::skipKey (found[(size_t) i].signature));
    found.erase (found.begin() + i);
    save();
    updateIcon();
    repaint();
}

void MainComponent::runRoutine (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) routines.size()) || third::mac::running()) return;
    if (! third::mac::accessibilityTrusted (false))
    {
        message = u8 ("くり返すには「アクセシビリティ」の許可が要ります");
        if (onShowRequest) onShowRequest();
        resized(); repaint();
        return;
    }
    if (onHideRequest) onHideRequest();
    runIndex = i;
    third::mac::run (routines[(size_t) i].steps,
        [this, i] (int step, const std::string& st)
        {
            runStep = step; runState = js (st);
            hud->showRun (i, step, runState);
            updateIcon();
        },
        [this] (bool ok, const std::string& why)
        {
            runIndex = -1;
            const auto w = js (why);
            if (ok) { hud->setVisible (false); message = {}; }
            else
            {
                if (w == "cancel") message = u8 ("くり返しをやめました");
                else if (w.startsWith ("app:")) message = u8 ("アプリを前に出せませんでした: ") + js (third::mac::appName (w.fromFirstOccurrenceOf (":", false, false).toStdString()));
                else if (w.startsWith ("click:")) message = u8 ("押す所が見つかりませんでした: ") + w.fromFirstOccurrenceOf (":", false, false);
                else if (w == "timeout") message = u8 ("入力を 90 秒待ったので、やめました");
                hud->setVisible (false);
                if (w != "cancel" && onShowRequest) onShowRequest();
            }
            updateIcon();
            repaint();
        });
}

//==============================================================================
void MainComponent::timerCallback()
{
    // 手が止まったら「流れが終わった」とみて、もう一度探す
    // （lastStepT は 2001/1/1 からの秒。Mac の NSDate と同じ数え方）
    const double nowRef = (double) juce::Time::currentTimeMillis() / 1000.0 - 978307200.0;
    if (! evaluated && nowRef - lastStepT >= pauseSeconds) check (false);

    const bool needInput = ! third::mac::inputMonitoringTrusted (false), needAx = ! third::mac::accessibilityTrusted (false);
    if (needInput != inputBtn.isVisible() || needAx != axBtn.isVisible()) resized();
    // 許可されたら見張りを始め直す
    if (wantWatch() && ! third::mac::watching() && ! needInput) setWatching (true);
    static int n = 0;
    if (++n % 10 == 0) updateMenu();
    if (isShowing()) repaint (statsArea);
}

void MainComponent::showSettings()
{
    juce::PopupMenu m, cnt;
    for (int c : { 2, 3, 4, 5 }) cnt.addItem (100 + c, juce::String (c) + u8 (" 回目"), true, minCount() == c);
    m.addSubMenu (u8 ("何回目で知らせるか"), cnt);
    m.addItem (1, u8 ("ログインしたら起動"), true, third::mac::launchAtLogin());
    m.addItem (2, u8 ("見ないアプリを編集…（パスワード管理などは最初から入っています）"));
    m.addSeparator();
    m.addItem (3, u8 ("「もう出さない」にした流れを元に戻す（") + juce::String ((int) dismissed.size()) + u8 (" 個）"), ! dismissed.empty());
    m.addItem (4, u8 ("今日の記録を消す"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsBtn), [this] (int r)
    {
        if (r == 1) third::mac::setLaunchAtLogin (! third::mac::launchAtLogin());
        else if (r == 2)
        {
            auto* aw = new juce::AlertWindow (u8 ("見ないアプリ"), u8 ("ここに書いたアプリ（バンドル ID の頭）の操作は記録しません。1 行に 1 つ。"), juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("skip", props->getValue ("skip", "com.1password\ncom.agilebits\ncom.bitwarden\ncom.apple.keychainaccess\ncom.lastpass\ncom.dashlane\ncom.apple.Passwords"));
            if (auto* ed = aw->getTextEditor ("skip")) { ed->setMultiLine (true); ed->setReturnKeyStartsNewLine (true); ed->setSize (420, 160); }
            aw->addButton ("OK", 1);
            aw->addButton (u8 ("やめる"), 0);
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
            {
                if (res != 1) return;
                props->setValue ("skip", aw->getTextEditorContents ("skip"));
                third::mac::setSkip (skipBundles());
            }), true);
        }
        else if (r == 3) { for (auto& d : dismissed) skip.erase (d); dismissed.clear(); save(); }
        else if (r == 4) { steps.clear(); todayCount = 0; skip = dismissed; found.clear(); updateIcon(); repaint(); }
        else if (r >= 100) props->setValue ("count", r - 100);
        props->saveIfNeeded();
    });
}

//==============================================================================
static juce::var stepVar (const third::Step& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", s.kind);
    o->setProperty ("key", js (s.key));
    o->setProperty ("label", js (s.label));
    o->setProperty ("strong", s.strong);
    o->setProperty ("bundle", js (s.bundle));
    o->setProperty ("keyCode", s.keyCode);
    o->setProperty ("flags", juce::String ((juce::uint64) s.flags));
    o->setProperty ("role", js (s.role));
    o->setProperty ("title", js (s.title));
    o->setProperty ("relX", s.relX);
    o->setProperty ("relY", s.relY);
    return o;
}

static third::Step varStep (const juce::var& v)
{
    third::Step s;
    s.kind = (int) v["kind"];
    s.key = v["key"].toString().toStdString();
    s.label = v["label"].toString().toStdString();
    s.strong = (bool) v["strong"];
    s.bundle = v["bundle"].toString().toStdString();
    s.keyCode = (int) v["keyCode"];
    s.flags = (unsigned long long) v["flags"].toString().getLargeIntValue();
    s.role = v["role"].toString().toStdString();
    s.title = v["title"].toString().toStdString();
    s.relX = (double) v["relX"];
    s.relY = (double) v["relY"];
    return s;
}

void MainComponent::save()
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> rs, ds;
    for (auto& r : routines)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", r.name);
        o->setProperty ("slot", r.slot);
        juce::Array<juce::var> st;
        for (auto& s : r.steps) st.add (stepVar (s));
        o->setProperty ("steps", st);
        rs.add (o);
    }
    for (auto& d : dismissed) ds.add (js (d));
    root->setProperty ("routines", rs);
    root->setProperty ("dismissed", ds);
    dataDir().createDirectory();
    dataDir().getChildFile ("data.json").replaceWithText (juce::JSON::toString (juce::var (root)));
}

void MainComponent::load()
{
    const auto v = juce::JSON::parse (dataDir().getChildFile ("data.json"));
    if (auto* a = v["routines"].getArray())
        for (auto& x : *a)
        {
            Flow r;
            r.name = x["name"].toString();
            r.slot = (int) x["slot"];
            if (auto* st = x["steps"].getArray()) for (auto& s : *st) r.steps.push_back (varStep (s));
            if (! r.steps.empty()) routines.push_back (r);
        }
    if (auto* a = v["dismissed"].getArray())
        for (auto& x : *a) { dismissed.insert (x.toString().toStdString()); skip.insert (x.toString().toStdString()); }
}

//==============================================================================
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    for (auto& h : hits)
    {
        if (! h.r.contains (e.getPosition())) continue;
        if (h.action == 1) automate (h.index);
        else if (h.action == 2) dismiss (h.index, true);
        else if (h.action == 3) runRoutine (h.index);
        else if (h.action == 4)
        {
            routines.erase (routines.begin() + h.index);
            registerKeys(); save(); updateMenu(); repaint();
        }
        else if (h.action == 5)
        {
            // キーを選び直す
            juce::PopupMenu m;
            std::set<int> used;
            for (auto& r : routines) used.insert (r.slot);
            m.addItem (1000, u8 ("なし"), true, routines[(size_t) h.index].slot == 0);
            for (int k = 1; k <= 9; ++k)
                m.addItem (1000 + k, u8 ("⌃⌥") + juce::String (k), ! used.count (k) || routines[(size_t) h.index].slot == k, routines[(size_t) h.index].slot == k);
            const int idx = h.index;
            m.showMenuAsync ({}, [this, idx] (int r)
            {
                if (r < 1000 || ! juce::isPositiveAndBelow (idx, (int) routines.size())) return;
                routines[(size_t) idx].slot = r - 1000;
                registerKeys(); save(); updateMenu(); repaint();
            });
        }
        return;
    }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int hv = -1;
    for (int i = 0; i < (int) hits.size(); ++i) if (hits[(size_t) i].r.contains (e.getPosition())) hv = i;
    if (hv != hover) { hover = hv; repaint(); }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (62);
    {
        auto h = header.reduced (18, 15);
        settingsBtn.setBounds (h.removeFromRight (64));
        h.removeFromRight (8);
        watchBtn.setBounds (h.removeFromRight (140));
    }
    const bool needInput = live && ! third::mac::inputMonitoringTrusted (false);
    const bool needAx = live && ! third::mac::accessibilityTrusted (false);
    inputBtn.setVisible (needInput);
    axBtn.setVisible (needAx);
    banner = (needInput || needAx || message.isNotEmpty()) ? r.removeFromTop (needInput || needAx ? 84 : 40) : juce::Rectangle<int>();
    if (! banner.isEmpty())
    {
        auto b = banner.reduced (18, 8).removeFromBottom (28);
        if (needInput) { inputBtn.setBounds (b.removeFromLeft (150)); b.removeFromLeft (8); }
        if (needAx) axBtn.setBounds (b.removeFromLeft (190));
    }
    statsArea = r.removeFromTop (30).reduced (18, 0);
    body = r.reduced (18, 6);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    hits.clear();

    {
        auto h = header.reduced (18, 0);
        g.setColour (textC);
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        g.drawText ("Third Time", h.removeFromLeft (130), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (u8 ("「3回目」検出係"), h.removeFromLeft (130), juce::Justification::centredLeft);
    }
    if (! banner.isEmpty())
    {
        g.setColour (accent.withAlpha (0.13f));
        g.fillRect (banner);
        g.setColour (textC);
        g.setFont (juce::FontOptions (12.5f));
        auto t = banner.reduced (18, 8);
        const juce::String txt = message.isNotEmpty() ? message
            : u8 ("操作を見るには「入力監視」、押した所の名前を知る・くり返すには「アクセシビリティ」の許可が要ります。"
                  "許可したら Third Time を開き直してください（打った文字の中身は記録しません）");
        g.drawFittedText (txt, t.removeFromTop (inputBtn.isVisible() || axBtn.isVisible() ? 40 : t.getHeight()), juce::Justification::centredLeft, 2);
    }

    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ((live && ! third::mac::watching() ? u8 ("止めています") : u8 ("見張っています"))
                    + u8 ("  ・  今日見た操作 ") + juce::String (todayCount) + u8 (" 個  ・  同じ流れの ") + juce::String (minCount())
                    + u8 (" 回目で知らせます（打った文字の中身は記録しません）"),
                statsArea, juce::Justification::centredLeft, true);

    auto b = body;
    auto sectionTitle = [&] (const juce::String& t)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (t, b.removeFromTop (22), juce::Justification::centredLeft);
    };
    auto button = [&] (juce::Rectangle<int> r, const juce::String& t, int action, int index, bool primary)
    {
        const bool hv = hover == (int) hits.size();
        g.setColour (primary ? (hv ? accent.brighter (0.2f) : accent) : (hv ? line.brighter (0.2f) : panel2));
        g.fillRoundedRectangle (r.toFloat(), 6);
        g.setColour (primary ? juce::Colour (0xff2a1400) : textC);
        g.setFont (juce::FontOptions (12.0f, primary ? juce::Font::bold : juce::Font::plain));
        g.drawText (t, r, juce::Justification::centred);
        hits.push_back ({ r, action, index });
    };

    sectionTitle (u8 ("見つけた流れ"));
    if (found.empty())
    {
        g.setColour (dim.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (u8 ("まだありません。ふだんどおり作業していると、同じ手順を 3 回くり返したときに右上で知らせます。\n"
                              "例: Excel でコピー → Chrome に切り替え → 検索欄に貼り付け → Return"),
                          b.removeFromTop (44), juce::Justification::topLeft, 3);
        b.removeFromTop (10);
    }
    for (int i = 0; i < (int) found.size() && b.getHeight() > 140; ++i)
    {
        const auto& f = found[(size_t) i];
        const int ch = drawChips (g, f.steps, b.reduced (12, 0).withTrimmedRight (230), -1, false);
        auto card = b.removeFromTop (std::max (60, ch + 36));
        g.setColour (panel);
        g.fillRoundedRectangle (card.toFloat(), 8);
        auto in = card.reduced (12, 8);
        auto btns = in.removeFromRight (220).removeFromTop (28);
        button (btns.removeFromLeft (110), u8 ("自動にする"), 1, i, true);
        btns.removeFromLeft (8);
        button (btns.removeFromLeft (100), u8 ("もう出さない"), 2, i, false);
        g.setColour (textC);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (f.when.formatted ("%H:%M") + u8 ("  今日 ") + juce::String (f.count) + u8 (" 回"), in.removeFromTop (18), juce::Justification::centredLeft);
        in.removeFromTop (4);
        drawChips (g, f.steps, in, -1);
        b.removeFromTop (8);
    }

    b.removeFromTop (6);
    sectionTitle (u8 ("自動にした流れ  ・  ⌃⌥ + 数字キー、メニューバーの ▶ でくり返せます"));
    if (routines.empty())
    {
        g.setColour (dim.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (u8 ("「自動にする」を押すと、ここに入ります。「文字を入力」の所だけは、くり返すときに止まって待つので自分で入力して ⌃⌥T で続けます。"),
                          b.removeFromTop (40), juce::Justification::topLeft, 3);
    }
    for (int i = 0; i < (int) routines.size() && b.getHeight() > 50; ++i)
    {
        const auto& r = routines[(size_t) i];
        const int ch = drawChips (g, r.steps, b.reduced (12, 0).withTrimmedRight (250), -1, false);
        auto card = b.removeFromTop (std::max (60, ch + 36));
        g.setColour (panel);
        g.fillRoundedRectangle (card.toFloat(), 8);
        auto in = card.reduced (12, 8);
        auto btns = in.removeFromRight (240).removeFromTop (28);
        button (btns.removeFromLeft (80), runIndex == i ? u8 ("実行中") : u8 ("▶ 実行"), 3, i, true);
        btns.removeFromLeft (6);
        button (btns.removeFromLeft (80), r.slot > 0 ? u8 ("⌃⌥") + juce::String (r.slot) : u8 ("キー…"), 5, i, false);
        btns.removeFromLeft (6);
        button (btns.removeFromLeft (64), u8 ("消す"), 4, i, false);
        g.setColour (textC);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (r.name, in.removeFromTop (18), juce::Justification::centredLeft, true);
        in.removeFromTop (4);
        drawChips (g, r.steps, in, runIndex == i ? runStep : -1);
        b.removeFromTop (8);
    }
}

//==============================================================================
void MainComponent::loadDemo()
{
    auto st = [] (int kind, const char* label, bool strong = true)
    { third::Step s; s.kind = kind; s.label = label; s.strong = strong; s.key = label; return s; };
    Flow f;
    f.steps = { st (third::App, "Microsoft Excel を前に"), st (third::Key, "⌘C（コピー）"), st (third::App, "Google Chrome を前に"),
                st (third::Click, "「検索」の欄をクリック（Google Chrome）"), st (third::Key, "⌘V（貼り付け）"), st (third::Key, "Return（決定）") };
    f.count = 3; f.when = juce::Time::getCurrentTime(); f.name = nameFor (f.steps);
    found.push_back (f);
    Flow r;
    r.steps = { st (third::App, "Finder を前に"), st (third::Click, "「新規フォルダ」（メニュー）（Finder）"), st (third::Type, "文字を入力（Finder）", false),
                st (third::Key, "Return（決定）"), st (third::App, "Logic Pro を前に"), st (third::Key, "⇧⌘S（別名で保存）") };
    r.name = nameFor (r.steps); r.slot = 1;
    routines.push_back (r);
    todayCount = 1284;
    resized();
}
