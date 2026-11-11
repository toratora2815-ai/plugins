#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include <functional>

namespace rrp
{
    const char* moduleName (int m)
    {
        static const char* n[] = { "MASTER", "NOISE", "WOBBLE", "DISTORT", "DIGITAL", "SPACE", "LOSS", "ECHO" };
        return n[juce::jlimit (0, kNumModules - 1, m)];
    }

    static juce::StringArray fromList (const char* const* l, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add (l[i]); return a; }

    const std::vector<Def>& defs()
    {
        using K = Kind; using S = Show;
        static const std::vector<Def> d = [] {
            juce::StringArray noiseT, distT, spaceT;
            for (auto& t : rr::noiseTypes()) noiseT.add (t.name);
            for (int i = 0; i < rr::kNumDistTypes; ++i) distT.add (rr::distTypeName (i));
            for (auto& t : rr::spaceTypes()) spaceT.add (t.name);
            const auto wowN = fromList (rr::wowNotes(), rr::kNumWowNotes), flN = fromList (rr::flutterNotes(), rr::kNumFlutterNotes),
                       magN = fromList (rr::magNotes(), rr::kNumMagNotes), echoN = fromList (rr::echoNotes(), rr::kNumEchoNotes),
                       resN = fromList (rr::resyncNotes(), rr::kNumResyncNotes);
            const juce::StringArray onoff { "Off", "On" };
            return std::vector<Def> {
                // 初期値は本物を立ち上げたときと同じ
                { "magnitude",   "Magnitude",          K::flt, 1.0f,    S::pct, 0 },
                { "inGain",      "In Gain",            K::flt, 0.5f,    S::db36, 0 },
                { "inWidth",     "In Width",           K::flt, 0.3017f, S::width, 0 },
                { "outWidth",    "Out Width",          K::flt, 0.5f,    S::width, 0 },
                { "outGain",     "Out Gain",           K::flt, 0.5f,    S::db36, 0 },
                { "mix",         "Mix",                K::flt, 1.0f,    S::pct, 0 },
                { "varispeed",   "Varispeed",          K::choice, 0,    S::none, 0, { "Off", "1/2x (-1 oct)", "1/4x (-2 oct)", "1/8x (-3 oct)" } },
                { "resync",      "Varispeed Resync",   K::choice, 2,    S::none, 0, resN },
                { "varispeedMix","Varispeed Mix",      K::flt, 1.0f,    S::pct, 0 },
                { "eqOn",        "EQ",                 K::toggle, 1,    S::none, 0 },
                { "cutLo",       "Cut Lo",             K::flt, 0.1581f, S::hz, 0 },
                { "cutHi",       "Cut Hi",             K::flt, 0.7991f, S::hz, 0 },
                { "cutLoHard",   "Cut Lo Hard",        K::toggle, 0,    S::none, 0 },
                { "cutHiHard",   "Cut Hi Hard",        K::toggle, 0,    S::none, 0 },
                { "tone",        "Tone",               K::flt, 0.3545f, S::bipolar, 0 },
                { "toneMode",    "Tone Mode",          K::choice, 1,    S::none, 0, { "Tilt", "Mid", "Filter 1", "Filter 2" } },
                { "compress",    "Compress",           K::flt, 0.3987f, S::pct, 0 },

                { "noiseOn",     "Noise On",           K::toggle, 1,    S::none, 1 },
                { "noiseAmt",    "Noise",              K::flt, 0.1238f, S::pct, 1 },
                { "noiseType",   "Noise Type",         K::choice, 0,    S::none, 1, noiseT },
                { "noiseTone",   "Noise Tone",         K::flt, 0.688f,  S::bipolar, 1 },
                { "noisePrePost","Noise Pre<>Post",    K::flt, 1.0f,    S::pct, 1 },
                { "noiseFollow", "Noise Follow",       K::flt, 0.0977f, S::pct, 1 },
                { "noiseDuck",   "Noise Duck",         K::flt, 0.0f,    S::pct, 1 },
                { "noiseFlux",   "Noise Flux",         K::flt, 0.0f,    S::pct, 1 },
                { "noiseLo",     "Noise Cut Lo",       K::flt, 0.0f,    S::hz, 1 },
                { "noiseHi",     "Noise Cut Hi",       K::flt, 1.0f,    S::hz, 1 },

                { "wobOn",       "Wobble On",          K::toggle, 1,    S::none, 2 },
                { "wobAmt",      "Wobble",             K::flt, 0.4123f, S::pct, 2 },
                { "wowFlutter",  "Wow<>Flutter",       K::flt, 0.1042f, S::pct, 2 },
                { "wowRate",     "Wow Rate",           K::flt, 0.0805f, S::wowHz, 2 },
                { "wowSync",     "Wow Sync",           K::toggle, 0,    S::none, 2 },
                { "wowNote",     "Wow Rate (Sync)",    K::choice, 7,    S::none, 2, wowN },
                { "wowStereo",   "Wobble Stereo",      K::toggle, 0,    S::none, 2 },
                { "flutterRate", "Flutter Rate",       K::flt, 0.294f,  S::flutHz, 2 },
                { "flutterSync", "Flutter Sync",       K::toggle, 0,    S::none, 2 },
                { "flutterNote", "Flutter Rate (Sync)",K::choice, 6,    S::none, 2, flN },
                { "wobMorph",    "Wobble Shape",       K::flt, 0.7105f, S::pct, 2 },
                { "wobWarp",     "Wobble Warp",        K::flt, 0.6842f, S::pct, 2 },
                { "wobMix",      "Wobble Mix",         K::flt, 1.0f,    S::pct, 2 },
                { "wobFlux",     "Wobble Flux",        K::flt, 0.441f,  S::pct, 2 },
                { "wobLo",       "Wobble Filter Lo",   K::flt, 0.0f,    S::hz, 2 },
                { "wobHi",       "Wobble Filter Hi",   K::flt, 1.0f,    S::hz, 2 },

                { "distOn",      "Distort On",         K::toggle, 1,    S::none, 3 },
                { "distAmt",     "Distort",            K::flt, 0.25f,   S::pct, 3 },
                { "distType",    "Distort Type",       K::choice, 6,    S::none, 3, distT },
                { "distOverload","Distort Overload",   K::flt, 0.2902f, S::pct, 3 },
                { "distMix",     "Distort Mix",        K::flt, 1.0f,    S::pct, 3 },
                { "distFlux",    "Distort Flux",       K::flt, 0.0741f, S::pct, 3 },
                { "distLo",      "Distort Filter Lo",  K::flt, 0.0f,    S::hz, 3 },
                { "distHi",      "Distort Filter Hi",  K::flt, 1.0f,    S::hz, 3 },
                { "distCut",     "Distort Filter Mode",K::choice, 0,    S::none, 3, { "Focus", "Cut" } },

                { "digOn",       "Digital On",         K::toggle, 1,    S::none, 4 },
                { "digAmt",      "Digital",            K::flt, 0.0619f, S::pct, 4 },
                { "digRateBits", "Rate<>Bits",         K::flt, 0.1491f, S::pct, 4 },
                { "digSmooth",   "Digital Smooth",     K::flt, 0.1022f, S::pct, 4 },
                { "digSputter",  "Bits Sputter",       K::flt, 0.3028f, S::pct, 4 },
                { "digDrive",    "Digital Drive",      K::flt, 0.0f,    S::pct, 4 },
                { "digMix",      "Digital Mix",        K::flt, 1.0f,    S::pct, 4 },
                { "digFlux",     "Digital Flux",       K::flt, 0.0f,    S::pct, 4 },
                { "digLo",       "Digital Filter Lo",  K::flt, 0.0f,    S::hz, 4 },
                { "digHi",       "Digital Filter Hi",  K::flt, 1.0f,    S::hz, 4 },
                { "digCut",      "Digital Filter Mode",K::choice, 1,    S::none, 4, { "Focus", "Cut" } },

                { "spaceOn",     "Space On",           K::toggle, 1,    S::none, 5 },
                { "spaceAmt",    "Space",              K::flt, 0.1f,    S::pct, 5 },
                { "spaceType",   "Space Type",         K::choice, 5,    S::none, 5, spaceT },
                { "spaceDecay",  "Space Decay",        K::flt, 0.8f,    S::decay, 5 },
                { "spacePre",    "Space PreDelay",     K::flt, 0.0f,    S::pre, 5 },
                { "spacePan",    "Space Pan",          K::flt, 0.5f,    S::bipolar, 5 },
                { "spaceWidth",  "Space Width",        K::flt, 1.0f,    S::pct, 5 },
                { "spaceDuck",   "Space Duck",         K::flt, 0.0f,    S::pct, 5 },
                { "spaceFlux",   "Space Flux",         K::flt, 0.0f,    S::pct, 5 },
                { "spaceLo",     "Space Filter Lo",    K::flt, 0.0f,    S::hz, 5 },
                { "spaceHi",     "Space Filter Hi",    K::flt, 1.0f,    S::hz, 5 },

                { "lossOn",      "Loss On",            K::toggle, 1,    S::none, 6 },
                { "lossAmt",     "Loss",               K::flt, 0.15f,   S::pct, 6 },
                { "lossXfade",   "Magnetic<>Codec",    K::flt, 0.2813f, S::pct, 6 },
                { "lossDrop",    "Dropouts",           K::flt, 0.0f,    S::pct, 6 },
                { "wearStereo",  "Wear Stereo",        K::toggle, 1,    S::none, 6 },
                { "magRate",     "Wear Rate",          K::flt, 0.5f,    S::magHz, 6 },
                { "magSync",     "Wear Sync",          K::toggle, 0,    S::none, 6 },
                { "magNote",     "Wear Rate (Sync)",   K::choice, 3,    S::none, 6, magN },
                { "magMorph",    "Wear Shape",         K::flt, 0.0f,    S::pct, 6 },
                { "magWarp",     "Wear Warp",          K::flt, 0.5f,    S::pct, 6 },
                { "magVolFilter","Wear Volume<>Filter",K::flt, 0.0f,    S::pct, 6 },
                { "codecDropMode","Codec Dropouts",    K::choice, 0,    S::none, 6, { "Repeat", "FFT Freeze" } },
                { "codecStretch","Codec Stretch",      K::flt, 0.0f,    S::pct, 6 },
                { "codecBlur",   "Codec Blur",         K::flt, 0.0f,    S::pct, 6 },
                { "codecMix",    "Codec Mix",          K::flt, 1.0f,    S::pct, 6 },
                { "codecCut",    "Codec Filter Mode",  K::choice, 1,    S::none, 6, { "Focus", "Cut" } },
                { "lossFlux",    "Loss Flux",          K::flt, 0.1f,    S::pct, 6 },
                { "lossLo",      "Loss Filter Lo",     K::flt, 0.0f,    S::hz, 6 },
                { "lossHi",      "Loss Filter Hi",     K::flt, 1.0f,    S::hz, 6 },

                { "echoOn",      "Echo On",            K::toggle, 1,    S::none, 7 },
                { "echoAmt",     "Echo",               K::flt, 0.15f,   S::pct, 7 },
                { "echoSolo",    "Echo Solo",          K::toggle, 0,    S::none, 7 },
                { "echoClear",   "Echo Clear",         K::toggle, 0,    S::none, 7 },
                { "echoFb",      "Echo Feedback",      K::flt, 0.1697f, S::pct, 7 },
                { "echoSyncL",   "Echo Sync L",        K::toggle, 1,    S::none, 7 },
                { "echoNoteL",   "Echo Time L (Sync)", K::choice, 6,    S::none, 7, echoN },
                { "echoTimeL",   "Echo Time L",        K::flt, 0.5f,    S::echoMs, 7 },
                { "echoLink",    "Echo Link L/R",      K::toggle, 1,    S::none, 7 },
                { "echoSyncR",   "Echo Sync R",        K::toggle, 1,    S::none, 7 },
                { "echoNoteR",   "Echo Time R (Sync)", K::choice, 6,    S::none, 7, echoN },
                { "echoTimeR",   "Echo Time R",        K::flt, 0.5f,    S::echoMs, 7 },
                { "echoScrub",   "Echo Scrub",         K::flt, 0.5f,    S::scrub, 7 },
                { "echoWidth",   "Echo Width",         K::flt, 0.7487f, S::pct, 7 },
                { "echoPingPong","Echo Ping-Pong",     K::flt, 0.3093f, S::pct, 7 },
                { "echoDuck",    "Echo Duck",          K::flt, 0.4152f, S::pct, 7 },
                { "grainAmt",    "Grains",             K::flt, 0.0f,    S::pct, 7 },
                { "grainPitch",  "Grains Pitch",       K::flt, 0.5f,    S::pitch, 7 },
                { "grainRev",    "Grains Reverse",     K::toggle, 0,    S::none, 7 },
                { "grainInv",    "Grains Invert",      K::toggle, 0,    S::none, 7 },
                { "echoFlux",    "Echo Flux",          K::flt, 0.1042f, S::pct, 7 },
                { "echoLo",      "Echo Filter Lo",     K::flt, 0.2863f, S::hz, 7 },
                { "echoHi",      "Echo Filter Hi",     K::flt, 0.7479f, S::hz, 7 },
                { "echoWob",     "Echo Wobble",        K::flt, 0.0f,    S::pct, 7 },
                { "echoDist",    "Echo Distort",       K::flt, 0.2366f, S::pct, 7 },
                { "echoDig",     "Echo Digital",       K::flt, 0.0f,    S::pct, 7 },
                { "echoSpace",   "Echo Space",         K::flt, 0.1161f, S::pct, 7 },
                { "echoLoss",    "Echo Loss",          K::flt, 0.0f,    S::pct, 7 },
            };
        }();
        return d;
    }

    juce::String showValue (Show s, float v)
    {
        auto hzText = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (f, f < 100.0f ? 1 : 0) + " Hz"; };
        switch (s)
        {
            case Show::pct:     return juce::String (juce::roundToInt (v * 100.0f)) + " %";
            case Show::db36:    { const float d = (v - 0.5f) * 36.0f; return (d > 0.05f ? "+" : "") + juce::String (d, 1) + " dB"; }
            case Show::hz:      return hzText (rr::knobHz (v));
            case Show::wowHz:   return juce::String (rr::Wobble::wowHz (v), 2) + " Hz";
            case Show::flutHz:  return juce::String (rr::Wobble::flutterHz (v), 1) + " Hz";
            case Show::magHz:   return juce::String (0.5 * std::pow (60.0, (double) v), 1) + " Hz";
            case Show::echoMs:  { const float ms = rr::Echo::freeMs (v); return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms"; }
            case Show::bipolar: { const int p = juce::roundToInt ((v - 0.5f) * 200.0f); return p == 0 ? juce::String ("0") : (p > 0 ? "+" : "") + juce::String (p); }
            case Show::decay:   return "x" + juce::String (std::pow (2.0f, (v - 0.8f) * 3.0f), 2);
            case Show::pre:     return juce::String (juce::roundToInt (v * 250.0f)) + " ms";
            case Show::pitch:   { const float st = (v - 0.5f) * 24.0f; return (st > 0.05f ? "+" : "") + juce::String (st, 1) + " st"; }
            case Show::scrub:   return "x" + juce::String (std::pow (2.0f, v - 0.5f), 2);
            case Show::width:   return juce::String (juce::roundToInt (v * 200.0f)) + " %";
            default:            return juce::String (v, 2);
        }
    }

    // つまみ → Knobs の書き込み先
    using Setter = std::function<void (rr::Knobs&, float)>;
    static const std::map<juce::String, Setter>& setters()
    {
        static const std::map<juce::String, Setter> m {
#define F(n) { #n, [] (rr::Knobs& k, float v) { k.n = v; } },
#define I(n) { #n, [] (rr::Knobs& k, float v) { k.n = (int) std::lround (v); } },
#define B(n) { #n, [] (rr::Knobs& k, float v) { k.n = v > 0.5f; } },
            F(magnitude) F(inGain) F(inWidth) F(outWidth) F(outGain) F(mix) I(varispeed) I(resync) F(varispeedMix) B(eqOn) F(cutLo) F(cutHi) B(cutLoHard) B(cutHiHard) F(tone) I(toneMode) F(compress)
            B(noiseOn) F(noiseAmt) I(noiseType) F(noiseTone) F(noisePrePost) F(noiseFollow) F(noiseDuck) F(noiseFlux) F(noiseLo) F(noiseHi)
            B(wobOn) F(wobAmt) F(wowFlutter) F(wowRate) I(wowNote) B(wowSync) B(wowStereo) F(flutterRate) I(flutterNote) B(flutterSync) F(wobMorph) F(wobWarp) F(wobMix) F(wobFlux) F(wobLo) F(wobHi)
            B(distOn) F(distAmt) I(distType) F(distOverload) F(distMix) F(distFlux) F(distLo) F(distHi) B(distCut)
            B(digOn) F(digAmt) F(digRateBits) F(digSmooth) F(digSputter) F(digDrive) F(digMix) F(digFlux) F(digLo) F(digHi) B(digCut)
            B(spaceOn) F(spaceAmt) I(spaceType) F(spaceDecay) F(spacePan) F(spaceWidth) F(spacePre) F(spaceDuck) F(spaceFlux) F(spaceLo) F(spaceHi)
            B(lossOn) F(lossAmt) F(lossXfade) F(lossDrop) B(wearStereo) F(magRate) I(magNote) B(magSync) F(magMorph) F(magWarp) F(magVolFilter) I(codecDropMode) F(codecStretch) F(codecBlur) F(codecMix) B(codecCut) F(lossFlux) F(lossLo) F(lossHi)
            B(echoOn) F(echoAmt) B(echoSolo) B(echoClear) F(echoFb) F(echoTimeL) F(echoTimeR) I(echoNoteL) I(echoNoteR) B(echoSyncL) B(echoSyncR) B(echoLink) F(echoScrub) F(echoWidth) F(echoPingPong) F(echoDuck)
            F(grainAmt) F(grainPitch) B(grainRev) B(grainInv) F(echoFlux) F(echoLo) F(echoHi) F(echoWob) F(echoDist) F(echoDig) F(echoSpace) F(echoLoss)
#undef F
#undef I
#undef B
        };
        return m;
    }
}

static std::vector<rrp::Setter> gSetters;

RetroRetroProcessor::RetroRetroProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    if (gSetters.empty())
        for (auto& d : rrp::defs())
        {
            auto it = rrp::setters().find (d.id);
            jassert (it != rrp::setters().end());
            gSetters.push_back (it != rrp::setters().end() ? it->second : rrp::Setter ([] (rr::Knobs&, float) {}));
        }
    for (auto& d : rrp::defs()) raw.push_back (apvts.getRawParameterValue (d.id));
    linkDuckDb = apvts.getRawParameterValue ("linkDuck");
    myPresets.exclude ("linkDuck");
    myPresets.exclude ("echoClear");
}

juce::AudioProcessorValueTreeState::ParameterLayout RetroRetroProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    for (auto& d : rrp::defs())
    {
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind == rrp::Kind::flt)
        {
            const auto show = d.show;
            ps.push_back (std::make_unique<juce::AudioParameterFloat> (pid, d.name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), d.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([show] (float v, int) { return rrp::showValue (show, v); })));
        }
        else if (d.kind == rrp::Kind::choice)
            ps.push_back (std::make_unique<juce::AudioParameterChoice> (pid, d.name, d.choices, (int) d.def));
        else
            ps.push_back (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f));
    }
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "linkDuck", 1 }, "Link Vocal Duck", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
                   juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    return { ps.begin(), ps.end() };
}

bool RetroRetroProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in == out) return true;
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();
}

void RetroRetroProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nOut = juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latency());
    gainMatch.prepare (sampleRate, nOut, samplesPerBlock);
}

rr::Knobs RetroRetroProcessor::currentKnobs() const
{
    rr::Knobs k;
    for (size_t i = 0; i < raw.size(); ++i) gSetters[i] (k, raw[i]->load());
    return k;
}

void RetroRetroProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    if (numIn == 1 && numOut >= 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch) buffer.clear (ch, 0, n);

    {
        float pk = 0; for (int c = 0; c < juce::jmin (2, numOut); ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
        meterIn.store (juce::Decibels::gainToDecibels (pk, -100.0f));
    }

    rr::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.bpm = juce::jlimit (20.0, 400.0, *b);
            tr.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { tr.ppq = *q; tr.hasPos = true; }
        }
    auto k = currentKnobs();

    // Plugin Link: Fader Pilot が歌っている間、ノイズを linkDuck dB 下げる（歌の隙間でだけ「サーッ」が聞こえる）
    const float vd = duck.next (link, linkDuckDb->load(), n, getSampleRate());
    k.noiseLinkGain = vd;

    if (needReset.exchange (false)) engine.reset();
    // ECHO CLEAR は Engine が「押した瞬間」だけ消す。ボタンを戻すのはエディターのタイマー
    const int nCh = juce::jmin (numOut, rr::maxCh);
    engine.process (const_cast<float**> (buffer.getArrayOfWritePointers()), nCh, n, k, tr);

    {
        float pk = 0; for (int c = 0; c < nCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
        meterOut.store (juce::Decibels::gainToDecibels (pk, -100.0f));
    }
    // GAIN MATCH: 本体の MIX が 10 % より下（ほぼ元の音）なら合わせる必要がないので少しずつ外す。ECHO SOLO は比べる元がないので外す
    float gmAmount = juce::jlimit (0.0f, 1.0f, k.mix * 10.0f);
    if (k.echoOn && k.echoSolo) gmAmount = 0.0f;
    gainMatch.process (buffer, nCh, engine.latency(), gmAmount);
    // GAIN MATCH が持ち上げた後にもう一度頭打ち（0 dBFS を超えない）
    for (int c = 0; c < nCh; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)   // 0.98 までは何もしない（MIX 0 % やふだんの音は変えない）
            if (std::abs (d[i]) > 0.98f) d[i] = (d[i] > 0 ? 1.0f : -1.0f) * (0.98f + 0.02f * std::tanh ((std::abs (d[i]) - 0.98f) / 0.02f));
    }
}

void RetroRetroProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void RetroRetroProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

//==============================================================================
// 内蔵プリセット（使いどころ別。名前と数値は自分で決めた）
namespace
{
    struct FP { const char* name; const char* values; };
    const FP kFactory[] = {
        { "Init (first open)",          "" },
        { "All Off (start clean)",      "noiseAmt=0,wobAmt=0,distAmt=0,digAmt=0,spaceAmt=0,lossAmt=0,echoAmt=0,eqOn=0,compress=0,inWidth=0.5" },
        { "Dusty Keys",                 "noiseAmt=0.32,noiseType=5,noiseTone=0.45,wobAmt=0.42,wowFlutter=0.08,wowRate=0.12,distAmt=0.3,distType=6,digAmt=0,spaceAmt=0.12,spaceType=5,lossAmt=0.2,lossXfade=0,echoAmt=0,cutLo=0.2,cutHi=0.72,tone=0.4,compress=0.35" },
        { "Lo-Fi Beat Bus",             "noiseAmt=0.22,noiseType=4,wobAmt=0.25,distAmt=0.38,distType=7,digAmt=0.32,digRateBits=0.2,spaceAmt=0.05,lossAmt=0.15,echoAmt=0,cutLo=0.15,cutHi=0.76,tone=0.42,compress=0.55" },
        { "Cassette Demo",              "noiseAmt=0.28,noiseType=3,wobAmt=0.5,wowFlutter=0.3,flutterRate=0.35,distAmt=0.35,distType=6,digAmt=0,spaceAmt=0.06,lossAmt=0.3,lossXfade=0,magVolFilter=0.6,echoAmt=0,cutLo=0.18,cutHi=0.74,tone=0.45,compress=0.45" },
        { "Old Radio Vocal",            "noiseAmt=0.2,noiseType=8,wobAmt=0.15,distAmt=0.45,distType=8,digAmt=0.1,spaceAmt=0.1,spaceType=9,lossAmt=0.2,lossXfade=0.6,echoAmt=0,toneMode=2,tone=0.85,cutLo=0.3,cutHi=0.7,compress=0.6" },
        { "Telephone Line",             "noiseAmt=0.18,noiseType=9,wobAmt=0,distAmt=0.4,distType=4,digAmt=0.3,digRateBits=0.4,spaceAmt=0,lossAmt=0.4,lossXfade=1,echoAmt=0,toneMode=3,tone=0.9,compress=0.6" },
        { "Vinyl Sample",               "noiseAmt=0.35,noiseType=5,noiseFollow=0,wobAmt=0.3,wowFlutter=0.05,distAmt=0.22,distType=0,digAmt=0.15,digRateBits=0.1,spaceAmt=0.08,lossAmt=0.1,echoAmt=0,cutLo=0.22,cutHi=0.78,tone=0.42,compress=0.4" },
        { "Warped Tape Pad",            "noiseAmt=0.15,noiseType=0,wobAmt=0.75,wowFlutter=0.05,wowRate=0.2,wowStereo=1,distAmt=0.2,distType=6,spaceAmt=0.35,spaceType=4,lossAmt=0.35,lossXfade=0,magVolFilter=1,echoAmt=0.2,cutLo=0.15,cutHi=0.8,tone=0.45,compress=0.3" },
        { "Broken Stream",              "noiseAmt=0.05,wobAmt=0.1,distAmt=0.15,digAmt=0.45,digRateBits=0.7,digSputter=0.7,spaceAmt=0.05,lossAmt=0.7,lossXfade=1,lossDrop=0.4,codecStretch=0.4,echoAmt=0,eqOn=0,compress=0.3" },
        { "Dream Echo Throw",            "noiseAmt=0.1,wobAmt=0.3,distAmt=0.2,spaceAmt=0.2,spaceType=4,lossAmt=0.1,echoAmt=0.45,echoFb=0.55,echoNoteL=8,echoWob=0.4,echoSpace=0.5,echoDist=0.3,echoLo=0.33,echoHi=0.7,cutHi=0.85,compress=0.3" },
        { "Crunchy Drums",              "noiseAmt=0,wobAmt=0,distAmt=0.55,distType=2,digAmt=0.25,digRateBits=0.8,spaceAmt=0.06,spaceType=1,lossAmt=0,echoAmt=0,cutLo=0.12,cutHi=0.9,tone=0.55,compress=0.7" },
        { "Subtle Warmth",              "noiseAmt=0.06,noiseType=0,wobAmt=0.12,wowFlutter=0.1,distAmt=0.22,distType=1,digAmt=0,spaceAmt=0.04,lossAmt=0.05,echoAmt=0,cutLo=0.1,cutHi=0.88,tone=0.45,compress=0.25,inWidth=0.5" },
        { "Half-Speed Tape (varispeed)","noiseAmt=0.12,wobAmt=0.35,distAmt=0.25,spaceAmt=0.1,lossAmt=0.15,echoAmt=0,varispeed=1,resync=4,compress=0.35" },
    };
}

int RetroRetroProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String RetroRetroProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void RetroRetroProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    myPresets.snapshot();
    // まず全部を初期値に（GAIN MATCH・LINK の番号・VOCAL DUCK は触らない）
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == myp::GainMatch::paramId || id == "linkDuck" || id == "linkGroup") continue;
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
    for (auto& kv : juce::StringArray::fromTokens (f.values, ",", ""))
    {
        const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
        const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (auto* r = apvts.getParameter (id))
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->convertTo0to1 (v));
            r->endChangeGesture();
        }
    }
    myPresets.commitSinceSnapshot();
    apvts.state.setProperty ("mypPreset", juce::String ("[") + f.name + "]", nullptr);
    gainMatch.requestRelearn();
}

juce::AudioProcessorEditor* RetroRetroProcessor::createEditor()
{
    return new RetroRetroEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RetroRetroProcessor();
}
