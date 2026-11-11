#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* kIds[] = { "input", "output", "mix", "analog", "pitch", "direction", "splice", "spliceNote", "delay", "delayNote", "sync", "recycle",
                           "threshold", "gateDuck", "pitchOffset", "spliceOffset", "delayOffset", "smoothing", "lowCut", "highCut", "attack", "release",
                           "duckMode", "fbMode", "linkDuck" };

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float v) { return a * std::pow (b / a, v); },
                 [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); },
                 [] (float a, float b, float x) { return juce::jlimit (a, b, x); } };
    }
    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 100.0f ? (v < 10.0f ? 2 : 1) : 0) + " Hz";
    }
    juce::String msText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String centsText (float v)
    {
        const int c = juce::roundToInt (v);
        juce::String s = (c > 0 ? "+" : "") + juce::String (c) + " c";
        if (c != 0 && c % 100 == 0) s << "  (" << (c > 0 ? "+" : "") << c / 100 << " st)";
        return s;
    }
}

juce::NormalisableRange<float> MyCrystalProcessor::expRange (float maxValue, float k)
{
    // 値 = 最大 × (e^(k·v) − 1) / (e^k − 1)（v = 0..1）。0 の近くが細かい
    return { 0.0f, maxValue,
             [k] (float, float b, float v) { return b * (std::exp (k * v) - 1.0f) / (std::exp (k) - 1.0f); },
             [k] (float, float b, float x) { return std::log (1.0f + juce::jlimit (0.0f, b, x) / b * (std::exp (k) - 1.0f)) / k; },
             [] (float a, float b, float x) { return juce::jlimit (a, b, x); } };
}

MyCrystalProcessor::MyCrystalProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < pNum; ++i) p[i] = apvts.getRawParameterValue (kIds[i]);
    myPresets.exclude ("linkDuck");
}

juce::AudioProcessorValueTreeState::ParameterLayout MyCrystalProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto pct1 = [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " %"; };
    auto db1 = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return hzText (v); };
    auto ms = [] (float v, int) { return msText (v); };

    ps.push_back (std::make_unique<F> (juce::ParameterID { "input", 1 }, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "analog", 1 }, "Analog", juce::StringArray { "Digital", "Analog" }, 1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "pitch", 1 }, "Pitch", juce::NormalisableRange<float> (-3600.0f, 3600.0f, 1.0f), 0.0f,
                                       A().withLabel ("cents").withStringFromValueFunction ([] (float v, int) { return centsText (v); })));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "direction", 1 }, "Direction", juce::StringArray { "Forward", "Reverse" }, 1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "splice", 1 }, "Splice", expRange (2050.0f, 6.0f), 500.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    {
        juce::StringArray sn, dn;
        for (int i = 0; i < mycry::kNumSpliceNotes; ++i) sn.add (mycry::spliceNoteName (i));
        for (int i = 0; i < mycry::kNumDelayNotes; ++i) dn.add (mycry::delayNoteName (i));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "spliceNote", 1 }, "Splice (Sync)", sn, 3));
        ps.push_back (std::make_unique<F> (juce::ParameterID { "delay", 1 }, "Delay", expRange (2050.0f, 6.0f), 100.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "delayNote", 1 }, "Delay (Sync)", dn, 4));
    }
    ps.push_back (std::make_unique<B> (juce::ParameterID { "sync", 1 }, "Sync to Logic", true));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "recycle", 1 }, "Recycle", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 21.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "threshold", 1 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -40.0f,
                                       A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "gateDuck", 1 }, "Gate / Duck", juce::NormalisableRange<float> (-60.0f, 60.0f, 0.1f), 0.0f,
                                       A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                                           if (std::abs (v) < 0.25f) return juce::String ("OFF");
                                           return juce::String (v > 0 ? "DUCK " : "GATE ") + juce::String (std::abs (v), 1) + " dB"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "pitchOffset", 1 }, "Pitch Offset", expRange (4800.0f, 8.48f), 12.0f,
                                       A().withLabel ("cents").withStringFromValueFunction ([] (float v, int) { return "L -" + juce::String (juce::roundToInt (v)) + " / R +" + juce::String (juce::roundToInt (v)) + " c"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "spliceOffset", 1 }, "Splice Offset", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 6.0f, A().withLabel ("%").withStringFromValueFunction (pct1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "delayOffset", 1 }, "Delay Offset", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 2.0f, A().withLabel ("%").withStringFromValueFunction (pct1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "smoothing", 1 }, "Smoothing", juce::NormalisableRange<float> (20.0f, 2000.0f, 1.0f), 20.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "lowCut", 1 }, "Low Cut", logRange (1.0f, 5000.0f), 10.0f, A().withLabel ("Hz").withStringFromValueFunction (hz)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "highCut", 1 }, "High Cut", logRange (500.0f, 20000.0f), 10000.0f, A().withLabel ("Hz").withStringFromValueFunction (hz)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "attack", 1 }, "Attack", logRange (0.1f, 5000.0f), 100.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "release", 1 }, "Release", logRange (0.1f, 5000.0f), 500.0f, A().withLabel ("ms").withStringFromValueFunction (ms)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "duckMode", 1 }, "Duck Mode", juce::StringArray { "Output", "Recycle", "Both" }, 0));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "fbMode", 1 }, "Feedback Mode", juce::StringArray { "Mixed", "Dual", "Ping-Pong" }, 0));
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "linkDuck", 1 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 4.0f,
                                       A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    return { ps.begin(), ps.end() };
}

bool MyCrystalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in == out) return true;
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();   // モノ → ステレオ（左右のずらしで広がる）
}

void MyCrystalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nOut = juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, nOut);
    setLatencySamples (0);   // 元の音は遅らせない（粒をためる時間はエコーの一部。Logic に遅れとして伝えない）
    gainMatch.prepare (sampleRate, nOut, samplesPerBlock);
}

mycry::Knobs MyCrystalProcessor::currentKnobs() const
{
    mycry::Knobs k;
    k.inDb = p[pIn]->load();
    k.outDb = p[pOut]->load();
    k.mix = p[pMix]->load() / 100.0f;
    k.analog = p[pAnalog]->load() > 0.5f;
    k.pitchCents = p[pPitch]->load();
    k.reverse = p[pDir]->load() > 0.5f;
    k.spliceMs = juce::jmax (1.0f, p[pSplice]->load());
    k.spliceNote = (int) p[pSpliceNote]->load();
    k.delayMs = p[pDelay]->load();
    k.delayNote = (int) p[pDelayNote]->load();
    k.sync = p[pSync]->load() > 0.5f;
    k.recycle = p[pRecycle]->load() / 100.0f;
    k.thresholdDb = p[pThr]->load();
    k.gateDuckDb = p[pGateDuck]->load();
    k.pitchOffCents = p[pPitchOff]->load();
    k.spliceOffPct = p[pSpliceOff]->load();
    k.delayOffPct = p[pDelayOff]->load();
    k.smoothMs = p[pSmooth]->load();
    k.lowCutHz = p[pLowCut]->load();
    k.highCutHz = p[pHighCut]->load();
    k.attackMs = p[pAtt]->load();
    k.releaseMs = p[pRel]->load();
    k.duckMode = (int) p[pDuckMode]->load();
    k.fbMode = (int) p[pFbMode]->load();
    return k;
}

void MyCrystalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    // モノ → ステレオ: 右に左を写してから（粒は左右で少しずれるので広がる）
    if (numIn == 1 && numOut >= 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch) buffer.clear (ch, 0, n);

    mycry::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.hostBpm = *b;
            tr.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { tr.ppq = *q; tr.hasPosition = true; }
        }
    auto k = currentKnobs();
    shownBpm.store (tr.hostBpm);

    // Plugin Link: Fader Pilot が歌っている間、ウェットを linkDuck dB 下げる
    k.vocalDuck = duck.next (link, p[pLinkDuck]->load(), n, getSampleRate());

    if (needReset.exchange (false)) engine.reset();
    engine.process (buffer.getArrayOfWritePointers(), n, k, tr);

    // GAIN MATCH: センド使い（MIX 90〜100 %）は比べる元が薄いので、少しずつ効かせない
    gainMatch.process (buffer, juce::jmin (numOut, mycry::Engine::maxCh), 0, juce::jlimit (0.0f, 1.0f, (100.0f - p[pMix]->load()) / 10.0f));
}

void MyCrystalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void MyCrystalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

//==============================================================================
// 内蔵プリセット（使いどころ別。音程は調に合う ±12・+7・+5 を中心に。名前と数値は自分で付けた）
namespace
{
    struct FP { const char* name; const char* values; };
    const FP kFactory[] = {
        { "Init",                              "" },
        { "Crystal Octave (classic)",          "direction=1,pitch=1200,sync=1,spliceNote=3,delayNote=6,recycle=78,mix=25,lowCut=5,highCut=3800,pitchOffset=3,spliceOffset=1,delayOffset=5,threshold=-34,attack=190,release=5000" },
        { "Vocal Reverse Throw (send)",        "direction=1,pitch=0,sync=1,spliceNote=3,delayNote=4,recycle=28,mix=100,lowCut=220,highCut=7000,smoothing=2000,threshold=-32,gateDuck=18,attack=30,release=600,pitchOffset=6" },
        { "Guitar Octave Shimmer",             "direction=0,pitch=1200,sync=0,splice=520,delay=40,recycle=62,mix=100,lowCut=180,highCut=6500,smoothing=2000,pitchOffset=8,fbMode=0" },
        { "Fifth Pad",                         "direction=0,pitch=700,sync=0,splice=420,delay=0,recycle=58,mix=100,lowCut=150,highCut=6000,smoothing=2000,pitchOffset=10,spliceOffset=18" },
        { "Fourth Harmony",                    "direction=0,pitch=500,sync=0,splice=140,delay=0,recycle=15,mix=30,lowCut=120,highCut=9000,smoothing=60,pitchOffset=4" },
        { "Octave Down Ghost",                 "direction=0,pitch=-1200,sync=0,splice=260,delay=20,recycle=22,mix=35,lowCut=90,highCut=5000,smoothing=200,pitchOffset=6" },
        { "Rising Spiral",                     "direction=0,pitch=1200,sync=0,splice=500,delay=500,recycle=45,mix=40,lowCut=120,highCut=8000,pitchOffset=0" },
        { "Falling Spiral",                    "direction=0,pitch=-1200,sync=0,splice=500,delay=500,recycle=55,mix=40,lowCut=140,highCut=7000,pitchOffset=0" },
        { "Reverse Swell (pre-chorus)",        "direction=1,pitch=0,sync=1,spliceNote=5,delayNote=0,recycle=40,mix=100,lowCut=150,highCut=9000,smoothing=2000,pitchOffset=8" },
        { "Detune Double",                     "direction=0,pitch=0,sync=0,splice=60,delay=14,recycle=0,mix=38,lowCut=80,highCut=16000,smoothing=40,pitchOffset=12,spliceOffset=20,delayOffset=30" },
        { "Reverse Ping-Pong",                 "direction=1,pitch=0,sync=1,spliceNote=2,delayNote=4,recycle=50,mix=100,fbMode=2,lowCut=160,highCut=8000,smoothing=2000,pitchOffset=0" },
        { "Dark Crystal Wash",                 "direction=1,pitch=1200,sync=0,splice=900,delay=60,recycle=74,mix=100,lowCut=250,highCut=3000,smoothing=2000,pitchOffset=7,spliceOffset=12" },
        { "Bloom When You Stop",               "direction=1,pitch=1200,sync=1,spliceNote=3,delayNote=5,recycle=65,mix=100,lowCut=200,highCut=6000,smoothing=2000,threshold=-35,gateDuck=30,attack=50,release=800,pitchOffset=5" },
        { "Short Reverse Grains",              "direction=1,pitch=0,sync=0,splice=45,delay=0,recycle=55,mix=45,lowCut=200,highCut=9000,smoothing=20,pitchOffset=0,fbMode=1" },
    };
}

int MyCrystalProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String MyCrystalProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void MyCrystalProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    myPresets.snapshot();
    // まず全部を初期値に（GAIN MATCH・LINK の番号・VOCAL DUCK は触らない）
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == myp::GainMatch::paramId || id == "linkDuck" || id == "linkGroup") continue;
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
    for (auto& kv : juce::StringArray::fromTokens (f.values, ",", ""))
    {
        const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
        const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (auto* r = apvts.getParameter (id))
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->convertTo0to1 (v));
            r->endChangeGesture();
        }
    }
    myPresets.commitSinceSnapshot();
    apvts.state.setProperty ("mypPreset", juce::String ("[") + f.name + "]", nullptr);
    gainMatch.requestRelearn();
}

juce::AudioProcessorEditor* MyCrystalProcessor::createEditor()
{
    return new MyCrystalEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyCrystalProcessor();
}
