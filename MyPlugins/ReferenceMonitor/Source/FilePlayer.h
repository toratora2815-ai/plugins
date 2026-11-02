#pragma once
// 参照曲ファイルの再生（ループ区間・シーク・サンプルレート変換）
#include <JuceHeader.h>

namespace refdsp
{
inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
}

struct RefAudio
{
    juce::AudioBuffer<float> buffer;   // 常に2ch
    double sampleRate = 44100.0;
    juce::String path, name;
    std::vector<float> overview;       // 波形の概形（0〜1）
    float integratedLufs = -200.0f;    // 曲全体の音量（配信サービスの正規化に使う）。-200 = 不明
    float peakDb = -120.0f;
    double lengthSeconds() const { return buffer.getNumSamples() / sampleRate; }
};

class FilePlayer
{
public:
    // ---- メッセージスレッド ----
    std::unique_ptr<RefAudio> swapAudio (std::unique_ptr<RefAudio> next)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (audio, next);
        posFrames = 0.0;
        positionSec = 0.0;
        lengthSec = audio != nullptr ? audio->lengthSeconds() : 0.0;
        fileLufs = audio != nullptr ? audio->integratedLufs : -200.0f;
        filePeakDb = audio != nullptr ? audio->peakDb : -120.0f;
        playing = false;
        return next;
    }
    const RefAudio* getAudio() const { return audio.get(); }   // メッセージスレッド専用

    void seekSeconds (double s) { seekRequest = juce::jmax (0.0, s); }
    void setLoop (bool on, double startSec, double endSec)
    {
        loopStart = startSec; loopEnd = endSec; loopOn = on;
    }

    std::atomic<bool> playing { false };
    std::atomic<double> positionSec { 0.0 }, lengthSec { 0.0 };
    std::atomic<float> fileLufs { -200.0f }, filePeakDb { -120.0f };
    std::atomic<int> jumpCounter { 0 };   // ループで頭に戻った回数（UI がメーターの区切りに使う）

    // ---- オーディオスレッド ----
    void process (float* L, float* R, int n, double hostRate) noexcept
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked() || audio == nullptr || audio->buffer.getNumSamples() < 4)
        {
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
            return;
        }

        const auto& buf = audio->buffer;
        const int len = buf.getNumSamples();
        const double fileRate = audio->sampleRate;
        const float* src[2] = { buf.getReadPointer (0), buf.getReadPointer (1) };

        const double sr = seekRequest.exchange (-1.0);
        if (sr >= 0.0) posFrames = juce::jlimit (0.0, (double) (len - 1), sr * fileRate);

        if (! playing.load())
        {
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
            positionSec = posFrames / fileRate;
            return;
        }

        const bool loop = loopOn.load();
        double ls = loopStart.load() * fileRate, le = loopEnd.load() * fileRate;
        le = juce::jmin (le, (double) len);
        const bool loopValid = loop && le - ls > fileRate * 0.05;

        const double step = fileRate / hostRate;
        auto at = [&] (int ch, long long i) -> float
        {
            return src[ch][(size_t) juce::jlimit (0LL, (long long) len - 1, i)];
        };

        for (int i = 0; i < n; ++i)
        {
            if (! playing.load(std::memory_order_relaxed)) { L[i] = R[i] = 0.0f; continue; }
            const double fl = std::floor (posFrames);
            const long long i0 = (long long) fl;
            const float t = (float) (posFrames - fl);
            L[i] = refdsp::hermite (at (0, i0 - 1), at (0, i0), at (0, i0 + 1), at (0, i0 + 2), t);
            R[i] = refdsp::hermite (at (1, i0 - 1), at (1, i0), at (1, i0 + 1), at (1, i0 + 2), t);
            posFrames += step;

            if (loopValid && posFrames >= le && posFrames - step < le)
            {
                posFrames = ls + (posFrames - le);
                ++jumpCounter;
            }
            else if (posFrames >= (double) len)
            {
                posFrames = 0.0;
                playing = false;
            }
        }
        positionSec = posFrames / fileRate;
    }

private:
    juce::SpinLock lock;
    std::unique_ptr<RefAudio> audio;
    double posFrames = 0.0;
    std::atomic<double> seekRequest { -1.0 }, loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<bool> loopOn { false };
};
