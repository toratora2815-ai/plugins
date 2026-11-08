// AIO Guitar の数値テスト（MY GUITAR）。
// 疑似のライン録り（カポ 0/3/6/9/12 ＋ 1〜3 弦は 15、6→1 弦、各弦ダウン→アップ。わざと ±20 セントずらし、音量も ±6 dB ばらつかせる）、
// ブリッジミュート（6〜4 弦 × 0/3/6/9）、ブラッシングを WAV に書き出し → 読み込み → 次を確かめる:
//   切り分けの数、弦とフレットの割り当て、音程の測定精度、音量のそろい方、鳴らした音の音程、連打で同じ録音が続かないこと、
//   同じ弦の前の音が止まること、ミュートの層が使われること、NaN・音割れ・プチノイズ、状態の保存と復元、画面の PNG
// 使い方: RenderTest <出力フォルダ>    失敗があると終了コード 1
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  OK   " : "  FAIL ") << what << std::endl;
        if (! ok) ++failures;
    }

    double noteHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    // a plucked-string-like tone: harmonics with frequency-dependent decay + a pick click
    double inharm = 0.0002;
    void addPluck (std::vector<float>& x, int at, double sr, double hz, float level, double seconds, double decay, bool up, juce::Random& rng)
    {
        const int len = (int) (seconds * sr);
        const double bright = up ? 0.8 : 1.0;
        for (int h = 1; h <= 24; ++h)
        {
            const double f = hz * h * (1.0 + inharm * h * h);   // slight inharmonicity
            if (f > sr * 0.45) break;
            const double amp = level * bright * std::pow (0.72, h - 1) * (h == 1 ? 1.0 : 0.8);
            const double tau = decay / (1.0 + 0.35 * (h - 1));
            const double ph = rng.nextDouble() * 6.28;
            for (int i = 0; i < len && at + i < (int) x.size(); ++i)
            {
                const double t = i / sr;
                const double attack = juce::jmin (1.0, t / 0.002);
                x[(size_t) (at + i)] += (float) (amp * attack * std::exp (-t / tau) * std::sin (6.283185307 * f * t + ph));
            }
        }
        for (int i = 0; i < (int) (0.004 * sr) && at + i < (int) x.size(); ++i)
            x[(size_t) (at + i)] += level * 0.3f * (rng.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) i / (float) (0.004 * sr));
    }

    struct Truth { int string, fret; bool up; float cents, levelDb; };

    void writeMono (const juce::File& f, const std::vector<float>& x, double sr)
    {
        juce::AudioBuffer<float> b (1, (int) x.size());
        for (size_t i = 0; i < x.size(); ++i) b.setSample (0, (int) i, x[i]);
        f.deleteFile();
        if (auto stream = f.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0));
            if (w) { stream.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        }
    }

    // one take per (position, string, down/up), low string -> high string
    std::vector<Truth> makeRecording (const juce::File& f, double sr, bool mute, juce::Random& rng)
    {
        std::vector<Truth> truth;
        std::vector<float> x ((size_t) (sr * 400.0), 0.0f);
        int at = (int) (0.5 * sr);
        auto play = [&] (int s, int fret)
        {
            for (int k = 0; k < 2; ++k)
            {
                Truth t { s, fret, k == 1, (rng.nextFloat() * 2.0f - 1.0f) * 20.0f, (rng.nextFloat() * 2.0f - 1.0f) * 6.0f };
                const double hz = noteHz (kOpenString[s] + fret + t.cents / 100.0);
                const double len = mute ? 0.45 : (s >= 5 ? 4.5 : 3.0);
                addPluck (x, at, sr, hz, 0.25f * juce::Decibels::decibelsToGain (t.levelDb), len, mute ? 0.07 : 0.9 + 0.1 * s, k == 1, rng);
                truth.push_back (t);
                at += (int) ((len + (mute ? 0.4 : 1.0)) * sr);
            }
        };
        if (mute)
        {
            for (int pos : { 0, 3, 6, 9 })
                for (int s = 6; s >= 4; --s) play (s, pos);
        }
        else
        {
            for (int pos : { 0, 3, 6, 9, 12 })
                for (int s = 6; s >= 1; --s) play (s, pos);
            for (int s = 3; s >= 1; --s) play (s, 15);
        }
        x.resize ((size_t) at + (size_t) sr);
        writeMono (f, x, sr);
        return truth;
    }

    void makeBrushing (const juce::File& f, double sr, juce::Random& rng)
    {
        std::vector<float> x ((size_t) (sr * 8.0), 0.0f);
        for (int k = 0; k < 8; ++k)
        {
            const int at = (int) ((0.3 + 0.6 * k) * sr);
            for (int i = 0; i < (int) (0.2 * sr); ++i)
                x[(size_t) (at + i)] += 0.3f * (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / (0.03f * (float) sr));
        }
        writeMono (f, x, sr);
    }

    // YIN on a mono buffer section
    float pitchOf (const juce::AudioBuffer<float>& b, int start, double sr)
    {
        std::vector<float> x ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (0, i);
        return Kit::detail::yinPitch (x, start, (int) x.size(), sr);
    }

    struct Ev { double t; juce::MidiMessage m; };

    juce::AudioBuffer<float> render (AIOGuitarProcessor& proc, std::vector<Ev> ev, double total, double sr, int bs = 256)
    {
        juce::MidiBuffer all;
        for (auto& e : ev) all.addEvent (e.m, juce::jmax (0, (int) (e.t * sr)));
        const int totalSamples = (int) (total * sr);
        juce::AudioBuffer<float> out (2, totalSamples), block (2, bs);
        for (int pos = 0; pos < totalSamples; pos += bs)
        {
            const int n = juce::jmin (bs, totalSamples - pos);
            block.setSize (2, n, false, false, true);
            juce::MidiBuffer mb;
            for (const auto meta : all)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + n)
                    mb.addEvent (meta.getMessage(), meta.samplePosition - pos);
            proc.processBlock (block, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, n);
        }
        return out;
    }

    // Logic's transport, as the plugin sees it
    struct FakeHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0; bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (playing);
            p.setBpm (bpm);
            p.setPpqPosition (ppq);
            p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    };

    // events at beat positions while the host plays from `fromPpq`
    struct BeatEv { double beat; juce::MidiMessage m; };
    juce::AudioBuffer<float> renderHost (AIOGuitarProcessor& proc, FakeHead& head, std::vector<BeatEv> ev, double fromPpq, double beats, double sr, int bs = 256)
    {
        head.playing = true;
        const double spb = 60.0 / head.bpm * sr;
        const int total = (int) (beats * spb);
        juce::AudioBuffer<float> out (2, total), block (2, bs);
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = juce::jmin (bs, total - pos);
            head.ppq = fromPpq + pos / spb;
            block.setSize (2, n, false, false, true);
            juce::MidiBuffer mb;
            for (auto& e : ev)
            {
                const int at = (int) std::llround ((e.beat - fromPpq) * spb);
                if (at >= pos && at < pos + n) mb.addEvent (e.m, at - pos);
            }
            proc.processBlock (block, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, n);
        }
        head.playing = false;
        juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer none;
        proc.processBlock (b, none);   // a stopped block ends the pass
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);   // let the capture timer run
        return out;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& b, double sr)
    {
        file.deleteFile();
        if (auto stream = file.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
            if (w) { stream.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        }
    }

    void setP (AIOGuitarProcessor& proc, const char* id, float v)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i))) return false;
        return true;
    }

    float rmsDb (const juce::AudioBuffer<float>& b, double a, double e, double sr)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, (int) (a * sr), (int) ((e - a) * sr)), -200.0f);
    }

    // largest sample-to-sample jump relative to the local level (clicks)
    float maxJump (const juce::AudioBuffer<float>& b, int a, int e)
    {
        float j = 0;
        for (int i = juce::jmax (1, a); i < juce::jmin (b.getNumSamples(), e); ++i)
            j = juce::jmax (j, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        return j;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const double sr = 48000.0;
    juce::Random rng (1234);
    if (argc > 2) inharm = juce::String (argv[2]).getDoubleValue();   // string stiffness of the fake guitar

    std::cout << "== recordings ==" << std::endl;
    const auto softFile = outDir.getChildFile ("fake_soft.wav"), muteFile = outDir.getChildFile ("fake_mute.wav"), brushFile = outDir.getChildFile ("fake_brush.wav");
    const auto softTruth = makeRecording (softFile, 44100.0, false, rng);
    const auto muteTruth = makeRecording (muteFile, 44100.0, true, rng);
    makeBrushing (brushFile, 44100.0, rng);

    AIOGuitarProcessor proc;
    proc.prepareToPlay (sr, 256);
    std::cout << "soft:  " << proc.loadRecordingNow (Kit::slotSoft, softFile) << std::endl;
    std::cout << "mute:  " << proc.loadRecordingNow (Kit::slotMute, muteFile) << std::endl;
    std::cout << "brush: " << proc.loadRecordingNow (Kit::slotBrush, brushFile) << std::endl;
    auto kit = proc.getKit();

    // ---- slicing + strings
    std::vector<const Kit::Sample*> soft, mute;
    for (auto& s : kit->samples) { if (s.slot == Kit::slotSoft) soft.push_back (&s); if (s.slot == Kit::slotMute) mute.push_back (&s); }
    check (soft.size() == softTruth.size(), "soft picking: " + juce::String ((int) soft.size()) + " / " + juce::String ((int) softTruth.size()) + " notes cut");
    check (mute.size() == muteTruth.size(), "palm mute: " + juce::String ((int) mute.size()) + " / " + juce::String ((int) muteTruth.size()) + " notes cut");
    check (kit->count (Kit::slotBrush) == 8, "brushing: 8 strokes (" + juce::String (kit->count (Kit::slotBrush)) + ")");

    if (soft.size() != softTruth.size())
        for (size_t i = 0; i < soft.size(); ++i)
            std::cout << "    " << i << ": note " << soft[i]->rootNote << " str " << soft[i]->string << " fret " << soft[i]->fret
                      << " len " << soft[i]->audio.getNumSamples() / 44100.0 << " s" << (i < softTruth.size() ? "  truth " + juce::String (softTruth[i].string) + "/" + juce::String (softTruth[i].fret) : juce::String()) << std::endl;
    auto verify = [&] (const std::vector<const Kit::Sample*>& got, const std::vector<Truth>& truth, const char* name)
    {
        if (got.size() != truth.size()) return;
        int strOk = 0, upOk = 0;
        float worstCents = 0, levelLo = 1e9f, levelHi = -1e9f;
        for (size_t i = 0; i < got.size(); ++i)
        {
            if (got[i]->string == truth[i].string && got[i]->fret == truth[i].fret) ++strOk;
            if (got[i]->up == truth[i].up) ++upOk;
            const double trueHz = noteHz (kOpenString[truth[i].string] + truth[i].fret + truth[i].cents / 100.0);
            worstCents = juce::jmax (worstCents, (float) std::abs (1200.0 * std::log2 (got[i]->rootHz / trueHz)));
            const float lvl = juce::Decibels::gainToDecibels (got[i]->rawRms * got[i]->gain);
            levelLo = juce::jmin (levelLo, lvl); levelHi = juce::jmax (levelHi, lvl);
        }
        check (strOk == (int) got.size(), juce::String (name) + ": string + fret right for " + juce::String (strOk) + " / " + juce::String ((int) got.size()));
        check (upOk == (int) got.size(), juce::String (name) + ": down / up right for " + juce::String (upOk) + " / " + juce::String ((int) got.size()));
        const float lim = juce::String (name) == "mute" ? 5.0f : 3.0f;   // palm-muted notes are short
        check (worstCents < lim, juce::String (name) + ": pitch measured within " + juce::String (worstCents, 2) + " cents (limit " + juce::String (lim, 0) + ")");
        check (levelHi - levelLo < 1.0f, juce::String (name) + ": level spread after matching " + juce::String (levelHi - levelLo, 2) + " dB (limit 1, before ~12)");
    };
    verify (soft, softTruth, "soft");
    verify (mute, muteTruth, "mute");

    // ---- playing: every note E2..E6 in tune
    std::cout << "== playing ==" << std::endl;
    proc.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, 256), *std::make_unique<juce::MidiBuffer>());   // pick up the kit
    {
        float worst = 0; int worstNote = 0;
        for (int note = 40; note <= 88; ++note)
        {
            auto out = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, note, 0.6f) }, { 1.1, juce::MidiMessage::noteOff (1, note) } }, 1.2, sr);
            std::vector<float> x ((size_t) out.getNumSamples());
            for (int i = 0; i < out.getNumSamples(); ++i) x[(size_t) i] = out.getSample (0, i);
            const float f = Kit::detail::measurePitch (x, (int) (0.01 * sr), (int) (1.1 * sr), sr, false);   // same tuner-like measure
            const float c = f > 0 ? (float) std::abs (1200.0 * std::log2 (f / noteHz (note))) : 999.0f;
            if (c > worst) { worst = c; worstNote = note; }
        }
        check (worst < 5.0f, "every note E2..E6 in tune: worst " + juce::String (worst, 2) + " cents at " + juce::MidiMessage::getMidiNoteName (worstNote, true, true, 3));
    }

    // ---- round robin: the same note twice does not use the same take
    {
        auto a = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, 52, 0.6f) }, { 0.3, juce::MidiMessage::noteOff (1, 52) } }, 0.4, sr);
        auto b = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, 52, 0.6f) }, { 0.3, juce::MidiMessage::noteOff (1, 52) } }, 0.4, sr);
        double diff = 0, ref = 0;
        for (int i = (int) (0.02 * sr); i < (int) (0.25 * sr); ++i) { diff += std::pow (a.getSample (0, i) - b.getSample (0, i), 2); ref += std::pow (a.getSample (0, i), 2); }
        check (diff > ref * 0.05, "repeated note uses another take (difference " + juce::String (10.0 * std::log10 (diff / ref + 1e-12), 1) + " dB)");
    }

    // ---- one note per string: ch 1 = string 1, second note chokes the first
    {
        setP (proc, "stringMode", 1);
        auto out = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, 64, 0.6f) }, { 0.5, juce::MidiMessage::noteOn (1, 69, 0.6f) },
                                   { 1.2, juce::MidiMessage::noteOff (1, 64) }, { 1.2, juce::MidiMessage::noteOff (1, 69) } }, 1.4, sr);
        const float f = pitchOf (out, (int) (0.7 * sr), sr);
        check (std::abs (1200.0 * std::log2 (f / noteHz (69))) < 10.0, "same string: only the new note sounds (" + juce::String (f, 1) + " Hz, want " + juce::String (noteHz (69), 1) + ")");
        const float jump = maxJump (out, (int) (0.49 * sr), (int) (0.53 * sr)), level = out.getMagnitude (0, (int) (0.3 * sr), (int) (0.2 * sr));
        check (jump < level * 0.5f, "choke is click-free (jump " + juce::String (jump, 4) + " vs level " + juce::String (level, 3) + ")");
        // different strings ring together
        auto two = render (proc, { { 0.01, juce::MidiMessage::noteOn (6, 40, 0.6f) }, { 0.01, juce::MidiMessage::noteOn (1, 64, 0.6f) },
                                   { 1.0, juce::MidiMessage::noteOff (6, 40) }, { 1.0, juce::MidiMessage::noteOff (1, 64) } }, 1.2, sr);
        check (rmsDb (two, 0.5, 0.9, sr) > rmsDb (out, 1.0, 1.15, sr), "different strings ring together");
        setP (proc, "stringMode", 0);
    }

    // ---- palm mute keyswitch uses the recorded mute layer (short)
    {
        auto pm = render (proc, { { 0.0, juce::MidiMessage::noteOn (1, kKeyswitchLow + artPalmMute, 0.5f) }, { 0.001, juce::MidiMessage::noteOff (1, kKeyswitchLow + artPalmMute) },
                                  { 0.01, juce::MidiMessage::noteOn (1, 40, 0.6f) }, { 1.5, juce::MidiMessage::noteOff (1, 40) },
                                  { 1.6, juce::MidiMessage::noteOn (1, kKeyswitchLow + artNormal, 0.5f) }, { 1.601, juce::MidiMessage::noteOff (1, kKeyswitchLow + artNormal) } }, 1.7, sr);
        auto open = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, 40, 0.6f) }, { 1.5, juce::MidiMessage::noteOff (1, 40) } }, 1.7, sr);
        const float pmDrop = rmsDb (pm, 0.02, 0.1, sr) - rmsDb (pm, 0.6, 0.8, sr), openDrop = rmsDb (open, 0.02, 0.1, sr) - rmsDb (open, 0.6, 0.8, sr);
        check (pmDrop > openDrop + 15.0f, "palm mute decays fast (drop " + juce::String (pmDrop, 1) + " dB vs open " + juce::String (openDrop, 1) + " dB)");
    }

    // ---- brushing keys
    {
        auto br = render (proc, { { 0.01, juce::MidiMessage::noteOn (1, Kit::kBrushDownNote, 0.8f) }, { 0.2, juce::MidiMessage::noteOff (1, Kit::kBrushDownNote) },
                                  { 0.3, juce::MidiMessage::noteOn (1, Kit::kBrushUpNote, 0.8f) }, { 0.5, juce::MidiMessage::noteOff (1, Kit::kBrushUpNote) } }, 0.7, sr);
        check (rmsDb (br, 0.01, 0.1, sr) > -50.0f && rmsDb (br, 0.3, 0.4, sr) > -50.0f, "brushing C1 / C#1 sound");
    }

    // ---- riff: level, NaN, release clicks, bends (written for listening)
    {
        std::vector<Ev> ev;
        const double e8 = 60.0 / 140.0 / 2.0;
        for (int i = 0; i < 16; ++i)
        {
            const double t = i * e8;
            const bool pm = (i % 4) != 3;
            const int ks = kKeyswitchLow + (pm ? artPalmMute : artNormal);
            ev.push_back ({ t, juce::MidiMessage::noteOn (1, ks, 0.5f) });
            ev.push_back ({ t + 0.001, juce::MidiMessage::noteOff (1, ks) });
            for (int n : { 40, 47 }) { ev.push_back ({ t + 0.002, juce::MidiMessage::noteOn (1, n, pm ? 0.7f : 0.95f) }); ev.push_back ({ t + e8 * 0.9, juce::MidiMessage::noteOff (1, n) }); }
        }
        const double t2 = 16 * e8 + 0.1;
        ev.push_back ({ t2, juce::MidiMessage::noteOn (1, kKeyswitchLow + artBendFull, 0.5f) });
        ev.push_back ({ t2 + 0.001, juce::MidiMessage::noteOff (1, kKeyswitchLow + artBendFull) });
        ev.push_back ({ t2 + 0.002, juce::MidiMessage::noteOn (1, 69, 0.8f) });
        ev.push_back ({ t2 + 1.5, juce::MidiMessage::noteOff (1, 69) });
        auto out = render (proc, ev, t2 + 2.2, sr);
        const float peak = out.getMagnitude (0, out.getNumSamples());
        check (allFinite (out), "riff: no NaN / inf");
        check (peak < 1.0f && peak > 0.05f, "riff: peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS (no clipping, not silent)");
        const float bent = pitchOf (out, (int) ((t2 + 0.6) * sr), sr);
        check (std::abs (1200.0 * std::log2 (bent / noteHz (71))) < 15.0, "bend full: A4 goes up to B4 (" + juce::String (bent, 1) + " Hz)");
        writeWav (outDir.getChildFile ("riff.wav"), out, sr);
    }

    // ---- state save / restore keeps the recordings
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        AIOGuitarProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        auto k2 = other.getKit();
        check (k2 != nullptr && k2->samples.size() == kit->samples.size(), "state restore: " + juce::String (k2 ? (int) k2->samples.size() : 0) + " takes ("
               + juce::String ((int) (mb.getSize() / 1024)) + " KB in the project)");
        if (k2 != nullptr && ! k2->samples.empty())
            check (k2->samples[3].string == kit->samples[3].string && k2->samples[3].fret == kit->samples[3].fret, "state restore keeps string / fret");
    }

    // ================= SCORE =================
    std::cout << "== score ==" << std::endl;
    {
        FakeHead head;
        proc.setPlayHead (&head);
        auto& sc = proc.getScore();
        sc.clear(); proc.scoreEdited();
        auto ks = [] (int art) { return juce::MidiMessage::noteOn (1, kKeyswitchLow + art, (juce::uint8) 100); };
        // Logic's region: 4 notes, the 3rd one with a vibrato keyswitch
        std::vector<BeatEv> logic {
            { 1.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 90) }, { 1.9, juce::MidiMessage::noteOff (1, 57) },
            { 2.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90) }, { 2.9, juce::MidiMessage::noteOff (1, 60) },
            { 2.99, ks (artVibrato) }, { 3.0, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90) }, { 4.5, juce::MidiMessage::noteOff (1, 64) },
            { 4.99, ks (artNormal) }, { 5.0, juce::MidiMessage::noteOn (1, 69, (juce::uint8) 90) }, { 6.5, juce::MidiMessage::noteOff (1, 69) } };
        renderHost (proc, head, logic, 0.0, 8.0, sr);
        bool posOk = sc.notes.size() == 4;
        if (posOk)
        {
            const double want[] { 1.0, 2.0, 3.0, 5.0 }, len[] { 0.9, 0.9, 1.5, 1.5 };
            for (int i = 0; i < 4; ++i)
                posOk = posOk && std::abs ((double) sc.notes[(size_t) i].tick / Score::kTicks - want[i]) < 0.002
                              && std::abs ((double) sc.notes[(size_t) i].length / Score::kTicks - len[i]) < 0.01;
        }
        check (posOk, "capture: " + juce::String ((int) sc.notes.size()) + " notes at the right beats and lengths (want 4)");
        check (sc.notes.size() == 4 && sc.notes[2].logicArt == artVibrato && sc.notes[3].logicArt == artNormal, "capture: Logic's articulation (vibrato) is read");

        // plugin setting: 4th note (A4) bent a whole step
        if (sc.notes.size() == 4)
        {
            proc.pushUndo();
            Score::Override o; o.art = artBendFull;
            sc.setOverride (sc.notes[3], o);
            proc.scoreEdited();
            for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer none; proc.processBlock (b, none);   // table swap
            auto out = renderHost (proc, head, logic, 0.0, 8.0, sr);
            std::vector<float> x ((size_t) out.getNumSamples());
            for (int i = 0; i < out.getNumSamples(); ++i) x[(size_t) i] = out.getSample (0, i);
            const float f = Kit::detail::yinPitch (x, (int) ((5.0 + 0.8) * 0.5 * sr), (int) x.size(), sr);
            check (std::abs (1200.0 * std::log2 (f / noteHz (71))) < 20.0, "plugin's bend is applied on the next playback (" + juce::String (f, 1) + " Hz, want " + juce::String (noteHz (71), 1) + ")");
            check (sc.pendingCount() == 1, "the bend is marked as not yet in Logic (" + juce::String (sc.pendingCount()) + ")");

            // Logic now has it (the region dragged back with a keyswitch): the mark goes away
            auto logic2 = logic;
            logic2.push_back ({ 4.995, ks (artBendFull) });
            renderHost (proc, head, logic2, 0.0, 8.0, sr);
            check (sc.pendingCount() == 0, "once Logic has the bend, it is no longer pending (" + juce::String (sc.pendingCount()) + ")");

            // a plugin vibrato on note 2, then that note is moved in Logic: kept as orphan, not lost
            proc.pushUndo();
            o.art = artWideVibrato;
            sc.setOverride (sc.notes[1], o);
            proc.scoreEdited();
            auto moved = logic2;
            for (auto& e : moved) if (e.m.getNoteNumber() == 60) e.beat += 0.5;
            renderHost (proc, head, moved, 0.0, 8.0, sr);
            check (sc.notes.size() == 4 && sc.orphaned == 1, "moved note: score follows Logic, the plugin setting is kept as 1 orphan (" + juce::String (sc.orphaned) + ")");
            renderHost (proc, head, logic2, 0.0, 8.0, sr);
            check (sc.orphaned == 0 && sc.pendingCount() == 1, "note moved back: the setting applies again");
            check (proc.undo() && sc.pendingCount() == 0, "undo removes the vibrato setting");
        }

        // export -> import gives the same notes and articulations
        {
            proc.pushUndo();
            Score::Override o; o.art = artSlideIn;
            sc.setOverride (sc.notes[0], o);
            proc.scoreEdited();
            int firstBar = 0;
            auto mf = sc.exportMidi (0, firstBar);
            auto f = outDir.getChildFile ("export.mid");
            f.deleteFile();
            { juce::FileOutputStream fos (f); mf.writeTo (fos); }
            Score::Model m2;
            const bool ok = m2.importMidi (f, firstBar - 1);
            bool same = ok && m2.notes.size() == sc.notes.size();
            for (size_t i = 0; same && i < sc.notes.size(); ++i)
                same = std::abs (m2.notes[i].tick - sc.notes[i].tick) <= 2 && m2.notes[i].note == sc.notes[i].note
                       && m2.notes[i].logicArt == sc.effectiveArt (sc.notes[i]);
            check (same, "drag to Logic: the MIDI file has the same notes + articulations (bar " + juce::String (firstBar) + ")");
        }

        // PLAY IN SYNC: the score plays with no MIDI from Logic
        {
            setP (proc, "scoreMode", 1);
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer none; proc.processBlock (b, none);
            auto out = renderHost (proc, head, {}, 0.0, 8.0, sr);
            check (rmsDb (out, 0.0, 0.45, sr) < -55.0f && rmsDb (out, 0.55, 0.9, sr) > -45.0f, "play in sync: nothing new before beat 1 (only the last test's reverb tail), sounds at beat 1 ("
                   + juce::String (rmsDb (out, 0.55, 0.9, sr), 1) + " dB)");
            // start in the middle (bar jump / cycle): plays from there
            auto mid = renderHost (proc, head, {}, 2.5, 2.0, sr);
            check (rmsDb (mid, 0.30, 0.6, sr) > -45.0f && allFinite (mid), "play in sync from the middle (beat 2.5): the beat-3 note sounds");
            setP (proc, "scoreMode", 0);
        }

        // preview inside the plugin
        {
            proc.setPlayHead (nullptr);
            proc.startPreview (1.0);
            auto out = render (proc, {}, 1.2, sr);
            check (rmsDb (out, 0.05, 0.4, sr) > -45.0f, "preview button plays the score");
            proc.stopPreview();
            render (proc, {}, 0.1, sr);
        }

        // the score is saved with the project
        {
            juce::MemoryBlock mb;
            proc.getStateInformation (mb);
            AIOGuitarProcessor other;
            other.setStateInformation (mb.getData(), (int) mb.getSize());
            check (other.getScore().notes.size() == sc.notes.size() && other.getScore().overrides.size() == sc.overrides.size(),
                   "state restore keeps the score (" + juce::String ((int) other.getScore().notes.size()) + " notes, " + juce::String ((int) other.getScore().overrides.size()) + " settings)");
        }
    }

    // ================= EFFECTS =================
    std::cout << "== effects ==" << std::endl;
    {
        proc.setPlayHead (nullptr);
        auto riff = [] { return std::vector<Ev> { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 0.01, juce::MidiMessage::noteOn (1, 52, 0.8f) },
                                                  { 1.6, juce::MidiMessage::noteOff (1, 45) }, { 1.6, juce::MidiMessage::noteOff (1, 52) } }; };
        setP (proc, "revMix", 0.0f);
        auto clean = render (proc, riff(), 2.0, sr);
        setP (proc, "pdOn", 1); setP (proc, "pdGain", 6);
        render (proc, {}, 0.05, sr);
        auto drv = render (proc, riff(), 2.0, sr);
        setP (proc, "pdOn", 0); setP (proc, "wfOn", 1);
        render (proc, {}, 0.05, sr);
        auto fz = render (proc, riff(), 2.0, sr);
        setP (proc, "wfOn", 0);
        render (proc, {}, 0.05, sr);
        auto clean2 = render (proc, riff(), 2.0, sr);
        check (allFinite (drv) && allFinite (fz), "drive / fuzz: no NaN");
        auto crest = [&] (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (b.getMagnitude (0, (int) (0.1 * sr), (int) (0.8 * sr)) / b.getRMSLevel (0, (int) (0.1 * sr), (int) (0.8 * sr))); };
        check (crest (drv) < crest (clean) - 1.0f && crest (fz) < crest (clean) - 1.0f, "drive / fuzz squash the peaks (crest clean " + juce::String (crest (clean), 1)
               + " dB, drive " + juce::String (crest (drv), 1) + ", fuzz " + juce::String (crest (fz), 1) + ")");
        // after switching both off again the sound is the same as before (timing included)
        double diff = 0, ref = 0;
        for (int i = (int) (0.05 * sr); i < (int) (1.5 * sr); ++i) { ref += std::pow (clean.getSample (0, i), 2); }
        juce::ignoreUnused (diff);
        check (std::abs (rmsDb (clean, 0.1, 1.5, sr) - rmsDb (clean2, 0.1, 1.5, sr)) < 1.0f, "pedals off again: same level as before ("
               + juce::String (rmsDb (clean, 0.1, 1.5, sr), 1) + " / " + juce::String (rmsDb (clean2, 0.1, 1.5, sr), 1) + " dB)");

        // switching while a note rings: no click. Compared with the same note played with the pedal
        // ON all the time (a click = a sudden bend in the waveform that the always-on take does not have)
        {
            auto run = [&] (bool switching)
            {
                std::vector<Ev> ev { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 1.9, juce::MidiMessage::noteOff (1, 45) } };
                juce::MidiBuffer all; for (auto& e : ev) all.addEvent (e.m, (int) (e.t * sr));
                const int total = (int) (2.0 * sr), bs = 256;
                juce::AudioBuffer<float> out (2, total), block (2, bs);
                setP (proc, "pdGain", 2); setP (proc, "pdLevel", -6);
                setP (proc, "pdOn", switching ? 0 : 1);
                proc.prepareToPlay (sr, bs);   // same start for both takes
                for (int pos = 0; pos < total; pos += bs)
                {
                    if (switching && pos == (int) (0.6 * sr) / bs * bs) setP (proc, "pdOn", 1);
                    if (pos == (int) (1.2 * sr) / bs * bs) setP (proc, "pdOn", 0);
                    const int n = juce::jmin (bs, total - pos);
                    block.setSize (2, n, false, false, true);
                    juce::MidiBuffer mb;
                    for (const auto meta : all) if (meta.samplePosition >= pos && meta.samplePosition < pos + n) mb.addEvent (meta.getMessage(), meta.samplePosition - pos);
                    proc.processBlock (block, mb);
                    for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, n);
                }
                return out;
            };
            auto ref = run (false), sw = run (true);
            auto bend = [&] (const juce::AudioBuffer<float>& b, double t0, double t1)
            {
                float m = 0;
                for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i)
                    m = juce::jmax (m, std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2)));
                return m;
            };
            const float on = bend (sw, 0.58, 0.66), onRef = bend (ref, 0.58, 0.66);
            const float off = bend (sw, 1.18, 1.26), offRef = juce::jmax (bend (ref, 1.18, 1.26), bend (sw, 1.0, 1.15));
            check (on <= onRef * 1.3f && off <= offRef * 1.3f, "pedal on / off while playing: no click (bends " + juce::String (on, 4) + " vs always-on "
                   + juce::String (onRef, 4) + ", off " + juce::String (off, 4) + " vs " + juce::String (offRef, 4) + ")");
            writeWav (outDir.getChildFile ("pedal_switch.wav"), sw, sr);
        }

        // resting: nothing played -> effects and amp stop; a note wakes them at once
        {
            render (proc, {}, 2.5, sr);
            check (proc.fxSleeping.load(), "silence: effects + amp rest");
            auto wake = render (proc, { { 0.0, juce::MidiMessage::noteOn (1, 52, 0.8f) }, { 0.5, juce::MidiMessage::noteOff (1, 52) } }, 0.6, sr);
            check (! proc.fxSleeping.load() && rmsDb (wake, 0.0, 0.05, sr) > -50.0f, "a note wakes them at once (" + juce::String (rmsDb (wake, 0.0, 0.05, sr), 1) + " dB in the first 50 ms)");
        }

        // CPU: time per block with pedals off / on / resting
        {
            auto timeIt = [&] (bool playing)
            {
                juce::AudioBuffer<float> b (2, 256);
                juce::MidiBuffer m;
                if (playing) render (proc, { { 0.0, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 0.0, juce::MidiMessage::noteOn (1, 52, 0.8f) } }, 0.01, sr);
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < 2000; ++i) proc.processBlock (b, m);
                const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                if (playing) render (proc, { { 0.0, juce::MidiMessage::noteOff (1, 45) }, { 0.0, juce::MidiMessage::noteOff (1, 52) } }, 0.01, sr);
                return 100.0 * sec / (2000 * 256 / sr);
            };
            setP (proc, "pdOn", 0); setP (proc, "wfOn", 0);
            const double off = timeIt (true);
            setP (proc, "pdOn", 1); setP (proc, "wfOn", 1);
            const double on = timeIt (true);
            setP (proc, "pdOn", 0); setP (proc, "wfOn", 0);
            render (proc, {}, 3.0, sr);
            const double rest = timeIt (false);
            std::cout << "  CPU (48 kHz, block 256, 1 core): pedals off " << juce::String (off, 2) << " %, both on " << juce::String (on, 2)
                      << " %, resting " << juce::String (rest, 3) << " %" << std::endl;
            check (rest < off * 0.2, "resting costs almost nothing");
        }
        setP (proc, "revMix", 0.12f);
    }

    // ---- GUI snapshot
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto snap = [&] (const char* name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream fos (f);
            if (fos.openedOk()) { juce::PNGImageFormat fmt; fmt.writeImageToStream (img, fos); }
        };
        snap ("editor.png");
        // SCORE tab
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "SCORE") b->triggerClick();
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        snap ("editor_score.png");
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "EFFECTS") b->triggerClick();
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        snap ("editor_effects.png");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
