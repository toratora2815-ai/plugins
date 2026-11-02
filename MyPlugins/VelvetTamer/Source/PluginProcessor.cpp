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
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
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

    return { v.begin(), v.end() };
}

bool VelvetTamerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void VelvetTamerProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setSettings (readSettings());
    engine.prepare (sampleRate, getTotalNumOutputChannels());
    setLatencySamples (engine.getLatency());
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
    return s;
}

void VelvetTamerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
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
