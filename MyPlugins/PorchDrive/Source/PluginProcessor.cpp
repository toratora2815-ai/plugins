#include "PluginProcessor.h"
#include "PluginEditor.h"

PorchDriveProcessor::PorchDriveProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    gainParam  = apvts.getRawParameterValue ("gain");
    toneParam  = apvts.getRawParameterValue ("tone");
    levelParam = apvts.getRawParameterValue ("level");
}

juce::AudioProcessorValueTreeState::ParameterLayout PorchDriveProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<P> (juce::ParameterID { "level", 1 }, "Level",
                                      juce::NormalisableRange<float> (-30.0f, 12.0f, 0.1f), -6.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("dB")));
    v.push_back (std::make_unique<P> (juce::ParameterID { "tone", 1 }, "Tone",
                                      juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));
    v.push_back (std::make_unique<P> (juce::ParameterID { "gain", 1 }, "Gain",
                                      juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f));
    // Plugin Link（LINK の番号・MATCH）
    v.push_back (myp::GainMatch::makeParameter());
    v.push_back (plink::makeGroupParameter (9));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "linkMatch", 9 }, "Link Match Level", true));
    return { v.begin(), v.end() };
}

bool PorchDriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PorchDriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (dsp.latency());
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
    dsp.setParams (gainParam->load(), toneParam->load(), levelParam->load(), true);
}

void PorchDriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    const int numCh = juce::jmin (numOut, pedal::PorchDriveDSP::maxCh);
    dsp.setParams (gainParam->load(), toneParam->load(), levelParam->load());
    dsp.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples());

    // Plugin Link: 出口を Gain Stage の TARGET にそろえる（MATCH が ON で、同じ LINK に Gain Stage がいるとき）
    // GAIN MATCH（LEVEL MATCH より前に。ON のときはこちらを優先して LEVEL MATCH は止める）
    gainMatch.process (buffer, numCh, getLatencySamples());
    const bool gmOn = apvts.getRawParameterValue (myp::GainMatch::paramId)->load() > 0.5f;
    match.process (link, ! gmOn && apvts.getRawParameterValue ("linkMatch")->load() > 0.5f, buffer.getArrayOfWritePointers(),
                   numCh, buffer.getNumSamples(), getSampleRate());
}

void PorchDriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void PorchDriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

juce::AudioProcessorEditor* PorchDriveProcessor::createEditor() { return new PorchDriveEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PorchDriveProcessor(); }
