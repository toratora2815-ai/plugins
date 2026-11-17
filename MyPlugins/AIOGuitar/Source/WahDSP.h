#pragma once
// BELLOWS WAH（0.7.0）: 定番のインダクター式ワウの性質を、動かしても跳ばない状態変数フィルター（TPT SVF）で作ったオリジナル。
//  - 共振: かかと 450 Hz（Q 9.4）→ 真ん中 830 Hz（Q 4）→ つま先 2.25 kHz（Q 2）。定番のインダクター式ワウの公開されている実測
//    （CCRMA の J. O. Smith: 460 / 840 / 2250 Hz、Q 9.4 / 4.0 / 2.0）と同じ動き。Q のつまみはこの曲線全体の倍率（0.5〜2 倍、真ん中 = 実測どおり）。
//    深い低音は入口で少し切る（60 Hz）
//  - 出口 = 共振（帯域、山の高さ 1）＋ 低い側を山より約 23 dB 下だけ（共振より下は 1 オクターブごとに約 6 dB 落ちる = ワウの「声」）。
//    位置と Q で大きさが変わらないよう中でそろえる（真ん中で ON/OFF が同じ。ワウは歪み・AMP の前にいるので、音量の差が歪み量の差になるため）
//  - PEDAL: 0 かかと 〜 10 つま先。Logic のオートメーション（ブロックの長さ以上かけてなめらかに）、または MIDI CC11 / CC4（ブロックの中の位置で、
//    20 ms でなめらかに）。どちらに従うかはプロセッサーが決める（最後に動いたほう）
//  - AUTO: 音が鳴るたびにつま先側へ開いて PEDAL の位置へ戻る。AIO は音の頭を前もって知っているので、音を聞いてから動くオートワウと違って
//    遅れない。開く量はベロシティ、戻る速さは SPEED。30 ms 以内に続けて来た音（和音・ストローク）は 1 回として数える
//  - 遅れ 0、線形（オーバーサンプリング不要）
//  - mix-engineer 2026-11-17: Q の曲線・低音の床・1 ストローク 1 回・SPEED 初期値 7。DSP 検査 2026-11-17: reset で音の頭を消さない、
//    CC は位置つき、オートメーションはブロックの長さ以上かけて動かす、休みのあいだは AUTO を閉じる、画面の棒も休み・OFF で動く、1.3 kHz 付近の大きさ
#include <JuceHeader.h>
#include <cmath>
#include <atomic>

namespace pedal
{
class WahDSP
{
public:
    static constexpr int maxCh = 2, maxEvents = 128;
    static constexpr float kHeelHz = 450.0f, kToeHz = 2250.0f;
    static float freqAt (float pos) { return kHeelHz * std::pow (kToeHz / kHeelHz, std::pow (pos, 1.4f)); }   // ~830 Hz at the middle
    static float qAt (float pos)    { return 9.4f * std::pow (2.0f / 9.4f, std::pow (pos, 0.857f)); }         // 9.4 .. 4 .. 2

    void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
    {
        sr = sampleRate;
        numCh = juce::jlimit (1, maxCh, numChannels);
        qSm.reset (sampleRate, 0.03);
        const double w = juce::MathConstants<double>::twoPi * 60.0 / sampleRate;   // input high-pass (deep lows a little)
        hpA = (float) std::exp (-w);
        attackK = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));         // AUTO opens in ~4 ms (no click)
        strokeLen = (int) (0.03 * sampleRate);                                       // notes this close together = one stroke
        ccRamp = juce::jmax (1, (int) (0.02 * sampleRate));                         // a CC step is glided over 20 ms
        rampLen = ccRamp;
        clearEvents();
        reset();
    }

    int latency() const { return 0; }

    // starting again from silence: the filter and AUTO from rest, the pedal where it is meant to be.
    // The notes and CCs of the current host block are kept (the slot calls this when it is switched on, mid-block too)
    void reset()
    {
        for (int c = 0; c < maxCh; ++c) { s1[c] = s2[c] = 0.0f; hpZ[c] = 0.0f; }
        env = envTarget = 0.0f;
        lastPos = lastQ = -1.0f;
        sinceTrigger = 1 << 30;
        pedalCur = pedalTarget; pedalLeft = 0;
    }

    // ---- once per host block, before anything else: the notes (AUTO) and the CC11 / CC4 values of this block, by sample position
    void beginBlock (int hostBlockLength)
    {
        clearEvents();
        rampLen = juce::jmax (ccRamp, hostBlockLength);   // automation arrives once per block: glide over the whole block (no stairs)
    }
    void noteAt (int offset, float velocity) { if (numNotes < maxEvents) { noteOff[numNotes] = offset; noteVel[numNotes] = velocity; ++numNotes; } }
    void ccAt (int offset, float pedalKnob)   // full (more than 128 in one block): the last place takes the newest value, never lost
    {
        const int k = numCC < maxEvents ? numCC++ : maxEvents - 1;
        ccOff[k] = offset; ccVal[k] = juce::jlimit (0.0f, 10.0f, pedalKnob) / 10.0f;
    }
    void useCC (bool fromCC) { ccSource = fromCC; }   // follow the CCs of this block (else they are only remembered by the processor)

    // pedal 0..10 (heel .. toe; < 0 = leave the pedal to the CCs), q 0..10, speed 0..10 (AUTO: slow .. fast).
    // snap: resting / prepare - everything at once, AUTO closed
    void setParams (float pedalKnob, float qKnob, float speedKnob, bool snap = false)
    {
        if (pedalKnob >= 0.0f)
        {
            const float p = juce::jlimit (0.0f, 10.0f, pedalKnob) / 10.0f;
            if (snap) { pedalCur = pedalTarget = p; pedalLeft = 0; }
            else if (p != pedalTarget) glideTo (p, rampLen);
        }
        else if (snap) { pedalCur = pedalTarget; pedalLeft = 0; }
        const float q = std::pow (2.0f, (juce::jlimit (0.0f, 10.0f, qKnob) - 5.0f) / 5.0f);   // the curve x 0.5 .. 2 (5 = as measured)
        if (snap) { qSm.setCurrentAndTargetValue (q); env = envTarget = 0.0f; shown.store (pedalTarget); }
        else qSm.setTargetValue (q);
        // AUTO: the time it takes to come back (to 1/e): SPEED 0 = 1.2 s, 7 = 0.15 s, 10 = 60 ms
        const float t = 1.2f * std::pow (0.05f, juce::jlimit (0.0f, 10.0f, speedKnob) / 10.0f);
        releaseK = 1.0f - std::exp (-1.0f / (t * (float) sr));
    }

    // the slot is off (not processing): the panel's bar still follows the pedal
    void showResting() { shown.store (pedalTarget); }

    void setAuto (bool on) { autoOn = on; if (! on) envTarget = 0.0f; }   // switched off: AUTO goes back by itself (no jump)

    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = juce::jmin (numChannels, numCh);
        for (int i = 0; i < numSamples; ++i)
        {
            const int at = blockPos + i;
            while (nextCC < numCC && ccOff[nextCC] <= at)   // CC11 / CC4 at its own place in the block
            {
                if (ccSource) glideTo (ccVal[nextCC], ccRamp);
                ++nextCC;
            }
            while (nextNote < numNotes && noteOff[nextNote] <= at)   // AUTO: a new stroke opens it (by its velocity), never closes it early
            {
                const float open = std::pow (juce::jlimit (0.0f, 1.0f, noteVel[nextNote]), 0.7f);
                if (autoOn)
                {
                    if (sinceTrigger > strokeLen) { envTarget = juce::jmax (env, open); sinceTrigger = 0; strokeOpen = open; }
                    else if (open > strokeOpen) { envTarget = juce::jmax (envTarget, open); strokeOpen = open; }   // same stroke: only ever more open
                }
                ++nextNote;
            }
            if (sinceTrigger < (1 << 30)) ++sinceTrigger;
            if (envTarget > env) { env += (envTarget - env) * attackK; if (envTarget - env < 1.0e-4f) { env = envTarget; envTarget = 0.0f; } }
            else env -= env * releaseK;   // back to the pedal (also after AUTO is switched off: no jump)

            if (pedalLeft > 0) { pedalCur += pedalStep; if (--pedalLeft == 0) pedalCur = pedalTarget; }
            const float pos = juce::jlimit (0.0f, 1.0f, pedalCur + (1.0f - pedalCur) * env);
            const float qv = qSm.getNextValue();
            if (pos != lastPos || qv != lastQ)   // a parked pedal costs nothing
            {
                lastPos = pos; lastQ = qv;
                const float fc = freqAt (pos);
                const float Q = qAt (pos) * qv;
                // TPT state variable filter (Zavalishin): g = tan(pi fc / fs), R = 1 / 2Q
                g = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, 0.45f * (float) sr) / (float) sr);
                R = 0.5f / Q;
                h = 1.0f / (1.0f + 2.0f * R * g + g * g);
                outK = kOut * levelComp (pos, qv);
            }
            for (int c = 0; c < nc; ++c)
            {
                // input: deep lows a little out (one pole high-pass at 60 Hz)
                const float x0 = data[c][i];
                hpZ[c] = x0 + hpA * (hpZ[c] - x0);
                const float x = x0 - hpZ[c];
                const float hp = (x - (2.0f * R + g) * s1[c] - s2[c]) * h;
                const float v1 = g * hp, bp = v1 + s1[c]; s1[c] = bp + v1;
                const float v2 = g * bp, lp = v2 + s2[c]; s2[c] = lp + v2;
                // the resonance (band-pass at unity peak) + a little of the low side
                data[c][i] = outK * (2.0f * R * bp + kBody * lp);
            }
        }
        blockPos += numSamples;
        shown.store (lastPos < 0.0f ? pedalCur : lastPos);
    }

    float shownPosition() const { return shown.load(); }   // 0 heel .. 1 toe (UI)

private:
    void clearEvents() { numNotes = numCC = 0; nextNote = nextCC = 0; blockPos = 0; }
    void glideTo (float p, int len)
    {
        pedalTarget = p;
        pedalLeft = juce::jmax (1, len);
        pedalStep = (pedalTarget - pedalCur) / (float) pedalLeft;
    }

    static constexpr float kBody = 0.08f, kOut = 2.77f;   // the low side ~23 dB under the peak; kOut: ON as loud as OFF in the middle (the user's DI)
    // the same loudness wherever the pedal is and whatever the Q (on the user's DI): a sharper Q is ~1.9 dB quieter per doubling,
    // and around 3/4 of the way (~1.3 kHz) it came out ~3 dB low (DSP inspection) - a gentle +1.5 dB there
    static float levelComp (float pos, float qMul)
    {
        const float d = (pos - 0.75f) / 0.15f;
        return std::pow (qMul, kQComp) * std::pow (10.0f, kBumpDb * std::exp (-d * d) / 20.0f);
    }
    static constexpr float kQComp = 0.316f, kBumpDb = 1.5f;

    double sr = 48000.0;
    int numCh = 2;
    juce::SmoothedValue<float> qSm;
    float s1[maxCh] {}, s2[maxCh] {}, hpZ[maxCh] {};
    float hpA = 0.99f, env = 0.0f, envTarget = 0.0f, attackK = 0.01f, releaseK = 0.001f;
    float lastPos = -1.0f, lastQ = -1.0f, g = 0.1f, R = 0.1f, h = 1.0f, outK = 1.0f, strokeOpen = 0.0f;
    float pedalCur = 0.5f, pedalTarget = 0.5f, pedalStep = 0.0f;
    int pedalLeft = 0, rampLen = 960, ccRamp = 960, strokeLen = 1440, sinceTrigger = 1 << 30;
    bool autoOn = false, ccSource = false;
    int noteOff[maxEvents] {}, ccOff[maxEvents] {}, numNotes = 0, numCC = 0, nextNote = 0, nextCC = 0, blockPos = 0;
    float noteVel[maxEvents] {}, ccVal[maxEvents] {};
    std::atomic<float> shown { 0.5f };
};
} // namespace pedal
