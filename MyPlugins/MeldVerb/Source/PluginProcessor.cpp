#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using NRange = juce::NormalisableRange<float>;
    NRange skewed (float lo, float hi, float centre, float step = 0.0f)
    {
        NRange r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
    const char* eqIds[12] = { "On", "LowGain", "LowFreq", "LowSlope", "LowShape", "HighGain", "HighFreq", "HighSlope", "HighShape", "BellGain", "BellFreq", "BellQ" };
    juce::String eqId (bool rev, int k) { return juce::String (rev ? "rev" : "pre") + eqIds[k]; }
}

const juce::StringArray& MeldVerbProcessor::noteNames()
{
    static const juce::StringArray n { "1/64T", "1/64", "1/64.", "1/32T", "1/32", "1/32.", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.",
                                       "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1T", "1", "1.", "2T", "2", "2.", "4T", "4", "4.", "8T", "8", "8." };
    return n;
}
const juce::StringArray& MeldVerbProcessor::preNoteNames()
{
    static const juce::StringArray n { "1/128T", "1/128", "1/128.", "1/64T", "1/64", "1/64.", "1/32T", "1/32", "1/32.", "1/16T", "1/16", "1/16.",
                                       "1/8T", "1/8", "1/8.", "1/4T", "1/4" };
    return n;
}
double MeldVerbProcessor::noteBeats (int i)
{
    i = juce::jlimit (0, 29, i);
    static const double whole[10] = { 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1, 2, 4, 8 };
    const double b = whole[i / 3] * 4.0;
    return (i % 3 == 0) ? b * 2.0 / 3.0 : (i % 3 == 2 ? b * 1.5 : b);
}
double MeldVerbProcessor::preNoteBeats (int i)
{
    i = juce::jlimit (0, 16, i);
    static const double whole[6] = { 1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4 };
    const double b = whole[i / 3] * 4.0;
    return (i % 3 == 0) ? b * 2.0 / 3.0 : (i % 3 == 2 ? b * 1.5 : b);
}

juce::AudioProcessorValueTreeState::ParameterLayout MeldVerbProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto add = [&ps] (const juce::String& id, const juce::String& name, NRange r, float def, const juce::String& unit = {})
    {
        ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                                   juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addB = [&ps] (const juce::String& id, const juce::String& name, bool def)
    { ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def)); };
    auto addC = [&ps] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    { ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def)); };

    add ("mix", "Dry/Wet", NRange (0, 100, 0.1f), 50, "%");
    add ("output", "Output", NRange (-60, 10, 0.1f), 0, "dB");
    add ("lvlE", "Early Level", skewed (-70, 0, -12, 0.1f), -3, "dB");
    add ("lvlP", "Plate Level", skewed (-70, 0, -12, 0.1f), -3, "dB");
    add ("lvlH", "Hall Level", skewed (-70, 0, -12, 0.1f), -3, "dB");

    add ("eTime", "Early Time", NRange (0, 250, 0.1f), 30.6f, "ms");
    add ("eSize", "Early Size", skewed (4, 40, 12, 0.01f), 4.9f);
    add ("eDiff", "Early Diffusion", NRange (0, 100, 0.1f), 100, "%");
    add ("eAngle", "Early Angle", NRange (0, 100, 0.1f), 40.2f);
    add ("eLp", "Early Lowpass", skewed (30, 20000, 1500, 1), 414.9f, "Hz");

    struct TankDef { const char* pre; const char* name; float time, size, diff, attack, lp, damp, bal, cross; };
    const TankDef tanks[2] = { { "p", "Plate", 1.1f, 7.44f, 39.6f, 24.6f, 4016, 4, 0.41f, 2678 },
                               { "h", "Hall", 2.5f, 11.0f, 30.0f, 43.1f, 7843, 3, 0.61f, 1818 } };
    for (auto& t : tanks)
    {
        const juce::String a (t.pre), n (t.name);
        add (a + "Time", n + " Time", skewed (0.02f, 24, 2.5f, 0.01f), t.time, "s");
        addB (a + "Sync", n + " Tempo Sync", false);
        addC (a + "Note", n + " Synced Time", noteNames(), 16);
        add (a + "Size", n + " Size", skewed (4, 40, 12, 0.01f), t.size);
        add (a + "Diff", n + " Diffusion", NRange (0, 100, 0.1f), t.diff, "%");
        add (a + "Attack", n + " Attack", skewed (0, 250, 50, 0.1f), t.attack, "ms");
        add (a + "Lp", n + " Lowpass", skewed (30, 20000, 1500, 1), t.lp, "Hz");
        add (a + "Damp", n + " Damping", NRange (0, 10, 0.01f), t.damp);
        add (a + "Bal", n + " Balance", NRange (0, 1, 0.001f), t.bal);
        add (a + "Cross", n + " Crossover", skewed (20, 4000, 600, 1), t.cross, "Hz");
    }

    add ("modRate", "Mod Rate", skewed (0.25f, 4, 1, 0.01f), 0.48f, "Hz");
    add ("modDepth", "Mod Depth", NRange (0, 100, 0.1f), 25, "%");
    addC ("modType", "Mod Type", { "Random", "Pitch" }, 0);
    add ("predelay", "Pre-Delay", skewed (0, 1000, 100, 0.1f), 20, "ms");
    addB ("pdSync", "Pre-Delay Tempo Sync", false);
    addC ("pdNote", "Pre-Delay Synced Time", preNoteNames(), 7);

    for (int rev = 0; rev < 2; ++rev)
    {
        const juce::String n = rev ? "Reverb EQ " : "Pre EQ ";
        addB (eqId (rev, 0), n + "On", true);
        add (eqId (rev, 1), n + "Low Gain", NRange (-30, 15, 0.1f), 0, "dB");
        add (eqId (rev, 2), n + "Low Freq", skewed (20, 20000, 1000, 1), 100, "Hz");
        add (eqId (rev, 3), n + "Low Slope", skewed (0.2f, 12, 1.5f, 0.01f), 0.707f);
        addC (eqId (rev, 4), n + "Low Shape", { "Low Shelf", "High Pass" }, 0);
        add (eqId (rev, 5), n + "High Gain", NRange (-30, 15, 0.1f), 0, "dB");
        add (eqId (rev, 6), n + "High Freq", skewed (20, 20000, 1000, 1), 11000, "Hz");
        add (eqId (rev, 7), n + "High Slope", skewed (0.2f, 12, 1.5f, 0.01f), 0.707f);
        addC (eqId (rev, 8), n + "High Shape", { "High Shelf", "Low Pass" }, 0);
        add (eqId (rev, 9), n + "Bell Gain", NRange (-30, 15, 0.1f), 0, "dB");
        add (eqId (rev, 10), n + "Bell Freq", skewed (20, 20000, 1000, 1), 2000, "Hz");
        add (eqId (rev, 11), n + "Bell Q", skewed (0.2f, 12, 1.5f, 0.01f), 0.707f);
    }
    addB ("smoothOn", "Smoothing", false);
    add ("smoothAmt", "Smoothing Amount", NRange (0, 100, 0.1f), 100, "%");
    add ("unmaskAmt", "Unmask Amount", NRange (0, 100, 0.1f), 70, "%");
    // Plugin Link（LINK の番号・VOCAL DUCK）
    ps.push_back (plink::makeGroupParameter (9));
    ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "linkDuck", 9 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 4.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    return { ps.begin(), ps.end() };
}

MeldVerbProcessor::MeldVerbProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MeldVerb", createLayout())
{
    for (auto& id : paramOrder()) { ptrs.push_back (apvts.getRawParameterValue (id)); jassert (ptrs.back() != nullptr); }
    fft = std::make_unique<juce::dsp::FFT> (12);   // 4096 点
    window.resize (4096); fftBuf.assign (8192, 0.0f);
    for (int i = 0; i < 4096; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / 4095.0f);
    chunkDry.assign (4096, 0.0f); chunkWet.assign (4096, 0.0f);
    for (int b = 0; b < specBands; ++b) { liveDry.db[b] = liveWet.db[b] = -120.0f; shownDry[b] = shownWet[b] = -120.0f; }
    startTimerHz (30);
}

MeldVerbProcessor::~MeldVerbProcessor() { stopTimer(); }

bool MeldVerbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void MeldVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; maxBlock = samplesPerBlock;
    engine.prepare (sampleRate);
    engine.set (readParams(), true);
    engine.reset();
    dryBuf.assign ((size_t) std::max (1, samplesPerBlock), 0.0f);
    wetBuf.assign ((size_t) std::max (1, samplesPerBlock), 0.0f);
    setLatencySamples (0);
}

const juce::StringArray& MeldVerbProcessor::paramOrder()
{
    // readParams が読む順番（音の処理の中で文字列を作らないよう、ポインターを先に集めておく）
    static const juce::StringArray ids = []
    {
        juce::StringArray a { "mix", "output", "lvlE", "lvlP", "lvlH", "eTime", "eSize", "eDiff", "eAngle", "eLp" };
        for (auto t : { "p", "h" })
            for (auto k : { "Time", "Sync", "Note", "Size", "Diff", "Attack", "Lp", "Damp", "Bal", "Cross" }) a.add (juce::String (t) + k);
        a.addArray ({ "modRate", "modDepth", "modType", "predelay", "pdSync", "pdNote" });
        for (int rev = 0; rev < 2; ++rev) for (int k = 0; k < 12; ++k) a.add (eqId (rev, k));
        a.addArray ({ "smoothOn", "smoothAmt" });
        return a;
    }();
    return ids;
}

meld::Params MeldVerbProcessor::readParams() const
{
    int i = 0;
    auto v = [&]() { return ptrs[(size_t) i++]->load(); };
    meld::Params q;
    q.mix = v(); q.outputDb = v();
    q.levelDb[0] = v(); q.levelDb[1] = v(); q.levelDb[2] = v();
    q.early.timeMs = v(); q.early.size = v(); q.early.diffusion = v(); q.early.angle = v(); q.early.lowpassHz = v();
    const double bpm = std::max (20.0, hostBpm.load());
    auto tank = [&] (meld::TankParams& t)
    {
        t.timeSec = v(); const bool sync = v() > 0.5f; const int note = (int) v();
        if (sync) t.timeSec = (float) juce::jlimit (0.02, 24.0, noteBeats (note) * 60.0 / bpm);
        t.size = v(); t.diffusion = v(); t.attackMs = v(); t.lowpassHz = v(); t.damping = v(); t.balance = v(); t.crossoverHz = v();
    };
    tank (q.plate); tank (q.hall);
    q.modRate = v(); q.modDepth = v(); q.modType = (int) v();
    q.predelayMs = v();
    { const bool sync = v() > 0.5f; const int note = (int) v();
      if (sync) q.predelayMs = (float) juce::jlimit (0.0, 1000.0, preNoteBeats (note) * 60000.0 / bpm); }
    auto eq = [&] (meld::EqSettings& e)
    {
        e.on = v() > 0.5f;
        e.lowGain = v(); e.lowFreq = v(); e.lowSlope = v(); e.lowShape = (int) v();
        e.highGain = v(); e.highFreq = v(); e.highSlope = v(); e.highShape = (int) v();
        e.bellGain = v(); e.bellFreq = v(); e.bellQ = v();
    };
    eq (q.preEq); eq (q.revEq);
    q.smoothOn = v() > 0.5f; q.smoothAmount = v();
    return q;
}

void MeldVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) hostBpm.store (*b);

    const int inCh = getTotalNumInputChannels();
    if (inCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = std::max (2, inCh); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    levels[0].store (std::max (levels[0].load() * 0.9f, buffer.getMagnitude (0, 0, n)));
    levels[1].store (std::max (levels[1].load() * 0.9f, buffer.getMagnitude (1, 0, n)));

    engine.set (readParams());
    engine.setDuck (duck.next (link, apvts.getRawParameterValue ("linkDuck")->load(), n, getSampleRate()));
    if ((int) dryBuf.size() < n) { dryBuf.resize ((size_t) n); wetBuf.resize ((size_t) n); }
    // 長いブロックでも大丈夫なように 512 ずつ
    for (int pos = 0; pos < n; pos += 512)
    {
        const int k = std::min (512, n - pos);
        engine.process (L + pos, R + pos, k, dryBuf.data() + pos, wetBuf.data() + pos);
    }
    smoothDb.store (engine.get().smoothOn ? engine.smoothing().currentDb() : 0.0f);

    // 画面と LEARN 用に、ドライと響きを渡す
    if (editorOpen.load() || learnMode.load() != LearnMode::none)
    {
        const auto scope = fifo.write (n);
        if (scope.blockSize1 > 0) { std::copy (dryBuf.begin(), dryBuf.begin() + scope.blockSize1, fifoDry.begin() + scope.startIndex1);
                                    std::copy (wetBuf.begin(), wetBuf.begin() + scope.blockSize1, fifoWet.begin() + scope.startIndex1); }
        if (scope.blockSize2 > 0) { std::copy (dryBuf.begin() + scope.blockSize1, dryBuf.begin() + scope.blockSize1 + scope.blockSize2, fifoDry.begin() + scope.startIndex2);
                                    std::copy (wetBuf.begin() + scope.blockSize1, wetBuf.begin() + scope.blockSize1 + scope.blockSize2, fifoWet.begin() + scope.startIndex2); }
    }
    levels[2].store (std::max (levels[2].load() * 0.9f, buffer.getMagnitude (0, 0, n)));
    levels[3].store (std::max (levels[3].load() * 0.9f, buffer.getMagnitude (1, 0, n)));
}

//==============================================================================
void MeldVerbProcessor::timerCallback()
{
    // FIFO から 2048 ずつ読み、4096 点の FFT で帯域ごとのパワーを出す
    for (;;)
    {
        const int need = 2048;
        if (fifo.getNumReady() < need) break;
        // 前半を残して後半を足す（50% 重ね）
        std::copy (chunkDry.begin() + need, chunkDry.end(), chunkDry.begin());
        std::copy (chunkWet.begin() + need, chunkWet.end(), chunkWet.begin());
        const auto scope = fifo.read (need);
        auto take = [&] (std::vector<float>& dst, const std::vector<float>& src)
        {
            std::copy (src.begin() + scope.startIndex1, src.begin() + scope.startIndex1 + scope.blockSize1, dst.begin() + need);
            if (scope.blockSize2 > 0) std::copy (src.begin() + scope.startIndex2, src.begin() + scope.startIndex2 + scope.blockSize2, dst.begin() + need + scope.blockSize1);
        };
        take (chunkDry, fifoDry); take (chunkWet, fifoWet);

        float dDb[specBands], wDb[specBands];
        std::vector<float> pw (2049);
        for (int which = 0; which < 2; ++which)
        {
            auto& src = which == 0 ? chunkDry : chunkWet;
            for (int i = 0; i < 4096; ++i) fftBuf[(size_t) i] = src[(size_t) i] * window[(size_t) i];
            std::fill (fftBuf.begin() + 4096, fftBuf.end(), 0.0f);
            fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
            for (int i = 0; i <= 2048; ++i) { const float m = fftBuf[(size_t) i] / 1024.0f; pw[(size_t) i] = m * m; }
            assist::Spectrum::fromPower (pw.data(), 2049, sr, 4096, which == 0 ? dDb : wDb);
        }
        {
            const juce::ScopedLock l (specLock);
            for (int b = 0; b < specBands; ++b)
            {
                liveDry.db[b] = dDb[b] > liveDry.db[b] ? 0.5f * (liveDry.db[b] + dDb[b]) : liveDry.db[b] * 0.85f + dDb[b] * 0.15f;
                liveWet.db[b] = wDb[b] > liveWet.db[b] ? 0.5f * (liveWet.db[b] + wDb[b]) : liveWet.db[b] * 0.85f + wDb[b] * 0.15f;
            }
        }
        if (learnMode.load() != LearnMode::none)
        {
            // 音が鳴っている所だけためる（-60 dBFS より大きいとき）
            double e = 0; for (int i = 2048; i < 4096; ++i) e += (double) chunkDry[(size_t) i] * chunkDry[(size_t) i];
            if (10.0 * std::log10 (e / 2048.0 + 1e-20) > -60.0)
            {
                accDry.add (dDb); accWet.add (wDb);
                learnSeconds += 2048.0 / sr;
            }
            learnProg.store ((float) juce::jlimit (0.0, 1.0, learnSeconds / 5.0));
            if (learnSeconds >= 5.0) finishLearn();
        }
    }
}

void MeldVerbProcessor::getSpectra (float* dryDb, float* wetDb) const
{
    const juce::ScopedLock l (specLock);
    std::copy (std::begin (liveDry.db), std::end (liveDry.db), dryDb);
    std::copy (std::begin (liveWet.db), std::end (liveWet.db), wetDb);
}

void MeldVerbProcessor::startLearn (LearnMode m)
{
    accDry.clear(); accWet.clear(); learnSeconds = 0; learnProg.store (0.0f);
    { const juce::ScopedLock l (msgLock); learnMsg = m == LearnMode::pre ? "Listening to the input... play the part (5 s)" : "Listening to dry and reverb... play the part (5 s)"; }
    learnMode.store (m);
}

void MeldVerbProcessor::cancelLearn()
{
    learnMode.store (LearnMode::none);
    const juce::ScopedLock l (msgLock); learnMsg = "Cancelled";
}

void MeldVerbProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void MeldVerbProcessor::finishLearn()
{
    const auto mode = learnMode.exchange (LearnMode::none);
    float d[specBands], w[specBands];
    accDry.mean (d); accWet.mean (w);
    juce::String msg;
    if (mode == LearnMode::pre)
    {
        const auto r = assist::learnPre (d);
        setParam ("preOn", 1);
        if (r.found)
        {
            setParam ("preBellFreq", r.bellFreq); setParam ("preBellGain", r.bellGain); setParam ("preBellQ", r.bellQ);
            msg << "Resonance at " << juce::String (juce::roundToInt (r.bellFreq)) << " Hz (+" << juce::String (r.excessDb, 1) << " dB) -> bell " << juce::String (r.bellGain, 1) << " dB";
        }
        else msg << "No strong resonance found in the input";
        if (r.lowCut) { setParam ("preLowShape", 1); setParam ("preLowFreq", r.lowCutHz); setParam ("preLowSlope", 0.707f); msg << ", low cut " << juce::String ((int) r.lowCutHz) << " Hz"; }
    }
    else if (mode == LearnMode::unmask)
    {
        const float mix = juce::jlimit (0.01f, 0.99f, p ("mix") / 100.0f);
        const auto r = assist::learnUnmask (d, w, 20.0f * std::log10 (mix / (1.0f - mix)));
        juce::ValueTree t ("UNMASK");
        t.setProperty ("found", r.found, nullptr);
        t.setProperty ("lowGain", r.lowGain, nullptr); t.setProperty ("lowFreq", r.lowFreq, nullptr);
        t.setProperty ("bellGain", r.bellGain, nullptr); t.setProperty ("bellFreq", r.bellFreq, nullptr); t.setProperty ("bellQ", r.bellQ, nullptr);
        t.setProperty ("highGain", r.highGain, nullptr); t.setProperty ("highFreq", r.highFreq, nullptr);
        auto old = apvts.state.getChildWithName ("UNMASK");
        if (old.isValid()) apvts.state.removeChild (old, nullptr);
        apvts.state.addChild (t, -1, nullptr);
        applyUnmaskAmount();
        if (r.found)
            msg << "Unmask: low " << juce::String (2 * r.lowGain, 1) << " dB, " << juce::String (juce::roundToInt (r.bellFreq)) << " Hz " << juce::String (r.bellGain, 1)
                << " dB, high " << juce::String (2 * r.highGain, 1) << " dB (at 100%)";
        else msg << "The reverb is not hiding the dry sound - nothing to cut";
    }
    learnProg.store (1.0f);
    const juce::ScopedLock l (msgLock); learnMsg = msg;
}

void MeldVerbProcessor::applyUnmaskAmount()
{
    auto t = apvts.state.getChildWithName ("UNMASK");
    if (! t.isValid() || ! (bool) t.getProperty ("found", false)) return;
    const float a = p ("unmaskAmt") / 100.0f;
    setParam ("revOn", 1);
    setParam ("revLowShape", 0); setParam ("revLowFreq", (float) t.getProperty ("lowFreq")); setParam ("revLowGain", a * (float) t.getProperty ("lowGain")); setParam ("revLowSlope", 0.707f);
    setParam ("revBellFreq", (float) t.getProperty ("bellFreq")); setParam ("revBellGain", a * (float) t.getProperty ("bellGain")); setParam ("revBellQ", (float) t.getProperty ("bellQ"));
    setParam ("revHighShape", 0); setParam ("revHighFreq", (float) t.getProperty ("highFreq")); setParam ("revHighGain", a * (float) t.getProperty ("highGain")); setParam ("revHighSlope", 0.707f);
}

meld::Early::View MeldVerbProcessor::earlyView() const
{
    meld::Early e; e.prepare (sr);
    const auto q = readParams();
    e.setParams (q.early);
    return e.view();
}

double MeldVerbProcessor::eqMagDb (bool reverb, double f) const
{
    meld::TriEQ eq;
    const auto q = readParams();
    eq.setup (reverb ? q.revEq : q.preEq, sr);
    return eq.magDb (f, sr);
}

//==============================================================================
juce::StringArray MeldVerbProcessor::presetNames()
{
    return { "Default", "Vocal Plate", "Snare Plate", "Drum Room", "Big Hall", "Piano Hall", "Ambient Wash", "Short Slap", "Wide Early", "Dark Cathedral" };
}

void MeldVerbProcessor::applyPreset (int index)
{
    // まず全部を初期値へ
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (r->getParameterID() != "unmaskAmt") r->setValueNotifyingHost (r->getDefaultValue());
    struct KV { const char* id; float v; };
    auto set = [this] (std::initializer_list<KV> kv) { for (auto& x : kv) setParam (x.id, x.v); };
    switch (index)
    {
        case 1: set ({ { "lvlE", -12 }, { "lvlP", 0 }, { "lvlH", -70 }, { "pTime", 1.6f }, { "pSize", 9 }, { "pAttack", 10 }, { "pLp", 6500 }, { "pBal", 0.55f },
                       { "predelay", 40 }, { "mix", 25 }, { "revLowShape", 1 }, { "revLowFreq", 180 }, { "smoothOn", 1 } }); break;
        case 2: set ({ { "lvlE", -9 }, { "lvlP", 0 }, { "lvlH", -18 }, { "pTime", 1.2f }, { "pSize", 6 }, { "pDiff", 70 }, { "pAttack", 0 }, { "pLp", 9000 }, { "pBal", 0.45f },
                       { "predelay", 8 }, { "mix", 30 } }); break;
        case 3: set ({ { "lvlE", 0 }, { "lvlP", -12 }, { "lvlH", -16 }, { "eTime", 70 }, { "eSize", 12 }, { "eAngle", 35 }, { "eLp", 2500 }, { "hTime", 0.9f }, { "hSize", 8 },
                       { "pTime", 0.6f }, { "predelay", 3 }, { "mix", 30 } }); break;
        case 4: set ({ { "lvlE", -10 }, { "lvlP", -16 }, { "lvlH", 0 }, { "hTime", 4.5f }, { "hSize", 26 }, { "hAttack", 90 }, { "hLp", 6500 }, { "hBal", 0.55f },
                       { "predelay", 35 }, { "modDepth", 35 }, { "mix", 35 } }); break;
        case 5: set ({ { "lvlE", -8 }, { "lvlP", -20 }, { "lvlH", 0 }, { "hTime", 2.8f }, { "hSize", 18 }, { "hAttack", 60 }, { "hLp", 5200 }, { "hBal", 0.5f }, { "hDiff", 60 },
                       { "predelay", 25 }, { "mix", 28 }, { "smoothOn", 1 } }); break;
        case 6: set ({ { "lvlE", -70 }, { "lvlP", -6 }, { "lvlH", 0 }, { "hTime", 9 }, { "hSize", 36 }, { "hAttack", 220 }, { "hLp", 4500 }, { "pTime", 6 }, { "pSize", 30 },
                       { "pAttack", 180 }, { "modDepth", 70 }, { "modType", 1 }, { "predelay", 60 }, { "mix", 50 } }); break;
        case 7: set ({ { "lvlE", 0 }, { "lvlP", -70 }, { "lvlH", -24 }, { "eTime", 120 }, { "eSize", 5 }, { "eDiff", 20 }, { "eAngle", 55 }, { "eLp", 3000 }, { "hTime", 0.6f },
                       { "predelay", 0 }, { "mix", 25 } }); break;
        case 8: set ({ { "lvlE", 0 }, { "lvlP", -18 }, { "lvlH", -12 }, { "eTime", 180 }, { "eSize", 20 }, { "eAngle", 85 }, { "eLp", 6000 }, { "hTime", 1.8f }, { "mix", 30 } }); break;
        case 9: set ({ { "lvlE", -14 }, { "lvlP", -70 }, { "lvlH", 0 }, { "hTime", 7 }, { "hSize", 40 }, { "hAttack", 120 }, { "hLp", 2200 }, { "hDamp", 2 }, { "hBal", 0.35f },
                       { "hCross", 900 }, { "predelay", 45 }, { "mix", 40 } }); break;
        default: break;
    }
}

//==============================================================================
void MeldVerbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void MeldVerbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MeldVerbProcessor::createEditor() { return new MeldVerbEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MeldVerbProcessor(); }
