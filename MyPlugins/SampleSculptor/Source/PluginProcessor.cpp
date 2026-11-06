#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pctSigned (float v, int) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; }
    juce::String pct (float v, int)       { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String pct1 (float v, int)      { return juce::String (v * 100.0f, 1) + " %"; }
    juce::String ms (float v, int)        { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String db (float v, int)        { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hzLow (float v, int)     { return v <= 20.5f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String hzHigh (float v, int)    { return v >= 19990.0f ? juce::String ("OFF") : (v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"); }
    juce::String semis (float v, int)     { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st"; }
    constexpr double maxSeconds = 600.0;
}

SampleSculptorProcessor::SampleSculptorProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Sample Sculptor render"),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& id : paramIds()) apvts.addParameterListener (id, this);
    startThread();
}

SampleSculptorProcessor::~SampleSculptorProcessor()
{
    for (auto& id : paramIds()) apvts.removeParameterListener (id, this);
    stopThread (4000);
}

const juce::StringArray& SampleSculptorProcessor::paramIds()
{
    static const juce::StringArray ids { "start", "end", "fadein", "fadeout", "reverse", "pitch", "attack", "snap", "length",
                                         "room", "size", "tone", "drive", "gain", "normalize",
                                         "stretch", "lowcut", "highcut", "width" };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout SampleSculptorProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto add = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def, juce::String (*fmt) (float, int))
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (fmt)));
    };
    auto skewed = [] (float lo, float hi, float step, float centre) { juce::NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };

    add ("start", "Start", { 0.0f, 1.0f, 0.0001f }, 0.0f, pct1);
    add ("end", "End", { 0.0f, 1.0f, 0.0001f }, 1.0f, pct1);
    add ("fadein", "Fade In", skewed (0.0f, 500.0f, 0.1f, 30.0f), 0.0f, ms);
    add ("fadeout", "Fade Out", skewed (0.0f, 3000.0f, 1.0f, 200.0f), 0.0f, ms);
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "reverse", 1 }, "Reverse",
                                                               juce::StringArray { "OFF", "SOUND", "SOUND + ROOM" }, 0));
    add ("pitch", "Pitch", { -24.0f, 24.0f, 0.1f }, 0.0f, semis);
    add ("stretch", "Stretch", skewed (25.0f, 400.0f, 1.0f, 100.0f), 100.0f, pct);
    add ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 1.0f, 120.0f), 20.0f, hzLow);
    add ("highcut", "High Cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), 20000.0f, hzHigh);
    add ("width", "Width", { 0.0f, 200.0f, 1.0f }, 100.0f, pct);
    add ("attack", "Attack", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    add ("snap", "Snap", skewed (1.0f, 50.0f, 0.1f, 12.0f), 15.0f, ms);
    add ("length", "Length", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    add ("room", "Room", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    add ("size", "Size", { 0.0f, 100.0f, 1.0f }, 40.0f, pct);
    add ("tone", "Tone", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    add ("drive", "Drive", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    add ("gain", "Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normalize", 1 }, "Normalize", true));
    // Plugin Link（LINK の番号）
    v.push_back (plink::makeGroupParameter (9));
    return { v.begin(), v.end() };
}

bool SampleSculptorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

ss::Settings SampleSculptorProcessor::readSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ss::Settings s;
    s.start = f ("start");
    s.end = juce::jmax (f ("end"), s.start + 0.001f);
    s.fadeInMs = f ("fadein");
    s.fadeOutMs = f ("fadeout");
    s.reverse = (int) f ("reverse");
    s.pitch = f ("pitch");
    s.stretch = f ("stretch") / 100.0f;
    s.lowCutHz = f ("lowcut");
    s.highCutHz = f ("highcut");
    s.width = f ("width") / 100.0f;
    s.attack = f ("attack") / 100.0f;
    s.snapMs = f ("snap");
    s.length = f ("length") / 100.0f;
    s.room = f ("room") / 100.0f;
    s.size = f ("size") / 100.0f;
    s.tone = f ("tone") / 100.0f;
    s.drive = f ("drive") / 100.0f;
    s.gainDb = f ("gain");
    s.normalize = f ("normalize") > 0.5f;
    return s;
}

void SampleSculptorProcessor::parameterChanged (const juce::String&, float)
{
    dirty = true;
    lastChangeMs = juce::Time::currentTimeMillis();
    if (editorOpen) playAfter = true;   // つまみを回したら、作り直してすぐ鳴らす
    notify();
}

void SampleSculptorProcessor::loadFile (const juce::File& f)
{
    {
        const juce::ScopedLock sl (loadLock);
        pendingLoad = f;
    }
    notify();
}

void SampleSculptorProcessor::run()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    while (! threadShouldExit())
    {
        wait (50);
        juce::File toLoad;
        {
            const juce::ScopedLock sl (loadLock);
            std::swap (toLoad, pendingLoad);
        }
        if (toLoad != juce::File())
        {
            busy = true;
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (toLoad));
            juce::String err;
            if (r == nullptr) err = "Can't read " + toLoad.getFileName() + "  (WAV / AIFF / FLAC / MP3 / M4A / CAF)";
            else if (r->lengthInSamples <= 16) err = toLoad.getFileName() + " is empty";
            else if (r->lengthInSamples > (juce::int64) (r->sampleRate * maxSeconds)) err = toLoad.getFileName() + " is too long (up to 10 minutes)";
            if (err.isEmpty())
            {
                const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
                const int n = (int) r->lengthInSamples;
                juce::AudioBuffer<float> buf (ch, n);
                r->read (&buf, 0, n, 0, true, ch > 1);
                auto a = std::make_shared<SampleAudio>();
                a->fs = r->sampleRate;
                for (int c = 0; c < ch; ++c) a->ch.emplace_back (buf.getReadPointer (c), buf.getReadPointer (c) + n);
                {
                    const juce::SpinLock::ScopedLockType l (lock);
                    source = a;
                }
                {
                    const juce::ScopedLock sl (loadLock);
                    loadedPath = toLoad.getFullPathName();
                    loadError = {};
                }
                dirty = true;
                lastChangeMs = 0;
                if (editorOpen) playAfter = true;
            }
            else
            {
                const juce::ScopedLock sl (loadLock);
                loadError = err;
            }
            ++version;
            busy = false;
        }

        // つまみが止まって 80 ms たったら作り直す
        if (dirty && juce::Time::currentTimeMillis() - lastChangeMs.load() > 80)
        {
            dirty = false;
            auto src = getSource();
            if (src == nullptr) continue;
            busy = true;
            auto out = std::make_shared<SampleAudio>();
            out->fs = src->fs;
            out->ch = ss::render (src->ch, src->fs, readSettings());
            std::shared_ptr<const SampleAudio> old;
            {
                const juce::SpinLock::ScopedLockType l (lock);
                old = std::move (rendered);
                rendered = out;
            }
            old.reset();   // 古い音はここ（裏のスレッド）で捨てる
            {
                // A/B の音量合わせ: 元の音（START〜END）と、できた音の RMS
                auto rms = [] (const ss::Channels& c, size_t a, size_t b)
                {
                    double e = 0; size_t k = 0;
                    for (auto& v : c) for (size_t i = a; i < std::min (b, v.size()); ++i) { e += (double) v[i] * v[i]; ++k; }
                    return std::sqrt (e / (double) std::max<size_t> (1, k));
                };
                const auto st = readSettings();
                const size_t n = src->length();
                const double rs = rms (src->ch, (size_t) (st.start * n), (size_t) (st.end * n)), ro = rms (out->ch, 0, out->length());
                matchGain = rs > 1e-7 ? (float) juce::jlimit (0.06, 16.0, ro / rs) : 1.0f;
            }
            busy = false;
            ++version;
            if (playAfter.exchange (false)) playRequest = 1;
        }
    }
}

void SampleSculptorProcessor::prepareToPlay (double sampleRate, int)
{
    hostFs = sampleRate;
    playing = false;
    playPos = -1.0f;
}

void SampleSculptorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    const int outCh = buffer.getNumChannels();

    const juce::SpinLock::ScopedTryLockType l (lock);
    if (! l.isLocked()) return;   // 作り直した音を入れ替えている瞬間（ほんの一瞬）
    const int req = playRequest.exchange (0);
    auto start = [&] (double semitones, float gain)
    {
        voiceOriginal = abMode.load() == 1;
        const SampleAudio* v = voiceOriginal ? source.get() : rendered.get();
        if (v == nullptr || v->length() < 2) { playing = false; return; }
        const double L = (double) v->length();
        if (voiceOriginal)
        {
            // A = 元の音（START〜END の所だけ）。音量は B（できた音）にそろえる
            const double st = apvts.getRawParameterValue ("start")->load(), en = apvts.getRawParameterValue ("end")->load();
            voiceFrom = juce::jlimit (0.0, L - 2, std::floor (st * L));
            voiceTo = juce::jlimit (voiceFrom + 1, L - 1, std::ceil (juce::jmax (en, st + 0.001) * L));
            gain *= matchGain.load();
        }
        else { voiceFrom = 0; voiceTo = L - 1; }
        pos = voiceFrom;
        rate = v->fs / hostFs * std::pow (2.0, semitones / 12.0);
        voiceGain = gain;
        declick = playing ? 0.0f : 1.0f;
        playing = true;
    };
    if (req == 1) start (0.0, 1.0f);
    if (req == 2) playing = false;

    // 鍵盤: C3（60）で作った音のまま、上下で音程が変わる
    int next = 0;
    auto renderTo = [&] (int until)
    {
        const SampleAudio* a = voiceOriginal ? source.get() : rendered.get();
        if (a == nullptr) playing = false;
        for (int i = next; i < until; ++i)
        {
            if (! playing) break;
            const size_t i0 = (size_t) pos;
            if ((double) i0 + 1 >= voiceTo || i0 + 1 >= a->length()) { playing = false; break; }
            const float f = (float) (pos - (double) i0);
            declick = juce::jmin (1.0f, declick + 0.01f);
            // 終わりの 3 ms はフェード（A の切り取りでプチッとしないように）
            const float tail = (float) juce::jmin (1.0, (voiceTo - pos) / (0.003 * a->fs));
            const float g = voiceGain * declick * tail;
            for (int c = 0; c < outCh; ++c)
            {
                const auto& src = a->ch[(size_t) juce::jmin (c, (int) a->ch.size() - 1)];
                buffer.addSample (c, i, (src[i0] + (src[i0 + 1] - src[i0]) * f) * g);
            }
            pos += rate;
        }
        next = until;
    };
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        renderTo (juce::jlimit (0, n, m.samplePosition));
        if (msg.isNoteOn()) start (msg.getNoteNumber() - 60, msg.getFloatVelocity());
    }
    renderTo (n);
    const SampleAudio* shown = voiceOriginal ? source.get() : rendered.get();
    playPos = playing && shown != nullptr && shown->length() > 0 ? (float) (pos / (double) shown->length()) : -1.0f;
    playingOriginal = playing && voiceOriginal;
}

juce::File SampleSculptorProcessor::outputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Sample Sculptor");
}

juce::File SampleSculptorProcessor::exportWav()
{
    if (isExported()) return lastExport;   // つまみを変えていない = 同じ音なので、同じファイル
    const int ver = version.load();
    auto a = getRendered();
    if (a == nullptr || a->length() == 0) return {};
    auto folder = outputFolder();
    folder.createDirectory();
    auto name = sourceFile().getFileNameWithoutExtension();
    if (name.isEmpty()) name = "Sample";
    // いつも新しいファイル（前に Logic へ入れたものを上書きしないように）
    auto out = folder.getChildFile (name + " sculpted.wav").getNonexistentSibling();
    std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
    if (os == nullptr) return {};
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), a->fs, (unsigned int) a->ch.size(), 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    std::vector<const float*> ptrs;
    for (auto& c : a->ch) ptrs.push_back (c.data());
    w->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) a->length());
    w.reset();
    lastExport = out;
    lastExportVersion = ver;
    return out;
}

void SampleSculptorProcessor::pollMessages()
{
    const juce::ScopedLock sl (loadLock);
    if (loadedPath.isNotEmpty()) { apvts.state.setProperty ("file", loadedPath, nullptr); loadedPath = {}; message = {}; }
    if (loadError.isNotEmpty())  { message = loadError; loadError = {}; }
}

void SampleSculptorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    pollMessages();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SampleSculptorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto f = sourceFile();
            if (f.existsAsFile()) loadFile (f);   // プロジェクトを開いたら、前の音を読み直す
        }
}

juce::AudioProcessorEditor* SampleSculptorProcessor::createEditor() { return new SampleSculptorEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SampleSculptorProcessor(); }
