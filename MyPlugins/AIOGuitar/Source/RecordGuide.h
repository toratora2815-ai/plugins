#pragma once
// RECORD GUIDE の画面（単体アプリだけ）。ギターを持ったまま進む:
//   弾く → 鳴り終わる（か手でミュート）→ 自動で判定 → OK なら次へ / だめなら理由を 1 つ出して同じ所でもう一回。
//   カポの付け替えだけ止まる（付け替えて次の音を弾けば再開）。録った生の音は WAV でも残し、終わったら .aioguitar に自動保存。
#include "PluginProcessor.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace GuideUI
{
    const juce::Colour bg { 0xff15161a }, panel { 0xff22242b }, panelHi { 0xff2c2f38 }, text { 0xffe8e6e1 }, dim { 0xff8b8d96 },
                       accent { 0xffe8a33d }, ok { 0xff4cc46a }, bad { 0xffe05a47 }, rec { 0xffff3b30 };
    inline juce::String jp (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    inline const char* layerName (int slot) { static const char* n[] { "SOFT PICKING", "HARD PICKING", "PALM MUTE", "BRUSHING", "MEDIUM PICKING" }; return n[juce::jlimit (0, Kit::numSlots - 1, slot)]; }
    inline const char* stringName (int s) { static const char* n[] { "", "1 (E)", "2 (B)", "3 (G)", "4 (D)", "5 (A)", "6 (E)" }; return n[juce::jlimit (0, 6, s)]; }

    // the stand-alone app mutes its input by default (feedback guard). The guide unmutes it: the plugin
    // never sends the input to the output, so there is no feedback.
    inline void unmuteInput()
    {
       #if JucePlugin_Build_Standalone
        if (auto* h = juce::StandalonePluginHolder::getInstance()) h->getMuteInputValue().setValue (false);
       #endif
    }
    inline void showAudioSettings()
    {
       #if JucePlugin_Build_Standalone
        if (auto* h = juce::StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog();
       #endif
    }
}

class GuidePanel : public juce::Component, private juce::Timer
{
public:
    // where the grid should glow (string, fret; string 0 = nothing)
    std::function<void (int string, int fret, bool recording)> onHighlight;
    std::function<void()> onClose;

    explicit GuidePanel (AIOGuitarProcessor& p) : proc (p)
    {
        using namespace GuideUI;
        const char* names[] { "SOFT", "HARD", "MUTE", "BRUSH", "MEDIUM" };
        for (int i = 0; i < Kit::numSlots; ++i)   // shown soft, medium, hard, mute, brush
        {
            const int layer = Kit::kLayerOrder[i];
            auto* b = layerButtons.add (new juce::TextButton (names[layer]));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->setColour (juce::TextButton::textColourOnId, bg);
            b->onClick = [this, layer] { start (layer); };
            addAndMakeVisible (b);
        }
        settingsButton.onClick = [] { GuideUI::showAudioSettings(); };
        inputBox.addItem (jp (u8"入力: 自動"), 1);
        inputBox.addItem (jp (u8"入力 1"), 2);
        inputBox.addItem (jp (u8"入力 2"), 3);
        inputBox.setSelectedId (1, juce::dontSendNotification);
        inputBox.onChange = [this] { proc.inputTap.useChannel.store (inputBox.getSelectedId() - 2); };
        inputBox.setTooltip (jp (u8"ギターをつないだ入力。自動ならギターが鳴っているほうを使います"));
        addAndMakeVisible (inputBox);
        closeButton.onClick = [this] { stop(); if (onClose) onClose(); };
        backButton.onClick = [this] { if (index > 0) { --index; capoWait = false; status = {}; statusColour = GuideUI::dim; refresh(); } };
        skipButton.onClick = [this] { if (tuning) { tuning = false; status = {}; refresh(); } else advance (false); };
        pauseButton.onClick = [this] { paused = ! paused; proc.inputTap.active.store (! paused && running); pauseButton.setButtonText (paused ? jp (u8"再開") : jp (u8"一時停止")); refresh(); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &settingsButton, &closeButton, &backButton, &skipButton, &pauseButton })
            addAndMakeVisible (c);
        settingsButton.setButtonText (jp (u8"入力の設定"));
        closeButton.setButtonText (jp (u8"閉じる"));
        backButton.setButtonText (jp (u8"← 1 つ戻る"));
        skipButton.setButtonText (jp (u8"飛ばす →"));
        pauseButton.setButtonText (jp (u8"一時停止"));
        setWantsKeyboardFocus (true);
    }

    ~GuidePanel() override { stop(); }

    bool isRunning() const { return running; }
    // tests: where files go, and a look inside
    juce::File baseDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("AIO Guitar");
    int stepIndex() const { return index; }
    bool isFinished() const { return finished; }
    bool isWaitingForCapo() const { return capoWait; }
    bool isTuning() const { return tuning; }
    juce::String statusText() const { return status; }
    const juce::StringArray& warningList() const { return warnings; }
    void pump() { timerCallback(); }   // what the timer does
    bool debugTakes = false;

    // a whole layer, in the README order
    void start (int slot)
    {
        beginSession (slot, Guide::buildSteps (slot), false);
    }

    // just this note again (down and up) - also adds a fret that was not recorded before
    void startSingle (int slot, int string, int fret)
    {
        std::vector<Guide::Step> st;
        st.push_back ({ slot, fret, string, false });
        st.push_back ({ slot, fret, string, true });
        beginSession (slot, st, true);
    }

    void stop()
    {
        stopTimer();
        if (running) commit();
        running = false;
        proc.inputTap.active.store (false);
        writer.reset();
        if (onHighlight) onHighlight (0, -1, false);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace GuideUI;
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 10.0f, 4.0f, 16.0f, 2.0f);
        g.setColour (text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("RECORD GUIDE" + (running ? juce::String (" - ") + layerName (slot) : juce::String()), juce::Rectangle<float> (22.0f, 6.0f, 400.0f, 24.0f), juce::Justification::centredLeft);

        auto body = getLocalBounds().reduced (24, 0).withTrimmedTop (48).withTrimmedBottom (58);
        if (! running && ! finished)
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (16.0f));
            g.drawFittedText (jp (u8"録る層を選ぶと始まります（上のボタン）。\n"
                                  u8"ギターをオーディオインターフェイスの Hi-Z（INST）入力につなぎ、エフェクトは外す。「入力の設定」でインターフェイスを選ぶ。\n"
                                  u8"1 音ずつ弾いて、鳴り終わるまで待つ（手でミュートしても OK）。自動で判定して次へ進みます。カポの付け替えだけ止まります。\n"
                                  u8"録り直したい 1 音は、下の表の ● を右クリック。"),
                              body, juce::Justification::centredLeft, 8);
            return;
        }
        if (finished)
        {
            g.setColour (ok);
            g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
            g.drawText (jp (u8"おわり！ ") + juce::String (doneCount) + jp (u8" 音を録りました"), body.removeFromTop (44), juce::Justification::centredLeft);
            g.setColour (text);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (jp (u8"保存しました: ") + savedTo, body.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (dim);
            juce::String w = warnings.isEmpty() ? jp (u8"気になる点はありませんでした。") : jp (u8"気になる点（録り直さなくても使えます）:\n・") + warnings.joinIntoString (jp (u8"\n・"));
            g.drawFittedText (w, body.reduced (0, 6), juce::Justification::topLeft, 8);
            return;
        }

        if (tuning)
        {
            auto big = body.removeFromTop (70);
            g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
            g.setColour (text);
            g.drawText (jp (u8"チューニング確認   開放 ") + juce::String (stringName (tuneString)) + jp (u8" 弦"), big, juce::Justification::centredLeft);
            g.setColour (dim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (juce::String (7 - tuneString) + " / 6", big, juce::Justification::centredRight);
            auto line = body.removeFromTop (34);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.setColour (statusColour);
            g.drawText (status.isNotEmpty() ? status : jp (u8"カポを外して、6 弦から 1 本ずつ弾いてください"), line, juce::Justification::centredLeft);
            body.removeFromTop (14);
            g.setColour (dim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText (jp (u8"・±20 セント以内なら次の弦へ進みます。ずれていたら何セントずれたか出るので、チューニングしてもう一回") + "\n"
                              + jp (u8"・急ぐときは「飛ばす →」で確認をまるごと飛ばせます（音程はあとで自動で直ります）"),
                              body.removeFromTop (60), juce::Justification::topLeft, 3);
            return;
        }

        const auto& st = steps[(size_t) index];
        // what to play, big
        auto big = body.removeFromTop (70);
        g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
        g.setColour (capoWait ? accent : text);
        juce::String what;
        if (st.slot == Kit::slotBrush) what = jp (u8"全弦ミュート  ") + (st.up ? jp (u8"↑ アップ") : jp (u8"↓ ダウン"));
        else what = (st.capo == 0 ? jp (u8"カポなし") : jp (u8"カポ ") + juce::String (st.capo)) + "   " + juce::String (stringName (st.string)) + jp (u8" 弦   ")
                  + (st.up ? jp (u8"↑ アップ") : jp (u8"↓ ダウン"));
        g.drawText (what, big, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (juce::String (index + 1) + " / " + juce::String ((int) steps.size()), big, juce::Justification::centredRight);

        // capo change / status
        auto line = body.removeFromTop (34);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        if (capoWait)
        {
            g.setColour (accent);
            g.drawText (st.capo == 0 ? jp (u8"カポを外して、上の音を弾けば再開します") : jp (u8"カポを ") + juce::String (st.capo) + jp (u8" フレットに付けて、上の音を弾けば再開します"),
                        line, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (statusColour);
            g.drawText (paused ? jp (u8"一時停止中") : (detector.isRecording() ? jp (u8"● 録音中…（鳴り終わるまで）") : (status.isNotEmpty() ? status : jp (u8"弾いてください"))),
                        line, juce::Justification::centredLeft);
        }

        // progress bar
        auto pb = body.removeFromTop (14).toFloat().reduced (0, 3);
        g.setColour (panelHi);
        g.fillRoundedRectangle (pb, 4.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (pb.withWidth (pb.getWidth() * (float) index / (float) juce::jmax (1, (int) steps.size())), 4.0f);

        // input meter
        body.removeFromTop (12);
        auto meter = body.removeFromTop (22);
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (jp (u8"入力"), meter.removeFromLeft (40), juce::Justification::centredLeft);
        auto mr = meter.removeFromLeft (juce::jmin (420, meter.getWidth() - 150)).toFloat().reduced (0, 5);
        g.setColour (panelHi);
        g.fillRoundedRectangle (mr, 3.0f);
        auto toX = [&] (float db) { return mr.getX() + mr.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (meterDb > -1.0f ? bad : (meterDb > -12.0f ? accent : ok));
        g.fillRoundedRectangle (mr.withRight (toX (meterDb)), 3.0f);
        g.setColour (text.withAlpha (0.6f));
        g.fillRect (toX (holdDb) - 1.0f, mr.getY(), 2.0f, mr.getHeight());
        g.setColour (dim);
        g.drawText (proc.inputTap.channels.load() == 0 ? jp (u8"入力が見つかりません →「入力の設定」")
                                                       : juce::String (holdDb, 1) + jp (u8" dB（ピークは -12〜-6 くらいが目安）  入力 ")
                                                         + juce::String (proc.inputTap.usedChannel.load() + 1),
                    meter.withTrimmedLeft (10), juce::Justification::centredLeft);

        // reminders
        body.removeFromTop (18);
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        juce::String tips = st.slot == Kit::slotBrush
            ? jp (u8"・左手で全部の弦を軽く押さえてミュートし、ジャッと 1 回ストローク") + "\n" + jp (u8"・1 回ずつ少し間を空ける")
            : jp (u8"・1 回だけ弾いて、鳴り終わるまで伸ばす（低い弦は 6〜8 秒）。手でミュートしたら次へ進みます") + "\n"
              + jp (u8"・ほかの弦は鳴らさない（前の弦は手でミュート）") + "\n"
              + jp (u8"・音程が少しずれていても大丈夫（あとで自動で直します）。半音以上ずれたら弦かカポを確認");
        if (st.slot == Kit::slotSoft)   tips << "\n" << jp (u8"・強さ: そっと（弦がやさしく鳴るくらい）");
        if (st.slot == Kit::slotMedium) tips << "\n" << jp (u8"・強さ: いつもコードやリフを弾くときの強さ");
        if (st.slot == Kit::slotHard)   tips << "\n" << jp (u8"・強さ: アクセントで思い切り");
        if (Kit::isOpenPicking (st.slot))
            tips << "\n" << jp (u8"・SOFT / MEDIUM / HARD でピック・弾く位置・ピックアップ・トーン・入力のゲインを変えない");
        tips << "\n" << jp (u8"・スペースキーで一時停止 / 再開");
        if (! warnings.isEmpty()) tips << "\n" << jp (u8"・気になる点 ") << warnings.size() << jp (u8" 件（最後にまとめて出します）");
        g.drawFittedText (tips, body.withTrimmedBottom (4), juce::Justification::topLeft, 10);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (30);
        closeButton.setBounds (top.removeFromRight (80));
        top.removeFromRight (6);
        settingsButton.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        inputBox.setBounds (top.removeFromRight (110));
        top.removeFromRight (16);
        for (int i = Kit::numSlots - 1; i >= 0; --i) { layerButtons[i]->setBounds (top.removeFromRight (i == 1 ? 84 : 66)); top.removeFromRight (4); }
        auto bottom = r.removeFromBottom (34);
        backButton.setBounds (bottom.removeFromLeft (130));
        bottom.removeFromLeft (8);
        pauseButton.setBounds (bottom.removeFromLeft (110));
        bottom.removeFromLeft (8);
        skipButton.setBounds (bottom.removeFromLeft (110));
    }

private:
    void beginSession (int newSlot, std::vector<Guide::Step> newSteps, bool single)
    {
        stop();
        slot = newSlot; steps = std::move (newSteps); singleMode = single;
        index = 0; doneCount = 0; finished = false; paused = false; capoWait = false;
        pending.clear(); warnings.clear(); status = {}; statusColour = GuideUI::dim;
        lastAcceptedNote = -1;
        pauseButton.setButtonText (GuideUI::jp (u8"一時停止"));
        for (int i = 0; i < Kit::numSlots; ++i) layerButtons[i]->setToggleState (Kit::kLayerOrder[i] == slot, juce::dontSendNotification);
        GuideUI::unmuteInput();
        sr = proc.getSampleRateNow();
        detector.prepare (sr, slot == Kit::slotBrush);
        proc.inputTap.clear();
        proc.inputTap.active.store (true);
        openBackup();
        running = true;
        tuning = ! singleMode && slot != Kit::slotBrush;   // open strings first: is the guitar in tune?
        tuneString = 6;
        if (! tuning && ! singleMode && steps.front().capo != 0) capoWait = true;
        refresh();
        startTimerHz (30);
        grabKeyboardFocus();
    }

    void openBackup()
    {
        auto dir = baseDir.getChildFile ("Recordings");
        dir.createDirectory();
        auto f = dir.getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S ") + GuideUI::layerName (slot) + ".wav");
        if (auto os = f.createOutputStream())
        {
            juce::WavAudioFormat wav;
            if (auto* w = wav.createWriterFor (os.get(), sr, 1, 24, {}, 0)) { os.release(); writer.reset (w); backupFile = f; }
        }
    }

    void timerCallback() override
    {
        if (running && ! finished)
        {
            // the sample rate was changed (e.g. in the input settings): start this note again at the new rate
            if (const double now = proc.getSampleRateNow(); std::abs (now - sr) > 1.0)
            {
                sr = now;
                proc.inputTap.clear();
                detector.prepare (sr, slot == Kit::slotBrush);
                writer.reset();
                openBackup();
                status = GuideUI::jp (u8"サンプルレートが変わったので、今の音をもう一回");
                statusColour = GuideUI::accent;
            }
            // part of the input was lost (the window was busy): this note again
            if (proc.inputTap.overflow.exchange (false))
            {
                proc.inputTap.clear();
                detector.prepare (sr, slot == Kit::slotBrush);
                status = GuideUI::jp (u8"途中が抜けたので、今の音をもう一回");
                statusColour = GuideUI::accent;
            }
        }
        // input level
        const float pk = proc.inputTap.peak.exchange (0.0f);
        const float db = juce::Decibels::gainToDecibels (pk, -100.0f);
        meterDb = juce::jmax (db, meterDb - 2.0f);
        holdDb = db > holdDb ? db : holdDb - 0.3f;

        // new audio -> backup file + take detector
        for (;;)
        {
            const int got = proc.inputTap.pull (chunk.data(), (int) chunk.size());
            if (got <= 0) break;
            if (writer != nullptr) { const float* chans[] { chunk.data() }; writer->writeFromFloatArrays (chans, 1, got); }
            if (! paused) detector.feed (chunk.data(), got);
        }
        while (! detector.finished.empty())
        {
            auto take = std::move (detector.finished.front());
            detector.finished.pop_front();
            if (running && ! finished && ! paused) judgeTake (take);
        }
        refresh();
    }

    void judgeTake (const std::vector<float>& take)
    {
        if (tuning)
        {
            const Guide::Step open { slot == Kit::slotMute ? Kit::slotSoft : slot, 0, tuneString, false };
            auto r = Guide::judge (take, sr, open, -1, 0.0f);
            if (! r.ok)
            {
                status = r.wrongNote ? GuideUI::jp (u8"開放の ") + juce::String (tuneString) + GuideUI::jp (u8" 弦を弾いてください（カポなし）")
                                     : juce::String (juce::CharPointer_UTF8 (u8"✗ ")) + r.reason;
                statusColour = GuideUI::bad;
                return;
            }
            const float c = r.sample.cents;
            if (std::abs (c) > 20.0f)
            {
                status = juce::String (tuneString) + GuideUI::jp (u8" 弦が ") + (c > 0 ? "+" : "") + juce::String (juce::roundToInt (c))
                       + (c > 0 ? GuideUI::jp (u8" セント高い（少し下げてもう一回）") : GuideUI::jp (u8" セント低い（少し上げてもう一回）"));
                statusColour = GuideUI::accent;
                return;
            }
            status = juce::String (tuneString) + GuideUI::jp (u8" 弦 OK（") + (c >= 0 ? "+" : "") + juce::String (juce::roundToInt (c)) + GuideUI::jp (u8" セント）");
            statusColour = GuideUI::ok;
            if (--tuneString < 1)
            {
                tuning = false;
                status = GuideUI::jp (u8"チューニング OK。録音を始めます");
                if (steps.front().capo != 0) capoWait = true;
            }
            refresh();
            return;
        }
        const auto& st = steps[(size_t) index];
        // the same note in the layers under / over this one (to warn when they sound alike)
        auto layerDb = [this, &st] (int layer)
        {
            float db = 0.0f;
            if (auto kit = proc.getKit())
                for (auto& s : kit->samples)
                    if (s.slot == layer && s.string == st.string && s.fret == st.capo && s.up == st.up && s.rawRms > 0)
                        db = juce::Decibels::gainToDecibels (s.rawRms);
            return db;
        };
        float belowDb = 0.0f, aboveDb = 0.0f;
        if (st.slot == Kit::slotHard)   { belowDb = layerDb (Kit::slotMedium); if (belowDb == 0.0f) belowDb = layerDb (Kit::slotSoft); }
        if (st.slot == Kit::slotMedium) { belowDb = layerDb (Kit::slotSoft); aboveDb = layerDb (Kit::slotHard); }
        const int prev = singleMode ? -1 : lastAcceptedNote;
        auto r = Guide::judge (take, sr, st, capoWait ? -1 : prev, belowDb, aboveDb);
        if (debugTakes)
        {
            juce::AudioBuffer<float> bb (1, (int) take.size());
            for (size_t i = 0; i < take.size(); ++i) bb.setSample (0, (int) i, take[i]);
            auto rr = Kit::slice (bb, sr, st.slot);
            std::cout << "      take " << take.size() / sr << " s -> " << (r.ok ? "OK" : r.reason) << "  [slice: " << rr.message << " n=" << rr.samples.size();
            for (auto& sm : rr.samples) std::cout << " len " << sm.audio.getNumSamples() / sm.sampleRate;
            std::cout << "]" << std::endl;
            if (rr.samples.empty())
            {
                const int fr = (int) (sr * 0.005);
                std::cout << "        env:";
                for (int i = 0; i < 20 && (i + 1) * fr <= (int) take.size(); ++i)
                {
                    double e = 0; for (int k = 0; k < fr; ++k) e += take[(size_t) (i * fr + k)] * take[(size_t) (i * fr + k)];
                    std::cout << " " << juce::roundToInt (10.0 * std::log10 (e / fr + 1e-14));
                }
                std::cout << std::endl;
            }
        }
        if (! r.ok)
        {
            if (capoWait && r.wrongNote) return;   // still changing the capo: wrong notes are fine
            status = juce::String (juce::CharPointer_UTF8 (u8"✗ ")) + r.reason;
            statusColour = GuideUI::bad;
            return;
        }
        for (auto& w : r.warnings) warnings.addIfNotAlreadyThere (w);
        pending.push_back (std::move (r.sample));
        lastAcceptedNote = st.expectedNote();
        ++doneCount;
        status = juce::String (juce::CharPointer_UTF8 (u8"✓ OK"));
        statusColour = GuideUI::ok;
        advance (true);
    }

    void advance (bool accepted)
    {
        juce::ignoreUnused (accepted);
        const int prevCapo = steps[(size_t) index].capo;
        ++index;
        capoWait = false;
        if (index >= (int) steps.size())
        {
            commit();
            finish();
            return;
        }
        if (steps[(size_t) index].capo != prevCapo && ! singleMode)
        {
            commit();   // a position is done: into the kit now (a crash later loses nothing)
            capoWait = true;
            lastAcceptedNote = -1;
        }
        refresh();
    }

    void commit()
    {
        if (pending.empty()) return;
        proc.putTakes (pending);
        pending.clear();
        save();
    }

    void save()
    {
        auto kit = proc.getKit();
        if (kit == nullptr) return;
        auto dir = baseDir;
        dir.createDirectory();
        auto f = dir.getChildFile (juce::File::createLegalFileName (kit->name) + ".aioguitar");
        if (proc.saveKit (f)) savedTo = f.getFullPathName();
    }

    void finish()
    {
        finished = true;
        running = false;
        stopTimer();
        proc.inputTap.active.store (false);
        writer.reset();
        if (onHighlight) onHighlight (0, -1, false);
        refresh();
    }

    void refresh()
    {
        if (onHighlight)
        {
            if (running && tuning) onHighlight (tuneString, 0, detector.isRecording());
            else if (running && ! finished && index < (int) steps.size())
                onHighlight (steps[(size_t) index].string, steps[(size_t) index].capo, detector.isRecording());
            else onHighlight (0, -1, false);
        }
        backButton.setEnabled (running && index > 0);
        skipButton.setEnabled (running);
        pauseButton.setEnabled (running);
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey && running) { pauseButton.triggerClick(); return true; }
        return false;
    }

    AIOGuitarProcessor& proc;
    juce::OwnedArray<juce::TextButton> layerButtons;
    juce::TextButton settingsButton, closeButton, backButton, skipButton, pauseButton;
    juce::ComboBox inputBox;
    Guide::TakeDetector detector;
    std::vector<Guide::Step> steps;
    std::vector<Kit::Sample> pending;
    std::vector<float> chunk = std::vector<float> (1 << 16);
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File backupFile;
    juce::StringArray warnings;
    juce::String status, savedTo;
    juce::Colour statusColour { GuideUI::dim };
    double sr = 48000.0;
    int slot = 0, index = 0, doneCount = 0, lastAcceptedNote = -1;
    bool running = false, finished = false, paused = false, capoWait = false, singleMode = false, tuning = false;
    int tuneString = 6;
    float meterDb = -100.0f, holdDb = -100.0f;
};
