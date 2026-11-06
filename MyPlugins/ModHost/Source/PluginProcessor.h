#pragma once
#include <JuceHeader.h>
#include "ModEngine.h"
#include "AuList.h"

// Mod Host: 好きな AU エフェクトを中に読み込み、MOD 1〜4（LFO / ENV / SIDECHAIN / RANDOM / STEPS）で
// 中のプラグインのどのつまみでも自動で動かす入れ物。
class ModHostProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorListener,
                         private juce::AsyncUpdater
{
public:
    ModHostProcessor();
    ~ModHostProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds.load(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String mid (int m, const char* what) { return "m" + juce::String (m + 1) + what; }
    static juce::String amountId (int m, int t) { return "m" + juce::String (m + 1) + "a" + juce::String (t + 1); }

    juce::AudioProcessorValueTreeState apvts;

    // ── 中のプラグイン（呼ぶのは画面のスレッドから） ──
    std::vector<mh::AuEntry> scanEffects() const;
    void loadPlugin (const mh::AuEntry&);
    void unloadPlugin();
    juce::AudioPluginInstance* hostedPlugin() const { return hosted.get(); }
    juce::String hostedName() const { return hosted != nullptr ? hosted->getName() : juce::String(); }
    bool isLoading() const { return loading; }
    juce::String lastError() const { return loadError; }

    // 画面は中のプラグインが替わる直前・直後に知らせを受ける（直前に中の画面を消すため）
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void hostedPluginAboutToChange() = 0;
        virtual void hostedPluginChanged() = 0;
    };
    void addHostListener (Listener* l) { hostListeners.add (l); }
    void removeHostListener (Listener* l) { hostListeners.remove (l); }

    // ── つなぐ先（MOD m の t 番目 → 中のプラグインのつまみの番号。-1 = なし） ──
    int targetOf (int m, int t) const { return targets[m][t].load(); }
    void setTarget (int m, int t, int paramIndex);
    juce::String paramName (int paramIndex) const;
    int numHostedParams() const;
    // LEARN: 次に中のプラグインで動かしたつまみを、この行につなぐ
    void startLearn (int m, int t) { learnSlot = m * mh::numTargets + t; }
    void stopLearn() { learnSlot = -1; }
    int learning() const { return learnSlot.load(); }

    // 画面用: 各 MOD の今の値、各行のつまみの元の位置と今の位置（0〜1）
    float modValue (int m) const { return modNow[m].load(); }
    bool modBipolar (int m) const { return bipolarParam[m]->load() > 0.5f; }
    float routeBase (int m, int t) const { return shownBase[m][t].load(); }
    float routeNow (int m, int t) const { return shownNow[m][t].load(); }

    // STEPS の値（0〜1）
    float stepValue (int m, int k) const { return stepVals[m][k].load(); }
    void setStepValue (int m, int k, float v) { stepVals[m][k].store (juce::jlimit (0.0f, 1.0f, v)); }

private:
    // juce::AudioProcessorListener（中のプラグインから）
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void handleAsyncUpdate() override;

    void finishLoad (std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error,
                     std::unique_ptr<juce::XmlElement> restore);
    void loadFromDescription (const juce::PluginDescription&, std::unique_ptr<juce::XmlElement> restore);
    void installHosted (std::unique_ptr<juce::AudioPluginInstance>);
    void prepareHosted (juce::AudioPluginInstance&);
    mh::Settings readSettings (int m) const;
    void processHosted (juce::AudioBuffer<float>& main, int start, int n);
    std::unique_ptr<juce::XmlElement> routesToXml() const;

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> hosted;   // 音のスレッドは hostLock を取ってから触る
    juce::PluginDescription hostedDesc;
    juce::CriticalSection hostLock;    // 音のスレッドと入れ替え
    juce::CriticalSection stateLock;   // 保存と入れ替え（音のスレッドは取らない）
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);
    std::atomic<int> learnedSlot { -1 }, learnedIndex { -1 };
    std::atomic<bool> hostChanged { false }, restorePending { false };
    bool loading = false;
    juce::String loadError;
    std::unique_ptr<juce::XmlElement> pendingState;      // 読み込み中に保存を頼まれたとき、これを返す
    juce::ListenerList<Listener> hostListeners;

    // 中のプラグイン用の作業バッファ（hostLock の中で大きさを変える）
    juce::AudioBuffer<float> scratch;
    int hostIns = 2, hostOuts = 2;
    double rate = 48000.0;
    int maxBlock = 512;

    // つまみごとの「元の位置」と、最後にこちらが入れた値（音のスレッドだけ。hostLock の中で大きさを変える）
    std::vector<float> baseVal, lastSet;
    std::vector<char> wasActive;
    std::vector<int> activeList, prevActive;
    std::vector<float> offsets;

    std::atomic<int> targets[mh::numMods][mh::numTargets];
    std::atomic<float> stepVals[mh::numMods][mh::maxSteps];
    std::atomic<float> modNow[mh::numMods];
    std::atomic<float> shownBase[mh::numMods][mh::numTargets], shownNow[mh::numMods][mh::numTargets];
    // 保存から戻すときの元の位置（読み込みが終わってから入れる）
    std::atomic<int> learnSlot { -1 };
    std::atomic<double> tailSeconds { 0.0 };

    mh::Modulator mods[mh::numMods];
    std::atomic<float>* typeParam[mh::numMods] {};
    std::atomic<float>* shapeParam[mh::numMods] {};
    std::atomic<float>* syncParam[mh::numMods] {};
    std::atomic<float>* divParam[mh::numMods] {};
    std::atomic<float>* rateParam[mh::numMods] {};
    std::atomic<float>* attackParam[mh::numMods] {};
    std::atomic<float>* releaseParam[mh::numMods] {};
    std::atomic<float>* gainParam[mh::numMods] {};
    std::atomic<float>* smoothParam[mh::numMods] {};
    std::atomic<float>* stepsParam[mh::numMods] {};
    std::atomic<float>* bipolarParam[mh::numMods] {};
    std::atomic<float>* phaseParam[mh::numMods] {};
    std::atomic<float>* amountParam[mh::numMods][mh::numTargets] {};
    juce::SmoothedValue<float> amountSmooth[mh::numMods][mh::numTargets];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModHostProcessor)
};
