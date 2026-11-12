#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* kIds[] = { "humOn", "humFreq", "humReduction", "humHarmonics", "noiseOn", "noiseMode", "noiseReduction", "noiseThreshold",
                           "roomOn", "roomReduction", "roomTail", "output", "delta",
                           "noiseOptimize", "noiseGentle", "roomBandLow", "roomBandLowMid", "roomBandHighMid", "roomBandHigh", "humWidth",
                           "mouthOn", "mouthSens", "mouthSkew", "breathOn", "breathReduction", "breathSens",
                           "plosiveOn", "plosiveStrength", "plosiveSens", "plosiveFreq" };
    const char* kVersionProp = "noiseOutVersion";   // 状態の版（2 = 1.1.0 から。ない = 1.0.0 で保存した曲）
    const char* kProfileProp = "noiseProfile";   // LEARN で覚えた形（23.4375 Hz おき 1025 点の dB をカンマ区切り）
    const char* kHumProp = "humF0";              // AUTO で最後に見つけたハムの周波数
}

NoiseOutProcessor::NoiseOutProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < pNum; ++i) p[i] = apvts.getRawParameterValue (kIds[i]);
    myPresets.exclude ("delta");
    startTimerHz (10);
}

juce::AudioProcessorValueTreeState::ParameterLayout NoiseOutProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using I = juce::AudioParameterInt;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto db1 = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto red = [] (float v, int) { return juce::String (v, 1) + " dB"; };

    ps.push_back (std::make_unique<B> (juce::ParameterID { "humOn", 1 }, "Hum On", false));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "humFreq", 1 }, "Hum Frequency", juce::StringArray { "Auto", "50 Hz", "60 Hz" }, 0));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "humReduction", 1 }, "Hum Reduction", juce::NormalisableRange<float> (0.0f, 60.0f, 0.1f), 60.0f,
                                       A().withLabel ("dB").withStringFromValueFunction (red)));
    ps.push_back (std::make_unique<I> (juce::ParameterID { "humHarmonics", 1 }, "Hum Harmonics", 1, nzo::maxHarm, 8));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "noiseOn", 1 }, "Noise On", true));
    ps.push_back (std::make_unique<C> (juce::ParameterID { "noiseMode", 1 }, "Noise Mode", juce::StringArray { "Adaptive", "Learned" }, 0));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "noiseReduction", 1 }, "Noise Reduction", juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 12.0f,
                                       A().withLabel ("dB").withStringFromValueFunction (red)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "noiseThreshold", 1 }, "Noise Threshold", juce::NormalisableRange<float> (-20.0f, 10.0f, 0.1f), 0.0f,
                                       A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "roomOn", 1 }, "Room On", false));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "roomReduction", 1 }, "Room Reduction", juce::NormalisableRange<float> (-10.0f, 20.0f, 0.1f), 10.0f,
                                       A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                                           return v < -0.05f ? "adds " + juce::String (-v, 1) + " dB" : juce::String (v, 1) + " dB"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "roomTail", 1 }, "Room Tail", juce::NormalisableRange<float> (0.5f, 4.0f, 0.01f, 0.6f), 1.0f,
                                       A().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " s"; })));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                       A().withLabel ("dB").withStringFromValueFunction (db1)));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "delta", 1 }, "Delta (removed only)", false));
    // ---- 1.1.0 で足したもの（版のヒント 2。Logic のパラメーター番号をずらさない）----
    auto zeroTen = [] (float v, int) { return juce::String (v, 1); };
    ps.push_back (std::make_unique<C> (juce::ParameterID { "noiseOptimize", 2 }, "Noise Optimize For", juce::StringArray { "Dialogue", "Music" }, 1));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "noiseGentle", 2 }, "Noise Gentle Filter", false));
    for (auto [id, nm] : { std::pair<const char*, const char*> { "roomBandLow", "Room Band Low" }, { "roomBandLowMid", "Room Band Low-Mid" },
                           { "roomBandHighMid", "Room Band High-Mid" }, { "roomBandHigh", "Room Band High" } })
        ps.push_back (std::make_unique<F> (juce::ParameterID { id, 2 }, nm, juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 6.0f, A().withStringFromValueFunction (zeroTen)));
    // WIDTH（Q）は 300〜2000 に絞る（測定: Q 100 で倍音の間の音楽が −20 dB、Q 300 で −6 dB。Q 3000 はハムが −53 dB までしか効かず、止めた後に鳴き残る）。真ん中 = 初期値 1000
    auto widthRange = juce::NormalisableRange<float> (300.0f, 2000.0f, 1.0f);
    widthRange.setSkewForCentre (1000.0f);
    ps.push_back (std::make_unique<F> (juce::ParameterID { "humWidth", 2 }, "Hum Width (Q)", widthRange, 1000.0f,
                                       A().withLabel ("Q").withStringFromValueFunction ([] (float v, int) { return "Q " + juce::String (juce::roundToInt (v)); })));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "mouthOn", 2 }, "Mouth Click On", false));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "mouthSens", 2 }, "Mouth Click Sensitivity", juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 4.0f, A().withStringFromValueFunction (zeroTen)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "mouthSkew", 2 }, "Mouth Click Frequency Skew", juce::NormalisableRange<float> (-10.0f, 10.0f, 0.1f), 0.0f, A().withStringFromValueFunction (zeroTen)));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "breathOn", 2 }, "Breath On", false));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "breathReduction", 2 }, "Breath Reduction", juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 12.0f,
                                       A().withLabel ("dB").withStringFromValueFunction (red)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "breathSens", 2 }, "Breath Sensitivity", juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f, A().withStringFromValueFunction (zeroTen)));
    ps.push_back (std::make_unique<B> (juce::ParameterID { "plosiveOn", 2 }, "Plosive On", false));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "plosiveStrength", 2 }, "Plosive Strength", juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f, A().withStringFromValueFunction (zeroTen)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "plosiveSens", 2 }, "Plosive Sensitivity", juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f, A().withStringFromValueFunction (zeroTen)));
    ps.push_back (std::make_unique<F> (juce::ParameterID { "plosiveFreq", 2 }, "Plosive Frequency Limit", juce::NormalisableRange<float> (50.0f, 500.0f, 1.0f, 0.6f), 200.0f,
                                       A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " Hz"; })));
    ps.push_back (myp::GainMatch::makeParameter());
    ps.push_back (plink::makeGroupParameter (1));
    return { ps.begin(), ps.end() };
}

const std::vector<std::pair<juce::String, int>>& NoiseOutProcessor::detailParams()
{
    static const std::vector<std::pair<juce::String, int>> v {
        { "humWidth", 0 }, { "noiseGentle", 1 },
        { "roomBandLow", 2 }, { "roomBandLowMid", 2 }, { "roomBandHighMid", 2 }, { "roomBandHigh", 2 },
        { "mouthSkew", 3 }, { "breathSens", 3 }, { "plosiveSens", 3 }, { "plosiveFreq", 3 } };
    return v;
}

bool NoiseOutProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void NoiseOutProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nOut = juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, nOut);   // LEARN の形は engine が自分で作り直す（setProfile 済みの publishedProfile から）
    // 見つけていたハムの周波数だけ入れ直す（LEARN 直後 100 ms の間に来ても、形を古い保存で上書きしない）
    {
        const juce::ScopedLock sl (stateLock);
        const double hz = (double) apvts.state.getProperty (kHumProp, 0.0);
        if (hz > 0.0) engine.seedHum (hz);
    }
    setLatencySamples (engine.latencySamples());
    gainMatch.prepare (sampleRate, nOut, samplesPerBlock);
}

nzo::Knobs NoiseOutProcessor::currentKnobs() const
{
    nzo::Knobs k;
    k.humOn = p[pHumOn]->load() > 0.5f;
    k.humFreq = (int) p[pHumFreq]->load();
    k.humDepthDb = p[pHumRed]->load();
    k.humHarm = (int) p[pHumHarm]->load();
    k.noiseOn = p[pNoiseOn]->load() > 0.5f;
    k.noiseMode = (int) p[pNoiseMode]->load();
    k.noiseRedDb = p[pNoiseRed]->load();
    k.noiseThrDb = p[pNoiseThr]->load();
    k.roomOn = p[pRoomOn]->load() > 0.5f;
    k.roomRedDb = p[pRoomRed]->load();
    k.roomTail = p[pRoomTail]->load();
    k.outDb = p[pOut]->load();
    k.delta = p[pDelta]->load() > 0.5f;
    k.noiseMusic = p[pNoiseOpt]->load() > 0.5f;
    k.noiseGentle = p[pNoiseGentle]->load() > 0.5f;
    k.roomBand[0] = p[pRoomB0]->load(); k.roomBand[1] = p[pRoomB1]->load(); k.roomBand[2] = p[pRoomB2]->load(); k.roomBand[3] = p[pRoomB3]->load();
    k.humWidthQ = p[pHumWidth]->load();
    k.mouthOn = p[pMouthOn]->load() > 0.5f; k.mouthSens = p[pMouthSens]->load(); k.mouthSkew = p[pMouthSkew]->load();
    k.breathOn = p[pBreathOn]->load() > 0.5f; k.breathGainDb = -p[pBreathRed]->load(); k.breathSens = p[pBreathSens]->load();
    k.plosiveOn = p[pPlosOn]->load() > 0.5f; k.plosiveStrength = p[pPlosStr]->load(); k.plosiveSens = p[pPlosSens]->load(); k.plosiveFreqHz = p[pPlosFreq]->load();
    k.learnStart = learnStartSeq.load();
    k.learnStop = learnStopSeq.load();
    return k;
}

void NoiseOutProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);
    // 前のプラグインから NaN・無限大が来たら 0 に（GAIN MATCH が測る前に。測り値が NaN になると保存まで壊れる）
    for (int c = 0; c < numIn; ++c)
    {
        auto* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (x[i])) x[i] = 0.0f;
    }

    gainMatch.measureInput (buffer, juce::jmax (1, numIn));
    auto k = currentKnobs();

    // Plugin Link: Fader Pilot が「声が鳴っている」と言っている間は、雑音の推定（ADAPTIVE）を止める
    bool found = false, singing = false;
    if (link.on())
    {
        plink::Peer pr;
        if (link.ep().find (plink::kindFaderPilot, pr) && pr.valuesAgeMs < 1500) { found = true; singing = pr.v[plink::fp::active] > 0.5f; }
    }
    sawPilot.store (found); pilotSinging.store (singing);
    k.freezeAdapt = singing;

    if (needReset.exchange (false)) engine.reset();
    engine.process (buffer.getArrayOfWritePointers(), n, k);

    // GAIN MATCH: DELTA（取った音だけ）を聴いている間は比べる元がないので効かせない
    gainMatch.process (buffer, juce::jmin (numOut, nzo::maxCh), engine.latencySamples(), k.delta ? 0.0f : 1.0f);
}

// LEARN が終わったら（音のスレッド）→ ここ（メッセージスレッド）でボタンを戻し、LEARNED に切り替えて形を保存
void NoiseOutProcessor::timerCallback()
{
    const int v = engine.profileVersion.load();
    if (v != seenProfileVersion)
    {
        seenProfileVersion = v;
        {
            const juce::ScopedLock sl (stateLock);
            saveExtraState (apvts.state);
        }
        if (auto* r = apvts.getParameter ("noiseMode"))
        {
            myPresets.snapshot();
            r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (1.0f)); r->endChangeGesture();
            myPresets.commitSinceSnapshot();
        }
    }
}

void NoiseOutProcessor::forgetProfile()
{
    engine.clearProfile();
    apvts.state.removeProperty (kProfileProp, nullptr);
    if (auto* r = apvts.getParameter ("noiseMode"))
    {
        r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (0.0f)); r->endChangeGesture();
    }
}

void NoiseOutProcessor::saveExtraState (juce::ValueTree& st)
{
    if (engine.hasProfile())
    {
        std::array<float, nzo::profileBins> prof {};
        engine.getProfile (prof);
        juce::String s;
        s.preallocateBytes (nzo::profileBins * 7);
        for (int j = 0; j < nzo::profileBins; ++j) { if (j > 0) s << ","; s << juce::String (prof[(size_t) j], 1); }
        st.setProperty (kProfileProp, s, nullptr);
    }
    if (engine.humFound.load()) st.setProperty (kHumProp, (double) engine.humF0.load(), nullptr);
}

void NoiseOutProcessor::loadExtraState (const juce::ValueTree& st)
{
    const auto s = st.getProperty (kProfileProp, "").toString();
    if (s.isNotEmpty())
    {
        juce::StringArray parts;
        parts.addTokens (s, ",", "");
        std::array<float, nzo::profileBins> prof {};
        for (int j = 0; j < nzo::profileBins; ++j) prof[(size_t) j] = j < parts.size() ? parts[j].getFloatValue() : -200.0f;
        engine.setProfile (prof.data(), nzo::profileBins);
    }
    const double hz = (double) st.getProperty (kHumProp, 0.0);
    if (hz > 0.0) engine.seedHum (hz);
}

void NoiseOutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (stateLock);
    auto st = apvts.copyState();
    st.setProperty (kVersionProp, 2, nullptr);
    saveExtraState (st);
    gainMatch.saveTo (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void NoiseOutProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const juce::ScopedLock sl (stateLock);
            const bool fromV1 = ! xml->hasAttribute (kVersionProp);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // 1.0.0 で保存した曲: NOISE は DIALOGUE のまま（新しく挿したときの初期値 MUSIC にすると音が変わるため）
            if (fromV1)
                if (auto* r = apvts.getParameter ("noiseOptimize")) r->setValueNotifyingHost (r->convertTo0to1 (0.0f));
            if (! apvts.state.hasProperty (kProfileProp)) engine.clearProfile();
            loadExtraState (apvts.state);
            gainMatch.loadFrom (apvts.state);
        }
}

//==============================================================================
// 内蔵プリセット（使いどころ別。名前と数値は自分で決めた）
namespace
{
    struct FP { const char* name; const char* values; };
    const FP kFactory[] = {
        { "Init (noise only, like the defaults)",  "" },
        { "Home Vocal + Air Conditioner",          "noiseOn=1,noiseMode=0,noiseReduction=14,noiseThreshold=0" },
        { "Home Vocal - Room Echo",                "noiseOn=1,noiseReduction=10,roomOn=1,roomReduction=8,roomTail=0.8" },
        { "Narration / Podcast",                   "noiseOn=1,noiseOptimize=0,noiseReduction=18,noiseThreshold=2,roomOn=1,roomReduction=6,roomTail=0.7,"
                                                   "mouthOn=1,mouthSens=4,breathOn=1,breathReduction=10,plosiveOn=1,plosiveStrength=5" },
        { "Guitar / Bass Line - Hum",              "humOn=1,humFreq=0,humReduction=60,humHarmonics=12,noiseOn=1,noiseReduction=8,noiseThreshold=-4" },
        { "Amp Buzz (many harmonics)",             "humOn=1,humFreq=0,humReduction=60,humHarmonics=16,noiseOn=0" },
        { "Gentle (just a little)",                "noiseOn=1,noiseReduction=6,noiseThreshold=-4,noiseGentle=1" },
        { "Big Room - Dry It Up",                  "noiseOn=0,roomOn=1,roomReduction=14,roomTail=2.0" },
        // 1.1.0 から（DETAIL の値も入れる）
        { "Small Boxy Room (muddy)",               "noiseOn=0,roomOn=1,roomReduction=10,roomTail=0.5,roomBandLowMid=8,roomBandHigh=5" },
        { "Bright Bathroom-like Echo",             "noiseOn=0,roomOn=1,roomReduction=12,roomTail=1.2,roomBandLow=4,roomBandHighMid=7,roomBandHigh=8" },
        { "Vocal Clean-up: Clicks + Breaths",      "noiseOn=1,noiseReduction=8,mouthOn=1,mouthSens=5,breathOn=1,breathReduction=8" },
        { "Plosive Fix (p / b pops)",              "noiseOn=0,plosiveOn=1,plosiveStrength=6" },
    };
}

int NoiseOutProcessor::numFactoryPresets() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }
juce::String NoiseOutProcessor::factoryPresetName (int i) { return (i >= 0 && i < numFactoryPresets()) ? kFactory[i].name : ""; }

void NoiseOutProcessor::loadFactoryPreset (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = kFactory[i];
    myPresets.snapshot();
    // まず全部を初期値に（GAIN MATCH・LINK の番号・DELTA・NOISE のモードは触らない）
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == myp::GainMatch::paramId || id == "linkGroup" || id == "delta" || id == "noiseMode") continue;
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

juce::AudioProcessorEditor* NoiseOutProcessor::createEditor()
{
    return new NoiseOutEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoiseOutProcessor();
}
