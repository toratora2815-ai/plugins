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
    return { v.begin(), v.end() };
}

bool PorchDriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

// 1段目：トランジスタ的なゆるい非対称ソフトクリップ（偶数倍音で温かく）
static inline float softStage (float x)
{
    const float b = 0.2f;
    return std::tanh (x + b) - std::tanh (b);
}

// 2段目：ダイオード対のクリップ（膝がやや硬め）
static inline float diodeStage (float x)
{
    const float p = 2.5f;
    return x / std::pow (1.0f + std::pow (std::abs (x), p), 1.0f / p);
}

static float gain1dB (float knob) { return knob * 2.6f; }          // 0〜26 dB
static float gain2dB (float knob) { return 2.0f + knob * 1.9f; }   // 2〜21 dB
static float toneDB  (float knob) { return (knob - 5.0f) * 2.4f; } // 高域 ±12 dB

void PorchDriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) std::round (oversampling->getLatencyInSamples()));

    g1Buf.assign ((size_t) samplesPerBlock * 4 + 16, 1.0f);
    g2Buf.assign ((size_t) samplesPerBlock * 4 + 16, 1.0f);

    const double os = sampleRate * 4.0;
    for (int ch = 0; ch < maxCh; ++ch)
    {
        inHP[ch].setCutoff (70.0f, sampleRate);
        dc1[ch].setCutoff (20.0f, os);
        dc2[ch].setCutoff (20.0f, os);
        lp1[ch].setCutoff (9000.0f, os);
        lp2[ch].setCutoff (7000.0f, os);
        toneSplit[ch].setCutoff (900.0f, sampleRate);
        fizzLP[ch].setCutoff (9500.0f, sampleRate);
        for (auto* f : { &inHP[ch], &dc1[ch], &dc2[ch], &lp1[ch], &lp2[ch], &toneSplit[ch], &fizzLP[ch] })
            f->reset();
    }

    for (auto* s : { &g1, &g2, &toneGain, &outGain })
        s->reset (sampleRate * 4.0, 0.03);

    const float gk = gainParam->load();
    g1.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain1dB (gk)));
    g2.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain2dB (gk)));
    toneGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (toneDB (toneParam->load())));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));
}

void PorchDriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numCh = juce::jmin (numOut, maxCh);
    const int numSamples = buffer.getNumSamples();

    const float gk = gainParam->load();
    g1.setTargetValue (juce::Decibels::decibelsToGain (gain1dB (gk)));
    g2.setTargetValue (juce::Decibels::decibelsToGain (gain2dB (gk)));
    toneGain.setTargetValue (juce::Decibels::decibelsToGain (toneDB (toneParam->load())));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            d[n] = inHP[ch].hp (d[n]);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);
    auto os = oversampling->processSamplesUp (sub);
    const size_t osN = os.getNumSamples();

    if (g1Buf.size() < osN) { g1Buf.resize (osN); g2Buf.resize (osN); }
    for (size_t n = 0; n < osN; ++n) { g1Buf[n] = g1.getNextValue(); g2Buf[n] = g2.getNextValue(); }

    for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
    {
        auto* d = os.getChannelPointer (ch);
        for (size_t n = 0; n < osN; ++n)
        {
            float x = softStage (d[n] * g1Buf[n]);
            x = lp1[ch].lp (dc1[ch].hp (x));
            x = diodeStage (x * g2Buf[n]);
            x = lp2[ch].lp (dc2[ch].hp (x));
            d[n] = x;
        }
    }

    oversampling->processSamplesDown (sub);

    // トーン：900Hz より上をブースト／カット
    for (int n = 0; n < numSamples; ++n)
    {
        float tg = 0.0f, og = 0.0f;
        for (int k = 0; k < 4; ++k) { tg = toneGain.getNextValue(); og = outGain.getNextValue(); }
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            const float lo = toneSplit[ch].lp (d[n]);
            const float hi = d[n] - lo;
            d[n] = fizzLP[ch].lp (lo + hi * tg) * og;
        }
    }
}

void PorchDriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PorchDriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PorchDriveProcessor::createEditor() { return new PorchDriveEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PorchDriveProcessor(); }
