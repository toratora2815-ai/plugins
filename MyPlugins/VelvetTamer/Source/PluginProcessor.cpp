#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pct (float v, int)  { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String db (float v, int)   { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String ms (float v, int)   { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String signedPct (float v, int)
    {
        const int i = juce::roundToInt (v);
        return (i > 0 ? "+" : "") + juce::String (i) + " %";
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
}

VelvetTamerProcessor::VelvetTamerProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < vt::numWeights; ++i)
        weightParams[i] = apvts.getRawParameterValue (weightId (i));
}

juce::AudioProcessorValueTreeState::ParameterLayout VelvetTamerProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;

    auto addF = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                     std::function<juce::String (float, int)> toText)
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def,
                                          A().withStringFromValueFunction (std::move (toText))));
    };
    auto addB = [&] (const char* id, const char* name, bool def)
    {
        v.push_back (std::make_unique<B> (juce::ParameterID { id, 1 }, name, def));
    };

    // 効き方
    addF ("depth",       "Depth",       { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    addF ("sharpness",   "Sharpness",   { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    addF ("selectivity", "Selectivity", { 0.0f, 100.0f, 0.1f }, 40.0f, pct);
    addF ("attack",      "Attack",      skewed (0.5f, 50.0f, 0.1f, 5.0f), 5.0f, ms);
    addF ("release",     "Release",     skewed (5.0f, 500.0f, 1.0f, 80.0f), 80.0f, ms);

    // FOCUS（効かせる帯域）
    addF ("low",  "Focus Low",  skewed (20.0f, 5000.0f, 1.0f, 500.0f), 300.0f, hz);
    addF ("high", "Focus High", skewed (1000.0f, 20000.0f, 1.0f, 8000.0f), 16000.0f, hz);

    // 出口
    addF ("mix",    "Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    addF ("output", "Output", { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    addB ("delta", "Delta", false);
    addB ("link",  "Stereo Link", true);

    // WEIGHT（帯域ごとの効きの強弱。v1.1 で追加。バージョン 2）
    for (int i = 0; i < vt::numWeights; ++i)
    {
        const float f = vt::weightHz[i];
        const auto name = "Weight " + (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String ((int) f) + " Hz");
        v.push_back (std::make_unique<F> (juce::ParameterID { weightId (i), 2 }, name,
                                          juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                          A().withStringFromValueFunction (signedPct)));
    }

    // v1.2 で追加（バージョン 3）
    v.push_back (std::make_unique<B> (juce::ParameterID { "lowlatency", 3 }, "Low Latency", false));
    v.push_back (std::make_unique<B> (juce::ParameterID { "sidechain", 3 }, "External Sidechain", false));
    // v1.3 で追加（バージョン 4）
    v.push_back (plink::makeGroupParameter (4));

    return { v.begin(), v.end() };
}

bool VelvetTamerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;

    // サイドチェーン：なし・モノラル・ステレオ
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void VelvetTamerProcessor::prepareToPlay (double sampleRate, int)
{
    const auto s = readSettings();
    const int numCh = getMainBusNumOutputChannels();
    for (int i = 0; i < 2; ++i)
    {
        engines[i].setSettings (s);
        engines[i].prepare (sampleRate, numCh, i == 1);
    }
    currentRate = sampleRate;
    activeLowLatency = apvts.getRawParameterValue ("lowlatency")->load() > 0.5f;
    setLatencySamples (engines[activeLowLatency ? 1 : 0].getLatency());
}

// 低遅延の切り替えは音の処理の中で気づき、遅延の報告だけはメッセージスレッドで行う
void VelvetTamerProcessor::handleAsyncUpdate()
{
    setLatencySamples (engines[activeLowLatency ? 1 : 0].getLatency());
}

vt::Settings VelvetTamerProcessor::readSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    vt::Settings s;
    s.depth = f ("depth") * 0.01f;
    s.sharpness = f ("sharpness") * 0.01f;
    s.selectivity = f ("selectivity") * 0.01f;
    s.attackMs = f ("attack");
    s.releaseMs = f ("release");
    s.lowHz = f ("low");
    s.highHz = f ("high");
    s.mix = f ("mix") * 0.01f;
    s.outDb = f ("output");
    s.delta = f ("delta") > 0.5f;
    s.link = f ("link") > 0.5f;
    s.external = f ("sidechain") > 0.5f;
    for (int i = 0; i < vt::numWeights; ++i)
        s.weight[i] = weightParams[i]->load() * 0.01f;
    return s;
}

void VelvetTamerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool wantLow = apvts.getRawParameterValue ("lowlatency")->load() > 0.5f;
    if (wantLow != activeLowLatency.load())
    {
        engines[wantLow ? 1 : 0].reset();
        activeLowLatency = wantLow;
        triggerAsyncUpdate();
    }
    auto& engine = engines[wantLow ? 1 : 0];

    auto main = getBusBuffer (buffer, true, 0);
    juce::AudioBuffer<float> scBuf;   // 中身はホストの音を指すだけ（コピーしない）
    const float* const* sc = nullptr;
    int numSc = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            scBuf = getBusBuffer (buffer, true, 1);
            sc = scBuf.getArrayOfReadPointers();
            numSc = scBuf.getNumChannels();
        }

    auto settings = readSettings();
    applyLinkFocus (settings, main.getNumSamples());
    engine.setSettings (settings);
    engine.process (main.getArrayOfWritePointers(), main.getNumChannels(), main.getNumSamples(), sc, numSc);
}

// Balance Scope が「出すぎ」（許容幅を 0.5 dB 以上超えた所）を送ってきている間は、FOCUS をその帯域へ。
// 来なくなったら自分のつまみの値へ戻る。どちらも約 0.8 秒（時定数）でなめらかに動く（周波数の対数で）。
void VelvetTamerProcessor::applyLinkFocus (vt::Settings& s, int numSamples)
{
    float wantLo = std::log2 (juce::jmax (20.0f, s.lowHz)), wantHi = std::log2 (juce::jmax (20.0f, s.highHz));
    bool following = false;
    if (link.on())
    {
        plink::Peer p;
        if (link.ep().find (plink::kindBalanceScope, p) && p.v[plink::bs::valid] > 0.5f
            && p.v[plink::bs::excessDb] >= 0.5f && p.valuesAgeMs < 5000
            && p.v[plink::bs::lowHz] >= 20.0f && p.v[plink::bs::highHz] > p.v[plink::bs::lowHz])
        {
            wantLo = std::log2 (p.v[plink::bs::lowHz]);
            wantHi = std::log2 (juce::jmin (20000.0f, p.v[plink::bs::highHz]));
            following = true;
        }
    }
    if (curLo < 0.0f) { curLo = wantLo; curHi = wantHi; }
    const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.8 * currentRate.load()));
    curLo += (wantLo - curLo) * a;
    curHi += (wantHi - curHi) * a;
    if (std::abs (curLo - wantLo) < 1.0e-3f) curLo = wantLo;
    if (std::abs (curHi - wantHi) < 1.0e-3f) curHi = wantHi;

    // エンジンは値が変わるたびに FOCUS の形を作り直すので、1/48 オクターブ刻みにする
    auto q = [] (float l2) { return std::exp2 (std::round (l2 * 48.0f) / 48.0f); };
    const bool moved = following || curLo != wantLo || curHi != wantHi;
    if (moved) { s.lowHz = q (curLo); s.highHz = q (curHi); }
    shownLow = s.lowHz;
    shownHigh = s.highHz;
    linkFollowing = following;
}

namespace
{
    struct Preset
    {
        const char* name;
        float depth, sharpness, selectivity, attack, release, low, high;
        float weight[vt::numWeights];   // %（100 / 250 / 600 / 1.5k / 3.5k / 7k / 14k）
    };

    const Preset presets[] =
    {
        { "Default",            50, 50, 40,  5,  80,  300, 16000, {   0,   0,   0,   0,   0,   0,   0 } },
        { "Vocal Harshness",    40, 60, 40,  5,  80, 1500, 10000, {   0,   0,   0,  10,  50,  20, -30 } },
        { "Sibilance",          60, 40, 30,  1,  40, 4000, 16000, {   0,   0,   0,   0, -20,  60,  20 } },
        { "Distorted Guitar",   50, 50, 40,  5, 100, 1000, 12000, {   0,   0,   0,  20,  40,  20, -20 } },
        { "Boxy Low-Mids",      40, 30, 50, 10, 150,  150,   800, {   0,  40,  40,   0,   0,   0,   0 } },
        { "Mix Bus Polish",     20, 50, 60, 10, 150,  300, 16000, {   0, -20, -20,   0,  30,  20,   0 } },
    };
}

juce::StringArray VelvetTamerProcessor::presetNames()
{
    juce::StringArray names;
    for (auto& p : presets) names.add (p.name);
    return names;
}

void VelvetTamerProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (presets))) return;
    const auto& p = presets[index];

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    };
    set ("depth", p.depth);
    set ("sharpness", p.sharpness);
    set ("selectivity", p.selectivity);
    set ("attack", p.attack);
    set ("release", p.release);
    set ("low", p.low);
    set ("high", p.high);
    for (int i = 0; i < vt::numWeights; ++i)
        set (weightId (i), p.weight[i]);
}

void VelvetTamerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VelvetTamerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VelvetTamerProcessor::createEditor() { return new VelvetTamerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VelvetTamerProcessor(); }
