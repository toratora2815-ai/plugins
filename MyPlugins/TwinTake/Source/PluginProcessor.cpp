#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float v) { return a * std::pow (b / a, v); },
                 [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); },
                 [] (float a, float b, float x) { return juce::jlimit (a, b, x); } };
    }
    constexpr float kDoubleOff = -40.0f;   // DOUBLE をいちばん左にしたら無音（OFF）
    constexpr float kLowKeepOff = 70.0f;   // LOW KEEP がこれより下なら OFF
}

TwinTakeProcessor::TwinTakeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    apvts.addParameterListener ("mode", this);
    // DOUBLE ONLY は「挿し方」の切り替えなので、自分のプリセット・取り消しでは変えない（AUX でプリセットを選ぶたびに原音が戻らないように）
    myPresets.exclude ("doubleOnly");
}

TwinTakeProcessor::~TwinTakeProcessor()
{
    apvts.removeParameterListener ("mode", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout TwinTakeProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using I = juce::AudioParameterInt;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // MODE は遅れ（レイテンシー）が変わるので、オートメーションできないようにする
    ps.push_back (std::make_unique<C> (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Vocal", "Guitar" }, 0,
                                       juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "voices", 1 }, "Voices", juce::StringArray { "1", "2", "4" }, 1));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "double", 1 }, "Double", juce::NormalisableRange<float> (kDoubleOff, 0.0f, 0.1f, 2.0f), -6.0f,
                                       A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                                           return v <= kDoubleOff + 0.05f ? juce::String ("OFF") : juce::String (v, 1) + " dB"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "loose", 1 }, "Loose", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "width", 1 }, "Width", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f, A().withLabel ("%").withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "lowKeep", 1 }, "Low Keep", logRange (60.0f, 400.0f), 200.0f,
                                       A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                                           return v < kLowKeepOff ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz"; })));
    ps.push_back (std::make_unique<I> (juce::ParameterID { "take", 1 }, "Take", 1, 8, 1));
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    // 1.1.0: 原音を出さずダブルだけ（AUX のセンドに挿す用）。版のヒント 11（GAIN MATCH の 10 より後ろ = 今ある曲のパラメーター番号をずらさない）
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "doubleOnly", 11 }, "Double Only", false,
                                                               juce::AudioParameterBoolAttributes().withAutomatable (false)));
    return { ps.begin(), ps.end() };
}

bool TwinTakeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in == out) return true;
    return in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();   // Logic の「Mono → Stereo」（おすすめ）
}

twin::Params TwinTakeProcessor::currentParams() const
{
    twin::Params p;
    p.mode = mode();
    p.voices = voices();
    const float d = apvts.getRawParameterValue ("double")->load();
    p.doubleDb = d <= kDoubleOff + 0.05f ? -100.0f : d;
    p.loose = apvts.getRawParameterValue ("loose")->load() / 100.0f;
    p.width = apvts.getRawParameterValue ("width")->load() / 100.0f;
    const float lk = apvts.getRawParameterValue ("lowKeep")->load();
    p.lowKeepHz = lk < kLowKeepOff ? 0.0f : lk;
    p.take = juce::roundToInt (apvts.getRawParameterValue ("take")->load());
    p.mono = monoListen.load();
    p.doubleOnly = doubleOnly();
    p.trackSeed = trackSeed.load();
    return p;
}

void TwinTakeProcessor::updateLatency()
{
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    setLatencySamples (mode() == 1 ? (int) std::lround (twin::Engine::guitarLatencyMs * 0.001 * sr) : 0);
}

void TwinTakeProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "mode") updateLatency();
}

void TwinTakeProcessor::updateTrackProperties (const TrackProperties& tp)
{
    if (! tp.name.has_value()) return;
    link.setTrackName (*tp.name);
    gainMatch.setTrackName (*tp.name);
    const auto name = *tp.name;
    if (name.isEmpty()) return;
    const juce::ScopedLock sl (seedLock);
    if (! seedSet || (justLoaded && savedTrack.isNotEmpty() && name != savedTrack))
    {
        trackSeed.store ((uint32_t) name.hashCode());   // トラックごとに違うテイクになる
        seedSet = true;
    }
    justLoaded = false;
    currentTrack = name;
}

void TwinTakeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nOut = juce::jmax (1, getTotalNumOutputChannels());
    engine.setParams (currentParams());
    engine.prepare (sampleRate);
    updateLatency();
    monoTrack.store (getTotalNumOutputChannels() < 2);
    gainMatch.prepare (sampleRate, nOut, samplesPerBlock);
    freeRun = 0;
}

void TwinTakeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    // NaN・無限大・とんでもない値は 0 に（前のプラグインの不具合 1 回で、GAIN MATCH の測り値や後ろのリバーブまで壊れないように）
    for (int ch = 0; ch < juce::jmax (numIn, 1) && ch < buffer.getNumChannels(); ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i) if (! (std::fabs (x[i]) < 1.0e6f)) x[i] = 0.0f;
    }
    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    // モノ → ステレオ: 右に左を写してから
    if (numIn == 1 && numOut >= 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch) buffer.clear (ch, 0, n);

    // 曲の位置（再生中だけ）。同じ所は、再生のたび・書き出しでも同じダブルになる
    int64_t tl = -1;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying())
                if (auto t = pos->getTimeInSamples()) tl = *t;
    if (tl < 0) tl = freeRun;
    freeRun += n;

    const auto prm = currentParams();
    if (needReset.exchange (false)) engine.reset();
    engine.setParams (prm);
    const int chs = juce::jmin (numOut, 2);
    engine.process (buffer.getWritePointer (0), chs > 1 ? buffer.getWritePointer (1) : nullptr, chs, n, tl);

    const auto& st = engine.getStats();
    for (int v = 0; v < twin::Engine::maxVoices; ++v) { shownDelayMs[v].store (st.lastDelayMs[v]); shownCents[v].store (st.lastCents[v]); }
    shownOnsets.store ((int) st.onsets);
    shownGate.store (st.gate);

    // GAIN MATCH: MONO を聴いている間・DOUBLE ONLY の間は比べる元がないので効かせない（覚えた値はそのまま）
    gainMatch.process (buffer, chs, getLatencySamples(), (prm.mono || prm.doubleOnly) ? 0.0f : 1.0f);
}

void TwinTakeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    gainMatch.saveTo (st);
    {
        const juce::ScopedLock sl (seedLock);
        // トラック名がまだ届いていなければ、読み込んだときの名前のまま保存（空で保存すると、次に「別のトラックへコピーした」か分からなくなる）
        if (seedSet) { st.setProperty ("ttSeed", (int) trackSeed.load(), nullptr); st.setProperty ("ttTrack", currentTrack.isNotEmpty() ? currentTrack : savedTrack, nullptr); }
    }
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void TwinTakeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            gainMatch.loadFrom (apvts.state);
            updateLatency();
            const juce::ScopedLock sl (seedLock);
            if (apvts.state.hasProperty ("ttSeed"))
            {
                trackSeed.store ((uint32_t) (int) apvts.state.getProperty ("ttSeed"));
                seedSet = true;
                savedTrack = apvts.state.getProperty ("ttTrack").toString();
                justLoaded = currentTrack.isEmpty();   // 名前がまだなら、来たときに比べる
                // もうトラックにいて名前が違う（別のトラックの設定を読んだ）なら、このトラックの名前から作り直す
                if (currentTrack.isNotEmpty() && savedTrack.isNotEmpty() && currentTrack != savedTrack)
                    trackSeed.store ((uint32_t) currentTrack.hashCode());
            }
        }
}

//==============================================================================
// 内蔵プリセット（ミックスエンジニア係の案。名前と数値は自分で付けた）
namespace
{
    struct FP { const char* name; const char* values; };
    const FP kFactory[] = {
        { "Init",         "" },
        { "Lead Thick",   "mode=0,voices=0,double=-8,loose=20" },                 // 1 本をまん中に、うっすら厚く
        { "Lead Double",  "mode=0,voices=1,double=-6,loose=30,width=80" },        // いちばんふつうのダブル
        { "Chorus Stack", "mode=0,voices=2,double=-3,loose=50,width=100" },       // サビの重ね（4 本）
        { "Rap Tight",    "mode=0,voices=1,double=-4,loose=10,width=60" },        // ラップ・速い言葉は、ずれを小さく
        { "Guitar L-R",   "mode=1,double=0,loose=40,width=100" },                 // 1 本録りのギターを左右に
    };
}

int TwinTakeProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String TwinTakeProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void TwinTakeProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    myPresets.snapshot();
    // まず全部を初期値に（GAIN MATCH・LINK の番号・DOUBLE ONLY は触らない）
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == myp::GainMatch::paramId || id == "linkGroup" || id == "doubleOnly") continue;
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
    for (auto& kv : juce::StringArray::fromTokens (f.values, ",", ""))
    {
        const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
        const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (auto* r = apvts.getParameter (id))
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->convertTo0to1 (v));
            r->endChangeGesture();
        }
    }
    myPresets.commitSinceSnapshot();
    apvts.state.setProperty ("mypPreset", juce::String ("[") + f.name + "]", nullptr);
    gainMatch.requestRelearn();
}

juce::AudioProcessorEditor* TwinTakeProcessor::createEditor()
{
    return new TwinTakeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TwinTakeProcessor();
}
