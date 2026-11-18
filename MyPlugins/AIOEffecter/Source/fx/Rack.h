#pragma once
// ラック: 8 つの差し込み口（スロット）を上から順に通す。JUCE なし（AIO Guitar へ移すときもこのまま使える）。
//
// 部品の入れ替え（type が変わった）:
//   メッセージスレッド（画面・タイマー）で新しい部品を作って prepare → incoming に置く
//   オーディオスレッドが受け取り、出口を 15 ms でドライへ寄せてから入れ替え → 新しい部品へ 15 ms で戻す。古い部品は outgoing へ
//   type が変わってから新しい部品が届くまでの間は、古い部品に新しいつまみを入れずドライへ寄せて待つ
//   メッセージスレッドが outgoing を消す（オーディオスレッドでは new / delete をしない）
// ON / OFF はドライとの 15 ms のクロスフェード。OFF の間は計算しない（戻すときに reset して古い響きを出さない。入る音も 15 ms でフェードイン）
// 部品の出力に NaN / inf / 巨大な値が出たら、その段はドライにして部品を reset。出口は ±16（+24 dBFS）で頭打ち
#include "Registry.h"
#include <atomic>
#include <array>

namespace aiofx
{
constexpr int kSlots = 8;

struct SlotKnobs
{
    int type = 0;
    bool on = false;
    float k[kMaxKnobs] {};
};

class Rack
{
public:
    ~Rack()
    {
        for (auto& s : slots) { delete s.incoming.exchange (nullptr); delete s.outgoing.exchange (nullptr); }
    }

    // ---- メッセージスレッド ----
    // オーディオが止まっている間（prepareToPlay）に、今の type で全部作り直す
    void prepare (double sampleRate, int maxBlock, const std::array<SlotKnobs, kSlots>& want)
    {
        sr = sampleRate; block = std::max (1, maxBlock);
        dry.assign ((size_t) block * 2, 0.0f);
        for (int i = 0; i < kSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            delete s.incoming.exchange (nullptr);
            delete s.outgoing.exchange (nullptr);
            s.mod = makeType (want[(size_t) i].type);
            // つまみを入れてから prepare（prepare がなめらかさを今の値に合わせる = 頭で初期値から動かない）
            if (s.mod) { applyKnobs (*s.mod, want[(size_t) i]); s.mod->prepare (sr, block); s.mod->reset(); }
            s.modType = s.mod ? want[(size_t) i].type : 0;
            s.modTypeShown.store (s.modType, std::memory_order_relaxed);
            s.requested = s.modType;
            s.wet = (s.mod && want[(size_t) i].on) ? 1.0f : 0.0f;
            s.inRamp = 1.0f; s.swapping = false;
        }
        fadeStep = (float) (1.0 / (0.015 * sr));
        prepared = true;
    }

    // タイマーから: type が変わったスロットの部品を作って渡す。消してよい古い部品を消す
    void service (const std::array<SlotKnobs, kSlots>& want)
    {
        for (int i = 0; i < kSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            delete s.outgoing.exchange (nullptr);
            if (! prepared) continue;
            const int t = want[(size_t) i].type;
            if (t == s.requested) continue;
            auto* p = new Pending();
            p->type = t;
            p->mod = makeType (t);
            if (p->mod) { applyKnobs (*p->mod, want[(size_t) i]); p->mod->prepare (sr, block); p->mod->reset(); }
            delete s.incoming.exchange (p);   // 前の受け渡しがまだなら、それは使われていないので消してよい
            s.requested = t;
        }
    }

    // ---- オーディオスレッド ----
    void process (float* L, float* R, int n, const std::array<SlotKnobs, kSlots>& want, double bpm = 120.0)
    {
        tempo = bpm;
        // 入口の壊れた数（NaN / inf）は 0 に（どの部品にも入れない）
        for (int j = 0; j < n; ++j) { if (! std::isfinite (L[j])) L[j] = 0.0f; if (! std::isfinite (R[j])) R[j] = 0.0f; }
        for (int i = 0; i < kSlots; ++i)
            processSlot (i, L, R, n, want[(size_t) i]);
        // 安全のための頭打ち（+24 dBFS）。ふだんは届かない（TUBE COMP を全部最大にしたときなどの事故よけ）
        for (int j = 0; j < n; ++j) { L[j] = std::clamp (L[j], -16.0f, 16.0f); R[j] = std::clamp (R[j], -16.0f, 16.0f); }
    }

    // ホストのリセット（再生の停止・位置の移動・書き出しの前）: 全部の段の響き・下げを消す（確保なし）
    void resetAll()
    {
        for (auto& s : slots)
            if (s.mod) { s.mod->reset(); s.inRamp = 1.0f; }
    }

    double tailSeconds() const { return 30.0; }
    float meter (int slot) const { return meters[(size_t) slot].load (std::memory_order_relaxed); }
    int activeType (int slot) const { return slots[(size_t) slot].modTypeShown.load (std::memory_order_relaxed); }

    static void applyKnobs (Module& m, const SlotKnobs& sk)
    {
        for (int k = 0; k < kMaxKnobs; ++k) m.setKnob (k, sk.k[k]);
    }

private:
    struct Pending { std::unique_ptr<Module> mod; int type = 0; };
    struct Slot
    {
        std::unique_ptr<Module> mod;
        int modType = 0;
        std::atomic<int> modTypeShown { 0 };
        int requested = 0;                          // メッセージスレッドだけが触る
        std::atomic<Pending*> incoming { nullptr };
        std::atomic<Pending*> outgoing { nullptr };
        float wet = 0.0f;                           // 0 = ドライ、1 = 部品の音
        float inRamp = 1.0f;                        // 動き始めの入口のフェードイン（0 → 1）
        bool swapping = false;
    };

    void processSlot (int idx, float* L, float* R, int n, const SlotKnobs& sk)
    {
        auto& s = slots[(size_t) idx];
        // 入れ替えの始まり
        if (! s.swapping && s.incoming.load (std::memory_order_acquire) != nullptr && s.outgoing.load (std::memory_order_acquire) == nullptr)
            s.swapping = true;

        // 部品を選び直した直後（type は変わったが新しい部品がまだ届いていない間）は、古い部品に新しい部品のつまみを入れず、すぐドライへ寄せて待つ
        const bool typeMatches = sk.type == s.modType;
        const bool wantOn = sk.on && s.mod != nullptr && ! s.swapping && typeMatches;
        const float target = wantOn ? 1.0f : 0.0f;

        if (s.mod && (s.wet > 0.0f || target > 0.0f))
        {
            if (s.wet <= 0.0f) { s.mod->reset(); s.inRamp = 0.0f; }   // 止まっていた部品を動かし始めるときは、古い響きを消して、入る音も 15 ms でフェードイン
            if (typeMatches) applyKnobs (*s.mod, sk);
            s.mod->setTempo (tempo);
            float* dL = dry.data();
            float* dR = dry.data() + block;
            bool broken = false;
            for (int off = 0; off < n; off += block)
            {
                const int m = std::min (block, n - off);
                std::copy (L + off, L + off + m, dL);
                std::copy (R + off, R + off + m, dR);
                if (s.inRamp < 1.0f)
                    for (int j = 0; j < m; ++j)
                    {
                        s.inRamp = std::min (1.0f, s.inRamp + fadeStep);
                        const float g = smoothstep (s.inRamp);
                        L[off + j] *= g; R[off + j] *= g;
                    }
                s.mod->process (L + off, R + off, m);
                for (int j = 0; j < m; ++j)
                {
                    if (! same (s.wet, target))
                        s.wet = target > s.wet ? std::min (target, s.wet + fadeStep) : std::max (target, s.wet - fadeStep);
                    const float w = smoothstep (s.wet);
                    const float l = L[off + j], r = R[off + j];
                    const bool ok = std::isfinite (l) && std::isfinite (r) && std::abs (l) < 1000.0f && std::abs (r) < 1000.0f;
                    if (! ok) broken = true;
                    L[off + j] = ok ? dL[j] + w * (l - dL[j]) : dL[j];
                    R[off + j] = ok ? dR[j] + w * (r - dR[j]) : dR[j];
                }
            }
            // NaN / inf・巨大な値が出たら、部品の中を消して出直す（1 回の事故で壊れたままにしない）
            if (broken) { s.mod->reset(); s.inRamp = 0.0f; }
            meters[(size_t) idx].store (s.mod->meter(), std::memory_order_relaxed);
        }
        else
        {
            s.wet = 0.0f;
            meters[(size_t) idx].store (-1.0f, std::memory_order_relaxed);
        }

        // 部品の音が抜けきったら入れ替える
        if (s.swapping && s.wet <= 0.0f)
        {
            if (auto* p = s.incoming.exchange (nullptr, std::memory_order_acq_rel))
            {
                std::swap (s.mod, p->mod);
                std::swap (s.modType, p->type);
                s.outgoing.store (p, std::memory_order_release);   // 古い部品はメッセージスレッドで消す
            }
            s.swapping = false;
            s.modTypeShown.store (s.modType, std::memory_order_relaxed);
        }
    }

    static float smoothstep (float x) { return x * x * (3.0f - 2.0f * x); }

    std::array<Slot, kSlots> slots;
    std::array<std::atomic<float>, kSlots> meters {};
    std::vector<float> dry;
    double sr = 48000.0, tempo = 120.0;
    int block = 512;
    float fadeStep = 0.001f;
    bool prepared = false;
};
} // namespace aiofx
