#include "MainComponent.h"
#include "MacSupport.h"
#include "KeyFinder.h"
#include <map>

namespace
{
const juce::Colour bg (0xff0e1117), panel (0xff171c26), panel2 (0xff1f2633), line (0xff2b3445),
                   textC (0xffe6e9f0), dim (0xff8b93a7), orange (0xffff8a3d), cyan (0xff38e1ff), pink (0xffff5c8a),
                   green (0xff5fe08f);
constexpr int numPads = hum::Params::numDrums;
const juce::Colour padCol[numPads] = { orange, pink, cyan, juce::Colour (0xff8ff0ff), juce::Colour (0xffb58cff), juce::Colour (0xffffd34d) };
const char* padName[numPads] = { "KICK", "SNARE", "HAT", "OPEN HAT", "TOM", "CLAP" };
const char* padSay[numPads]  = { "ドン / ブッ", "タッ / カッ / プシッ", "ツッ / チッ", "ツー / チー（のばす）", "ボン / ドゥン", "パッ / ッパ" };

juce::String jp (const char* s) { return juce::String::fromUTF8 (s); }

// Logic と同じ呼び方（真ん中の C = C3 = 60）
juce::String noteName (int n)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (nm[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);
}
juce::String pitchClassName (int k)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return nm[((k % 12) + 12) % 12];
}

const char* scaleNames[] = { "Chromatic（全部の音）", "Major（メジャー）", "Minor（マイナー）", "Harmonic Minor",
                             "Dorian（ドリアン）", "Mixolydian（ミクソリディアン）", "Major Pentatonic（ヨナ抜き）",
                             "Minor Pentatonic", "Blues（ブルース）" };

void styleSlider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setRange (lo, hi, step);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::trackColourId, orange.withAlpha (0.45f));
    s.setColour (juce::Slider::backgroundColourId, panel2);
    s.setColour (juce::Slider::textBoxTextColourId, textC);
    s.setColour (juce::Slider::textBoxOutlineColourId, line);
}

constexpr float minDb = -80.0f;
float dbToY (float db, juce::Rectangle<int> r)
{
    return juce::jmap (juce::jlimit (minDb, 0.0f, db), minDb, 0.0f, (float) r.getBottom(), (float) r.getY());
}

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// 保存した .mid を試し聴き用の音に
bool loadTakeNotes (const juce::File& f, std::vector<Preview::Note>& out, bool& drums, double& lengthSec)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) return false;
    mf.convertTimestampTicksToSeconds();
    drums = false; out.clear(); lengthSec = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            const auto& m = ev->message;
            if (m.isTextMetaEvent() && m.getTextFromTextMetaEvent().contains ("drums")) drums = true;
            if (m.isNoteOn())
            {
                const double s = m.getTimeStamp();
                const double e = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : s + 0.2;
                out.push_back ({ s, e, m.getNoteNumber(), (int) m.getVelocity() });
                lengthSec = std::max (lengthSec, e);
            }
        }
    }
    if (! out.empty()) lengthSec -= out.front().s;
    return true;
}
} // namespace

//==============================================================================
// テイクの履歴（~/Music/Hum Keys の .mid）。▶ で試し聴き、行をつかんで Logic へドラッグ、× でゴミ箱へ
class HistoryPanel : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit HistoryPanel (MainComponent& o) : owner (o)
    {
        addAndMakeVisible (list);
        list.setModel (this);
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, panel);
        addAndMakeVisible (closeButton);
        closeButton.setButtonText (jp ("閉じる"));
        closeButton.onClick = [this] { owner.closeHistory(); };
        reload();
        startTimerHz (15);
    }

    void reload()
    {
        items.clear();
        auto files = MainComponent::takeFolder().findChildFiles (juce::File::findFiles, false, "*.mid");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });
        for (auto& f : files)
        {
            if (items.size() >= 300) break;
            Item it;
            it.file = f;
            double len = 0;
            if (! loadTakeNotes (f, it.notes, it.drums, len)) continue;
            it.when = f.getLastModificationTime().formatted ("%m/%d  %H:%M");
            it.info = juce::String ((int) it.notes.size()) + jp (" 音 · ") + juce::String (len, 1) + jp (" 秒")
                    + (it.drums ? jp (" · ドラム") : juce::String());
            const auto name = f.getFileNameWithoutExtension();
            if (name.contains (" bar")) it.info += jp (" · ") + name.fromLastOccurrenceOf (" bar", false, false) + jp (" 小節目から");
            items.push_back (std::move (it));
        }
        list.updateContent();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        g.setColour (textC);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (jp ("テイクの履歴"), 14, 8, 200, 24, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (items.empty() ? jp ("まだありません（歌うと自動でたまります）")
                                  : jp ("▶ で試し聴き ・ 行をつかんで Logic へドラッグ ・ × でゴミ箱へ"),
                    220, 8, getWidth() - 320, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (30);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        r.removeFromTop (6);
        list.setBounds (r);
    }

private:
    struct Item { juce::File file; juce::String when, info; std::vector<Preview::Note> notes; bool drums = false; };
    MainComponent& owner;
    juce::ListBox list;
    juce::TextButton closeButton;
    std::vector<Item> items;
    double lastDragMs = 0;

    void timerCallback() override
    {
        if (! owner.preview.isPlaying() && owner.preview.currentId().isNotEmpty()) owner.preview.stop();
        list.repaint();
    }

    int getNumRows() override { return (int) items.size(); }

    static juce::Rectangle<int> playRect (int h) { return { 6, 6, h - 12, h - 12 }; }
    static juce::Rectangle<int> trashRect (int w, int h) { return { w - h + 4, 6, h - 12, h - 12 }; }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) items.size()) return;
        const auto& it = items[(size_t) row];
        const bool playing = owner.preview.isPlaying() && owner.preview.currentId() == it.file.getFullPathName();
        g.setColour (row % 2 ? panel : panel2.withAlpha (0.6f));
        g.fillRect (0, 0, w, h);
        auto pr = playRect (h).toFloat();
        g.setColour (playing ? orange : line);
        g.fillRoundedRectangle (pr, 5);
        g.setColour (playing ? juce::Colours::black : textC);
        juce::Path p;
        if (playing) { p.addRectangle (pr.reduced (8)); }
        else p.addTriangle (pr.getX() + 10, pr.getY() + 7, pr.getX() + 10, pr.getBottom() - 7, pr.getRight() - 8, pr.getCentreY());
        g.fillPath (p);
        g.setColour (textC);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (it.when, h + 4, 0, 120, h, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (it.info, h + 128, 0, w - h - 128 - h - 90, h, juce::Justification::centredLeft);
        g.setColour (orange.withAlpha (0.8f));
        g.drawText (jp ("⇩ ドラッグ"), w - h - 90, 0, 84, h, juce::Justification::centredRight);
        auto tr = trashRect (w, h).toFloat();
        g.setColour (line);
        g.fillRoundedRectangle (tr, 5);
        g.setColour (dim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (jp ("×"), tr, juce::Justification::centred);
        if (playing)
        {
            g.setColour (orange.withAlpha (0.35f));
            g.fillRect (0, h - 3, (int) (w * owner.preview.progress()), 3);
        }
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) items.size()) return;
        auto& it = items[(size_t) row];
        const int h = list.getRowHeight(), w = list.getVisibleRowWidth();
        if (playRect (h).contains (e.x, e.y) || ! trashRect (w, h).contains (e.x, e.y))
        {
            if (owner.preview.isPlaying() && owner.preview.currentId() == it.file.getFullPathName()) owner.preview.stop();
            else owner.preview.play (it.notes, it.drums, it.file.getFullPathName());
        }
        else
        {
            owner.preview.stop();
            it.file.moveToTrash();   // ゴミ箱から戻せる
            reload();
        }
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        // JUCE の中のドラッグではなく、Finder と同じファイルのドラッグにする（Logic に落とせる）
        if (nowMs() - lastDragMs < 800) return {};
        lastDragMs = nowMs();
        if (rows.size() > 0)
        {
            const int r = rows[0];
            if (r >= 0 && r < (int) items.size())
            {
                const auto path = items[(size_t) r].file.getFullPathName();
                juce::MessageManager::callAsync ([path, this]
                {
                    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ path }, false, this);
                });
            }
        }
        return {};
    }
};

//==============================================================================
MainComponent::MainComponent (bool live) : snapshotMode (! live)
{
    auto& lf = getLookAndFeel();
    lf.setColour (juce::ComboBox::backgroundColourId, panel2);
    lf.setColour (juce::ComboBox::outlineColourId, line);
    lf.setColour (juce::ComboBox::textColourId, textC);
    lf.setColour (juce::ComboBox::arrowColourId, dim);
    lf.setColour (juce::PopupMenu::backgroundColourId, panel);
    lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, orange.withAlpha (0.35f));
    lf.setColour (juce::TextButton::buttonColourId, panel2);
    lf.setColour (juce::TextButton::buttonOnColourId, orange);
    lf.setColour (juce::TextButton::textColourOffId, textC);
    lf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lf.setColour (juce::ComboBox::focusedOutlineColourId, orange);
    lf.setColour (juce::ResizableWindow::backgroundColourId, bg);
    lf.setColour (juce::ListBox::outlineColourId, line);

    hist.assign (1900, { -1.0f, -100.0f, -1 });
    voiced.reserve (7400);

    juce::PropertiesFile::Options o;
    o.applicationName = "Hum Keys";
    o.folderName = "Hum Keys";
    o.osxLibrarySubFolder = "Application Support";
    o.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (o);

    // ---- 上の行 ----
    for (auto* b : { &onButton, &melodyTab, &drumsTab, &miniButton, &pinButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    onButton.setTooltip (jp ("MIDI を出す／止める（スペースキー）"));
    onButton.onClick = [this] { engine.p.enabled = ! engine.p.enabled.load(); refreshButtons(); saveSettings(); };
    melodyTab.onClick = [this] { setMode (mini ? 1 - engine.p.mode.load() : 0); };
    drumsTab.onClick  = [this] { setMode (1); };
    miniButton.setTooltip (jp ("小さい表示にする／戻す（M キー）"));
    miniButton.onClick = [this] { setMini (! mini); };
    pinButton.setTooltip (jp ("いつも手前に出す（Logic の全画面の上にも）。ON のあいだ Dock とメニューバーからは消えます（P キー）"));
    pinButton.onClick = [this] { setPinned (! pinned); };
    addAndMakeVisible (audioButton);
    audioButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (panicButton);
    panicButton.setTooltip (jp ("鳴りっぱなしの音を全部止める（esc）"));
    panicButton.onClick = [this] { engine.panicRequest = true; };
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (autoGateButton);
    autoGateButton.setButtonText ("AUTO GATE");
    autoGateButton.setTooltip (jp ("2 秒間まわりの音を測って、GATE をちょうどいい高さにする（静かにして押す）"));
    autoGateButton.onClick = [this] { startAutoGate(); };

    // ---- MELODY ----
    for (int k = 0; k < 12; ++k) keyBox.addItem (pitchClassName (k), k + 1);
    for (int s = 0; s < hum::scNumScales; ++s) scaleBox.addItem (jp (scaleNames[s]), s + 1);
    for (int v = -2; v <= 2; ++v) octaveBox.addItem ((v > 0 ? "+" : "") + juce::String (v), v + 3);
    for (int c = 1; c <= 16; ++c) chanBox.addItem (juce::String (c), c);
    keyBox.onChange    = [this] { engine.p.key = keyBox.getSelectedId() - 1; saveSettings(); repaint(); };
    scaleBox.onChange  = [this] { engine.p.scale = scaleBox.getSelectedId() - 1; saveSettings(); repaint(); };
    octaveBox.onChange = [this] { engine.p.octave = octaveBox.getSelectedId() - 3; saveSettings(); };
    chanBox.onChange   = [this] { engine.panicRequest = true; engine.p.channel = chanBox.getSelectedId(); saveSettings(); };
    autoKeyButton.setTooltip (jp ("歌った音から KEY・SCALE（メジャー／マイナー）・OCTAVE を自動で決める（声のある所 約 6 秒ぶん）"));
    autoKeyButton.onClick = [this] { autoKey(); };

    styleSlider (glueSlider, 10, 150, 1, " ms");
    styleSlider (holdSlider, 20, 300, 1, " ms");
    glueSlider.setTooltip (jp ("同じ高さがこの長さ続いたら音を変える。短い＝速い／長い＝しゃくりや揺れに強い"));
    holdSlider.setTooltip (jp ("声がこの長さ途切れたら音を止める。短い＝細かく切れる／長い＝つながる"));
    glueSlider.onValueChange = [this] { engine.p.glueMs = (float) glueSlider.getValue(); saveSettings(); };
    holdSlider.onValueChange = [this] { engine.p.holdMs = (float) holdSlider.getValue(); saveSettings(); };

    velButton.onClick  = [this] { engine.p.dynamicVel = ! engine.p.dynamicVel.load(); refreshButtons(); saveSettings(); };
    bendButton.onClick = [this] { engine.p.bend = ! engine.p.bend.load(); engine.panicRequest = true; refreshButtons(); saveSettings(); };
    velButton.setTooltip (jp ("DYNAMIC: 声の大きさで強弱が付く／FIXED: いつも 100"));
    bendButton.setTooltip (jp ("ON にすると、音程の細かいゆれ（ビブラートなど）をピッチベンドで送る（音源のベンド幅を ±2 に）"));

    caption (keyBox, "KEY"); caption (scaleBox, "SCALE"); caption (octaveBox, "OCTAVE");
    caption (glueSlider, "GLUE"); caption (holdSlider, "HOLD"); caption (velButton, "VELOCITY");
    caption (bendButton, "BEND"); caption (chanBox, "MIDI CH");

    // ---- DRUMS ----
    styleSlider (sensSlider, 3, 20, 0.5, " dB");
    sensSlider.setTooltip (jp ("どれだけ急に大きくなったら「叩いた」とみなすか。小さいほど敏感"));
    sensSlider.onValueChange = [this] { engine.p.drumSens = (float) sensSlider.getValue(); saveSettings(); };
    caption (sensSlider, "SENS");
    for (int i = 0; i < numPads; ++i)
    {
        auto& nb = drumNoteBox[(size_t) i];
        for (int n = 24; n <= 84; ++n) nb.addItem (noteName (n) + "  (" + juce::String (n) + ")", n);
        nb.onChange = [this, i] { engine.p.drumNote[i] = drumNoteBox[(size_t) i].getSelectedId(); saveSettings(); };
        nb.setTooltip (jp ("鳴らすノート。Logic の Drum Kit Designer: KICK=C1(36) SNARE=D1(38) HAT=F#1(42) OPEN HAT=A#1(46) TOM=A1(45) CLAP=D#1(39)"));
        auto& lb = learnButton[(size_t) i];
        lb.setTooltip (jp ("押してから、この音を 4 回口で鳴らすと、あなたの声の癖を覚えます"));
        lb.onClick = [this, i]
        {
            engine.learnCount = 0;
            engine.learnClass = engine.learnClass.load() == i ? -1 : i;
            refreshButtons();
        };
        auto& rb = resetButton[(size_t) i];
        rb.setButtonText ("RESET");
        rb.setTooltip (i < 3 ? jp ("覚えた癖を消して、最初の聞き分けに戻す") : jp ("覚えた癖を消す（この音は使わなくなる）"));
        rb.onClick = [this, i] { engine.p.resetProto (i); saveSettings(); repaint(); };
        addChildComponent (nb); addChildComponent (lb); addChildComponent (rb);
    }

    // ---- テイク ----
    styleSlider (tempoSlider, 40, 240, 0.1, " BPM");
    tempoSlider.setTooltip (jp ("曲のテンポ。Logic から MIDI クロックが来ていれば自動で合います"));
    tempoSlider.onValueChange = [this] { saveSettings(); };
    addAndMakeVisible (tempoSlider);
    caption (tempoSlider, "TEMPO");
    folderButton.setButtonText (jp ("フォルダ"));
    folderButton.setTooltip (jp ("保存したテイク（~/Music/Hum Keys）を Finder で開く"));
    folderButton.onClick = [] { auto d = takeFolder(); d.createDirectory(); d.startAsProcess(); };
    historyButton.setButtonText (jp ("履歴"));
    historyButton.setTooltip (jp ("これまでのテイクを一覧で見る・試し聴きする（H キー）"));
    historyButton.onClick = [this] { if (history != nullptr) closeHistory(); else openHistory(); };
    newTakeButton.setButtonText ("NEW");
    newTakeButton.setTooltip (jp ("今のテイクを保存して、新しいテイクを始める（N キー）"));
    newTakeButton.onClick = [this] { newTake(); };
    addAndMakeVisible (folderButton);
    addAndMakeVisible (historyButton);
    addAndMakeVisible (newTakeButton);

    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &autoKeyButton, &scaleBox, &octaveBox, &glueSlider, &holdSlider,
                                                             &velButton, &bendButton, &chanBox, &sensSlider })
        addChildComponent (c);

    loadSettings();

    setWantsKeyboardFocus (true);
    setSize (props->getIntValue ("fullW", 860), props->getIntValue ("fullH", 620));
    refreshButtons();
    if (! live) return;   // 画面の書き出し（確認用）のときは音も MIDI も使わない

    // ---- 仮想 MIDI（出口：Logic へノート ／ 入口：Logic から MIDI クロック） ----
    sink.port = juce::MidiOutput::createNewDevice ("Hum Keys");
    clockIn = juce::MidiInput::createNewDevice ("Hum Keys", &sync);
    if (clockIn != nullptr) clockIn->start();
    midiStatus = sink.port != nullptr ? jp ("Logic の MIDI 入力に「Hum Keys」が出ています")
                                      : jp ("仮想 MIDI を作れませんでした（アプリを再起動してください）");

    remote = std::make_unique<RemoteCommands> ([this] (const std::string& c) { handleCommand (juce::String (c)); });

    // ---- マイク ----
    juce::Component::SafePointer<MainComponent> safe (this);
    macsupport::requestMicPermission ([safe] (bool ok)
    {
        if (safe == nullptr) return;
        if (! ok)
        {
            safe->midiStatus = jp ("マイクの許可がありません：システム設定 → プライバシーとセキュリティ → マイク で Hum Keys をオン");
            safe->refreshButtons();
            return;
        }
        safe->startAudio();
    });

    startTimerHz (60);
}

MainComponent::~MainComponent()
{
    stopTimer();
    history = nullptr;
    preview.stop();
    if (! snapshotMode)
    {
        pullCapture();
        finishTake();
        saveSettings();
    }
    if (clockIn != nullptr) clockIn->stop();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    hum::MidiSink& s = sink;
    engine.allOff (s);
}

void MainComponent::startAudio()
{
    auto xml = props->getXmlValue ("audio");
    const auto err = devices.initialise (2, 0, xml.get(), true);
    if (xml == nullptr)
    {
        // 遅れを小さく：一番小さめのバッファ（128 前後）にする
        auto setup = devices.getAudioDeviceSetup();
        setup.bufferSize = 128;
        devices.setAudioDeviceSetup (setup, true);
    }
    devices.addAudioCallback (this);
    if (err.isNotEmpty()) midiStatus = err;
    refreshButtons();
}

//==============================================================================
void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    mono.assign ((size_t) juce::jmax (4096, d->getCurrentBufferSizeSamples() * 2), 0.0f);
    inputLatency = d->getInputLatencyInSamples();
    engine.prepare (d->getCurrentSampleRate());
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                      int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    if ((size_t) n > mono.size()) return;

    // このブロックの最初のサンプルが「いつ」マイクに入ったか（Logic の拍と合わせるため）
    const double fs = engine.sampleRate();
    mapClock = engine.clockNow.load();
    mapHostMs = nowMs() - (double) (n + inputLatency) / fs * 1000.0;

    int used = 0;
    juce::FloatVectorOperations::clear (mono.data(), n);
    for (int c = 0; c < numIn; ++c)
        if (in[c] != nullptr) { juce::FloatVectorOperations::add (mono.data(), in[c], n); ++used; }
    if (used > 1) juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) used, n);

    const auto r = juce::FloatVectorOperations::findMinAndMax (mono.data(), n);
    const float pk = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
    if (pk > inPeak.load()) inPeak = pk;

    engine.process (mono.data(), n, sink);
}

double MainComponent::hostMsOf (int64_t sample) const
{
    return mapHostMs.load() + (double) (sample - mapClock.load()) / engine.sampleRate() * 1000.0;
}

//==============================================================================
void MainComponent::flash (const juce::String& s, double secs)
{
    flashMsg = s;
    flashUntil = nowMs() + secs * 1000.0;
}

void MainComponent::timerCallback()
{
    hum::Frame f;
    int got = 0;
    const bool gateCollect = autoGateUntil > 0;
    while (engine.frames.pop (f))
    {
        hist[(size_t) histPos] = { f.pitch, f.level, f.note };
        histPos = (histPos + 1) % (int) hist.size();
        meterDb = juce::jmax (meterDb - 0.6f, f.level);
        if (gateCollect) autoGateLevels.push_back (f.level);
        if (f.pitch > 0)
        {
            if (voiced.size() < 7400) voiced.push_back (f.pitch);
            else { voiced[voicedPos] = f.pitch; voicedPos = (voicedPos + 1) % voiced.size(); }
        }
        ++got;
    }
    if (got == 0) meterDb = juce::jmax (minDb, meterDb - 1.5f);

    const float pk = juce::Decibels::gainToDecibels (inPeak.exchange (0.0f), -100.0f);
    peakHoldDb = pk > peakHoldDb ? pk : peakHoldDb - 0.5f;

    hum::Hit h;
    while (engine.hits.pop (h))
        if (h.cls >= 0 && h.cls < numPads) { padFlash[(size_t) h.cls] = 1.0f; padVel[(size_t) h.cls] = h.velocity; lastHitCls = h.cls; }
    for (auto& p : padFlash) p *= 0.88f;

    if (gateCollect && nowMs() > autoGateUntil) finishAutoGate();
    if (autoKeyListening)
    {
        if (voiced.size() >= voicedNeed) applyAutoKey();
        else flash (jp ("キーを聞いています… 歌ってください  ") + juce::String ((int) (100 * voiced.size() / voicedNeed)) + "%", 1.0);
    }

    pullCapture();

    // Logic のテンポ
    const double bpm = sync.bpm();
    if (bpm > 0 && std::abs (bpm - tempoSlider.getValue()) > 0.05)
        tempoSlider.setValue (std::round (bpm * 10.0) / 10.0, juce::dontSendNotification);

    // 表示の中心を、いま鳴っている／歌っている高さへゆっくり寄せる
    const int note = engine.currentNote.load();
    if (note >= 0) viewCenter += 0.06f * ((float) note - viewCenter);

    static int lastLearn = -2, lastCount = -1;
    if (engine.learnClass.load() != lastLearn || engine.learnCount.load() != lastCount)
    {
        if (lastLearn >= 0 && engine.learnClass.load() < 0) saveSettings();   // 覚え終わった
        lastLearn = engine.learnClass.load(); lastCount = engine.learnCount.load();
        refreshButtons();
    }

    if (! preview.isPlaying() && preview.currentId().isNotEmpty()) preview.stop();

    // 下の 1 行（知らせ ＞ 同期の様子 ＞ MIDI の様子）
    juce::String st = midiStatus;
    if (bpm > 0)
        st = jp ("Logic と同期中  ♩=") + juce::String (bpm, 1) + (sync.isRunning() ? jp ("  ● 再生中") : jp ("  停止中"));
    if (nowMs() < flashUntil) st = flashMsg;
    if (statusLabel.getText() != st) statusLabel.setText (st, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, nowMs() < flashUntil ? orange : (bpm > 0 ? green : dim));

    repaint();
}

//==============================================================================
void MainComponent::startAutoGate()
{
    autoGateLevels.clear();
    autoGateUntil = nowMs() + 2000.0;
    flash (jp ("2 秒間、静かにしてください… まわりの音を測っています"), 2.2);
}

void MainComponent::finishAutoGate()
{
    autoGateUntil = 0;
    if (autoGateLevels.size() < 50) { flash (jp ("マイクの音が来ていません（入力を確かめてください）")); return; }
    std::sort (autoGateLevels.begin(), autoGateLevels.end());
    const float noise = autoGateLevels[autoGateLevels.size() * 9 / 10];   // 雑音の大きい方から 1 割の所
    const float g = juce::jlimit (-70.0f, -20.0f, std::round (noise + 10.0f));
    engine.p.gateDb = g;
    saveSettings();
    flash (noise > -32.0f ? jp ("まわりの音が大きめです。GATE を ") + juce::String ((int) g) + jp (" dB にしました（マイクに近づいて歌うと確実です）")
                          : jp ("GATE を ") + juce::String ((int) g) + jp (" dB にしました（雑音 ") + juce::String ((int) noise) + jp (" dB）"));
}

void MainComponent::autoKey()
{
    if (voiced.size() >= voicedNeed) { applyAutoKey(); return; }
    autoKeyListening = true;
    flash (jp ("キーを聞いています… 歌ってください"), 1.0);
}

void MainComponent::applyAutoKey()
{
    autoKeyListening = false;
    const auto g = hum::guessKey (voiced);
    if (! g.ok) return;
    int sc = engine.p.scale.load();
    if (sc == hum::scMajorPenta || sc == hum::scMinorPenta) sc = g.minor ? hum::scMinorPenta : hum::scMajorPenta;
    else sc = g.minor ? hum::scMinor : hum::scMajor;
    engine.p.key = g.key; engine.p.scale = sc; engine.p.octave = g.octave;
    keyBox.setSelectedId (g.key + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (sc + 1, juce::dontSendNotification);
    octaveBox.setSelectedId (g.octave + 3, juce::dontSendNotification);
    saveSettings();
    flash (jp ("キー ") + pitchClassName (g.key) + (g.minor ? jp (" マイナー") : jp (" メジャー"))
           + jp ("・OCTAVE ") + (g.octave > 0 ? "+" : "") + juce::String (g.octave)
           + jp (" にしました（歌った真ん中 ") + noteName ((int) std::lround (g.median)) + jp ("）")
           + (g.confidence < 0.6f ? jp ("  ※自信低め。もう少し歌ってから押すと確かになります") : juce::String()), 8.0);
    repaint();
}

//==============================================================================
void MainComponent::setMini (bool m)
{
    auto* w = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent());
    if (m == mini && ! (m && w != nullptr && w->getWidth() > 600)) return;
    closeHistory();
    if (m)
    {
        if (! mini && w != nullptr) fullBounds = w->getBounds();
        mini = true;
        if (w != nullptr)
        {
            w->setResizeLimits (340, 150, 900, 400);
            const int x = props->getIntValue ("miniX", w->getX()), y = props->getIntValue ("miniY", w->getY());
            w->setBounds (x, y, props->getIntValue ("miniW", 430), props->getIntValue ("miniH", 176));
        }
    }
    else
    {
        if (w != nullptr && mini)
        {
            props->setValue ("miniX", w->getX()); props->setValue ("miniY", w->getY());
            props->setValue ("miniW", w->getWidth()); props->setValue ("miniH", w->getHeight());
        }
        mini = false;
        if (w != nullptr)
        {
            w->setResizeLimits (620, 470, 1800, 1300);
            if (fullBounds.getWidth() >= 620) w->setBounds (fullBounds);
            else w->setSize (860, 620);
        }
    }
    props->setValue ("mini", mini);
    props->saveIfNeeded();
    refreshButtons();
}

void MainComponent::setPinned (bool p)
{
    pinned = p;
    macsupport::setFloatOnTop (p);
    if (auto* w = getTopLevelComponent()) w->setAlwaysOnTop (p);
    props->setValue ("pin", pinned);
    props->saveIfNeeded();
    refreshButtons();
    flash (p ? jp ("いつも手前に出します（Logic の全画面の上にも）。もう一度 PIN で戻ります") : jp ("いつも手前をやめました"), 4.0);
}

void MainComponent::applySavedViewState()
{
    const auto fb = props->getValue ("fullBounds");
    if (fb.isNotEmpty()) fullBounds = juce::Rectangle<int>::fromString (fb);
    if (props->getBoolValue ("mini", false)) { mini = false; setMini (true); }
    if (props->getBoolValue ("pin", false)) setPinned (true);
}

//==============================================================================
void MainComponent::handleCommand (const juce::String& c)
{
    if      (c == "toggle")   onButton.triggerClick();
    else if (c == "on")       { engine.p.enabled = true;  refreshButtons(); saveSettings(); }
    else if (c == "off")      { engine.p.enabled = false; refreshButtons(); saveSettings(); }
    else if (c == "melody")   setMode (0);
    else if (c == "drums")    setMode (1);
    else if (c == "mode")     setMode (1 - engine.p.mode.load());
    else if (c == "new")      newTake();
    else if (c == "panic")    engine.panicRequest = true;
    else if (c == "autoKey")  autoKey();
    else if (c == "autoGate") startAutoGate();
    else if (c == "octUp" || c == "octDown")
    {
        const int o = juce::jlimit (-2, 2, engine.p.octave.load() + (c == "octUp" ? 1 : -1));
        engine.p.octave = o;
        octaveBox.setSelectedId (o + 3, juce::dontSendNotification);
        saveSettings();
        flash ("OCTAVE " + juce::String (o > 0 ? "+" : "") + juce::String (o), 2.0);
    }
    else if (c == "mini")     setMini (! mini);
    else if (c == "pin")      setPinned (! pinned);
    else if (c == "preview")  previewTake();
    else if (c == "history")  { if (history != nullptr) closeHistory(); else openHistory(); }
    else if (c == "show")     { if (auto* w = getTopLevelComponent()) { w->setVisible (true); w->toFront (true); } }
    repaint();
}

//==============================================================================
void MainComponent::refreshButtons()
{
    const bool on = engine.p.enabled.load();
    onButton.setButtonText (on ? "ON" : "OFF");
    onButton.setToggleState (on, juce::dontSendNotification);
    const int mode = engine.p.mode.load();
    melodyTab.setButtonText (mini ? (mode == 0 ? "MELODY" : "DRUMS") : "MELODY");
    melodyTab.setToggleState (mini || mode == 0, juce::dontSendNotification);
    drumsTab.setToggleState (mode == 1, juce::dontSendNotification);
    miniButton.setButtonText (mini ? "FULL" : "MINI");
    pinButton.setToggleState (pinned, juce::dontSendNotification);

    velButton.setButtonText (engine.p.dynamicVel.load() ? "DYNAMIC" : "FIXED");
    bendButton.setButtonText (engine.p.bend.load() ? "ON" : "OFF");
    bendButton.setToggleState (engine.p.bend.load(), juce::dontSendNotification);

    const bool full = ! mini;
    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &autoKeyButton, &scaleBox, &octaveBox, &glueSlider, &holdSlider, &bendButton })
        c->setVisible (full && mode == 0);
    sensSlider.setVisible (full && mode == 1);
    for (auto* c : std::initializer_list<juce::Component*> { &velButton, &chanBox, &audioButton, &panicButton, &statusLabel, &autoGateButton,
                                                             &tempoSlider, &folderButton, &historyButton, &newTakeButton, &drumsTab })
        c->setVisible (full);

    const int lc = engine.learnClass.load();
    for (int i = 0; i < numPads; ++i)
    {
        const bool show = full && mode == 1 && history == nullptr;
        drumNoteBox[(size_t) i].setVisible (show);
        learnButton[(size_t) i].setVisible (show);
        resetButton[(size_t) i].setVisible (show);
        learnButton[(size_t) i].setButtonText (lc == i ? jp ("あと ") + juce::String (hum::Engine::learnShots - engine.learnCount.load()) + jp (" 回")
                                                       : juce::String ("LEARN"));
        learnButton[(size_t) i].setToggleState (lc == i, juce::dontSendNotification);
    }

    if (auto* d = devices.getCurrentAudioDevice())
        audioButton.setButtonText (jp ("入力: ") + d->getName());
    else
        audioButton.setButtonText (jp ("入力を選ぶ"));
    if (statusLabel.getText().isEmpty()) statusLabel.setText (midiStatus, juce::dontSendNotification);
    resized();
    repaint();
}

void MainComponent::setMode (int m)
{
    engine.learnClass = -1;
    engine.p.mode = m;
    refreshButtons();
    saveSettings();
}

void MainComponent::openAudioSettings()
{
    auto* sel = new juce::AudioDeviceSelectorComponent (devices, 1, 2, 0, 0, false, false, false, false);
    sel->setSize (520, 360);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = jp ("マイク（入力）を選ぶ");
    o.dialogBackgroundColour = panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
    juce::Timer::callAfterDelay (500, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr) { safe->refreshButtons(); safe->saveSettings(); }
    });
}

//==============================================================================
void MainComponent::loadSettings()
{
    auto& p = engine.p;
    p.enabled    = props->getBoolValue ("enabled", true);
    p.mode       = props->getIntValue ("mode", 0);
    p.key        = props->getIntValue ("key", 0);
    p.scale      = props->getIntValue ("scale", hum::scMajor);
    p.octave     = props->getIntValue ("octave", 0);
    p.gateDb     = (float) props->getDoubleValue ("gate", -45.0);
    p.glueMs     = (float) props->getDoubleValue ("glue", 35.0);
    p.holdMs     = (float) props->getDoubleValue ("hold", 70.0);
    p.dynamicVel = props->getBoolValue ("dynamicVel", true);
    p.bend       = props->getBoolValue ("bend", false);
    p.channel    = props->getIntValue ("channel", 1);
    p.drumSens   = (float) props->getDoubleValue ("sens", 9.0);
    for (int i = 0; i < numPads; ++i)
    {
        const auto c = juce::String (i);
        p.drumNote[i] = props->getIntValue ("drumNote" + c, p.drumNote[i].load());
        // 1.2.0 から聞き分けの特徴が 6 つの帯域に（前の版の "proto" は使わない）
        if (props->containsKey ("dhas" + c))
        {
            p.has[i] = props->getBoolValue ("dhas" + c);
            p.protoSus[i] = (float) props->getDoubleValue ("dsus" + c, p.protoSus[i].load());
            for (int k = 0; k < hum::Params::numFeat; ++k)
                p.proto[i][k] = (float) props->getDoubleValue ("dproto" + c + "_" + juce::String (k), p.proto[i][k].load());
        }
    }

    keyBox.setSelectedId (p.key.load() + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (p.scale.load() + 1, juce::dontSendNotification);
    octaveBox.setSelectedId (p.octave.load() + 3, juce::dontSendNotification);
    chanBox.setSelectedId (p.channel.load(), juce::dontSendNotification);
    glueSlider.setValue (p.glueMs.load(), juce::dontSendNotification);
    holdSlider.setValue (p.holdMs.load(), juce::dontSendNotification);
    sensSlider.setValue (p.drumSens.load(), juce::dontSendNotification);
    tempoSlider.setValue (props->getDoubleValue ("tempo", 120.0), juce::dontSendNotification);
    for (int i = 0; i < numPads; ++i) drumNoteBox[(size_t) i].setSelectedId (p.drumNote[i].load(), juce::dontSendNotification);
}

void MainComponent::saveSettings()
{
    if (props == nullptr || snapshotMode) return;
    auto& p = engine.p;
    props->setValue ("enabled", p.enabled.load());
    props->setValue ("mode", p.mode.load());
    props->setValue ("key", p.key.load());
    props->setValue ("scale", p.scale.load());
    props->setValue ("octave", p.octave.load());
    props->setValue ("gate", p.gateDb.load());
    props->setValue ("glue", p.glueMs.load());
    props->setValue ("hold", p.holdMs.load());
    props->setValue ("dynamicVel", p.dynamicVel.load());
    props->setValue ("bend", p.bend.load());
    props->setValue ("channel", p.channel.load());
    props->setValue ("sens", p.drumSens.load());
    props->setValue ("tempo", tempoSlider.getValue());
    for (int i = 0; i < numPads; ++i)
    {
        const auto c = juce::String (i);
        props->setValue ("drumNote" + c, p.drumNote[i].load());
        props->setValue ("dhas" + c, p.has[i].load());
        props->setValue ("dsus" + c, p.protoSus[i].load());
        for (int k = 0; k < hum::Params::numFeat; ++k)
            props->setValue ("dproto" + c + "_" + juce::String (k), p.proto[i][k].load());
    }
    if (devices.getCurrentAudioDevice() != nullptr)
        if (auto xml = devices.createStateXml())
            props->setValue ("audio", xml.get());
    props->saveIfNeeded();
}

juce::String MainComponent::savedWindowState() const { return props->getValue ("window"); }
void MainComponent::saveWindowState (const juce::String& s)
{
    if (snapshotMode) return;
    if (mini)
    {
        // MINI のまま終わったときは、大きい画面の位置も覚えておく
        if (auto* w = getTopLevelComponent())
        {
            props->setValue ("miniX", w->getX()); props->setValue ("miniY", w->getY());
            props->setValue ("miniW", w->getWidth()); props->setValue ("miniH", w->getHeight());
        }
        if (fullBounds.getWidth() >= 620) props->setValue ("window", juce::String());
    }
    else props->setValue ("window", s);
    if (fullBounds.getWidth() >= 620 || ! mini)
        props->setValue ("fullBounds", (mini ? fullBounds : getTopLevelComponent()->getBounds()).toString());
    props->saveIfNeeded();
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto ch = k.getTextCharacter();
    if (k == juce::KeyPress::spaceKey) { onButton.triggerClick(); return true; }
    if (k == juce::KeyPress::escapeKey) { if (history != nullptr) closeHistory(); else engine.panicRequest = true; return true; }
    if (ch == '1') { setMode (0); return true; }
    if (ch == '2') { setMode (1); return true; }
    if (ch == 'm' || ch == 'M') { setMini (! mini); return true; }
    if (ch == 'p' || ch == 'P') { setPinned (! pinned); return true; }
    if (ch == 'n' || ch == 'N') { newTake(); return true; }
    if (ch == 'h' || ch == 'H') { if (history != nullptr) closeHistory(); else openHistory(); return true; }
    if (k == juce::KeyPress::returnKey) { previewTake(); return true; }
    return false;
}

// メーターの上をドラッグ → GATE（これより小さい音は無視）／ テイクをつかんで Logic へ
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (playArea.contains (e.getPosition())) { previewTake(); return; }
    mouseDrag (e);
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto p0 = e.getMouseDownPosition();
    if (dragArea.contains (p0) || takeArea.contains (p0))
    {
        if (! dragStarted && e.getDistanceFromDragStart() >= 4) { dragStarted = true; startDragOut(); }
        return;
    }
    if (! meterArea.expanded (8, 0).contains (p0)) return;
    const float db = juce::jmap ((float) e.y, (float) meterArea.getBottom(), (float) meterArea.getY(), minDb, 0.0f);
    engine.p.gateDb = juce::jlimit (-75.0f, -10.0f, std::round (db));
    saveSettings();
    repaint();
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (meterArea.expanded (8, 0).contains (e.getPosition())) { engine.p.gateDb = -45.0f; saveSettings(); }
}

//==============================================================================
void MainComponent::resized()
{
    if (mini)
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (28);
        onButton.setBounds (top.removeFromLeft (54));
        top.removeFromLeft (6);
        melodyTab.setBounds (top.removeFromLeft (84));
        miniButton.setBounds (top.removeFromRight (54));
        top.removeFromRight (6);
        pinButton.setBounds (top.removeFromRight (46));
        r.removeFromTop (8);
        displayArea = r;
        meterArea = r.removeFromLeft (16);
        r.removeFromLeft (8);
        dragArea = r.removeFromRight (juce::jmin (150, r.getWidth() / 3));
        r.removeFromRight (8);
        playArea = dragArea.removeFromBottom (26);
        dragArea.removeFromBottom (4);
        bigArea = r;
        takeArea = {};
        rollArea = {};
        return;
    }

    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (34);
    onButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (14);
    melodyTab.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (4);
    drumsTab.setBounds (top.removeFromLeft (96));
    panicButton.setBounds (top.removeFromRight (66));
    top.removeFromRight (8);
    miniButton.setBounds (top.removeFromRight (58));
    top.removeFromRight (6);
    pinButton.setBounds (top.removeFromRight (50));
    top.removeFromRight (8);
    audioButton.setBounds (top.removeFromRight (juce::jmax (80, juce::jmin (230, top.getWidth() - 20))));

    auto st = r.removeFromTop (30);
    autoGateButton.setBounds (st.removeFromRight (104).reduced (0, 3));
    statusLabel.setBounds (st);

    auto bottom = r.removeFromBottom (58);
    r.removeFromBottom (8);
    {
        auto strip = r.removeFromBottom (46);
        r.removeFromBottom (8);
        newTakeButton.setBounds (strip.removeFromRight (52).reduced (0, 7));
        strip.removeFromRight (5);
        folderButton.setBounds (strip.removeFromRight (70).reduced (0, 7));
        strip.removeFromRight (5);
        historyButton.setBounds (strip.removeFromRight (56).reduced (0, 7));
        strip.removeFromRight (8);
        tempoSlider.setBounds (strip.removeFromRight (112).withTrimmedTop (17).withHeight (26));
        strip.removeFromRight (8);
        dragArea = strip.removeFromRight (148);
        strip.removeFromRight (6);
        playArea = strip.removeFromRight (46);
        strip.removeFromRight (6);
        takeArea = strip;
    }
    displayArea = r;
    meterArea = r.removeFromLeft (26).reduced (0, 6);
    r.removeFromLeft (10);
    rollArea = r;
    bigArea = {};
    if (history != nullptr) history->setBounds (displayArea);

    // 下のつまみ類（上 18px に名前）
    bottom.removeFromTop (18);
    auto place = [&bottom] (juce::Component& c, int w, int gap = 8) { c.setBounds (bottom.removeFromLeft (w).withHeight (30)); bottom.removeFromLeft (gap); };
    if (engine.p.mode.load() == 0)
    {
        const int w = bottom.getWidth();
        const int unit = (w - 8 * 8 - 50) / 10;
        place (keyBox, unit - 6, 4); place (autoKeyButton, 50); place (scaleBox, unit * 2 + 6); place (octaveBox, unit - 10);
        place (glueSlider, unit + 10); place (holdSlider, unit + 10);
        place (velButton, unit + 5); place (bendButton, unit - 15);
        place (chanBox, bottom.getWidth());
    }
    else
    {
        place (sensSlider, 180); place (velButton, 110); place (chanBox, 80);
    }

    // ドラムのパッド（2 段 × 3）
    const int pw = (rollArea.getWidth() - 20) / 3, ph = (rollArea.getHeight() - 10) / 2;
    for (int i = 0; i < numPads; ++i)
    {
        const int col = i % 3, row = i / 3;
        auto pr = juce::Rectangle<int> (rollArea.getX() + col * (pw + 10), rollArea.getY() + row * (ph + 10), pw, ph);
        padAreas[(size_t) i] = pr;
        auto strip = pr.reduced (10, 8).removeFromBottom (28);
        resetButton[(size_t) i].setBounds (strip.removeFromRight (56));
        strip.removeFromRight (4);
        learnButton[(size_t) i].setBounds (strip.removeFromRight (juce::jmin (78, strip.getWidth() / 2)));
        strip.removeFromRight (4);
        drumNoteBox[(size_t) i].setBounds (strip);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    const int mode = engine.p.mode.load();
    const bool on = engine.p.enabled.load();

    // 名前
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (auto& c : captions)
        if (c.c->isVisible())
            g.drawText (c.c == &tempoSlider && sync.bpm() > 0 ? juce::String ("TEMPO (Logic)") : c.text,
                        c.c->getX(), c.c->getY() - 17, c.c->getWidth() + 40, 14, juce::Justification::left);

    // ---- テイク（録り逃し防止） ----
    const bool can = ! takeNotes.empty();
    if (! mini)
    {
        auto ta = takeArea.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (ta, 6);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("TAKE", ta.reduced (8, 3).removeFromTop (12), juce::Justification::left);
        auto roll = ta.reduced (8, 4).withTrimmedTop (12).withTrimmedRight (112);
        if (! can)
        {
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (jp ("歌うと、ここに自動でたまります（録音ボタン不要）"), roll, juce::Justification::centredLeft);
        }
        else
        {
            double t0 = takeNotes.front().s, t1 = t0 + 2.0;
            int lo = 127, hi = 0;
            for (auto& n : takeNotes) { t1 = std::max (t1, n.e); lo = std::min (lo, n.n); hi = std::max (hi, n.n); }
            lo -= 1; hi += 1;
            for (auto& n : takeNotes)
            {
                const float x0 = roll.getX() + (float) ((n.s - t0) / (t1 - t0)) * roll.getWidth();
                const float x1 = roll.getX() + (float) ((n.e - t0) / (t1 - t0)) * roll.getWidth();
                const float y = roll.getBottom() - (float) (n.n - lo + 1) / (float) (hi - lo + 1) * roll.getHeight();
                g.setColour (orange.withAlpha (0.9f));
                g.fillRect (x0, y, std::max (1.5f, x1 - x0 - 0.5f), std::max (2.0f, roll.getHeight() / (float) (hi - lo + 1) - 0.5f));
            }
            g.setColour (textC);
            g.setFont (juce::FontOptions (12.0f));
            juce::String info = juce::String ((int) takeNotes.size()) + jp (" 音 · ") + juce::String (t1 - t0, 1) + jp (" 秒");
            if (takeSynced()) info += "\n" + juce::String (takeBar()) + jp (" 小節目から");
            else if (takeSaved) info += jp ("\n保存済み");
            g.drawFittedText (info, ta.reduced (8, 2).removeFromRight (106).toNearestInt(), juce::Justification::centredRight, 2);
        }
    }

    // 試し聴き ▶ / ■
    {
        auto pa = playArea.toFloat();
        const bool playing = preview.isPlaying() && preview.currentId() == "take";
        g.setColour (playing ? orange : (can ? panel2 : panel));
        g.fillRoundedRectangle (pa, 6);
        g.setColour (playing ? juce::Colours::black : (can ? textC : dim));
        juce::Path p;
        auto c = pa.getCentre();
        if (playing) p.addRectangle (c.x - 6, c.y - 6, 12, 12);
        else p.addTriangle (c.x - 5, c.y - 8, c.x - 5, c.y + 8, c.x + 8, c.y);
        g.fillPath (p);
    }

    // Logic へドラッグ
    {
        auto da = dragArea.toFloat();
        g.setColour (can ? orange.withAlpha (0.18f) : panel);
        g.fillRoundedRectangle (da, 6);
        g.setColour (can ? orange : line);
        g.drawRoundedRectangle (da.reduced (1), 6, can ? 1.6f : 1.0f);
        g.setColour (can ? textC : dim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        juce::String label = jp ("⇩ Logic へドラッグ");
        if (mini && can) label += "\n" + juce::String ((int) takeNotes.size()) + jp (" 音") + (takeSynced() ? jp (" · ") + juce::String (takeBar()) + jp (" 小節") : juce::String());
        g.drawFittedText (label, da.toNearestInt().reduced (4), juce::Justification::centred, 2);
    }

    // ---- 入力メーター＋GATE ----
    g.setColour (panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 4);
    const float gate = engine.p.gateDb.load();
    {
        const float y = dbToY (meterDb, meterArea);
        auto bar = meterArea.toFloat().reduced (mini ? 3.0f : 4.0f, 0).withTop (y);
        g.setColour (meterDb > gate ? cyan : dim.withAlpha (0.6f));
        g.fillRect (bar);
        const float py = dbToY (peakHoldDb, meterArea);
        g.setColour (peakHoldDb > -1.0f ? juce::Colours::red : textC.withAlpha (0.7f));
        g.fillRect ((float) meterArea.getX() + 2, py - 1, (float) meterArea.getWidth() - 4, 2.0f);
        const float gy = dbToY (gate, meterArea);
        g.setColour (orange);
        g.fillRect ((float) meterArea.getX() - 3, gy - 1.0f, (float) meterArea.getWidth() + 6, 2.5f);
        if (! mini)
        {
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("GATE", meterArea.getX() - 6, (int) gy + 3, meterArea.getWidth() + 12, 12, juce::Justification::centred);
        }
        if (autoGateUntil > 0)
        {
            g.setColour (orange.withAlpha (0.25f));
            g.fillRoundedRectangle (meterArea.toFloat(), 4);
        }
    }

    if (mini)
    {
        // ---- 小さい表示：今の音 or 今叩いた音 ----
        auto ba = bigArea.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (ba, 8);
        if (mode == 0)
        {
            const int note = engine.currentNote.load();
            g.setColour (note >= 0 ? orange : dim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (juce::jmin (52.0f, ba.getHeight() * 0.6f), juce::Font::bold));
            g.drawText (note >= 0 ? noteName (note) : jp ("—"), ba, juce::Justification::centred);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (pitchClassName (engine.p.key.load()) + " " + jp (scaleNames[engine.p.scale.load()]).upToFirstOccurrenceOf (jp ("（"), false, false)
                          + "  OCT " + juce::String (engine.p.octave.load()),
                        ba.reduced (8, 4).removeFromBottom (14), juce::Justification::centred);
        }
        else
        {
            const int c = lastHitCls;
            const float fl = c >= 0 ? padFlash[(size_t) c] : 0.0f;
            if (c >= 0)
            {
                g.setColour (padCol[c].withAlpha (0.15f + 0.8f * fl));
                g.fillRoundedRectangle (ba.reduced (4), 6);
            }
            g.setColour (fl > 0.4f ? juce::Colours::black : textC);
            g.setFont (juce::FontOptions (juce::jmin (34.0f, ba.getHeight() * 0.4f), juce::Font::bold));
            g.drawText (c >= 0 ? juce::String (padName[c]) : jp ("—"), ba, juce::Justification::centred);
        }
        if (! on)
        {
            g.setColour (bg.withAlpha (0.65f)); g.fillRoundedRectangle (ba, 8);
            g.setColour (textC); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText ("OFF", ba, juce::Justification::centred);
        }
        return;
    }

    if (history != nullptr) return;

    if (mode == 0)
    {
        // ---- ピアノロール ----
        auto ra = rollArea;
        g.setColour (panel);
        g.fillRoundedRectangle (ra.toFloat(), 6);
        const float span = 26.0f;            // 縦に約 2 オクターブ
        const float lo = viewCenter - span * 0.5f;
        const float rowH = (float) ra.getHeight() / span;
        auto yOf = [&] (float m) { return (float) ra.getBottom() - (m - lo) * rowH; };
        const int key = engine.p.key.load(), sc = engine.p.scale.load();

        g.saveState();
        g.reduceClipRegion (ra);
        for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (lo + span) + 1; ++n)
        {
            const float y = yOf ((float) n + 0.5f);
            const bool in = hum::inScale (n, key, sc);
            if (in) { g.setColour (panel2.withAlpha (sc == hum::scChromatic ? 0.35f : 1.0f)); g.fillRect ((float) ra.getX(), y, (float) ra.getWidth(), rowH - 1); }
            if (((n - key) % 12 + 12) % 12 == 0) { g.setColour (orange.withAlpha (0.10f)); g.fillRect ((float) ra.getX(), y, (float) ra.getWidth(), rowH - 1); }
            if (in || n % 12 == 0)
            {
                g.setColour (((n - key) % 12 + 12) % 12 == 0 ? orange.withAlpha (0.8f) : dim.withAlpha (in ? 0.9f : 0.5f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (noteName (n), ra.getX() + 6, (int) y, 40, (int) rowH, juce::Justification::centredLeft);
            }
        }

        // 歌った高さ（点）と鳴らしたノート（棒）
        const int N = (int) hist.size();
        const float dx = (float) ra.getWidth() / (float) N;
        const int oct = 12 * engine.p.octave.load();
        for (int i = 0; i < N; ++i)
        {
            const auto& h = hist[(size_t) ((histPos + i) % N)];
            const float x = (float) ra.getX() + (float) i * dx;
            if (h.note >= 0)
            {
                g.setColour (orange.withAlpha (0.75f));
                g.fillRect (x, yOf ((float) h.note + 0.5f) + 1, dx + 0.6f, rowH - 2);
            }
            if (h.pitch > 0)
            {
                g.setColour (cyan);
                g.fillEllipse (x - 1.2f, yOf (h.pitch + (float) oct) - 1.2f, 2.4f, 2.4f);
            }
        }
        g.restoreState();

        // 今の音
        const int note = engine.currentNote.load();
        g.setColour (note >= 0 ? orange : dim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
        g.drawText (note >= 0 ? noteName (note) : jp ("—"), ra.removeFromTop (60).removeFromRight (140).reduced (10, 4),
                    juce::Justification::centredRight);
    }
    else
    {
        // ---- パッド ----
        const int lc = engine.learnClass.load();
        for (int i = 0; i < numPads; ++i)
        {
            auto pr = padAreas[(size_t) i].toFloat();
            const auto c = padCol[i];
            const bool usable = engine.p.has[i].load();
            const float fl = padFlash[(size_t) i];
            g.setColour (panel);
            g.fillRoundedRectangle (pr, 10);
            g.setColour (c.withAlpha ((usable ? 0.12f : 0.04f) + 0.75f * fl));
            g.fillRoundedRectangle (pr.reduced (5).withTrimmedBottom (40), 8);
            g.setColour (lc == i ? c : line);
            g.drawRoundedRectangle (pr.reduced (1), 10, lc == i ? 2.5f : 1.0f);

            auto t = pr.reduced (12, 8).withTrimmedBottom (40);
            g.setColour (fl > 0.4f ? juce::Colours::black : (usable ? textC : dim));
            g.setFont (juce::FontOptions (juce::jmin (24.0f, t.getHeight() * 0.28f), juce::Font::bold));
            g.drawText (padName[i], t.removeFromTop (juce::jmin (30.0f, t.getHeight() * 0.36f)), juce::Justification::centred);
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (fl > 0.4f ? juce::Colours::black : dim);
            g.drawText (jp (padSay[i]), t.removeFromTop (18), juce::Justification::centred);
            if (lc == i)
            {
                g.setColour (textC);
                g.drawFittedText (jp ("この音を口で 4 回鳴らしてください"), t.toNearestInt(), juce::Justification::centred, 2);
            }
            else if (! usable)
            {
                g.setColour (dim);
                g.drawFittedText (jp ("LEARN で使えるようになります"), t.toNearestInt(), juce::Justification::centred, 2);
            }
            else if (padVel[(size_t) i] > 0)
                g.drawText ("vel " + juce::String (padVel[(size_t) i]), t.removeFromBottom (18), juce::Justification::centred);
        }
    }

    if (! on)
    {
        g.setColour (bg.withAlpha (0.6f)); g.fillRoundedRectangle (rollArea.toFloat(), 6);
        g.setColour (textC); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (jp ("OFF（スペースで ON）"), rollArea, juce::Justification::centred);
    }
}

//==============================================================================
juce::File MainComponent::takeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Hum Keys");
}

// 音の処理側でたまった MIDI を受け取って、テイクにまとめる（4 秒黙るか、Logic が止まったら区切って保存）
void MainComponent::pullCapture()
{
    const double fs = engine.sampleRate();
    const int64_t gap = (int64_t) (fs * 4.0);
    hum::CapEvent c;
    bool changed = false;
    while (engine.capture.pop (c))
    {
        if (! take.empty() && (c.t - takeLastT > gap || c.t < takeLastT - (int64_t) fs))
            newTake();
        if (take.empty())
        {
            takeNotes.clear(); takeSaved = false; takeLastT = c.t;
            takeDrums = engine.p.mode.load() == 1;
        }
        take.push_back (c);
        takeBeats.push_back (sync.beatAt (hostMsOf (c.t)));
        takeLastT = std::max (takeLastT, c.t);
        takeRate = fs;
        changed = true;
    }
    if (changed) rebuildTakeNotes();

    // Logic が止まった → そこまでを 1 テイクに
    const int stops = sync.stops();
    if (stops != lastStops)
    {
        lastStops = stops;
        if (! take.empty() && takeSynced()) newTake();
    }

    if (! take.empty() && ! takeSaved && engine.currentNote.load() < 0
        && engine.clockNow.load() - takeLastT > gap)
        finishTake();
}

void MainComponent::newTake()
{
    finishTake();
    take.clear(); takeBeats.clear();
    takeSaved = false;
    repaint();
}

void MainComponent::rebuildTakeNotes()
{
    takeNotes.clear();
    std::map<int, size_t> open;   // (ch * 128 + note) → takeNotes の番号
    long lastOn = -1;
    const double fs = takeRate;
    for (size_t i = 0; i < take.size(); ++i)
    {
        const auto& c = take[i];
        const double b = i < takeBeats.size() ? takeBeats[i] : std::nan ("");
        const int st = c.st & 0xF0, ch = c.st & 0x0F, key = ch * 128 + c.d1;
        const double t = (double) c.t / fs;
        if (c.kind == 1)
        {
            // 出だしの高さを直す
            if (lastOn >= 0)
            {
                auto& n = takeNotes[(size_t) lastOn];
                open.erase (n.ch * 128 + n.n);
                n.n = c.d1;
                open[n.ch * 128 + n.n] = (size_t) lastOn;
            }
            continue;
        }
        if (st == 0x90 && c.d2 > 0)
        {
            if (auto it = open.find (key); it != open.end()) { takeNotes[it->second].e = t; takeNotes[it->second].be = b; }
            takeNotes.push_back ({ t, -1.0, c.d1, ch, c.d2, b, std::nan ("") });
            lastOn = (long) takeNotes.size() - 1;
            open[key] = (size_t) lastOn;
        }
        else if (st == 0x80 || (st == 0x90 && c.d2 == 0))
        {
            if (auto it = open.find (key); it != open.end())
            {
                auto& n = takeNotes[it->second];
                n.e = std::max (t, n.s + 0.01);
                n.be = b;
                open.erase (it);
            }
        }
    }
    const double now = (double) engine.clockNow.load() / fs;
    const double bpm = sync.bpm() > 0 ? sync.bpm() : tempoSlider.getValue();
    std::vector<TNote> keep;
    for (auto n : takeNotes)
    {
        if (n.e < 0) n.e = std::max (now, n.s + 0.05);   // まだ鳴っている
        if (std::isfinite (n.bs) && ! (std::isfinite (n.be) && n.be > n.bs)) n.be = n.bs + (n.e - n.s) * bpm / 60.0;
        if (n.e - n.s >= 0.02) keep.push_back (n);        // 20 ms 未満のかけらは捨てる
    }
    takeNotes = std::move (keep);
}

bool MainComponent::takeSynced() const
{
    if (takeNotes.empty()) return false;
    for (auto& n : takeNotes) if (! std::isfinite (n.bs)) return false;
    return true;
}

int MainComponent::takeBar() const
{
    if (! takeSynced()) return 0;
    double first = 1e18;
    for (auto& n : takeNotes) first = std::min (first, n.bs);
    return (int) std::floor (first / 4.0) + 1;   // 4/4 として
}

juce::File MainComponent::writeTakeMidi (const juce::File& dest) const
{
    if (takeNotes.empty()) return {};
    const bool synced = takeSynced();
    const double bpm = sync.bpm() > 0 ? sync.bpm() : tempoSlider.getValue();
    const int ppq = 960;
    const double t0 = takeNotes.front().s;
    const double barStart = synced ? (takeBar() - 1) * 4.0 : 0.0;
    auto tickSec = [&] (double t) { return std::max (0.0, (t - t0) * bpm / 60.0 * ppq); };
    auto tickBeat = [&] (double b) { return std::max (0.0, (b - barStart) * ppq); };

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Hum Keys"), 0);
    if (takeDrums) seq.addEvent (juce::MidiMessage::textMetaEvent (1, "Hum Keys drums"), 0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    for (auto& n : takeNotes)
    {
        seq.addEvent (juce::MidiMessage::noteOn (n.ch + 1, n.n, (juce::uint8) n.vel), synced ? tickBeat (n.bs) : tickSec (n.s));
        seq.addEvent (juce::MidiMessage::noteOff (n.ch + 1, n.n), synced ? tickBeat (n.be) : tickSec (n.e));
    }
    // ピッチベンド（BEND が ON のとき）
    for (size_t i = 0; i < take.size(); ++i)
    {
        const auto& c = take[i];
        if ((c.st & 0xF0) != 0xE0) continue;
        const double b = i < takeBeats.size() ? takeBeats[i] : std::nan ("");
        seq.addEvent (juce::MidiMessage ((int) c.st, (int) c.d1, (int) c.d2),
                      synced && std::isfinite (b) ? tickBeat (b) : tickSec ((double) c.t / takeRate));
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    juce::FileOutputStream os (dest);
    if (! os.openedOk() || ! mf.writeTo (os, 1)) return {};
    return dest;
}

void MainComponent::finishTake()
{
    if (takeSaved || take.empty()) return;
    rebuildTakeNotes();
    if (takeNotes.empty()) return;
    auto name = "Hum Keys " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    if (takeSynced()) name += " bar" + juce::String (takeBar());
    takeFile = writeTakeMidi (takeFolder().getChildFile (name + ".mid"));
    takeSaved = takeFile.existsAsFile();
    repaint();
}

void MainComponent::previewTake()
{
    if (preview.isPlaying() && preview.currentId() == "take") { preview.stop(); return; }
    if (takeNotes.empty()) return;
    std::vector<Preview::Note> ns;
    for (auto& n : takeNotes) ns.push_back ({ n.s, n.e, n.n, n.vel });
    preview.play (std::move (ns), takeDrums, "take");
}

void MainComponent::startDragOut()
{
    if (takeNotes.empty()) return;
    auto f = takeSaved && takeFile.existsAsFile()
               ? takeFile
               : writeTakeMidi (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile (takeSynced() ? "Hum Keys take bar" + juce::String (takeBar()) + ".mid" : "Hum Keys take.mid"));
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void MainComponent::openHistory()
{
    if (mini) setMini (false);
    finishTake();
    history = std::make_unique<HistoryPanel> (*this);
    addAndMakeVisible (*history);
    historyButton.setToggleState (true, juce::dontSendNotification);
    refreshButtons();
}

void MainComponent::closeHistory()
{
    if (history == nullptr) return;
    preview.stop();
    removeChildComponent (history.get());
    history = nullptr;
    historyButton.setToggleState (false, juce::dontSendNotification);
    refreshButtons();
}

//==============================================================================
void MainComponent::showSnapshot (int which)
{
    // 見本の線（ド・レ・ミ・ソ と歌った形）
    const int N = (int) hist.size();
    for (int i = 0; i < N; ++i)
    {
        const int seg = i * 5 / N, notes[] = { 60, 62, 64, 67, 64 };
        const bool gap = (i % (N / 5)) > (N / 5) - 25;
        const float p = (float) notes[seg] + 0.25f * std::sin ((float) i * 0.08f);
        hist[(size_t) i] = { gap ? -1.0f : p, -20.0f, gap ? -1 : notes[seg] };
    }
    histPos = 0; viewCenter = 63; meterDb = -22; peakHoldDb = -12;
    padFlash = { 0.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f }; padVel = { 96, 112, 70, 0, 0, 0 };
    lastHitCls = 1;
    engine.p.has[4] = true;   // TOM は覚えた、OPEN HAT と CLAP はまだ、の見本
    takeRate = 48000; take.clear(); takeBeats.clear();
    { const int nn[] = { 60, 62, 64, 67, 64, 62, 60, 62, 64, 60 }; int64_t t = 48000;
      for (int k = 0; k < 10; ++k)
      {
          take.push_back ({ t, 0x90, (uint8_t) nn[k], 100, 0 }); takeBeats.push_back (64.0 + k * 0.75);
          t += 20000;
          take.push_back ({ t, 0x80, (uint8_t) nn[k], 0, 0 }); takeBeats.push_back (64.0 + k * 0.75 + 0.6);
          t += 2000;
      } }
    rebuildTakeNotes(); takeSaved = true;
    tempoSlider.setValue (100.0, juce::dontSendNotification);
    flash (jp ("キー A マイナー・OCTAVE +1 にしました（歌った真ん中 F#1）"), 60.0);
    statusLabel.setText (flashMsg, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, orange);
    engine.p.mode = which == 1 ? 1 : 0;
    if (which == 2) { mini = true; setSize (430, 176); }
    else { mini = false; setSize (860, 620); }
    refreshButtons();
}
