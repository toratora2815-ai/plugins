#pragma once
// My Tremolo の音の処理（JUCE なし）。
// 本人の Mac にある市販のトレモロ（aufx TRM / SToy）をオフラインで測って、揺れ方・色付けを同じ動きに合わせ込んだ。
// 測り方と数値の出どころは NOTES.md と build/tremlab（コミットしない）。
//
// 流れ（1 チャンネル）:
//   INPUT → 4 倍 → [色付けの前半] → 揺れ（VCA。LOW KEEP なら高い帯域だけ）→ OUTPUT → [色付けの後半] → 1 倍
// 揺れの時計:
//   拍の位置（Logic に同期 or 自前の BPM）→ RATE MOD で速さを変える → FEEL でずらす → GROOVE で 2 つ組の長さを変える
//   → リズム（音符の長さ or パターン）の中のどの「1 回」か → 形の表 × ACCENT → DEPTH（DEPTH MOD）で利得へ
#include "TremoloStyle.h"
#include "TremoloShapes.h"
#include "TremoloStyleTables.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>

namespace mytrem
{
    //==============================================================================
    // リズム（RHYTHM の選択肢）。拍（4 分音符 = 1）の長さ
    constexpr int kNumRhythms = 16;
    constexpr int kRhythmPattern = 15;   // 最後 = 自分で描いたパターン
    inline const char* rhythmName (int i)
    {
        static const char* n[] = { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T",
                                   "1/16.", "1/16", "1/16T", "1/32", "Pattern" };
        return (i >= 0 && i < kNumRhythms) ? n[i] : "?";
    }
    inline double rhythmBeats (int i)
    {
        static const double b[] = { 16, 8, 4, 2, 4.0 / 3, 1.5, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.375, 0.25, 1.0 / 6, 0.125, 0.5 };
        return (i >= 0 && i < kNumRhythms) ? b[i] : 0.5;
    }

    // パターンの升目の細かさ（GRID）
    constexpr int kNumGrids = 6;
    inline const char* gridName (int i) { static const char* n[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return (i >= 0 && i < kNumGrids) ? n[i] : "?"; }
    inline double gridBeats (int i) { static const double b[] = { 1, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125 }; return (i >= 0 && i < kNumGrids) ? b[i] : 0.5; }

    // 自分で描くパターン: 小節数 × 拍子 × 升目。升目ごとに「鳴らす（1 回の揺れの始まり）」「続き（前の 1 回を伸ばす）」「休み」
    struct Pattern
    {
        static constexpr int kMaxCells = 4 * 7 * 8;   // 4 小節 × 7 拍 × 1/32（拍に 8 つ）
        int bars = 1, beatsPerBar = 4, grid = 1;      // grid = gridName の番号
        std::array<uint8_t, kMaxCells> cell {};       // 0 = 休み、1 = 始まり、2 = 続き
        int numCells() const { return std::min (kMaxCells, (int) std::lround (bars * beatsPerBar / gridBeats (grid))); }
        double lengthBeats() const { return bars * beatsPerBar; }
        void setDefault()
        {
            bars = 1; beatsPerBar = 4; grid = 1;
            cell.fill (0);
            for (int i = 0; i < numCells(); ++i) cell[(size_t) i] = 1;
        }
        Pattern() { setDefault(); }
    };

    // 再生用に「1 回」ごとの開始と長さにしたもの
    struct EventList
    {
        static constexpr int kMax = Pattern::kMaxCells;
        int n = 0;
        double length = 4;
        std::array<double, kMax> start {}, len {};   // double（3 連の升目 1/3・1/6 を float にすると境目に丸めの誤差が乗り、続いている回を「すき間」と取り違える）
        void build (const Pattern& p)
        {
            n = 0; length = p.lengthBeats();
            const double g = gridBeats (p.grid);
            const int cells = p.numCells();
            for (int i = 0; i < cells; ++i)
            {
                if (p.cell[(size_t) i] == 1)
                {
                    int j = i + 1;
                    while (j < cells && p.cell[(size_t) j] == 2) ++j;
                    start[(size_t) n] = i * g; len[(size_t) n] = (j - i) * g; ++n;
                    if (n >= kMax) break;
                }
            }
        }
    };

    //==============================================================================
    // 自分で描く形: 点（x 0..1、y 0..1）を SMOOTHING と曲線の種類でつなぐ
    constexpr int kNumSmoothModes = 5;
    inline const char* smoothModeName (int i) { static const char* n[] = { "Linear", "Sine", "Exp", "Sym", "Rev" }; return (i >= 0 && i < kNumSmoothModes) ? n[i] : "?"; }

    struct CustomShape
    {
        static constexpr int kMaxPoints = 32;
        int n = 3;
        std::array<float, kMaxPoints> x {}, y {};
        CustomShape() { setSine(); }
        void setSine() { n = 3; x[0] = 0; y[0] = 0; x[1] = 0.5f; y[1] = 1; x[2] = 1; y[2] = 0; }

        static float curve (float t, int mode)
        {
            switch (mode)
            {
                case 0:  return t;
                case 1:  return 0.5f - 0.5f * std::cos ((float) kPi * t);
                case 2:  { const float u = 1 - t; return 1 - u * u * u; }                       // 速く上がる
                case 3:  return t < 0.5f ? 4 * t * t * t : 1 - 4 * (1 - t) * (1 - t) * (1 - t);  // 左右対称の S 字
                default: return t * t * t;                                                    // ゆっくり上がって最後に速く
            }
        }
        // 256 点の表にする。smoothing 0 = 階段、1 = 区間全体を曲線で
        void render (float* tab, int len, float smoothing, int mode) const
        {
            const float sm = std::max (0.0f, std::min (1.0f, smoothing));
            for (int i = 0; i < len; ++i)
            {
                const float ph = (float) i / (float) len;
                int k = 0;
                while (k < n - 2 && ph >= x[(size_t) k + 1]) ++k;
                const float x0 = x[(size_t) k], x1 = std::max (x0 + 1e-6f, x[(size_t) k + 1]);
                const float u = std::max (0.0f, std::min (1.0f, (ph - x0) / (x1 - x0)));
                float v = y[(size_t) k];
                if (sm > 0.0f && u > 1.0f - sm)
                    v += (y[(size_t) k + 1] - y[(size_t) k]) * curve ((u - (1.0f - sm)) / sm, mode);
                tab[i] = std::max (0.0f, std::min (1.0f, v));
            }
        }
    };

    //==============================================================================
    // つまみの値（プロセッサーが毎ブロック入れる）
    struct Knobs
    {
        float inDb = 0, outDb = 0;
        float depth = 1;          // 0..1
        float groove = 0;         // −0.25..+0.25（− = SHUFFLE、+ = SWING）
        float feel = 0;           // −0.5..+0.5（周期の割合。+ = 遅れる）
        float accentDb = 0;       // −20..+20
        float width = 0;          // 0..1（右を半周期まで先に）
        int shape = 8;            // 形の番号（kNumShapes = 自分で描いた形）
        int rhythm = 9;           // 1/8
        float rateBpm = 120;      // 自前の速さ
        bool sync = true;         // Logic のテンポと位置に合わせる
        int style = 1;            // 0 = DIGITAL、1.. = 色付け
        float thresholdDb = -40, attackMs = 500, releaseMs = 500;
        bool gate = false;
        float rateModOct = 0;     // −6..+6
        float depthMod = 0;       // −1..+1
        float lowKeepHz = 0;      // 0 = OFF
        float duck = 1;           // Plugin Link（歌っている間の深さの倍率）
        float smoothing = 1;      // 自分の形の SMOOTH 0..1
        int smoothMode = 1;
    };

    struct Transport
    {
        bool hostPlaying = false;
        bool hasPosition = false;
        double ppq = 0;           // ブロック先頭の拍の位置
        double hostBpm = 120;
    };

    //==============================================================================
    // 4 次のリンクウィッツ・ライリー（LOW KEEP の分け目）
    struct LR4
    {
        Biquad lp1, lp2, hp1, hp2;
        void set (double fs, double hz)
        {
            lp1.setLowpass (fs, hz, 0.70710678); lp2.setLowpass (fs, hz, 0.70710678);
            hp1.setHighpass (fs, hz, 0.70710678); hp2.setHighpass (fs, hz, 0.70710678);
        }
        void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
        // 4 次（バターワース 2 段）は低・高の位相がそろっているので、そのまま足すと平ら（2 次のときのように反転しない）
        inline void split (float x, float& lo, float& hi) { lo = lp2.process (lp1.process (x)); hi = hp2.process (hp1.process (x)); }
    };

    //==============================================================================
    // 揺れの時計から利得を出す部分（エンジンと画面の表示で共用）
    struct Modulator
    {
        static inline double floorMod (double a, double m) { double r = std::fmod (a, m); return r < 0 ? r + m : r; }

        // GROOVE: 2 つ組の 1 つ目を (1+g) 倍、2 つ目を (1−g) 倍の長さに（時刻 → 揺れの時計）
        static inline double grooveWarp (double t, double unit, double g)
        {
            if (std::abs (g) < 1e-6) return t;
            const double pair = 2 * unit;
            const double p0 = std::floor (t / pair) * pair;
            const double u = t - p0;
            const double split = unit * (1 + g);
            return p0 + (u < split ? u / (1 + g) : unit + (u - split) / (1 - g));
        }

        static inline float shapeAt (const float* tab, double phase)
        {
            const double fi = phase * kShapeLen;
            const int i0 = (int) fi;
            const float fr = (float) (fi - i0);
            const float a0 = tab[i0 & (kShapeLen - 1)], a1 = tab[(i0 + 1) & (kShapeLen - 1)];
            return a0 + (a1 - a0) * fr;
        }
        // ACCENT: 2 拍ごとの頭の 1 回を強く（+）/ 弱く（−）。強いほうが 1 になるように正規化
        static inline float accentGain (double nominalStart, float accentDb)
        {
            if (std::abs (accentDb) < 0.01f) return 1.0f;
            const bool onDown = floorMod (nominalStart + 1e-6, 2.0) < 1e-4;
            return (accentDb > 0 ? ! onDown : onDown) ? std::pow (10.0f, -std::abs (accentDb) / 20.0f) : 1.0f;
        }

        // 拍 b での利得。
        // 測って確かめた決まり（build/tremlab、2026-11-10）:
        //  - GROOVE: 2 回ずつ組にして、1 回目の長さ (1+g)、2 回目 (1−g)
        //  - FEEL: 1 回ごとに「その回の長さ × 量」だけずらす（全体を同じ時間ずらすのではない。GROOVE と一緒に回すと差が出る）
        //  - WIDTH は呼ぶ側で右の時刻を一定量先にする（lead は今は 0 で使う）
        //  - ずらしてできたすき間・パターンの休みは無音（形 0）、重なった所は大きいほう
        //  - ただし、すき間・休みの入口と出口は rampBeats（呼ぶ側で 1.5 ms 分）の傾きでつなぐ（本物どおり一気に 0 にすると、
        //    終わりの値が 0 でない形＝Vintage・Round Square などでプツッと鳴るため。DSP 検査係 2026-11-10）
        static float gain (double b, int rhythm, const EventList& events, double unit, const float* tab,
                           float depth, float accentDb, double lead, double feel, double groove, double rampBeats = 0.0)
        {
            const double sh = feel - lead;   // + = 遅らせる（各回の長さに対する割合）
            float v = 0.0f;
            double prevEnd = -1.0e30, nextStart = 1.0e30;
            float prevVal = 0.0f, nextVal = 0.0f;
            float prevAcc = 1.0f, nextAcc = 1.0f;
            constexpr int kMaxIn = 8;
            double inS[kMaxIn], inE[kMaxIn];
            float inAcc[kMaxIn];
            int nIn = 0;
            auto visit = [&] (double pos, double S, double L, double nominal)
            {
                const float acc = accentGain (nominal, accentDb);
                if (pos >= S && pos < S + L)
                {
                    v = std::max (v, shapeAt (tab, (pos - S) / L) * acc);
                    if (nIn < kMaxIn) { inS[nIn] = S; inE[nIn] = S + L; inAcc[nIn] = acc; ++nIn; }
                }
                else if (S + L <= pos) { if (S + L > prevEnd) { prevEnd = S + L; prevVal = tab[0] * acc; prevAcc = acc; } }   // 終わりに近づくと表は頭の値へつながる
                else if (S < nextStart) { nextStart = S; nextVal = tab[0] * acc; nextAcc = acc; }
            };
            double pos = b;
            if (rhythm != kRhythmPattern)
            {
                const double U = unit, L0 = U * (1 + groove), L1 = U * (1 - groove);
                const long long k0 = (long long) std::floor (b / (2 * U)) * 2;
                // 組の頭（k0）からの手元の座標で比べる（曲の後ろのほうで拍の数が大きくなっても、境目の判定がずれない）
                const double base = (double) k0 * U;
                pos = b - base;
                for (long long k = k0 - 2; k <= k0 + 4; ++k)
                {
                    const long long p = (long long) std::floor ((double) k / 2.0);
                    const bool second = (k - 2 * p) != 0;
                    const double L = second ? L1 : L0;
                    visit (pos, (double) (2 * p - k0) * U + (second ? L0 : 0.0) + sh * L, L, (double) k * U);
                }
            }
            else if (events.n > 0)
            {
                // パターン: GROOVE は升目の 2 つ組で時間をゆがめ、その中で各回をその回の長さ × 量だけずらす
                const double t = grooveWarp (b, unit, groove);
                const double P = events.length;
                pos = floorMod (t, P);
                for (int e = 0; e < events.n; ++e)
                {
                    const double l = events.len[(size_t) e];
                    const double s0 = events.start[(size_t) e] + sh * l;
                    for (int w = -1; w <= 1; ++w)   // ずらしてパターンの端をまたいだ分
                        visit (pos, s0 + w * P, l, events.start[(size_t) e]);
                }
            }
            if (rampBeats > 0.0)
            {
                // 前の回が終わった所・次の回が始まる所に、ぴったり続く回（ACCENT の倍率も同じ）がない
                // （すき間・重なり・休み・ACCENT の境目）ときだけ傾きでつなぐ。pos は手元の座標なので eps は一定でよい
                const double eps = 1.0e-9;
                bool contPrev = false, contNext = false;
                for (int i = 0; i < nIn; ++i)
                {
                    if (std::abs (inS[i] - prevEnd) < eps && inAcc[i] == prevAcc) contPrev = true;
                    if (std::abs (inE[i] - nextStart) < eps && inAcc[i] == nextAcc) contNext = true;
                }
                const double r = 1.0 / rampBeats;
                if (! contPrev) v = std::max (v, prevVal * (float) std::max (0.0, 1.0 - (pos - prevEnd) * r));
                if (! contNext) v = std::max (v, nextVal * (float) std::max (0.0, 1.0 - (nextStart - pos) * r));
            }
            return 1.0f - depth * (1.0f - v);
        }

        static double unitFor (int rhythm, const Pattern& p) { return rhythm == kRhythmPattern ? gridBeats (p.grid) : rhythmBeats (rhythm); }
    };

    //==============================================================================
    class Engine
    {
    public:
        static constexpr int maxCh = 2;

        void prepare (double sampleRate, int numChannels)
        {
            fs = sampleRate;
            nCh = std::max (1, std::min (maxCh, numChannels));
            for (auto& o : os) { o.prepare(); o.reset(); }
            curStyle = -1; fadeLeft = 0;
            beat = 0; expectHostPpq = -1.0e9; catching = false;
            env = 0; peak = 0; envGate = 0; blkMax = 0; blkCount = 0;
            lastSpeed = 1.0; spdMoved = false; spdSteady = true; spdCount = 0;
            for (auto& g : lastGain) g = 1;
            declickArm = false; declickLeft = 0; dcOffset[0] = dcOffset[1] = 0; declickLen = std::max (1, (int) (0.01 * fs));
            depthS = 1; widthS = 0; feelS = 0; grooveS = 0; accS = 0; inS = -1; outS = -1; lkMix = 0;
            first = true;
            lkHz = 150; for (auto& l : lr) { l.set (fs * 4, lkHz); l.reset(); }
            for (auto& l : lrOld) { l.set (fs * 4, lkHz); l.reset(); }
            lastShape = -1; lastRhythm = -1; lastSmooth = -1; lastMode = -1;
            patternDirty.store (true); customDirty.store (true);
        }

        int latencySamples() const { return (int) Oversampler4::latency(); }

        // UI から（メッセージスレッド）: パターン・自分の形の点を入れ替える。オーディオ側は次のブロックで拾う
        void setPattern (const Pattern& p) { const juce_spin sl (lock); pendingPattern = p; patternDirty.store (true); }
        void setCustomShape (const CustomShape& c) { const juce_spin sl (lock); pendingCustom = c; customDirty.store (true); }

        // 画面の表示用
        std::atomic<float> shownEnv { 0 };      // 0..1（しきい値を超えた量）
        std::atomic<float> shownGainL { 1 }, shownGainR { 1 };
        std::atomic<float> shownPhase { 0 };    // パターン内の位置 0..1
        std::atomic<float> shownInDb { -100 };

        void process (float* const* ch, int numSamples, const Knobs& k, const Transport& tr)
        {
            pickUpEdits (k);
            if (curStyle < 0) setStyle (k.style, false);
            else if (k.style != curStyle && fadeLeft == 0) setStyle (k.style, true);
            if (k.shape != lastShape || k.rhythm != lastRhythm)
            {
                if (lastShape >= 0) armDeclick();
                lastShape = k.shape; lastRhythm = k.rhythm;
            }
            const bool lkOn = k.lowKeepHz > 1.0f;
            if (lkOn && std::abs (k.lowKeepHz - lkHz) > 0.5f)
            {
                lkHz = k.lowKeepHz;
                for (auto& l : lr) l.set (fs * 4, lkHz);
                for (auto& l : lrOld) l.set (fs * 4, lkHz);
            }

            const double bpm = (k.sync ? tr.hostBpm : (double) k.rateBpm);
            const double beatsPerSample = std::max (1.0, std::min (999.0, bpm)) / 60.0 / fs;
            const bool locked = k.sync && tr.hostPlaying && tr.hasPosition;

            const float inT = std::pow (10.0f, k.inDb / 20.0f);
            const float outT = std::pow (10.0f, k.outDb / 20.0f);
            if (inS < 0) { inS = inT; outS = outT; lkMix = lkOn ? 1.0f : 0.0f; }
            const float thrLin = std::pow (10.0f, k.thresholdDb / 20.0f);
            const float attC = (float) std::exp (-4.6 / (std::max (0.1f, k.attackMs) * 1e-3 * fs));
            const float relC = (float) std::exp (-4.6 / (std::max (0.1f, k.releaseMs) * 1e-3 * fs));
            const int blkLen = std::max (1, (int) std::lround (fs * (32.0 / 48000.0)));   // 入力の大きさは 0.67 ms ごとの最大値（本物と同じ。48 kHz で 32 サンプル）
            const float pkC = (float) std::exp (-(double) blkLen / (0.5e-3 * fs));       // それを 0.5 ms でならす（下がる時だけ）
            const float smC = (float) std::exp (-1.0 / (0.02 * fs));   // つまみの動き 20 ms
            const double unit = Modulator::unitFor (k.rhythm, pattern);
            const float* tab = k.shape >= 0 && k.shape < kNumShapes ? kShapes[k.shape] : customTab.data();
            const int fadeLen = (int) (0.015 * fs);

            // Logic の位置に合わせる（測った本物の動き、build/tremlab 2026-11-10・fix2）:
            //  - ふだんは Logic の拍にぴったり
            //  - RATE MOD で速さが変わっている間・終わった後は、「Logic の位置 × 今の速さ」（小節の長さの倍数のずれは同じとみなす）へ
            //    時定数 約 2.5 秒で寄せる。効き始めは数秒だけ速く・遅く回り、終わると小節の頭にそろう。すぐには戻らない
            //  - 再生位置が飛んだ（ループ・頭出し）ら、その場で合わせる（つなぎ 10 ms）。ただし寄せている最中は飛ばさずに寄せ続ける（本物も同じ）
            const double hostBps = std::max (1.0, std::min (999.0, tr.hostBpm)) / 60.0 / fs;
            if (locked)
            {
                const bool jumped = std::abs (tr.ppq - expectHostPpq) > 0.01;
                const bool pulling = catching || std::abs (k.rateModOct * lastAmt) > 1e-3f;
                if (first || (jumped && ! pulling))
                {
                    if (! first && std::abs (tr.ppq - beat) > 0.01) armDeclick();
                    beat = tr.ppq; catching = false;
                }
                else if (! catching && std::abs (k.rateModOct * lastAmt) <= 1e-3f)
                {
                    // ブロックの途中でテンポが変わったなど、少しだけずれた: 1 回の長さの 0.2 % を超えたらつないでから合わせる
                    if (std::abs (tr.ppq - beat) > 0.002 * unit) armDeclick();
                    beat = tr.ppq;
                }
                expectHostPpq = tr.ppq + numSamples * hostBps;
            }
            else
            {
                catching = false;
                expectHostPpq = -1.0e9;
            }
            first = false;
            const double wrapLen = lockWrap (k.rhythm, unit);
            const double pullC = 1.0 / (2.5 * fs);

            float inPk = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                inS = smC * inS + (1 - smC) * inT;
                outS = smC * outS + (1 - smC) * outT;
                lkMix = smC * lkMix + (1 - smC) * (lkOn ? 1.0f : 0.0f);

                // 入力の大きさ（INPUT のつまみの前。両チャンネルの大きいほう）→ 0.67 ms ごとの最大値を 0.5 ms でならす → ATTACK / RELEASE
                // INPUT の前で測ることは、INPUT ±12 dB でしきい値の効き方が変わらないことで確かめた
                float a = 0;
                for (int c = 0; c < nCh; ++c) a = std::max (a, std::abs (ch[c][i]));
                inPk = std::max (inPk, a);
                blkMax = std::max (blkMax, a);
                if (++blkCount >= blkLen) { peak = blkMax > peak ? blkMax : pkC * peak + (1 - pkC) * blkMax; blkMax = 0; blkCount = 0; }
                float amtD, amt;   // DEPTH MOD に使う量・RATE MOD に使う量
                if (! k.gate)
                {
                    env = peak > env ? attC * env + (1 - attC) * peak : relC * env + (1 - relC) * peak;
                    // しきい値を超えた分 dd → DEPTH MOD は直線（dd / 0.25）、RATE MOD は少し丸まる（dd / (0.178 + 0.294 dd)。小さい音で 1.4 倍よく効き、満タンでそろう）
                    // しきい値 −60・−40・−20 dB、0.002〜0.9 の直流と 1 kHz で本物と比べた（build/tremlab/rm）
                    const float dd = std::max (0.0f, env - thrLin);
                    amtD = std::min (1.0f, dd / 0.25f);
                    amt = std::min (1.0f, dd / (0.1778f + 0.2936f * dd));
                }
                else
                {
                    const float tgt = peak > thrLin ? 1.0f : 0.0f;
                    envGate = tgt > envGate ? attC * envGate + (1 - attC) * tgt : relC * envGate + (1 - relC) * tgt;
                    env = envGate;
                    amtD = amt = envGate;
                }
                lastAmt = amt;

                depthS = smC * depthS + (1 - smC) * k.depth;
                widthS = smC * widthS + (1 - smC) * k.width;
                feelS = smC * feelS + (1 - smC) * k.feel;
                grooveS = smC * grooveS + (1 - smC) * k.groove;
                accS = smC * accS + (1 - smC) * k.accentDb;

                // 深さ: DEPTH MOD が + なら「無音のとき浅く、鳴ると DEPTH まで」、− なら「鳴ると浅く」
                float d = depthS;
                if (k.depthMod > 0) d *= 1.0f - k.depthMod * (1.0f - amtD);
                else if (k.depthMod < 0) d *= 1.0f + k.depthMod * amtD;
                d *= k.duck;

                const bool modNow = std::abs (k.rateModOct * amt) > 1e-3f;
                const double speed = modNow ? std::exp2 ((double) k.rateModOct * amt) : 1.0;   // 効いていない間は 1 ちょうど（寄せる先が Logic の位置そのものになるように）
                // 速さが少しでも動いた 0.67 ms の区切りの間は寄せない（本物: ゆっくり上げ下げしている間・低い音で量が細かく揺れる間は自走、
                // 落ち着いたらすぐ寄せ始める。30 秒かけた上げ下げ・300 Hz / 100 Hz の正弦波で確認）
                if (std::abs (speed - lastSpeed) > 1e-9 * speed) spdMoved = true;
                lastSpeed = speed;
                if (++spdCount >= blkLen) { spdSteady = ! spdMoved; spdMoved = false; spdCount = 0; }
                const double b = beat;
                beat += beatsPerSample * speed;
                if (locked)
                {
                    const bool modding = modNow;
                    if (modding || catching)
                    {
                        // RATE MOD が効いている間も、終わった後も、「Logic の位置 × 今の速さ」（小節の長さの倍数のずれは同じとみなす）へ
                        // 2.5 秒の時定数で寄せる（本物と同じ。効き始めは数秒だけ速く・遅く回り、終わると小節の頭にそろう）
                        const double hostNow = tr.ppq + (i + 1) * hostBps;
                        const double tgt = hostNow * speed;
                        const double err = tgt - beat;
                        double errW = err + std::floor (0.5 - err / wrapLen) * wrapLen;   // ちょうど半分のずれは前へ（本物と同じ向き）
                        if (! modding && std::abs (errW) < 1e-5) { beat += errW; catching = false; }
                        else
                        {
                            // 後ろへ寄せる分が速さを上回ると、しばらく逆向きに回る（本物も同じ。−2 oct・−6 oct、ずれ −1.0〜−1.9 拍で本物と 1 % 前後で一致）
                            if (spdSteady) beat += errW * pullC;
                            catching = true;
                        }
                    }
                }

                const float acc = std::abs (accS) < 0.01f ? 0.0f : accS;
                const double ramp = 0.0015 * fs * std::max (beatsPerSample * speed, std::abs (beat - b));   // すき間の入口・出口の傾き 1.5 ms（拍で。寄せている分も含めた進み方で）
                float g2[2];
                g2[0] = Modulator::gain (b, k.rhythm, events, unit, tab, d, acc, 0.0, feelS, grooveS, ramp);
                // WIDTH: 右は左の揺れ全体を「WIDTH/2 × 1 回の長さ」だけ先に（一定の時間。GROOVE と一緒に回して確かめた）
                g2[1] = nCh > 1 ? Modulator::gain (b + (double) widthS * 0.5 * unit, k.rhythm, events, unit, tab, d, acc, 0.0, feelS, grooveS, ramp) : g2[0];
                // つなぎ: 飛んだ瞬間の差を覚えて、10 ms でまっすぐ 0 に（終わりに段差が残らない）
                if (declickArm)
                {
                    declickArm = false;
                    for (int cc = 0; cc < 2; ++cc) dcOffset[cc] = lastGain[cc] - g2[cc];
                    declickLeft = declickLen;
                }
                if (declickLeft > 0)
                {
                    const float w = (float) declickLeft / (float) declickLen;
                    for (int cc = 0; cc < 2; ++cc) g2[cc] = std::max (0.0f, std::min (1.0f, g2[cc] + dcOffset[cc] * w));   // つなぎの間に角が来ても 0〜1 の外へ出さない
                    --declickLeft;
                }

                for (int c = 0; c < nCh; ++c)
                {
                    float o4[4];
                    os[c].up (ch[c][i] * inS, o4);
                    const float g0 = lastGain[c], g1 = g2[c];
                    for (int q = 0; q < 4; ++q)
                    {
                        const float g = g0 + (g1 - g0) * (float) (q + 1) * 0.25f;
                        float y = path (style[c], lr[c], o4[q], g, lkMix);
                        if (fadeLeft > 0)
                        {
                            // STYLE を変えた直後: 前の色付けと 15 ms 重ねる
                            const float yo = path (styleOld[c], lrOld[c], o4[q], g, lkMix);
                            const float w = (float) fadeLeft / (float) fadeLen;
                            y = y * (1.0f - w) + yo * w;
                        }
                        o4[q] = y;
                    }
                    ch[c][i] = std::max (-4.0f, std::min (4.0f, os[c].down (o4)));   // 念のための天井（+12 dBFS）
                }
                if (fadeLeft > 0) --fadeLeft;
                lastGain[0] = g2[0]; lastGain[1] = g2[1];
            }
            // ずっと自走していると拍の数が大きくなりすぎるので、リズム・ACCENT・GROOVE の区切りがそろう長さの倍数で折り返す
            if (! locked && beat > 1.0e6) { beat -= std::floor (beat / wrapLen) * wrapLen; armDeclick(); }

            shownEnv.store (lastAmt);
            shownGainL.store (lastGain[0]); shownGainR.store (lastGain[1]);
            shownInDb.store (20.0f * std::log10 (inPk + 1e-9f));
            {
                const double len = k.rhythm == kRhythmPattern ? events.length : std::max (4.0, rhythmBeats (k.rhythm));
                shownPhase.store ((float) (Modulator::floorMod (beat, len) / len));
            }
        }

    private:
        // spin lock（JUCE なしで使えるように std::atomic_flag で）
        struct SpinLock { std::atomic_flag f = ATOMIC_FLAG_INIT; void lock() { while (f.test_and_set (std::memory_order_acquire)) {} } bool tryLock() { return ! f.test_and_set (std::memory_order_acquire); } void unlock() { f.clear (std::memory_order_release); } };
        struct juce_spin { SpinLock& l; explicit juce_spin (SpinLock& s) : l (s) { l.lock(); } ~juce_spin() { l.unlock(); } };

        double fs = 48000;
        int nCh = 2;
        Oversampler4 os[maxCh];
        StyleChannel style[maxCh], styleOld[maxCh];
        LR4 lr[maxCh], lrOld[maxCh];
        int curStyle = -1, fadeLeft = 0;
        float lkHz = 150;

        double beat = 0, expectHostPpq = -1.0e9;
        bool catching = false;
        float env = 0, peak = 0, envGate = 0, lastAmt = 0, blkMax = 0;
        int blkCount = 0, spdCount = 0;
        double lastSpeed = 1.0;
        bool spdMoved = false, spdSteady = true;
        float lastGain[2] { 1, 1 };
        bool declickArm = false;
        int declickLeft = 0, declickLen = 480;
        float dcOffset[2] { 0, 0 };
        void armDeclick() { declickArm = true; }   // 次のサンプルで「今の利得との差」を覚え、10 ms でまっすぐ 0 に
        float depthS = 1, widthS = 0, feelS = 0, grooveS = 0, accS = 0, inS = -1, outS = -1, lkMix = 0;
        bool first = true;
        int lastShape = -1, lastRhythm = -1, lastMode = -1;
        float lastSmooth = -1;

        SpinLock lock;
        Pattern pattern, pendingPattern;
        EventList events;
        std::atomic<bool> patternDirty { true };
        CustomShape custom, pendingCustom;
        std::atomic<bool> customDirty { true };
        std::array<float, kShapeLen> customTab {};

        // 追いつくときの「そろえる単位」: 4 拍（付点のときは 12 拍）。パターンはその長さ。
        // ACCENT（2 拍）と GROOVE（2 回の組）の区切りもそろう長さにする
        double lockWrap (int rhythm, double unit) const
        {
            const double base = rhythm == kRhythmPattern ? std::max (1.0, events.length) : std::max (4.0, rhythmBeats (rhythm));
            for (int m = 1; m <= 64; ++m)
            {
                const double L = base * m;
                auto fits = [L] (double d) { const double r = std::fmod (L, d); return r < 1e-6 || d - r < 1e-6; };
                if (fits (2.0) && fits (2.0 * unit)) return L;
            }
            return base;
        }

        inline float path (StyleChannel& st, LR4& l, float x, float g, float mix) const
        {
            float v = st.front (x);
            float lo, hi; l.split (v, lo, hi);   // OFF の間も回して状態を新しく保つ
            v = (1.0f - mix) * (v * g) + mix * (lo + g * hi);
            return st.back (v * outS);
        }

        void pickUpEdits (const Knobs& k)
        {
            bool shapeChanged = false;
            if (patternDirty.load() || customDirty.load())
            {
                if (lock.tryLock())   // UI が書いている途中ならこのブロックは見送る
                {
                    if (patternDirty.exchange (false)) { pattern = pendingPattern; events.build (pattern); if (! first) armDeclick(); }
                    if (customDirty.exchange (false)) { custom = pendingCustom; shapeChanged = true; }
                    lock.unlock();
                }
            }
            if (shapeChanged || k.smoothing != lastSmooth || k.smoothMode != lastMode)
            {
                lastSmooth = k.smoothing; lastMode = k.smoothMode;
                custom.render (customTab.data(), kShapeLen, k.smoothing, k.smoothMode);
                if (! first && k.shape == kNumShapes) armDeclick();
            }
        }

        void setStyle (int s, bool crossfade)
        {
            if (crossfade)
            {
                for (int c = 0; c < maxCh; ++c) { styleOld[c] = style[c]; lrOld[c] = lr[c]; }
                fadeLeft = (int) (0.015 * fs);
            }
            curStyle = s;
            const StyleParams sp = styleParams (s);
            for (int c = 0; c < maxCh; ++c)
            {
                style[c].prepare (fs * 4, sp);
                if (crossfade)
                {
                    // 出口側の低域カット・棚の状態は前の色付けから引き継ぐ（0 から始めると低い音が数百 ms ふくらむ）
                    style[c].hp.z1 = styleOld[c].hp.z1; style[c].hp.z2 = styleOld[c].hp.z2;
                    style[c].lowShelf.z = styleOld[c].lowShelf.z;
                    style[c].deemph.z = styleOld[c].deemph.z;
                }
            }
        }
    };

    // 画面に描く用: ある拍の位置での利得（プロセッサーが持っているパターン・形の写しから計算）
    inline float previewGain (double b, const Knobs& k, const EventList& ev, const Pattern& p, const float* customTab)
    {
        const float* tab = k.shape >= 0 && k.shape < kNumShapes ? kShapes[k.shape] : customTab;
        return Modulator::gain (b, k.rhythm, ev, Modulator::unitFor (k.rhythm, p), tab, k.depth, k.accentDb, 0.0, k.feel, k.groove);
    }
}
