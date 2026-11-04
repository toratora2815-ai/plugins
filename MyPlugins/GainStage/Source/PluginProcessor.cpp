#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String db (float v, int)   { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String dbfs (float v, int) { return juce::String (v, 1) + " dB"; }
}

GainStageProcessor::GainStageProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    startTimerHz (20);
}

juce::AudioProcessorValueTreeState::ParameterLayout GainStageProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;

    v.push_back (std::make_unique<F> (juce::ParameterID { "gain", 1 }, "Gain",
                                      juce::NormalisableRange<float> (-36.0f, 36.0f, 0.1f), 0.0f, A().withStringFromValueFunction (db)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "target", 1 }, "Target",
                                      juce::NormalisableRange<float> (-36.0f, 0.0f, 0.5f), -18.0f, A().withStringFromValueFunction (dbfs)));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "measure", 1 }, "Measure",
                                                               juce::StringArray { "RMS", "Peak" }, 0));
    v.push_back (std::make_unique<B> (juce::ParameterID { "polarity", 1 }, "Polarity Invert", false));
    v.push_back (std::make_unique<B> (juce::ParameterID { "mono", 1 }, "Mono", false));
    v.push_back (std::make_unique<B> (juce::ParameterID { "swap", 1 }, "Swap L/R", false));
    return { v.begin(), v.end() };
}

bool GainStageProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GainStageProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (0);
}

void GainStageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    gs::Settings s;
    s.gainDb = f ("gain");
    s.polarity = f ("polarity") > 0.5f;
    s.mono = f ("mono") > 0.5f;
    s.swap = f ("swap") > 0.5f;
    engine.setSettings (s);
    engine.process (buffer.getArrayOfWritePointers(), getMainBusNumInputChannels(), buffer.getNumSamples());
}

void GainStageProcessor::toggleLearn()
{
    if (engine.isLearning()) engine.finishLearn();
    else                     engine.startLearn();
}

void GainStageProcessor::timerCallback()
{
    float rmsDb = 0.0f, peakDb = 0.0f;
    if (engine.takeLearnResult (rmsDb, peakDb))
    {
        const bool peak = apvts.getRawParameterValue ("measure")->load() > 0.5f;
        const float measured = peak ? peakDb : rmsDb;
        const float target = apvts.getRawParameterValue ("target")->load();
        const float gain = juce::jlimit (-36.0f, 36.0f, target - measured);

        if (auto* p = apvts.getParameter ("gain"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (gain));
            p->endChangeGesture();
        }
        juce::String info;
        info << "IN " << juce::String (measured, 1) << " dB " << (peak ? "PEAK" : "RMS")
             << "  ->  GAIN " << (gain > 0.05f ? "+" : "") << juce::String (gain, 1) << " dB"
             << "   (RMS " << juce::String (rmsDb, 1) << " / PEAK " << juce::String (peakDb, 1) << ")";
        if (std::abs (target - measured) > 36.0f) info << "   * limited to +-36 dB";
        apvts.state.setProperty ("learnInfo", info, nullptr);
    }
    else if (wasLearning && ! engine.isLearning())
        flash = "No signal while learning - play the track and press LEARN again";   // 無音だけで終わった
    wasLearning = engine.isLearning();
}

void GainStageProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GainStageProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* GainStageProcessor::createEditor() { return new GainStageEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GainStageProcessor(); }
