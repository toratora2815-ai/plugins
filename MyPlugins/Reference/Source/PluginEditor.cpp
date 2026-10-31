#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff111418), panel (0xff1a1f26), grid (0xff2a313b), text (0xfff2f2f2),
                       mixCol (0xff8f86ff), refCol (0xffff8a4c), neutral (0xff3d6b8f),
                       good (0xff5fd18a), warn (0xffffa040), bad (0xffff5a5a);
    constexpr float fMin = 20.0f, fMax = 20000.0f;

    juce::String fmt (float v, int decimals = 1, bool plus = false)
    {
        if (! std::isfinite (v) || v <= -99.0f) return "--";
        const juce::String num = decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return (plus && v > 0.0f ? "+" : "") + num;
    }
}

// ============================================================================
Segmented::Segmented (juce::RangedAudioParameter& p, const juce::StringArray& labels,
                      std::vector<juce::Colour> onColours, float fontSize)
    : attachment (p, [this] (float v) { setIndex (juce::roundToInt (v)); })
{
    juce::ignoreUnused (fontSize);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setColour (juce::TextButton::buttonOnColourId, onColours[(size_t) juce::jmin (i, (int) onColours.size() - 1)]);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.08f));
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < labels.size() - 1) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges (edges);
        b->onClick = [this, i]
        {
            attachment.setValueAsCompleteGesture ((float) i);
            if (onChange) onChange (i);
        };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void Segmented::setIndex (int idx)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void Segmented::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / juce::jmax (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == buttons.size() - 1 ? r : r.removeFromLeft (w));
}

// ============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel.brighter (0.05f));
    g.fillRoundedRectangle (r, 4.0f);

    const auto* a = proc.player.getAudio();
    g.setFont (juce::FontOptions (12.0f));

    if (proc.isLoading() || a == nullptr)
    {
        g.setColour (text.withAlpha (0.6f));
        const auto status = proc.getFileStatus();
        g.drawText (status.isNotEmpty() ? status : "Click LOAD or drop an audio file here (WAV / AIFF / MP3 / M4A)",
                    r.reduced (8, 0), juce::Justification::centred);
        return;
    }

    const auto& ov = a->overview;
    const float mid = r.getCentreY(), h = r.getHeight() * 0.42f;
    const float pos = proc.player.position.load();
    const int w = (int) r.getWidth();
    for (int x = 0; x < w; ++x)
    {
        const float v = ov[(size_t) juce::jlimit (0, (int) ov.size() - 1, (int) ((float) x / (float) w * (float) ov.size()))];
        g.setColour ((float) x / (float) w <= pos ? refCol.withAlpha (0.9f) : refCol.withAlpha (0.35f));
        g.drawVerticalLine ((int) r.getX() + x, mid - v * h, mid + v * h);
    }
    g.setColour (text);
    g.fillRect (r.getX() + pos * r.getWidth() - 1.0f, r.getY(), 2.0f, r.getHeight());

    const double total = a->buffer.getNumSamples() / a->sampleRate;
    auto mmss = [] (double s) { return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2); };
    g.setColour (text.withAlpha (0.85f));
    g.drawText (a->name, r.reduced (8, 2), juce::Justification::topLeft);
    g.drawText (mmss (pos * total) + " / " + mmss (total), r.reduced (8, 2), juce::Justification::topRight);
}

void WaveformView::seekTo (const juce::MouseEvent& e)
{
    if (proc.player.getAudio() != nullptr)
        proc.player.seek ((float) e.x / (float) juce::jmax (1, getWidth()));
}

// ============================================================================
ReferenceEditor::ReferenceEditor (ReferenceProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      sourceSel (*p.apvts.getParameter ("source"), { "FILE", "LIVE" }, { neutral.brighter (0.6f) }),
      abSel     (*p.apvts.getParameter ("ab"), { "MY MIX", "REFERENCE" }, { mixCol, refCol }, 18.0f),
      levelSel  (*p.apvts.getParameter ("level"), { "OFF", "MATCH", "-14 LUFS" }, { good }),
      bandSel   (*p.apvts.getParameter ("band"), { "FULL", "LOW", "MID", "HIGH" }, { neutral.brighter (0.6f) }),
      stereoSel (*p.apvts.getParameter ("stereo"), { "STEREO", "MONO", "SIDE" }, { neutral.brighter (0.6f) }),
      waveform (p)
{
    lnf.setColourScheme ({ bg, panel, panel, grid, text, neutral, text, neutral, text });
    setLookAndFeel (&lnf);

    for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f))
        centers.push_back (f);

    for (auto* c : std::initializer_list<juce::Component*> { &sourceSel, &abSel, &levelSel, &bandSel, &stereoSel,
                                                             &resetButton, &loadButton, &playButton, &loopButton,
                                                             &followButton, &waveform, &deviceBox, &refreshButton, &trimSlider })
        addAndMakeVisible (c);

    sourceSel.onChange = [this] (int) { updateSourceVisibility(); };
    resetButton.onClick = [this] { proc.resetMeters(); };

    loadButton.onClick = [this] { chooseFile(); };
    playButton.onClick = [this]
    {
        if (auto* f = proc.apvts.getParameter ("follow"); f != nullptr && f->getValue() > 0.5f)
        {
            f->setValueNotifyingHost (0.0f);
            proc.player.userPlaying = true;
        }
        else
            proc.player.userPlaying = ! proc.player.userPlaying.load();
    };
    for (auto* b : { &loopButton, &followButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, neutral.brighter (0.6f));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
    loopAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "loop", loopButton);
    followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "follow", followButton);

    deviceBox.setTextWhenNothingSelected ("Choose input device (BlackHole 2ch)");
    deviceBox.onChange = [this]
    {
        const auto name = deviceBox.getText();
        if (name.isNotEmpty() && name != proc.getLiveDeviceName()) proc.setLiveDevice (name);
    };
    refreshButton.onClick = [this] { refreshDevices(); proc.retryLive(); };

    trimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    trimSlider.setTextValueSuffix (" dB");
    trimSlider.setColour (juce::Slider::thumbColourId, refCol);
    trimSlider.setColour (juce::Slider::trackColourId, refCol.withAlpha (0.5f));
    trimAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "trim", trimSlider);
    trimSlider.setDoubleClickReturnValue (true, 0.0);

    refreshDevices();
    updateSourceVisibility();

    setResizable (true, true);
    setResizeLimits (860, 560, 1800, 1100);
    setSize (980, 640);
    startTimerHz (25);
}

ReferenceEditor::~ReferenceEditor()
{
    setLookAndFeel (nullptr);
}

void ReferenceEditor::refreshDevices()
{
    const auto names = proc.getLiveDeviceNames();
    deviceBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        deviceBox.addItem (names[i], i + 1);
    const int idx = names.indexOf (proc.getLiveDeviceName());
    if (idx >= 0) deviceBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void ReferenceEditor::updateSourceVisibility()
{
    shownSource = (int) proc.apvts.getRawParameterValue ("source")->load();
    const bool live = shownSource == ReferenceProcessor::liveSource;
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &playButton, &loopButton, &followButton, &waveform })
        c->setVisible (! live);
    deviceBox.setVisible (live);
    refreshButton.setVisible (live);
    if (live) refreshDevices();
    repaint();
}

void ReferenceEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.flac;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadFile (f);
                          });
}

bool ReferenceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;m4a;aac;flac;caf")) return true;
    return false;
}

void ReferenceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;m4a;aac;flac;caf"))
        {
            if (auto* s = proc.apvts.getParameter ("source")) s->setValueNotifyingHost (0.0f);
            proc.loadFile (juce::File (f));
            updateSourceVisibility();
            return;
        }
}

// ----------------------------------------------------------------------------
std::vector<float> ReferenceEditor::toBands (const std::vector<double>& power, double sampleRate) const
{
    const double bw = sampleRate / SpectrumAnalyser::size;
    const int maxBin = (int) power.size() - 1;
    const double norm = 20.0 * std::log10 (SpectrumAnalyser::size / 4.0);   // 正弦波フルスケール ≒ 0dB
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
            const double pos = fc / bw;
            const int i0 = juce::jlimit (1, maxBin - 1, (int) pos);
            const double fr = juce::jlimit (0.0, 1.0, pos - i0);
            density = power[(size_t) i0] * (1.0 - fr) + power[(size_t) i0 + 1] * fr;
        }
        out[k] = (float) (10.0 * std::log10 (density * (hi - lo) / bw + 1.0e-30) - norm);
    }
    return out;
}

void ReferenceEditor::timerCallback()
{
    if ((int) proc.apvts.getRawParameterValue ("source")->load() != shownSource)
        updateSourceVisibility();

    double rate = 48000.0;
    mixHas = proc.mixSpec.getSnapshot (snapBuf, rate);
    mixDb = toBands (snapBuf, rate);
    refHas = proc.refSpec.getSnapshot (snapBuf, rate);
    refDb = toBands (snapBuf, rate);

    // いま聴いている音量に合わせてカーブをずらす
    const float gm = proc.mixGainDb.load(), gr = proc.refGainDb.load();
    float top = -200.0f;
    for (size_t k = 0; k < centers.size(); ++k)
    {
        if (mixDb[k] > -250.0f) { mixDb[k] += gm; if (mixHas) top = juce::jmax (top, mixDb[k]); }
        if (refDb[k] > -250.0f) { refDb[k] += gr; if (refHas) top = juce::jmax (top, refDb[k]); }
    }
    if (top > -150.0f)
    {
        const float target = std::ceil ((top + 3.0f) / 6.0f) * 6.0f;
        displayTop += 0.15f * (target - displayTop);
        if (std::abs (target - displayTop) < 0.3f) displayTop = target;
    }

    // 音色の差（全体の音量差は除く：100Hz〜8kHz の平均で揃える）
    diffDb.assign (centers.size(), -300.0f);
    if (mixHas && refHas)
    {
        double sum = 0.0; int cnt = 0;
        std::vector<float> raw (centers.size(), 0.0f);
        for (size_t k = 0; k < centers.size(); ++k)
        {
            if (mixDb[k] < -250.0f || refDb[k] < -250.0f) continue;
            raw[k] = mixDb[k] - refDb[k];
            if (centers[k] >= 100.0f && centers[k] <= 8000.0f) { sum += raw[k]; ++cnt; }
        }
        const float off = cnt > 0 ? (float) (sum / cnt) : 0.0f;
        for (size_t k = 0; k < centers.size(); ++k)
        {
            if (mixDb[k] < -250.0f || refDb[k] < -250.0f) continue;
            // 隣の帯域と軽くならす
            const size_t a = k > 0 ? k - 1 : k, b = k + 1 < centers.size() ? k + 1 : k;
            const bool okA = mixDb[a] > -250.0f && refDb[a] > -250.0f, okB = mixDb[b] > -250.0f && refDb[b] > -250.0f;
            diffDb[k] = (raw[k] * 2.0f + (okA ? raw[a] : raw[k]) + (okB ? raw[b] : raw[k])) * 0.25f - off;
        }
    }

    // 再生ボタンの表示
    const bool follow = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f;
    const bool running = follow ? proc.hostPlaying.load() : proc.player.userPlaying.load();
    playButton.setButtonText (follow ? (running ? "DAW: PLAYING" : "DAW: STOPPED") : (running ? "PAUSE" : "PLAY"));

    // ライブ入力の状態
    if ((int) proc.apvts.getRawParameterValue ("source")->load() == ReferenceProcessor::liveSource)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (proc.getLiveError().isNotEmpty())
        {
            liveStatus = "Couldn't open device: " + proc.getLiveError() + "  (press REFRESH to retry)";
            liveStatusColour = bad;
        }
        else if (proc.getLiveDeviceName().isEmpty())
        {
            liveStatus = "BlackHole not found. Install BlackHole 2ch, then press REFRESH (see README).";
            liveStatusColour = warn;
        }
        else if (! proc.live.isOpen())
        {
            liveStatus = "Opening " + proc.getLiveDeviceName() + " ...";
            liveStatusColour = text.withAlpha (0.7f);
        }
        else
        {
            const float pk = proc.live.getInputPeakDb();
            if (pk > -80.0f) silentSince = now;
            if (now - silentSince > 2.0)
            {
                liveStatus = "No sound from " + proc.getLiveDeviceName()
                           + ". Set Mac sound output to it and press play in Spotify / Apple Music.";
                liveStatusColour = warn;
            }
            else
            {
                liveStatus = "Receiving from " + proc.getLiveDeviceName() + "   peak " + fmt (pk) + " dBFS";
                liveStatusColour = good;
            }
        }
        if (deviceBox.getText() != proc.getLiveDeviceName()) refreshDevices();
    }
    repaint();
}

// ----------------------------------------------------------------------------
void ReferenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("REFERENCE", headerArea.withWidth (160), juce::Justification::centredLeft);

    g.setColour (panel);
    g.fillRoundedRectangle (sourceArea.toFloat(), 6.0f);

    const bool live = (int) proc.apvts.getRawParameterValue ("source")->load() == ReferenceProcessor::liveSource;
    if (live)
    {
        auto r = sourceArea.withLeft (refreshButton.getRight() + 14).reduced (0, 6);
        g.setColour (liveStatusColour);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (liveStatus, r, juce::Justification::centredLeft, 2);
    }

    drawSpectrum (g, specArea.toFloat());
    drawMeters (g, meterArea.toFloat());

    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("LEVEL",    levelLabel,  juce::Justification::centredLeft);
    g.drawText ("BAND SOLO", bandLabel,  juce::Justification::centredLeft);
    g.drawText ("STEREO",   stereoLabel, juce::Justification::centredLeft);
    g.drawText ("REF TRIM", trimLabel,   juce::Justification::centredLeft);
}

void ReferenceEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (36, 12).withTrimmedBottom (14);
    auto main = r.removeFromTop (r.getHeight() * 0.68f);
    r.removeFromTop (18);
    auto diff = r;

    auto xOf = [&] (float f) { return main.getX() + main.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    const float range = 60.0f;
    auto yMain = [&] (float db) { return main.getY() + (displayTop - db) / range * main.getHeight(); };
    const float diffRange = 12.0f;
    auto yDiff = [&] (float db) { return diff.getCentreY() - juce::jlimit (-diffRange, diffRange, db) / diffRange * diff.getHeight() * 0.5f; };

    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.setColour (grid);
        g.drawVerticalLine ((int) x, main.getY(), main.getBottom());
        g.drawVerticalLine ((int) x, diff.getY(), diff.getBottom());
        g.setColour (text.withAlpha (0.5f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    (int) x - 20, (int) diff.getBottom() + 2, 40, 14, juce::Justification::centred);
    }
    for (int i = 0; i <= 60; i += 12)
    {
        const float db = displayTop - (float) i, y = yMain (db);
        g.setColour (grid);
        g.drawHorizontalLine ((int) y, main.getX(), main.getRight());
        g.setColour (text.withAlpha (0.5f));
        g.drawText (juce::String (juce::roundToInt (db)), (int) main.getX() - 34, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }
    for (int db : { -12, -6, 0, 6, 12 })
    {
        const float y = yDiff ((float) db);
        g.setColour (db == 0 ? text.withAlpha (0.35f) : grid);
        g.drawHorizontalLine ((int) y, diff.getX(), diff.getRight());
        g.setColour (text.withAlpha (0.5f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), (int) diff.getX() - 34, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }

    auto curve = [&] (const std::vector<float>& db, juce::Rectangle<float> clip, std::function<float (float)> yOf,
                      juce::Colour c, float w, bool fill)
    {
        juce::Path p; bool started = false; float lastX = 0;
        for (size_t k = 0; k < db.size() && k < centers.size(); ++k)
        {
            if (db[k] < -250.0f) continue;
            const juce::Point<float> pt (xOf (centers[k]), yOf (db[k]));
            if (! started) { p.startNewSubPath (pt); started = true; }
            else p.lineTo (pt);
            lastX = pt.x;
        }
        if (! started) return;
        g.saveState();
        g.reduceClipRegion (clip.toNearestInt());
        if (fill)
        {
            juce::Path f (p);
            f.lineTo (lastX, clip.getBottom());
            f.lineTo (xOf (centers[0]), clip.getBottom());
            f.closeSubPath();
            g.setColour (c.withAlpha (0.12f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    };

    if (refHas) curve (refDb, main, yMain, refCol, 2.0f, true);
    if (mixHas) curve (mixDb, main, yMain, mixCol, 2.0f, true);
    if (mixHas && refHas) curve (diffDb, diff, yDiff, text.withAlpha (0.9f), 1.8f, false);
    else
    {
        g.setColour (text.withAlpha (0.4f));
        g.drawText ("Play both MY MIX and REFERENCE once to see the difference", diff.toNearestInt(), juce::Justification::centred);
    }

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (mixCol); g.drawText ("MY MIX",    (int) main.getRight() - 160, (int) main.getY() + 2, 70, 14, juce::Justification::centredRight);
    g.setColour (refCol); g.drawText ("REFERENCE", (int) main.getRight() - 80,  (int) main.getY() + 2, 80, 14, juce::Justification::centredRight);
    g.setColour (text.withAlpha (0.7f));
    g.drawText ("MY MIX - REFERENCE (tone only)   above 0 = your mix has more", diff.toNearestInt().withHeight (14).translated (0, -16),
                juce::Justification::centredLeft);
}

void ReferenceEditor::drawMeters (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (12, 10).toNearestInt();

    auto& m = proc.mixMeter;
    auto& f = proc.refMeter;
    struct Row { const char* name; float a, b; int dec; bool diff; };
    const float mixPsr = m.signalPresent ? m.peakShort.load() - m.shortTerm.load() : -100.0f;
    const float refPsr = f.signalPresent ? f.peakShort.load() - f.shortTerm.load() : -100.0f;
    const bool mOk = m.signalPresent.load(), fOk = f.signalPresent.load();
    const Row rows[] = {
        { "SHORT-TERM LUFS", mOk ? m.shortTerm.load() : -100.0f, fOk ? f.shortTerm.load() : -100.0f, 1, true },
        { "INTEGRATED LUFS", m.gatedSeconds > 0.0f ? m.integrated.load() : -100.0f, f.gatedSeconds > 0.0f ? f.integrated.load() : -100.0f, 1, true },
        { "PEAK (3s) dBFS",  mOk ? m.peakShort.load() : -100.0f, fOk ? f.peakShort.load() : -100.0f, 1, true },
        { "MAX PEAK dBFS",   m.peakMax.load() > -119.0f ? m.peakMax.load() : -100.0f, f.peakMax.load() > -119.0f ? f.peakMax.load() : -100.0f, 1, false },
        { "DYNAMICS (PSR)",  mixPsr, refPsr, 1, true },
        { "CORRELATION",     mOk ? m.correlation.load() : -100.0f, fOk ? f.correlation.load() : -100.0f, 2, false },
        { "SIDE %",          mOk ? m.sideRatio.load() * 100.0f : -100.0f, fOk ? f.sideRatio.load() * 100.0f : -100.0f, 0, true },
        { "GAIN APPLIED dB", proc.mixGainDb.load(), proc.refGainDb.load(), 1, false },
    };

    const int rowH = juce::jmin (30, r.getHeight() / 9);
    const int nameW = r.getWidth() - 3 * 58;
    auto header = r.removeFromTop (rowH);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    header.removeFromLeft (nameW);
    g.setColour (mixCol);               g.drawText ("MIX",  header.removeFromLeft (58), juce::Justification::centredRight);
    g.setColour (refCol);               g.drawText ("REF",  header.removeFromLeft (58), juce::Justification::centredRight);
    g.setColour (text.withAlpha (0.6f)); g.drawText ("DIFF", header.removeFromLeft (58), juce::Justification::centredRight);

    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowH);
        g.setColour (grid);
        g.drawHorizontalLine (line.getY(), (float) line.getX(), (float) line.getRight());
        g.setColour (text.withAlpha (0.65f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (row.name, line.removeFromLeft (nameW), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (mixCol.brighter (0.3f));
        g.drawText (fmt (row.a, row.dec, row.name[0] == 'G'), line.removeFromLeft (58), juce::Justification::centredRight);
        g.setColour (refCol.brighter (0.2f));
        g.drawText (fmt (row.b, row.dec, row.name[0] == 'G'), line.removeFromLeft (58), juce::Justification::centredRight);
        g.setColour (text.withAlpha (0.8f));
        const bool both = row.a > -99.0f && row.b > -99.0f;
        g.drawText (row.diff && both ? fmt (row.a - row.b, row.dec, true) : "", line.removeFromLeft (58), juce::Justification::centredRight);
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.5f));
    g.drawFittedText ("DIFF = MIX - REF.  PSR: lower = more compressed.  Press RESET METERS when you change the song or section.",
                      r.withTrimmedTop (4), juce::Justification::topLeft, 3);
}

void ReferenceEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    headerArea = area.removeFromTop (34);
    {
        auto h = headerArea;
        h.removeFromLeft (170);
        sourceSel.setBounds (h.removeFromLeft (180).reduced (0, 3));
        resetButton.setBounds (h.removeFromRight (130).reduced (0, 3));
    }

    area.removeFromTop (8);
    sourceArea = area.removeFromTop (56);
    {
        auto s = sourceArea.reduced (10, 10);
        loadButton.setBounds (s.removeFromLeft (70));   s.removeFromLeft (6);
        playButton.setBounds (s.removeFromLeft (110));  s.removeFromLeft (6);
        loopButton.setBounds (s.removeFromLeft (60));   s.removeFromLeft (6);
        followButton.setBounds (s.removeFromLeft (100)); s.removeFromLeft (10);
        waveform.setBounds (s);

        auto d = sourceArea.reduced (10, 12);
        deviceBox.setBounds (d.removeFromLeft (280)); d.removeFromLeft (6);
        refreshButton.setBounds (d.removeFromLeft (80));
    }

    area.removeFromTop (8);
    abSel.setBounds (area.removeFromTop (52));

    area.removeFromTop (8);
    bottomArea = area.removeFromBottom (54);
    area.removeFromBottom (8);
    meterArea = area.removeFromRight (360);
    area.removeFromRight (8);
    specArea = area;

    {
        auto b = bottomArea;
        const int gap = 14;
        auto col = [&] (int w, juce::Rectangle<int>& label)
        {
            auto c = b.removeFromLeft (w);
            b.removeFromLeft (gap);
            label = c.removeFromTop (16);
            return c.reduced (0, 2);
        };
        levelSel.setBounds  (col (240, levelLabel));
        bandSel.setBounds   (col (240, bandLabel));
        stereoSel.setBounds (col (200, stereoLabel));
        trimSlider.setBounds (col (juce::jmax (120, b.getWidth()), trimLabel));
    }
}
