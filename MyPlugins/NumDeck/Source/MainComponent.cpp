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

    // コントローラーのボタン表示のような、キーキャップ
    int drawKeycap (juce::Graphics& g, int x, int centreY, const juce::String& label, float alpha = 1.0f)
    {
        const auto f = font (12.0f, true);
        const int w = juce::jmax (22, juce::GlyphArrangement::getStringWidthInt (f, label) + 12);
        juce::Rectangle<float> r ((float) x, (float) centreY - 11.0f, (float) w, 22.0f);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (alpha));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (sub.withAlpha (0.8f * alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (text.withAlpha (alpha));
        g.setFont (f);
        g.drawText (label, r, juce::Justification::centred);
        return w;
    }

    void drawPill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, bool on, bool hover)
    {
        auto rf = r.toFloat();
        g.setColour (on ? accent.withAlpha (0.22f) : card.withAlpha (hover ? 1.0f : 0.7f));
        g.fillRoundedRectangle (rf, rf.getHeight() / 2);
        g.setColour (on ? accent : line.brighter (hover ? 0.4f : 0.0f));
        g.drawRoundedRectangle (rf.reduced (0.5f), rf.getHeight() / 2, 1.0f);
        g.setColour (on ? text : sub);
        g.setFont (font (12.0f, on));
        g.drawText ((on ? juce::String::fromUTF8 ("\xe2\x9c\x93 ") : juce::String()) + label, r, juce::Justification::centred);
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

    tap.onLearnKey = [this] (int code, int mods)
    {
        if (! (code == 53 && mods == 0)) // Esc だけならやめる
        {
            Slot s;
            s.actionId = "custom";
            s.keyCode = code;
            s.mods = mods;
            deck.setSlot (learnPage, learnIndex, s);
            flash[(size_t) learnIndex] = 1.0f;
        }
        tap.learning = false;
        learnPage = learnIndex = -1;
        repaint();
        return true;
    };

    deck.onFired = [this] (int i) { flash[(size_t) i] = 1.0f; repaint(); };
    deck.onStateChanged = [this] { repaint(); };
    deck.onIgnored = [this] { ignoredHint = 1.0f; repaint(); };

    setSize (860, 600);
    refreshStatus();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    tap.onPadKey = nullptr;
    tap.onLearnKey = nullptr;
}

void MainComponent::refreshStatus()
{
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
    for (auto& f : flash)
        if (f > 0.0f) { f = juce::jmax (0.0f, f - 0.07f); animating = true; }
    if (ignoredHint > 0.0f) { ignoredHint = juce::jmax (0.0f, ignoredHint - 0.012f); animating = true; }
    if (animating) repaint();

    if (tick % 6 == 0) refreshStatus();
}

// ============================================================================
void MainComponent::resized()
{
    auto b = getLocalBounds().reduced (18, 12);

    headerArea = b.removeFromTop (60);
    {
        auto h = headerArea;
        h.removeFromLeft (200);
        statusPill = h.removeFromRight (230).withSizeKeepingCentre (230, 40);
        auto tabs = h.reduced (16, 0).withSizeKeepingCentre (juce::jmin (h.getWidth() - 32, 420), 40);
        prevPage = tabs.removeFromLeft (30).withSizeKeepingCentre (26, 24);
        nextPage = tabs.removeFromRight (30).withSizeKeepingCentre (26, 24);
        const int w = tabs.getWidth() / Deck::kPages;
        for (int i = 0; i < Deck::kPages; ++i)
            pageTabs[i] = tabs.removeFromLeft (w);
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
        onTopPill = f.removeFromRight (120).withSizeKeepingCentre (120, 28);
        f.removeFromRight (8);
        logicOnlyPill = f.removeFromRight (200).withSizeKeepingCentre (200, 28);
    }
    b.removeFromBottom (10);

    gridArea = b;
    const int gap = 12;
    const int tw = (gridArea.getWidth() - 2 * gap) / 3;
    const int th = (gridArea.getHeight() - 2 * gap) / 3;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            tiles[kGridOrder[row * 3 + col]] = { gridArea.getX() + col * (tw + gap), gridArea.getY() + row * (th + gap), tw, th };

    learnCancel = getLocalBounds().withSizeKeepingCentre (140, 34).translated (0, 70);
}

// ============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bg2, 0, 0, bg, 0, (float) getHeight(), false));
    g.fillAll();

    paintHeader (g);
    if (! bannerArea.isEmpty()) paintBanner (g);
    for (int i = 0; i < 9; ++i) paintTile (g, i);

    if (! deck.enabled)
    {
        g.setColour (bg.withAlpha (0.6f));
        g.fillRoundedRectangle (gridArea.toFloat().expanded (6), 16.0f);
        g.setColour (text);
        g.setFont (font (26.0f, true));
        g.drawText ("OFF", gridArea.withTrimmedBottom (gridArea.getHeight() / 2 - 10), juce::Justification::centredBottom);
        g.setColour (sub);
        g.setFont (font (15.0f));
        auto msg = juce::String::fromUTF8 ("テンキーは普通の数字キーとして使えます");
        if (deck.toggleKey != Deck::ToggleNone)
            msg += juce::String::fromUTF8 ("（もう一度 ")
                 + (deck.toggleKey == Deck::ToggleClear ? "Clear" : "=")
                 + juce::String::fromUTF8 (" で ON）");
        g.drawText (msg, gridArea.withTrimmedTop (gridArea.getHeight() / 2 + 14).withHeight (24), juce::Justification::centred);
    }

    if (ignoredHint > 0.0f)
    {
        auto r = gridArea.withSizeKeepingCentre (juce::jmin (gridArea.getWidth(), 460), 40).withBottomY (gridArea.getBottom() - 8);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (0.92f * juce::jmin (1.0f, ignoredHint * 3.0f)));
        g.fillRoundedRectangle (r.toFloat(), 20.0f);
        g.setColour (warn.withAlpha (juce::jmin (1.0f, ignoredHint * 3.0f)));
        g.setFont (font (13.0f, true));
        g.drawText (juce::String::fromUTF8 ("Logic Pro が前面のときだけ動きます（下の設定で変えられます）"), r, juce::Justification::centred);
    }

    paintFooter (g);
    if (learnIndex >= 0) paintLearning (g);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    auto h = headerArea;
    auto title = h.removeFromLeft (200);
    g.setColour (text);
    g.setFont (font (24.0f, true));
    g.drawText ("NUM DECK", title.removeFromTop (34).withTrimmedTop (6), juce::Justification::centredLeft);
    g.setColour (sub);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("テンキー  \xe2\x86\x92  Logic Pro"), title.removeFromTop (18), juce::Justification::centredLeft);

    // ページ（左右の − / + はテンキーの − / +）
    const bool hoverPrev = prevPage.contains (getMouseXYRelative()), hoverNext = nextPage.contains (getMouseXYRelative());
    drawKeycap (g, prevPage.getX(), prevPage.getCentreY(), juce::String::fromUTF8 ("\xe2\x88\x92"), hoverPrev ? 1.0f : 0.7f);
    drawKeycap (g, nextPage.getX(), nextPage.getCentreY(), "+", hoverNext ? 1.0f : 0.7f);
    for (int i = 0; i < Deck::kPages; ++i)
    {
        const bool sel = i == deck.page;
        auto r = pageTabs[i];
        g.setColour (sel ? text : sub);
        g.setFont (font (14.0f, sel));
        g.drawText (Deck::pageName (i), r, juce::Justification::centred);
        if (sel)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth() - 24, 3).withBottomY (r.getBottom()).toFloat(), 1.5f);
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
    auto inner = statusPill.reduced (16, 0);
    auto dot = inner.removeFromLeft (14).withSizeKeepingCentre (10, 10).toFloat();
    g.setColour (deck.enabled ? juce::Colour (0xff0b2a18) : sub);
    g.fillEllipse (dot);
    inner.removeFromLeft (8);
    g.setColour (deck.enabled ? juce::Colour (0xff06160c) : text);
    g.setFont (font (16.0f, true));
    g.drawText (deck.enabled ? "ON" : "OFF", inner.removeFromLeft (40), juce::Justification::centredLeft);
    g.setFont (font (12.0f));
    g.drawText (deck.enabled ? juce::String::fromUTF8 ("テンキーで Logic を操作")
                             : juce::String::fromUTF8 ("普通の数字キー"),
                inner, juce::Justification::centredLeft);
}

void MainComponent::paintBanner (juce::Graphics& g)
{
    auto r = bannerArea.toFloat();
    g.setColour (warn.withAlpha (0.14f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (warn.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto t = bannerArea.reduced (16, 6);
    t.removeFromRight (170);
    g.setColour (text);
    g.setFont (font (14.0f, true));
    g.drawText (! permitted ? juce::String::fromUTF8 ("テンキーを受け取るには「アクセシビリティ」の許可が必要です")
                            : juce::String::fromUTF8 ("キーの受け取りを開始できませんでした"),
                t.removeFromTop (t.getHeight() / 2), juce::Justification::centredLeft);
    g.setColour (sub);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("システム設定 \xe2\x86\x92 プライバシーとセキュリティ \xe2\x86\x92 アクセシビリティ で「Num Deck」を ON"),
                t, juce::Justification::centredLeft);

    auto br = bannerButton.toFloat();
    g.setColour (warn);
    g.fillRoundedRectangle (br, br.getHeight() / 2);
    g.setColour (juce::Colour (0xff1a1006));
    g.setFont (font (13.0f, true));
    g.drawText (juce::String::fromUTF8 ("許可する\xe2\x80\xa6"), bannerButton, juce::Justification::centred);
}

void MainComponent::paintTile (juce::Graphics& g, int index)
{
    auto r = tiles[index].toFloat();
    const auto res = deck.resolve (deck.page, index);
    const juce::Colour ac = res.valid ? catColour (res.cat) : sub;
    const float f = flash[(size_t) index];
    const bool hover = hoverTile == index && learnIndex < 0;
    const float corner = 14.0f;

    // 押した瞬間の光
    if (f > 0.0f)
        for (int k = 4; k >= 1; --k)
        {
            g.setColour (ac.withAlpha (0.10f * f));
            g.fillRoundedRectangle (r.expanded ((float) k * 3.0f), corner + (float) k * 3.0f);
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
    g.drawText (juce::String (index + 1), badge, juce::Justification::centred);

    if (res.valid)
    {
        // ショートカット（右上）
        const auto sc = shortcutText (res.keyCode, res.mods);
        g.setFont (font (12.0f * scale));
        const float sw = (float) juce::GlyphArrangement::getStringWidthInt (font (12.0f * scale), sc) + 14.0f;
        auto scr = top.removeFromRight (sw).withSizeKeepingCentre (sw, 20.0f * scale);
        g.setColour (juce::Colour (0xff0a0e13).withAlpha (0.7f));
        g.fillRoundedRectangle (scr, 6.0f);
        g.setColour (sub);
        g.drawText (sc, scr, juce::Justification::centred);

        // 名前（下）
        auto nameArea = in.removeFromBottom (26.0f * scale);
        g.setColour (text);
        g.setFont (font (15.0f * scale, true));
        g.drawFittedText (res.name, nameArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // アイコン（真ん中）
        const float isz = juce::jmin (in.getHeight() * 0.92f, in.getWidth() * 0.5f);
        auto iconArea = in.withSizeKeepingCentre (isz, isz);
        if (res.icon != Icon::None)
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

void MainComponent::paintFooter (juce::Graphics& g)
{
    g.setColour (line);
    g.fillRect (footerArea.getX(), footerArea.getY(), footerArea.getWidth(), 1);

    auto f = footerArea;
    const int cy = f.getCentreY();
    int x = f.getX();

    // Logic の状態
    juce::Colour dot = logicFront ? onCol : (logicRunning ? warn : offCol.brighter (0.3f));
    g.setColour (dot);
    g.fillEllipse ((float) x, (float) cy - 4.0f, 8.0f, 8.0f);
    x += 14;
    g.setColour (sub);
    g.setFont (font (12.0f));
    const auto ls = logicFront ? juce::String::fromUTF8 ("Logic 前面")
                  : logicRunning ? juce::String::fromUTF8 ("Logic 背面")
                                 : juce::String::fromUTF8 ("Logic 未起動");
    g.drawText (ls, x, cy - 10, 80, 20, juce::Justification::centredLeft);
    x += 86;

    // 操作のヒント
    if (deck.toggleKey != Deck::ToggleNone)
    {
        x += drawKeycap (g, x, cy, deck.toggleKey == Deck::ToggleClear ? "Clear" : "=") + 6;
        g.setColour (sub);
        g.drawText ("ON/OFF", x, cy - 10, 60, 20, juce::Justification::centredLeft);
        x += 62;
    }
    x += drawKeycap (g, x, cy, juce::String::fromUTF8 ("\xe2\x88\x92")) + 4;
    x += drawKeycap (g, x, cy, "+") + 6;
    g.setColour (sub);
    if (x + 60 < logicOnlyPill.getX())
        g.drawText (juce::String::fromUTF8 ("ページ"), x, cy - 10, 60, 20, juce::Justification::centredLeft);

    const auto mouse = getMouseXYRelative();
    drawPill (g, logicOnlyPill, juce::String::fromUTF8 ("Logic が前面のときだけ"), deck.logicOnly, logicOnlyPill.contains (mouse));
    drawPill (g, onTopPill, juce::String::fromUTF8 ("いつも手前"), deck.alwaysOnTop, onTopPill.contains (mouse));
    drawPill (g, settingsPill, juce::String::fromUTF8 ("設定"), false, settingsPill.contains (mouse));
}

void MainComponent::paintLearning (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.78f));
    g.fillAll();

    auto c = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 520), 220).toFloat();
    g.setColour (bg2);
    g.fillRoundedRectangle (c, 18.0f);
    g.setColour (catColour (Cat::Custom));
    g.drawRoundedRectangle (c.reduced (0.75f), 18.0f, 1.5f);

    auto t = c.reduced (24.0f, 22.0f).toNearestInt();
    g.setColour (sub);
    g.setFont (font (13.0f));
    g.drawText (Deck::pageName (learnPage) + juce::String::fromUTF8 (" ページ ・ テンキー ") + juce::String (learnIndex + 1),
                t.removeFromTop (20), juce::Justification::centred);
    g.setColour (text);
    g.setFont (font (20.0f, true));
    g.drawText (juce::String::fromUTF8 ("入れたいショートカットを押してください"), t.removeFromTop (40), juce::Justification::centred);
    g.setColour (sub);
    g.setFont (font (13.0f));
    g.drawText (juce::String::fromUTF8 ("例: \xe2\x8c\x98 + T 、 \xe2\x87\xa7 + R など（Logic のキーコマンドと同じものを押す）"),
                t.removeFromTop (24), juce::Justification::centred);
    g.drawText (juce::String::fromUTF8 ("Esc でやめる"), t.removeFromTop (22), juce::Justification::centred);

    auto br = learnCancel.toFloat();
    g.setColour (card);
    g.fillRoundedRectangle (br, br.getHeight() / 2);
    g.setColour (line.brighter (0.3f));
    g.drawRoundedRectangle (br.reduced (0.5f), br.getHeight() / 2, 1.0f);
    g.setColour (text);
    g.setFont (font (13.0f, true));
    g.drawText (juce::String::fromUTF8 ("やめる"), learnCancel, juce::Justification::centred);
}

// ============================================================================
void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < 9; ++i)
        if (tiles[i].contains (e.getPosition())) h = i;
    hoverTile = h;
    const bool clickable = h >= 0 || statusPill.contains (e.getPosition()) || bannerButton.contains (e.getPosition())
                        || logicOnlyPill.contains (e.getPosition()) || onTopPill.contains (e.getPosition())
                        || settingsPill.contains (e.getPosition()) || prevPage.contains (e.getPosition())
                        || nextPage.contains (e.getPosition())
                        || std::any_of (std::begin (pageTabs), std::end (pageTabs), [&] (auto& r) { return r.contains (e.getPosition()); });
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
        if (learnCancel.contains (p)) { tap.learning = false; learnPage = learnIndex = -1; repaint(); }
        return;
    }

    if (bannerButton.contains (p))
    {
        KeyTap::requestPermission();
        KeyTap::openPermissionSettings();
        return;
    }
    if (statusPill.contains (p))   { deck.enabled = ! deck.enabled; deck.save(); repaint(); return; }
    if (prevPage.contains (p))     { deck.page = (deck.page + Deck::kPages - 1) % Deck::kPages; deck.save(); repaint(); return; }
    if (nextPage.contains (p))     { deck.page = (deck.page + 1) % Deck::kPages; deck.save(); repaint(); return; }
    for (int i = 0; i < Deck::kPages; ++i)
        if (pageTabs[i].contains (p)) { deck.page = i; deck.save(); repaint(); return; }
    if (logicOnlyPill.contains (p)) { deck.logicOnly = ! deck.logicOnly; deck.save(); repaint(); return; }
    if (onTopPill.contains (p))     { deck.alwaysOnTop = ! deck.alwaysOnTop; deck.save(); applyOnTop(); repaint(); return; }
    if (settingsPill.contains (p))  { showSettingsMenu(); return; }
    for (int i = 0; i < 9; ++i)
        if (tiles[i].contains (p)) { showTileMenu (i); return; }
}

void MainComponent::showTileMenu (int index)
{
    const int page = deck.page;
    const auto& current = deck.slots[(size_t) page][(size_t) index];

    juce::PopupMenu m;
    m.addSectionHeader (Deck::pageName (page) + juce::String::fromUTF8 (" ページ ・ テンキー ") + juce::String (index + 1));

    std::map<int, juce::String> ids;
    int id = 1;
    for (auto cat : { Cat::Transport, Cat::Edit, Cat::Track, Cat::Window, Cat::File })
    {
        juce::PopupMenu sub;
        bool any = false;
        for (auto& a : catalog())
        {
            if (a.cat != cat) continue;
            const bool sel = current.actionId == a.id;
            any = any || sel;
            sub.addItem (id, jp (a.name) + "    " + shortcutText (a.keyCode, a.mods), true, sel);
            ids[id++] = a.id;
        }
        m.addSubMenu (catName (cat), sub, true, nullptr, any);
    }
    m.addSeparator();
    m.addItem (1000, juce::String::fromUTF8 ("ショートカットを覚えさせる\xe2\x80\xa6"), true, current.actionId == "custom");
    if (current.actionId == "custom")
        m.addItem (1001, juce::String::fromUTF8 ("名前を変える\xe2\x80\xa6"));
    m.addItem (1002, juce::String::fromUTF8 ("なし（普通の数字キーとして使う）"), true, current.actionId.isEmpty());

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (tiles[index])),
                     [safe, page, index, ids] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         auto& self = *safe;
                         if (r == 1000) { self.startLearning (page, index); return; }
                         if (r == 1001) { self.renameSlot (page, index); return; }
                         Slot s;
                         if (r != 1002)
                         {
                             auto it = ids.find (r);
                             if (it == ids.end()) return;
                             s.actionId = it->second;
                         }
                         self.deck.setSlot (page, index, s);
                         self.repaint();
                     });
}

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("ON / OFF を切り替えるテンキー"));
    m.addItem (1, juce::String::fromUTF8 ("Clear（NumLock）"), true, deck.toggleKey == Deck::ToggleClear);
    m.addItem (2, "=", true, deck.toggleKey == Deck::ToggleEquals);
    m.addItem (3, juce::String::fromUTF8 ("なし（画面のボタンだけ）"), true, deck.toggleKey == Deck::ToggleNone);
    m.addSeparator();
    m.addItem (10, juce::String::fromUTF8 ("このページを最初の割り当てに戻す"));
    m.addItem (11, juce::String::fromUTF8 ("全部のページを最初の割り当てに戻す"));
    m.addSeparator();
    m.addItem (20, juce::String::fromUTF8 ("アクセシビリティの設定を開く"));

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (settingsPill)),
                     [safe] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         auto& d = safe->deck;
                         if (r >= 1 && r <= 3) d.toggleKey = r - 1;
                         if (r == 10) d.resetPage (d.page);
                         if (r == 11) d.resetAll();
                         if (r == 20) KeyTap::openPermissionSettings();
                         d.save();
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
    tap.learning = true;
    repaint();
}

void MainComponent::renameSlot (int page, int index)
{
    const auto current = deck.resolve (page, index).name;
    renameWindow = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("名前を変える"),
                                                        juce::String::fromUTF8 ("タイルに出す名前"),
                                                        juce::MessageBoxIconType::NoIcon);
    renameWindow->addTextEditor ("name", current);
    renameWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameWindow->addButton (juce::String::fromUTF8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    renameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, page, index] (int r)
    {
        if (safe == nullptr || safe->renameWindow == nullptr) return;
        if (r == 1)
        {
            auto s = safe->deck.slots[(size_t) page][(size_t) index];
            s.label = safe->renameWindow->getTextEditorContents ("name").trim();
            safe->deck.setSlot (page, index, s);
        }
        safe->renameWindow->setVisible (false);
        safe->repaint();
    }), false);
}
