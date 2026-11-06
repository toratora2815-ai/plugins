#include "PluginProcessor.h"
#include "PluginEditor.h"

BalanceScopeProcessor::BalanceScopeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    ring.assign (fftSize, 0.0f);
    fftData.assign (fftSize * 2, 0.0f);
    sumPower.assign (numBins, 0.0);
    livePower.assign (numBins, 0.0);
    recentPower.assign (numBins, 0.0);
    startTimerHz (2);
}

juce::AudioProcessorValueTreeState::ParameterLayout BalanceScopeProcessor::createLayout()
{
    juce::StringArray names;
    for (auto& t : kTargets) names.add (t.name);
    names.add ("Custom");   // v1.1 で追加（最後に足したので、前の保存データの選択はそのまま）
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "target", 1 }, "Target", names, 0));

    // CUSTOM の目標カーブ（v1.1 で追加、バージョン 2）
    auto dbOct = [] (float x, int) { return juce::String (x, 1) + " dB/oct"; };
    auto db = [] (float x, int) { return (x > 0.05f ? "+" : "") + juce::String (x, 1) + " dB"; };
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "customTilt", 2 }, "Custom Tilt",
                    juce::NormalisableRange<float> (-3.5f, 0.0f, 0.1f), -1.5f,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbOct)));
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "customLow", 2 }, "Custom Low (below 120 Hz)",
                    juce::NormalisableRange<float> (-4.0f, 6.0f, 0.1f), 0.0f,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    // v1.2 で追加（バージョン 3）
    v.push_back (plink::makeGroupParameter (3));
    // v1.3 で追加（バージョン 4）：出すぎの帯域を、曲全体の平均で決めるか、直近 10 秒で決めるか
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "linkWindow", 4 }, "Link Window",
                                                               juce::StringArray { "WHOLE", "LAST 10 s" }, 0));
    return { v.begin(), v.end() };
}

bool BalanceScopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void BalanceScopeProcessor::prepareToPlay (double sampleRate, int)
{
    juce::SpinLock::ScopedLockType sl (lock);
    sr = sampleRate;
    loudness.prepare (sampleRate);
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * 60.0 / sampleRate, cs = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
        const double a0 = 1.0 + al;
        musicHp = {};
        musicHp.b0 = (1.0 + cs) / 2.0 / a0; musicHp.b1 = -(1.0 + cs) / a0; musicHp.b2 = musicHp.b0;
        musicHp.a1 = -2.0 * cs / a0; musicHp.a2 = (1.0 - al) / a0;
        musicCoef = 1.0 - std::exp (-1.0 / (0.4 * sampleRate));
        musicMs = 0.0;
    }
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = samplesSinceFFT = 0;
}

void BalanceScopeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // 再生中かどうか（ホストが情報をくれない場合は常に解析）
    bool playing = true;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    // Plugin Link: 同じ LINK の Reference が参照曲を流している間は測らない（自分の曲の測定に混ぜない）
    bool refOn = false;
    if (link.on())
    {
        plink::Peer rp;
        refOn = link.ep().find (plink::kindReference, rp) && rp.v[plink::ref::listeningRef] > 0.5f && rp.valuesAgeMs < 2000;
    }
    refPaused = refOn;
    if (refOn) playing = false;
    hostPlaying = playing;

    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    if (numCh == 0) return;

    if (loudResetRequested.exchange (false)) loudness.reset();
    loudness.process (buffer.getArrayOfReadPointers(), numCh, n, playing);

    const float scale = 1.0f / (float) numCh;
    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mono += buffer.getReadPointer (ch)[i];
        ring[(size_t) ringPos] = mono * scale;
        const float m = musicHp.run (mono * scale);
        musicMs += ((double) m * m - musicMs) * musicCoef;
        ringPos = (ringPos + 1) & (fftSize - 1);

        if (++samplesSinceFFT >= hopSize)
        {
            samplesSinceFFT = 0;
            if (! refOn) analyseFrame();
        }
    }
    if (! std::isfinite (musicMs)) { musicMs = 0.0; musicHp.z1 = musicHp.z2 = 0.0; }
    musicLevel = (float) juce::jmax (-120.0, 10.0 * std::log10 (musicMs + 1.0e-12));

    // Plugin Link へ：オケの音量（ブロックごと。Fader Pilot の FOLLOW MUSIC が使う）
    if (link.on())
    {
        link.ep().publish (plink::bs::musicDb, musicLevel.load());
        link.ep().stamp();
        link.ep().heartbeat();
    }
    // 音はそのまま通す（解析専用）
}

void BalanceScopeProcessor::analyseFrame()
{
    double ms = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const float x = ring[(size_t) ((ringPos + i) & (fftSize - 1))];
        ms += (double) x * x;
        fftData[(size_t) i] = x * window[(size_t) i];
    }
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    ms /= fftSize;

    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    juce::SpinLock::ScopedLockType sl (lock);

    if (resetRequested.exchange (false))
    {
        std::fill (sumPower.begin(), sumPower.end(), 0.0);
        std::fill (livePower.begin(), livePower.end(), 0.0);
        std::fill (recentPower.begin(), recentPower.end(), 0.0);
        frameCount = 0;
        recentSecs = 0.0;
    }

    const bool silent = ms < 1.0e-7;   // 約 -70 dBFS 未満は無視
    if (silent) return;

    const double a = std::exp (-(double) hopSize / (sr * 1.5));   // 直近1.5秒くらいの表示用
    // 直近 10 秒（時定数 10 秒の平均。たまり始めは今までの平均と同じになるよう重みを変える）
    const double hopSecs = (double) hopSize / sr;
    if (hostPlaying) recentSecs += hopSecs;
    const double ar = std::max (std::exp (-hopSecs / 10.0), 1.0 - hopSecs / std::max (hopSecs, recentSecs));
    for (int b = 0; b < numBins; ++b)
    {
        const double p = (double) fftData[(size_t) b] * fftData[(size_t) b];
        livePower[(size_t) b] = livePower[(size_t) b] * a + p * (1.0 - a);
        if (hostPlaying)
        {
            sumPower[(size_t) b] += p;
            recentPower[(size_t) b] = recentPower[(size_t) b] * ar + p * (1.0 - ar);
        }
    }
    if (hostPlaying) ++frameCount;
}

void BalanceScopeProcessor::getSnapshot (Snapshot& s)
{
    juce::SpinLock::ScopedLockType sl (lock);
    s.avgPower = sumPower;
    if (frameCount > 0)
        for (auto& v : s.avgPower) v /= frameCount;
    s.livePower = livePower;
    s.recentPower = recentPower;
    s.recentSecs = recentSecs;
    s.frames = frameCount;
    s.sampleRate = sr;
    s.playing = hostPlaying;
}

void BalanceScopeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BalanceScopeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

TargetCurve BalanceScopeProcessor::currentTarget() const
{
    return makeTarget ((int) apvts.getRawParameterValue ("target")->load(),
                       apvts.getRawParameterValue ("customTilt")->load(),
                       apvts.getRawParameterValue ("customLow")->load());
}

// ---- Plugin Link ----
// 平均（RESET からの全体、または直近 10 秒）がある程度たまったら、それで「出すぎの帯域」を上位 2 か所探す。
// 画面で参照曲（REF A〜C）と比べているときは、目標カーブの代わりに参照曲と比べる。
void BalanceScopeProcessor::timerCallback()
{
    LinkInfo info;
    info.on = link.on();
    if (! info.on) { linkInfo = info; return; }
    auto& ep = link.ep();
    info.peers = ep.countPeers();
    info.musicDb = musicLevel.load();
    ep.find (plink::kindHushField, info.tamer);
    ep.find (plink::kindGainStage, info.gain);

    getSnapshot (linkSnap);
    info.recent = apvts.getRawParameterValue ("linkWindow")->load() > 0.5f;
    const double secs = info.recent ? linkSnap.recentSecs : linkSnap.frames * (double) hopSize / linkSnap.sampleRate;
    if (secs >= 3.0)
    {
        const auto t = currentTarget();
        auto db = bandmath::toBands (info.recent ? linkSnap.recentPower : linkSnap.avgPower, linkSnap.sampleRate, fftSize, linkCenters);
        bandmath::normalise (db, linkCenters, t);

        std::vector<float> ref;
        const int slot = (int) apvts.state.getProperty ("compare", -1);
        if (slot >= 0 && slot < 3)
        {
            const char* names[] = { "A", "B", "C" };
            juce::StringArray parts;
            parts.addTokens (apvts.state.getChildWithName ("REFS").getProperty (juce::String ("data") + names[slot]).toString(), ",", {});
            if (parts.size() == (int) linkCenters.size())
            {
                for (auto& x : parts) ref.push_back (x.getFloatValue());
                bandmath::normalise (ref, linkCenters, t);
            }
        }
        bandmath::findExcess2 (db, linkCenters, t, ref, info.first, info.second);
        info.valid = true;
    }
    ep.publish (plink::bs::valid, info.valid ? 1.0f : 0.0f);
    ep.publish (plink::bs::excessDb, info.first.excessDb);
    ep.publish (plink::bs::lowHz, info.first.lowHz);
    ep.publish (plink::bs::highHz, info.first.highHz);
    ep.publish (plink::bs::peakHz, info.first.peakHz);
    ep.publish (plink::bs::excess2Db, info.second.excessDb);
    ep.publish (plink::bs::low2Hz, info.second.lowHz);
    ep.publish (plink::bs::high2Hz, info.second.highHz);
    ep.publish (plink::bs::peak2Hz, info.second.peakHz);
    ep.publish (plink::bs::window, info.recent ? 1.0f : 0.0f);
    ep.stamp();
    linkInfo = info;
}

BalanceScopeProcessor::LinkInfo BalanceScopeProcessor::getLinkInfo() const { return linkInfo; }

juce::AudioProcessorEditor* BalanceScopeProcessor::createEditor() { return new BalanceScopeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BalanceScopeProcessor(); }
