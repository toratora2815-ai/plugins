#include "PluginEditor.h"
#include "Targets.h"

namespace
{
    const juce::Colour bg (0xff111418), panel (0xff1a1f26), grid (0xff2a313b),
                       zone (0xff3d6b8f), curve (0xfff2f2f2), live (0xff5fd1c8),
                       ok (0xff5fd18a), warn (0xffffa040);
    constexpr float fMin = 20.0f, fMax = 20000.0f, dbRange = 18.0f;
}

BalanceScopeEditor::BalanceScopeEditor (BalanceScopeProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f))
        centers.push_back (f);

    int id = 1;
    for (auto& t : kTargets) targetBox.addItem (t.name, id++);
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "target", targetBox);

    resetButton.onClick = [this] { processor.requestReset(); };
    addAndMakeVisible (resetButton);

    setResizable (true, true);
    setResizeLimits (700, 400, 1800, 1000);
    setSize (920, 500);
    startTimerHz (15);
}

std::vector<float> BalanceScopeEditor::toBands (const std::vector<double>& power, double sampleRate) const
{
    const double bw = sampleRate / BalanceScopeProcessor::fftSize;
    const int maxBin = (int) power.size() - 1;
    std::vector<float> out (centers.size(), -300.0f);

    for (size_t k = 0; k < centers.size(); ++k)
    {
        const double fc = centers[k];
        if (fc > sampleRate * 0.45) break;
        const double lo = fc * std::pow (2.0, -1.0 / 12.0), hi = fc * std::pow (2.0, 1.0 / 12.0);
        const int b0 = (int) std::ceil (lo / bw), b1 = std::min (maxBin, (int) std::floor (hi / bw));

        double density = 0.0;
        if (b1 >= b0 && b0 >= 1)
        {
            for (int b = b0; b <= b1; ++b) density += power[(size_t) b];
            density /= (b1 - b0 + 1);
        }
        else
        {
            // 低域でビンが帯域に入らないときは近くのビンを補間
            const double pos = fc / bw;
            const int i0 = juce::jlimit (1, maxBin - 1, (int) pos);
            const double fr = juce::jlimit (0.0, 1.0, pos - i0);
            density = power[(size_t) i0] * (1.0 - fr) + power[(size_t) i0 + 1] * fr;
        }
        out[k] = (float) (10.0 * std::log10 (density * (hi - lo) / bw + 1.0e-30));
    }
    return out;
}

// 目標カーブに合わせて全体の音量差を取り除く（100Hz〜8kHzの平均で揃える）
static void normalise (std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t)
{
    double sum = 0.0; int n = 0;
    for (size_t k = 0; k < db.size(); ++k)
        if (centers[k] >= 100.0f && centers[k] <= 8000.0f && db[k] > -250.0f)
        { sum += db[k] - targetDb (t, centers[k]); ++n; }
    const float off = n > 0 ? (float) (sum / n) : 0.0f;
    for (auto& v : db) v -= off;
}

void BalanceScopeEditor::timerCallback()
{
    processor.getSnapshot (snap);
    const auto& t = kTargets[juce::jlimit (0, kNumTargets - 1,
                              (int) processor.apvts.getRawParameterValue ("target")->load())];

    hasData = snap.frames > 0;
    avgDb  = toBands (snap.avgPower,  snap.sampleRate);
    liveDb = toBands (snap.livePower, snap.sampleRate);
    normalise (avgDb, centers, t);
    normalise (liveDb, centers, t);

    for (int b = 0; b < 4; ++b)
    {
        double dev = 0.0, tol = 0.0; int n = 0;
        for (size_t k = 0; k < centers.size(); ++k)
        {
            const float f = centers[k];
            if (f < kBroadBands[b].lo || f >= kBroadBands[b].hi || avgDb[k] < -250.0f) continue;
            dev += avgDb[k] - targetDb (t, f);
            tol += targetTol (t, f);
            ++n;
        }
        broadDev[b] = n > 0 ? (float) (dev / n) : 0.0f;
        broadTol[b] = n > 0 ? (float) (tol / n) : 3.0f;
    }
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

    drawGraph (g, graphArea.toFloat());
    drawBroad (g, broadArea.toFloat());
}

void BalanceScopeEditor::drawGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (36, 14).withTrimmedBottom (14);

    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yOf = [&] (float db, float centreDb) { return r.getCentreY() - (db - centreDb) / dbRange * r.getHeight() * 0.5f; };

    const auto& t = kTargets[juce::jlimit (0, kNumTargets - 1,
                              (int) processor.apvts.getRawParameterValue ("target")->load())];

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

    // 目標範囲
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
    g.setColour (zone.withAlpha (0.35f));
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
    if (hasData)          drawCurve (avgDb, curve, 2.5f);
    g.restoreState();

    // 凡例
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (curve);        g.drawText ("AVERAGE", (int) r.getRight() - 150, (int) r.getY(), 70, 14, juce::Justification::centredRight);
    g.setColour (live);         g.drawText ("LIVE",    (int) r.getRight() - 75,  (int) r.getY(), 30, 14, juce::Justification::centredRight);
    g.setColour (zone.brighter()); g.drawText ("TARGET", (int) r.getRight() - 42, (int) r.getY(), 42, 14, juce::Justification::centredRight);
}

void BalanceScopeEditor::drawBroad (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (10, 14);

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
        g.setColour (zone.withAlpha (0.5f));
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

void BalanceScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    resetButton.setBounds (header.removeFromRight (80).reduced (2, 4));
    header.removeFromRight (8);
    targetBox.setBounds (header.removeFromRight (240).reduced (2, 4));

    area.removeFromTop (8);
    broadArea = area.removeFromRight (juce::jmax (220, area.getWidth() / 4));
    area.removeFromRight (10);
    graphArea = area;
}
