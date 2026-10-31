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
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());

    // 4倍オーバーサンプリング（折り返しノイズ対策）
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) std::round (oversampling->getLatencyInSamples()));

    const double osRate = sampleRate * 4.0;

    for (int ch = 0; ch < maxCh; ++ch)
    {
        inHP[ch].setCutoff (90.0f, sampleRate);
        stage1LP[ch].setCutoff (6000.0f, osRate);
        stage2LP[ch].setCutoff (4500.0f, osRate);
        dcBlock1[ch].setCutoff (25.0f, osRate);
        dcBlock2[ch].setCutoff (25.0f, osRate);
        toneLP[ch].setCutoff (480.0f, sampleRate);   // ビッグマフ系トーン回路の定数に近い値
        toneHP[ch].setCutoff (1100.0f, sampleRate);

        for (auto* f : { &inHP[ch], &stage1LP[ch], &stage2LP[ch], &dcBlock1[ch],
                         &dcBlock2[ch], &toneLP[ch], &toneHP[ch] })
            f->reset();
    }

    for (auto* s : { &driveGain, &toneMix, &outGain })
        s->reset (sampleRate, 0.03);

    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (sustainParam->load() * 4.5f + 6.0f));
    toneMix.setCurrentAndTargetValue (toneParam->load() / 10.0f);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
}

// ダイオードっぽい非対称ソフトクリップ
static inline float clipStage (float x, float bias)
{
    return std::tanh (x + bias) - std::tanh (bias);
}

void WoolFuzzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numCh = juce::jmin (numOut, maxCh);
    const int numSamples = buffer.getNumSamples();

    // Sustain 0〜10 → +6〜+51 dB
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (sustainParam->load() * 4.5f + 6.0f));
    toneMix.setTargetValue (toneParam->load() / 10.0f);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));

    // 1) 入力：低域カット＋ドライブ
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = driveGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[n] = inHP[ch].hp (d[n]) * g;
        }
    }

    // 2) オーバーサンプリングした状態で2段クリップ
    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);
    auto os = oversampling->processSamplesUp (sub);

    for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
    {
        auto* d = os.getChannelPointer (ch);
        for (size_t n = 0; n < os.getNumSamples(); ++n)
        {
            float x = d[n];
            x = clipStage (x, 0.25f);
            x = dcBlock1[ch].hp (x);
            x = stage1LP[ch].lp (x);

            x = clipStage (x * 9.0f, -0.15f);
            x = dcBlock2[ch].hp (x);
            x = stage2LP[ch].lp (x);
            d[n] = x;
        }
    }

    oversampling->processSamplesDown (sub);

    // 3) トーン（LPとHPのブレンド → 真ん中で中域がへこむ）＋出力音量
    for (int n = 0; n < numSamples; ++n)
    {
        const float t = toneMix.getNextValue();
        const float g = outGain.getNextValue() * 2.0f; // トーン回路での減衰を補正
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            const float x = d[n];
            const float lo = toneLP[ch].lp (x);
            const float hi = toneHP[ch].hp (x);
            d[n] = ((1.0f - t) * lo + t * hi) * g;
        }
    }
}

void WoolFuzzProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WoolFuzzProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* WoolFuzzProcessor::createEditor()
{
    return new WoolFuzzEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WoolFuzzProcessor();
}
