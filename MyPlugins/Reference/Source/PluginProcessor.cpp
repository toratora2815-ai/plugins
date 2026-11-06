#include "PluginProcessor.h"
#include "PluginEditor.h"

ReferenceProcessor::ReferenceProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pAb     = apvts.getRawParameterValue ("ab");
    pSource = apvts.getRawParameterValue ("source");
    pLevel  = apvts.getRawParameterValue ("level");
    pBand   = apvts.getRawParameterValue ("band");
    pStereo = apvts.getRawParameterValue ("stereo");
    pTrim   = apvts.getRawParameterValue ("trim");
    pLoop   = apvts.getRawParameterValue ("loop");
    pFollow = apvts.getRawParameterValue ("follow");

    mixMeter.prepare (sr);
    refMeter.prepare (sr);
    startTimerHz (10);
}

ReferenceProcessor::~ReferenceProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 10000);
    live.close();
}

juce::AudioProcessorValueTreeState::ParameterLayout ReferenceProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    v.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "ab", 1 }, "A/B (Reference)", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Source", StringArray { "File", "Live" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "level", 1 }, "Level", StringArray { "Off", "Match", "-14 LUFS" }, 1));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "band", 1 }, "Band", StringArray { "Full", "Low", "Mid", "High" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "stereo", 1 }, "Stereo", StringArray { "Stereo", "Mono", "Side" }, 0));
    v.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "trim", 1 }, "Ref Trim",
                                                         NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                         AudioParameterFloatAttributes().withLabel ("dB")));
    v.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "loop", 1 }, "Loop", true));
    v.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "follow", 1 }, "Follow DAW", false));
    // Plugin Link（LINK の番号）
    v.push_back (plink::makeGroupParameter (9));
    return { v.begin(), v.end() };
}

bool ReferenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void ReferenceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    blockSize = samplesPerBlock;
    mixMeter.prepare (sr);
    refMeter.prepare (sr);
    mixSpec.prepare (sr);
    refSpec.prepare (sr);

    abSmooth.reset (sr, 0.02);
    abSmooth.setCurrentAndTargetValue (pAb->load() > 0.5f ? 1.0f : 0.0f);
    for (auto* s : { &wSt, &wMono, &wSide, &wFull, &wLow, &wMid, &wHigh })
        s->reset (sr, 0.02);
    const int stereo = (int) pStereo->load(), band = (int) pBand->load();
    wSt.setCurrentAndTargetValue   (stereo == 0 ? 1.0f : 0.0f);
    wMono.setCurrentAndTargetValue (stereo == 1 ? 1.0f : 0.0f);
    wSide.setCurrentAndTargetValue (stereo == 2 ? 1.0f : 0.0f);
    wFull.setCurrentAndTargetValue (band == 0 ? 1.0f : 0.0f);
    wLow.setCurrentAndTargetValue  (band == 1 ? 1.0f : 0.0f);
    wMid.setCurrentAndTargetValue  (band == 2 ? 1.0f : 0.0f);
    wHigh.setCurrentAndTargetValue (band == 3 ? 1.0f : 0.0f);
    mixGain.reset (sr, 0.3);
    refGain.reset (sr, 0.3);
    mixGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (mixGainDb.load(), -200.0f));
    refGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (refGainDb.load(), -200.0f));

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 2 };
    xoLow.prepare (spec);  xoLow.setCutoffFrequency (200.0f);
    xoHigh.prepare (spec); xoHigh.setCutoffFrequency (3000.0f);
}

void ReferenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    hostPlaying = playing;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (numOut == 0) return;
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, buffer.getNumSamples());

    const bool mono = numOut < 2;
    float* L = buffer.getWritePointer (0);
    float* R = mono ? L : buffer.getWritePointer (1);

    const int n = buffer.getNumSamples();
    for (int off = 0; off < n; off += kChunk)
        processChunk (L + off, R + off, juce::jmin (kChunk, n - off), mono);

    // Plugin Link: 参照曲を聴いているか（同じ LINK の Balance Scope はその間測らない）
    if (link.on())
    {
        link.ep().publish (plink::ref::listeningRef, pAb->load() > 0.5f ? 1.0f : 0.0f);
        link.ep().stamp();
        link.ep().heartbeat();
    }
}

void ReferenceProcessor::processChunk (float* L, float* R, int n, bool mono)
{
    float* refL = refBuf.getWritePointer (0);
    float* refR = refBuf.getWritePointer (1);

    // ---- 参照曲を取ってくる ----
    const int source = (int) pSource->load();
    if (source != lastSource) { refMeter.requestReset(); refWasSilent = false; lastSource = source; }

    if (source == liveSource)
        live.read (refL, refR, n, sr);
    else
    {
        const bool play = pFollow->load() > 0.5f ? hostPlaying.load() : player.userPlaying.load();
        player.process (refL, refR, n, sr, play, pLoop->load() > 0.5f);
    }
    if (mono)
        for (int i = 0; i < n; ++i) refL[i] = 0.5f * (refL[i] + refR[i]);

    // ---- 計測（どちらを聴いていても両方測る）----
    mixMeter.process (L, R, n);
    refMeter.process (refL, mono ? refL : refR, n);
    mixSpec.push (L, R, n);
    refSpec.push (refL, mono ? refL : refR, n);

    // ライブ入力で 3 秒以上無音のあと音が来たら、別の曲とみなして参照側の計測をやり直す
    if (source == liveSource)
    {
        const bool present = refMeter.signalPresent.load();
        if (! present) refWasSilent = true;
        else if (refWasSilent) { refWasSilent = false; refMeter.requestReset(); }
    }

    // ---- 音量合わせ ----
    const int level = (int) pLevel->load();
    const float mixLufs = refdsp::comparisonLoudness (mixMeter);
    const float refLufs = refdsp::comparisonLoudness (refMeter);
    float mDb = 0.0f, rDb = 0.0f;
    if (level == levelMatch)
    {
        if (mixLufs > -100.0f && refLufs > -100.0f)
            heldMatchDb = juce::jlimit (-30.0f, 30.0f, mixLufs - refLufs);
        rDb = heldMatchDb;
    }
    else if (level == levelStream)
    {
        if (mixLufs > -100.0f) heldMixStreamDb = refdsp::streamingGainDb (mixLufs, mixMeter.peakMax.load());
        if (refLufs > -100.0f) heldRefStreamDb = refdsp::streamingGainDb (refLufs, refMeter.peakMax.load());
        mDb = heldMixStreamDb;
        rDb = heldRefStreamDb;
    }
    rDb += pTrim->load();
    mixGainDb = mDb;
    refGainDb = rDb;
    mixGain.setTargetValue (juce::Decibels::decibelsToGain (mDb, -200.0f));
    refGain.setTargetValue (juce::Decibels::decibelsToGain (rDb, -200.0f));

    // ---- モニター切替の目標値 ----
    abSmooth.setTargetValue (pAb->load() > 0.5f ? 1.0f : 0.0f);
    const int stereo = mono ? 0 : (int) pStereo->load();
    wSt.setTargetValue   (stereo == 0 ? 1.0f : 0.0f);
    wMono.setTargetValue (stereo == 1 ? 1.0f : 0.0f);
    wSide.setTargetValue (stereo == 2 ? 1.0f : 0.0f);
    const int band = (int) pBand->load();
    wFull.setTargetValue (band == 0 ? 1.0f : 0.0f);
    wLow.setTargetValue  (band == 1 ? 1.0f : 0.0f);
    wMid.setTargetValue  (band == 2 ? 1.0f : 0.0f);
    wHigh.setTargetValue (band == 3 ? 1.0f : 0.0f);

    const int numCh = mono ? 1 : 2;
    for (int i = 0; i < n; ++i)
    {
        const float gm = mixGain.getNextValue(), gr = refGain.getNextValue();
        const float x = abSmooth.getNextValue();

        float l = L[i] * gm * (1.0f - x) + refL[i] * gr * x;
        float r = mono ? l : R[i] * gm * (1.0f - x) + refR[i] * gr * x;

        // ステレオ／モノ／サイド
        const float st = wSt.getNextValue(), mo = wMono.getNextValue(), si = wSide.getNextValue();
        if (! mono)
        {
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            const float nl = st * l + mo * mid + si * side;
            const float nr = st * r + mo * mid + si * side;
            l = nl; r = nr;
        }

        // 帯域ソロ（200Hz / 3kHz の LR4 クロスオーバー）
        const float fu = wFull.getNextValue(), lo = wLow.getNextValue(), mi = wMid.getNextValue(), hi = wHigh.getNextValue();
        float in[2] = { l, r };
        for (int ch = 0; ch < numCh; ++ch)
        {
            float low = 0, rest = 0, mid = 0, high = 0;
            xoLow.processSample (ch, in[ch], low, rest);
            xoHigh.processSample (ch, rest, mid, high);
            in[ch] = fu * in[ch] + lo * low + mi * mid + hi * high;
        }

        L[i] = in[0];
        if (! mono) R[i] = in[1];
    }
}

// ---------------------------------------------------------------------------
// ファイル読み込み・ライブ入力（メッセージスレッド）

void ReferenceProcessor::loadFile (const juce::File& f)
{
    if (! f.existsAsFile()) { fileStatus = "File not found: " + f.getFileName(); return; }

    filePath = f.getFullPathName();
    apvts.state.setProperty ("filePath", filePath, nullptr);
    fileStatus = "Loading " + f.getFileName() + " ...";
    loading = true;

    loaderPool.removeAllJobs (true, 2000);
    loaderPool.addJob ([this, f]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));

        std::unique_ptr<RefAudio> a;
        juce::String err;
        if (reader == nullptr)
            err = "Can't read this file type: " + f.getFileName();
        else
        {
            const auto maxLen = (juce::int64) (reader->sampleRate * 60.0 * 20.0);   // 20分まで
            const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
            if (len < 1000) err = "File is too short: " + f.getFileName();
            else
            {
                a = std::make_unique<RefAudio>();
                a->sampleRate = reader->sampleRate;
                a->name = f.getFileNameWithoutExtension();
                a->buffer.setSize (2, len);
                reader->read (&a->buffer, 0, len, 0, true, true);
                if (reader->numChannels == 1)
                    a->buffer.copyFrom (1, 0, a->buffer, 0, 0, len);

                constexpr int bins = 1200;
                a->overview.resize (bins);
                float maxV = 1.0e-6f;
                for (int b = 0; b < bins; ++b)
                {
                    const int s0 = (int) ((juce::int64) len * b / bins), s1 = (int) ((juce::int64) len * (b + 1) / bins);
                    float pk = 0.0f;
                    for (int ch = 0; ch < 2; ++ch)
                        pk = juce::jmax (pk, a->buffer.getMagnitude (ch, s0, juce::jmax (1, s1 - s0)));
                    a->overview[(size_t) b] = pk;
                    maxV = juce::jmax (maxV, pk);
                }
                for (auto& v : a->overview) v /= maxV;
            }
        }

        const juce::ScopedLock sl (pendingLock);
        pendingAudio = std::move (a);
        pendingError = err;
        loading = false;
    });
}

void ReferenceProcessor::setLiveDevice (const juce::String& name)
{
    liveDeviceName = name;
    apvts.state.setProperty ("liveDevice", name, nullptr);
    live.close();
    liveFailed = false;
    liveError.clear();
}

void ReferenceProcessor::timerCallback()
{
    // 読み込みが終わった音源を差し替え
    {
        std::unique_ptr<RefAudio> next;
        juce::String err;
        {
            const juce::ScopedLock sl (pendingLock);
            next = std::move (pendingAudio);
            err = pendingError;
            pendingError.clear();
        }
        if (next != nullptr)
        {
            fileStatus = next->name;
            auto old = player.swapAudio (std::move (next));   // 古い音源はここ（メッセージスレッド）で破棄
            if ((int) pSource->load() == fileSource) refMeter.requestReset();
        }
        else if (err.isNotEmpty())
            fileStatus = err;
    }

    // 保存されていた設定の復元
    if (stateRestored.exchange (false))
    {
        const auto fp = apvts.state.getProperty ("filePath").toString();
        if (fp.isNotEmpty() && fp != filePath) loadFile (juce::File (fp));
        const auto dev = apvts.state.getProperty ("liveDevice").toString();
        if (dev != liveDeviceName) setLiveDevice (dev);
    }

    // ライブ入力の開閉
    if ((int) pSource->load() == liveSource)
    {
        if (liveDeviceName.isEmpty() && --autoScanCountdown <= 0)
        {
            autoScanCountdown = 30;   // 3秒ごと
            for (auto& n : live.getDeviceNames())
                if (n.containsIgnoreCase ("BlackHole")) { setLiveDevice (n); break; }
        }
        if (! live.isOpen() && liveDeviceName.isNotEmpty() && ! liveFailed)
        {
            liveError = live.open (liveDeviceName, sr, blockSize);
            liveFailed = liveError.isNotEmpty();
        }
    }
    else if (live.isOpen())
        live.close();
}

void ReferenceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ReferenceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            stateRestored = true;
        }
}

juce::AudioProcessorEditor* ReferenceProcessor::createEditor() { return new ReferenceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReferenceProcessor(); }
