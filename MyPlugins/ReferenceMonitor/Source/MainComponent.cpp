#include "MainComponent.h"

namespace
{
    const juce::Colour bg (0xff111418), panel (0xff1a1f26), grid (0xff2a313b), text (0xfff2f2f2),
                       mixCol (0xff8f86ff), refCol (0xffff8a4c), neutral (0xff3d6b8f),
                       good (0xff5fd18a), warn (0xffffa040), bad (0xffff5a5a);
    const juce::Colour selCol = neutral.brighter (0.6f);
    constexpr float fMin = 20.0f, fMax = 20000.0f;
    const char* kAudioExt = "wav;aif;aiff;mp3;m4a;aac;flac;caf";

    juce::String fmt (float v, int decimals = 1, bool plus = false)
    {
        if (! std::isfinite (v) || v <= -99.0f) return "--";
        const juce::String num = decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return (plus && v > 0.0f ? "+" : "") + num;
    }

    juce::String mmss (double s)
    {
        s = juce::jmax (0.0, s);
        return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2);
    }

    void styleToggle (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
}

// ============================================================================
Seg::Seg (const juce::StringArray& labels, std::vector<juce::Colour> onColours, std::function<void (int)> cb)
    : onSelect (std::move (cb))
{
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
        b->onClick = [this, i] { setIndex (i); if (onSelect) onSelect (i); };
        addAndMakeVisible (b);
    }
}

void Seg::setIndex (int idx)
{
    index = idx;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void Seg::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / juce::jmax (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == buttons.size() - 1 ? r : r.removeFromLeft (w));
}

// ============================================================================
MainComponent::MainComponent()
    : abSeg ({ "MY MIX  (Logic)", "REFERENCE" }, { mixCol, refCol }, [this] (int i) { setAB (i == 1); }),
      levelSeg ({ "OFF", "MATCH", "-14 LUFS" }, { good }, [this] (int i) { engine.level = i; saveSettings(); }),
      bandSeg ({ "FULL", "LOW", "MID", "HIGH" }, { selCol }, [this] (int i) { engine.band = i; saveSettings(); }),
      stereoSeg ({ "STEREO", "MONO", "SIDE" }, { selCol }, [this] (int i) { engine.stereo = i; saveSettings(); }),
      outSeg ({ "A", "B" }, { selCol }, [this] (int) { saveSettings(); captureError.clear(); retryAt = 0; })
{
    for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f))
        centers.push_back (f);

    refList.setRowHeight (30);
    refList.setColour (juce::ListBox::backgroundColourId, panel);
    refList.setOutlineThickness (0);

    for (auto* c : std::initializer_list<juce::Component*> {
             &refList, &addButton, &removeButton, &liveAppBox, &abSeg, &levelSeg, &bandSeg, &stereoSeg, &outSeg,
             &playButton, &loopButton, &restartButton, &addCueButton, &setMixButton, &resetButton, &dimButton, &muteButton,
             &pinButton, &bypassButton, &trimSlider, &volumeSlider, &outABox, &outBBox })
    {
        addAndMakeVisible (c);
        noFocus (*c);
    }
    for (auto* s : { &abSeg, &levelSeg, &bandSeg, &stereoSeg, &outSeg })
        for (auto* b : s->buttons) noFocus (*b);

    styleToggle (loopButton, selCol);
    setMixButton.setColour (juce::TextButton::textColourOffId, mixCol.brighter (0.4f));
    styleToggle (restartButton, selCol);
    styleToggle (dimButton, warn);
    styleToggle (muteButton, bad);
    styleToggle (pinButton, selCol);
    styleToggle (bypassButton, warn);

    addButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add reference tracks", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.flac;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  juce::StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  addFiles (paths);
                              });
    };
    removeButton.onClick = [this] { removeSelected(); };

    liveAppBox.onChange = [this]
    {
        const int id = liveAppBox.getSelectedId();
        if (id <= 0) return;
        const auto b = liveBundles[id - 1];
        if (b.isNotEmpty() && b != liveBundle) { liveBundle = b; saveSettings(); retryAt = 0; refList.repaint(); }
    };

    playButton.onClick = [this] { togglePlay(); };
    loopButton.onClick = [this] { loopOn = ! loopOn; updateLoop(); saveSettings(); explainLoop(); };
    restartButton.onClick = [this] { restartOnSwitch = ! restartOnSwitch; saveSettings(); };
    addCueButton.onClick = [this] { addCue(); };
    setMixButton.onClick = [this] { setMixPosition (activeCue, false); };
    resetButton.onClick = [this] { engine.resetMeters(); };
    dimButton.onClick = [this] { engine.dim = ! engine.dim.load(); };
    muteButton.onClick = [this] { engine.mute = ! engine.mute.load(); };
    pinButton.onClick = [this]
    {
        pinButton.setToggleState (! pinButton.getToggleState(), juce::dontSendNotification);
        if (auto* top = getTopLevelComponent()) top->setAlwaysOnTop (pinButton.getToggleState());
        saveSettings();
    };
    bypassButton.onClick = [this]
    {
        bypass = ! bypass;
        if (bypass) capture.stop();
        retryAt = 0;
    };

    trimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    trimSlider.setRange (-12.0, 12.0, 0.1);
    trimSlider.setTextValueSuffix (" dB");
    trimSlider.setDoubleClickReturnValue (true, 0.0);
    trimSlider.setColour (juce::Slider::thumbColourId, refCol);
    trimSlider.onValueChange = [this] { engine.trimDb = (float) trimSlider.getValue(); saveSettings(); };

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    volumeSlider.setRange (-60.0, 6.0, 0.5);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.onValueChange = [this] { engine.volumeDb = (float) volumeSlider.getValue(); saveSettings(); };

    outABox.onChange = [this] { outA = outUIDs[outABox.getSelectedId() - 1]; saveSettings(); retryAt = 0; };
    outBBox.onChange = [this] { outB = outUIDs[outBBox.getSelectedId() - 1]; saveSettings(); retryAt = 0; };

    library.load();
    loadSettings();
    refreshOutputs();
    refreshLiveApps();
    refList.updateContent();
    refList.selectRow (selectedRow, juce::dontSendNotification);
    selectItem (selectedRow);

    setWantsKeyboardFocus (true);
    setSize (1180, 780);
    startTimerHz (25);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSettings();
    capture.stop();
    loader.removeAllJobs (true, 5000);
}

void MainComponent::noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
}

// ----------------------------------------------------------------------------
// 設定

void MainComponent::loadSettings()
{
    auto& s = library.settings;
    engine.level  = (int) s.getWithDefault ("level", 1);
    engine.band   = (int) s.getWithDefault ("band", 0);
    engine.stereo = (int) s.getWithDefault ("stereo", 0);
    engine.trimDb = (float) (double) s.getWithDefault ("trim", 0.0);
    engine.volumeDb = (float) (double) s.getWithDefault ("volume", 0.0);
    outA = s.getWithDefault ("outA", "").toString();
    outB = s.getWithDefault ("outB", "").toString();
    loopOn = (bool) s.getWithDefault ("loop", false);
    restartOnSwitch = (bool) s.getWithDefault ("restart", true);
    liveBundle = s.getWithDefault ("liveBundle", "com.apple.Music").toString();
    selectedRow = juce::jlimit (0, (int) library.files.size(), (int) s.getWithDefault ("selected", 0));
    pinButton.setToggleState ((bool) s.getWithDefault ("pin", false), juce::dontSendNotification);

    levelSeg.setIndex (engine.level);
    bandSeg.setIndex (engine.band);
    stereoSeg.setIndex (engine.stereo);
    outSeg.setIndex ((int) s.getWithDefault ("outSel", 0));
    abSeg.setIndex (0);
    trimSlider.setValue (engine.trimDb.load(), juce::dontSendNotification);
    volumeSlider.setValue (engine.volumeDb.load(), juce::dontSendNotification);
}

void MainComponent::saveSettings()
{
    auto& s = library.settings;
    s.set ("level", engine.level.load());
    s.set ("band", engine.band.load());
    s.set ("stereo", engine.stereo.load());
    s.set ("trim", (double) engine.trimDb.load());
    s.set ("volume", (double) engine.volumeDb.load());
    s.set ("outA", outA);
    s.set ("outB", outB);
    s.set ("outSel", outSeg.getIndex());
    s.set ("loop", loopOn);
    s.set ("restart", restartOnSwitch);
    s.set ("liveBundle", liveBundle);
    s.set ("selected", selectedRow);
    s.set ("pin", pinButton.getToggleState());
    library.save();
}

juce::String MainComponent::outputUID() const
{
    const auto& u = outSeg.getIndex() == 1 ? outB : outA;
    return u.isNotEmpty() ? u : CaptureEngine::defaultOutputUID();
}

void MainComponent::refreshOutputs()
{
    if (outABox.isPopupActive() || outBBox.isPopupActive()) return;
    const auto devs = CaptureEngine::listOutputDevices();
    juce::StringArray uids { juce::String() }, names { "System default" };
    for (auto& d : devs) { uids.add (d.uid); names.add (d.name); }
    if (uids == outUIDs && outABox.getNumItems() > 0) return;
    outUIDs = uids;
    for (auto* box : { &outABox, &outBBox })
    {
        const auto& cur = box == &outABox ? outA : outB;
        box->clear (juce::dontSendNotification);
        int sel = 1;
        for (int i = 0; i < names.size(); ++i)
        {
            box->addItem (names[i], i + 1);
            if (uids[i] == cur) sel = i + 1;
        }
        box->setSelectedId (sel, juce::dontSendNotification);
    }
}

void MainComponent::refreshLiveApps()
{
    if (liveAppBox.isPopupActive()) return;
    juce::StringArray names { "Apple Music" }, bundles { "com.apple.Music" };
    for (auto& p : CaptureEngine::listAudioProcesses())
    {
        if (p.bundleId.isEmpty() || CaptureEngine::matches (p.bundleId, mixBundle) || bundles.contains (p.bundleId)) continue;
        names.add (p.name);
        bundles.add (p.bundleId);
    }
    if (! bundles.contains (liveBundle)) { names.add (liveBundle); bundles.add (liveBundle); }
    if (bundles == liveBundles && liveAppBox.getNumItems() > 0) return;

    liveBundles = bundles;
    liveAppBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
    {
        liveAppBox.addItem (names[i], i + 1);
        if (bundles[i] == liveBundle) liveAppBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
}

// ----------------------------------------------------------------------------
// 参照曲リスト

int MainComponent::getNumRows() { return 1 + (int) library.files.size(); }

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected) { g.setColour (refCol.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
    g.setFont (juce::FontOptions (13.0f));
    if (row == 0)
    {
        const auto name = liveAppBox.getText();
        g.setColour (good);
        g.fillEllipse (8.0f, (float) h * 0.5f - 4.0f, 8.0f, 8.0f);
        g.setColour (text);
        g.drawText ((name.isNotEmpty() ? name : juce::String ("Apple Music")) + "  (LIVE)", 24, 0, w - 28, h, juce::Justification::centredLeft);
        return;
    }
    if (row - 1 < (int) library.files.size())
    {
        const auto& f = library.files[(size_t) (row - 1)];
        const bool exists = juce::File (f.path).existsAsFile();
        g.setColour (exists ? text.withAlpha (0.9f) : bad);
        g.drawText (f.name + (exists ? "" : "  (missing)"), 24, 0, w - 28, h, juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.4f));
        g.fillRect (10, h / 2 - 5, 6, 10);
    }
}

void MainComponent::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    selectItem (row);
    grabKeyboardFocus();
}

void MainComponent::selectItem (int row)
{
    row = juce::jlimit (0, (int) library.files.size(), row);
    const bool wasPlaying = engine.player.playing.load();
    selectedRow = row;
    refList.selectRow (row, juce::dontSendNotification);
    activeCue = -1;
    engine.refSource = isLive() ? Engine::sourceLive : Engine::sourceFile;
    if (! isLive())
    {
        const auto& f = library.files[(size_t) (row - 1)];
        const auto* a = engine.player.getAudio();
        if (a == nullptr || a->path != f.path)
        {
            engine.player.playing = false;
            playAfterLoad = wasPlaying || engine.ab.load();
            loadFileAsync (f.path);
        }
    }
    else
        engine.player.playing = false;
    updateLoop();
    retryAt = 0;
    saveSettings();
    repaint();
}

void MainComponent::addFiles (const juce::StringArray& paths)
{
    int firstNew = -1;
    for (auto& p : paths)
    {
        juce::File f (p);
        if (! f.existsAsFile() || ! f.hasFileExtension (kAudioExt)) continue;
        bool dup = false;
        for (auto& it : library.files) if (it.path == p) dup = true;
        if (dup) continue;
        library.files.push_back ({ p, f.getFileNameWithoutExtension(), {} });
        if (firstNew < 0) firstNew = (int) library.files.size();
    }
    refList.updateContent();
    if (firstNew > 0) selectItem (firstNew);
    library.save();
}

void MainComponent::removeSelected()
{
    if (isLive()) return;
    library.files.erase (library.files.begin() + (selectedRow - 1));
    refList.updateContent();
    selectItem (juce::jmin (selectedRow, (int) library.files.size()));
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension (kAudioExt)) return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int) { addFiles (files); }

void MainComponent::loadFileAsync (const juce::String& path)
{
    loadingPath = path;
    loader.removeAllJobs (true, 2000);
    loader.addJob ([this, path]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (path)));
        std::unique_ptr<RefAudio> a;
        juce::String err;
        if (reader == nullptr) err = "Can't read: " + juce::File (path).getFileName();
        else
        {
            const auto maxLen = (juce::int64) (reader->sampleRate * 60.0 * 20.0);
            const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
            a = std::make_unique<RefAudio>();
            a->sampleRate = reader->sampleRate;
            a->path = path;
            a->name = juce::File (path).getFileNameWithoutExtension();
            a->buffer.setSize (2, juce::jmax (4, len));
            a->buffer.clear();
            reader->read (&a->buffer, 0, len, 0, true, true);
            if (reader->numChannels == 1) a->buffer.copyFrom (1, 0, a->buffer, 0, 0, len);
            constexpr int bins = 1600;
            a->overview.resize (bins);
            float maxV = 1.0e-6f;
            for (int b = 0; b < bins; ++b)
            {
                const int s0 = (int) ((juce::int64) len * b / bins), s1 = (int) ((juce::int64) len * (b + 1) / bins);
                float pk = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    pk = juce::jmax (pk, a->buffer.getMagnitude (ch, s0, juce::jmax (1, s1 - s0)));
                a->overview[(size_t) b] = pk;
                maxV = juce::jmax (maxV, pk);
            }
            for (auto& v : a->overview) v /= maxV;
        }
        const juce::ScopedLock sl (pendingLock);
        pendingAudio = std::move (a);
        pendingError = err;
    });
}

void MainComponent::pickUpLoadedAudio()
{
    std::unique_ptr<RefAudio> next;
    juce::String err;
    {
        const juce::ScopedLock sl (pendingLock);
        next = std::move (pendingAudio);
        err = pendingError;
        pendingError.clear();
    }
    if (next != nullptr)
    {
        auto old = engine.player.swapAudio (std::move (next));
        loadingPath.clear();
        engine.resetRefMeters();
        updateLoop();
        if (playAfterLoad) engine.player.playing = true;
        playAfterLoad = false;
    }
    else if (err.isNotEmpty())
    {
        statusRef = err;
        loadingPath.clear();
    }
}

// ----------------------------------------------------------------------------
// 再生・キュー

std::vector<Cue>* MainComponent::currentCues()
{
    if (isLive())
        return musicState.trackId.isNotEmpty() ? &library.musicCues[musicState.trackId] : nullptr;
    if (selectedRow - 1 < (int) library.files.size())
        return &library.files[(size_t) (selectedRow - 1)].cues;
    return nullptr;
}

double MainComponent::currentPosition() const
{
    return isLive() ? musicState.position : engine.player.positionSec.load();
}

double MainComponent::currentLength() const
{
    return isLive() ? musicState.duration : engine.player.lengthSec.load();
}

void MainComponent::refPlay()
{
    if (isLive()) { music.play(); musicState.playing = true; }
    else engine.player.playing = true;
}

void MainComponent::refPause()
{
    if (isLive()) { music.pause(); musicState.playing = false; }
    else engine.player.playing = false;
}

bool MainComponent::refIsPlaying() const
{
    return isLive() ? musicState.playing : engine.player.playing.load();
}

void MainComponent::flash (const juce::String& msg)
{
    flashMessage = msg;
    flashUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
}

// いま聴いている側を再生／停止する（MY MIX = Logic、REFERENCE = 参照曲）
void MainComponent::togglePlay()
{
    if (! engine.ab.load())
    {
        if (! logic.hasTimecode())
            flash ("To play/stop Logic from here, turn on sync in Logic (see README: Logic setup).");
        if (logic.isPlaying()) logic.stop(); else logic.play();
        return;
    }
    if (isLive())
    {
        music.playPause();
        pollMusic();   // すぐに本当の状態を読み直す
    }
    else
        engine.player.playing = ! engine.player.playing.load();
}

// キューに「自分の曲（Logic）の位置」を登録する
void MainComponent::setMixPosition (int idx, bool clear)
{
    auto* cues = currentCues();
    if (cues == nullptr || idx < 0 || idx >= (int) cues->size())
    {
        flash ("Select a cue first (or add one with + CUE), then press SET MIX.");
        return;
    }
    if (clear) (*cues)[(size_t) idx].mixT = -1.0;
    else
    {
        if (! logic.hasTimecode())
        {
            flash ("Logic isn't sending its position yet. Turn on sync in Logic (see README: Logic setup).");
            return;
        }
        (*cues)[(size_t) idx].mixT = logic.getPosition();
        flash ("MY MIX position for \"" + (*cues)[(size_t) idx].name + "\" set to " + LogicLink::format ((*cues)[(size_t) idx].mixT));
    }
    library.save();
    cueSignature.clear();
    rebuildCueButtons();
}

void MainComponent::setAB (bool ref)
{
    // 切り替えは「聴く方を変えるだけ」。もう片方は止めずに流し続ける。
    // CUE ON SWITCH がオンでキューを選んでいるときは、切り替えた側だけキューの頭に戻す。
    engine.ab = ref;
    abSeg.setIndex (ref ? 1 : 0);
    auto* cues = currentCues();
    const bool cueReady = restartOnSwitch && cues != nullptr && activeCue >= 0 && activeCue < (int) cues->size();
    if (ref)
    {
        if (cueReady) seekTo ((*cues)[(size_t) activeCue].t);
        refPlay();
    }
    else if (cueReady && (*cues)[(size_t) activeCue].mixT >= 0.0)
    {
        engine.mixMeter.requestReset();
        engine.mixSpec.requestReset();
        logic.locateAndPlay ((*cues)[(size_t) activeCue].mixT);
    }
}

void MainComponent::seekTo (double seconds)
{
    if (isLive()) { music.setPosition (seconds); musicState.position = seconds; }
    else engine.player.seekSeconds (seconds);
    engine.resetRefMeters();
}

void MainComponent::jumpToCue (int idx, bool play)
{
    auto* cues = currentCues();
    if (cues == nullptr || idx < 0 || idx >= (int) cues->size()) return;
    activeCue = idx;
    const auto cue = (*cues)[(size_t) idx];
    seekTo (cue.t);
    const bool hasMix = cue.mixT >= 0.0;
    if (hasMix) { engine.mixMeter.requestReset(); engine.mixSpec.requestReset(); }

    if (play)
    {
        // キューを押したら、両方の曲がそれぞれのキューの位置から再生する
        refPlay();
        if (hasMix) logic.locateAndPlay (cue.mixT);
        else if (! engine.ab.load())
            flash ("This cue has no MY MIX position yet: move Logic to that part and press SET MIX.");
    }
    updateLoop();
}

void MainComponent::explainLoop()
{
    if (! loopOn) return;
    auto* cues = currentCues();
    if (cues == nullptr || activeCue < 0) { flash ("LOOP: pick a cue first. It loops from that cue to the next one."); return; }
    if (activeCue + 1 >= (int) cues->size()) { flash ("LOOP: this is the last cue, so the reference loops to the end. Add a cue after it to set the loop end."); return; }
    const bool mixOk = (*cues)[(size_t) activeCue].mixT >= 0.0 && (*cues)[(size_t) activeCue + 1].mixT >= 0.0;
    flash (mixOk ? "LOOP: both songs loop between cue " + juce::String (activeCue + 1) + " and cue " + juce::String (activeCue + 2) + "."
                 : "LOOP: only the reference loops. To loop Logic too, give both cue " + juce::String (activeCue + 1)
                   + " and cue " + juce::String (activeCue + 2) + " a MY MIX position (SET MIX).");
}

void MainComponent::updateLoop()
{
    loopButton.setToggleState (loopOn, juce::dontSendNotification);
    restartButton.setToggleState (restartOnSwitch, juce::dontSendNotification);
    auto* cues = currentCues();
    if (isLive() || cues == nullptr || activeCue < 0 || activeCue >= (int) cues->size())
    {
        engine.player.setLoop (false, 0.0, 0.0);
        return;
    }
    const double s = (*cues)[(size_t) activeCue].t;
    const double e = activeCue + 1 < (int) cues->size() ? (*cues)[(size_t) activeCue + 1].t : currentLength();
    engine.player.setLoop (loopOn, s, e);
}

void MainComponent::addCue()
{
    auto* cues = currentCues();
    if (cues == nullptr)
    {
        flashMessage = isLive() ? "Play a song in Apple Music first, then press + CUE."
                                : "Select a reference track first.";
        flashUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 4.0;
        return;
    }
    const double t = currentPosition();
    Cue c { "Cue " + juce::String ((int) cues->size() + 1), t };
    cues->push_back (c);
    Library::sortCues (*cues);
    for (int i = 0; i < (int) cues->size(); ++i)
        if (std::abs ((*cues)[(size_t) i].t - t) < 1.0e-6) activeCue = i;
    library.save();
    rebuildCueButtons();
    updateLoop();
    renameCue (activeCue);   // すぐ名前を付けられるように
}

void MainComponent::renameCue (int idx)
{
    auto* cues = currentCues();
    if (cues == nullptr || idx < 0 || idx >= (int) cues->size()) return;
    auto* w = new juce::AlertWindow ("Cue name", "Name this cue (e.g. Verse, Chorus). You can rename it later with right-click.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", (*cues)[(size_t) idx].name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, idx] (int res)
    {
        auto* c2 = currentCues();
        if (res == 1 && c2 != nullptr && idx < (int) c2->size())
        {
            const auto n = w->getTextEditorContents ("name").trim();
            if (n.isNotEmpty()) (*c2)[(size_t) idx].name = n;
            library.save();
            rebuildCueButtons();
        }
        grabKeyboardFocus();
    }), true);
}

void MainComponent::cueMenu (int idx)
{
    auto* cues = currentCues();
    if (cues == nullptr || idx < 0 || idx >= (int) cues->size()) return;

    juce::PopupMenu m;
    m.addSectionHeader ((*cues)[(size_t) idx].name + "  " + mmss ((*cues)[(size_t) idx].t));
    m.addItem (1, "Rename...");
    m.addItem (2, "Move to current reference position");
    m.addItem (4, "Set MY MIX position (Logic now)");
    m.addItem (5, "Clear MY MIX position", (*cues)[(size_t) idx].mixT >= 0.0);
    m.addSeparator();
    m.addItem (3, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, idx] (int r)
    {
        auto* cs = currentCues();
        if (cs == nullptr || idx >= (int) cs->size()) return;
        if (r == 1)
            renameCue (idx);
        else if (r == 2)
        {
            (*cs)[(size_t) idx].t = currentPosition();
            Library::sortCues (*cs);
            library.save();
            rebuildCueButtons();
            updateLoop();
        }
        else if (r == 4) setMixPosition (idx, false);
        else if (r == 5) setMixPosition (idx, true);
        else if (r == 3)
        {
            cs->erase (cs->begin() + idx);
            if (activeCue >= (int) cs->size()) activeCue = (int) cs->size() - 1;
            library.save();
            rebuildCueButtons();
            updateLoop();
        }
    });
}

void MainComponent::rebuildCueButtons()
{
    auto* cues = currentCues();
    juce::String sig = juce::String (selectedRow) + ":" + juce::String (activeCue) + ":";
    if (cues != nullptr) for (auto& c : *cues) sig += c.name + "@" + juce::String (c.t, 2) + "/" + juce::String (c.mixT, 2) + ";";
    if (sig == cueSignature) return;
    cueSignature = sig;

    cueButtons.clear();
    if (cues != nullptr)
        for (int i = 0; i < (int) cues->size(); ++i)
        {
            auto* b = cueButtons.add (new CueButton());
            b->setButtonText (juce::String (i + 1) + ". " + (*cues)[(size_t) i].name + ((*cues)[(size_t) i].mixT >= 0.0 ? "  +MIX" : ""));
            styleToggle (*b, refCol);
            b->setToggleState (i == activeCue, juce::dontSendNotification);
            b->onClick = [this, i] { jumpToCue (i, true); rebuildCueButtons(); };
            b->onRightClick = [this, i] { cueMenu (i); };
            noFocus (*b);
            addAndMakeVisible (b);
        }
    resized();
}

// ----------------------------------------------------------------------------
// 定期処理

// Logic 側のループ: 選んだキューの MIX 位置から、次のキューの MIX 位置までを繰り返す
void MainComponent::checkLogicLoop()
{
    if (! loopOn || ! logic.hasTimecode() || ! logic.isPlaying()) return;
    auto* cues = currentCues();
    if (cues == nullptr || activeCue < 0 || activeCue + 1 >= (int) cues->size()) return;
    const double s = (*cues)[(size_t) activeCue].mixT, e = (*cues)[(size_t) activeCue + 1].mixT;
    if (s < 0.0 || e <= s + 0.5) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now < logicLoopCooldown) return;
    const double pos = logic.getPosition();
    if (pos >= e - 0.05 && pos < e + 3.0)
    {
        logic.locateAndPlay (s);
        logicLoopCooldown = now + 0.8;   // 戻した直後の古い位置情報で、もう一度戻さないように
    }
}

void MainComponent::timerCallback()
{
    ++tick;
    pickUpLoadedAudio();
    checkLogicLoop();
    if (tick % 6 == 0) pollMusic();
    if (tick % 25 == 0) manageCapture();
    if (tick % 125 == 0) { refreshOutputs(); refreshLiveApps(); }

    // ボタンの状態
    if (engine.ab.load()) playButton.setButtonText (refIsPlaying() ? "PAUSE  (Ref)" : "PLAY  (Ref)");
    else                  playButton.setButtonText (logic.isPlaying() ? "STOP  (Logic)" : "PLAY  (Logic)");
    dimButton.setToggleState (engine.dim.load(), juce::dontSendNotification);
    muteButton.setToggleState (engine.mute.load(), juce::dontSendNotification);
    bypassButton.setToggleState (bypass, juce::dontSendNotification);
    abSeg.setIndex (engine.ab.load() ? 1 : 0);
    rebuildCueButtons();
    updateSpectra();
    repaint();
}

void MainComponent::pollMusic()
{
    if (! isLive() || ! CaptureEngine::matches ("com.apple.Music", liveBundle)) { musicState = {}; return; }
    const auto prevTrack = musicState.trackId;
    musicState = music.poll();
    if (musicState.trackId != prevTrack) { activeCue = -1; cueSignature.clear(); engine.resetRefMeters(); }

    // ライブのループ: キュー区間の終わりに来たら頭に戻す
    auto* cues = currentCues();
    if (loopOn && musicState.playing && cues != nullptr && activeCue >= 0 && activeCue < (int) cues->size())
    {
        const double s = (*cues)[(size_t) activeCue].t;
        const double e = activeCue + 1 < (int) cues->size() ? (*cues)[(size_t) activeCue + 1].t : musicState.duration;
        if (musicState.position >= e - 0.15 && musicState.position < e + 2.0)
        {
            music.setPosition (s);
            musicState.position = s;
        }
    }
}

void MainComponent::manageCapture()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (bypass)
    {
        if (capture.isRunning()) capture.stop();
        return;
    }
    if (now < retryAt) return;

    const auto out = outputUID();
    const auto refB = isLive() ? liveBundle : juce::String();
    const auto procs = CaptureEngine::listAudioProcesses();
    const auto sig = CaptureEngine::makeSignature (out, mixBundle, refB, procs);
    if (! capture.isRunning() || sig != capture.getSignature())
    {
        captureError = capture.start (out, mixBundle, refB);
        if (captureError.isNotEmpty()) retryAt = now + 5.0;
    }
    capture.getLog();   // 初回の動作記録をログに残す
}

// ----------------------------------------------------------------------------
// 操作

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { setAB (! engine.ab.load()); return true; }
    const auto c = k.getTextCharacter();
    if (c >= '1' && c <= '9') { jumpToCue (c - '1', true); return true; }
    if (c == 'l' || c == 'L') { loopOn = ! loopOn; updateLoop(); saveSettings(); explainLoop(); return true; }
    if (c == 'p' || c == 'P') { togglePlay(); return true; }
    if (c == 'c' || c == 'C') { addCue(); return true; }
    if (c == 's' || c == 'S') { setMixPosition (activeCue, false); return true; }
    if (c == 'm' || c == 'M') { stereoSeg.setIndex (engine.stereo == 1 ? 0 : 1); engine.stereo = stereoSeg.getIndex(); return true; }
    if (c == 'd' || c == 'D') { engine.dim = ! engine.dim.load(); return true; }
    return false;
}

// 波形の上の x 座標 ⇔ 秒
double MainComponent::timeAtX (int x) const
{
    const double len = currentLength();
    const auto r = timelineArea.reduced (8, 0);
    return juce::jlimit (0.0, juce::jmax (0.0, len), (double) (x - r.getX()) / juce::jmax (1, r.getWidth()) * len);
}

// x 座標の近く（6px 以内）にあるキューの番号。なければ -1
int MainComponent::cueNearX (int x)
{
    auto* cues = currentCues();
    const double len = currentLength();
    if (cues == nullptr || len <= 0.0) return -1;
    const auto r = timelineArea.reduced (8, 0);
    int best = -1; float bestD = 7.0f;
    for (int i = 0; i < (int) cues->size(); ++i)
    {
        const float cx = (float) r.getX() + (float) ((*cues)[(size_t) i].t / len) * (float) r.getWidth();
        const float d = std::abs (cx - (float) x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const bool overCue = timelineArea.contains (e.getPosition()) && cueNearX (e.x) >= 0;
    setMouseCursor (overCue ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    draggingCue = -1;
    if (! timelineArea.contains (e.getPosition()) || currentLength() <= 0.0) return;
    draggingCue = cueNearX (e.x);
    if (draggingCue >= 0)
    {
        activeCue = draggingCue;     // つかんだキューを選ぶ
        cueSignature.clear();
        return;
    }
    if (! isLive()) seekTo (timeAtX (e.x));
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! timelineArea.contains (e.getMouseDownPosition()) || currentLength() <= 0.0) return;
    auto* cues = currentCues();
    if (draggingCue >= 0 && cues != nullptr && draggingCue < (int) cues->size())
    {
        // キューの線をドラッグして位置を変える
        (*cues)[(size_t) draggingCue].t = timeAtX (e.x);
        updateLoop();
        repaint();
        return;
    }
    if (! isLive()) seekTo (timeAtX (e.x));   // ミュージックは離した位置だけ
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    auto* cues = currentCues();
    if (draggingCue >= 0 && cues != nullptr && draggingCue < (int) cues->size())
    {
        // 並べ直して、動かしたキューを選んだままにする
        const double t = (*cues)[(size_t) draggingCue].t;
        const auto name = (*cues)[(size_t) draggingCue].name;
        Library::sortCues (*cues);
        for (int i = 0; i < (int) cues->size(); ++i)
            if (std::abs ((*cues)[(size_t) i].t - t) < 1.0e-9 && (*cues)[(size_t) i].name == name) activeCue = i;
        library.save();
        cueSignature.clear();
        rebuildCueButtons();
        updateLoop();
        flash ("Moved cue to " + mmss (t));
    }
    else if (isLive() && timelineArea.contains (e.getMouseDownPosition()) && currentLength() > 0.0)
        seekTo (timeAtX (e.x));
    draggingCue = -1;
}

// ----------------------------------------------------------------------------
// 表示

std::vector<float> MainComponent::toBands (const std::vector<double>& power, double sampleRate) const
{
    const double bw = sampleRate / SpectrumAnalyser::size;
    const int maxBin = (int) power.size() - 1;
    const double norm = 20.0 * std::log10 (SpectrumAnalyser::size / 4.0);
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

void MainComponent::updateSpectra()
{
    double rate = 48000.0;
    mixHas = engine.mixSpec.getSnapshot (snapBuf, rate);
    mixDb = toBands (snapBuf, rate);
    refHas = engine.refSpec.getSnapshot (snapBuf, rate);
    refDb = toBands (snapBuf, rate);

    const float gm = engine.mixGainDb.load(), gr = engine.refGainDb.load();
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
            const size_t a = k > 0 ? k - 1 : k, b = k + 1 < centers.size() ? k + 1 : k;
            const bool okA = mixDb[a] > -250.0f && refDb[a] > -250.0f, okB = mixDb[b] > -250.0f && refDb[b] > -250.0f;
            diffDb[k] = (raw[k] * 2.0f + (okA ? raw[a] : raw[k]) + (okB ? raw[b] : raw[k])) * 0.25f - off;
        }
    }

    // 状態の文字
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    statusHint.clear();
    if (bypass)
    {
        statusMix = "BYPASS: Logic and other apps play directly (no delay). Use this while recording.";
        statusMixColour = warn;
    }
    else if (captureError.isNotEmpty())
    {
        statusMix = "Audio error: " + captureError;
        statusMixColour = bad;
    }
    else if (capture.numMixProcesses() == 0)
    {
        statusMix = "Logic Pro: not running";
        statusMixColour = text.withAlpha (0.6f);
    }
    else
    {
        if (engine.mixPeakDb.load() > -90.0f) mixSilentSince = now;
        if (now - mixSilentSince > 3.0)
        {
            statusMix = "Logic Pro: connected (silent)";
            statusMixColour = text.withAlpha (0.7f);
            statusHint = "If you hear nothing from Logic while it plays, allow Reference Monitor in System Settings > Privacy & Security > Screen & System Audio Recording.";
        }
        else
        {
            statusMix = "Logic Pro: receiving";
            statusMixColour = good;
        }
    }
    if (! bypass && capture.numMixProcesses() > 0)
        statusMix += logic.hasTimecode() ? ("   |   Logic " + LogicLink::format (logic.getPosition()) + (logic.isPlaying() ? "  playing" : "  stopped"))
                                         : juce::String ("   |   sync off (Logic can't be controlled yet)");

    if (isLive())
    {
        if (musicState.error == "not-allowed")
        {
            statusRef = "Apple Music: control not allowed";
            statusRefColour = bad;
            statusHint = "Allow Reference Monitor to control Music in System Settings > Privacy & Security > Automation.";
        }
        else if (musicState.error.isNotEmpty() && musicState.error != "unsupported")
        {
            statusRef = "Apple Music: " + musicState.error;
            statusRefColour = bad;
        }
        else if (CaptureEngine::matches ("com.apple.Music", liveBundle) && ! musicState.running)
        {
            statusRef = "Apple Music: not running";
            statusRefColour = text.withAlpha (0.6f);
        }
        else if (musicState.title.isNotEmpty())
        {
            statusRef = musicState.title + "  -  " + musicState.artist;
            statusRefColour = refCol;
        }
        else
        {
            statusRef = liveAppBox.getText() + (capture.numRefProcesses() > 0 ? ": connected" : ": waiting");
            statusRefColour = refCol;
        }
    }
    else if (loadingPath.isNotEmpty())
    {
        statusRef = "Loading " + juce::File (loadingPath).getFileName() + " ...";
        statusRefColour = text.withAlpha (0.7f);
    }
    else if (auto* a = engine.player.getAudio())
    {
        statusRef = a->name;
        statusRefColour = refCol;
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto h = headerArea;
    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("REFERENCE MONITOR", h.removeFromLeft (240), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    auto st = h.withTrimmedRight (310);
    g.setColour (statusMixColour);
    g.drawText ("MIX  " + statusMix, st.removeFromTop (st.getHeight() / 2), juce::Justification::centredLeft);
    g.setColour (statusRefColour);
    g.drawText ("REF  " + statusRef, st, juce::Justification::centredLeft);

    // 左の見出し
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("REFERENCES", leftArea.getX(), leftArea.getY() - 16, 200, 14, juce::Justification::centredLeft);
    g.drawText ("LIVE APP", liveAppBox.getX(), liveAppBox.getY() - 16, 200, 14, juce::Justification::centredLeft);

    if (cueButtons.isEmpty())
    {
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No cues yet: press + CUE (or C) at the start of a section, e.g. Verse / Chorus. "
                    "Then move Logic to the same part of your song and press SET MIX (or S).",
                    cueRow, juce::Justification::centredLeft);
    }
    drawTimeline (g, timelineArea.toFloat());
    drawSpectrum (g, specArea.toFloat());
    drawMeters (g, meterArea.toFloat());

    if (flashMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() * 0.001 < flashUntil)
        statusHint = flashMessage;
    if (statusHint.isNotEmpty())
    {
        g.setColour (warn);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (statusHint, hintArea, juce::Justification::topLeft, 4);
    }

    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("LEVEL",     levelLabel,  juce::Justification::centredLeft);
    g.drawText ("BAND SOLO", bandLabel,   juce::Justification::centredLeft);
    g.drawText ("STEREO",    stereoLabel, juce::Justification::centredLeft);
    g.drawText ("REF TRIM",  trimLabel,   juce::Justification::centredLeft);
    g.drawText ("OUTPUT  (A / B)", outLabel, juce::Justification::centredLeft);
    g.drawText ("VOLUME",    volLabel,    juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.4f));
    g.drawText ("Keys: SPACE = A/B   1-9 = cue   P = play   C = add cue   S = set MIX position   L = loop   M = mono   D = dim",
                specArea.getX(), meterArea.getBottom() + 2, 700, 14, juce::Justification::centredLeft);
}

void MainComponent::drawTimeline (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    const auto inner = r.reduced (8, 6);
    const double len = currentLength();
    const double pos = currentPosition();
    g.setFont (juce::FontOptions (12.0f));

    if (len <= 0.0)
    {
        g.setColour (text.withAlpha (0.5f));
        g.drawText (isLive() ? "Play a song in Apple Music" : (loadingPath.isNotEmpty() ? "Loading..." : "Add or drop audio files on the left"),
                    inner.toNearestInt(), juce::Justification::centred);
        return;
    }

    auto xOf = [&] (double t) { return inner.getX() + (float) (t / len) * inner.getWidth(); };

    // ループ区間
    auto* cues = currentCues();
    if (cues != nullptr && activeCue >= 0 && activeCue < (int) cues->size())
    {
        const double s = (*cues)[(size_t) activeCue].t;
        const double e = activeCue + 1 < (int) cues->size() ? (*cues)[(size_t) activeCue + 1].t : len;
        g.setColour (refCol.withAlpha (loopOn ? 0.22f : 0.10f));
        g.fillRect (juce::Rectangle<float> (xOf (s), inner.getY(), xOf (e) - xOf (s), inner.getHeight()));
    }

    if (! isLive())
    {
        if (auto* a = engine.player.getAudio())
        {
            const auto& ov = a->overview;
            const float mid = inner.getCentreY(), hh = inner.getHeight() * 0.42f;
            const int w = (int) inner.getWidth();
            for (int x = 0; x < w; ++x)
            {
                const float v = ov[(size_t) juce::jlimit (0, (int) ov.size() - 1, (int) ((float) x / (float) w * (float) ov.size()))];
                g.setColour ((double) x / w <= pos / len ? refCol.withAlpha (0.85f) : refCol.withAlpha (0.35f));
                g.drawVerticalLine ((int) inner.getX() + x, mid - v * hh, mid + v * hh);
            }
        }
    }
    else
    {
        const auto bar = inner.withSizeKeepingCentre (inner.getWidth(), 8.0f);
        g.setColour (grid);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (refCol);
        g.fillRoundedRectangle (bar.withWidth ((float) (pos / len) * bar.getWidth()), 4.0f);
    }

    // キューの目印
    if (cues != nullptr)
        for (int i = 0; i < (int) cues->size(); ++i)
        {
            const float x = xOf ((*cues)[(size_t) i].t);
            g.setColour (i == activeCue ? text : text.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            // つかめる印（三角）
            juce::Path tri;
            tri.addTriangle (x - 5.0f, r.getY(), x + 5.0f, r.getY(), x, r.getY() + 7.0f);
            g.fillPath (tri);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), (int) x + 2, (int) inner.getY(), 20, 12, juce::Justification::centredLeft);
        }

    g.setColour (text);
    g.fillRect (xOf (pos) - 1.0f, r.getY(), 2.0f, r.getHeight());
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (text.withAlpha (0.85f));
    g.drawText (mmss (pos) + " / " + mmss (len), inner.toNearestInt(), juce::Justification::bottomRight);
}

void MainComponent::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (36, 12).withTrimmedBottom (14);
    auto main = r.removeFromTop (r.getHeight() * 0.66f);
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
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
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

void MainComponent::drawMeters (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (12, 10).toNearestInt();

    auto& m = engine.mixMeter;
    auto& f = engine.refMeter;
    struct Row { const char* name; float a, b; int dec; bool diff; };
    const bool mOk = m.signalPresent.load(), fOk = f.signalPresent.load();
    const float mixPsr = mOk ? m.peakShort.load() - m.shortTerm.load() : -100.0f;
    const float refPsr = fOk ? f.peakShort.load() - f.shortTerm.load() : -100.0f;
    const Row rows[] = {
        { "SHORT-TERM LUFS", mOk ? m.shortTerm.load() : -100.0f, fOk ? f.shortTerm.load() : -100.0f, 1, true },
        { "SECTION LUFS",    m.gatedSeconds > 0.0f ? m.integrated.load() : -100.0f, f.gatedSeconds > 0.0f ? f.integrated.load() : -100.0f, 1, true },
        { "PEAK (3s) dBFS",  mOk ? m.peakShort.load() : -100.0f, fOk ? f.peakShort.load() : -100.0f, 1, true },
        { "MAX PEAK dBFS",   m.peakMax.load() > -119.0f ? m.peakMax.load() : -100.0f, f.peakMax.load() > -119.0f ? f.peakMax.load() : -100.0f, 1, false },
        { "DYNAMICS (PSR)",  mixPsr, refPsr, 1, true },
        { "CORRELATION",     mOk ? m.correlation.load() : -100.0f, fOk ? f.correlation.load() : -100.0f, 2, false },
        { "SIDE %",          mOk ? m.sideRatio.load() * 100.0f : -100.0f, fOk ? f.sideRatio.load() * 100.0f : -100.0f, 0, true },
        { "GAIN APPLIED dB", engine.mixGainDb.load(), engine.refGainDb.load(), 1, false },
    };

    const int rowH = juce::jmin (30, r.getHeight() / 10);
    const int colW = 58;
    const int nameW = r.getWidth() - 3 * colW;
    auto header = r.removeFromTop (rowH);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    header.removeFromLeft (nameW);
    g.setColour (mixCol);               g.drawText ("MIX",  header.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (refCol);               g.drawText ("REF",  header.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (text.withAlpha (0.6f)); g.drawText ("DIFF", header.removeFromLeft (colW), juce::Justification::centredRight);

    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowH);
        g.setColour (grid);
        g.drawHorizontalLine (line.getY(), (float) line.getX(), (float) line.getRight());
        g.setColour (text.withAlpha (0.65f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (row.name, line.removeFromLeft (nameW), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        const bool plus = row.name[0] == 'G';
        g.setColour (mixCol.brighter (0.3f));
        g.drawText (fmt (row.a, row.dec, plus), line.removeFromLeft (colW), juce::Justification::centredRight);
        g.setColour (refCol.brighter (0.2f));
        g.drawText (fmt (row.b, row.dec, plus), line.removeFromLeft (colW), juce::Justification::centredRight);
        g.setColour (text.withAlpha (0.8f));
        const bool both = row.a > -99.0f && row.b > -99.0f;
        g.drawText (row.diff && both ? fmt (row.a - row.b, row.dec, true) : "", line.removeFromLeft (colW), juce::Justification::centredRight);
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.5f));
    g.drawFittedText ("SECTION = loudness since the section started (resets on cue jump, or when playback restarts after 1.5 s of silence). "
                      "PSR: lower = more compressed.",
                      r.withTrimmedTop (4), juce::Justification::topLeft, 4);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    headerArea = area.removeFromTop (40);
    {
        auto h = headerArea;
        bypassButton.setBounds (h.removeFromRight (90).reduced (0, 5));
        h.removeFromRight (6);
        pinButton.setBounds (h.removeFromRight (60).reduced (0, 5));
        h.removeFromRight (6);
        resetButton.setBounds (h.removeFromRight (130).reduced (0, 5));
    }
    area.removeFromTop (24);

    // 左: 参照曲リスト
    auto left = area.removeFromLeft (240);
    area.removeFromLeft (12);
    auto liveRow = left.removeFromBottom (28);
    left.removeFromBottom (22);
    liveAppBox.setBounds (liveRow);
    auto btnRow = left.removeFromBottom (30);
    left.removeFromBottom (6);
    hintArea = left.removeFromBottom (60);   // 注意書き用
    left.removeFromBottom (6);
    addButton.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2 - 3));
    btnRow.removeFromLeft (6);
    removeButton.setBounds (btnRow);
    refList.setBounds (left);
    leftArea = left;

    // 右: メイン
    abSeg.setBounds (area.removeFromTop (52));
    area.removeFromTop (8);

    auto transport = area.removeFromTop (30);
    playButton.setBounds (transport.removeFromLeft (120));   transport.removeFromLeft (6);
    loopButton.setBounds (transport.removeFromLeft (70));    transport.removeFromLeft (6);
    restartButton.setBounds (transport.removeFromLeft (130)); transport.removeFromLeft (6);
    addCueButton.setBounds (transport.removeFromLeft (70));  transport.removeFromLeft (6);
    setMixButton.setBounds (transport.removeFromLeft (80));  transport.removeFromLeft (10);
    area.removeFromTop (6);
    cueRow = area.removeFromTop (30);
    {
        auto cr = cueRow;
        const int n = juce::jmax (1, cueButtons.size());
        const int w = juce::jlimit (60, 170, cr.getWidth() / n - 6);
        for (auto* b : cueButtons)
        {
            if (cr.getWidth() < 40) { b->setBounds ({}); continue; }
            b->setBounds (cr.removeFromLeft (juce::jmin (w, cr.getWidth())));
            cr.removeFromLeft (6);
        }
    }
    area.removeFromTop (8);
    timelineArea = area.removeFromTop (60);
    area.removeFromTop (8);

    // 下の2段
    auto row2 = area.removeFromBottom (48);
    area.removeFromBottom (6);
    auto row1 = area.removeFromBottom (48);
    area.removeFromBottom (22);
    auto col = [] (juce::Rectangle<int>& row, int w, juce::Rectangle<int>& label)
    {
        auto c = row.removeFromLeft (w);
        row.removeFromLeft (14);
        label = c.removeFromTop (16);
        return c.reduced (0, 2);
    };
    levelSeg.setBounds  (col (row1, 230, levelLabel));
    bandSeg.setBounds   (col (row1, 230, bandLabel));
    stereoSeg.setBounds (col (row1, 200, stereoLabel));
    trimSlider.setBounds (col (row1, juce::jmax (120, row1.getWidth()), trimLabel));

    {
        auto o = col (row2, 520, outLabel);
        outABox.setBounds (o.removeFromLeft (220)); o.removeFromLeft (6);
        outBBox.setBounds (o.removeFromLeft (220)); o.removeFromLeft (6);
        outSeg.setBounds (o);
    }
    {
        auto rb = row2.removeFromRight (150);
        auto b = rb.withTrimmedTop (16).reduced (0, 2);
        dimButton.setBounds (b.removeFromLeft (72));
        b.removeFromLeft (6);
        muteButton.setBounds (b);
        row2.removeFromRight (14);
    }
    volumeSlider.setBounds (col (row2, juce::jmax (120, row2.getWidth()), volLabel));

    meterArea = area.removeFromRight (360);
    area.removeFromRight (8);
    specArea = area;
}
