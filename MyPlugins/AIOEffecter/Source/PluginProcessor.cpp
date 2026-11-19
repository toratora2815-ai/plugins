#include "PluginProcessor.h"
#include "PluginEditor.h"

AIOEffecterProcessor::AIOEffecterProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int s = 0; s < aiofx::kSlots; ++s)
    {
        pType[(size_t) s] = apvts.getRawParameterValue (typeId (s));
        pOn[(size_t) s] = apvts.getRawParameterValue (onId (s));
        for (int k = 0; k < aiofx::kMaxKnobs; ++k)
            pKnob[(size_t) s][(size_t) k] = apvts.getRawParameterValue (knobId (s, k));
    }
    pGainMatch = apvts.getRawParameterValue (myp::GainMatch::paramId);
    pLinkMatch = apvts.getRawParameterValue ("linkMatch");
    tVerb = aiofx::typeForId ("studiovb"); tSpring = aiofx::typeForId ("spring"); tDelay = aiofx::typeForId ("twindly"); tLimit = aiofx::typeForId ("limit"); tMax = aiofx::typeForId ("maximizer");
    // GAIN MATCH: センド使い（ドライがほぼない）を見分けるつまみ（部品の id、つまみの番号、DRY 型か）
    for (const auto& r : { MixRuleDef { "spacevb", 13, false }, MixRuleDef { "classicvb", 1, false }, MixRuleDef { "shaperoom", 1, true },
                           MixRuleDef { "pickroom", 3, false }, MixRuleDef { "clubverb", 3, false }, MixRuleDef { "digihall", 10, false },
                           MixRuleDef { "digihallxl", 15, false }, MixRuleDef { "frostverb", 3, false }, MixRuleDef { "eightverb", 6, false },
                           MixRuleDef { "eightverb", 7, false },
                           MixRuleDef { "warpdly", 2, false }, MixRuleDef { "bbdecho", 3, false }, MixRuleDef { "widedly", 2, false },
                           MixRuleDef { "clubdly", 3, false }, MixRuleDef { "styledly", 0, false } })
        // RING MOD の DRY/WET は入れない（100 % ウェットでトラックに挿すのがふつうの使い方。センド使いではないので GAIN MATCH は効かせる）
        if (const int t = aiofx::typeForId (r.id); t > 0) mixRules.push_back ({ t, r.knob, r.dry });
    startTimerHz (20);
}

AIOEffecterProcessor::~AIOEffecterProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout AIOEffecterProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    for (int s = 0; s < aiofx::kSlots; ++s)
    {
        const juce::String n = "Slot " + juce::String (s + 1);
        // 部品の番号は 0..255 で固定（部品を足しても保存の値の意味が変わらないように）
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { typeId (s), 1 }, n + " Type", 0, 255, 0,
            juce::AudioParameterIntAttributes().withAutomatable (false).withStringFromValueFunction ([] (int v, int)
            {
                if (auto* i = aiofx::infoFor (v)) return juce::String (i->name);
                return juce::String ("-");
            })));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { onId (s), 1 }, n + " On", true));
        // つまみ 13〜16 は 0.2.0 で足した（版のヒント 11 = 前からのパラメーターの番号をずらさない）
        for (int k = 0; k < aiofx::kMaxKnobs; ++k)
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { knobId (s, k), k < 12 ? 1 : 11 }, n + " Knob " + juce::String (k + 1),
                juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    }
    params.push_back (myp::GainMatch::makeParameter());
    params.push_back (plink::makeGroupParameter (1));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "linkMatch", 1 }, "Link Match Level", true));
    return { params.begin(), params.end() };
}

bool AIOEffecterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AIOEffecterProcessor::readSlots (std::array<aiofx::SlotKnobs, aiofx::kSlots>& dst) const
{
    for (int s = 0; s < aiofx::kSlots; ++s)
    {
        auto& d = dst[(size_t) s];
        const int t = (int) pType[(size_t) s]->load();
        d.type = (t >= 0 && t <= aiofx::numTypes()) ? t : 0;   // 知らない番号（新しい版の部品など）は空き
        d.on = pOn[(size_t) s]->load() > 0.5f;
        for (int k = 0; k < aiofx::kMaxKnobs; ++k)
            d.k[k] = juce::jlimit (0.0f, 1.0f, pKnob[(size_t) s][(size_t) k]->load());
    }
}

void AIOEffecterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType sl (prepLock);
    readSlots (msgKnobs);
    rack.prepare (sampleRate, juce::jmax (16, samplesPerBlock), msgKnobs);
    monoTmp.assign ((size_t) juce::jmax (16, samplesPerBlock), 0.0f);
    prepared = true;
    setLatencySamples (rack.latency());   // 先読みのある部品（MAXIMIZER）があるときだけ遅れる
    gainMatch.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), samplesPerBlock);
}

void AIOEffecterProcessor::timerCallback()
{
    const juce::SpinLock::ScopedTryLockType sl (prepLock);
    if (! sl.isLocked() || ! prepared) return;
    readSlots (msgKnobs);
    rack.service (msgKnobs);
    if (rack.latency() != getLatencySamples()) setLatencySamples (rack.latency());   // 部品を選び直して遅れが変わった
}

void AIOEffecterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (numOut >= 2 && numIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // モノの入口は左右に

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));

    readSlots (audioKnobs);
    double bpm = 120.0, ppq = -1.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;   // 曲の位置（揺れものの SYNC を小節にそろえる）
            playing = pos->getIsPlaying();
        }
    float* L = buffer.getWritePointer (0);
    float* R = numOut >= 2 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);
    if (numOut >= 2)
        rack.process (L, R, n, audioKnobs, bpm, ppq, playing);
    else
    {
        // モノの出口: 右に同じものを作って通し、左右の平均を返す（作業用の場所は prepareToPlay で確保済み）
        if (monoTmp.empty()) return;
        for (int off = 0; off < n; off += (int) monoTmp.size())
        {
            const int m = juce::jmin ((int) monoTmp.size(), n - off);
            std::copy (L + off, L + off + m, monoTmp.data());
            const double ppqHere = ppq >= 0.0 ? ppq + (double) off / getSampleRate() * bpm / 60.0 : -1.0;
            rack.process (L + off, monoTmp.data(), m, audioKnobs, bpm, ppqHere, playing);
            for (int i = 0; i < m; ++i) L[off + i] = 0.5f * (L[off + i] + monoTmp[(size_t) i]);
        }
    }

    const int numCh = juce::jmin (numOut, 2);
    // GAIN MATCH: リバーブ・ディレイを MIX 90〜100 % で使っている（センド使い）なら少しずつ効かなくする。
    // 最後に効いている段が LIMITER / MAXIMIZER なら上げない（決まり: 最後にリミッターがあるなら limitBoost (0)）
    float amount = 1.0f;
    int lastType = 0;
    for (const auto& sk : audioKnobs)
    {
        if (! sk.on || sk.type == 0) continue;
        lastType = sk.type;
        const int mixIdx = sk.type == tVerb ? 0 : sk.type == tSpring ? 1 : sk.type == tDelay ? 10 : -1;
        if (mixIdx >= 0) amount = juce::jmin (amount, juce::jlimit (0.0f, 1.0f, (1.0f - sk.k[mixIdx]) / 0.1f));
        // 0.3.0 からの響き・ディレイ: MIX 型は (1 − つまみ) / 0.1、DRY 型（ドライの量そのもの）は つまみ / 0.1
        for (const auto& r : mixRules)
            if (r.type == sk.type)
                amount = juce::jmin (amount, juce::jlimit (0.0f, 1.0f, r.dryKnob ? sk.k[r.knob] / 0.1f : (1.0f - sk.k[r.knob]) / 0.1f));
    }
    gainMatch.limitBoost ((lastType == tLimit || lastType == tMax) ? 0.0f : myp::GainMatch::maxBoostDb);
    gainMatch.process (buffer, numCh, getLatencySamples(), amount);
    const bool gmOn = pGainMatch->load() > 0.5f;
    match.process (link, ! gmOn && pLinkMatch->load() > 0.5f, buffer.getArrayOfWritePointers(), numCh, n, getSampleRate());

    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
    const float pkDb = juce::Decibels::gainToDecibels (pk, -100.0f);
    const float prev = outPeakDb.load();
    outPeakDb.store (pkDb > prev ? pkDb : prev - 0.3f);
}

void AIOEffecterProcessor::reset()
{
    rack.resetAll();
}

void AIOEffecterProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void AIOEffecterProcessor::setSlotType (int slot, int type)
{
    setParam (typeId (slot), (float) type);
    if (auto* inf = aiofx::infoFor (type))
        for (int k = 0; k < aiofx::kMaxKnobs; ++k)
            setParam (knobId (slot, k), k < (int) inf->knobs.size() ? inf->knobs[(size_t) k].def : 0.5f);
    setParam (onId (slot), 1.0f);
    // タイマー（最大 50 ms）を待たずに、その場で新しい部品を作って渡す（ドライで待つ時間を短く）
    const juce::SpinLock::ScopedTryLockType sl (prepLock);
    if (sl.isLocked() && prepared) { readSlots (msgKnobs); rack.service (msgKnobs); }
}

void AIOEffecterProcessor::moveSlot (int from, int to)
{
    if (from < 0 || to < 0 || from >= aiofx::kSlots || to >= aiofx::kSlots || from == to) return;
    aiofx::SlotKnobs a {}, b {};
    std::array<aiofx::SlotKnobs, aiofx::kSlots> cur {};
    readSlots (cur);
    a = cur[(size_t) from]; b = cur[(size_t) to];
    auto write = [this] (int s, const aiofx::SlotKnobs& v)
    {
        setParam (typeId (s), (float) v.type);
        setParam (onId (s), v.on ? 1.0f : 0.0f);
        for (int k = 0; k < aiofx::kMaxKnobs; ++k) setParam (knobId (s, k), v.k[k]);
    };
    write (from, b);
    write (to, a);
}

// ---- 内蔵ラック（つまみは Guitar Rig と同じ 0..1 の尺度）----
namespace
{
    struct RackSlot { const char* id; std::vector<float> k; };
    struct RackPreset { const char* name; std::vector<RackSlot> slots; };
    const std::vector<RackPreset>& rackPresets()
    {
        static const std::vector<RackPreset> r {
            { "Clean Gate + Comp", { { "gate", { 0.25f, 0.0f, 0.0f, 0.3f } },
                                     { "tubecomp", { 0.59f, 0.5f, 0.49f, 0.225f, 0.54f, 0.25f, 0.09f, 0.246f, 0.535f } } } },
            { "Spring Clean",      { { "gate", { 0.2f, 0.0f, 0.0f, 0.35f } },
                                     { "spring", { 0.12f, 0.45f, 0.257f, 0.4f, 0.5f, 0.9f } } } },
            { "Room + Echo",       { { "twindly", { 0.55f, 0.6f, 0.45f, 0.45f, 0.3f, 0.3f, 0.0f, 0.02f, 0.0f, 1.0f, 0.3f } },
                                     { "studiovb", { 0.35f, 0.15f, 0.55f, 0.6f, 0.6f, 1.0f } } } },
            { "Tight High Gain",   { { "gate", { 0.35f, 0.0f, 0.0f, 0.2f } },
                                     { "nred", { 0.25f, 0.4f, 0.5f } },
                                     { "limit", { 0.47f, 0.2f, 0.0f, 0.5f } } } },
            { "Hiss Cleanup",      { { "nred", { 0.3f, 0.6f, 0.6f } } } },
            // 0.2.0
            { "Opto Vocal",        { { "optocomp", {} },
                                     { "studiovb", { 0.25f, 0.1f, 0.5f, 0.6f, 0.6f, 1.0f } } } },
            { "Drum Bus Glue",     { { "buscomp", {} },
                                     { "transient", { 0.65f, 0.4f, 0.56f, 1.0f, 1.0f } } } },
            { "Push + Maximize",   { { "tubepush", { 0.433333f, 0.35f, 0.0f, 0.0f, 0.0f, 0.5f } },
                                     { "maximizer", {} } } },
            // 0.3.0（響き）
            { "Big Space",         { { "spacevb", { 0.5f, 0.0f, 0.25f, 0.0f, 0.6f, 0.35f, 0.45f, 0.35f, 0.25f, 0.8f, 0.3f, 0.25f, 1.0f, 0.3f, 0.0f, 0.0f } } } },
            { "Vintage Plate",     { { "classicvb", { 2.0f / 7.0f, 0.18f } } } },
            { "Reverse Swell",     { { "shaperoom", { 0.5f, 1.0f, 0.3333f, 0.0f, 0.6644f, 0.5f, 0.0f, 1.0f, 0.5f, 0.7686f, 0.5f, 0.5f, 0.5f, 0.5f, 0.4215f, 0.5985f } } } },
            { "80s Hall",          { { "digihall", { 0.5f, 0.2018f, 0.5179f, 0.4551f, 1.0f, 0.0f, 0.1783f, 0.6078f, 0.7668f, 1.0f, 0.25f, 0.5f, 0.5f, 0.0f } } } },
            // 0.4.0（ディレイ）
            { "Tape Echo",         { { "tapeheads", {} } } },
            { "Analog Echo",       { { "bbdecho", {} } } },
            { "Ping-Pong 1/8",     { { "styledly", { 0.25f, 0.375f, 1.0f / 3.0f, 0.35f, 0.0f, 0.0f, 0.15f, 0.0f, 1.0f } } } },
            { "Club Echo",         { { "clubdly", {} } } },
            // 0.5.0（EQ・揺れもの）
            { "Warm + Air",        { { "shelfeq", { 0.5f, 0.2475f, 0.575f, 0.785f, 0.575f } } } },
            { "Classic Tremolo",   { { "tremolo", {} } } },
            { "Auto-Pan",          { { "tremolo", { 0.0f, 0.5f, 0.45f, 1.0f / 3.0f, 1.0f, 0.85f, 0.85f } } } },
            { "Ring Bell",         { { "ringmod", { 0.6f, 0.2f, 1.0f, 0.5f } } } },
            { "Jet Flanger",       { { "flanger", { 0.0f, 1.0f, 0.25f, 0.8f, 0.8f, 1.0f } } } },
        };
        return r;
    }
}

juce::StringArray AIOEffecterProcessor::rackPresetNames()
{
    juce::StringArray s;
    for (auto& p : rackPresets()) s.add (p.name);
    return s;
}

void AIOEffecterProcessor::loadRackPreset (int index)
{
    if (index < 0 || index >= (int) rackPresets().size()) return;
    const auto& p = rackPresets()[(size_t) index];
    for (int s = 0; s < aiofx::kSlots; ++s)
    {
        if (s < (int) p.slots.size())
        {
            const int t = aiofx::typeForId (p.slots[(size_t) s].id);
            setSlotType (s, t);
            const auto& kk = p.slots[(size_t) s].k;
            for (int k = 0; k < (int) kk.size() && k < aiofx::kMaxKnobs; ++k) setParam (knobId (s, k), kk[(size_t) k]);
        }
        else
        {
            setParam (typeId (s), 0.0f);
            setParam (onId (s), 1.0f);
        }
    }
    gainMatch.requestRelearn();
}

void AIOEffecterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void AIOEffecterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
        }
}

juce::AudioProcessorEditor* AIOEffecterProcessor::createEditor()
{
    return new AIOEffecterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIOEffecterProcessor();
}
