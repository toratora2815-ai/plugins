#include "PluginProcessor.h"
#include "PluginEditor.h"

DecaSatuProcessor::DecaSatuProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pStyle   = apvts.getRawParameterValue ("style");
    pDrive   = apvts.getRawParameterValue ("drive");
    pPunish  = apvts.getRawParameterValue ("punish");
    pLowCut  = apvts.getRawParameterValue ("lowCut");
    pThump   = apvts.getRawParameterValue ("thump");
    pTone    = apvts.getRawParameterValue ("tone");
    pHighCut = apvts.getRawParameterValue ("highCut");
    pSteep   = apvts.getRawParameterValue ("steep");
    pMix     = apvts.getRawParameterValue ("mix");
    pOutput  = apvts.getRawParameterValue ("output");
    pGainMatch = apvts.getRawParameterValue (myp::GainMatch::paramId);
    pLinkMatch = apvts.getRawParameterValue ("linkMatch");
}

static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float v) { return a * std::pow (b / a, v); },
             [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); },
             [] (float a, float b, float x) { return juce::jlimit (a, b, std::round (x)); } };
}

juce::AudioProcessorValueTreeState::ParameterLayout DecaSatuProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "style", 1 }, "Style",
                                                                    juce::StringArray { "A", "E", "N", "T", "P" }, 0));
    params.push_back (std::make_unique<F> (juce::ParameterID { "drive", 1 }, "Drive",
                                           juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));
    params.push_back (std::make_unique<B> (juce::ParameterID { "punish", 1 }, "Punish", false));
    params.push_back (std::make_unique<F> (juce::ParameterID { "lowCut", 1 }, "Low Cut", logRange (20.0f, 1000.0f), 20.0f,
                                           A().withLabel ("Hz")));
    params.push_back (std::make_unique<B> (juce::ParameterID { "thump", 1 }, "Thump", false));
    params.push_back (std::make_unique<F> (juce::ParameterID { "tone", 1 }, "Tone",
                                           juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, A().withLabel ("dB")));
    params.push_back (std::make_unique<F> (juce::ParameterID { "highCut", 1 }, "High Cut", logRange (1000.0f, 20000.0f), 20000.0f,
                                           A().withLabel ("Hz")));
    params.push_back (std::make_unique<B> (juce::ParameterID { "steep", 1 }, "Steep", false));
    params.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mix",
                                           juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withLabel ("%")));
    params.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output",
                                           juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f, A().withLabel ("dB")));
    params.push_back (myp::GainMatch::makeParameter());
    params.push_back (plink::makeGroupParameter (1));
    params.push_back (std::make_unique<B> (juce::ParameterID { "linkMatch", 1 }, "Link Match Level", true));
    return { params.begin(), params.end() };
}

bool DecaSatuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void DecaSatuProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (engine.latencySamples());
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
}

void DecaSatuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    const int numCh = juce::jmin (numOut, decasatu::Engine::maxCh);

    decasatu::Knobs k;
    k.style = (int) pStyle->load();
    k.drive = pDrive->load();
    k.punish = pPunish->load() > 0.5f;
    k.lowCutHz = pLowCut->load();
    k.thump = pThump->load() > 0.5f;
    k.tone = pTone->load() / 12.0f;
    k.highCutHz = pHighCut->load();
    k.steep = pSteep->load() > 0.5f;
    k.mix = pMix->load() / 100.0f;
    k.outputDb = pOutput->load();
    // GAIN MATCH が ON の間は、本物の AUTO と同じ量だけ DRIVE に合わせて出口を下げる（つまみを回している最中も音量がそろう）。
    // 残りの差は GainMatch が測って埋める
    const bool gmOn = pGainMatch->load() > 0.5f;
    k.extraGainDb = gmOn ? (float) decasatu::Engine::autoDb (k) : 0.0f;   // エンジンの中で 20 ms でなめらかに変わる
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples(), k);

    gainMatch.process (buffer, numCh, getLatencySamples());
    match.process (link, ! gmOn && pLinkMatch->load() > 0.5f, buffer.getArrayOfWritePointers(),
                   numCh, buffer.getNumSamples(), getSampleRate());

    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, buffer.getNumSamples()));
    const float pkDb = juce::Decibels::gainToDecibels (pk, -100.0f);
    const float prev = outPeakDb.load();
    outPeakDb.store (pkDb > prev ? pkDb : prev - 0.3f);
}

void DecaSatuProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void DecaSatuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

juce::AudioProcessorEditor* DecaSatuProcessor::createEditor()
{
    return new DecaSatuEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecaSatuProcessor();
}
