#pragma once
#include <JuceHeader.h>
#include "PresetBar.h"
#include "LoudnessMeter.h"
#include "Targets.h"
#include "BandMath.h"
#include "PluginLinkJuce.h"

class BalanceScopeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize  = 1 << fftOrder;   // 8192
    static constexpr int hopSize  = fftSize / 2;
    static constexpr int numBins  = fftSize / 2 + 1;

    BalanceScopeProcessor();
    ~BalanceScopeProcessor() override { stopTimer(); }

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

    // UI側から呼ぶ
    struct Snapshot
    {
        std::vector<double> avgPower, livePower, recentPower;   // recent = 直近 10 秒くらい（Plugin Link の LAST 10 s 用）
        int frames = 0;
        double recentSecs = 0.0;
        double sampleRate = 48000.0;
        bool playing = false;
    };
    void getSnapshot (Snapshot& s);
    void requestReset() { resetRequested = true; loudResetRequested = true; }

    // 今の目標カーブ（CUSTOM のときはつまみの値から作る）
    TargetCurve currentTarget() const;

    // ラウドネス（INTEGRATED と PEAK は再生中だけ測る。RESET で 0 から）
    const LoudnessMeter& getLoudness() const { return loudness; }

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "BalanceScope" };   // 自分のプリセット・取り消し

    // Plugin Link（同じ LINK 番号の Hush Field へ「出すぎの帯域」、Fader Pilot へオケの音量を送る。
    // Hush Field の削り量と Gain Stage の GAIN を受け取って表示する）
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setTrackName (*p.name); }
    struct LinkInfo
    {
        bool on = false, valid = false, recent = false;
        bandmath::Excess first, second;
        float musicDb = -120.0f;
        int peers = 0;
        plink::Peer tamer, gain;   // found = 見つかった
    };
    LinkInfo getLinkInfo() const;
    std::atomic<bool> refPaused { false };   // Reference が参照曲を流していて、測るのを止めている
    plink::Link link { plink::kindBalanceScope, apvts };

private:
    void timerCallback() override;   // 2 回/秒：出すぎの帯域を計算して掲示板へ
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void analyseFrame();

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, ring, fftData;
    int ringPos = 0, samplesSinceFFT = 0;
    double sr = 48000.0;

    std::vector<double> sumPower, livePower;
    int frameCount = 0;
    bool hostPlaying = true;

    juce::SpinLock lock;
    std::atomic<bool> resetRequested { false }, loudResetRequested { false };
    LoudnessMeter loudness;

    std::vector<float> linkCenters = bandmath::makeCenters();
    Snapshot linkSnap;
    LinkInfo linkInfo;               // メッセージスレッドだけで読み書き

    // オケの音量（Fader Pilot と同じ測り方：左右を足して 60 Hz 以下を切り、0.4 秒でならした RMS）
    struct Hp { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                float run (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; } };
    Hp musicHp;
    double musicMs = 0.0, musicCoef = 0.0;
    std::atomic<float> musicLevel { -120.0f };

    std::vector<double> recentPower;
    double recentSecs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceScopeProcessor)
};
