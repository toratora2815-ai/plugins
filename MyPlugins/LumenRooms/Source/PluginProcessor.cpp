#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

const juce::StringArray& LumenRoomsProcessor::patternNames()
{
    static const juce::StringArray n { "Natural", "Even", "Even Tight", "Spreading", "Gathering", "Cluster A", "Cluster B" };
    return n;
}
const juce::StringArray& LumenRoomsProcessor::proximityNames()
{
    static const juce::StringArray n { "Touching", "Near 1", "Near 2", "Middle 1", "Middle 2", "Away 1", "Away 2",
                                       "Far 1", "Far 2", "Backward 1", "Backward 2" };
    return n;
}
const juce::StringArray& LumenRoomsProcessor::slopeNames()
{
    static const juce::StringArray n { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" };
    return n;
}

const juce::StringArray& LumenRoomsProcessor::noteNames()
{
    static const juce::StringArray n { "1/64 T", "1/64", "1/64 D", "1/32 T", "1/32", "1/32 D", "1/16 T", "1/16", "1/16 D",
                                       "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2 T", "1/2", "1/2 D" };
    return n;
}

const juce::StringArray& LumenRoomsProcessor::colorNames()
{
    static const juce::StringArray n = [] {
        juce::StringArray a { "Off" };
        for (int g = 0; g < 8; ++g)
            for (int i = 1; i <= 10; ++i) a.add (juce::String::charToString ((juce::juce_wchar) ('A' + g)) + juce::String (i));
        return a;
    }();
    return n;
}

namespace
{
// 音符の長さ（4 分音符 = 1）
double noteQuarters (int i)
{
    i = juce::jlimit (0, 17, i);
    const double base = 4.0 / (64.0 / (double) (1 << (i / 3)));
    const int kind = i % 3;
    return base * (kind == 0 ? 2.0 / 3.0 : (kind == 2 ? 1.5 : 1.0));
}

juce::NormalisableRange<float> freqRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi, 1.0f);
    r.setSkewForCentre (centre);
    return r;
}

std::unique_ptr<juce::AudioParameterFloat> fp (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                                               const char* unit = "", int decimals = 1)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [decimals, unit = juce::String (unit)] (float v, int) {
                if (unit == "Hz" && v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
                return juce::String (v, decimals) + (unit.isEmpty() ? "" : " " + unit);
            }));
}

std::unique_ptr<juce::AudioParameterFloat> dbOff (const char* id, const char* name, float lo, float hi, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.1f), def,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [lo] (float v, int) { return v <= lo + 0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; }));
}

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p {
        { "Init", {} },
        { "Vocal Hall", { { "time", 2.6f }, { "bloom", 3.0f }, { "predelay", 30.0f }, { "trebMult", 0.6f }, { "trebFreq", 5000.0f },
                          { "bassMult", 1.1f }, { "bassFreq", 400.0f }, { "erSize", 6.0f }, { "balance", 65.0f },
                          { "revRoll", 9000.0f }, { "lowCut", 120.0f } } },
        { "Cinematic Hall", { { "time", 6.5f }, { "bloom", 6.0f }, { "predelay", 40.0f }, { "erSize", 8.0f }, { "pattern", 3.0f },
                              { "proximity", 3.0f }, { "trebMult", 0.7f }, { "trebFreq", 6000.0f }, { "bassMult", 1.3f },
                              { "bassFreq", 300.0f }, { "revRoll", 8000.0f }, { "balance", 75.0f }, { "modDepth", 8.0f } } },
        { "Tight Drum Room", { { "time", 0.8f }, { "bloom", 0.5f }, { "predelay", 0.0f }, { "erSize", 2.5f }, { "pattern", 5.0f },
                               { "proximity", 1.0f }, { "reflectivity", 7.0f }, { "diffusion", 3.0f }, { "balance", 35.0f },
                               { "trebMult", 0.55f }, { "trebFreq", 4000.0f }, { "revRoll", 10000.0f }, { "lowCut", 80.0f } } },
        { "Stone Chamber", { { "time", 1.8f }, { "bloom", 2.0f }, { "erSize", 4.0f }, { "reflectivity", 8.0f }, { "diffusion", 7.0f },
                             { "balance", 55.0f }, { "trebMult", 0.8f }, { "revRoll", 7000.0f }, { "bassMult", 0.9f } } },
        { "Ambience", { { "time", 0.6f }, { "bloom", 1.0f }, { "erSize", 3.0f }, { "balance", 25.0f }, { "diffusion", 6.0f },
                        { "revRoll", 12000.0f } } },
        { "Dark Space", { { "time", 12.0f }, { "bloom", 8.0f }, { "predelay", 60.0f }, { "trebMult", 0.4f }, { "trebFreq", 2500.0f },
                          { "revRoll", 3500.0f }, { "revSlope", 3.0f }, { "bassMult", 1.5f }, { "balance", 85.0f }, { "modDepth", 9.0f } } },
        { "Infinite Pad", { { "infinite", 1.0f }, { "bloom", 10.0f }, { "balance", 100.0f }, { "time", 20.0f } } },
        { "Slap Room", { { "echoLevel", -6.0f }, { "echoTime", 180.0f }, { "time", 1.2f }, { "erSize", 3.0f }, { "balance", 55.0f } } },
        { "Backward Gather", { { "pattern", 4.0f }, { "proximity", 9.0f }, { "erSize", 7.0f }, { "balance", 30.0f }, { "time", 2.0f } } },
    };
    return p;
}
} // namespace

APVTS::ParameterLayout LumenRoomsProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    // ミックス
    v.push_back (fp ("mix", "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    v.push_back (fp ("balance", "Refl / Reverb", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
    v.push_back (fp ("master", "Output", { -20.0f, 20.0f, 0.1f }, 0.0f, "dB"));
    v.push_back (dbOff ("erTrim", "Refl Trim", -20.0f, 20.0f, 0.0f));
    v.push_back (dbOff ("revTrim", "Reverb Trim", -20.0f, 20.0f, 0.0f));
    // 初期反射
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pattern", 1 }, "Pattern", patternNames(), 0));
    v.push_back (fp ("erSize", "Refl Size", { 0.0f, 10.0f, 0.01f }, 5.0f));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "proximity", 1 }, "Proximity", proximityNames(), 2));
    v.push_back (fp ("reflectivity", "Reflectivity", { 0.0f, 10.0f, 0.01f }, 5.0f));
    v.push_back (fp ("diffusion", "Diffusion", { 0.0f, 10.0f, 0.01f }, 5.0f));
    v.push_back (fp ("erWidth", "Refl Width", { 0.0f, 150.0f, 0.1f }, 100.0f, "%", 0));
    v.push_back (fp ("erRoll", "Refl Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 8000.0f, "Hz", 0));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "erSlope", 1 }, "Refl Slope", slopeNames(), 0));
    v.push_back (fp ("erXfLevel", "Refl XFeed", { -20.0f, 0.0f, 0.1f }, 0.0f, "dB"));
    v.push_back (fp ("erXfDelay", "Refl XFeed Delay", { 0.0f, 100.0f, 0.1f }, 0.0f, "ms"));
    v.push_back (fp ("erXfRoll", "Refl XFeed Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
    v.push_back (fp ("erModDepth", "Refl Mod Depth", { 0.0f, 10.0f, 0.01f }, 7.5f));
    v.push_back (fp ("erModRate", "Refl Mod Rate", { 0.2f, 2.0f, 0.01f }, 0.7f, "Hz", 2));
    // テール
    { juce::NormalisableRange<float> r (0.2f, 42.5f, 0.01f); r.setSkewForCentre (4.0f);
      v.push_back (fp ("time", "Reverb Time", r, 2.25f, "s", 2)); }
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "infinite", 1 }, "Freeze", false));
    v.push_back (fp ("bloom", "Bloom", { 0.0f, 10.0f, 0.01f }, 2.5f));
    { juce::NormalisableRange<float> r (0.0f, 500.0f, 0.1f); r.setSkewForCentre (60.0f);
      v.push_back (fp ("predelay", "Pre-delay", r, 10.0f, "ms")); }
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "preSync", 1 }, "Pre-delay Sync", false));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "preNote", 1 }, "Pre-delay Note", noteNames(), 7));
    v.push_back (fp ("revWidth", "Reverb Width", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    v.push_back (fp ("bassFreq", "Bass Freq", freqRange (20.0f, 4000.0f, 500.0f), 796.0f, "Hz", 0));
    v.push_back (fp ("bassMult", "Bass Mult", { 0.2f, 2.5f, 0.01f }, 1.0f, "x", 2));
    v.push_back (fp ("trebFreq", "Treble Freq", freqRange (500.0f, 18000.0f, 4000.0f), 6000.0f, "Hz", 0));
    v.push_back (fp ("trebMult", "Treble Mult", { 0.2f, 1.0f, 0.01f }, 1.0f, "x", 2));
    v.push_back (fp ("revRoll", "Reverb Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 8000.0f, "Hz", 0));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "revSlope", 1 }, "Reverb Slope", slopeNames(), 2));
    v.push_back (fp ("lowCut", "Low Cut", freqRange (20.0f, 750.0f, 150.0f), 20.0f, "Hz", 0));
    v.push_back (dbOff ("lowBoost", "Low Boost", -20.0f, 3.0f, -17.3f));
    v.push_back (fp ("boostWeight", "Boost Weight", { 0.0f, 10.0f, 0.01f }, 5.0f));
    v.push_back (fp ("modDepth", "Mod Depth", { 0.0f, 10.0f, 0.01f }, 7.5f));
    v.push_back (fp ("modRate", "Mod Rate", { 0.0f, 2.0f, 0.01f }, 0.7f, "Hz", 2));
    v.push_back (fp ("undDepth", "Undulation", { 0.0f, 10.0f, 0.01f }, 2.5f));
    v.push_back (fp ("undShape", "Undulation Shape", { 0.0f, 10.0f, 0.01f }, 2.5f));
    v.push_back (dbOff ("echoLevel", "Echo Level", -20.0f, 0.0f, -20.0f));
    { juce::NormalisableRange<float> r (100.0f, 1500.0f, 1.0f); r.setSkewForCentre (400.0f);
      v.push_back (fp ("echoTime", "Echo Time", r, 100.0f, "ms", 0)); }
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "echoSync", 1 }, "Echo Sync", false));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "echoNote", 1 }, "Echo Note", noteNames(), 9));
    v.push_back (fp ("echoRoll", "Echo Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
    v.push_back (fp ("revXfLevel", "Reverb XFeed", { -20.0f, 0.0f, 0.1f }, -3.0f, "dB"));
    v.push_back (fp ("revXfDelay", "Reverb XFeed Delay", { 0.0f, 100.0f, 0.1f }, 10.0f, "ms"));
    v.push_back (fp ("revXfRoll", "Reverb XFeed Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
    // 音色の型
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "revColor", 1 }, "Reverb Color", colorNames(), 0));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "erColor", 1 }, "Refl Color", colorNames(), 0));
    // ポスト EQ
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "eqOn", 1 }, "EQ", false));
    v.push_back (fp ("eqLowCut", "EQ Low Cut", freqRange (20.0f, 1500.0f, 200.0f), 20.0f, "Hz", 0));
    v.push_back (fp ("eqLsFreq", "EQ Low Freq", freqRange (50.0f, 5000.0f, 600.0f), 500.0f, "Hz", 0));
    v.push_back (fp ("eqLsGain", "EQ Low Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    v.push_back (fp ("eqLsQ", "EQ Low Q", { 0.2f, 2.0f, 0.01f }, 0.7f, "", 2));
    v.push_back (fp ("eqMidFreq", "EQ Mid Freq", freqRange (200.0f, 15000.0f, 2500.0f), 3000.0f, "Hz", 0));
    v.push_back (fp ("eqMidGain", "EQ Mid Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    v.push_back (fp ("eqMidQ", "EQ Mid Q", { 0.2f, 2.0f, 0.01f }, 0.7f, "", 2));
    v.push_back (fp ("eqHsFreq", "EQ High Freq", freqRange (1000.0f, 18000.0f, 6000.0f), 8000.0f, "Hz", 0));
    v.push_back (fp ("eqHsGain", "EQ High Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    v.push_back (fp ("eqHsQ", "EQ High Q", { 0.2f, 2.0f, 0.01f }, 0.7f, "", 2));
    v.push_back (fp ("eqRoll", "EQ Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "eqSlope", 1 }, "EQ Slope", slopeNames(), 0));
    return { v.begin(), v.end() };
}

LumenRoomsProcessor::LumenRoomsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    profiles.load (lumen::ProfileTable::defaultPath());
    startTimerHz (20);
}

LumenRoomsProcessor::~LumenRoomsProcessor() { stopTimer(); }

bool LumenRoomsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

lumen::ReflParams LumenRoomsProcessor::readRefl() const
{
    lumen::ReflParams r;
    r.pattern = (int) p ("pattern");
    r.size = p ("erSize") / 10.0f;
    r.proximity = (int) p ("proximity");
    r.reflectivity = p ("reflectivity") / 10.0f;
    r.diffusion = p ("diffusion");
    r.rolloffHz = p ("erRoll");
    r.rolloffSlope = (int) p ("erSlope") + 1;
    r.xfeedDb = p ("erXfLevel");
    r.xfeedMs = p ("erXfDelay");
    r.xfeedRollHz = p ("erXfRoll");
    return r;
}

lumen::TailParams LumenRoomsProcessor::readTail() const
{
    lumen::TailParams t;
    t.timeSec = p ("infinite") > 0.5f ? 100.0f : p ("time");
    t.bloom = p ("bloom");
    t.bassFreq = p ("bassFreq"); t.bassMult = p ("bassMult");
    t.trebFreq = p ("trebFreq"); t.trebMult = p ("trebMult");
    t.lowBoostDb = p ("lowBoost") <= -19.95f ? -100.0f : p ("lowBoost");
    t.lowBoostWeight = p ("boostWeight") / 10.0f;
    const double qMs = 60000.0 / juce::jlimit (20.0, 400.0, hostBpm.load());
    t.predelayMs = p ("preSync") > 0.5f ? (float) juce::jmin (500.0, qMs * noteQuarters ((int) p ("preNote"))) : p ("predelay");
    t.echoDb = p ("echoLevel") <= -19.95f ? -100.0f : p ("echoLevel");
    t.echoMs = p ("echoSync") > 0.5f ? (float) juce::jlimit (5.0, 1500.0, qMs * noteQuarters ((int) p ("echoNote"))) : p ("echoTime");
    t.echoRollHz = p ("echoRoll");
    t.rolloffHz = p ("revRoll"); t.rolloffSlope = (int) p ("revSlope") + 1;
    t.lowCutHz = p ("lowCut");
    t.undDepth = p ("undDepth") / 10.0f; t.undShape = p ("undShape") / 10.0f;
    t.modDepth = p ("modDepth") / 10.0f; t.modRateHz = p ("modRate");
    t.xfeedDb = p ("revXfLevel"); t.xfeedMs = p ("revXfDelay"); t.xfeedRollHz = p ("revXfRoll");
    return t;
}

lumen::MixParams LumenRoomsProcessor::readMix() const
{
    lumen::MixParams m;
    m.dryWet = p ("mix") / 100.0f;
    m.reflRev = p ("balance") / 100.0f;
    m.masterDb = p ("master");
    m.reflTrimDb = p ("erTrim") <= -19.95f ? -100.0f : p ("erTrim");
    m.revTrimDb = p ("revTrim") <= -19.95f ? -100.0f : p ("revTrim");
    m.reflWidth = p ("erWidth") / 100.0f;
    m.revWidth = p ("revWidth") / 100.0f;
    m.erModDepth = p ("erModDepth") / 10.0f; m.erModRate = p ("erModRate");
    m.eqOn = p ("eqOn") > 0.5f;
    m.eqLowCut = p ("eqLowCut");
    m.eqLsF = p ("eqLsFreq"); m.eqLsG = p ("eqLsGain"); m.eqLsQ = p ("eqLsQ");
    m.eqMidF = p ("eqMidFreq"); m.eqMidG = p ("eqMidGain"); m.eqMidQ = p ("eqMidQ");
    m.eqHsF = p ("eqHsFreq"); m.eqHsG = p ("eqHsGain"); m.eqHsQ = p ("eqHsQ");
    m.eqRoll = p ("eqRoll"); m.eqRollSlope = (int) p ("eqSlope") + 1;
    return m;
}

void LumenRoomsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    engine.prepare (sr, maxBlock);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    convFromL.prepare (spec);
    convFromR.prepare (spec);
    bufL.setSize (2, maxBlock);
    liveDryBuf.assign ((size_t) maxBlock, 0.0f);
    liveWetBuf.assign ((size_t) maxBlock, 0.0f);
    bufR.setSize (2, maxBlock);
    haveRefl = false;
    rebuildReflections();
}

void LumenRoomsProcessor::rebuildReflections()
{
    const auto r = readRefl();
    std::vector<float> own, cross;
    lumen::buildReflections (r, sr, own, cross);
    const int n = (int) own.size();
    juce::AudioBuffer<float> a (2, n), b (2, n);
    for (int i = 0; i < n; ++i)
    {
        a.setSample (0, i, own[(size_t) i]); a.setSample (1, i, cross[(size_t) i]);   // 左の入力 → 左は自分側、右は反対側
        b.setSample (0, i, cross[(size_t) i]); b.setSample (1, i, own[(size_t) i]);   // 右の入力 → 左は反対側、右は自分側
    }
    convFromL.loadImpulseResponse (std::move (a), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                   juce::dsp::Convolution::Normalise::no);
    convFromR.loadImpulseResponse (std::move (b), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                   juce::dsp::Convolution::Normalise::no);
    lastRefl = r;
    haveRefl = true;
}

void LumenRoomsProcessor::timerCallback()
{
    // 反射のつまみが動いたら、少し待ってからインパルス応答を作り直す（音はなめらかに切り替わる）
    const auto r = readRefl();
    const bool changed = ! haveRefl || r.pattern != lastRefl.pattern || r.size != lastRefl.size || r.proximity != lastRefl.proximity
                         || r.reflectivity != lastRefl.reflectivity || r.diffusion != lastRefl.diffusion || r.rolloffHz != lastRefl.rolloffHz
                         || r.rolloffSlope != lastRefl.rolloffSlope || r.xfeedDb != lastRefl.xfeedDb || r.xfeedMs != lastRefl.xfeedMs
                         || r.xfeedRollHz != lastRefl.xfeedRollHz;
    if (changed && sr > 0)
        rebuildReflections();
}

void LumenRoomsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    engine.setTail (readTail());
    engine.setMix (readMix());
    engine.setProfiles (profiles.revGains ((int) p ("revColor")), profiles.erGains ((int) p ("erColor")));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    for (int start = 0; start < n; start += maxBlock)
    {
        const int k = juce::jmin (maxBlock, n - start);
        for (int c = 0; c < 2; ++c) { bufL.copyFrom (c, 0, L + start, k); bufR.copyFrom (c, 0, R + start, k); }
        juce::dsp::AudioBlock<float> bl (bufL.getArrayOfWritePointers(), 2, (size_t) k), br (bufR.getArrayOfWritePointers(), 2, (size_t) k);
        convFromL.process (juce::dsp::ProcessContextReplacing<float> (bl));
        convFromR.process (juce::dsp::ProcessContextReplacing<float> (br));
        // 反射の合計：左 = 左入力の自分側 + 右入力の反対側
        bufL.addFrom (0, 0, bufR, 0, 0, k);
        bufL.addFrom (1, 0, bufR, 1, 0, k);
        const bool live = liveWanted.load (std::memory_order_relaxed);
        if (live) for (int i = 0; i < k; ++i) liveDryBuf[(size_t) i] = 0.5f * (L[start + i] + R[start + i]);
        engine.process (L + start, R + start, bufL.getReadPointer (0), bufL.getReadPointer (1), k);
        if (live)
        {
            const float dryG = lumen::mixLaw (1.0f - p ("mix") / 100.0f);
            for (int i = 0; i < k; ++i) liveWetBuf[(size_t) i] = 0.5f * (L[start + i] + R[start + i]) - dryG * liveDryBuf[(size_t) i];
            liveTap.push (liveDryBuf.data(), liveWetBuf.data(), k);
        }
    }

    for (int c = 0; c < 2; ++c)
    {
        const float pk = buffer.getMagnitude (c, 0, n);
        const float old = outLevel[c].load();
        outLevel[c].store (pk > old ? pk : old * 0.92f);
    }
}

int LumenRoomsProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String LumenRoomsProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, (int) presets().size()) ? presets()[(size_t) i].name : "";
}

void LumenRoomsProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) presets().size())) return;
    currentProgram = i;
    importedName = {};
    // まず全部を初期値に戻してから、プリセットの値を入れる
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->getParameterID() != "mix" && rp->getParameterID() != "master")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& kv : presets()[(size_t) i].values)
        if (auto* rp = apvts.getParameter (kv.first))
            rp->setValueNotifyingHost (rp->convertTo0to1 (kv.second));
}

LumenRoomsProcessor::ErView LumenRoomsProcessor::getErView() const
{
    static const float lenMs[12] = { 14, 12, 18, 17, 15, 20, 21, 17, 19, 15, 22, 15 };
    const auto r = readRefl();
    ErView v {};
    const float* t = lumen::patternTimes (r.pattern);
    const float a = lumen::sizeScale (r.size);
    const float post = 8.0f - 5.0f * r.reflectivity, pre = post * 0.5f;
    for (int k = 0; k < 12; ++k)
    {
        float db = (k >= r.proximity) ? -13.0f - post * (float) (k - r.proximity) : -13.0f - pre * (float) (r.proximity - k);
        const bool cross = (k & 1) != 0;
        v.timeMs[k] = 4.0f + a * (t[k] - 4.0f) + (cross ? r.xfeedMs : 0.0f);
        v.db[k] = db + (cross ? r.xfeedDb : 0.0f);
        v.lenMs[k] = lenMs[k];
    }
    return v;
}

juce::File LumenRoomsProcessor::defaultPresetFolder()
{
    // まずこの Mac に取っておいた控え（参考リバーブを消しても残る）、なければ元の場所
    const auto backup = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("MyPlugins/LumenRooms/Presets");
    if (backup.isDirectory()) return backup;
    const juce::File f ("/Library/Application Support/LiquidSonics/Cinematic Rooms Professional/Factory Presets");
    return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

juce::File LumenRoomsProcessor::myPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MyPlugins/LumenRooms/My Presets");
}

juce::String LumenRoomsProcessor::saveMyPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return "名前を入れてください";
    const auto dir = myPresetFolder();
    if (! dir.createDirectory()) return "保存先のフォルダを作れませんでした";
    auto state = apvts.copyState();
    state.setProperty ("presetName", rawName.trim(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (name + ".lrpreset"))) return "保存できませんでした";
    importedName = rawName.trim();
    return {};
}

juce::String LumenRoomsProcessor::loadMyPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) return "ファイルを読めませんでした";
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.isValid()) return "形式が違います";
    // DRY/WET と OUTPUT 以外を、保存した値にする
    for (auto c : st)
    {
        const auto id = c.getProperty ("id").toString();
        if (id.isEmpty() || id == "mix" || id == "master") continue;
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (double) c.getProperty ("value")));
    }
    importedName = st.getProperty ("presetName", file.getFileNameWithoutExtension()).toString();
    return {};
}

juce::File LumenRoomsProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("LiquidSonics/Cinematic Rooms Professional/User Presets");
}

void LumenRoomsProcessor::setParam (const char* id, float value)
{
    if (auto* rp = apvts.getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (rp->getNormalisableRange().snapToLegalValue (value)));
}

juce::String LumenRoomsProcessor::importPreset (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return "ファイルを読めませんでした";
    auto root = juce::ValueTree::readFromData (mb.getData(), mb.getSize());
    if (! root.isValid()) return "形式が違います";
    const auto ver = root.getProperty ("written_by_version__string").toString();
    if (ver.isNotEmpty() && ver.compareNatural ("0.3") < 0) return "古い形式（" + ver + "）のプリセットには対応していません";
    auto st = root.getChildWithName ("CRP_StateVT");
    if (! st.isValid()) st = root.getChildWithName ("CRP_PartialStateVT").getChildWithName ("CRP_StateVT");
    if (! st.isValid()) return "中にプリセットが見つかりません";

    std::map<int, double> v;
    for (auto c : st)
        if (c.hasType ("PARAM") && c.hasProperty ("value"))
            v[(int) c.getProperty ("id")] = (double) c.getProperty ("value");
    if (v.size() < 40) return "中身が足りません";
    auto has = [&] (int id) { return v.count (id) > 0; };
    auto get = [&] (int id, double def) { return has (id) ? v[id] : def; };

    // まず全部（DRY/WET と OUTPUT 以外）を初期値に戻す
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->getParameterID() != "mix" && rp->getParameterID() != "master")
                rp->setValueNotifyingHost (rp->getDefaultValue());

    struct Map { int id; const char* param; double scale; };
    static const Map direct[] = {
        { 58, "balance", 100.0 }, { 64, "revTrim", 1 }, { 186, "erTrim", 1 },
        { 6, "revXfLevel", 1 }, { 12, "revXfDelay", 1 }, { 18, "revXfRoll", 1 },
        { 27, "erXfLevel", 1 }, { 33, "erXfDelay", 1 }, { 39, "erXfRoll", 1 },
        { 24, "modDepth", 1 }, { 25, "modRate", 1 }, { 26, "boostWeight", 1 }, { 45, "erModDepth", 1 }, { 46, "erModRate", 1 },
        { 76, "bloom", 1 }, { 82, "revWidth", 10.0 }, { 88, "bassFreq", 1 }, { 94, "bassMult", 1 }, { 100, "trebFreq", 1 },
        { 106, "trebMult", 1 }, { 112, "lowBoost", 1 }, { 118, "predelay", 1 }, { 131, "echoLevel", 1 }, { 137, "echoTime", 1 },
        { 150, "echoRoll", 1 }, { 156, "revRoll", 1 }, { 168, "lowCut", 1 }, { 174, "undDepth", 1 }, { 180, "undShape", 1 },
        { 199, "erSize", 1 }, { 205, "reflectivity", 1 }, { 211, "diffusion", 1 }, { 223, "erRoll", 1 },
        { 236, "eqLowCut", 1 }, { 247, "eqLsFreq", 1 }, { 258, "eqLsGain", 1 }, { 264, "eqLsQ", 1 }, { 270, "eqMidFreq", 1 },
        { 281, "eqMidGain", 1 }, { 287, "eqMidQ", 1 }, { 293, "eqHsFreq", 1 }, { 304, "eqHsGain", 1 }, { 310, "eqHsQ", 1 },
        { 316, "eqRoll", 1 } };
    for (auto& m : direct)
        if (has (m.id)) setParam (m.param, (float) (v[m.id] * m.scale));

    static const std::pair<int, const char*> choices[] = { { 192, "pattern" }, { 193, "proximity" }, { 162, "revSlope" },
                                                           { 229, "erSlope" }, { 327, "eqSlope" }, { 124, "preNote" }, { 143, "echoNote" } };
    for (auto& c : choices)
        if (has (c.first)) setParam (c.second, (float) juce::roundToInt (v[c.first]));
    if (has (333) && has (334)) setParam ("revColor", (float) lumen::ProfileTable::indexOf (juce::roundToInt (v[333]), juce::roundToInt (v[334])));
    if (has (335) && has (336)) setParam ("erColor", (float) lumen::ProfileTable::indexOf (juce::roundToInt (v[335]), juce::roundToInt (v[336])));
    static const std::pair<int, const char*> bools[] = { { 235, "eqOn" }, { 130, "preSync" }, { 149, "echoSync" } };
    for (auto& b : bools)
        if (has (b.first)) setParam (b.second, v[b.first] > 0.5 ? 1.0f : 0.0f);

    if (has (70))
    {
        const double t = v[70];
        setParam ("infinite", t >= 44.9 ? 1.0f : 0.0f);
        setParam ("time", (float) juce::jmin (42.5, t));
    }
    if (has (217))   // 反射の幅：0..7.5 が 0..100%、7.5..10 が 100..150%
    {
        const double w = v[217];
        setParam ("erWidth", (float) (w <= 7.5 ? w / 7.5 * 100.0 : 100.0 + (w - 7.5) * 20.0));
    }
    if (has (168) && v[168] < 20.0) setParam ("lowCut", 20.0f);
    (void) get;

    importedName = st.getProperty ("preset_name", "").toString();
    if (importedName.isEmpty()) importedName = file.getFileNameWithoutExtension();
    return {};
}

void LumenRoomsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("imported", importedName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LumenRoomsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) st.getProperty ("program", 0);
            importedName = st.getProperty ("imported", "").toString();
            apvts.replaceState (st);
        }
}

juce::AudioProcessorEditor* LumenRoomsProcessor::createEditor() { return new LumenRoomsEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenRoomsProcessor(); }
