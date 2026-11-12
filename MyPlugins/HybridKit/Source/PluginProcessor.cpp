#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
}

HybridKitProcessor::HybridKitProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput (outName (0), juce::AudioChannelSet::stereo(), true)
                        .withOutput (outName (1), juce::AudioChannelSet::stereo(), false)
                        .withOutput (outName (2), juce::AudioChannelSet::stereo(), false)
                        .withOutput (outName (3), juce::AudioChannelSet::stereo(), false)
                        .withOutput (outName (4), juce::AudioChannelSet::stereo(), false)
                        .withOutput (outName (5), juce::AudioChannelSet::stereo(), false)
                        .withOutput (outName (6), juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pKit = apvts.getRawParameterValue ("kit");
    pClose = apvts.getRawParameterValue ("close");
    pOH = apvts.getRawParameterValue ("oh");
    pRoom = apvts.getRawParameterValue ("room");
    pBleed = apvts.getRawParameterValue ("bleed");
    pOutput = apvts.getRawParameterValue ("output");
    for (int g = 0; g < hk::NumGroups; ++g)
    {
        const juce::String id (groupId (g));
        pVol[(size_t) g] = apvts.getRawParameterValue (id + "Vol");
        pTune[(size_t) g] = apvts.getRawParameterValue (id + "Tune");
        pLen[(size_t) g] = apvts.getRawParameterValue (id + "Len");
    }
    pKickBlend = apvts.getRawParameterValue ("kickBlend");
    pKickSound = apvts.getRawParameterValue ("kickSound");
    pKickNote = apvts.getRawParameterValue ("kickNote");
    pSnareBlend = apvts.getRawParameterValue ("snareBlend");
    pSnareSound = apvts.getRawParameterValue ("snareSound");
    pSnareEdmTune = apvts.getRawParameterValue ("snareEdmTune");
    static const char* sepIds[] = { "sepKick", "sepSnare", "sepToms", "sepHiHat", "sepCym", "sepRoom" };
    for (size_t i = 0; i < pSep.size(); ++i)
    {
        pSep[i] = apvts.getRawParameterValue (sepIds[i]);
        myPresets.exclude (sepIds[i]);   // 出口は曲の配線なので、自分のプリセット・取り消しでは変えない
    }

    tmpL.assign ((size_t) hk::Engine::maxBlock, 0.0f);
    tmpR.assign ((size_t) hk::Engine::maxBlock, 0.0f);
    for (auto& b : ibuf) b.assign ((size_t) hk::Engine::maxBlock, 0.0f);
    engine.setPrefetcher (&prefetcher);
    engine.prepare (internalRate);
    for (auto& r : resamplers) r.setup (internalRate, 44100.0);
    prefetcher.start();
    loader.startThread (juce::Thread::Priority::low);
    lastKitParam = juce::roundToInt (pKit->load());
    requestKit (lastKitParam);
    startTimerHz (10);
}

HybridKitProcessor::~HybridKitProcessor()
{
    stopTimer();
    cancelLoad.store (true);
    loader.signalThreadShouldExit();
    loader.notify();
    loader.stopThread (8000);
    prefetcher.stop();
    logStream.reset();
}

const char* HybridKitProcessor::outName (int i)
{
    static const char* n[] = { "Main", "Kick", "Snare", "Room", "Toms", "Hi-Hat", "Cymbals" };   // hk::Engine::OutIndex と同じ並び
    return n[juce::jlimit (0, hk::Engine::maxOuts - 1, i)];
}

// Logic が出口を足すときも、決まった名前・ステレオで（JUCE の初期は「Output #N」になる）
bool HybridKitProcessor::canApplyBusCountChange (bool isInput, bool isAdding, BusProperties& out)
{
    if (isInput) return false;
    const int n = getBusCount (false);
    if (isAdding)
    {
        if (n >= hk::Engine::maxOuts) return false;
        out.busName = outName (n);
        out.defaultLayout = juce::AudioChannelSet::stereo();
        out.isActivatedByDefault = true;
        return true;
    }
    return n > 1;
}

const char* HybridKitProcessor::groupId (int g)
{
    static const char* n[] = { "kick", "snare", "toms", "hihat", "cym" };
    return n[juce::jlimit (0, hk::NumGroups - 1, g)];
}

const char* HybridKitProcessor::groupName (int g)
{
    static const char* n[] = { "KICK", "SNARE", "TOMS", "HI-HAT", "CYMBALS" };
    return n[juce::jlimit (0, hk::NumGroups - 1, g)];
}

juce::StringArray HybridKitProcessor::kitNames()
{
    juce::StringArray a;
    for (auto& n : hk::producerKitNames()) a.add (n);
    return a;
}

juce::AudioProcessorValueTreeState::ParameterLayout HybridKitProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    using C = juce::AudioParameterChoice;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto db = [] (float x, int) { return x <= -59.9f ? juce::String ("-inf") : (x > 0.05f ? "+" : "") + juce::String (x, 1) + " dB"; };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x)) + " %"; };
    auto semis = [] (float x, int) { return (x > 0.05f ? "+" : "") + juce::String (x, 1) + " st"; };
    auto dbRange = [] (float lo, float hi) { juce::NormalisableRange<float> r (lo, hi, 0.1f); r.setSkewForCentre (-9.0f); return r; };

    v.push_back (std::make_unique<C> (juce::ParameterID { "kit", 1 }, "Kit", kitNames(), 0));
    v.push_back (std::make_unique<F> (juce::ParameterID { "close", 1 }, "Close Mics", dbRange (-60.0f, 12.0f), 0.0f, A().withStringFromValueFunction (db)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "oh", 1 }, "Overheads", dbRange (-60.0f, 12.0f), 0.0f, A().withStringFromValueFunction (db)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "room", 1 }, "Room", dbRange (-60.0f, 12.0f), 0.0f, A().withStringFromValueFunction (db)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "bleed", 1 }, "Bleed", dbRange (-60.0f, 12.0f), 0.0f, A().withStringFromValueFunction (db)));
    v.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, A().withStringFromValueFunction (db)));
    for (int g = 0; g < hk::NumGroups; ++g)
    {
        const juce::String id (groupId (g));
        const juce::String label = juce::String (groupName (g)).substring (0, 1) + juce::String (groupName (g)).substring (1).toLowerCase();
        v.push_back (std::make_unique<F> (juce::ParameterID { id + "Vol", 1 }, label + " Volume", dbRange (-60.0f, 12.0f), 0.0f, A().withStringFromValueFunction (db)));
        v.push_back (std::make_unique<F> (juce::ParameterID { id + "Tune", 1 }, label + " Tune", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, A().withStringFromValueFunction (semis)));
        v.push_back (std::make_unique<F> (juce::ParameterID { id + "Len", 1 }, label + " Length", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, A().withStringFromValueFunction (pct)));
    }
    juce::StringArray kickSounds, kickNotes, snareSounds;
    for (int i = 0; i < hk::kNumKickSounds; ++i) kickSounds.add (hk::kickSound (i).name);
    for (int i = 0; i < hk::kNumKickNotes; ++i) kickNotes.add (hk::kickNoteName (i));
    for (int i = 0; i < hk::kNumSnareSounds; ++i) snareSounds.add (hk::snareSoundName (i));
    v.push_back (std::make_unique<F> (juce::ParameterID { "kickBlend", 1 }, "Kick EDM Blend", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, A().withStringFromValueFunction (pct)));
    v.push_back (std::make_unique<C> (juce::ParameterID { "kickSound", 1 }, "Kick EDM Sound", kickSounds, 0));
    v.push_back (std::make_unique<C> (juce::ParameterID { "kickNote", 1 }, "Kick EDM Note", kickNotes, 1));
    v.push_back (std::make_unique<F> (juce::ParameterID { "snareBlend", 1 }, "Snare EDM Blend", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, A().withStringFromValueFunction (pct)));
    v.push_back (std::make_unique<C> (juce::ParameterID { "snareSound", 1 }, "Snare EDM Sound", snareSounds, 0));
    v.push_back (std::make_unique<F> (juce::ParameterID { "snareEdmTune", 1 }, "Snare EDM Tune", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, A().withStringFromValueFunction (semis)));
    // マルチアウト: 部品ごとに「別の出口へ」（Logic で Multi Output 版を使ったときだけ効く。Stereo 版なら Main のまま）
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepKick", 1 }, "Kick To Own Out", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepSnare", 1 }, "Snare To Own Out", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepToms", 1 }, "Toms To Own Out", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepHiHat", 1 }, "Hi-Hat To Own Out", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepCym", 1 }, "Cymbals To Own Out", false));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sepRoom", 1 }, "Room To Own Out", false));
    v.push_back (plink::makeGroupParameter (1));
    return { v.begin(), v.end() };
}

hk::Settings HybridKitProcessor::readSettings() const
{
    hk::Settings s;
    s.close = dbToGain (pClose->load());
    s.oh = dbToGain (pOH->load());
    s.room = dbToGain (pRoom->load());
    s.bleed = dbToGain (pBleed->load());
    s.output = dbToGain (pOutput->load());
    for (size_t g = 0; g < (size_t) hk::NumGroups; ++g)
    {
        s.vol[g] = dbToGain (pVol[g]->load());
        s.tune[g] = pTune[g]->load();
        s.length[g] = pLen[g]->load() * 0.01f;
    }
    s.kickBlend = pKickBlend->load() * 0.01f;
    s.kickSound = juce::roundToInt (pKickSound->load());
    s.kickNote = juce::roundToInt (pKickNote->load());
    s.snareBlend = pSnareBlend->load() * 0.01f;
    s.snareSound = juce::roundToInt (pSnareSound->load());
    s.snareTune = pSnareEdmTune->load();
    return s;
}

bool HybridKitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty()) return false;
    const int n = layouts.outputBuses.size();
    if (n < 1 || n > hk::Engine::maxOuts) return false;
    const auto main = layouts.outputBuses.getReference (0);
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono()) return false;
    for (int i = 1; i < n; ++i)
    {
        const auto b = layouts.outputBuses.getReference (i);
        if (! b.isDisabled() && b != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void HybridKitProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate > 0 ? sampleRate : 44100.0;
    engine.prepare (internalRate);
    for (auto& r : resamplers) r.setup (internalRate, hostRate);
    setLatencySamples (resamplers[0].latencyOut());
}

void HybridKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // 書き出し（オフライン）のときは、選ばれているキットを読み終わるまで待つ（曲の頭の音が抜けないように）。
    // 待つのは 1 回の読み込みにつき 1 回・最長 30 秒（ブロックごとに待たない。DSP 検査係 2026-11-12）
    if (isNonRealtime())
    {
        const int lk = loadingKit.load();
        const uint32_t serial = loadSerial.load();
        if (lk >= 0 && lk == requestedKit.load() && serial != waitedSerial)
        {
            waitedSerial = serial;
            for (int i = 0; i < 6000 && loadingKit.load() == lk && loadSerial.load() == serial && pendingKit.load() == nullptr; ++i)
                juce::Thread::sleep (5);
        }
    }
    if (resetRequested.exchange (false))
    {
        engine.hardReset();
        for (auto& r : resamplers) r.reset();
    }

    if (auto* k = pendingKit.exchange (nullptr)) engine.setKit (k);
    if (toRetire == nullptr)
    {
        toRetire = engine.takeRetiredKit();
        toRetireFence = prefetcher.enqueued();   // これより後に、このキットの先読みは渡さない
    }
    if (toRetire != nullptr)
    {
        const hk::Kit* expected = nullptr;
        retiredFence.store (toRetireFence);
        if (retiredKit.compare_exchange_strong (expected, toRetire)) toRetire = nullptr;
    }
    engine.setSettings (readSettings());

    // ---- イベントを集める（画面で叩いた音が先、そのあと MIDI。どちらも時間順） ----
    int ne = 0;
    {
        int s1, n1, s2, n2;
        auditionFifo.prepareToRead (auditionFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1 + n2 && ne < (int) events.size(); ++i)
        {
            const auto& a = auditionBuf[(size_t) (i < n1 ? s1 + i : s2 + (i - n1))];
            events[(size_t) ne++] = { 0, 0x90, (uint8_t) juce::jlimit (0, 127, a.first), (uint8_t) juce::jlimit (1, 127, a.second) };
        }
        auditionFifo.finishedRead (n1 + n2);
    }
    const bool logging = logEnabled.load();
    double ppq = -1.0, bpm = 120.0;
    if (logging)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (pos->getPpqPosition().hasValue()) ppq = *pos->getPpqPosition();
                if (pos->getBpm().hasValue()) bpm = *pos->getBpm();
            }
    for (const auto meta : midi)
    {
        const auto& m = meta.getMessage();
        const uint8_t* raw = m.getRawData();
        const int sz = m.getRawDataSize();
        if (sz < 2) continue;
        const int st = raw[0] & 0xf0;
        if (st != 0x90 && st != 0xb0 && st != 0x80) continue;
        const int off = juce::jlimit (0, n - 1, meta.samplePosition);
        const uint8_t d2 = sz > 2 ? raw[2] : 0;
        if (ne < (int) events.size()) events[(size_t) ne++] = { off, raw[0], raw[1], d2 };
        if (logging && st != 0x80)
        {
            int s1, n1, s2, n2;
            logFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 > 0)
            {
                logBuf[(size_t) (n1 > 0 ? s1 : s2)] = { (double) (samplesSeen + off) / hostRate,
                                                         ppq >= 0 ? ppq + (double) off / hostRate * bpm / 60.0 : -1.0, raw[0], raw[1], d2 };
                logFifo.finishedWrite (1);
            }
        }
    }
    auto apply = [this] (const Ev& e)
    {
        const int st = e.st & 0xf0;
        if (st == 0x90 && e.d2 > 0) engine.noteOn (e.d1, e.d2);
        else if (st == 0xb0) engine.controller (e.d1, e.d2);
    };

    // ---- 出口（Logic がつないでいる出口だけ書く。つないでいない出口に分けた部品は Main に出す） ----
    constexpr int K = hk::Engine::maxOuts;
    std::array<float*, 2 * K> host {};
    int connected = 0;
    const int numBuses = juce::jmin (K, getBusCount (false));
    for (int b = 0; b < numBuses; ++b)
    {
        auto* bus = getBus (false, b);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 1) continue;
        const int ch0 = getChannelIndexInProcessBlockBuffer (false, b, 0);
        if (ch0 < 0 || ch0 >= buffer.getNumChannels()) continue;
        host[(size_t) (2 * b)] = buffer.getWritePointer (ch0);
        host[(size_t) (2 * b + 1)] = bus->getNumberOfChannels() > 1 && ch0 + 1 < buffer.getNumChannels() ? buffer.getWritePointer (ch0 + 1) : nullptr;
        ++connected;
    }
    connectedOutCount.store (connected);
    {
        auto has = [&] (int o) { return o < numBuses && host[(size_t) (2 * o)] != nullptr; };
        static constexpr int groupOut[] = { hk::Engine::OutKick, hk::Engine::OutSnare, hk::Engine::OutToms, hk::Engine::OutHiHat, hk::Engine::OutCymbals };
        std::array<int, hk::NumPieces> direct {}, room {};
        const bool roomOwn = pSep[5]->load() > 0.5f && has (hk::Engine::OutRoom);
        for (int p = 0; p < hk::NumPieces; ++p)
        {
            const int g = hk::groupOf (p);
            const int o = groupOut[g];
            direct[(size_t) p] = pSep[(size_t) g]->load() > 0.5f && has (o) ? o : hk::Engine::OutMain;
            room[(size_t) p] = roomOwn ? (int) hk::Engine::OutRoom : direct[(size_t) p];
        }
        engine.setRouting (direct, room);
    }
    std::array<float*, 2 * K> iptr {};
    for (size_t k = 0; k < iptr.size(); ++k) iptr[k] = ibuf[k].data();
    auto writeHost = [&] (int b, int pos, const float* l, const float* r, int c)   // 中の音 → ホストの出口（モノなら平均）
    {
        float* hl = host[(size_t) (2 * b)];
        float* hr = host[(size_t) (2 * b + 1)];
        if (hl == nullptr) return;
        if (hr != nullptr) { std::copy (l, l + c, hl + pos); std::copy (r, r + c, hr + pos); }
        else for (int i = 0; i < c; ++i) hl[pos + i] = 0.5f * (l[i] + r[i]);
    };

    auto& rs0 = resamplers[0];
    if (rs0.isBypass())
    {
        int pos = 0, idx = 0;
        while (pos < n)
        {
            while (idx < ne && events[(size_t) idx].offset <= pos) apply (events[(size_t) idx++]);
            const int end = idx < ne ? juce::jmin (n, events[(size_t) idx].offset) : n;
            const int c = juce::jmax (1, juce::jmin (end - pos, hk::Engine::maxBlock));
            engine.render (iptr.data(), K, c);
            for (int b = 0; b < numBuses; ++b) writeHost (b, pos, iptr[(size_t) (2 * b)], iptr[(size_t) (2 * b + 1)], c);
            pos += c;
        }
        while (idx < ne) apply (events[(size_t) idx++]);
    }
    else
    {
        const int64_t base = rs0.outputPos();
        const int64_t need = rs0.needInputUpTo (n);
        int idx = 0;
        while (rs0.inputWritten() < need)
        {
            while (idx < ne && rs0.eventIndexFor (base + events[(size_t) idx].offset) <= rs0.inputWritten()) apply (events[(size_t) idx++]);
            int64_t until = need;
            if (idx < ne) until = juce::jmin (until, rs0.eventIndexFor (base + events[(size_t) idx].offset));
            const int c = (int) juce::jmax<int64_t> (1, juce::jmin<int64_t> (hk::Engine::maxBlock, until - rs0.inputWritten()));
            engine.render (iptr.data(), K, c);
            for (int k = 0; k < K; ++k) resamplers[(size_t) k].push (iptr[(size_t) (2 * k)], iptr[(size_t) (2 * k + 1)], c);   // 全部の出口を同じ位置で進める
        }
        while (idx < ne) apply (events[(size_t) idx++]);
        for (int k = 0; k < K; ++k)
        {
            auto& rs = resamplers[(size_t) k];
            float* hl = k < numBuses ? host[(size_t) (2 * k)] : nullptr;
            float* hr = k < numBuses ? host[(size_t) (2 * k + 1)] : nullptr;
            if (hl == nullptr) { rs.skip (n); continue; }   // つながっていない出口は計算しない（位置だけ進める）
            if (hr != nullptr) rs.produce (hl, hr, n);
            else
                for (int pos = 0; pos < n; pos += hk::Engine::maxBlock)
                {
                    const int c = juce::jmin (hk::Engine::maxBlock, n - pos);
                    rs.produce (tmpL.data(), tmpR.data(), c);
                    for (int i = 0; i < c; ++i) hl[pos + i] = 0.5f * (tmpL[(size_t) i] + tmpR[(size_t) i]);
                }
        }
    }
    samplesSeen += n;
}

// ---- キットを読む ----
void HybridKitProcessor::requestKit (int index)
{
    index = juce::jlimit (0, kitNames().size() - 1, index);
    if (requestedKit.exchange (index) != index)
    {
        cancelLoad.store (true);
        loader.notify();
    }
}

void HybridKitProcessor::deleteKit (const hk::Kit* k)
{
    std::unique_ptr<hk::Kit> victim;
    {
        std::lock_guard<std::mutex> g (kitsLock);
        for (auto it = ownedKits.begin(); it != ownedKits.end(); ++it)
            if (it->get() == k) { victim = std::move (*it); ownedKits.erase (it); break; }
    }
    // victim はここで消える（mmap を閉じる）。ロックの外で
}

void HybridKitProcessor::Loader::run()
{
    auto setStatus = [this] (const juce::String& m) { const juce::ScopedLock sl (owner.statusLock); owner.statusMessage = m; };
    while (! threadShouldExit())
    {
        const int want = owner.requestedKit.load();
        // 読んでいる途中で、もう持っているキットに戻した（A→B→A）: 読むものはない。状態を戻す（DSP 検査係 2026-11-12）
        if (want >= 0 && want == owner.loaderKit)
        {
            if (owner.loadStateValue.load() == stateLoading)
            {
                owner.loadingKit.store (-1);
                setStatus (juce::String (hk::producerKitNames()[(size_t) want]));
                owner.loadStateValue.store (owner.lastLoadOk ? stateReady : stateError);
            }
            // 頭 → 尾のつなぎ方を決めている途中で選び直されて戻ってきた: 続きを決める
            if (! owner.spliceDone && owner.publishedKit != nullptr)
            {
                owner.cancelLoad.store (false);
                owner.spliceDone = hk::refineSplices (*owner.publishedKit, &owner.cancelLoad);
                continue;
            }
        }
        if (want >= 0 && want != owner.loaderKit)
        {
            owner.cancelLoad.store (false);
            owner.progress.store (0.0f);
            owner.loadingKit.store (want);
            owner.loadSerial.fetch_add (1);
            owner.loadStateValue.store (stateLoading);
            const std::string name = hk::producerKitNames()[(size_t) want];
            setStatus (juce::String (name));
            auto kit = std::make_unique<hk::Kit>();
            std::string err;
            const bool installed = hk::kitInstalled (name);
            bool ok = installed && kit->loadProducerKit (hk::dkdKitFolder() + "/" + name + ".exs", err, &owner.progress, &owner.cancelLoad);
            if (ok) hk::analyzeEdm (*kit, owner.readSettings(), &owner.cancelLoad);   // 出す前に（書き出しの最初の音からサブが鳴るように）
            if (threadShouldExit()) break;
            if (owner.cancelLoad.load())   // 途中でやめた（別のキットが選ばれた・同じキットを選び直した）→ 失敗ではない。上に戻って読み直す
            {
                owner.loadingKit.store (-1);
                continue;
            }
            if (! ok)
            {
                owner.loaderKit = want;   // 同じものを何度も読みに行かない
                owner.lastLoadOk = false;
                owner.loadingKit.store (-1);
                setStatus (installed ? juce::String ("Could not load ") + name + " (" + juce::String (err) + ")"
                                     : juce::String ("Drum Kit Designer sounds not found: ") + name + " (Logic Pro > Sound Library)");
                owner.loadStateValue.store (stateError);
                continue;
            }
            uint32_t mask = 0;
            for (int p = 0; p < hk::NumPieces; ++p) if (kit->present[(size_t) p]) mask |= 1u << p;
            hk::Kit* raw = kit.get();
            {
                std::lock_guard<std::mutex> g (owner.kitsLock);
                owner.ownedKits.push_back (std::move (kit));
            }
            if (hk::Kit* prev = owner.pendingKit.exchange (raw)) owner.deleteKit (prev);   // オーディオスレッドが一度も使っていない
            owner.publishedKit = raw;
            owner.loaderKit = want;
            owner.lastLoadOk = true;
            owner.presentMask.store (mask);
            owner.loadingKit.store (-1);
            setStatus (juce::String (name));
            owner.loadStateValue.store (stateReady);
            owner.spliceDone = false;
            owner.spliceDone = hk::refineSplices (*raw, &owner.cancelLoad);   // 鳴らせるようにした後で、頭 → 尾のつなぎ方を整える（数秒）
            continue;
        }
        if (owner.analysisDirty.exchange (false) && owner.publishedKit != nullptr)
            hk::analyzeEdm (*owner.publishedKit, owner.readSettings(), nullptr);
        wait (200);
    }
}

juce::String HybridKitProcessor::statusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusMessage;
}

void HybridKitProcessor::timerCallback()
{
    const int k = juce::roundToInt (pKit->load());
    if (k != lastKitParam) { lastKitParam = k; requestKit (k); }
    const std::array<int, 4> a { juce::roundToInt (pKickSound->load()), juce::roundToInt (pKickNote->load()),
                                 juce::roundToInt (pSnareSound->load()), juce::roundToInt (pSnareEdmTune->load() * 10.0f) };
    if (a != lastAnalysis) { lastAnalysis = a; analysisDirty.store (true); loader.notify(); }
    if (const hk::Kit* r = retiredKit.exchange (nullptr))
        pendingDeletes.push_back ({ r, retiredFence.load() });   // 先読みが古いキットのページを触り終えてから消す（途中で消すと落ちる）
    for (auto it = pendingDeletes.begin(); it != pendingDeletes.end();)
    {
        if (hk::Prefetcher::reached (prefetcher.done(), it->second)) { deleteKit (it->first); it = pendingDeletes.erase (it); }
        else ++it;
    }
    flushMidiLog();
}

// 届いた MIDI を ~/Library/Application Support/MyPlugins/Hybrid Kit/midi-log.txt に（Drummer が送るノートを確かめる用。開いてから 10 分・5000 行まで。
// 割り当てを確かめたら次の版で外す）
void HybridKitProcessor::flushMidiLog()
{
    const int ready = logFifo.getNumReady();
    if (ready == 0) return;
    int s1, n1, s2, n2;
    logFifo.prepareToRead (ready, s1, n1, s2, n2);
    const bool keep = juce::Time::getMillisecondCounter() - createdMs < 10u * 60u * 1000u && logLines < 5000;
    if (! keep) logEnabled.store (false);
    if (keep && logStream == nullptr)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MyPlugins/Hybrid Kit");
        dir.createDirectory();
        auto f = dir.getChildFile ("midi-log.txt");
        if (f.getSize() > 2 * 1024 * 1024) f.moveFileTo (dir.getChildFile ("midi-log-old.txt"));
        logStream = std::make_unique<juce::FileOutputStream> (f);
        if (logStream->failedToOpen()) { logStream.reset(); logEnabled.store (false); }
        else *logStream << "\n# " << juce::Time::getCurrentTime().toString (true, true) << "  track=" << trackName
                        << "  rate=" << juce::String (hostRate, 0) << "  (sec  ppq  status  data1  data2)\n";
    }
    if (keep && logStream != nullptr)
    {
        for (int i = 0; i < n1 + n2; ++i)
        {
            const auto& e = logBuf[(size_t) (i < n1 ? s1 + i : s2 + (i - n1))];
            *logStream << juce::String (e.sec, 4) << "  " << (e.ppq >= 0 ? juce::String (e.ppq, 3) : juce::String ("-")) << "  "
                       << juce::String::toHexString ((int) e.st) << "  " << (int) e.d1 << "  " << (int) e.d2 << "\n";
            ++logLines;
        }
        logStream->flush();
    }
    logFifo.finishedRead (n1 + n2);
}

void HybridKitProcessor::audition (int note, int velocity)
{
    int s1, n1, s2, n2;
    auditionFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    auditionBuf[(size_t) (n1 > 0 ? s1 : s2)] = { note, velocity };
    auditionFifo.finishedWrite (1);
}

void HybridKitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HybridKitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            lastKitParam = juce::roundToInt (pKit->load());
            requestKit (lastKitParam);   // 書き出しがすぐ始まっても間に合うように、タイマーを待たずに読み始める
        }
}

juce::StringArray HybridKitProcessor::presetNames()
{
    return { "Studio Natural", "Pop Hybrid", "Trap Hybrid", "EDM Punch", "Rock Room", "Tight Funk", "Classic 60s" };
}

void HybridKitProcessor::applyPreset (int index)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    // まず全部を初期値に（LINK の番号と出口のスイッチは変えない。出口は曲の配線なので、プリセットを選んだだけでキックが Main に戻らないように）
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (r->getParameterID() != "linkGroup" && ! r->getParameterID().startsWith ("sep"))
                r->setValueNotifyingHost (r->getDefaultValue());
    auto kit = [&] (const char* name) { set ("kit", (float) kitNames().indexOf (name)); };
    switch (index)
    {
        case 1:   // Pop Hybrid: 今っぽいポップス。キックの下に 808、スネアにクラップを少し
            kit ("Modern Maple Kit");
            set ("kickBlend", 70); set ("kickSound", 0); set ("kickNote", 1);
            set ("snareBlend", 45); set ("snareSound", 0);
            set ("room", -3);
            break;
        case 2:   // Trap Hybrid: 長い 808 と大きいクラップ、ハイハットは短め
            kit ("Tight Maple Kit");
            set ("kickBlend", 100); set ("kickSound", 1); set ("kickNote", 1);
            set ("snareBlend", 80); set ("snareSound", 1);
            set ("hihatLen", 55); set ("room", -6);
            break;
        case 3:   // EDM Punch: 短いサブで押し出し、スナップで前に。部屋は控えめ
            kit ("Studio Kit");
            set ("kickBlend", 100); set ("kickSound", 2);
            set ("snareBlend", 100); set ("snareSound", 3);
            set ("room", -8); set ("bleed", -6); set ("cymLen", 70);
            break;
        case 4:   // Rock Room: 広い部屋の鳴り
            kit ("Stadium Kit");
            set ("room", 3); set ("oh", 1);
            break;
        case 5:   // Tight Funk: 部屋を絞って近く、タムは短く
            kit ("Tight Maple Kit");
            set ("room", -10); set ("bleed", -4); set ("tomsLen", 60); set ("snareTune", 1);
            break;
        case 6:   // Classic 60s: 60 年代の小さめのキット、部屋は少し控えめ
            kit ("Classic Sixties Kit");
            set ("room", -2); set ("tomsLen", 80);
            break;
        default:  // Studio Natural: Studio Kit をそのまま
            kit ("Studio Kit");
            break;
    }
}

juce::AudioProcessorEditor* HybridKitProcessor::createEditor() { return new HybridKitEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HybridKitProcessor(); }
