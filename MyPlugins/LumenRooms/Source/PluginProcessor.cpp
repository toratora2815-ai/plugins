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

namespace
{
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
      v.push_back (fp ("time", "Reverb Time", r, 3.29f, "s", 2)); }
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "infinite", 1 }, "Freeze", false));
    v.push_back (fp ("bloom", "Bloom", { 0.0f, 10.0f, 0.01f }, 2.5f));
    { juce::NormalisableRange<float> r (0.0f, 500.0f, 0.1f); r.setSkewForCentre (60.0f);
      v.push_back (fp ("predelay", "Pre-delay", r, 10.0f, "ms")); }
    v.push_back (fp ("revWidth", "Reverb Width", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    v.push_back (fp ("bassFreq", "Bass Freq", freqRange (20.0f, 4000.0f, 500.0f), 796.0f, "Hz", 0));
    v.push_back (fp ("bassMult", "Bass Mult", { 0.2f, 2.5f, 0.01f }, 1.0f, "x", 2));
    v.push_back (fp ("trebFreq", "Treble Freq", freqRange (500.0f, 18000.0f, 4000.0f), 6000.0f, "Hz", 0));
    v.push_back (fp ("trebMult", "Treble Mult", { 0.2f, 1.0f, 0.01f }, 1.0f, "x", 2));
    v.push_back (fp ("revRoll", "Reverb Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 6010.0f, "Hz", 0));
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
    v.push_back (fp ("echoRoll", "Echo Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
    v.push_back (fp ("revXfLevel", "Reverb XFeed", { -20.0f, 0.0f, 0.1f }, -3.0f, "dB"));
    v.push_back (fp ("revXfDelay", "Reverb XFeed Delay", { 0.0f, 100.0f, 0.1f }, 10.0f, "ms"));
    v.push_back (fp ("revXfRoll", "Reverb XFeed Roll-off", freqRange (500.0f, 18000.0f, 4000.0f), 18000.0f, "Hz", 0));
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
    t.predelayMs = p ("predelay");
    t.echoDb = p ("echoLevel") <= -19.95f ? -100.0f : p ("echoLevel");
    t.echoMs = p ("echoTime"); t.echoRollHz = p ("echoRoll");
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
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    engine.setTail (readTail());
    engine.setMix (readMix());

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
        engine.process (L + start, R + start, bufL.getReadPointer (0), bufL.getReadPointer (1), k);
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

void LumenRoomsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LumenRoomsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) st.getProperty ("program", 0);
            apvts.replaceState (st);
        }
}

juce::AudioProcessorEditor* LumenRoomsProcessor::createEditor() { return new LumenRoomsEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenRoomsProcessor(); }
