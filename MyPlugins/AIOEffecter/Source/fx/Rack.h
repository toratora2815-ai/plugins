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
#include <chrono>

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
        rFadeStep = (float) (1.0 / (0.002 * sr)); rFade = 1.0f; rTau = (float) (8.0 * sr / 48000.0); gateLen = std::max (1, (int) (0.002 * sr));
        lastOut[0] = lastOut[1] = prevOut[0] = prevOut[1] = hold[0] = hold[1] = slope[0] = slope[1] = 0.0f;
        envA = (float) std::exp (-1.0 / (0.01 * sr)); envPk[0] = envPk[1] = 0.0f; lastInPk = 0.0f;
        for (auto& s : slots) s.dryGate = 1 << 30;
        gapUs = std::max<int64_t> (100000, (int64_t) (4.0e6 * block / sr)); lastProcUs.store (0, std::memory_order_relaxed); resetPending = false;
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
            if (p->mod) { applyKnobs (*p->mod, want[(size_t) i]); p->mod->prepare (sr, block); p->mod->reset(); p->mod->startFresh(); p->lat = std::clamp (p->mod->latency(), 0, ringLen - block - 2); }
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
        // reset の前後で process が途切れていた（Logic が止めている間・バイパスの間は呼ばないことがある）なら、続きではないので reset のつなぎをやめて頭から
        if (resetPending)
        {
            resetPending = false;
            if (processGap()) undoContinuation();
        }
        // 入口の壊れた数（NaN / inf）は 0 に（どの部品にも入れない）
        float inPk = 0.0f;
        for (int j = 0; j < n; ++j) { if (! std::isfinite (L[j])) L[j] = 0.0f; if (! std::isfinite (R[j])) R[j] = 0.0f; inPk = std::max (inPk, std::max (std::abs (L[j]), std::abs (R[j]))); }
        if (n > 0) lastInPk = inPk;
        for (int i = 0; i < kSlots; ++i)
            processSlot (i, L, R, n, want[(size_t) i]);
        // 安全のための頭打ち（+24 dBFS）。ふだんは届かない（TUBE COMP を全部最大にしたときなどの事故よけ）
        for (int j = 0; j < n; ++j) { L[j] = std::clamp (L[j], -16.0f, 16.0f); R[j] = std::clamp (R[j], -16.0f, 16.0f); }
        // ホストの reset の直後は、reset 前の最後の音（とその傾き。8 サンプルの時定数で止める）から 2 ms でつなぐ
        // （中を全部消すので、音が流れている最中だと段になってプチッ: DSP 検査 0.7.0 の 2 回目）
        if (rFade < 1.0f)
            for (int j = 0; j < n; ++j)
            {
                rFade = std::min (1.0f, rFade + rFadeStep);
                const float f = smoothstep (rFade), t = (float) (++rPos);
                const float ext = rTau * (1.0f - std::exp (-t / rTau));
                // 傾きの外挿は直前の出口の山（10 ms で減る包絡）より外へ出さない（高い音で行き過ぎて +10〜+28 dBFS になった: DSP 検査 0.7.0 の 3 回目）
                const float hl = std::clamp (hold[0] + slope[0] * ext, -envPk[0], envPk[0]), hr = std::clamp (hold[1] + slope[1] * ext, -envPk[1], envPk[1]);
                L[j] = std::clamp (hl + f * (L[j] - hl), -16.0f, 16.0f); R[j] = std::clamp (hr + f * (R[j] - hr), -16.0f, 16.0f);
            }
        else
            for (int j = 0; j < n; ++j) { envPk[0] = std::max (std::abs (L[j]), envPk[0] * envA); envPk[1] = std::max (std::abs (R[j]), envPk[1] * envA); }
        if (n > 1) { prevOut[0] = L[n - 2]; prevOut[1] = R[n - 2]; }
        else if (n == 1) { prevOut[0] = lastOut[0]; prevOut[1] = lastOut[1]; }
        if (n > 0) { lastOut[0] = L[n - 1]; lastOut[1] = R[n - 1]; }
        lastProcUs.store (nowUs(), std::memory_order_relaxed);
    }

    // ホストのリセット（再生の停止・位置の移動・書き出しの前）: 全部の段の響き・下げを消す（確保なし）。ドライの遅れの記録も消す（前の音が出ないように: DSP 検査 0.2.0）。
    // 音が流れている最中の reset だけ、段にならないようにつなぐ（DSP 検査 0.7.0 の 2・3 回目）:
    //   遅れのある段は、新しい音が遅れの分だけ届いてから段の出口全体を 2 ms でフェードイン / 天井のない遅れのある部品は先読みが埋まるまでドライ → 15 ms で部品の音へ /
    //   響きのある部品の入口は 15 ms でフェードイン / 出口が鳴っていたら、最後の音と傾き（山の包絡で頭打ち）から 2 ms でつなぐ。無音からの reset では何もしない
    void resetAll()
    {
        // 入口に音が来ていた（−80 dBFS より上）ときだけ、響きの部品の入口をフェードイン。出口が −50 dBFS より上で鳴っていたときだけつなぐ
        // （どちらも無音・サーッだけなら何もしない = 再生の頭・書き出しの頭の最初の音を小さくしない。TAPE WOW のサーッは山で −54 dBFS ほど）
        const bool gap = processGap();   // 最後の process から時間がたっている = 止まっていた（そのときの値は古い）
        // reset が続けて来た（間に process がない）: 途切れていたら前の reset のつなぎを先にやめる。途切れていなければ、最初の reset の前の wet を残す
        // （2 回目の reset で 0 を写すと、途切れたあとに戻せずドライのまま始まった: DSP 検査 0.7.0 の 5 回目）
        if (resetPending && gap) { undoContinuation(); resetPending = false; }
        const bool keepBefore = resetPending;
        const bool flowing = ! gap && lastInPk >= 1.0e-4f;
        const bool sounding = ! gap && std::max (envPk[0], envPk[1]) >= 3.0e-3f;
        for (auto& s : slots)
        {
            // 音が流れている最中なら、響き・遅れの線のある部品の入口を 15 ms でフェードイン（空にした遅れの線に途中から音が入って、あとで段になって出るのを防ぐ）
            if (! keepBefore) s.wetBeforeReset = s.wet;
            if (s.mod) { s.mod->reset(); s.inRamp = (flowing && s.mod->tailSeconds() > 0.0) ? 0.0f : 1.0f; }
            std::fill (s.ring.begin(), s.ring.end(), 0.0f);
            s.dryGate = (flowing && s.lat > 0) ? -s.lat : (1 << 30);   // 音が流れていたら、遅れのある段の出口（ドライも部品の音も）を、新しい音が届いてから 2 ms でフェードイン
            // 天井のない部品（コンプ類）は、そのあと 15 ms ドライから部品の音へ（下げを消した直後の跳ねを隠す）。
            // 天井のある部品（MAXIMIZER、Module::hasCeiling）はドライを出さない（出すと天井 −1 dBFS を +3.7〜+5.2 dBFS 超えた: DSP 検査 0.7.0 の 3 回目）
            if (flowing && s.running && s.lat > 0 && s.mod && ! s.mod->hasCeiling()) { s.prime = s.lat; s.wet = 0.0f; }
            s.latFade = 1.0f;
        }
        hold[0] = lastOut[0]; hold[1] = lastOut[1]; slope[0] = lastOut[0] - prevOut[0]; slope[1] = lastOut[1] - prevOut[1]; rFade = sounding ? 0.0f : 1.0f; rPos = 0;
        resetPending = keepBefore || flowing || sounding;
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
        int dryGate = 1 << 30;                      // reset の直後のドライ（遅れの記録）のフェードイン（負 = 新しい音が届くまで待つ）
        float wetBeforeReset = 0.0f;                // reset の直前の wet（reset の前後で process が途切れていたら戻す）
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
                const int g0 = s.dryGate;   // reset の直後のゲート（このブロックの頭の値）
                auto gateAt = [&] (int j) { const int gv = g0 + j; return gv < 0 ? 0.0f : (gv >= gateLen ? 1.0f : smoothstep ((float) gv / (float) gateLen)); };
                if (s.dryGate < gateLen) s.dryGate = std::min (gateLen, s.dryGate + m);
                if (! active)
                {
                    std::copy (dL, dL + m, L + off); std::copy (dR, dR + m, R + off);
                    if (g0 < gateLen) for (int j = 0; j < m; ++j) { const float gg = gateAt (j); L[off + j] *= gg; R[off + j] *= gg; }
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
                    if (g0 < gateLen) { const float gg = gateAt (j); L[off + j] *= gg; R[off + j] *= gg; }
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

    static int64_t nowUs() { return (int64_t) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
    bool processGap() const { const int64_t t = lastProcUs.load (std::memory_order_relaxed); return t == 0 || nowUs() - t > gapUs; }
    void undoContinuation()   // reset で用意したつなぎ（出口のつなぎ・段のゲート・入口のフェード・ドライの混ぜ）をやめる = 無音からの reset と同じ
    {
        rFade = 1.0f;
        for (auto& s : slots)
        {
            s.dryGate = 1 << 30; s.inRamp = 1.0f;
            if (s.prime > 0 && s.wet <= 0.0f && s.wetBeforeReset > 0.0f) { s.prime = 0; s.wet = s.wetBeforeReset; }
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
    float rFade = 1.0f, rFadeStep = 0.01f, rTau = 8.0f, hold[2] {}, slope[2] {}, lastOut[2] {}, prevOut[2] {};   // reset の直後のつなぎ
    int rPos = 0, gateLen = 96;
    float envPk[2] {}, envA = 0.998f, lastInPk = 0.0f;   // 出口の山の包絡（10 ms で減る。reset のつなぎの頭打ち）
    std::atomic<int64_t> lastProcUs { 0 };   // 最後の process の終わり（壁の時計、マイクロ秒。0 = まだ）
    int64_t gapUs = 100000;                  // これより長く process が来なかったら「途切れた」（100 ms か 4 ブロックの長いほう）
    bool resetPending = false;               // reset でつなぎを用意した（次の process で、途切れていたらやめる）
    bool prepared = false;
};
} // namespace aiofx
