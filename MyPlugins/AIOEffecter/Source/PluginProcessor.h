#pragma once
#include <JuceHeader.h>
#include "PluginLinkJuce.h"
#include "PresetBar.h"
#include "GainMatch.h"
#include "fx/Rack.h"   // 音の部分（JUCE なし。将来 AIO Guitar のエフェクター欄にも使えるように分けてある）

// Plugin Link の種類（PluginLink.h には未登録。Gain Stage の LEVEL MATCH を受けるだけなので Shared は変えない）
// 17〜27 は他のプラグインが使用中
constexpr uint32_t kAIOEffecterLinkKind = 28;

class AIOEffecterProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    AIOEffecterProcessor();
    ~AIOEffecterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;   // ホストのリセット（停止・位置の移動・書き出しの前）で響き・下げを消す
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return rack.tailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // パラメーター ID
    static juce::String typeId (int slot) { return "s" + juce::String (slot + 1) + "_type"; }
    static juce::String onId (int slot)   { return "s" + juce::String (slot + 1) + "_on"; }
    static juce::String knobId (int slot, int k) { return "s" + juce::String (slot + 1) + "_k" + juce::String (k + 1); }

    // 画面から: スロットの部品を変える（つまみを部品の初期値に、ON に）
    void setSlotType (int slot, int type);
    void moveSlot (int from, int to);       // 中身を入れ替える（上下の矢印）
    void loadRackPreset (int index);        // 内蔵ラック
    static juce::StringArray rackPresetNames();

    int slotType (int slot) const
    {
        const int t = (int) pType[(size_t) slot]->load();
        return (t >= 0 && t <= aiofx::numTypes()) ? t : 0;
    }
    float slotMeter (int slot) const { return rack.meter (slot); }

    juce::AudioProcessorValueTreeState apvts;

    myp::Presets myPresets { *this, apvts, "AIOEffecter" };   // 自分のプリセット・取り消し

    plink::Link link { (plink::Kind) kAIOEffecterLinkKind, apvts };   // Plugin Link
    plink::LevelMatch match;   // 出口を Gain Stage の TARGET にそろえる
    myp::GainMatch gainMatch { *this, myPresets };   // GAIN MATCH

    void updateTrackProperties (const TrackProperties& tp) override { if (tp.name.has_value()) { link.setTrackName (*tp.name); gainMatch.setTrackName (*tp.name); } }

    std::atomic<float> outPeakDb { -100.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void readSlots (std::array<aiofx::SlotKnobs, aiofx::kSlots>& dst) const;
    void setParam (const juce::String& id, float plainValue);

    std::array<std::atomic<float>*, aiofx::kSlots> pType {}, pOn {};
    std::array<std::array<std::atomic<float>*, aiofx::kMaxKnobs>, aiofx::kSlots> pKnob {};
    std::atomic<float>* pGainMatch = nullptr;
    std::atomic<float>* pLinkMatch = nullptr;
    int tVerb = 0, tSpring = 0, tDelay = 0, tLimit = 0, tMax = 0, tPeak = 0;   // GAIN MATCH の判断に使う部品の番号
    struct MixRuleDef { const char* id; int knob; bool dry; };
    struct MixRule { int type, knob; bool dryKnob; };
    std::vector<MixRule> mixRules;   // 0.3.0 からの響き・ディレイの MIX / DRY のつまみ（GAIN MATCH の amount）

    aiofx::Rack rack;
    std::array<aiofx::SlotKnobs, aiofx::kSlots> audioKnobs {};   // オーディオスレッド用（毎ブロック読み直す）
    std::array<aiofx::SlotKnobs, aiofx::kSlots> msgKnobs {};     // タイマー用
    std::vector<float> monoTmp;
    juce::SpinLock prepLock;   // prepareToPlay とタイマーの部品作りが重ならないように（オーディオスレッドは使わない）
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIOEffecterProcessor)
};
