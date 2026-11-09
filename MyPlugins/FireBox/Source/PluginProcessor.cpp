#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

FireBoxProcessor::FireBoxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto f = [this] (P& p, const char* id) { p.v = apvts.getRawParameterValue (id); };
    f (pIn, "in"); f (pOut, "out"); f (pBass, "bass"); f (pThick, "thick"); f (pBody, "body"); f (pComp, "comp");
    f (pSc, "softclip"); f (pCrunch, "crunch"); f (pMix, "mix"); f (pLoud, "loud");
    bassOn = apvts.getRawParameterValue ("bassOn"); thickOn = apvts.getRawParameterValue ("thickOn"); bodyOn = apvts.getRawParameterValue ("bodyOn");
    compOn = apvts.getRawParameterValue ("compOn"); ottMode = apvts.getRawParameterValue ("ott"); scOn = apvts.getRawParameterValue ("softclipOn");
    crunchOn = apvts.getRawParameterValue ("crunchOn"); loudOn = apvts.getRawParameterValue ("loudOn"); unityOn = apvts.getRawParameterValue ("unity");
    liveOn = apvts.getRawParameterValue ("live"); osOn = apvts.getRawParameterValue ("oversample"); bypassOn = apvts.getRawParameterValue ("bypass");
    linkMatchOn = apvts.getRawParameterValue ("linkMatch");
    engine.setClipTable (reinterpret_cast<const float*> (BinaryData::clip2d_bin));
    apvts.addParameterListener ("live", this);
    apvts.addParameterListener ("oversample", this);
}

FireBoxProcessor::~FireBoxProcessor()
{
    apvts.removeParameterListener ("live", this);
    apvts.removeParameterListener ("oversample", this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout FireBoxProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using B = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto db = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<F> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.01f), def,
                                    juce::AudioParameterFloatAttributes().withLabel ("dB")
                                        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " dB"; }));
    };
    auto pc = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<F> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                    juce::AudioParameterFloatAttributes().withLabel ("%")
                                        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
    };
    auto sw = [] (const char* id, const char* name, bool def) { return std::make_unique<B> (juce::ParameterID { id, 1 }, name, def); };
    ps.push_back (db ("in", "In", -12.0f, 12.0f, 0.0f));
    ps.push_back (pc ("bass", "Bass", 15.0f));            ps.push_back (sw ("bassOn", "Bass On", true));
    ps.push_back (pc ("thick", "Thick", 13.0f));          ps.push_back (sw ("thickOn", "Thick On", true));
    ps.push_back (pc ("body", "Body", 0.0f));             ps.push_back (sw ("bodyOn", "Body On", true));
    ps.push_back (db ("comp", "Comp / OTT", 0.0f, 48.0f, 0.0f)); ps.push_back (sw ("compOn", "Comp On", true));
    ps.push_back (sw ("ott", "OTT Mode", false));
    ps.push_back (pc ("softclip", "Soft Clip", 0.0f));    ps.push_back (sw ("softclipOn", "Soft Clip On", true));
    ps.push_back (pc ("crunch", "Crunch", 0.0f));         ps.push_back (sw ("crunchOn", "Crunch On", true));
    ps.push_back (pc ("mix", "Mix", 100.0f));
    ps.push_back (db ("loud", "Loud", 0.0f, 24.0f, 0.0f)); ps.push_back (sw ("loudOn", "Loud On", true));
    ps.push_back (db ("out", "Out", -12.0f, 12.0f, -0.1f));
    ps.push_back (sw ("unity", "Unity", false));
    // LIVE / 192k は切り替えると作り直す（音が一瞬止まる）ので、オートメーションはできないようにする
    ps.push_back (std::make_unique<B> (juce::ParameterID { "live", 1 }, "Live Mode", false, juce::AudioParameterBoolAttributes().withAutomatable (false)));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "oversample", 1 }, "Oversample 192k", false, juce::AudioParameterBoolAttributes().withAutomatable (false)));
    ps.push_back (sw ("bypass", "Bypass", false));
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (sw ("linkMatch", "Link Match Level", true));
    return { ps.begin(), ps.end() };
}

bool FireBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void FireBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate; maxBlock = juce::jmax (1, samplesPerBlock);
    for (P* p : { &pIn, &pOut, &pBass, &pThick, &pBody, &pComp, &pSc, &pCrunch, &pMix, &pLoud })
    {
        p->s.reset (sampleRate, 0.03);
        p->s.setCurrentAndTargetValue (p->v->load());
    }
    inMeter.prepare (sampleRate); outMeter.prepare (sampleRate);
    rebuild();
}

void FireBoxProcessor::rebuild()
{
    builtLive = liveOn->load() > 0.5f; builtOs = osOn->load() > 0.5f;
    // 192k: 44.1/48k は 4 倍、88.2/96k は 2 倍、それ以上はそのまま
    osFactorLog2 = builtOs ? (hostRate < 60000 ? 2 : hostRate < 120000 ? 1 : 0) : 0;
    const int numCh = juce::jlimit (1, 2, getTotalNumOutputChannels());
    osChannels = numCh;
    os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) numCh, (size_t) osFactorLog2,
                                                           juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    os->initProcessing ((size_t) maxBlock);
    engine.prepare (hostRate * (double) (1 << osFactorLog2), builtLive, readSwitches());
    prepared = true;
    const double lat = (double) engine.getLatency() / (double) (1 << osFactorLog2) + (osFactorLog2 > 0 ? os->getLatencyInSamples() : 0.0);
    setLatencySamples ((int) std::lround (lat));
}

firebox::Params FireBoxProcessor::readSwitches() const
{
    firebox::Params prm;
    prm.bassOn = bassOn->load() > 0.5f; prm.thickOn = thickOn->load() > 0.5f; prm.bodyOn = bodyOn->load() > 0.5f;
    prm.compOn = compOn->load() > 0.5f; prm.ott = ottMode->load() > 0.5f; prm.scOn = scOn->load() > 0.5f;
    prm.crunchOn = crunchOn->load() > 0.5f; prm.loudOn = loudOn->load() > 0.5f; prm.unity = unityOn->load() > 0.5f;
    return prm;
}

void FireBoxProcessor::handleAsyncUpdate()
{
    if (! prepared) return;
    suspendProcessing (true);
    rebuild();
    suspendProcessing (false);
    rebuildPending = false;
}

void FireBoxProcessor::processChunk (float* const* chs, int numCh, int n, firebox::Params& prm, bool bypass)
{
    const int fac = 1 << osFactorLog2;
    auto setP = [&] (int adv)
    {
        for (P* p : { &pIn, &pOut, &pBass, &pThick, &pBody, &pComp, &pSc, &pCrunch, &pMix, &pLoud }) p->s.skip (adv);
        prm.inDb = pIn.s.getCurrentValue(); prm.outDb = pOut.s.getCurrentValue(); prm.bass = pBass.s.getCurrentValue() * 0.01;
        prm.thick = pThick.s.getCurrentValue() * 0.01; prm.body = pBody.s.getCurrentValue() * 0.01; prm.comp = pComp.s.getCurrentValue() / 48.0;
        prm.sc = pSc.s.getCurrentValue() * 0.01; prm.crunch = pCrunch.s.getCurrentValue() * 0.01; prm.mix = pMix.s.getCurrentValue() * 0.01;
        prm.loud = pLoud.s.getCurrentValue() / 24.0;
    };
    if (fac > 1)
    {
        juce::dsp::AudioBlock<float> block (chs, (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        float* u[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
        const int m = (int) up.getNumSamples();
        for (int s = 0; s < m; s += 32 * fac)
        {
            const int len = juce::jmin (32 * fac, m - s);
            setP (len / fac);
            engine.process (u[0] + s, u[1] != nullptr ? u[1] + s : nullptr, len, prm, bypass);
        }
        os->processSamplesDown (block);
    }
    else
    {
        for (int s = 0; s < n; s += 32)
        {
            const int len = juce::jmin (32, n - s);
            setP (len);
            engine.process (chs[0] + s, numCh > 1 ? chs[1] + s : nullptr, len, prm, bypass);
        }
    }
}

void FireBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    if (n <= 0 || numCh <= 0) return;

    if ((liveOn->load() > 0.5f) != builtLive || (osOn->load() > 0.5f) != builtOs || numCh != osChannels)
    {
        // 作り直しが済むまでは素通し（作り直しはメッセージスレッドで）
        if (! rebuildPending.exchange (true)) triggerAsyncUpdate();
        if (numCh != osChannels) return;
    }

    inMeter.process (buffer.getArrayOfReadPointers(), numCh, n);

    for (P* p : { &pIn, &pOut, &pBass, &pThick, &pBody, &pComp, &pSc, &pCrunch, &pMix, &pLoud }) p->s.setTargetValue (p->v->load());
    firebox::Params prm = readSwitches();
    const bool bypass = bypassOn->load() > 0.5f;

    // ホストが prepareToPlay より大きいブロックを渡してきても、オーバーサンプラーの大きさを超えないよう分けて処理する
    float* chs[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    for (int s = 0; s < n; s += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - s);
        float* c2[2] = { chs[0] + s, chs[1] != nullptr ? chs[1] + s : nullptr };
        processChunk (c2, numCh, len, prm, bypass);
    }

    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // Plugin Link: 出口を Gain Stage の TARGET にそろえる（MATCH が ON で、同じ LINK に Gain Stage がいるとき）
    if (! bypass)
        match.process (link, linkMatchOn->load() > 0.5f && unityOn->load() < 0.5f,   // GAIN MATCH（unity）が ON なら LEVEL MATCH は止める
                        buffer.getArrayOfWritePointers(), numCh, n, getSampleRate());

    outMeter.process (buffer.getArrayOfReadPointers(), numCh, n);
}

void FireBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void FireBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    // LIVE / 192k が変わっていたら作り直して、遅れの申告を早めに直す
    if (prepared && ((liveOn->load() > 0.5f) != builtLive || (osOn->load() > 0.5f) != builtOs)) triggerAsyncUpdate();
}

juce::AudioProcessorEditor* FireBoxProcessor::createEditor() { return new FireBoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FireBoxProcessor(); }
