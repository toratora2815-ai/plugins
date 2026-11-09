#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String one (float v, int) { return juce::String (v, 1); }
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String db (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String times (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + "x"; }
    juce::String maxCutText (float v, int) { return v >= 21.95f ? "off" : juce::String (v, 1) + " dB"; }
    juce::String focusText (float v, int)
    {
        const int r = juce::roundToInt (v);
        if (r == 50) return "even";
        // 0 = 左(Mid)だけ … 100 = 右(Side)だけ
        const int a = juce::roundToInt (juce::jmin (1.0f, 2.0f * (1.0f - v / 100.0f)) * 100.0f);
        const int b = juce::roundToInt (juce::jmin (1.0f, 2.0f * v / 100.0f) * 100.0f);
        return juce::String (a) + " / " + juce::String (b);
    }
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float, float, float v) { return v; } };
    }
    juce::NormalisableRange<float> freqRange()
    {
        return { 20.0f, 20000.0f,
                 [] (float, float, float t) { return hf::posToHz (t); },
                 [] (float, float, float v) { return juce::jlimit (0.0f, 1.0f, hf::hzToPos (v)); },
                 [] (float, float, float v) { return v; } };
    }
}

HushFieldProcessor::HushFieldProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout HushFieldProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto addF = [&] (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                     std::function<juce::String (float, int)> toText)
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (std::move (toText))));
    };
    auto addB = [&] (const juce::String& id, const juce::String& name, bool def)
    { v.push_back (std::make_unique<B> (juce::ParameterID { id, 1 }, name, def)); };
    auto addC = [&] (const juce::String& id, const juce::String& name, juce::StringArray items, int def)
    { v.push_back (std::make_unique<C> (juce::ParameterID { id, 1 }, name, items, def)); };

    addC ("mode", "Mode", { "soft", "hard" }, 0);
    addF ("depth",   "Depth",   { 0.0f, 10.0f, 0.1f }, 5.0f, one);
    addF ("detail",  "Detail",  { 0.0f, 10.0f, 0.1f }, 5.0f, one);
    addF ("attack",  "Attack",  { 0.0f, 10.0f, 0.1f }, 5.0f, one);
    addF ("release", "Release", { 0.0f, 10.0f, 0.1f }, 3.0f, one);

    addF ("mix",     "Mix",      { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    addF ("outTrim", "Out Trim", { -18.0f, 18.0f, 0.1f }, 0.0f, db);
    addF ("wetTrim", "Wet Trim", { -18.0f, 18.0f, 0.1f }, 0.0f, db);
    addB ("delta",  "Delta", false);
    addB ("bypass", "Bypass", false);
    addF ("maxCut", "Max Cut", { 0.0f, 22.0f, 0.1f }, 22.0f, maxCutText);

    addC ("stereoMode", "Stereo Mode", { "L/R", "M/S" }, 0);
    addF ("stereoLink",  "Stereo Link",  { 0.0f, 100.0f, 1.0f }, 100.0f, pct);
    addF ("stereoFocus", "Stereo Focus", { 0.0f, 100.0f, 1.0f }, 50.0f, focusText);

    addB ("sidechain", "Sidechain", false);
    addB ("scListen",  "Sidechain Listen", false);
    addC ("quality", "Quality", { "normal", "high", "ultra" }, 0);
    addB ("lowLatency",  "Low Latency", false);
    addB ("linearPhase", "Linear Phase", false);
    addC ("bandSolo", "Band Solo", { "off", "1", "2", "3", "4", "5", "6", "7", "8" }, 0);

    for (auto* t : { "detail", "attack", "release" })
        for (auto* side : { "Low", "High" })
        {
            const juce::String id = juce::String (t) + "Tilt" + side;
            const juce::String name = juce::String (t).substring (0, 1).toUpperCase() + juce::String (t).substring (1) + " Tilt " + side;
            auto r = logRange (0.1f, 10.0f);
            addF (id, name, r, 1.0f, times);
        }

    const juce::StringArray shapes { "Low cut", "Low shelf", "Bell", "Bandpass", "Band reject", "Tilt", "High shelf", "High cut" };
    const float defHz[hf::numBands] = { 100.0f, 4000.0f, 16000.0f, 250.0f, 1000.0f, 2500.0f, 7000.0f, 600.0f };
    const int defShape[hf::numBands] = { hf::LowCut, hf::Bell, hf::HighCut, hf::Bell, hf::Bell, hf::Bell, hf::Bell, hf::Bell };
    for (int b = 0; b < hf::numBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";
        addB (bandId (b, "used"), n + "Used", b < 3);
        addB (bandId (b, "on"), n + "Enabled", true);
        addC (bandId (b, "shape"), n + "Shape", shapes, defShape[b]);
        addF (bandId (b, "freq"), n + "Frequency", freqRange(), defHz[b], hz);
        auto qr = logRange (0.1f, 10.0f);
        addF (bandId (b, "q"), n + "Q", qr, 0.7f, [] (float x, int) { return juce::String (x, x < 1.0f ? 2 : 1); });
        addF (bandId (b, "gain"), n + "Depth", { -10.0f, 10.0f, 0.1f }, b == 1 ? 2.5f : 0.0f,
              [] (float x, int) { return (x > 0.05f ? "+" : "") + juce::String (x, 1); });
        addF (bandId (b, "slope"), n + "Slope", { 0.0f, 10.0f, 0.1f }, 5.0f, one);
        addF (bandId (b, "focus"), n + "Focus", { 0.0f, 100.0f, 1.0f }, 50.0f, focusText);
    }
    // v1.1: Plugin Link
    v.push_back (myp::GainMatch::makeParameter());
    v.push_back (plink::makeGroupParameter (2));
    v.push_back (std::make_unique<B> (juce::ParameterID { "linkFollow", 2 }, "Link Follow", true));
    return { v.begin(), v.end() };
}

bool HushFieldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void HushFieldProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    eng.prepare (sampleRate, samplesPerBlock);
    const int lat = eng.latencyFor (readSettings());
    wantLatency = lat;
    reportedLatency = lat;
    setLatencySamples (lat);
    gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
}

void HushFieldProcessor::handleAsyncUpdate()
{
    const int lat = wantLatency.load();
    if (lat != reportedLatency.load()) { reportedLatency = lat; setLatencySamples (lat); }
}

hf::Settings HushFieldProcessor::readSettings() const
{
    auto f = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    hf::Settings s;
    s.hard = f ("mode") > 0.5f;
    s.depth = f ("depth"); s.detail = f ("detail"); s.attack = f ("attack"); s.release = f ("release");
    s.detailTiltLow = f ("detailTiltLow"); s.detailTiltHigh = f ("detailTiltHigh");
    s.attackTiltLow = f ("attackTiltLow"); s.attackTiltHigh = f ("attackTiltHigh");
    s.releaseTiltLow = f ("releaseTiltLow"); s.releaseTiltHigh = f ("releaseTiltHigh");
    s.midSide = f ("stereoMode") > 0.5f;
    s.stereoLink = f ("stereoLink") * 0.01f;
    s.stereoFocus = f ("stereoFocus") * 0.01f;
    const float mc = f ("maxCut");
    s.maxCutDb = mc >= 21.95f ? 1000.0f : mc;
    s.mix = f ("mix") * 0.01f;
    s.wetTrimDb = f ("wetTrim"); s.outTrimDb = f ("outTrim");
    s.delta = f ("delta") > 0.5f; s.bypass = f ("bypass") > 0.5f;
    s.sidechain = f ("sidechain") > 0.5f; s.sidechainListen = f ("scListen") > 0.5f;
    s.quality = juce::roundToInt (f ("quality"));
    s.lowLatency = f ("lowLatency") > 0.5f; s.linearPhase = f ("linearPhase") > 0.5f;
    s.bandSolo = juce::roundToInt (f ("bandSolo")) - 1;
    for (int b = 0; b < hf::numBands; ++b)
    {
        auto& bd = s.bands[b];
        bd.used = f (bandId (b, "used")) > 0.5f;
        bd.enabled = f (bandId (b, "on")) > 0.5f;
        bd.shape = juce::roundToInt (f (bandId (b, "shape")));
        bd.hz = f (bandId (b, "freq"));
        bd.q = f (bandId (b, "q"));
        bd.gain = f (bandId (b, "gain"));
        bd.slope = f (bandId (b, "slope"));
        bd.focus = f (bandId (b, "focus")) * 0.01f;
    }
    return s;
}

void HushFieldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const auto s = readSettings();

    const int lat = eng.latencyFor (s);
    if (lat != wantLatency.load()) { wantLatency = lat; triggerAsyncUpdate(); }

    juce::AudioBuffer<float> scBuf;
    const float* const* sc = nullptr;
    int numSc = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
        {
            scBuf = getBusBuffer (buffer, true, 1);
            sc = scBuf.getArrayOfReadPointers();
            numSc = scBuf.getNumChannels();
        }
    scConnected = numSc > 0;
    auto s2 = s;
    applyLink (s2, main.getNumSamples());
    gainMatch.measureInput (main, main.getNumChannels());
    eng.process (main.getArrayOfWritePointers(), main.getNumChannels(), sc, numSc, main.getNumSamples(), s2);
    // GAIN MATCH（DELTA・SC LISTEN・SOLO・BYPASS の間は効かない）
    gainMatch.process (main, main.getNumChannels(), lat, (! s.delta && ! s.sidechainListen && s.bandSolo < 0 && ! s.bypass) ? 1.0f : 0.0f);
    publishReduction (main.getNumSamples());
}

// Balance Scope が「出すぎ」を送ってきている間、その帯域にベルを足して強めに処理する（画面のカーブはそのまま）。
// 強さ = 出すぎの量に応じて +2〜+8、約 0.8 秒でなめらかに出入りする。2 か所目は 1 か所目より控えめ
void HushFieldProcessor::applyLink (hf::Settings& s, int numSamples)
{
    float want[2] = { 0.0f, 0.0f }, lo[2] = { 0, 0 }, hi[2] = { 0, 0 };
    if (link.on() && apvts.getRawParameterValue ("linkFollow")->load() > 0.5f)
    {
        plink::Peer p;
        if (link.ep().find (plink::kindBalanceScope, p) && p.v[plink::bs::valid] > 0.5f && p.valuesAgeMs < 5000)
        {
            const int idx[2][3] = { { plink::bs::excessDb, plink::bs::lowHz, plink::bs::highHz },
                                    { plink::bs::excess2Db, plink::bs::low2Hz, plink::bs::high2Hz } };
            for (int i = 0; i < 2; ++i)
            {
                const float ex = p.v[idx[i][0]], l = p.v[idx[i][1]], h = p.v[idx[i][2]];
                if (ex >= 0.5f && l >= 20.0f && h > l)
                {
                    want[i] = juce::jlimit (2.0f, 8.0f, 2.0f + 1.5f * ex) * (i == 0 ? 1.0f : 0.7f);
                    lo[i] = l; hi[i] = juce::jmin (20000.0f, h);
                }
            }
        }
    }
    const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.8 * getSampleRate() + 1.0));
    for (int i = 0; i < 2; ++i)
    {
        if (want[i] > 0.0f) { if (linkLo[i] < 0.0f || linkGain[i] < 0.05f) { linkLo[i] = lo[i]; linkHi[i] = hi[i]; }
                              linkLo[i] += (lo[i] - linkLo[i]) * a; linkHi[i] += (hi[i] - linkHi[i]) * a; }
        linkGain[i] += (want[i] - linkGain[i]) * a;
        if (linkGain[i] < 0.02f) linkGain[i] = 0.0f;
        auto& b = s.linkBands[i];
        b.used = linkGain[i] > 0.0f && linkLo[i] > 0.0f;
        b.enabled = true;
        b.shape = hf::Bell;
        b.hz = std::sqrt (juce::jmax (20.0f, linkLo[i]) * juce::jmax (21.0f, linkHi[i]));
        const float oct = juce::jmax (0.3f, std::log2 (juce::jmax (21.0f, linkHi[i]) / juce::jmax (20.0f, linkLo[i])));
        b.q = juce::jlimit (0.2f, 4.0f, 1.4f / oct);     // ベルの幅（オクターブ）≈ 1.4 / Q
        b.gain = std::round (linkGain[i] * 8.0f) / 8.0f;  // 細かく変わりすぎないように
        b.focus = 0.5f;
        linkShown[i] = b.used ? linkGain[i] : 0.0f;
        linkHz[i] = b.hz;
    }
}

// 削っている量を 16 帯域（100 Hz〜16 kHz）にまとめ、約 0.5 秒でならして掲示板へ
void HushFieldProcessor::publishReduction (int numSamples)
{
    if (! link.on()) return;
    const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.5 * getSampleRate() + 1.0));
    auto& ep = link.ep();
    float maxRed = 0.0f;
    for (int i = 0; i < plink::tamer::numBands; ++i)
    {
        const int from = juce::jlimit (0, hf::HushEngine::displayPoints - 1, (int) (hf::hzToPos (plink::tamer::bandEdgeHz (i)) * hf::HushEngine::displayPoints));
        const int to = juce::jlimit (from, hf::HushEngine::displayPoints - 1, (int) (hf::hzToPos (plink::tamer::bandEdgeHz (i + 1)) * hf::HushEngine::displayPoints) - 1);
        float m = 0.0f;
        for (int k = from; k <= to; ++k) m = juce::jmax (m, eng.shownReduction (k));
        maxRed = juce::jmax (maxRed, m);
        bandRed[i] += (-m - bandRed[i]) * a;
        ep.publish (plink::tamer::band0 + i, bandRed[i]);
    }
    ep.publish (plink::tamer::maxReductionDb, -maxRed);
    ep.stamp();
    ep.heartbeat();
}

// ---------------- 状態の保存（A/B のもう片方も一緒に） ----------------
void HushFieldProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("onB", onB, nullptr);
    state.removeChild (state.getChildWithName ("OTHER"), nullptr);
    if (otherState.isValid())
    {
        juce::ValueTree o ("OTHER");
        o.appendChild (otherState.createCopy(), nullptr);
        state.appendChild (o, nullptr);
    }
    gainMatch.saveTo (state);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void HushFieldProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (apvts.state.getType())) return;
        auto o = tree.getChildWithName ("OTHER");
        otherState = o.isValid() && o.getNumChildren() > 0 ? o.getChild (0).createCopy() : juce::ValueTree();
        tree.removeChild (o, nullptr);
        onB = (bool) tree.getProperty ("onB", false);
        apvts.replaceState (tree);
        gainMatch.loadFrom (apvts.state);
    }
}

void HushFieldProcessor::switchAB (bool toB)
{
    if (toB == onB) return;
    auto now = apvts.copyState();
    now.removeChild (now.getChildWithName ("OTHER"), nullptr);
    auto* gm = apvts.getParameter (myp::GainMatch::paramId);   // GAIN MATCH は A/B で変えない
    const float gmNow = gm->getValue();
    if (otherState.isValid()) apvts.replaceState (otherState.createCopy());
    gm->setValueNotifyingHost (gmNow);
    otherState = now;
    onB = toB;
    gainMatch.requestRelearn();   // GAIN MATCH: プリセットを読んだら測り直す
}

void HushFieldProcessor::copyAtoB()
{
    // 今見ている側を、もう片方へ写す
    auto now = apvts.copyState();
    now.removeChild (now.getChildWithName ("OTHER"), nullptr);
    otherState = now.createCopy();
}

// ---------------- プリセット ----------------
juce::StringArray HushFieldProcessor::presetNames()
{
    return { "Init",
             "Vocal - smooth harshness",
             "Hi-hat / cymbal - harsh only, keep air",
             "Acoustic guitar - boom & honk",
             "De-ess - sibilance only",
             "Mud cleaner (200-500 Hz)",
             "Mix bus - gentle",
             "Master - very gentle" };
}

void HushFieldProcessor::applyPreset (int index)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    // まず全部を初期値に
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (r->getParameterID() != myp::GainMatch::paramId)   // GAIN MATCH はプリセットで変えない
                r->setValueNotifyingHost (r->getDefaultValue());

    struct BandSet { int shape; float hz, q, gain, slope; };
    auto bands = [&] (std::initializer_list<BandSet> list)
    {
        int b = 0;
        for (const auto& x : list)
        {
            set (bandId (b, "used"), 1.0f); set (bandId (b, "on"), 1.0f);
            set (bandId (b, "shape"), (float) x.shape); set (bandId (b, "freq"), x.hz);
            set (bandId (b, "q"), x.q); set (bandId (b, "gain"), x.gain); set (bandId (b, "slope"), x.slope);
            ++b;
        }
        for (; b < hf::numBands; ++b) set (bandId (b, "used"), 0.0f);
    };
    auto main = [&] (int hard, float depth, float detail, float attack, float release)
    {
        set ("mode", (float) hard); set ("depth", depth); set ("detail", detail); set ("attack", attack); set ("release", release);
    };

    switch (index)
    {
        case 1: main (0, 6.0f, 5.0f, 4.0f, 3.0f);
                bands ({ { hf::LowCut, 180.0f, 0.7f, 0, 5 }, { hf::Bell, 3500.0f, 1.0f, 4.0f, 5 }, { hf::HighCut, 14000.0f, 0.7f, 0, 5 } }); break;
        case 2: // 2.5 kHz より下は触らない、5 kHz あたりの刺さる所を強め、10 kHz より上（空気感）は守る
                main (0, 6.0f, 6.0f, 2.0f, 2.0f);
                bands ({ { hf::LowCut, 2500.0f, 0.7f, 0, 6 }, { hf::Bell, 5000.0f, 0.9f, 4.0f, 5 }, { hf::HighShelf, 10000.0f, 1.0f, -10.0f, 5 } }); break;
        case 3: main (0, 5.0f, 4.0f, 5.0f, 3.0f);
                bands ({ { hf::LowCut, 70.0f, 0.7f, 0, 5 }, { hf::Bell, 200.0f, 0.8f, 3.0f, 5 }, { hf::Bell, 1200.0f, 0.8f, 2.0f, 5 }, { hf::HighCut, 12000.0f, 0.7f, 0, 5 } }); break;
        case 4: main (1, 6.0f, 6.0f, 1.0f, 2.0f);
                bands ({ { hf::BandPass, 7000.0f, 0.8f, 2.0f, 6 } }); break;
        case 5: main (0, 5.0f, 4.0f, 5.0f, 4.0f);
                bands ({ { hf::BandPass, 320.0f, 0.7f, 0.0f, 4 } }); break;
        case 6: main (0, 3.5f, 3.0f, 6.0f, 5.0f);
                bands ({ { hf::LowCut, 40.0f, 0.7f, 0, 5 }, { hf::HighCut, 18000.0f, 0.7f, 0, 5 } }); break;
        case 7: main (0, 2.5f, 4.0f, 6.0f, 5.0f);
                bands ({ { hf::LowCut, 30.0f, 0.7f, 0, 5 } }); break;
        default: break;   // Init = 全部初期値
    }
    gainMatch.requestRelearn();   // GAIN MATCH: プリセットを読んだら測り直す
}

juce::AudioProcessorEditor* HushFieldProcessor::createEditor() { return new HushFieldEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HushFieldProcessor(); }
