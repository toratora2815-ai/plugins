#pragma once
// 2つのバウンス（A と B）を同じ位置で鳴らし、一瞬で切り替える再生部分
#include <JuceHeader.h>

struct Track
{
    juce::AudioBuffer<float> buffer;   // 常に2ch
    double sampleRate = 44100.0;
    juce::String versionId;
    std::vector<float> overview;       // 波形の概形（0〜1）
    double lengthSeconds() const { return buffer.getNumSamples() / sampleRate; }
};

namespace mtmdsp
{
inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
}

class Player : public juce::AudioIODeviceCallback
{
public:
    // ---- メッセージスレッド ----
    // slot 0 = A, 1 = B。古い方を返す（メッセージスレッドで捨てる）
    std::shared_ptr<const Track> setTrack (int slot, std::shared_ptr<const Track> t)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (tracks[slot & 1], t);
        return t;
    }

    void seek (double seconds) { seekRequest = juce::jmax (0.0, seconds); }
    void setLoop (bool on, double s, double e) { loopStart = s; loopEnd = e; loopOn = on; }

    // 時間はすべて「A の時間」。B の位置 = A の位置 + offsetB
    std::atomic<double> offsetB { 0.0 };
    std::atomic<float> gainDbA { 0.0f }, gainDbB { 0.0f }, volumeDb { 0.0f };
    std::atomic<int> which { 0 };        // 0 = A, 1 = B
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 }, length { 0.0 };
    std::atomic<float> outPeak { 0.0f };

    // ---- オーディオスレッド ----
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        sr = d->getCurrentSampleRate() > 0 ? d->getCurrentSampleRate() : 48000.0;
        xf.reset (sr, 0.015);
        xf.setCurrentAndTargetValue ((float) which.load());
        for (auto* g : { &gA, &gB, &gOut }) g->reset (sr, 0.05);
        gA.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDbA.load()));
        gB.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDbB.load()));
        gOut.setCurrentAndTargetValue (outGainFor (volumeDb.load()));
        fade.reset (sr, 0.01);
        fade.setCurrentAndTargetValue (0.0f);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        juce::ScopedNoDenormals nd;
        for (int ch = 0; ch < numOut; ++ch) if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);
        if (numOut < 1 || out[0] == nullptr) return;
        float* L = out[0];
        float* R = numOut > 1 && out[1] != nullptr ? out[1] : nullptr;

        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked()) return;
        const Track* a = tracks[0].get();
        const Track* b = tracks[1].get();
        if (a == nullptr && b == nullptr) { length = 0.0; return; }

        const double offB = a != nullptr ? offsetB.load() : 0.0;
        const double lenA = a != nullptr ? a->lengthSeconds() : 0.0;
        const double lenB = b != nullptr ? b->lengthSeconds() - offB : 0.0;
        const double len = juce::jmax (lenA, lenB);
        length = len;

        const double req = seekRequest.exchange (-1.0);
        if (req >= 0.0) { pos = juce::jmin (req, len); fade.setCurrentAndTargetValue (0.0f); }

        xf.setTargetValue (which.load() == 1 ? 1.0f : 0.0f);
        gA.setTargetValue (juce::Decibels::decibelsToGain (gainDbA.load()));
        gB.setTargetValue (juce::Decibels::decibelsToGain (gainDbB.load()));
        gOut.setTargetValue (outGainFor (volumeDb.load()));
        const bool play = playing.load();
        fade.setTargetValue (play ? 1.0f : 0.0f);
        if (! play && fade.getCurrentValue() <= 0.0f) { position = pos; return; }

        const bool loop = loopOn.load();
        const double ls = loopStart.load(), le = juce::jmin (loopEnd.load(), len);
        const bool loopValid = loop && le - ls > 0.05;
        const double step = 1.0 / sr;
        float pk = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float x = xf.getNextValue(), ga = gA.getNextValue(), gb = gB.getNextValue();
            const float f = fade.getNextValue() * gOut.getNextValue();
            float al = 0, ar = 0, bl = 0, br = 0;
            if (a != nullptr && x < 1.0f) read (*a, pos, al, ar);
            if (b != nullptr && x > 0.0f) read (*b, pos + offB, bl, br);
            const float l = (al * ga * (1.0f - x) + bl * gb * x) * f;
            const float r = (ar * ga * (1.0f - x) + br * gb * x) * f;
            if (R != nullptr) { L[i] = l; R[i] = r; }
            else L[i] = 0.5f * (l + r);
            pk = juce::jmax (pk, std::abs (l), std::abs (r));

            pos += step;   // 止めるときも、フェードアウトの 10ms は進めながら消す
            if (loopValid && pos >= le) pos = ls;
            else if (pos >= len) { pos = 0.0; playing = false; }
        }
        position = pos;
        outPeak = juce::jmax (pk, outPeak.load() * 0.9f);
    }

private:
    static float outGainFor (float db) noexcept { return juce::jmax (1.0e-5f, juce::Decibels::decibelsToGain (db, -100.0f)); }

    static void read (const Track& t, double seconds, float& l, float& r) noexcept
    {
        const auto& buf = t.buffer;
        const int len = buf.getNumSamples();
        const double fpos = seconds * t.sampleRate;
        if (fpos < 0.0 || fpos >= len - 1 || len < 4) { l = r = 0.0f; return; }
        const long long i0 = (long long) fpos;
        const float frac = (float) (fpos - (double) i0);
        auto at = [&] (int ch, long long k) { return buf.getSample (ch, (int) juce::jlimit (0LL, (long long) len - 1, k)); };
        l = mtmdsp::hermite (at (0, i0 - 1), at (0, i0), at (0, i0 + 1), at (0, i0 + 2), frac);
        r = mtmdsp::hermite (at (1, i0 - 1), at (1, i0), at (1, i0 + 1), at (1, i0 + 2), frac);
    }

    juce::SpinLock lock;
    std::shared_ptr<const Track> tracks[2];
    double sr = 48000.0, pos = 0.0;
    std::atomic<double> seekRequest { -1.0 }, loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<bool> loopOn { false };
    juce::LinearSmoothedValue<float> xf, fade;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gA { 1.0f }, gB { 1.0f }, gOut { 1.0f };
};
