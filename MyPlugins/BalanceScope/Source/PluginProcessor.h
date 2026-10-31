#pragma once
#include <JuceHeader.h>

class BalanceScopeProcessor : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize  = 1 << fftOrder;   // 8192
    static constexpr int hopSize  = fftSize / 2;
    static constexpr int numBins  = fftSize / 2 + 1;

    BalanceScopeProcessor();
    ~BalanceScopeProcessor() override = default;

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
        std::vector<double> avgPower, livePower;
        int frames = 0;
        double sampleRate = 48000.0;
        bool playing = false;
    };
    void getSnapshot (Snapshot& s);
    void requestReset() { resetRequested = true; }

    juce::AudioProcessorValueTreeState apvts;

private:
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
    std::atomic<bool> resetRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceScopeProcessor)
};
