#include "PluginEditor.h"

namespace col
{
    const juce::Colour bg     (0xff141417);
    const juce::Colour panel  (0xff1d1d22);
    const juce::Colour edge   (0xff2f2f37);
    const juce::Colour text   (0xffe8e4dc);
    const juce::Colour dim    (0xff8e8a83);
    const juce::Colour accent (0xffffb454);
    const juce::Colour teal   (0xff6fd3c8);
}

static juce::Colour trackColour (int slot)
{
    return juce::Colour::fromHSV (std::fmod (slot * 0.137f + 0.05f, 1.0f), 0.55f, 0.95f, 1.0f);
}

void MixField::timerCallback()
{
    shown = proc.tracks();
    if (drag >= 0)
        for (auto& t : shown) if (std::abs (t.token - dragToken) < 0.5f) { t.gainDb = dragState.gainDb; t.pan = dragState.pan; t.width = dragState.width; }
    repaint();
}

int MixField::hit (juce::Point<float> p) const
{
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int) shown.size(); ++i)
    {
        const auto& t = shown[(size_t) i];
        const float d = p.getDistanceFrom ({ panToX (t.pan), dbToY (t.gainDb) });
        if (d < radius (t) + 4 && d < bd) { bd = d; best = i; }
    }
    return best;
}

void MixField::paint (juce::Graphics& g)
{
    auto r = plot();
    g.setColour (col::bg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setFont (juce::FontOptions (11.0f));
    for (float d : { 10.0f, 0.0f, -10.0f, -20.0f, -30.0f, -40.0f })
    {
        const float y = dbToY (d);
        g.setColour (d == 0 ? col::edge.brighter (0.3f) : col::edge); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (col::dim); g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), 2, (int) y - 7, 26, 14, juce::Justification::centredRight);
    }
    for (float p : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        const float x = panToX (p);
        g.setColour (p == 0 ? col::edge.brighter (0.3f) : col::edge); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }
    g.setColour (col::dim);
    g.drawText ("L", (int) r.getX() - 4, (int) r.getBottom() + 2, 20, 14, juce::Justification::centredLeft);
    g.drawText ("R", (int) r.getRight() - 14, (int) r.getBottom() + 2, 20, 14, juce::Justification::centredRight);
    if (shown.empty())
    {
        g.setColour (col::dim); g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (juce::String::fromUTF8 ("ここに Forge Strip が出ます。\n各トラックの Forge Strip と、この Forge Mixer の LINK を同じ番号（1〜4）にしてください。"),
                          getLocalBounds().reduced (40), juce::Justification::centred, 3);
        return;
    }
    for (int i = 0; i < (int) shown.size(); ++i)
    {
        const auto& t = shown[(size_t) i];
        const float x = panToX (t.pan), y = dbToY (t.gainDb), rad = radius (t);
        const auto c = trackColour (t.slot);
        // 音量で明るさ（-60〜0 dB）
        const float lit = juce::jlimit (0.15f, 1.0f, (t.levelDb + 60.0f) / 60.0f);
        g.setColour (c.withAlpha (0.18f + 0.35f * lit));
        g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
        g.setColour (t.mute ? col::dim : c);
        g.drawEllipse (x - rad, y - rad, rad * 2, rad * 2, i == drag ? 3.0f : 1.8f);
        g.setColour (col::text); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (t.name, (int) (x - 80), (int) (y + rad + 2), 160, 14, juce::Justification::centred);
    }
}

void MixField::mouseDown (const juce::MouseEvent& e)
{
    drag = hit (e.position);
    if (drag >= 0) { dragState = shown[(size_t) drag]; dragToken = dragState.token; }
}

void MixField::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    dragState.pan = e.mods.isShiftDown() ? dragState.pan : xToPan (e.position.x);   // Shift = 上下だけ
    dragState.gainDb = yToDb (e.position.y);
    proc.command (dragToken, dragState.gainDb, dragState.pan, dragState.width);
}

void MixField::mouseUp (const juce::MouseEvent&) { drag = -1; }

void MixField::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h >= 0) proc.command (shown[(size_t) h].token, 0.0f, 0.0f, 0.0f);
}

void MixField::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int h = hit (e.position);
    if (h < 0) return;
    auto t = shown[(size_t) h];
    t.width = juce::jlimit (-100.0f, 100.0f, t.width + w.deltaY * 60.0f);
    proc.command (t.token, t.gainDb, t.pan, t.width);
}

// =====================================================================
ForgeMixerEditor::ForgeMixerEditor (ForgeMixerProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (field);
    addAndMakeVisible (linkBar);
    linkBar.status = [this] (bool& lit)
    {
        const int n = (int) field.shown.size();
        lit = n > 0;
        if (! processor.link.on()) return juce::String::fromUTF8 ("LINK を 1〜4 にしてください");
        return juce::String (n) + juce::String::fromUTF8 (" トラック。点をドラッグ = 音量とパン、ホイール = 幅、ダブルクリック = 元に戻す");
    };
    setSize (980, 600);
    startTimerHz (10);
}

void ForgeMixerEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::accent); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FORGE MIXER", 14, 8, 300, 26, juce::Justification::centredLeft);
    // 右の一覧（数値と 3 帯域の音量）
    auto a = listArea;
    g.setColour (col::panel); g.fillRoundedRectangle (a.toFloat(), 8.0f);
    a = a.reduced (10, 8);
    g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("トラック　　音量　パン　幅　　低 / 中 / 高"), a.removeFromTop (18), juce::Justification::centredLeft);
    for (const auto& t : field.shown)
    {
        auto row = a.removeFromTop (30);
        if (row.getBottom() > listArea.getBottom()) break;
        g.setColour (trackColour (t.slot)); g.fillEllipse (row.getX() + 0.0f, row.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (col::text); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (t.name, row.getX() + 14, row.getY(), 70, row.getHeight(), juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ((t.gainDb > 0 ? "+" : "") + juce::String (t.gainDb, 1), row.getX() + 86, row.getY(), 40, row.getHeight(), juce::Justification::centredRight);
        const int pp = juce::roundToInt (t.pan * 100);
        g.drawText (pp == 0 ? "C" : (pp < 0 ? "L" + juce::String (-pp) : "R" + juce::String (pp)), row.getX() + 128, row.getY(), 36, row.getHeight(), juce::Justification::centredRight);
        g.drawText (juce::String (juce::roundToInt (t.width)), row.getX() + 166, row.getY(), 32, row.getHeight(), juce::Justification::centredRight);
        // 3 帯域
        const float lv[3] = { t.low, t.mid, t.high };
        for (int k = 0; k < 3; ++k)
        {
            auto bar = juce::Rectangle<float> ((float) row.getX() + 206 + k * 26, (float) row.getY() + 9, 22, 12);
            g.setColour (col::edge); g.fillRect (bar);
            const float f = juce::jlimit (0.0f, 1.0f, (lv[k] + 60.0f) / 60.0f);
            g.setColour (k == 0 ? col::accent : (k == 1 ? col::text.withAlpha (0.8f) : col::teal));
            g.fillRect (bar.withWidth (bar.getWidth() * f));
        }
    }
}

void ForgeMixerEditor::resized()
{
    auto r = getLocalBounds();
    linkBar.setBounds (r.removeFromBottom (plink::LinkBar::height));
    r.removeFromTop (40);
    r = r.reduced (12, 6);
    listArea = r.removeFromRight (300);
    r.removeFromRight (10);
    field.setBounds (r);
}
