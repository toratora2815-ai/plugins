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
    for (int k = 0; k < numInternal; ++k)
    {
        internalParam[k] = apvts.getParameter (internalId (k));
        internalShown[k] = internalParam[k]->getValue();
    }
    bandsParam = apvts.getRawParameterValue ("bands");
    autoGainParam = apvts.getRawParameterValue ("autoGain");

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
    // 通り道（DRIVE → 中のプラグイン → MIX → AUTO GAIN → OUT）と帯域分け・MORPH
    addF ("drive", "Drive", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    addF ("mix",   "Mix",   { 0.0f, 100.0f, 0.1f }, 100.0f, upct);
    addF ("out",   "Output", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    addF ("morph", "Morph", { 0.0f, 100.0f, 0.1f }, 0.0f, upct);
    addF ("sendLow",  "Send Low",  { 0.0f, 100.0f, 0.1f }, 100.0f, upct);
    addF ("sendMid",  "Send Mid",  { 0.0f, 100.0f, 0.1f }, 100.0f, upct);
    addF ("sendHigh", "Send High", { 0.0f, 100.0f, 0.1f }, 100.0f, upct);
    auto hzText = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    addF ("splitLow",  "Split Low",  skewed (40.0f, 2000.0f, 1.0f, 250.0f), 200.0f, hzText);
    addF ("splitHigh", "Split High", skewed (500.0f, 16000.0f, 1.0f, 3000.0f), 3000.0f, hzText);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoGain", 1 }, "Auto Gain", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bands", 1 }, "Bands", false));
    return { v.begin(), v.end() };
}

const char* ModHostProcessor::internalId (int k)
{
    static const char* ids[numInternal] = { "drive", "mix", "out", "morph", "sendLow", "sendMid", "sendHigh", "splitLow", "splitHigh" };
    return ids[juce::jlimit (0, numInternal - 1, k)];
}

juce::String ModHostProcessor::internalName (int k)
{
    static const char* n[numInternal] = { "DRIVE", "MIX", "OUT", "MORPH", "SEND LOW", "SEND MID", "SEND HIGH", "SPLIT LOW", "SPLIT HIGH" };
    return juce::String::fromUTF8 ("◆ Mod Host: ") + n[juce::jlimit (0, numInternal - 1, k)];
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

    maxDelay = (int) (sampleRate * 2.0);
    dUn.prepare (maxDelay); dSent.prepare (maxDelay); dRaw.prepare (maxDelay);
    xover.reset();
    autoGain.prepare (sampleRate, mh::chunk);
    analyzer.setSampleRate (sampleRate);
    work.setSize (6, maxBlock, false, true, false);
    rampDrive = rampMix = rampOut = rampAg = -1.0f;   // 下で今の値に

    const juce::ScopedLock sl (hostLock);
    if (hosted != nullptr) prepareHosted (*hosted);
    resizeFor (hosted.get(), scratch, hostIns, hostOuts, maxBlock, baseVal, lastSet, wasActive, activeList, prevActive, offsets);
    rebuildMorphLocked();
    {
        auto val = [this] (int k) { return internalParam[k]->convertFrom0to1 (internalParam[k]->getValue()); };
        rampDrive = juce::Decibels::decibelsToGain (val (inDrive)); rampMix = val (inMix) * 0.01f; rampOut = juce::Decibels::decibelsToGain (val (inOut));
        rampAg = 1.0f;
        for (int k = 0; k < 3; ++k) rampSend[k] = val (inSendLow + k) * 0.01f;
    }
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

    bool anyRoute = morphIdx.size() > 0 && p != nullptr;
    for (int m = 0; m < mh::numMods && ! anyRoute; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); (idx >= 0 && idx < nParams) || isInternal (idx)) { anyRoute = true; break; }

    // つながっている間は 64 サンプルごと、そうでなければまとめて
    const int maxPiece = juce::jmin (anyRoute ? mh::chunk : maxBlock, scratch.getNumSamples(), work.getNumSamples());
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

    const bool bandsOn = bandsParam->load() > 0.5f;
    const bool autoOn = autoGainParam->load() > 0.5f;
    const int latency = p != nullptr ? juce::jlimit (0, maxDelay, p->getLatencySamples()) : 0;
    const int mainCh = main.getNumChannels();
    float eff[numInternal] {};

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
            for (int c = 0; c < mainCh; ++c) b.inputPeak = juce::jmax (b.inputPeak, main.getMagnitude (c, start + done, k));
            if (haveSide)
                for (int c = 0; c < scNum && scFirst + c < buffer.getNumChannels(); ++c)
                    b.sidechainPeak = juce::jmax (b.sidechainPeak, buffer.getMagnitude (scFirst + c, start + done, k));
            b.linkValue = linkValue;
            for (int m = 0; m < mh::numMods; ++m) vals[m] = mods[m].process (set[m], b, steps[m]);
        }

        // 各つまみに足す量（中のプラグインのつまみ / Mod Host 自身のつまみ）
        float ioff[numInternal] {};
        activeList.clear();
        for (int m = 0; m < mh::numMods; ++m)
            for (int t = 0; t < mh::numTargets; ++t)
            {
                const float amt = amountSmooth[m][t].skip (n);
                const int idx = targets[m][t].load();
                if (isInternal (idx)) { ioff[idx - internalBase] += amt * vals[m]; continue; }
                if (p == nullptr || idx < 0 || idx >= nParams) continue;
                if (std::find (activeList.begin(), activeList.end(), idx) == activeList.end())
                {
                    if (activeList.size() >= activeList.capacity()) continue;   // 確保し直さない
                    activeList.push_back (idx);
                    offsets[(size_t) idx] = 0.0f;
                }
                offsets[(size_t) idx] += amt * vals[m];
            }
        for (int k = 0; k < numInternal; ++k)
        {
            eff[k] = juce::jlimit (0.0f, 1.0f, internalParam[k]->getValue() + ioff[k]);
            internalShown[k] = eff[k];
        }

        if (p != nullptr)
        {
            // MORPH: A と B の間の値を計算（つないでいるつまみは、その値を「元の位置」にする）
            const float mo = eff[inMorph];
            for (size_t j = 0; j < morphIdx.size(); ++j)
            {
                const int idx = morphIdx[j];
                if (idx >= nParams) continue;
                const float v = mh::morphValue (morphA[j], morphB[j], mo, morphDisc[j] != 0);
                morphCur[(size_t) idx] = v;
                if (std::find (activeList.begin(), activeList.end(), idx) != activeList.end()) continue;
                if (std::abs (v - morphLast[(size_t) idx]) > 1.0e-4f) { (*params)[idx]->setValue (v); morphLast[(size_t) idx] = v; }
            }

            // つながりが外れたつまみは元の位置に戻す
            for (int idx : prevActive)
                if (idx < nParams && std::find (activeList.begin(), activeList.end(), idx) == activeList.end())
                {
                    (*params)[idx]->setValue (baseVal[(size_t) idx]);
                    wasActive[(size_t) idx] = 0;
                    morphLast[(size_t) idx] = -1.0f;
                }

            for (int idx : activeList)
            {
                auto* prm = (*params)[idx];
                const float cur = prm->getValue();
                if (morphDriven[(size_t) idx]) baseVal[(size_t) idx] = morphCur[(size_t) idx];
                // こちらが最後に入れた値と違う＝その間に人がつまみを動かした → そこを新しい元の位置に
                else if (! wasActive[(size_t) idx] || std::abs (cur - lastSet[(size_t) idx]) > 1.0e-5f)
                    baseVal[(size_t) idx] = cur;
                const float nv = mh::applyOffset (baseVal[(size_t) idx], offsets[(size_t) idx]);
                if (std::abs (nv - cur) > 1.0e-6f) prm->setValue (nv);
                lastSet[(size_t) idx] = nv;
                wasActive[(size_t) idx] = 1;
            }
            prevActive.assign (activeList.begin(), activeList.end());
        }

        // ── 音の通り道 ──
        float now[numInternal];
        for (int k = 0; k < numInternal; ++k) now[k] = internalParam[k]->convertFrom0to1 (eff[k]);
        const float drive1 = juce::Decibels::decibelsToGain (now[inDrive]), mix1 = now[inMix] * 0.01f, out1 = juce::Decibels::decibelsToGain (now[inOut]);
        const float send1[3] = { now[inSendLow] * 0.01f, now[inSendMid] * 0.01f, now[inSendHigh] * 0.01f };
        if (bandsOn) xover.set (now[inSplitLow], now[inSplitHigh], (float) rate);

        float* un[2]   = { work.getWritePointer (0), work.getWritePointer (1) };
        float* sent[2] = { work.getWritePointer (2), work.getWritePointer (3) };
        float* raw[2]  = { work.getWritePointer (4), work.getWritePointer (5) };
        const int scCh = scratch.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            const float r = (float) (i + 1) / (float) n;
            const float drive = rampDrive + (drive1 - rampDrive) * r;
            float s3[3];
            for (int k = 0; k < 3; ++k) s3[k] = rampSend[k] + (send1[k] - rampSend[k]) * r;
            for (int c = 0; c < 2; ++c)
            {
                const float x = main.getSample (juce::jmin (c, mainCh - 1), start + i);
                float sv = x, uv = 0.0f;
                if (bandsOn)
                {
                    float lo, mi, hi;
                    xover.process (c, x, lo, mi, hi);
                    sv = s3[0] * lo + s3[1] * mi + s3[2] * hi;
                    uv = (lo + mi + hi) - sv;
                }
                un[c][i] = uv; sent[c][i] = sv; raw[c][i] = x;
            }
            // 中のプラグインへ（モノラルのプラグインなら L+R）
            if (hostIns == 1) scratch.setSample (0, i, 0.5f * (sent[0][i] + sent[1][i]) * drive);
            else for (int c = 0; c < 2 && c < scCh; ++c) scratch.setSample (c, i, sent[c][i] * drive);
        }
        for (int c = (hostIns == 1 ? 1 : 2); c < scCh; ++c) scratch.clear (c, 0, n);

        if (p != nullptr)
        {
            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), scCh, n);
            juce::MidiBuffer midi;
            p->processBlock (view, midi);
        }

        const float* wet[2] = { scratch.getReadPointer (0), scratch.getReadPointer (p != nullptr && hostOuts == 1 ? 0 : juce::jmin (1, scCh - 1)) };
        if (p == nullptr && hostIns == 1) wet[1] = wet[0];
        const float agTarget = autoOn ? autoGain.gain : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float r = (float) (i + 1) / (float) n;
            const float mix = rampMix + (mix1 - rampMix) * r;
            const float outG = rampOut + (out1 - rampOut) * r;
            const float ag = rampAg + (agTarget - rampAg) * r;
            float uL = un[0][i], uR = un[1][i], sL = sent[0][i], sR = sent[1][i], xL = raw[0][i], xR = raw[1][i];
            dUn.process (uL, uR, latency);
            dSent.process (sL, sR, latency);
            dRaw.process (xL, xR, latency);
            float yL = uL + (1.0f - mix) * sL + mix * wet[0][i];
            float yR = uR + (1.0f - mix) * sR + mix * wet[1][i];
            autoGain.measure (xL, xR, yL, yR);
            yL *= ag * outG; yR *= ag * outG;
            analyzer.push (xL, xR, yL, yR);
            main.setSample (0, start + i, mainCh > 1 ? yL : 0.5f * (yL + yR));
            if (mainCh > 1) main.setSample (1, start + i, yR);
        }
        autoGain.update();
        rampDrive = drive1; rampMix = mix1; rampOut = out1; rampAg = agTarget;
        for (int k = 0; k < 3; ++k) rampSend[k] = send1[k];
    }
    autoGainShown = rampAg;

    for (int m = 0; m < mh::numMods; ++m)
    {
        modNow[m] = vals[m];
        for (int t = 0; t < mh::numTargets; ++t)
        {
            const int idx = targets[m][t].load();
            if (isInternal (idx))
            {
                shownBase[m][t] = internalParam[idx - internalBase]->getValue();
                shownNow[m][t] = eff[idx - internalBase];
            }
            else if (p != nullptr && idx >= 0 && idx < nParams && wasActive[(size_t) idx])
            {
                shownBase[m][t] = baseVal[(size_t) idx];
                shownNow[m][t] = lastSet[(size_t) idx];
            }
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
        for (int t = 0; t < mh::numTargets; ++t)
            if (! ModHostProcessor::isInternal (targets[m][t].load())) targets[m][t] = -1;

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
    else clearHostedTargets();

    hasA = hasB = false; storedA.clear(); storedB.clear();
    installHosted (std::move (inst));
    if (restore != nullptr) restoreMorph (*restore);
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
        rebuildMorphLocked();
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
    clearHostedTargets();
    hasA = hasB = false; storedA.clear(); storedB.clear();
    installHosted (nullptr);
}

void ModHostProcessor::clearHostedTargets()
{
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (! isInternal (targets[m][t].load())) targets[m][t] = -1;
}

// ───────────────────────── MORPH ─────────────────────────

void ModHostProcessor::storeMorph (int which)
{
    if (hosted == nullptr) return;
    const auto& params = hosted->getParameters();
    std::vector<float> v ((size_t) params.size());
    for (int i = 0; i < params.size(); ++i) v[(size_t) i] = params[i]->getValue();
    // MOD でつないでいるつまみは、動いている途中ではなく元の位置を覚える
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); idx >= 0 && idx < params.size()) v[(size_t) idx] = shownBase[m][t].load();
    (which == 0 ? storedA : storedB) = std::move (v);
    (which == 0 ? hasA : hasB) = true;
    {
        const juce::ScopedLock sl (hostLock);
        rebuildMorphLocked();
    }
    // 覚えた直後に音が飛ばないように、MORPH をいま覚えた側に置く
    if (hasA && hasB)
        if (auto* mp = apvts.getParameter ("morph")) mp->setValueNotifyingHost (which == 0 ? 0.0f : 1.0f);
}

void ModHostProcessor::clearMorph()
{
    hasA = hasB = false; storedA.clear(); storedB.clear();
    const juce::ScopedLock sl (hostLock);
    rebuildMorphLocked();
}

void ModHostProcessor::rebuildMorphLocked()
{
    morphIdx.clear(); morphA.clear(); morphB.clear(); morphDisc.clear();
    const size_t n = baseVal.size();
    morphDriven.assign (n, 0); morphCur.assign (n, 0.0f); morphLast.assign (n, -1.0f);
    morphIdx.reserve (n); morphA.reserve (n); morphB.reserve (n); morphDisc.reserve (n);
    if (hosted != nullptr && hasA && hasB)
    {
        const auto& params = hosted->getParameters();
        const size_t m = std::min ({ n, storedA.size(), storedB.size(), (size_t) params.size() });
        for (size_t i = 0; i < m; ++i)
            if (std::abs (storedA[i] - storedB[i]) > 1.0e-5f)
            {
                morphIdx.push_back ((int) i);
                morphA.push_back (storedA[i]); morphB.push_back (storedB[i]);
                auto* prm = params[(int) i];
                morphDisc.push_back (prm->isDiscrete() || prm->isBoolean() ? 1 : 0);
                morphDriven[i] = 1;
            }
    }
    morphCount = (int) morphIdx.size();
}

static juce::String floatsToString (const std::vector<float>& v)
{
    juce::StringArray a;
    for (float x : v) a.add (juce::String (x, 5));
    return a.joinIntoString (",");
}

std::unique_ptr<juce::XmlElement> ModHostProcessor::morphToXml() const
{
    auto e = std::make_unique<juce::XmlElement> ("MORPH");
    if (hasA) e->setAttribute ("a", floatsToString (storedA));
    if (hasB) e->setAttribute ("b", floatsToString (storedB));
    return e;
}

void ModHostProcessor::restoreMorph (const juce::XmlElement& x)
{
    hasA = hasB = false; storedA.clear(); storedB.clear();
    if (auto* e = x.getChildByName ("MORPH"))
    {
        auto parse = [] (const juce::String& s, std::vector<float>& out)
        {
            juce::StringArray a; a.addTokens (s, ",", {});
            out.clear();
            for (auto& t : a) out.push_back (t.getFloatValue());
            return ! out.empty();
        };
        hasA = e->hasAttribute ("a") && parse (e->getStringAttribute ("a"), storedA);
        hasB = e->hasAttribute ("b") && parse (e->getStringAttribute ("b"), storedB);
    }
    const juce::ScopedLock sl (hostLock);
    rebuildMorphLocked();
}

// ───────────────────────── つなぐ先 ─────────────────────────

int ModHostProcessor::numHostedParams() const { return hosted != nullptr ? hosted->getParameters().size() : 0; }

juce::String ModHostProcessor::paramName (int i) const
{
    if (isInternal (i)) return internalName (i - internalBase);
    if (hosted == nullptr || i < 0 || i >= hosted->getParameters().size()) return {};
    return hosted->getParameters()[i]->getName (64);
}

void ModHostProcessor::setTarget (int m, int t, int idx)
{
    if (! isInternal (idx) && (hosted == nullptr || idx < 0 || idx >= hosted->getParameters().size())) idx = -1;
    if (idx >= 0)
    {
        const float v = isInternal (idx) ? internalParam[idx - internalBase]->getValue() : hosted->getParameters()[idx]->getValue();
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
            {
                resizeFor (hosted.get(), scratch, hostIns, hostOuts, maxBlock, baseVal, lastSet, wasActive, activeList, prevActive, offsets);
                rebuildMorphLocked();
            }
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
            restoreMorph (*x);
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
            if (const int idx = targets[m][t].load(); idx >= 0 && ! isInternal (idx))
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

    // Mod Host 自身のつまみへのつなぎ（中のプラグインがなくても残す）
    auto* ir = root.createNewChildElement ("IROUTES");
    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (const int idx = targets[m][t].load(); isInternal (idx))
            {
                auto* e = ir->createNewChildElement ("R");
                e->setAttribute ("m", m); e->setAttribute ("t", t);
                e->setAttribute ("id", internalId (idx - internalBase));
            }

    {
        const juce::ScopedLock sl (stateLock);
        if (pendingState != nullptr)
        {
            // まだ読み込み中（または、この Mac にそのプラグインが無い）→ 受け取ったままを返す
            for (auto* name : { "PLUGIN", "PSTATE", "ROUTES", "MORPH" })
                if (auto* c = pendingState->getChildByName (name)) root.addChildElement (new juce::XmlElement (*c));
        }
        else if (hosted != nullptr)
        {
            root.createNewChildElement ("PLUGIN")->addChildElement (hostedDesc.createXml().release());
            juce::MemoryBlock mb;
            hosted->getStateInformation (mb);
            root.createNewChildElement ("PSTATE")->addTextElement (mb.toBase64Encoding());
            root.addChildElement (routesToXml().release());
            root.addChildElement (morphToXml().release());
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

    for (int m = 0; m < mh::numMods; ++m)
        for (int t = 0; t < mh::numTargets; ++t)
            if (isInternal (targets[m][t].load())) targets[m][t] = -1;
    if (auto* ir = xml->getChildByName ("IROUTES"))
        for (auto* r : ir->getChildIterator())
        {
            const int m = r->getIntAttribute ("m", -1), t = r->getIntAttribute ("t", -1);
            if (m < 0 || m >= mh::numMods || t < 0 || t >= mh::numTargets) continue;
            for (int k = 0; k < numInternal; ++k)
                if (r->getStringAttribute ("id") == internalId (k)) targets[m][t] = internalBase + k;
        }

    {
        const juce::ScopedLock sl (stateLock);
        pendingState = std::make_unique<juce::XmlElement> ("PENDING");
        for (auto* name : { "PLUGIN", "PSTATE", "ROUTES", "MORPH" })
            if (auto* c = xml->getChildByName (name)) pendingState->addChildElement (new juce::XmlElement (*c));
        if (pendingState->getChildByName ("PLUGIN") == nullptr) pendingState.reset();
    }
    // 中のプラグインを開くのは画面のスレッドで
    restorePending = true;
    triggerAsyncUpdate();
}

juce::AudioProcessorEditor* ModHostProcessor::createEditor() { return new ModHostEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ModHostProcessor(); }
