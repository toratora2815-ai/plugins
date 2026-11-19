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
// 遅れ（先読みのある部品、0.2.0 の MAXIMIZER など）: 段ごとにドライも同じだけ遅らせる（OFF の間も遅らせて、全体の遅れを変えない）。
//   部品を選び直して遅れが変わるときは、ドライの遅れを 15 ms でつなぎ替える。全体の遅れは latency() → プロセッサーがホストに伝える
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
        ringLen = (int) (0.06 * sr) + block + 4;
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
            s.inRamp = 1.0f; s.swapping = false; s.prime = 0; s.running = false;
            s.ring.assign ((size_t) ringLen * 2, 0.0f); s.ringPos = 0;
            s.lat = s.mod ? std::clamp (s.mod->latency(), 0, ringLen - block - 2) : 0;
            s.oldLat = s.lat; s.latFade = 1.0f;
        }
        updateLatency();
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
            if (p->mod) { applyKnobs (*p->mod, want[(size_t) i]); p->mod->prepare (sr, block); p->mod->reset(); p->lat = std::clamp (p->mod->latency(), 0, ringLen - block - 2); }
            delete s.incoming.exchange (p);   // 前の受け渡しがまだなら、それは使われていないので消してよい
            s.requested = t;
        }
    }

    // ---- オーディオスレッド ----
    void process (float* L, float* R, int n, const std::array<SlotKnobs, kSlots>& want, double bpm = 120.0, double ppq = -1.0, bool playing = false)
    {
        // テンポは 5〜999 BPM だけ受ける（inf や 0 が来たら前の値のまま。DSP 検査 0.5.0）
        if (std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0) tempo = bpm;
        songPpq = std::isfinite (ppq) ? ppq : -1.0; songPlaying = playing && songPpq >= 0.0;
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
        {
            if (s.mod) { s.mod->reset(); s.inRamp = 1.0f; }
            std::fill (s.ring.begin(), s.ring.end(), 0.0f);   // ドライの遅れの中身も消す（前の音が出ないように）
            s.latFade = 1.0f;
        }
    }

    double tailSeconds() const { return 30.0; }
    int latency() const { return totalLat.load (std::memory_order_relaxed); }
    float meter (int slot) const { return meters[(size_t) slot].load (std::memory_order_relaxed); }
    int activeType (int slot) const { return slots[(size_t) slot].modTypeShown.load (std::memory_order_relaxed); }

    static void applyKnobs (Module& m, const SlotKnobs& sk)
    {
        for (int k = 0; k < kMaxKnobs; ++k) m.setKnob (k, sk.k[k]);
    }

private:
    struct Pending { std::unique_ptr<Module> mod; int type = 0; int lat = 0; };
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
        std::vector<float> ring;                    // ドライの遅れ用（ステレオ交互）
        int ringPos = 0;
        int lat = 0, oldLat = 0;                    // 今の部品の遅れ / つなぎ替える前の遅れ
        float latFade = 1.0f;                       // ドライの遅れのつなぎ替え（0 → 1）
        bool running = false;                       // 部品を回している（OFF でも遅れのある部品は回し続ける）
        int prime = 0;                              // 動かし始めた部品の先読みを埋めている残りのサンプル数（その間はドライ）
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

        // 遅れのある部品（先読み）は OFF の間も回し続ける（ON にした瞬間に先読みが空で音が抜けないように）
        const bool keepRunning = s.mod != nullptr && s.lat > 0 && typeMatches && ! s.swapping;
        const bool active = s.mod && (s.wet > 0.0f || target > 0.0f || keepRunning);
        const bool delayDry = s.lat > 0 || s.latFade < 1.0f;
        if (active || delayDry)
        {
            // 止まっていた部品を動かし始めるときは古い響きを消す。響きの残る部品（リバーブ・ディレイ）だけ、入る音も 15 ms でフェードイン
            // （急に入った音があとで響きとして出てこないように。響きのない部品はフェードインしない = ON の瞬間に音が痩せない）
            // 遅れのある部品（先読み）は、先読みが本物の音で埋まるまで（lat サンプル）ドライのままにしてから混ぜ始める（空の先読みからいきなり音が出るプチッを防ぐ）
            if (active && s.wet <= 0.0f && ! s.running) { s.mod->reset(); s.inRamp = s.mod->tailSeconds() > 0.0 ? 0.0f : 1.0f; s.prime = s.lat; }
            s.running = active;
            if (active)
            {
                if (typeMatches) applyKnobs (*s.mod, sk);
                s.mod->setTempo (tempo);
                s.mod->setTransport (songPpq, songPlaying);
            }
            float* dL = dry.data();
            float* dR = dry.data() + block;
            bool broken = false;
            for (int off = 0; off < n; off += block)
            {
                const int m = std::min (block, n - off);
                // ドライ（部品の遅れと同じだけ遅らせる）
                for (int j = 0; j < m; ++j)
                {
                    float* rg = s.ring.data();
                    rg[2 * s.ringPos] = L[off + j]; rg[2 * s.ringPos + 1] = R[off + j];
                    if (s.lat == 0 && s.latFade >= 1.0f) { dL[j] = L[off + j]; dR[j] = R[off + j]; }
                    else
                    {
                        const int p1 = (s.ringPos - s.lat + ringLen) % ringLen;
                        float l = rg[2 * p1], r = rg[2 * p1 + 1];
                        if (s.latFade < 1.0f)
                        {
                            s.latFade = std::min (1.0f, s.latFade + fadeStep);
                            const int p0 = (s.ringPos - s.oldLat + ringLen) % ringLen;
                            const float f = smoothstep (s.latFade);
                            l = rg[2 * p0] + f * (l - rg[2 * p0]); r = rg[2 * p0 + 1] + f * (r - rg[2 * p0 + 1]);
                        }
                        dL[j] = l; dR[j] = r;
                    }
                    if (++s.ringPos >= ringLen) s.ringPos = 0;
                }
                if (! active)
                {
                    std::copy (dL, dL + m, L + off); std::copy (dR, dR + m, R + off);
                    continue;
                }
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
                    const float tg = s.prime > 0 ? 0.0f : target;
                    if (s.prime > 0) --s.prime;
                    if (! same (s.wet, tg))
                        s.wet = tg > s.wet ? std::min (tg, s.wet + fadeStep) : std::max (tg, s.wet - fadeStep);
                    const float w = smoothstep (s.wet);
                    const float l = L[off + j], r = R[off + j];
                    const bool ok = std::isfinite (l) && std::isfinite (r) && std::abs (l) < 1000.0f && std::abs (r) < 1000.0f;
                    if (! ok) broken = true;
                    L[off + j] = ok ? dL[j] + w * (l - dL[j]) : dL[j];
                    R[off + j] = ok ? dR[j] + w * (r - dR[j]) : dR[j];
                }
            }
            // NaN / inf・巨大な値が出たら、部品の中を消して出直す（1 回の事故で壊れたままにしない）
            if (active && broken) { s.mod->reset(); s.inRamp = s.mod->tailSeconds() > 0.0 ? 0.0f : 1.0f; s.prime = s.lat; s.wet = 0.0f; }
            if (! active) { s.wet = 0.0f; s.running = false; }
            meters[(size_t) idx].store (active && s.wet > 0.0f ? s.mod->meter() : -1.0f, std::memory_order_relaxed);
        }
        else
        {
            s.wet = 0.0f; s.running = false;
            // 遅れのない段は何もしない（入口をそのまま）。ドライの遅れ用の記録だけ続ける
            for (int j = 0; j < n; ++j)
            {
                float* rg = s.ring.data();
                rg[2 * s.ringPos] = L[j]; rg[2 * s.ringPos + 1] = R[j];
                if (++s.ringPos >= ringLen) s.ringPos = 0;
            }
            meters[(size_t) idx].store (-1.0f, std::memory_order_relaxed);
        }

        // 部品の音が抜けきったら入れ替える
        if (s.swapping && s.wet <= 0.0f && s.latFade >= 1.0f)   // ドライの遅れのつなぎ替えが終わってから次を入れ替える
        {
            if (auto* p = s.incoming.exchange (nullptr, std::memory_order_acq_rel))
            {
                std::swap (s.mod, p->mod);
                std::swap (s.modType, p->type);
                s.running = false;
                if (p->lat != s.lat) { s.oldLat = s.lat; s.lat = p->lat; s.latFade = 0.0f; }   // ドライの遅れを 15 ms でつなぎ替える
                p->lat = s.oldLat;
                s.outgoing.store (p, std::memory_order_release);   // 古い部品はメッセージスレッドで消す
                updateLatency();
            }
            s.swapping = false;
            s.modTypeShown.store (s.modType, std::memory_order_relaxed);
        }
    }

    static float smoothstep (float x) { return x * x * (3.0f - 2.0f * x); }
    void updateLatency()
    {
        int t = 0;
        for (auto& s : slots) t += s.lat;
        totalLat.store (t, std::memory_order_relaxed);
    }

    std::array<Slot, kSlots> slots;
    std::array<std::atomic<float>, kSlots> meters {};
    std::vector<float> dry;
    std::atomic<int> totalLat { 0 };
    double sr = 48000.0, tempo = 120.0, songPpq = -1.0;
    bool songPlaying = false;
    int block = 512, ringLen = 4096;
    float fadeStep = 0.001f;
    bool prepared = false;
};
} // namespace aiofx
