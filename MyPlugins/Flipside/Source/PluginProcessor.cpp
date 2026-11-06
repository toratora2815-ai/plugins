#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double maxSliceSec = 12.0;
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String ms (float v, int)  { return juce::String (v, 1) + " ms"; }
    juce::String db (float v, int)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
}

FlipsideProcessor::FlipsideProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

const juce::StringArray& FlipsideProcessor::gridNames()
{
    static const juce::StringArray g { "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS" };
    return g;
}

juce::AudioProcessorValueTreeState::ParameterLayout FlipsideProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<C> (juce::ParameterID { "grid", 1 }, "Grid", gridNames(), 2));
    v.push_back (std::make_unique<C> (juce::ParameterID { "pattern", 1 }, "Pattern",
                                      juce::StringArray { "ALL", "EVERY 2ND", "EVERY 4TH", "OFF" }, 0));
    juce::NormalisableRange<float> fadeR (1.0f, 50.0f, 0.1f);
    fadeR.setSkewForCentre (8.0f);
    v.push_back (std::make_unique<F> (juce::ParameterID { "fade", 1 }, "Fade", fadeR, 6.0f, A().withStringFromValueFunction (ms)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                      A().withStringFromValueFunction (pct)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                      A().withStringFromValueFunction (db)));
    return { v.begin(), v.end() };
}

bool FlipsideProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

double FlipsideProcessor::sliceBeats (const juce::AudioPlayHead::PositionInfo* pos) const
{
    const int g = (int) apvts.getRawParameterValue ("grid")->load();
    double barBeats = 4.0;
    if (pos != nullptr)
        if (auto ts = pos->getTimeSignature())
            if (ts->numerator > 0 && ts->denominator > 0) barBeats = ts->numerator * 4.0 / ts->denominator;
    switch (g)
    {
        case 0:  return 0.25;
        case 1:  return 0.5;
        case 2:  return 1.0;
        case 3:  return 2.0;
        case 4:  return barBeats;
        default: return barBeats * 2.0;
    }
}

void FlipsideProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs, maxSliceSec);
    // 最初の遅れ（テンポが分かるまでは前のテンポか 120）
    const double len = std::min (maxSliceSec * fs - 32, sliceBeats (nullptr) * 60.0 / lastBpm * fs);
    reportedLatency = (int) std::lround (len);
    setLatencySamples (reportedLatency);
    outGain.reset (fs, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    mixSm.reset (fs, 0.05);
    mixSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load() / 100.0f);
}

void FlipsideProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead()) pos = ph->getPosition();
    const juce::AudioPlayHead::PositionInfo* pi = pos.hasValue() ? &*pos : nullptr;

    if (pi != nullptr)
        if (auto b = pi->getBpm()) if (*b > 20.0 && *b < 400.0) lastBpm = *b;
    const double beats = sliceBeats (pi);
    const double len = std::min (maxSliceSec * fs - 32, beats * 60.0 / lastBpm * fs);

    const int lat = (int) std::lround (len);
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples (lat); }

    fl::Block b;
    b.sliceLen = len;
    b.latency = lat;
    b.pattern = (int) apvts.getRawParameterValue ("pattern")->load();
    b.fadeMs = apvts.getRawParameterValue ("fade")->load();
    mixSm.setTargetValue (apvts.getRawParameterValue ("mix")->load() / 100.0f);
    b.mix = mixSm.skip (n);
    b.synced = false;
    if (pi != nullptr && pi->getIsPlaying())
        if (auto ppq = pi->getPpqPosition())
        {
            const double s = juce::jmax (0.0, *ppq) / beats;
            b.synced = true;
            b.index = (int64_t) std::floor (s);
            b.phase = (s - (double) b.index) * len;
        }
    engine.process (buffer.getArrayOfWritePointers(), numCh, n, b);

    synced = b.synced;
    sliceSeconds = (float) (len / fs);
    bpmShown = (float) lastBpm;

    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] *= g;
    }
}

void FlipsideProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FlipsideProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FlipsideProcessor::createEditor() { return new FlipsideEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FlipsideProcessor(); }
