#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pct (float v, int)  { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; }
    juce::String upct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String hz (float v, int)   { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }
    juce::String ms (float v, int)   { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String db (float v, int)   { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String deg (float v, int)  { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("°"); }

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    // 自分自身（入れ子にしない）
    constexpr const char* selfCode = "Mdhs";
    constexpr const char* selfManu = "Mypl";
}

ModHostProcessor::ModHostProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());

    for (int m = 0; m < mh::numMods; ++m)
    {
        typeParam[m]    = apvts.getRawParameterValue (mid (m, "type"));
        shapeParam[m]   = apvts.getRawParameterValue (mid (m, "shape"));
        syncParam[m]    = apvts.getRawParameterValue (mid (m, "sync"));
        divParam[m]     = apvts.getRawParameterValue (mid (m, "div"));
        rateParam[m]    = apvts.getRawParameterValue (mid (m, "rate"));
        attackParam[m]  = apvts.getRawParameterValue (mid (m, "attack"));
        releaseParam[m] = apvts.getRawParameterValue (mid (m, "release"));
        gainParam[m]    = apvts.getRawParameterValue (mid (m, "gain"));
        smoothParam[m]  = apvts.getRawParameterValue (mid (m, "smooth"));
        stepsParam[m]   = apvts.getRawParameterValue (mid (m, "steps"));
        bipolarParam[m] = apvts.getRawParameterValue (mid (m, "bipolar"));
        phaseParam[m]   = apvts.getRawParameterValue (mid (m, "phase"));
        modNow[m] = 0.0f;
        mods[m].setSeed ((uint32_t) (m + 1) * 7919u);

        for (int t = 0; t < mh::numTargets; ++t)
        {
            amountParam[m][t] = apvts.getRawParameterValue (amountId (m, t));
            targets[m][t] = -1;
            shownBase[m][t] = 0.0f;
            shownNow[m][t] = 0.0f;
        }
        // STEPS の最初の形: 上がって下がる
        for (int k = 0; k < mh::maxSteps; ++k)
            stepVals[m][k] = (float) (k % 8 < 4 ? (k % 8 + 1) / 4.0 : (8 - k % 8) / 4.0 - 0.25);
    }
}

ModHostProcessor::~ModHostProcessor()
{
    *aliveFlag = false;
    cancelPendingUpdate();
    if (hosted != nullptr) hosted->removeListener (this);
}

juce::AudioProcessorValueTreeState::ParameterLayout ModHostProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;

    auto addF = [&] (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                     std::function<juce::String (float, int)> toText)
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def,
                                          A().withStringFromValueFunction (std::move (toText))));
    };

    juce::StringArray types { "LFO", "ENV", "SIDECHAIN", "RANDOM", "STEPS", "LINK" };
    juce::StringArray shapes { "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE" };
    juce::StringArray divs;
    for (int i = 0; i < mh::numDivs; ++i) divs.add (mh::divName (i));

    const int defType[mh::numMods] = { mh::typeLfo, mh::typeEnv, mh::typeRandom, mh::typeSteps };
    const bool defBi[mh::numMods] = { true, false, true, false };

    for (int m = 0; m < mh::numMods; ++m)
    {
        const juce::String n = "Mod " + juce::String (m + 1) + " ";
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mid (m, "type"), 1 }, n + "Type", types, defType[m]));
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mid (m, "shape"), 1 }, n + "Shape", shapes, 0));
        v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { mid (m, "sync"), 1 }, n + "Sync", true));
        v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mid (m, "div"), 1 }, n + "Division", divs,
                                                                   m == 3 ? 1 : 6));
        addF (mid (m, "rate"),    n + "Rate",    skewed (0.02f, 20.0f, 0.01f, 1.0f), 1.0f, hz);
        addF (mid (m, "attack"),  n + "Attack",  skewed (0.5f, 200.0f, 0.1f, 10.0f), 5.0f, ms);
        addF (mid (m, "release"), n + "Release", skewed (5.0f, 2000.0f, 1.0f, 150.0f), 150.0f, ms);
        addF (mid (m, "gain"),    n + "Gain",    { -24.0f, 24.0f, 0.1f }, 0.0f, db);
        addF (mid (m, "smooth"),  n + "Smooth",  { 0.0f, 100.0f, 1.0f }, 0.0f, upct);
        addF (mid (m, "phase"),   n + "Phase",   { 0.0f, 360.0f, 1.0f }, 0.0f, deg);
        v.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { mid (m, "steps"), 1 }, n + "Steps", 2, mh::maxSteps, 8));
        v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { mid (m, "bipolar"), 1 }, n + "Bipolar", defBi[m]));
        for (int t = 0; t < mh::numTargets; ++t)
            addF (amountId (m, t), n + "Amount " + juce::String (t + 1), { -100.0f, 100.0f, 0.1f }, 0.0f, pct);
    }
    // Plugin Link（LINK の番号）
    v.push_back (plink::makeGroupParameter (9));
    return { v.begin(), v.end() };
}

bool ModHostProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

// ───────────────────────── 中のプラグインの準備 ─────────────────────────

void ModHostProcessor::prepareHosted (juce::AudioPluginInstance& p)
{
    // メインの入出力をステレオに（できなければプラグインの最初の形のまま）、それ以外のバスは切る
    auto l = p.getBusesLayout();
    for (int i = 0; i < l.inputBuses.size(); ++i)
        l.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    for (int i = 0; i < l.outputBuses.size(); ++i)
        l.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (p.checkBusesLayoutSupported (l)) p.setBusesLayout (l);

    p.setRateAndBufferSizeDetails (rate, maxBlock);
    p.prepareToPlay (rate, maxBlock);
}

// hostLock の中で呼ぶ: 作業バッファとつまみごとの記録の大きさを合わせる
static void resizeFor (juce::AudioPluginInstance* p, juce::AudioBuffer<float>& scratch, int& ins, int& outs, int maxBlock,
                       std::vector<float>& base, std::vector<float>& last, std::vector<char>& was,
                       std::vector<int>& act, std::vector<int>& prev, std::vector<float>& offs)
{
    ins = p != nullptr ? p->getTotalNumInputChannels() : 2;
    outs = p != nullptr ? p->getTotalNumOutputChannels() : 2;
    scratch.setSize (juce::jmax (2, ins, outs), juce::jmax (maxBlock, mh::chunk), false, true, false);
    const size_t n = p != nullptr ? (size_t) p->getParameters().size() : 0;
    base.assign (n, 0.0f); last.assign (n, -1.0f); was.assign (n, 0); offs.assign (n, 0.0f);
    act.clear(); act.reserve (mh::numMods * mh::numTargets);
    prev.clear(); prev.reserve (mh::numMods * mh::numTargets);
}

void ModHostProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    maxBlock = juce::jmax (samplesPerBlock, mh::chunk);
    for (int m = 0; m < mh::numMods; ++m)
    {
        mods[m].reset();
        for (int t = 0; t < mh::numTargets; ++t)
            amountSmooth[m][t].reset (sampleRate, 0.03), amountSmooth[m][t].setCurrentAndTargetValue (amountParam[m][t]->load() * 0.01f);
    }

    const juce::ScopedLock sl (hostLock);
    if (hosted != nullptr) prepareHosted (*hosted);
    resizeFor (hosted.get(), scratch, hostIns, hostOuts, maxBlock, baseVal, lastSet, wasActive, activeList, prevActive, offsets);
    setLatencySamples (hosted != nullptr ? hosted->getLatencySamples() : 0);
}

void ModHostProcessor::releaseResources()
{
    const juce::ScopedLock sl (hostLock);
    if (hosted != nullptr) hosted->releaseResources();
}

// ───────────────────────── 音の処理 ─────────────────────────

mh::Settings ModHostProcessor::readSettings (int m) const
{
    mh::Settings s;
    s.type = (int) typeParam[m]->load();
    s.shape = (int) shapeParam[m]->load();
    s.sync = syncParam[m]->load() > 0.5f;
    s.div = (int) divParam[m]->load();
    s.rateHz = rateParam[m]->load();
    s.attackMs = attackParam[m]->load();
    s.releaseMs = releaseParam[m]->load();
    s.gainDb = gainParam[m]->load();
    s.smooth = smoothParam[m]->load() * 0.01f;
    s.steps = (int) stepsParam[m]->load();
    s.bipolar = bipolarParam[m]->load() > 0.5f;
    s.phase = phaseParam[m]->load() / 360.0f;
    return s;
}

void ModHostProcessor::processHosted (juce::AudioBuffer<float>& main, int start, int n)
{
    const int mainCh = main.getNumChannels();
    const int sc = scratch.getNumChannels();
    for (int c = 0; c < sc; ++c)
    {
        if (c < hostIns && mainCh > 0) scratch.copyFrom (c, 0, main, juce::jmin (c, mainCh - 1), start, n);
        else scratch.clear (c, 0, n);
    }
    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), sc, n);
    juce::MidiBuffer midi;
    hosted->processBlock (view, midi);
    for (int c = 0; c < mainCh; ++c)
    {
        if (hostOuts <= 0) break;
        main.copyFrom (c, start, scratch, juce::jmin (c, hostOuts - 1), 0, n);
    }
}

void ModHostProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const bool haveSide = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    const int scFirst = haveSide ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : 0;
    const int scNum = haveSide ? getChannelCountOfBus (true, 1) : 0;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, numSamples);

    // 曲の位置
    bool playing = false; double ppq = 0, bpm = 120, barQ = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) barQ = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
            if (auto q = pos->getPpqPosition()) { ppq = *q; playing = pos->getIsPlaying(); }
        }

    mh::Settings set[mh::numMods];
    float steps[mh::numMods][mh::maxSteps];
    for (int m = 0; m < mh::numMods; ++m)
    {
        set[m] = readSettings (m);
        for (int k = 0; k < mh::maxSteps; ++k) steps[m][k] = stepVals[m][k].load();
        for (int t = 0; t < mh::numTargets; ++t) amountSmooth[m][t].setTargetValue (amountParam[m][t]->load() * 0.01f);
    }

    const juce::ScopedTryLock tl (hostLock);
    auto* p = tl.isLocked() ? hosted.get() : nullptr;
    const auto* params = p != nullptr ? &p->getParameters() : nullptr;
    const int nParams = params != nullptr ? juce::jmin (params->size(), (int) baseVal.size()) : 0;
    if (p != nullptr) p->setPlayHead (getPlayHead());

    bool anyRoute = false;
    for (int m = 0; m < mh::numMods && ! anyRoute; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); idx >= 0 && idx < nParams) { anyRoute = true; break; }

    // つながっている間は 64 サンプルごと、そうでなければまとめて
    const int maxPiece = p != nullptr ? juce::jmin (anyRoute ? mh::chunk : maxBlock, scratch.getNumSamples()) : mh::chunk;
    float vals[mh::numMods] {};

    // Plugin Link の値（LINK 型の MOD が使う）: Fader Pilot のフェーダー位置（-12〜+12 dB → 0〜1）、
    // いなければ Balance Scope のオケの音量（-60〜0 dB → 0〜1）
    float linkValue = 0.0f; int linkFrom = 0;
    if (link.on())
    {
        plink::Peer lp;
        if (link.ep().find (plink::kindFaderPilot, lp) && lp.valuesAgeMs < 1500)
        { linkValue = juce::jlimit (0.0f, 1.0f, (lp.v[plink::fp::rideDb] + 12.0f) / 24.0f); linkFrom = 1; }
        else if (link.ep().find (plink::kindBalanceScope, lp) && lp.valuesAgeMs < 1500)
        { linkValue = juce::jlimit (0.0f, 1.0f, (lp.v[plink::bs::musicDb] + 60.0f) / 60.0f); linkFrom = 2; }
    }
    linkValueNow = linkValue; linkSourceNow = linkFrom;

    for (int start = 0; start < numSamples; start += maxPiece)
    {
        const int n = juce::jmin (maxPiece, numSamples - start);

        // 変調の値（ENV 系の入力は区切りごとの最大値）
        for (int done = 0; done < n; done += mh::chunk)
        {
            const int k = juce::jmin (mh::chunk, n - done);
            mh::Block b;
            b.numSamples = k; b.sampleRate = rate; b.playing = playing; b.bpm = bpm; b.barQuarters = barQ;
            b.ppq = ppq + (start + done) / rate * bpm / 60.0;
            for (int c = 0; c < main.getNumChannels(); ++c) b.inputPeak = juce::jmax (b.inputPeak, main.getMagnitude (c, start + done, k));
            if (haveSide)
                for (int c = 0; c < scNum && scFirst + c < buffer.getNumChannels(); ++c)
                    b.sidechainPeak = juce::jmax (b.sidechainPeak, buffer.getMagnitude (scFirst + c, start + done, k));
            b.linkValue = linkValue;
            for (int m = 0; m < mh::numMods; ++m) vals[m] = mods[m].process (set[m], b, steps[m]);
        }

        if (p == nullptr) continue;

        // 各つまみに足す量
        activeList.clear();
        for (int m = 0; m < mh::numMods; ++m)
            for (int t = 0; t < mh::numTargets; ++t)
            {
                const float amt = amountSmooth[m][t].skip (n);
                const int idx = targets[m][t].load();
                if (idx < 0 || idx >= nParams) continue;
                if (std::find (activeList.begin(), activeList.end(), idx) == activeList.end())
                {
                    if (activeList.size() >= activeList.capacity()) continue;   // 確保し直さない
                    activeList.push_back (idx);
                    offsets[(size_t) idx] = 0.0f;
                }
                offsets[(size_t) idx] += amt * vals[m];
            }

        // つながりが外れたつまみは元の位置に戻す
        for (int idx : prevActive)
            if (idx < nParams && std::find (activeList.begin(), activeList.end(), idx) == activeList.end())
            {
                (*params)[idx]->setValue (baseVal[(size_t) idx]);
                wasActive[(size_t) idx] = 0;
            }

        for (int idx : activeList)
        {
            auto* prm = (*params)[idx];
            const float cur = prm->getValue();
            // こちらが最後に入れた値と違う＝その間に人がつまみを動かした → そこを新しい元の位置に
            if (! wasActive[(size_t) idx] || std::abs (cur - lastSet[(size_t) idx]) > 1.0e-5f)
                baseVal[(size_t) idx] = cur;
            const float nv = mh::applyOffset (baseVal[(size_t) idx], offsets[(size_t) idx]);
            if (std::abs (nv - cur) > 1.0e-6f) prm->setValue (nv);
            lastSet[(size_t) idx] = nv;
            wasActive[(size_t) idx] = 1;
        }
        prevActive.assign (activeList.begin(), activeList.end());

        processHosted (main, start, n);
    }

    for (int m = 0; m < mh::numMods; ++m)
    {
        modNow[m] = vals[m];
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); p != nullptr && idx >= 0 && idx < nParams && wasActive[(size_t) idx])
            {
                shownBase[m][t] = baseVal[(size_t) idx];
                shownNow[m][t] = lastSet[(size_t) idx];
            }
    }
}

// ───────────────────────── 読み込み・入れ替え ─────────────────────────

std::vector<mh::AuEntry> ModHostProcessor::scanEffects() const { return mh::listAuEffects (selfCode, selfManu); }

void ModHostProcessor::loadPlugin (const mh::AuEntry& e)
{
    juce::PluginDescription d;
    d.name = juce::String::fromUTF8 (e.name.c_str());
    d.descriptiveName = d.name;
    d.manufacturerName = juce::String::fromUTF8 (e.manufacturer.c_str());
    d.pluginFormatName = "AudioUnit";
    d.fileOrIdentifier = juce::String::fromUTF8 (e.identifier.c_str());
    d.category = "Effect";
    d.isInstrument = false;
    loadFromDescription (d, nullptr);
}

namespace { std::atomic<int> loadGeneration { 0 }; }

void ModHostProcessor::loadFromDescription (const juce::PluginDescription& d, std::unique_ptr<juce::XmlElement> restore)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    loading = true;
    loadError.clear();
    hostedDesc = d;
    const int gen = ++loadGeneration;
    hostListeners.call ([] (Listener& l) { l.hostedPluginChanged(); });   // 「読み込み中」の表示

    std::shared_ptr<juce::XmlElement> keep (restore.release());
    auto alive = aliveFlag;
    formats.createPluginInstanceAsync (d, rate, maxBlock,
        [this, alive, gen, keep] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            if (! *alive || gen != loadGeneration.load()) return;   // もう消えた・次の読み込みが始まった
            std::unique_ptr<juce::XmlElement> r (keep != nullptr ? new juce::XmlElement (*keep) : nullptr);
            finishLoad (std::move (inst), err, std::move (r));
        });
}

// 保存してあった中のプラグインの中身とつなぎ先を入れる
static void restoreInto (juce::AudioPluginInstance& p, const juce::XmlElement& x,
                         std::atomic<int> (&targets)[mh::numMods][mh::numTargets],
                         std::atomic<float> (&shownBase)[mh::numMods][mh::numTargets])
{
    if (auto* ps = x.getChildByName ("PSTATE"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (ps->getAllSubText().trim()) && mb.getSize() > 0)
            p.setStateInformation (mb.getData(), (int) mb.getSize());
    }
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t) targets[m][t] = -1;

    const auto& params = p.getParameters();
    if (auto* rs = x.getChildByName ("ROUTES"))
        for (auto* r : rs->getChildIterator())
        {
            const int m = r->getIntAttribute ("m", -1), t = r->getIntAttribute ("t", -1);
            if (m < 0 || m >= mh::numMods || t < 0 || t >= mh::numTargets) continue;
            int idx = r->getIntAttribute ("idx", -1);
            const auto name = r->getStringAttribute ("name");
            // 番号が変わっていたら名前で探す
            if (idx < 0 || idx >= params.size() || (name.isNotEmpty() && params[idx]->getName (128) != name))
            {
                idx = -1;
                for (int i = 0; i < params.size(); ++i)
                    if (params[i]->getName (128) == name) { idx = i; break; }
            }
            if (idx < 0) continue;
            const float base = (float) r->getDoubleAttribute ("base", params[idx]->getValue());
            params[idx]->setValue (juce::jlimit (0.0f, 1.0f, base));   // 保存の中身は動いている途中の値なので、元の位置に戻す
            shownBase[m][t] = base;
            targets[m][t] = idx;
        }
}

void ModHostProcessor::finishLoad (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err,
                                   std::unique_ptr<juce::XmlElement> restore)
{
    loading = false;
    if (inst == nullptr)
    {
        loadError = err.isNotEmpty() ? err : juce::String ("could not open");
        hostListeners.call ([] (Listener& l) { l.hostedPluginChanged(); });
        return;   // pendingState は残す（プラグインが無い Mac で開いても、保存し直して中身が消えないように）
    }

    prepareHosted (*inst);
    if (restore != nullptr) restoreInto (*inst, *restore, targets, shownBase);
    else
        for (int m = 0; m < mh::numMods; ++m)
            for (int t = 0; t < mh::numTargets; ++t) targets[m][t] = -1;

    installHosted (std::move (inst));
    const juce::ScopedLock sl (stateLock);
    pendingState.reset();
}

void ModHostProcessor::installHosted (std::unique_ptr<juce::AudioPluginInstance> inst)
{
    hostListeners.call ([] (Listener& l) { l.hostedPluginAboutToChange(); });   // 画面が中の画面を消す
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock s1 (stateLock);
        const juce::ScopedLock s2 (hostLock);
        old = std::move (hosted);
        hosted = std::move (inst);
        resizeFor (hosted.get(), scratch, hostIns, hostOuts, maxBlock, baseVal, lastSet, wasActive, activeList, prevActive, offsets);
    }
    if (old != nullptr) { old->removeListener (this); old->releaseResources(); old.reset(); }
    if (hosted != nullptr) hosted->addListener (this);

    setLatencySamples (hosted != nullptr ? hosted->getLatencySamples() : 0);
    tailSeconds = hosted != nullptr ? hosted->getTailLengthSeconds() : 0.0;
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    hostListeners.call ([] (Listener& l) { l.hostedPluginChanged(); });
}

void ModHostProcessor::unloadPlugin()
{
    ++loadGeneration;
    loading = false;
    loadError.clear();
    hostedDesc = {};
    {
        const juce::ScopedLock sl (stateLock);
        pendingState.reset();
    }
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t) targets[m][t] = -1;
    installHosted (nullptr);
}

// ───────────────────────── つなぐ先 ─────────────────────────

int ModHostProcessor::numHostedParams() const { return hosted != nullptr ? hosted->getParameters().size() : 0; }

juce::String ModHostProcessor::paramName (int i) const
{
    if (hosted == nullptr || i < 0 || i >= hosted->getParameters().size()) return {};
    return hosted->getParameters()[i]->getName (64);
}

void ModHostProcessor::setTarget (int m, int t, int idx)
{
    if (hosted == nullptr || idx < 0 || idx >= hosted->getParameters().size()) idx = -1;
    if (idx >= 0)
    {
        const float v = hosted->getParameters()[idx]->getValue();
        shownBase[m][t] = v; shownNow[m][t] = v;
        // AMOUNT が 0 のままだと何も起きないので、最初は +50 % に
        if (std::abs (amountParam[m][t]->load()) < 0.05f && juce::MessageManager::existsAndIsCurrentThread())
            if (auto* a = apvts.getParameter (amountId (m, t)))
                a->setValueNotifyingHost (a->convertTo0to1 (50.0f));
    }
    targets[m][t] = idx;
}

void ModHostProcessor::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float)
{
    // LEARN 中に、中のプラグインのつまみが（人の手で）動いた
    int slot = learnSlot.load();
    if (slot < 0) return;
    if (! learnSlot.compare_exchange_strong (slot, -1)) return;
    learnedSlot = slot; learnedIndex = index;
    triggerAsyncUpdate();
}

void ModHostProcessor::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&)
{
    hostChanged = true;
    triggerAsyncUpdate();
}

void ModHostProcessor::handleAsyncUpdate()
{
    if (const int slot = learnedSlot.exchange (-1); slot >= 0)
        setTarget (slot / mh::numTargets, slot % mh::numTargets, learnedIndex.load());

    if (hostChanged.exchange (false) && hosted != nullptr)
    {
        {
            const juce::ScopedLock sl (hostLock);
            if ((int) baseVal.size() != hosted->getParameters().size())
                resizeFor (hosted.get(), scratch, hostIns, hostOuts, maxBlock, baseVal, lastSet, wasActive, activeList, prevActive, offsets);
        }
        setLatencySamples (hosted->getLatencySamples());
        tailSeconds = hosted->getTailLengthSeconds();
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }

    if (restorePending.exchange (false))
    {
        std::unique_ptr<juce::XmlElement> x;
        {
            const juce::ScopedLock sl (stateLock);
            if (pendingState != nullptr) x = std::make_unique<juce::XmlElement> (*pendingState);
        }
        if (x == nullptr || x->getChildByName ("PLUGIN") == nullptr) { if (hosted != nullptr || loading) unloadPlugin(); return; }

        juce::PluginDescription d;
        if (auto* pd = x->getChildByName ("PLUGIN")->getFirstChildElement()) d.loadFromXml (*pd);

        if (hosted != nullptr && ! loading && d.fileOrIdentifier == hostedDesc.fileOrIdentifier)
        {
            // 同じプラグインがもう入っている → 中身だけ入れ直す
            restoreInto (*hosted, *x, targets, shownBase);
            const juce::ScopedLock sl (stateLock);
            pendingState.reset();
            hostListeners.call ([] (Listener& l) { l.hostedPluginChanged(); });
        }
        else loadFromDescription (d, std::move (x));
    }
}

// ───────────────────────── 保存 ─────────────────────────

std::unique_ptr<juce::XmlElement> ModHostProcessor::routesToXml() const
{
    auto r = std::make_unique<juce::XmlElement> ("ROUTES");
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); idx >= 0)
            {
                auto* e = r->createNewChildElement ("R");
                e->setAttribute ("m", m);
                e->setAttribute ("t", t);
                e->setAttribute ("idx", idx);
                e->setAttribute ("name", paramName (idx));
                e->setAttribute ("base", (double) shownBase[m][t].load());
            }
    return r;
}

void ModHostProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("MODHOST");
    root.setAttribute ("version", JucePlugin_VersionString);
    if (auto x = apvts.copyState().createXml()) root.addChildElement (x.release());

    auto* st = root.createNewChildElement ("STEPS");
    for (int m = 0; m < mh::numMods; ++m)
    {
        juce::StringArray a;
        for (int k = 0; k < mh::maxSteps; ++k) a.add (juce::String (stepVals[m][k].load(), 3));
        st->createNewChildElement ("M")->setAttribute ("v", a.joinIntoString (","));
    }

    {
        const juce::ScopedLock sl (stateLock);
        if (pendingState != nullptr)
        {
            // まだ読み込み中（または、この Mac にそのプラグインが無い）→ 受け取ったままを返す
            for (auto* name : { "PLUGIN", "PSTATE", "ROUTES" })
                if (auto* c = pendingState->getChildByName (name)) root.addChildElement (new juce::XmlElement (*c));
        }
        else if (hosted != nullptr)
        {
            root.createNewChildElement ("PLUGIN")->addChildElement (hostedDesc.createXml().release());
            juce::MemoryBlock mb;
            hosted->getStateInformation (mb);
            root.createNewChildElement ("PSTATE")->addTextElement (mb.toBase64Encoding());
            root.addChildElement (routesToXml().release());
        }
    }
    copyXmlToBinary (root, destData);
}

void ModHostProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("MODHOST")) return;

    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    if (auto* st = xml->getChildByName ("STEPS"))
    {
        int m = 0;
        for (auto* e : st->getChildIterator())
        {
            if (m >= mh::numMods) break;
            juce::StringArray a; a.addTokens (e->getStringAttribute ("v"), ",", {});
            for (int k = 0; k < mh::maxSteps && k < a.size(); ++k) setStepValue (m, k, a[k].getFloatValue());
            ++m;
        }
    }

    {
        const juce::ScopedLock sl (stateLock);
        pendingState = std::make_unique<juce::XmlElement> ("PENDING");
        for (auto* name : { "PLUGIN", "PSTATE", "ROUTES" })
            if (auto* c = xml->getChildByName (name)) pendingState->addChildElement (new juce::XmlElement (*c));
        if (pendingState->getChildByName ("PLUGIN") == nullptr) pendingState.reset();
    }
    // 中のプラグインを開くのは画面のスレッドで
    restorePending = true;
    triggerAsyncUpdate();
}

juce::AudioProcessorEditor* ModHostProcessor::createEditor() { return new ModHostEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ModHostProcessor(); }
