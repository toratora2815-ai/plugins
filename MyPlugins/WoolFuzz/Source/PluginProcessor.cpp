#include "PluginProcessor.h"
#include "PluginEditor.h"

WoolFuzzProcessor::WoolFuzzProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    sustainParam = apvts.getRawParameterValue ("sustain");
    toneParam    = apvts.getRawParameterValue ("tone");
    volumeParam  = apvts.getRawParameterValue ("volume");
}

juce::AudioProcessorValueTreeState::ParameterLayout WoolFuzzProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<P> (juce::ParameterID { "sustain", 1 }, "Sustain",
                                           juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 6.0f));
    params.push_back (std::make_unique<P> (juce::ParameterID { "tone", 1 }, "Tone",
                                           juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));
    params.push_back (std::make_unique<P> (juce::ParameterID { "volume", 1 }, "Volume",
                                           juce::NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -12.0f,
                                           juce::AudioParameterFloatAttributes().withLabel ("dB")));
    // Plugin Link（LINK の番号・MATCH）
    params.push_back (myp::GainMatch::makeParameter());
    params.push_back (plink::makeGroupParameter (9));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "linkMatch", 9 }, "Link Match Level", true));
    return { params.begin(), params.end() };
}

bool WoolFuzzProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void WoolFuzzProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (dsp.latency());
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
    dsp.setParams (sustainParam->load(), toneParam->load(), volumeParam->load(), true);
}

void WoolFuzzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    const int numCh = juce::jmin (numOut, pedal::WoolFuzzDSP::maxCh);
    dsp.setParams (sustainParam->load(), toneParam->load(), volumeParam->load());
    dsp.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples());

    // Plugin Link: 出口を Gain Stage の TARGET にそろえる（MATCH が ON で、同じ LINK に Gain Stage がいるとき）
    // GAIN MATCH（LEVEL MATCH より前に。ON のときはこちらを優先して LEVEL MATCH は止める）
    gainMatch.process (buffer, numCh, getLatencySamples());
    const bool gmOn = apvts.getRawParameterValue (myp::GainMatch::paramId)->load() > 0.5f;
    match.process (link, ! gmOn && apvts.getRawParameterValue ("linkMatch")->load() > 0.5f, buffer.getArrayOfWritePointers(),
                   numCh, buffer.getNumSamples(), getSampleRate());
}

void WoolFuzzProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void WoolFuzzProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

juce::AudioProcessorEditor* WoolFuzzProcessor::createEditor()
{
    return new WoolFuzzEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WoolFuzzProcessor();
}
