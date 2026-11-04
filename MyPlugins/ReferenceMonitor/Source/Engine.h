#pragma once
// 音の処理の中心。自分の曲（Logic）と参照曲（ファイル or ライブ）を受け取り、
// 音量合わせ・A/B・帯域ソロ・ステレオ切替・モニター音量をかけて出力する
#include <JuceHeader.h>
#include "LoudnessMeter.h"
#include "Spectrum.h"
#include "FilePlayer.h"
#include "ListenSim.h"
#include "Punch.h"
#include "LowEnd.h"

class Engine
{
public:
    // levelStream 以降は配信サービス（refdsp::streamingPlatform の順: Spotify / Apple Music / YouTube）
    enum Level  { levelOff = 0, levelMatch = 1, levelStream = 2, numLevels = 5 };
    enum Source { sourceLive = 0, sourceFile = 1 };

    Engine();
    void prepare (double sampleRate, int maxBlock);

    // オーディオスレッド。mix / live は nullptr なら無音扱い
    void process (const float* mixL, const float* mixR,
                  const float* liveL, const float* liveR,
                  float* outL, float* outR, int n) noexcept;

    void resetMeters() { mixMeter.requestReset(); refMeter.requestReset(); mixSpec.requestReset(); refSpec.requestReset();
                         mixPunch.requestReset(); refPunch.requestReset();
                         mixLow.requestReset(); refLow.requestReset(); }
    void resetRefMeters() { refMeter.requestReset(); refSpec.requestReset(); refPunch.requestReset(); refLow.requestReset(); }

    // ---- 設定（どのスレッドからでも）----
    std::atomic<bool>  ab { false };              // true = REFERENCE
    std::atomic<int>   refSource { sourceLive };
    std::atomic<int>   level { levelMatch };
    std::atomic<int>   band { 0 };                // 0 全体 1 低 2 中 3 高
    std::atomic<int>   stereo { 0 };              // 0 ST 1 MONO 2 SIDE
    std::atomic<float> trimDb { 0.0f };
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<bool>  dim { false }, mute { false };

    // ---- 計測・表示用 ----
    refdsp::LoudnessMeter mixMeter, refMeter;
    SpectrumAnalyser mixSpec, refSpec;
    refdsp::PunchMeter mixPunch, refPunch;   // 帯域ごとの迫力（PUNCH 表示）
    LowAnalyser mixLow, refLow;              // 低域を細かく（LOW 表示）
    FilePlayer player;
    std::atomic<float> mixGainDb { 0.0f }, refGainDb { 0.0f };
    std::atomic<float> mixPeakDb { -120.0f }, livePeakDb { -120.0f };
    double getSampleRate() const { return sr; }

    // LISTEN AS（スマホ・車などの聞こえ方）。sim.target に番号を入れる
    refdsp::ListenSim sim;

    // 配信サービス platform（0 = Spotify ...）で、この曲が何 dB 上げ下げされるか。-200 = まだ測れていない
    float platformGainDb (int platform, bool ref) const noexcept;

private:
    void processChunk (const float* mixL, const float* mixR, const float* liveL, const float* liveR,
                       float* outL, float* outR, int n) noexcept;
    void silenceWatch (float peak, int n, int& silentSamples, bool& armed, bool isRef) noexcept;

    static constexpr int kChunk = 1024;
    juce::AudioBuffer<float> refBuf { 2, kChunk }, zeroBuf { 2, kChunk };
    double sr = 48000.0;

    juce::LinearSmoothedValue<float> abSmooth, wSt, wMono, wSide, wFull, wLow, wMid, wHigh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> mixGain { 1.0f }, refGain { 1.0f }, outGain { 1.0f };
    juce::dsp::LinkwitzRileyFilter<float> xoLow, xoHigh;
    bool platformBasis (bool ref, float& lufs, float& peakDb) const noexcept;
    float heldMatchDb = 0.0f, heldMixStreamDb = 0.0f, heldRefStreamDb = 0.0f;
    int mixSilent = 0, refSilent = 0;
    bool mixArmed = false, refArmed = false;
    int lastSource = -1;
};
