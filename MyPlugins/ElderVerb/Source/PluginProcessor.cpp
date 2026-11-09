#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using elder::Maps;

const juce::StringArray& ElderVerbProcessor::modeNames()
{
    static const juce::StringArray n { "Concert Hall", "Plate", "Room", "Chamber", "Random Space", "Chorus Space", "Ambience",
                                       "Bright Hall", "Sanctum", "Grainy Hall", "Grainy Plate", "Smooth Plate", "Smooth Room",
                                       "Smooth Space", "Gated", "Wild Chamber", "Wild Hall", "Wild Space", "Cathedral",
                                       "Grand Hall", "Vintage Chamber", "Vintage Hall" };
    return n;
}
const juce::StringArray& ElderVerbProcessor::colorNames()
{
    static const juce::StringArray n { "70s", "80s", "NOW" };
    return n;
}
const juce::StringArray& ElderVerbProcessor::knobIds()
{
    static const juce::StringArray n { "mix", "predelay", "decay", "size", "attack", "bassMult", "bassXover", "highShelf", "highFreq",
                                       "earlyDiff", "lateDiff", "modRate", "modDepth", "highCut", "lowCut" };
    return n;
}

namespace
{
juce::String hzText (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}
std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float def, std::function<juce::String (float)> text)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([text] (float v, int) { return text (v); }));
}
juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

// 本物の .vpreset の属性名 → このプラグインの ID
const std::pair<const char*, const char*> kVNames[] = {
    { "Mix", "mix" }, { "PreDelay", "predelay" }, { "Decay", "decay" }, { "Size", "size" }, { "Attack", "attack" },
    { "BassMult", "bassMult" }, { "BassXover", "bassXover" }, { "HighShelf", "highShelf" }, { "HighFreq", "highFreq" },
    { "EarlyDiffusion", "earlyDiff" }, { "LateDiffusion", "lateDiff" }, { "ModRate", "modRate" }, { "ModDepth", "modDepth" },
    { "HighCut", "highCut" }, { "LowCut", "lowCut" } };
}

APVTS::ParameterLayout ElderVerbProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    l.add (knob ("mix", "Mix", 1.0f, pct));
    l.add (knob ("predelay", "Pre-Delay", 0.0f, [] (float v) { return juce::String (Maps::predelayMs (v), 1) + " ms"; }));
    l.add (knob ("decay", "Decay", 0.42f, [] (float v) { const float s = Maps::decaySec (v); return juce::String (s, s < 10 ? 2 : 1) + " s"; }));
    l.add (knob ("size", "Size", 1.0f, pct));
    l.add (knob ("attack", "Attack", 0.5f, pct));
    l.add (knob ("bassMult", "Bass Mult", 0.623f, [] (float v) { return "x" + juce::String (Maps::bassMult (v), 2); }));
    l.add (knob ("bassXover", "Bass Xover", 0.381f, [] (float v) { return hzText (Maps::bassXoverHz (v)); }));
    l.add (knob ("highShelf", "High Shelf", 0.0f, [] (float v) { return juce::String (Maps::highShelfDb (v), 1) + " dB"; }));
    l.add (knob ("highFreq", "High Freq", 0.5f, [] (float v) { return hzText (Maps::highFreqHz (v)); }));
    l.add (knob ("earlyDiff", "Early Diffusion", 1.0f, pct));
    l.add (knob ("lateDiff", "Late Diffusion", 1.0f, pct));
    l.add (knob ("modRate", "Mod Rate", 0.245f, [] (float v) { return juce::String (Maps::modRateHz (v), 2) + " Hz"; }));
    l.add (knob ("modDepth", "Mod Depth", 0.38f, pct));
    l.add (knob ("highCut", "High Cut", 0.59f, [] (float v) { return v >= 0.999f ? juce::String ("Off") : hzText (Maps::highCutHz (v)); }));
    l.add (knob ("lowCut", "Low Cut", 0.0f, [] (float v) { return v <= 0.002f ? juce::String ("Off") : hzText (Maps::lowCutHz (v)); }));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Reverb Mode", modeNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "color", 1 }, "Color", colorNames(), 0));
    // Plugin Link
    l.add (myp::GainMatch::makeParameter());
    l.add (plink::makeGroupParameter (9));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "linkDuck", 9 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 4.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    return l;
}

ElderVerbProcessor::ElderVerbProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

bool ElderVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void ElderVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    wetBuf.setSize (2, samplesPerBlock);
    dryGain.reset (sampleRate, 0.05); wetGain.reset (sampleRate, 0.05);
    firstBlock = true; lastMode = -1;
    gainMatch.prepare (sampleRate, 2, samplesPerBlock);
}

elder::Params ElderVerbProcessor::currentParams() const
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    elder::Params p;
    p.mix = 1.0f;   // ドライとの混ぜ込みはこちらで（なめらかに）
    p.predelay = g ("predelay"); p.decay = g ("decay"); p.size = g ("size"); p.attack = g ("attack");
    p.bassMult = g ("bassMult"); p.bassXover = g ("bassXover"); p.highShelf = g ("highShelf"); p.highFreq = g ("highFreq");
    p.earlyDiff = g ("earlyDiff"); p.lateDiff = g ("lateDiff"); p.modRate = g ("modRate"); p.modDepth = g ("modDepth");
    p.highCut = g ("highCut"); p.lowCut = g ("lowCut");
    p.mode = juce::jlimit (0, elder::kNumModes - 1, (int) g ("mode"));
    p.color = juce::jlimit (0, 2, (int) g ("color"));
    return p;
}

void ElderVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    gainMatch.measureInput (buffer, 2);

    const auto p = currentParams();
    if (p.mode != lastMode)
    {   // モードを変えたら、前の響きの残りで大きな音が出ないように空にする
        if (lastMode >= 0) engine.reset();
        lastMode = p.mode;
    }
    if (firstBlock || std::memcmp (&p, &lastParams, sizeof (p)) != 0) { engine.setParams (p); lastParams = p; }

    const float m = apvts.getRawParameterValue ("mix")->load();
    const float duckG = duck.next (link, apvts.getRawParameterValue ("linkDuck")->load(), n, getSampleRate());
    dryGain.setTargetValue (std::cos (m * juce::MathConstants<float>::halfPi));
    wetGain.setTargetValue (std::sin (m * juce::MathConstants<float>::halfPi));
    if (firstBlock) { dryGain.setCurrentAndTargetValue (dryGain.getTargetValue()); wetGain.setCurrentAndTargetValue (wetGain.getTargetValue()); firstBlock = false; }

    if (wetBuf.getNumSamples() < n) wetBuf.setSize (2, n, false, false, true);
    engine.process (buffer.getReadPointer (0), buffer.getReadPointer (1), wetBuf.getWritePointer (0), wetBuf.getWritePointer (1), n);

    float pk[2] = { 0, 0 };
    auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
    const auto* wl = wetBuf.getReadPointer (0); const auto* wr = wetBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float d = dryGain.getNextValue(), w = wetGain.getNextValue() * duckG;
        L[i] = d * L[i] + w * wl[i];
        R[i] = d * R[i] + w * wr[i];
    }
    // GAIN MATCH（MIX 90〜100% は少しずつ弱め、100% = センド使いでは効かない）
    gainMatch.process (buffer, 2, getLatencySamples(), (1.0f - m) / 0.1f);
    for (int i = 0; i < n; ++i) { pk[0] = std::max (pk[0], std::abs (L[i])); pk[1] = std::max (pk[1], std::abs (R[i])); }
    for (int c = 0; c < 2; ++c) outLevel[c].store (std::max (pk[c], outLevel[c].load() * 0.85f));
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// ---------------------------------------------------------------- 状態とプリセット
void ElderVerbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presetName, nullptr);
    gainMatch.saveTo (state);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ElderVerbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
            presetName = apvts.state.getProperty ("presetName", "Init").toString();
        }
}

void ElderVerbProcessor::applyValues (const std::map<juce::String, float>& v, const juce::String& name)
{
    for (auto& [id, val] : v)
        if (auto* prm = apvts.getParameter (id))
        {
            float nv = val;
            if (id == "mode") nv = prm->convertTo0to1 ((float) juce::jlimit (0, elder::kNumModes - 1, (int) val));
            else if (id == "color") nv = prm->convertTo0to1 ((float) juce::jlimit (0, 2, (int) val));
            prm->beginChangeGesture(); prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, nv)); prm->endChangeGesture();
        }
    presetName = name;
    gainMatch.requestRelearn();   // GAIN MATCH: プリセットを読んだら測り直す
}

juce::File ElderVerbProcessor::vpresetFolder()
{
    return juce::File ("/Library/Application Support/Valhalla DSP, LLC/ValhallaVintageVerb/Presets");
}
juce::File ElderVerbProcessor::myPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MyPlugins/Elder Verb/Presets");
}

juce::String ElderVerbProcessor::importVPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return "読めませんでした";
    std::map<juce::String, float> v;
    for (auto& [src, dst] : kVNames)
        if (xml->hasAttribute (src)) v[dst] = (float) xml->getDoubleAttribute (src);
    if (xml->hasAttribute ("ReverbMode"))
    {   // 1/24 きざみ。1 = Concert Hall
        const int k = juce::roundToInt (xml->getDoubleAttribute ("ReverbMode") * 24.0);
        v["mode"] = (float) juce::jlimit (0, elder::kNumModes - 1, k - 1);
    }
    if (xml->hasAttribute ("ColorMode"))
    {
        const double c = xml->getDoubleAttribute ("ColorMode");
        v["color"] = c < 0.6666 ? 0.0f : (c < 0.9999 ? 1.0f : 2.0f);
    }
    if (v.empty()) return "中身がありませんでした";
    applyValues (v, xml->getStringAttribute ("presetName", f.getFileNameWithoutExtension()));
    return {};
}

juce::String ElderVerbProcessor::saveMyPreset (const juce::String& name)
{
    auto dir = myPresetFolder();
    if (! dir.createDirectory()) return "フォルダを作れませんでした";
    juce::XmlElement x ("ElderVerbPreset");
    x.setAttribute ("presetName", name);
    for (auto& id : knobIds()) x.setAttribute (id, (double) apvts.getRawParameterValue (id)->load());
    x.setAttribute ("mode", (int) apvts.getRawParameterValue ("mode")->load());
    x.setAttribute ("color", (int) apvts.getRawParameterValue ("color")->load());
    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".evpreset");
    if (! x.writeTo (file)) return "保存できませんでした";
    presetName = name;
    return {};
}

const std::vector<ElderVerbProcessor::Preset>& ElderVerbProcessor::builtinPresets()
{
    // 自作のプリセット（値は 0..1。mode は番号、color は 0=70s 1=80s 2=NOW）
    static const std::vector<Preset> list = [] {
        auto P = [] (const char* name, int mode, int color, std::initializer_list<std::pair<const char*, float>> kv) {
            Preset p { name, {} };
            p.v = { { "mix", 1.0f }, { "predelay", 0.0f }, { "decay", 0.42f }, { "size", 1.0f }, { "attack", 0.5f }, { "bassMult", 0.623f },
                    { "bassXover", 0.381f }, { "highShelf", 0.0f }, { "highFreq", 0.5f }, { "earlyDiff", 1.0f }, { "lateDiff", 1.0f },
                    { "modRate", 0.245f }, { "modDepth", 0.38f }, { "highCut", 0.59f }, { "lowCut", 0.0f } };
            for (auto& [k, val] : kv) p.v[k] = val;
            p.v["mode"] = (float) mode; p.v["color"] = (float) color;
            return p;
        };
        return std::vector<Preset> {
            P ("Init", 0, 0, {}),
            P ("Warm Vocal Hall", 0, 1, { { "mix", 0.3f }, { "predelay", 0.28f }, { "decay", 0.36f }, { "size", 0.75f }, { "highCut", 0.5f }, { "lowCut", 0.12f } }),
            P ("Ballad Plate", 1, 2, { { "mix", 0.3f }, { "predelay", 0.2f }, { "decay", 0.33f }, { "size", 0.6f }, { "highShelf", 0.6f }, { "lowCut", 0.15f } }),
            P ("Snare Room", 2, 1, { { "mix", 0.25f }, { "decay", 0.22f }, { "size", 0.4f }, { "attack", 0.3f }, { "highCut", 0.7f } }),
            P ("Wood Chamber", 3, 0, { { "mix", 0.3f }, { "decay", 0.3f }, { "size", 0.55f }, { "highCut", 0.45f } }),
            P ("Wide Pad Space", 5, 2, { { "mix", 0.45f }, { "decay", 0.55f }, { "modDepth", 0.7f }, { "modRate", 0.35f }, { "attack", 0.7f } }),
            P ("Tight Ambience", 6, 2, { { "mix", 0.2f }, { "decay", 0.15f }, { "size", 0.3f }, { "highCut", 0.75f } }),
            P ("Air Hall", 7, 2, { { "mix", 0.3f }, { "predelay", 0.3f }, { "decay", 0.4f }, { "highShelf", 0.85f }, { "highCut", 0.85f }, { "lowCut", 0.2f } }),
            P ("Old Steel", 8, 0, { { "mix", 0.3f }, { "decay", 0.3f }, { "size", 0.45f }, { "attack", 0.2f } }),
            P ("Gated Drums", 14, 2, { { "mix", 0.35f }, { "size", 0.3f }, { "attack", 0.5f }, { "highCut", 0.7f } }),
            P ("Dark Cathedral", 18, 0, { { "mix", 0.4f }, { "decay", 0.55f }, { "highCut", 0.35f }, { "bassMult", 0.7f } }),
            P ("Endless Hall", 19, 2, { { "mix", 0.5f }, { "decay", 0.75f }, { "modDepth", 0.5f }, { "lowCut", 0.15f } }),
            P ("Studio Chamber 70s", 20, 0, { { "mix", 0.3f }, { "decay", 0.32f }, { "size", 0.6f } }),
            P ("Lush 80s Hall", 21, 1, { { "mix", 0.35f }, { "predelay", 0.25f }, { "decay", 0.42f }, { "attack", 0.55f }, { "modDepth", 0.55f } }),
        };
    }();
    return list;
}

juce::AudioProcessorEditor* ElderVerbProcessor::createEditor() { return new ElderVerbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ElderVerbProcessor(); }
