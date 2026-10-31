#pragma once
// Mac のほかのアプリ（Logic Pro・ミュージック）の音を直接受け取り、出力デバイスに鳴らす部分。
// macOS 14.2 以降の「プロセスタップ」を使う。受け取ったアプリの元の音は消える（二重に鳴らない）。
// アプリを終了すると、タップは自動で消え、元の鳴り方に戻る。
#include <JuceHeader.h>
#include "Engine.h"

struct AppAudioProcess
{
    int pid = 0;
    juce::String bundleId, name;
    bool runningOutput = false;
};

struct OutputDevice
{
    juce::String uid, name;
};

class CaptureEngine
{
public:
    explicit CaptureEngine (Engine&);
    ~CaptureEngine();

    static juce::Array<OutputDevice> listOutputDevices();
    static juce::String defaultOutputUID();
    static std::vector<AppAudioProcess> listAudioProcesses();
    static bool matches (const juce::String& bundleId, const juce::String& target)
    {
        return target.isNotEmpty() && (bundleId == target || bundleId.startsWith (target + "."));
    }

    // mixBundle / refBundle が空ならその側はタップしない。戻り値はエラー文（成功なら空）
    juce::String start (const juce::String& outputUID, const juce::String& mixBundle, const juce::String& refBundle);
    void stop();
    bool isRunning() const;

    // 状態（UI 用）
    juce::String getSignature() const { return signature; }     // 今つないでいる構成
    int numMixProcesses() const { return mixCount; }
    int numRefProcesses() const { return refCount; }
    double getSampleRate() const;
    juce::String getLog() const;

    // 開始すべき構成を表す文字列（変わったらつなぎ直す）
    static juce::String makeSignature (const juce::String& outputUID, const juce::String& mixBundle, const juce::String& refBundle,
                                       const std::vector<AppAudioProcess>& procs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Engine& engine;
    juce::String signature;
    int mixCount = 0, refCount = 0;
};
