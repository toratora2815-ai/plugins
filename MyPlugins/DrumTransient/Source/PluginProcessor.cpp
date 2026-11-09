#include "PluginProcessor.h"
#include "PluginEditor.h"

DrumTransientProcessor::DrumTransientProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pPush = apvts.getRawParameterValue ("push");
    pPunch = apvts.getRawParameterValue ("punch");
    pBody = apvts.getRawParameterValue ("body");
    pClip = apvts.getRawParameterValue ("clip");
    pRelease = apvts.getRawParameterValue ("release");
    pLowKeep = apvts.getRawParameterValue ("lowKeep");
    pCeiling = apvts.getRawParameterValue ("ceiling");
    pLink = apvts.getRawParameterValue ("stereoLink");
    pDelta = apvts.getRawParameterValue ("delta");
    pGainMatch = apvts.getRawParameterValue (myp::GainMatch::paramId);
    gainMatch.limitBoost (0.0f);   // 最後がリミッターなので、GAIN MATCH は下げるだけ（上げると天井を越える）
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumTransientProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x)) + " %"; };
    v.push_back (std::make_unique<F> (juce::ParameterID { "push", 1 }, "Push",
                                      juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 6.0f, A().withLabel ("dB")));
    v.push_back (std::make_unique<F> (juce::ParameterID { "punch", 1 }, "Punch",
                                      juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, A().withStringFromValueFunction (pct)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "body", 1 }, "Body",
                                      juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                      A().withStringFromValueFunction ([] (float x, int) { const int i = juce::roundToInt (x); return (i > 0 ? "+" : "") + juce::String (i); })));
    v.push_back (std::make_unique<F> (juce::ParameterID { "clip", 1 }, "Clip / Limit",
                                      juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
                                      A().withStringFromValueFunction ([] (float x, int) { const int i = juce::roundToInt (x); return "CLIP " + juce::String (i) + " / " + juce::String (100 - i); })));
    v.push_back (std::make_unique<F> (juce::ParameterID { "release", 1 }, "Release",
                                      juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                      A().withStringFromValueFunction ([] (float x, int) { const int i = juce::roundToInt (x); return i < 34 ? "TIGHT " + juce::String (i) : i < 67 ? juce::String (i) : "SMOOTH " + juce::String (i); })));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lowKeep", 1 }, "Low Keep", lowKeepNames(), 2));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ceiling", 1 }, "Ceiling", ceilingNames(), 1));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "stereoLink", 1 }, "Stereo Link", true));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 }, "Delta", false));
    v.push_back (myp::GainMatch::makeParameter());
    v.push_back (plink::makeGroupParameter (1));
    return { v.begin(), v.end() };
}

dt::Params DrumTransientProcessor::readParams() const
{
    static constexpr float lowKeepHz[] = { 0.0f, 60.0f, 90.0f, 120.0f, 160.0f };
    static constexpr float ceil[] = { -0.1f, -0.3f, -1.0f };
    dt::Params p;
    p.pushDb = pPush->load();
    p.punch = pPunch->load() * 0.01f;
    p.body = pBody->load() * 0.01f;
    p.clip = pClip->load() * 0.01f;
    p.release = pRelease->load() * 0.01f;
    p.lowKeepHz = lowKeepHz[juce::jlimit (0, 4, (int) pLowKeep->load())];
    p.ceilingDb = ceil[juce::jlimit (0, 2, (int) pCeiling->load())];
    p.link = pLink->load() > 0.5f;
    p.delta = pDelta->load() > 0.5f;
    return p;
}

bool DrumTransientProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void DrumTransientProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()), readParams());
    setLatencySamples (dsp.latency());
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
}

void DrumTransientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numCh = juce::jmin (numOut, dt::Engine::maxCh);
    // 前のプラグインから NaN・無限大が来ても GAIN MATCH の測定に入れない（入ると抜けない）
    for (int ch = 0; ch < numIn; ++ch)
        for (auto* d = buffer.getWritePointer (ch), *e = d + buffer.getNumSamples(); d != e; ++d)
            if (! std::isfinite (*d)) *d = 0.0f;
    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    const auto p = readParams();
    dsp.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples(), p);
    gainMatch.process (buffer, numCh, getLatencySamples(), p.delta ? 0.0f : 1.0f);
}

void DrumTransientProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumTransientProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

juce::StringArray DrumTransientProcessor::presetNames()
{
    return { "Init", "Kick", "Snare", "Drum Bus", "Room / Overheads", "Breaks / Loops", "Smash (parallel)" };
}

void DrumTransientProcessor::applyPreset (int index)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    // まず音のつまみを初期値に（GAIN MATCH・LINK の番号・CEILING・STEREO LINK・DELTA はプリセットで変えない。
    // 配信用に -1.0 dB にしていた CEILING が、プリセットを選んだだけで戻らないように）
    static const juce::StringArray keep { myp::GainMatch::paramId, "linkGroup", "ceiling", "stereoLink", "delta" };
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! keep.contains (r->getParameterID()))
                r->setValueNotifyingHost (r->getDefaultValue());

    // push, punch, body, clip, release, lowKeep（0=OFF 1=60 2=90 3=120 4=160）
    auto main = [&] (float push, float punch, float body, float clip, float release, int lowKeep)
    {
        set ("push", push); set ("punch", punch); set ("body", body); set ("clip", clip); set ("release", release); set ("lowKeep", (float) lowKeep);
    };
    switch (index)
    {
        case 1: main (6.0f, 70.0f, 0.0f, 30.0f, 40.0f, 3); break;     // Kick: 基音は切らない、アタックを残す
        case 2: main (8.0f, 75.0f, 10.0f, 60.0f, 35.0f, 2); break;    // Snare: 先を切って前に、胴を少し
        case 3: main (6.0f, 55.0f, 0.0f, 40.0f, 55.0f, 3); break;     // Drum Bus
        case 4: main (8.0f, 40.0f, 40.0f, 20.0f, 70.0f, 2); break;    // Room / OH: 響きを前に
        case 5: main (10.0f, 60.0f, 15.0f, 50.0f, 45.0f, 2); break;   // Breaks / Loops
        case 6: main (16.0f, 30.0f, 50.0f, 80.0f, 30.0f, 1); break;   // Smash: 並列（センド）用
        default: break;   // Init
    }
    gainMatch.requestRelearn();   // GAIN MATCH: プリセットを読んだら測り直す
}

juce::AudioProcessorEditor* DrumTransientProcessor::createEditor() { return new DrumTransientEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumTransientProcessor(); }
