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
// 名前を付けていないキュー（空、または前の版の「Cue 3」のような自動の名前）は番号だけで出す
static bool cueUnnamed (const juce::String& name)
{
    const auto n = name.trim();
    return n.isEmpty() || n.equalsIgnoreCase ("Cue") || (n.startsWithIgnoreCase ("Cue ") && n.substring (4).trim().containsOnly ("0123456789"));
}

static juce::String cueLabel (const juce::String& name, int idx)
{
    return cueUnnamed (name) ? "CUE " + juce::String (idx + 1) : juce::String (idx + 1) + ". " + name.trim();
}

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
      // SPOTIFY / APPLE / YOUTUBE は MORE の中（あまり使わないので）
      levelSeg ({ "OFF", "MATCH" }, { good }, [this] (int i) { engine.level = i; saveSettings(); updateMoreButton(); }),
      bandSeg ({ "FULL", "LOW", "MID", "HIGH" }, { selCol }, [this] (int i) { engine.band = i; saveSettings(); }),
      stereoSeg ({ "STEREO", "MONO", "SIDE" }, { selCol }, [this] (int i) { engine.stereo = i; saveSettings(); }),
      outSeg ({ "A", "B" }, { selCol }, [this] (int) { saveSettings(); captureError.clear(); retryAt = 0; }),
      listenSeg ({ "OFF", "PHONE", "LAPTOP", "EARBUDS", "BT SPEAKER", "CAR", "CLUB" }, { selCol, warn }, [this] (int i) { setListenAs (i); }),
      viewSeg ({ "TONE", "WIDTH", "PUNCH" }, { selCol }, [this] (int i) { setView (i); })
{
    for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f))
        centers.push_back (f);

    refList.setRowHeight (30);
    refList.setColour (juce::ListBox::backgroundColourId, panel);
    refList.setOutlineThickness (0);

    for (auto* c : std::initializer_list<juce::Component*> {
             &refBox, &addButton, &removeButton, &liveAppBox, &abSeg, &levelSeg, &bandSeg, &stereoSeg, &outSeg, &listenSeg,
             &playButton, &loopButton, &restartButton, &addCueButton, &setMixButton, &resetButton, &dimButton, &muteButton,
             &pinButton, &bypassButton, &trimSlider, &volumeSlider, &outABox, &outBBox, &viewSeg, &snapButton, &avgButton, &moreButton })
    {
        addAndMakeVisible (c);
        noFocus (*c);
    }
    for (auto* s : { &abSeg, &levelSeg, &bandSeg, &stereoSeg, &outSeg, &listenSeg, &viewSeg })
        for (auto* b : s->buttons) noFocus (*b);

    styleToggle (loopButton, selCol);
    setMixButton.setColour (juce::TextButton::textColourOffId, mixCol.brighter (0.4f));
    styleToggle (restartButton, selCol);
    styleToggle (dimButton, warn);
    styleToggle (muteButton, bad);
    styleToggle (pinButton, selCol);
    styleToggle (bypassButton, warn);
    styleToggle (snapButton, mixCol);
    snapButton.setButtonText ("SNAP");
    snapButton.setTooltip ("Remember how your mix sounds now (dashed line). Right-click to clear.");
    snapButton.onClick = [this] { takeSnapshot(); };
    snapButton.onRightClick = [this] { clearSnapshot(); };

    styleToggle (avgButton, refCol);
    avgButton.setButtonText ("AVG");
    avgButton.setTooltip ("Compare your tone with the average of several references. Right-click to add / remove songs.");
    avgButton.onClick = [this]
    {
        if (avgCurves.empty()) { avgMenu(); return; }
        avgOn = ! avgOn;
        saveAvg();
        repaint();
    };
    avgButton.onRightClick = [this] { avgMenu(); };

    // LISTEN AS（PHONE など）と配信サービスの音量は、あまり使わないので MORE の中に
    listenSeg.setVisible (false);
    styleToggle (moreButton, warn);
    moreButton.onClick = [this] { showMoreMenu(); };

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

    refBox.onChange = [this]
    {
        const int id = refBox.getSelectedId();
        if (id > 0 && id - 1 != selectedRow) selectItem (id - 1);
        grabKeyboardFocus();
    };
    refBox.setTooltip ("Reference: Apple Music (or another app) live, or an audio file. Drop files on the window to add them.");

    liveAppBox.onChange = [this]
    {
        const int id = liveAppBox.getSelectedId();
        if (id <= 0) return;
        const auto b = liveBundles[id - 1];
        if (b.isNotEmpty() && b != liveBundle) { liveBundle = b; saveSettings(); retryAt = 0; refreshRefBox(); }
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

    // VOLUME と REF TRIM は「数字の箱」: 上下にドラッグ／マウスのホイールで変える、ダブルクリックで 0 dB
    for (auto* sl : { &trimSlider, &volumeSlider })
    {
        sl->setSliderStyle (juce::Slider::LinearBarVertical);
        sl->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 0, 0);
        sl->setTextBoxIsEditable (false);
        sl->setSliderSnapsToMousePosition (false);
        sl->setTextValueSuffix (" dB");
        sl->setDoubleClickReturnValue (true, 0.0);
        sl->setScrollWheelEnabled (true);
        sl->setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        sl->setColour (juce::Slider::backgroundColourId, panel);
        sl->setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);   // 値の位置まで塗らない（数字だけ）
        sl->setColour (juce::Slider::textBoxOutlineColourId, grid);
        sl->setColour (juce::Slider::textBoxTextColourId, text);
    }
    trimSlider.setRange (-12.0, 12.0, 0.1);
    trimSlider.setMouseDragSensitivity (240);   // 1 ピクセル = 0.1 dB
    trimSlider.setTooltip ("Reference level fine-tune. Drag up/down or scroll. Double-click = 0 dB.");
    trimSlider.onValueChange = [this] { engine.trimDb = (float) trimSlider.getValue(); saveSettings(); };

    volumeSlider.setRange (-60.0, 6.0, 0.5);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setMouseDragSensitivity (300);
    volumeSlider.setTooltip ("Monitor volume. Drag up/down or scroll. Double-click = 0 dB.");
    volumeSlider.onValueChange = [this] { engine.volumeDb = (float) volumeSlider.getValue(); saveSettings(); };

    outABox.onChange = [this] { outA = outUIDs[outABox.getSelectedId() - 1]; saveSettings(); retryAt = 0; };
    outBBox.onChange = [this] { outB = outUIDs[outBBox.getSelectedId() - 1]; saveSettings(); retryAt = 0; };

    library.load();
    loadSettings();
    refreshOutputs();
    refreshLiveApps();
    refreshRefBox();
    selectItem (selectedRow);

    setWantsKeyboardFocus (true);
    setSize (750, 900);
    startTimerHz (25);

    remote = std::make_unique<RemoteCommands> ([this] (const std::string& c)
    {
        remoteCommand (juce::String::fromUTF8 (c.c_str()));
    });
}

MainComponent::~MainComponent()
{
    remote = nullptr;
    stopTimer();
    saveSettings();
    capture.stop();
    loader.removeAllJobs (true, 5000);
}

// LISTEN AS: 鳴らし先の聞こえ方を切り替える（自分の曲にも参照曲にもかかる）
void MainComponent::setListenAs (int idx)
{
    idx = juce::jlimit (0, refdsp::ListenSim::kNumPresets - 1, idx);
    engine.sim.target = idx;
    listenSeg.setIndex (idx);
    saveSettings();
    updateMoreButton();
    if (idx > 0) flash (refdsp::ListenSim::preset (idx).hint);
    else         flashMessage.clear();
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
    engine.level  = juce::jlimit (0, Engine::numLevels - 1, (int) s.getWithDefault ("level", 1));
    engine.sim.target = juce::jlimit (0, refdsp::ListenSim::kNumPresets - 1, (int) s.getWithDefault ("listen", 0));
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
    listenSeg.setIndex (engine.sim.target.load());
    bandSeg.setIndex (engine.band);
    stereoSeg.setIndex (engine.stereo);
    outSeg.setIndex ((int) s.getWithDefault ("outSel", 0));
    abSeg.setIndex (0);
    viewMode = juce::jlimit (0, 2, (int) s.getWithDefault ("view", (bool) s.getWithDefault ("widthView", false) ? 1 : 0));
    viewSeg.setIndex (viewMode);
    loadAvg();
    updateMoreButton();
    trimSlider.setValue (engine.trimDb.load(), juce::dontSendNotification);
    volumeSlider.setValue (engine.volumeDb.load(), juce::dontSendNotification);
}

void MainComponent::saveSettings()
{
    auto& s = library.settings;
    s.set ("level", engine.level.load());
    s.set ("listen", engine.sim.target.load());
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
    s.set ("view", viewMode);
    library.save();
}

// 選んでいる出力先。つながっていない（オーディオインターフェイスを外した・Bluetooth が切れた）ときは、
// Mac の今の出力先で鳴らす。つなぎ直せば、manageCapture が数秒で元の出力先に戻す
juce::String MainComponent::outputUID()
{
    const auto& u = outSeg.getIndex() == 1 ? outB : outA;
    outputFallback = false;
    if (u.isEmpty()) return CaptureEngine::defaultOutputUID();
    for (auto& d : CaptureEngine::listOutputDevices())
        if (d.uid == u) return u;
    outputFallback = true;
    return CaptureEngine::defaultOutputUID();
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
    refreshRefBox();
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

// 参照曲の選択肢を作り直す（0 番目 = ライブのアプリ、その後ろにファイル）
void MainComponent::refreshRefBox()
{
    if (refBox.isPopupActive()) return;
    refBox.clear (juce::dontSendNotification);
    const auto live = liveAppBox.getText();
    refBox.addItem ((live.isNotEmpty() ? live : juce::String ("Apple Music")) + "  (LIVE)", 1);
    if (! library.files.empty()) refBox.addSeparator();
    for (int i = 0; i < (int) library.files.size(); ++i)
    {
        const auto& f = library.files[(size_t) i];
        refBox.addItem (f.name + (juce::File (f.path).existsAsFile() ? "" : "  (missing)"), i + 2);
    }
    refBox.setSelectedId (selectedRow + 1, juce::dontSendNotification);
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
    refBox.setSelectedId (row + 1, juce::dontSendNotification);
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
    refreshRefBox();
    if (firstNew > 0) selectItem (firstNew);
    library.save();
}

void MainComponent::removeSelected()
{
    if (isLive()) return;
    library.files.erase (library.files.begin() + (selectedRow - 1));
    refreshRefBox();
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

            // 曲全体の音量（配信サービスの正規化に使う）
            refdsp::LoudnessMeter lm;
            lm.prepare (a->sampleRate);
            for (int pos = 0; pos < len; pos += 4096)
            {
                const int k = juce::jmin (4096, len - pos);
                lm.process (a->buffer.getReadPointer (0, pos), a->buffer.getReadPointer (1, pos), k);
            }
            a->integratedLufs = lm.gatedSeconds.load() > 0.0f ? lm.integrated.load() : -200.0f;
            a->peakDb = lm.peakMax.load();
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
        // Logic が答えるか 1.5 秒待ってから、答えなければ設定の案内を出す（timerCallback）
        if (! logic.hasTimecode()) syncCheckAt = juce::Time::getMillisecondCounterHiRes() * 0.001 + 1.5;
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
        flash ("MY MIX position for \"" + cueLabel ((*cues)[(size_t) idx].name, idx) + "\" set to " + LogicLink::format ((*cues)[(size_t) idx].mixT));
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
    Cue c { {}, t };   // 名前なし＝「CUE 番号」と出す。名前はボタンのダブルクリックで後から
    cues->push_back (c);
    Library::sortCues (*cues);
    for (int i = 0; i < (int) cues->size(); ++i)
        if (std::abs ((*cues)[(size_t) i].t - t) < 1.0e-6) activeCue = i;
    library.save();
    rebuildCueButtons();
    updateLoop();
}

void MainComponent::renameCue (int idx)
{
    auto* cues = currentCues();
    if (cues == nullptr || idx < 0 || idx >= (int) cues->size()) return;
    auto* w = new juce::AlertWindow ("Cue name", "Name this cue (e.g. Verse, Chorus). Leave it empty to show just the number.",
                                     juce::MessageBoxIconType::NoIcon);
    const auto& cur = (*cues)[(size_t) idx].name;
    w->addTextEditor ("name", cueUnnamed (cur) ? juce::String() : cur);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, idx] (int res)
    {
        auto* c2 = currentCues();
        if (res == 1 && c2 != nullptr && idx < (int) c2->size())
        {
            (*c2)[(size_t) idx].name = w->getTextEditorContents ("name").trim();   // 空なら番号だけに戻る
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
    m.addSectionHeader (cueLabel ((*cues)[(size_t) idx].name, idx) + "  " + mmss ((*cues)[(size_t) idx].t));
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
            b->setButtonText (cueLabel ((*cues)[(size_t) i].name, i) + ((*cues)[(size_t) i].mixT >= 0.0 ? "  +MIX" : ""));
            styleToggle (*b, refCol);
            b->setToggleState (i == activeCue, juce::dontSendNotification);
            b->onClick = [this, i] { jumpToCue (i, true); rebuildCueButtons(); };
            b->onRightClick = [this, i] { cueMenu (i); };
            b->onDoubleClick = [this, i] { renameCue (i); };
            b->setTooltip ("Click = jump here.  Double-click = rename.  Right-click = more.");
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
    if (syncCheckAt > 0.0 && juce::Time::getMillisecondCounterHiRes() * 0.001 >= syncCheckAt)
    {
        syncCheckAt = 0.0;
        if (! logic.hasTimecode())
            flash ("Logic doesn't answer: this Logic project isn't set up yet (once per project). In Logic: File > Project Settings > Sync > MIDI, "
                   "set a row to Reference Monitor, check MTC and MMC, and turn on \"Auto-enable MMC input\" below. Then save.");
    }
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
    if (c == 't' || c == 'T') { setListenAs ((engine.sim.target.load() + 1) % refdsp::ListenSim::kNumPresets); return true; }
    if (c == '0')             { setListenAs (0); return true; }
    if (c == 'w' || c == 'W') { setView ((viewMode + 1) % 3); return true; }
    if (c == 'n' || c == 'N') { takeSnapshot(); return true; }
    return false;
}

// Num Deck などからの操作（キーボードのショートカットと同じ動き）
void MainComponent::remoteCommand (const juce::String& c)
{
    if      (c == "ab")        setAB (! engine.ab.load());
    else if (c == "mix")       setAB (false);
    else if (c == "ref")       setAB (true);
    else if (c == "play")      togglePlay();
    else if (c == "loop")      { loopOn = ! loopOn; updateLoop(); saveSettings(); explainLoop(); }
    else if (c.startsWith ("cue")) { jumpToCue (c.substring (3).getIntValue() - 1, true); rebuildCueButtons(); }
    else if (c == "addCue")    addCue();
    else if (c == "setMix")    setMixPosition (activeCue, false);
    else if (c == "mono")      { stereoSeg.setIndex (engine.stereo == 1 ? 0 : 1); engine.stereo = stereoSeg.getIndex(); saveSettings(); }
    else if (c == "dim")       engine.dim = ! engine.dim.load();
    else if (c == "mute")      engine.mute = ! engine.mute.load();
    else if (c == "listen")    setListenAs ((engine.sim.target.load() + 1) % refdsp::ListenSim::kNumPresets);
    else if (c == "listenOff") setListenAs (0);
    else if (c == "reset")     engine.resetMeters();
    else if (c == "width")     setView ((viewMode + 1) % 3);
    else if (c == "snap")      takeSnapshot();
    else if (c == "snapClear") clearSnapshot();
    else if (c == "out" || c == "outA" || c == "outB")
    {
        outSeg.setIndex (c == "outA" ? 0 : c == "outB" ? 1 : 1 - outSeg.getIndex());
        saveSettings(); captureError.clear(); retryAt = 0;
    }
    else return;
    repaint();
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
    // 広がりは表示がふらつかないよう、約 2 秒でならす（25 回 / 秒）
    auto smoothWidth = [] (std::array<float, 3>& cur, const std::array<float, 3>& now)
    {
        for (size_t b = 0; b < 3; ++b)
        {
            if (now[b] < 0.0f) continue;
            cur[b] = cur[b] < 0.0f ? now[b] : cur[b] + 0.02f * (now[b] - cur[b]);
        }
    };
    mixHas = engine.mixSpec.getSnapshot (snapBuf, sideBuf, rate);
    mixDb = toBands (snapBuf, rate);
    if (mixHas) smoothWidth (mixWidth, bandWidths (snapBuf, sideBuf, rate));
    refHas = engine.refSpec.getSnapshot (snapBuf, sideBuf, rate);
    refDb = toBands (snapBuf, rate);
    if (refHas) smoothWidth (refWidth, bandWidths (snapBuf, sideBuf, rate));

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

    // 帯域ごとの迫力（測れたときだけ、少しならす）
    for (size_t b = 0; b < 3; ++b)
    {
        const float m = engine.mixPunch.crestDb[b].load(), f = engine.refPunch.crestDb[b].load();
        if (m >= 0.0f) mixPunchV[b] = mixPunchV[b] < 0.0f ? m : mixPunchV[b] + 0.05f * (m - mixPunchV[b]);
        if (f >= 0.0f) refPunchV[b] = refPunchV[b] < 0.0f ? f : refPunchV[b] + 0.05f * (f - refPunchV[b]);
    }

    // 長め（約 20 秒）にならした音色。AVG と比べる・参照曲を AVG に足すのに使う
    auto longAvg = [] (std::vector<float>& acc, double& count, const std::vector<float>& now)
    {
        if (acc.size() != now.size()) { acc.assign (now.size(), -300.0f); count = 0; }
        const float a = (float) juce::jmax (1.0 / (25.0 * 20.0), 1.0 / (count + 1.0));   // 最初は単純平均、のちに 20 秒
        for (size_t k = 0; k < now.size(); ++k)
        {
            if (now[k] < -250.0f) continue;
            acc[k] = acc[k] < -250.0f ? now[k] : acc[k] + a * (now[k] - acc[k]);
        }
        count += 1.0;
    };
    const auto refKey = isLive() ? musicState.trackId : juce::String (selectedRow);
    if (refKey != refLongKey) { refLongKey = refKey; refLong.clear(); refLongCount = 0; }
    if (mixHas && engine.mixMeter.signalPresent.load()) longAvg (mixLong, mixLongCount, mixDb);
    if (refHas && engine.refMeter.signalPresent.load()) longAvg (refLong, refLongCount, refDb);

    // SNAP で覚える元: ミックスの音色を約 3 秒でならしたもの
    if (mixHas)
    {
        if (mixSlow.size() != mixDb.size()) mixSlow = mixDb;
        for (size_t k = 0; k < mixDb.size(); ++k)
        {
            if (mixDb[k] < -250.0f) continue;
            mixSlow[k] = mixSlow[k] < -250.0f ? mixDb[k] : mixSlow[k] + 0.013f * (mixDb[k] - mixSlow[k]);
        }
    }

    // AVG: 参照曲の代わりに「何曲かの平均」と、自分の曲の 20 秒の平均を比べる
    if (avgOn && ! avgDb.empty() && mixLongCount > 0)
    {
        mixDb = mixLong;
        refDb = avgDb;
        const float shift = curveLevel (mixLong) - curveLevel (avgDb);   // 見やすいよう、平均の線を自分の曲の高さに合わせる
        for (auto& v : refDb) if (v > -250.0f) v += shift;
        mixHas = refHas = true;
    }

    diffDb.assign (centers.size(), -300.0f);
    if (mixHas && refHas) diffDb = toneDiff (mixDb, refDb);
    snapDiffDb.assign (centers.size(), -300.0f);
    if (snapHas && refHas) snapDiffDb = toneDiff (snapDb, refDb);
    updateLiveShape();

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
            statusMix = "Logic Pro: receiving" + (capture.getMixInfo().isNotEmpty() ? " (" + capture.getMixInfo() + ")" : juce::String());
            statusMixColour = good;
        }
    }
    if (! bypass && capture.numMixProcesses() > 0)
        statusMix += logic.hasTimecode() ? ("   |   Logic " + LogicLink::format (logic.getPosition()) + (logic.isPlaying() ? "  playing" : "  stopped"))
                                         : juce::String ("   |   sync off: set up this Logic project (README)");
    if (outputFallback && ! bypass)
        statusHint = "OUTPUT " + juce::String (outSeg.getIndex() == 1 ? "B" : "A")
                   + " is not connected, so the sound goes to the Mac's current output for now. It switches back when you reconnect it.";

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
            statusRef = musicState.title + "  -  " + musicState.artist
                      + (capture.getRefInfo().isNotEmpty() ? "   (" + capture.getRefInfo() + ")" : juce::String());
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

    g.setColour (text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("REFERENCE MONITOR", headerArea, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    auto st = statusArea;
    g.setColour (statusMixColour);
    g.drawText ("MIX  " + statusMix, st.removeFromTop (st.getHeight() / 2), juce::Justification::centredLeft);
    g.setColour (statusRefColour);
    g.drawText ("REF  " + statusRef, st, juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("REF", refLabel, juce::Justification::centredLeft);
    g.drawText ("LIVE APP", liveLabel, juce::Justification::centredRight);

    if (cueButtons.isEmpty())
    {
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No cues yet: press + CUE (or C) at the start of a section, e.g. Verse / Chorus. "
                    "Then move Logic to the same part of your song and press SET MIX (or S).",
                    cueRow, juce::Justification::centredLeft);
    }
    drawTimeline (g, timelineArea.toFloat());
    g.setColour (panel);
    g.fillRoundedRectangle (specArea.toFloat(), 6.0f);
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto lg = legendArea;
        const bool avgShown = viewMode == 0 && avgOn && ! avgDb.empty();
        g.setColour (mixCol); g.drawText (avgShown ? "MY MIX (20s)" : "MY MIX", lg.removeFromLeft (avgShown ? 84 : 52), juce::Justification::centredLeft);
        g.setColour (refCol); g.drawText (avgShown ? "AVG OF " + juce::String ((int) avgCurves.size()) : juce::String ("REFERENCE"),
                                          lg.removeFromLeft (avgShown ? 70 : 74), juce::Justification::centredLeft);
        if (snapHas)
        {
            g.setColour (text.withAlpha (0.75f));
            g.setFont (juce::FontOptions (11.0f));
            const int ago = (int) (juce::Time::getMillisecondCounterHiRes() * 0.001 - snapTime);
            g.drawText ("- - SNAP " + (ago < 60 ? juce::String (ago) + "s" : juce::String (ago / 60) + "m") + " ago",
                        lg, juce::Justification::centredLeft);
        }
    }
    if (viewMode == 1)      drawBands (g, specArea.withTrimmedTop (specHeader.getHeight()).toFloat(), false);
    else if (viewMode == 2) drawBands (g, specArea.withTrimmedTop (specHeader.getHeight()).toFloat(), true);
    else                    drawSpectrum (g, specArea.withTrimmedTop (specHeader.getHeight()).toFloat());
    drawMeters (g, meterArea.toFloat());

    if (flashMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() * 0.001 < flashUntil)
        statusHint = flashMessage;
    if (statusHint.isNotEmpty())
    {
        g.setColour (warn);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (statusHint, hintArea, juce::Justification::centredLeft, 2);
    }

    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("LEVEL",     levelLabel,  juce::Justification::centredLeft);
    g.drawText ("BAND SOLO", bandLabel,   juce::Justification::centredLeft);
    g.drawText ("STEREO",    stereoLabel, juce::Justification::centredLeft);
    g.drawText ("REF TRIM",  trimLabel,   juce::Justification::centredLeft);
    g.drawText ("OUTPUT  (A / B)", outLabel, juce::Justification::centredLeft);
    g.drawText ("VOLUME",    volLabel,    juce::Justification::centredLeft);
    g.drawText ("MORE  (listen as / streaming)", moreLabel, juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.4f));
    g.drawFittedText ("Keys: SPACE = A/B  1-9 = cue  P = play  C = cue  S = set MIX  L = loop  M = mono  D = dim  T = listen as  W = width  N = snap",
                      juce::Rectangle<int> (specArea.getX(), meterArea.getBottom() + 2, meterArea.getRight() - specArea.getX(), 14),
                      juce::Justification::centredLeft, 1, 0.8f);
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
        // 流れた所だけ、音量の形を描く（大きい所ほど高い。いちばん大きい所から 24 dB 下までを表示）
        const auto it = liveShapes.find (musicState.trackId);
        float top = -200.0f;
        if (it != liveShapes.end()) for (float v : it->second) top = juce::jmax (top, v);
        if (top > -70.0f)
        {
            const auto& sh = it->second;
            const float mid = inner.getCentreY(), hh = inner.getHeight() * 0.42f;
            const int w = (int) inner.getWidth();
            g.setColour (grid);
            g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());
            for (int x = 0; x < w; ++x)
            {
                const float v = sh[(size_t) juce::jlimit (0, (int) sh.size() - 1, (int) ((float) x / (float) w * (float) sh.size()))];
                if (v < -199.0f) continue;
                const float h = juce::jlimit (0.06f, 1.0f, (v - (top - 24.0f)) / 24.0f);
                g.setColour ((double) x / w <= pos / len ? refCol.withAlpha (0.85f) : refCol.withAlpha (0.35f));
                g.drawVerticalLine ((int) inner.getX() + x, mid - h * hh, mid + h * hh);
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
    auto r = area.reduced (36, 4).withTrimmedBottom (22);
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

    // SNAP（さっきのミックス）は点線
    auto dashed = [&] (const std::vector<float>& db, juce::Rectangle<float> clip, std::function<float (float)> yOf, juce::Colour c)
    {
        juce::Path p; bool started = false;
        for (size_t k = 0; k < db.size() && k < centers.size(); ++k)
        {
            if (db[k] < -250.0f) continue;
            const juce::Point<float> pt (xOf (centers[k]), yOf (db[k]));
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
        }
        if (! started) return;
        juce::Path d;
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (d, p, dash, 2);
        g.saveState();
        g.reduceClipRegion (clip.toNearestInt());
        g.setColour (c);
        g.fillPath (d);
        g.restoreState();
    };

    if (refHas) curve (refDb, main, yMain, refCol, 2.0f, true);
    if (mixHas) curve (mixDb, main, yMain, mixCol, 2.0f, true);
    if (snapHas) dashed (snapDb, main, yMain, mixCol.brighter (0.5f).withAlpha (0.8f));
    if (snapHas && refHas) dashed (snapDiffDb, diff, yDiff, mixCol.brighter (0.5f).withAlpha (0.8f));
    if (mixHas && refHas) curve (diffDb, diff, yDiff, text.withAlpha (0.9f), 1.8f, false);
    else
    {
        g.setColour (text.withAlpha (0.4f));
        g.drawText ("Play both MY MIX and REFERENCE once to see the difference", diff.toNearestInt(), juce::Justification::centred);
    }

    g.setColour (text.withAlpha (0.7f));
    g.drawText ("MY MIX - REFERENCE (tone only)   above 0 = your mix has more", diff.toNearestInt().withHeight (14).translated (0, -16),
                juce::Justification::centredLeft);
}

std::vector<float> MainComponent::toneDiff (const std::vector<float>& a, const std::vector<float>& b) const
{
    std::vector<float> out (centers.size(), -300.0f);
    if (a.size() < centers.size() || b.size() < centers.size()) return out;
    auto ok = [&] (size_t k) { return a[k] > -250.0f && b[k] > -250.0f; };
    double sum = 0.0; int cnt = 0;
    std::vector<float> raw (centers.size(), 0.0f);
    for (size_t k = 0; k < centers.size(); ++k)
    {
        if (! ok (k)) continue;
        raw[k] = a[k] - b[k];
        if (centers[k] >= 100.0f && centers[k] <= 8000.0f) { sum += raw[k]; ++cnt; }
    }
    const float off = cnt > 0 ? (float) (sum / cnt) : 0.0f;
    for (size_t k = 0; k < centers.size(); ++k)
    {
        if (! ok (k)) continue;
        const size_t lo = k > 0 ? k - 1 : k, hi = k + 1 < centers.size() ? k + 1 : k;
        out[k] = (raw[k] * 2.0f + (ok (lo) ? raw[lo] : raw[k]) + (ok (hi) ? raw[hi] : raw[k])) * 0.25f - off;
    }
    return out;
}

std::array<float, 3> MainComponent::bandWidths (const std::vector<double>& mid, const std::vector<double>& side, double sampleRate)
{
    // LOW 30〜200 Hz / MID 200 Hz〜4 kHz / HIGH 4〜16 kHz。サイドの割合 = S / (M + S)（パワー）
    static const double edges[4] = { 30.0, 200.0, 4000.0, 16000.0 };
    std::array<float, 3> out { -1.0f, -1.0f, -1.0f };
    const int n = (int) juce::jmin (mid.size(), side.size());
    if (n < 2) return out;
    const double bw = sampleRate / SpectrumAnalyser::size;
    double m[3] {}, sd[3] {}, loudest = 0.0;
    for (int b = 0; b < 3; ++b)
    {
        const int b0 = juce::jmax (1, (int) std::ceil (edges[b] / bw));
        const int b1 = juce::jmin (n - 1, (int) std::floor (juce::jmin (edges[b + 1], sampleRate * 0.45) / bw));
        for (int i = b0; i <= b1; ++i) { m[b] += mid[(size_t) i]; sd[b] += side[(size_t) i]; }
        loudest = juce::jmax (loudest, m[b] + sd[b]);
    }
    // ほとんど音がない帯域（いちばん大きい帯域より 60 dB 以上小さい）は測らない
    for (int b = 0; b < 3; ++b)
        if (m[b] + sd[b] > 1.0e-12 && m[b] + sd[b] > loudest * 1.0e-6)
            out[(size_t) b] = (float) (sd[b] / (m[b] + sd[b]));
    return out;
}

void MainComponent::setView (int mode)
{
    viewMode = juce::jlimit (0, 2, mode);
    viewSeg.setIndex (viewMode);
    saveSettings();
    repaint();
}

void MainComponent::takeSnapshot()
{
    if (mixSlow.empty() || ! mixHas)
    {
        flash ("SNAP: play MY MIX for a few seconds first, then press SNAP.");
        return;
    }
    snapDb = mixSlow;
    snapWidth = mixWidth;
    snapPunch = mixPunchV;
    snapHas = true;
    snapTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    snapButton.setToggleState (true, juce::dontSendNotification);
    flash ("SNAP: your mix is remembered (dashed line). Change things, then compare. Press again to re-take, right-click to clear.");
    repaint();
}

void MainComponent::clearSnapshot()
{
    snapHas = false;
    snapDb.clear();
    snapWidth = { -1, -1, -1 };
    snapPunch = { -1, -1, -1 };
    snapButton.setToggleState (false, juce::dontSendNotification);
    repaint();
}

// ----------------------------------------------------------------------------
// MORE: LISTEN AS（スマホ・車などの聞こえ方）と、配信サービスの音量（SPOTIFY など）

void MainComponent::showMoreMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("LISTEN AS  (T = next, 0 = off)");
    for (int i = 0; i < refdsp::ListenSim::kNumPresets; ++i)
        m.addItem (100 + i, i == 0 ? juce::String ("Off (normal)") : juce::String (refdsp::ListenSim::preset (i).name),
                   true, engine.sim.target.load() == i);
    m.addSeparator();
    m.addSectionHeader ("LEVEL: like a streaming service");
    for (int i = 0; i < 3; ++i)
        m.addItem (200 + i, refdsp::streamingPlatform (i).name, true, engine.level.load() == Engine::levelStream + i);
    m.addItem (199, "Back to MATCH", engine.level.load() >= Engine::levelStream);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton), [this] (int r)
    {
        if (r >= 100 && r < 100 + refdsp::ListenSim::kNumPresets)
            setListenAs (r - 100);
        else if (r == 199 || (r >= 200 && r < 203))
        {
            const int lv = r == 199 ? (int) Engine::levelMatch : Engine::levelStream + (r - 200);
            engine.level = lv;
            levelSeg.setIndex (lv);
            saveSettings();
            if (lv >= Engine::levelStream)
                flash (juce::String (refdsp::streamingPlatform (lv - Engine::levelStream).name)
                       + ": both songs are turned up/down the way the service does it (whole-song loudness). "
                         "If yours gets turned down more than the reference, the extra loudness is wasted.");
        }
        updateMoreButton();
        repaint();
    });
}

// どれかが入っているときは、MORE のボタンに出して色を付ける（入れっぱなしに気づけるように）
void MainComponent::updateMoreButton()
{
    juce::StringArray on;
    const int li = engine.sim.target.load();
    if (li > 0) on.add (refdsp::ListenSim::preset (li).name);
    const int lv = engine.level.load();
    if (lv >= Engine::levelStream) on.add (refdsp::streamingPlatform (lv - Engine::levelStream).name);
    moreButton.setButtonText (on.isEmpty() ? juce::String ("MORE") : on.joinIntoString (" + "));
    moreButton.setToggleState (! on.isEmpty(), juce::dontSendNotification);
}

// ----------------------------------------------------------------------------
// AVG: 何曲かのリファレンスの「平均の音色」と比べる。1 曲のクセに引っぱられないように

// 100Hz〜8kHz の平均の高さ（音色の線どうしを比べるときの基準）
float MainComponent::curveLevel (const std::vector<float>& v) const
{
    double sum = 0.0; int cnt = 0;
    for (size_t k = 0; k < v.size() && k < centers.size(); ++k)
        if (v[k] > -250.0f && centers[k] >= 100.0f && centers[k] <= 8000.0f) { sum += v[k]; ++cnt; }
    return cnt > 0 ? (float) (sum / cnt) : 0.0f;
}

std::vector<float> MainComponent::normalisedCurve (std::vector<float> v) const
{
    const float lv = curveLevel (v);
    for (auto& x : v) if (x > -250.0f) x -= lv;
    return v;
}

// ファイルは曲全体を解析する（ミッドの平均パワー）
std::vector<float> MainComponent::fileCurve (const RefAudio& a) const
{
    constexpr int order = SpectrumAnalyser::order, size = SpectrumAnalyser::size;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) size), buf ((size_t) size * 2);
    for (int i = 0; i < size; ++i)
        win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
    std::vector<double> power ((size_t) size / 2 + 1, 0.0);
    const float* L = a.buffer.getReadPointer (0);
    const float* R = a.buffer.getReadPointer (1);
    const int n = a.buffer.getNumSamples();
    int frames = 0;
    for (int start = 0; start + size <= n; start += size)
    {
        double ms = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float x = 0.5f * (L[start + i] + R[start + i]);
            ms += (double) x * x;
            buf[(size_t) i] = x * win[(size_t) i];
        }
        if (ms / size < 1.0e-8) continue;   // 無音の所は数えない
        std::fill (buf.begin() + size, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (size_t b = 0; b < power.size(); ++b) power[b] += (double) buf[b] * buf[b];
        ++frames;
    }
    if (frames == 0) return {};
    for (auto& p : power) p /= frames;
    return toBands (power, a.sampleRate);
}

void MainComponent::addCurrentToAvg()
{
    std::vector<float> curve;
    juce::String name;
    if (! isLive())
    {
        if (auto* a = engine.player.getAudio()) { curve = fileCurve (*a); name = a->name; }
    }
    else if (refLongCount >= 25.0 * 15.0)   // 15 秒以上聴いた分の平均
    {
        curve = refLong;
        name = musicState.title.isNotEmpty() ? musicState.title : juce::String ("Apple Music song");
    }
    else
    {
        flash ("AVG: play this reference for at least 15 seconds first (the longer, the better), then add it.");
        return;
    }
    if (curve.empty()) { flash ("AVG: this reference could not be analysed."); return; }

    curve = normalisedCurve (curve);
    for (auto& c : avgCurves)
        if (c.name == name) { c.db = curve; name.clear(); break; }   // 同じ曲は入れ替え
    if (name.isNotEmpty()) avgCurves.push_back ({ name, curve });
    avgOn = true;
    rebuildAvg();
    saveAvg();
    flash ("AVG: " + juce::String ((int) avgCurves.size()) + " song(s). The tone graph now compares your mix (20 s average) with their average."
           + (avgCurves.size() < 3 ? "  Adding 2-3 songs works best." : ""));
    repaint();
}

void MainComponent::avgMenu()
{
    juce::PopupMenu m;
    const juce::String cur = isLive() ? musicState.title : (engine.player.getAudio() != nullptr ? engine.player.getAudio()->name : juce::String());
    m.addItem (1, "Add current reference to AVG" + (cur.isNotEmpty() ? "  (" + cur + ")" : juce::String()), cur.isNotEmpty());
    m.addItem (2, avgOn ? "Turn AVG off (compare with the current reference)" : "Turn AVG on", ! avgCurves.empty());
    if (! avgCurves.empty())
    {
        m.addSeparator();
        m.addSectionHeader ("In AVG (click to remove)");
        for (int i = 0; i < (int) avgCurves.size(); ++i)
            m.addItem (100 + i, avgCurves[(size_t) i].name);
        m.addItem (3, "Remove all");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (avgButton), [this] (int r)
    {
        if (r == 1) { addCurrentToAvg(); return; }
        if (r == 2) avgOn = ! avgOn;
        else if (r == 3) { avgCurves.clear(); avgOn = false; }
        else if (r >= 100 && r - 100 < (int) avgCurves.size())
        {
            avgCurves.erase (avgCurves.begin() + (r - 100));
            if (avgCurves.empty()) avgOn = false;
        }
        else return;
        rebuildAvg();
        saveAvg();
        repaint();
    });
}

void MainComponent::rebuildAvg()
{
    avgDb.clear();
    if (! avgCurves.empty())
    {
        avgDb.assign (centers.size(), -300.0f);
        for (size_t k = 0; k < centers.size(); ++k)
        {
            double sum = 0.0; int cnt = 0;
            for (auto& c : avgCurves)
                if (k < c.db.size() && c.db[k] > -250.0f) { sum += c.db[k]; ++cnt; }
            if (cnt > 0) avgDb[k] = (float) (sum / cnt);
        }
    }
    avgButton.setButtonText (avgCurves.empty() ? juce::String ("AVG") : "AVG " + juce::String ((int) avgCurves.size()));
    avgButton.setToggleState (avgOn && ! avgCurves.empty(), juce::dontSendNotification);
}

void MainComponent::saveAvg()
{
    juce::Array<juce::var> arr;
    for (auto& c : avgCurves)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", c.name);
        juce::Array<juce::var> db;
        for (float v : c.db) db.add (v > -250.0f ? juce::roundToInt (v * 10.0f) / 10.0 : -300.0);
        o->setProperty ("db", db);
        arr.add (juce::var (o));
    }
    library.settings.set ("avgCurves", arr);
    library.settings.set ("avgOn", avgOn);
    library.save();
}

void MainComponent::loadAvg()
{
    avgCurves.clear();
    if (auto* arr = library.settings.getWithDefault ("avgCurves", {}).getArray())
        for (auto& v : *arr)
        {
            AvgCurve c;
            c.name = v.getProperty ("name", "").toString();
            if (auto* db = v.getProperty ("db", {}).getArray())
                for (auto& x : *db) c.db.push_back ((float) (double) x);
            if (c.db.size() == centers.size()) avgCurves.push_back (std::move (c));
        }
    avgOn = (bool) library.settings.getWithDefault ("avgOn", false) && ! avgCurves.empty();
    rebuildAvg();
}

// ----------------------------------------------------------------------------
// ミュージックの曲は波形を読めないので、流れた所の音量（モーメンタリー LUFS）を少しずつ描いていく

void MainComponent::updateLiveShape()
{
    if (! isLive() || ! musicState.playing || musicState.duration < 5.0 || musicState.trackId.isEmpty()) return;
    const float m = engine.refMeter.momentary.load();
    if (m < -70.0f) return;
    auto& v = liveShapes[musicState.trackId];
    if (v.empty()) v.assign (400, -200.0f);
    const int idx = juce::jlimit (0, (int) v.size() - 1, (int) (musicState.position / musicState.duration * (double) v.size()));
    v[(size_t) idx] = v[(size_t) idx] < -199.0f ? m : juce::jmax (v[(size_t) idx], m);
}

void MainComponent::drawBands (juce::Graphics& g, juce::Rectangle<float> area, bool punch)
{
    auto r = area.reduced (12, 6);
    auto foot = r.removeFromBottom (16);
    // WIDTH: サイドの割合（棒の右端 = 50%）。PUNCH: クレスト dB（棒の右端 = 24 dB）
    const float maxV = punch ? 24.0f : 0.5f;
    const auto& mixV = punch ? mixPunchV : mixWidth;
    const auto& refV = punch ? refPunchV : refWidth;
    const auto& snapV = punch ? snapPunch : snapWidth;
    auto fmtV = [punch] (float v) { return punch ? juce::String (v, 1) + " dB" : juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
    const char* names[3]  = { "LOW", "MID", "HIGH" };
    const char* ranges[3] = { "< 200 Hz", "200 Hz - 4k", "> 4 kHz" };
    const float rowH = r.getHeight() / 3.0f;

    for (int row = 0; row < 3; ++row)
    {
        const int b = 2 - row;               // 上から HIGH / MID / LOW
        auto rr = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) row, r.getWidth(), rowH).reduced (0, 4);
        if (row > 0)
        {
            g.setColour (grid);
            g.drawHorizontalLine ((int) rr.getY() - 4, r.getX(), r.getRight());
        }
        auto label = rr.removeFromLeft (70);
        g.setColour (text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (names[b], label.removeFromTop (label.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft);
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (ranges[b], label.toNearestInt(), juce::Justification::topLeft);

        const float mv = mixV[(size_t) b], fv = refV[(size_t) b], sv = snapHas ? snapV[(size_t) b] : -1.0f;
        const bool both = mv >= 0.0f && fv >= 0.0f;

        // ひとこと
        juce::String note; juce::Colour noteCol = text.withAlpha (0.7f);
        if (punch)
        {
            if      (both && fv - mv > 3.0f)  { note = "REF hits much harder (yours is squashed)"; noteCol = warn; }
            else if (both && fv - mv > 1.5f)  note = "REF hits harder";
            else if (both && mv - fv > 1.5f)  note = "Yours hits harder";
            else if (both)                    note = "About the same";
        }
        else
        {
            if (b == 0 && mv > 0.12f)         { note = "Wide lows: thinner in mono"; noteCol = warn; }
            else if (both && fv - mv > 0.05f) note = "REF is wider";
            else if (both && mv - fv > 0.05f) note = "Yours is wider";
            else if (both)                    note = "About the same";
        }
        auto noteArea = rr.removeFromRight (juce::jmin (150.0f, rr.getWidth() * 0.34f));
        g.setColour (noteCol);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (note, noteArea.toNearestInt().reduced (6, 0), juce::Justification::centredLeft, 2);

        auto bars = rr.withSizeKeepingCentre (rr.getWidth(), juce::jmin (rr.getHeight(), 44.0f));
        const float barH = juce::jmin (16.0f, (bars.getHeight() - 4.0f) * 0.5f);
        auto drawBar = [&] (juce::Rectangle<float> br, float v, juce::Colour c, const char* who)
        {
            auto valueArea = br.removeFromRight (punch ? 56 : 40);
            auto whoArea = br.removeFromLeft (34);
            g.setColour (c.withAlpha (0.9f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (who, whoArea.toNearestInt(), juce::Justification::centredLeft);
            g.setColour (grid);
            g.fillRoundedRectangle (br, 3.0f);
            g.setFont (juce::FontOptions (11.0f));
            if (v >= 0.0f)
            {
                g.setColour (c);
                g.fillRoundedRectangle (br.withWidth (br.getWidth() * juce::jlimit (0.0f, 1.0f, v / maxV)), 3.0f);
                g.setColour (text.withAlpha (0.85f));
                g.drawText (fmtV (v), valueArea.toNearestInt(), juce::Justification::centredRight);
            }
            else
            {
                g.setColour (text.withAlpha (0.4f));
                g.drawText ("--", valueArea.toNearestInt(), juce::Justification::centredRight);
            }
            return br;
        };
        auto mixBar = drawBar (bars.removeFromTop (barH), mv, mixCol, "MIX");
        bars.removeFromTop (4);
        drawBar (bars.removeFromTop (barH), fv, refCol, "REF");

        // SNAP したときのミックスの位置（白い縦線）
        if (sv >= 0.0f)
        {
            const float x = mixBar.getX() + mixBar.getWidth() * juce::jlimit (0.0f, 1.0f, sv / maxV);
            g.setColour (text);
            g.fillRect (x - 1.0f, mixBar.getY() - 3.0f, 2.0f, mixBar.getHeight() + 6.0f);
        }
    }

    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ((punch ? juce::String ("Punch = peak vs. average level in each band over 3 s (bar end = 24 dB). Lower = more squashed.")
                             : juce::String ("Width = how much of each band is in the sides (bar end = 50%)."))
                          + (snapHas ? "  White mark = SNAP." : ""),
                      foot.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void MainComponent::drawMeters (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (12, 10).toNearestInt();

    auto& m = engine.mixMeter;
    auto& f = engine.refMeter;
    struct Row { const char* name; float a, b; int dec; bool diff; bool plus = false; };
    const bool mOk = m.signalPresent.load(), fOk = f.signalPresent.load();
    const float mixPsr = mOk ? m.peakShort.load() - m.shortTerm.load() : -100.0f;
    const float refPsr = fOk ? f.peakShort.load() - f.shortTerm.load() : -100.0f;
    const bool mRec = mOk && m.recentSeconds.load() > 0.0f, fRec = fOk && f.recentSeconds.load() > 0.0f;
    const float mixHeard = mRec ? m.recent.load() + engine.mixGainDb.load() : -100.0f;
    const float refHeard = fRec ? f.recent.load() + engine.refGainDb.load() : -100.0f;
    const Row rows[] = {
        { "HEARD LUFS (10s)", mixHeard, refHeard, 1, true },
        { "3s LUFS",          mOk ? m.shortTerm.load() : -100.0f, fOk ? f.shortTerm.load() : -100.0f, 1, true },
        { "10s LUFS",         mRec ? m.recent.load() : -100.0f, fRec ? f.recent.load() : -100.0f, 1, true },
        { "SECTION LUFS",     m.gatedSeconds > 0.0f ? m.integrated.load() : -100.0f, f.gatedSeconds > 0.0f ? f.integrated.load() : -100.0f, 1, true },
        { "PEAK (3s) dBFS",  mOk ? m.peakShort.load() : -100.0f, fOk ? f.peakShort.load() : -100.0f, 1, true },
        { "MAX PEAK dBFS",   m.peakMax.load() > -119.0f ? m.peakMax.load() : -100.0f, f.peakMax.load() > -119.0f ? f.peakMax.load() : -100.0f, 1, false },
        { "DYNAMICS (PSR)",  mixPsr, refPsr, 1, true },
        { "CORRELATION",     mOk ? m.correlation.load() : -100.0f, fOk ? f.correlation.load() : -100.0f, 2, false },
        { "SIDE %",          mOk ? m.sideRatio.load() * 100.0f : -100.0f, fOk ? f.sideRatio.load() * 100.0f : -100.0f, 0, true },
        { "GAIN APPLIED dB", engine.mixGainDb.load(), engine.refGainDb.load(), 1, false, true },
        // 配信サービスで何 dB 上げ下げされるか（曲全体の音量で決まる）
        { "SPOTIFY dB",      engine.platformGainDb (0, false), engine.platformGainDb (0, true), 1, false, true },
        { "APPLE MUSIC dB",  engine.platformGainDb (1, false), engine.platformGainDb (1, true), 1, false, true },
        { "YOUTUBE dB",      engine.platformGainDb (2, false), engine.platformGainDb (2, true), 1, false, true },
    };

    const int rowH = juce::jmin (26, r.getHeight() / 17);
    const int colW = r.getWidth() < 300 ? 50 : 58;
    const int nameW = r.getWidth() - 3 * colW;
    auto header = r.removeFromTop (rowH);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    header.removeFromLeft (nameW);
    g.setColour (mixCol);               g.drawText ("MIX",  header.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (refCol);               g.drawText ("REF",  header.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (text.withAlpha (0.6f)); g.drawText ("DIFF", header.removeFromLeft (colW), juce::Justification::centredRight);

    const bool showStream = engine.level.load() >= Engine::levelStream;
    for (auto& row : rows)
    {
        const juce::String nm (row.name);
        if (! showStream && (nm.startsWith ("SPOTIFY") || nm.startsWith ("APPLE") || nm.startsWith ("YOUTUBE"))) continue;
        auto line = r.removeFromTop (rowH);
        g.setColour (grid);
        g.drawHorizontalLine (line.getY(), (float) line.getX(), (float) line.getRight());
        g.setColour (text.withAlpha (0.65f));
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (row.name[0] == 'H' ? text : text.withAlpha (0.65f));
        g.drawText (row.name, line.removeFromLeft (nameW), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        const bool plus = row.plus;
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
    g.drawFittedText ("HEARD = what you actually hear after LEVEL (MATCH makes the two equal). "
                      "3s / 10s / SECTION = each song's own loudness before matching. "
                      "SECTION restarts on cue jump or after 1.5 s of silence. PSR: lower = more compressed."
                      + juce::String (showStream ? " SPOTIFY / APPLE / YOUTUBE = how much each service turns the song down (-) or up (+)." : ""),
                      r.withTrimmedTop (4), juce::Justification::topLeft, 4);
}

void MainComponent::resized()
{
    // MacBook の画面の半分（幅 740 前後）に収まるように、上から縦に並べる
    auto area = getLocalBounds().reduced (12);

    headerArea = area.removeFromTop (32);
    {
        auto h = headerArea;
        bypassButton.setBounds (h.removeFromRight (84).reduced (0, 3));
        h.removeFromRight (6);
        pinButton.setBounds (h.removeFromRight (54).reduced (0, 3));
        h.removeFromRight (6);
        resetButton.setBounds (h.removeFromRight (120).reduced (0, 3));
    }
    statusArea = area.removeFromTop (34);
    area.removeFromTop (4);

    // 参照曲の選択（前は左のリストだったもの）
    {
        auto r = area.removeFromTop (28);
        refLabel = r.removeFromLeft (32);
        removeButton.setBounds (r.removeFromRight (74));
        r.removeFromRight (6);
        addButton.setBounds (r.removeFromRight (64));
        r.removeFromRight (10);
        liveAppBox.setBounds (r.removeFromRight (juce::jlimit (120, 200, r.getWidth() / 3)));
        liveLabel = r.removeFromRight (64);
        r.removeFromRight (6);
        refBox.setBounds (r.withWidth (juce::jmin (r.getWidth(), 460)));
    }
    hintArea = area.removeFromTop (30);   // 注意書き用（2行まで）
    area.removeFromTop (2);

    abSeg.setBounds (area.removeFromTop (46));
    area.removeFromTop (8);

    auto transport = area.removeFromTop (30);
    playButton.setBounds (transport.removeFromLeft (116));   transport.removeFromLeft (6);
    loopButton.setBounds (transport.removeFromLeft (66));    transport.removeFromLeft (6);
    restartButton.setBounds (transport.removeFromLeft (124)); transport.removeFromLeft (6);
    addCueButton.setBounds (transport.removeFromLeft (66));  transport.removeFromLeft (6);
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
    timelineArea = area.removeFromTop (56);
    area.removeFromTop (8);

    // 下の2段。横に広げすぎないよう、それぞれの幅は決めておく（ウィンドウを広げても伸びない）
    auto row2 = area.removeFromBottom (46);
    area.removeFromBottom (4);
    auto row1 = area.removeFromBottom (46);
    area.removeFromBottom (20);
    auto col = [] (juce::Rectangle<int>& row, int w, juce::Rectangle<int>& label, int gap = 12)
    {
        auto c = row.removeFromLeft (juce::jmin (w, row.getWidth()));
        row.removeFromLeft (gap);
        label = c.removeFromTop (16);
        return c.reduced (0, 2);
    };
    const int W = row1.getWidth();
    listenSeg.setBounds ({});
    listenLabel = {};
    levelSeg.setBounds (col (row1, 150, levelLabel));
    bandSeg.setBounds  (col (row1, 280, bandLabel));
    moreButton.setBounds (col (row1, 190, moreLabel));
    {
        // OUTPUT | STEREO | REF TRIM | VOLUME | DIM / MUTE
        const int gap = 8, stW = 156, trimW = 66, volW = 70, dmW = 110;
        const int boxW = juce::jlimit (100, 170, (W - (stW + trimW + volW + dmW + gap * 4) - 56 - 12) / 2);
        auto o = col (row2, boxW * 2 + 6 + 6 + 56, outLabel, gap);
        auto segR = o.removeFromRight (56);
        o.removeFromRight (6);
        outABox.setBounds (o.removeFromLeft (boxW));
        o.removeFromLeft (6);
        outBBox.setBounds (o);
        outSeg.setBounds (segR);
        stereoSeg.setBounds    (col (row2, stW, stereoLabel, gap));
        trimSlider.setBounds   (col (row2, trimW, trimLabel, gap));
        volumeSlider.setBounds (col (row2, volW, volLabel, gap));
        auto b = row2.withTrimmedTop (16).reduced (0, 2).withWidth (juce::jmin (dmW, row2.getWidth()));
        dimButton.setBounds (b.removeFromLeft (b.getWidth() / 2 - 3));
        b.removeFromLeft (6);
        muteButton.setBounds (b);
    }

    meterArea = area.removeFromRight (juce::jlimit (290, 360, area.getWidth() * 2 / 5));
    area.removeFromRight (8);
    specArea = area;
    // グラフの上の段: TONE / WIDTH の切替、色の見本、SNAP
    specHeader = specArea.withHeight (30);
    {
        auto h = specHeader.reduced (8, 4);
        viewSeg.setBounds (h.removeFromLeft (180));
        h.removeFromLeft (10);
        snapButton.setBounds (h.removeFromRight (58));
        h.removeFromRight (6);
        avgButton.setBounds (h.removeFromRight (64));
        h.removeFromRight (6);
        legendArea = h;
    }
}
