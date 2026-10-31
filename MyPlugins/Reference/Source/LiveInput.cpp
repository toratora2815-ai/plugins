#include "LiveInput.h"

LiveInput::LiveInput()
{
   #if JUCE_MAC
    type.reset (juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
   #elif JUCE_LINUX && JUCE_ALSA
    type.reset (juce::AudioIODeviceType::createAudioIODeviceType_ALSA());
   #endif
}

LiveInput::~LiveInput()
{
    close();
}

juce::StringArray LiveInput::getDeviceNames()
{
    if (type == nullptr) return {};
    type->scanForDevices();
    return type->getDeviceNames (true);
}

juce::String LiveInput::open (const juce::String& deviceName, double preferredRate, int hostBlockSize)
{
    close();
    if (type == nullptr) return "This system has no audio device support";

    type->scanForDevices();
    if (! type->getDeviceNames (true).contains (deviceName))
        return "Device not found: " + deviceName;

    std::unique_ptr<juce::AudioIODevice> d (type->createDevice ({}, deviceName));
    if (d == nullptr) return "Could not create device: " + deviceName;

    const auto names = d->getInputChannelNames();
    if (names.isEmpty()) return deviceName + " has no inputs";

    juce::BigInteger inChans;
    inChans.setRange (0, juce::jmin (2, names.size()), true);

    // できればホストと同じサンプルレートで開く（変換なしで一番きれい）
    double rate = 0.0;
    const auto rates = d->getAvailableSampleRates();
    for (auto r : rates)
        if (std::abs (r - preferredRate) < 1.0) rate = r;
    if (rate <= 0.0) rate = d->getCurrentSampleRate();
    if (rate <= 0.0 && ! rates.isEmpty()) rate = rates[0];

    const int bufSize = d->getDefaultBufferSize();
    const auto err = d->open (inChans, juce::BigInteger(), rate, bufSize);
    if (err.isNotEmpty()) return err;

    const double actualRate = d->getCurrentSampleRate() > 0.0 ? d->getCurrentSampleRate() : rate;
    deviceRate = actualRate;
    // 目標の溜まり量: 約85ms＋両側のバッファ分（参照用なので遅延より安定性を優先）
    const int devBuf = juce::jmax (64, d->getCurrentBufferSizeSamples());
    const int hostBufInDev = (int) std::ceil (juce::jmax (64, hostBlockSize) * actualRate / juce::jmax (8000.0, preferredRate));
    targetFrames = 4096 + 2 * devBuf + 2 * hostBufInDev;
    ++generation;

    device = std::move (d);
    openName = deviceName;
    active = true;
    device->start (this);
    return {};
}

void LiveInput::close()
{
    active = false;
    receiving = false;
    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }
    openName.clear();
    inputPeakDb = -120.0f;
}

void LiveInput::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                  float* const* out, int numOut,
                                                  int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);

    const float* L = numIn > 0 ? in[0] : nullptr;
    const float* R = numIn > 1 ? in[1] : L;
    ring.write (L, R, n);

    float pk = 0.0f;
    if (L != nullptr)
        pk = juce::jmax (juce::FloatVectorOperations::findMaximum (L, n),
                         -juce::FloatVectorOperations::findMinimum (L, n));
    if (R != nullptr && R != L)
        pk = juce::jmax (pk, juce::FloatVectorOperations::findMaximum (R, n),
                         -juce::FloatVectorOperations::findMinimum (R, n));
    inputPeakDb = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -120.0f;
}

void LiveInput::read (float* L, float* R, int n, double hostRate) noexcept
{
    if (! active.load())
    {
        juce::FloatVectorOperations::clear (L, n);
        juce::FloatVectorOperations::clear (R, n);
        receiving = false;
        return;
    }

    const int gen = generation.load();
    if (gen != readerGeneration)
    {
        readerGeneration = gen;
        reader.setTarget (targetFrames.load());
    }

    const bool ok = reader.read (ring, L, R, n, deviceRate.load() / hostRate, hostRate);
    receiving = ok;
}
