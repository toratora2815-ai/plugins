#pragma once
// Mac の入力デバイス（BlackHole など）を開いて、その音をプラグインに渡す
#include <JuceHeader.h>
#include "DriftReader.h"

class LiveInput : private juce::AudioIODeviceCallback
{
public:
    LiveInput();
    ~LiveInput() override;

    // ---- メッセージスレッドから ----
    juce::StringArray getDeviceNames();
    juce::String open (const juce::String& deviceName, double preferredRate, int hostBlockSize); // 失敗時はエラー文
    void close();
    bool isOpen() const { return device != nullptr; }
    juce::String getOpenName() const { return openName; }
    double getDeviceRate() const { return deviceRate.load(); }

    // ---- オーディオスレッドから ----
    void read (float* L, float* R, int n, double hostRate) noexcept;

    // ---- UI 用 ----
    float getInputPeakDb() const noexcept { return inputPeakDb.load(); }
    bool isReceiving() const noexcept { return receiving.load(); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String openName;

    refdsp::StereoRing ring { 1 << 16 };
    refdsp::DriftReader reader;
    std::atomic<double> deviceRate { 48000.0 };
    std::atomic<int> generation { 0 }, targetFrames { 4096 };
    std::atomic<bool> active { false }, receiving { false };
    std::atomic<float> inputPeakDb { -120.0f };
    int readerGeneration = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveInput)
};
