#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using NRange = juce::NormalisableRange<float>;
    NRange skewed (float lo, float hi, float centre, float step = 0.0f)
    {
        NRange r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
}

juce::StringArray EchoDriftProcessor::styleNames()
{
    juce::StringArray s;
    for (int i = 0; i < echo::numProfiles(); ++i) s.add (echo::profiles()[i].name);
    return s;
}

const juce::StringArray& EchoDriftProcessor::noteNames()
{
    static const juce::StringArray n { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    return n;
}

const juce::StringArray& EchoDriftProcessor::rhythmNoteNames()
{
    static const juce::StringArray n { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    return n;
}

juce::AudioProcessorValueTreeState::ParameterLayout EchoDriftProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto add = [&ps] (const juce::String& id, const juce::String& name, NRange r, float def, const juce::String& unit = {})
    {
        ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                                   juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addB = [&ps] (const juce::String& id, const juce::String& name, bool def)
    { ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def)); };
    auto addC = [&ps] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    { ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def)); };

    const juce::StringArray syncModes { "Time", "Note", "Dotted", "Triplet" };
    addC ("mode", "Mode", { "Single", "Dual", "Ping-Pong", "Rhythm" }, 0);
    addC ("style", "Style", styleNames(), 1);
    add ("time1", "Echo 1 Time", skewed (1, 15000, 300, 0.1f), 250, "ms");
    addC ("sync1", "Echo 1 Sync", syncModes, 0);
    addC ("note1", "Echo 1 Note", noteNames(), 3);
    add ("time2", "Echo 2 Time", skewed (1, 15000, 300, 0.1f), 375, "ms");
    addC ("sync2", "Echo 2 Sync", syncModes, 0);
    addC ("note2", "Echo 2 Note", noteNames(), 3);
    add ("feedback", "Feedback", NRange (0, 125, 0.1f), 30, "%");
    add ("mix", "Mix", NRange (0, 100, 0.1f), 30, "%");
    add ("input", "Input", NRange (-24, 24, 0.1f), 0, "dB");
    add ("output", "Output", NRange (-24, 24, 0.1f), 0, "dB");
    add ("saturation", "Saturation", NRange (0, 24, 0.01f), 0, "dB");
    add ("lowCut", "Low Cut", NRange (0, 100, 0.1f), 0, "%");
    add ("highCut", "High Cut", NRange (0, 100, 0.1f), 0, "%");
    add ("groove", "Groove", NRange (-25, 25, 0.1f), 0, "%");
    add ("feel", "Feel", NRange (-25, 25, 0.1f), 0, "%");
    addB ("prime", "Prime Numbers", false);
    add ("wobble", "Wobble", NRange (0, 200, 0.1f), 100, "%");
    addC ("rSync", "Rhythm Sync", syncModes, 0);
    addC ("rNote", "Rhythm Note", rhythmNoteNames(), 3);
    add ("rTime", "Rhythm Time", skewed (1, 2000, 120, 0.1f), 125, "ms");
    ps.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "repeats", 1 }, "Repeats", 1, 16, 8));
    add ("decay", "Decay", NRange (0, 40, 0.1f), 12, "dB");
    // Plugin Link（LINK の番号・VOCAL DUCK）
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "linkDuck", 1 }, "Link Vocal Duck", NRange (0.0f, 12.0f, 0.1f), 4.0f,
                                                               juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    return { ps.begin(), ps.end() };
}

EchoDriftProcessor::EchoDriftProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EchoDrift", createLayout())
{
    for (auto& id : paramOrder()) { ptrs.push_back (apvts.getRawParameterValue (id)); jassert (ptrs.back() != nullptr); }
}

bool EchoDriftProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void EchoDriftProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    engine.set (readParams());
    engine.reset();
    setLatencySamples (0);
}

const juce::StringArray& EchoDriftProcessor::paramOrder()
{
    static const juce::StringArray ids { "mode", "style", "time1", "sync1", "note1", "time2", "sync2", "note2", "feedback", "mix", "input", "output",
                                         "saturation", "lowCut", "highCut", "groove", "feel", "prime", "wobble", "rSync", "rNote", "rTime", "repeats", "decay" };
    return ids;
}

echo::Params EchoDriftProcessor::readParams() const
{
    int i = 0;
    auto v = [&]() { return ptrs[(size_t) i++]->load(); };
    const double bpm = juce::jlimit (20.0, 400.0, hostBpm.load());
    const double beatMs = 60000.0 / bpm;
    // 音符 → ms（whole = 4 拍）。sync: 0 Time 1 Note 2 Dotted 3 Triplet
    auto noteMs = [&] (int sync, int idx, double firstWholeFraction)
    {
        double ms = 4.0 * beatMs * firstWholeFraction / std::pow (2.0, idx);
        if (sync == 2) ms *= 1.5;
        if (sync == 3) ms *= 2.0 / 3.0;
        return ms;
    };
    echo::Params q;
    q.mode = (int) v();
    q.style = (int) v();
    { const double t = v(); const int s = (int) v(); const int n = (int) v(); q.time1Ms = s == 0 ? t : noteMs (s, n, 1.0); q.sync1 = s != 0; }
    { const double t = v(); const int s = (int) v(); const int n = (int) v(); q.time2Ms = s == 0 ? t : noteMs (s, n, 1.0); q.sync2 = s != 0; }
    q.time1Ms = juce::jlimit (0.5, 15000.0, q.time1Ms);
    q.time2Ms = juce::jlimit (0.5, 15000.0, q.time2Ms);
    q.feedback = v() / 100.0f;
    q.mix = v() / 100.0f;
    q.inputDb = v(); q.outputDb = v();
    q.saturationDb = v();
    q.lowCut = v() / 100.0f; q.highCut = v() / 100.0f;
    q.groove = v() / 100.0f;
    q.feelMs = v() / 100.0 * beatMs;
    q.prime = v() > 0.5f;
    q.wobble = v() / 100.0f;
    { const int s = (int) v(); const int n = (int) v(); const double t = v(); q.rhythmMs = s == 0 ? t : noteMs (s, n, 0.5); q.syncR = s != 0; }
    q.rhythmMs = juce::jlimit (1.0, 4000.0, q.rhythmMs);
    q.repeats = (int) v();
    q.decayDb = v();
    q.loopBeatsMs = 2.0 * beatMs;
    return q;
}

void EchoDriftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) hostBpm.store (*b);

    const int inCh = getTotalNumInputChannels();
    if (inCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = std::max (2, inCh); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    levels[0].store (std::max (levels[0].load() * 0.9f, buffer.getMagnitude (0, 0, n)));
    levels[1].store (std::max (levels[1].load() * 0.9f, buffer.getMagnitude (1, 0, n)));

    engine.set (readParams());
    engine.setDuck (duck.next (link, apvts.getRawParameterValue ("linkDuck")->load(), n, getSampleRate()));
    for (int pos = 0; pos < n; pos += 512)
    {
        const int k = std::min (512, n - pos);
        engine.process (L + pos, R + pos, k);
    }
    levels[2].store (std::max (levels[2].load() * 0.9f, buffer.getMagnitude (0, 0, n)));
    levels[3].store (std::max (levels[3].load() * 0.9f, buffer.getMagnitude (1, 0, n)));
}

void EchoDriftProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

//==============================================================================
juce::StringArray EchoDriftProcessor::presetNames()
{
    return { "Default", "Vocal Slap", "Dotted Eighth Lead", "Dub Tape Spin", "Wide Ping-Pong", "Radio Ghost", "Chorus Doubler",
             "Warm Bucket", "Ambient Wash", "Gallop Rhythm", "Runaway Feedback", "Lo-Fi Cassette" };
}

void EchoDriftProcessor::applyPreset (int index)
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (r->getParameterID() != "linkGroup" && r->getParameterID() != "linkDuck") r->setValueNotifyingHost (r->getDefaultValue());
    struct KV { const char* id; float v; };
    auto set = [this] (std::initializer_list<KV> kv) { for (auto& x : kv) setParam (x.id, x.v); };
    // style の番号は Profiles.h の並び
    switch (index)
    {
        case 1: set ({ { "style", 1 }, { "time1", 95 }, { "feedback", 8 }, { "mix", 22 }, { "lowCut", 25 }, { "highCut", 30 } }); break;
        case 2: set ({ { "style", 0 }, { "sync1", 2 }, { "note1", 3 }, { "feedback", 38 }, { "mix", 25 }, { "lowCut", 20 } }); break;
        case 3: set ({ { "style", 3 }, { "sync1", 1 }, { "note1", 2 }, { "feedback", 72 }, { "mix", 35 }, { "saturation", 9 }, { "highCut", 35 } }); break;
        case 4: set ({ { "mode", 2 }, { "style", 15 }, { "sync1", 1 }, { "note1", 3 }, { "sync2", 1 }, { "note2", 2 }, { "feedback", 45 }, { "mix", 30 }, { "lowCut", 30 } }); break;
        case 5: set ({ { "style", 11 }, { "time1", 180 }, { "feedback", 40 }, { "mix", 30 } }); break;
        case 6: set ({ { "style", 19 }, { "time1", 18 }, { "feedback", 0 }, { "mix", 40 } }); break;
        case 7: set ({ { "style", 6 }, { "sync1", 1 }, { "note1", 3 }, { "feedback", 45 }, { "mix", 30 }, { "saturation", 6 } }); break;
        case 8: set ({ { "style", 28 }, { "mode", 1 }, { "time1", 420 }, { "time2", 610 }, { "feedback", 65 }, { "mix", 35 }, { "lowCut", 25 }, { "highCut", 25 } }); break;
        case 9: set ({ { "mode", 3 }, { "style", 2 }, { "rSync", 1 }, { "rNote", 3 }, { "repeats", 3 }, { "decay", 9 }, { "feedback", 30 }, { "mix", 30 } }); break;
        case 10: set ({ { "style", 4 }, { "sync1", 1 }, { "note1", 2 }, { "feedback", 108 }, { "mix", 40 }, { "saturation", 10 }, { "lowCut", 20 } }); break;
        case 11: set ({ { "style", 5 }, { "time1", 330 }, { "feedback", 40 }, { "mix", 30 }, { "saturation", 8 }, { "wobble", 130 } }); break;
        default: break;
    }
}

//==============================================================================
void EchoDriftProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void EchoDriftProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* EchoDriftProcessor::createEditor() { return new EchoDriftEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EchoDriftProcessor(); }
