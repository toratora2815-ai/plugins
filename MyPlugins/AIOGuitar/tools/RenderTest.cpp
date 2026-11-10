// AIO Guitar の数値テスト（MY GUITAR）。
// 疑似のライン録り（カポ 0/3/6/9/12 ＋ 1〜3 弦は 15、6→1 弦、各弦ダウン→アップ。わざと ±20 セントずらし、音量も ±6 dB ばらつかせる）、
// ブリッジミュート（6〜4 弦 × 0/3/6/9）、ブラッシングを WAV に書き出し → 読み込み → 次を確かめる:
//   切り分けの数、弦とフレットの割り当て、音程の測定精度、音量のそろい方、鳴らした音の音程、連打で同じ録音が続かないこと、
//   同じ弦の前の音が止まること、ミュートの層が使われること、NaN・音割れ・プチノイズ、状態の保存と復元、画面の PNG
// 使い方: RenderTest <出力フォルダ>    失敗があると終了コード 1
#include "BuzzSynth.h"
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/RecordGuide.h"

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

// AMP probe: RenderTest --amp <in.wav> <out.wav> model gain bass mid treble presence master cabOn cab mic pos dist
// (to compare with Amp Designer: the same test signal through both, build/AmpLab/analyse_ad.py)
static int ampProbe (int argc, char** argv)
{
    if (argc < 15) { std::cout << "usage: --amp in out model gain bass mid treble presence master cabOn cab mic pos dist" << std::endl; return 2; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[2])));
    if (r == nullptr) return 3;
    juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    FXChain ch; FXParams fp;
    auto f = [&] (int i) { return (float) juce::String (argv[i]).getDoubleValue(); };
    fp.ampOn = f (4) >= 0.0f; fp.ampModel = juce::jmax (0, (int) f (4)); fp.ampGain = f (5); fp.ampBass = f (6); fp.ampMid = f (7); fp.ampTreble = f (8);
    fp.ampPresence = f (9); fp.ampMaster = f (10); fp.cabOn = f (11) > 0.5f; fp.cabType = (int) f (12); fp.cabMicType = (int) f (13);
    fp.cabPos = f (14); fp.cabDist = argc > 15 ? f (15) : 2.0f;
    ch.prepare (r->sampleRate, 512);
    for (int pos = 0; pos < b.getNumSamples(); pos += 512)
    {
        const int n = juce::jmin (512, b.getNumSamples() - pos);
        juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, n);
        ch.process (blk, n, fp);
    }
    // undo the oversampling latency so the output lines up with the input
    const int lat = ch.getLatencySamples();
    juce::AudioBuffer<float> o (2, b.getNumSamples());
    o.clear();
    for (int c = 0; c < 2; ++c) o.copyFrom (c, 0, b, c, lat, b.getNumSamples() - lat);
    juce::File out (argv[3]); out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), r->sampleRate, 2, 32, {}, 0));
        if (w) { os.release(); w->writeFromAudioSampleBuffer (o, 0, o.getNumSamples()); }
    }
    return 0;
}

// RenderTest --padtable: how loud the AMP makes the guitar's DI, for every model / GAIN / MASTER (prints AmpPad.h's table).
// The DI: single notes and down-stroked chords, every string -21 dB RMS (10-350 ms after it starts) like the kit's picking
static int padTable()
{
    const double sr = 48000.0;
    juce::Random rng (77);
    std::vector<float> x ((size_t) (sr * 16.0), 0.0f);
    std::vector<std::pair<int, int>> win;
    auto pluckAt = [&] (int at, double nn)
    {
        std::vector<float> one ((size_t) (sr * 0.9), 0.0f);
        addPluck (one, 0, sr, noteHz (nn), 0.3f, 0.9, 0.6, false, rng);
        double e = 0; const int a = (int) (0.01 * sr), b = (int) (0.35 * sr);
        for (int i = a; i < b; ++i) e += one[(size_t) i] * one[(size_t) i];
        const float g = juce::Decibels::decibelsToGain (-21.0f) / (float) std::sqrt (e / (b - a));
        for (size_t i = 0; i < one.size() && at + (int) i < (int) x.size(); ++i) x[(size_t) at + i] += one[i] * g;
    };
    int at = (int) (0.1 * sr);
    for (double nn : { 40.0, 45.0, 50.0, 55.0, 59.0, 64.0, 52.0, 57.0, 62.0, 67.0, 71.0, 76.0 })
    {
        pluckAt (at, nn); win.push_back ({ at, at + (int) (0.6 * sr) }); at += (int) (0.8 * sr);
    }
    for (auto chord : { std::vector<double> { 40, 47, 52, 56, 59, 64 }, std::vector<double> { 45, 52, 57, 61, 64 }, std::vector<double> { 43, 47, 50, 55, 59, 67 } })
    {
        for (size_t k = 0; k < chord.size(); ++k) pluckAt (at + (int) (k * 0.007 * sr), chord[k]);
        win.push_back ({ at, at + (int) (0.8 * sr) }); at += (int) (1.1 * sr);
    }
    auto rmsOver = [&] (const float* d, int shift)
    {
        double e = 0; long n = 0;
        for (auto [a, b] : win) for (int i = a; i < b; ++i) { e += (double) d[i + shift] * d[i + shift]; ++n; }
        return 10.0 * std::log10 (e / (double) n + 1e-30);
    };
    const double inDb = rmsOver (x.data(), 0);
    std::cout << "    // [model][gain 0, 2.5, 5, 7.5, 10][master 0, 2.5, 5, 7.5, 10]   (DI " << juce::String (inDb, 1) << " dB RMS)" << std::endl;
    std::cout << "    inline constexpr float db[5][5][5] {" << std::endl;
    for (int m = 0; m < ampsim::numModels; ++m)
    {
        std::cout << "        { // " << ampsim::modelNames()[m] << std::endl;
        for (int gi = 0; gi < 5; ++gi)
        {
            std::cout << "            { ";
            for (int mi = 0; mi < 5; ++mi)
            {
                FXChain ch; FXParams fp;
                fp.ampOn = true; fp.ampModel = m; fp.ampGain = gi * 2.5f; fp.ampMaster = mi * 2.5f; fp.cabOn = false;
                ch.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, (int) x.size());
                for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, x[(size_t) i]); b.setSample (1, i, x[(size_t) i]); }
                ch.process (b, b.getNumSamples(), fp);
                const double pad = juce::jlimit (-36.0, 12.0, inDb - rmsOver (b.getReadPointer (0), ch.getLatencySamples()));
                std::cout << juce::String (pad, 2) << "f" << (mi < 4 ? ", " : "");
            }
            std::cout << " }" << (gi < 4 ? "," : "") << std::endl;
        }
        std::cout << "        }" << (m < 4 ? "," : "") << std::endl;
    }
    std::cout << "    };" << std::endl;
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--amp") return ampProbe (argc, argv);
    if (argc > 1 && juce::String (argv[1]) == "--padtable") return padTable();
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
        std::vector<float> xs ((size_t) out.getNumSamples());
        for (int i = 0; i < out.getNumSamples(); ++i) xs[(size_t) i] = out.getSample (0, i);
        const float f = Kit::detail::measurePitch (xs, (int) (0.7 * sr), (int) (1.15 * sr), sr, false);   // tuner-like (YIN reads stiff strings sharp)
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
        setP (proc, "strumOn", 0);   // these compare pedal sounds, not strums
        setP (proc, "ampOn", 0); setP (proc, "cabOn", 0);   // the pedals alone (the amp would distort the clean sound too)
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
        setP (proc, "ampOn", 1); setP (proc, "cabOn", 1);
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
            setP (proc, "kitHuman", 0.0f);   // both takes identical apart from the switch
            auto ref = run (false), sw = run (true);
            setP (proc, "kitHuman", 0.5f);
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
        setP (proc, "strumOn", 1);
    }

    // ---- COTTON COMP (compressor in front of the drive / fuzz)
    std::cout << "== cotton comp ==" << std::endl;
    {
        // the DSP alone: a sine at a fixed level, gain measured once it has settled
        auto through = [&] (float comp, float level, float blend, float inDb, double hz, bool noise = false)
        {
            pedal::CottonCompDSP c;
            c.prepare (sr, 512, 2);
            c.setParams (comp, level, blend, true);
            const int total = (int) (1.5 * sr);
            juce::AudioBuffer<float> b (2, total);
            juce::Random nr (7);
            const float a = juce::Decibels::decibelsToGain (inDb);
            for (int i = 0; i < total; ++i)
            {
                const float v = noise ? a * 1.7f * (nr.nextFloat() * 2.0f - 1.0f) : a * (float) std::sin (6.283185307 * hz * i / sr);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            juce::AudioBuffer<float> in (b);
            for (int pos = 0; pos < total; pos += 512) { float* d[2] { b.getWritePointer (0, pos), b.getWritePointer (1, pos) }; c.process (d, 2, juce::jmin (512, total - pos)); }
            const int s0 = (int) (1.0 * sr), len = total - s0;
            return std::make_pair (juce::Decibels::gainToDecibels (b.getRMSLevel (0, s0, len) / juce::jmax (1.0e-12f, in.getRMSLevel (0, s0, len))), b);
        };
        // 1) squashes: inputs 24 dB apart come out much closer together
        const float lo = through (10, 0, 100, -30, 196).first - 30.0f + 0.0f, hi = through (10, 0, 100, -6, 196).first - 6.0f;
        juce::ignoreUnused (lo, hi);
        const float outLo = -30.0f + through (10, 0, 100, -30, 196).first, outHi = -6.0f + through (10, 0, 100, -6, 196).first;
        check (outHi - outLo < 9.0f, "COMP 10: inputs 24 dB apart come out " + juce::String (outHi - outLo, 1) + " dB apart");
        const float outLo5 = -30.0f + through (5, 0, 100, -30, 196).first, outHi5 = -6.0f + through (5, 0, 100, -6, 196).first;
        check (outHi5 - outLo5 > outHi - outLo + 3.0f && outHi5 - outLo5 < 20.0f, "COMP 5 squashes less (" + juce::String (outHi5 - outLo5, 1) + " dB apart)");
        // 2) the lift is capped at +20 dB, noise-level sound is not lifted
        float maxLift = -100;
        for (float inDb : { -58.0f, -55.0f, -50.0f, -45.0f, -40.0f }) maxLift = juce::jmax (maxLift, through (10, 0, 100, inDb, 330).first);
        check (maxLift <= 20.5f && maxLift > 15.0f, "lift at most +20 dB (" + juce::String (maxLift, 1) + " dB at most)");
        const float noiseLift = through (10, 0, 100, -80, 0, true).first, hum = through (10, 0, 100, -78, 60).first;
        check (noiseLift < 1.0f && hum < 1.0f, "hiss (-80 dBFS) and hum (-78 dBFS) are not lifted (" + juce::String (noiseLift, 1) + " / " + juce::String (hum, 1) + " dB)");
        // 3) COMP 0 is nearly transparent, BLEND 0 is the dry sound, LEVEL moves the output
        check (std::abs (through (0, 0, 100, -14, 440).first) < 1.0f, "COMP 0: about unity at -14 dBFS (" + juce::String (through (0, 0, 100, -14, 440).first, 2) + " dB)");
        check (std::abs (through (10, 0, 0, -30, 440).first) < 0.05f, "BLEND 0 = the dry sound");
        check (std::abs (through (5, 6, 100, -20, 440).first - through (5, 0, 100, -20, 440).first - 6.0f) < 0.1f, "LEVEL +6 dB adds 6 dB");
        // 4) low E keeps its shape (no ripple distortion from the detector): harmonic 2 and 3 far below
        {
            auto [g, b] = through (10, 0, 100, -10, 82.41);
            juce::ignoreUnused (g);
            auto bandAt = [&] (double f)
            {
                double re = 0, im = 0; const int s0 = (int) (1.0 * sr), len = (int) (0.4 * sr);
                for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (6.283185307 * i / len), x = b.getSample (0, s0 + i) * w;
                                                re += x * std::cos (6.283185307 * f * i / sr); im += x * std::sin (6.283185307 * f * i / sr); }
                return 10.0 * std::log10 (re * re + im * im + 1e-30);
            };
            const double h1 = bandAt (82.41), h2 = bandAt (164.8), h3 = bandAt (247.2);
            check (h2 < h1 - 40.0 && h3 < h1 - 40.0, "low E squashed hard stays clean (2nd " + juce::String (h2 - h1, 0) + " dB, 3rd " + juce::String (h3 - h1, 0) + " dB)");
        }
        // 4b) real hiss (gaussian) and hum at -70 dBFS RMS are not lifted
        {
            auto liftOf = [&] (bool hum)
            {
                pedal::CottonCompDSP c; c.prepare (sr, 512, 2); c.setParams (10, 0, 100, true);
                const int total = (int) (2.0 * sr);
                juce::AudioBuffer<float> b (2, total);
                juce::Random nr (11);
                const float a = juce::Decibels::decibelsToGain (-70.0f);
                for (int i = 0; i < total; ++i)
                {
                    float v;
                    if (hum) v = a * 1.41421f * (float) (std::sin (6.283185307 * 60.0 * i / sr) * 0.8 + std::sin (6.283185307 * 180.0 * i / sr) * 0.6);
                    else { float g = 0; for (int k = 0; k < 12; ++k) g += nr.nextFloat(); v = a * (g - 6.0f); }   // ~gaussian, RMS = a
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                juce::AudioBuffer<float> in (b);
                for (int pos = 0; pos < total; pos += 512) { float* d[2] { b.getWritePointer (0, pos), b.getWritePointer (1, pos) }; c.process (d, 2, juce::jmin (512, total - pos)); }
                const int s0 = (int) (1.0 * sr), len = total - s0;
                return juce::Decibels::gainToDecibels (b.getRMSLevel (0, s0, len) / in.getRMSLevel (0, s0, len));
            };
            const float hiss = liftOf (false), hum70 = liftOf (true);
            check (hiss <= 3.0f && hum70 <= 3.0f, "COMP 10: gaussian hiss / hum at -70 dBFS RMS lifted at most 3 dB (" + juce::String (hiss, 1) + " / " + juce::String (hum70, 1) + " dB)");
        }
        // 4c) a quiet note gets its lift from the start (no 1-2 s swell), a second pick does not jump out
        {
            auto pluckGain = [&] (bool second, bool compOn, float peakDb, std::vector<float>* outP = nullptr)
            {
                pedal::CottonCompDSP c; c.prepare (sr, 512, 2); c.setParams (10, 0, 100, true);
                const int total = (int) (3.0 * sr), at2 = (int) (1.2 * sr);
                juce::AudioBuffer<float> b (2, total);
                b.clear();
                auto add = [&] (int at)
                {
                    for (int i = at; i < total; ++i)
                    {
                        const double t = (i - at) / sr;
                        const double v = std::exp (-t / 0.9) * (std::sin (6.283185307 * 110.0 * t) + 0.5 * std::sin (6.283185307 * 220.0 * t + 1.0) + 0.3 * std::sin (6.283185307 * 330.0 * t))
                                         * juce::jmin (1.0, t / 0.002);
                        b.setSample (0, i, b.getSample (0, i) + (float) (v * juce::Decibels::decibelsToGain (peakDb) / 1.6));
                    }
                };
                add (0);
                if (second) add (at2);
                b.copyFrom (1, 0, b, 0, 0, total);
                juce::AudioBuffer<float> in (b);
                if (compOn)
                    for (int pos = 0; pos < total; pos += 512) { float* d[2] { b.getWritePointer (0, pos), b.getWritePointer (1, pos) }; c.process (d, 2, juce::jmin (512, total - pos)); }
                if (outP) { outP->resize ((size_t) total); for (int i = 0; i < total; ++i) (*outP)[(size_t) i] = b.getSample (0, i); }
                auto gainIn = [&] (double t0, double t1) { const int a0 = (int) (t0 * sr), n0 = (int) ((t1 - t0) * sr);
                                                           return juce::Decibels::gainToDecibels (b.getRMSLevel (0, a0, n0) / juce::jmax (1.0e-9f, in.getRMSLevel (0, a0, n0))); };
                return std::make_pair (gainIn (0.01, 0.05), gainIn (0.4, 0.8));
            };
            const auto q = pluckGain (false, true, -32.0f);
            check (q.second - q.first < 6.0f && q.first > 4.0f, "COMP 10, quiet note (peak -32 dBFS): lifted from the start (" + juce::String (q.first, 1) + " dB at 10-50 ms, "
                   + juce::String (q.second, 1) + " dB at 0.4-0.8 s)");
            std::vector<float> on2, off2;
            pluckGain (true, true, -14.0f, &on2); pluckGain (true, false, -14.0f, &off2);
            auto pk = [&] (const std::vector<float>& v, double t0, double t1) { float m = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) m = juce::jmax (m, std::abs (v[(size_t) i])); return juce::Decibels::gainToDecibels (m); };
            const float first = pk (on2, 0.0, 0.03), second = pk (on2, 1.2, 1.23), sustain = pk (on2, 1.0, 1.19), offSecond = pk (off2, 1.2, 1.23);
            check (std::abs (second - first) < 3.0f && second < offSecond + 3.0f && second - sustain < 11.0f,
                   "COMP 10: a second pick over a ringing note does not jump out (1st " + juce::String (first, 1) + ", 2nd " + juce::String (second, 1)
                   + ", ringing " + juce::String (sustain, 1) + ", comp off " + juce::String (offSecond, 1) + " dBFS)");
        }
        // 5) nothing odd with silence / full scale / DC
        {
            pedal::CottonCompDSP c; c.prepare (sr, 512, 2); c.setParams (10, 12, 100, true);
            juce::AudioBuffer<float> b (2, 4096);
            bool fine = true;
            for (float v : { 0.0f, 1.0f, -1.0f, 0.5f })
            {
                for (int i = 0; i < 4096; ++i) { const float x = (v == 1.0f && (i & 1)) ? -1.0f : v; b.setSample (0, i, x); b.setSample (1, i, x); }
                for (int k = 0; k < 8; ++k) { float* d[2] { b.getWritePointer (0, k * 512), b.getWritePointer (1, k * 512) }; c.process (d, 2, 512); }
                for (int i = 0; i < 4096; ++i) fine = fine && std::isfinite (b.getSample (0, i)) && std::abs (b.getSample (0, i)) < 8.0f;
            }
            check (fine, "silence / full scale / DC: finite and bounded");
        }

        // in the plugin: a note through the comp (ON vs OFF) - longer sustain, about the same loudness at LEVEL 0
        setP (proc, "strumOn", 0); setP (proc, "ampOn", 0); setP (proc, "cabOn", 0);
        setP (proc, "kitHuman", 0.0f);
        auto note = [] { return std::vector<Ev> { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 2.8, juce::MidiMessage::noteOff (1, 45) } }; };
        render (proc, {}, 0.3, sr);
        auto dry = render (proc, note(), 3.0, sr);
        setP (proc, "ccOn", 1); setP (proc, "ccComp", 7);
        render (proc, {}, 0.3, sr);
        auto wet = render (proc, note(), 3.0, sr);
        const float dryDrop = rmsDb (dry, 0.05, 0.3, sr) - rmsDb (dry, 2.0, 2.5, sr), wetDrop = rmsDb (wet, 0.05, 0.3, sr) - rmsDb (wet, 2.0, 2.5, sr);
        check (wetDrop < dryDrop - 6.0f, "comp: the note sustains longer (decay 0.2 s -> 2.2 s: " + juce::String (dryDrop, 1) + " dB dry, " + juce::String (wetDrop, 1) + " dB with comp)");
        check (std::abs (rmsDb (wet, 0.05, 1.5, sr) - rmsDb (dry, 0.05, 1.5, sr)) < 4.0f, "comp at LEVEL 0: about the same loudness ("
               + juce::String (rmsDb (dry, 0.05, 1.5, sr), 1) + " / " + juce::String (rmsDb (wet, 0.05, 1.5, sr), 1) + " dB)");
        check (allFinite (wet), "comp in the plugin: no NaN");
        writeWav (outDir.getChildFile ("comp_dry.wav"), dry, sr);
        writeWav (outDir.getChildFile ("comp_on.wav"), wet, sr);

        // switching the comp while a note rings: no click
        {
            auto run = [&] (bool switching)
            {
                std::vector<Ev> ev { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 1.9, juce::MidiMessage::noteOff (1, 45) } };
                juce::MidiBuffer all; for (auto& e : ev) all.addEvent (e.m, (int) (e.t * sr));
                const int total = (int) (2.0 * sr), bs = 256;
                juce::AudioBuffer<float> out (2, total), block (2, bs);
                setP (proc, "ccOn", switching ? 0 : 1);
                proc.prepareToPlay (sr, bs);
                for (int pos = 0; pos < total; pos += bs)
                {
                    if (switching && pos == (int) (0.6 * sr) / bs * bs) setP (proc, "ccOn", 1);
                    if (pos == (int) (1.2 * sr) / bs * bs) setP (proc, "ccOn", 0);
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
            const float on = bend (sw, 0.58, 0.66), onRef = juce::jmax (bend (ref, 0.58, 0.66), bend (sw, 0.45, 0.57));
            const float off = bend (sw, 1.18, 1.26), offRef = juce::jmax (bend (ref, 1.18, 1.26), bend (sw, 1.0, 1.15));
            check (on <= onRef * 1.3f && off <= offRef * 1.3f, "comp on / off while playing: no click (bends " + juce::String (on, 4) + " vs " + juce::String (onRef, 4)
                   + ", off " + juce::String (off, 4) + " vs " + juce::String (offRef, 4) + ")");
        }
        setP (proc, "ccOn", 0); setP (proc, "ccComp", 5);
        setP (proc, "kitHuman", 0.5f);
        setP (proc, "ampOn", 1); setP (proc, "cabOn", 1);
        setP (proc, "strumOn", 1);
    }

    // ---- EFFECTS order: COMP / DRIVE / FUZZ / AMP in any order, CAB and OUTPUT always last
    std::cout << "== effects order ==" << std::endl;
    {
        proc.setPlayHead (nullptr);
        setP (proc, "strumOn", 0); setP (proc, "kitHuman", 0.0f);
        auto riff = [] { return std::vector<Ev> { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 0.01, juce::MidiMessage::noteOn (1, 52, 0.8f) },
                                                  { 0.7, juce::MidiMessage::noteOn (1, 57, 0.9f) }, { 1.4, juce::MidiMessage::noteOff (1, 45) },
                                                  { 1.4, juce::MidiMessage::noteOff (1, 52) }, { 1.4, juce::MidiMessage::noteOff (1, 57) } }; };
        // every note alternates its down / up take: a spare pass between two takes makes them use the same recordings
        auto take = [&] (int order)
        {
            setP (proc, "fxChain", (float) order); proc.prepareToPlay (sr, 256);
            auto out = render (proc, riff(), 1.6, sr);
            render (proc, riff(), 1.6, sr);   // spare
            return out;
        };
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            float d = 0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) d = juce::jmax (d, std::abs (a.getSample (c, i) - b.getSample (c, i)));
            return d;
        };
        const int ampFirst = rig::indexOf ({ rig::blkAmp, rig::blkComp, rig::blkDrive, rig::blkFuzz, rig::blkTrem });
        const int driveAfter = rig::indexOf ({ rig::blkComp, rig::blkFuzz, rig::blkAmp, rig::blkDrive, rig::blkTrem });
        const int compLast = rig::indexOf ({ rig::blkDrive, rig::blkFuzz, rig::blkAmp, rig::blkComp, rig::blkTrem });

        // names, the round trip index <-> order, moving one block
        bool namesOk = rig::orderNames().size() == rig::kNumOrders && rig::orderNames()[0] == "Comp > Drive > Fuzz > Amp > Trem";
        for (int k = 0; k < rig::kNumOrders; ++k) namesOk = namesOk && rig::indexOf (rig::orderAt (k)) == k;
        namesOk = namesOk && rig::moved (rig::orderAt (0), rig::blkAmp, 1) == rig::Order { rig::blkComp, rig::blkAmp, rig::blkDrive, rig::blkFuzz, rig::blkTrem };
        check (namesOk, "24 orders: names, index round trip, moving one block");
        bool padOk = true;
        for (int m = 0; m < ampsim::numModels; ++m) for (float g : { 0.0f, 3.3f, 10.0f }) for (float ms : { 0.0f, 6.1f, 10.0f })
        {
            const float v = ampPad::padDb (m, g, ms);
            padOk = padOk && std::isfinite (v) && v >= -36.0f && v <= 12.0f;
        }
        check (padOk, "AMP pad table: finite, -36..+12 dB");

        // everything off: every order is the same sound, sample for sample (each block is only its delay)
        for (auto* id : { "ccOn", "pdOn", "wfOn", "ampOn", "cabOn" }) setP (proc, id, 0);
        const auto offA = take (rig::kDefault), offB = take (rig::kNumOrders - 1), offA2 = take (rig::kDefault);
        const float dOrder = maxDiff (offA, offB), dSame = maxDiff (offA, offA2);   // dSame: two takes of the very same setting
        check (dOrder <= dSame * 1.01f + 1.0e-6f, "everything off: the order changes nothing (" + juce::String (dOrder, 8) + ", the same order twice "
               + juce::String (dSame, 8) + ")");
        // the amp on, the pedals off but placed after it: turned down and back up = the same as the amp last
        setP (proc, "ampOn", 1); setP (proc, "cabOn", 1); setP (proc, "ampModel", 4); setP (proc, "ampGain", 6);
        const auto ampLast = take (rig::kDefault), ampFront = take (ampFirst);
        const float dPad = juce::Decibels::gainToDecibels (maxDiff (ampLast, ampFront) / juce::jmax (1.0e-9f, ampLast.getMagnitude (0, 0, ampLast.getNumSamples())), -200.0f);
        check (dPad < -80.0f, "AMP first, pedals off after it: the same as AMP last (" + juce::String (dPad, 1) + " dB)");
        check (proc.getLatencySamples() == [&] { setP (proc, "fxChain", 0); proc.prepareToPlay (sr, 256); return proc.getLatencySamples(); }(),
               "the reported latency is the same for every order (" + juce::String (proc.getLatencySamples()) + " samples)");

        // drive after the amp: about as loud as before it (the amp's output is brought back to guitar level in between)
        setP (proc, "ampModel", 3); setP (proc, "ampGain", 5); setP (proc, "pdOn", 1); setP (proc, "pdGain", 4); setP (proc, "pdLevel", -6);
        const auto dBefore = take (rig::kDefault), dAfter = take (driveAfter);
        const float lb = rmsDb (dBefore, 0.05, 1.3, sr), la = rmsDb (dAfter, 0.05, 1.3, sr);
        // (after the amp its LEVEL adds straight to the output; before it the amp squashes it: up to ~6 dB louder is right)
        check (std::abs (la - lb) < 8.0f, "PORCH DRIVE after the AMP: within 8 dB of before it (" + juce::String (lb, 1) + " / " + juce::String (la, 1) + " dB)");
        // the comp after a clean amp still works on guitar-sized levels: a soft and a loud note come out closer together
        {
            setP (proc, "pdOn", 0); setP (proc, "ampModel", 0); setP (proc, "ampGain", 3); setP (proc, "ccComp", 6);
            auto twoNotes = [&] (bool compOn)
            {
                setP (proc, "ccOn", compOn ? 1 : 0); setP (proc, "fxChain", (float) compLast); proc.prepareToPlay (sr, 256);
                std::vector<Ev> ev { { 0.01, juce::MidiMessage::noteOn (1, 52, 0.3f) }, { 0.75, juce::MidiMessage::noteOff (1, 52) },
                                     { 0.8, juce::MidiMessage::noteOn (1, 57, 1.0f) }, { 1.5, juce::MidiMessage::noteOff (1, 57) } };
                auto out = render (proc, ev, 1.6, sr);
                render (proc, ev, 1.6, sr);   // spare
                return (float) (rmsDb (out, 0.9, 1.4, sr) - rmsDb (out, 0.1, 0.6, sr));
            };
            const float apart = twoNotes (false), apartComp = twoNotes (true);
            check (apartComp < apart - 3.0f, "COTTON COMP after a clean AMP: a soft and a loud note come out closer (" + juce::String (apart, 1) + " -> "
                   + juce::String (apartComp, 1) + " dB apart)");
            setP (proc, "ccOn", 0); setP (proc, "ampModel", 3); setP (proc, "ampGain", 5);
        }

        // every order with everything on: sound, no NaN, never over 0 dBFS
        for (auto* id : { "ccOn", "pdOn", "wfOn" }) setP (proc, id, 1);
        setP (proc, "ccComp", 5); setP (proc, "pdGain", 4); setP (proc, "pdLevel", -6);
        bool allOk = true; float lo = 1e9f, hi = -1e9f; juce::String bad, levels;
        for (int k = 0; k < rig::kNumOrders; ++k)
        {
            const auto out = take (k);
            const float pk = out.getMagnitude (0, 0, out.getNumSamples()), lv = rmsDb (out, 0.05, 1.3, sr);
            if (! allFinite (out) || pk > 1.0f || lv < -45.0f) { allOk = false; bad << k << " "; }
            lo = juce::jmin (lo, lv); hi = juce::jmax (hi, lv);
        }
        check (allOk, "all 24 orders, every block on: sound, no NaN, never over 0 dBFS" + (bad.isEmpty() ? juce::String() : " (bad: " + bad + ")"));
        check (hi - lo < 15.0f, "levels over the 24 orders stay within 15 dB (" + juce::String (lo, 1) + " .. " + juce::String (hi, 1) + " dB)");

        // reordering while a note rings: 5 ms down, swap (clean), 20 ms silent, 8 ms up - no click (compared with both orders played steadily)
        {
            auto run = [&] (int from, int to, bool switching)
            {
                setP (proc, "fxChain", (float) from);
                proc.prepareToPlay (sr, 256);
                std::vector<Ev> ev { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.8f) }, { 1.4, juce::MidiMessage::noteOff (1, 45) } };
                juce::MidiBuffer all; for (auto& e : ev) all.addEvent (e.m, (int) (e.t * sr));
                const int total = (int) (1.5 * sr), bs = 256;
                juce::AudioBuffer<float> out (2, total), block (2, bs);
                for (int pos = 0; pos < total; pos += bs)
                {
                    if (switching && pos == (int) (0.6 * sr) / bs * bs) setP (proc, "fxChain", (float) to);
                    const int n = juce::jmin (bs, total - pos);
                    block.setSize (2, n, false, false, true);
                    juce::MidiBuffer mb;
                    for (const auto meta : all) if (meta.samplePosition >= pos && meta.samplePosition < pos + n) mb.addEvent (meta.getMessage(), meta.samplePosition - pos);
                    proc.processBlock (block, mb);
                    for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, n);
                }
                return out;
            };
            auto bend = [&] (const juce::AudioBuffer<float>& b, double t0, double t1)
            {
                float m = 0;
                for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i)
                    m = juce::jmax (m, std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2)));
                return m;
            };
            float worst = 0; juce::String txt;
            for (auto [from, to] : { std::pair<int, int> { rig::kDefault, ampFirst }, { ampFirst, driveAfter }, { compLast, rig::kDefault } })
            {
                const auto sw = run (from, to, true), a = run (from, from, false), b = run (to, to, false);
                const float r = bend (sw, 0.58, 0.66) / juce::jmax (bend (a, 0.58, 0.66), bend (b, 0.58, 0.66), 1.0e-6f);
                worst = juce::jmax (worst, r);
                txt << juce::String (r, 2) << " ";
                writeWav (outDir.getChildFile ("order_switch_" + juce::String (from) + "_" + juce::String (to) + ".wav"), sw, sr);
                writeWav (outDir.getChildFile ("order_steady_" + juce::String (from) + ".wav"), a, sr);
                writeWav (outDir.getChildFile ("order_steady_" + juce::String (to) + ".wav"), b, sr);
            }
            check (worst < 1.3f, "reordering while a note rings: no click (bend vs steady " + txt + ")");
        }

        // reordering while resting: the next note starts in the new order at once (no fade in its first 30 ms)
        {
            setP (proc, "fxChain", (float) rig::kDefault);
            proc.prepareToPlay (sr, 256);
            render (proc, {}, 1.0, sr);                       // rests
            setP (proc, "fxChain", (float) driveAfter);
            render (proc, {}, 0.1, sr);                       // still resting: taken at once
            const auto got = render (proc, riff(), 1.6, sr);
            render (proc, riff(), 1.6, sr);   // spare: the same takes again below
            const auto want = take (driveAfter);
            check (std::abs (rmsDb (got, 0.01, 0.04, sr) - rmsDb (want, 0.01, 0.04, sr)) < 0.5f, "reordered while resting: the next note starts in the new order ("
                   + juce::String (rmsDb (got, 0.01, 0.04, sr), 1) + " / " + juce::String (rmsDb (want, 0.01, 0.04, sr), 1) + " dB)");
        }

        // a project from 0.2.0 with "Fuzz > Drive" opens as Comp > Fuzz > Drive > Amp
        {
            AIOGuitarProcessor p5;
            p5.prepareToPlay (sr, 256);
            juce::MemoryBlock mb; p5.getStateInformation (mb);
            juce::MemoryInputStream in (mb, false); in.readInt(); in.readInt();
            auto tree = juce::ValueTree::readFromStream (in);
            for (auto c : tree) if (c["id"].toString() == "fxChain") { c.setProperty ("id", "pedalOrder", nullptr); c.setProperty ("value", 1.0f, nullptr); }
            juce::MemoryBlock old;
            { juce::MemoryOutputStream mos (old, false); mos.writeInt (0x47494f41); mos.writeInt (2); tree.writeToStream (mos); juce::ValueTree ("NOKIT").writeToStream (mos); }
            p5.setStateInformation (old.getData(), (int) old.getSize());
            const int got = (int) p5.apvts.getRawParameterValue ("fxChain")->load();
            check (got == rig::fromOldPedalOrder (true), "0.2.0 project with Fuzz > Drive: opens as " + rig::orderNames()[got]);
        }

        for (auto* id : { "ccOn", "pdOn", "wfOn" }) setP (proc, id, 0);
        setP (proc, "fxChain", (float) rig::kDefault); setP (proc, "ampModel", 3); setP (proc, "ampGain", 4); setP (proc, "pdGain", 4);
        setP (proc, "ccComp", 5); setP (proc, "kitHuman", 0.5f); setP (proc, "strumOn", 1);
        proc.prepareToPlay (sr, 256);
    }

    // ---- MEDIUM layer: velocity -> layer, no level step at the hand-overs
    std::cout << "== medium layer ==" << std::endl;
    {
        // three recordings of the same notes: medium 6 dB louder, hard 12 dB louder and brighter
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (softFile));
        juce::AudioBuffer<float> sb ((int) rd->numChannels, (int) rd->lengthInSamples);
        rd->read (&sb, 0, sb.getNumSamples(), 0, true, true);
        auto variant = [&] (const char* name, float db, float bright)
        {
            std::vector<float> x ((size_t) sb.getNumSamples());
            float prev = 0;
            for (int i = 0; i < sb.getNumSamples(); ++i) { const float v = sb.getSample (0, i); x[(size_t) i] = juce::Decibels::decibelsToGain (db) * (v + bright * (v - prev)); prev = v; }
            auto f = outDir.getChildFile (name);
            writeMono (f, x, rd->sampleRate);
            return f;
        };
        AIOGuitarProcessor p3;
        p3.prepareToPlay (sr, 256);
        p3.loadRecordingNow (Kit::slotSoft, softFile);
        p3.loadRecordingNow (Kit::slotMedium, variant ("fake_medium.wav", 6.0f, 0.3f));
        p3.loadRecordingNow (Kit::slotHard, variant ("fake_hard.wav", 12.0f, 0.8f));
        auto k3 = p3.getKit();
        check (k3->count (Kit::slotMedium) == (int) softTruth.size(), "medium picking: " + juce::String (k3->count (Kit::slotMedium)) + " / " + juce::String ((int) softTruth.size()) + " notes cut");
        auto L = [&] (int vel) { return Kit::layerForVelocity (*k3, (float) vel / 127.0f); };
        check (L (1) == Kit::slotSoft && L (60) == Kit::slotSoft && L (61) == Kit::slotMedium && L (100) == Kit::slotMedium && L (101) == Kit::slotHard && L (127) == Kit::slotHard,
               "velocity 1-60 soft, 61-100 medium, 101-127 hard");
        // only medium (+ palm mute): every velocity plays medium, never the palm mute
        {
            Kit::GuitarKit only;
            for (auto& s : k3->samples) if (s.slot == Kit::slotMedium) only.samples.push_back (s);
            for (auto& s : kit->samples) if (s.slot == Kit::slotMute) only.samples.push_back (s);
            check (Kit::layerForVelocity (only, 0.1f) == Kit::slotMedium && Kit::layerForVelocity (only, 1.0f) == Kit::slotMedium && Kit::pickingLayers (only) == 1,
                   "a kit with only MEDIUM: every velocity plays medium (not the palm mute)");
            Kit::GuitarKit sh;
            for (auto& s : k3->samples) if (s.slot != Kit::slotMedium) sh.samples.push_back (s);
            check (Kit::layerForVelocity (sh, 78.0f / 127.0f) == Kit::slotSoft && Kit::layerForVelocity (sh, 80.0f / 127.0f) == Kit::slotHard, "SOFT + HARD only: hard from velocity 79-80 as before");
        }
        // the layers sit at one level: going one velocity step over a hand-over is not a jump
        setP (p3, "strumOn", 0); setP (p3, "ampOn", 0); setP (p3, "cabOn", 0); setP (p3, "kitHuman", 0.0f);
        auto lvl = [&] (int vel)
        {
            render (p3, {}, 0.5, sr);
            auto o = render (p3, { { 0.01, juce::MidiMessage::noteOn (1, 52, (juce::uint8) vel) }, { 0.6, juce::MidiMessage::noteOff (1, 52) } }, 0.7, sr);
            return rmsDb (o, 0.02, 0.5, sr);
        };
        const float s60 = lvl (60), m61 = lvl (61), m100 = lvl (100), h101 = lvl (101), h127 = lvl (127), s20 = lvl (20);
        check (std::abs (m61 - s60) < 1.5f && std::abs (h101 - m100) < 1.5f, "no level step at the hand-overs (60->61 " + juce::String (m61 - s60, 1) + " dB, 100->101 "
               + juce::String (h101 - m100, 1) + " dB; recorded 6 / 6 dB apart)");
        check (h127 > s20 + 6.0f, "loudness still follows velocity (20: " + juce::String (s20, 1) + " dB, 127: " + juce::String (h127, 1) + " dB)");
        // palm mute keeps its level when SOFT is added after MEDIUM (the recommended order)
        {
            AIOGuitarProcessor pm; pm.prepareToPlay (sr, 256);
            pm.loadRecordingNow (Kit::slotMedium, outDir.getChildFile ("fake_medium.wav"));
            pm.loadRecordingNow (Kit::slotMute, muteFile);
            float before = 0, after = 0;
            for (auto& smp : pm.getKit()->samples) if (smp.slot == Kit::slotMute) { before = smp.gain; break; }
            pm.loadRecordingNow (Kit::slotSoft, softFile);
            for (auto& smp : pm.getKit()->samples) if (smp.slot == Kit::slotMute) { after = smp.gain; break; }
            check (std::abs (juce::Decibels::gainToDecibels (after / before)) < 0.1f, "adding SOFT after MEDIUM leaves the palm mute level alone ("
                   + juce::String (juce::Decibels::gainToDecibels (after / before), 2) + " dB)");
        }
        // a project saved before the AMP page was rebuilt: channel Lead -> Modern High Gain, cabinet 4x12 Closed stays 4x12 Closed
        {
            AIOGuitarProcessor pm; pm.prepareToPlay (sr, 256);
            juce::MemoryBlock mb; pm.getStateInformation (mb);
            juce::MemoryInputStream mis (mb, false);
            mis.readInt(); const int ver = mis.readInt();
            auto tree = juce::ValueTree::readFromStream (mis);
            auto kitT = juce::ValueTree::readFromStream (mis);
            auto scoreT = juce::ValueTree::readFromStream (mis);
            for (int i = tree.getNumChildren(); --i >= 0;)
            {
                auto c = tree.getChild (i);
                const auto id = c["id"].toString();
                if (id == "ampModel" || id == "cabMicType") tree.removeChild (i, nullptr);
                if (id == "cabType") c.setProperty ("value", 0.0f, nullptr);   // old 0 = 4x12 Closed
            }
            juce::ValueTree oc ("PARAM"); oc.setProperty ("id", "ampChannel", nullptr); oc.setProperty ("value", 2.0f, nullptr); tree.appendChild (oc, nullptr);
            juce::MemoryBlock old;
            { juce::MemoryOutputStream mos (old, false); mos.writeInt (0x47494f41); mos.writeInt (ver); tree.writeToStream (mos); kitT.writeToStream (mos); scoreT.writeToStream (mos); }
            AIOGuitarProcessor p2; p2.prepareToPlay (sr, 256);
            p2.setStateInformation (old.getData(), (int) old.getSize());
            const int mdl = (int) p2.apvts.getRawParameterValue ("ampModel")->load(), cab = (int) p2.apvts.getRawParameterValue ("cabType")->load();
            check (mdl == 4 && cab == 2, "old project: channel Lead -> Modern High Gain, cabinet 4x12 Closed kept (" + juce::String (mdl) + ", " + juce::String (cab) + ")");
        }
        // the output never goes over 0 dBFS (clean amp all the way up, OUTPUT +12)
        {
            setP (p3, "ampOn", 1); setP (p3, "cabOn", 1); setP (p3, "ampModel", 0); setP (p3, "ampGain", 10); setP (p3, "ampMaster", 10); setP (p3, "outGain", 12);
            auto o = render (p3, { { 0.01, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 127) }, { 0.01, juce::MidiMessage::noteOn (1, 52, (juce::uint8) 127) },
                                   { 0.8, juce::MidiMessage::noteOff (1, 40) }, { 0.8, juce::MidiMessage::noteOff (1, 52) } }, 1.0, sr);
            check (o.getMagnitude (0, o.getNumSamples()) <= 1.0f, "output never over 0 dBFS (clean, GAIN / MASTER 10, OUTPUT +12: peak " + juce::String (juce::Decibels::gainToDecibels (o.getMagnitude (0, o.getNumSamples())), 2) + " dBFS)");
            setP (p3, "outGain", 0);
            // after playing loud through a high-gain amp, the tail dies away and the effects rest (no tiny tone left running)
            setP (p3, "ampModel", 4);
            render (p3, { { 0.01, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 127) }, { 0.6, juce::MidiMessage::noteOff (1, 40) } }, 0.8, sr);
            render (p3, {}, 4.0, sr);
            check (p3.fxSleeping.load(), "after a loud high-gain note the amp comes to rest");
            {
                auto first = [&] (int fromModel)
                {
                    setP (p3, "ampModel", (float) fromModel); render (p3, {}, 3.0, sr);
                    setP (p3, "ampModel", 1); render (p3, {}, 0.1, sr);   // changed while resting
                    auto o = render (p3, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) }, { 0.3, juce::MidiMessage::noteOff (1, 45) } }, 0.4, sr);
                    render (p3, {}, 3.0, sr);
                    if (std::getenv ("DBG")) std::cout << "      from " << fromModel << ": " << rmsDb (o, 0.0, 0.005, sr) << " " << rmsDb (o, 0.005, 0.015, sr) << " "
                                                       << rmsDb (o, 0.015, 0.03, sr) << " " << rmsDb (o, 0.05, 0.1, sr) << " " << rmsDb (o, 0.1, 0.3, sr) << std::endl;
                    return rmsDb (o, 0.005, 0.015, sr) - rmsDb (o, 0.05, 0.1, sr);   // the start against the same note later (takes alternate)
                };
                // each note alternates down / up takes: a throw-away note in between so both cases play the same take
                auto spare = [&] { render (p3, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) }, { 0.2, juce::MidiMessage::noteOff (1, 45) } }, 0.3, sr); };
                const float changed = first (4); spare();
                const float same = first (1); spare();
                const float changed2 = first (4); spare();
                const float same2 = first (1);
                if (std::getenv ("DBG")) std::cout << "      resting: changed " << changed << " / " << changed2 << ", same " << same << " / " << same2 << std::endl;
                check (std::abs (changed - same) < 1.5f, "model changed while resting: the next note starts right away (start vs later " + juce::String (changed, 1) + " / " + juce::String (same, 1) + " dB)");
            }
            // OUTPUT and a cabinet IR changed while resting: the next note starts exactly as set
            {
                auto spare = [&] { render (p3, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) }, { 0.2, juce::MidiMessage::noteOff (1, 45) } }, 0.3, sr); };
                auto noteNow = [&] { auto o = render (p3, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) }, { 0.3, juce::MidiMessage::noteOff (1, 45) } }, 0.4, sr);
                                     render (p3, {}, 3.0, sr); return o; };
                setP (p3, "outGain", 0); render (p3, {}, 3.0, sr);
                auto ref = noteNow(); spare();
                setP (p3, "outGain", -12); render (p3, {}, 0.2, sr);      // changed while resting
                auto quiet = noteNow(); spare();
                const float d0 = rmsDb (quiet, 0.0, 0.03, sr) - rmsDb (ref, 0.0, 0.03, sr), d1 = rmsDb (quiet, 0.1, 0.3, sr) - rmsDb (ref, 0.1, 0.3, sr);
                check (std::abs (d0 - d1) < 0.5f && std::abs (d1 + 12.0f) < 0.5f, "OUTPUT changed while resting: the first 30 ms already at the new level ("
                       + juce::String (d0, 1) + " / " + juce::String (d1, 1) + " dB)");
                setP (p3, "outGain", 0);
                // an IR loaded while resting
                juce::AudioBuffer<float> irb (1, 2400);
                for (int i = 0; i < 2400; ++i) irb.setSample (0, i, (float) (std::exp (-i / 150.0) * std::sin (i * 0.21)));
                auto irFile = outDir.getChildFile ("test_ir.wav");
                { std::vector<float> v (2400); for (int i = 0; i < 2400; ++i) v[(size_t) i] = irb.getSample (0, i); writeMono (irFile, v, 48000.0); }
                setP (p3, "ampModel", 0); setP (p3, "ampGain", 3); render (p3, {}, 3.0, sr);
                p3.setNonRealtime (true);   // this test renders much faster than real time, like a bounce: then it waits for the IR
                const auto err = p3.loadCabIR (irFile);
                render (p3, {}, 0.3, sr);                                     // still resting: the IR gets ready meanwhile
                auto first = noteNow(); spare();
                auto later = noteNow(); spare();
                juce::ignoreUnused (later);
                auto again = noteNow();   // same take as "first" (one note in between)
                const float a = rmsDb (first, 0.0, 0.1, sr), b = rmsDb (again, 0.0, 0.1, sr);
                p3.setNonRealtime (false);
                check (err.isEmpty() && std::abs (a - b) < 0.5f, "IR loaded while resting: the first note already through the IR (" + juce::String (a, 1) + " / " + juce::String (b, 1) + " dB)");
                setP (p3, "cabType", 2); render (p3, {}, 3.0, sr);
            }
            setP (p3, "ampGain", 4); setP (p3, "ampMaster", 5); setP (p3, "ampModel", 3);
        }
        // the guide records 66 medium notes, like soft
        check (Guide::buildSteps (Kit::slotMedium).size() == 66, "RECORD GUIDE: MEDIUM is 66 notes (" + juce::String ((int) Guide::buildSteps (Kit::slotMedium).size()) + ")");
        // state keeps the medium layer
        juce::MemoryBlock mb3; p3.getStateInformation (mb3);
        AIOGuitarProcessor p4; p4.prepareToPlay (sr, 256); p4.setStateInformation (mb3.getData(), (int) mb3.getSize());
        auto k4 = p4.getKit();
        check (k4 != nullptr && k4->count (Kit::slotMedium) == k3->count (Kit::slotMedium), "state restore keeps the MEDIUM layer");
    }

    // ================= STRUM =================
    std::cout << "== strum ==" << std::endl;
    {
        FakeHead head;
        proc.setPlayHead (&head);
        setP (proc, "ampOn", 0); setP (proc, "cabOn", 0);
        setP (proc, "kitHuman", 0.0f); setP (proc, "scoreMode", 0); setP (proc, "strumOn", 1); setP (proc, "strumSpeed", 7.0f);
        auto& sc = proc.getScore();
        const int chord[] { 40, 47, 52, 56, 59, 64 };   // open E major, strings 6..1
        auto chordAt = [&] (double beat, double len)
        {
            std::vector<BeatEv> ev;
            for (int nn : chord) { ev.push_back ({ beat, juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100) }); ev.push_back ({ beat + len, juce::MidiMessage::noteOff (1, nn) }); }
            return ev;
        };
        // envelope (1 ms RMS) and when it gets to 10 % / 90 % of its peak in the first 120 ms after `t`
        auto spread = [&] (const juce::AudioBuffer<float>& b, double t)
        {
            const int a = (int) (t * sr), w = (int) (0.001 * sr);
            std::vector<float> env;
            for (int i = a; i < a + (int) (0.12 * sr); i += w) env.push_back (b.getRMSLevel (0, i, w));
            const float pk = *std::max_element (env.begin(), env.end());
            int i10 = 0, i90 = 0;
            while (i10 < (int) env.size() && env[(size_t) i10] < 0.1f * pk) ++i10;
            while (i90 < (int) env.size() && env[(size_t) i90] < 0.9f * pk) ++i90;
            return (double) (i90 - i10);   // ms
        };
        // which note started when (sample clock) and with which take direction
        struct Hit { int note, string, up; juce::int64 at; };
        std::vector<Hit> hits;
        proc.testNoteLog = [&] (int note, int string, int up, juce::int64 at) { hits.push_back ({ note, string, up, at }); };
        auto orderOf = [&] (juce::String& txt)
        {
            std::vector<Hit> h = hits;
            std::sort (h.begin(), h.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
            txt.clear();
            for (auto& x : h) txt << x.string << (x.up == 1 ? "^" : (x.up == 0 ? "v" : "")) << "@" << juce::String ((x.at - h.front().at) / sr * 1000.0, 1) << " ";
            return h;
        };
        auto isDown = [] (const std::vector<Hit>& h) { if (h.size() < 2) return false; for (size_t i = 1; i < h.size(); ++i) if (h[i].string >= h[i - 1].string) return false; return true; };
        auto isUp   = [] (const std::vector<Hit>& h) { if (h.size() < 2) return false; for (size_t i = 1; i < h.size(); ++i) if (h[i].string <= h[i - 1].string) return false; return true; };
        auto takesMatch = [] (const std::vector<Hit>& h, int dir) { for (auto& x : h) if (x.up != dir) return false; return true; };

        sc.clear(); proc.scoreEdited();
        juce::AudioBuffer<float> b0 (2, 256); juce::MidiBuffer none0; proc.processBlock (b0, none0);
        hits.clear();
        auto down = renderHost (proc, head, chordAt (1.0, 1.5), 0.0, 4.0, sr);   // beat 1: AUTO = down
        const double sDown = spread (down, 0.5);
        juce::String downTxt; auto downHits = orderOf (downTxt);
        setP (proc, "strumOn", 0);
        auto block = renderHost (proc, head, chordAt (1.0, 1.5), 0.0, 4.0, sr);
        const double sBlock = spread (block, 0.5);
        setP (proc, "strumOn", 1);
        check (sDown > sBlock + 15.0, "strum spreads the chord: rise " + juce::String (sDown, 0) + " ms (block chord " + juce::String (sBlock, 0) + " ms)");
        check (downHits.size() == 6 && isDown (downHits) && takesMatch (downHits, 0), "down-stroke on the beat: strings 6 -> 1, down takes (" + downTxt + ")");
        const double span = downHits.size() == 6 ? (downHits.back().at - downHits.front().at) / sr * 1000.0 : 0.0;
        check (span > 25.0 && span < 45.0, "down-stroke across 6 strings takes " + juce::String (span, 1) + " ms (7 ms a string)");

        hits.clear();
        auto up = renderHost (proc, head, chordAt (1.5, 1.0), 0.0, 4.0, sr);   // beat 1.5 (the "and"): AUTO = up
        juce::String upTxt; auto upHits = orderOf (upTxt);
        check (upHits.size() == 4 && isUp (upHits) && takesMatch (upHits, 1), "up-stroke on the 'and': strings 1 -> 4 only, up takes (" + upTxt + ")");
        // only the top 4 strings: no low E (82 Hz) in an up-stroke
        auto band = [&] (const juce::AudioBuffer<float>& b, double t0, double f)
        {
            juce::dsp::IIR::Filter<float> bp;
            bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, f, 8.0);
            double e = 0;
            for (int i = (int) (t0 * sr); i < (int) ((t0 + 0.3) * sr); ++i) { const float y = bp.processSample (b.getSample (0, i)); e += y * y; }
            return 10.0 * std::log10 (e + 1e-20);
        };
        check (band (up, 0.8, 82.4) < band (down, 0.55, 82.4) - 15.0, "up-stroke plays the top 4 strings only (low E "
               + juce::String (band (up, 0.8, 82.4) - band (down, 0.55, 82.4), 1) + " dB vs down)");

        // the score still has Logic's timing (only the sound is spread): both chords in one playback
        {
            auto both = chordAt (1.0, 0.4);
            for (auto& e : chordAt (1.5, 1.0)) both.push_back (e);
            renderHost (proc, head, both, 0.0, 4.0, sr);
        }
        bool onGrid = sc.notes.size() >= 6;
        for (auto& nt : sc.notes) onGrid = onGrid && (std::abs ((double) nt.tick / Score::kTicks - 1.0) < 0.002 || std::abs ((double) nt.tick / Score::kTicks - 1.5) < 0.002);
        check (onGrid && sc.notes.size() == 12, "SCORE keeps Logic's timing and every note (" + juce::String ((int) sc.notes.size()) + " / 12 notes on beats 1 / 1.5)");

        // SCORE: flip the beat-1 chord to up by hand -> the next playback strums it up
        sc.clear(); proc.scoreEdited();
        renderHost (proc, head, chordAt (1.0, 1.5), 0.0, 4.0, sr);
        proc.pushUndo();
        for (auto& nt : sc.notes) { Score::Override o = sc.overrideFor (nt); o.strum = 1; sc.setOverride (nt, o); }
        proc.scoreEdited();
        juce::AudioBuffer<float> b1 (2, 256); juce::MidiBuffer none1; proc.processBlock (b1, none1);
        hits.clear();
        renderHost (proc, head, chordAt (1.0, 1.5), 0.0, 4.0, sr);
        juce::String flTxt; auto flHits = orderOf (flTxt);
        check (isUp (flHits) && takesMatch (flHits, 1), "SCORE up-arrow on a down-beat chord: played as an up-stroke (" + flTxt + ")");

        // dragged back to Logic: D#1 before the chord, read back as Logic's direction
        {
            int firstBar = 0;
            auto mf = sc.exportMidi (0, firstBar);
            auto f = outDir.getChildFile ("strum.mid");
            f.deleteFile(); { juce::FileOutputStream fos (f); mf.writeTo (fos); }
            Score::Model m2;
            m2.importMidi (f, firstBar - 1);
            bool ok = m2.notes.size() == sc.notes.size();
            for (auto& nt : m2.notes) ok = ok && nt.logicStrum == 1;
            check (ok, "drag to Logic: the up-stroke goes out as D#1 and comes back (" + juce::String ((int) m2.notes.size()) + " notes)");
            // Logic now plays it with D#1: the plugin's setting is no longer needed
            auto ev = chordAt (1.0, 1.5);
            ev.push_back ({ 0.995, juce::MidiMessage::noteOn (1, Score::kStrumUpKey, (juce::uint8) 100) });
            ev.push_back ({ 0.998, juce::MidiMessage::noteOff (1, Score::kStrumUpKey) });
            hits.clear();
            renderHost (proc, head, ev, 0.0, 4.0, sr);
            check (sc.pendingCount() == 0, "with D#1 in Logic the up-stroke is no longer pending (" + juce::String (sc.pendingCount()) + ")");
            juce::String kTxt; auto kHits = orderOf (kTxt);
            check (isUp (kHits), "D#1 from Logic plays an up-stroke (" + kTxt + ")");
        }

        // down then up, legato (each chord held right up to the next): the up-stroke's top strings keep ringing
        {
            sc.clear(); proc.scoreEdited();
            auto ev = chordAt (1.0, 0.5);
            for (auto& e : chordAt (1.5, 0.5)) ev.push_back (e);
            auto legato = renderHost (proc, head, ev, 0.0, 3.0, sr);
            auto upAlone = renderHost (proc, head, chordAt (1.5, 0.5), 0.0, 3.0, sr);
            // high E (330 Hz) 60 - 200 ms after the up-stroke, compared with the up-stroke played alone
            const double d = band (legato, 0.81, 329.6) - band (upAlone, 0.81, 329.6);
            check (d > -6.0, "down then up (legato): the up-stroke's top string keeps ringing (" + juce::String (d, 1) + " dB vs alone)");
        }

        // a chord played by hand: the notes come 0-20 ms apart (low to high) -> still a strum
        {
            sc.clear(); proc.scoreEdited();
            auto rolled = [&] (double beat)
            {
                std::vector<BeatEv> ev;
                for (int k = 0; k < 6; ++k)
                {
                    const double b = beat + (k * 0.004) * 2.0;   // 4 ms apart at 120 bpm (1 beat = 0.5 s)
                    ev.push_back ({ b, juce::MidiMessage::noteOn (1, chord[k], (juce::uint8) 100) });
                    ev.push_back ({ beat + 1.2, juce::MidiMessage::noteOff (1, chord[k]) });
                }
                return ev;
            };
            hits.clear();
            renderHost (proc, head, rolled (1.0), 0.0, 3.0, sr);
            juce::String rTxt; auto rHits = orderOf (rTxt);
            double minGap = 1e9;
            for (size_t k = 1; k < rHits.size(); ++k) minGap = juce::jmin (minGap, (rHits[k].at - rHits[k - 1].at) / sr * 1000.0);
            check (rHits.size() == 6 && isDown (rHits) && minGap > 4.5, "hand-played chord (notes 4 ms apart): strummed down, strings at least "
                   + juce::String (minGap, 1) + " ms apart (" + rTxt + ")");
            hits.clear();
            auto uHand = renderHost (proc, head, rolled (1.5), 0.0, 3.0, sr);
            juce::String uTxt; auto uHits = orderOf (uTxt);
            double uGap = 1e9;
            int top4 = 0;
            for (size_t k = 1; k < uHits.size(); ++k) uGap = juce::jmin (uGap, (uHits[k].at - uHits[k - 1].at) / sr * 1000.0);
            for (auto& h : uHits) if (h.string >= 1 && h.string <= 4) ++top4;
            check (top4 == 4 && uGap > 3.0 && takesMatch (uHits.size() > 1 ? std::vector<Hit> (uHits.begin() + 1, uHits.end()) : uHits, 1),
                   "hand-played chord on the 'and': the top 4 strings spread with up takes (" + uTxt + ")");
            // the low strings came first and could not wait: they stop quickly once the higher ones are in
            const float lowE = (float) (band (uHand, 0.84, 82.4) - band (down, 0.59, 82.4)), lowB = (float) (band (uHand, 0.84, 123.5) - band (down, 0.59, 123.5));
            check (lowE < -15.0f && lowB < -15.0f, "hand-played up-stroke (low string first): strings 6 and 5 do not ring on (low E "
                   + juce::String (lowE, 1) + " dB, B2 " + juce::String (lowB, 1) + " dB vs the down-stroke)");
            // the strummer's "stop this string" message coming from outside (CC 102 on channel 16) does nothing
            {
                hits.clear();
                std::vector<BeatEv> ev { { 1.0, juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100) }, { 1.6, juce::MidiMessage::controllerEvent (16, GuitarSynth::kChokeCC, 103) },
                                         { 2.6, juce::MidiMessage::noteOff (1, 55) } };
                auto cc = renderHost (proc, head, ev, 0.0, 3.0, sr);
                const float before = rmsDb (cc, 0.74, 0.79, sr), after = rmsDb (cc, 0.81, 0.86, sr);
                check (after > before - 3.0f, "CC 102 on channel 16 from outside does not stop a string (" + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB)");
            }
            hits.clear();
            // two separate single notes 60 ms apart are not a chord
            renderHost (proc, head, { { 1.0, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) }, { 1.12, juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) },
                                      { 1.5, juce::MidiMessage::noteOff (1, 64) }, { 1.5, juce::MidiMessage::noteOff (1, 67) } }, 0.0, 3.0, sr);
            juce::String sTxt; auto sHits = orderOf (sTxt);
            check (sHits.size() == 2 && std::abs ((sHits[1].at - sHits[0].at) / sr * 1000.0 - 60.0) < 1.0, "notes 60 ms apart stay where they are (" + sTxt + ")");
        }

        // brushing stops the ringing chord
        {
            sc.clear(); proc.scoreEdited();
            auto ev = chordAt (1.0, 2.5);
            ev.push_back ({ 2.0, juce::MidiMessage::noteOn (1, Kit::kBrushDownNote, (juce::uint8) 100) });
            ev.push_back ({ 2.1, juce::MidiMessage::noteOff (1, Kit::kBrushDownNote) });
            auto br = renderHost (proc, head, ev, 0.0, 4.0, sr);
            auto ring = renderHost (proc, head, chordAt (1.0, 2.5), 0.0, 4.0, sr);
            check (band (br, 1.2, 164.8) < band (ring, 1.2, 164.8) - 20.0, "a brush stroke stops the chord ("
                   + juce::String (band (br, 1.2, 164.8) - band (ring, 1.2, 164.8), 1) + " dB)");
        }

        // stop in the middle of a strum: nothing hangs
        {
            auto ev = chordAt (1.0, 3.0);
            auto part = renderHost (proc, head, ev, 0.995, 0.02, sr, 64);   // stops 10 ms into the strum
            auto after = render (proc, {}, 2.0, sr);
            check (rmsDb (after, 1.5, 2.0, sr) < -80.0f, "stopping mid-strum: no hanging strings (" + juce::String (rmsDb (after, 1.5, 2.0, sr), 1) + " dB)");
        }
        proc.testNoteLog = nullptr;
        sc.clear(); proc.scoreEdited();
        setP (proc, "ampOn", 1); setP (proc, "cabOn", 1); setP (proc, "kitHuman", 0.5f);
        proc.setPlayHead (nullptr);
    }

    // ================= RECORD GUIDE =================
    std::cout << "== record guide ==" << std::endl;
    {
        AIOGuitarProcessor gp;
        gp.inputTap.allocate();   // as in the stand-alone app
        gp.prepareToPlay (44100.0, 512);
        GuidePanel guide (gp);
        guide.baseDir = outDir.getChildFile ("guide");
        guide.baseDir.deleteRecursively();
        juce::Random r2 (getenv ("GSEED") ? juce::String (getenv ("GSEED")).getLargeIntValue() : 99);
        const double gsr = 44100.0;
        std::vector<float> stream;
        auto pluck = [&] (int note, float level, double len, float cents = 0.0f)
        {
            const size_t at = stream.size();
            stream.resize (at + (size_t) ((len + 1.0) * gsr), 0.0f);
            std::vector<float> tmp ((size_t) (len * gsr + 10), 0.0f);
            addPluck (tmp, 0, gsr, noteHz (note + cents / 100.0), level, len, 1.2, false, r2);
            for (size_t i = 0; i < tmp.size() && at + i < stream.size(); ++i) stream[at + i] += tmp[i];
        };
        auto silence = [&] (double sec) { stream.resize (stream.size() + (size_t) (sec * gsr), 0.0f); };
        // play the stream into the guide, as the audio interface would (blocks of 512), letting the guide work as it goes
        size_t fed = 0;
        auto feed = [&]
        {
            for (; fed < stream.size(); fed += 512)
            {
                const int n = (int) juce::jmin ((size_t) 512, stream.size() - fed);
                const float* ch[] { stream.data() + fed };
                gp.inputTap.push (ch, 1, n);
                if ((fed / 512) % 16 == 0) guide.pump();
            }
            guide.pump();
        };

        guide.start (Kit::slotSoft);
        silence (0.3);
        check (guide.isRunning() && guide.isTuning(), "guide starts with the tuning check");
        // tuning: string 6 is 40 cents sharp -> asked again; then all six in tune
        pluck (Kit::kOpen[6], 0.2f, 2.5, 40.0f); feed();
        check (guide.isTuning() && guide.statusText().contains (juce::String (juce::CharPointer_UTF8 (u8"セント高い"))), "tuning check: a string 40 cents sharp is caught (" + guide.statusText() + ")");
        for (int str = 6; str >= 1; --str) { pluck (Kit::kOpen[str], 0.2f, 2.5, 5.0f); feed(); }
        check (! guide.isTuning() && guide.stepIndex() == 0, "tuning check passes when every string is within 20 cents");
        const auto steps = Guide::buildSteps (Kit::slotSoft);
        int mistakesStopped = 0, capoNoise = 0, reasonsOk = 0;
        auto reasonHas = [&] (const char* utf8) { if (guide.statusText().contains (juce::String (juce::CharPointer_UTF8 (utf8)))) ++reasonsOk; };
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const auto& st = steps[i];
            const int want = st.expectedNote();
            const double len = st.string >= 5 ? 3.0 : 2.0;
            if (i > 0 && st.capo != steps[i - 1].capo)   // changing the capo: some noodling first (must be ignored)
            {
                pluck (want + 5, 0.15f, 0.6); pluck (want + 2, 0.15f, 0.6);
                feed();
                if (guide.stepIndex() == (int) i && guide.isWaitingForCapo()) ++capoNoise;
            }
            if (i == 2)  { pluck (want + 5, 0.2f, len); feed(); if (guide.stepIndex() == 2) ++mistakesStopped; reasonHas (u8"半音"); if (getenv ("DBG")) std::cout << "    [wrong string] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl; }   // wrong string
            if (i == 4)  { pluck (want, 1.3f, len);     feed(); if (guide.stepIndex() == 4) ++mistakesStopped; reasonHas (u8"割れ"); if (getenv ("DBG")) std::cout << "    [clipped] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl; }   // clipped
            if (i == 6)                                                                                              // picked twice
            {
                pluck (want, 0.2f, 0.06); stream.resize (stream.size() - (size_t) (1.0 * gsr));
                pluck (want, 0.2f, len); feed();
                if (guide.stepIndex() == 6) ++mistakesStopped; reasonHas (u8"2 回"); if (getenv ("DBG")) std::cout << "    [twice] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl;
            }
            if (i == 10)                                                                                             // sympathetic ringing (-20 dB): fine, a warning
            {
                const size_t at = stream.size();
                pluck (want, 0.2f, len);
                std::vector<float> ring ((size_t) (len * gsr), 0.0f);
                addPluck (ring, 0, gsr, noteHz (steps[i - 1].expectedNote()), 0.02f, len, 3.0, false, r2);
                for (size_t k = 0; k < ring.size(); ++k) stream[at + k] += ring[k];
                feed();
                check (guide.stepIndex() == 11, "sympathetic ringing (-20 dB) is accepted (" + guide.statusText() + ")");
                continue;
            }
            if (i == 8)                                                                                              // the previous string still ringing
            {
                const size_t at = stream.size();
                pluck (want, 0.2f, len);
                std::vector<float> ring ((size_t) (len * gsr), 0.0f);
                addPluck (ring, 0, gsr, noteHz (steps[i - 1].expectedNote()), 0.2f, len, 3.0, false, r2);
                for (size_t k = 0; k < ring.size(); ++k) stream[at + k] += ring[k];
                feed();
                if (guide.stepIndex() == 8) ++mistakesStopped; reasonHas (u8"前の弦"); if (getenv ("DBG")) std::cout << "    [ringing] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl;
            }
            pluck (want, 0.2f * juce::Decibels::decibelsToGain ((r2.nextFloat() * 2.0f - 1.0f) * 5.0f), len, (r2.nextFloat() * 2.0f - 1.0f) * 15.0f);
            feed();
        }
        silence (1.0); feed();
        check (mistakesStopped == 4 && reasonsOk == 4, "mistakes stop the guide (with the right reason) on the same note: " + juce::String (mistakesStopped) + " / 4 (wrong string, clipping, picked twice, previous string ringing)");
        check (capoNoise == 5, "noodling while changing the capo is ignored (" + juce::String (capoNoise) + " / 5 capo changes)");
        check (guide.isFinished(), "the whole SOFT layer is done (" + juce::String (guide.stepIndex()) + " / " + juce::String ((int) steps.size()) + ")");
        auto kit = gp.getKit();
        int right = 0, soft = 0;
        if (kit != nullptr)
            for (auto& smp : kit->samples)
                if (smp.slot == Kit::slotSoft)
                {
                    ++soft;
                    if (smp.string >= 1 && smp.rootNote == Kit::kOpen[smp.string] + smp.fret) ++right;
                }
        check (soft == (int) steps.size() && right == soft, "kit: " + juce::String (soft) + " soft takes, " + juce::String (right) + " on the right string / fret");
        check (guide.baseDir.getChildFile ("My Guitar.aioguitar").existsAsFile(), "saved to .aioguitar automatically");
        check (guide.baseDir.getChildFile ("Recordings").findChildFiles (juce::File::findFiles, false, "*.wav").size() == 1, "the raw recording is kept as a WAV");

        // one note again (right-click in the table): 5th string, 7th fret, down + up
        guide.startSingle (Kit::slotSoft, 5, 7);
        stream.clear(); fed = 0;
        silence (0.3);
        pluck (Kit::kOpen[5] + 7, 0.2f, 3.0); feed();
        if (getenv ("DBG")) std::cout << "    [single 1] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl;
        pluck (Kit::kOpen[5] + 7, 0.2f, 3.0); silence (0.5);
        feed();
        if (getenv ("DBG")) std::cout << "    [single] index " << guide.stepIndex() << " finished " << (int) guide.isFinished() << " status " << guide.statusText() << std::endl;
        kit = gp.getKit();
        int at7 = 0;
        for (auto& smp : kit->samples) if (smp.slot == Kit::slotSoft && smp.string == 5 && smp.fret == 7) ++at7;
        check (guide.isFinished() && at7 == 2, "one note again: 5th string 7th fret added (down + up)");

        // mains hum on the input: recorded anyway, with a warning
        guide.startSingle (Kit::slotSoft, 4, 2);
        stream.clear(); fed = 0;
        silence (1.0);   // the hum is there before anything is played
        pluck (Kit::kOpen[4] + 2, 0.2f, 3.0); pluck (Kit::kOpen[4] + 2, 0.2f, 3.0); silence (0.5);
        for (size_t i = 0; i < stream.size(); ++i) stream[i] += 0.006f * std::sin (2.0 * 3.14159265 * 50.0 * i / gsr);   // -30 dB under the note
        feed();
        if (getenv ("DBG"))
        {
            Guide::TakeDetector td; td.prepare (gsr, false);
            td.feed (stream.data(), (int) stream.size());
            std::cout << "    [hum detector] takes " << td.finished.size() << " recording " << (int) td.isRecording() << " floor " << td.noiseFloorDb() << " :";
            for (auto& t : td.finished) std::cout << " " << t.size() / gsr << "s";
            std::cout << std::endl;
        }
        if (getenv ("DBG")) std::cout << "    [hum] index " << guide.stepIndex() << " finished " << (int) guide.isFinished() << " status " << guide.statusText()
                                      << " warnings " << guide.warningList().joinIntoString (" | ") << std::endl;
        check (guide.isFinished() && guide.warningList().joinIntoString (" ").contains (juce::String (juce::CharPointer_UTF8 (u8"ハム"))),
               "mains hum (50 Hz): recorded, with a warning");
        // a long low E that beats (T60 15 s, 0.3 Hz, ~4 dB): one take, kept long, not cut into pieces
        {
            guide.startSingle (Kit::slotSoft, 6, 0);
            stream.clear(); fed = 0;
            silence (0.5);
            auto longNote = [&] (bool up)
            {
                const size_t at = stream.size();
                const double len = 12.0;
                stream.resize (at + (size_t) ((len + 1.0) * gsr), 0.0f);
                std::vector<float> tmp ((size_t) (len * gsr), 0.0f);
                addPluck (tmp, 0, gsr, noteHz (40), 0.25f, len, 15.0 / 6.9, up, r2);
                for (size_t k = 0; k < tmp.size(); ++k)
                    stream[at + k] += tmp[k] * (1.0f + 0.25f * (float) std::sin (2.0 * 3.14159265 * 0.3 * k / gsr));
            };
            longNote (false);
            if (getenv ("DBG"))
            {
                Guide::TakeDetector td; td.prepare (gsr, false);
                td.feed (stream.data(), (int) stream.size());
                std::cout << "    [long] takes " << td.finished.size() << " recording " << (int) td.isRecording() << " :";
                for (auto& t : td.finished) std::cout << " " << t.size() / gsr << "s";
                std::cout << std::endl;
            }
            feed();
            const int afterFirst = guide.stepIndex();
            if (getenv ("DBG")) std::cout << "    [long] after first: index " << afterFirst << " status " << guide.statusText() << std::endl;
            if (getenv ("DBG"))
            {
                const size_t a0 = stream.size();
                longNote (true);
                std::vector<float> tk (stream.begin() + (long) a0, stream.begin() + (long) (a0 + (size_t) (0.3 * gsr)));
                const int fr = (int) (gsr * 0.005);
                std::cout << "    [long up hf]";
                for (int i = 2; i + fr <= (int) tk.size(); i += fr)
                {
                    double e = 0; for (int k = 0; k < fr; ++k) { const float d = tk[(size_t) (i + k)] - 2.0f * tk[(size_t) (i + k - 1)] + tk[(size_t) (i + k - 2)]; e += d * d; }
                    std::cout << " " << juce::roundToInt (10.0 * std::log10 (e / fr + 1e-16));
                }
                std::cout << std::endl;
                silence (0.5); feed();
            }
            else { longNote (true); silence (0.5); feed(); }
            int lowE = 0; double shortest = 1e9;
            if (auto k2 = gp.getKit())
                for (auto& smp : k2->samples)
                    if (smp.slot == Kit::slotSoft && smp.string == 6 && smp.fret == 0) { ++lowE; shortest = juce::jmin (shortest, smp.audio.getNumSamples() / smp.sampleRate); }
            if (getenv ("DBG"))
            {
                std::cout << "    [long] finished " << (int) guide.isFinished() << " index " << guide.stepIndex() << " status " << guide.statusText() << " :";
                if (auto k2 = gp.getKit()) for (auto& smp : k2->samples) if (smp.slot == Kit::slotSoft && smp.string == 6 && smp.fret == 0)
                    std::cout << " " << (smp.up ? "up " : "down ") << smp.audio.getNumSamples() / smp.sampleRate << "s";
                std::cout << std::endl;
            }
            check (afterFirst == 1 && guide.isFinished() && lowE == 2 && shortest > 8.0, "a long, beating low E is one take each (down then up), "
                   + juce::String (shortest, 1) + " s long (" + juce::String (lowE) + " takes)");
        }

        // a high note 15 cents sharp with the previous string ringing along at -40 dB: accepted
        {
            guide.startSingle (Kit::slotSoft, 1, 15);
            stream.clear(); fed = 0;
            silence (0.5);
            for (int k = 0; k < 2; ++k)
            {
                const size_t at = stream.size();
                pluck (Kit::kOpen[1] + 15, 0.2f, 2.5, 15.0f);
                std::vector<float> ring ((size_t) (2.5 * gsr), 0.0f);
                addPluck (ring, 0, gsr, noteHz (Kit::kOpen[2] + 15), 0.002f, 2.5, 3.0, false, r2);
                for (size_t i = 0; i < ring.size(); ++i) stream[at + i] += ring[i];
                feed();
            }
            silence (0.5); feed();
            check (guide.isFinished(), "high G5 15 cents sharp with a -40 dB ring of the next string: accepted (" + guide.statusText() + ")");
            check (! guide.warningList().joinIntoString (" ").contains (juce::String (juce::CharPointer_UTF8 (u8"ハム"))), "... and no false hum warning");
        }

        // PALM MUTE (short notes) and BRUSHING (noise strokes) go through too
        {
            if (getenv ("DBG"))
            {
                for (int up = 0; up < 2; ++up)
                {
                    std::vector<float> tmp ((size_t) (0.8 * gsr), 0.0f);
                    juce::Random rdbg (11);
                    addPluck (tmp, (int) (0.015 * gsr), gsr, noteHz (40), 0.2f, 0.45, 0.07, up == 1, rdbg);
                    juce::AudioBuffer<float> bb (1, (int) tmp.size());
                    for (size_t i = 0; i < tmp.size(); ++i) bb.setSample (0, (int) i, tmp[i]);
                    auto rr = Kit::slice (bb, gsr, Kit::slotMute);
                    std::cout << "    [mute slice " << up << "] notes " << rr.samples.size() << " unpitched " << rr.unpitched << " msg " << rr.message;
                    for (auto& sm : rr.samples) std::cout << " len " << sm.audio.getNumSamples() / sm.sampleRate << " note " << sm.rootNote;
                    std::cout << std::endl;
                }
            }
            guide.debugTakes = getenv ("DBG") != nullptr;
            guide.start (Kit::slotMute);
            stream.clear(); fed = 0;
            silence (0.3);
            for (int str = 6; str >= 1; --str)   // tuning check
            {
                pluck (Kit::kOpen[str], 0.2f, 2.0); feed();
                if (getenv ("DBG")) std::cout << "    [mute tuning " << str << "] tuning " << (int) guide.isTuning() << " status " << guide.statusText() << std::endl;
            }
            const auto ms = Guide::buildSteps (Kit::slotMute);
            for (size_t i = 0; i < ms.size(); ++i)
            {
                if (i > 0 && ms[i].capo != ms[i - 1].capo) { silence (1.0); feed(); }
                const size_t at = stream.size();
                stream.resize (at + (size_t) (1.2 * gsr), 0.0f);
                std::vector<float> tmp ((size_t) (0.5 * gsr), 0.0f);
                addPluck (tmp, 0, gsr, noteHz (ms[i].expectedNote()), 0.2f, 0.45, 0.07, ms[i].up, r2);
                for (size_t k = 0; k < tmp.size(); ++k) stream[at + k] += tmp[k];
                feed();
                if (getenv ("DBG") && i < 4) std::cout << "    [mute " << i << "] index " << guide.stepIndex() << " status " << guide.statusText() << std::endl;
            }
            silence (0.5); feed();
            check (guide.isFinished(), "PALM MUTE layer: " + juce::String (guide.stepIndex()) + " / " + juce::String ((int) ms.size()) + " short notes accepted ("
                   + guide.statusText() + ")");

            guide.start (Kit::slotBrush);
            stream.clear(); fed = 0;
            silence (0.3);
            for (int k = 0; k < 8; ++k)
            {
                const size_t at = stream.size();
                stream.resize (at + (size_t) (0.7 * gsr), 0.0f);
                for (int i = 0; i < (int) (0.2 * gsr); ++i) stream[at + (size_t) i] = 0.3f * (r2.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / (0.03f * (float) gsr));
                feed();
            }
            silence (0.5); feed();
            int strokes = 0;
            if (auto k2 = gp.getKit()) strokes = k2->count (Kit::slotBrush);
            check (guide.isFinished() && strokes == 8, "BRUSHING: 8 strokes (" + juce::String (strokes) + ")");
        }
        // picture of the guide (capo 3, string 5, up) with the table under it
        {
            guide.start (Kit::slotSoft);
            for (int k = 0; k < 15; ++k) guide.pump();   // a few steps in (via skip)
            StringGrid grid (gp);
            guide.onHighlight = [&] (int s2, int f2, bool rec) { grid.setHighlight (s2, f2, rec); };
            for (int k = 0; k < 15; ++k) { juce::MouseEvent* none = nullptr; juce::ignoreUnused (none); }
            juce::Component holder;
            holder.setSize (1076, 760);
            holder.addAndMakeVisible (guide);
            holder.addAndMakeVisible (grid);
            guide.setBounds (0, 0, 1076, 512);
            grid.setBounds (0, 520, 1076, 240);
            for (int k = 0; k < 15; ++k) { auto* b = dynamic_cast<juce::TextButton*> (guide.getChildComponent (8)); juce::ignoreUnused (b); }
            for (auto* c : guide.getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText().contains ("→")) for (int k = 0; k < 15; ++k) b->triggerClick();
            for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            guide.pump();
            auto img = holder.createComponentSnapshot (holder.getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("guide.png"); f.deleteFile();
            juce::FileOutputStream fos (f);
            if (fos.openedOk()) { juce::PNGImageFormat fmt; fmt.writeImageToStream (img, fos); }
            holder.removeAllChildren();
        }
        guide.stop();
    }

    // ---- AMP / CAB on and off while a note rings: no click (2nd difference vs the steady sound around it)
    {
        FXChain ch; FXParams fp; fp.ampModel = 3; fp.cabOn = true;
        ch.prepare (48000.0, 256);
        const int total = 48000;
        juce::AudioBuffer<float> b (2, total);
        for (int i = 0; i < total; ++i) { const float v = 0.2f * (float) std::sin (2.0 * 3.14159265 * 110.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
        for (int pos = 0; pos < total; pos += 256)
        {
            fp.ampOn = ! (pos >= 12288 && pos < 24576);
            fp.cabOn = ! (pos >= 30720 && pos < 38912);
            const int nn = juce::jmin (256, total - pos);
            juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
            ch.process (blk, nn, fp);
        }
        auto bend = [&] (int a, int e) { float m = 0; for (int i = juce::jmax (2, a); i < e; ++i) m = juce::jmax (m, std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2))); return m; };
        // the steady sound of every state the test goes through (amp + cab, cab only, amp only)
        const float steady = juce::jmax (juce::jmax (bend (4000, 12000), bend (14000, 24000)), juce::jmax (bend (26000, 30000), bend (32000, 38000)));
        if (std::getenv ("DBG")) std::cout << "      steady " << bend (4000, 12000) << " " << bend (14000, 24000) << " " << bend (26000, 30000) << " " << bend (32000, 38000) << std::endl;
        float sw = 0;
        for (int at : { 12288, 24576, 30720, 38912 })
        {
            sw = juce::jmax (sw, bend (at - 50, at + 900));
            if (std::getenv ("DBG")) std::cout << "      switch at " << at << ": " << bend (at - 50, at + 900) << std::endl;
        }
        check (sw < steady * 3.0f, "AMP / CAB switched while playing: no click (" + juce::String (sw, 4) + " vs steady " + juce::String (steady, 4) + ")");
    }

    // ---- AMP: silence in -> silence out when it starts (no pop), model switch while playing (no bang),
    //      IR handed to the audio thread / no old sound when coming back to it, NaN recovery, 192 kHz
    std::cout << "== amp ==" << std::endl;
    {
        auto runChain = [] (FXChain& ch, FXParams& fp, juce::AudioBuffer<float>& b, int bs = 256)
        {
            for (int pos = 0; pos < b.getNumSamples(); pos += bs)
            {
                const int n = juce::jmin (bs, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, n);
                ch.process (blk, n, fp);
            }
        };
        float worstPop = 0;
        for (int m = 0; m < ampsim::numModels; ++m)
        {
            FXChain ch; FXParams fp; fp.ampModel = m; fp.ampGain = 10; fp.ampMaster = 10;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 9600); b.clear();
            runChain (ch, fp, b);
            worstPop = juce::jmax (worstPop, b.getMagnitude (0, 0, b.getNumSamples()));
        }
        check (worstPop < 1.0e-5f, "AMP starting (every model, GAIN 10 MASTER 10): silence stays silence (" + juce::String (juce::Decibels::gainToDecibels (worstPop + 1e-12f), 1) + " dBFS)");

        // model switch while a note plays: the first 40 ms after the switch are not louder than the new model itself
        auto sine = [] (juce::AudioBuffer<float>& b, float level)
        {
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = level * (float) std::sin (6.283185307 * 196.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
        };
        float worstJump = -100;
        for (auto [from, to] : { std::pair<int,int> { 4, 0 }, { 1, 0 }, { 3, 4 }, { 0, 4 } })
        {
            FXChain ch; FXParams fp; fp.ampModel = from; fp.ampGain = 5; fp.cabOn = true;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 48000); sine (b, 0.5f);
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                if (pos == 24064) fp.ampModel = to;
                const int nn = juce::jmin (256, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                ch.process (blk, nn, fp);
            }
            FXChain ref; FXParams rp = fp; rp.ampModel = to;
            ref.prepare (48000.0, 256);
            juce::AudioBuffer<float> r (2, 48000); sine (r, 0.5f);
            runChain (ref, rp, r);
            // the 40 ms after the switch: never louder than the louder of the two models playing steadily
            const float after = b.getMagnitude (0, 24064, 1920), steady = juce::jmax (r.getMagnitude (0, 36000, 9600), b.getMagnitude (0, 12000, 12000));
            worstJump = juce::jmax (worstJump, juce::Decibels::gainToDecibels (after / steady));
        }
        check (worstJump < 1.5f, "AMP model switched while playing: no bang (at most " + juce::String (worstJump, 1) + " dB over the louder model)");

        // IR: loaded (handed to the audio thread), used, and no old sound when switching back to it from a box
        {
            FXChain ch; FXParams fp; fp.ampOn = false; fp.cabOn = true; fp.cabType = ampsim::numCabs - 1;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> ir (1, 2400); ir.clear();
            for (int i = 0; i < 2400; ++i) ir.setSample (0, i, (float) (std::exp (-i / 300.0) * std::sin (i * 0.3)));
            ch.getCab().loadIR (std::move (ir), 48000.0);
            juce::AudioBuffer<float> b (2, 48000);
            juce::Random nr (3);
            for (int i = 0; i < 48000; ++i) { const float v = 0.1f * (nr.nextFloat() * 2 - 1); b.setSample (0, i, v); b.setSample (1, i, v); }
            runChain (ch, fp, b);
            for (int k = 0; k < 20; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);   // JUCE loads it in the background
            juce::AudioBuffer<float> b2 (2, 48000);
            for (int i = 0; i < 48000; ++i) { const float v = 0.1f * (nr.nextFloat() * 2 - 1); b2.setSample (0, i, v); b2.setSample (1, i, v); }
            runChain (ch, fp, b2);
            check (ch.getCab().irLoaded() && b2.getRMSLevel (0, 24000, 24000) > 0.001f, "Loaded IR: handed over and used");
            fp.cabType = 2;                                   // box, with noise
            juce::AudioBuffer<float> b3 (2, 24000);
            for (int i = 0; i < 24000; ++i) { const float v = 0.1f * (nr.nextFloat() * 2 - 1); b3.setSample (0, i, v); b3.setSample (1, i, v); }
            runChain (ch, fp, b3);
            juce::AudioBuffer<float> sil (2, 24000); sil.clear();
            runChain (ch, fp, sil);                           // silence
            fp.cabType = ampsim::numCabs - 1;                 // back to the IR, still silent
            juce::AudioBuffer<float> back (2, 9600); back.clear();
            runChain (ch, fp, back);
            check (back.getMagnitude (0, 0, 9600) < 1.0e-4f, "back to the Loaded IR in silence: no old sound (" + juce::String (juce::Decibels::gainToDecibels (back.getMagnitude (0, 0, 9600) + 1e-12f), 1) + " dBFS)");
        }

        // the first IR, loaded while playing in "Loaded IR": never the bare amp at +9 dB while it gets ready
        {
            FXChain ch; FXParams fp; fp.ampModel = 4; fp.ampGain = 8; fp.cabType = ampsim::numCabs - 1;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 48000 * 2); sine (b, 0.3f);
            juce::AudioBuffer<float> ir (1, 2400);
            for (int i = 0; i < 2400; ++i) ir.setSample (0, i, (float) (std::exp (-i / 200.0) * std::sin (i * 0.15)));
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                if (pos == 24064) { juce::AudioBuffer<float> c (ir); ch.getCab().loadIR (std::move (c), 48000.0); }
                const int nn = juce::jmin (256, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                ch.process (blk, nn, fp);
                if (pos % 2048 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }
            const float before = b.getMagnitude (0, 12000, 12000), during = b.getMagnitude (0, 24064, 9600), steady = b.getMagnitude (0, 72000, 24000);
            check (during <= juce::jmax (before, steady) * 1.12f, "first IR loaded while playing: no bare-amp burst (" + juce::String (juce::Decibels::gainToDecibels (during), 1)
                   + " dBFS vs " + juce::String (juce::Decibels::gainToDecibels (before), 1) + " before / " + juce::String (juce::Decibels::gainToDecibels (steady), 1) + " after)");
        }

        // cabinet / mic type switched while playing: no click (2nd difference vs the steady sound)
        {
            FXChain ch; FXParams fp; fp.ampModel = 0; fp.ampGain = 2; fp.cabType = 1; fp.cabMicType = 1;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 48000 * 2);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.2f * (float) std::sin (6.283185307 * 110.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                if (pos >= 24064) fp.cabType = 2;
                if (pos >= 48128) fp.cabMicType = 2;
                if (pos >= 72192) fp.cabPos = 10;
                const int nn = juce::jmin (256, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                ch.process (blk, nn, fp);
            }
            auto bend = [&] (int a0, int e0) { float m = 0; for (int i = a0; i < e0; ++i) m = juce::jmax (m, std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2))); return m; };
            const float steady = juce::jmax (juce::jmax (bend (12000, 24000), bend (36000, 48000)), juce::jmax (bend (60000, 72000), bend (84000, 96000)));
            const float sw = juce::jmax (bend (24064 - 50, 24064 + 1500), bend (48128 - 50, 48128 + 1500));
            const float glide = bend (72192 - 50, 72192 + 2500);
            check (sw < steady * 1.5f, "cabinet / mic changed while playing: no click (" + juce::String (sw, 6) + " vs steady " + juce::String (steady, 6) + ")");
            // POSITION 0 -> 10 at once glides in 40 ms: what is left is around -80 dBFS (a pure sine shows every bit of it)
            check (glide < steady * 3.0f, "POSITION jumped 0 -> 10 while playing: glides without a click (" + juce::String (glide, 6) + ")");
        }

        // IR A in use -> removed -> the same-length IR B loaded twice in a row (real-time pace): B is used within 0.5 s,
        // never stuck on the stand-in cabinet and never the removed A
        {
            auto makeIR = [] (double w) { juce::AudioBuffer<float> ir (1, 2400); for (int i = 0; i < 2400; ++i) ir.setSample (0, i, (float) (std::exp (-i / 200.0) * std::sin (i * w))); return ir; };
            auto runRealtime = [&] (FXChain& ch, FXParams& fp, juce::AudioBuffer<float>& b, int from, int to)
            {
                for (int pos = from; pos < to; pos += 256)
                {
                    const int nn = juce::jmin (256, to - pos);
                    juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                    ch.process (blk, nn, fp);
                    juce::Thread::sleep (5);
                }
            };
            FXChain ch; FXParams fp; fp.ampModel = 0; fp.ampGain = 2; fp.cabType = ampsim::numCabs - 1;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 48000 * 2); sine (b, 0.2f);
            ch.getCab().loadIR (makeIR (0.31), 48000.0);
            runRealtime (ch, fp, b, 0, 24064);
            ch.getCab().clearIR();
            runRealtime (ch, fp, b, 24064, 36096);
            ch.getCab().loadIR (makeIR (0.07), 48000.0);
            ch.getCab().loadIR (makeIR (0.07), 48000.0);
            runRealtime (ch, fp, b, 36096, 96000);
            FXChain ref; FXParams rp = fp;
            ref.prepare (48000.0, 256);
            ref.getCab().loadIR (makeIR (0.07), 48000.0);
            juce::AudioBuffer<float> r (2, 96000); sine (r, 0.2f);
            runRealtime (ref, rp, r, 0, 96000);
            double diff = 0, sig = 0;
            for (int i = 72000; i < 96000; ++i) { diff += std::pow (b.getSample (0, i) - r.getSample (0, i), 2); sig += std::pow (r.getSample (0, i), 2); }
            const double dDb = 10.0 * std::log10 (diff / sig + 1e-20);
            check (dDb < -30.0, "IR removed, then the same-length IR loaded twice: the new IR plays within 0.75 s (difference " + juce::String (dDb, 1) + " dB)");
        }

        // a loaded IR: changing the mic does nothing (no dip)
        {
            FXChain ch; FXParams fp; fp.ampModel = 0; fp.ampGain = 2; fp.cabType = ampsim::numCabs - 1;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> ir (1, 2400);
            for (int i = 0; i < 2400; ++i) ir.setSample (0, i, (float) (std::exp (-i / 200.0) * std::sin (i * 0.15)));
            ch.getCab().loadIR (std::move (ir), 48000.0);
            juce::AudioBuffer<float> b (2, 48000 * 2); sine (b, 0.2f);
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                if (pos >= 60160) fp.cabMicType = 1;
                const int nn = juce::jmin (256, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                ch.process (blk, nn, fp);
                if (pos % 2048 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }
            float lo = 1e9f; for (int w = 60160; w < 60160 + 4800; w += 240) lo = juce::jmin (lo, b.getRMSLevel (0, w, 240));
            const float steady = b.getRMSLevel (0, 48000, 9600);
            check (lo > steady * 0.85f, "loaded IR: changing the mic does nothing (lowest " + juce::String (juce::Decibels::gainToDecibels (lo / steady), 1) + " dB)");
        }

        // tone knobs stopped at noon / thrown across noon: no tick (clean amp, no cabinet, a pure 110 Hz tone)
        {
            FXChain ch; FXParams fp; fp.ampModel = 0; fp.ampGain = 2; fp.cabOn = false; fp.ampBass = 2;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 48000 * 3);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.2f * (float) std::sin (6.283185307 * 110.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                fp.ampBass = pos < 48128 ? 2.0f : (pos < 96256 ? 8.0f : 5.0f);   // 2 -> 8 (across noon), then 8 -> 5 (stop at noon)
                fp.ampPresence = pos < 72192 ? 5.0f : 10.0f;                      // presence thrown 5 -> 10
                const int nn = juce::jmin (256, b.getNumSamples() - pos);
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, nn);
                ch.process (blk, nn, fp);
            }
            auto bend = [&] (int a0, int e0) { float m = 0; for (int i = a0; i < e0; ++i) m = juce::jmax (m, std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2))); return m; };
            const float steady = juce::jmax (juce::jmax (bend (24000, 48000), bend (60000, 72000)), juce::jmax (bend (84000, 96000), bend (120000, 144000)));
            if (std::getenv ("DBG"))
            {
                std::cout << "      knob steady " << bend (24000, 48000) << " " << bend (60000, 72000) << " " << bend (84000, 96000) << " " << bend (120000, 144000) << std::endl;
                int at = 0; float m = 0;
                for (int i = 2; i < b.getNumSamples(); ++i) { const float d = std::abs (b.getSample (0, i) - 2.0f * b.getSample (0, i - 1) + b.getSample (0, i - 2)); if (d > m) { m = d; at = i; } }
                std::cout << "      knob worst at " << at << " = " << m << "  samples:";
                for (int i = at - 3; i <= at + 3 && i < b.getNumSamples(); ++i) std::cout << " " << b.getSample (0, i);
                std::cout << "  len " << b.getNumSamples() << std::endl;
            }
            float worst = 0;
            for (int at : { 48128, 72192, 96256 })
            {
                worst = juce::jmax (worst, bend (at - 50, at + 4800));
                if (std::getenv ("DBG"))
                {
                    std::cout << "      knob event at " << at << ": " << bend (at - 50, at + 4800) << "  windows:";
                    for (int w = at - 480; w < at + 4800; w += 480) std::cout << " " << bend (w, w + 480);
                    std::cout << std::endl;
                }
            }
            // what is left while a +20 dB treble boost glides in 30 ms is around -70 dB under the tone (a pure sine shows all of it)
            check (worst < steady * 3.0f, "BASS across / to noon and PRESENCE thrown: no tick (" + juce::String (worst, 6) + " vs steady " + juce::String (steady, 6) + ")");
        }

        // NaN once: the rig (pedals + amp + cab) drops that block and plays on
        {
            Rig rg; FXParams fp; fp.ampModel = 3;
            rg.prepare (48000.0, 256, rig::indexOf ({ rig::blkAmp, rig::blkComp, rig::blkDrive, rig::blkFuzz, rig::blkTrem }), fp);
            rg.drive.setOn (true); rg.drive.setParams (4.0f, 5.0f, -6.0f);
            juce::AudioBuffer<float> b (2, 48000); sine (b, 0.3f);
            b.setSample (0, 5000, std::numeric_limits<float>::quiet_NaN());
            for (int pos = 0; pos < b.getNumSamples(); pos += 256)
            {
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, juce::jmin (256, b.getNumSamples() - pos));
                rg.process (blk, blk.getNumSamples(), fp, rig::indexOf ({ rig::blkAmp, rig::blkComp, rig::blkDrive, rig::blkFuzz, rig::blkTrem }));
            }
            bool fin = true; for (int i = 0; i < 48000; ++i) fin = fin && std::isfinite (b.getSample (0, i)) && std::isfinite (b.getSample (1, i));
            check (fin && b.getRMSLevel (0, 24000, 24000) > 0.01f, "a NaN in the input: no NaN out, the amp plays on");
        }

        // 192 kHz project (the amp runs at 768 kHz): same level as at 48 kHz
        {
            auto level = [&] (double rate)
            {
                FXChain ch; FXParams fp; fp.ampModel = 2; fp.ampGain = 5;
                ch.prepare (rate, 512);
                juce::AudioBuffer<float> b (2, (int) (rate * 3));
                for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.3f * (float) std::sin (6.283185307 * 82.41 * i / rate); b.setSample (0, i, v); b.setSample (1, i, v); }
                runChain (ch, fp, b, 512);
                return juce::Decibels::gainToDecibels (b.getRMSLevel (0, (int) (rate * 2), (int) rate));
            };
            const float l48 = level (48000.0), l192 = level (192000.0);
            check (std::abs (l192 - l48) < 2.0f, "192 kHz: British Chime as loud as at 48 kHz (" + juce::String (l48, 1) + " / " + juce::String (l192, 1) + " dB)");
        }
    }

    // ---- two IRs loaded right after each other while resting: the next note starts with the second one
    {
        auto makeIR = [] (double tau, double w) { juce::AudioBuffer<float> ir (1, 2400); for (int i = 0; i < 2400; ++i) ir.setSample (0, i, (float) (std::exp (-i / tau) * std::sin (i * w))); return ir; };
        FXParams fp; fp.ampOn = false; fp.cabOn = true; fp.cabType = ampsim::numCabs - 1;
        auto tone = [&] (FXChain& ch)
        {
            juce::AudioBuffer<float> b (2, 2400);   // 50 ms
            for (int i = 0; i < 2400; ++i) { const float v = 0.2f * (float) std::sin (6.283185307 * 196.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            for (int pos = 0; pos < 2400; pos += 240) { juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, 240); ch.process (blk, 240, fp); }
            return b;
        };
        FXChain ref; ref.prepare (48000.0, 256);
        ref.getCab().loadIR (makeIR (150.0, 0.5), 48000.0);
        for (int k = 0; k < 200; ++k) { ref.rest (fp, 256); juce::Thread::sleep (1); }
        const auto want = tone (ref);
        float worst = -200.0f;
        for (int gapMs : { 3, 6, 9, 12, 16, 3, 6, 9, 12, 16 })
        {
            FXChain ch; ch.prepare (48000.0, 256);
            ch.getCab().loadIR (makeIR (400.0, 0.12), 48000.0);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            while (juce::Time::getMillisecondCounterHiRes() - t0 < gapMs) { ch.rest (fp, 256); juce::Thread::sleep (1); }
            ch.getCab().loadIR (makeIR (150.0, 0.5), 48000.0);
            for (int k = 0; k < 150; ++k) { ch.rest (fp, 256); juce::Thread::sleep (1); }   // 0.8 s of resting
            const auto got = tone (ch);
            double e = 0, r = 0;
            for (int i = 0; i < 1200; ++i) { e += std::pow (got.getSample (0, i) - want.getSample (0, i), 2); r += std::pow (want.getSample (0, i), 2); }
            worst = juce::jmax (worst, (float) (10.0 * std::log10 (e / (r + 1e-20) + 1e-20)));
        }
        check (worst < -40.0f, "two IRs loaded 3-16 ms apart while resting: the next note plays the second one from its start (first 25 ms differ by "
               + juce::String (worst, 1) + " dB)");
    }

    // ---- latency: the same with the amp on or off (an impulse through the chain lands at the same place)
    {
        auto impulseAt = [&] (bool ampOn)
        {
            FXChain ch; FXParams fp; fp.ampOn = ampOn; fp.cabOn = false;
            fp.ampModel = 0; fp.ampGain = 0.0f;
            ch.prepare (48000.0, 256);
            juce::AudioBuffer<float> b (2, 256);
            b.clear(); b.setSample (0, 10, 0.01f); b.setSample (1, 10, 0.01f);
            ch.process (b, 256, fp);
            int at = 0; float mx = 0;
            for (int i = 0; i < 256; ++i) if (std::abs (b.getSample (0, i)) > mx) { mx = std::abs (b.getSample (0, i)); at = i; }
            return std::make_pair (at - 10, ch.getLatencySamples());
        };
        const auto off = impulseAt (false);
        check (off.first == off.second, "AMP off: the delay (" + juce::String (off.first) + ") matches the reported latency (" + juce::String (off.second) + ")");
    }

    // ---- RECORD GUIDE input: a 2-input interface with the guitar on input 1 only
    {
        Guide::InputTap tap;
        tap.allocate();
        tap.active.store (true);
        juce::Random r3 (5);
        std::vector<float> in1 (512), in2 (512), got (512 * 40);
        int pulled = 0;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 512; ++i) { in1[(size_t) i] = 0.5f * std::sin (0.05f * (b * 512 + i)); in2[(size_t) i] = 0.001f * (r3.nextFloat() - 0.5f); }
            const float* ch[] { in1.data(), in2.data() };
            tap.push (ch, 2, 512);
            pulled += tap.pull (got.data() + pulled, 512 * 40 - pulled);
        }
        float pk = 0; for (int i = 0; i < pulled; ++i) pk = juce::jmax (pk, std::abs (got[(size_t) i]));
        check (std::abs (pk - 0.5f) < 0.01f && tap.usedChannel.load() == 0, "guide input: guitar on input 1 of 2 comes in at full level (" + juce::String (pk, 3) + ")");
        // the guitar moves to input 2: follows it
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 512; ++i) { in2[(size_t) i] = 0.5f * std::sin (0.05f * (b * 512 + i)); in1[(size_t) i] = 0.001f * (r3.nextFloat() - 0.5f); }
            const float* ch[] { in1.data(), in2.data() };
            tap.push (ch, 2, 512);
            tap.pull (got.data(), 512 * 40);
        }
        check (tap.usedChannel.load() == 1, "guide input: follows the guitar to input 2");
        // nobody reads: overflow is flagged
        for (int b = 0; b < 5000; ++b) { const float* ch[] { in1.data(), in2.data() }; tap.push (ch, 2, 512); }
        check (tap.overflow.load(), "guide input: a full buffer is flagged (the note is asked again)");
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
        // SCORE tab, with a few chords: AUTO down / AUTO up / set by hand
        {
            auto& sc = proc.getScore();
            sc.clear();
            const int ch[] { 40, 47, 52, 56, 59, 64 };
            const double beats[] { 0.0, 0.5, 1.0, 1.5, 2.0, 2.75, 3.0 };
            for (double bt : beats)
                for (int nn : ch) { Score::Note n; n.tick = (juce::int64) (bt * Score::kTicks); n.length = Score::kTicks / 3; n.note = nn; n.autoString = 0; sc.notes.push_back (n); }
            for (auto& n : sc.notes) { const int s6[] { 6, 5, 4, 3, 2, 1 }; for (int k = 0; k < 6; ++k) if (n.note == ch[k]) n.autoString = s6[k]; }
            sc.sort();
            for (auto& n : sc.notes) if (n.tick == 2 * Score::kTicks) { Score::Override o; o.strum = 1; sc.setOverride (n, o); }
            proc.scoreEdited();
        }
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
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "AMP") b->triggerClick();
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        snap ("editor_amp.png");
        // the amp in the middle: COMP > DRIVE > AMP > FUZZ, then CAB
        setP (proc, "fxChain", (float) rig::indexOf ({ rig::blkComp, rig::blkDrive, rig::blkAmp, rig::blkFuzz, rig::blkTrem }));
        setP (proc, "wfOn", 1); setP (proc, "ampModel", 4);
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        snap ("editor_amp_mid.png");
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "EFFECTS") b->triggerClick();
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        snap ("editor_effects_mid.png");
        setP (proc, "fxChain", (float) rig::kDefault); setP (proc, "wfOn", 0); setP (proc, "ampModel", 3);
    }


    // ================= BUZZ FIX =================
    std::cout << "== buzz fix ==" << std::endl;
    {
        check (kit->countBuzz() == 0, "the clean fake recordings: no buzz found (" + juce::String (kit->countBuzz()) + ")");

        // one buzzing take (A2, the string reaches the fret 1.5 s in) and a clean take of the same note
        auto toBuffer = [] (const std::vector<float>& v) { juce::AudioBuffer<float> b (1, (int) v.size()); b.copyFrom (0, 0, v.data(), (int) v.size()); return b; };
        auto takeOf = [&] (const std::vector<float>& v, double rate, bool up)
        {
            auto res = Kit::slice (toBuffer (v), rate, Kit::slotMedium);
            Kit::Sample t;
            if (! res.samples.empty()) t = std::move (res.samples.front());
            t.slot = Kit::slotMedium; t.up = up; t.string = 5; t.fret = 0;
            return t;
        };
        buzzsynth::NoteSpec bs; bs.f0 = 110.0; bs.buzzAt = 1.5; bs.len = 6.0; bs.sr = 48000.0; bs.seed = 77; bs.buzzAmt = 1.0;
        const auto bn = buzzsynth::makeNote (bs);
        buzzsynth::NoteSpec cs = bs; cs.buzzAt = -1.0; cs.seed = 78; cs.pluckPos = 0.13;
        const auto cn = buzzsynth::makeNote (cs);
        auto bad = takeOf (bn.buzzed, 48000.0, false), good = takeOf (cn.clean, 48000.0, true);
        check (bad.hasBuzz() && ! good.hasBuzz(), "slice: the buzzing take is found (at " + juce::String (bad.buzzAt / 48000.0, 2)
               + " s, contact at " + juce::String (bn.trueOnset / 48000.0, 2) + " s), the clean one is not");
        check (bad.buzzAt > 0 && bad.buzzAt <= bn.trueOnset, "slice: the fix starts before the first contact");
        {
            bool same = bad.audio.getNumSamples() > 0;
            const int pre = bad.buzzAt - (int) (0.05 * 48000.0);
            for (int i = 0; i < pre && same; ++i) same = bad.fixed.getSample (0, i) == bad.audio.getSample (0, i);
            check (same, "slice: the fixed take is the recording itself up to the buzz");
        }

        // play it: FIX BUZZ on / off / this take off (amp and cabinet off: the DI itself)
        AIOGuitarProcessor pb;
        pb.prepareToPlay (sr, 256);
        setP (pb, "ampOn", 0); setP (pb, "cabOn", 0); setP (pb, "kitHuman", 0);   // the same hit every time
        pb.putTakes ({ bad });
        pb.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, 256), *std::make_unique<juce::MidiBuffer>());
        auto settle = [&] { for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                            pb.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, 256), *std::make_unique<juce::MidiBuffer>()); };
        auto play = [&] { auto o = render (pb, { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.6f) }, { 4.5, juce::MidiMessage::noteOff (1, 45) } }, 4.8, sr);
                          std::vector<float> v ((size_t) o.getNumSamples()); for (int i = 0; i < o.getNumSamples(); ++i) v[(size_t) i] = o.getSample (0, i); return v; };
        const int b0 = (int) ((0.01 + bad.buzzAt / 48000.0 + 0.1) * sr), b1 = b0 + (int) (0.9 * sr), c0 = (int) (0.3 * sr), c1 = (int) (1.0 * sr);
        const auto on = play();
        setP (pb, "kitBuzz", 0); settle();
        const auto off = play();
        const double hiOn = buzzsynth::bandDb (on, b0, b1, sr, 2500, true), hiOff = buzzsynth::bandDb (off, b0, b1, sr, 2500, true);
        const double preOn = buzzsynth::rmsDb (on, c0, c1), preOff = buzzsynth::rmsDb (off, c0, c1);
        // the same string without the buzz (what it should have sounded like), played the same way
        double hiClean = 0;
        {
            AIOGuitarProcessor pc;
            pc.prepareToPlay (sr, 256);
            setP (pc, "ampOn", 0); setP (pc, "cabOn", 0); setP (pc, "kitHuman", 0);
            pc.putTakes ({ takeOf (bn.clean, 48000.0, false) });
            pc.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, 256), *std::make_unique<juce::MidiBuffer>());
            auto o = render (pc, { { 0.01, juce::MidiMessage::noteOn (1, 45, 0.6f) }, { 4.5, juce::MidiMessage::noteOff (1, 45) } }, 4.8, sr);
            std::vector<float> v ((size_t) o.getNumSamples()); for (int i = 0; i < o.getNumSamples(); ++i) v[(size_t) i] = o.getSample (0, i);
            hiClean = buzzsynth::bandDb (v, b0, b1, sr, 2500, true);
        }
        check (hiOff > hiClean + 6.0 && hiOn < hiClean + 3.0, "FIX BUZZ: the highs after the buzz, over the string without one: off "
               + juce::String (hiOff - hiClean, 1) + " dB, on " + juce::String (hiOn - hiClean, 1) + " dB");
        check (std::abs (preOn - preOff) < 0.05, "before the buzz: on and off sound the same (" + juce::String (preOn - preOff, 2) + " dB)");
        setP (pb, "kitBuzz", 1); settle();
        pb.setBuzzOff (0, true); settle();
        const auto takeOff = play();
        const double hiTakeOff = buzzsynth::bandDb (takeOff, b0, b1, sr, 2500, true);
        check (std::abs (hiTakeOff - hiOff) < 1.0, "this take off (right-click): played as recorded (" + juce::String (hiTakeOff - hiOff, 2) + " dB against FIX BUZZ off)");

        // the project keeps it all: the fixed take itself, where, and the take switched off
        {
            juce::MemoryBlock mb; pb.getStateInformation (mb);
            AIOGuitarProcessor p2;
            p2.prepareToPlay (sr, 256);
            p2.setStateInformation (mb.getData(), (int) mb.getSize());
            auto k2 = p2.getKit();
            bool ok = k2 != nullptr && k2->samples.size() == 1;
            float diff = 1.0f;
            if (ok)
            {
                const auto& t = k2->samples.front();
                ok = t.hasBuzz() && t.buzzOff && t.buzzAt == bad.buzzAt && t.buzzVersion == buzz::kVersion && t.fixed.getNumSamples() == bad.fixed.getNumSamples();
                if (ok) { diff = 0; for (int i = 0; i < t.fixed.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (t.fixed.getSample (0, i) - bad.fixed.getSample (0, i))); }
            }
            check (ok && diff < 1.0e-5f, "save / load: fixed take, its start and the switched-off take come back (largest difference " + juce::String (diff, 7) + ")");
        }
        // a kit from before 0.5.0 (no buzz info): opened at once (as recorded), looked at in the background,
        // then the result is in the kit and in what the project saves
        {
            Kit::GuitarKit k; k.samples.push_back (bad); k.samples.back().buzzOff = false;
            auto tree = Kit::encode (k);
            for (auto c : tree) for (auto id : { "bzV", "bzAt", "bzCut", "bzStrong", "bzBorrow", "bzTried", "bzOff", "fixflac" }) c.removeProperty (id, nullptr);
            const auto f = outDir.getChildFile ("old_kit.aioguitar");
            f.deleteFile();
            { juce::FileOutputStream fos (f); juce::GZIPCompressorOutputStream gz (fos); tree.writeToStream (gz); }
            AIOGuitarProcessor po;
            po.prepareToPlay (sr, 256);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            po.loadKit (f);
            const double openMs = juce::Time::getMillisecondCounterHiRes() - t0;
            const bool asRecorded = po.getKit() != nullptr && ! po.getKit()->samples[0].hasBuzz();
            for (int i = 0; i < 100 && ! po.getKit()->samples[0].hasBuzz(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            auto ko = po.getKit();
            check (asRecorded && ko->samples[0].hasBuzz() && (int) ko->encoded.getChild (0).getProperty ("bzV", 0) == buzz::kVersion
                   && ko->encoded.getChild (0).hasProperty ("fixflac"),
                   "old kit: opens at once (" + juce::String (openMs, 0) + " ms, as recorded), the buzz is found in the background and saved with it");
        }
        // an old song saved with LEVEL MATCH off: opening it changes that switch, which copies the kit while the
        // background look is running. The result must still go in
        {
            AIOGuitarProcessor px;
            px.prepareToPlay (sr, 256);
            px.loadKit (outDir.getChildFile ("old_kit.aioguitar"));   // not looked at yet (the loop has not run)
            setP (px, "kitLevel", 0);
            juce::MemoryBlock mb; px.getStateInformation (mb);
            AIOGuitarProcessor py;
            py.prepareToPlay (sr, 256);
            py.setStateInformation (mb.getData(), (int) mb.getSize());
            for (int i = 0; i < 150 && ! (py.getKit() != nullptr && py.getKit()->samples[0].hasBuzz()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            check (py.getKit() != nullptr && py.getKit()->samples[0].hasBuzz() && py.getParamValue ("kitLevel") < 0.5f,
                   "old song with LEVEL MATCH off: the background look still gets into the kit");
        }
        // reloading a layer with the same number of notes: the project keeps the new recording (child i = samples[i])
        {
            const auto fa = outDir.getChildFile ("relo_a.wav"), fb = outDir.getChildFile ("relo_b.wav");
            juce::Random r1 (11), r2 (22);
            makeRecording (fa, 44100.0, false, r1);
            makeRecording (fb, 44100.0, false, r2);
            AIOGuitarProcessor pr;
            pr.prepareToPlay (sr, 256);
            pr.loadRecordingNow (Kit::slotSoft, fa);
            pr.loadRecordingNow (Kit::slotSoft, fb);
            juce::MemoryBlock mb; pr.getStateInformation (mb);
            AIOGuitarProcessor pq;
            pq.prepareToPlay (sr, 256);
            pq.setStateInformation (mb.getData(), (int) mb.getSize());
            auto k1 = pr.getKit(), k2 = pq.getKit();
            bool same = k1 != nullptr && k2 != nullptr && k1->samples.size() == k2->samples.size();
            float diff = 0;
            if (same)
                for (size_t i = 0; i < k1->samples.size(); ++i)
                {
                    const auto& a1 = k1->samples[i].audio; const auto& a2 = k2->samples[i].audio;
                    if (a1.getNumSamples() != a2.getNumSamples()) { same = false; break; }
                    for (int j = 0; j < a1.getNumSamples(); j += 7) diff = juce::jmax (diff, std::abs (a1.getSample (0, j) - a2.getSample (0, j)));
                }
            check (same && diff < 1.0e-5f, "a layer loaded again with as many notes: the project saves the new recording (difference " + juce::String (diff, 6) + ")");
        }
        // FIX BUZZ switched while a note rings: the note goes on (the switch counts from the next note)
        {
            setP (pb, "kitBuzz", 1); setP (pb, "kitHuman", 0);
            pb.setBuzzOff (0, false); settle();
            juce::AudioBuffer<float> out (2, (int) (2.0 * sr)); out.clear();
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.6f), 0);
            for (int pos = 0; pos < out.getNumSamples(); pos += 256)
            {
                if (pos >= (int) (1.0 * sr) && pos < (int) (1.0 * sr) + 256) setP (pb, "kitBuzz", 0);
                juce::AudioBuffer<float> blk (2, 256); blk.clear();
                pb.processBlock (blk, m); m.clear();
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, juce::jmin (256, out.getNumSamples() - pos));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }
            const float before = rmsDb (out, 0.8, 0.95, sr), after = rmsDb (out, 1.05, 1.2, sr);
            check (after > before - 3.0f, "FIX BUZZ switched while a note rings: it rings on (" + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB)");
            setP (pb, "kitBuzz", 1);
        }

        // a strong buzz + a clean take of the same note: the clean take's tail is borrowed
        {
            buzzsynth::NoteSpec ss; ss.f0 = 329.6; ss.buzzAt = 1.2; ss.swell = 0.5; ss.buzzAmt = 2.0; ss.beat = 0.6; ss.sr = 48000.0; ss.len = 6.0; ss.seed = 90; ss.B = 3.0e-5;
            ss.t60 = 15.0; ss.barrier = 0.8;   // a string that keeps hitting the fret for a second or so
            const auto sn = buzzsynth::makeNote (ss);
            buzzsynth::NoteSpec ds = ss; ds.buzzAt = -1.0; ds.seed = 91; ds.pluckPos = 0.13; ds.attackHard = 0.5; ds.len = 7.0;
            const auto dn = buzzsynth::makeNote (ds);
            auto sb = takeOf (sn.buzzed, 48000.0, false), sg = takeOf (dn.clean, 48000.0, true);
            sb.string = sg.string = 1; sb.fret = sg.fret = 0;
            check (sb.hasBuzz() && sb.buzzStrong, "strong buzz: found and marked strong (highs cut " + juce::String (sb.buzzCut, 1) + " dB)");
            AIOGuitarProcessor p3;
            p3.prepareToPlay (sr, 256);
            p3.putTakes ({ sb });                  // alone: nothing to borrow from yet
            const bool aloneKeeps = p3.getKit()->samples[0].hasBuzz() && ! p3.getKit()->samples[0].buzzBorrowed && ! p3.getKit()->samples[0].buzzTried;
            p3.putTakes ({ sg });                  // the up-stroke comes in: now it can
            auto k3 = p3.getKit();
            int bi = -1; for (int i = 0; i < (int) k3->samples.size(); ++i) if (! k3->samples[(size_t) i].up) bi = i;
            const bool borrowed = bi >= 0 && k3->samples[(size_t) bi].buzzBorrowed;
            check (aloneKeeps && borrowed, "strong buzz: lowered while alone, a clean tail once the other take is there");
            check (bi >= 0 && (bool) k3->encoded.getChild (bi).getProperty ("bzBorrow", false), "strong buzz: the borrowed tail is in the saved kit");

            // A/B: one take through the effects, then quiet again
            p3.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, 256), *std::make_unique<juce::MidiBuffer>());
            p3.auditionSample (bi, false);
            auto a1 = render (p3, {}, 1.0, sr);
            p3.stopAudition();
            auto a2 = render (p3, {}, 0.5, sr);
            check (allFinite (a1) && allFinite (a2) && rmsDb (a1, 0.1, 0.9, sr) > -50.0f && rmsDb (a2, 0.2, 0.5, sr) < -90.0f,
                   "A/B: the take plays (" + juce::String (rmsDb (a1, 0.1, 0.9, sr), 1) + " dB) and stops (" + juce::String (rmsDb (a2, 0.2, 0.5, sr), 1) + " dB)");
            check (maxJump (a2, 0, (int) (0.05 * sr)) < 0.05f, "A/B: stopping fades (largest step " + juce::String (maxJump (a2, 0, (int) (0.05 * sr)), 4) + ")");

            // the table and the switch, for a look
            std::unique_ptr<juce::AudioProcessorEditor> ed3 (p3.createEditor());
            for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto img = ed3->createComponentSnapshot (ed3->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("editor_buzz.png"); f.deleteFile();
            juce::FileOutputStream fos (f);
            if (fos.openedOk()) { juce::PNGImageFormat fmt; fmt.writeImageToStream (img, fos); }
        }
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
