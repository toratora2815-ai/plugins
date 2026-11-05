#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String db (float v, int)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String dbfs (float v, int) { return juce::String (v, 1) + " dB"; }
    juce::String ms (float v, int)  { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; }

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
}

FaderPilotProcessor::FaderPilotProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    startTimerHz (30);
}

FaderPilotProcessor::~FaderPilotProcessor()
{
    stopTimer();
    if (writing)
        if (auto* p = apvts.getParameter ("ride")) p->endChangeGesture();
}

juce::AudioProcessorValueTreeState::ParameterLayout FaderPilotProcessor::createLayout()
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

    // 目標
    addF ("target", "Target", { -40.0f, -6.0f, 0.1f }, -18.0f, dbfs);
    addF ("floor",  "Floor",  { -80.0f, -20.0f, 0.1f }, -50.0f, dbfs);
    // 動き
    addF ("range", "Range", { 0.0f, 15.0f, 0.1f }, 6.0f, db);
    addF ("speed", "Speed", skewed (20.0f, 2000.0f, 1.0f, 250.0f), 250.0f, ms);
    // オケに合わせる
    addB ("follow", "Follow Music", false);
    addF ("offset", "Music Offset", { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    // フェーダー
    addB ("auto",  "Auto Ride", true);
    addB ("write", "Write Ride", false);
    addF ("ride",  "Ride", { -15.0f, 15.0f, 0.01f }, 0.0f, db);
    addF ("output", "Output", { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    // v1.1 で追加（バージョン 2）
    v.push_back (plink::makeGroupParameter (2));

    return { v.begin(), v.end() };
}

bool FaderPilotProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

fp::Settings FaderPilotProcessor::readSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    fp::Settings s;
    s.targetDb = f ("target");
    s.floorDb = f ("floor");
    s.rangeDb = f ("range");
    s.speedMs = f ("speed");
    s.follow = f ("follow") > 0.5f;
    s.offsetDb = f ("offset");
    s.autoRide = f ("auto") > 0.5f;
    s.manualDb = f ("ride");
    s.outDb = f ("output");
    return s;
}

void FaderPilotProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setSettings (readSettings());
    engine.prepare (sampleRate);
    setLatencySamples (0);
}

void FaderPilotProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    bool isPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            isPlaying = pos->getIsPlaying();
    playing = isPlaying;

    auto main = getBusBuffer (buffer, true, 0);
    juce::AudioBuffer<float> scBuf;   // ホストの音を指すだけ（コピーしない）
    const float* const* sc = nullptr;
    int numSc = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            scBuf = getBusBuffer (buffer, true, 1);
            sc = scBuf.getArrayOfReadPointers();
            numSc = scBuf.getNumChannels();
        }
    scConnected = numSc > 0;

    engine.setSettings (readSettings());
    engine.process (main.getArrayOfWritePointers(), main.getNumChannels(), main.getNumSamples(), sc, numSc);

    // Plugin Link へ：今のフェーダー位置と、声が鳴っているか（ブロックごと）
    if (link.on())
    {
        auto& ep = link.ep();
        ep.publish (plink::fp::rideDb, engine.getGainDb());
        ep.publish (plink::fp::active, engine.isVoiceActive() ? 1.0f : 0.0f);
        ep.stamp();
        ep.heartbeat();
    }
}

// WRITE ON（かつ AUTO ON）で再生している間、今のフェーダー位置を「Ride」パラメーターに送り続ける。
// Logic のオートメーションが Latch / Touch / Write なら、それがそのまま記録される。
void FaderPilotProcessor::timerCallback()
{
    auto* ride = apvts.getParameter ("ride");
    if (ride == nullptr) return;

    const bool shouldWrite = apvts.getRawParameterValue ("write")->load() > 0.5f
                          && apvts.getRawParameterValue ("auto")->load() > 0.5f
                          && playing.load();
    if (shouldWrite)
    {
        if (! writing) { ride->beginChangeGesture(); writing = true; }
        const float v = ride->convertTo0to1 (juce::jlimit (-15.0f, 15.0f, engine.getGainDb()));
        if (std::abs (v - ride->getValue()) > 0.0005f)
            ride->setValueNotifyingHost (v);
    }
    else if (writing)
    {
        ride->endChangeGesture();
        writing = false;
    }
}

namespace
{
    struct Preset { const char* name; float target, floor, range, speed; bool follow; float offset; };

    const Preset presets[] =
    {
        { "Default",              -18, -50,  6, 250, false, 0 },
        { "Pop Vocal",            -18, -45,  6, 250, false, 0 },
        { "Rap / Fast Words",     -16, -42,  6, 120, false, 0 },
        { "Ballad (Gentle)",      -20, -50,  4, 600, false, 0 },
        { "Narration",            -18, -45,  9, 200, false, 0 },
        { "Bass Guitar",          -20, -50,  4, 400, false, 0 },
        { "Follow Mix (+2 dB)",   -18, -45,  6, 300, true,  2 },
        { "Follow Mix (Behind)",  -18, -45,  6, 400, true, -2 },
    };
}

juce::StringArray FaderPilotProcessor::presetNames()
{
    juce::StringArray names;
    for (auto& p : presets) names.add (p.name);
    return names;
}

void FaderPilotProcessor::applyPreset (int index)
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
    set ("target", p.target);
    set ("floor", p.floor);
    set ("range", p.range);
    set ("speed", p.speed);
    set ("follow", p.follow ? 1.0f : 0.0f);
    set ("offset", p.offset);
}

void FaderPilotProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FaderPilotProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FaderPilotProcessor::createEditor() { return new FaderPilotEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FaderPilotProcessor(); }
