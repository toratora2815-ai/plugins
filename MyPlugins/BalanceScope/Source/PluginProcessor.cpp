#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Targets.h"

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
}

juce::AudioProcessorValueTreeState::ParameterLayout BalanceScopeProcessor::createLayout()
{
    juce::StringArray names;
    for (auto& t : kTargets) names.add (t.name);
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "target", 1 }, "Target", names, 0));
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
    hostPlaying = playing;

    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    if (numCh == 0) return;

    const float scale = 1.0f / (float) numCh;
    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mono += buffer.getReadPointer (ch)[i];
        ring[(size_t) ringPos] = mono * scale;
        ringPos = (ringPos + 1) & (fftSize - 1);

        if (++samplesSinceFFT >= hopSize)
        {
            samplesSinceFFT = 0;
            analyseFrame();
        }
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
        frameCount = 0;
    }

    const bool silent = ms < 1.0e-7;   // 約 -70 dBFS 未満は無視
    if (silent) return;

    const double a = std::exp (-(double) hopSize / (sr * 1.5));   // 直近1.5秒くらいの表示用
    for (int b = 0; b < numBins; ++b)
    {
        const double p = (double) fftData[(size_t) b] * fftData[(size_t) b];
        livePower[(size_t) b] = livePower[(size_t) b] * a + p * (1.0 - a);
        if (hostPlaying) sumPower[(size_t) b] += p;
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

juce::AudioProcessorEditor* BalanceScopeProcessor::createEditor() { return new BalanceScopeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BalanceScopeProcessor(); }
