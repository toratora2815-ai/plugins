#include "MainComponent.h"
#include "MacSupport.h"

namespace
{
const juce::Colour bg (0xff0e1117), panel (0xff171c26), panel2 (0xff1f2633), line (0xff2b3445),
                   textC (0xffe8ecf3), dim (0xff8a94a6), amber (0xffffc24b), red (0xffff4d5e), cyan (0xff4fd6e8);

constexpr int W = 460, H = 772;
constexpr double maxSeconds = 300.0;     // 1 回の録音は 5 分まで
constexpr double autoStopSeconds = 4.0;  // 黙ってこれだけたったら止める
constexpr float loudThreshold = 0.004f;  // 約 -48 dBFS より大きければ「鳴っている」

void styleButton (juce::TextButton& b, bool accent = false)
{
    b.setColour (juce::TextButton::buttonColourId, accent ? amber : panel2);
    b.setColour (juce::TextButton::buttonOnColourId, amber);
    b.setColour (juce::TextButton::textColourOffId, accent ? juce::Colour (0xff1a1300) : textC);
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1300));
    b.setColour (juce::ComboBox::outlineColourId, line);
}

void styleToggle (juce::ToggleButton& t)
{
    t.setColour (juce::ToggleButton::textColourId, dim);
    t.setColour (juce::ToggleButton::tickColourId, amber);
    t.setColour (juce::ToggleButton::tickDisabledColourId, dim);
}

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

const char* kindName (int k) { return k == idea::kDrums ? "ドラム" : k == idea::kBoth ? "メロディー＋ドラム" : "メロディー"; }

// Hum Keys で覚えさせた口ドラムの癖があれば、それを使う（Hum Keys 1.2.0 の設定の形）
idea::DrumVoice humKeysDrumVoice()
{
    idea::DrumVoice d;
    juce::PropertiesFile::Options o;
    o.applicationName = "Hum Keys"; o.folderName = "Hum Keys"; o.filenameSuffix = ".settings";
    juce::PropertiesFile hk (o);
    for (int i = 0; i < idea::DrumVoice::ND; ++i)
    {
        const auto c = juce::String (i);
        d.notes[i] = hk.getIntValue ("drumNote" + c, d.notes[i]);
        if (! hk.containsKey ("dhas" + c)) continue;
        d.has[i] = hk.getBoolValue ("dhas" + c);
        d.sus[i] = (float) hk.getDoubleValue ("dsus" + c, d.sus[i]);
        for (int k = 0; k < idea::DrumVoice::NF; ++k)
            d.proto[i][k] = (float) hk.getDoubleValue ("dproto" + c + "_" + juce::String (k), d.proto[i][k]);
    }
    return d;
}

bool readWav (const juce::File& f, std::vector<float>& out, double& fs)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b ((int) r->numChannels, n);
    r->read (&b, 0, n, 0, true, true);
    out.assign ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) out[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    fs = r->sampleRate;
    return true;
}
} // namespace

//==============================================================================
juce::File MainComponent::ideasRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Idea Catcher");
}

MainComponent::MainComponent (bool isLive) : live (isLive)
{
    juce::PropertiesFile::Options po;
    po.applicationName = "Idea Catcher";
    po.folderName = "Idea Catcher";
    po.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (po);

    for (auto* b : { &kindBtn, &keyDownBtn, &keyUpBtn, &modeBtn, &bpmMinus, &bpmPlus, &bpmHalf, &bpmDouble,
                     &quantBtn, &finderBtn, &hotkeyBtn, &inputBtn })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    styleButton (logicBtn, true);
    addAndMakeVisible (logicBtn);
    for (auto* t : { &autoOpenBtn, &autoStopBtn, &loginBtn })
    {
        styleToggle (*t);
        addAndMakeVisible (*t);
    }

    logicBtn.setButtonText (u8 ("Logic で開く")); inputBtn.setButtonText (u8 ("入力…"));
    autoOpenBtn.setButtonText (u8 ("止めたら Logic で開く")); autoStopBtn.setButtonText (u8 ("黙って 4 秒で止める"));
    loginBtn.setButtonText (u8 ("ログイン時に起動"));
    keyDownBtn.setButtonText (u8 ("◀")); keyUpBtn.setButtonText (u8 ("▶"));
    bpmMinus.setButtonText (u8 ("−")); bpmPlus.setButtonText ("+");
    bpmHalf.setButtonText (u8 ("÷2")); bpmDouble.setButtonText (u8 ("×2"));

    keyDownBtn.onClick = [this] { editOptions ([] (auto& o) { o.key = (o.key + 11) % 12; }); };
    keyUpBtn.onClick   = [this] { editOptions ([] (auto& o) { o.key = (o.key + 1) % 12; }); };
    modeBtn.onClick    = [this] { editOptions ([] (auto& o) { o.minor = ! o.minor; }); };
    bpmMinus.onClick   = [this] { editOptions ([] (auto& o) { o.bpm = std::max (40.0, o.bpm - 1); }); };
    bpmPlus.onClick    = [this] { editOptions ([] (auto& o) { o.bpm = std::min (240.0, o.bpm + 1); }); };
    bpmHalf.onClick    = [this] { editOptions ([] (auto& o) { o.bpm = std::max (40.0, std::round (o.bpm / 2)); }); };
    bpmDouble.onClick  = [this] { editOptions ([] (auto& o) { o.bpm = std::min (240.0, o.bpm * 2); }); };
    kindBtn.onClick    = [this] { editOptions ([] (auto& o) { o.kind = (o.kind + 1) % 3; }); };
    quantBtn.onClick   = [this] { editOptions ([] (auto& o) { o.quantize = (o.quantize + 1) % 3; }); };
    logicBtn.onClick   = [this] { openLastInLogic(); };
    finderBtn.onClick  = [this] { if (cur.valid) (cur.mid.existsAsFile() ? cur.mid : cur.folder).revealToUser(); else ideasRoot().revealToUser(); };
    inputBtn.onClick   = [this] { showInputSettings(); };
    hotkeyBtn.onClick  = [this]
    {
        juce::PopupMenu m;
        const auto& c = mac::hotKeyChoices();
        for (int i = 0; i < (int) c.size(); ++i)
            m.addItem (i + 1, u8 (c[(size_t) i].label), true, props->getIntValue ("hotkey", 0) == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (hotkeyBtn), [this] (int r)
        {
            if (r <= 0) return;
            props->setValue ("hotkey", r - 1);
            registerHotKey();
        });
    };

    autoOpenBtn.setToggleState (props->getBoolValue ("autoOpen", true), juce::dontSendNotification);
    autoStopBtn.setToggleState (props->getBoolValue ("autoStop", true), juce::dontSendNotification);
    autoOpenBtn.onClick = [this] { props->setValue ("autoOpen", autoOpenBtn.getToggleState()); };
    autoStopBtn.onClick = [this] { props->setValue ("autoStop", autoStopBtn.getToggleState()); };
    loginBtn.setToggleState (live && mac::launchAtLogin(), juce::dontSendNotification);
    loginBtn.onClick = [this]
    {
        const bool want = loginBtn.getToggleState();
        if (! mac::setLaunchAtLogin (want))
        {
            message = u8 ("ログイン時の起動を変えられませんでした（システム設定 → 一般 → ログイン項目 で設定できます）");
            repaint();
        }
        loginBtn.setToggleState (mac::launchAtLogin(), juce::dontSendNotification);
    };

    setSize (W, H);
    refreshRecent();

    if (live)
    {
        registerHotKey();
        mac::setStatusIcon (0);
        mac::listenCommands ([this] (const std::string& c) { command (juce::String (c)); });
        refreshMenu();
        if (! mac::logicInstalled()) message = u8 ("Logic Pro が見つかりません（MIDI はふつうのアプリで開きます）");
        startTimerHz (30);
    }
    refreshButtons();
}

MainComponent::~MainComponent()
{
    stopTimer();
    recording = false;
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    props->saveIfNeeded();
}

//==============================================================================
juce::String MainComponent::hotKeyLabel() const
{
    const auto& c = mac::hotKeyChoices();
    const int i = juce::jlimit (0, (int) c.size() - 1, props->getIntValue ("hotkey", 0));
    return u8 (c[(size_t) i].label);
}

void MainComponent::registerHotKey()
{
    const bool ok = mac::registerHotKey (props->getIntValue ("hotkey", 0), [this] { toggleRecord(); });
    if (! ok) message = hotKeyLabel() + u8 (" はほかのアプリが使っているようです。下の「録音キー」で別のキーを選んでください");
    hotkeyBtn.setButtonText (u8 ("録音キー: ") + hotKeyLabel());
    refreshMenu();
    repaint();
}

void MainComponent::refreshMenu()
{
    if (! live) return;
    std::vector<std::pair<std::string, int>> items;
    const auto hk = hotKeyLabel().toStdString();
    items.push_back ({ (state == Recording || state == Starting ? "録音を止める（" : "録音する（") + hk + "）", 1 });
    items.push_back ({ "画面を出す", 2 });
    if (cur.valid) items.push_back ({ "このアイデアを Logic で開く", 3 });
    items.push_back ({ "保存フォルダを開く", 4 });
    items.push_back ({ "", 0 });
    items.push_back ({ "Idea Catcher を終了", 9 });
    mac::setStatusMenu (items, [this] (int tag)
    {
        switch (tag)
        {
            case 1: toggleRecord(); break;
            case 2: showMe (true); break;
            case 3: openLastInLogic(); break;
            case 4: ideasRoot().createDirectory(); ideasRoot().revealToUser(); break;
            case 9: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
    });
}

void MainComponent::command (const juce::String& c)
{
    if (c == "toggle") toggleRecord();
    else if (c == "start") { if (state != Recording && state != Starting) toggleRecord(); }
    else if (c == "stop") { if (state == Recording) toggleRecord(); }
    else if (c == "show") showMe (true);
    else if (c == "open") openLastInLogic();
}

void MainComponent::showMe (bool focus)
{
    if (onShowRequest) onShowRequest (state == Starting || state == Recording || state == Analysing, focus);
}

void MainComponent::setState (State s)
{
    state = s;
    if (live) mac::setStatusIcon (s == Recording || s == Starting ? 1 : s == Analysing ? 2 : 0);
    refreshMenu();
    refreshButtons();
    repaint();
}

//==============================================================================
void MainComponent::toggleRecord()
{
    if (state == Recording) stopRecording();
    else if (state == Idle || state == Done) startRecording();
}

void MainComponent::startRecording()
{
    message.clear();
    setState (Starting);
    showMe (false);
    mac::requestMic ([sp = juce::Component::SafePointer<MainComponent> (this)] (bool ok)
    {
        if (sp == nullptr) return;
        if (! ok)
        {
            sp->message = u8 ("マイクが使えません。システム設定 → プライバシーとセキュリティ → マイク で Idea Catcher を ON にしてください");
            sp->setState (sp->cur.valid ? Done : Idle);
            return;
        }
        sp->openDeviceAndRecord();
    });
}

void MainComponent::openDeviceAndRecord()
{
    if (devices.getCurrentAudioDevice() == nullptr)
    {
        static bool inited = false;
        juce::String err;
        if (! inited)
        {
            auto xml = props->getXmlValue ("audio");
            err = devices.initialise (2, 0, xml.get(), true);
            inited = true;
        }
        else
            devices.restartLastAudioDevice();
        if (devices.getCurrentAudioDevice() == nullptr)
        {
            message = u8 ("マイク（入力）が見つかりません。「入力…」で選んでください") + (err.isNotEmpty() ? " (" + err + ")" : juce::String());
            setState (cur.valid ? Done : Idle);
            return;
        }
    }
    recFs = devices.getCurrentAudioDevice()->getCurrentSampleRate();
    const size_t cap = (size_t) (recFs * maxSeconds);
    if (rec.size() != cap) rec.assign (cap, 0.0f);
    recN = 0; lastLoud = -1; level = 0; meter = 0;
    recording = true;
    devices.addAudioCallback (this);
    recStartMs = juce::Time::getMillisecondCounterHiRes();
    setState (Recording);
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                      int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (! recording.load()) return;
    int64_t w = recN.load();
    float pk = 0;
    for (int i = 0; i < n && w < (int64_t) rec.size(); ++i, ++w)
    {
        float v = 0;
        for (int c = 0; c < numIn; ++c) if (in[c] != nullptr) v += in[c][i];
        rec[(size_t) w] = v;
        pk = std::max (pk, std::abs (v));
    }
    recN.store (w);
    if (pk > loudThreshold) lastLoud.store (w);
    level.store (std::max (level.load() * 0.85f, pk));
}

void MainComponent::timerCallback()
{
    if (state != Recording)
    {
        if (state == Analysing || state == Starting) repaint (recArea);
        return;
    }
    meter = std::max (meter * 0.88f, level.load());
    level = level.load() * 0.7f;
    const auto n = recN.load(), loud = lastLoud.load();
    const auto fs = (int64_t) recFs;
    if (n >= (int64_t) rec.size()) { stopRecording(); return; }
    if (autoStopBtn.getToggleState() && loud >= 0 && n > fs * 3 / 2 && n - loud > (int64_t) (recFs * autoStopSeconds))
    {
        stopRecording();
        return;
    }
    if (loud < 0 && n > fs * 20)
    {
        stopRecording();   // 20 秒なにも聞こえなければやめる
        return;
    }
    repaint (recArea);
}

void MainComponent::stopRecording()
{
    recording = false;
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();   // マイクを使っている印（オレンジの点）を消す
    const auto n = recN.load(), loud = lastLoud.load();
    if (loud < 0)
    {
        message = u8 ("声が聞こえませんでした。「入力…」でマイクを確かめてください");
        setState (cur.valid ? Done : Idle);
        return;
    }
    const auto end = std::min<int64_t> (n, loud + (int64_t) (recFs * 0.6));
    std::vector<float> samples (rec.begin(), rec.begin() + (long) end);
    setState (Analysing);

    const auto dv = humKeysDrumVoice();
    const double fs = recFs, fallback = props->getDoubleValue ("lastBpm", 100.0);
    juce::Thread::launch ([sp = juce::Component::SafePointer<MainComponent> (this), samples = std::move (samples), fs, dv, fallback]() mutable
    {
        auto r = idea::analyse (samples, fs, dv, fallback);
        juce::MessageManager::callAsync ([sp, r = std::move (r), samples = std::move (samples), fs]() mutable
        {
            if (sp != nullptr) sp->finishAnalysis (std::move (r), std::move (samples), fs);
        });
    });
}

void MainComponent::finishAnalysis (idea::Result r, std::vector<float> samples, double fs)
{
    if (! r.heard || (r.melody.empty() && r.drums.empty()))
    {
        message = u8 ("音にできるものがありませんでした。もう少し大きく、はっきりめに");
        setState (cur.valid ? Done : Idle);
        return;
    }
    const auto folder = ideasRoot().getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
    folder.createDirectory();

    // 元の声もとっておく（あとで Logic にドラッグできる）
    const auto wav = folder.getChildFile ("Voice.wav");
    {
        juce::AudioBuffer<float> b (1, (int) samples.size());
        b.copyFrom (0, 0, samples.data(), (int) samples.size());
        std::unique_ptr<juce::OutputStream> os (wav.createOutputStream());
        if (os != nullptr)
        {
            juce::WavAudioFormat wf;
            auto w = wf.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (1).withBitsPerSample (24));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
    }

    applyResult (folder, std::move (r), {});
    props->setValue ("lastBpm", cur.o.bpm);
    message.clear();
    if (autoOpenBtn.getToggleState() && cur.mid.existsAsFile())
    {
        mac::openInLogic (cur.mid.getFullPathName().toStdString());
        showMe (false);   // Logic が前に来るので、こちらはふつうの高さに
    }
    else
        showMe (true);
}

void MainComponent::applyResult (const juce::File& folder, idea::Result r, const juce::var& saved)
{
    cur.valid = true;
    cur.folder = folder;
    cur.wav = folder.getChildFile ("Voice.wav");
    cur.o.key = r.key.key; cur.o.minor = r.key.minor; cur.o.bpm = r.bpm; cur.o.kind = r.kind; cur.o.quantize = props->getIntValue ("quantize", 0);
    if (auto* obj = saved.getDynamicObject())
    {
        cur.o.key = (int) obj->getProperty ("key");
        cur.o.minor = (bool) obj->getProperty ("minor");
        cur.o.bpm = (double) obj->getProperty ("bpm");
        cur.o.kind = (int) obj->getProperty ("kind");
        cur.o.quantize = (int) obj->getProperty ("quantize");
    }
    cur.r = std::move (r);
    writeMidi();
    saveInfo();
    refreshRecent();
    setState (Done);
}

void MainComponent::loadIdea (const juce::File& folder)
{
    if (state == Recording || state == Starting || state == Analysing) return;
    std::vector<float> x; double fs = 48000;
    if (! readWav (folder.getChildFile ("Voice.wav"), x, fs))
    {
        message = u8 ("このアイデアの録音が読めませんでした");
        repaint();
        return;
    }
    const auto saved = juce::JSON::parse (folder.getChildFile ("info.json"));
    setState (Analysing);
    const auto dv = humKeysDrumVoice();
    juce::Thread::launch ([sp = juce::Component::SafePointer<MainComponent> (this), x = std::move (x), fs, dv, folder, saved]() mutable
    {
        auto r = idea::analyse (std::move (x), fs, dv, 100);
        juce::MessageManager::callAsync ([sp, r = std::move (r), folder, saved]() mutable
        {
            if (sp != nullptr) { sp->message.clear(); sp->applyResult (folder, std::move (r), saved); }
        });
    });
}

bool MainComponent::loadForSnapshot (const juce::File& wav)
{
    std::vector<float> x; double fs = 48000;
    if (! readWav (wav, x, fs)) return false;
    auto r = idea::analyse (std::move (x), fs, humKeysDrumVoice(), 100);
    cur.folder = juce::File();
    applyResult (wav.getParentDirectory().getChildFile ("IdeaCatcherSnapshot"), std::move (r), {});
    return true;
}

//==============================================================================
juce::String MainComponent::shortKey (int key, bool minor) { return u8 (idea::keyName (key)) + (minor ? "m" : ""); }

juce::String MainComponent::keyLabel() const
{
    return u8 (idea::keyName (cur.o.key)) + (cur.o.minor ? u8 (" マイナー") : u8 (" メジャー"));
}

void MainComponent::writeMidi()
{
    if (! cur.valid) return;
    cur.folder.createDirectory();
    for (auto& f : cur.folder.findChildFiles (juce::File::findFiles, false, "*.mid")) f.deleteFile();
    // Logic のプロジェクト名にもなる
    const auto stamp = cur.folder.getFileName().substring (5, 16);   // "11-05 15.30"
    const auto name = juce::File::createLegalFileName ("Idea " + stamp + " " + shortKey (cur.o.key, cur.o.minor)
                                                        + " " + juce::String ((int) std::lround (cur.o.bpm)) + "bpm");
    cur.mid = cur.folder.getChildFile (name + ".mid");
    const auto bytes = idea::buildMidiFile (cur.r, cur.o);
    cur.mid.replaceWithData (bytes.data(), bytes.size());
}

void MainComponent::saveInfo()
{
    if (! cur.valid) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", cur.o.key);
    o->setProperty ("minor", cur.o.minor);
    o->setProperty ("bpm", cur.o.bpm);
    o->setProperty ("kind", cur.o.kind);
    o->setProperty ("quantize", cur.o.quantize);
    o->setProperty ("seconds", cur.r.seconds);
    o->setProperty ("notes", (int) idea::includedNotes (cur.r, cur.o.kind).size());
    o->setProperty ("keyConfidence", cur.r.key.confidence);
    o->setProperty ("tempoConfidence", cur.r.tempoConf);
    cur.folder.getChildFile ("info.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

void MainComponent::editOptions (std::function<void (idea::MidiOptions&)> f)
{
    if (! cur.valid) return;
    f (cur.o);
    props->setValue ("quantize", cur.o.quantize);
    writeMidi();
    saveInfo();
    refreshRecent();
    refreshButtons();
    repaint();
}

void MainComponent::openLastInLogic()
{
    if (! cur.valid || ! cur.mid.existsAsFile()) return;
    mac::openInLogic (cur.mid.getFullPathName().toStdString());
}

void MainComponent::refreshRecent()
{
    recent.clear();
    auto dirs = ideasRoot().findChildFiles (juce::File::findDirectories, false);
    std::sort (dirs.begin(), dirs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (auto& d : dirs)
        if (d.getChildFile ("Voice.wav").existsAsFile() && recent.size() < 4) recent.add (d);
}

void MainComponent::refreshButtons()
{
    const bool show = cur.valid && state != Analysing;
    for (auto* b : { &kindBtn, &keyDownBtn, &keyUpBtn, &modeBtn, &bpmMinus, &bpmPlus, &bpmHalf, &bpmDouble, &quantBtn, &logicBtn })
        b->setVisible (show);
    finderBtn.setButtonText (cur.valid ? "Finder" : u8 ("保存フォルダ"));
    kindBtn.setButtonText (u8 (kindName (cur.o.kind)));
    modeBtn.setButtonText (cur.o.minor ? u8 ("マイナー") : u8 ("メジャー"));
    quantBtn.setButtonText (cur.o.quantize == 1 ? u8 ("クオンタイズ 1/16") : cur.o.quantize == 2 ? u8 ("クオンタイズ 1/8") : u8 ("クオンタイズ なし"));
    hotkeyBtn.setButtonText (u8 ("録音キー: ") + hotKeyLabel());
    inputBtn.setEnabled (state != Recording && state != Starting);
}

//==============================================================================
namespace
{
// 入力を選ぶ画面（閉じたら設定を保存して、マイクを閉じる）
struct InputPanel : juce::Component
{
    juce::AudioDeviceManager& dm;
    juce::PropertiesFile& props;
    juce::AudioDeviceSelectorComponent sel;
    InputPanel (juce::AudioDeviceManager& d, juce::PropertiesFile& p)
        : dm (d), props (p), sel (d, 1, 2, 0, 0, false, false, false, false)
    {
        addAndMakeVisible (sel);
        setSize (460, 260);
    }
    ~InputPanel() override
    {
        if (auto xml = dm.createStateXml()) props.setValue ("audio", xml.get());
        props.saveIfNeeded();
        dm.closeAudioDevice();
    }
    void paint (juce::Graphics& g) override { g.fillAll (panel); }
    void resized() override { sel.setBounds (getLocalBounds().reduced (8)); }
};
}

void MainComponent::showInputSettings()
{
    if (state == Recording || state == Starting) return;
    if (devices.getCurrentAudioDevice() == nullptr)
    {
        auto xml = props->getXmlValue ("audio");
        devices.initialise (2, 0, xml.get(), true);
    }
    juce::DialogWindow::LaunchOptions lo;
    lo.content.setOwned (new InputPanel (devices, *props));
    lo.dialogTitle = u8 ("マイク（入力）");
    lo.dialogBackgroundColour = panel;
    lo.useNativeTitleBar = true;
    lo.resizable = false;
    lo.componentToCentreAround = this;
    lo.launchAsync();
}

//==============================================================================
void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    header = r.removeFromTop (58);
    recArea = r.removeFromTop (112);
    recCircle = recArea.withSizeKeepingCentre (84, 84).withX (recArea.getX() + 14);
    r.removeFromTop (12);
    card = r.removeFromTop (262);
    r.removeFromTop (12);
    actions = r.removeFromTop (40);
    r.removeFromTop (8);
    auto drags = r.removeFromTop (28);
    r.removeFromTop (14);
    recentArea = r.removeFromTop (124);
    footer = r;

    auto c = card.reduced (14, 12);
    auto row = [&] (int h) { auto x = c.removeFromTop (h); c.removeFromTop (8); return x; };
    {
        auto a = row (30);
        a.removeFromLeft (64);
        keyDownBtn.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (120);
        keyUpBtn.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (8);
        modeBtn.setBounds (a.removeFromLeft (84));
    }
    {
        auto a = row (30);
        a.removeFromLeft (64);
        bpmMinus.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (120);
        bpmPlus.setBounds (a.removeFromLeft (30));
        a.removeFromLeft (8);
        bpmHalf.setBounds (a.removeFromLeft (40));
        a.removeFromLeft (4);
        bpmDouble.setBounds (a.removeFromLeft (40));
    }
    {
        auto a = row (28);
        kindBtn.setBounds (a.removeFromLeft (150));
        a.removeFromLeft (8);
        quantBtn.setBounds (a.removeFromLeft (150));
    }
    roll = c.removeFromTop (100);

    auto ac = actions;
    finderBtn.setBounds (ac.removeFromRight (110));
    ac.removeFromRight (8);
    logicBtn.setBounds (ac);
    dragMidi = drags.removeFromLeft (drags.getWidth() / 2 - 4);
    dragWav = drags.removeFromRight (dragMidi.getWidth());

    auto f = footer.reduced (0, 6);
    auto f1 = f.removeFromTop (28);
    hotkeyBtn.setBounds (f1.removeFromLeft (170));
    f1.removeFromLeft (8);
    inputBtn.setBounds (f1.removeFromLeft (80));
    f1.removeFromLeft (8);
    loginBtn.setBounds (f1);
    f.removeFromTop (4);
    auto f2 = f.removeFromTop (26);
    autoOpenBtn.setBounds (f2.removeFromLeft (200));
    autoStopBtn.setBounds (f2);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // 見出し
    g.setColour (textC);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Idea Catcher", header.withTrimmedTop (10).removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (u8 ("思いつきを、そのまま曲の入口に"), header.withTrimmedTop (38), juce::Justification::topLeft);

    // 録音の所
    g.setColour (panel);
    g.fillRoundedRectangle (recArea.toFloat(), 12.0f);
    const bool recNow = state == Recording || state == Starting;
    {
        auto cc = recCircle.toFloat();
        if (state == Recording)
        {
            const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounterHiRes() * 0.006f);
            g.setColour (red.withAlpha (0.18f + 0.15f * pulse));
            g.fillEllipse (cc.expanded (4 + 6 * pulse));
        }
        g.setColour (recNow ? red : panel2);
        g.fillEllipse (cc);
        g.setColour (recNow ? juce::Colours::white : red);
        if (recNow) g.fillRoundedRectangle (cc.withSizeKeepingCentre (26, 26), 4.0f);
        else g.fillEllipse (cc.withSizeKeepingCentre (30, 30));
    }
    auto info = recArea.withTrimmedLeft (recCircle.getRight() - recArea.getX() + 18).reduced (0, 16);
    juce::String big, small;
    switch (state)
    {
        case Idle:      big = hotKeyLabel() + u8 (" で録音"); small = u8 ("鼻歌でも口ドラムでも。もう一度押すか、黙ると止まります"); break;
        case Starting:  big = u8 ("マイクを開いています…"); break;
        case Recording:
        {
            const int s = (int) (recN.load() / recFs);
            big = u8 ("録音中  ") + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
            small = hotKeyLabel() + u8 (" で止める") + (autoStopBtn.getToggleState() ? u8 ("（黙って 4 秒でも止まります）") : juce::String());
            break;
        }
        case Analysing: big = u8 ("MIDI にしています…"); small = u8 ("キーとテンポも調べています"); break;
        case Done:      big = u8 ("できました"); small = hotKeyLabel() + u8 (" で次のアイデアを録音"); break;
    }
    g.setColour (recNow ? red : textC);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (big, info.removeFromTop (28), juce::Justification::centredLeft);
    if (state == Recording)
    {
        auto m = info.removeFromTop (14).reduced (0, 3).withTrimmedRight (16).toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (m, 3.0f);
        const float db = 20.0f * std::log10 (meter + 1e-6f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -3 ? red : amber);
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * t), 3.0f);
    }
    else if (state == Analysing)
    {
        const int dots = (int) (juce::Time::getMillisecondCounter() / 300) % 4;
        g.setColour (amber);
        g.drawText (juce::String::repeatedString (u8 ("●"), dots), info.removeFromTop (14), juce::Justification::centredLeft);
    }
    else info.removeFromTop (6);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (small, info.withTrimmedRight (12), juce::Justification::topLeft, 2);

    // 結果のカード
    g.setColour (panel);
    g.fillRoundedRectangle (card.toFloat(), 12.0f);
    if (! cur.valid || state == Analysing)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (state == Analysing ? u8 ("少し待ってください…")
                                             : u8 ("録ると、ここにキー・テンポ・MIDI が出ます。\nできた MIDI は、キーとテンポ入りの Logic プロジェクトとして開きます。"),
                          card.reduced (24), juce::Justification::centred, 4);
    }
    else
    {
        auto c = card.reduced (14, 12);
        auto label = [&] (juce::Rectangle<int> row, const juce::String& name, const juce::String& value, const juce::String& note)
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (name, row.removeFromLeft (64), juce::Justification::centredLeft);
            row.removeFromLeft (30);
            g.setColour (textC);
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (value, row.removeFromLeft (120), juce::Justification::centred);
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (note, row.withTrimmedLeft (30 + 8 + 92), juce::Justification::centredRight);
        };
        const auto pct = [] (float v) { return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100)) + "%"; };
        const bool keyUsed = cur.o.kind != idea::kDrums;
        label (c.removeFromTop (30), "KEY", keyUsed ? u8 (idea::keyName (cur.o.key)) + (cur.o.minor ? "m" : "") : u8 ("（ドラムだけ）"), {});
        c.removeFromTop (8);
        label (c.removeFromTop (30), "TEMPO", juce::String ((int) std::lround (cur.o.bpm)) + " BPM", {});
        c.removeFromTop (8 + 28 + 8);

        // 小さいピアノロール
        auto rr = roll.toFloat();
        g.setColour (panel2);
        g.fillRoundedRectangle (rr, 6.0f);
        const auto placed = idea::place (cur.r, cur.o);
        double lastBeat = 4;
        int lo = 127, hi = 0;
        for (auto& p : placed)
        {
            lastBeat = std::max (lastBeat, p.b1);
            if (p.ch != 9) { lo = std::min (lo, p.n); hi = std::max (hi, p.n); }
        }
        const double bars = std::ceil (lastBeat / 4.0);
        const float x0 = rr.getX() + 6, wpx = rr.getWidth() - 12;
        auto bx = [&] (double b) { return x0 + (float) (b / (bars * 4.0)) * wpx; };
        for (int b = 0; b <= (int) bars * 4; ++b)
        {
            g.setColour (b % 4 == 0 ? line.brighter (0.25f) : line.withAlpha (0.45f));
            g.drawVerticalLine (juce::roundToInt (bx (b)), rr.getY() + 4, rr.getBottom() - 4);
        }
        const bool hasDrums = std::any_of (placed.begin(), placed.end(), [] (auto& p) { return p.ch == 9; });
        const bool hasMel = std::any_of (placed.begin(), placed.end(), [] (auto& p) { return p.ch != 9; });
        auto melArea = rr.reduced (0, 6);
        juce::Rectangle<float> drumArea;
        if (hasDrums) drumArea = hasMel ? melArea.removeFromBottom (36) : melArea;
        if (hi < lo) { lo = 60; hi = 72; }
        juce::Array<int> drumRows;   // 上から高いノート番号順（ハイハットが上、キックが下）
        for (auto& p : placed) if (p.ch == 9) drumRows.addIfNotAlreadyThere (p.n);
        drumRows.sort();
        std::reverse (drumRows.begin(), drumRows.end());
        const float span = (float) std::max (12, hi - lo + 2);
        for (auto& p : placed)
        {
            juce::Rectangle<float> nr;
            if (p.ch == 9)
            {
                const int row = drumRows.indexOf (p.n);
                const float h = drumArea.getHeight() / (float) juce::jmax (1, drumRows.size());
                nr = { bx (p.b0), drumArea.getY() + h * (float) row + 1, std::max (3.0f, bx (p.b0 + 0.12) - bx (p.b0)), h - 2 };
                g.setColour (cyan.withAlpha (0.35f + 0.65f * (float) p.vel / 127.0f));
            }
            else
            {
                const float y = melArea.getBottom() - ((float) (p.n - lo + 1) / span) * melArea.getHeight();
                nr = { bx (p.b0), y - 2.5f, std::max (3.0f, bx (p.b1) - bx (p.b0) - 1), 5.0f };
                g.setColour (amber);
            }
            g.fillRoundedRectangle (nr, 1.5f);
        }
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.5f));
        const int nNotes = (int) placed.size();
        g.drawText (juce::String (nNotes) + u8 (" 音 · ") + juce::String ((int) bars) + u8 (" 小節 · ")
                        + juce::String (cur.r.seconds, 1) + u8 (" 秒   確かさ: ")
                        + (keyUsed ? u8 ("キー ") + pct (cur.r.key.confidence) + " / " : juce::String())
                        + u8 ("テンポ ") + pct (cur.r.tempoConf),
                    card.withTop (roll.getBottom() + 4).reduced (14, 0).withHeight (18), juce::Justification::centredLeft);
    }

    // ドラッグ
    auto chip = [&] (juce::Rectangle<int> r, const juce::String& t, bool on)
    {
        g.setColour (on ? panel2 : panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (on ? textC : dim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (t, r, juce::Justification::centred);
    };
    chip (dragMidi, u8 ("⠿ MIDI をドラッグ → Logic"), cur.valid);
    chip (dragWav, u8 ("⠿ 声の録音 (WAV) をドラッグ"), cur.valid && cur.wav.existsAsFile());

    // 最近のアイデア
    auto ra = recentArea;
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (u8 ("最近のアイデア（クリックで開き直す）"), ra.removeFromTop (20), juce::Justification::centredLeft);
    if (recent.isEmpty())
    {
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (u8 ("まだありません。~/Music/Idea Catcher に保存されます"), ra.removeFromTop (26), juce::Justification::centredLeft);
    }
    for (int i = 0; i < recent.size(); ++i)
    {
        auto rr = ra.removeFromTop (26);
        const bool isCur = cur.valid && recent[i] == cur.folder;
        if (isCur || i == hoverRecent)
        {
            g.setColour (isCur ? panel2 : panel);
            g.fillRoundedRectangle (rr.toFloat(), 5.0f);
        }
        const auto info = juce::JSON::parse (recent[i].getChildFile ("info.json"));
        juce::String desc;
        if (auto* o = info.getDynamicObject())
            desc = u8 (kindName ((int) o->getProperty ("kind"))) + "  "
                 + ((int) o->getProperty ("kind") == idea::kDrums ? juce::String() : shortKey ((int) o->getProperty ("key"), (bool) o->getProperty ("minor")) + "  ")
                 + juce::String ((int) std::lround ((double) o->getProperty ("bpm"))) + u8 (" BPM  ·  ")
                 + juce::String ((double) o->getProperty ("seconds"), 1) + u8 (" 秒");
        g.setColour (isCur ? amber : textC);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (recent[i].getFileName(), rr.withTrimmedLeft (8).removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (dim);
        g.drawText (desc, rr.withTrimmedLeft (160), juce::Justification::centredLeft);
    }

    // 下の一言
    if (message.isNotEmpty())
    {
        g.setColour (amber);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (message, getLocalBounds().removeFromBottom (24).reduced (16, 2), juce::Justification::centredLeft, 1);
    }
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    dragStarted = false;
    if (recCircle.expanded (6).contains (p)) { toggleRecord(); return; }
    auto ra = recentArea.withTrimmedTop (20);
    for (int i = 0; i < recent.size(); ++i)
        if (ra.removeFromTop (26).contains (p))
        {
            loadIdea (recent[i]);
            return;
        }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    auto ra = recentArea.withTrimmedTop (20);
    for (int i = 0; i < recent.size(); ++i)
        if (ra.removeFromTop (26).contains (e.getPosition())) h = i;
    const bool overHot = h >= 0 || recCircle.contains (e.getPosition()) || ((dragMidi.contains (e.getPosition()) || dragWav.contains (e.getPosition())) && cur.valid);
    setMouseCursor (overHot ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hoverRecent) { hoverRecent = h; repaint (recentArea); }
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! cur.valid || dragStarted || e.getDistanceFromDragStart() < 4) return;
    const auto start = e.getMouseDownPosition();
    juce::File f;
    if (dragMidi.contains (start)) f = cur.mid;
    else if (dragWav.contains (start)) f = cur.wav;
    if (! f.existsAsFile()) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
