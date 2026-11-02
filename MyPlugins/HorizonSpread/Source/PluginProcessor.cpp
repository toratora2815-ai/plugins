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
    juce::String bass (float v, int) { return v <= 20.5f ? juce::String ("OFF") : hz (v, 0); }
    juce::String ms (float v, int)    { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String cents (float v, int) { return juce::String (v, 1) + " ct"; }
    juce::String rate (float v, int)  { return juce::String (v, 2) + " Hz"; }
    juce::String trans (float v, int)
    {
        const int i = juce::roundToInt (v);
        return i == 0 ? juce::String ("0") : (i > 0 ? "+" : "") + juce::String (i);
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
    juce::NormalisableRange<float> freqRange (float lo, float hi, float centre) { return skewed (lo, hi, 1.0f, centre); }
}

HorizonSpreadProcessor::HorizonSpreadProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    startTimerHz (10);
}

juce::AudioProcessorValueTreeState::ParameterLayout HorizonSpreadProcessor::createLayout()
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
    // CREATE ページ（第2段階で追加。バージョン 2）
    auto addF2 = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                      std::function<juce::String (float, int)> toText)
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 2 }, name, r, def,
                                          A().withStringFromValueFunction (std::move (toText))));
    };
    auto addB2 = [&] (const char* id, const char* name, bool def)
    {
        v.push_back (std::make_unique<B> (juce::ParameterID { id, 2 }, name, def));
    };
    auto addC2 = [&] (const char* id, const char* name, juce::StringArray choices)
    {
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 2 }, name, choices, 0));
    };

    // 全体
    addF ("spread", "Spread", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    addF ("mix",    "Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    addF ("output", "Output", { -12.0f, 12.0f, 0.1f }, 0.0f, db);

    // WIDTH
    addB ("widthOn", "Width On", true);
    addF ("widthLow",  "Low Width",  { 0.0f, 200.0f, 0.1f }, 100.0f, pct);
    addF ("widthMid",  "Mid Width",  { 0.0f, 200.0f, 0.1f }, 115.0f, pct);
    addF ("widthHigh", "High Width", { 0.0f, 200.0f, 0.1f }, 130.0f, pct);
    addF ("xoverLow",  "Low/Mid Split",  freqRange (80.0f, 1000.0f, 250.0f), 250.0f, hz);
    addF ("xoverHigh", "Mid/High Split", freqRange (1500.0f, 12000.0f, 4000.0f), 4000.0f, hz);

    // AIR
    addB ("airOn", "Air On", true);
    addF ("air",     "Air",      { 0.0f, 9.0f, 0.1f }, 2.0f, db);
    addF ("airFreq", "Air Freq", freqRange (2000.0f, 16000.0f, 8000.0f), 10000.0f, hz);

    // SIDE DRIVE
    addB ("driveOn", "Drive On", true);
    addF ("drive", "Side Drive", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);

    // TRANSIENT
    addB ("transOn", "Transient On", true);
    addF ("transient", "Transient", { -100.0f, 100.0f, 1.0f }, 0.0f, trans);

    // 安全装置
    addF ("bassMono", "Bass Mono", freqRange (20.0f, 300.0f, 100.0f), 100.0f, bass);
    addB ("guard", "Guard", true);
    addB ("monoCheck", "Mono Check", false);

    // SHIFT（ずらし）
    addB2 ("shiftOn", "Shift On", true);
    addF2 ("shiftTime", "Shift Time", skewed (0.1f, 25.0f, 0.01f, 6.0f), 8.0f, ms);
    addF2 ("shiftAmount", "Shift Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    addC2 ("shiftMode", "Shift Mode", { "SAFE", "CLASSIC" });

    // DOUBLE（ダブル）
    addB2 ("doubleOn", "Double On", true);
    addF2 ("doubleAmount", "Double Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    addF2 ("detune", "Detune", { 0.0f, 20.0f, 0.1f }, 8.0f, cents);

    // BANDS（ばらまき）
    addB2 ("bandsOn", "Bands On", true);
    addF2 ("bandsAmount", "Bands Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    addF2 ("bandsCount", "Bands", { 4.0f, 12.0f, 1.0f }, 8.0f, [] (float x, int) { return juce::String (juce::roundToInt (x)); });
    addC2 ("bandsMode", "Bands Mode", { "ALT", "TILT" });

    // HAZE（もわっと）
    addB2 ("hazeOn", "Haze On", true);
    addF2 ("hazeAmount", "Haze Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    addF2 ("hazeSize", "Haze Size", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    addC2 ("hazeMode", "Haze Mode", { "CLOUD", "SMOOTH" });

    // MOTION（ゆらぎ）
    addB2 ("motionOn", "Motion On", true);
    addF2 ("motionRate", "Motion Rate", skewed (0.05f, 2.0f, 0.01f, 0.3f), 0.25f, rate);
    addF2 ("motionDepth", "Motion Depth", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);

    return { v.begin(), v.end() };
}

bool HorizonSpreadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void HorizonSpreadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatency());
}

hs::ShapeSettings HorizonSpreadProcessor::readSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto b = [&] (const char* id) { return f (id) > 0.5f; };

    hs::ShapeSettings s;
    s.spread = f ("spread") * 0.01f;
    s.mix = f ("mix") * 0.01f;
    s.outDb = f ("output");
    s.widthOn = b ("widthOn");
    s.wLow = f ("widthLow") * 0.01f;
    s.wMid = f ("widthMid") * 0.01f;
    s.wHigh = f ("widthHigh") * 0.01f;
    s.xLow = f ("xoverLow");
    s.xHigh = f ("xoverHigh");
    s.airOn = b ("airOn");
    s.airDb = f ("air");
    s.airFreq = f ("airFreq");
    s.driveOn = b ("driveOn");
    s.drive = f ("drive") * 0.01f;
    s.transOn = b ("transOn");
    s.transient = f ("transient") * 0.01f;
    s.bassMonoHz = f ("bassMono");
    s.guard = b ("guard");
    s.mono = b ("monoCheck");

    auto& c = s.create;
    c.shiftOn = b ("shiftOn");
    c.shiftMs = f ("shiftTime");
    c.shiftAmt = f ("shiftAmount") * 0.01f;
    c.shiftClassic = b ("shiftMode");
    c.doubleOn = b ("doubleOn");
    c.doubleAmt = f ("doubleAmount") * 0.01f;
    c.detune = f ("detune");
    c.bandsOn = b ("bandsOn");
    c.bandsAmt = f ("bandsAmount") * 0.01f;
    c.bands = juce::roundToInt (f ("bandsCount"));
    c.bandsTilt = b ("bandsMode");
    c.hazeOn = b ("hazeOn");
    c.hazeAmt = f ("hazeAmount") * 0.01f;
    c.hazeSize = f ("hazeSize") * 0.01f;
    c.hazeSmooth = b ("hazeMode");
    c.motionOn = b ("motionOn");
    c.motionRate = f ("motionRate");
    c.motionDepth = f ("motionDepth") * 0.01f;
    return s;
}

void HorizonSpreadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // モノラル入力のときは右にも同じ音を入れる
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    if (buffer.getNumChannels() < 2) return;

    engine.setSettings (readSettings());
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

// AUTO の結果を WIDTH のつまみに反映（メッセージスレッドで）
void HorizonSpreadProcessor::timerCallback()
{
    if (engine.autoState.load() != hs::SpreadEngine::autoReady) return;

    const char* ids[] = { "widthLow", "widthMid", "widthHigh" };
    for (int b = 0; b < 3; ++b)
    {
        if (auto* p = apvts.getParameter (ids[b]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (engine.autoResult[b].load() * 100.0f));
            p->endChangeGesture();
        }
    }
    engine.autoState = hs::SpreadEngine::autoApplied;
}

void HorizonSpreadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HorizonSpreadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HorizonSpreadProcessor::createEditor() { return new HorizonSpreadEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HorizonSpreadProcessor(); }
