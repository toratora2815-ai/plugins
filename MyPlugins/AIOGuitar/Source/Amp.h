#pragma once
// AMP と CAB の音の部品（AIO Guitar の AMP ページ）。
// AMP（4 倍オーバーサンプリングの中で動かす。上げ下げは FXChain）:
//   入力の結合コンデンサー → ブライト（GAIN が小さいほど高域が出る）→ プリアンプの真空管段（2〜4 段）
//   → トーン回路（BASS / MIDDLE / TREBLE）→ パワー管（プッシュプル、電源のサグ）→ PRESENCE・低域の共振
//   真空管段は非対称（グリッド側はやわらかく、カットオフ側は硬く）で、強く弾くとグリッド電流でバイアスが動く（歪みの質が変わる）。
// CAB（元のサンプルレート）: スピーカー＋箱の特性（種類ごと）→ マイク（DYNAMIC / CONDENSER / RIBBON、位置 CENTER〜EDGE、距離 CLOSE〜FAR）。
//   読み込んだ IR（畳み込み）にも切り替えられる。
// モデルの数値は Logic の Amp Designer を測って合わせ込む（build/AmpLab、NOTES.md）。名前・見た目はオリジナル。
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Biquad.h"
#include "AmpTables.h"

namespace ampsim
{
constexpr int numModels = 5;
inline const juce::StringArray& modelNames()
{
    static const juce::StringArray n { "American Clean", "Tweed", "British Chime", "British Crunch", "Modern High Gain" };
    return n;
}
constexpr int numCabs = 6;   // the last one = the loaded IR
inline const juce::StringArray& cabNames()
{
    static const juce::StringArray n { "1x12 Open", "2x12 Open", "4x12 Closed", "4x12 Modern", "4x10 Open", "Loaded IR" };
    return n;
}
inline const juce::StringArray& micNames()
{
    static const juce::StringArray n { "Dynamic", "Condenser", "Ribbon" };
    return n;
}

//==============================================================================
struct Stage { float gainDb, bias, hp, lp; };   // gain into the stage, grid bias, coupling high-pass, Miller low-pass (Hz)

struct ModelDef
{
    float inHp;                 // input coupling (Hz)
    float brightDb;             // bright cap: treble boost at GAIN 0 (fades out towards GAIN 10)
    float gainLoDb, gainHiDb;   // into the first stage at GAIN 0 / 10
    int   numStages;
    std::array<Stage, 4> st;
    int   stackAfter;           // the tone stack comes after this stage (1 = early, like a tweed / blackface; numStages = after all)
    float bassF, midF, midQ, trebF;
    float bassDb, midDb, trebDb;   // knob 0 -> -range/2 ... knob 10 -> +range/2 (bass and treble cut more than they boost)
    float scoopDb;              // the passive stack's fixed mid dip at noon
    float powerDrive;           // how hard MASTER 10 drives the power amp
    float sag;                  // supply sag 0..1 (bloom / compression when played hard)
    float presF, presDb;        // PRESENCE shelf (range)
    float resF, resDb;          // low resonance (speaker on the output transformer)
    float trimDb;               // so the models come out about equally loud
    float voiceF, voiceQ, voiceDb;   // (old hand-fitted voice, not used: tables::voice measured from Amp Designer)
};
// which measured tone-control behaviour a model uses (tables::knob): 0 American (clean, chime), 1 British (tweed, crunch), 2 modern
inline int knobFamily (int m) { static const int f[numModels] { 0, 1, 0, 1, 2 }; return f[juce::jlimit (0, numModels - 1, m)]; }

inline const ModelDef& model (int m)
{
    static const ModelDef defs[numModels]
    {
        // American Clean: two stages, early scooped stack, big clean headroom
        { 20.0f, 1.656f, -22.0f, 8.0f, 2, {{ { 0.0f, 0.10f, 20.0f, 1.411e+04f }, { 6.0f, -0.15f, 30.0f, 1.058e+04f }, {}, {} }}, 1,
          100.0f, 482.7f, 0.7013f, 2600.0f, 20.0f, 12.0f, 18.0f, -11.34f, 1.6f, 0.15f, 3500.0f, 10.0f, 120.7f, 2.487f, 1.0f,
          1215.0f, 0.4f, 3.933f },
        // Tweed: breaks up early, fat mids, lots of sag
        { 43.74f, 0.0625f, -8.0f, 24.0f, 2, {{ { 0.0f, 0.20f, 40.0f, 1.263e+04f }, { 8.0f, -0.20f, 60.0f, 9470.0f }, {}, {} }}, 2,
          150.0f, 1200.0f, 1.177f, 2000.0f, 14.0f, 6.0f, 14.0f, -0.01816f, 4.0f, 0.50f, 3000.0f, 8.0f, 156.9f, 1.781f, -10.8f,
          6000.0f, 0.4f, -4.086f },
        // British Chime: class-A, bright top end, chimey break-up
        { 20.0f, 0.0625f, -12.0f, 18.0f, 2, {{ { 0.0f, 0.05f, 40.0f, 1.563e+04f }, { 6.0f, -0.10f, 60.0f, 1.117e+04f }, {}, {} }}, 2,
          120.0f, 655.3f, 1.98f, 3000.0f, 16.0f, 8.0f, 18.0f, -3.646f, 3.5f, 0.30f, 4500.0f, 10.0f, 150.5f, 1.95f, -1.0f,
          3292.0f, 0.4088f, 6.5f },
        // British Crunch: three stages, mid-forward stack after the cathode follower
        { 20.0f, 10.0f, -14.0f, 16.0f, 3, {{ { 0.0f, 0.08f, 30.0f, 1.377e+04f }, { 8.0f, -0.18f, 50.0f, 1.101e+04f }, { 6.0f, 0.06f, 80.0f, 9637.0f }, {} }}, 3,
          110.0f, 368.2f, 2.0f, 2400.0f, 18.0f, 14.0f, 18.0f, -3.907f, 3.0f, 0.25f, 4000.0f, 12.0f, 85.0f, 3.5f, -11.0f,
          6000.0f, 0.9562f, -6.0f },
        // Modern High Gain: four stages, tight (the later stages cut the lows before they clip), scooped
        { 70.0f, 0.0f, -2.0f, 32.0f, 4, {{ { 0.0f, 0.08f, 30.0f, 1.8e+04f }, { 10.0f, -0.18f, 90.0f, 1.4e+04f }, { 8.0f, 0.06f, 120.0f, 1.2e+04f }, { 6.0f, -0.10f, 60.0f, 1.3e+04f } }}, 4,
          100.0f, 478.4f, 0.9135f, 2200.0f, 18.0f, 16.0f, 18.0f, -14.0f, 2.5f, 0.20f, 4500.0f, 12.0f, 80.0f, 5.0f, 5.7f,
          6000.0f, 3.0f, -2.322f },
    };
    return defs[juce::jlimit (0, numModels - 1, m)];
}

// tanh without the cost (error < 0.3 %, exactly +/-1 from |x| = 3)
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// one triode stage: soft on the grid-current side, a harder knee on the cut-off side
inline float triode (float v) noexcept
{
    if (v >= 0.0f) return fastTanh (v);
    const float u = v * (1.0f / 1.5f);
    return u > -1.0f ? 1.5f * (u - u * u * u * (1.0f / 3.0f)) : -1.0f;
}

//==============================================================================
class Amp
{
public:
    void prepare (double osRate)
    {
        osFs = osRate;
        gridAtk = 1.0f - std::exp (-1.0f / (float) (osFs * 0.001));
        gridRel = 1.0f - std::exp (-1.0f / (float) (osFs * 0.040));
        sagAtk  = 1.0f - std::exp (-1.0f / (float) (osFs * 0.004));
        sagRel  = 1.0f - std::exp (-1.0f / (float) (osFs * 0.150));
        fadeOutStep = (float) (1.0 / (osFs * 0.005));
        fadeInStep  = (float) (1.0 / (osFs * 0.010));
        current = want = -1; fadeState = 0; fadeGain = 1.0f;
        preS.reset (osFs, 0.03); powS.reset (osFs, 0.03);
        for (auto& k : knobS) k.reset (osFs, 0.03);
        for (auto& f : knobFlat) f = true;
        for (int k = 0; k < 4; ++k) { knobHold[k] = 0; knobSide[k] = 0; }
        knobCount = 0;
        reset();
    }

    // also called on the audio thread when the amp starts again: no allocation. Every stage starts at its
    // resting point, so silence in = silence out (no "pop" when the amp starts)
    void reset()
    {
        inHp.reset(); pwrLp.reset();
        for (int s = 0; s < 4; ++s) { stHp[s].reset(); stLp[s].reset(); }
        for (auto& b : voiceEq) b.reset();
        for (auto& row : knobEq) for (auto& b : row) b.reset();
        const auto& d = model (juce::jmax (0, current));
        for (int s = 0; s < 4; ++s) grid[s][0] = grid[s][1] = juce::jmax (0.0f, d.st[(size_t) s].bias);
        supply[0] = supply[1] = 0.0f;
    }

    // knobs 0..10, once per block. A new model: 5 ms down, switch (from rest), 10 ms up - never a bang
    void setParams (int modelIndex, float gain, float bass, float mid, float treble, float presence, float master)
    {
        kGain = gain; kBass = bass; kMid = mid; kTreble = treble; kPres = presence; kMaster = master;
        if (current < 0) { current = modelIndex; apply (true); reset(); return; }
        if (modelIndex != current) { want = modelIndex; if (fadeState == 0) fadeState = 1; }
        if (fadeState == 1) return;   // the knobs are applied with the new model
        apply (false);
    }

    // nothing is playing: take the settings at once (no fades on the next note)
    void snapTo (int modelIndex, float gain, float bass, float mid, float treble, float presence, float master)
    {
        kGain = gain; kBass = bass; kMid = mid; kTreble = treble; kPres = presence; kMaster = master;
        const bool newModel = modelIndex != current || fadeState != 0;
        current = modelIndex; fadeState = 0; fadeGain = 1.0f;
        apply (true);
        if (newModel) reset();
    }

    // the oversampled block (both channels)
    void process (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            if (--knobCount <= 0)   // tone knobs glide (20 ms); while one moves the filters follow every 2 samples
            {
                bool moving = false;
                for (int k = 0; k < 4; ++k) moving = moving || knobS[k].isSmoothing() || knobHold[k] > 0;
                knobCount = moving ? 4 : 32;
                for (int k = 0; k < 4; ++k) tickKnob (k, knobCount);
            }
            if (fadeState == 1 && (fadeGain -= fadeOutStep) <= 0.0f)
            {
                fadeGain = 0.0f;
                current = want;
                apply (true);
                reset();
                fadeState = 2;
            }
            else if (fadeState == 2 && (fadeGain += fadeInStep) >= 1.0f) { fadeGain = 1.0f; fadeState = 0; }
            const float pg = preS.getNextValue(), pw = powS.getNextValue();
            const float fg = fadeGain * fadeGain * (3.0f - 2.0f * fadeGain);   // S-curve: no corner where the fade starts / ends
            L[i] = sample (L[i], 0, pg, pw) * fg;
            if (R != nullptr) R[i] = sample (R[i], 1, pg, pw) * fg;
        }
    }

private:
    void apply (bool force)
    {
        const auto& d = model (current);
        preS.setTargetValue (juce::Decibels::decibelsToGain (d.gainLoDb + (d.gainHiDb - d.gainLoDb) * std::pow (kGain / 10.0f, 0.8f)));
        powS.setTargetValue (d.powerDrive * juce::Decibels::decibelsToGain (-14.0f + 1.6f * kMaster));
        if (force) { preS.setCurrentAndTargetValue (preS.getTargetValue()); powS.setCurrentAndTargetValue (powS.getTargetValue()); }
        outGain = 0.5f * juce::Decibels::decibelsToGain (d.trimDb);
        const float knobs[4] { kBass, kMid, kTreble, kPres };
        for (int k = 0; k < 4; ++k)
        {
            if (force)
            {
                knobS[k].setCurrentAndTargetValue (knobs[k]);
                knobHold[k] = 0;
                const float t = (knobs[k] - 5.0f) / 5.0f;
                knobFlat[k] = std::abs (t) < 1.0e-4f; knobSide[k] = t < 0.0f ? 0 : 1;
                if (! knobFlat[k]) setKnobSet (k, knobSide[k], std::abs (t));
            }
            else knobS[k].setTargetValue (knobs[k]);
        }
        if (! force && current == fModel) return;
        fModel = current;

        using B = Biquad;
        inHp.set (B::HighPass, osFs, 20.0, 0.707);
        for (int s = 0; s < 4; ++s)
        {
            stHp[s].set (B::HighPass, osFs, d.st[(size_t) s].hp > 0 ? d.st[(size_t) s].hp : 20.0, 0.707);
            stLp[s].set (B::LowPass,  osFs, d.st[(size_t) s].lp > 0 ? d.st[(size_t) s].lp : 15000.0, 0.707);
            stGain[s] = juce::Decibels::decibelsToGain (d.st[(size_t) s].gainDb);
            const float b = d.st[(size_t) s].bias;
            restOut[s] = triode (b - 0.35f * juce::jmax (0.0f, b));   // what the stage puts out at rest
        }
        // the model's own tone (measured at GAIN 2, knobs at noon)
        for (int k = 0; k < 5; ++k) { const auto& f = tables::voice[current][k]; voiceEq[k].set (f.type, osFs, f.f, f.q, f.g); }
        for (int k = 0; k < 4; ++k)   // the family may have changed
            if (! knobFlat[k]) setKnobSet (k, knobSide[k], knobHold[k] > 0 ? 0.0f : std::abs ((knobS[k].getCurrentValue() - 5.0f) / 5.0f));
        pwrLp.set (B::LowPass, osFs, 12000.0, 0.707);
    }

    // a tone control: the measured change at 0 or 10 ("cut" and "boost" sets of 3 filters), scaled towards noon.
    // Never a jump: crossing noon passes through "no change" on the old set and waits 3 ms (the knob pauses) before
    // the other set takes over; stopping at noon keeps "no change" running for 30 ms before the filters are skipped
    void setKnobSet (int n, int side, float amount)
    {
        const auto* set = tables::knob[knobFamily (juce::jmax (0, current))][n][side];
        for (int k = 0; k < 3; ++k) knobEq[n][k].set (set[k].type, osFs, set[k].f, set[k].q, set[k].g * amount);
    }

    void updateKnob (int n, float value)
    {
        const float t = (value - 5.0f) / 5.0f;          // -1 .. 1
        const int side = t < 0.0f ? 0 : 1;
        if (std::abs (t) < 1.0e-4f)                     // at noon
        {
            if (knobFlat[n]) return;
            setKnobSet (n, knobSide[n], 0.0f);
            if (knobS[n].isSmoothing()) return;          // only passing over noon
            if (knobHold[n] <= 0) { knobHold[n] = (int) (osFs * 0.03); knobHoldTo[n] = -1; }
            return;
        }
        if (knobFlat[n])                                // waking up: from a clean state
        {
            for (auto& b : knobEq[n]) b.reset();
            knobFlat[n] = false; knobSide[n] = side; knobHold[n] = 0;
            setKnobSet (n, side, std::abs (t));
            return;
        }
        if (side != knobSide[n])                        // crossing noon
        {
            setKnobSet (n, knobSide[n], 0.0f);
            if (knobHold[n] <= 0 || knobHoldTo[n] != side) { knobHold[n] = (int) (osFs * 0.003); knobHoldTo[n] = side; }
            return;
        }
        knobHold[n] = 0;
        setKnobSet (n, side, std::abs (t));
    }

    void tickKnob (int n, int samples)
    {
        if (knobHold[n] > 0)
        {
            if ((knobHold[n] -= samples) > 0) return;   // the knob waits while the old set rests at "no change"
            knobHold[n] = 0;
            if (knobHoldTo[n] < 0) { knobFlat[n] = true; return; }
            for (auto& b : knobEq[n]) b.reset();
            knobSide[n] = knobHoldTo[n];
            const float t = (knobS[n].getCurrentValue() - 5.0f) / 5.0f;
            if (std::abs (t) < 1.0e-4f) { knobHold[n] = (int) (osFs * 0.03); knobHoldTo[n] = -1; setKnobSet (n, knobSide[n], 0.0f); return; }
            setKnobSet (n, knobSide[n], std::abs (t));
            return;
        }
        if (knobS[n].isSmoothing()) updateKnob (n, knobS[n].skip (samples));
    }

    inline float sample (float x, int c, float preGain, float powerGain) noexcept
    {
        const auto& d = model (current);
        x = inHp.process (x, c) * preGain;
        for (int s = 0; s < d.numStages; ++s)
        {
            const float b = d.st[(size_t) s].bias;
            const float v = x * stGain[s] + b;
            // grid current charges the coupling cap on the positive swing: the bias slides down while played hard
            auto& g = grid[s][c];
            const float pos = v > 0.0f ? v : 0.0f;
            g += (pos - g) * (pos > g ? gridAtk : gridRel);
            x = triode (v - 0.35f * g) - restOut[s];
            x = stLp[s].process (stHp[s].process (x, c), c);
            if (s + 1 == d.stackAfter)
            {
                for (int k = 0; k < 5; ++k) x = voiceEq[k].process (x, c);
                for (int n = 0; n < 3; ++n)   // bass, middle, treble
                    if (! knobFlat[n]) for (int k = 0; k < 3; ++k) x = knobEq[n][k].process (x, c);
            }
        }
        // power amp: push-pull (odd harmonics), the supply sags under load
        float& sp = supply[c];
        const float h = 1.0f - d.sag * 0.5f * sp;
        const float y = h * fastTanh (x * powerGain / h);
        const float a = std::abs (y);
        sp += (a - sp) * (a > sp ? sagAtk : sagRel);
        float o = y;
        if (! knobFlat[3]) for (int k = 0; k < 3; ++k) o = knobEq[3][k].process (o, c);   // presence (in the power amp's feedback)
        return pwrLp.process (o, c) * outGain;
    }

    double osFs = 192000.0;
    int current = -1, want = -1, fadeState = 0, fModel = -1;
    float fadeGain = 1.0f, fadeOutStep = 0.001f, fadeInStep = 0.001f;
    float kGain = 5, kBass = 5, kMid = 5, kTreble = 5, kPres = 5, kMaster = 5;
    juce::SmoothedValue<float> preS, powS, knobS[4];
    bool knobFlat[4] { true, true, true, true };
    int knobSide[4] {}, knobHold[4] {}, knobHoldTo[4] {};
    int knobCount = 0;
    float outGain = 0.5f, stGain[4] { 1, 1, 1, 1 }, restOut[4] {};
    float gridAtk = 0.01f, gridRel = 0.001f, sagAtk = 0.01f, sagRel = 0.001f;
    float grid[4][2] {}, supply[2] {};
    Biquad inHp, stHp[4], stLp[4], voiceEq[5], knobEq[4][3], pwrLp;
};

//==============================================================================
// speaker + cabinet + microphone (base rate). numCabs - 1 = the loaded IR (used as it is: no mic EQ on it)
class Cab
{
public:
    static constexpr int convBlock = 256;   // the convolution runs in blocks of up to 256
    static constexpr int fallbackCab = 2;   // "Loaded IR" without an IR (yet): the 4x12 Closed instead of no cabinet

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        {   // an IR handed over before prepare: load it now, so prepare gets it ready at once
            const juce::SpinLock::ScopedLockType sl (irLock);
            if (irPending.load())
            {
                conv.loadImpulseResponse (std::move (pendingIR), pendingRate, juce::dsp::Convolution::Stereo::no,
                                          juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
                irPending.store (false);
                hasIR.store (true);
            }
        }
        conv.prepare ({ fs, (juce::uint32) convBlock, 2 });
        engineHasIR = hasIR.load(); engineStale = false; waiting = false; settleLeft = 0;
        scratch.setSize (2, convBlock);
        fadeStep = (float) (1.0 / (fs * 0.008));
        posS.reset (fs, 0.04); distS.reset (fs, 0.04);
        irWarm = hasIR.load() && conv.getCurrentIRSize() > 1;
        warmLeft = (int) (fs * 0.06);
        sawIR = hasIR.load();
        curType = -1; fadeState = 0; fadeGain = 1.0f;
        reset();
    }

    void reset()
    {
        for (auto& b : eq) b.reset();
        conv.reset();
    }

    // message thread: a new IR (mono, any rate). Handed to the audio thread, which gives it to the convolution
    // (JUCE: loading must not interleave with processing from another thread)
    void loadIR (juce::AudioBuffer<float>&& ir, double irRate)
    {
        // never the same length as the last one: the audio thread knows the new IR is in when the size changes
        if (ir.getNumSamples() == lastRawLen) ir.setSize (ir.getNumChannels(), ir.getNumSamples() + 1, true, true);
        lastRawLen = ir.getNumSamples();
        const juce::SpinLock::ScopedLockType sl (irLock);
        pendingIR = std::move (ir);
        pendingRate = irRate;
        irPending.store (true);
    }
    void clearIR()
    {
        const juce::SpinLock::ScopedLockType sl (irLock);
        irPending.store (false);
        pendingIR.setSize (0, 0);
        hasIR.store (false);
    }
    bool irLoaded() const { return hasIR.load() || irPending.load(); }

    // once per block. Cabinet / mic / IR changes: 8 ms down, switch (from silence), 8 ms up.
    // POSITION and DISTANCE glide (20 ms)
    void setParams (int type, int mic, float position, float distance)
    {
        tType = type; tMic = mic;
        posS.setTargetValue (edgeOf (position)); distS.setTargetValue (farOf (distance));
        if (curType < 0) { posS.setCurrentAndTargetValue (edgeOf (position)); distS.setCurrentAndTargetValue (farOf (distance)); configure(); }
    }

    // nothing is playing: take the settings at once
    void snapTo (int type, int mic, float position, float distance)
    {
        tType = type; tMic = mic;
        posS.setCurrentAndTargetValue (edgeOf (position)); distS.setCurrentAndTargetValue (farOf (distance));
        fadeState = 0; fadeGain = 1.0f;
        if (needsSwitch()) { configure(); for (auto& b : eq) b.reset(); conv.reset(); }
        else updatePosDist();
    }

    // audio thread (also while resting): take a waiting IR and get it ready. JUCE swaps a new IR in on its own
    // thread and then crossfades for 50 ms from what it had (a plain impulse at first, or the old IR).
    //  - a first IR (or one after the IR was removed): used only once it is really in (+60 ms), until then the
    //    4x12 Closed plays. "Really in" = the engine's IR size changed, or 0.3 s at the latest (never stuck)
    //  - a further IR while playing: JUCE's crossfade from the old one. While resting: run it on silence and
    //    cut the crossfade short, so the next note starts with the new IR
    //  - offline rendering waits for it (at most 0.5 s)
    //  - while resting, the engine keeps running on silence for 0.3 s after each hand-over: a second IR loaded right
    //    after the first may only get in after the first one did; whenever the size changes again, end its crossfade
    void prepareIR (int n, bool offline, bool resting)
    {
        const bool have = hasIR.load();
        if (! have && sawIR) engineStale = true;   // removed: what the engine holds must not be heard again
        sawIR = have;
        if (irPending.load())
        {
            const juce::SpinLock::ScopedTryLockType tl (irLock);
            if (tl.isLocked() && irPending.load())
            {
                sizeAtHandoff = conv.getCurrentIRSize();
                conv.loadImpulseResponse (std::move (pendingIR), pendingRate, juce::dsp::Convolution::Stereo::no,
                                          juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
                irPending.store (false);
                hasIR.store (true); sawIR = true;
                replacing = engineHasIR && ! engineStale;
                if (! replacing) irWarm = false;
                waiting = true; swapped = false;
                waitLeft = (int) (fs * 0.3); warmLeft = (int) (fs * 0.06);
                settleLeft = (int) (fs * 0.3);
            }
        }
        if (! hasIR.load()) return;
        if (! waiting)
        {
            if (settleLeft > 0 && resting)   // after a hand-over, while nothing plays: catch an IR that gets in late
            {
                settleLeft -= n;
                runSilentPart (n);
                if (const int sz = conv.getCurrentIRSize(); sz != settledSize) { settledSize = sz; conv.reset(); }
            }
            else settleLeft = 0;
            return;
        }
        auto runSilent = [this] (int m) { runSilentPart (m); };
        auto done = [this, resting]
        {
            waiting = false;
            if (replacing && resting) conv.reset();   // nothing playing: end the crossfade now
            irWarm = true; engineHasIR = true; engineStale = false;
            settledSize = conv.getCurrentIRSize();
        };
        if (offline && (! irWarm || resting))
        {
            for (int k = 0; k < 500 && conv.getCurrentIRSize() == sizeAtHandoff; ++k) { runSilent (convBlock); juce::Thread::sleep (1); }
            for (int left = (int) (fs * 0.06); left > 0; left -= convBlock) runSilent (convBlock);
            done();
            return;
        }
        if (! curIR || resting) runSilent (n);           // the convolution is not in use: drive it on silence
        if (! swapped && (conv.getCurrentIRSize() != sizeAtHandoff || (waitLeft -= n) <= 0)) swapped = true;
        if (swapped && (resting || (warmLeft -= n) <= 0)) done();
    }

    void runSilentPart (int m)
    {
        for (int left = m; left > 0; left -= convBlock)
        {
            scratch.clear();
            float* sc[2] { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            juce::dsp::AudioBlock<float> sb (sc, 2, (size_t) juce::jmin (left, convBlock));
            conv.process (juce::dsp::ProcessContextReplacing<float> (sb));
        }
    }

    void process (float* L, float* R, int n)
    {
        for (int pos = 0; pos < n; pos += convBlock)
        {
            const int m = juce::jmin (convBlock, n - pos);
            processPart (L + pos, R + pos, m);
        }
    }

private:
    // how much of the "edge" / "far" curves: these glide (linear in dB), not the knob values
    static float edgeOf (float position) { return std::sqrt (juce::jlimit (0.0f, 1.0f, position / 10.0f)); }   // most of the change is near the centre
    static float farOf (float distance)  { return juce::jlimit (0.0f, 1.0f, distance / 10.0f); }

    // a different cabinet / IR, or another mic on a cabinet (the mic does nothing on a loaded IR)
    bool needsSwitch() const
    {
        const int e = effectiveType();
        return e != curType || (e != numCabs - 1 && tMic != curMic);
    }

    int effectiveType() const
    {
        if (tType == numCabs - 1 && ! (hasIR.load() && irWarm)) return fallbackCab;
        return juce::jlimit (0, numCabs - 1, tType);
    }

    void processPart (float* L, float* R, int n)
    {
        if (fadeState == 3)   // faded out at the end of the last part: switch now, before anything is filtered
        {
            configure();                // silent: the new path starts from nothing (no old sound)
            for (auto& b : eq) b.reset();
            conv.reset();
            fadeState = 2;
        }
        if (needsSwitch() && fadeState == 0) fadeState = 1;

        float* ch[2] { L, R };
        if (curIR)
        {
            juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            conv.process (juce::dsp::ProcessContextReplacing<float> (block));
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply (ch[c], irGain, n);
        }
        const int step = (posS.isSmoothing() || distS.isSmoothing()) ? 2 : 64;   // while POSITION / DISTANCE glide: new filters every 2 samples
        for (int start = 0; start < n; start += step)
        {
            const int m = juce::jmin (step, n - start);
            if (posS.isSmoothing() || distS.isSmoothing()) { posS.skip (m); distS.skip (m); updatePosDist(); }
            for (int c = 0; c < 2; ++c)
                for (int i = start; i < start + m; ++i)
                {
                    float x = ch[c][i];
                    for (int k = 0; k < numEq; ++k) x = eq[(size_t) k].process (x, c);
                    ch[c][i] = x;
                }
        }
        if (fadeState == 0) return;
        for (int i = 0; i < n; ++i)
        {
            if (fadeState == 1 && (fadeGain -= fadeStep) <= 0.0f) { fadeGain = 0.0f; fadeState = 3; }   // silent until the next part
            else if (fadeState == 3) fadeGain = 0.0f;
            else if (fadeState == 2 && (fadeGain += fadeStep) >= 1.0f) { fadeGain = 1.0f; fadeState = 0; }
            const float fg = fadeGain * fadeGain * (3.0f - 2.0f * fadeGain);   // S-curve
            L[i] *= fg; R[i] *= fg;
        }
    }

    // fixed slots: cab 7, mic 4, position 3, distance 4. With the loaded IR all of them are flat (the IR as it is)
    void configure()
    {
        curType = effectiveType(); curMic = tMic;
        curIR = curType == numCabs - 1;
        for (int k = 0; k < 7; ++k)
        {
            if (curIR) eq[(size_t) k].setBypass();
            else { const auto& f = tables::cab[juce::jlimit (0, 4, curType)][k]; eq[(size_t) k].set (f.type, fs, f.f, f.q, f.g); }
        }
        for (int k = 0; k < 4; ++k)
        {
            const tables::F* f = curIR ? nullptr : (curMic == 0 ? &tables::micDynamic[k] : (curMic == 1 ? &tables::micCondenser[k] : nullptr));
            if (f == nullptr) eq[(size_t) (7 + k)].setBypass();
            else eq[(size_t) (7 + k)].set (f->type, fs, f->f, f->q, f->g);
        }
        numEq = 18;
        updatePosDist();
    }

    void updatePosDist()
    {
        const float edge = posS.getCurrentValue(), far = distS.getCurrentValue();
        for (int k = 0; k < 3; ++k)
        {
            if (curIR) { eq[(size_t) (11 + k)].setBypass(); continue; }
            const auto& f = tables::posEdge[k]; eq[(size_t) (11 + k)].set (f.type, fs, f.f, f.q, f.g * edge);
        }
        for (int k = 0; k < 4; ++k)
        {
            if (curIR) { eq[(size_t) (14 + k)].setBypass(); continue; }
            const auto& f = tables::distFar[k]; eq[(size_t) (14 + k)].set (f.type, fs, f.f, f.q, f.g * far);
        }
    }

    double fs = 48000.0;
    int curType = -1, curMic = -1, numEq = 0, tType = 2, tMic = 0, fadeState = 0, warmLeft = 0;
    float fadeGain = 1.0f, fadeStep = 0.003f;
    const float irGain = juce::Decibels::decibelsToGain (9.0f);   // JUCE's normalising leaves an IR ~9 dB under the built-in cabinets
    bool curIR = false, irWarm = false, sawIR = false, swapped = false, engineHasIR = false, engineStale = false;
    bool waiting = false, replacing = false;
    int sizeAtHandoff = 1, lastRawLen = -1, waitLeft = 0, settleLeft = 0, settledSize = -1;
    juce::SmoothedValue<float> posS, distS;
    std::atomic<bool> hasIR { false }, irPending { false };
    juce::SpinLock irLock;
    juce::AudioBuffer<float> pendingIR, scratch;
    double pendingRate = 48000.0;
    std::array<Biquad, 20> eq;
    juce::dsp::Convolution conv;
};
} // namespace ampsim
