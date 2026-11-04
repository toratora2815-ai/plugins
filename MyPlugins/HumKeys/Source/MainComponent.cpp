#include "MainComponent.h"
#include "MicPermission.h"

namespace
{
const juce::Colour bg (0xff0e1117), panel (0xff171c26), panel2 (0xff1f2633), line (0xff2b3445),
                   textC (0xffe6e9f0), dim (0xff8b93a7), orange (0xffff8a3d), cyan (0xff38e1ff), pink (0xffff5c8a);
const juce::Colour padCol[3] = { orange, pink, cyan };
const char* padName[3] = { "KICK", "SNARE", "HAT" };
const char* padSay[3]  = { "ドン / ブッ", "タッ / カッ", "ツッ / チッ" };

// Logic と同じ呼び方（真ん中の C = C3 = 60）
juce::String noteName (int n)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (nm[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);
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
} // namespace

//==============================================================================
MainComponent::MainComponent (bool live)
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

    hist.assign (1900, { -1.0f, -100.0f, -1 });

    juce::PropertiesFile::Options o;
    o.applicationName = "Hum Keys";
    o.folderName = "Hum Keys";
    o.osxLibrarySubFolder = "Application Support";
    o.filenameSuffix = ".settings";
    props = std::make_unique<juce::PropertiesFile> (o);

    // ---- 上の行 ----
    for (auto* b : { &onButton, &melodyTab, &drumsTab })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    onButton.setTooltip ("MIDI を出す／止める（スペースキー）");
    onButton.onClick = [this] { engine.p.enabled = ! engine.p.enabled.load(); refreshButtons(); saveSettings(); };
    melodyTab.onClick = [this] { setMode (0); };
    drumsTab.onClick  = [this] { setMode (1); };
    addAndMakeVisible (audioButton);
    audioButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (panicButton);
    panicButton.setTooltip ("鳴りっぱなしの音を全部止める（esc）");
    panicButton.onClick = [this] { engine.panicRequest = true; };
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setFont (juce::FontOptions (13.0f));

    // ---- MELODY ----
    for (int k = 0; k < 12; ++k) keyBox.addItem (juce::String (noteName (k + 60)).dropLastCharacters (1), k + 1);
    for (int s = 0; s < hum::scNumScales; ++s) scaleBox.addItem (juce::String::fromUTF8 (scaleNames[s]), s + 1);
    for (int v = -2; v <= 2; ++v) octaveBox.addItem ((v > 0 ? "+" : "") + juce::String (v), v + 3);
    for (int c = 1; c <= 16; ++c) chanBox.addItem (juce::String (c), c);
    keyBox.onChange    = [this] { engine.p.key = keyBox.getSelectedId() - 1; saveSettings(); repaint(); };
    scaleBox.onChange  = [this] { engine.p.scale = scaleBox.getSelectedId() - 1; saveSettings(); repaint(); };
    octaveBox.onChange = [this] { engine.p.octave = octaveBox.getSelectedId() - 3; saveSettings(); };
    chanBox.onChange   = [this] { engine.panicRequest = true; engine.p.channel = chanBox.getSelectedId(); saveSettings(); };

    styleSlider (glueSlider, 10, 150, 1, " ms");
    styleSlider (holdSlider, 20, 300, 1, " ms");
    glueSlider.setTooltip ("同じ高さがこの長さ続いたら音にする。短い＝速い／長い＝しゃくりや揺れに強い");
    holdSlider.setTooltip ("声がこの長さ途切れたら音を止める。短い＝細かく切れる／長い＝つながる");
    glueSlider.onValueChange = [this] { engine.p.glueMs = (float) glueSlider.getValue(); saveSettings(); };
    holdSlider.onValueChange = [this] { engine.p.holdMs = (float) holdSlider.getValue(); saveSettings(); };

    velButton.onClick  = [this] { engine.p.dynamicVel = ! engine.p.dynamicVel.load(); refreshButtons(); saveSettings(); };
    bendButton.onClick = [this] { engine.p.bend = ! engine.p.bend.load(); engine.panicRequest = true; refreshButtons(); saveSettings(); };
    velButton.setTooltip ("DYNAMIC: 声の大きさで強弱が付く／FIXED: いつも 100");
    bendButton.setTooltip ("ON にすると、音程の細かいゆれ（ビブラートなど）をピッチベンドで送る（音源のベンド幅を ±2 に）");

    caption (keyBox, "KEY"); caption (scaleBox, "SCALE"); caption (octaveBox, "OCTAVE");
    caption (glueSlider, "GLUE"); caption (holdSlider, "HOLD"); caption (velButton, "VELOCITY");
    caption (bendButton, "BEND"); caption (chanBox, "MIDI CH");

    // ---- DRUMS ----
    styleSlider (sensSlider, 3, 20, 0.5, " dB");
    sensSlider.setTooltip ("どれだけ急に大きくなったら「叩いた」とみなすか。小さいほど敏感");
    sensSlider.onValueChange = [this] { engine.p.drumSens = (float) sensSlider.getValue(); saveSettings(); };
    caption (sensSlider, "SENS");
    for (int i = 0; i < 3; ++i)
    {
        auto& nb = drumNoteBox[(size_t) i];
        for (int n = 24; n <= 84; ++n) nb.addItem (noteName (n) + "  (" + juce::String (n) + ")", n);
        nb.onChange = [this, i] { engine.p.drumNote[i] = drumNoteBox[(size_t) i].getSelectedId(); saveSettings(); };
        nb.setTooltip ("鳴らすノート。Logic の Drummer / Drum Kit Designer は KICK=C1(36) SNARE=D1(38) HAT=F#1(42)");
        auto& lb = learnButton[(size_t) i];
        lb.setTooltip ("押してから、この音を 4 回口で鳴らすと、あなたの声の癖を覚えます");
        lb.onClick = [this, i]
        {
            engine.learnCount = 0;
            engine.learnClass = engine.learnClass.load() == i ? -1 : i;
            refreshButtons();
        };
        auto& rb = resetButton[(size_t) i];
        rb.setButtonText ("RESET");
        rb.setTooltip ("覚えた癖を消して、最初の聞き分けに戻す");
        rb.onClick = [this, i] { engine.p.resetProto (i); saveSettings(); };
        addChildComponent (nb); addChildComponent (lb); addChildComponent (rb);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &scaleBox, &octaveBox, &glueSlider, &holdSlider,
                                                             &velButton, &bendButton, &chanBox, &sensSlider })
        addChildComponent (c);

    loadSettings();

    setWantsKeyboardFocus (true);
    setSize (820, 580);
    refreshButtons();
    if (! live) return;   // 画面の書き出し（確認用）のときは音も MIDI も使わない

    // ---- 仮想 MIDI ----
    sink.port = juce::MidiOutput::createNewDevice ("Hum Keys");
    midiStatus = sink.port != nullptr ? juce::String::fromUTF8 ("Logic の MIDI 入力に「Hum Keys」が出ています")
                                      : juce::String::fromUTF8 ("仮想 MIDI を作れませんでした（アプリを再起動してください）");

    // ---- マイク ----
    juce::Component::SafePointer<MainComponent> safe (this);
    requestMicPermission ([safe] (bool ok)
    {
        if (safe == nullptr) return;
        if (! ok)
        {
            safe->midiStatus = juce::String::fromUTF8 ("マイクの許可がありません：システム設定 → プライバシーとセキュリティ → マイク で Hum Keys をオン");
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
    saveSettings();
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
    engine.prepare (d->getCurrentSampleRate());
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                      int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    if ((size_t) n > mono.size()) return;
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

//==============================================================================
void MainComponent::timerCallback()
{
    hum::Frame f;
    int got = 0;
    while (engine.frames.pop (f))
    {
        hist[(size_t) histPos] = { f.pitch, f.level, f.note };
        histPos = (histPos + 1) % (int) hist.size();
        meterDb = juce::jmax (meterDb - 0.6f, f.level);
        ++got;
    }
    if (got == 0) meterDb = juce::jmax (minDb, meterDb - 1.5f);

    const float pk = juce::Decibels::gainToDecibels (inPeak.exchange (0.0f), -100.0f);
    peakHoldDb = pk > peakHoldDb ? pk : peakHoldDb - 0.5f;

    hum::Hit h;
    while (engine.hits.pop (h))
        if (h.cls >= 0 && h.cls < 3) { padFlash[(size_t) h.cls] = 1.0f; padVel[(size_t) h.cls] = h.velocity; }
    for (auto& p : padFlash) p *= 0.88f;

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
    if (note != lastShownNote) lastShownNote = note;
    repaint();
}

//==============================================================================
void MainComponent::refreshButtons()
{
    const bool on = engine.p.enabled.load();
    onButton.setButtonText (on ? "ON" : "OFF");
    onButton.setToggleState (on, juce::dontSendNotification);
    const int mode = engine.p.mode.load();
    melodyTab.setToggleState (mode == 0, juce::dontSendNotification);
    drumsTab.setToggleState (mode == 1, juce::dontSendNotification);

    velButton.setButtonText (engine.p.dynamicVel.load() ? "DYNAMIC" : "FIXED");
    bendButton.setButtonText (engine.p.bend.load() ? "ON" : "OFF");
    bendButton.setToggleState (engine.p.bend.load(), juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &scaleBox, &octaveBox, &glueSlider, &holdSlider, &bendButton })
        c->setVisible (mode == 0);
    sensSlider.setVisible (mode == 1);
    velButton.setVisible (true); chanBox.setVisible (true);

    const int lc = engine.learnClass.load();
    for (int i = 0; i < 3; ++i)
    {
        drumNoteBox[(size_t) i].setVisible (mode == 1);
        learnButton[(size_t) i].setVisible (mode == 1);
        resetButton[(size_t) i].setVisible (mode == 1);
        learnButton[(size_t) i].setButtonText (lc == i ? juce::String::fromUTF8 ("あと ") + juce::String (hum::Engine::learnShots - engine.learnCount.load()) + juce::String::fromUTF8 (" 回")
                                                       : juce::String ("LEARN"));
        learnButton[(size_t) i].setToggleState (lc == i, juce::dontSendNotification);
    }

    if (auto* d = devices.getCurrentAudioDevice())
        audioButton.setButtonText (juce::String::fromUTF8 ("入力: ") + d->getName());
    else
        audioButton.setButtonText (juce::String::fromUTF8 ("入力を選ぶ"));
    statusLabel.setText (midiStatus, juce::dontSendNotification);
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
    o.dialogTitle = juce::String::fromUTF8 ("マイク（入力）を選ぶ");
    o.dialogBackgroundColour = panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    auto* w = o.launchAsync();
    juce::ignoreUnused (w);
    // 閉じたあとに名前と設定を反映（少し後で確かめる）
    juce::Timer::callAfterDelay (500, [this] { refreshButtons(); saveSettings(); });
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
    for (int i = 0; i < 3; ++i)
    {
        p.drumNote[i] = props->getIntValue ("drumNote" + juce::String (i), p.drumNote[i].load());
        for (int k = 0; k < 2; ++k)
            p.proto[i][k] = (float) props->getDoubleValue ("proto" + juce::String (i) + "_" + juce::String (k),
                                                           hum::Params::defaultProto[i][k]);
    }

    keyBox.setSelectedId (p.key.load() + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (p.scale.load() + 1, juce::dontSendNotification);
    octaveBox.setSelectedId (p.octave.load() + 3, juce::dontSendNotification);
    chanBox.setSelectedId (p.channel.load(), juce::dontSendNotification);
    glueSlider.setValue (p.glueMs.load(), juce::dontSendNotification);
    holdSlider.setValue (p.holdMs.load(), juce::dontSendNotification);
    sensSlider.setValue (p.drumSens.load(), juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) drumNoteBox[(size_t) i].setSelectedId (p.drumNote[i].load(), juce::dontSendNotification);
}

void MainComponent::saveSettings()
{
    if (props == nullptr) return;
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
    for (int i = 0; i < 3; ++i)
    {
        props->setValue ("drumNote" + juce::String (i), p.drumNote[i].load());
        for (int k = 0; k < 2; ++k)
            props->setValue ("proto" + juce::String (i) + "_" + juce::String (k), p.proto[i][k].load());
    }
    if (devices.getCurrentAudioDevice() != nullptr)
        if (auto xml = devices.createStateXml())
            props->setValue ("audio", xml.get());
    props->saveIfNeeded();
}

void MainComponent::showModeForSnapshot (int m)
{
    const int keep = engine.p.mode.load();
    engine.p.mode = m;
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
    padFlash = { 0.0f, 1.0f, 0.2f }; padVel = { 96, 112, 70 };
    refreshButtons();
    engine.p.mode = keep;
    engine.p.mode = m;
}

juce::String MainComponent::savedWindowState() const { return props->getValue ("window"); }
void MainComponent::saveWindowState (const juce::String& s) { props->setValue ("window", s); props->saveIfNeeded(); }

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { onButton.triggerClick(); return true; }
    if (k == juce::KeyPress::escapeKey) { engine.panicRequest = true; return true; }
    if (k.getTextCharacter() == '1') { setMode (0); return true; }
    if (k.getTextCharacter() == '2') { setMode (1); return true; }
    return false;
}

// メーターの上をドラッグ → GATE（これより小さい音は無視）
void MainComponent::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }
void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! meterArea.expanded (8, 0).contains (e.getMouseDownPosition())) return;
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
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (34);
    onButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (14);
    melodyTab.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (4);
    drumsTab.setBounds (top.removeFromLeft (100));
    panicButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    audioButton.setBounds (top.removeFromRight (juce::jmin (260, top.getWidth() - 20)));

    statusLabel.setBounds (r.removeFromTop (26));

    auto bottom = r.removeFromBottom (58);
    r.removeFromBottom (10);
    displayArea = r;
    meterArea = r.removeFromLeft (26).reduced (0, 6);
    r.removeFromLeft (10);
    rollArea = r;

    // 下のつまみ類（上 18px に名前）
    bottom.removeFromTop (18);
    auto place = [&bottom] (juce::Component& c, int w) { c.setBounds (bottom.removeFromLeft (w).withHeight (30)); bottom.removeFromLeft (8); };
    if (engine.p.mode.load() == 0)
    {
        const int w = bottom.getWidth();
        const int unit = (w - 7 * 8) / 10;
        place (keyBox, unit); place (scaleBox, unit * 2 + 10); place (octaveBox, unit - 10);
        place (glueSlider, unit + 10); place (holdSlider, unit + 10);
        place (velButton, unit + 5); place (bendButton, unit - 15);
        place (chanBox, bottom.getWidth());
    }
    else
    {
        place (sensSlider, 180); place (velButton, 110); place (chanBox, 80);
    }

    // ドラムのパッド
    auto pads = rollArea;
    const int pw = (pads.getWidth() - 20) / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto pr = pads.removeFromLeft (pw);
        pads.removeFromLeft (10);
        padAreas[(size_t) i] = pr;
        auto strip = pr.reduced (12).removeFromBottom (66);
        drumNoteBox[(size_t) i].setBounds (strip.removeFromTop (28));
        strip.removeFromTop (8);
        resetButton[(size_t) i].setBounds (strip.removeFromRight (64));
        strip.removeFromRight (6);
        learnButton[(size_t) i].setBounds (strip);
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
            g.drawText (c.text, c.c->getX(), c.c->getY() - 17, c.c->getWidth(), 14, juce::Justification::left);

    // ---- 入力メーター＋GATE ----
    g.setColour (panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 4);
    const float gate = engine.p.gateDb.load();
    {
        const float y = dbToY (meterDb, meterArea);
        auto bar = meterArea.toFloat().reduced (4, 0).withTop (y);
        g.setColour (meterDb > gate ? cyan : dim.withAlpha (0.6f));
        g.fillRect (bar);
        const float py = dbToY (peakHoldDb, meterArea);
        g.setColour (peakHoldDb > -1.0f ? juce::Colours::red : textC.withAlpha (0.7f));
        g.fillRect ((float) meterArea.getX() + 3, py - 1, (float) meterArea.getWidth() - 6, 2.0f);
        const float gy = dbToY (gate, meterArea);
        g.setColour (orange);
        g.fillRect ((float) meterArea.getX() - 3, gy - 1.0f, (float) meterArea.getWidth() + 6, 2.5f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("GATE", meterArea.getX() - 6, (int) gy + 3, meterArea.getWidth() + 12, 12, juce::Justification::centred);
    }

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
        g.drawText (note >= 0 ? noteName (note) : juce::String::fromUTF8 ("—"), ra.removeFromTop (60).removeFromRight (140).reduced (10, 4),
                    juce::Justification::centredRight);

        if (! on)
        {
            g.setColour (bg.withAlpha (0.6f)); g.fillRoundedRectangle (rollArea.toFloat(), 6);
            g.setColour (textC); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("OFF（スペースで ON）"), rollArea, juce::Justification::centred);
        }
    }
    else
    {
        // ---- パッド ----
        const int lc = engine.learnClass.load();
        for (int i = 0; i < 3; ++i)
        {
            auto pr = padAreas[(size_t) i].toFloat();
            const auto c = padCol[i];
            g.setColour (panel);
            g.fillRoundedRectangle (pr, 10);
            g.setColour (c.withAlpha (0.12f + 0.75f * padFlash[(size_t) i]));
            g.fillRoundedRectangle (pr.reduced (6).withTrimmedBottom (78), 8);
            g.setColour (lc == i ? c : line);
            g.drawRoundedRectangle (pr.reduced (1), 10, lc == i ? 2.5f : 1.0f);

            auto t = pr.reduced (14).withTrimmedBottom (78);
            g.setColour (padFlash[(size_t) i] > 0.4f ? juce::Colours::black : textC);
            g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
            g.drawText (padName[i], t.removeFromTop (34), juce::Justification::centred);
            g.setFont (juce::FontOptions (14.0f));
            g.setColour (padFlash[(size_t) i] > 0.4f ? juce::Colours::black : dim);
            g.drawText (juce::String::fromUTF8 (padSay[i]), t.removeFromTop (22), juce::Justification::centred);
            if (padVel[(size_t) i] > 0)
                g.drawText ("vel " + juce::String (padVel[(size_t) i]), t.removeFromBottom (20), juce::Justification::centred);
            if (lc == i)
            {
                g.setColour (textC);
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText (juce::String::fromUTF8 ("この音を口で 4 回鳴らしてください"), t.toNearestInt(), juce::Justification::centred, 2);
            }
        }
        if (! on)
        {
            g.setColour (bg.withAlpha (0.6f)); g.fillRoundedRectangle (rollArea.toFloat(), 6);
            g.setColour (textC); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("OFF（スペースで ON）"), rollArea, juce::Justification::centred);
        }
    }
}
