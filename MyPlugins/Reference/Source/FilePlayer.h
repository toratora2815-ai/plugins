#pragma once
// 参照曲ファイルの再生（ループ・シーク・サンプルレート変換）
#include <JuceHeader.h>
#include "DriftReader.h"   // hermite()

struct RefAudio
{
    juce::AudioBuffer<float> buffer;   // 常に2ch
    double sampleRate = 44100.0;
    juce::String name;
    std::vector<float> overview;       // 波形の概形（0〜1）
};

class FilePlayer
{
public:
    // ---- メッセージスレッド: 新しい音源に差し替え。古いものを返す（呼び出し側で破棄）----
    std::unique_ptr<RefAudio> swapAudio (std::unique_ptr<RefAudio> next)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (audio, next);
        posFrames = 0.0;
        position = 0.0f;
        return next;
    }
    const RefAudio* getAudio() const { return audio.get(); }   // メッセージスレッド専用

    void seek (float normalised) { seekRequest = juce::jlimit (0.0f, 1.0f, normalised); }

    std::atomic<bool> userPlaying { false };
    std::atomic<float> position { 0.0f };   // 0〜1
    std::atomic<bool> isRunning { false };  // 実際に鳴っているか

    // ---- オーディオスレッド ----
    void process (float* L, float* R, int n, double hostRate, bool play, bool loop) noexcept
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked() || audio == nullptr || audio->buffer.getNumSamples() < 4)
        {
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
            isRunning = false;
            return;
        }

        const auto& buf = audio->buffer;
        const int len = buf.getNumSamples();
        const float* src[2] = { buf.getReadPointer (0), buf.getReadPointer (1) };

        const float sr = seekRequest.exchange (-1.0f);
        if (sr >= 0.0f) { posFrames = (double) sr * (len - 1); ended = false; }

        if (! play) ended = false;
        if (! play || ended)
        {
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
            isRunning = false;
            return;
        }

        const double step = audio->sampleRate / hostRate;
        auto at = [&] (int ch, long long i) -> float
        {
            if (i < 0) i = loop ? i + len : 0;
            if (i >= len) i = loop ? i - len : len - 1;
            return src[ch][(size_t) juce::jlimit (0LL, (long long) len - 1, i)];
        };

        bool stopped = false;
        for (int i = 0; i < n; ++i)
        {
            if (stopped) { L[i] = R[i] = 0.0f; continue; }
            const double fl = std::floor (posFrames);
            const long long i0 = (long long) fl;
            const float t = (float) (posFrames - fl);
            L[i] = refdsp::hermite (at (0, i0 - 1), at (0, i0), at (0, i0 + 1), at (0, i0 + 2), t);
            R[i] = refdsp::hermite (at (1, i0 - 1), at (1, i0), at (1, i0 + 1), at (1, i0 + 2), t);
            posFrames += step;
            if (posFrames >= (double) len)
            {
                if (loop) posFrames -= (double) len;
                else { posFrames = 0.0; stopped = true; ended = true; userPlaying = false; }
            }
        }
        position = (float) (posFrames / (double) len);
        isRunning = ! stopped;
    }

private:
    juce::SpinLock lock;
    std::unique_ptr<RefAudio> audio;
    double posFrames = 0.0;
    bool ended = false;
    std::atomic<float> seekRequest { -1.0f };
};
