#include "MainComponent.h"
#include <set>
#include "MacSupport.h"
#include "NoiseReduce.h"

namespace
{
const juce::Colour bg (0xff0e1117), panel (0xff171c26), panel2 (0xff1f2633), line (0xff2b3445),
                   textC (0xffe8ecf3), dim (0xff8a94a6), amber (0xffffc24b), red (0xffff4d5e), cyan (0xff4fd6e8),
                   green (0xff5fe08f), violet (0xffb58cff);

constexpr int W = 460, H = 846;
constexpr double maxSeconds = 300.0;     // 1 回の録音は 5 分まで
constexpr double ringSeconds = 32.0;     // さかのぼり録音で覚えておく長さ（使うのは 30 秒まで）
constexpr double autoStopSeconds = 4.0;  // 黙ってこれだけたったら止める
constexpr float loudThreshold = 0.004f;  // 約 -48 dBFS より大きければ「鳴っている」

void styleButton (juce::TextButton& b, bool accent = false)
{
    b.setColour (juce::TextButton::buttonColourId, accent ? amber : panel2);
    b.setColour (juce::TextButton::buttonOnColourId, amber);
    b.setColour (juce::TextButton::textColourOffId, accent ? juce::Colour (0xff1a1300) : textC);
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1300));
    b.setColour (juce::ComboBox::outlineColourId, line);
}

void styleToggle (juce::ToggleButton& t)
{
    t.setColour (juce::ToggleButton::textColourId, dim);
    t.setColour (juce::ToggleButton::tickColourId, amber);
    t.setColour (juce::ToggleButton::tickDisabledColourId, dim);
}

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }



bool readWav (const juce::File& f, std::vector<float>& out, double& fs)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b ((int) r->numChannels, n);
    r->read (&b, 0, n, 0, true, true);
    out.assign ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) out[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    fs = r->sampleRate;
    return true;
}

bool writeWav (const juce::File& f, const std::vector<float>& x, double fs)
{
    juce::AudioBuffer<float> b (1, (int) x.size());
    if (! x.empty()) b.copyFrom (0, 0, x.data(), (int) x.size());
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wf;
    auto w = wf.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (1).withBitsPerSample (24));
    return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// サンプリング周波数を合わせる（重ね録りの再生用。素朴な直線補間）
// Logic と同じ呼び方（真ん中の C = C3 = 60）
juce::String noteName (int n)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (nm[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);
}

std::vector<float> resample (const std::vector<float>& x, double from, double to)
{
    if (std::abs (from - to) < 1.0 || x.empty()) return x;
    const double ratio = from / to;
    std::vector<float> y ((size_t) ((double) x.size() / ratio));
    for (size_t i = 0; i < y.size(); ++i)
    {
        const double p = (double) i * ratio;
        const size_t k = (size_t) p;
        const float f = (float) (p - (double) k);
        y[i] = k + 1 < x.size() ? x[k] * (1 - f) + x[k + 1] * f : x[std::min (k, x.size() - 1)];
    }
    return y;
}

juce::String layerFileName (int i) { return i == 0 ? "Voice.wav" : "Voice " + juce::String (i + 1) + ".wav"; }
} // namespace

//==============================================================================
void Hud::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xf0141922));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (r, 14.0f, 1.2f);
    auto c = getLocalBounds().reduced (16, 9);
    auto dot = c.removeFromLeft (14).withSizeKeepingCentre (10, 10).translated (0, -9).toFloat();
    g.setColour (accent);
    g.fillEllipse (dot);
    c.removeFromLeft (8);
    g.setColour (textC);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (big, c.removeFromTop (22), juce::Justification::centredLeft);
    if (meter >= 0)
    {
        auto m = c.removeFromTop (8).reduced (0, 2).toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (m, 2.0f);
        g.setColour (meter > 0.92f ? red : accent);
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * juce::jlimit (0.0f, 1.0f, meter)), 2.0f);
    }
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (small, c, juce::Justification::topLeft, 1);
}

//==============================================================================
//==============================================================================
// 手直しの画面。音をクリックで選ぶ（shift で足す）→ 上下にドラッグ／↑↓ で高さ、delete で消す、J で 2 音をつなぐ、⌘Z で 1 つ戻す。
// ▶ で MIDI・声・両方を聴き比べ
class EditPanel : public juce::Component, private juce::Timer
{
public:
    explicit EditPanel (MainComponent& o) : owner (o)
    {
        const char* labels[] = { "▶ MIDI", "▶ 声", "▶ 両方", "↑", "↓", "消す", "つなぐ", "1 つ戻す", "最初の状態", "閉じる" };
        const char* tips[] = { "できた MIDI を鳴らす（スペース）", "録った声を鳴らす", "声と MIDI を一緒に鳴らして聴き比べる",
                               "半音上げる（↑ キー、shift で 1 オクターブ）。選んでいなければ全部", "半音下げる（↓ キー、shift で 1 オクターブ）。選んでいなければ全部",
                               "選んだ音を消す（delete）", "選んだ音を 1 つの音にまとめる（J）。割れた音を直すとき",
                               "1 つ前に戻す（⌘Z）", "手直しを全部やめて、録ったままに戻す", "閉じる（esc）。直した内容は MIDI に残ります" };
        for (int i = 0; i < numButtons; ++i)
        {
            auto& b = buttons[(size_t) i];
            b.setButtonText (u8 (labels[i]));
            b.setTooltip (u8 (tips[i]));
            b.setWantsKeyboardFocus (false);
            styleButton (b, i == 9);
            b.onClick = [this, i] { press (i); };
            addAndMakeVisible (b);
        }
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    void clearSelection() { selected.clear(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        g.setColour (textC);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (u8 ("手直し"), 14, 8, 120, 24, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (u8 ("クリックで選ぶ（shift で足す）・上下にドラッグで高さ・delete で消す・J でつなぐ・⌘Z で戻す"),
                          juce::Rectangle<int> (80, 8, getWidth() - 94, 26), juce::Justification::centredLeft, 2);

        auto ra = roll.toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (ra, 6.0f);
        layout();
        if (items.empty())
        {
            g.setColour (dim);
            g.drawText (u8 ("音がありません"), roll, juce::Justification::centred);
            return;
        }
        // 行（音名）と小節線
        for (int n = loNote; n <= hiNote; ++n)
        {
            const float y = yOf (n);
            const bool white = hum::inScale (n, 0, hum::scMajor);
            if (! white) { g.setColour (bg.withAlpha (0.35f)); g.fillRect (ra.getX() + labelW, y, ra.getWidth() - labelW, rowH); }
            if (n % 12 == 0 || rowH >= 11.0f)
            {
                g.setColour (n % 12 == 0 ? textC.withAlpha (0.8f) : dim.withAlpha (0.7f));
                g.setFont (juce::FontOptions (juce::jmin (10.0f, rowH)));
                g.drawText (noteName (n), (int) ra.getX() + 3, (int) y, labelW - 4, (int) rowH, juce::Justification::centredLeft);
            }
        }
        g.saveState();
        g.reduceClipRegion (roll.withTrimmedLeft (labelW));
        for (int b = 0; b <= (int) lenBeats; ++b)
        {
            g.setColour (b % 4 == 0 ? line.brighter (0.3f) : line.withAlpha (0.5f));
            g.fillRect (xOf (b), ra.getY(), b % 4 == 0 ? 1.5f : 1.0f, ra.getHeight());
            if (b % 4 == 0)
            {
                g.setColour (dim);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (b / 4 + 1), (int) xOf (b) + 3, (int) ra.getY() + 1, 24, 12, juce::Justification::left);
            }
        }
        const juce::Colour melCol[] = { amber, violet, green, red };
        for (auto& it : items)
        {
            auto r = rectOf (it);
            const bool sel = selected.count ({ it.layer, it.p.src }) > 0;
            g.setColour (melCol[it.layer % 4].withAlpha (sel ? 1.0f : 0.8f));
            g.fillRoundedRectangle (r, 2.0f);
            if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.expanded (1.0f), 2.5f, 1.5f); }
        }
        if (owner.player.isPlaying())
        {
            const float x = xOf (owner.player.seconds() * owner.options().bpm / 60.0);
            g.setColour (cyan);
            g.fillRect (x, ra.getY(), 1.5f, ra.getHeight());
        }
        g.restoreState();
        if (lenBeats > viewLen + 1e-6)
        {
            // 横の位置（下のバー）
            const float bw = (float) (roll.getWidth() - labelW), x0 = (float) roll.getX() + labelW;
            g.setColour (line);
            g.fillRoundedRectangle (x0, ra.getBottom() - 4, bw, 3, 1.5f);
            g.setColour (amber.withAlpha (0.8f));
            g.fillRoundedRectangle (x0 + bw * (float) (viewStart / lenBeats), ra.getBottom() - 4, bw * (float) (viewLen / lenBeats), 3, 1.5f);
            g.setColour (dim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (u8 ("ホイールで横へ"), roll.withTrimmedBottom (6).removeFromBottom (12).reduced (6, 0), juce::Justification::right);
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
        viewStart = juce::jlimit (0.0, std::max (0.0, lenBeats - viewLen), viewStart + d * 8.0);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        r.removeFromTop (30);
        auto b2 = r.removeFromBottom (28);
        r.removeFromBottom (6);
        auto b1 = r.removeFromBottom (28);
        r.removeFromBottom (8);
        roll = r;
        auto put = [] (juce::Rectangle<int>& row, juce::Component& c, int w, int gap) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
        put (b1, buttons[0], 76, 4); put (b1, buttons[1], 60, 4); put (b1, buttons[2], 70, 14);
        put (b1, buttons[3], 40, 4); put (b1, buttons[4], 40, 4);
        put (b2, buttons[5], 60, 4); put (b2, buttons[6], 70, 14); put (b2, buttons[7], 84, 4); put (b2, buttons[8], 90, 4);
        buttons[9].setBounds (b2);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const bool cmd = k.getModifiers().isCommandDown(), shift = k.getModifiers().isShiftDown();
        if (k == juce::KeyPress::escapeKey) { owner.closeEditor(); return true; }
        if (k == juce::KeyPress::upKey)   { transpose (shift ? 12 : 1); return true; }
        if (k == juce::KeyPress::downKey) { transpose (shift ? -12 : -1); return true; }
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
        if (k == juce::KeyPress::spaceKey) { press (0); return true; }
        if (! cmd && (k.getKeyCode() == 'J' || k.getKeyCode() == 'j')) { joinSelected(); return true; }
        if (cmd && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z')) { press (7); return true; }
        if (cmd && (k.getKeyCode() == 'A' || k.getKeyCode() == 'a'))
        {
            selected.clear();
            for (auto& it : items) selected.insert ({ it.layer, it.p.src });
            repaint(); return true;
        }
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragging = false; moved = 0;
        const auto* hit = itemAt (e.position);
        if (hit == nullptr) { if (! e.mods.isShiftDown()) selected.clear(); repaint(); return; }
        const Key key { hit->layer, hit->p.src };
        if (e.mods.isShiftDown()) { if (selected.count (key)) selected.erase (key); else selected.insert (key); }
        else if (! selected.count (key)) { selected.clear(); selected.insert (key); }
        dragging = true; startY = e.position.y;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || rowH <= 0) return;
        const int rows = (int) std::lround ((startY - e.position.y) / rowH);
        if (rows == moved) return;
        if (moved == 0) owner.beginEdit();
        shift (rows - moved);
        moved = rows;
        owner.editDone();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    using Key = std::pair<int, size_t>;   // (レイヤー, r.melody の何番目)
    struct Item { idea::Placed p; int layer; };
    static constexpr int numButtons = 10, labelW = 30;
    MainComponent& owner;
    std::array<juce::TextButton, numButtons> buttons;
    juce::Rectangle<int> roll;
    std::vector<Item> items;
    std::set<Key> selected;
    int loNote = 48, hiNote = 72, moved = 0;
    float rowH = 8, startY = 0;
    double lenBeats = 8, viewStart = 0, viewLen = 16;   // 一度に見せるのは 4 小節
    bool dragging = false;

    void timerCallback() override
    {
        if (owner.player.isPlaying())
        {
            // 再生している所が見えるように
            const double b = owner.player.seconds() * owner.options().bpm / 60.0;
            if (b < viewStart || b > viewStart + viewLen * 0.9)
                viewStart = juce::jlimit (0.0, std::max (0.0, lenBeats - viewLen), std::floor (b / 4.0) * 4.0);
            repaint();
        }
        else if (isMouseButtonDown()) repaint();
    }

    void layout()
    {
        items.clear();
        auto& Ls = owner.layers();
        const double pick = idea::pickupBeats (Ls, owner.options());
        int lo = 127, hi = 0;
        lenBeats = 4;
        for (size_t i = 0; i < Ls.size(); ++i)
            for (auto p : idea::place (Ls[i], owner.options()))
            {
                p.b0 += pick; p.b1 += pick;
                items.push_back ({ p, (int) i });
                lo = std::min (lo, p.n); hi = std::max (hi, p.n);
                lenBeats = std::max (lenBeats, p.b1);
            }
        lenBeats = std::ceil (lenBeats / 4.0) * 4.0;
        viewLen = std::min (16.0, lenBeats);
        viewStart = juce::jlimit (0.0, std::max (0.0, lenBeats - viewLen), viewStart);
        if (hi < lo) { lo = 60; hi = 72; }
        lo -= 2; hi += 2;
        while (hi - lo < 14) { --lo; ++hi; }
        loNote = juce::jlimit (0, 127, lo); hiNote = juce::jlimit (0, 127, hi);
        rowH = (float) roll.getHeight() / (float) (hiNote - loNote + 1);
        // 消した音を選んだままにしない
        for (auto it = selected.begin(); it != selected.end();)
            it = (it->first < (int) Ls.size() && it->second < Ls[(size_t) it->first].r.melody.size()) ? std::next (it) : selected.erase (it);
    }
    float xOf (double beat) const
    {
        const float x0 = (float) roll.getX() + labelW, w = (float) roll.getWidth() - labelW - 4;
        return x0 + (float) ((beat - viewStart) / std::max (1.0, viewLen)) * w;
    }
    float yOf (int n) const { return (float) roll.getY() + (float) (hiNote - n) * rowH; }
    juce::Rectangle<float> rectOf (const Item& it) const
    {
        const float x0 = xOf (it.p.b0), x1 = xOf (it.p.b1);
        return { x0, yOf (it.p.n) + 1, juce::jmax (3.0f, x1 - x0 - 1), juce::jmax (2.0f, rowH - 2) };
    }
    const Item* itemAt (juce::Point<float> p)
    {
        layout();
        for (int i = (int) items.size() - 1; i >= 0; --i)
            if (p.x > (float) roll.getX() + labelW && rectOf (items[(size_t) i]).expanded (2, 2).contains (p)) return &items[(size_t) i];
        return nullptr;
    }

    // 選んだ音（なければ全部）を半音ずつ
    void shift (int d)
    {
        auto& Ls = owner.layers();
        for (size_t l = 0; l < Ls.size(); ++l)
        {
            auto& mel = Ls[l].r.melody;
            bool any = false;
            for (size_t i = 0; i < mel.size(); ++i)
                if (selected.empty() || selected.count ({ (int) l, i })) { mel[i].n = juce::jlimit (0, 127, mel[i].n + d); any = true; }
            if (any) Ls[l].edited = true;
        }
    }
    void transpose (int d)
    {
        if (owner.layers().empty()) return;
        owner.beginEdit(); shift (d); owner.editDone(); repaint();
    }
    void removeSelected()
    {
        if (selected.empty()) return;
        owner.beginEdit();
        auto& Ls = owner.layers();
        for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        {
            auto& mel = Ls[(size_t) it->first].r.melody;
            if (it->second < mel.size()) { mel.erase (mel.begin() + (long) it->second); Ls[(size_t) it->first].edited = true; }
        }
        selected.clear();
        owner.editDone(); repaint();
    }
    // 同じレイヤーで選んだ音を 1 つに（いちばん長い音の高さ、最初から最後まで）
    void joinSelected()
    {
        if (selected.size() < 2) return;
        const int layer = selected.begin()->first;
        std::vector<size_t> idx;
        for (auto& k : selected) if (k.first == layer) idx.push_back (k.second);
        if (idx.size() < 2) return;
        owner.beginEdit();
        auto& L = owner.layers()[(size_t) layer];
        auto& mel = L.r.melody;
        idea::Note m = mel[idx[0]];
        double longest = 0;
        for (auto i : idx)
        {
            const auto& n = mel[i];
            if (n.e - n.s > longest) { longest = n.e - n.s; m.n = n.n; }
            m.s = std::min (m.s, n.s); m.e = std::max (m.e, n.e); m.vel = std::max (m.vel, n.vel);
        }
        std::sort (idx.rbegin(), idx.rend());
        for (auto i : idx) mel.erase (mel.begin() + (long) i);
        mel.push_back (m);
        std::sort (mel.begin(), mel.end(), [] (auto& a, auto& b) { return a.s < b.s; });
        L.edited = true;
        selected.clear();
        owner.editDone(); repaint();
    }
    void press (int i)
    {
        switch (i)
        {
            case 0: case 1: case 2:
                if (owner.player.isPlaying()) owner.player.stop();
                else owner.preview (i != 1, i != 0);
                break;
            case 3: transpose (1); break;
            case 4: transpose (-1); break;
            case 5: removeSelected(); break;
            case 6: joinSelected(); break;
            case 7: if (owner.undoEdit()) selected.clear(); break;
            case 8: selected.clear(); owner.revertEdits(); break;
            case 9: owner.closeEditor(); return;
            default: break;
        }
        grabKeyboardFocus();
        repaint();
    }
};

juce::File MainComponent::ideasRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Idea Catcher");
}

MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Idea Catcher";
    po.folderName = "Idea Catcher";
    po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);

    for (auto* b : { &editBtn, &keyDownBtn, &keyUpBtn, &modeBtn, &bpmMinus, &bpmPlus, &bpmHalf, &bpmDouble, &humMinus, &humPlus, &tempoReset,
                     &quantBtn, &dubBtn, &hotkeyBtn, &inputBtn, &countBtn, &moreBtn, &tapBtn })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    styleButton (finderBtn, true);
    addAndMakeVisible (finderBtn);
    for (auto* t : { &autoStopBtn, &loginBtn, &prerollBtn })
    {
        styleToggle (*t);
        addAndMakeVisible (*t);
    }

    editBtn.setButtonText (u8 ("手直し")); inputBtn.setButtonText (u8 ("入力…"));
    editBtn.setTooltip (u8 ("いらない音を消す・高さを直す・2 音をつなぐ。声と MIDI を聴き比べられます"));
    autoStopBtn.setButtonText (u8 ("黙って 4 秒で止める"));
    loginBtn.setButtonText (u8 ("ログイン時に起動"));
    prerollBtn.setButtonText (u8 ("さかのぼり録音（直前 30 秒も残す）"));
    keyDownBtn.setButtonText (u8 ("◀")); keyUpBtn.setButtonText (u8 ("▶"));
    bpmMinus.setButtonText (u8 ("−")); bpmPlus.setButtonText ("+");
    bpmHalf.setButtonText (u8 ("÷2")); bpmDouble.setButtonText (u8 ("×2"));
    dubBtn.setButtonText (u8 ("＋ 重ねて録る"));
    moreBtn.setButtonText (u8 ("その他…"));
    tapBtn.setButtonText ("TAP");

    keyDownBtn.onClick = [this] { editOptions ([] (auto& o) { o.key = (o.key + 11) % 12; }); };
    keyUpBtn.onClick   = [this] { editOptions ([] (auto& o) { o.key = (o.key + 1) % 12; }); };
    modeBtn.onClick    = [this] { editOptions ([] (auto& o) { o.minor = ! o.minor; }); };
    // 曲のテンポ（MIDI の速さだけ変わる。音の拍の位置はそのまま）
    bpmMinus.onClick   = [this] { editOptions ([] (auto& o) { o.outBpm = std::max (30.0, std::round (o.tempoOut()) - 1); }); };
    bpmPlus.onClick    = [this] { editOptions ([] (auto& o) { o.outBpm = std::min (300.0, std::round (o.tempoOut()) + 1); }); };
    tempoReset.onClick = [this] { editOptions ([] (auto& o) { o.outBpm = 0; }); };
    // 鼻歌のテンポ（何秒を 1 拍とみなすか。判定が倍・半分にずれたとき・少しずれたとき）
    auto humEdit = [this] (std::function<double (double)> f)
    {
        editOptions ([f] (auto& o)
        {
            const bool follow = o.outBpm <= 0;
            o.bpm = std::clamp (f (o.bpm), 30.0, 300.0);
            if (follow) o.outBpm = 0;
        });
    };
    humMinus.onClick   = [humEdit] { humEdit ([] (double b) { return b - 1; }); };
    humPlus.onClick    = [humEdit] { humEdit ([] (double b) { return b + 1; }); };
    bpmHalf.onClick    = [humEdit] { humEdit ([] (double b) { return std::round (b / 2); }); };
    bpmDouble.onClick  = [humEdit] { humEdit ([] (double b) { return b * 2; }); };
    humMinus.setButtonText (u8 ("−")); humPlus.setButtonText ("+");
    tempoReset.setButtonText (u8 ("元の速さ"));
    tempoLabel.setEditable (true, true, false);
    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, textC);
    tempoLabel.setColour (juce::Label::backgroundColourId, panel2.withAlpha (0.6f));
    tempoLabel.setColour (juce::Label::textWhenEditingColourId, textC);
    tempoLabel.setColour (juce::Label::backgroundWhenEditingColourId, panel2);
    tempoLabel.setColour (juce::Label::outlineWhenEditingColourId, amber);
    tempoLabel.setTooltip (u8 ("クリックして数字を打つと、曲のテンポを変えられます"));
    tempoLabel.onTextChange = [this]
    {
        const double v = tempoLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v >= 30 && v <= 300) editOptions ([v] (auto& o) { o.outBpm = std::abs (v - o.bpm) < 0.01 ? 0.0 : v; });
        else refreshButtons();
    };
    addAndMakeVisible (tempoLabel);
    editBtn.onClick    = [this] { if (editor != nullptr) closeEditor(); else openEditor(); };
    quantBtn.onClick   = [this] { showQuantMenu(); };
    dubBtn.onClick     = [this] { showDubMenu(); };
    tapBtn.onClick     = [this] { tap(); };
    finderBtn.onClick  = [this] { revealMidi(); };
    inputBtn.onClick   = [this] { showInputSettings(); };
    countBtn.onClick   = [this] { showCountMenu(); };
    moreBtn.onClick    = [this] { showMoreMenu(); };
    hotkeyBtn.onClick  = [this]
    {
        juce::PopupMenu m;
        const auto& c = mac::hotKeyChoices();
        for (int i = 0; i < (int) c.size(); ++i)
            m.addItem (i + 1, u8 (c[(size_t) i].label), true, props->getIntValue ("hotkey", 0) == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (hotkeyBtn), [this] (int r)
        {
            if (r <= 0) return;
            props->setValue ("hotkey", r - 1);
            registerHotKey();
        });
    };

    autoStopBtn.setToggleState (props->getBoolValue ("autoStop", true), juce::dontSendNotification);
    prerollBtn.setToggleState (props->getBoolValue ("preroll", false), juce::dontSendNotification);
    autoStopBtn.onClick = [this] { props->setValue ("autoStop", autoStopBtn.getToggleState()); };
    prerollBtn.onClick  = [this] { setPreroll (prerollBtn.getToggleState()); };
    loginBtn.setToggleState (live && mac::launchAtLogin(), juce::dontSendNotification);
    loginBtn.onClick = [this]
    {
        const bool want = loginBtn.getToggleState();
        if (! mac::setLaunchAtLogin (want))
        {
            message = u8 ("ログイン時の起動を変えられませんでした（システム設定 → 一般 → ログイン項目 で設定できます）");
            repaint();
        }
        loginBtn.setToggleState (mac::launchAtLogin(), juce::dontSendNotification);
    };

    setSize (W, H);
    refreshRecent();

    if (live)
    {
        hud = std::make_unique<Hud>();
        hud->setSize (340, 62);
        hud->setOpaque (false);

        registerHotKey();
        mac::setStatusIcon (0);
        mac::listenCommands ([this] (const std::string& c) { command (juce::String (c)); });
        mac::setNotificationHandler ([this] (const std::string& id)
        {
            const juce::File f (juce::String::fromUTF8 (id.c_str()));
            if (cur.valid && f == cur.folder) revealMidi();
            else if (f.isDirectory()) { loadIdea (f); showMe (true); }
        });
        refreshMenu();
        devices.addAudioCallback (this);
        if (prerollBtn.getToggleState()) setPreroll (true);
        startTimerHz (30);
    }
    refreshButtons();
}

MainComponent::~MainComponent()
{
    stopTimer();
    editor = nullptr;
    player.stop();
    recording = false;
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    if (hud != nullptr) hud->removeFromDesktop();
    props->saveIfNeeded();
}

//==============================================================================
juce::String MainComponent::hotKeyLabel() const
{
    const auto& c = mac::hotKeyChoices();
    const int i = juce::jlimit (0, (int) c.size() - 1, props->getIntValue ("hotkey", 0));
    return u8 (c[(size_t) i].label);
}

void MainComponent::registerHotKey()
{
    const bool ok = mac::registerHotKey (props->getIntValue ("hotkey", 0), [this] { toggleRecord(); });
    if (! ok) message = hotKeyLabel() + u8 (" はほかのアプリが使っているようです。下の「録音キー」で別のキーを選んでください");
    hotkeyBtn.setButtonText (u8 ("録音キー: ") + hotKeyLabel());
    refreshMenu();
    repaint();
}

void MainComponent::refreshMenu()
{
    if (! live) return;
    std::vector<std::pair<std::string, int>> items;
    const auto hk = hotKeyLabel().toStdString();
    const bool busy = state == Recording || state == Starting;
    items.push_back ({ (busy ? "録音を止める（" : "録音する（") + hk + "）", 1 });
    if (busy) items.push_back ({ "テンポをたたく（スペース）", 7 });
    if (! busy && prerollBtn.getToggleState()) items.push_back ({ "直前を残す（さかのぼり）", 6 });
    if (! busy && cur.valid) items.push_back ({ "このアイデアに重ねて録る", 5 });
    items.push_back ({ "画面を出す", 2 });
    if (cur.valid) items.push_back ({ "できた MIDI を Finder で表示", 8 });
    if (cur.valid) items.push_back ({ "できた MIDI を出す（Logic へドラッグ）", 3 });
    items.push_back ({ "保存フォルダを開く", 4 });
    items.push_back ({ "", 0 });
    items.push_back ({ "Idea Catcher を終了", 9 });
    mac::setStatusMenu (items, [this] (int tag)
    {
        switch (tag)
        {
            case 1: toggleRecord(); break;
            case 2: showMe (true); break;
            case 3: showMe (true); break;
            case 4: ideasRoot().createDirectory(); ideasRoot().revealToUser(); break;
            case 5: command ("overdub"); break;
            case 6: command ("catch"); break;
            case 7: tap(); break;
            case 8: revealMidi(); break;
            case 9: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
    });
}

void MainComponent::command (const juce::String& c)
{
    const bool idle = state == Idle || state == Done;
    if (c == "toggle") toggleRecord();
    else if (c == "start") { if (idle) startRecording (Normal); }
    else if (c == "stop") { if (state == Recording) stopRecording(); }
    else if (c == "show") showMe (true);
    else if (c == "open") showMe (true);   // 1.4.0 から Logic は開かない。画面を出して、MIDI をドラッグしてもらう
    else if (c == "tap") tap();
    else if (c == "edit") { if (editor != nullptr) closeEditor(); else { showMe (true); openEditor(); } }
    else if (c == "catch") { if (idle) startRecording (Catch); }
    else if (c == "overdub")
    {
        if (state == Recording) stopRecording();
        else if (idle && cur.valid) startRecording (Overdub);
        else if (idle) report (u8 ("重ねる元のアイデアがありません。先に 1 つ録ってください"), true);
    }
}

void MainComponent::showMe (bool focus)
{
    if (onShowRequest) onShowRequest (false, focus);
}

void MainComponent::setState (State s)
{
    state = s;
    if (s != Done) closeEditor();
    if (live) mac::setStatusIcon (s == Recording || s == Starting ? 1 : s == Analysing ? 2 : 0);
    refreshMenu();
    refreshButtons();
    updateHud();
    repaint();
}

//==============================================================================
// HUD: 画面（このアプリ）が前に出ていないときだけ、画面の上に出す
void MainComponent::updateHud()
{
    if (hud == nullptr) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool busy = state == Starting || state == Recording || state == Analysing;
    const bool windowFront = juce::Process::isForegroundProcess() && isWindowVisible && isWindowVisible();
    const bool want = props->getBoolValue ("hud", true) && ! windowFront && (busy || now < hudUntil);
    if (! want)
    {
        if (hud->isOnDesktop()) { hud->setVisible (false); hud->removeFromDesktop(); }
        return;
    }
    if (busy)
    {
        hud->meter = -1;
        if (state == Starting) { hud->big = u8 ("マイクを開いています…"); hud->small = {}; hud->accent = red; }
        else if (state == Analysing) { hud->big = u8 ("MIDI にしています…"); hud->small = u8 ("キーとテンポも調べています"); hud->accent = amber; }
        else
        {
            const auto w = ringW.load();
            const bool counting = ses.gridAbs >= 0 && w < ses.gridAbs;
            if (counting)
            {
                const int left = (int) std::ceil ((double) (ses.gridAbs - w) / clickPeriod.load());
                hud->big = u8 ("カウント  ") + juce::String (juce::jlimit (1, 4, left));
                hud->small = juce::String ((int) std::lround (ses.bpm)) + u8 (" BPM · 次の 1 拍目から");
            }
            else
            {
                const int sec = (int) (recN.load() / recFs);
                hud->big = juce::String (ses.mode == Overdub ? u8 ("重ね録り中  ") : u8 ("録音中  "))
                         + juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2);
                const auto tg = idea::tapGrid (ses.taps);
                hud->small = hotKeyLabel() + u8 (" で止める · ")
                           + (ses.mode == Overdub || ses.countIn ? juce::String ((int) std::lround (ses.bpm)) + " BPM"
                              : tg.ok ? u8 ("タップ ") + juce::String ((int) tg.bpm) + " BPM"
                              : props->getBoolValue ("tapKey", true) ? u8 ("スペースでテンポをたたける") : juce::String());
            }
            hud->accent = red;
            const float db = 20.0f * std::log10 (meter + 1e-6f);
            hud->meter = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        }
    }
    if (! hud->isOnDesktop())
    {
        int x = 0, y = 0;
        mac::hudPosition (hud->getWidth(), x, y);
        hud->setTopLeftPosition (x, y);
        hud->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks);
        hud->setVisible (true);
        if (auto* peer = hud->getPeer()) mac::makeOverlay (peer->getNativeHandle());
    }
    hud->repaint();
}

void MainComponent::flashHud (const juce::String& big, const juce::String& small, juce::Colour c, double seconds)
{
    if (hud == nullptr) return;
    hud->big = big; hud->small = small; hud->accent = c; hud->meter = -1;
    hudUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    updateHud();
}

void MainComponent::report (const juce::String& text, bool important)
{
    message = text;
    repaint();
    if (important) flashHud ("Idea Catcher", text, amber, 5.0);
}

//==============================================================================
void MainComponent::toggleRecord()
{
    if (state == Recording) stopRecording();
    else if (state == Idle || state == Done) startRecording (Normal);
}

void MainComponent::startRecording (Mode m)
{
    if (m == Overdub && ! cur.valid) return;
    if (m == Catch && ! prerollBtn.getToggleState())
    {
        report (u8 ("「さかのぼり録音」を ON にすると、押す前の声も残せます"), true);
        return;
    }
    message.clear();
    setState (Starting);
    mac::requestMic ([sp = juce::Component::SafePointer<MainComponent> (this), m] (bool ok)
    {
        if (sp == nullptr) return;
        if (! ok)
        {
            sp->setState (sp->cur.valid ? Done : Idle);
            sp->report (u8 ("マイクが使えません。システム設定 → プライバシーとセキュリティ → マイク で Idea Catcher を ON にしてください"), true);
            return;
        }
        if (! sp->ensureDevice()) return;
        sp->beginCapture (m);
    });
}

bool MainComponent::ensureDevice()
{
    // さかのぼり用のバッファは、音の処理を止めてから作り直す（処理中に作り直すと落ちる）
    auto prepare = [this]
    {
        const double fs = devices.getCurrentAudioDevice()->getCurrentSampleRate();
        const auto want = (size_t) (fs * ringSeconds);
        recFs = fs;
        if (ring.size() == want) return;
        devices.removeAudioCallback (this);
        recording = false;
        ring.assign (want, 0.0f);
        ringW = 0;
        devices.addAudioCallback (this);
    };
    // マイクは Mac 本体に固定（Bluetooth のマイクを開くと、AirPods などが電話用の低い音質になる）
    auto useBuiltIn = [this]
    {
        if (! props->getBoolValue ("builtInMic", true)) return;
        const auto name = juce::String::fromUTF8 (mac::builtInMicName().c_str());
        if (name.isEmpty()) return;
        auto setup = devices.getAudioDeviceSetup();
        if (setup.inputDeviceName == name) return;
        setup.inputDeviceName = name;
        setup.useDefaultInputChannels = true;
        devices.setAudioDeviceSetup (setup, true);
    };
    if (devices.getCurrentAudioDevice() != nullptr) { useBuiltIn(); if (devices.getCurrentAudioDevice() == nullptr) return false; prepare(); return true; }
    juce::String err;
    if (! devicesInited)
    {
        auto xml = props->getXmlValue ("audio");
        err = devices.initialise (2, 2, xml.get(), true);
        devicesInited = true;
    }
    else
        devices.restartLastAudioDevice();
    if (devices.getCurrentAudioDevice() != nullptr) useBuiltIn();
    if (devices.getCurrentAudioDevice() == nullptr)
    {
        setState (cur.valid ? Done : Idle);
        report (u8 ("マイク（入力）が見つかりません。「入力…」で選んでください") + (err.isNotEmpty() ? " (" + err + ")" : juce::String()), true);
        return false;
    }
    prepare();
    return true;
}

void MainComponent::setPreroll (bool on)
{
    props->setValue ("preroll", on);
    if (on)
    {
        mac::requestMic ([sp = juce::Component::SafePointer<MainComponent> (this)] (bool ok)
        {
            if (sp == nullptr) return;
            if (! ok || ! sp->ensureDevice())
            {
                sp->prerollBtn.setToggleState (false, juce::dontSendNotification);
                sp->props->setValue ("preroll", false);
                if (! ok) sp->report (u8 ("マイクが使えないので、さかのぼり録音はできません"), true);
                return;
            }
            sp->refreshMenu();
            sp->repaint();
        });
    }
    else if (state != Recording && state != Starting)
        devices.closeAudioDevice();
    refreshMenu();
}

// 押した所から前を見て、声のかたまりの始まりを探す（2 秒黙っていた所で区切る。直前 5 秒に声がなければ何もしない）
int64_t MainComponent::findPreStart (int64_t S) const
{
    const auto R = (int64_t) ring.size();
    if (R == 0) return S;
    const int64_t blk = std::max<int64_t> (1, (int64_t) (recFs * 0.01));
    const int64_t maxBack = std::min<int64_t> (S, (int64_t) (recFs * 30.0));
    int64_t silence = 0, start = S;
    bool found = false;
    for (int64_t back = 0; back + blk <= maxBack; back += blk)
    {
        float pk = 0;
        for (int64_t a = S - back - blk; a < S - back; ++a) pk = std::max (pk, std::abs (ring[(size_t) (a % R)]));
        if (pk > loudThreshold) { found = true; silence = 0; start = S - back - blk; }
        else
        {
            silence += blk;
            if (! found && silence > (int64_t) (recFs * 5.0)) break;
            if (found && silence >= (int64_t) (recFs * 2.0)) break;
        }
    }
    if (! found) return S;
    return std::max (S - maxBack, start - (int64_t) (recFs * 0.2));
}

void MainComponent::beginCapture (Mode m)
{
    auto* dev = devices.getCurrentAudioDevice();
    const size_t cap = (size_t) (recFs * maxSeconds);
    if (rec.size() != cap) rec.assign (cap, 0.0f);

    ses = {};
    ses.mode = m;
    ses.latency = dev->getOutputLatencyInSamples() + dev->getInputLatencyInSamples();
    ses.countIn = m == Overdub || (m == Normal && props->getBoolValue ("countIn", false));
    ses.bpm = m == Overdub ? cur.o.bpm : countBpm();

    const int64_t S = ringW.load();
    int64_t from = S;
    if (prerollBtn.getToggleState() && (m == Catch || (m == Normal && ! ses.countIn)))
        from = findPreStart (S);
    if (m == Catch && from >= S)
    {
        setState (cur.valid ? Done : Idle);
        report (u8 ("直前 5 秒に声がありませんでした"), true);
        return;
    }
    // 直前の声は、そのまま rec の頭に写す（押したあとの分は音の処理のスレッドが続きを写す）
    const auto R = (int64_t) ring.size();
    for (int64_t a = from; a < S; ++a) rec[(size_t) (a - from)] = ring[(size_t) (a % R)];
    recN = S - from;
    lastLoud = from < S ? S - from : -1;
    copyPos = S;
    ses.from0 = from;
    level = 0; meter = 0;

    if (ses.countIn)
    {
        const double period = recFs * 60.0 / ses.bpm;
        const int64_t cs = S + (int64_t) (recFs * 0.25);
        clickPeriod = period;
        clickBeats = props->getBoolValue ("clickDuring", false) && m == Normal ? -1 : 4;
        clickStart = cs;
        ses.gridAbs = cs + (int64_t) std::llround (4 * period);
    }
    if (m == Overdub)
    {
        buildPlayback();
        playStart = ses.gridAbs;
        playing = ! play.empty();
    }
    recording = true;
    if (props->getBoolValue ("tapKey", true) && m != Overdub && ! ses.countIn)
        mac::registerExtraHotKey (1, mac::kSpaceKey, 0, [this] { tap(); });
    setState (Recording);
    if (m == Catch) stopRecording();
}

// 重ね録りで流す音（今までの声を 1 小節目の頭から、小節の切れ目でループ）
void MainComponent::buildPlayback()
{
    play.clear();
    if (! cur.valid) return;
    const double bpm = cur.o.bpm, barSec = 240.0 / bpm;
    double lastBeat = 4;
    for (auto& L : cur.layers)
        for (auto& p : idea::place (L, cur.o)) lastBeat = std::max (lastBeat, p.b1);
    const double bars = std::ceil (lastBeat / 4.0 - 0.05);
    play.assign ((size_t) (bars * barSec * recFs), 0.0f);
    for (size_t i = 0; i < cur.layers.size() && i < (size_t) cur.wavs.size(); ++i)
    {
        std::vector<float> x; double fs = 48000;
        if (! readWav (cur.wavs[(int) i], x, fs)) continue;
        x = resample (x, fs, recFs);
        const auto g = (int64_t) std::llround (idea::effectiveGrid (cur.layers[i], bpm) * recFs);
        for (size_t k = 0; k < play.size(); ++k)
        {
            const int64_t s = g + (int64_t) k;
            if (s >= 0 && s < (int64_t) x.size()) play[k] += x[(size_t) s] * 0.8f;
        }
    }
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                      int n, const juce::AudioIODeviceCallbackContext&)
{
    const auto R = (int64_t) ring.size();
    const int64_t a0 = ringW.load();
    const int64_t cs = clickStart.load(), ps = playStart.load();
    const double period = clickPeriod.load();
    const int beats = clickBeats.load();
    const bool pl = playing.load() && ! play.empty();
    const int clickLen = (int) (recFs * 0.035);
    float pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const int64_t a = a0 + i;
        float v = 0;
        for (int c = 0; c < numIn; ++c) if (in[c] != nullptr) v += in[c][i];
        if (R > 0) ring[(size_t) (a % R)] = v;
        pk = std::max (pk, std::abs (v));

        float o = 0;
        if (cs >= 0 && a >= cs)
        {
            const double d = (double) (a - cs);
            const int beat = (int) (d / period);
            const double ph = d - beat * period;
            if ((beats < 0 || beat < beats) && ph < clickLen)
            {
                const double f = beat % 4 == 0 ? 1760.0 : 1320.0;
                o += (float) (0.35 * std::exp (-ph / (recFs * 0.008)) * std::sin (juce::MathConstants<double>::twoPi * f * ph / recFs));
            }
        }
        if (pl && ps >= 0 && a >= ps) o += play[(size_t) ((a - ps) % (int64_t) play.size())];
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) out[c][i] = o;
    }
    ringW.store (a0 + n);
    level.store (std::max (level.load() * 0.85f, pk));

    if (! recording.load() || R == 0) return;
    int64_t w = recN.load(), from = std::max (copyPos.load(), a0 + n - R);
    float rpk = 0;
    for (int64_t a = from; a < a0 + n && w < (int64_t) rec.size(); ++a, ++w)
    {
        const float v = ring[(size_t) (a % R)];
        rec[(size_t) w] = v;
        rpk = std::max (rpk, std::abs (v));
    }
    copyPos.store (a0 + n);
    recN.store (w);
    if (rpk > loudThreshold) lastLoud.store (w);
}

void MainComponent::tap()
{
    if (state != Recording || ses.mode == Overdub || ses.countIn) return;
    const int inLat = devices.getCurrentAudioDevice() != nullptr ? devices.getCurrentAudioDevice()->getInputLatencyInSamples() : 0;
    ses.taps.push_back ((double) (recN.load() + inLat) / recFs);
    repaint (recArea);
    updateHud();
}

void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (state != Recording)
    {
        if (state == Analysing || state == Starting) { repaint (recArea); updateHud(); }
        else if (hud != nullptr && hud->isOnDesktop() && now >= hudUntil) updateHud();
        return;
    }
    meter = std::max (meter * 0.88f, level.load());
    level = level.load() * 0.7f;
    const auto n = recN.load(), loud = lastLoud.load();
    const auto fs = (int64_t) recFs;
    const bool counting = ses.gridAbs >= 0 && ringW.load() < ses.gridAbs;
    if (n >= (int64_t) rec.size()) { stopRecording(); return; }
    if (! counting && autoStopBtn.getToggleState() && loud >= 0 && n > fs * 3 / 2 && n - loud > (int64_t) (recFs * autoStopSeconds))
    {
        stopRecording();
        return;
    }
    if (loud < 0 && n > fs * 20)
    {
        stopRecording();   // 20 秒なにも聞こえなければやめる
        return;
    }
    repaint (recArea);
    updateHud();
}

void MainComponent::stopRecording()
{
    recording = false;
    playing = false;
    clickStart = -1;
    mac::registerExtraHotKey (1, -1, 0, nullptr);
    if (! prerollBtn.getToggleState()) devices.closeAudioDevice();   // マイクを使っている印（オレンジの点）を消す
    const auto n = recN.load(), loud = lastLoud.load();
    const Mode m = ses.mode;
    if (loud < 0)
    {
        setState (cur.valid ? Done : Idle);
        report (u8 ("声が聞こえませんでした。「入力…」でマイクを確かめてください"), true);
        return;
    }
    const auto end = std::min<int64_t> (n, loud + (int64_t) (recFs * 0.6));
    std::vector<float> samples (rec.begin(), rec.begin() + (long) end);
    setState (Analysing);

    idea::AnalyseOptions ao;
    ao.fallbackBpm = props->getDoubleValue ("lastBpm", 100.0);
    double gridT0 = -1; int gridStep = 0;
    if (m == Overdub)
    {
        ao.fixedBpm = cur.o.bpm;
        if (hasMelodyLayer()) { ao.fixedKey = true; ao.key = cur.o.key; ao.minor = cur.o.minor; }
        gridT0 = (double) (ses.gridAbs + ses.latency - ses.from0) / recFs;
    }
    else if (ses.countIn)
    {
        ao.fixedBpm = ses.bpm;
        gridT0 = (double) (ses.gridAbs + ses.latency - ses.from0) / recFs;
        gridStep = 4;
    }
    else if (const auto tg = idea::tapGrid (ses.taps); tg.ok)
    {
        ao.fixedBpm = tg.bpm;
        gridT0 = tg.t0;
        gridStep = 1;
    }

    const double fs = recFs;
    const int noise = props->getIntValue ("noise", 1);
    juce::Thread::launch ([sp = juce::Component::SafePointer<MainComponent> (this), samples = std::move (samples), fs, ao, gridT0, gridStep, m, noise]() mutable
    {
        nr::reduce (samples, fs, noise);   // 保存する声にもかかる
        auto r = idea::analyse (samples, fs, ao);
        r.gridT0 = gridT0; r.gridStep = gridStep;
        juce::MessageManager::callAsync ([sp, r = std::move (r), samples = std::move (samples), fs, m]() mutable
        {
            if (sp != nullptr) sp->finishAnalysis (std::move (r), std::move (samples), fs, m);
        });
    });
}

void MainComponent::finishAnalysis (idea::Result r, std::vector<float> samples, double fs, Mode m)
{
    if (! r.heard || r.melody.empty())
    {
        setState (cur.valid ? Done : Idle);
        report (u8 ("音にできるものがありませんでした。もう少し大きく、はっきりめに"), true);
        return;
    }
    Idea next;
    if (m == Overdub && cur.valid)
    {
        next = cur;
        // 前のレイヤーの小節の頭を、今の位置で固定する（あとでテンポを変えても、重ねたものとずれない）
        for (auto& L : next.layers) { L.r.gridT0 = idea::effectiveGrid (L, next.o.bpm); L.r.gridStep = 0; }
        const auto wav = next.folder.getChildFile (layerFileName ((int) next.layers.size()));
        writeWav (wav, samples, fs);
        next.wavs.add (wav);
        next.snapKey.push_back (r.key.key); next.snapMinor.push_back (r.key.minor ? 1 : 0);
        next.layers.push_back ({ r, idea::kMelody });
    }
    else
    {
        next.folder = ideasRoot().getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
        next.keepCopy = true;
        next.folder.createDirectory();
        const auto wav = next.folder.getChildFile (layerFileName (0));   // 元の声もとっておく
        writeWav (wav, samples, fs);
        next.wavs.add (wav);
        next.o.key = r.key.key; next.o.minor = r.key.minor; next.o.bpm = r.bpm;
        // クオンタイズ：テンポが分かっている録音（カウントイン・タップ）だけ最初から入れる。
        // 自動で調べたテンポは外れていることがあるので、最初は「なし」（外れたテンポでそろえると全部ずれる）
        const bool tempoKnown = r.gridT0 >= 0 && r.gridStep != 0;
        next.o.quantize = tempoKnown ? props->getIntValue ("quantKnown", 3) : 0;
        next.o.strength = (float) props->getDoubleValue ("strength", 0.75);
        next.snapKey.push_back (r.key.key); next.snapMinor.push_back (r.key.minor ? 1 : 0);
        next.layers.push_back ({ r, idea::kMelody });
    }
    next.valid = true;
    applyIdea (std::move (next));
    props->setValue ("lastBpm", cur.o.bpm);
    message.clear();

    const auto& L = cur.layers.back();
    const auto summary = shortKey (cur.o.key, cur.o.minor) + u8 (" · ")
                       + juce::String ((int) std::lround (cur.o.tempoOut())) + u8 (" BPM · ")
                       + juce::String ((int) idea::includedNotes (L.r, L.kind).size()) + u8 (" 音")
                       + (cur.layers.size() > 1 ? u8 (" · 重ね ") + juce::String ((int) cur.layers.size()) : juce::String());
    {
        const auto where = cur.copy.existsAsFile() ? (props->getIntValue ("copyTo", 1) == 2 ? u8 ("デスクトップ") : u8 ("ダウンロード")) : u8 ("ミュージック → Idea Catcher");
        flashHud (u8 ("MIDI ができました  ") + summary, u8 ("画面から Logic へドラッグ（") + where + u8 (" にも保存）"), green, 5.0);
        if (live && ! (juce::Process::isForegroundProcess() && isWindowVisible && isWindowVisible()))
            mac::notify ("Idea Catcher", (u8 ("MIDI ができました: ") + summary + u8 ("。") + where + u8 (" に保存しました（クリックで Finder に表示）")).toStdString(),
                         cur.folder.getFullPathName().toStdString());
    }
}

void MainComponent::applyIdea (Idea next)
{
    const bool sameIdea = cur.valid && next.folder == cur.folder;
    cur = std::move (next);
    if (! sameIdea) undoStack.clear();
    writeMidi();
    saveInfo();
    refreshRecent();
    setState (Done);
}

bool MainComponent::hasMelodyLayer() const
{
    for (auto& L : cur.layers) if (! L.r.melody.empty()) return true;
    return false;
}

void MainComponent::removeLastLayer()
{
    if (! cur.valid || cur.layers.size() < 2 || state != Done) return;
    auto next = cur;
    next.wavs.getLast().deleteFile();
    next.wavs.removeLast();
    next.layers.pop_back();
    next.snapKey.pop_back(); next.snapMinor.pop_back();
    applyIdea (std::move (next));
}

void MainComponent::loadIdea (const juce::File& folder, bool ignoreEdits)
{
    if (state == Recording || state == Starting || state == Analysing) return;
    const auto saved = juce::JSON::parse (folder.getChildFile ("info.json"));
    auto* obj = saved.getDynamicObject();
    struct Job { juce::File wav; int kind; double gridT0; int gridStep; int snapKey; bool snapMinor; std::vector<idea::Note> notes; bool edited; };
    std::vector<Job> jobs;
    const int key = obj != nullptr ? (int) obj->getProperty ("key") : 0;
    const bool minor = obj != nullptr && (bool) obj->getProperty ("minor");
    if (obj != nullptr && obj->getProperty ("layers").isArray())
    {
        for (auto& v : *obj->getProperty ("layers").getArray())
            if (auto* l = v.getDynamicObject())
            {
                Job j { folder.getChildFile (l->getProperty ("file").toString()), (int) l->getProperty ("kind"),
                        (double) l->getProperty ("gridT0"), (int) l->getProperty ("gridStep"),
                        l->hasProperty ("snapKey") ? (int) l->getProperty ("snapKey") : key,
                        l->hasProperty ("snapMinor") ? (bool) l->getProperty ("snapMinor") : minor, {}, false };
                if (j.kind == idea::kDrums) continue;   // 1.4.0 から口ドラムはなし
                // 手直しした音（[秒, 秒, ノート, 強さ] の並び）
                if (! ignoreEdits && l->getProperty ("notes").isArray())
                {
                    j.edited = true;
                    for (auto& nv : *l->getProperty ("notes").getArray())
                        if (nv.isArray() && nv.size() >= 4)
                            j.notes.push_back ({ (double) nv[0], (double) nv[1], (int) nv[2], (int) nv[3], 0 });
                }
                jobs.push_back (std::move (j));
            }
    }
    else   // 1.0.0 の形
        jobs.push_back ({ folder.getChildFile ("Voice.wav"), obj != nullptr ? (int) obj->getProperty ("kind") : -1, -1.0, 0, key, minor, {}, false });
    if (jobs.empty() || ! jobs[0].wav.existsAsFile())
    {
        report (u8 ("このアイデアの録音が読めませんでした"), false);
        return;
    }
    Idea base;
    base.folder = folder;
    base.o.key = key; base.o.minor = minor;
    base.o.bpm = obj != nullptr ? (double) obj->getProperty ("bpm") : 100.0;
    base.o.outBpm = obj != nullptr && obj->hasProperty ("outBpm") ? (double) obj->getProperty ("outBpm") : 0.0;
    if (obj != nullptr && obj->hasProperty ("copy"))
    {
        base.copy = juce::File (obj->getProperty ("copy").toString());
        base.keepCopy = base.copy.existsAsFile();   // 動かしたり消したりしたものは、もう作り直さない
    }
    // クオンタイズ（1.3 までの "quantize" は 1 = 1/16、2 = 1/8）
    if (obj != nullptr && obj->hasProperty ("quant2")) base.o.quantize = juce::jlimit (0, 4, (int) obj->getProperty ("quant2"));
    else { const int q = obj != nullptr ? (int) obj->getProperty ("quantize") : 0; base.o.quantize = q == 1 ? 3 : q == 2 ? 2 : 0; }
    base.o.strength = obj != nullptr && obj->hasProperty ("strength") ? (float) (double) obj->getProperty ("strength") : 1.0f;
    const bool hasSaved = obj != nullptr;

    setState (Analysing);
    juce::Thread::launch ([sp = juce::Component::SafePointer<MainComponent> (this), jobs, base, hasSaved]() mutable
    {
        for (auto& j : jobs)
        {
            std::vector<float> x; double fs = 48000;
            if (! readWav (j.wav, x, fs)) continue;
            idea::AnalyseOptions ao;
            if (hasSaved) { ao.fixedBpm = base.o.bpm; ao.fixedKey = true; ao.key = j.snapKey; ao.minor = j.snapMinor; }
            auto r = idea::analyse (std::move (x), fs, ao);
            r.gridT0 = j.gridT0; r.gridStep = j.gridStep;
            if (j.edited) r.melody = j.notes;
            if (! hasSaved && base.layers.empty()) { base.o.key = r.key.key; base.o.minor = r.key.minor; base.o.bpm = r.bpm; }
            base.layers.push_back ({ r, idea::kMelody, j.edited });
            base.wavs.add (j.wav);
            base.snapKey.push_back (r.key.key); base.snapMinor.push_back (r.key.minor ? 1 : 0);
        }
        juce::MessageManager::callAsync ([sp, base = std::move (base)]() mutable
        {
            if (sp == nullptr) return;
            sp->message.clear();
            if (base.layers.empty()) { sp->setState (sp->cur.valid ? Done : Idle); sp->report (u8 ("このアイデアの録音が読めませんでした"), false); return; }
            base.valid = true;
            sp->applyIdea (std::move (base));
        });
    });
}

bool MainComponent::loadForSnapshot (const juce::File& wav)
{
    std::vector<float> x; double fs = 48000;
    if (! readWav (wav, x, fs)) return false;
    auto r = idea::analyse (x, fs);
    Idea next;
    next.folder = wav.getParentDirectory().getChildFile ("IdeaCatcherSnapshot");
    next.o.key = r.key.key; next.o.minor = r.key.minor; next.o.bpm = r.bpm;
    next.snapKey.push_back (r.key.key); next.snapMinor.push_back (r.key.minor ? 1 : 0);
    next.wavs.add (wav);
    next.layers.push_back ({ r, idea::kMelody });
    next.o.quantize = 3; next.o.strength = 0.75f;
    next.valid = true;
    applyIdea (std::move (next));
    return true;
}

//==============================================================================
juce::String MainComponent::shortKey (int key, bool minor) { return u8 (idea::keyName (key)) + (minor ? "m" : ""); }

juce::String MainComponent::keyLabel() const
{
    return u8 (idea::keyName (cur.o.key)) + (cur.o.minor ? u8 (" マイナー") : u8 (" メジャー"));
}

double MainComponent::countBpm() const
{
    const double b = props->getDoubleValue ("countBpm", 0.0);
    return b > 0 ? b : props->getDoubleValue ("lastBpm", 100.0);
}

void MainComponent::writeMidi()
{
    if (! cur.valid) return;
    cur.folder.createDirectory();
    for (auto& f : cur.folder.findChildFiles (juce::File::findFiles, false, "*.mid")) f.deleteFile();
    // Logic のプロジェクト名にもなる
    const auto stamp = cur.folder.getFileName().substring (5, 16);   // "11-05 15.30"
    const auto name = juce::File::createLegalFileName ("Idea " + stamp + " " + shortKey (cur.o.key, cur.o.minor)
                                                        + " " + juce::String ((int) std::lround (cur.o.tempoOut())) + "bpm");
    cur.mid = cur.folder.getChildFile (name + ".mid");
    const auto bytes = idea::buildMidiFile (cur.layers, cur.o);
    cur.mid.replaceWithData (bytes.data(), bytes.size());

    // 見つけやすいよう、ダウンロード（設定で変えられる）にもコピーを置く。名前が変わったら前のコピーは消す
    const auto dest = copyFolder();
    if (cur.keepCopy && dest != juce::File())
    {
        const auto to = dest.getChildFile (cur.mid.getFileName());
        if (cur.copy.existsAsFile() && cur.copy != to) cur.copy.deleteFile();
        cur.mid.copyFileTo (to);
        cur.copy = to;
    }
}

juce::File MainComponent::copyFolder() const
{
    const int c = props->getIntValue ("copyTo", 1);
    return c == 1 ? juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads")
         : c == 2 ? juce::File::getSpecialLocation (juce::File::userDesktopDirectory) : juce::File();
}

void MainComponent::revealMidi()
{
    if (! cur.valid) { ideasRoot().createDirectory(); ideasRoot().revealToUser(); return; }
    (cur.copy.existsAsFile() ? cur.copy : cur.mid.existsAsFile() ? cur.mid : cur.folder).revealToUser();
}

void MainComponent::saveInfo()
{
    if (! cur.valid) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", cur.o.key);
    o->setProperty ("minor", cur.o.minor);
    o->setProperty ("bpm", cur.o.bpm);
    o->setProperty ("outBpm", cur.o.outBpm);
    if (cur.copy != juce::File()) o->setProperty ("copy", cur.copy.getFullPathName());
    o->setProperty ("kind", cur.layers.empty() ? 0 : cur.layers[0].kind);
    o->setProperty ("quant2", cur.o.quantize);
    o->setProperty ("strength", (double) cur.o.strength);
    double secs = 0; int notes = 0;
    juce::Array<juce::var> layers;
    for (size_t i = 0; i < cur.layers.size(); ++i)
    {
        const auto& L = cur.layers[i];
        secs = std::max (secs, L.r.seconds);
        notes += (int) idea::includedNotes (L.r, L.kind).size();
        auto* l = new juce::DynamicObject();
        l->setProperty ("file", i < (size_t) cur.wavs.size() ? cur.wavs[(int) i].getFileName() : layerFileName ((int) i));
        l->setProperty ("kind", L.kind);
        l->setProperty ("gridT0", L.r.gridT0);
        l->setProperty ("gridStep", L.r.gridStep);
        if (i < cur.snapKey.size()) { l->setProperty ("snapKey", cur.snapKey[i]); l->setProperty ("snapMinor", cur.snapMinor[i] != 0); }
        if (L.edited)
        {
            juce::Array<juce::var> ns;
            for (auto& n : L.r.melody) ns.add (juce::Array<juce::var> { n.s, n.e, n.n, n.vel });
            l->setProperty ("notes", ns);
        }
        layers.add (juce::var (l));
    }
    o->setProperty ("layers", layers);
    o->setProperty ("seconds", secs);
    o->setProperty ("notes", notes);
    if (! cur.layers.empty())
    {
        o->setProperty ("keyConfidence", cur.layers[0].r.key.confidence);
        o->setProperty ("tempoConfidence", cur.layers[0].r.tempoConf);
    }
    cur.folder.getChildFile ("info.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

// Logic に入れる声: 全部のレイヤーを、1 小節目の頭がファイルの頭に来るようにそろえて 1 本に
juce::File MainComponent::writeLogicVoice()
{
    const auto out = cur.folder.getChildFile ("Voice for Logic.wav");
    std::vector<float> mix;
    double outFs = 0;
    for (size_t i = 0; i < cur.layers.size() && i < (size_t) cur.wavs.size(); ++i)
    {
        std::vector<float> x; double fs = 48000;
        if (! readWav (cur.wavs[(int) i], x, fs)) continue;
        if (outFs == 0) outFs = fs;
        x = resample (x, fs, outFs);
        const auto g = (int64_t) std::llround (idea::effectiveGrid (cur.layers[i], cur.o.bpm) * outFs);
        const auto len = (int64_t) x.size() - g;
        if (len > (int64_t) mix.size()) mix.resize ((size_t) len, 0.0f);
        for (int64_t k = 0; k < len; ++k)
        {
            const int64_t s = g + k;
            if (s >= 0 && s < (int64_t) x.size()) mix[(size_t) k] += x[(size_t) s];
        }
    }
    if (mix.empty()) return {};
    float pk = 0;
    for (auto v : mix) pk = std::max (pk, std::abs (v));
    if (pk > 0.98f) for (auto& v : mix) v *= 0.98f / pk;
    return writeWav (out, mix, outFs) ? out : juce::File();
}

void MainComponent::editOptions (std::function<void (idea::MidiOptions&)> f)
{
    if (! cur.valid) return;
    const int qBefore = cur.o.quantize;
    f (cur.o);
    // 次の録音の初期値として覚える（テンポが分かっている録音のときだけ）
    const auto& L0 = cur.layers.front();
    if (cur.o.quantize != qBefore && L0.r.gridT0 >= 0 && L0.r.gridStep != 0) props->setValue ("quantKnown", cur.o.quantize);
    props->setValue ("strength", (double) cur.o.strength);
    writeMidi();
    saveInfo();
    refreshRecent();
    refreshButtons();
    repaint();
}

//==============================================================================
void MainComponent::showQuantMenu()
{
    if (! cur.valid) return;
    juce::PopupMenu m;
    for (int q = 0; q <= 4; ++q) m.addItem (1 + q, u8 ("クオンタイズ: ") + u8 (idea::quantizeName (q)), true, cur.o.quantize == q);
    m.addSeparator();
    const int st = juce::roundToInt (cur.o.strength * 100);
    m.addItem (11, u8 ("寄せる強さ 100%（ぴったり）"), true, st == 100);
    m.addItem (12, u8 ("寄せる強さ 75%（おすすめ）"), true, st == 75);
    m.addItem (13, u8 ("寄せる強さ 50%（人っぽさを残す）"), true, st == 50);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (quantBtn), [this] (int r)
    {
        if (r >= 1 && r <= 5) editOptions ([r] (auto& o) { o.quantize = r - 1; });
        else if (r >= 11) editOptions ([r] (auto& o) { o.strength = r == 11 ? 1.0f : r == 12 ? 0.75f : 0.5f; });
    });
}

void MainComponent::openEditor()
{
    if (! cur.valid || state != Done) return;
    editor = std::make_unique<EditPanel> (*this);
    addAndMakeVisible (*editor);
    resized();
    refreshButtons();
    editor->grabKeyboardFocus();
}

void MainComponent::closeEditor()
{
    if (editor == nullptr) return;
    player.stop();
    removeChildComponent (editor.get());
    editor = nullptr;
    refreshButtons();
    repaint();
}

void MainComponent::beginEdit()
{
    undoStack.push_back (cur.layers);
    if (undoStack.size() > 60) undoStack.erase (undoStack.begin());
}

void MainComponent::editDone()
{
    if (! cur.valid) return;
    writeMidi();
    saveInfo();
    refreshRecent();
    repaint();
}

bool MainComponent::undoEdit()
{
    if (undoStack.empty()) return false;
    cur.layers = undoStack.back();
    undoStack.pop_back();
    editDone();
    return true;
}

void MainComponent::revertEdits()
{
    if (! cur.valid) return;
    beginEdit();
    const auto keep = undoStack;
    loadIdea (cur.folder, true);   // 声から作り直す（手直しした音は使わない）
    undoStack = keep;
}

// 試し聴き：MIDI は鼻歌のテンポで鳴らす（声と同じ速さ）。0 秒 = ファイルの頭（弱起があれば 1 小節前）
void MainComponent::preview (bool midi, bool voice)
{
    player.stop();
    if (! cur.valid) return;
    const double spb = 60.0 / cur.o.bpm;
    const double pick = idea::pickupBeats (cur.layers, cur.o);
    std::vector<Player::Note> ns;
    for (auto& L : cur.layers)
        for (auto& p : idea::place (L, cur.o))
            ns.push_back ({ (p.b0 + pick) * spb, (p.b1 + pick) * spb, p.n, p.vel });
    std::vector<float> mix;
    double outFs = 48000;
    if (voice)
    {
        bool first = true;
        for (size_t i = 0; i < cur.layers.size() && i < (size_t) cur.wavs.size(); ++i)
        {
            std::vector<float> x; double fs = 48000;
            if (! readWav (cur.wavs[(int) i], x, fs)) continue;
            if (first) { outFs = fs; first = false; }
            x = resample (x, fs, outFs);
            const auto start = (int64_t) std::llround ((idea::effectiveGrid (cur.layers[i], cur.o.bpm) - pick * spb) * outFs);
            const auto len = (int64_t) x.size() - start;
            if (len > (int64_t) mix.size()) mix.resize ((size_t) len, 0.0f);
            for (int64_t k = 0; k < len; ++k)
            {
                const int64_t si = start + k;
                if (si >= 0 && si < (int64_t) x.size()) mix[(size_t) k] += x[(size_t) si];
            }
        }
        float pk = 0;
        for (auto v : mix) pk = std::max (pk, std::abs (v));
        if (pk > 0) for (auto& v : mix) v *= 0.7f / pk;
    }
    player.play (std::move (ns), std::move (mix), outFs, midi, voice);
}

void MainComponent::refreshRecent()
{
    recent.clear();
    auto dirs = ideasRoot().findChildFiles (juce::File::findDirectories, false);
    std::sort (dirs.begin(), dirs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (auto& d : dirs)
        if (d.getChildFile ("Voice.wav").existsAsFile() && recent.size() < 4) recent.add (d);
}

void MainComponent::refreshButtons()
{
    const bool show = cur.valid && state != Analysing;
    for (juce::Component* b : { (juce::Component*) &editBtn, (juce::Component*) &keyDownBtn, (juce::Component*) &keyUpBtn, (juce::Component*) &modeBtn,
                                (juce::Component*) &bpmMinus, (juce::Component*) &bpmPlus, (juce::Component*) &bpmHalf, (juce::Component*) &bpmDouble,
                                (juce::Component*) &humMinus, (juce::Component*) &humPlus, (juce::Component*) &tempoLabel,
                                (juce::Component*) &quantBtn, (juce::Component*) &dubBtn })
        b->setVisible (show);
    tempoReset.setVisible (show && cur.o.outBpm > 0 && std::abs (cur.o.outBpm - cur.o.bpm) > 0.01);
    if (! tempoLabel.isBeingEdited())
        tempoLabel.setText (juce::String ((int) std::lround (cur.o.tempoOut())) + " BPM", juce::dontSendNotification);
    dubBtn.setEnabled (state == Done);
    tapBtn.setVisible (state == Recording && ses.mode != Overdub && ! ses.countIn);
    finderBtn.setButtonText (cur.valid ? u8 ("MIDI を Finder で表示") : u8 ("保存フォルダ"));
    editBtn.setToggleState (editor != nullptr, juce::dontSendNotification);
    editBtn.setEnabled (state == Done);
    modeBtn.setButtonText (cur.o.minor ? u8 ("マイナー") : u8 ("メジャー"));
    quantBtn.setButtonText (u8 ("クオンタイズ ") + u8 (idea::quantizeName (cur.o.quantize))
                            + (cur.o.quantize > 0 ? "  " + juce::String (juce::roundToInt (cur.o.strength * 100)) + "%" : juce::String()));
    hotkeyBtn.setButtonText (u8 ("録音キー: ") + hotKeyLabel());
    countBtn.setButtonText (props->getBoolValue ("countIn", false)
                                ? u8 ("カウントイン ") + juce::String ((int) std::lround (countBpm())) + " BPM"
                                : u8 ("カウントイン なし"));
    inputBtn.setEnabled (state != Recording && state != Starting);
}

//==============================================================================
void MainComponent::showCountMenu()
{
    juce::PopupMenu m;
    const bool on = props->getBoolValue ("countIn", false);
    const double fixed = props->getDoubleValue ("countBpm", 0.0);
    m.addItem (1, u8 ("なし（テンポは自動で調べる・タップでも）"), true, ! on);
    m.addItem (2, u8 ("前のアイデアのテンポで（") + juce::String ((int) std::lround (props->getDoubleValue ("lastBpm", 100.0))) + " BPM)", true, on && fixed <= 0);
    m.addSeparator();
    for (int b = 60; b <= 160; b += 10) m.addItem (100 + b, juce::String (b) + " BPM", true, on && std::abs (fixed - b) < 0.5);
    m.addSeparator();
    m.addItem (3, u8 ("録音中もクリックを鳴らす（ヘッドホンで）"), true, props->getBoolValue ("clickDuring", false));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (countBtn), [this] (int r)
    {
        if (r == 1) props->setValue ("countIn", false);
        else if (r == 2) { props->setValue ("countIn", true); props->setValue ("countBpm", 0.0); }
        else if (r == 3) props->setValue ("clickDuring", ! props->getBoolValue ("clickDuring", false));
        else if (r > 100) { props->setValue ("countIn", true); props->setValue ("countBpm", (double) (r - 100)); }
        refreshButtons();
    });
}

void MainComponent::showMoreMenu()
{
    juce::PopupMenu m;
    m.addItem (2, u8 ("録音中はスペースでテンポをたたく"), true, props->getBoolValue ("tapKey", true));
    m.addItem (3, u8 ("ほかのアプリを使っているとき、画面の上に様子を出す"), true, props->getBoolValue ("hud", true));
    const auto mic = juce::String::fromUTF8 (mac::builtInMicName().c_str());
    m.addItem (4, u8 ("マイクは Mac 本体に固定（") + (mic.isNotEmpty() ? mic : u8 ("本体のマイクなし")) + u8 ("）"),
               mic.isNotEmpty(), props->getBoolValue ("builtInMic", true));
    m.addSeparator();
    const int ct = props->getIntValue ("copyTo", 1);
    m.addItem (20, u8 ("MIDI のコピーを置く場所: ダウンロード"), true, ct == 1);
    m.addItem (22, u8 ("MIDI のコピーを置く場所: デスクトップ"), true, ct == 2);
    m.addItem (21, u8 ("MIDI のコピーを置かない（ミュージック → Idea Catcher だけ）"), true, ct == 0);
    m.addSeparator();
    const int nz = props->getIntValue ("noise", 1);
    m.addItem (10, u8 ("ノイズ除去: なし"), true, nz == 0);
    m.addItem (11, u8 ("ノイズ除去: ふつう"), true, nz == 1);
    m.addItem (12, u8 ("ノイズ除去: 強め（テンポがずれることも）"), true, nz == 2);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreBtn), [this] (int r)
    {
        if (r >= 20) { props->setValue ("copyTo", r == 20 ? 1 : r == 22 ? 2 : 0); return; }
        if (r >= 10) { props->setValue ("noise", r - 10); return; }
        if (r == 4)
        {
            props->setValue ("builtInMic", ! props->getBoolValue ("builtInMic", true));
            if (devices.getCurrentAudioDevice() != nullptr && state != Recording && state != Starting) ensureDevice();   // 開いていれば今すぐ切り替え
            return;
        }
        const char* k = r == 2 ? "tapKey" : r == 3 ? "hud" : nullptr;
        if (k == nullptr) return;
        props->setValue (k, ! props->getBoolValue (k, true));
    });
}

void MainComponent::showDubMenu()
{
    if (! cur.valid || state != Done) return;
    if (cur.layers.size() < 2) { startRecording (Overdub); return; }
    juce::PopupMenu m;
    m.addItem (1, u8 ("もう 1 つ重ねて録る"));
    m.addItem (2, u8 ("最後に重ねたもの（") + juce::String ((int) cur.layers.size()) + u8 (" 本目）を消す"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dubBtn), [this] (int r)
    {
        if (r == 1) startRecording (Overdub);
        else if (r == 2) removeLastLayer();
    });
}

//==============================================================================
namespace
{
// 入力・出力を選ぶ画面（閉じたら設定を保存。さかのぼり録音でなければマイクを閉じる）
struct InputPanel : juce::Component
{
    juce::AudioDeviceManager& dm;
    juce::PropertiesFile& props;
    juce::AudioDeviceSelectorComponent sel;
    InputPanel (juce::AudioDeviceManager& d, juce::PropertiesFile& p)
        : dm (d), props (p), sel (d, 1, 2, 0, 2, false, false, false, false)
    {
        addAndMakeVisible (sel);
        setSize (460, 300);
    }
    ~InputPanel() override
    {
        if (auto xml = dm.createStateXml()) props.setValue ("audio", xml.get());
        props.saveIfNeeded();
        if (! props.getBoolValue ("preroll", false)) dm.closeAudioDevice();
    }
    void paint (juce::Graphics& g) override { g.fillAll (panel); }
    void resized() override { sel.setBounds (getLocalBounds().reduced (8)); }
};
}

void MainComponent::showInputSettings()
{
    if (state == Recording || state == Starting) return;
    if (devices.getCurrentAudioDevice() == nullptr)
    {
        auto xml = props->getXmlValue ("audio");
        devices.initialise (2, 2, xml.get(), true);
        devicesInited = true;
    }
    juce::DialogWindow::LaunchOptions lo;
    lo.content.setOwned (new InputPanel (devices, *props));
    lo.dialogTitle = u8 ("マイク（入力）とクリックの出力");
    lo.dialogBackgroundColour = panel;
    lo.useNativeTitleBar = true;
    lo.resizable = false;
    lo.componentToCentreAround = this;
    lo.launchAsync();
}

//==============================================================================
void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    header = r.removeFromTop (58);
    recArea = r.removeFromTop (112);
    recCircle = recArea.withSizeKeepingCentre (84, 84).withX (recArea.getX() + 14);
    tapBtn.setBounds (recArea.getRight() - 70, recArea.getBottom() - 40, 56, 28);
    r.removeFromTop (12);
    card = r.removeFromTop (300);
    r.removeFromTop (12);
    actions = r.removeFromTop (40);
    r.removeFromTop (8);
    auto drags = r.removeFromTop (28);
    r.removeFromTop (14);
    recentArea = r.removeFromTop (124);
    footer = r;

    auto c = card.reduced (14, 12);
    auto row = [&] (int h) { auto x = c.removeFromTop (h); c.removeFromTop (8); return x; };
    {
        auto a = row (30);
        a.removeFromLeft (64);
        keyDownBtn.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (120);
        keyUpBtn.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (8);
        modeBtn.setBounds (a.removeFromLeft (84));
    }
    {
        auto a = row (30);   // 曲のテンポ
        a.removeFromLeft (64);
        bpmMinus.setBounds (a.removeFromLeft (30));
        tempoLabel.setBounds (a.removeFromLeft (120).reduced (6, 0));
        bpmPlus.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (8);
        tempoReset.setBounds (a.removeFromLeft (84));
    }
    {
        auto a = row (30);   // 鼻歌のテンポ
        a.removeFromLeft (64);
        humMinus.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (120);
        humPlus.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (8);
        bpmHalf.setBounds (a.removeFromLeft (40));
        a.removeFromLeft (4);
        bpmDouble.setBounds (a.removeFromLeft (40));
    }
    {
        auto a = row (28);
        quantBtn.setBounds (a.removeFromLeft (220));
    }
    roll = c.removeFromTop (100);

    auto ac = actions;
    editBtn.setBounds (ac.removeFromRight (70));
    ac.removeFromRight (8);
    dubBtn.setBounds (ac.removeFromRight (120));
    ac.removeFromRight (8);
    finderBtn.setBounds (ac);
    dragMidi = drags.removeFromLeft (drags.getWidth() / 2 - 4);
    dragWav = drags.removeFromRight (dragMidi.getWidth());

    auto f = footer.reduced (0, 6);
    auto f1 = f.removeFromTop (28);
    hotkeyBtn.setBounds (f1.removeFromLeft (170));
    f1.removeFromLeft (8);
    inputBtn.setBounds (f1.removeFromLeft (80));
    f1.removeFromLeft (8);
    loginBtn.setBounds (f1);
    f.removeFromTop (6);
    auto f2 = f.removeFromTop (28);
    countBtn.setBounds (f2.removeFromLeft (170));
    f2.removeFromLeft (8);
    moreBtn.setBounds (f2.removeFromLeft (80));
    f.removeFromTop (2);
    auto f3 = f.removeFromTop (24);
    autoStopBtn.setBounds (f3);
    prerollBtn.setBounds (f.removeFromTop (24));
    if (editor != nullptr) editor->setBounds (card.getUnion (recentArea));
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // 見出し
    g.setColour (textC);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Idea Catcher", header.withTrimmedTop (10).removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (u8 ("思いつきを、そのまま曲の入口に"), header.withTrimmedTop (38), juce::Justification::topLeft);
    if (prerollBtn.getToggleState())
    {
        g.setColour (red.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (u8 ("● さかのぼり録音中"), header.withTrimmedTop (14).removeFromTop (20), juce::Justification::centredRight);
    }

    // 録音の所
    g.setColour (panel);
    g.fillRoundedRectangle (recArea.toFloat(), 12.0f);
    const bool recNow = state == Recording || state == Starting;
    {
        auto cc = recCircle.toFloat();
        if (state == Recording)
        {
            const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounterHiRes() * 0.006f);
            g.setColour (red.withAlpha (0.18f + 0.15f * pulse));
            g.fillEllipse (cc.expanded (4 + 6 * pulse));
        }
        g.setColour (recNow ? red : panel2);
        g.fillEllipse (cc);
        g.setColour (recNow ? juce::Colours::white : red);
        if (recNow) g.fillRoundedRectangle (cc.withSizeKeepingCentre (26, 26), 4.0f);
        else g.fillEllipse (cc.withSizeKeepingCentre (30, 30));
    }
    auto info = recArea.withTrimmedLeft (recCircle.getRight() - recArea.getX() + 18).reduced (0, 16);
    juce::String big, small;
    switch (state)
    {
        case Idle:      big = hotKeyLabel() + u8 (" で録音"); small = u8 ("鼻歌を歌って、もう一度押すか、黙ると止まります"); break;
        case Starting:  big = u8 ("マイクを開いています…"); break;
        case Recording:
        {
            const auto w = ringW.load();
            if (ses.gridAbs >= 0 && w < ses.gridAbs)
            {
                const int left = (int) std::ceil ((double) (ses.gridAbs - w) / clickPeriod.load());
                big = u8 ("カウント  ") + juce::String (juce::jlimit (1, 4, left));
                small = juce::String ((int) std::lround (ses.bpm)) + u8 (" BPM。次の 1 拍目から歌ってください");
                break;
            }
            const int s = (int) (recN.load() / recFs);
            big = juce::String (ses.mode == Overdub ? u8 ("重ね録り中  ") : u8 ("録音中  ")) + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
            const auto tg = idea::tapGrid (ses.taps);
            small = hotKeyLabel() + u8 (" で止める")
                  + (ses.mode == Overdub ? u8 ("。前の声に合わせて")
                     : tg.ok ? u8 ("。タップ ") + juce::String ((int) tg.bpm) + " BPM"
                     : ! ses.countIn && props->getBoolValue ("tapKey", true) ? u8 ("。スペースか TAP でテンポをたたける")
                     : autoStopBtn.getToggleState() ? u8 ("（黙って 4 秒でも止まります）") : juce::String());
            break;
        }
        case Analysing: big = u8 ("MIDI にしています…"); small = u8 ("キーとテンポも調べています"); break;
        case Done:      big = u8 ("できました"); small = hotKeyLabel() + u8 (" で次のアイデアを録音"); break;
    }
    g.setColour (recNow ? red : textC);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (big, info.removeFromTop (28), juce::Justification::centredLeft);
    if (state == Recording)
    {
        auto m = info.removeFromTop (14).reduced (0, 3).withTrimmedRight (16).toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (m, 3.0f);
        const float db = 20.0f * std::log10 (meter + 1e-6f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -3 ? red : amber);
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * t), 3.0f);
    }
    else if (state == Analysing)
    {
        const int dots = (int) (juce::Time::getMillisecondCounter() / 300) % 4;
        g.setColour (amber);
        g.drawText (juce::String::repeatedString (u8 ("●"), dots), info.removeFromTop (14), juce::Justification::centredLeft);
    }
    else info.removeFromTop (6);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (small, info.withTrimmedRight (state == Recording ? 70 : 12), juce::Justification::topLeft, 2);

    // 結果のカード
    g.setColour (panel);
    g.fillRoundedRectangle (card.toFloat(), 12.0f);
    if (! cur.valid || state == Analysing)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (state == Analysing ? u8 ("少し待ってください…")
                                             : u8 ("録ると、ここにキー・テンポ・MIDI が出ます。\nできた MIDI は、つかんで Logic のトラックへドラッグすると入ります。"),
                          card.reduced (24), juce::Justification::centred, 4);
    }
    else
    {
        auto c = card.reduced (14, 12);
        auto label = [&] (juce::Rectangle<int> row, const juce::String& name, const juce::String& value)
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (name, row.removeFromLeft (64), juce::Justification::centredLeft);
            row.removeFromLeft (30);
            g.setColour (textC);
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (value, row.removeFromLeft (120), juce::Justification::centred);
        };
        const auto pct = [] (float v) { return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100)) + "%"; };
        const bool keyUsed = hasMelodyLayer();
        label (c.removeFromTop (30), "KEY", keyUsed ? shortKey (cur.o.key, cur.o.minor) : u8 ("—"));
        c.removeFromTop (8);
        {
            auto tr = c.removeFromTop (30);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText ("TEMPO", tr.removeFromLeft (64), juce::Justification::centredLeft);
            if (! tempoReset.isVisible())
            {
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (u8 ("数字を押して打てる"), tr.withTrimmedLeft (30 + 120 + 30 + 8), juce::Justification::centredLeft);
            }
        }
        c.removeFromTop (8);
        {
            auto hr = c.removeFromTop (30);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (u8 ("鼻歌"), hr.removeFromLeft (64), juce::Justification::centredLeft);
            hr.removeFromLeft (30);
            g.setColour (dim.brighter (0.3f));
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (juce::String ((int) std::lround (cur.o.bpm)) + " BPM", hr.removeFromLeft (120), juce::Justification::centred);
        }

        // 小さいピアノロール（レイヤーごとに色を変える）。ここをつかんで Logic へドラッグできる
        auto rr = roll.toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (rr, 6.0f);
        g.setColour (amber.withAlpha (0.55f));
        g.drawRoundedRectangle (rr.reduced (0.5f), 6.0f, 1.2f);
        struct P { idea::Placed p; int layer; };
        std::vector<P> placed;
        const double pick = idea::pickupBeats (cur.layers, cur.o);
        for (size_t i = 0; i < cur.layers.size(); ++i)
            for (auto p : idea::place (cur.layers[i], cur.o)) { p.b0 += pick; p.b1 += pick; placed.push_back ({ p, (int) i }); }
        double lastBeat = 4;
        int lo = 127, hi = 0;
        for (auto& q : placed)
        {
            lastBeat = std::max (lastBeat, q.p.b1);
            lo = std::min (lo, q.p.n); hi = std::max (hi, q.p.n);
        }
        const double bars = std::ceil (lastBeat / 4.0);
        const float x0 = rr.getX() + 6, wpx = rr.getWidth() - 12;
        auto bx = [&] (double b) { return x0 + (float) (b / (bars * 4.0)) * wpx; };
        for (int b = 0; b <= (int) bars * 4; ++b)
        {
            g.setColour (b % 4 == 0 ? line.brighter (0.25f) : line.withAlpha (0.45f));
            g.drawVerticalLine (juce::roundToInt (bx (b)), rr.getY() + 4, rr.getBottom() - 4);
        }
        if (pick > 0)
        {
            g.setColour (amber.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (bx (0), rr.getY() + 2, bx (pick) - bx (0), rr.getHeight() - 4));
        }
        auto melArea = rr.reduced (0, 6).withTrimmedBottom (14);
        if (hi < lo) { lo = 60; hi = 72; }
        const float span = (float) std::max (12, hi - lo + 2);
        const juce::Colour melCol[] = { amber, violet, green, red };
        for (auto& q : placed)
        {
            const auto& p = q.p;
            const float y = melArea.getBottom() - ((float) (p.n - lo + 1) / span) * melArea.getHeight();
            g.setColour (melCol[q.layer % 4]);
            g.fillRoundedRectangle ({ bx (p.b0), y - 2.5f, std::max (3.0f, bx (p.b1) - bx (p.b0) - 1), 5.0f }, 1.5f);
        }
        g.setColour (textC.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (pick > 0 ? u8 ("⠿ つかんで Logic へ（弱起あり：歌い出しの 1 小節前に落とす）") : u8 ("⠿ つかんで Logic のトラックへドラッグ"),
                    rr.reduced (8, 3).removeFromBottom (13), juce::Justification::centredRight);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.5f));
        double secs = 0;
        for (auto& L : cur.layers) secs = std::max (secs, L.r.seconds);
        const auto& L0 = cur.layers.front();
        const bool tempoFixed = L0.r.gridT0 >= 0 && L0.r.gridStep != 0;
        g.drawText (juce::String ((int) placed.size()) + u8 (" 音 · ") + juce::String ((int) bars) + u8 (" 小節")
                        + (cur.layers.size() > 1 ? u8 (" · 重ね ") + juce::String ((int) cur.layers.size()) : u8 (" · ") + juce::String (secs, 1) + u8 (" 秒"))
                        + u8 ("   確かさ: ")
                        + (cur.layers.back().edited ? u8 ("手直し済み · ") : juce::String())
                        + (keyUsed ? u8 ("キー ") + pct (L0.r.key.confidence) + " / " : juce::String())
                        + u8 ("テンポ ") + (tempoFixed ? (L0.r.gridStep == 4 ? u8 ("カウント") : u8 ("タップ")) : pct (L0.r.tempoConf)),
                    card.withTop (roll.getBottom() + 4).reduced (14, 0).withHeight (18), juce::Justification::centredLeft);
    }

    // ドラッグ
    auto chip = [&] (juce::Rectangle<int> r, const juce::String& t, bool on)
    {
        g.setColour (on ? panel2 : panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (on ? textC : dim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (t, r, juce::Justification::centred);
    };
    chip (dragMidi, u8 ("⠿ MIDI をドラッグ → Logic"), cur.valid && state == Done);
    chip (dragWav, u8 ("⠿ 声の録音 (WAV) をドラッグ"), cur.valid && ! cur.wavs.isEmpty());

    // 最近のアイデア
    auto ra = recentArea;
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (u8 ("最近のアイデア（クリックで開き直す）"), ra.removeFromTop (20), juce::Justification::centredLeft);
    if (recent.isEmpty())
    {
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (u8 ("まだありません。~/Music/Idea Catcher に保存されます"), ra.removeFromTop (26), juce::Justification::centredLeft);
    }
    for (int i = 0; i < recent.size(); ++i)
    {
        auto rr = ra.removeFromTop (26);
        const bool isCur = cur.valid && recent[i] == cur.folder;
        if (isCur || i == hoverRecent)
        {
            g.setColour (isCur ? panel2 : panel);
            g.fillRoundedRectangle (rr.toFloat(), 5.0f);
        }
        const auto inf = juce::JSON::parse (recent[i].getChildFile ("info.json"));
        juce::String desc;
        if (auto* o = inf.getDynamicObject())
        {
            const int nl = o->getProperty ("layers").isArray() ? o->getProperty ("layers").getArray()->size() : 1;
            desc = (nl > 1 ? u8 ("重ね ") + juce::String (nl) + "  " : juce::String())
                 + shortKey ((int) o->getProperty ("key"), (bool) o->getProperty ("minor")) + "  "
                 + juce::String ((int) std::lround ((double) o->getProperty ("outBpm") > 0 ? (double) o->getProperty ("outBpm") : (double) o->getProperty ("bpm"))) + u8 (" BPM  ·  ")
                 + juce::String ((double) o->getProperty ("seconds"), 1) + u8 (" 秒");
        }
        g.setColour (isCur ? amber : textC);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (recent[i].getFileName(), rr.withTrimmedLeft (8).removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (dim);
        g.drawText (desc, rr.withTrimmedLeft (160), juce::Justification::centredLeft);
    }

    // 下の一言
    if (message.isNotEmpty())
    {
        g.setColour (amber);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (message, getLocalBounds().removeFromBottom (22).reduced (16, 2), juce::Justification::centredLeft, 1);
    }
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    dragStarted = false;
    if (recCircle.expanded (6).contains (p)) { toggleRecord(); return; }
    auto ra = recentArea.withTrimmedTop (20);
    for (int i = 0; i < recent.size(); ++i)
        if (ra.removeFromTop (26).contains (p))
        {
            loadIdea (recent[i]);
            return;
        }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    auto ra = recentArea.withTrimmedTop (20);
    for (int i = 0; i < recent.size(); ++i)
        if (ra.removeFromTop (26).contains (e.getPosition())) h = i;
    const bool overDrag = cur.valid && state == Done && (dragMidi.contains (e.getPosition()) || dragWav.contains (e.getPosition()) || roll.contains (e.getPosition()));
    const bool overHot = h >= 0 || recCircle.contains (e.getPosition());
    setMouseCursor (overDrag ? juce::MouseCursor::DraggingHandCursor : overHot ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hoverRecent) { hoverRecent = h; repaint (recentArea); }
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! cur.valid || state != Done || dragStarted || e.getDistanceFromDragStart() < 4) return;
    const auto start = e.getMouseDownPosition();
    juce::File f;
    if (dragMidi.contains (start) || roll.contains (start)) f = cur.mid;
    else if (dragWav.contains (start)) f = cur.layers.size() > 1 ? writeLogicVoice() : cur.wavs[0];
    if (! f.existsAsFile()) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
