#pragma once
// 音の流れ全体（EFFECTS ページの並び）: IN → [COMP / DRIVE A / DRIVE B / AMP / TREM / WAH を好きな順に] → CAB → OUTPUT
//  - WAH（0.7.0）: 並びは「前の 5 箱の並び fxChain（そのまま）＋ WAH の場所 wahAt（0〜5）」で持つ。前の曲・前のプリセットは
//    読み替えなしで WAH がいちばん前（OFF）。並びの鍵 = fxChain × 6 + wahAt（0〜719）
//  - DRIVE A / B（0.6.0）は 0.5.0 までの DRIVE（PORCH DRIVE）と FUZZ（WOOL FUZZ）の箱。どちらでも 5 機種から選べる（DriveSlot.h）。
//    並びの番号（fxChain）は 0.5.0 と同じ: DRIVE A = 前の DRIVE の位置、DRIVE B = 前の FUZZ の位置
//  - TREM（0.4.0）は My Tremolo の小型版。初期位置は AMP の直後（本物のアンプのトレモロと同じ所）
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
    enum Block { blkComp = 0, blkDrive, blkFuzz, blkAmp, blkTrem, blkWah, numBlocks };   // numBlocks = 6 (all movable blocks)
    constexpr int numChainBlocks = 5;                       // the blocks whose order is fxChain (all but WAH)
    using Order = std::array<int, numChainBlocks>;          // fxChain
    using Route = std::array<int, numBlocks>;               // the whole row, left to right
    constexpr int kNumOrders = 120;  // 5 blocks: 5! orders（パラメーター fxChain の番号）
    constexpr int kDefault = 0;      // Comp > Drive > Fuzz > Amp > Trem
    constexpr int kDefaultWahAt = 0; // WAH first (straight from the guitar, like most pedalboards)
    constexpr int kNumRoutes = kNumOrders * numBlocks;

    inline Order orderAt (int index)
    {
        Order o { 0, 1, 2, 3, 4 };
        for (int k = juce::jlimit (0, kNumOrders - 1, index); k > 0; --k) std::next_permutation (o.begin(), o.end());
        return o;
    }
    inline int indexOf (const Order& o)
    {
        Order p { 0, 1, 2, 3, 4 };
        for (int k = 0; k < kNumOrders; ++k) { if (p == o) return k; std::next_permutation (p.begin(), p.end()); }
        return kDefault;
    }
    inline const char* blockName (int b) { static const char* n[] { "Comp", "Drive A", "Drive B", "Amp", "Trem", "Wah" }; return n[juce::jlimit (0, numBlocks - 1, b)]; }
    inline juce::StringArray orderNames()
    {
        juce::StringArray s;
        for (int k = 0; k < kNumOrders; ++k)
        {
            const auto o = orderAt (k);
            juce::String t;
            for (int j = 0; j < numChainBlocks; ++j) t << (j ? " > " : "") << blockName (o[(size_t) j]);
            s.add (t);
        }
        return s;
    }
    // 0.3.0 の「Effects Order」（COMP / DRIVE / FUZZ / AMP の 24 通り、パラメーター fxOrder）→ TREM を AMP の直後に入れた番号
    inline int fromOrder4 (int index4)
    {
        std::array<int, 4> o4 { 0, 1, 2, 3 };
        for (int k = juce::jlimit (0, 23, index4); k > 0; --k) std::next_permutation (o4.begin(), o4.end());
        Order o {}; int j = 0;
        for (int b : o4) { o[(size_t) j++] = b; if (b == blkAmp) o[(size_t) j++] = blkTrem; }
        return indexOf (o);
    }
    template <size_t N> int posOf (const std::array<int, N>& o, int block) { for (size_t k = 0; k < N; ++k) if (o[k] == block) return (int) k; return 0; }
    // one block taken out and put back at position `to` (the others keep their order)
    template <size_t N> std::array<int, N> moved (const std::array<int, N>& o, int block, int to)
    {
        int rest[N], n = 0;
        for (int b : o) if (b != block) rest[n++] = b;
        to = juce::jlimit (0, (int) N - 1, to);
        std::array<int, N> r {};
        for (int k = 0, j = 0; k < (int) N; ++k) r[(size_t) k] = k == to ? block : rest[j++];
        return r;
    }
    // the whole row: the five of fxChain with WAH put in at wahAt
    inline Route routeOf (int chainIndex, int wahAt)
    {
        const auto o = orderAt (chainIndex);
        wahAt = juce::jlimit (0, numBlocks - 1, wahAt);
        Route r {};
        for (int k = 0, j = 0; k < numBlocks; ++k) r[(size_t) k] = k == wahAt ? (int) blkWah : o[(size_t) j++];
        return r;
    }
    inline void splitRoute (const Route& r, int& chainIndex, int& wahAt)
    {
        Order o {}; int j = 0;
        for (int b : r) if (b != blkWah) o[(size_t) j++] = b;
        chainIndex = indexOf (o);
        wahAt = posOf (r, blkWah);
    }
    inline int routeKey (int chainIndex, int wahAt) { return juce::jlimit (0, kNumOrders - 1, chainIndex) * numBlocks + juce::jlimit (0, numBlocks - 1, wahAt); }
    inline Route routeAtKey (int key) { key = juce::jlimit (0, kNumRoutes - 1, key); return routeOf (key / numBlocks, key % numBlocks); }
    // projects from before 0.3.0: "Pedal Order" (Drive > Fuzz / Fuzz > Drive), the comp first, then the amp and the trem
    inline int fromOldPedalOrder (bool fuzzFirst) { return fuzzFirst ? indexOf ({ blkComp, blkFuzz, blkDrive, blkAmp, blkTrem }) : kDefault; }
}

class Rig
{
public:
    Rig() { fuzz.get().setSlotDefaultLevel (-12.0f); }   // DRIVE B's LEVEL default (WOOL FUZZ's own)

    PedalSlot<pedal::CottonCompDSP> comp;    // Cotton Comp
    PedalSlot<pedal::DriveSlotDSP>  drive;   // DRIVE A (PORCH DRIVE by default; params pd*)
    PedalSlot<pedal::DriveSlotDSP>  fuzz;    // DRIVE B (WOOL FUZZ by default; params wf*)
    PedalSlot<pedal::TremDSP>       trem;    // TREM（My Tremolo の小型版）
    PedalSlot<pedal::WahDSP>        wah;     // BELLOWS WAH (0.7.0)
    FXChain chain;                           // AMP / CAB / OUTPUT

    void prepare (double sampleRate, int maxBlockSize, int orderIndex, const FXParams& p)
    {
        chain.prepare (sampleRate, maxBlockSize);
        maxBlock = chain.getMaxBlock();
        comp.prepare (sampleRate, maxBlock); drive.prepare (sampleRate, maxBlock); fuzz.prepare (sampleRate, maxBlock); trem.prepare (sampleRate, maxBlock);
        wah.prepare (sampleRate, maxBlock);
        fadeLen = juce::jmax (1, (int) (sampleRate * 0.005));
        fadeInLen = juce::jmax (1, (int) (sampleRate * 0.008));
        warmLen = juce::jmax (1, (int) (sampleRate * 0.020));
        pad.reset (sampleRate, 0.015);   // as fast as the AMP's own on / off crossfade
        padBuf.assign ((size_t) maxBlock, 1.0f);
        setOrderNow (orderIndex, p);
    }

    int latency() const { return comp.latency() + drive.latency() + fuzz.latency() + trem.latency() + wah.latency() + chain.getLatencySamples(); }
    bool anyPedalRunning() const { return comp.isRunning() || drive.isRunning() || fuzz.isRunning() || trem.isRunning() || wah.isRunning(); }
    int orderIndex() const { return orderIdx; }

    // prepare / load / resting: this order at once, no fade. index = rig::routeKey (fxChain, wahAt)
    void setOrderNow (int index, const FXParams& p)
    {
        orderIdx = juce::jlimit (0, rig::kNumRoutes - 1, index);
        order = rig::routeAtKey (orderIdx);
        fadeState = 0;
        pad.setCurrentAndTargetValue (padTarget (p));
    }

    // nothing is playing: take everything at once (the processor snaps the pedal knobs)
    void rest (const FXParams& p, int orderIndex, int numSamples)
    {
        chain.rest (p, numSamples);
        setOrderNow (orderIndex, p);
        trem.reset();   // 弾き始めたら、古い揺れの位置からではなく Logic の今の位置から（SYNC OFF なら頭から。つなぎもなし）
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p, int wantOrder)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        // a broken sample (NaN / inf / absurdly large) would stay in the filters: drop the block and start clean
        if (broken (L, R, numSamples)) { buffer.clear (0, numSamples); resetAll(); return; }
        wantOrder = juce::jlimit (0, rig::kNumRoutes - 1, wantOrder);
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
        comp.reset(); drive.reset(); fuzz.reset(); trem.reset(); wah.reset();
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
            orderIdx = wantOrder; order = rig::routeAtKey (orderIdx);
            pad.setCurrentAndTargetValue (padTarget (p));
            comp.reset(); drive.reset(); fuzz.reset(); trem.reset(); wah.reset(); chain.resetAmp();
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
                case rig::blkTrem:  trem.process (d, n); break;
                case rig::blkWah:   wah.process (d, n); break;
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
    rig::Route order = rig::routeAtKey (rig::routeKey (rig::kDefault, rig::kDefaultWahAt));
    int orderIdx = rig::routeKey (rig::kDefault, rig::kDefaultWahAt);
    int fadeLen = 240, fadeInLen = 384, warmLen = 960, holdLeft = 0, fadePos = 0;
    int fadeState = 0;   // 0 steady, 1 fading out, 3 silent (swap next chunk), 4 silent (settling), 2 fading in
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> pad { 1.0f };
    std::vector<float> padBuf;
};
