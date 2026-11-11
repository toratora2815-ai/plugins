#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* kIds[] = { "input", "output", "mix", "analog", "style", "filters", "routing", "filterLink",
                           "freq1", "res1", "shape1", "poles1", "gain1", "freqMod1", "resMod1", "levelMod1",
                           "freq2", "res2", "shape2", "poles2", "gain2", "freqMod2", "resMod2", "levelMod2",
                           "mod", "modMode", "lfoRate", "lfoShape", "rhythm", "tempo", "sync", "groove",
                           "threshold", "envGain", "attack", "release", "trigLevel", "trigger", "adsrA", "adsrD", "adsrS", "adsrR",
                           "lrOffset", "lrMode", "smoothing", "smoothMode", myp::GainMatch::paramId, "linkDuck" };

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float v) { return a * std::pow (b / a, v); },
                 [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); },
                 [] (float a, float b, float x) { return juce::jlimit (a, b, x); } };
    }
    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
    }
    juce::String msText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }

    // つまみの値（Hz）⇄ 本物の 0..1（20 Hz × 1000^v）
    inline float hzToNorm (float hz) { return std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f); }
}

MyFilterProcessor::MyFilterProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < pNum; ++i) p[i] = apvts.getRawParameterValue (kIds[i]);
    myPresets.exclude ("linkDuck");
    myPresets.exclude ("trigger");
    customShape.setSine();
    engine.setPattern (pattern);
    engine.setCustomShape (customShape);
    for (auto id : { "freq1", "freq2", "res1", "res2", "gain1", "gain2" })
    {
        apvts.addParameterListener (id, this);
        lastNorm[id] = apvts.getParameter (id)->getValue();
    }
}

MyFilterProcessor::~MyFilterProcessor()
{
    for (auto id : { "freq1", "freq2", "res1", "res2", "gain1", "gain2" })
        apvts.removeParameterListener (id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MyFilterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto spct = [] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; };
    auto db1 = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return hzText (v); };
    auto ms = [] (float v, int) { return msText (v); };
    auto oct = [] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, 2) + " oct"; };

    ps.push_back (std::make_unique<F> (juce::ParameterID { "input", 1 }, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "analog", 1 }, "Analog", juce::StringArray { "Digital", "Analog" }, 1));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "style", 1 }, "Analog Style", juce::StringArray { "Clean", "Fat", "Squash", "Dirt", "Crunch", "Shred", "Pump" }, 0));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "filters", 1 }, "Filters", juce::StringArray { "1 Filter", "2 Filters" }, 0));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "routing", 1 }, "Routing", juce::StringArray { "Serial", "Parallel" }, 1));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "filterLink", 1 }, "Filter Link", false));

    const juce::StringArray shapes { "Lowpass", "Bandpass", "Highpass", "Notch" };
    const juce::StringArray poles { "2 Pole", "4 Pole", "6 Pole", "8 Pole" };
    for (int f = 1; f <= 2; ++f)
    {
        const juce::String n (f);
        ps.push_back (std::make_unique<F> (juce::ParameterID { "freq" + n, 1 }, "Frequency " + n, logRange (20.0f, 20000.0f), f == 1 ? 4968.0f : 880.0f, A().withLabel ("Hz").withStringFromValueFunction (hz)));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "res" + n, 1 }, "Resonance " + n, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "shape" + n, 1 }, "Shape " + n, shapes, f == 1 ? 0 : 1));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "poles" + n, 1 }, "Poles " + n, poles, 0));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "gain" + n, 1 }, "Gain " + n, juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "freqMod" + n, 1 }, "Freq Mod " + n, juce::NormalisableRange<float> (-10.0f, 10.0f, 0.01f), 8.0f, A().withLabel ("oct").withStringFromValueFunction (oct)));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "resMod" + n, 1 }, "Res Mod " + n, juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (spct)));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "levelMod" + n, 1 }, "Level Mod " + n, juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (spct)));
    }
    ps.push_back (std::make_unique<F> (juce::ParameterID { "mod", 1 }, "Mod", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    {
        juce::StringArray modes;
        for (int i = 0; i < myfilt::kNumModes; ++i) modes.add (myfilt::modeName (i));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "modMode", 1 }, "Mod Mode", modes, myfilt::kEnvelope));
    }
    ps.push_back (std::make_unique<F> (juce::ParameterID { "lfoRate", 1 }, "LFO Rate", logRange (0.01f, 100.0f), 0.25f,
                                       A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 1.0f ? 3 : (v < 10.0f ? 2 : 1)) + " Hz"; })));
    {
        juce::StringArray sh;
        for (int i = 0; i < mytrem::kNumShapes; ++i) sh.add (mytrem::shapeName (i));
        sh.add ("Custom");
        ps.push_back (std::make_unique<C> (juce::ParameterID { "lfoShape", 1 }, "Mod Shape", sh, 0));
        juce::StringArray rh;
        for (int i = 0; i < mytrem::kNumRhythms; ++i) rh.add (mytrem::rhythmName (i));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "rhythm", 1 }, "Rhythm", rh, 9));
    }
    ps.push_back (std::make_unique<F> (juce::ParameterID { "tempo", 1 }, "Tempo", juce::NormalisableRange<float> (30.0f, 240.0f, 0.1f), 120.0f, A().withLabel ("BPM")));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "sync", 1 }, "Sync to Logic", true));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "groove", 1 }, "Groove", juce::NormalisableRange<float> (-25.0f, 25.0f, 0.1f), 0.0f,
                                       A().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
                                           if (std::abs (v) < 0.05f) return juce::String ("0");
                                           return juce::String (v < 0 ? "Shuffle " : "Swing ") + juce::String (std::abs (v), 1) + " %"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "threshold", 1 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, A().withLabel ("dB")));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "envGain", 1 }, "Envelope Gain", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "attack", 1 }, "Attack", logRange (1.0f, 5000.0f), 570.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "release", 1 }, "Release", logRange (1.0f, 5000.0f), 550.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "trigLevel", 1 }, "Trigger Level", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, A().withLabel ("dB")));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "trigger", 1 }, "Trigger", false));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "adsrA", 1 }, "ADSR Attack", logRange (1.0f, 5000.0f), 10.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "adsrD", 1 }, "ADSR Decay", logRange (1.0f, 5000.0f), 300.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "adsrS", 1 }, "ADSR Sustain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "adsrR", 1 }, "ADSR Release", logRange (1.0f, 5000.0f), 500.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "lrOffset", 1 }, "L/R Offset", juce::NormalisableRange<float> (-2.0f, 2.0f, 0.01f), 0.0f, A().withLabel ("oct").withStringFromValueFunction (oct)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "lrMode", 1 }, "L/R Mode", juce::StringArray { "Normal", "Flipped" }, 0));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "smoothing", 1 }, "Smoothing", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "smoothMode", 1 }, "Smoothing Mode", juce::StringArray { "Lin", "Sin", "Exp", "Sym", "Rev" }, 1));
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "linkDuck", 1 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                       A().withStringFromValueFunction (pct)));
    return { ps.begin(), ps.end() };
}

bool MyFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in == out) return true;
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();   // モノ → ステレオ（L/R OFFSET・L/R MODE・2 つのフィルターを左右に）
}

void MyFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nIn = juce::jmax (1, getTotalNumInputChannels()), nOut = juce::jmax (1, getTotalNumOutputChannels());
    monoToStereo.store (nIn == 1 && nOut == 2);
    engine.prepare (sampleRate, nIn, nOut);
    engine.setPattern (getPattern());
    engine.setCustomShape (getCustomShape());
    setLatencySamples (engine.latencySamples());
    gainMatch.prepare (sampleRate, nOut, samplesPerBlock);
}

myfilt::Knobs MyFilterProcessor::currentKnobs() const
{
    myfilt::Knobs k;
    k.inDb = p[pIn]->load();
    k.outDb = p[pOut]->load();
    k.mix = p[pMix]->load() / 100.0f;
    k.style = p[pAnalog]->load() > 0.5f ? 1 + (int) p[pStyle]->load() : 0;
    k.filters = p[pFilters]->load() > 0.5f ? 2 : 1;
    k.parallel = p[pRouting]->load() > 0.5f;
    for (int f = 0; f < 2; ++f)
    {
        const int o = f == 0 ? 0 : (pFreq2 - pFreq1);
        k.freq[f] = hzToNorm (p[pFreq1 + o]->load());
        k.res[f] = p[pRes1 + o]->load() / 100.0f;
        k.shape[f] = (int) p[pShape1 + o]->load();
        k.poles[f] = 2 + 2 * (int) p[pPoles1 + o]->load();
        k.gainDb[f] = p[pGain1 + o]->load();
        k.freqModOct[f] = p[pFMod1 + o]->load();
        k.resMod[f] = p[pRMod1 + o]->load() / 100.0f;
        k.levelMod[f] = p[pLMod1 + o]->load() / 100.0f;
    }
    k.mod = p[pMod]->load() / 100.0f;
    k.mode = (int) p[pMode]->load();
    k.lfoHz = p[pLfoRate]->load();
    k.shapeIdx = (int) p[pLfoShape]->load();
    k.rhythm = (int) p[pRhythm]->load();
    k.tempoBpm = p[pTempo]->load();
    k.sync = p[pSync]->load() > 0.5f;
    k.groove = p[pGroove]->load() / 100.0f;
    k.thresholdDb = p[pThr]->load();
    k.envGainDb = p[pEnvGain]->load();
    k.attackMs = p[pAtt]->load();
    k.releaseMs = p[pRel]->load();
    k.trigDb = p[pTrigLvl]->load();
    k.trigger = p[pTrigger]->load() > 0.5f;
    k.aMs = p[pA]->load(); k.dMs = p[pD]->load(); k.sustain = p[pS]->load() / 100.0f; k.rMs = p[pR]->load();
    k.lrOffsetOct = p[pLrOff]->load();
    k.lrFlip = p[pLrMode]->load() > 0.5f;
    k.smoothing = p[pSmooth]->load() / 100.0f;
    k.smoothMode = (int) p[pSmoothMode]->load();
    return k;
}

void MyFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    // 入っていない出口のチャンネルは、エンジンが入口から作る（モノ → ステレオ）ので、ここでは消さない
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    myfilt::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.hostBpm = *b;
            tr.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { tr.ppq = *q; tr.hasPosition = true; }
        }
    auto k = currentKnobs();
    shownBpm.store (k.sync ? tr.hostBpm : (double) k.tempoBpm);

    // Plugin Link: Fader Pilot が歌っている間、MOD を linkDuck % だけ浅く
    {
        const float g = duck.next (link, 1.0f, n, getSampleRate());            // 1（歌っていない）〜 0.891（歌っている）
        const float a = juce::jlimit (0.0f, 1.0f, (1.0f - g) / (1.0f - 0.8912509f));
        k.duck = 1.0f - a * p[pLinkDuck]->load() / 100.0f;
    }

    engine.process (buffer.getArrayOfWritePointers(), n, k, tr);

    // GAIN MATCH: 比べる元がない間（MIX 0 %）は効かせない
    gainMatch.process (buffer, juce::jmin (numOut, myfilt::Engine::maxCh), getLatencySamples(), k.mix > 0.001f ? 1.0f : 0.0f);
}

//==============================================================================
// LINK: 1 と 2 の FREQ・RES・GAIN を、同じ向きに同じだけ動かす（本物と同じ。片方が端に着いたら、もう片方もそれ以上は動かない）
void MyFilterProcessor::parameterChanged (const juce::String& id, float)
{
    auto* prm = apvts.getParameter (id);
    if (prm == nullptr) return;
    const float now = prm->getValue();
    const float before = lastNorm[id];
    lastNorm[id] = now;
    if (linking || p[pFLink]->load() < 0.5f || p[pFilters]->load() < 0.5f) return;
    // 画面でつまみをつかんでいる時だけ（自分のプリセット・取り消し・オートメーションでは、両方の値がもう入っている）
    if (linkGesture.load() <= 0 || ! juce::MessageManager::existsAndIsCurrentThread()) return;
    const juce::String other = id.dropLastCharacters (1) + (id.endsWith ("1") ? "2" : "1");
    if (auto* o = apvts.getParameter (other))
    {
        const juce::ScopedValueSetter<bool> guard (linking, true);
        const float to = juce::jlimit (0.0f, 1.0f, o->getValue() + (now - before));
        o->setValueNotifyingHost (to);
        lastNorm[other] = to;
    }
}

void MyFilterProcessor::setPattern (const mytrem::Pattern& pt)
{
    { const juce::ScopedLock sl (editLock); pattern = pt; }
    engine.setPattern (pt);
    ++editVersion;
}

void MyFilterProcessor::setCustomShape (const mytrem::CustomShape& c)
{
    { const juce::ScopedLock sl (editLock); customShape = c; }
    engine.setCustomShape (c);
    ++editVersion;
}

float MyFilterProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 6) taps.erase (taps.begin());
    if (taps.size() < 2) return 0.0f;
    const double per = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    const float bpm = juce::jlimit (30.0f, 240.0f, (float) (60.0 / per));
    if (auto* prm = apvts.getParameter ("tempo"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (bpm));
        prm->endChangeGesture();
    }
    return bpm;
}

//==============================================================================
juce::String MyFilterProcessor::patternToString (const mytrem::Pattern& pt)
{
    juce::String s;
    s << pt.bars << ";" << pt.beatsPerBar << ";" << pt.grid << ";";
    for (int i = 0; i < pt.numCells(); ++i) s << (int) pt.cell[(size_t) i];
    return s;
}

bool MyFilterProcessor::patternFromString (const juce::String& s, mytrem::Pattern& pt)
{
    auto parts = juce::StringArray::fromTokens (s, ";", "");
    if (parts.size() < 4) return false;
    mytrem::Pattern q;
    q.bars = juce::jlimit (1, 4, parts[0].getIntValue());
    q.beatsPerBar = juce::jlimit (2, 7, parts[1].getIntValue());
    q.grid = juce::jlimit (0, mytrem::kNumGrids - 1, parts[2].getIntValue());
    q.cell.fill (0);
    const auto c = parts[3];
    for (int i = 0; i < q.numCells() && i < c.length(); ++i) q.cell[(size_t) i] = (uint8_t) juce::jlimit (0, 2, (int) (c[i] - '0'));
    pt = q;
    return true;
}

juce::String MyFilterProcessor::shapeToString (const mytrem::CustomShape& c)
{
    juce::String s;
    for (int i = 0; i < c.n; ++i) s << juce::String (c.x[(size_t) i], 4) << "," << juce::String (c.y[(size_t) i], 4) << ";";
    return s;
}

bool MyFilterProcessor::shapeFromString (const juce::String& s, mytrem::CustomShape& c)
{
    auto pts = juce::StringArray::fromTokens (s, ";", "");
    pts.removeEmptyStrings();
    if (pts.size() < 2) return false;
    mytrem::CustomShape q; q.n = 0;
    for (auto& t : pts)
    {
        if (q.n >= mytrem::CustomShape::kMaxPoints) break;
        auto xy = juce::StringArray::fromTokens (t, ",", "");
        if (xy.size() < 2) continue;
        q.x[(size_t) q.n] = juce::jlimit (0.0f, 1.0f, xy[0].getFloatValue());
        q.y[(size_t) q.n] = juce::jlimit (0.0f, 1.0f, xy[1].getFloatValue());
        ++q.n;
    }
    if (q.n < 2) return false;
    q.x[0] = 0.0f; q.x[(size_t) q.n - 1] = 1.0f;
    c = q;
    return true;
}

void MyFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("pattern", patternToString (getPattern()), nullptr);
    st.setProperty ("customShape", shapeToString (getCustomShape()), nullptr);
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void MyFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            {
                const juce::ScopedValueSetter<bool> guard (linking, true);   // 読み込みで LINK が動かないように
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
            }
            for (auto& kv : lastNorm) kv.second = apvts.getParameter (kv.first)->getValue();
            mytrem::Pattern pt;
            if (patternFromString (apvts.state.getProperty ("pattern").toString(), pt)) setPattern (pt);
            mytrem::CustomShape c;
            if (shapeFromString (apvts.state.getProperty ("customShape").toString(), c)) setCustomShape (c);
            gainMatch.loadFrom (apvts.state);
        }
}

//==============================================================================
// 内蔵プリセット（本物の内蔵プリセットの分類 = スイープ・リズム・EQ・ギター・ベース・ドラム・効果 を参考に、名前と数値は付け直した）
namespace
{
    struct FP { const char* name; const char* values; const char* pattern; };
    const FP kFactory[] = {
        { "Init (Envelope)",            "", nullptr },
        { "Auto-Wah Funk",              "shape1=1,res1=60,freq1=280,mod=80,freqMod1=4,modMode=2,threshold=-30,envGain=12,attack=15,release=180", nullptr },
        { "Envelope Low Funk 24dB",     "shape1=0,poles1=1,res1=50,freq1=180,mod=70,freqMod1=6,modMode=2,threshold=-32,attack=8,release=250", nullptr },
        { "Slow Sweep 12dB",            "shape1=0,res1=50,freq1=150,mod=80,freqMod1=8,modMode=0,lfoShape=1,lfoRate=0.12", nullptr },
        { "Resonant Highpass Sweep",    "shape1=2,res1=70,freq1=60,mod=75,freqMod1=8,modMode=0,lfoShape=1,lfoRate=0.08", nullptr },
        { "Notch Phaser Sweep",         "shape1=3,poles1=1,res1=30,freq1=200,mod=70,freqMod1=6,modMode=0,lfoShape=0,lfoRate=0.3", nullptr },
        { "1/8 Wah Rhythm",             "shape1=1,res1=60,freq1=400,mod=60,freqMod1=5,modMode=1,rhythm=9,lfoShape=0", nullptr },
        { "1/16 Ramp Gate",             "shape1=0,poles1=1,res1=40,freq1=120,mod=85,freqMod1=7,modMode=1,rhythm=12,lfoShape=6", nullptr },
        { "Swing Pattern Filter",       "shape1=0,poles1=1,res1=55,freq1=150,mod=80,freqMod1=6,modMode=1,rhythm=15,lfoShape=6,groove=16", "1;4;3;1011101110111011" },
        { "Random Sample & Hold 1/16",  "shape1=0,res1=70,freq1=300,mod=70,freqMod1=6,modMode=3,rhythm=12,smoothing=0", nullptr },
        { "Random Glide 1/8",           "shape1=1,res1=55,freq1=300,mod=70,freqMod1=5,modMode=3,rhythm=9,smoothing=70", nullptr },
        { "Drum Step Filter",           "shape1=1,res1=45,freq1=250,mod=70,freqMod1=5,modMode=4,trigLevel=-24,smoothing=5", nullptr },
        { "Synth Pluck ADSR",           "shape1=0,poles1=1,res1=60,freq1=120,mod=90,freqMod1=6,modMode=5,trigLevel=-30,adsrA=2,adsrD=180,adsrS=10,adsrR=150", nullptr },
        { "Talking Wah (2 filters)",    "filters=1,routing=1,shape1=1,shape2=1,res1=65,res2=65,freq1=500,freq2=1400,mod=60,freqMod1=1.5,freqMod2=-1,modMode=0,lfoShape=0,lfoRate=0.8", nullptr },
        { "Telephone",                  "filters=1,routing=0,shape1=2,poles1=1,shape2=0,poles2=1,freq1=450,freq2=3200,res1=20,res2=20,mod=0,style=4,input=6,output=-4", nullptr },
        { "Fat Low Boost",              "shape1=0,res1=45,freq1=110,mod=0,style=1,input=4", nullptr },
        // 1.1.0: 曲全体（マスターかバス）にかけてキメを作る用。MIX 0 % にしておき、キメの小節だけ MIX を上げる使い方（README）
        // 0 dBFS 近い曲で、ピークは入力以下・RMS −1〜−6 dB（render_myfilter で確認）
        { "Mix Build-Up LP 4 Bars",     "shape1=0,poles1=1,res1=30,freq1=150,mod=100,freqMod1=7,modMode=1,rhythm=0,lfoShape=5", nullptr },
        { "Mix Build-Up HP 4 Bars",     "shape1=2,poles1=0,res1=25,freq1=20,mod=100,freqMod1=5.6,modMode=1,rhythm=0,lfoShape=5", nullptr },
        { "Mix Muffled Break",          "shape1=0,poles1=1,res1=15,freq1=400,mod=0", nullptr },
        { "Mix Radio Break",            "filters=1,routing=0,shape1=2,poles1=1,shape2=0,poles2=1,freq1=350,freq2=3500,res1=15,res2=15,mod=0,output=4", nullptr },
        { "Mix HP Drop Out",            "shape1=2,poles1=1,res1=20,freq1=250,mod=0", nullptr },
        { "Mix 1/16 Stab",              "shape1=0,poles1=1,res1=25,freq1=200,mod=85,freqMod1=7,modMode=1,rhythm=12,lfoShape=6", nullptr },
        { "Mix 1/8 Gate Stab",          "shape1=0,poles1=1,res1=25,freq1=200,mod=85,freqMod1=7,levelMod1=100,modMode=1,rhythm=9,lfoShape=6", nullptr },
        { "Mix Kime Pattern",           "shape1=0,poles1=1,res1=25,freq1=200,mod=85,freqMod1=7,modMode=1,rhythm=15,lfoShape=6", "1;4;3;1001001001001010" },
        { "Mix Hit Pluck (ADSR)",       "shape1=0,poles1=1,res1=30,freq1=150,mod=90,freqMod1=7,modMode=5,trigLevel=-10,adsrA=1,adsrD=120,adsrS=0,adsrR=80", nullptr },
    };
}

int MyFilterProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String MyFilterProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void MyFilterProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    myPresets.snapshot();
    // まず全部を初期値に（GAIN MATCH・LINK の番号・VOCAL DUCK・TRIGGER は触らない）
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == myp::GainMatch::paramId || id == "linkDuck" || id == "trigger" || id == "linkGroup") continue;
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
    const juce::ScopedValueSetter<bool> guard (linking, true);
    for (auto& kv : juce::StringArray::fromTokens (f.values, ",", ""))
    {
        const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
        const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (auto* r = apvts.getParameter (id))
        {
            const bool choice = dynamic_cast<juce::AudioParameterChoice*> (r) != nullptr || dynamic_cast<juce::AudioParameterBool*> (r) != nullptr;
            r->beginChangeGesture();
            r->setValueNotifyingHost (choice ? r->convertTo0to1 (v) : r->convertTo0to1 (v));
            r->endChangeGesture();
        }
    }
    for (auto& kv : lastNorm) kv.second = apvts.getParameter (kv.first)->getValue();
    myPresets.commitSinceSnapshot();
    mytrem::Pattern pt;
    if (f.pattern != nullptr && patternFromString (f.pattern, pt)) setPattern (pt);
    apvts.state.setProperty ("mypPreset", juce::String ("[") + f.name + "]", nullptr);
    gainMatch.requestRelearn();
}

juce::AudioProcessorEditor* MyFilterProcessor::createEditor()
{
    return new MyFilterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyFilterProcessor();
}
