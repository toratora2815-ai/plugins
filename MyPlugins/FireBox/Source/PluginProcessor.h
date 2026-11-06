#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "FireEngine.h"

// 短期ラウドネス（K 特性、3 秒）とピーク。画面のメーター用
struct LoudMeter
{
    firebox::Biquad hs[2], hp[2];
    std::vector<double> blocks; int bpos = 0, bfill = 0, blockLen = 4800, cnt = 0; double acc = 0;
    std::atomic<float> lufs { -100.0f }, peak { 0.0f };
    void prepare (double sr)
    {
        for (int c = 0; c < 2; ++c) { hs[c].set (2, 1681.97, 0.7072, 3.99984, sr); hp[c].set (1, 38.135, 0.5003, 0, sr); hs[c].reset(); hp[c].reset(); }
        blockLen = juce::jmax (1, (int) (sr * 0.1)); blocks.assign (30, 0.0); bpos = bfill = cnt = 0; acc = 0;
    }
    void process (const float* const* ch, int numCh, int n)
    {
        float pk = peak.load() * 0.93f;
        for (int i = 0; i < n; ++i)
        {
            double e = 0;
            for (int c = 0; c < numCh && c < 2; ++c)
            {
                const double x = ch[c][i]; pk = juce::jmax (pk, (float) std::fabs (x));
                const double k = hp[c].process (hs[c].process (x)); e += k * k;
            }
            acc += e;
            if (++cnt >= blockLen)
            {
                blocks[(size_t) bpos] = acc / blockLen; bpos = (bpos + 1) % 30; bfill = juce::jmin (30, bfill + 1); acc = 0; cnt = 0;
                double s = 0; for (int b = 0; b < bfill; ++b) s += blocks[(size_t) b];
                const double ms = s / juce::jmax (1, bfill);
                lufs.store (ms > 1e-10 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -100.0f);
            }
        }
        peak.store (pk);
    }
};

class FireBoxProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
{
public:
    FireBoxProcessor();
    ~FireBoxProcessor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "FireBox" };   // 自分のプリセット・取り消し

    plink::Link link { plink::kindFireBox, apvts };        // Plugin Link
    plink::LevelMatch match;                                // 出口を Gain Stage の TARGET にそろえる

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) link.setTrackName (*tp.name); }

    LoudMeter inMeter, outMeter;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& id, float) override { if (id == "live" || id == "oversample") triggerAsyncUpdate(); }
    firebox::Params readSwitches() const;
    void processChunk (float* const* ch, int numCh, int n, firebox::Params& prm, bool bypass);
    std::atomic<float>* linkMatchOn = nullptr;
    bool prepared = false; int osChannels = 0;
    void rebuild();   // LIVE / 192k を変えたとき・prepareToPlay

    firebox::Engine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int osFactorLog2 = 0;
    bool builtLive = false, builtOs = false;
    std::atomic<bool> rebuildPending { false };
    double hostRate = 48000; int maxBlock = 512;

    struct P { std::atomic<float>* v = nullptr; juce::SmoothedValue<float> s; };
    P pIn, pOut, pBass, pThick, pBody, pComp, pSc, pCrunch, pMix, pLoud;
    std::atomic<float>* bassOn = nullptr; std::atomic<float>* thickOn = nullptr; std::atomic<float>* bodyOn = nullptr;
    std::atomic<float>* compOn = nullptr; std::atomic<float>* ottMode = nullptr; std::atomic<float>* scOn = nullptr;
    std::atomic<float>* crunchOn = nullptr; std::atomic<float>* loudOn = nullptr; std::atomic<float>* unityOn = nullptr;
    std::atomic<float>* liveOn = nullptr; std::atomic<float>* osOn = nullptr; std::atomic<float>* bypassOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FireBoxProcessor)
};
