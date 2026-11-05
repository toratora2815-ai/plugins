#include "MainComponent.h"
#include "MacSupport.h"

namespace
{
const juce::Colour bg (0xff0e1117), panel (0xff171c26), panel2 (0xff1f2633), line (0xff2b3445),
                   textC (0xffe8ecf3), dim (0xff8a94a6), accent (0xff5ee0b0), red (0xffff4d5e), amber (0xffffc24b);

constexpr int W = 780, H = 640, rowH = 40;
constexpr double maxRecSeconds = 5.0;      // 口マネは 5 秒まで
constexpr double silenceStop = 0.55;       // 鳴ったあと、これだけ黙ったら止める
constexpr float loudThreshold = 0.02f;     // 約 -34 dBFS より大きければ「鳴っている」
constexpr int maxHits = 60;

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

void styleButton (juce::TextButton& b, bool on = false)
{
    b.setColour (juce::TextButton::buttonColourId, on ? accent : panel2);
    b.setColour (juce::TextButton::buttonOnColourId, accent);
    b.setColour (juce::TextButton::textColourOffId, textC);
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff06261b));
    b.setColour (juce::ComboBox::outlineColourId, line);
}

juce::String secondsText (float s)
{
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000)) + " ms";
    if (s < 60.0f) return juce::String (s, 1) + u8 (" 秒");
    return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2);
}

juce::String kindTags (uint32_t k)
{
    juce::StringArray t;
    for (int i = 1; i < sketch::kNumKinds; ++i) if (k & (1u << i)) t.add (u8 (sketch::kindLabel (i)));
    return t.joinIntoString (" / ");
}
} // namespace

//==============================================================================
// 結果の一覧（クリックで試聴、ドラッグで Logic へ、右クリックでメニュー、ダブルクリックで「これに似た音」）
class MainComponent::ResultList : public juce::Component
{
public:
    explicit ResultList (MainComponent& o) : owner (o) {}

    void update()
    {
        setSize (getWidth(), std::max (1, (int) owner.hits.size()) * rowH);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        const auto& E = *owner.lib;
        if (owner.hits.empty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (13.0f));
            juce::String t;
            if (E.empty()) t = owner.library.scanning() ? u8 ("音を読み込んでいます…") : u8 ("音のフォルダがありません。右上の「フォルダ…」から足してください");
            else if (owner.qkind == None) t = u8 ("左の丸を押して（またはスペースキー）、探したい音を口で言ってみてください");
            else t = u8 ("当てはまる音がありません（種類や文字のしぼり込みを外してみてください）");
            g.drawFittedText (t, getLocalBounds().reduced (20, 0).withHeight (rowH * 2), juce::Justification::centred, 2);
            return;
        }
        const auto clip = g.getClipBounds();
        for (int r = std::max (0, clip.getY() / rowH); r < std::min ((int) owner.hits.size(), clip.getBottom() / rowH + 1); ++r)
        {
            const auto& h = owner.hits[(size_t) r];
            if (h.index < 0 || h.index >= (int) E.size()) continue;
            const auto& e = E[(size_t) h.index];
            auto b = juce::Rectangle<int> (0, r * rowH, getWidth(), rowH);
            const bool isPlaying = owner.playing == h.index, isHover = owner.hover == r;
            if (isPlaying) g.setColour (accent.withAlpha (0.16f));
            else g.setColour (isHover ? panel2 : (r % 2 ? panel : panel.brighter (0.02f)));
            g.fillRect (b);
            g.setColour (line.withAlpha (0.6f));
            g.fillRect (b.removeFromBottom (1));

            // 再生ボタン
            auto icon = b.removeFromLeft (34).toFloat().withSizeKeepingCentre (14, 14);
            g.setColour (isPlaying ? accent : (isHover ? textC : dim));
            if (isPlaying) g.fillRect (icon.reduced (1.5f));
            else
            {
                juce::Path p; p.addTriangle (icon.getX() + 2, icon.getY(), icon.getX() + 2, icon.getBottom(), icon.getRight(), icon.getCentreY());
                g.fillPath (p);
            }

            // 似ている度
            if (owner.qkind != None)
            {
                auto m = b.removeFromLeft (78).reduced (4, 13);
                const int pct = sketch::matchPercent (h.dist);
                g.setColour (panel2.brighter (0.05f));
                g.fillRoundedRectangle (m.toFloat(), 3);
                g.setColour (accent.withAlpha (0.35f + 0.65f * pct / 100.0f));
                g.fillRoundedRectangle (m.toFloat().withWidth (std::max (3.0f, m.getWidth() * pct / 100.0f)), 3);
                g.setColour (textC);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (juce::String (pct) + "%", m.translated (0, 0), juce::Justification::centred);
            }

            auto right = b.removeFromRight (isHover ? 168 : 90).reduced (8, 0);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (dim);
            juce::String len = secondsText (e.fileSeconds);
            if (isHover) len = u8 ("ドラッグで Logic へ  ") + len;
            g.drawText (len, right, juce::Justification::centredRight);

            b = b.reduced (6, 4);
            g.setColour (textC);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (e.name().upToLastOccurrenceOf (".", false, false), b.removeFromTop (17), juce::Justification::centredLeft, true);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            auto tags = kindTags (e.kinds);
            g.drawText ((tags.isNotEmpty() ? tags + u8 ("  ・  ") : juce::String()) + e.folder(), b, juce::Justification::centredLeft, true);
        }
    }

    int rowAt (juce::Point<int> p) const
    {
        const int r = p.y / rowH;
        return r >= 0 && r < (int) owner.hits.size() ? r : -1;
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override  { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        downRow = rowAt (e.getPosition());
        dragged = false;
        if (downRow >= 0 && e.mods.isPopupMenu()) { owner.showRowMenu (downRow); downRow = -1; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (downRow < 0 || dragged || e.getDistanceFromDragStart() < 6) return;
        dragged = true;
        owner.startDrag (downRow);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (downRow < 0 || dragged || e.mods.isPopupMenu()) return;
        const int idx = owner.hits[(size_t) downRow].index;
        if (owner.playing == idx) owner.stopPlayback();
        else owner.play (idx);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.getPosition());
        if (r >= 0) owner.searchLike (owner.hits[(size_t) r].index);
    }

private:
    MainComponent& owner;
    int downRow = -1;
    bool dragged = false;

    void setHover (int r)
    {
        if (r == owner.hover) return;
        owner.hover = r;
        repaint();
    }
};

//==============================================================================
MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Sound Sketch"; po.folderName = "Sound Sketch"; po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);
    formats.registerBasicFormats();

    folderBtn.setButtonText (u8 ("フォルダ…"));
    styleButton (folderBtn);
    folderBtn.onClick = [this] { showFolderMenu(); };
    addAndMakeVisible (folderBtn);

    voiceBtn.setButtonText (u8 ("▶ 自分の声"));
    styleButton (voiceBtn);
    voiceBtn.onClick = [this]
    {
        if (query.empty()) return;
        stopPlayback();
        { const juce::ScopedLock sl (audioLock); previewVoice = query; }
        voicePos = 0;
    };
    addAndMakeVisible (voiceBtn);

    deviceBtn.setButtonText (u8 ("マイク…"));
    styleButton (deviceBtn);
    deviceBtn.onClick = [this]
    {
        auto sel = std::make_unique<juce::AudioDeviceSelectorComponent> (devices, 1, 2, 2, 2, false, false, false, false);
        sel->setSize (420, 260);
        juce::CallOutBox::launchAsynchronously (std::move (sel), deviceBtn.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (deviceBtn);

    kindFilter = props->getIntValue ("kind", 0);
    for (int k = 0; k < sketch::kNumKinds; ++k)
    {
        auto* b = kindBtns.add (new juce::TextButton (u8 (sketch::kindLabel (k))));
        styleButton (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (11);
        b->setToggleState (k == kindFilter, juce::dontSendNotification);
        b->onClick = [this, k] { if (kindBtns[k]->getToggleState()) { kindFilter = k; props->setValue ("kind", k); runQuery(); } };
        addAndMakeVisible (b);
    }

    searchBox.setTextToShowWhenEmpty (u8 ("名前でしぼる"), dim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, panel2);
    searchBox.setColour (juce::TextEditor::outlineColourId, line);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    searchBox.setColour (juce::TextEditor::textColourId, textC);
    searchBox.setFont (juce::FontOptions (13.0f));
    searchBox.onTextChange = [this] { runQuery(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); runQuery(); grabKeyboardFocus(); };
    addAndMakeVisible (searchBox);

    list = std::make_unique<ResultList> (*this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, line.brighter (0.3f));
    addAndMakeVisible (viewport);

    setWantsKeyboardFocus (true);
    setSize (W, H);

    lib = library.entries();
    libGen = library.generation();
    runQuery();

    if (live)
    {
        mac::requestMic ([this] (bool ok)
        {
            micOk = ok;
            if (! ok) message = u8 ("マイクの許可がありません（システム設定 → プライバシーとセキュリティ → マイク で Sound Sketch を ON）");
            openDevice (ok);
            repaint();
        });
        library.scan (folderList());
        startTimerHz (30);
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    devices.removeAudioCallback (this);
    if (live)
        if (auto xml = devices.createStateXml()) props->setValue ("audioDevice", xml.get());
    transport.setSource (nullptr);
    props->saveIfNeeded();
}

//==============================================================================
juce::StringArray MainComponent::folderList() const
{
    juce::StringArray f;
    if (props->containsKey ("folders"))
    {
        f.addLines (props->getValue ("folders"));
        f.removeEmptyStrings();
        return f;
    }
    // 最初は Splice のダウンロードした音（あれば）
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    if (home.getChildFile ("Splice/sounds").isDirectory()) f.add (home.getChildFile ("Splice/sounds").getFullPathName());
    else if (home.getChildFile ("Splice").isDirectory()) f.add (home.getChildFile ("Splice").getFullPathName());
    return f;
}

void MainComponent::setFolders (const juce::StringArray& f)
{
    props->setValue ("folders", f.joinIntoString ("\n"));
    props->saveIfNeeded();
    library.scan (f);
    repaint();
}

void MainComponent::showFolderMenu()
{
    const auto folders = folderList();
    juce::PopupMenu m;
    m.addSectionHeader (u8 ("探すフォルダ（中のフォルダも全部）"));
    for (int i = 0; i < folders.size(); ++i)
    {
        juce::PopupMenu sub;
        sub.addItem (1000 + i, u8 ("Finder で開く"));
        sub.addItem (2000 + i, u8 ("外す"));
        m.addSubMenu (folders[i].replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~"), sub);
    }
    if (folders.isEmpty()) m.addItem (-1, u8 ("（まだありません）"), false);
    m.addSeparator();
    m.addItem (1, u8 ("フォルダを足す…"));
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    struct Quick { const char* label; juce::File dir; };
    const Quick quick[] { { "Splice の音を足す", home.getChildFile ("Splice/sounds") },
                          { "Logic のサンプル（Apple Loops）を足す", juce::File ("/Library/Audio/Apple Loops") },
                          { "ミュージック フォルダを足す", juce::File::getSpecialLocation (juce::File::userMusicDirectory) } };
    for (int i = 0; i < 3; ++i)
        if (quick[i].dir.isDirectory() && ! folders.contains (quick[i].dir.getFullPathName()))
            m.addItem (10 + i, u8 (quick[i].label));
    m.addItem (2, u8 ("全部読み直す"), ! library.scanning());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (folderBtn), [this, folders, quick] (int r)
    {
        auto f = folders;
        if (r == 1)
        {
            auto chooser = std::make_shared<juce::FileChooser> (u8 ("音の入ったフォルダを選ぶ"),
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, chooser, f] (const juce::FileChooser& c) mutable
            {
                const auto d = c.getResult();
                if (d.isDirectory() && ! f.contains (d.getFullPathName())) { f.add (d.getFullPathName()); setFolders (f); }
            });
        }
        else if (r == 2) library.scan (f);
        else if (r >= 10 && r < 13) { f.add (quick[r - 10].dir.getFullPathName()); setFolders (f); }
        else if (r >= 2000) { f.remove (r - 2000); setFolders (f); }
        else if (r >= 1000) juce::File (f[r - 1000]).revealToUser();
    });
}

//==============================================================================
void MainComponent::openDevice (bool withInput)
{
    devices.removeAudioCallback (this);
    auto xml = props->getXmlValue ("audioDevice");
    const auto err = devices.initialise (withInput ? 1 : 0, 2, xml.get(), true);
    if (err.isNotEmpty()) message = u8 ("オーディオ機器を開けませんでした: ") + err;
    devices.addAudioCallback (this);
}

void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    deviceFs = d->getCurrentSampleRate();
    deviceBlock = d->getCurrentBufferSizeSamples();
    transport.prepareToPlay (deviceBlock, deviceFs);
}

void MainComponent::audioDeviceStopped() { transport.releaseResources(); }

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                      int n, const juce::AudioIODeviceCallbackContext&)
{
    // 入力（口マネ）
    if (recording.load() && numIn > 0 && in[0] != nullptr)
    {
        int pos = recN.load();
        float pk = 0;
        for (int i = 0; i < n && pos < (int) rec.size(); ++i, ++pos)
        {
            float x = in[0][i];
            if (numIn > 1 && in[1] != nullptr) x = 0.5f * (x + in[1][i]);
            rec[(size_t) pos] = x;
            const float a = std::abs (x);
            pk = std::max (pk, a);
            if (a > loudThreshold) lastLoud = pos;
        }
        recN = pos;
        level = std::max (pk, level.load() * 0.9f);
    }

    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    // 出力（自分の声 か 試聴）
    int vp = voicePos.load();
    if (vp >= 0)
    {
        const juce::ScopedTryLock sl (audioLock);
        if (sl.isLocked())
        {
            const int m = std::min (n, (int) previewVoice.size() - vp);
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr && m > 0) juce::FloatVectorOperations::copy (out[c], previewVoice.data() + vp, m);
            vp += std::max (0, m);
            voicePos = vp >= (int) previewVoice.size() ? -1 : vp;
        }
        return;
    }
    if (numOut > 0)
    {
        juce::AudioBuffer<float> buf (out, numOut, n);
        juce::AudioSourceChannelInfo info (buf);
        transport.getNextAudioBlock (info);
    }
}

//==============================================================================
void MainComponent::toggleRecord()
{
    if (recording) stopRecording();
    else startRecording();
}

void MainComponent::startRecording()
{
    if (! micOk) { message = u8 ("マイクの許可がないので録れません（システム設定 → プライバシーとセキュリティ → マイク）"); repaint(); return; }
    stopPlayback();
    voicePos = -1;
    rec.assign ((size_t) (deviceFs * maxRecSeconds), 0.0f);
    recN = 0;
    lastLoud = -1;
    level = 0;
    recStartMs = juce::Time::getMillisecondCounterHiRes();
    recording = true;
    message = u8 ("録っています… 言い終わったら少し黙ると止まります");
    repaint();
}

void MainComponent::stopRecording()
{
    if (! recording) return;
    recording = false;
    const int n = recN.load();
    query.assign (rec.begin(), rec.begin() + std::min (n, (int) rec.size()));
    queryFs = deviceFs;
    auto f = sketch::analyse (query.data(), (int) query.size(), queryFs);
    if (! f.valid || lastLoud.load() < 0)
    {
        message = u8 ("声が小さすぎて聞き取れませんでした。マイクに近づいて、もう一度どうぞ");
        repaint();
        return;
    }
    qf = sketch::voiceToLibrary (f);
    qkind = Voice;
    message = {};
    runQuery();
    // 1 位をすぐ鳴らす
    if (! hits.empty()) play (hits.front().index);
}

void MainComponent::searchWithFile (const juce::File& wav)
{
    std::vector<float> mono; double fs = 0, secs = 0;
    if (! Library::readMono (formats, wav, mono, fs, secs)) return;
    query = mono; queryFs = fs;
    qf = sketch::voiceToLibrary (sketch::analyse (mono.data(), (int) mono.size(), fs));
    qkind = Voice;
    runQuery();
}

void MainComponent::waitForLibrary()
{
    library.scan (folderList());
    for (int i = 0; i < 6000 && library.scanning(); ++i) juce::Thread::sleep (10);
    lib = library.entries();
    libGen = library.generation();
    runQuery();
}

void MainComponent::searchLike (int idx)
{
    if (idx < 0 || idx >= (int) lib->size() || ! (*lib)[(size_t) idx].f.valid) return;
    qf = (*lib)[(size_t) idx].f;
    exampleName = (*lib)[(size_t) idx].name();
    qkind = Example;
    query.clear();
    runQuery();
    viewport.setViewPosition (0, 0);
}

void MainComponent::runQuery()
{
    hits.clear();
    const auto& E = *lib;
    const auto text = searchBox.getText().trim().toLowerCase();
    juce::StringArray words;
    words.addTokens (text, " ", "");
    words.removeEmptyStrings();
    const juce::String self = qkind == Example ? exampleName : juce::String();

    for (int i = 0; i < (int) E.size(); ++i)
    {
        const auto& e = E[(size_t) i];
        if (kindFilter > 0 && ! (e.kinds & (1u << kindFilter))) continue;
        if (! words.isEmpty())
        {
            const auto p = e.path.toLowerCase();
            bool all = true;
            for (auto& w : words) if (! p.contains (w)) { all = false; break; }
            if (! all) continue;
        }
        if (qkind != None)
        {
            if (! e.f.valid) continue;
            if (qkind == Example && e.name() == self) continue;
            hits.push_back ({ i, sketch::distance (qf, e.f) });
        }
        else hits.push_back ({ i, 0.0f });
    }
    if (qkind != None)
    {
        const auto k = std::min<size_t> (maxHits, hits.size());
        std::partial_sort (hits.begin(), hits.begin() + (long) k, hits.end(), [] (const Hit& a, const Hit& b) { return a.dist < b.dist; });
        hits.resize (k);
    }
    else
    {
        std::sort (hits.begin(), hits.end(), [&E] (const Hit& a, const Hit& b)
                   { return E[(size_t) a.index].name().compareNatural (E[(size_t) b.index].name()) < 0; });
        if (hits.size() > 500) hits.resize (500);
    }
    hover = -1;
    list->update();
    repaint();
}

//==============================================================================
void MainComponent::play (int idx)
{
    stopPlayback();
    voicePos = -1;
    if (idx < 0 || idx >= (int) lib->size()) return;
    const juce::File f ((*lib)[(size_t) idx].path);
    auto* reader = formats.createReaderFor (f);
    if (reader == nullptr) { message = u8 ("このファイルは開けませんでした: ") + f.getFileName(); repaint(); return; }
    const double fs = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (readerSource.get(), 0, nullptr, fs);
    transport.setPosition (0);
    transport.start();
    playing = idx;
    list->repaint();
}

void MainComponent::stopPlayback()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    if (playing >= 0) { playing = -1; list->repaint(); }
}

void MainComponent::startDrag (int row)
{
    if (row < 0 || row >= (int) hits.size()) return;
    const auto& e = (*lib)[(size_t) hits[(size_t) row].index];
    if (! juce::File (e.path).existsAsFile()) return;
    performExternalDragDropOfFiles ({ e.path }, false, this);
}

void MainComponent::showRowMenu (int row)
{
    if (row < 0 || row >= (int) hits.size()) return;
    const int idx = hits[(size_t) row].index;
    const auto path = (*lib)[(size_t) idx].path;
    juce::PopupMenu m;
    m.addItem (1, u8 ("鳴らす"));
    m.addItem (2, u8 ("これに似た音を探す（ダブルクリックでも）"));
    m.addItem (3, u8 ("Finder で表示"));
    m.addItem (4, u8 ("場所をコピー"));
    m.showMenuAsync ({}, [this, idx, path] (int r)
    {
        if (r == 1) play (idx);
        else if (r == 2) searchLike (idx);
        else if (r == 3) juce::File (path).revealToUser();
        else if (r == 4) juce::SystemClipboard::copyTextToClipboard (path);
    });
}

//==============================================================================
void MainComponent::timerCallback()
{
    if (recording)
    {
        const double el = (juce::Time::getMillisecondCounterHiRes() - recStartMs) / 1000.0;
        const int ll = lastLoud.load();
        if (el >= maxRecSeconds - 0.05 || (ll >= 0 && (recN.load() - ll) / deviceFs > silenceStop)) stopRecording();
        level = level.load() * 0.85f;
        repaint (recArea);
    }
    if (library.generation() != libGen)
    {
        libGen = library.generation();
        stopPlayback();
        lib = library.entries();
        runQuery();
    }
    if (playing >= 0 && ! transport.isPlaying() && transport.hasStreamFinished()) { playing = -1; list->repaint(); }
    static int tick = 0;
    if (library.scanning() && ++tick % 10 == 0) repaint (header);
    voiceBtn.setEnabled (! query.empty());
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { toggleRecord(); return true; }
    if (k == juce::KeyPress::escapeKey) { stopPlayback(); voicePos = -1; return true; }
    return false;
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (recCircle.contains (e.getPosition())) toggleRecord();
}

//==============================================================================
juce::String MainComponent::statusText() const
{
    int valid = 0;
    for (auto& e : *lib) valid += e.f.valid ? 1 : 0;
    if (library.scanning())
        return u8 ("読み込み中 ") + juce::String (library.progressDone()) + " / " + juce::String (library.progressTotal());
    return juce::String (valid) + u8 (" 個の音");
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (58);
    {
        auto h = header.reduced (18, 14);
        folderBtn.setBounds (h.removeFromRight (96));
        h.removeFromRight (8);
        deviceBtn.setBounds (h.removeFromRight (76));
        h.removeFromRight (120);   // 音の数
        searchBox.setBounds (h.removeFromRight (juce::jlimit (120, 220, h.getWidth() - 300)));
    }
    recArea = r.removeFromTop (128).reduced (18, 6);
    {
        auto a = recArea;
        recCircle = a.removeFromLeft (112).withTrimmedBottom (20).withSizeKeepingCentre (84, 84);
        a.removeFromLeft (14);
        wave = a;
        voiceBtn.setBounds (wave.getRight() - 96, wave.getBottom() - 28, 88, 22);
    }
    filters = r.removeFromTop (44).reduced (18, 8);
    {
        auto f = filters;
        const int n = kindBtns.size();
        int x = f.getX();
        for (int i = 0; i < n; ++i)
        {
            const int w = juce::jlimit (36, 110, (int) (juce::GlyphArrangement::getStringWidth (juce::FontOptions (16.0f), kindBtns[i]->getButtonText()) + 22));
            kindBtns[i]->setBounds (x, f.getY(), w, f.getHeight());
            x += w + 4;
        }
        // はみ出すときは小さく
        if (x > f.getRight())
        {
            const float s = (float) (f.getWidth() - 4 * n) / (float) (x - f.getX() - 4 * n);
            x = f.getX();
            for (int i = 0; i < n; ++i)
            {
                const int w = (int) (kindBtns[i]->getWidth() * s);
                kindBtns[i]->setBounds (x, f.getY(), w, f.getHeight());
                x += w + 4;
            }
        }
    }
    footer = r.removeFromBottom (30);
    viewport.setBounds (r.reduced (18, 4));
    list->setSize (viewport.getMaximumVisibleWidth(), list->getHeight());
    list->update();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // 見出し
    {
        auto h = header.reduced (18, 0);
        g.setColour (textC);
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        g.drawText ("Sound Sketch", h.removeFromLeft (170), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (u8 ("口マネで音を探す"), h.removeFromLeft (120), juce::Justification::centredLeft);
        g.drawText (statusText(), h.withTrimmedRight (186).removeFromRight (114), juce::Justification::centredRight);
    }

    // 録音の丸
    {
        const auto c = recCircle.toFloat();
        const bool rec_ = recording.load();
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (level.load()) * 1.6f);
        if (rec_)
        {
            g.setColour (red.withAlpha (0.25f));
            g.fillEllipse (c.expanded (4 + 10 * lv));
        }
        g.setColour (rec_ ? red : panel2);
        g.fillEllipse (c);
        g.setColour (rec_ ? red.brighter (0.3f) : line.brighter (0.2f));
        g.drawEllipse (c.reduced (1), 2);
        g.setColour (rec_ ? juce::Colours::white : red);
        if (rec_) g.fillRoundedRectangle (c.withSizeKeepingCentre (26, 26), 4);
        else g.fillEllipse (c.withSizeKeepingCentre (30, 30));
        g.setColour (rec_ ? red : dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (rec_ ? u8 ("止める") : u8 ("録る（スペース）"), recCircle.withY (recCircle.getBottom() + 4).withHeight (18).expanded (24, 0),
                    juce::Justification::centred);
    }

    // 口マネの波形と、いま何で探しているか
    {
        g.setColour (panel);
        g.fillRoundedRectangle (wave.toFloat(), 8);
        auto w = wave.reduced (14, 10);
        g.setFont (juce::FontOptions (12.0f));
        if (recording.load())
        {
            g.setColour (red);
            g.drawText (u8 ("● 録っています"), w.removeFromTop (18), juce::Justification::centredLeft);
        }
        else if (qkind == Voice && ! query.empty())
        {
            g.setColour (dim);
            g.drawText (u8 ("あなたの口マネに近い順"), w.removeFromTop (18), juce::Justification::centredLeft);
        }
        else if (qkind == Example)
        {
            g.setColour (dim);
            g.drawText (u8 ("「") + exampleName + u8 ("」に似た音"), w.removeFromTop (18), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (dim);
            g.drawFittedText (u8 ("丸を押して（またはスペースキー）、探したい音を口で言ってみてください。\n"
                                  "例:「ドゥン」→ キック、「ツッ」→ ハット、「パッ」→ スネア、「シューッ↑」→ ライザー"),
                              w, juce::Justification::centredLeft, 3);
        }

        const std::vector<float>* src = recording.load() ? &rec : &query;
        const int n = recording.load() ? recN.load() : (int) query.size();
        if (n > 0 && qkind != Example)
        {
            auto plot = w.withTrimmedRight (104).toFloat();
            const int cols = (int) plot.getWidth();
            const int total = recording.load() ? (int) (deviceFs * maxRecSeconds) : n;
            g.setColour (recording.load() ? red.withAlpha (0.8f) : accent.withAlpha (0.85f));
            for (int x = 0; x < cols; ++x)
            {
                const int a = (int) ((juce::int64) x * total / cols), b = (int) ((juce::int64) (x + 1) * total / cols);
                if (a >= n) break;
                float pk = 0;
                for (int i = a; i < std::min (b, n); ++i) pk = std::max (pk, std::abs ((*src)[(size_t) i]));
                const float hgt = std::min (1.0f, std::sqrt (pk) * 1.2f) * plot.getHeight() * 0.5f;
                g.fillRect (plot.getX() + x, plot.getCentreY() - hgt, 1.0f, std::max (1.0f, hgt * 2));
            }
        }
    }

    // 下の一言
    {
        auto f = footer.reduced (18, 0);
        g.setFont (juce::FontOptions (11.5f));
        if (message.isNotEmpty()) { g.setColour (amber); g.drawText (message, f, juce::Justification::centredLeft, true); }
        else
        {
            g.setColour (dim);
            g.drawText (u8 ("クリックで試聴 ・ ドラッグで Logic へ ・ ダブルクリックで「これに似た音」 ・ 右クリックでほかの操作"),
                        f, juce::Justification::centredLeft, true);
        }
    }
}
