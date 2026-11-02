#include "MainComponent.h"

namespace
{
    const juce::Colour bg (0xff111418), panel (0xff1a1f26), grid (0xff2a313b), text (0xfff2f2f2),
                       colA (0xff5ab0ff), colB (0xffff8a4c), blindCol (0xffb08cff),
                       good (0xff5fd18a), warn (0xffffa040), bad (0xffff5a5a);
    const juce::Colour selCol = juce::Colour (0xff3d6b8f).brighter (0.6f);
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

    juce::String when (juce::int64 ms) { return juce::Time (ms).formatted ("%m/%d %H:%M"); }

    void styleToggle (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }

    // 音色の差を、聴いてわかる帯域にまとめる
    struct Region { const char* name; float lo, hi; };
    const Region kRegions[] = {
        { "Sub", 20, 60 }, { "Bass", 60, 250 }, { "Low-mid", 250, 800 },
        { "Mid", 800, 2500 }, { "Presence", 2500, 6000 }, { "Air", 6000, 20000 },
    };

    float regionDiff (const std::array<float, mtm::kNumBands>& d, const Region& r)
    {
        double s = 0; int n = 0;
        for (int i = 0; i < mtm::kNumBands; ++i)
            if (mtm::bandCentre (i) >= r.lo && mtm::bandCentre (i) < r.hi) { s += d[(size_t) i]; ++n; }
        return n > 0 ? (float) (s / n) : 0.0f;
    }
}

// ============================================================================
MainComponent::MainComponent()
{
    songModel.rows = [this] { return library.songs().size(); };
    songModel.paintRow = [this] (int r, juce::Graphics& g, int w, int h, bool)
    {
        const auto songs = library.songs();
        if (r >= songs.size()) return;
        const bool sel = songs[r] == selectedSong;
        if (sel) { g.setColour (selCol.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
        g.setColour (text.withAlpha (sel ? 1.0f : 0.85f));
        g.setFont (juce::FontOptions (13.0f, sel ? juce::Font::bold : juce::Font::plain));
        g.drawText (songs[r], 10, 0, w - 60, h, juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String ((int) library.versionsOf (songs[r]).size()), w - 46, 0, 36, h, juce::Justification::centredRight);
    };
    songModel.clicked = [this] (int r, const juce::MouseEvent& e)
    {
        const auto songs = library.songs();
        if (r >= songs.size()) return;
        if (e.mods.isPopupMenu()) { songMenu (songs[r]); return; }
        selectSong (songs[r]);
        grabKeyboardFocus();
    };

    versionModel.rows = [this] { return (int) currentVersions().size(); };
    versionModel.paintRow = [this] (int r, juce::Graphics& g, int w, int h, bool)
    {
        auto vs = currentVersions();
        if (r >= (int) vs.size()) return;
        const Version& v = *vs[(size_t) r];
        if (v.id == selectedVersion) { g.setColour (selCol.withAlpha (0.22f)); g.fillRect (0, 0, w, h); }

        // A / B のバッジ（押すとその枠に入る）
        for (int s = 0; s < 2; ++s)
        {
            const auto rc = juce::Rectangle<float> (6.0f + (float) s * 26.0f, (float) h * 0.5f - 11.0f, 22.0f, 22.0f);
            const bool on = slotId[s] == v.id;
            const auto c = blind ? blindCol : (s == 0 ? colA : colB);
            g.setColour (on ? c : c.withAlpha (0.25f));
            if (on) g.fillRoundedRectangle (rc, 4.0f); else g.drawRoundedRectangle (rc, 4.0f, 1.0f);
            g.setColour (on ? juce::Colours::black : text.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (s == 0 ? "A" : "B", rc, juce::Justification::centred);
        }

        const int x = 64;
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (v.keeper ? good : text);
        g.drawText ((v.keeper ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + v.name,
                    x, 3, w - x - 80, h / 2, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (text.withAlpha (0.75f));
        g.drawText (v.analysing ? juce::String ("...") : (v.an.ok ? fmt (v.an.lufs) + " LUFS" : juce::String ("--")),
                    w - 84, 3, 76, h / 2, juce::Justification::centredRight);

        juce::String sub = when (v.fileTime);
        const bool playable = v.playPath().isNotEmpty();
        if (v.analysing)                       sub << "   analysing...";
        else if (! playable)                   sub << "   audio gone (overwritten)";
        else if (v.archivePath.isNotEmpty())   sub << "   saved copy";
        if (v.rounds > 0) sub << "   blind " << v.wins << "/" << v.rounds;
        if (v.note.isNotEmpty()) sub << "   " << v.note;
        g.setColour (playable ? text.withAlpha (0.5f) : bad);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (sub, x, h / 2, w - x - 8, h / 2 - 3, juce::Justification::centredLeft);
        g.setColour (grid);
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
    };
    versionModel.clicked = [this] (int r, const juce::MouseEvent& e)
    {
        auto vs = currentVersions();
        if (r >= (int) vs.size()) return;
        auto& v = *vs[(size_t) r];
        selectedVersion = v.id;
        if (e.mods.isPopupMenu()) versionMenu (v);
        else if (e.x < 31) assign (0, v.id);
        else if (e.x < 58) assign (1, v.id);
        versionList.repaint();
        repaint();
        grabKeyboardFocus();
    };

    for (auto* l : { &songList, &versionList })
    {
        l->setColour (juce::ListBox::backgroundColourId, panel);
        l->setOutlineThickness (0);
    }
    songList.setRowHeight (28);
    versionList.setRowHeight (44);

    for (auto* c : std::initializer_list<juce::Component*> {
             &songList, &versionList, &slotA, &slotB, &playButton, &loopButton, &matchButton, &alignButton, &blindButton,
             &pickX, &pickY, &foldersButton, &keepButton, &noteButton, &keeperButton, &volumeSlider, &outputBox })
    {
        addAndMakeVisible (c);
        noFocus (*c);
    }
    pickX.setVisible (false);
    pickY.setVisible (false);

    for (auto* b : { &slotA, &slotB })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, panel.brighter (0.08f));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
    slotA.setConnectedEdges (juce::Button::ConnectedOnRight);
    slotB.setConnectedEdges (juce::Button::ConnectedOnLeft);
    slotA.onClick = [this] { setWhich (0); };
    slotB.onClick = [this] { setWhich (1); };

    styleToggle (loopButton, selCol);
    styleToggle (matchButton, good);
    styleToggle (alignButton, good);
    styleToggle (blindButton, blindCol);
    styleToggle (keepButton, good);
    styleToggle (keeperButton, good);
    for (auto* b : { &pickX, &pickY }) b->setColour (juce::TextButton::buttonColourId, blindCol.withAlpha (0.5f));

    playButton.onClick = [this] { togglePlay(); };
    loopButton.onClick = [this]
    {
        loopOn = ! loopOn;
        if (loopOn && loopE - loopS < 0.1) flash ("LOOP: drag across the waveform to choose the range to repeat.");
        updateLoop();
    };
    matchButton.onClick = [this]
    {
        matchLevel = ! matchLevel;
        if (! matchLevel) flash ("MATCH LEVEL is off: the louder version will usually sound \"better\" just because it is louder.");
        updateGainsAndOffset();
        saveSettings();
    };
    alignButton.onClick = [this] { align = ! align; updateGainsAndOffset(); saveSettings(); };
    blindButton.onClick = [this] { if (blind) stopBlind(); else startBlind(); };
    pickX.onClick = [this] { blindPick (0); };
    pickY.onClick = [this] { blindPick (1); };
    foldersButton.onClick = [this] { foldersMenu(); };
    keepButton.onClick = [this]
    {
        keepCopies = ! keepCopies;
        flash (keepCopies ? "KEEP COPIES on: every new bounce is saved (FLAC) in Music > Mix Time Machine, so overwriting it in Logic loses nothing."
                          : "KEEP COPIES off: if you bounce again with the same name, the old version is gone.");
        saveSettings();
    };
    noteButton.onClick = [this] { editNote (selectedVersion); };
    keeperButton.onClick = [this]
    {
        if (auto* v = library.find (selectedVersion)) { v->keeper = ! v->keeper; library.save(); versionList.repaint(); }
    };

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    volumeSlider.setRange (-60.0, 6.0, 0.5);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.onValueChange = [this] { player.volumeDb = (float) volumeSlider.getValue(); saveSettings(); };

    outputBox.onChange = [this]
    {
        const int id = outputBox.getSelectedId();
        if (id <= 0 || id > outputNames.size()) return;
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        if (setup.outputDeviceName == outputNames[id - 1]) return;
        setup.outputDeviceName = outputNames[id - 1];
        const auto err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty()) flash ("Output: " + err);
        saveSettings();
    };

    // ---- 読み込み ----
    library.load();
    auto& s = library.settings;
    matchLevel = (bool) s.getWithDefault ("match", true);
    align = (bool) s.getWithDefault ("align", true);
    keepCopies = (bool) s.getWithDefault ("keep", true);
    volumeSlider.setValue ((double) s.getWithDefault ("volume", 0.0), juce::dontSendNotification);
    player.volumeDb = (float) volumeSlider.getValue();
    for (auto& v : library.versions)
        if (! v.an.ok && v.playPath().isNotEmpty()) queueAnalysis (v, false);   // 前回解析が終わらなかったもの

    deviceManager.initialiseWithDefaultDevices (0, 2);
    const auto savedOut = s.getWithDefault ("output", "").toString();
    if (savedOut.isNotEmpty())
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        setup.outputDeviceName = savedOut;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
    deviceManager.addAudioCallback (&player);
    refreshOutputs();

    // 初回は Logic の標準のフォルダ（ミュージック > Logic）を見張る
    if (library.roots.isEmpty())
    {
        const auto logicDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Logic");
        if (logicDir.isDirectory()) library.roots.add (logicDir.getFullPathName());
    }
    scanFolders();

    const auto songs = library.songs();
    const auto lastSong = s.getWithDefault ("song", "").toString();
    if (songs.contains (lastSong)) selectSong (lastSong);
    else if (! songs.isEmpty()) selectSong (songs[0]);

    setWantsKeyboardFocus (true);
    setSize (1240, 800);
    startTimerHz (25);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (&player);
    deviceManager.closeAudioDevice();
    analysisPool.removeAllJobs (true, 20000);
    loadPool.removeAllJobs (true, 20000);
    saveSettings();
}

void MainComponent::noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
}

void MainComponent::saveSettings()
{
    auto& s = library.settings;
    s.set ("match", matchLevel);
    s.set ("align", align);
    s.set ("keep", keepCopies);
    s.set ("volume", volumeSlider.getValue());
    s.set ("song", selectedSong);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    s.set ("output", setup.outputDeviceName);
    library.save();
}

void MainComponent::refreshOutputs()
{
    if (outputBox.isPopupActive()) return;
    juce::StringArray names;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        names = type->getDeviceNames (false);
    }
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    if (names == outputNames && outputBox.getNumItems() > 0)
    {
        outputBox.setSelectedId (names.indexOf (setup.outputDeviceName) + 1, juce::dontSendNotification);
        return;
    }
    outputNames = names;
    outputBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) outputBox.addItem (names[i], i + 1);
    outputBox.setSelectedId (names.indexOf (setup.outputDeviceName) + 1, juce::dontSendNotification);
}

void MainComponent::flash (const juce::String& msg)
{
    flashMessage = msg;
    flashUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 7.0;
    repaint();
}

// ----------------------------------------------------------------------------
// フォルダの見張り
//  - 見張るフォルダの直下と、その下（3階層まで）の「Bounces」フォルダにある音声ファイルを拾う
//  - 新しいファイル・書き換わったファイルは、大きさと時刻が落ち着いて（3秒）から取り込む
//  - KEEP COPIES がオンなら、取り込むときに FLAC でコピーをとっておく（あとで上書きされても聴ける）
//  - フォルダを見張り始めた時点で既にあったファイルは、一覧に載せるだけでコピーはしない

void MainComponent::refreshBounceDirs()
{
    bounceDirs.clear();
    projects.clear();
    std::function<void (const juce::File&, int)> walk = [&] (const juce::File& dir, int depth)
    {
        for (auto& d : dir.findChildFiles (juce::File::findDirectories, false))
        {
            const auto name = d.getFileName();
            if (name.startsWith (".")) continue;
            if (d.hasFileExtension ("logicx")) { projects.push_back (d.getFileNameWithoutExtension().toStdString()); continue; }
            if (name == "Bounces") { bounceDirs.add (d); continue; }
            if (name == "Audio Files" || name == "Mix Time Machine") continue;
            if (depth < 3) walk (d, depth + 1);
        }
    };
    for (auto& r : library.roots)
    {
        const juce::File root (r);
        if (! root.isDirectory()) continue;
        walk (root, 1);
    }
}

void MainComponent::scanFolders()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now >= nextDirScan) { refreshBounceDirs(); nextDirScan = now + 60.0; }

    const auto before = library.versions.size();
    bool baselined = false;
    for (auto& r : library.roots)
    {
        const juce::File root (r);
        if (! root.isDirectory()) continue;
        const bool baseline = ! library.baselinedRoots.contains (r);
        auto files = root.findChildFiles (juce::File::findFiles, false);
        for (auto& d : bounceDirs)
            if (d.isAChildOf (root)) files.addArray (d.findChildFiles (juce::File::findFiles, false));
        for (auto& f : files)
            if (! f.getFileName().startsWith (".") && f.hasFileExtension (kAudioExt))
                handleFile (f, baseline);
        if (baseline) { library.baselinedRoots.add (r); baselined = true; }
    }
    if (library.versions.size() != before || baselined)
    {
        library.save();
        songList.updateContent();
        versionList.updateContent();
        repaint();
    }
}

void MainComponent::handleFile (const juce::File& f, bool baseline)
{
    const auto path = f.getFullPathName();
    const auto t = f.getLastModificationTime().toMilliseconds();
    const auto sz = f.getSize();
    if (sz < 4096) return;
    if (library.ignored.contains (Library::ignoreKey (path, t))) return;

    auto* latest = library.latestForSource (path);
    if (latest != nullptr && latest->fileTime == t && latest->fileSize == sz) { pending.erase (path); return; }
    if (baseline && latest == nullptr) { addVersion (f, false); return; }

    // 書き出し中かもしれないので、2回続けて同じ大きさ・時刻で、3秒以上たっていたら取り込む
    auto it = pending.find (path);
    if (it != pending.end() && it->second.time == t && it->second.size == sz
        && juce::Time::currentTimeMillis() - t > 3000)
    {
        pending.erase (it);
        const auto& v = addVersion (f, keepCopies);
        const auto song = v.song;
        flash ("New bounce: " + f.getFileNameWithoutExtension() + "  (" + song + ")");
        if (! blind && song == selectedSong) selectSong (song);   // 新しい版を B に
        return;
    }
    pending[path] = { t, sz };
}

Version& MainComponent::addVersion (const juce::File& f, bool archive)
{
    Version v;
    v.id = juce::Uuid().toString();
    v.name = f.getFileNameWithoutExtension();
    v.sourcePath = f.getFullPathName();
    v.fileTime = f.getLastModificationTime().toMilliseconds();
    v.fileSize = f.getSize();
    const auto ov = library.songOverrides.find (v.sourcePath);
    if (ov != library.songOverrides.end()) v.song = ov->second;
    else
    {
        std::vector<std::string> known;
        for (auto& s : library.songs()) known.push_back (s.toStdString());
        v.song = juce::String::fromUTF8 (mtm::songKey (v.name.toStdString(), projects, known).c_str());
    }
    if (v.song.isEmpty()) v.song = v.name;
    library.versions.push_back (v);
    queueAnalysis (library.versions.back(), archive);
    return library.versions.back();
}

void MainComponent::queueAnalysis (const Version& v, bool archive)
{
    if (auto* lv = library.find (v.id)) lv->analysing = true;
    ++analysingCount;
    const auto id = v.id, path = v.sourcePath, song = v.song, name = v.name;
    const auto playPath = v.playPath();
    const auto time = v.fileTime;
    analysisPool.addJob ([this, id, path, playPath, song, name, time, archive]
    {
        Result res;
        res.id = id;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (archive ? path : playPath)));
        if (reader == nullptr)
            res.error = "Can't read " + name;
        else
        {
            const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0 * 30.0));
            juce::AudioBuffer<float> buf (2, juce::jmax (16, len));
            buf.clear();
            reader->read (&buf, 0, len, 0, true, true);
            if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
            res.an = mtm::analyse (buf.getReadPointer (0), buf.getReadPointer (1), len, reader->sampleRate);

            if (archive)
            {
                // 元のバウンスが上書きされても聴けるように、FLAC（劣化なし）でとっておく
                const auto dir = Library::archiveRoot().getChildFile (juce::File::createLegalFileName (song));
                dir.createDirectory();
                const auto stamp = juce::Time (time).formatted ("%Y-%m-%d %H.%M.%S");
                auto out = dir.getNonexistentChildFile (juce::File::createLegalFileName (stamp + " " + name), ".flac", false);
                std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
                if (os != nullptr)
                {
                    juce::FlacAudioFormat flac;
                    auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (reader->sampleRate)
                                                                                       .withNumChannels (2)
                                                                                       .withBitsPerSample (24));
                    if (w != nullptr)
                    {
                        if (w->writeFromAudioSampleBuffer (buf, 0, len)) res.archivePath = out.getFullPathName();
                        w.reset();
                    }
                    os.reset();
                }
                if (res.archivePath.isEmpty()) out.deleteFile();
            }
        }
        const juce::ScopedLock sl (resultLock);
        results.push_back (std::move (res));
    });
}

void MainComponent::pickUpResults()
{
    std::vector<Result> rs;
    std::vector<std::shared_ptr<const Track>> ts;
    std::vector<OffsetResult> os;
    {
        const juce::ScopedLock sl (resultLock);
        std::swap (rs, results);
        std::swap (ts, loadedTracks);
        std::swap (os, offsetResults);
    }

    for (auto& r : rs)
    {
        analysingCount = juce::jmax (0, analysingCount - 1);
        if (auto* v = library.find (r.id))
        {
            v->analysing = false;
            if (r.error.isEmpty())
            {
                v->an = r.an;
                if (r.archivePath.isNotEmpty()) v->archivePath = r.archivePath;
            }
        }
    }
    for (auto& t : ts)
    {
        loading.erase (t->versionId);
        cache[t->versionId] = t;
        for (int s = 0; s < 2; ++s)
            if (slotId[s] == t->versionId) trash.push_back (player.setTrack (s, t));
    }
    for (auto& o : os)
    {
        const auto key = o.a + "|" + o.b;
        offsetsRunning.erase (key);
        offsets[key] = o.off;
    }
    if (! rs.empty() || ! ts.empty() || ! os.empty())
    {
        if (! rs.empty()) library.save();
        updateGainsAndOffset();
        versionList.repaint();
        repaint();
    }

    // 使っていない読み込み済みの音は、4つを超えたら捨てる
    while (cache.size() > 4)
    {
        auto it = std::find_if (cache.begin(), cache.end(), [this] (auto& p) { return p.first != slotId[0] && p.first != slotId[1]; });
        if (it == cache.end()) break;
        cache.erase (it);
    }
    trash.clear();
}

void MainComponent::addRoot (const juce::File& dir)
{
    if (! dir.isDirectory() || library.roots.contains (dir.getFullPathName())) return;
    library.roots.add (dir.getFullPathName());
    nextDirScan = 0.0;
    scanFolders();
    saveSettings();
    flash ("Watching " + dir.getFullPathName() + ". New bounces there (and in its Bounces folders) appear here automatically.");
}

void MainComponent::foldersMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Watching (click to stop watching)");
    for (int i = 0; i < library.roots.size(); ++i) m.addItem (100 + i, library.roots[i]);
    if (library.roots.isEmpty()) m.addItem (99, "(none)", false);
    m.addSeparator();
    m.addItem (1, "Add folder...");
    m.addItem (2, "Show saved copies in Finder");
    m.addItem (3, "Rescan now");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (foldersButton), [this] (int r)
    {
        if (r == 1)
        {
            chooser = std::make_unique<juce::FileChooser> ("Folder where you bounce your mixes",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) addRoot (fc.getResult()); });
        }
        else if (r == 2)
        {
            Library::archiveRoot().createDirectory();
            Library::archiveRoot().revealToUser();
        }
        else if (r == 3) { nextDirScan = 0.0; scanFolders(); }
        else if (r >= 100 && r - 100 < library.roots.size())
        {
            const auto root = library.roots[r - 100];
            library.roots.remove (r - 100);
            library.baselinedRoots.removeString (root);
            nextDirScan = 0.0;
            saveSettings();
            flash ("Stopped watching " + root + ". Versions already in the list stay.");
        }
    });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (kAudioExt) || juce::File (f).isDirectory()) return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    juce::String lastSong;
    for (auto& p : files)
    {
        const juce::File f (p);
        if (f.isDirectory()) { addRoot (f); continue; }
        if (! f.hasFileExtension (kAudioExt)) continue;
        auto* latest = library.latestForSource (p);
        if (latest != nullptr && ! latest->sourceChanged()) { lastSong = latest->song; continue; }
        lastSong = addVersion (f, keepCopies).song;
    }
    library.save();
    songList.updateContent();
    if (lastSong.isNotEmpty()) selectSong (lastSong);
}

// ----------------------------------------------------------------------------
// 曲と版

std::vector<Version*> MainComponent::currentVersions() { return library.versionsOf (selectedSong); }

Version* MainComponent::slotVersion (int slot) { return slotId[slot & 1].isEmpty() ? nullptr : library.find (slotId[slot & 1]); }

void MainComponent::selectSong (const juce::String& song)
{
    if (blind) { flash ("Finish or stop the blind test first."); return; }
    selectedSong = song;
    auto vs = library.versionsOf (song);
    if (! vs.empty())
    {
        // B = いちばん新しい版、A = その前の版
        assign (1, vs[0]->id);
        assign (0, vs.size() >= 2 ? vs[1]->id : vs[0]->id);
        selectedVersion = vs[0]->id;
    }
    songList.updateContent();
    versionList.updateContent();
    songList.repaint();
    versionList.repaint();
    saveSettings();
    repaint();
}

void MainComponent::assign (int slot, const juce::String& versionId)
{
    if (blind) { flash ("Finish or stop the blind test first."); return; }
    slot &= 1;
    if (slotId[slot] == versionId && player.length.load() > 0.0) return;
    slotId[slot] = versionId;
    auto it = cache.find (versionId);
    if (it != cache.end()) trash.push_back (player.setTrack (slot, it->second));
    else
    {
        trash.push_back (player.setTrack (slot, nullptr));
        requestTrack (versionId);
    }
    updateGainsAndOffset();
    versionList.repaint();
    repaint();
}

void MainComponent::requestTrack (const juce::String& versionId)
{
    if (loading.count (versionId) > 0) return;
    auto* v = library.find (versionId);
    if (v == nullptr) return;
    const auto path = v->playPath();
    if (path.isEmpty())
    {
        flash ("\"" + v->name + "\" can't be played: the bounce was overwritten before a copy was kept.");
        return;
    }
    loading.insert (versionId);
    loadPool.addJob ([this, versionId, path]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (path)));
        auto t = std::make_shared<Track>();
        t->versionId = versionId;
        if (reader != nullptr)
        {
            const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0 * 30.0));
            t->sampleRate = reader->sampleRate;
            t->buffer.setSize (2, juce::jmax (16, len));
            t->buffer.clear();
            reader->read (&t->buffer, 0, len, 0, true, true);
            if (reader->numChannels == 1) t->buffer.copyFrom (1, 0, t->buffer, 0, 0, len);
            constexpr int bins = 2000;
            t->overview.resize (bins);
            for (int b = 0; b < bins; ++b)
            {
                const int s0 = (int) ((juce::int64) len * b / bins), s1 = (int) ((juce::int64) len * (b + 1) / bins);
                t->overview[(size_t) b] = juce::jmax (t->buffer.getMagnitude (0, s0, juce::jmax (1, s1 - s0)),
                                                      t->buffer.getMagnitude (1, s0, juce::jmax (1, s1 - s0)));
            }
        }
        const juce::ScopedLock sl (resultLock);
        loadedTracks.push_back (t);
    });
}

// 音量合わせと、頭のずれ合わせ
void MainComponent::updateGainsAndOffset()
{
    auto* a = slotVersion (0);
    auto* b = slotVersion (1);
    float ga = 0.0f, gb = 0.0f;
    if ((matchLevel || blind) && a != nullptr && b != nullptr && a->an.ok && b->an.ok && a->an.lufs > -100.0f && b->an.lufs > -100.0f)
    {
        // 小さい方に合わせる（大きい方を下げる）ので、音割れしない
        const float target = juce::jmin (a->an.lufs, b->an.lufs);
        ga = target - a->an.lufs;
        gb = target - b->an.lufs;
    }
    player.gainDbA = ga;
    player.gainDbB = gb;

    double off = 0.0;
    if (align && a != nullptr && b != nullptr && a->id != b->id)
    {
        const auto key = a->id + "|" + b->id;
        auto it = offsets.find (key);
        if (it != offsets.end())
        {
            if (it->second.confidence >= 0.3) off = it->second.seconds;
        }
        else if (offsetsRunning.count (key) == 0 && cache.count (a->id) > 0 && cache.count (b->id) > 0)
        {
            offsetsRunning.insert (key);
            auto ta = cache[a->id], tb = cache[b->id];
            const auto ida = a->id, idb = b->id;
            loadPool.addJob ([this, ta, tb, ida, idb]
            {
                OffsetResult r { ida, idb, mtm::findOffset (ta->buffer.getReadPointer (0), ta->buffer.getReadPointer (1), ta->buffer.getNumSamples(), ta->sampleRate,
                                                            tb->buffer.getReadPointer (0), tb->buffer.getReadPointer (1), tb->buffer.getNumSamples(), tb->sampleRate) };
                const juce::ScopedLock sl (resultLock);
                offsetResults.push_back (r);
            });
        }
    }
    player.offsetB = off;
    matchButton.setToggleState (matchLevel || blind, juce::dontSendNotification);
    alignButton.setToggleState (align, juce::dontSendNotification);
}

void MainComponent::versionMenu (Version& v)
{
    const auto id = v.id;
    juce::PopupMenu m;
    m.addSectionHeader (v.name + "   " + when (v.fileTime));
    m.addItem (1, "Put in A");
    m.addItem (2, "Put in B");
    m.addSeparator();
    m.addItem (3, "Note...");
    m.addItem (4, v.keeper ? "Unmark keeper" : "Mark as keeper");
    m.addItem (5, "Move to another song...");
    m.addItem (6, "Show in Finder", v.playPath().isNotEmpty());
    m.addSeparator();
    m.addItem (7, "Remove from list");
    m.addItem (8, "Remove and move the saved copy to Trash", v.archivePath.isNotEmpty());
    m.showMenuAsync (juce::PopupMenu::Options(), [this, id] (int r)
    {
        auto* x = library.find (id);
        if (x == nullptr) return;
        if (r == 1) assign (0, id);
        else if (r == 2) assign (1, id);
        else if (r == 3) editNote (id);
        else if (r == 4) { x->keeper = ! x->keeper; library.save(); versionList.repaint(); }
        else if (r == 5) moveToSong (id);
        else if (r == 6) juce::File (x->playPath()).revealToUser();
        else if (r == 7) removeVersion (id, false);
        else if (r == 8) removeVersion (id, true);
    });
}

void MainComponent::songMenu (const juce::String& song)
{
    juce::PopupMenu m;
    m.addSectionHeader (song);
    m.addItem (1, "Rename / merge into another song...");
    m.addItem (2, "Remove this song from the list");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, song] (int r)
    {
        if (r == 1) renameSong (song);
        else if (r == 2)
        {
            for (auto* v : library.versionsOf (song)) removeVersion (v->id, false);
            if (selectedSong == song) selectedSong.clear();
            songList.updateContent();
            repaint();
        }
    });
}

void MainComponent::editNote (const juce::String& versionId)
{
    auto* v = library.find (versionId);
    if (v == nullptr) { flash ("Click a version first."); return; }
    auto* w = new juce::AlertWindow ("Note", "What did you change in \"" + v->name + "\"? (e.g. vocal +1dB, new limiter)",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("note", v->note);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, versionId] (int res)
    {
        if (auto* x = library.find (versionId); x != nullptr && res == 1)
        {
            x->note = w->getTextEditorContents ("note").trim();
            library.save();
            versionList.repaint();
        }
        grabKeyboardFocus();
    }), true);
}

void MainComponent::moveToSong (const juce::String& versionId)
{
    auto* v = library.find (versionId);
    if (v == nullptr) return;
    auto* w = new juce::AlertWindow ("Move to song", "Song name (type an existing name to merge):", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("song", v->song);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, versionId] (int res)
    {
        const auto song = w->getTextEditorContents ("song").trim();
        if (auto* x = library.find (versionId); x != nullptr && res == 1 && song.isNotEmpty())
        {
            x->song = song;
            library.songOverrides[x->sourcePath] = song;
            library.save();
            selectSong (song);
        }
        grabKeyboardFocus();
    }), true);
}

void MainComponent::renameSong (const juce::String& song)
{
    auto* w = new juce::AlertWindow ("Rename song", "New name (type an existing name to merge the two):", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("song", song);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, song] (int res)
    {
        const auto to = w->getTextEditorContents ("song").trim();
        if (res == 1 && to.isNotEmpty() && to != song)
        {
            for (auto* v : library.versionsOf (song))
            {
                v->song = to;
                library.songOverrides[v->sourcePath] = to;
            }
            library.save();
            selectSong (to);
        }
        grabKeyboardFocus();
    }), true);
}

void MainComponent::removeVersion (const juce::String& versionId, bool trashArchive)
{
    if (blind) { flash ("Finish or stop the blind test first."); return; }
    auto it = std::find_if (library.versions.begin(), library.versions.end(), [&] (const Version& v) { return v.id == versionId; });
    if (it == library.versions.end()) return;
    library.ignored.addIfNotAlreadyThere (Library::ignoreKey (it->sourcePath, it->fileTime));
    if (trashArchive && it->archivePath.isNotEmpty()) juce::File (it->archivePath).moveToTrash();
    for (int s = 0; s < 2; ++s)
        if (slotId[s] == versionId) { slotId[s].clear(); trash.push_back (player.setTrack (s, nullptr)); }
    cache.erase (versionId);
    library.versions.erase (it);
    library.save();
    songList.updateContent();
    versionList.updateContent();
    updateGainsAndOffset();
    repaint();
}

// ----------------------------------------------------------------------------
// 再生

void MainComponent::setWhich (int w)
{
    uiWhich = w & 1;
    const int actual = blind ? ((uiWhich == 0) == xIsA ? 0 : 1) : uiWhich;
    player.which = actual;
    repaint();
}

void MainComponent::togglePlay()
{
    if (slotVersion (0) == nullptr && slotVersion (1) == nullptr) { flash ("Pick a song on the left first."); return; }
    player.playing = ! player.playing.load();
}

void MainComponent::updateLoop()
{
    player.setLoop (loopOn && loopE - loopS >= 0.1, loopS, loopE);
    loopButton.setToggleState (loopOn, juce::dontSendNotification);
    repaint();
}

double MainComponent::timeAtX (int x) const
{
    const auto r = timelineArea.reduced (8, 0);
    const double len = player.length.load();
    if (len <= 0.0 || r.getWidth() <= 0) return 0.0;
    return juce::jlimit (0.0, len, (double) (x - r.getX()) / r.getWidth() * len);
}

int MainComponent::xAtTime (double t) const
{
    const auto r = timelineArea.reduced (8, 0);
    const double len = player.length.load();
    if (len <= 0.0) return r.getX();
    return r.getX() + (int) std::lround (t / len * r.getWidth());
}

// ----------------------------------------------------------------------------
// ブラインドテスト: どっちがどっちか伏せて 5 回選ぶ。毎回 X と Y の中身を混ぜ直す

juce::String MainComponent::slotLabel (int slot)
{
    if (blind) return slot == 0 ? "X" : "Y";
    auto* v = slotVersion (slot);
    juce::String s = slot == 0 ? "A    " : "B    ";
    if (v == nullptr) return s + "(click A / B next to a version)";
    s << v->name << "    " << when (v->fileTime);
    if (loading.count (v->id) > 0) s << "   loading...";
    return s;
}

void MainComponent::startBlind()
{
    auto* a = slotVersion (0);
    auto* b = slotVersion (1);
    if (a == nullptr || b == nullptr || a->id == b->id)
    {
        flash ("BLIND TEST: put two different versions in A and B first.");
        return;
    }
    if (! a->an.ok || ! b->an.ok)
    {
        flash ("BLIND TEST: wait until both versions are analysed (the level must be matched).");
        return;
    }
    blind = true;
    blindRound = 0;
    blindChoices.clear();
    xIsA = rng.nextBool();
    setWhich (0);
    updateGainsAndOffset();
    pickX.setVisible (true);
    pickY.setVisible (true);
    blindButton.setToggleState (true, juce::dontSendNotification);
    if (! player.playing.load()) player.playing = true;
    flash ("BLIND TEST round 1 of " + juce::String (kBlindRounds) + ": switch between X and Y (SPACE), then press the one you like.");
    versionList.repaint();
}

void MainComponent::stopBlind()
{
    blind = false;
    pickX.setVisible (false);
    pickY.setVisible (false);
    blindButton.setToggleState (false, juce::dontSendNotification);
    setWhich (player.which.load());
    updateGainsAndOffset();
    versionList.repaint();
    repaint();
}

void MainComponent::blindPick (int xy)
{
    if (! blind) return;
    const int chosen = (xy == 0) == xIsA ? 0 : 1;
    blindChoices.push_back (chosen);
    ++blindRound;
    if (blindRound < kBlindRounds)
    {
        xIsA = rng.nextBool();
        setWhich (0);
        flash ("Round " + juce::String (blindRound + 1) + " of " + juce::String (kBlindRounds)
               + ": X and Y were shuffled again (they may have swapped).");
        return;
    }

    int aWins = 0;
    for (int c : blindChoices) if (c == 0) ++aWins;
    const int bWins = kBlindRounds - aWins;
    auto* a = slotVersion (0);
    auto* b = slotVersion (1);
    juce::String msg;
    if (a != nullptr && b != nullptr)
    {
        a->wins += aWins; a->rounds += kBlindRounds;
        b->wins += bWins; b->rounds += kBlindRounds;
        library.save();
        const bool aBetter = aWins > bWins;
        const auto winner = aBetter ? "A  (" + a->name + ")" : "B  (" + b->name + ")";
        const int top = juce::jmax (aWins, bWins);
        if (top == kBlindRounds)
            msg = "You picked " + winner + " every time (" + juce::String (top) + " of " + juce::String (kBlindRounds) + ").\nYou can really hear the difference, and you prefer it.";
        else if (top == kBlindRounds - 1)
            msg = "You picked " + winner + " " + juce::String (top) + " of " + juce::String (kBlindRounds) + " times.\nProbably a real preference. Run it once more to be sure.";
        else
            msg = "A " + juce::String (aWins) + " : " + juce::String (bWins) + " B.\nThat's a coin flip: you can't really hear a difference between these two. "
                  "Keep whichever you like, and spend your time on something else.";
    }
    stopBlind();
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Blind test result", msg);
}

// ----------------------------------------------------------------------------
// タイマー・操作

void MainComponent::timerCallback()
{
    ++tick;
    pickUpResults();
    if (tick % 100 == 0) scanFolders();       // 4 秒ごと
    if (tick % 125 == 0) refreshOutputs();

    const bool playing = player.playing.load();
    playButton.setButtonText (playing ? "PAUSE" : "PLAY");
    playButton.setToggleState (playing, juce::dontSendNotification);
    loopButton.setToggleState (loopOn, juce::dontSendNotification);
    keepButton.setToggleState (keepCopies, juce::dontSendNotification);
    matchButton.setToggleState (matchLevel || blind, juce::dontSendNotification);
    alignButton.setToggleState (align, juce::dontSendNotification);
    if (auto* v = library.find (selectedVersion)) keeperButton.setToggleState (v->keeper, juce::dontSendNotification);

    slotA.setButtonText (slotLabel (0));
    slotB.setButtonText (slotLabel (1));
    slotA.setColour (juce::TextButton::buttonOnColourId, blind ? blindCol : colA);
    slotB.setColour (juce::TextButton::buttonOnColourId, blind ? blindCol : colB);
    slotA.setToggleState (uiWhich == 0, juce::dontSendNotification);
    slotB.setToggleState (uiWhich == 1, juce::dontSendNotification);
    pickX.setButtonText ("I PREFER X  (" + juce::String (juce::jmin (blindRound + 1, kBlindRounds)) + "/" + juce::String (kBlindRounds) + ")");
    pickY.setButtonText ("I PREFER Y");

    repaint (timelineArea);
    repaint (statusArea);
    repaint (headerArea);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { setWhich (1 - uiWhich); return true; }
    if (k == juce::KeyPress::leftKey)  { player.seek (player.position.load() - 5.0); return true; }
    if (k == juce::KeyPress::rightKey) { player.seek (player.position.load() + 5.0); return true; }
    if (k == juce::KeyPress::homeKey)  { player.seek (loopOn && loopE > loopS ? loopS : 0.0); return true; }
    const auto c = k.getTextCharacter();
    if (c == 'p' || c == 'P' || k == juce::KeyPress::returnKey) { togglePlay(); return true; }
    if (c == 'l' || c == 'L') { loopButton.onClick(); return true; }
    if (c == 'a' || c == 'A') { setWhich (0); return true; }
    if (c == 'b' || c == 'B') { setWhich (1); return true; }
    return false;
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    draggingLoop = false;
    if (timelineArea.contains (e.getPosition())) player.seek (timeAtX (e.x));
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! timelineArea.contains (e.getMouseDownPosition())) return;
    if (std::abs (e.getDistanceFromDragStartX()) < 6 && ! draggingLoop) return;
    // ドラッグした範囲をループにする
    draggingLoop = true;
    const double t0 = timeAtX (e.getMouseDownX()), t1 = timeAtX (e.x);
    loopS = juce::jmin (t0, t1);
    loopE = juce::jmax (t0, t1);
    loopOn = true;
    updateLoop();
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    if (draggingLoop) { player.seek (loopS); draggingLoop = false; }
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! timelineArea.contains (e.getPosition())) return;
    loopOn = false;
    loopS = loopE = 0.0;
    updateLoop();
}

// ----------------------------------------------------------------------------
// 描画

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto h = headerArea;
    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MIX TIME MACHINE", h.removeFromLeft (230), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (text.withAlpha (0.6f));
    juce::String st;
    st << "Watching " << library.roots.size() << (library.roots.size() == 1 ? " folder" : " folders")
       << "   |   " << (int) library.versions.size() << " versions";
    if (analysingCount > 0) st << "   |   analysing " << analysingCount << "...";
    if (! pending.empty()) st << "   |   new bounce being written...";
    g.drawText (st, h.withTrimmedRight (520), juce::Justification::centredLeft);
    g.drawText ("OUTPUT", outLabel, juce::Justification::centredRight);

    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("SONGS", songsLabel, juce::Justification::centredLeft);
    g.drawText ("VERSIONS  (newest first - click A / B to compare)", versionsLabel, juce::Justification::centredLeft);
    g.drawText ("VOLUME", volLabel, juce::Justification::centredLeft);

    drawTimeline (g, timelineArea.toFloat());
    drawCompare (g, compareArea.toFloat());

    // 下の案内
    const bool showFlash = flashMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() * 0.001 < flashUntil;
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (showFlash ? warn : text.withAlpha (0.4f));
    g.drawText (showFlash ? flashMessage
                          : juce::String ("Keys: SPACE = switch A/B   A / B = pick   P = play   LEFT / RIGHT = 5 s   L = loop   "
                                          "drag on the waveform = loop range   double-click = clear loop"),
                statusArea, juce::Justification::centredLeft);
}

void MainComponent::drawTimeline (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    const auto r = area.reduced (8.0f, 6.0f);
    const double len = player.length.load();
    const double offB = player.offsetB.load();

    std::shared_ptr<const Track> tr[2];
    for (int s = 0; s < 2; ++s)
        if (auto it = cache.find (slotId[s]); it != cache.end()) tr[s] = it->second;

    const int heard = player.which.load();
    for (int s = 0; s < 2; ++s)
    {
        auto lane = s == 0 ? r.withHeight (r.getHeight() * 0.5f - 2.0f) : r.withTrimmedTop (r.getHeight() * 0.5f + 2.0f);
        const auto c = blind ? blindCol : (s == 0 ? colA : colB);
        const bool active = ! blind && heard == s;
        g.setColour (c.withAlpha (active ? 0.85f : 0.35f));
        if (tr[s] != nullptr && len > 0.0 && ! tr[s]->overview.empty())
        {
            const auto& ov = tr[s]->overview;
            const double tlen = tr[s]->lengthSeconds();
            const float mid = lane.getCentreY();
            for (int x = 0; x < (int) lane.getWidth(); ++x)
            {
                double t = (double) x / lane.getWidth() * len + (s == 1 ? offB : 0.0);
                if (t < 0.0 || t >= tlen) continue;
                const float v = ov[(size_t) juce::jlimit (0, (int) ov.size() - 1, (int) (t / tlen * (double) ov.size()))];
                const float hh = juce::jmax (0.5f, v * lane.getHeight() * 0.5f);
                g.fillRect (lane.getX() + (float) x, mid - hh, 1.0f, hh * 2.0f);
            }
        }
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (blind ? juce::String (s == 0 ? "?" : "?") : juce::String (s == 0 ? "A" : "B"),
                    lane.withWidth (16.0f).translated (2.0f, 0.0f), juce::Justification::centredLeft);
    }

    if (len > 0.0)
    {
        if (loopOn && loopE - loopS >= 0.1)
        {
            g.setColour (selCol.withAlpha (0.18f));
            g.fillRect ((float) xAtTime (loopS), r.getY(), (float) (xAtTime (loopE) - xAtTime (loopS)), r.getHeight());
        }
        const double pos = player.position.load();
        g.setColour (text);
        g.fillRect ((float) xAtTime (pos) - 0.5f, area.getY(), 1.5f, area.getHeight());
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (mmss (pos) + " / " + mmss (len), r.withTrimmedLeft (r.getWidth() - 110.0f).withHeight (14.0f),
                    juce::Justification::centredRight);
        if (! blind && std::abs (offB) > 0.0005)
        {
            g.setColour (text.withAlpha (0.5f));
            g.drawText ("B aligned " + juce::String (offB * 1000.0, 0) + " ms", r.withTrimmedLeft (r.getWidth() - 200.0f).translated (0, r.getHeight() - 14.0f).withHeight (14.0f),
                        juce::Justification::centredRight);
        }
    }
    else
    {
        g.setColour (text.withAlpha (0.4f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (library.versions.empty() ? "Bounce a mix in Logic (File > Bounce > Project or Section). It appears on the left automatically."
                                             : "Pick a song on the left.",
                    r, juce::Justification::centred);
    }
}

void MainComponent::drawCompare (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    auto r = area.reduced (14.0f, 10.0f);

    if (blind)
    {
        g.setColour (blindCol);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("BLIND TEST   round " + juce::String (juce::jmin (blindRound + 1, kBlindRounds)) + " of " + juce::String (kBlindRounds),
                    r.removeFromTop (40.0f), juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText ("Names and numbers are hidden, and the level is matched.\n"
                          "Switch between X and Y with SPACE (or click X / Y above) as often as you like, then press I PREFER X or I PREFER Y.\n"
                          "After each pick, X and Y are shuffled again. After " + juce::String (kBlindRounds) + " rounds you see which one you really prefer.\n\n"
                          "Tip: loop the part you care about most (drag on the waveform), e.g. the chorus.",
                          r.toNearestInt(), juce::Justification::topLeft, 8);
        return;
    }

    auto* a = slotVersion (0);
    auto* b = slotVersion (1);
    if (a == nullptr || b == nullptr || ! a->an.ok || ! b->an.ok)
    {
        g.setColour (text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ((a != nullptr && a->analysing) || (b != nullptr && b->analysing) ? "Analysing..." : "Put two versions in A and B to see what changed.",
                    r, juce::Justification::centred);
        return;
    }

    // ---- 左: 数値 ----
    auto table = r.removeFromLeft (430.0f);
    r.removeFromLeft (20.0f);
    struct Row { const char* name; float a, b; int dec; const char* unit; };
    const Row rows[] = {
        { "LOUDNESS (whole song)", a->an.lufs, b->an.lufs, 1, "LUFS" },
        { "PEAK", a->an.peakDb, b->an.peakDb, 1, "dBFS" },
        { "DYNAMICS (PLR)", a->an.plr, b->an.plr, 1, "dB" },
        { "LOUDNESS RANGE", a->an.loudRange, b->an.loudRange, 1, "LU" },
        { "WIDTH (side)", a->an.sideRatio * 100.0f, b->an.sideRatio * 100.0f, 0, "%" },
        { "LENGTH", a->an.lengthSec, b->an.lengthSec, 1, "s" },
    };
    const float rowH = 24.0f;
    const float colW = 72.0f, nameW = table.getWidth() - colW * 3.0f;
    auto hdr = table.removeFromTop (rowH);
    hdr.removeFromLeft (nameW);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (colA); g.drawText ("A", hdr.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (colB); g.drawText ("B", hdr.removeFromLeft (colW), juce::Justification::centredRight);
    g.setColour (text.withAlpha (0.6f)); g.drawText ("B - A", hdr.removeFromLeft (colW), juce::Justification::centredRight);
    for (auto& row : rows)
    {
        auto line = table.removeFromTop (rowH);
        g.setColour (grid);
        g.drawHorizontalLine ((int) line.getY(), line.getX(), line.getRight());
        g.setColour (text.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (row.name) + "  " + row.unit, line.removeFromLeft (nameW), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (colA.brighter (0.2f)); g.drawText (fmt (row.a, row.dec), line.removeFromLeft (colW), juce::Justification::centredRight);
        g.setColour (colB.brighter (0.2f)); g.drawText (fmt (row.b, row.dec), line.removeFromLeft (colW), juce::Justification::centredRight);
        g.setColour (text);
        g.drawText (fmt (row.b - row.a, row.dec, true), line.removeFromLeft (colW), juce::Justification::centredRight);
    }

    // ---- 何が変わったか（文章） ----
    const auto diff = mtm::toneDiff (a->an, b->an);
    juce::StringArray lines;
    const float dl = b->an.lufs - a->an.lufs;
    if (std::abs (dl) >= 0.3f)
        lines.add ("B is " + juce::String (std::abs (dl), 1) + " LU " + (dl > 0 ? "louder" : "quieter")
                   + (matchLevel ? " (playback is level-matched, so you hear only the mix difference)" : ""));
    const float dp = b->an.plr - a->an.plr;
    if (dp <= -0.5f)     lines.add ("B is more compressed: PLR " + juce::String (-dp, 1) + " dB lower (less punch, denser)");
    else if (dp >= 0.5f) lines.add ("B has more dynamics: PLR " + juce::String (dp, 1) + " dB higher (more punch)");
    const float dw = (b->an.sideRatio - a->an.sideRatio) * 100.0f;
    if (std::abs (dw) >= 2.0f) lines.add ("B is " + juce::String (dw > 0 ? "wider" : "narrower") + " (side " + fmt (dw, 0, true) + " %)");
    juce::StringArray tone;
    std::vector<std::pair<float, juce::String>> regs;
    for (auto& rg : kRegions)
    {
        const float d = regionDiff (diff, rg);
        if (std::abs (d) >= 0.6f) regs.push_back ({ std::abs (d), juce::String (rg.name) + " " + fmt (d, 1, true) + " dB" });
    }
    std::sort (regs.begin(), regs.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    for (auto& p : regs) tone.add (p.second);
    if (! tone.isEmpty()) lines.add ("Tone in B: " + tone.joinIntoString (",  "));
    if (lines.isEmpty()) lines.add ("A and B are nearly the same in loudness, dynamics, width and tone. Use the blind test to see if you can even hear it.");

    table.removeFromTop (12.0f);
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("WHAT CHANGED (A -> B)", table.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (text.withAlpha (0.9f));
    const auto bullet = juce::String::fromUTF8 ("\xe2\x80\xa2 ");
    g.drawFittedText (bullet + lines.joinIntoString ("\n" + bullet),
                      table.toNearestInt(), juce::Justification::topLeft, 8);

    // ---- 右: 音色の差のグラフ ----
    auto gr = r;
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("TONE: B - A   (loudness removed; above 0 = B has more)", gr.removeFromTop (16.0f), juce::Justification::centredLeft);
    gr.removeFromTop (4.0f);
    gr.removeFromBottom (14.0f);
    const float range = 6.0f;
    auto yOf = [&] (float db) { return gr.getCentreY() - juce::jlimit (-range, range, db) / range * gr.getHeight() * 0.5f; };
    auto xOf = [&] (float f) { return gr.getX() + std::log (f / 20.0f) / std::log (1000.0f) * gr.getWidth(); };
    g.setFont (juce::FontOptions (10.0f));
    for (float db : { -6.0f, -3.0f, 0.0f, 3.0f, 6.0f })
    {
        g.setColour (db == 0.0f ? text.withAlpha (0.35f) : grid);
        g.drawHorizontalLine ((int) yOf (db), gr.getX(), gr.getRight());
        g.setColour (text.withAlpha (0.4f));
        g.drawText (fmt (db, 0, true), (int) gr.getX() - 2, (int) yOf (db) - 7, 24, 14, juce::Justification::centredLeft);
    }
    for (auto& rg : kRegions)
    {
        const float x0 = xOf (rg.lo), x1 = xOf (rg.hi);
        g.setColour (grid);
        g.drawVerticalLine ((int) x0, gr.getY(), gr.getBottom());
        g.setColour (text.withAlpha (0.45f));
        g.drawText (rg.name, (int) x0, (int) gr.getBottom() + 1, (int) (x1 - x0), 12, juce::Justification::centred);
    }
    juce::Path p;
    for (int i = 0; i < mtm::kNumBands; ++i)
    {
        const float x = xOf (mtm::bandCentre (i)), y = yOf (diff[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colB);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    headerArea = area.removeFromTop (40);
    {
        auto h = headerArea;
        outputBox.setBounds (h.removeFromRight (230).reduced (0, 6));
        outLabel = h.removeFromRight (60);
        h.removeFromRight (12);
        keepButton.setBounds (h.removeFromRight (120).reduced (0, 6));
        h.removeFromRight (6);
        foldersButton.setBounds (h.removeFromRight (110).reduced (0, 6));
    }
    area.removeFromTop (8);
    statusArea = area.removeFromBottom (20);
    area.removeFromBottom (6);

    // 左: 曲と版
    auto left = area.removeFromLeft (360);
    area.removeFromLeft (12);
    songsLabel = left.removeFromTop (16);
    songList.setBounds (left.removeFromTop (juce::jmax (120, left.getHeight() / 3)));
    left.removeFromTop (8);
    versionsLabel = left.removeFromTop (16);
    auto btns = left.removeFromBottom (30);
    left.removeFromBottom (6);
    versionList.setBounds (left);
    noteButton.setBounds (btns.removeFromLeft (btns.getWidth() / 2 - 3));
    btns.removeFromLeft (6);
    keeperButton.setBounds (btns);

    // 右
    auto slots = area.removeFromTop (52);
    slotA.setBounds (slots.removeFromLeft (slots.getWidth() / 2));
    slotB.setBounds (slots);
    area.removeFromTop (8);

    auto tr = area.removeFromTop (32);
    playButton.setBounds (tr.removeFromLeft (100));  tr.removeFromLeft (6);
    loopButton.setBounds (tr.removeFromLeft (70));   tr.removeFromLeft (6);
    matchButton.setBounds (tr.removeFromLeft (120)); tr.removeFromLeft (6);
    alignButton.setBounds (tr.removeFromLeft (70));  tr.removeFromLeft (14);
    blindButton.setBounds (tr.removeFromLeft (110)); tr.removeFromLeft (6);
    pickX.setBounds (tr.removeFromLeft (150));       tr.removeFromLeft (6);
    pickY.setBounds (tr.removeFromLeft (110));       tr.removeFromLeft (14);
    volLabel = tr.removeFromLeft (52);
    volumeSlider.setBounds (tr);
    area.removeFromTop (8);

    timelineArea = area.removeFromTop (120);
    area.removeFromTop (8);
    compareArea = area;
}
