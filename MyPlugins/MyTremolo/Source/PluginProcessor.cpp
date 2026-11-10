#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* kIds[] = { "input", "output", "depth", "groove", "feel", "accent", "width", "shape", "rhythm", "rate", "sync", "style",
                           "threshold", "attack", "release", "envMode", "rateMod", "depthMod", "lowKeep", "smoothing", "smoothMode",
                           myp::GainMatch::paramId, "linkDuck" };

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float v) { return a * std::pow (b / a, v); },
                 [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); },
                 [] (float a, float b, float x) { return juce::jlimit (a, b, x); } };
    }
}

MyTremoloProcessor::MyTremoloProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < pNum; ++i) p[i] = apvts.getRawParameterValue (kIds[i]);
    myPresets.exclude ("linkDuck");
    customShape.setSine();
    engine.setPattern (pattern);
    engine.setCustomShape (customShape);
}

juce::AudioProcessorValueTreeState::ParameterLayout MyTremoloProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto db1 = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };

    ps.push_back (std::make_unique<F> (juce::ParameterID { "input", 1 }, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "depth", 1 }, "Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "groove", 1 }, "Groove", juce::NormalisableRange<float> (-25.0f, 25.0f, 0.1f), 0.0f,
                                       A().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
                                           if (std::abs (v) < 0.05f) return juce::String ("0");
                                           return juce::String (v < 0 ? "Shuffle " : "Swing ") + juce::String (std::abs (v), 1) + " %"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "feel", 1 }, "Feel", juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
                                       A().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
                                           if (std::abs (v) < 0.05f) return juce::String ("0");
                                           return juce::String (v < 0 ? "Rush " : "Drag ") + juce::String (std::abs (v), 1) + " %"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "accent", 1 }, "Accent", juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f, A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "width", 1 }, "Width", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    {
        juce::StringArray shapes;
        for (int i = 0; i < mytrem::kNumShapes; ++i) shapes.add (mytrem::shapeName (i));
        shapes.add ("Custom");
        ps.push_back (std::make_unique<C> (juce::ParameterID { "shape", 1 }, "Shape", shapes, 8));
        juce::StringArray rh;
        for (int i = 0; i < mytrem::kNumRhythms; ++i) rh.add (mytrem::rhythmName (i));
        ps.push_back (std::make_unique<C> (juce::ParameterID { "rhythm", 1 }, "Rhythm", rh, 9));
    }
    ps.push_back (std::make_unique<F> (juce::ParameterID { "rate", 1 }, "Rate", juce::NormalisableRange<float> (30.0f, 240.0f, 0.1f), 120.0f, A().withLabel ("BPM")));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "sync", 1 }, "Sync to Logic", true));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "style", 1 }, "Style", juce::StringArray { "Digital", "Smooth", "Even", "Squeeze", "Dense", "Crunch", "Grit", "Pump" }, 1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "threshold", 1 }, "Threshold", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -40.0f, A().withLabel ("dB")));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "attack", 1 }, "Attack", logRange (0.1f, 5000.0f), 500.0f, A().withLabel ("ms")));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "release", 1 }, "Release", logRange (0.1f, 5000.0f), 500.0f, A().withLabel ("ms")));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "envMode", 1 }, "Envelope Mode", juce::StringArray { "Env", "Gate" }, 0));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "rateMod", 1 }, "Rate Mod", juce::NormalisableRange<float> (-6.0f, 6.0f, 0.01f), 0.0f, A().withLabel ("oct")));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "depthMod", 1 }, "Depth Mod", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "lowKeep", 1 }, "Low Keep", juce::NormalisableRange<float> (0.0f, 400.0f, 1.0f, 0.6f), 0.0f,
                                       A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "smoothing", 1 }, "Shape Smoothing", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "smoothMode", 1 }, "Smoothing Mode", juce::StringArray { "Linear", "Sine", "Exp", "Sym", "Rev" }, 1));
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "linkDuck", 1 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                       A().withStringFromValueFunction (pct)));
    return { ps.begin(), ps.end() };
}

bool MyTremoloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void MyTremoloProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.setPattern (getPattern());
    engine.setCustomShape (getCustomShape());
    setLatencySamples (engine.latencySamples());
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
}

mytrem::Knobs MyTremoloProcessor::currentKnobs() const
{
    mytrem::Knobs k;
    k.inDb = p[pIn]->load();
    k.outDb = p[pOut]->load();
    k.depth = p[pDepth]->load() / 100.0f;
    k.groove = p[pGroove]->load() / 100.0f;
    k.feel = p[pFeel]->load() / 100.0f;
    k.accentDb = p[pAccent]->load();
    k.width = p[pWidth]->load() / 100.0f;
    k.shape = (int) p[pShape]->load();
    k.rhythm = (int) p[pRhythm]->load();
    k.rateBpm = p[pRate]->load();
    k.sync = p[pSync]->load() > 0.5f;
    k.style = (int) p[pStyle]->load();
    k.thresholdDb = p[pThr]->load();
    k.attackMs = p[pAtt]->load();
    k.releaseMs = p[pRel]->load();
    k.gate = p[pEnvMode]->load() > 0.5f;
    k.rateModOct = p[pRateMod]->load();
    k.depthMod = p[pDepthMod]->load() / 100.0f;
    k.lowKeepHz = p[pLowKeep]->load() < 20.0f ? 0.0f : p[pLowKeep]->load();
    k.smoothing = p[pSmooth]->load() / 100.0f;
    k.smoothMode = (int) p[pSmoothMode]->load();
    return k;
}

void MyTremoloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));

    mytrem::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.hostBpm = *b;
            tr.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { tr.ppq = *q; tr.hasPosition = true; }
        }
    auto k = currentKnobs();
    shownBpm.store (k.sync ? tr.hostBpm : (double) k.rateBpm);

    // Plugin Link: Fader Pilot が歌っている間、DEPTH を linkDuck % だけ浅く
    {
        const float g = duck.next (link, 1.0f, n, getSampleRate());            // 1（歌っていない）〜 0.891（歌っている）
        const float a = juce::jlimit (0.0f, 1.0f, (1.0f - g) / (1.0f - 0.8912509f));
        k.duck = 1.0f - a * p[pLinkDuck]->load() / 100.0f;
    }

    const int numCh = juce::jmin (numOut, mytrem::Engine::maxCh);
    engine.process (buffer.getArrayOfWritePointers(), n, k, tr);

    // GAIN MATCH: 比べる元がないもの（なし）。深さ 0 なら素通しに近いので普通に測る
    gainMatch.process (buffer, numCh, getLatencySamples());
}

void MyTremoloProcessor::setPattern (const mytrem::Pattern& pt)
{
    { const juce::ScopedLock sl (editLock); pattern = pt; }
    engine.setPattern (pt);
    ++editVersion;
}

void MyTremoloProcessor::setCustomShape (const mytrem::CustomShape& c)
{
    { const juce::ScopedLock sl (editLock); customShape = c; }
    engine.setCustomShape (c);
    ++editVersion;
}

float MyTremoloProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 6) taps.erase (taps.begin());
    if (taps.size() < 2) return 0.0f;
    const double per = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    const float bpm = juce::jlimit (30.0f, 240.0f, (float) (60.0 / per));
    if (auto* prm = apvts.getParameter ("rate"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (bpm));
        prm->endChangeGesture();
    }
    return bpm;
}

//==============================================================================
// パターン・形を文字にする（セッション・別ファイルの保存用）
juce::String MyTremoloProcessor::patternToString (const mytrem::Pattern& pt)
{
    juce::String s;
    s << pt.bars << ";" << pt.beatsPerBar << ";" << pt.grid << ";";
    for (int i = 0; i < pt.numCells(); ++i) s << (int) pt.cell[(size_t) i];
    return s;
}

bool MyTremoloProcessor::patternFromString (const juce::String& s, mytrem::Pattern& pt)
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

juce::String MyTremoloProcessor::shapeToString (const mytrem::CustomShape& c)
{
    juce::String s;
    for (int i = 0; i < c.n; ++i) s << juce::String (c.x[(size_t) i], 4) << "," << juce::String (c.y[(size_t) i], 4) << ";";
    return s;
}

bool MyTremoloProcessor::shapeFromString (const juce::String& s, mytrem::CustomShape& c)
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

void MyTremoloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("pattern", patternToString (getPattern()), nullptr);
    st.setProperty ("customShape", shapeToString (getCustomShape()), nullptr);
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void MyTremoloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            mytrem::Pattern pt;
            if (patternFromString (apvts.state.getProperty ("pattern").toString(), pt)) setPattern (pt);
            mytrem::CustomShape c;
            if (shapeFromString (apvts.state.getProperty ("customShape").toString(), c)) setCustomShape (c);
            gainMatch.loadFrom (apvts.state);
        }
}

//==============================================================================
// 内蔵プリセット（測った市販トレモロの内蔵プリセットの「動き」を参考に、名前は付け直した）
namespace
{
    struct FP
    {
        const char* name;
        int shape, rhythm, style;
        float depth, groove, feel, accent, width;
        float thr, att, rel; int gate; float rateMod, depthMod;
        const char* pattern;   // nullptr = リズムはそのまま
        float rateBpm; int sync;
        float outDb = 0;
    };
    const FP kFactory[] = {
        // name                    shape rh  sty  depth groove feel acc  width  thr   att   rel  gate rmod  dmod   pattern  bpm sync
        { "Init - Classic 8ths",      8,  9,  1,  100,   0,    0,   0,    0,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Amp Trem - Bright",       10,  9,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Amp Trem - Pedal",        11,  9,  0,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Amp Trem - Deep",         12,  9,  0,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Amp Trem - Lopsided",     13,  9,  1,   80,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Electric Piano Wobble",   14, 12,  0,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Stereo Keys Pan",          0,  6,  1,   72,   0,    0,   0,   50,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Square Gate 1/8",          2,  9,  0,  100,   0,    0,   0,    0,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Soft Chop 1/16",           3, 12,  0,  100,   0,    0,   0,    0,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Swing Sine 1/16",          0, 12,  0,  100,  24,    0,  4.4f,  0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Shuffle Pulse 1/4",        2,  6,  0,  100, -15,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Ramp Up 1/16",             5, 12,  0,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Ramp Down Accent 1/16",    6, 12,  0,  100,  16,    0, -12,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Trapezoid Throb 1/4",      7,  6,  0,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Triplet Chop",             2, 10,  0,  100,  15,    0, -12,    0,   -30,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Downbeats Only",           2, 15,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  "1;4;1;10001000", 120, 1 },
        { "Upbeats Only",             2, 15,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  "1;4;1;00100010", 120, 1 },
        { "Stutter Bar",              2, 15,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  "1;4;3;1111111110101010", 120, 1 },
        { "Gallop",                   3, 15,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  "1;4;3;1011101110111011", 120, 1 },
        { "Clave Gate",               2, 15,  1,  100,   0,    0,   0,    0,   -30,  500,  500,  0,  0,     0,  "2;4;3;10010010001010001000101000100000", 120, 1 },
        { "Speeds Up When Loud",      8,  9,  2,  100,  15,    0,  5,     0,   -80,   67,  1300,  0,  2.2f, -16,  nullptr, 120, 1 },
        { "Slows Down When Loud",     8,  9,  2,  100,   0,    0,   0,    0,   -64,  176,  600,  0,  2.0f,  -28,  nullptr, 120, 1 },
        { "Deeper When Loud",         1,  9,  1,  100,   0,    0,   0,    0,   -40,    8,  1300,  0,  0,     100, nullptr, 120, 1 },
        { "Attack Saver",             8,  9,  0,  100,   0,    0,   0,    0,   -41,  0.1f, 85,  0,  0,    -100, nullptr, 120, 1 },
        { "Ring Mod Low",             0,  9,  0,  100,   0,    0,   0,    0,   -80,  0.1f, 0.1f, 1,  4.0f,   0,  nullptr, 120, 1 },
        { "Ring Mod High",            0,  9,  0,  100,   0,    0,   0,    0,   -80,  0.1f, 0.1f, 1,  6.0f,   0,  nullptr, 120, 1 },
        { "Dynamic Ring",             0,  9,  1,  100,   0,    0,   0,    0,   -35,  22,   30,   0,  3.0f,   0,  nullptr, 120, 1 },
        { "Pump Chop 1/16",           9, 12,  7,  100,  25,    0, -4.8f,  0,     0,  9,   85,  1,  0,     0,  nullptr, 120, 1, 24 },
        { "Gritty Throb 1/8",         8,  9,  6,  100,   0,    0,   0,    0,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
        { "Wide Auto-Pan 1/4",        0,  6,  1,  100,   0,    0,   0,  100,   -40,  500,  500,  0,  0,     0,  nullptr, 120, 1 },
    };
}

int MyTremoloProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String MyTremoloProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void MyTremoloProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    myPresets.snapshot();
    set ("shape", (float) f.shape); set ("rhythm", (float) f.rhythm); set ("style", (float) f.style);
    set ("depth", f.depth); set ("groove", f.groove); set ("feel", f.feel); set ("accent", f.accent); set ("width", f.width);
    set ("threshold", f.thr); set ("attack", f.att); set ("release", f.rel); set ("envMode", (float) f.gate);
    set ("rateMod", f.rateMod); set ("depthMod", f.depthMod);
    set ("rate", f.rateBpm); set ("sync", (float) f.sync);
    set ("input", 0); set ("output", f.outDb); set ("lowKeep", 0);
    myPresets.commitSinceSnapshot();
    if (f.pattern != nullptr)
    {
        mytrem::Pattern pt;
        if (patternFromString (f.pattern, pt)) setPattern (pt);
    }
    apvts.state.setProperty ("mypPreset", juce::String ("[") + f.name + "]", nullptr);
    gainMatch.requestRelearn();
}

juce::AudioProcessorEditor* MyTremoloProcessor::createEditor()
{
    return new MyTremoloEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyTremoloProcessor();
}
