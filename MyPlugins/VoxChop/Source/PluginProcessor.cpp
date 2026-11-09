#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String semis (float v, int)  { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st"; }
    juce::String body (float v, int)   { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (std::pow (2.0f, v / 12.0f), 2); }
    juce::String pct (float v, int)    { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String ms (float v, int)     { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String db (float v, int)     { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hzLow (float v, int)  { return v <= 20.5f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String hzHigh (float v, int) { return v >= 19990.0f ? juce::String ("OFF") : (v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"); }
    constexpr double maxFileSeconds = 600.0;
    const double gridBeats[] = { 1.0, 0.5, 0.25, 2.0, 4.0 };

    // 鍵盤で弾くときの粒の決め方（高さ ratio、FORMANT KEEP なら響きはそのまま）
    struct KeyCtl
    {
        double ratio; bool keep;
        vox::GrainCtl control (double, float, bool) const
        {
            vox::GrainCtl c;
            c.pitchRatio = (float) ratio;
            c.formantRate = keep ? 1.0f : (float) ratio;
            return c;
        }
        void emitted (double, double, bool) {}
    };
}

//==============================================================================
VoxChopProcessor::VoxChopProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Vox Chop worker"),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* ids : { &voiceIds(), &chopIds() })
        for (auto& id : *ids) apvts.addParameterListener (id, this);
    // 切り方はプリセット・取り消しで変えない（切れ目が勝手に変わらないように）
    for (auto& id : chopIds()) presets.exclude (id);
    presets.exclude ("mode");
    presets.exclude ("keyMode");
    startThread();
}

VoxChopProcessor::~VoxChopProcessor()
{
    for (auto* ids : { &voiceIds(), &chopIds() })
        for (auto& id : *ids) apvts.removeParameterListener (id, this);
    cancelWork = true;          // 計算を途中でやめさせてから、終わるのを待つ
    signalThreadShouldExit();
    notify();
    stopThread (-1);
}

const juce::StringArray& VoxChopProcessor::voiceIds()
{
    static const juce::StringArray ids { "shift", "formant", "breath", "grit", "robot", "alien", "lowcut", "highcut" };
    return ids;
}

const juce::StringArray& VoxChopProcessor::chopIds()
{
    static const juce::StringArray ids { "chopBy", "sens", "grid" };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxChopProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    // 声を変えるつまみは、変えると全体を計算し直すのでオートメーションには出さない
    auto addVoice = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def, juce::String (*fmt) (float, int))
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (fmt).withAutomatable (false)));
    };
    auto add = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def, juce::String (*fmt) (float, int))
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (fmt)));
    };
    auto skewed = [] (float lo, float hi, float step, float centre) { juce::NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
    auto choice = [&] (const char* id, const char* name, juce::StringArray items, int def)
    {
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def,
                                                                   juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    };

    choice ("mode", "Mode", { "CHOP", "WHOLE" }, 0);
    choice ("chopBy", "Chop By", { "AUTO", "GRID" }, 0);
    v.push_back (std::make_unique<F> (juce::ParameterID { "sens", 1 }, "Sensitivity", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                      A().withStringFromValueFunction (pct).withAutomatable (false)));
    choice ("grid", "Grid", { "1/4", "1/8", "1/16", "1/2", "1 BAR" }, 0);
    choice ("keyMode", "Key Mode", { "SLICES", "MELODIC" }, 0);

    addVoice ("shift", "Pitch", { -24.0f, 24.0f, 0.1f }, 0.0f, semis);
    addVoice ("formant", "Body", { -12.0f, 12.0f, 0.1f }, 0.0f, body);
    addVoice ("breath", "Breath", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    addVoice ("grit", "Grit", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    addVoice ("robot", "Robot", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    addVoice ("alien", "Alien", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    addVoice ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 1.0f, 150.0f), 20.0f, hzLow);
    addVoice ("highcut", "High Cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), 20000.0f, hzHigh);

    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "keep", 1 }, "Formant Keep", true));
    add ("attack", "Attack", skewed (0.0f, 2000.0f, 0.1f, 50.0f), 1.0f, ms);
    add ("decay", "Decay", skewed (1.0f, 5000.0f, 1.0f, 400.0f), 400.0f, ms);
    add ("sustain", "Sustain", { 0.0f, 100.0f, 1.0f }, 100.0f, pct);
    add ("release", "Release", skewed (1.0f, 5000.0f, 1.0f, 200.0f), 40.0f, ms);
    add ("velocity", "Velocity", { 0.0f, 100.0f, 1.0f }, 60.0f, pct);
    add ("level", "Level", { -24.0f, 12.0f, 0.1f }, 0.0f, db);
    v.push_back (plink::makeGroupParameter (1));
    return { v.begin(), v.end() };
}

bool VoxChopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.isEmpty()) return true;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

vox::VoiceSettings VoxChopProcessor::voiceSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    vox::VoiceSettings s;
    s.shift = f ("shift");
    s.formant = f ("formant");
    s.breath = f ("breath") / 100.0f;
    s.grit = f ("grit") / 100.0f;
    s.robot = f ("robot") / 100.0f;
    s.alien = f ("alien") / 100.0f;
    s.lowCut = f ("lowcut");
    s.highCut = f ("highcut");
    return s;
}

//==============================================================================
const std::vector<VoxChopProcessor::Landmark>& VoxChopProcessor::landmarks()
{
    static const std::vector<Landmark> l {
        { "MAN", 115.0f, 0.0f }, { "WOMAN", 215.0f, 3.0f }, { "NEUTRAL", 165.0f, 1.5f },
        { "CHILD", 290.0f, 5.0f }, { "GIANT", 70.0f, -6.0f }, { "TINY", 380.0f, 8.0f },
    };
    return l;
}

float VoxChopProcessor::bodyOf (float f0)
{
    if (f0 <= 0.0f) return 0.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (std::log2 (f0) - std::log2 (110.0f)) / (std::log2 (230.0f) - std::log2 (110.0f)));
    return 3.0f * t;
}

float VoxChopProcessor::sourceF0() const
{
    auto s = getSource();
    return s != nullptr ? s->an.medianF0 : 0.0f;
}

const std::vector<VoxChopProcessor::VoicePreset>& VoxChopProcessor::voicePresets()
{
    // name, hint, landmark, shift, formant, breath, grit, robot, alien, lowCut, highCut
    static const std::vector<VoicePreset> p {
        { "ORIGINAL",  "your voice as it is",               -1,   0,   0,  0,    0,    0,    0,    20,  20000 },
        { "MAN",       "male voice",                         0,   0,   0,  0,    0,    0,    0,    20,  20000 },
        { "WOMAN",     "female voice",                       1,   0,   0,  0,    0,    0,    0,    20,  20000 },
        { "NEUTRAL",   "in between, a little air",           2,   0,   0,  0.18f,0,    0,    0,    20,  20000 },
        { "CHILD",     "small and high",                     3,   0,   0,  0,    0,    0,    0,    20,  20000 },
        { "GIANT",     "huge body",                          4,   0,   0,  0,    0.1f, 0,    0,    20,  20000 },
        { "TINY",      "very small creature",                5,   0,   0,  0,    0,    0,    0,    20,  20000 },
        { "CHIPMUNK",  "fast-forward tape",                 -1,  12,  12,  0,    0,    0,    0,    20,  20000 },
        { "SLOWED",    "slowed-down tape",                  -1,  -5,  -5,  0,    0,    0,    0,    20,  20000 },
        { "HELIUM",    "body shrinks, pitch almost same",   -1,   2,   7,  0,    0,    0,    0,    20,  20000 },
        { "WHISPER",   "only breath",                       -1,   0,   0,  1.0f, 0,    0,    0,    120, 20000 },
        { "BREATHY",   "soft and airy",                     -1,   0, 0.5f, 0.35f,0,    0,    0,    20,  20000 },
        { "FRY",       "creaky low voice",                  -1,  -2, -0.5f,0,    0.85f,0,    0,    20,  20000 },
        { "GROWL",     "rough monster",                     -1,  -5,  -4,  0,    1.0f, 0,    0,    20,  20000 },
        { "DEMON",     "deep and evil",                     -1, -10,  -5,  0.1f, 0.6f, 0,    0,    20,  12000 },
        { "GHOST",     "airy and far away",                 -1,   5,   2,  0.75f,0,    0,    0.25f,200, 9000 },
        { "ROBOT",     "one note",                          -1,   0,   0,  0,    0,    1.0f, 0,    20,  20000 },
        { "CYBORG",    "robot + metal",                     -1,   2,   1,  0,    0.1f, 0.8f, 0.45f,20,  20000 },
        { "ALIEN",     "not human any more",                -1,   4,   5,  0.1f, 0.2f, 0.3f, 0.9f, 20,  20000 },
        { "RADIO",     "old radio",                         -1,   0,   0,  0,    0.25f,0,    0,    500, 3500 },
        { "PHONE",     "telephone",                         -1,   0,   0,  0,    0.1f, 0,    0,    400, 3000 },
        { "MEGAPHONE", "loud and rough",                    -1,   0,   0,  0,    0.6f, 0,    0.1f, 700, 4500 },
        { "WALKIE",    "walkie-talkie",                     -1,   0,   0,  0.15f,0.45f,0,    0,    600, 2800 },
    };
    return p;
}

void VoxChopProcessor::applyVoicePreset (int index)
{
    const auto& all = voicePresets();
    if (index < 0 || index >= (int) all.size()) return;
    const auto& p = all[(size_t) index];
    float shift = p.shift, formant = p.formant;
    if (p.landmark >= 0)
    {
        const float f0 = sourceF0() > 0.0f ? sourceF0() : 120.0f;
        const auto& lm = landmarks()[(size_t) p.landmark];
        shift = 12.0f * std::log2 (lm.hz / f0);
        formant = lm.body - bodyOf (f0);
    }
    auto set = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    };
    set ("shift", juce::jlimit (-24.0f, 24.0f, shift));
    set ("formant", juce::jlimit (-12.0f, 12.0f, formant));
    set ("breath", p.breath * 100.0f);
    set ("grit", p.grit * 100.0f);
    set ("robot", p.robot * 100.0f);
    set ("alien", p.alien * 100.0f);
    set ("lowcut", p.lowCut);
    set ("highcut", p.highCut);
}

//==============================================================================
void VoxChopProcessor::parameterChanged (const juce::String& id, float)
{
    if (voiceIds().contains (id))
    {
        needRender = true;
        lastVoiceChange = juce::Time::currentTimeMillis();
    }
    else if (! restoring.load())
    {
        needChop = true;
    }
}

void VoxChopProcessor::loadFile (const juce::File& f)
{
    auto r = std::make_unique<LoadRequest>();
    r->file = f;
    {
        const juce::ScopedLock sl (loadLock);
        pendingLoad = std::move (r);
    }
    notify();
}

void VoxChopProcessor::rechop()
{
    needChop = true;
    notify();
}

void VoxChopProcessor::setSlices (VoxSlices s, bool manualEdit)
{
    if ((int) s.size() > maxSlices) s.resize ((size_t) maxSlices);
    auto ns = std::make_shared<const VoxSlices> (std::move (s));
    std::shared_ptr<const VoxSlices> old;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        old = std::move (slices);
        slices = ns;
    }
    if (manualEdit) manualSlices = true;
    selectedSlice = juce::jlimit (0, juce::jmax (0, (int) ns->size() - 1), selectedSlice.load());
    ++version;
}

void VoxChopProcessor::preview (int sliceIndex)
{
    previewReq = sliceIndex;
}

void VoxChopProcessor::armCapture (bool on)
{
    if (! on)
    {
        capArmed = false;
        if (capRecording.load()) capStopReq = true;
        return;
    }
    const juce::ScopedLock sl (capLock);
    if (capRecording.load() || capDone.load()) return;
    const size_t need = (size_t) (maxCaptureSeconds * hostFs);
    if (capBuf.size() < need) capBuf.assign (need, 0.0f);
    capLen = 0;
    capStopReq = false;
    capArmed = true;
}

double VoxChopProcessor::gridStepSeconds (const VoxSource& s) const
{
    const int g = juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("grid")->load());
    const double bpm = s.bpm > 0 ? s.bpm : lastHostBpm.load();
    return gridBeats[g] * 60.0 / juce::jlimit (20.0, 400.0, bpm);
}

//==============================================================================
void VoxChopProcessor::doChop()
{
    auto src = getSource();
    if (src == nullptr) return;
    std::vector<vox::Cut> cuts;
    if ((int) apvts.getRawParameterValue ("chopBy")->load() == 0)
    {
        cuts = vox::autoChop (src->audio, src->fs, apvts.getRawParameterValue ("sens")->load() / 100.0f, maxSlices);
    }
    else
    {
        const int g = juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("grid")->load());
        const double stepBeats = gridBeats[g];
        const double bpm = juce::jlimit (20.0, 400.0, src->bpm > 0 ? src->bpm : lastHostBpm.load());
        const double firstLine = std::ceil (src->startPpq / stepBeats - 1e-6) * stepBeats;
        const double phaseSec = (firstLine - src->startPpq) * 60.0 / bpm;
        cuts = vox::gridChop (src->audio, src->fs, stepBeats * 60.0 / bpm, phaseSec, maxSlices);
    }
    VoxSlices out;
    for (auto& c : cuts) { VoxSlice s; s.start = c.start; s.end = c.end; out.push_back (s); }
    manualSlices = false;
    setSlices (std::move (out), false);
}

void VoxChopProcessor::doRender()
{
    auto src = getSource();
    if (src == nullptr) return;
    busy = true;
    const auto settings = voiceSettings();
    auto r = vox::renderVoice (src->audio, src->an, settings, 1, &cancelWork);
    if (cancelWork.load()) { busy = false; return; }
    auto v = std::make_shared<VoxVoice>();
    v->audio = std::move (r.audio);
    v->an = std::move (r.an);
    v->fs = src->fs;
    std::shared_ptr<const VoxVoice> old;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (source == src)   // 計算している間に別の声に変わっていなければ
        {
            old = std::move (voice);
            voice = v;
            ++audioVersion;
        }
    }
    old.reset();
    busy = false;
    ++version;
    if (previewAfterRender.exchange (false) && ! needRender.load())
        previewReq = (int) apvts.getRawParameterValue ("mode")->load() == 1 ? -1 : selectedSlice.load();
}

void VoxChopProcessor::startLoaded (std::shared_ptr<VoxSource> src, const VoxSlices* keepSlices)
{
    if (src->an.period.empty()) src->an = vox::analyse (src->audio, src->fs, &cancelWork);
    if (cancelWork.load()) return;
    // 計算し直すまでの間は、元の声のまま鳴らせるように
    auto v = std::make_shared<VoxVoice>();
    v->audio = src->audio;
    v->an = src->an;
    v->fs = src->fs;
    std::shared_ptr<const VoxSource> oldS;
    std::shared_ptr<const VoxVoice> oldV;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        oldS = std::move (source);
        oldV = std::move (voice);
        source = src;
        voice = v;
        ++audioVersion;
    }
    oldS.reset();
    oldV.reset();

    if (keepSlices != nullptr && ! keepSlices->empty())
    {
        const long n = (long) src->audio.size();
        VoxSlices ok;
        for (auto s : *keepSlices)
        {
            s.start = juce::jlimit (0L, n, s.start);
            s.end = juce::jlimit (s.start, n, s.end);
            if (s.end - s.start > 16) ok.push_back (s);
        }
        setSlices (std::move (ok), false);
    }
    else
    {
        doChop();
    }
    needRender = ! voiceSettings().isNeutral();
    lastVoiceChange = 0;
    ++version;
}

void VoxChopProcessor::waitForWork()
{
    // Bounce・Freeze: 計算し直し・読み込みが終わってから鳴らす（最大 30 秒）
    for (int waited = 0; waited < 30000 && ! cancelWork.load(); waited += 5)
    {
        bool loading;
        {
            const juce::ScopedLock sl (loadLock);
            loading = pendingLoad != nullptr;
        }
        if (! loading && ! busy.load() && ! needRender.load() && ! needChop.load() && ! capDone.load()) return;
        juce::Thread::sleep (5);
    }
}

void VoxChopProcessor::run()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    while (! threadShouldExit())
    {
        wait (40);
        graveyard.clear();
        if (threadShouldExit()) break;

        // CAPTURE が終わった
        if (capDone.load())
        {
            busy = true;
            std::vector<float> got;
            double fs = hostFs, startPpq = 0.0, bpm = 120.0;
            {
                const juce::ScopedLock sl (capLock);
                const long len = juce::jlimit (0L, (long) capBuf.size(), capLen.load());
                got.assign (capBuf.begin(), capBuf.begin() + len);
                std::vector<float>().swap (capBuf);   // 大きいので捨てる
                startPpq = capStartPpq;
                bpm = capBpm;
                capDone = false;
            }
            vox::sanitize (got);
            // 鳴った所だけ（前は 50 ms、後ろは 300 ms 残す）
            long first = -1, last = -1;
            for (long i = 0; i < (long) got.size(); ++i)
                if (std::abs (got[(size_t) i]) > 0.003f) { if (first < 0) first = i; last = i; }
            if (first < 0)
            {
                const juce::ScopedLock sl (loadLock);
                pendingMessage = "CAPTURE: nothing came in. Choose the vocal track in the Side Chain menu at the top of this window, then press CAPTURE and play";
            }
            else
            {
                const long a = juce::jmax (0L, first - (long) (0.05 * fs)), b = juce::jmin ((long) got.size(), last + (long) (0.3 * fs));
                auto src = std::make_shared<VoxSource>();
                src->audio.assign (got.begin() + a, got.begin() + b);
                src->fs = fs;
                src->bpm = bpm;
                src->startPpq = startPpq + (double) a / fs * bpm / 60.0;
                src->name = "Capture " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
                // ファイルにしておく（プロジェクトを開き直したら読み直す）
                auto folder = outputFolder().getChildFile ("Captures");
                folder.createDirectory();
                auto file = folder.getChildFile (src->name + ".wav").getNonexistentSibling();
                bool saved = false;
                if (auto os = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                {
                    juce::WavAudioFormat wav;
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), fs, 1, 24, {}, 0)))
                    {
                        os.release();
                        const float* p = src->audio.data();
                        saved = w->writeFromFloatArrays (&p, 1, (int) src->audio.size());
                    }
                }
                {
                    const juce::ScopedLock sl (loadLock);
                    unresolved.reset();
                    curFile = saved ? file : juce::File();
                    curPpq = src->startPpq;
                    curBpm = src->bpm;
                    pendingMessage = saved ? juce::String() : "Captured, but could not save the WAV in Music / Vox Chop / Captures (it will be lost when the project closes)";
                }
                startLoaded (src, nullptr);
            }
            busy = false;
            ++version;
        }
        if (threadShouldExit()) break;

        // ファイルを読む
        std::unique_ptr<LoadRequest> req;
        {
            const juce::ScopedLock sl (loadLock);
            std::swap (req, pendingLoad);
        }
        if (req != nullptr)
        {
            busy = true;
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (req->file));
            juce::String err;
            if (! req->file.existsAsFile()) err = "Can't find the voice file: " + req->file.getFullPathName();
            else if (r == nullptr) err = "Can't read " + req->file.getFileName() + "  (WAV / AIFF / FLAC / MP3 / M4A / CAF)";
            else if (r->lengthInSamples <= 1024) err = req->file.getFileName() + " is too short";
            else if (r->lengthInSamples > (juce::int64) (r->sampleRate * maxFileSeconds)) err = req->file.getFileName() + " is too long (up to 10 minutes)";
            if (err.isEmpty())
            {
                const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
                const int n = (int) r->lengthInSamples;
                juce::AudioBuffer<float> buf (ch, n);
                r->read (&buf, 0, n, 0, true, ch > 1);
                auto src = std::make_shared<VoxSource>();
                src->fs = r->sampleRate;
                src->audio.resize ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    float s = 0.0f;
                    for (int c = 0; c < ch; ++c) s += buf.getSample (c, i);
                    src->audio[(size_t) i] = s / (float) ch;
                }
                vox::sanitize (src->audio);
                src->name = req->file.getFileNameWithoutExtension();
                if (req->restore) { src->startPpq = req->ppq; src->bpm = req->bpm; }   // CAPTURE の拍の位置も戻す
                {
                    const juce::ScopedLock sl (loadLock);
                    curFile = req->file;
                    curPpq = src->startPpq;
                    curBpm = src->bpm;
                    pendingMessage = {};
                    unresolved.reset();
                }
                startLoaded (src, req->restore ? &req->slices : nullptr);
            }
            else
            {
                const juce::ScopedLock sl (loadLock);
                pendingMessage = err;
                if (req->restore) unresolved = std::move (req);   // 見つからなくても、保存するときは前の切れ目・拍の位置を残す
            }
            busy = false;
            ++version;
        }
        if (threadShouldExit()) break;

        if (needChop.exchange (false)) doChop();

        // 声のつまみが止まって 120 ms たったら計算し直す
        if (needRender.load() && juce::Time::currentTimeMillis() - lastVoiceChange.load() > 120)
        {
            needRender = false;
            doRender();
        }
    }
}

//==============================================================================
void VoxChopProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostFs = sampleRate;
    scratch.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.0f);
    for (auto& v : voices) { v.active = false; v.buf.reset(); }
    levelSm.reset (sampleRate, 0.05);
    levelSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("level")->load()));
    playPos = -1.0;
}

void VoxChopProcessor::startVoice (int note, float velocity, const VoxSlices& sl, int sliceIndex, double semitones, bool forceOneShot)
{
    if (audioVoice == nullptr) return;
    const long len = (long) audioVoice->audio.size();
    const VoxSlice* s = sliceIndex >= 0 && sliceIndex < (int) sl.size() ? &sl[(size_t) sliceIndex] : nullptr;
    long from = s != nullptr ? s->start : 0, to = s != nullptr ? s->end : len;
    from = juce::jlimit (0L, len, from);
    to = juce::jlimit (from, len, to);
    if (to - from < 16) return;

    // 同じ鍵盤・CHOKE は前の音を素早く止める
    const bool choke = s != nullptr && s->choke;
    int audible = 0;
    for (auto& v : voices)
    {
        if (v.active && ((note != -1 && v.note == note) || (choke && v.choke)))
        {
            v.released = true; v.fast = true; v.stage = 3;
        }
        audible += v.active && ! v.fast ? 1 : 0;   // リリース中も数える（いつも 4 枠をフェード用に空ける）
    }
    // 鳴っている声が多すぎるときは 1 つを 6 ms で消す（いきなり切らない）。リリース中でいちばん小さい声、なければいちばん古い声
    if (audible >= maxAudible)
    {
        Voice* victim = nullptr;
        for (auto& v : voices) if (v.active && ! v.fast && v.released && (victim == nullptr || v.env < victim->env)) victim = &v;
        if (victim == nullptr)
            for (auto& v : voices) if (v.active && ! v.fast && (victim == nullptr || v.age < victim->age)) victim = &v;
        if (victim != nullptr) { victim->released = true; victim->fast = true; victim->stage = 3; }
    }
    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        // 空きがない: 消えかけている声（いちばん小さい）を使う
        for (auto& v : voices) if (v.released && (target == nullptr || v.env < target->env)) target = &v;
        if (target == nullptr) for (auto& v : voices) if (target == nullptr || v.age < target->age) target = &v;
    }
    auto& v = *target;
    graveyard.bury (v.buf);
    v.buf = audioVoice;
    v.active = true;
    v.released = false;
    v.fast = false;
    v.note = note;
    v.slice = sliceIndex;
    v.from = from;
    v.to = to;
    v.reverse = s != nullptr && s->reverse;
    v.loop = s != nullptr && s->loop && ! forceOneShot;
    v.oneShot = forceOneShot || (s != nullptr && s->oneShot && ! v.loop);   // LOOP は鍵盤を離したら消える
    v.choke = choke;
    v.ratio = std::pow (2.0, juce::jlimit (-36.0, 36.0, semitones) / 12.0);
    const float vs = apvts.getRawParameterValue ("velocity")->load() / 100.0f;
    v.gain = (1.0f - vs + vs * velocity) * juce::Decibels::decibelsToGain (s != nullptr ? s->gainDb : 0.0f);
    v.env = 0.0f;
    v.stage = 0;
    v.age = ++ageCounter;
    v.gp.reset (v.reverse ? (double) (to - 1) : (double) from, false);
    if (v.loop) v.gp.setLoop ((double) from, (double) to);
    const bool keep = apvts.getRawParameterValue ("keep")->load() > 0.5f;
    v.gp.setLevelFollow (keep && std::abs (v.ratio - 1.0) > 0.01);
    playSlice = sliceIndex;
}

void VoxChopProcessor::renderVoices (float* out, int n)
{
    const bool keep = apvts.getRawParameterValue ("keep")->load() > 0.5f;
    const float msToS = (float) hostFs / 1000.0f;
    const float att = apvts.getRawParameterValue ("attack")->load() * msToS;
    const float dec = apvts.getRawParameterValue ("decay")->load() * msToS;
    const float sus = apvts.getRawParameterValue ("sustain")->load() / 100.0f;
    const float rel = apvts.getRawParameterValue ("release")->load() * msToS;
    const float attStep = 1.0f / juce::jmax (1.0f, att);
    const float decCoef = std::exp (-4.6f / juce::jmax (1.0f, dec));    // 決めた時間で −40 dB
    const float relCoef = std::exp (-4.6f / juce::jmax (1.0f, rel));
    const float fastCoef = std::exp (-4.6f / juce::jmax (1.0f, 0.006f * (float) hostFs));
    const Voice* newest = nullptr;

    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (v.buf == nullptr) { endVoice (v); continue; }
        const auto& vv = *v.buf;
        const long len = (long) vv.audio.size();
        v.to = juce::jlimit (0L, len, v.to);
        v.from = juce::jlimit (0L, v.to, v.from);
        const double srcPerOut = vv.fs / hostFs;
        const int dir = v.reverse ? -1 : 1;
        const double adv = (keep ? 1.0 : v.ratio) * srcPerOut * dir;
        KeyCtl ctl { v.ratio, keep };
        // LOOP は声全体から読む（粒が片の端をまたいでも途切れない）。それ以外は片の中だけ、端は 2 ms 絞る
        const vox::Source src = v.loop ? vox::Source { vv.audio.data(), 0, len, 0.0 }
                                       : vox::Source { vv.audio.data(), v.from, v.to, 0.002 * vv.fs };
        for (int done = 0; done < n && v.active;)
        {
            float tmp[32] = {};
            const int chunk = juce::jmin (32, n - done);
            v.gp.process (src, vv.an, srcPerOut, adv, tmp, chunk, ctl);
            const double pos = v.gp.position();
            const bool beyond = dir > 0 ? pos >= (double) v.to : pos <= (double) v.from;
            if (beyond && ! v.loop && ! (v.released && v.fast))
            {
                // 最後まで鳴った（その先は無音なので、残りの粒が消えたら終わり）
                v.released = true; v.fast = true; v.stage = 3;
            }
            for (int i = 0; i < chunk; ++i)
            {
                switch (v.stage)
                {
                    case 0: v.env += attStep; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } break;
                    case 1: v.env = sus + (v.env - sus) * decCoef; if (std::abs (v.env - sus) < 1.0e-4f) { v.env = sus; v.stage = 2; } break;
                    case 2: break;
                    default: v.env *= v.fast ? fastCoef : relCoef; break;
                }
                out[done + i] += tmp[i] * v.env * v.gain;
            }
            if ((v.stage == 3 && v.env < 1.0e-4f) || (v.stage == 2 && sus <= 0.0f)) endVoice (v);
            done += chunk;
        }
        if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
    }
    playPos = newest != nullptr ? newest->gp.position() : -1.0;
    if (newest == nullptr) playSlice = -1;
}

void VoxChopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (isNonRealtime()) waitForWork();
    if (killAll.exchange (false)) for (auto& v : voices) if (v.active) endVoice (v);

    // 曲の位置・テンポ
    bool playing = false;
    double ppq = 0.0, bpm = lastHostBpm.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    lastHostBpm = bpm;

    // サイドチェーン（CAPTURE）。出口と同じチャンネルに入っているので、消す前に読む
    const int inCh = getTotalNumInputChannels();
    if (inCh > 0)
    {
        float pk = 0.0f;
        for (int c = 0; c < juce::jmin (inCh, buffer.getNumChannels()); ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
        if (pk > 1.0e-5f) sideHold = (int) (hostFs * 2.0);
        else sideHold = juce::jmax (0, sideHold - n);
        sideActive = sideHold > 0;

        if (capArmed.load() && ! capRecording.load() && playing && ! capBuf.empty())
        {
            capLen = 0;
            capStartPpq = ppq;
            capBpm = bpm;
            capRecording = true;
            capArmed = false;
        }
        if (capRecording.load())
        {
            long len = capLen.load();
            const long room = (long) capBuf.size() - len;
            const int take = (int) juce::jmin ((long) n, room);
            if (playing && ! capStopReq.load())
            {
                const int chs = juce::jmin (inCh, buffer.getNumChannels());
                for (int i = 0; i < take; ++i)
                {
                    float s = 0.0f;
                    for (int c = 0; c < chs; ++c) s += buffer.getSample (c, i);
                    capBuf[(size_t) (len + i)] = s / (float) juce::jmax (1, chs);
                }
                len += take;
                capLen = len;
            }
            if (! playing || capStopReq.load() || take < n)
            {
                capRecording = false;
                capStopReq = false;
                capDone = true;
            }
        }
    }
    else
    {
        sideActive = false;
        if (capRecording.load()) { capRecording = false; capDone = true; }
    }

    buffer.clear();
    if ((int) scratch.size() < n) return;   // prepareToPlay より大きいブロック（来ないはず）
    float* out = scratch.data();
    std::fill (out, out + n, 0.0f);

    // 新しい声・切れ目を手元に取る（取れなかったら前のまま。MIDI は捨てない）
    {
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (l.isLocked())
        {
            if (audioVoice != voice) { graveyard.bury (audioVoice); audioVoice = voice; }
            if (audioSlices != slices) { graveyard.bury (audioSlices); audioSlices = slices; }
        }
    }
    static const VoxSlices noSlices;
    const VoxSlices& sl = audioSlices != nullptr ? *audioSlices : noSlices;
    const bool whole = (int) apvts.getRawParameterValue ("mode")->load() == 1;
    const bool melodic = (int) apvts.getRawParameterValue ("keyMode")->load() == 1;

    const int req = previewReq.exchange (-1000);
    if (req == -100 || req >= -1)
        for (auto& v : voices) if (v.active && v.note == -1) { v.released = true; v.fast = true; v.stage = 3; }
    if (req >= -1)
    {
        const int idx = whole ? -1 : req;
        const double pitch = idx >= 0 && idx < (int) sl.size() ? sl[(size_t) idx].pitch : 0.0;
        startVoice (-1, 1.0f, sl, idx, pitch, true);
    }

    int next = 0;
    for (const auto m : midi)
    {
        const int at = juce::jlimit (0, n, m.samplePosition);
        if (at > next) { renderVoices (out + next, at - next); next = at; }
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (whole)
                startVoice (note, msg.getFloatVelocity(), sl, -1, note - 60, false);
            else if (melodic)
            {
                const int s = juce::jlimit (0, juce::jmax (0, (int) sl.size() - 1), selectedSlice.load());
                if (! sl.empty()) startVoice (note, msg.getFloatVelocity(), sl, s, note - 60 + sl[(size_t) s].pitch, false);
            }
            else
            {
                const int s = note - firstNote;
                if (s >= 0 && s < (int) sl.size()) startVoice (note, msg.getFloatVelocity(), sl, s, sl[(size_t) s].pitch, false);
            }
        }
        else if (msg.isNoteOff())
        {
            for (auto& v : voices)
                if (v.active && v.note == msg.getNoteNumber() && ! v.oneShot && ! v.released) { v.released = true; v.stage = 3; }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) if (v.active) { v.released = true; v.fast = true; v.stage = 3; }
        }
    }
    if (next < n) renderVoices (out + next, n - next);

    levelSm.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("level")->load()));
    const int outCh = getTotalNumOutputChannels();
    for (int i = 0; i < n; ++i)
    {
        const float g = levelSm.getNextValue();
        float s = out[i] * g;
        if (! std::isfinite (s) || std::abs (s) > 8.0f) s = 0.0f;   // 安全弁
        for (int c = 0; c < outCh; ++c) buffer.setSample (c, i, s);
    }
}

//==============================================================================
juce::File VoxChopProcessor::outputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Vox Chop");
}

juce::File VoxChopProcessor::uniqueFile (const juce::String& name, const juce::String& ext)
{
    auto src = getSource();
    auto folder = outputFolder().getChildFile (juce::File::createLegalFileName (src != nullptr ? src->name : juce::String ("Voice")));
    folder.createDirectory();
    return folder.getChildFile (juce::File::createLegalFileName (name) + ext).getNonexistentSibling();
}

static bool writeWav (const juce::File& f, const vox::Buf& a, double fs)
{
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, 1, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    const float* p = a.data();
    return w->writeFromFloatArrays (&p, 1, (int) a.size());
}

vox::Buf VoxChopProcessor::renderSliceOffline (const VoxVoice& vv, const VoxSlice& s, bool keep) const
{
    const long len = (long) vv.audio.size();
    const long from = juce::jlimit (0L, len, s.start), to = juce::jlimit (from, len, s.end);
    vox::Buf out;
    if (to - from < 2) return out;
    const float g = juce::Decibels::decibelsToGain (s.gainDb);
    if (std::abs (s.pitch) < 0.01f)
    {
        out.assign (vv.audio.begin() + from, vv.audio.begin() + to);
        if (s.reverse) std::reverse (out.begin(), out.end());
    }
    else
    {
        const double ratio = std::pow (2.0, s.pitch / 12.0);
        const int dir = s.reverse ? -1 : 1;
        const double adv = (keep ? 1.0 : ratio) * dir;
        KeyCtl ctl { ratio, keep };
        vox::GrainPlayer gp;
        gp.reset (s.reverse ? (double) (to - 1) : (double) from, false);
        const vox::Source src { vv.audio.data(), from, to, 0.002 * vv.fs };
        const size_t maxLen = (size_t) ((double) (to - from) / std::abs (adv)) + (size_t) (0.05 * vv.fs);
        out.assign (maxLen, 0.0f);
        size_t done = 0;
        while (done < maxLen)
        {
            const int chunk = (int) juce::jmin ((size_t) 512, maxLen - done);
            gp.process (src, vv.an, 1.0, adv, out.data() + done, chunk, ctl);
            done += (size_t) chunk;
        }
    }
    const size_t fade = juce::jmin (out.size() / 2, (size_t) (0.002 * vv.fs));
    for (size_t i = 0; i < fade; ++i)
    {
        const float k = (float) i / (float) fade;
        out[i] *= k;
        out[out.size() - 1 - i] *= k;
    }
    for (auto& x : out) x *= g;
    return out;
}

juce::File VoxChopProcessor::exportSlice (int index)
{
    auto v = getVoice();
    auto s = getSlices();
    if (v == nullptr || s == nullptr || index < 0 || index >= (int) s->size()) return {};
    const bool keep = apvts.getRawParameterValue ("keep")->load() > 0.5f;
    const auto audio = renderSliceOffline (*v, (*s)[(size_t) index], keep);
    if (audio.empty()) return {};
    auto f = uniqueFile ("slice " + juce::String (index + 1).paddedLeft ('0', 2) + " "
                         + juce::MidiMessage::getMidiNoteName (firstNote + index, true, true, 3), ".wav");
    return writeWav (f, audio, v->fs) ? f : juce::File();
}

juce::Array<juce::File> VoxChopProcessor::exportAllSlices()
{
    juce::Array<juce::File> files;
    auto s = getSlices();
    if (s == nullptr) return files;
    for (int i = 0; i < (int) s->size(); ++i)
        if (auto f = exportSlice (i); f.existsAsFile()) files.add (f);
    return files;
}

juce::File VoxChopProcessor::exportWhole()
{
    auto v = getVoice();
    if (v == nullptr || v->audio.empty()) return {};
    auto f = uniqueFile ("voice", ".wav");
    return writeWav (f, v->audio, v->fs) ? f : juce::File();
}

juce::File VoxChopProcessor::exportMidi()
{
    auto src = getSource();
    auto s = getSlices();
    if (src == nullptr || s == nullptr || s->empty()) return {};
    const double bpm = juce::jlimit (20.0, 400.0, src->bpm > 0 ? src->bpm : lastHostBpm.load());
    // 頭のある小節の頭から（4/4 として）。CAPTURE なら、その小節に置けば元の位置とそろう
    const double barPpq = std::floor (src->startPpq / 4.0 + 1e-9) * 4.0;
    const int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (int i = 0; i < (int) s->size(); ++i)
    {
        const auto& sl = (*s)[(size_t) i];
        const double b0 = src->startPpq - barPpq + (double) sl.start / src->fs * bpm / 60.0;
        const double b1 = src->startPpq - barPpq + (double) sl.end / src->fs * bpm / 60.0;
        seq.addEvent (juce::MidiMessage::noteOn (1, firstNote + i, (juce::uint8) 100), b0 * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, firstNote + i), juce::jmax (b0 * tpq + 1, b1 * tpq - 1));
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    auto f = uniqueFile ("chops (bar " + juce::String ((int) (barPpq / 4.0) + 1) + ")", ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return {};
    return mf.writeTo (os) ? f : juce::File();
}

//==============================================================================
juce::String VoxChopProcessor::takeMessage()
{
    const juce::ScopedLock sl (loadLock);
    auto m = pendingMessage;
    pendingMessage = {};
    return m;
}

void VoxChopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("SLICES"), nullptr);
    VoxSlices toSave;
    {
        const juce::ScopedLock sl (loadLock);
        if (unresolved != nullptr)
        {
            state.setProperty ("file", unresolved->file.getFullPathName(), nullptr);
            state.setProperty ("srcPpq", unresolved->ppq, nullptr);
            state.setProperty ("srcBpm", unresolved->bpm, nullptr);
            toSave = unresolved->slices;
        }
        else
        {
            state.setProperty ("file", curFile.getFullPathName(), nullptr);
            state.setProperty ("srcPpq", curPpq, nullptr);
            state.setProperty ("srcBpm", curBpm, nullptr);
            if (auto s = getSlices()) toSave = *s;
        }
    }
    juce::ValueTree list ("SLICES");
    for (auto& x : toSave)
    {
        juce::ValueTree t ("S");
        t.setProperty ("a", (juce::int64) x.start, nullptr);
        t.setProperty ("b", (juce::int64) x.end, nullptr);
        t.setProperty ("p", x.pitch, nullptr);
        t.setProperty ("g", x.gainDb, nullptr);
        t.setProperty ("f", (x.reverse ? 1 : 0) | (x.loop ? 2 : 0) | (x.oneShot ? 4 : 0) | (x.choke ? 8 : 0), nullptr);
        list.appendChild (t, nullptr);
    }
    state.appendChild (list, nullptr);
    state.setProperty ("manual", manualSlices.load(), nullptr);
    state.setProperty ("selected", selectedSlice.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxChopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto req = std::make_unique<LoadRequest>();
    req->restore = true;
    const auto list = tree.getChildWithName ("SLICES");
    for (int i = 0; i < list.getNumChildren(); ++i)
    {
        const auto t = list.getChild (i);
        VoxSlice x;
        x.start = (long) (juce::int64) t.getProperty ("a", 0);
        x.end = (long) (juce::int64) t.getProperty ("b", 0);
        x.pitch = (float) t.getProperty ("p", 0.0f);
        x.gainDb = (float) t.getProperty ("g", 0.0f);
        const int f = (int) t.getProperty ("f", 0);
        x.reverse = f & 1; x.loop = f & 2; x.oneShot = f & 4; x.choke = f & 8;
        req->slices.push_back (x);
    }
    tree.removeChild (list, nullptr);
    req->file = juce::File (tree.getProperty ("file", "").toString());
    req->ppq = (double) tree.getProperty ("srcPpq", 0.0);
    req->bpm = (double) tree.getProperty ("srcBpm", 0.0);
    manualSlices = (bool) tree.getProperty ("manual", false);
    selectedSlice = (int) tree.getProperty ("selected", 0);

    restoring = true;
    apvts.replaceState (tree);
    restoring = false;

    if (req->file.getFullPathName().isNotEmpty())
    {
        // ファイル・切れ目・拍の位置を 1 つの依頼にまとめて渡す（続けて来ても混ざらない）
        {
            const juce::ScopedLock sl (loadLock);
            pendingLoad = std::move (req);
        }
        notify();
    }
}

juce::AudioProcessorEditor* VoxChopProcessor::createEditor() { return new VoxChopEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxChopProcessor(); }
