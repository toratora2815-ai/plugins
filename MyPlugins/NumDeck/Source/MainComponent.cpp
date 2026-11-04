#include "MainComponent.h"

using namespace numdeck;

namespace
{
    const juce::Colour bg (0xff0d1218), bg2 (0xff141b23), card (0xff1c2530), card2 (0xff151c25),
                       line (0xff2a3542), text (0xfff1f4f8), sub (0xff8a97a8),
                       accent (0xff3aa6ff), onCol (0xff38d27f), offCol (0xff3a434f), warn (0xffffa040),
                       recCol (0xffff4f5e);

    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // テンキーの並び（上から 7 8 9 / 4 5 6 / 1 2 3）→ キー番号 - 1
    const int kGridOrder[9] = { 6, 7, 8, 3, 4, 5, 0, 1, 2 };
    // タイルの番号（添字 0〜8 = キー 1〜9、添字 9 = キー 0、添字 10 = キー .）
    constexpr int kLearnModeKey = 100;    // learnIndex がこれなら、モード切替のキーを覚えさせている
    constexpr int kLearnToggleKey = 101;  // learnIndex がこれなら、ON / OFF のキーを覚えさせている
    juce::String keyNumber (int index) { return index == 10 ? juce::String (".") : juce::String ((index + 1) % 10); }

    // コントローラーのボタン表示のような、キーキャップ
    int drawKeycap (juce::Graphics& g, int x, int centreY, const juce::String& label, float alpha = 1.0f, float h = 22.0f)
    {
        const auto f = font (h * 0.55f, true);
        const int w = juce::jmax ((int) h, juce::GlyphArrangement::getStringWidthInt (f, label) + (int) (h * 0.55f));
        juce::Rectangle<float> r ((float) x, (float) centreY - h / 2, (float) w, h);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (alpha));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (sub.withAlpha (0.8f * alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (text.withAlpha (alpha));
        g.setFont (f);
        g.drawText (label, r, juce::Justification::centred);
        return w;
    }

    void drawPill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, bool on, bool hover, float fontSize = 12.0f)
    {
        auto rf = r.toFloat();
        g.setColour (on ? accent.withAlpha (0.22f) : card.withAlpha (hover ? 1.0f : 0.7f));
        g.fillRoundedRectangle (rf, rf.getHeight() / 2);
        g.setColour (on ? accent : line.brighter (hover ? 0.4f : 0.0f));
        g.drawRoundedRectangle (rf.reduced (0.5f), rf.getHeight() / 2, 1.0f);
        g.setColour (on ? text : sub);
        g.setFont (font (fontSize, on));
        g.drawFittedText ((on ? juce::String::fromUTF8 ("\xe2\x9c\x93 ") : juce::String()) + label, r.reduced (4, 0),
                          juce::Justification::centred, 1, 0.7f);
    }

    void triangle (juce::Path& p, juce::Rectangle<float> r, bool right)
    {
        if (right) p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getCentreY(), r.getX(), r.getBottom());
        else       p.addTriangle (r.getRight(), r.getY(), r.getX(), r.getCentreY(), r.getRight(), r.getBottom());
    }

    void arrowArc (juce::Graphics& g, juce::Rectangle<float> r, float from, float to, float stroke)
    {
        juce::Path arc;
        arc.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() / 2, r.getHeight() / 2, 0.0f, from, to, true);
        g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // 矢じり
        const auto end = arc.getCurrentPosition();
        const float dir = to > from ? 1.0f : -1.0f;
        const float tangent = to + dir * juce::MathConstants<float>::halfPi;
        const float a = stroke * 2.4f;
        juce::Path head;
        head.addTriangle (end.x + std::sin (tangent) * a,          end.y - std::cos (tangent) * a,
                          end.x + std::sin (tangent + 2.2f) * a,   end.y - std::cos (tangent + 2.2f) * a,
                          end.x + std::sin (tangent - 2.2f) * a,   end.y - std::cos (tangent - 2.2f) * a);
        g.fillPath (head);
    }

    void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        const float s = r.getWidth();
        const float stroke = juce::jmax (2.0f, s * 0.08f);
        juce::Path p;
        switch (icon)
        {
            case Icon::Play:
            {
                auto left = r.withWidth (s * 0.5f).reduced (s * 0.04f, s * 0.12f);
                triangle (p, left, true);
                g.fillPath (p);
                auto right = r.withTrimmedLeft (s * 0.6f).reduced (s * 0.02f, s * 0.24f);
                g.fillRoundedRectangle (right, s * 0.04f);
                break;
            }
            case Icon::Record:
                g.setColour (recCol);
                g.fillEllipse (r.reduced (s * 0.18f));
                break;
            case Icon::Capture:
                g.setColour (recCol);
                g.drawEllipse (r.reduced (s * 0.12f), stroke);
                g.fillEllipse (r.reduced (s * 0.3f));
                break;
            case Icon::Rewind:
            case Icon::Forward:
            {
                const bool fwd = icon == Icon::Forward;
                auto a = r.reduced (s * 0.12f, s * 0.24f);
                triangle (p, a.removeFromLeft (a.getWidth() / 2), fwd);
                triangle (p, a, fwd);
                g.fillPath (p);
                break;
            }
            case Icon::ToStart:
            {
                auto a = r.reduced (s * 0.16f, s * 0.22f);
                g.fillRect (a.removeFromLeft (stroke * 1.2f));
                a.removeFromLeft (s * 0.04f);
                triangle (p, a.removeFromLeft (a.getWidth() / 2), false);
                triangle (p, a, false);
                g.fillPath (p);
                break;
            }
            case Icon::Cycle:
            {
                auto a = r.reduced (s * 0.16f);
                arrowArc (g, a, 0.3f, 2.8f, stroke);
                arrowArc (g, a, 3.44f, 5.94f, stroke);
                break;
            }
            case Icon::Metronome:
            {
                auto a = r.reduced (s * 0.14f);
                p.startNewSubPath (a.getX() + a.getWidth() * 0.32f, a.getY());
                p.lineTo (a.getX() + a.getWidth() * 0.68f, a.getY());
                p.lineTo (a.getRight(), a.getBottom());
                p.lineTo (a.getX(), a.getBottom());
                p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.drawLine (a.getCentreX(), a.getBottom() - s * 0.1f, a.getRight() - s * 0.02f, a.getY() + s * 0.05f, stroke);
                break;
            }
            case Icon::Undo:
            case Icon::Redo:
            {
                auto a = r.reduced (s * 0.18f);
                if (icon == Icon::Undo) arrowArc (g, a, 2.2f, -1.4f, stroke);
                else                    arrowArc (g, a, -2.2f, 1.4f, stroke);
                break;
            }
            case Icon::Split:
            {
                auto a = r.reduced (s * 0.08f, s * 0.3f);
                auto left = a.withWidth (a.getWidth() * 0.44f);
                auto right = a.withTrimmedLeft (a.getWidth() * 0.56f);
                g.fillRoundedRectangle (left, s * 0.05f);
                g.fillRoundedRectangle (right, s * 0.05f);
                g.setColour (text);
                g.drawLine (r.getCentreX(), r.getY() + s * 0.1f, r.getCentreX(), r.getBottom() - s * 0.1f, stroke * 0.8f);
                break;
            }
            case Icon::None: break;
        }
    }
}

// ============================================================================
MainComponent::MainComponent()
{
    tap.onPadKey = [this] (int pad, bool down, bool repeat) { return deck.handlePadKey (pad, down, repeat); };
    tap.onAnyKey = [this] (int code, bool down, bool repeat) { return deck.handleAnyKey (code, down, repeat); };

    tap.onLearnKey = [this] (int code, int mods)
    {
        if (learnOverride)   // マクロの画面: 続けて覚えさせる（Esc で終わるまで learning のまま）
        {
            auto fn = learnOverride;
            fn (code, mods);
            return true;
        }
        if (learnIndex == kLearnToggleKey)
        {
            if (! (code == 53 && mods == 0)) { deck.toggleKey = Deck::ToggleLearned; deck.toggleKeyCode = code; deck.save(); }
        }
        else if (learnIndex == kLearnModeKey)
        {
            if (! (code == 53 && mods == 0)) { deck.modeKey = code; deck.save(); }
        }
        else if (! (code == 53 && mods == 0)) // Esc だけならやめる
        {
            Slot s;
            s.actionId = "custom";
            s.keyCode = code;
            s.mods = mods;
            deck.setSlot (learnPage, learnIndex, s);
            flash[(size_t) learnIndex] = 1.0f;
        }
        tap.learning = false;
        tap.learnAnyKey = false;
        learnPage = learnIndex = -1;
        repaint();
        return true;
    };

    deck.onFired = [this] (int i) { flash[(size_t) i] = 1.0f; repaint(); };
    deck.onStateChanged = [this] { resized(); repaint(); };
    deck.onIgnored = [this] { ignoredHint = 1.0f; repaint(); };
    deck.macro.onChanged = [this] { repaint(); };

    setSize (860, 600);
    refreshStatus();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    tap.onPadKey = nullptr;
    tap.onAnyKey = nullptr;
    tap.onLearnKey = nullptr;
    deck.macro.onChanged = nullptr;
    macroWindow = nullptr;
}

void MainComponent::refreshStatus()
{
    KeyTap::rememberFrontApp();
    const bool p = KeyTap::hasPermission();
    if (p && ! tap.isRunning()) tap.start();
    const bool r = tap.isRunning();
    const bool lr = KeyTap::isLogicRunning();
    const bool lf = KeyTap::isLogicFrontmost();

    const bool layoutChanged = p != permitted || r != tapRunning;
    if (layoutChanged || lr != logicRunning || lf != logicFront)
    {
        permitted = p; tapRunning = r; logicRunning = lr; logicFront = lf;
        if (layoutChanged) resized();
        repaint();
    }
}

void MainComponent::applyOnTop()
{
    if (auto* w = getTopLevelComponent())
        if (w != this) { w->setAlwaysOnTop (deck.alwaysOnTop); onTopApplied = true; }
}

void MainComponent::timerCallback()
{
    ++tick;
    if (! onTopApplied) applyOnTop();

    bool animating = false;
    // マクロの実行中は、そのタイルを光らせたままにする
    if (deck.macro.isRunning() && deck.macro.tile() >= 0 && deck.macro.tile() < Deck::kSlots)
        flash[(size_t) deck.macro.tile()] = juce::jmax (flash[(size_t) deck.macro.tile()], 0.75f + 0.25f * std::sin ((float) tick * 0.3f));
    for (auto& f : flash)
        if (f > 0.0f) { f = juce::jmax (0.0f, f - 0.07f); animating = true; }
    if (ignoredHint > 0.0f) { ignoredHint = juce::jmax (0.0f, ignoredHint - 0.012f); animating = true; }
    if (animating) repaint();

    if (tick % 6 == 0) refreshStatus();
}

// ============================================================================
void MainComponent::resized()
{
    // 小さいウィンドウでは、文字やすき間を詰めた配置にする
    compact = getWidth() < 640 || getHeight() < 440;
    if (compact)
    {
        layoutCompact();
        return;
    }

    auto b = getLocalBounds().reduced (18, 12);

    headerArea = b.removeFromTop (60);
    {
        auto h = headerArea;
        auto title = h.removeFromLeft (200);
        title.removeFromTop (30);
        auto seg = title.removeFromTop (26).withWidth (190);
        for (int m = 0; m < Deck::kModes; ++m)
            modeTabs[m] = seg.removeFromLeft (95);
        const int pw = juce::jlimit (150, 230, h.getWidth() / 3);
        statusPill = h.removeFromRight (pw).withSizeKeepingCentre (pw, 40);
        auto tabs = h.reduced (12, 0).withSizeKeepingCentre (juce::jmin (h.getWidth() - 24, 480), 40);
        prevPage = tabs.removeFromLeft (30).withSizeKeepingCentre (26, 24);
        nextPage = tabs.removeFromRight (30).withSizeKeepingCentre (26, 24);
        const int n = deck.numVisible();
        const int w = tabs.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < Deck::kPages; ++i)
            pageTabs[i] = i < n ? tabs.removeFromLeft (w) : juce::Rectangle<int>();
    }

    b.removeFromTop (8);
    bannerArea = {};
    bannerButton = {};
    if (! permitted || ! tapRunning)
    {
        bannerArea = b.removeFromTop (54);
        bannerButton = bannerArea.reduced (12, 0).removeFromRight (150).withSizeKeepingCentre (150, 32);
        b.removeFromTop (10);
    }

    footerArea = b.removeFromBottom (44);
    {
        auto f = footerArea;
        settingsPill = f.removeFromRight (70).withSizeKeepingCentre (70, 28);
        f.removeFromRight (8);
        sizePill = f.removeFromRight (90).withSizeKeepingCentre (90, 28);
        f.removeFromRight (8);
        onTopPill = f.removeFromRight (120).withSizeKeepingCentre (120, 28);
        f.removeFromRight (8);
        logicOnlyPill = f.removeFromRight (200).withSizeKeepingCentre (200, 28);
    }
    b.removeFromBottom (10);

    gridArea = b;
    const int gap = 12;
    layoutTiles (gap);

    learnCancel = getLocalBounds().withSizeKeepingCentre (140, 34).translated (0, 70);
}

// 3×3 のタイルと、その下にテンキーと同じく横長の 0（左 2 列分）と .（高さは半分）
void MainComponent::layoutTiles (int gap)
{
    const int tw = (gridArea.getWidth() - 2 * gap) / 3;
    const int th = (int) ((gridArea.getHeight() - 3 * gap) / 3.5);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            tiles[kGridOrder[row * 3 + col]] = { gridArea.getX() + col * (tw + gap), gridArea.getY() + row * (th + gap), tw, th };
    const int y0 = gridArea.getY() + 3 * (th + gap);
    tiles[9]  = { gridArea.getX(), y0, 2 * tw + gap, gridArea.getBottom() - y0 };
    tiles[10] = { gridArea.getX() + 2 * (tw + gap), y0, tw, gridArea.getBottom() - y0 };
}

void MainComponent::layoutCompact()
{
    auto b = getLocalBounds().reduced (8, 6);

    headerArea = b.removeFromTop (28);
    {
        auto h = headerArea;
        auto seg = h.removeFromLeft (100).withSizeKeepingCentre (100, 22);
        for (int m = 0; m < Deck::kModes; ++m)
            modeTabs[m] = seg.removeFromLeft (50);
        statusPill = h.removeFromRight (52).withSizeKeepingCentre (52, 22);
        auto tabs = h.reduced (6, 0);
        prevPage = tabs.removeFromLeft (22).withSizeKeepingCentre (18, 18);
        nextPage = tabs.removeFromRight (22).withSizeKeepingCentre (18, 18);
        const int n = deck.numVisible();
        const int w = tabs.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < Deck::kPages; ++i)
            pageTabs[i] = i < n ? tabs.removeFromLeft (w) : juce::Rectangle<int>();
    }

    b.removeFromTop (5);
    bannerArea = {};
    bannerButton = {};
    if (! permitted || ! tapRunning)
    {
        bannerArea = b.removeFromTop (28);
        bannerButton = bannerArea.reduced (4, 0).removeFromRight (78).withSizeKeepingCentre (78, 20);
        b.removeFromTop (5);
    }

    footerArea = b.removeFromBottom (24);
    {
        auto f = footerArea;
        settingsPill = f.removeFromRight (40).withSizeKeepingCentre (40, 18);
        f.removeFromRight (4);
        sizePill = f.removeFromRight (50).withSizeKeepingCentre (50, 18);
        f.removeFromRight (4);
        onTopPill = f.removeFromRight (50).withSizeKeepingCentre (50, 18);
        f.removeFromRight (4);
        logicOnlyPill = f.removeFromRight (78).withSizeKeepingCentre (78, 18);
    }
    b.removeFromBottom (5);

    gridArea = b;
    const int gap = 5;
    layoutTiles (gap);

    learnCancel = getLocalBounds().withSizeKeepingCentre (90, 24).translated (0, juce::jmin (60, getHeight() / 2 - 20));
}

// ============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bg2, 0, 0, bg, 0, (float) getHeight(), false));
    g.fillAll();

    paintHeader (g);
    if (! bannerArea.isEmpty()) paintBanner (g);
    for (int i = 0; i < Deck::kSlots; ++i) paintTile (g, i);

    if (! deck.enabled)
    {
        g.setColour (bg.withAlpha (0.6f));
        g.fillRoundedRectangle (gridArea.toFloat().expanded (6), 16.0f);
        g.setColour (text);
        g.setFont (font (compact ? 18.0f : 26.0f, true));
        g.drawText ("OFF", gridArea.withTrimmedBottom (gridArea.getHeight() / 2 - (compact ? 4 : 10)), juce::Justification::centredBottom);
        g.setColour (sub);
        g.setFont (font (compact ? 11.0f : 15.0f));
        auto msg = juce::String::fromUTF8 ("テンキーは普通の数字キーとして使えます");
        if (compact)
            msg = juce::String::fromUTF8 ("テンキーは普通の数字キー");
        else if (deck.toggleKey != Deck::ToggleNone)
            msg += juce::String::fromUTF8 ("（もう一度 ")
                 + toggleKeyLabel()
                 + juce::String::fromUTF8 (" で ON）");
        g.drawText (msg, gridArea.withTrimmedTop (gridArea.getHeight() / 2 + (compact ? 6 : 14)).withHeight (24), juce::Justification::centred);
    }

    if (ignoredHint > 0.0f)
    {
        auto r = gridArea.withSizeKeepingCentre (juce::jmin (gridArea.getWidth(), 460), compact ? 26 : 40).withBottomY (gridArea.getBottom() - 8);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (0.92f * juce::jmin (1.0f, ignoredHint * 3.0f)));
        g.fillRoundedRectangle (r.toFloat(), 20.0f);
        g.setColour (warn.withAlpha (juce::jmin (1.0f, ignoredHint * 3.0f)));
        g.setFont (font (compact ? 11.0f : 13.0f, true));
        g.drawFittedText (compact ? juce::String::fromUTF8 ("Logic が前面のときだけ動きます")
                                  : juce::String::fromUTF8 ("Logic Pro が前面のときだけ動きます（下の設定で変えられます）"),
                          r.reduced (8, 0), juce::Justification::centred, 1, 0.7f);
    }

    paintFooter (g);
    // ドラッグ中: 動かしているタイルを薄く、落とす先を枠で示し、マウスの所に名前を出す
    if (draggingTile && pressTile >= 0)
    {
        g.setColour (bg.withAlpha (0.55f));
        g.fillRoundedRectangle (tiles[pressTile].toFloat(), compact ? 8.0f : 14.0f);
        if (dropTile >= 0)
        {
            g.setColour (modeAccent());
            g.drawRoundedRectangle (tiles[dropTile].toFloat().reduced (1.0f), compact ? 8.0f : 14.0f, 3.0f);
        }
        const auto res = deck.resolve (deck.page(), pressTile);
        const auto label = keyNumber (pressTile) + "  " + (res.valid ? res.name : juce::String::fromUTF8 ("なし"));
        const auto m = getMouseXYRelative().toFloat();
        const float fs = compact ? 11.0f : 14.0f;
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (font (fs, true), label) + 20.0f;
        juce::Rectangle<float> ghost (m.x + 10.0f, m.y + 6.0f, w, fs + 14.0f);
        g.setColour (card.withAlpha (0.95f));
        g.fillRoundedRectangle (ghost, 6.0f);
        g.setColour (modeAccent());
        g.drawRoundedRectangle (ghost, 6.0f, 1.5f);
        g.setColour (text);
        g.setFont (font (fs, true));
        g.drawText (label, ghost, juce::Justification::centred);
    }

    if (learnIndex >= 0) paintLearning (g);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    if (! compact)
    {
        g.setColour (text);
        g.setFont (font (20.0f, true));
        g.drawText ("NUM DECK", headerArea.withWidth (200).withHeight (28), juce::Justification::centredLeft);
    }

    // モード（クリックでだけ切り替わる）
    const auto mouse = getMouseXYRelative();
    auto segAll = modeTabs[0].getUnion (modeTabs[Deck::kModes - 1]).toFloat();
    g.setColour (juce::Colour (0xff0a0e13));
    g.fillRoundedRectangle (segAll, segAll.getHeight() / 2);
    for (int m = 0; m < Deck::kModes; ++m)
    {
        const bool sel = m == deck.mode;
        auto r = modeTabs[m].toFloat().reduced (2.0f);
        if (sel)
        {
            g.setColour (modeAccent());
            g.fillRoundedRectangle (r, r.getHeight() / 2);
        }
        else if (modeTabs[m].contains (mouse))
        {
            g.setColour (card.brighter (0.2f));
            g.fillRoundedRectangle (r, r.getHeight() / 2);
        }
        g.setColour (sel ? juce::Colour (0xff06121c) : sub);
        g.setFont (font (compact ? 10.0f : 12.0f, true));
        g.drawFittedText (compact && m == Deck::ModeLogic ? juce::String ("Logic") : Deck::modeName (m),
                          modeTabs[m], juce::Justification::centred, 1, 0.7f);
    }

    // ページ（左右の / と * はテンキーの / と *）
    const bool hoverPrev = prevPage.contains (getMouseXYRelative()), hoverNext = nextPage.contains (getMouseXYRelative());
    const float kh = compact ? 18.0f : 22.0f;
    drawKeycap (g, prevPage.getX(), prevPage.getCentreY(), "/", hoverPrev ? 1.0f : 0.7f, kh);
    drawKeycap (g, nextPage.getX(), nextPage.getCentreY(), "*", hoverNext ? 1.0f : 0.7f, kh);
    for (int i = 0; i < deck.numVisible(); ++i)
    {
        const int pg = deck.visiblePage (i);
        const bool sel = pg == deck.page();
        auto r = pageTabs[i];
        if (draggingTab && pressTab == i)
        {
            g.setColour (modeAccent().withAlpha (0.18f));
            g.fillRoundedRectangle (r.toFloat().reduced (1.0f), 6.0f);
        }
        if (draggingTab && dropTab == i)
        {
            g.setColour (modeAccent());
            g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
        g.setColour (sel ? text : sub);
        g.setFont (font (compact ? 11.0f : 14.0f, sel));
        g.drawFittedText (deck.pageName (pg), r.reduced (2, 0), juce::Justification::centred, 1, 0.6f);
        if (sel)
        {
            g.setColour (modeAccent());
            g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth() - (compact ? 8 : 24), compact ? 2 : 3)
                                     .withBottomY (r.getBottom()).toFloat(), 1.5f);
        }
    }

    // ON / OFF
    auto pr = statusPill.toFloat();
    const bool hover = statusPill.contains (getMouseXYRelative());
    g.setColour (deck.enabled ? onCol : offCol.brighter (hover ? 0.2f : 0.0f));
    g.fillRoundedRectangle (pr, pr.getHeight() / 2);
    if (hover)
    {
        g.setColour (text.withAlpha (0.6f));
        g.drawRoundedRectangle (pr.reduced (0.5f), pr.getHeight() / 2, 1.5f);
    }
    if (compact)
    {
        g.setColour (deck.enabled ? juce::Colour (0xff06160c) : text);
        g.setFont (font (12.0f, true));
        g.drawText (deck.enabled ? "ON" : "OFF", statusPill, juce::Justification::centred);
        return;
    }
    auto inner = statusPill.reduced (16, 0);
    auto dot = inner.removeFromLeft (14).withSizeKeepingCentre (10, 10).toFloat();
    g.setColour (deck.enabled ? juce::Colour (0xff0b2a18) : sub);
    g.fillEllipse (dot);
    inner.removeFromLeft (8);
    g.setColour (deck.enabled ? juce::Colour (0xff06160c) : text);
    g.setFont (font (16.0f, true));
    g.drawText (deck.enabled ? "ON" : "OFF", inner.removeFromLeft (40), juce::Justification::centredLeft);
    g.setFont (font (12.0f));
    g.drawFittedText (! deck.enabled ? juce::String::fromUTF8 ("普通の数字キー")
                      : deck.mode == Deck::ModeLogic ? juce::String::fromUTF8 ("テンキーで Logic を操作")
                                                     : juce::String::fromUTF8 ("テンキーで操作"),
                      inner, juce::Justification::centredLeft, 1, 0.7f);
}

void MainComponent::paintBanner (juce::Graphics& g)
{
    auto r = bannerArea.toFloat();
    g.setColour (warn.withAlpha (0.14f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (warn.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto br = bannerButton.toFloat();
    g.setColour (warn);
    g.fillRoundedRectangle (br, br.getHeight() / 2);
    g.setColour (juce::Colour (0xff1a1006));
    g.setFont (font (compact ? 11.0f : 13.0f, true));
    g.drawText (juce::String::fromUTF8 ("許可する\xe2\x80\xa6"), bannerButton, juce::Justification::centred);

    if (compact)
    {
        g.setColour (text);
        g.setFont (font (11.0f, true));
        g.drawFittedText (juce::String::fromUTF8 ("アクセシビリティの許可が必要です"),
                          bannerArea.reduced (10, 0).withRight (bannerButton.getX() - 6), juce::Justification::centredLeft, 1, 0.7f);
        return;
    }

    auto t = bannerArea.reduced (16, 6);
    t.removeFromRight (170);
    g.setColour (text);
    g.setFont (font (14.0f, true));
    g.drawText (! permitted ? juce::String::fromUTF8 ("テンキーを受け取るには「アクセシビリティ」の許可が必要です")
                            : juce::String::fromUTF8 ("キーの受け取りを開始できませんでした"),
                t.removeFromTop (t.getHeight() / 2), juce::Justification::centredLeft);
    g.setColour (sub);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("右のボタン \xe2\x86\x92 開いた一覧で「Num Deck」を ON（一覧で ON に見えていても、もう一度 ON に）"),
                t, juce::Justification::centredLeft);
}

void MainComponent::paintTile (juce::Graphics& g, int index)
{
    auto r = tiles[index].toFloat();
    const auto res = deck.resolve (deck.page(), index);
    const juce::Colour ac = res.valid ? catColour (res.cat) : sub;
    const float f = flash[(size_t) index];
    const bool hover = hoverTile == index && learnIndex < 0;
    const float corner = compact ? 8.0f : 14.0f;

    // 押した瞬間の光
    if (f > 0.0f)
        for (int k = 4; k >= 1; --k)
        {
            g.setColour (ac.withAlpha (0.10f * f));
            g.fillRoundedRectangle (r.expanded ((float) k * (compact ? 1.5f : 3.0f)), corner + (float) k * 3.0f);
        }

    g.setGradientFill (juce::ColourGradient (card.brighter (hover ? 0.12f : 0.0f), 0, r.getY(),
                                             card2, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    if (f > 0.0f)
    {
        g.setColour (ac.withAlpha (0.28f * f));
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (f > 0.05f ? ac : (hover ? text.withAlpha (0.55f) : line));
    g.drawRoundedRectangle (r.reduced (0.75f), corner, f > 0.05f ? 2.0f : 1.5f);

    if (compact)
    {
        paintTileCompact (g, index, r, res, ac);
        return;
    }

    // 横長のタイル（キー 0）: 番号・記号・名前・ショートカットを横一列に
    if (r.getWidth() > r.getHeight() * 2.5f)
    {
        auto in = r.reduced (12.0f, 8.0f);
        const float scale = juce::jlimit (0.75f, 1.5f, r.getHeight() / 70.0f);
        auto badge = in.removeFromLeft (28.0f * scale).withSizeKeepingCentre (28.0f * scale, 28.0f * scale).reduced (1.0f);
        g.setColour (juce::Colour (0xff0a0e13));
        g.fillEllipse (badge);
        g.setColour (res.valid ? ac.withAlpha (0.9f) : line.brighter (0.3f));
        g.drawEllipse (badge, 1.5f);
        g.setColour (text);
        g.setFont (font (14.0f * scale, true));
        g.drawText (keyNumber (index), badge, juce::Justification::centred);
        in.removeFromLeft (12.0f);

        if (! res.valid)
        {
            g.setColour (sub.withAlpha (0.8f));
            g.setFont (font (14.0f * scale, true));
            g.drawText (juce::String::fromUTF8 ("なし（普通の数字キー）"), in, juce::Justification::centredLeft);
            return;
        }

        const auto sc = res.kind == Resolved::Remote ? (res.remote.startsWith ("!") ? juce::String() : juce::String ("RefMon"))
                      : res.kind == Resolved::Key ? shortcutText (res.keyCode, res.mods)
                      : res.kind == Resolved::App ? juce::String::fromUTF8 ("アプリ")
                      : res.kind == Resolved::Macro ? juce::String::fromUTF8 ("マクロ ") + juce::String ((int) res.steps.size()) + juce::String::fromUTF8 (" 手順")
                                                  : juce::URL (res.url).getDomain().replace ("www.", "");
        g.setFont (font (12.0f * scale));
        const float sw = juce::jmin (in.getWidth() * 0.35f,
                                     (float) juce::GlyphArrangement::getStringWidthInt (font (12.0f * scale), sc) + 14.0f);
        auto scr = in.removeFromRight (sw).withSizeKeepingCentre (sw, 20.0f * scale);
        if (sc.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff0a0e13).withAlpha (0.7f));
            g.fillRoundedRectangle (scr, 6.0f);
            g.setColour (sub);
            g.drawFittedText (sc, scr.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.6f);
        }

        const float isz = juce::jmin (in.getHeight(), 44.0f * scale);
        auto iconArea = in.removeFromLeft (isz).withSizeKeepingCentre (isz, isz);
        const auto img = res.kind == Resolved::App ? appIcon (res.path) : juce::Image();
        if (img.isValid())
            g.drawImage (img, iconArea, juce::RectanglePlacement::centred);
        else if (res.icon != Icon::None)
            drawIcon (g, res.icon, iconArea, ac);
        else
        {
            g.setColour (ac);
            g.setFont (font (juce::jmin (isz * 0.7f, 30.0f * scale), true));
            g.drawFittedText (res.glyph, iconArea.toNearestInt(), juce::Justification::centred, 1, 0.5f);
        }
        in.removeFromLeft (12.0f);
        g.setColour (text);
        g.setFont (font (15.0f * scale, true));
        g.drawFittedText (res.name, in.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

        g.setColour (ac.withAlpha (0.85f));
        g.fillRoundedRectangle (r.getX() + corner, r.getBottom() - 4.0f, r.getWidth() - corner * 2, 2.5f, 1.25f);
        return;
    }

    auto in = r.reduced (12.0f, 10.0f);
    const float scale = juce::jlimit (0.75f, 1.5f, r.getHeight() / 140.0f);

    // キー番号（コントローラーのボタンのような丸）
    auto top = in.removeFromTop (28.0f * scale);
    auto badge = top.removeFromLeft (28.0f * scale).reduced (1.0f);
    g.setColour (juce::Colour (0xff0a0e13));
    g.fillEllipse (badge);
    g.setColour (res.valid ? ac.withAlpha (0.9f) : line.brighter (0.3f));
    g.drawEllipse (badge, 1.5f);
    g.setColour (text);
    g.setFont (font (14.0f * scale, true));
    g.drawText (keyNumber (index), badge, juce::Justification::centred);

    if (res.valid)
    {
        // ショートカット（右上）。アプリ・ページは何を開くか
        const auto sc = res.kind == Resolved::Remote ? (res.remote.startsWith ("!") ? juce::String() : juce::String ("RefMon"))
                      : res.kind == Resolved::Key ? shortcutText (res.keyCode, res.mods)
                      : res.kind == Resolved::App ? juce::String::fromUTF8 ("アプリ")
                      : res.kind == Resolved::Macro ? juce::String::fromUTF8 ("マクロ ") + juce::String ((int) res.steps.size()) + juce::String::fromUTF8 (" 手順")
                                                  : juce::URL (res.url).getDomain().replace ("www.", "");
        const auto scShown = sc.isNotEmpty() ? sc : juce::String::fromUTF8 ("リンク");
        g.setFont (font (12.0f * scale));
        const float sw = juce::jmin (top.getWidth() - 4.0f,
                                     (float) juce::GlyphArrangement::getStringWidthInt (font (12.0f * scale), scShown) + 14.0f);
        auto scr = top.removeFromRight (sw).withSizeKeepingCentre (sw, 20.0f * scale);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (0.7f));
        g.fillRoundedRectangle (scr, 6.0f);
        g.setColour (sub);
        g.drawFittedText (scShown, scr.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.6f);

        // 名前（下）
        auto nameArea = in.removeFromBottom (26.0f * scale);
        g.setColour (text);
        g.setFont (font (15.0f * scale, true));
        g.drawFittedText (res.name, nameArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // アイコン（真ん中）
        const float isz = juce::jmin (in.getHeight() * 0.92f, in.getWidth() * 0.5f);
        auto iconArea = in.withSizeKeepingCentre (isz, isz);
        const auto img = res.kind == Resolved::App ? appIcon (res.path) : juce::Image();
        if (img.isValid())
            g.drawImage (img, iconArea.reduced (isz * 0.04f), juce::RectanglePlacement::centred);
        else if (res.icon != Icon::None)
            drawIcon (g, res.icon, iconArea, ac);
        else
        {
            g.setColour (ac);
            g.setFont (font (juce::jmin (isz * 0.62f, 44.0f * scale), true));
            g.drawFittedText (res.glyph, in.toNearestInt(), juce::Justification::centred, 1, 0.5f);
        }

        // 分類の色の線
        g.setColour (ac.withAlpha (0.85f));
        g.fillRoundedRectangle (r.getX() + corner, r.getBottom() - 4.0f, r.getWidth() - corner * 2, 2.5f, 1.25f);
    }
    else
    {
        g.setColour (sub.withAlpha (0.8f));
        g.setFont (font (15.0f * scale, true));
        g.drawText (juce::String::fromUTF8 ("なし"), in.withTrimmedBottom (in.getHeight() * 0.3f), juce::Justification::centred);
        g.setFont (font (12.0f * scale));
        g.setColour (sub.withAlpha (0.6f));
        g.drawText (juce::String::fromUTF8 ("普通の数字キー"), in.withTrimmedTop (in.getHeight() * 0.55f), juce::Justification::centredTop);
    }
}

// 小さい画面のタイル: 上に番号と名前、真ん中にアイコン（ショートカット表示は省く）
void MainComponent::paintTileCompact (juce::Graphics& g, int index, juce::Rectangle<float> r, const Resolved& res, juce::Colour ac)
{
    auto in = r.reduced (5.0f, 4.0f);
    auto top = in.removeFromTop (16.0f);
    auto badge = top.removeFromLeft (16.0f).reduced (0.5f);
    g.setColour (juce::Colour (0xff0a0e13));
    g.fillEllipse (badge);
    g.setColour (res.valid ? ac.withAlpha (0.9f) : line.brighter (0.3f));
    g.drawEllipse (badge, 1.0f);
    g.setColour (text);
    g.setFont (font (10.0f, true));
    g.drawText (keyNumber (index), badge, juce::Justification::centred);
    top.removeFromLeft (4.0f);

    g.setColour (res.valid ? text : sub.withAlpha (0.8f));
    g.setFont (font (11.0f, true));
    g.drawFittedText (res.valid ? res.name : juce::String::fromUTF8 ("なし"), top.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);

    if (! res.valid) return;

    in.removeFromBottom (3.0f);
    const float isz = juce::jmin (in.getHeight(), in.getWidth() * 0.6f);
    auto iconArea = in.withSizeKeepingCentre (isz, isz);
    const auto img = res.kind == Resolved::App ? appIcon (res.path) : juce::Image();
    if (img.isValid())
        g.drawImage (img, iconArea, juce::RectanglePlacement::centred);
    else if (res.icon != Icon::None)
        drawIcon (g, res.icon, iconArea, ac);
    else
    {
        g.setColour (ac);
        g.setFont (font (juce::jmin (isz * 0.7f, 22.0f), true));
        g.drawFittedText (res.glyph, in.toNearestInt(), juce::Justification::centred, 1, 0.5f);
    }

    g.setColour (ac.withAlpha (0.85f));
    g.fillRoundedRectangle (r.getX() + 8.0f, r.getBottom() - 3.0f, r.getWidth() - 16.0f, 2.0f, 1.0f);
}

void MainComponent::paintFooter (juce::Graphics& g)
{
    g.setColour (line);
    g.fillRect (footerArea.getX(), footerArea.getY(), footerArea.getWidth(), 1);

    auto f = footerArea;
    const int cy = f.getCentreY();
    int x = f.getX();

    // Logic の状態（業務モードでは、どのアプリでも動く）
    const bool logicMode = deck.mode == Deck::ModeLogic;
    juce::Colour dot = ! logicMode ? modeAccent() : logicFront ? onCol : (logicRunning ? warn : offCol.brighter (0.3f));
    g.setColour (dot);
    g.fillEllipse ((float) x, (float) cy - 4.0f, 8.0f, 8.0f);
    x += 14;
    const auto mouse = getMouseXYRelative();
    if (compact)
    {
        if (logicMode)
            drawPill (g, logicOnlyPill, juce::String::fromUTF8 ("Logicだけ"), deck.logicOnly, logicOnlyPill.contains (mouse), 10.0f);
        drawPill (g, onTopPill, juce::String::fromUTF8 ("手前"), deck.alwaysOnTop, onTopPill.contains (mouse), 10.0f);
        drawPill (g, sizePill, juce::String::fromUTF8 ("大きく"), false, sizePill.contains (mouse), 10.0f);
        drawPill (g, settingsPill, juce::String::fromUTF8 ("設定"), false, settingsPill.contains (mouse), 10.0f);
        g.setColour (sub);
        g.setFont (font (10.0f));
        g.drawFittedText (deck.enabled ? deck.pageName (deck.page()) : juce::String ("OFF"),
                          x, cy - 8, (logicMode ? logicOnlyPill.getX() : onTopPill.getX()) - x - 4, 16,
                          juce::Justification::centredLeft, 1, 0.7f);
        return;
    }
    g.setColour (sub);
    g.setFont (font (12.0f));
    const auto ls = ! logicMode ? juce::String::fromUTF8 ("どのアプリでも")
                  : logicFront ? juce::String::fromUTF8 ("Logic 前面")
                  : logicRunning ? juce::String::fromUTF8 ("Logic 背面")
                                 : juce::String::fromUTF8 ("Logic 未起動");
    g.drawText (ls, x, cy - 10, 90, 20, juce::Justification::centredLeft);
    x += 96;

    // 操作のヒント（場所が足りないときは出さない）
    const int hintLimit = logicMode ? logicOnlyPill.getX() : onTopPill.getX();
    if (x + 200 < hintLimit)
    {
    if (deck.toggleKey != Deck::ToggleNone)
    {
        x += drawKeycap (g, x, cy, toggleKeyLabel()) + 6;
        g.setColour (sub);
        g.drawText ("ON/OFF", x, cy - 10, 60, 20, juce::Justification::centredLeft);
        x += 62;
    }
    x += drawKeycap (g, x, cy, "/") + 4;
    x += drawKeycap (g, x, cy, "*") + 6;
    g.setColour (sub);
    if (x + 60 < hintLimit)
        g.drawText (juce::String::fromUTF8 ("ページ"), x, cy - 10, 60, 20, juce::Justification::centredLeft);
    }

    if (logicMode)
        drawPill (g, logicOnlyPill, juce::String::fromUTF8 ("Logic が前面のときだけ"), deck.logicOnly, logicOnlyPill.contains (mouse));
    drawPill (g, onTopPill, juce::String::fromUTF8 ("いつも手前"), deck.alwaysOnTop, onTopPill.contains (mouse));
    drawPill (g, sizePill, juce::String::fromUTF8 ("小さく"), false, sizePill.contains (mouse));
    drawPill (g, settingsPill, juce::String::fromUTF8 ("設定"), false, settingsPill.contains (mouse));
}

void MainComponent::paintLearning (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.78f));
    g.fillAll();

    if (compact)
    {
        auto c = getLocalBounds().reduced (10).toFloat();
        g.setColour (bg2);
        g.fillRoundedRectangle (c, 10.0f);
        g.setColour (catColour (Cat::Custom));
        g.drawRoundedRectangle (c.reduced (0.75f), 10.0f, 1.5f);
        auto t = c.reduced (12.0f).toNearestInt().withBottom (learnCancel.getY() - 4);
        g.setColour (sub);
        g.setFont (font (11.0f));
        g.drawText (learnIndex == kLearnToggleKey ? juce::String::fromUTF8 ("ON / OFF の切り替え")
                  : learnIndex == kLearnModeKey ? juce::String::fromUTF8 ("モードの切り替え")
                                                : deck.pageName (learnPage) + juce::String::fromUTF8 (" ・ テンキー ") + keyNumber (learnIndex),
                    t.removeFromTop (18), juce::Justification::centred);
        g.setColour (text);
        g.setFont (font (15.0f, true));
        g.drawFittedText (juce::String::fromUTF8 (learnIndex >= kLearnModeKey ? "切り替えに使うキーを押す" : "入れたいショートカットを押す"), t.removeFromTop (30), juce::Justification::centred, 1, 0.7f);
        g.setColour (sub);
        g.setFont (font (11.0f));
        g.drawText (juce::String::fromUTF8 ("Esc でやめる"), t.removeFromTop (18), juce::Justification::centred);
    }
    else
    {
    auto c = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 520), 220).toFloat();
    g.setColour (bg2);
    g.fillRoundedRectangle (c, 18.0f);
    g.setColour (catColour (Cat::Custom));
    g.drawRoundedRectangle (c.reduced (0.75f), 18.0f, 1.5f);

    auto t = c.reduced (24.0f, 22.0f).toNearestInt();
    g.setColour (sub);
    g.setFont (font (13.0f));
    g.drawText (learnIndex == kLearnToggleKey ? juce::String::fromUTF8 ("テンキーの ON / OFF の切り替え")
              : learnIndex == kLearnModeKey ? juce::String::fromUTF8 ("モード（Logic Pro / 業務）の切り替え")
                                            : deck.pageName (learnPage) + juce::String::fromUTF8 (" ページ ・ テンキー ") + keyNumber (learnIndex),
                t.removeFromTop (20), juce::Justification::centred);
    g.setColour (text);
    g.setFont (font (20.0f, true));
    g.drawText (juce::String::fromUTF8 (learnIndex >= kLearnModeKey ? "切り替えに使うキーを押してください" : "入れたいショートカットを押してください"),
                t.removeFromTop (40), juce::Justification::centred);
    g.setColour (sub);
    g.setFont (font (13.0f));
    g.drawText (learnIndex >= kLearnModeKey ? juce::String::fromUTF8 ("例: テンキーのいちばん上の Home キーなど（そのキーは切り替え専用になります）")
                                            : juce::String::fromUTF8 ("例: \xe2\x8c\x98 + T 、 \xe2\x87\xa7 + R など（Logic のキーコマンドと同じものを押す）"),
                t.removeFromTop (24), juce::Justification::centred);
    g.drawText (juce::String::fromUTF8 ("Esc でやめる"), t.removeFromTop (22), juce::Justification::centred);
    }

    auto br = learnCancel.toFloat();
    g.setColour (card);
    g.fillRoundedRectangle (br, br.getHeight() / 2);
    g.setColour (line.brighter (0.3f));
    g.drawRoundedRectangle (br.reduced (0.5f), br.getHeight() / 2, 1.0f);
    g.setColour (text);
    g.setFont (font (compact ? 11.0f : 13.0f, true));
    g.drawText (juce::String::fromUTF8 ("やめる"), learnCancel, juce::Justification::centred);
}

// ============================================================================
void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    int h = -1;
    for (int i = 0; i < Deck::kSlots; ++i)
        if (tiles[i].contains (p)) h = i;
    hoverTile = h;
    auto any = [&] (auto& rects) { return std::any_of (std::begin (rects), std::end (rects), [&] (auto& r) { return r.contains (p); }); };
    const bool clickable = h >= 0 || statusPill.contains (p) || bannerButton.contains (p)
                        || (deck.mode == Deck::ModeLogic && logicOnlyPill.contains (p))
                        || onTopPill.contains (p) || settingsPill.contains (p) || sizePill.contains (p)
                        || prevPage.contains (p) || nextPage.contains (p) || any (pageTabs) || any (modeTabs);
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void MainComponent::mouseExit (const juce::MouseEvent&)
{
    hoverTile = -1;
    repaint();
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    if (learnIndex >= 0)
    {
        if (learnCancel.contains (p)) { tap.learning = false; tap.learnAnyKey = false; learnPage = learnIndex = -1; repaint(); }
        return;
    }

    if (bannerButton.contains (p))
    {
        KeyTap::resetPermission();
        KeyTap::requestPermission();
        KeyTap::openPermissionSettings();
        return;
    }
    // ボタンを押したら、さっきまで使っていたアプリ（Logic など）に前面を戻す。
    // Num Deck が前面のままだと、テンキーが Logic に届かないため
    auto done = [this]
    {
        resized();
        repaint();
        juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
    };
    for (int m = 0; m < Deck::kModes; ++m)
        if (modeTabs[m].contains (p)) { deck.setMode (m); done(); return; }
    if (statusPill.contains (p))   { deck.enabled = ! deck.enabled; deck.save(); done(); return; }
    if (prevPage.contains (p))     { deck.stepPage (-1); done(); return; }
    if (nextPage.contains (p))     { deck.stepPage (1); done(); return; }
    // タブは離したときに決める（そのまま離せばそのページへ、ほかのタブまでドラッグすれば並べ替え）
    for (int i = 0; i < deck.numVisible(); ++i)
        if (pageTabs[i].contains (p)) { pressTab = i; dropTab = -1; draggingTab = false; return; }
    if (deck.mode == Deck::ModeLogic && logicOnlyPill.contains (p)) { deck.logicOnly = ! deck.logicOnly; deck.save(); done(); return; }
    if (onTopPill.contains (p))     { deck.alwaysOnTop = ! deck.alwaysOnTop; deck.save(); applyOnTop(); done(); return; }
    if (settingsPill.contains (p))  { showSettingsMenu(); return; }
    if (sizePill.contains (p))      { setWindowSize (! compact); done(); return; }
    // タイルは離したときに決める（そのまま離せばメニュー、ほかのタイルまでドラッグすれば入れ替え）
    for (int i = 0; i < Deck::kSlots; ++i)
        if (tiles[i].contains (p)) { pressTile = i; dropTile = -1; draggingTile = false; return; }
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (pressTab >= 0)
    {
        if (! draggingTab && e.getDistanceFromDragStart() < 8) return;
        draggingTab = true;
        dropTab = -1;
        for (int i = 0; i < deck.numVisible(); ++i)
            if (pageTabs[i].contains (e.getPosition()) && i != pressTab) dropTab = i;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
        return;
    }
    if (pressTile < 0) return;
    if (! draggingTile && e.getDistanceFromDragStart() < 8) return;
    draggingTile = true;
    dropTile = -1;
    for (int i = 0; i < Deck::kSlots; ++i)
        if (tiles[i].contains (e.getPosition()) && i != pressTile) dropTile = i;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    if (pressTab >= 0)
    {
        const int from = pressTab, to = dropTab;
        const bool dragged = draggingTab;
        pressTab = dropTab = -1;
        draggingTab = false;
        if (! dragged)          deck.setPage (deck.visiblePage (from));
        else if (to >= 0)       deck.moveTab (from, to);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        resized();
        repaint();
        juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
        return;
    }
    const int from = pressTile, to = dropTile;
    const bool dragged = draggingTile;
    pressTile = dropTile = -1;
    draggingTile = false;
    if (from < 0) return;
    if (! dragged) { showTileMenu (from); return; }
    if (to >= 0)
    {
        deck.swapSlots (deck.page(), from, to);
        flash[(size_t) from] = flash[(size_t) to] = 1.0f;
        juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    repaint();
}

// ============================================================================
void MainComponent::setWindowSize (bool small)
{
    if (auto* w = findParentComponentOfClass<juce::ResizableWindow>())
        w->setContentComponentSize (small ? 430 : 860, small ? 300 : 600);
}

juce::String MainComponent::toggleKeyLabel() const
{
    if (deck.toggleKey == Deck::ToggleLearned) return keyName (deck.toggleKeyCode);
    return deck.toggleKey == Deck::ToggleClear ? "Clear" : "=";
}

juce::Colour MainComponent::modeAccent() const
{
    return deck.mode == Deck::ModeLogic ? accent : juce::Colour (0xff4fd6c8);
}

juce::Image MainComponent::appIcon (const juce::String& path)
{
    auto it = iconCache.find (path);
    if (it != iconCache.end()) return it->second;
    const auto png = KeyTap::appIconPNG (path.toStdString(), 256);
    auto img = png.empty() ? juce::Image() : juce::ImageFileFormat::loadFrom (png.data(), png.size());
    iconCache[path] = img;
    return img;
}

void MainComponent::showTileMenu (int index)
{
    const int page = deck.page();
    const auto current = deck.slot (page, index);

    juce::PopupMenu m;
    m.addSectionHeader (Deck::modeName (deck.mode) + juce::String::fromUTF8 (" ・ ") + deck.pageName (page)
                        + juce::String::fromUTF8 (" ページ ・ テンキー ") + keyNumber (index));

    // 決まった操作（モードに合う分類を先に）
    std::map<int, juce::String> ids;
    int id = 1;
    const std::vector<Cat> order = deck.mode == Deck::ModeLogic
        ? std::vector<Cat> { Cat::Transport, Cat::Edit, Cat::Track, Cat::Window, Cat::File, Cat::RefMon, Cat::General, Cat::Browser }
        : std::vector<Cat> { Cat::General, Cat::Browser, Cat::Edit, Cat::File, Cat::RefMon };
    for (auto cat : order)
    {
        juce::PopupMenu sub;
        bool any = false;
        for (auto& a : catalog())
        {
            if (a.cat != cat) continue;
            const bool sel = current.actionId == a.id;
            any = any || sel;
            sub.addItem (id, a.remote != nullptr ? jp (a.name) : jp (a.name) + "    " + shortcutText (a.keyCode, a.mods), true, sel);
            ids[id++] = a.id;
        }
        m.addSubMenu (catName (cat), sub, true, nullptr, any);
    }

    // アプリを開く
    std::map<int, juce::String> appIds;
    {
        juce::PopupMenu apps;
        static const char* featured[] = {
            "/Applications/Slack.app", "/Applications/Google Chrome.app", "/Applications/Safari.app",
            "/System/Library/CoreServices/Finder.app", "/Applications/Microsoft Excel.app", "/Applications/Microsoft Word.app",
            "/Applications/Microsoft PowerPoint.app", "/Applications/Microsoft Teams.app", "/Applications/zoom.us.app",
            "/Applications/LINE.app", "/Applications/Discord.app", "/Applications/Logic Pro.app",
            "/System/Applications/Notes.app", "/System/Applications/Calendar.app", "/System/Applications/Mail.app",
            "/System/Applications/Reminders.app", "/System/Applications/Calculator.app" };
        int aid = 2000;
        for (auto* path : featured)
        {
            if (! juce::File (path).exists()) continue;
            const juce::String ps (path);
            apps.addItem (aid, juce::String::fromUTF8 (KeyTap::appDisplayName (path).c_str()), true,
                          current.actionId == "app" && current.path == ps);
            appIds[aid++] = ps;
        }
        apps.addSeparator();
        apps.addItem (1010, juce::String::fromUTF8 ("ほかのアプリを選ぶ\xe2\x80\xa6"));
        m.addSeparator();
        m.addSubMenu (catName (Cat::App), apps, true, nullptr, current.actionId == "app");
    }

    // ページを開く
    {
        juce::PopupMenu links;
        links.addItem (1020, juce::String::fromUTF8 ("Chrome で今開いているページを登録"));
        links.addItem (1021, juce::String::fromUTF8 ("アドレス（URL）を入力\xe2\x80\xa6"));
        m.addSubMenu (catName (Cat::Link), links, true, nullptr, current.actionId == "url");
    }

    // 2 画面で開く（リンクのときだけ）: 左にページ、右に Chrome の新しいウィンドウかアプリ
    if (current.actionId == "url")
    {
        juce::PopupMenu split;
        split.addItem (1030, juce::String::fromUTF8 ("しない（いつもどおり開く）"), true, current.split.isEmpty());
        split.addItem (1031, juce::String::fromUTF8 ("右に Chrome の新しいウィンドウ"), true, current.split == "chrome");
        const juce::String claude ("/Applications/Claude.app");
        if (juce::File (claude).exists())
            split.addItem (1032, juce::String::fromUTF8 ("右に Claude"), true, current.split == claude);
        const bool otherApp = current.split.isNotEmpty() && current.split != "chrome" && current.split != claude;
        split.addItem (1033, otherApp ? juce::String::fromUTF8 ("右に ") + juce::String::fromUTF8 (KeyTap::appDisplayName (current.split.toStdString()).c_str())
                                      : juce::String::fromUTF8 ("右にほかのアプリを選ぶ\xe2\x80\xa6"),
                       true, otherApp);
        m.addSubMenu (juce::String::fromUTF8 ("2 画面で開く"), split, true, nullptr, current.split.isNotEmpty());
    }

    // リンクごとの Chrome アカウントと、開く場所（v1.16.0）
    std::vector<std::pair<std::string, std::string>> profiles;
    std::vector<std::pair<juce::String, juce::String>> placeIds;   // 1201 から → (画面, 場所)
    if (current.actionId == "url")
    {
        profiles = KeyTap::chromeProfiles();
        juce::PopupMenu acc;
        acc.addItem (1100, juce::String::fromUTF8 ("設定のアカウントのまま")
                             + (deck.chromeProfileName.isNotEmpty() ? juce::String::fromUTF8 ("（") + deck.chromeProfileName + juce::String::fromUTF8 ("）") : juce::String()),
                     true, current.place.profile.isEmpty());
        for (size_t i = 0; i < profiles.size(); ++i)
            acc.addItem (1101 + (int) i, juce::String::fromUTF8 (profiles[i].second.c_str()), true,
                         current.place.profile == juce::String::fromUTF8 (profiles[i].first.c_str()));
        if (profiles.empty()) acc.addItem (1099, juce::String::fromUTF8 ("（一覧を読めませんでした）"), false);
        m.addSubMenu (juce::String::fromUTF8 ("このリンクを開くアカウント")
                        + (current.place.profileName.isNotEmpty() ? juce::String::fromUTF8 ("：") + current.place.profileName : juce::String()),
                      acc, true, nullptr, current.place.profile.isNotEmpty());

        juce::PopupMenu where;
        where.addItem (1200, juce::String::fromUTF8 ("Chrome にまかせる（置き直さない）"), true, ! current.place.placed());
        static const char* regions[] = { "full", "left", "right", "top", "bottom" };
        auto addScreen = [&] (const juce::String& screenName, const juce::String& shown)
        {
            juce::PopupMenu sub;
            bool any = false;
            for (auto* rg : regions)
            {
                const bool sel = current.place.placed() && current.place.screen == screenName && current.place.region == rg;
                any = any || sel;
                placeIds.push_back ({ screenName, rg });
                sub.addItem (1200 + (int) placeIds.size(), LinkPlace::regionName (rg), true, sel);
            }
            where.addSubMenu (shown, sub, true, nullptr, any);
        };
        const auto screens = KeyTap::screenNames();
        for (size_t i = 0; i < screens.size(); ++i)
        {
            const auto n = juce::String::fromUTF8 (screens[i].c_str());
            addScreen (n, n + (i == 0 ? juce::String::fromUTF8 ("（メイン）") : juce::String()));
        }
        addScreen ({}, juce::String::fromUTF8 ("マウスのある画面"));
        m.addSubMenu (juce::String::fromUTF8 ("開く場所：") + current.place.placeText(), where, current.split.isEmpty(), nullptr, current.place.placed());
    }

    // マクロ（いくつもの操作を順番に）
    m.addSeparator();
    m.addItem (1040, current.actionId == "macro" ? juce::String::fromUTF8 ("マクロを直す\xe2\x80\xa6")
                                                 : juce::String::fromUTF8 ("マクロを作る（いくつもの操作を順番に）\xe2\x80\xa6"),
               true, current.actionId == "macro");

    m.addSeparator();
    m.addItem (1000, juce::String::fromUTF8 ("ショートカットを覚えさせる\xe2\x80\xa6"), true, current.actionId == "custom");
    if (current.isUser())
        m.addItem (1001, juce::String::fromUTF8 ("名前を変える\xe2\x80\xa6"));
    m.addItem (1002, juce::String::fromUTF8 ("なし（普通の数字キーとして使う）"), true, current.actionId.isEmpty());

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (tiles[index])),
                     [safe, page, index, ids, appIds, profiles, placeIds] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         auto& self = *safe;
                         if (r == 1000) { self.startLearning (page, index); return; }
                         if (r == 1001) { self.renameSlot (page, index); return; }
                         if (r == 1010) { self.chooseOtherApp (page, index); return; }
                         if (r == 1020) { self.registerChromePage (page, index); return; }
                         if (r == 1021) { self.enterUrl (page, index); return; }
                         if (r >= 1030 && r <= 1032)
                         {
                             auto s = self.deck.slot (page, index);
                             s.split = r == 1030 ? juce::String() : r == 1031 ? juce::String ("chrome") : juce::String ("/Applications/Claude.app");
                             self.deck.setSlot (page, index, s);
                             self.flash[(size_t) index] = 1.0f;
                             self.repaint();
                             juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
                             return;
                         }
                         if (r == 1033) { self.chooseSplitApp (page, index); return; }
                         if (r == 1040) { self.openMacroEditor (page, index); return; }
                         if (r >= 1100 && r <= 1300)
                         {
                             auto s = self.deck.slot (page, index);
                             if (r == 1100) { s.place.profile = {}; s.place.profileName = {}; }
                             else if (r < 1200 && r - 1101 < (int) profiles.size())
                             {
                                 s.place.profile = juce::String::fromUTF8 (profiles[(size_t) (r - 1101)].first.c_str());
                                 s.place.profileName = juce::String::fromUTF8 (profiles[(size_t) (r - 1101)].second.c_str());
                             }
                             else if (r == 1200) { s.place.region = {}; s.place.screen = {}; }
                             else if (r > 1200 && r - 1201 < (int) placeIds.size())
                             {
                                 s.place.screen = placeIds[(size_t) (r - 1201)].first;
                                 s.place.region = placeIds[(size_t) (r - 1201)].second;
                             }
                             self.deck.setSlot (page, index, s);
                             self.flash[(size_t) index] = 1.0f;
                             self.repaint();
                             juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
                             return;
                         }
                         Slot s;
                         if (auto a = appIds.find (r); a != appIds.end())
                             s = Slot::app (a->second);
                         else if (auto it = ids.find (r); it != ids.end())
                             s.actionId = it->second;
                         else if (r != 1002)
                             return;
                         self.deck.setSlot (page, index, s);
                         self.flash[(size_t) index] = 1.0f;
                         self.repaint();
                         juce::Timer::callAfterDelay (120, [] { KeyTap::activateLastApp(); });
                     });
}

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("ON / OFF を切り替えるテンキー"));
    m.addItem (1, juce::String::fromUTF8 ("Clear（NumLock）"), true, deck.toggleKey == Deck::ToggleClear);
    m.addItem (2, "=", true, deck.toggleKey == Deck::ToggleEquals);
    m.addItem (3, juce::String::fromUTF8 ("なし（画面のボタンだけ）"), true, deck.toggleKey == Deck::ToggleNone);
    m.addItem (4, deck.toggleKey == Deck::ToggleLearned
                    ? juce::String::fromUTF8 ("覚えさせたキー：") + keyName (deck.toggleKeyCode) + juce::String::fromUTF8 ("（覚え直す\xe2\x80\xa6）")
                    : juce::String::fromUTF8 ("ほかのキーを覚えさせる\xe2\x80\xa6"),
               true, deck.toggleKey == Deck::ToggleLearned);
    m.addSectionHeader (juce::String::fromUTF8 ("モード（Logic Pro / 業務）を切り替えるキー：")
                        + (deck.modeKey >= 0 ? keyName (deck.modeKey) : juce::String::fromUTF8 ("なし")));
    m.addItem (6, juce::String::fromUTF8 ("切り替えのキーを覚えさせる\xe2\x80\xa6"));
    m.addItem (7, juce::String::fromUTF8 ("キーで切り替えない（画面のクリックだけ）"), true, deck.modeKey < 0);
    m.addSeparator();
    m.addItem (5, juce::String::fromUTF8 ("このページの名前を変える\xe2\x80\xa6"));
    {
        // 出すページ（チェックを外すと隠れる。割り当ては残る）。並べ替えは上のタブをドラッグ
        juce::PopupMenu vis;
        for (int p = 0; p < Deck::kPages; ++p)
            vis.addItem (60 + p, deck.pageName (p), true, deck.isVisible (deck.mode, p));
        m.addSubMenu (juce::String::fromUTF8 ("出すページ（") + Deck::modeName (deck.mode) + juce::String::fromUTF8 ("）"), vis);
    }
    m.addItem (10, juce::String::fromUTF8 ("このページを最初の割り当てに戻す"));
    m.addItem (11, Deck::modeName (deck.mode) + juce::String::fromUTF8 (" の全部のページを最初に戻す"));
    m.addSeparator();
    // リンクを開く Chrome のアカウント（初回は macOS が Chrome のデータを読む許可を聞く）
    juce::PopupMenu prof;
    const auto profiles = KeyTap::chromeProfiles();
    prof.addItem (40, juce::String::fromUTF8 ("指定しない（今使っている Chrome）"), true, deck.chromeProfile.isEmpty());
    for (size_t i = 0; i < profiles.size(); ++i)
        prof.addItem (41 + (int) i, juce::String::fromUTF8 (profiles[i].second.c_str()), true,
                      deck.chromeProfile == juce::String::fromUTF8 (profiles[i].first.c_str()));
    if (profiles.empty())
        prof.addItem (39, juce::String::fromUTF8 ("（一覧を読めませんでした）"), false);
    m.addSubMenu (juce::String::fromUTF8 ("リンクを開く Chrome のアカウント")
                    + (deck.chromeProfileName.isNotEmpty() ? juce::String::fromUTF8 ("：") + deck.chromeProfileName : juce::String()),
                  prof);
    m.addSeparator();
    m.addItem (32, juce::String::fromUTF8 ("押したとき画面の下に名前を出す"), true, deck.showHud);
    // 全部終了で残すアプリ（チェック付き = 残す）。今開いているアプリも並べて、クリックで切り替え
    juce::StringArray keepIds, keepNames;
    for (auto& k : deck.keepApps)
    {
        keepIds.add (k);
        keepNames.add (k == "com.anthropic.claudefordesktop" ? juce::String ("Claude")
                     : k == "com.apple.logic" ? juce::String ("Logic Pro") : k);
    }
    for (auto& [bid, name] : KeyTap::runningApps())
    {
        const juce::String b = juce::String::fromUTF8 (bid.c_str());
        bool covered = false;
        for (auto& k : deck.keepApps) if (b.startsWith (k)) covered = true;
        if (! covered && ! keepIds.contains (b)) { keepIds.add (b); keepNames.add (juce::String::fromUTF8 (name.c_str())); }
    }
    {
        juce::PopupMenu keep;
        for (int i = 0; i < keepIds.size() && i < 60; ++i)
            keep.addItem (200 + i, keepNames[i], true, deck.keepApps.contains (keepIds[i]));
        m.addSubMenu (juce::String::fromUTF8 ("「アプリを全部終了」で残すアプリ"), keep);
    }
    m.addItem (33, juce::String::fromUTF8 ("設定を書き出す（バックアップ）\xe2\x80\xa6"));
    m.addItem (34, juce::String::fromUTF8 ("設定を読み込む\xe2\x80\xa6"));
    m.addSeparator();
    m.addItem (30, juce::String::fromUTF8 ("大きい画面にする"), true, ! compact);
    m.addItem (31, juce::String::fromUTF8 ("小さい画面にする（4 分の 1）"), true, compact);
    m.addSeparator();
    m.addItem (20, juce::String::fromUTF8 ("アクセシビリティの設定を開く"));

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (settingsPill)),
                     [safe, profiles, keepIds] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         auto& d = safe->deck;
                         if (r >= 1 && r <= 3) { d.toggleKey = r - 1; d.save(); }
                         if (r == 4) safe->startLearning (0, kLearnToggleKey);
                         if (r >= 60 && r < 60 + Deck::kPages)
                         {
                             const int p = r - 60;
                             d.setPageVisible (p, ! d.isVisible (d.mode, p));
                             safe->resized();
                         }
                         if (r == 6) safe->startLearning (0, kLearnModeKey);
                         if (r == 32) { d.showHud = ! d.showHud; d.save(); }
                         if (r >= 200 && r - 200 < keepIds.size())
                         {
                             const auto id = keepIds[r - 200];
                             if (d.keepApps.contains (id)) d.keepApps.removeString (id);
                             else                          d.keepApps.add (id);
                             d.save();
                         }
                         if (r == 33) safe->exportSettings();
                         if (r == 34) safe->importSettings();
                         if (r == 7) { d.modeKey = -1; d.save(); }
                         if (r == 40) { d.chromeProfile.clear(); d.chromeProfileName.clear(); d.save(); }
                         if (r > 40 && r - 41 < (int) profiles.size())
                         {
                             d.chromeProfile     = juce::String::fromUTF8 (profiles[(size_t) (r - 41)].first.c_str());
                             d.chromeProfileName = juce::String::fromUTF8 (profiles[(size_t) (r - 41)].second.c_str());
                             d.save();
                         }
                         if (r == 5)
                         {
                             const int p = d.page();
                             safe->askText (juce::String::fromUTF8 ("ページの名前"), {},
                                            { { juce::String::fromUTF8 ("名前"), d.pageName (p) } },
                                            [safe, p] (const juce::StringArray& v)
                                            {
                                                if (safe != nullptr) { safe->deck.setPageName (p, v[0]); safe->repaint(); }
                                            });
                         }
                         if (r == 10) d.resetCurrentPage();
                         if (r == 11) d.resetCurrentMode();
                         if (r == 20) KeyTap::openPermissionSettings();
                         if (r == 30 || r == 31) safe->setWindowSize (r == 31);
                         safe->repaint();
                     });
}

void MainComponent::startLearning (int page, int index)
{
    if (! tap.isRunning())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8 ("先に許可が必要です"),
            juce::String::fromUTF8 ("キーを覚えさせるには、上の「許可する…」から「アクセシビリティ」の許可をしてください。"));
        return;
    }
    learnPage = page;
    learnIndex = index;
    tap.learnAnyKey = index >= kLearnModeKey;
    tap.learning = true;
    repaint();
}

// 設定のバックアップ（書き出し・読み込み）
void MainComponent::exportSettings()
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("設定の書き出し先"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile ("Num Deck 設定 " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".settings"),
                                                   "*.settings");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              const bool ok = safe->deck.exportSettings (fc.getResult());
                              KeyTap::showHUD (ok ? "設定を書き出しました" : "書き出せませんでした");
                          });
}

void MainComponent::importSettings()
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("読み込む設定ファイル"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.settings");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              const bool ok = safe->deck.importSettings (fc.getResult());
                              KeyTap::showHUD (ok ? "設定を読み込みました" : "読み込めませんでした");
                              safe->resized();
                              safe->repaint();
                          });
}

// 2 画面で開くときに右に置くアプリを選ぶ
void MainComponent::chooseSplitApp (int page, int index)
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("右に置くアプリを選んでください"),
                                                   juce::File ("/Applications"), "*.app");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, page, index] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              auto s = safe->deck.slot (page, index);
                              s.split = f.getFullPathName();
                              safe->deck.setSlot (page, index, s);
                              safe->flash[(size_t) index] = 1.0f;
                              safe->repaint();
                          });
}

void MainComponent::chooseOtherApp (int page, int index)
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("開きたいアプリを選んでください"),
                                                   juce::File ("/Applications"), "*.app");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, page, index] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              safe->deck.setSlot (page, index, Slot::app (f.getFullPathName()));
                              safe->flash[(size_t) index] = 1.0f;
                              safe->repaint();
                          });
}

void MainComponent::registerChromePage (int page, int index)
{
    std::string url, title;
    if (! KeyTap::chromeFrontPage (url, title))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8 ("Chrome のページが取れませんでした"),
            juce::String::fromUTF8 ("Chrome で登録したいページを開いてから、もう一度選んでください。\n"
                                    "「Num Deck が Google Chrome を制御しようとしています」と出たら「許可」を押してください。\n"
                                    "（システム設定 → プライバシーとセキュリティ → オートメーション でも ON にできます）"));
        return;
    }
    // 名前は短くできるよう、登録の前に確認する
    auto name = juce::String::fromUTF8 (title.c_str()).upToFirstOccurrenceOf (" - ", false, false)
                                                     .upToFirstOccurrenceOf (" | ", false, false).trim();
    const auto u = juce::String::fromUTF8 (url.c_str());
    juce::Component::SafePointer<MainComponent> safe (this);
    askText (juce::String::fromUTF8 ("このページを登録"), u,
             { { juce::String::fromUTF8 ("タイルに出す名前（短いほど見やすい）"), name } },
             [safe, page, index, u] (const juce::StringArray& v)
             {
                 if (safe == nullptr) return;
                 Slot s; s.actionId = "url"; s.url = u; s.label = v[0].trim();
                 safe->deck.setSlot (page, index, s);
                 safe->flash[(size_t) index] = 1.0f;
                 safe->repaint();
             });
}

void MainComponent::enterUrl (int page, int index)
{
    const auto& cur = deck.slot (page, index);
    juce::Component::SafePointer<MainComponent> safe (this);
    askText (juce::String::fromUTF8 ("ページを登録"), {},
             { { juce::String::fromUTF8 ("アドレス（URL）"), cur.actionId == "url" ? cur.url : juce::String ("https://") },
               { juce::String::fromUTF8 ("タイルに出す名前"), cur.actionId == "url" ? cur.label : juce::String() } },
             [safe, page, index] (const juce::StringArray& v)
             {
                 if (safe == nullptr) return;
                 auto u = v[0].trim();
                 if (u.isEmpty() || u == "https://") return;
                 if (! u.containsChar (':')) u = "https://" + u;
                 Slot s; s.actionId = "url"; s.url = u; s.label = v[1].trim();
                 safe->deck.setSlot (page, index, s);
                 safe->flash[(size_t) index] = 1.0f;
                 safe->repaint();
             });
}

// マクロの画面（別ウィンドウ）
void MainComponent::openMacroEditor (int page, int index)
{
    if (macroWindow != nullptr) { macroWindow->toFront (true); return; }

    struct Window : juce::DocumentWindow
    {
        std::function<void()> onClose;
        Window() : DocumentWindow (juce::String::fromUTF8 ("マクロ"), juce::Colour (0xff0d1218), DocumentWindow::closeButton) {}
        void closeButtonPressed() override { if (onClose) onClose(); }
    };

    juce::Component::SafePointer<MainComponent> safe (this);
    auto closeLater = [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->learnOverride = nullptr;
            safe->tap.learning = false;
            safe->macroWindow = nullptr;
            safe->repaint();
        });
    };

    MacroEditor::Host host;
    host.mode = deck.mode;
    host.learn = [safe] (std::function<void (int, int)> fn)
    {
        if (safe == nullptr) return;
        if (fn != nullptr && ! safe->tap.isRunning())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                juce::String::fromUTF8 ("先に許可が必要です"),
                juce::String::fromUTF8 ("キーを覚えさせるには、「アクセシビリティ」の許可をしてください。"));
            return;
        }
        safe->learnOverride = fn;
        safe->tap.learnAnyKey = false;
        safe->tap.learning = fn != nullptr;
    };
    host.save = [safe, page, index] (const Slot& s)
    {
        if (safe == nullptr) return;
        safe->deck.setSlot (page, index, s);
        safe->flash[(size_t) index] = 1.0f;
        safe->repaint();
    };
    host.test = [safe] (const std::vector<Step>& steps)
    {
        if (safe == nullptr) return;
        // 前にいたアプリに戻してから実行する（Num Deck の画面にキーが届かないように）
        KeyTap::activateLastApp();
        juce::Timer::callAfterDelay (300, [safe, steps]
        {
            if (safe != nullptr) safe->deck.macro.start (steps, safe->deck.mode == Deck::ModeLogic, -1);
        });
    };
    host.close = closeLater;

    auto* w = new Window();
    w->onClose = closeLater;
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (new MacroEditor (deck.slot (page, index), host), true);
    w->setResizable (true, false);
    w->setResizeLimits (520, 420, 1200, 1200);
    w->centreAroundComponent (this, w->getWidth(), w->getHeight());
    w->setVisible (true);
    w->toFront (true);
    macroWindow.reset (w);
}

void MainComponent::renameSlot (int page, int index)
{
    juce::Component::SafePointer<MainComponent> safe (this);
    askText (juce::String::fromUTF8 ("名前を変える"), {},
             { { juce::String::fromUTF8 ("タイルに出す名前"), deck.resolve (page, index).name } },
             [safe, page, index] (const juce::StringArray& v)
             {
                 if (safe == nullptr) return;
                 auto s = safe->deck.slot (page, index);
                 s.label = v[0].trim();
                 safe->deck.setSlot (page, index, s);
                 safe->repaint();
             });
}

void MainComponent::askText (const juce::String& title, const juce::String& message,
                             const std::vector<std::pair<juce::String, juce::String>>& fields,
                             std::function<void (const juce::StringArray&)> done)
{
    inputWindow = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon);
    for (size_t i = 0; i < fields.size(); ++i)
        inputWindow->addTextEditor ("f" + juce::String ((int) i), fields[i].second, fields[i].first);
    inputWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    inputWindow->addButton (juce::String::fromUTF8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const int n = (int) fields.size();
    juce::Component::SafePointer<MainComponent> safe (this);
    inputWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, n, done] (int r)
    {
        if (safe == nullptr || safe->inputWindow == nullptr) return;
        juce::StringArray values;
        for (int i = 0; i < n; ++i)
            values.add (safe->inputWindow->getTextEditorContents ("f" + juce::String (i)));
        safe->inputWindow->setVisible (false);
        if (r == 1 && done) done (values);
    }), false);
}
