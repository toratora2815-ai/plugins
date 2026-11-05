#include "PluginEditor.h"
#include "Targets.h"

namespace
{
    const juce::Colour bg (0xff111418), panel (0xff1a1f26), grid (0xff2a313b),
                       zone (0xff3d6b8f), curve (0xfff2f2f2), live (0xff5fd1c8),
                       ok (0xff5fd18a), warn (0xffffa040), refCol (0xffff8fb0);
    constexpr float fMin = 20.0f, fMax = 20000.0f, dbRange = 18.0f;
    constexpr float diffRange = 12.0f, refTol = 2.0f;   // DIFF 表示の範囲と、参照との「同じくらい」の幅
    const char* slotNames[] = { "A", "B", "C" };
}

BalanceScopeEditor::BalanceScopeEditor (BalanceScopeProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f))
        centers.push_back (f);

    int id = 1;
    for (auto& t : kTargets) targetBox.addItem (t.name, id++);
    targetBox.addItem ("Custom", id++);
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "target", targetBox);

    resetButton.onClick = [this] { processor.requestReset(); };
    addAndMakeVisible (resetButton);

    // 参照カーブ
    compareBox.addItem ("COMPARE: OFF", 1);
    for (int i = 0; i < numRefs; ++i) compareBox.addItem (juce::String ("REF ") + slotNames[i], i + 2);
    compareBox.onChange = [this] { setCompareSlot (compareBox.getSelectedId() - 2); };
    addAndMakeVisible (compareBox);

    captureButton.setTooltip ("Save the current AVERAGE as the reference curve (play the reference song first)");
    captureButton.onClick = [this] { capture(); };
    addAndMakeVisible (captureButton);
    clearButton.onClick = [this] { clearRef(); };
    addAndMakeVisible (clearButton);
    diffButton.setClickingTogglesState (true);
    diffButton.setTooltip ("Show only the difference from the reference");
    diffButton.setColour (juce::TextButton::buttonOnColourId, refCol);
    diffButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    diffButton.onClick = [this] { diffView = diffButton.getToggleState(); repaint(); };
    addAndMakeVisible (diffButton);

    // CUSTOM の目標カーブ
    auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const char* text)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 18);
        s.setColour (juce::Slider::trackColourId, zone.brighter());
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addChildComponent (s);
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, curve.withAlpha (0.7f));
        addChildComponent (l);
    };
    setupSlider (tiltSlider, tiltLabel, "CUSTOM TILT");
    setupSlider (lowSlider, lowLabel, "CUSTOM LOW");
    tiltAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "customTilt", tiltSlider);
    lowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "customLow", lowSlider);

    updateRefControls();

    // Plugin Link の帯（いちばん下）
    linkBar.status = [this] (bool& lit)
    {
        const auto li = processor.getLinkInfo();
        if (! li.on) return juce::String ("Plugin Link OFF  -  set the same LINK number on Velvet Tamer to send the harsh band to its FOCUS");
        juce::String who = li.peers > 0 ? juce::String (li.peers) + " linked plugin" + (li.peers > 1 ? "s" : "") : juce::String ("no other plugin on this LINK yet");
        if (! li.valid) return "Listening...  (" + who + ")";
        if (li.excessDb <= 0.0f) return "Balanced - nothing to send  (" + who + ")";
        lit = li.peers > 0;
        return "Sending: too much around " + plink::hzText (li.peakHz) + " (+" + juce::String (li.excessDb, 1) + " dB)  ->  "
             + plink::hzText (li.lowHz) + " - " + plink::hzText (li.highHz) + "   (" + who + ")";
    };
    addAndMakeVisible (linkBar);

    setResizable (true, true);
    setResizeLimits (760, 510, 1800, 1130);
    setSize (940, 632);
    startTimerHz (15);
}

//==============================================================================
// 参照カーブ
juce::ValueTree BalanceScopeEditor::refsTree()
{
    return processor.apvts.state.getOrCreateChildWithName ("REFS", nullptr);
}

std::vector<float> BalanceScopeEditor::loadRef (int slot)
{
    std::vector<float> out;
    if (slot < 0 || slot >= numRefs) return out;
    juce::StringArray parts;
    parts.addTokens (refsTree().getProperty (juce::String ("data") + slotNames[slot]).toString(), ",", {});
    if (parts.size() != (int) centers.size()) return out;
    for (auto& s : parts) out.push_back (s.getFloatValue());
    return out;
}

juce::String BalanceScopeEditor::refInfo (int slot)
{
    if (slot < 0 || slot >= numRefs) return {};
    auto t = refsTree();
    const auto key = juce::String (slotNames[slot]);
    if (! t.hasProperty ("data" + key)) return "REF " + key + ": empty  -  play a reference song, then CAPTURE";
    return "REF " + key + ":  " + t.getProperty ("secs" + key).toString() + " s analysed,  captured "
         + t.getProperty ("when" + key).toString();
}

int BalanceScopeEditor::compareSlot()
{
    return juce::jlimit (-1, numRefs - 1, (int) processor.apvts.state.getProperty ("compare", -1));
}

void BalanceScopeEditor::setCompareSlot (int slot)
{
    processor.apvts.state.setProperty ("compare", juce::jlimit (-1, numRefs - 1, slot), nullptr);
    updateRefControls();
}

void BalanceScopeEditor::capture()
{
    if (! hasData || avgRaw.size() != centers.size())
    {
        flashText = "Nothing to capture yet - press play first";
        flashTicks = 45;
        return;
    }
    int slot = compareSlot();
    if (slot < 0)
    {
        slot = 0;   // 比べていないときは、空いている最初のスロット（全部埋まっていれば A）
        for (int i = 0; i < numRefs; ++i)
            if (! refsTree().hasProperty (juce::String ("data") + slotNames[i])) { slot = i; break; }
    }

    juce::StringArray parts;
    for (float v : avgRaw) parts.add (juce::String (juce::jmax (-250.0f, v), 2));
    const auto key = juce::String (slotNames[slot]);
    auto t = refsTree();
    t.setProperty ("data" + key, parts.joinIntoString (","), nullptr);
    t.setProperty ("secs" + key, juce::String (snap.frames * (double) BalanceScopeProcessor::hopSize / snap.sampleRate, 1), nullptr);
    t.setProperty ("when" + key, juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M"), nullptr);

    setCompareSlot (slot);
    flashText = "Captured to REF " + key + "  -  press RESET and play your mix to compare";
    flashTicks = 60;
}

void BalanceScopeEditor::clearRef()
{
    const int slot = compareSlot();
    if (slot < 0) return;
    const auto key = juce::String (slotNames[slot]);
    auto t = refsTree();
    for (auto* name : { "data", "secs", "when" }) t.removeProperty (name + key, nullptr);
    updateRefControls();
}

void BalanceScopeEditor::updateRefControls()
{
    const int slot = compareSlot();
    compareBox.setSelectedId (slot + 2, juce::dontSendNotification);
    clearButton.setEnabled (slot >= 0 && refsTree().hasProperty (juce::String ("data") + slotNames[slot]));
    diffButton.setEnabled (slot >= 0);
    shownSlot = slot;
}

//==============================================================================
std::vector<float> BalanceScopeEditor::toBands (const std::vector<double>& power, double sampleRate) const
{
    return bandmath::toBands (power, sampleRate, BalanceScopeProcessor::fftSize, centers);
}

static void normalise (std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t)
{
    bandmath::normalise (db, centers, t);
}

void BalanceScopeEditor::timerCallback()
{
    processor.getSnapshot (snap);
    const auto t = processor.currentTarget();

    hasData = snap.frames > 0;
    avgRaw = toBands (snap.avgPower, snap.sampleRate);
    avgDb  = avgRaw;
    liveDb = toBands (snap.livePower, snap.sampleRate);
    normalise (avgDb, centers, t);
    normalise (liveDb, centers, t);

    // プロジェクトを開き直したときなどに表示を合わせる
    if (compareSlot() != shownSlot) updateRefControls();

    refDb = loadRef (compareSlot());
    if (! refDb.empty()) normalise (refDb, centers, t);

    for (int b = 0; b < 4; ++b)
    {
        double dev = 0.0, tol = 0.0; int n = 0;
        for (size_t k = 0; k < centers.size(); ++k)
        {
            const float f = centers[k];
            if (f < kBroadBands[b].lo || f >= kBroadBands[b].hi || avgDb[k] < -250.0f) continue;
            if (! refDb.empty())
            {
                if (refDb[k] < -250.0f) continue;
                dev += avgDb[k] - refDb[k];   // 参照曲と比べる
                tol += refTol;
            }
            else
            {
                dev += avgDb[k] - targetDb (t, f);
                tol += targetTol (t, f);
            }
            ++n;
        }
        broadDev[b] = n > 0 ? (float) (dev / n) : 0.0f;
        broadTol[b] = n > 0 ? (float) (tol / n) : 3.0f;
    }

    const bool custom = (int) processor.apvts.getRawParameterValue ("target")->load() == kCustomTarget;
    for (juce::Component* c : { (juce::Component*) &tiltSlider, (juce::Component*) &lowSlider, (juce::Component*) &tiltLabel, (juce::Component*) &lowLabel })
        c->setVisible (custom);

    if (flashTicks > 0) --flashTicks;
    repaint();
}

void BalanceScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (curve);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("BALANCE SCOPE", 16, 8, 220, 32, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (curve.withAlpha (0.7f));
    const double secs = snap.frames * (double) BalanceScopeProcessor::hopSize / snap.sampleRate;
    const juce::String status = juce::String (snap.playing ? "ANALYZING" : "PAUSED")
                              + "   " + juce::String (secs, 1) + " s";
    g.drawText (status, 240, 8, 220, 32, juce::Justification::centredLeft);

    // 参照カーブの説明（またはひとこと）
    g.setFont (juce::FontOptions (12.0f));
    if (flashTicks > 0) { g.setColour (refCol); g.drawText (flashText, refInfoArea, juce::Justification::centredLeft); }
    else if (compareSlot() >= 0)
    {
        g.setColour (refDb.empty() ? curve.withAlpha (0.5f) : refCol);
        g.drawText (refInfo (compareSlot()), refInfoArea, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (curve.withAlpha (0.45f));
        g.drawText ("Play a reference song and press CAPTURE to compare your mix with it", refInfoArea, juce::Justification::centredLeft);
    }

    if (diffView && ! refDb.empty()) drawDiff (g, graphArea.toFloat());
    else                             drawGraph (g, graphArea.toFloat());
    drawBroad (g, broadArea.toFloat());
    drawLoudness (g, loudArea.toFloat());
}

void BalanceScopeEditor::drawGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (36, 14).withTrimmedBottom (14);

    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yOf = [&] (float db, float centreDb) { return r.getCentreY() - (db - centreDb) / dbRange * r.getHeight() * 0.5f; };

    const auto t = processor.currentTarget();

    // 見やすさのため、目標カーブの傾きを差し引かず、1kHzの目標値を中心に表示
    const float centreDb = targetDb (t, 1000.0f);

    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.setColour (grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (curve.withAlpha (0.5f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    (int) x - 20, (int) r.getBottom() + 2, 40, 14, juce::Justification::centred);
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yOf ((float) db + centreDb, centreDb);
        g.setColour (grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (curve.withAlpha (0.5f));
        g.drawText (juce::String (db), (int) r.getX() - 34, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }

    // 目標範囲（参照と比べているときは薄く）
    const bool comparing = ! refDb.empty();
    juce::Path zonePath;
    for (size_t k = 0; k < centers.size(); ++k)
    {
        const float f = centers[k];
        const juce::Point<float> pt (xOf (f), yOf (targetDb (t, f) + targetTol (t, f), centreDb));
        k == 0 ? zonePath.startNewSubPath (pt) : zonePath.lineTo (pt);
    }
    for (size_t k = centers.size(); k-- > 0;)
        zonePath.lineTo (xOf (centers[k]), yOf (targetDb (t, centers[k]) - targetTol (t, centers[k]), centreDb));
    zonePath.closeSubPath();
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (zone.withAlpha (comparing ? 0.15f : 0.35f));
    g.fillPath (zonePath);

    auto drawCurve = [&] (const std::vector<float>& db, juce::Colour c, float w)
    {
        juce::Path p; bool started = false;
        for (size_t k = 0; k < db.size(); ++k)
        {
            if (db[k] < -250.0f) continue;
            const juce::Point<float> pt (xOf (centers[k]), yOf (db[k], centreDb));
            started ? p.lineTo (pt) : p.startNewSubPath (pt);
            started = true;
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    if (! liveDb.empty()) drawCurve (liveDb, live.withAlpha (0.55f), 1.2f);
    if (comparing)        drawCurve (refDb, refCol, 2.0f);
    if (hasData)          drawCurve (avgDb, curve, 2.5f);
    g.restoreState();

    // 凡例
    g.setFont (juce::FontOptions (11.0f));
    auto legend = juce::Rectangle<float> (r.getRight() - 250, r.getY(), 250, 14);
    g.setColour (zone.brighter()); g.drawText ("TARGET", legend.removeFromRight (46), juce::Justification::centredRight);
    g.setColour (live);            g.drawText ("LIVE",   legend.removeFromRight (36), juce::Justification::centredRight);
    g.setColour (curve);           g.drawText ("AVERAGE", legend.removeFromRight (64), juce::Justification::centredRight);
    if (comparing) { g.setColour (refCol); g.drawText ("REF " + juce::String (slotNames[compareSlot()]), legend.removeFromRight (52), juce::Justification::centredRight); }
}

// 参照との差だけを表示（上 = 自分のミックスのほうが多い）
void BalanceScopeEditor::drawDiff (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (36, 14).withTrimmedBottom (14);

    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yOf = [&] (float db) { return r.getCentreY() - juce::jlimit (-diffRange, diffRange, db) / diffRange * r.getHeight() * 0.5f; };

    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.setColour (grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (curve.withAlpha (0.5f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    (int) x - 20, (int) r.getBottom() + 2, 40, 14, juce::Justification::centred);
    }
    for (int db = -12; db <= 12; db += 3)
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? curve.withAlpha (0.4f) : grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (curve.withAlpha (0.5f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), (int) r.getX() - 34, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }

    // ±2 dB は「同じくらい」
    g.setColour (refCol.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yOf (refTol), r.getRight(), yOf (-refTol)));

    if (hasData)
    {
        juce::Path p; bool started = false;
        for (size_t k = 0; k < centers.size(); ++k)
        {
            if (avgDb[k] < -250.0f || refDb[k] < -250.0f) continue;
            const juce::Point<float> pt (xOf (centers[k]), yOf (avgDb[k] - refDb[k]));
            started ? p.lineTo (pt) : p.startNewSubPath (pt);
            started = true;
        }
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (refCol);
        g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    }

    g.setColour (refCol);
    g.drawText ("MIX - REF " + juce::String (slotNames[compareSlot()]) + "   (up = your mix has more)",
                juce::Rectangle<float> (r.getRight() - 300, r.getY(), 300, 14), juce::Justification::centredRight);
}

void BalanceScopeEditor::drawBroad (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);

    // 何と比べているか
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (refDb.empty() ? zone.brighter() : refCol);
    g.drawText (refDb.empty() ? "vs TARGET" : "vs REF " + juce::String (slotNames[compareSlot()]),
                r.removeFromTop (22).reduced (10, 0), juce::Justification::centredLeft);

    r = r.reduced (10, 6);

    const float colW = r.getWidth() / 4.0f;
    const float range = 12.0f;

    for (int b = 0; b < 4; ++b)
    {
        auto col = juce::Rectangle<float> (r.getX() + colW * b, r.getY(), colW, r.getHeight());
        auto label = col.removeFromBottom (18);
        auto value = col.removeFromBottom (18);
        auto meter = col.reduced (colW * 0.3f, 4);

        auto yOf = [&] (float db) { return meter.getCentreY() - juce::jlimit (-range, range, db) / range * meter.getHeight() * 0.5f; };

        g.setColour (grid);
        g.fillRoundedRectangle (meter, 3.0f);
        g.setColour ((refDb.empty() ? zone : refCol).withAlpha (0.5f));
        const float zt = yOf (broadTol[b]), zb = yOf (-broadTol[b]);
        g.fillRect (meter.getX(), zt, meter.getWidth(), zb - zt);
        g.setColour (curve.withAlpha (0.3f));
        g.drawHorizontalLine ((int) meter.getCentreY(), meter.getX(), meter.getRight());

        const bool inside = std::abs (broadDev[b]) <= broadTol[b];
        const juce::Colour c = ! hasData ? curve.withAlpha (0.3f) : (inside ? ok : warn);
        if (hasData)
        {
            const float y = yOf (broadDev[b]);
            g.setColour (c);
            g.fillRoundedRectangle (meter.getX() - 4, y - 3, meter.getWidth() + 8, 6, 3.0f);
        }

        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (c);
        g.drawText (hasData ? (broadDev[b] > 0 ? "+" : "") + juce::String (broadDev[b], 1) + " dB" : "--",
                    value.toNearestInt(), juce::Justification::centred);
        g.setColour (curve.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (kBroadBands[b].name, label.toNearestInt(), juce::Justification::centred);
    }
}

// ラウドネス（下の帯）
void BalanceScopeEditor::drawLoudness (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (12, 6);

    const auto& m = processor.getLoudness();
    const float mom = m.getMomentary(), st = m.getShortTerm(), integ = m.getIntegrated(), pk = m.getPeak();
    auto fmt = [] (float v) { return v > -100.0f ? juce::String (v, 1) : juce::String ("--"); };
    const juce::String plr = (integ > -100.0f && pk > -100.0f) ? juce::String (pk - integ, 1) : juce::String ("--");

    struct Item { const char* name; juce::String value; const char* unit; juce::Colour c; };
    const Item items[] = {
        { "INTEGRATED", fmt (integ), "LUFS", curve },
        { "SHORT-TERM", fmt (st),    "LUFS", live },
        { "MOMENTARY",  fmt (mom),   "LUFS", live.withAlpha (0.7f) },
        { "PEAK",       fmt (pk),    "dBFS", pk > -0.1f ? warn : curve.withAlpha (0.85f) },
        { "PLR",        plr,         "dB",   curve.withAlpha (0.85f) },
    };

    // CUSTOM のつまみがあるときは右側をあける
    auto row = r;
    if (tiltSlider.isVisible()) row.removeFromRight (330);
    const float w = row.getWidth() / (float) std::size (items);
    for (auto& it : items)
    {
        auto cell = row.removeFromLeft (w);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (curve.withAlpha (0.5f));
        g.drawText (it.name, cell.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (it.c);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        auto valueRect = cell.removeFromLeft (cell.getWidth() * 0.62f);
        g.drawText (it.value, valueRect, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (curve.withAlpha (0.5f));
        g.drawText (it.unit, cell, juce::Justification::centredLeft);
    }
}

void BalanceScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    linkBar.setBounds (area.removeFromBottom (plink::LinkBar::height));
    area.removeFromBottom (8);
    auto header = area.removeFromTop (36);
    resetButton.setBounds (header.removeFromRight (80).reduced (2, 4));
    header.removeFromRight (8);
    targetBox.setBounds (header.removeFromRight (240).reduced (2, 4));

    // 参照カーブの行
    auto refRow = area.removeFromTop (34);
    compareBox.setBounds (refRow.removeFromLeft (160).reduced (2, 4));
    refRow.removeFromLeft (6);
    captureButton.setBounds (refRow.removeFromLeft (90).reduced (2, 4));
    clearButton.setBounds (refRow.removeFromLeft (70).reduced (2, 4));
    diffButton.setBounds (refRow.removeFromLeft (64).reduced (2, 4));
    refRow.removeFromLeft (10);
    refInfoArea = refRow;

    // ラウドネスの帯（下）と CUSTOM のつまみ
    area.removeFromBottom (0);
    loudArea = area.removeFromBottom (58);
    {
        auto c = loudArea.reduced (12, 6).removeFromRight (320);
        auto top = c.removeFromTop (c.getHeight() / 2), bottom = c;
        tiltLabel.setBounds (top.removeFromLeft (90));
        tiltSlider.setBounds (top);
        lowLabel.setBounds (bottom.removeFromLeft (90));
        lowSlider.setBounds (bottom);
    }
    area.removeFromBottom (10);

    area.removeFromTop (4);
    broadArea = area.removeFromRight (juce::jmax (220, area.getWidth() / 4));
    area.removeFromRight (10);
    graphArea = area;
}
