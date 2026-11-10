#pragma once
// 音の流れ全体（EFFECTS ページの並び）: IN → [COMP / DRIVE / FUZZ / AMP を好きな順に] → CAB → OUTPUT
//  - CAB（スピーカー＋マイク）と OUTPUT はいつも最後（本物のギターと同じ。AMP の後ろの歪みもスピーカーが丸める）
//  - 並びを変えたら: 5 ms で下げる → 次のブロックで入れ替え、ペダルと AMP をまっさらにして 20 ms 無音で慣らす → 8 ms で上げる
//    （前の位置での状態＝コンプの下げ量などを持ち越すと、つないだ直後に音が飛び出すため）。遅れの合計は並びに関係なく同じ
//  - AMP より後ろにペダルがあるとき: AMP の出口を、機種・GAIN・MASTER で決まる固定の量だけ上げ下げして（ギターの DI の大きさに
//    そろえる。AmpPad.h＝オフラインで測った表）、最後のペダルの後で元に戻す。ペダルは DI を前提に作ってあるので、
//    AMP の後ろでも同じように効く。鳴っている音は追いかけない（コンプにならない）
#include "Pedals.h"
#include "FXChain.h"
#include "AmpPad.h"

namespace rig
{
    enum Block { blkComp = 0, blkDrive, blkFuzz, blkAmp, numBlocks };
    using Order = std::array<int, numBlocks>;
    constexpr int kNumOrders = 24;   // 4 blocks: 4! orders
    constexpr int kDefault = 0;      // Comp > Drive > Fuzz > Amp

    inline Order orderAt (int index)
    {
        Order o { 0, 1, 2, 3 };
        for (int k = juce::jlimit (0, kNumOrders - 1, index); k > 0; --k) std::next_permutation (o.begin(), o.end());
        return o;
    }
    inline int indexOf (const Order& o)
    {
        Order p { 0, 1, 2, 3 };
        for (int k = 0; k < kNumOrders; ++k) { if (p == o) return k; std::next_permutation (p.begin(), p.end()); }
        return kDefault;
    }
    inline const char* blockName (int b) { static const char* n[] { "Comp", "Drive", "Fuzz", "Amp" }; return n[juce::jlimit (0, 3, b)]; }
    inline juce::StringArray orderNames()
    {
        juce::StringArray s;
        for (int k = 0; k < kNumOrders; ++k)
        {
            const auto o = orderAt (k);
            s.add (juce::String (blockName (o[0])) + " > " + blockName (o[1]) + " > " + blockName (o[2]) + " > " + blockName (o[3]));
        }
        return s;
    }
    inline int posOf (const Order& o, int block) { for (int k = 0; k < numBlocks; ++k) if (o[(size_t) k] == block) return k; return 0; }
    // one block taken out and put back at position `to` (the others keep their order)
    inline Order moved (const Order& o, int block, int to)
    {
        int rest[numBlocks - 1], n = 0;
        for (int b : o) if (b != block) rest[n++] = b;
        to = juce::jlimit (0, numBlocks - 1, to);
        Order r {};
        for (int k = 0, j = 0; k < numBlocks; ++k) r[(size_t) k] = k == to ? block : rest[j++];
        return r;
    }
    // projects from before 0.3.0: "Pedal Order" (Drive > Fuzz / Fuzz > Drive), the comp first and the amp last
    inline int fromOldPedalOrder (bool fuzzFirst) { return fuzzFirst ? indexOf ({ blkComp, blkFuzz, blkDrive, blkAmp }) : kDefault; }
}

class Rig
{
public:
    PedalSlot<pedal::CottonCompDSP> comp;    // Cotton Comp
    PedalSlot<pedal::PorchDriveDSP> drive;   // Porch Drive
    PedalSlot<pedal::WoolFuzzDSP>   fuzz;    // Wool Fuzz
    FXChain chain;                           // AMP / CAB / OUTPUT

    void prepare (double sampleRate, int maxBlockSize, int orderIndex, const FXParams& p)
    {
        chain.prepare (sampleRate, maxBlockSize);
        maxBlock = chain.getMaxBlock();
        comp.prepare (sampleRate, maxBlock); drive.prepare (sampleRate, maxBlock); fuzz.prepare (sampleRate, maxBlock);
        fadeLen = juce::jmax (1, (int) (sampleRate * 0.005));
        fadeInLen = juce::jmax (1, (int) (sampleRate * 0.008));
        warmLen = juce::jmax (1, (int) (sampleRate * 0.020));
        pad.reset (sampleRate, 0.015);   // as fast as the AMP's own on / off crossfade
        padBuf.assign ((size_t) maxBlock, 1.0f);
        setOrderNow (orderIndex, p);
    }

    int latency() const { return comp.latency() + drive.latency() + fuzz.latency() + chain.getLatencySamples(); }
    bool anyPedalRunning() const { return comp.isRunning() || drive.isRunning() || fuzz.isRunning(); }
    int orderIndex() const { return orderIdx; }

    // prepare / load / resting: this order at once, no fade
    void setOrderNow (int index, const FXParams& p)
    {
        orderIdx = juce::jlimit (0, rig::kNumOrders - 1, index);
        order = rig::orderAt (orderIdx);
        fadeState = 0;
        pad.setCurrentAndTargetValue (padTarget (p));
    }

    // nothing is playing: take everything at once (the processor snaps the pedal knobs)
    void rest (const FXParams& p, int orderIndex, int numSamples)
    {
        chain.rest (p, numSamples);
        setOrderNow (orderIndex, p);
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p, int wantOrder)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        // a broken sample (NaN / inf / absurdly large) would stay in the filters: drop the block and start clean
        if (broken (L, R, numSamples)) { buffer.clear (0, numSamples); resetAll(); return; }
        wantOrder = juce::jlimit (0, rig::kNumOrders - 1, wantOrder);
        chain.beginBlock (p);
        for (int pos = 0; pos < numSamples; pos += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - pos);
            processChunk (L + pos, R + pos, n, p, wantOrder);
            if (broken (L + pos, R + pos, n)) { buffer.clear (0, numSamples); resetAll(); return; }   // never pass one on
        }
    }

private:
    static bool broken (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
            if (! (std::abs (L[i]) < 1.0e4f) || ! (std::abs (R[i]) < 1.0e4f)) return true;
        return false;
    }

    void resetAll()
    {
        chain.reset();
        comp.reset(); drive.reset(); fuzz.reset();
    }

    // the AMP output brought to the guitar's own level (up or down), only when pedals follow it
    float padTarget (const FXParams& p) const
    {
        if (! p.ampOn || rig::posOf (order, rig::blkAmp) == rig::numBlocks - 1) return 1.0f;
        return juce::Decibels::decibelsToGain (juce::jlimit (-36.0f, 12.0f, ampPad::padDb (p.ampModel, p.ampGain, p.ampMaster)));
    }

    void processChunk (float* L, float* R, int n, const FXParams& p, int wantOrder)
    {
        // changing the order: 5 ms down, swap at the next chunk with every block starting clean, 20 ms silent
        // while they settle on the real input, 8 ms up
        if (fadeState == 3)
        {
            orderIdx = wantOrder; order = rig::orderAt (orderIdx);
            pad.setCurrentAndTargetValue (padTarget (p));
            comp.reset(); drive.reset(); fuzz.reset(); chain.resetAmp();
            fadeState = 4; holdLeft = warmLen; fadePos = 0;
        }
        if (fadeState == 0 && wantOrder != orderIdx) { fadeState = 1; fadePos = fadeLen; }

        pad.setTargetValue (padTarget (p));
        const bool padMoving = pad.isSmoothing();
        const bool usePad = rig::posOf (order, rig::blkAmp) < rig::numBlocks - 1
                            && (padMoving || std::abs (pad.getCurrentValue() - 1.0f) > 1.0e-6f);
        if (padMoving) for (int i = 0; i < n; ++i) padBuf[(size_t) i] = pad.getNextValue();
        else std::fill (padBuf.begin(), padBuf.begin() + n, pad.getCurrentValue());

        float* d[2] { L, R };
        for (int b : order)
        {
            switch (b)
            {
                case rig::blkComp:  comp.process (d, n); break;
                case rig::blkDrive: drive.process (d, n); break;
                case rig::blkFuzz:  fuzz.process (d, n); break;
                default:
                    chain.processAmp (L, R, n, p);
                    if (usePad) for (int i = 0; i < n; ++i) { L[i] *= padBuf[(size_t) i]; R[i] *= padBuf[(size_t) i]; }
                    break;
            }
        }
        if (usePad)   // back to the amp's own level after the last pedal
            for (int i = 0; i < n; ++i) { const float g = 1.0f / padBuf[(size_t) i]; L[i] *= g; R[i] *= g; }

        if (fadeState != 0)
            for (int i = 0; i < n; ++i)
            {
                float g = 1.0f;
                if (fadeState == 1)      { g = (float) fadePos / (float) fadeLen; if (fadePos > 0) --fadePos; else fadeState = 3; }
                else if (fadeState == 3) g = 0.0f;   // silent until the next chunk, where the order changes
                else if (fadeState == 4) { g = 0.0f; if (--holdLeft <= 0) { fadeState = 2; fadePos = 0; } }   // settling
                else if (fadeState == 2) { g = (float) fadePos / (float) fadeInLen; if (++fadePos >= fadeInLen) fadeState = 0; }
                g = g * g * (3.0f - 2.0f * g);       // S-curve: no corner at either end
                L[i] *= g; R[i] *= g;
            }

        chain.processCabOut (L, R, n, p);
    }

    int maxBlock = 512;
    rig::Order order { 0, 1, 2, 3 };
    int orderIdx = rig::kDefault;
    int fadeLen = 240, fadeInLen = 384, warmLen = 960, holdLeft = 0, fadePos = 0;
    int fadeState = 0;   // 0 steady, 1 fading out, 3 silent (swap next chunk), 4 silent (settling), 2 fading in
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> pad { 1.0f };
    std::vector<float> padBuf;
};
