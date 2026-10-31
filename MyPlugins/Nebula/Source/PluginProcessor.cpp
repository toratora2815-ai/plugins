#include "PluginProcessor.h"
#include "PluginEditor.h"

NebulaProcessor::NebulaProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NebulaProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");
    auto hz  = juce::AudioParameterFloatAttributes().withLabel ("Hz");

    R decay (1.0f, 30.0f, 0.01f);   decay.setSkewForCentre (8.0f);
    R damp (1000.0f, 20000.0f, 1.0f); damp.setSkewForCentre (5000.0f);
    R low (20.0f, 800.0f, 1.0f);    low.setSkewForCentre (150.0f);

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 },      "Mix",       R (0, 100, 0.1f), 35.0f, pct));
    v.push_back (std::make_unique<F> (juce::ParameterID { "decay", 1 },    "Decay",     decay, 8.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("s")));
    v.push_back (std::make_unique<F> (juce::ParameterID { "size", 1 },     "Size",      R (0, 100, 0.1f), 70.0f, pct));
    v.push_back (std::make_unique<F> (juce::ParameterID { "predelay", 1 }, "Pre-Delay", R (0, 250, 0.1f), 40.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("ms")));
    v.push_back (std::make_unique<F> (juce::ParameterID { "damping", 1 },  "Damping",   damp, 6000.0f, hz));
    v.push_back (std::make_unique<F> (juce::ParameterID { "mod", 1 },      "Modulation",R (0, 100, 0.1f), 40.0f, pct));
    v.push_back (std::make_unique<F> (juce::ParameterID { "shimmer", 1 },  "Shimmer",   R (0, 100, 0.1f), 0.0f, pct));
    v.push_back (std::make_unique<F> (juce::ParameterID { "width", 1 },    "Width",     R (0, 150, 0.1f), 100.0f, pct));
    v.push_back (std::make_unique<F> (juce::ParameterID { "lowcut", 1 },   "Low Cut",   low, 120.0f, hz));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false));
    return { v.begin(), v.end() };
}

bool NebulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void NebulaProcessor::prepareToPlay (double sampleRate, int)
{
    reverb.prepare ((float) sampleRate);
    mixSmoothed.reset (sampleRate, 0.05);
    mixSmoothed.setCurrentAndTargetValue (p ("mix") / 100.0f);
}

void NebulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    reverb.decaySec   = p ("decay");
    reverb.size       = 0.5f + 2.0f * p ("size") / 100.0f;
    reverb.preDelayMs = p ("predelay");
    reverb.dampHz     = p ("damping");
    reverb.modAmount  = p ("mod") / 100.0f;
    reverb.shimmer    = p ("shimmer") / 100.0f;
    reverb.width      = p ("width") / 100.0f;
    reverb.lowCutHz   = p ("lowcut");
    reverb.freeze     = p ("freeze") > 0.5f;
    reverb.update();
    mixSmoothed.setTargetValue (p ("mix") / 100.0f);

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    auto* L = buffer.getWritePointer (0);
    auto* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool stereoIn = numIn > 1;

    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i];
        const float dr = stereoIn ? R[i] : dl;
        float wl, wr;
        reverb.process (dl, dr, wl, wr);

        const float m = mixSmoothed.getNextValue();
        const float dryG = std::cos (m * juce::MathConstants<float>::halfPi);
        const float wetG = std::sin (m * juce::MathConstants<float>::halfPi);

        if (R != nullptr)
        {
            L[i] = dl * dryG + wl * wetG;
            R[i] = dr * dryG + wr * wetG;
        }
        else
        {
            L[i] = dl * dryG + 0.5f * (wl + wr) * wetG;
        }
    }
}

void NebulaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NebulaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NebulaProcessor::createEditor() { return new NebulaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NebulaProcessor(); }
