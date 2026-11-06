#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pctSigned (float v, int) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; }
    juce::String pct (float v, int)       { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String ms (float v, int)        { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String db (float v, int)        { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
}

HitSculptorProcessor::HitSculptorProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout HitSculptorProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto add = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def, juce::String (*fmt) (float, int))
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (fmt)));
    };
    add ("attack", "Attack", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    juce::NormalisableRange<float> snapR (1.0f, 50.0f, 0.1f);
    snapR.setSkewForCentre (12.0f);
    add ("snap", "Snap", snapR, 15.0f, ms);
    add ("length", "Length", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    add ("room", "Room", { -100.0f, 100.0f, 1.0f }, 0.0f, pctSigned);
    add ("size", "Size", { 0.0f, 100.0f, 1.0f }, 40.0f, pct);
    add ("tone", "Tone", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    add ("drive", "Drive", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    add ("mix", "Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, pct);
    add ("output", "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, db);
    return { v.begin(), v.end() };
}

const std::vector<HitSculptorProcessor::Preset>& HitSculptorProcessor::presets()
{
    //                         ATTACK SNAP  LENGTH ROOM  SIZE TONE DRIVE MIX  OUT
    static const std::vector<Preset> p {
        { "Init",               0,   15,    0,    0,   40,  50,   0, 100,  0 },
        { "Tight Kick",        30,   12,  -55,  -40,   40,  50,   0, 100,  0 },
        { "Punchy Kick",       60,   15,  -20,    0,   40,  50,  15, 100, -1 },
        { "Long Boom",        -10,   20,   70,    0,   40,  50,  10, 100, -2 },
        { "Clipped Kick",      40,   15,  -10,  -30,   40,  50,  60, 100, -3 },
        { "Bone Dry",          10,   15,  -30, -100,   40,  50,   0, 100,  0 },
        { "Snappy Snare",      70,    8,  -30,  -20,   40,  50,   0, 100, -2 },
        { "Fat Snare",        -20,   25,   40,   25,   45,  55,  10, 100, -1 },
        { "Big Room Snare",    20,   15,   20,   70,   75,  60,   0, 100, -2 },
        { "Small Room",         0,   15,    0,   45,   20,  45,   0, 100, -1 },
        { "Soft Hit",         -60,   30,    0,    0,   40,  50,   0, 100,  2 },
        { "Dry & Short Hats",  30,    5,  -60,  -60,   40,  50,   0, 100,  0 },
    };
    return p;
}

void HitSculptorProcessor::applyPreset (int index)
{
    const auto& all = presets();
    if (index < 0 || index >= (int) all.size()) return;
    const auto& p = all[(size_t) index];
    const std::pair<const char*, float> vals[] = { { "attack", p.attack }, { "snap", p.snap }, { "length", p.length },
                                                   { "room", p.room }, { "size", p.size }, { "tone", p.tone },
                                                   { "drive", p.drive }, { "mix", p.mix }, { "output", p.output } };
    for (auto& [id, val] : vals)
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (val));
            prm->endChangeGesture();
        }
    apvts.state.setProperty ("preset", p.name, nullptr);
}

bool HitSculptorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

hs::Settings HitSculptorProcessor::readSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    hs::Settings s;
    s.attack = f ("attack") / 100.0f;
    s.snapMs = f ("snap");
    s.length = f ("length") / 100.0f;
    s.room   = f ("room") / 100.0f;
    s.size   = f ("size") / 100.0f;
    s.tone   = f ("tone") / 100.0f;
    s.mix    = f ("mix") / 100.0f;
    return s;
}

void HitSculptorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setSettings (readSettings());
    engine.prepare (sampleRate);

    // DRIVE は 4 倍オーバーサンプリング（折り返しノイズ対策）
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) std::round (oversampling->getLatencyInSamples()));

    driveAmt.reset (sampleRate * 4.0, 0.03);
    driveAmt.setCurrentAndTargetValue (apvts.getRawParameterValue ("drive")->load() / 100.0f);
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
}

void HitSculptorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), numCh, n);

    // DRIVE（tanh のソフトクリップ。小さい音は持ち上がり、ピークは丸まる）
    driveAmt.setTargetValue (apvts.getRawParameterValue ("drive")->load() / 100.0f);
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
    auto up = oversampling->processSamplesUp (block);
    const int un = (int) up.getNumSamples();
    for (int i = 0; i < un; ++i)
    {
        const float d = driveAmt.getNextValue();
        if (d <= 0.0001f) continue;
        const float g = juce::Decibels::decibelsToGain (d * 24.0f);
        const float comp = 1.0f / std::sqrt (g);
        const float blend = juce::jmin (1.0f, d * 4.0f);
        for (int c = 0; c < numCh; ++c)
        {
            float* p = up.getChannelPointer ((size_t) c);
            const float x = p[i];
            const float sat = std::tanh (x * g) * comp;
            p[i] = x + (sat - x) * blend;
        }
    }
    oversampling->processSamplesDown (block);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] *= g;
    }
}

void HitSculptorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HitSculptorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HitSculptorProcessor::createEditor() { return new HitSculptorEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HitSculptorProcessor(); }
