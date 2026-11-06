#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <unordered_map>

namespace
{
    juce::String db (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String ms (float v, int) { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String ratio (float v, int) { return v < 1.0f ? juce::String (v, 2) + ":1" : juce::String (v, 1) + ":1"; }
    juce::String num (float v, int) { return juce::String (v, 2); }
    juce::String deg (float v, int) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("°"); }
    juce::String sec (float v, int) { return juce::String (v, 2) + " s"; }
    juce::String one (float v, int) { return juce::String (v, 1); }

    juce::NormalisableRange<float> logR (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float, float, float v) { return v; } };
    }
    juce::NormalisableRange<float> skewR (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r;
    }

}

ForgeStripProcessor::ForgeStripProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < fs::numModules; ++i) order[i] = i;
    token = (float) (1 + juce::Random::getSystemRandom().nextInt (8000000));
    startTimerHz (30);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            params[r->getParameterID().toStdString()] = apvts.getRawParameterValue (r->getParameterID());
}

juce::AudioProcessorValueTreeState::ParameterLayout ForgeStripProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto addF = [&] (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                     juce::String (*text) (float, int))
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (text)));
    };
    auto addB = [&] (const juce::String& id, const juce::String& name, bool def) { v.push_back (std::make_unique<B> (juce::ParameterID { id, 1 }, name, def)); };
    auto addC = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& ch, int def)
    { v.push_back (std::make_unique<C> (juce::ParameterID { id, 1 }, name, ch, def)); };

    // 全体
    addB ("bypass", "Bypass", false);
    addF ("inGain", "Input Gain", { -60.0f, 10.0f, 0.01f }, 0.0f, db);
    addF ("outGain", "Output Gain", { -60.0f, 10.0f, 0.01f }, 0.0f, db);
    addF ("pan", "Pan", { -1.0f, 1.0f, 0.001f }, 0.0f, num);
    addF ("width", "Width", { -100.0f, 100.0f, 0.1f }, 0.0f, pct);
    addB ("mono", "Sum to Mono", false);
    addB ("swap", "Swap Channels", false);
    addB ("invert", "Invert Phase", false);
    addF ("delayL", "Delay L", { 0.0f, 100.0f, 0.01f }, 0.0f, ms);
    addF ("delayR", "Delay R", { 0.0f, 100.0f, 0.01f }, 0.0f, ms);
    addB ("lim_on", "Limiter", false);
    addC ("lim_mode", "Limiter Mode", { "Soft", "Smart", "Hard", "Zero Latency" }, 1);
    addF ("lim_ceiling", "Limiter Ceiling", { -20.0f, 0.0f, 0.01f }, -0.3f, db);
    addC ("lim_style", "Limiter Style", { "Clear", "Thick", "Smooth" }, 1);
    v.push_back (plink::makeGroupParameter (1));
    addB ("eq_softsat", "EQ Soft Saturation", false);

    for (int m = 0; m < fs::numModules; ++m)
    {
        const juce::String nm = fs::moduleName (m);
        addB (fsid::J (fsid::id (m, "on")), nm + " On", m == fs::ModEq || m == fs::ModComp1);
        addF (fsid::J (fsid::id (m, "mix")), nm + " Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
        if (fsid::multiband (m))
        {
            addC (fsid::J (fsid::id (m, "bands")), nm + " Bands", { "1", "2", "3" }, 0);
            addF (fsid::J (fsid::id (m, "x1")), nm + " Crossover 1", logR (40.0f, 4000.0f), 200.0f, hz);
            addF (fsid::J (fsid::id (m, "x2")), nm + " Crossover 2", logR (400.0f, 16000.0f), 6000.0f, hz);
        }
    }

    // EQ
    static const float eqFreqs[12] = { 100, 500, 3000, 11000, 600, 1200, 2000, 3000, 5000, 7000, 11000, 16000 };
    static const int eqShapes[12] = { fs::LowShelfAnalog, fs::PropBell, fs::PropBell, fs::HighShelfAnalog, 0, 0, 0, 0, 0, 0, 0, 0 };
    juce::StringArray shapes; for (int s = 0; s < fs::numEqShapes; ++s) shapes.add (fs::eqShapeName (s));
    for (int b = 0; b < 12; ++b)
    {
        const juce::String n = "EQ " + juce::String (b + 1) + " ";
        addB (fsid::J (fsid::eqId (b, "on")), n + "On", false);
        addC (fsid::J (fsid::eqId (b, "shape")), n + "Shape", shapes, eqShapes[b]);
        addF (fsid::J (fsid::eqId (b, "freq")), n + "Frequency", logR (20.0f, 20000.0f), eqFreqs[b], hz);
        addF (fsid::J (fsid::eqId (b, "gain")), n + "Gain", { -30.0f, 15.0f, 0.01f }, 0.0f, db);
        addF (fsid::J (fsid::eqId (b, "q")), n + "Q", logR (0.1f, 40.0f), (b == 0 || b == 3) ? 1.0f : 2.0f, num);
        addB (fsid::J (fsid::eqId (b, "dyn")), n + "Dynamic", false);
        addC (fsid::J (fsid::eqId (b, "dir")), n + "Dynamic Mode", { "Down", "Up" }, 0);
        addF (fsid::J (fsid::eqId (b, "thr")), n + "Threshold", { -80.0f, 0.0f, 0.1f }, -40.0f, db);
    }

    // コンプ 2 台
    for (int m : { (int) fs::ModComp1, (int) fs::ModComp2 })
    {
        const juce::String nm = fs::moduleName (m);
        addC (fsid::J (fsid::id (m, "detect")), nm + " Detection", { "Peak", "RMS", "True Peak" }, 0);
        addB (fsid::J (fsid::id (m, "autorel")), nm + " Auto Release", false);
        addB (fsid::J (fsid::id (m, "autogain")), nm + " Auto Gain", true);
        addC (fsid::J (fsid::id (m, "cmode")), nm + " Mode", { "Modern", "Vintage", "Punch" }, m == fs::ModComp2 ? 1 : 0);
        addF (fsid::J (fsid::id (m, "gain")), nm + " Global Gain", { -30.0f, 30.0f, 0.01f }, 0.0f, db);
        addF (fsid::J (fsid::id (m, "hpf")), nm + " SC Highpass", logR (20.0f, 20000.0f), 20.0f, hz);
        addF (fsid::J (fsid::id (m, "lpf")), nm + " SC Lowpass", logR (20.0f, 20000.0f), 20000.0f, hz);
        addB (fsid::J (fsid::id (m, "ext")), nm + " External Sidechain", false);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = nm + " B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "thr")), n + "Threshold", { -80.0f, 0.0f, 0.1f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "ratio")), n + "Ratio", skewR (0.1f, 30.0f, 4.0f), 4.0f, ratio);
            addF (fsid::J (fsid::bandId (m, b, "att")), n + "Attack", logR (0.1f, 500.0f), 10.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "rel")), n + "Release", logR (0.1f, 5000.0f), 50.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "knee")), n + "Knee", { 0.0f, 50.0f, 0.1f }, 0.0f, one);
            addF (fsid::J (fsid::bandId (m, b, "gain")), n + "Gain", { -30.0f, 30.0f, 0.01f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "mix")), n + "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
            addF (fsid::J (fsid::bandId (m, b, "pamt")), n + "Punch Amount", { 10.0f, 300.0f, 0.1f }, 100.0f, pct);
            addF (fsid::J (fsid::bandId (m, b, "patt")), n + "Punch Attack", logR (1.0f, 500.0f), 1.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "psus")), n + "Punch Sustain", logR (1.0f, 1000.0f), 150.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "pgain")), n + "Punch Gain", { -30.0f, 30.0f, 0.01f }, 0.0f, db);
        }
    }

    // エキサイター
    {
        const int m = fs::ModExciter;
        addF (fsid::J (fsid::id (m, "postf")), "Exciter Post Filter Freq", logR (1000.0f, 20000.0f), 14000.0f, hz);
        addF (fsid::J (fsid::id (m, "postg")), "Exciter Post Filter Gain", { -12.0f, 0.0f, 0.01f }, 0.0f, db);
        addC (fsid::J (fsid::id (m, "fmode")), "Exciter Filter Mode", { "Full", "Defined", "Clear", "Flat" }, 3);
        addB (fsid::J (fsid::id (m, "tame")), "Exciter Tame", false);
        addF (fsid::J (fsid::id (m, "tone")), "Exciter Tone", { -100.0f, 100.0f, 0.1f }, 0.0f, one);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = "Exciter B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "x")), n + "X", { 0.0f, 1.0f, 0.001f }, 0.5f, num);
            addF (fsid::J (fsid::bandId (m, b, "y")), n + "Y", { 0.0f, 1.0f, 0.001f }, 0.5f, num);
            addF (fsid::J (fsid::bandId (m, b, "drive")), n + "Drive", { 0.0f, 15.0f, 0.01f }, 0.0f, one);
            addF (fsid::J (fsid::bandId (m, b, "blend")), n + "Blend", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
            addB (fsid::J (fsid::bandId (m, b, "trash")), n + "Trash", false);
        }
    }
    // トランジェント
    {
        const int m = fs::ModTransient;
        addC (fsid::J (fsid::id (m, "mode")), "Transient Mode", { "Precise", "Balanced", "Loose" }, 1);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = "Transient B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "att")), n + "Attack", { -15.0f, 15.0f, 0.01f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "sus")), n + "Sustain", { -15.0f, 15.0f, 0.01f }, 0.0f, db);
            addC (fsid::J (fsid::bandId (m, b, "contour")), n + "Contour", { "Sharp", "Normal", "Smooth" }, 0);
        }
    }
    // ゲート
    {
        const int m = fs::ModGate;
        addF (fsid::J (fsid::id (m, "hpf")), "Gate SC Highpass", logR (20.0f, 20000.0f), 20.0f, hz);
        addF (fsid::J (fsid::id (m, "lpf")), "Gate SC Lowpass", logR (20.0f, 20000.0f), 20000.0f, hz);
        addB (fsid::J (fsid::id (m, "ext")), "Gate External Sidechain", false);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = "Gate B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "att")), n + "Attack", skewR (0.0f, 500.0f, 20.0f), 1.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "ratio")), n + "Ratio", skewR (1.0f, 30.0f, 6.0f), 8.0f, ratio);
            addF (fsid::J (fsid::bandId (m, b, "rel")), n + "Release", skewR (0.0f, 5000.0f, 200.0f), 100.0f, ms);
            addF (fsid::J (fsid::bandId (m, b, "open")), n + "Threshold", { -130.0f, 0.0f, 0.1f }, -80.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "close")), n + "Hysteresis", { 0.0f, 30.0f, 0.1f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "hold")), n + "Hold", { 0.0f, 1.0f, 0.001f }, 0.0f, sec);
        }
    }
    // Density
    {
        const int m = fs::ModDensity;
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = "Density B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "range")), n + "Range", { 0.0f, 20.0f, 0.01f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "ratio")), n + "Ratio", skewR (1.1f, 30.0f, 4.0f), 4.0f, ratio);
            addF (fsid::J (fsid::bandId (m, b, "thr")), n + "Threshold", { -80.0f, 0.0f, 0.1f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "speed")), n + "Speed", logR (1.0f, 300.0f), 120.0f, ms);
        }
    }
    // クリッパー
    {
        const int m = fs::ModClipper;
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n = "Clipper B" + juce::String (b + 1) + " ";
            addB (fsid::J (fsid::bandId (m, b, "byp")), n + "Bypass", false);
            addF (fsid::J (fsid::bandId (m, b, "thr")), n + "Threshold", { -80.0f, 0.0f, 0.1f }, 0.0f, db);
            addF (fsid::J (fsid::bandId (m, b, "boost")), n + "Softclip Boost", { 0.0f, 20.0f, 0.01f }, 0.0f, db);
        }
    }
    // 位相
    addF (fsid::J (fsid::id (fs::ModPhase, "rot")), "Phase Rotation", { -180.0f, 180.0f, 0.1f }, 0.0f, deg);
    addF (fsid::J (fsid::id (fs::ModPhase, "delay")), "Phase Delay", { 0.0f, 75.0f, 0.01f }, 0.0f, ms);
    // Sculptor
    {
        juce::StringArray t; for (int i = 0; i < fs::kNumToneTargets; ++i) t.add (fs::toneTargetName (i));
        t.add ("Add Punch"); t.add ("Add Polish"); t.add ("None");
        const int m = fs::ModSculptor;
        addC (fsid::J (fsid::id (m, "target")), "Sculptor Target", t, 22);
        addF (fsid::J (fsid::id (m, "amount")), "Sculptor Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
        addF (fsid::J (fsid::id (m, "tone")), "Sculptor Tone", { -50.0f, 50.0f, 0.1f }, 0.0f, one);
        addF (fsid::J (fsid::id (m, "speed")), "Sculptor Speed", { 0.0f, 100.0f, 0.1f }, 50.0f, one);
        addF (fsid::J (fsid::id (m, "lo")), "Sculptor Low Freq", logR (20.0f, 20000.0f), 20.0f, hz);
        addF (fsid::J (fsid::id (m, "hi")), "Sculptor High Freq", logR (20.0f, 20000.0f), 20000.0f, hz);
    }
    // Unmask
    {
        const int m = fs::ModUnmask;
        addF (fsid::J (fsid::id (m, "amount")), "Unmask Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
        addF (fsid::J (fsid::id (m, "sens")), "Unmask Sensitivity", { 0.0f, 100.0f, 0.1f }, 50.0f, one);
        addF (fsid::J (fsid::id (m, "att")), "Unmask Attack", logR (5.0f, 200.0f), 16.0f, ms);
        addF (fsid::J (fsid::id (m, "rel")), "Unmask Release", skewR (0.0f, 500.0f, 100.0f), 100.0f, ms);
        addF (fsid::J (fsid::id (m, "lo")), "Unmask Low Freq", logR (20.0f, 20000.0f), 20.0f, hz);
        addF (fsid::J (fsid::id (m, "hi")), "Unmask High Freq", logR (20.0f, 20000.0f), 20000.0f, hz);
    }
    return { v.begin(), v.end() };
}

float ForgeStripProcessor::p (const std::string& id) const
{
    auto it = params.find (id);
    jassert (it != params.end());
    return it != params.end() ? it->second->load() : 0.0f;
}

void ForgeStripProcessor::getOrder (int* o) const
{
    const juce::SpinLock::ScopedLockType l (orderLock);
    for (int i = 0; i < fs::numModules; ++i) o[i] = order[i];
}

void ForgeStripProcessor::setOrder (const int* o)
{
    {
        const juce::SpinLock::ScopedLockType l (orderLock);
        for (int i = 0; i < fs::numModules; ++i) order[i] = o[i];
    }
    apvts.state.setProperty ("order", juce::String (fs::orderToString (o)), nullptr);
}

fs::StripSettings ForgeStripProcessor::readSettings() const
{
    fs::StripSettings s;
    auto& g = s.global;
    g.bypass = p ("bypass") > 0.5f; g.inGain = p ("inGain"); g.outGain = p ("outGain"); g.pan = p ("pan"); g.width = p ("width");
    g.mono = p ("mono") > 0.5f; g.swap = p ("swap") > 0.5f; g.invert = p ("invert") > 0.5f; g.delayL = p ("delayL"); g.delayR = p ("delayR");
    s.limiterOn = p ("lim_on") > 0.5f;
    s.limiter.mode = (int) p ("lim_mode"); s.limiter.ceiling = p ("lim_ceiling"); s.limiter.style = (int) p ("lim_style");
    getOrder (s.order);
    for (int m = 0; m < fs::numModules; ++m) { s.on[m] = p (fsid::id (m, "on")) > 0.5f; s.mix[m] = p (fsid::id (m, "mix")) * 0.01f; }

    auto bandsOf = [&] (int m, int& nb, float& x1, float& x2) { nb = (int) p (fsid::id (m, "bands")) + 1; x1 = p (fsid::id (m, "x1")); x2 = p (fsid::id (m, "x2")); };

    for (int b = 0; b < 12; ++b)
    {
        auto& e = s.eq.band[b];
        e.on = p (fsid::eqId (b, "on")) > 0.5f; e.shape = (int) p (fsid::eqId (b, "shape")); e.freq = p (fsid::eqId (b, "freq"));
        e.gain = p (fsid::eqId (b, "gain")); e.q = p (fsid::eqId (b, "q")); e.dynamic = p (fsid::eqId (b, "dyn")) > 0.5f;
        e.up = p (fsid::eqId (b, "dir")) > 0.5f; e.threshold = p (fsid::eqId (b, "thr"));
    }
    s.eq.softSat = p ("eq_softsat") > 0.5f;
    for (int k = 0; k < 2; ++k)
    {
        const int m = k == 0 ? fs::ModComp1 : fs::ModComp2;
        auto& c = s.comp[k];
        bandsOf (m, c.bands, c.xover1, c.xover2);
        c.detect = (int) p (fsid::id (m, "detect")); c.autoRelease = p (fsid::id (m, "autorel")) > 0.5f;
        c.autoGain = p (fsid::id (m, "autogain")) > 0.5f;
        { const int cm = (int) p (fsid::id (m, "cmode")); c.vintage = cm == 1; c.punch = cm == 2; }
        c.globalGain = p (fsid::id (m, "gain")); c.scHpf = p (fsid::id (m, "hpf")); c.scLpf = p (fsid::id (m, "lpf"));
        c.scExternal = p (fsid::id (m, "ext")) > 0.5f;
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = c.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.threshold = p (fsid::bandId (m, b, "thr")); bb.ratio = p (fsid::bandId (m, b, "ratio"));
            bb.attack = p (fsid::bandId (m, b, "att")); bb.release = p (fsid::bandId (m, b, "rel")); bb.knee = p (fsid::bandId (m, b, "knee"));
            bb.gain = p (fsid::bandId (m, b, "gain")); bb.mix = p (fsid::bandId (m, b, "mix")) * 0.01f;
            bb.punchAmount = p (fsid::bandId (m, b, "pamt")); bb.punchAttack = p (fsid::bandId (m, b, "patt"));
            bb.punchSustain = p (fsid::bandId (m, b, "psus")); bb.punchGain = p (fsid::bandId (m, b, "pgain"));
        }
    }
    {
        const int m = fs::ModExciter; auto& e = s.exciter;
        bandsOf (m, e.bands, e.xover1, e.xover2);
        e.postFreq = p (fsid::id (m, "postf")); e.postGain = p (fsid::id (m, "postg")); e.filterMode = (int) p (fsid::id (m, "fmode"));
        e.tame = p (fsid::id (m, "tame")) > 0.5f; e.tone = p (fsid::id (m, "tone"));
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = e.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.x = p (fsid::bandId (m, b, "x")); bb.y = p (fsid::bandId (m, b, "y"));
            bb.drive = p (fsid::bandId (m, b, "drive")); bb.blend = p (fsid::bandId (m, b, "blend")) * 0.01f; bb.trash = p (fsid::bandId (m, b, "trash")) > 0.5f;
        }
    }
    {
        const int m = fs::ModTransient; auto& t = s.transient;
        bandsOf (m, t.bands, t.xover1, t.xover2); t.mode = (int) p (fsid::id (m, "mode"));
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = t.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.attack = p (fsid::bandId (m, b, "att")); bb.sustain = p (fsid::bandId (m, b, "sus"));
            bb.contour = (int) p (fsid::bandId (m, b, "contour"));
        }
    }
    {
        const int m = fs::ModGate; auto& gt = s.gate;
        bandsOf (m, gt.bands, gt.xover1, gt.xover2);
        gt.scHpf = p (fsid::id (m, "hpf")); gt.scLpf = p (fsid::id (m, "lpf")); gt.scExternal = p (fsid::id (m, "ext")) > 0.5f;
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = gt.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.attack = p (fsid::bandId (m, b, "att")); bb.ratio = p (fsid::bandId (m, b, "ratio"));
            bb.release = p (fsid::bandId (m, b, "rel")); bb.open = p (fsid::bandId (m, b, "open")); bb.close = p (fsid::bandId (m, b, "close"));
            bb.hold = p (fsid::bandId (m, b, "hold"));
        }
    }
    {
        const int m = fs::ModDensity; auto& d = s.density;
        bandsOf (m, d.bands, d.xover1, d.xover2);
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = d.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.range = p (fsid::bandId (m, b, "range")); bb.ratio = p (fsid::bandId (m, b, "ratio"));
            bb.threshold = p (fsid::bandId (m, b, "thr")); bb.speed = p (fsid::bandId (m, b, "speed"));
        }
    }
    {
        const int m = fs::ModClipper; auto& c = s.clipper;
        bandsOf (m, c.bands, c.xover1, c.xover2);
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = c.band[b];
            bb.bypass = p (fsid::bandId (m, b, "byp")) > 0.5f; bb.threshold = p (fsid::bandId (m, b, "thr")); bb.boost = p (fsid::bandId (m, b, "boost"));
        }
    }
    s.phase.rotation = p (fsid::id (fs::ModPhase, "rot")); s.phase.delayMs = p (fsid::id (fs::ModPhase, "delay"));
    {
        const int m = fs::ModSculptor; auto& sc = s.sculptor;
        sc.target = (int) p (fsid::id (m, "target")); sc.amount = p (fsid::id (m, "amount")); sc.tone = p (fsid::id (m, "tone"));
        sc.speed = p (fsid::id (m, "speed")); sc.lowHz = p (fsid::id (m, "lo")); sc.highHz = p (fsid::id (m, "hi"));
    }
    {
        const int m = fs::ModUnmask; auto& u = s.unmask;
        u.amount = p (fsid::id (m, "amount")); u.sensitivity = p (fsid::id (m, "sens")); u.attack = p (fsid::id (m, "att"));
        u.release = p (fsid::id (m, "rel")); u.lowHz = p (fsid::id (m, "lo")); u.highHz = p (fsid::id (m, "hi"));
    }
    return s;
}

bool ForgeStripProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void ForgeStripProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    eng.prepare (sampleRate, samplesPerBlock);
    const auto s = readSettings();
    eng.set (s);
    scBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    monoBuf.setSize (1, juce::jmax (1, samplesPerBlock));
    meterLp.lowpass (200.0, 0.7071, sampleRate); meterHp.highpass (4000.0, 0.7071, sampleRate); meterLp.reset(); meterHp.reset();
    const int lat = eng.latencyFor (s);
    wantLatency = lat; reportedLatency = lat;
    setLatencySamples (lat);
}

void ForgeStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int nch = mainIn.getNumChannels();
    if (nch == 0 || n == 0) return;
    float* L = mainIn.getWritePointer (0);
    float* R = nch > 1 ? mainIn.getWritePointer (1) : nullptr;
    if (R == nullptr)
    {
        if (monoBuf.getNumSamples() < n) monoBuf.setSize (1, n, false, false, true);
        monoBuf.copyFrom (0, 0, L, n); R = monoBuf.getWritePointer (0);
    }

    // サイドチェーン
    const float* scL = nullptr; const float* scR = nullptr;
    bool sc = false;
    if (getBusCount (true) > 1)
    {
        auto* bus = getBus (true, 1);
        if (bus != nullptr && bus->isEnabled())
        {
            auto scIn = getBusBuffer (buffer, true, 1);
            if (scIn.getNumChannels() > 0)
            {
                if (scBuf.getNumSamples() < n) scBuf.setSize (2, n, false, false, true);
                scBuf.copyFrom (0, 0, scIn, 0, 0, n);
                scBuf.copyFrom (1, 0, scIn, juce::jmin (1, scIn.getNumChannels() - 1), 0, n);
                scL = scBuf.getReadPointer (0); scR = scBuf.getReadPointer (1); sc = true;
            }
        }
    }
    scConnected = sc;

    const auto s = readSettings();
    eng.set (s);
    const int lat = eng.latencyFor (s);
    if (lat != wantLatency.load()) { wantLatency = lat; triggerAsyncUpdate(); }

    float pin = 0; for (int i = 0; i < n; ++i) pin = juce::jmax (pin, std::abs (L[i]), std::abs (R[i]));
    eng.process (L, R, n, scL, scR);
    float pout = 0, pl = 0, ph = 0;
    for (int i = 0; i < n; ++i)
    {
        pout = juce::jmax (pout, std::abs (L[i]), std::abs (R[i]));
        const float mid = 0.5f * (L[i] + R[i]);
        pl = juce::jmax (pl, std::abs (meterLp.process (mid, 0)));
        ph = juce::jmax (ph, std::abs (meterHp.process (mid, 0)));
    }
    bandLevel[0] = juce::jmax (fs::gainToDb (pl), bandLevel[0].load() - 1.5f);
    bandLevel[2] = juce::jmax (fs::gainToDb (ph), bandLevel[2].load() - 1.5f);
    bandLevel[1] = juce::jmax (fs::gainToDb (pout), bandLevel[1].load() - 1.5f);
    inLevel = juce::jmax (fs::gainToDb (pin), inLevel.load() - 1.5f);
    outLevel = juce::jmax (fs::gainToDb (pout), outLevel.load() - 1.5f);

    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// Plugin Link: 自分の様子を出し、Forge Mixer の指示を自分のつまみに反映する（メッセージスレッド）
void ForgeStripProcessor::setParam (const char* id, float v)
{
    if (auto* prm = apvts.getParameter (id))
    {
        const float nv = prm->convertTo0to1 (v);
        if (std::abs (nv - prm->getValue()) > 1.0e-4f) { prm->beginChangeGesture(); prm->setValueNotifyingHost (nv); prm->endChangeGesture(); }
    }
}

void ForgeStripProcessor::timerCallback()
{
    auto& ep = link.ep();
    if (! link.on()) { mixerSeen = false; return; }
    plink::Peer mx;
    if (ep.find (fslink::kindMixer, mx) && mx.valuesAgeMs < 700)
    {
        mixerSeen = true;
        const int cnt = juce::jlimit (0, fslink::mixer::maxCommands, (int) mx.v[fslink::mixer::count]);
        for (int i = 0; i < cnt; ++i)
        {
            const float* c = mx.v + i * fslink::mixer::stride;
            if (std::abs (c[0] - token) > 0.5f) continue;
            setParam ("outGain", c[1]); setParam ("pan", c[2]); setParam ("width", c[3]);
        }
    }
    else mixerSeen = ep.find (fslink::kindMixer, mx);
    using namespace fslink::strip;
    ep.publish (fslink::strip::token, token);
    ep.publish (gainDb, p ("outGain")); ep.publish (pan, p ("pan")); ep.publish (width, p ("width"));
    ep.publish (levelDb, outLevel.load());
    ep.publish (lowDb, bandLevel[0].load()); ep.publish (midDb, bandLevel[1].load()); ep.publish (highDb, bandLevel[2].load());
    ep.publish (mute, p ("bypass"));
    ep.stamp();
}

void ForgeStripProcessor::handleAsyncUpdate()
{
    const int l = wantLatency.load();
    if (l != reportedLatency.load()) { reportedLatency = l; setLatencySamples (l); }
}

void ForgeStripProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    int o[fs::numModules]; getOrder (o);
    apvts.state.setProperty ("order", juce::String (fs::orderToString (o)), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void ForgeStripProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            int o[fs::numModules];
            fs::orderFromString (apvts.state.getProperty ("order", "").toString().toStdString(), o);
            const juce::SpinLock::ScopedLockType l (orderLock);
            for (int i = 0; i < fs::numModules; ++i) order[i] = o[i];
        }
}

juce::AudioProcessorEditor* ForgeStripProcessor::createEditor() { return new ForgeStripEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ForgeStripProcessor(); }
