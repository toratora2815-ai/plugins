// Deca Satu の音の処理（JUCE なし）。
// プラグイン本体と、当てはめ用の道具（build/decalab、コミットしない）で同じものを使う。
//
// 流れ（1 チャンネル）:
//   入口 → LOW CUT（2 次の低域カット、THUMP で山）→ TONE（歪みの前の分）→ DRIVE（＋PUNISH）
//   → STYLE ごとの処理 [ 前の EQ → 4 倍オーバーサンプリング → 真空管ふうの歪み（グリッド電流・中心のずれ）
//                        → 後ろの低域通過 → 元のサンプリングに戻す → 直流カット ]
//   → TONE（歪みの後の分）→ HIGH CUT（ゆるやか／急）→ OUTPUT → MIX
// STYLE を切り替えると 25 ms で前後のスタイルを混ぜて切り替える（プチッとしない）。
#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include "DecaSatuTables.h"

namespace decasatu
{
    constexpr double kPi = 3.14159265358979323846;

    //==========================================================================
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
        void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }
        void setNorm (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }
        // RBJ の式（当てはめの道具 model.py と同じ）
        enum Type { peak, lowShelf, highShelf, lowPass, highPass };
        void design (Type t, double f, double gainDb, double Q, double fs) noexcept
        {
            f = std::clamp (f, 1.0, fs * 0.49);
            const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs;
            const double c = std::cos (w), s = std::sin (w), al = s / (2 * Q);
            switch (t)
            {
                case peak:      setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); break;
                case lowShelf:
                {
                    const double sa = 2 * std::sqrt (A) * al;
                    setNorm (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
                    break;
                }
                case highShelf:
                {
                    const double sa = 2 * std::sqrt (A) * al;
                    setNorm (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
                    break;
                }
                case lowPass:   setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); break;
                case highPass:  setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); break;
            }
        }
        // 1 次の低域通過（当てはめの道具と同じ形: (1-k)/(1-k z^-1)）
        void designLp1 (double f, double fs) noexcept
        {
            const double k = std::exp (-2 * kPi * std::min (f, fs * 0.49) / fs);
            b0 = 1 - k; b1 = b2 = 0; a1 = -k; a2 = 0;
        }
        // 1 次の高域通過
        void designHp1 (double f, double fs) noexcept
        {
            const double k = std::exp (-2 * kPi * f / fs);
            b0 = (1 + k) / 2; b1 = -(1 + k) / 2; b2 = 0; a1 = -k; a2 = 0;
        }
    };

    //==========================================================================
    // 2 倍のオーバーサンプリング（直線位相の FIR ハーフバンド、カイザー窓）。
    // 本物も直線位相（遅れが周波数で変わらない）なので、こちらも同じ形にして波形まで合わせる
    template <int N>
    struct FirHalfband
    {
        static_assert (N % 4 == 3, "halfband length must be 4k+3");
        double h[N] {};
        double buf[2 * N] {};
        int pos = 0;

        void design (double beta)
        {
            const int c = (N - 1) / 2;
            auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
            double sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const double n = i - c;
                const double sinc = n == 0 ? 0.5 : std::sin (kPi * n / 2) / (kPi * n);
                const double r = n / c;
                h[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
                sum += h[i];
            }
            for (auto& v : h) v /= sum;   // 直流の倍率 1（間引き用）
            reset();
        }
        void reset() { std::memset (buf, 0, sizeof (buf)); pos = 0; }
        inline void push (double x) noexcept
        {
            pos = pos == 0 ? N - 1 : pos - 1;
            buf[pos] = buf[pos + N] = x;
        }
        // 1 サンプル → 2 サンプル（入力の履歴は低い側のサンプリング）
        // ハーフバンドは「真ん中以外の奇数番目」が 0 なので、偶数番目と真ん中だけ計算する
        static constexpr int C = (N - 1) / 2;   // 真ん中（奇数）
        inline void up (double x, double& o0, double& o1) noexcept
        {
            push (x);
            const double* q = buf + pos;   // q[0] が最新
            double s0 = 0;
            for (int k = 0; 2 * k < N; ++k) s0 += h[2 * k] * q[k];
            o0 = 2 * s0;
            o1 = 2 * h[C] * q[(C - 1) / 2];
        }
        // 2 サンプル → 1 サンプル（in0 が先）
        inline double down (double in0, double in1) noexcept
        {
            push (in0); push (in1);
            const double* q = buf + pos;
            double s = h[C] * q[C];
            for (int k = 0; k < N; k += 2) s += h[k] * q[k];
            return s;
        }
    };
    constexpr int kOs1 = 95, kOs2 = 23;   // 1 段目（48→96k）・2 段目（96→192k）のタップ数
    constexpr double kOsBeta1 = 8.96, kOsBeta2 = 8.96;   // 約 −90 dB

    //==========================================================================
    // STYLE ひとつ分の処理（歪みの前の EQ から直流カットまで）
    struct StyleProc
    {
        StyleModel p;
        double fsBase = 48000, fsOs = 192000;
        int os = 4;
        Biquad pre[4], post;
        FirHalfband<kOs1> up1, dn1;
        FirHalfband<kOs2> up2, dn2;
        double cc = 0, sagM = 0, sagE = 0, dc = 0, dc2 = 0;
        double kcc = 0, kgc = 0, ksag = 0, kdc = 0, kdc2 = 0;
        double nl0 = 0;

        static inline double sat (double z, double k) noexcept
        {
            return z / std::pow (1.0 + std::pow (std::fabs (z), k), 1.0 / k);
        }
        // 正確な形（表を作るときと、当てはめの確認用）
        inline double nlExact (double u) const noexcept
        {
            const double s = u >= 0 ? p.lp * sat (u / p.lp, p.kp) : p.ln * sat (u / p.ln, p.kn);
            return s + std::max (0.0f, p.slope) * std::clamp (u, -30.0, 30.0);
        }
        // 速い形: 片側ずつ z = |u|/L を 0..kZMax で表にして直線補間。その先は sat ≈ 1 − z^-k / k
        static constexpr int kTab = 8192;
        static constexpr double kZMax = 32.0;
        // 表は Engine が STYLE ごとに prepare で作って持つ（音のスレッドで作らない）。ここは指すだけ
        const float* tabP = nullptr;
        const float* tabN = nullptr;
        static void buildTable (float* t, double k)
        {
            for (int i = 0; i <= kTab + 1; ++i) t[i] = (float) sat (kZMax * i / kTab, k);
        }
        static inline double look (const float* t, double z, double k) noexcept
        {
            if (! (z < kZMax)) return z == z ? 1.0 - std::pow (z, -k) / k : 0.0;   // NaN は 0 に（表の外を読まない）
            const double x = z * (kTab / kZMax);
            const int i = (int) x;
            const double f = x - i;
            return t[i] + (t[i + 1] - t[i]) * f;
        }
        inline double nl (double u) const noexcept
        {
            const double s = u >= 0 ? p.lp * look (tabP, u / p.lp, p.kp) : -p.ln * look (tabN, -u / p.ln, p.kn);
            return s + slopePos * std::clamp (u, -30.0, 30.0);   // 傾きの分にも上限（とても大きい音で素通りの音が足されないように）
        }
        double slopePos = 0;   // 傾きは 0 以上だけ（マイナスだと強く入れたとき形が折り返して大きくなる）

        // 2 段目の歪み（1 段目の出口 × 段の間の倍率 → 結合コンデンサ → プラス・マイナス別の飽和）
        const float* tabP2 = nullptr;
        const float* tabN2 = nullptr;
        double cc2 = 0, kcc2 = 0, nl20 = 0;
        double peakCc = 0, peakSag = 0, peakCc2 = 0;   // 当てはめの道具が上限を決めるために見る（音には使わない）
        inline double nl2 (double u) const noexcept
        {
            return u >= 0 ? p.lp2 * look (tabP2, u / p.lp2, p.kp2) : -p.ln2 * look (tabN2, -u / p.ln2, p.kn2);
        }

        // tb: この STYLE の表 4 本（1 段目のプラス・マイナス、2 段目のプラス・マイナス）
        void prepare (double sampleRate, const StyleModel& model, const float (*tb)[kTab + 2])
        {
            fsBase = sampleRate;
            os = sampleRate < 60000 ? 4 : (sampleRate < 120000 ? 2 : 1);
            fsOs = sampleRate * os;
            for (auto* f : { &up1, &dn1 }) f->design (kOsBeta1);
            for (auto* f : { &up2, &dn2 }) f->design (kOsBeta2);
            setModel (model, tb);
            reset();
        }
        void setModel (const StyleModel& model, const float (*tb)[kTab + 2])
        {
            p = model;
            tabP = tb[0]; tabN = tb[1]; tabP2 = tb[2]; tabN2 = tb[3];
            kcc2 = 1 - std::exp (-2 * kPi * std::max (0.01f, p.cc2Hz) / fsOs);
            slopePos = std::max (0.0f, p.slope);
            pre[0].designHp1 (p.hpf, fsBase);
            pre[1].design (Biquad::lowShelf, p.lsf, p.lsg, 0.707, fsBase);
            pre[2].design (Biquad::peak, p.pkf, p.pkg, p.pkq, fsBase);
            pre[3].design (Biquad::highShelf, p.hsf, p.hsg, 0.707, fsBase);
            post.design (Biquad::lowPass, std::min ((double) p.postLp, fsOs * 0.45), 0, p.postQ, fsOs);
            kcc = 1 - std::exp (-2 * kPi * p.ccHz / fsOs);
            kgc = 1 - std::exp (-p.gcRate / fsOs);
            ksag = 1 - std::exp (-1000.0 / (std::max (0.1f, p.sagMs) * fsOs));
            kdc = 1 - std::exp (-2 * kPi * p.dcHz / fsBase);
            kdc2 = 1 - std::exp (-2 * kPi * p.dc2Hz / fsBase);
            nl0 = nl (p.bias);
            nl20 = nl2 (p.bias2);
        }
        void reset()
        {
            for (auto& b : pre) b.reset();
            post.reset(); up1.reset(); dn1.reset(); up2.reset(); dn2.reset();
            cc = sagM = sagE = dc = dc2 = cc2 = 0;
            peakCc = peakSag = peakCc2 = 0;
        }

        // 高いサンプリングでの 1 サンプル（u はドライブ後の信号）
        double stage2Gain = 1;   // 段の間の倍率（DRIVE で変わる。process で受け取る）

        // 無音を流して、中心のずれ・直流カットを落ち着いた状態にしておく（読み込んだ直後のうねりを出さない。prepare だけで使う）
        void settle (double seconds, double drive)
        {
            const int n = (int) (seconds * fsBase);
            const double ig = std::pow (10.0, (p.g2A + p.g2B * drive / 10.0) / 20.0);   // 今の DRIVE での段の間の倍率で落ち着かせる
            for (int i = 0; i < n; ++i) process (0.0, 1.0, ig);
        }
        inline double core (double u) noexcept
        {
            const double g = u - cc;
            const double over = g + p.bias - p.gth;
            if (over > 0) cc += kgc * over;
            cc += kcc * (u - cc);
            // 当てはめた範囲で振れた量の 1.5 倍まで（PUNISH の極端な所で、止めた後に大きく揺れ戻らないように）
            cc = std::clamp (cc, -(double) p.ccMax, (double) p.ccMax);
            if (cc > peakCc) peakCc = cc; else if (-cc > peakCc) peakCc = -cc;
            const double gin = g + p.bias - p.sagAmt * sagM - p.sagEnv * sagE;
            const double v = nl (gin) - nl0;
            sagM = std::clamp (sagM + ksag * (v - sagM), -(double) p.sagMax, (double) p.sagMax);
            sagE = std::min (sagE + ksag * (std::fabs (v) - sagE), (double) p.sagMax);
            peakSag = std::max (peakSag, std::max (std::fabs (sagM), sagE));
            const double w = v * stage2Gain;
            const double g2 = w - cc2;
            cc2 = std::clamp (cc2 + kcc2 * g2, -(double) p.cc2Max, (double) p.cc2Max);
            if (cc2 > peakCc2) peakCc2 = cc2; else if (-cc2 > peakCc2) peakCc2 = -cc2;
            return post.process (nl2 (g2 + p.bias2) - nl20);
        }

        // x: 入口（LOW CUT・TONE 済み）、driveGain: 倍率
        inline double process (double x, double driveGain, double interGain) noexcept
        {
            stage2Gain = interGain;
            double u = x;
            for (auto& b : pre) u = b.process (u);
            u *= driveGain;
            double y;
            if (os == 4)
            {
                double a0, a1, b0, b1, b2, b3;
                up1.up (u, a0, a1);
                up2.up (a0, b0, b1); up2.up (a1, b2, b3);
                b0 = core (b0); b1 = core (b1); b2 = core (b2); b3 = core (b3);
                y = dn1.down (dn2.down (b0, b1), dn2.down (b2, b3));
            }
            else if (os == 2)
            {
                double a0, a1;
                up1.up (u, a0, a1);
                y = dn1.down (core (a0), core (a1));
            }
            else y = core (u);
            // 直流カット（1 次の高域通過を 2 段。大きい音が止まったあとの「行き過ぎて戻る」揺れ）
            dc += kdc * (y - dc);
            y -= dc;
            dc2 += kdc2 * (y - dc2);
            return (y - dc2) * p.outGain;
        }

        // MIX の原音を同じ位相にそろえるため、オーバーサンプリングの上げ下げだけを通す
        FirHalfband<kOs1> pu1, pd1; FirHalfband<kOs2> pu2, pd2;
        void prepareDry() { for (auto* f : { &pu1, &pd1 }) f->design (kOsBeta1); for (auto* f : { &pu2, &pd2 }) f->design (kOsBeta2); }
        inline double dryAlign (double x) noexcept
        {
            if (os == 4)
            {
                double a0, a1, b0, b1, b2, b3;
                pu1.up (x, a0, a1); pu2.up (a0, b0, b1); pu2.up (a1, b2, b3);
                return pd1.down (pd2.down (b0, b1), pd2.down (b2, b3));
            }
            if (os == 2) { double a0, a1; pu1.up (x, a0, a1); return pd1.down (a0, a1); }
            return x;
        }
    };

    //==========================================================================
    // つまみ（値はどれも本物の画面と同じ意味）
    struct Knobs
    {
        int style = 0;            // 0..4
        float drive = 0;          // 0..10
        bool punish = false;
        float lowCutHz = 20;      // 20..1000
        bool thump = false;
        float tone = 0;           // -1..+1（-12..+12 dB の表示）
        float highCutHz = 20000;  // 1000..20000
        bool steep = false;
        float mix = 1;            // 0..1
        float outputDb = 0;       // -24..+12
        float extraGainDb = 0;    // GAIN MATCH・AUTO・Plugin Link など外からの出口の補正
    };

    // 1 チャンネル分
    struct Channel
    {
        StyleProc cur, prev, dryP;   // dryP は MIX の原音の位相合わせだけに使う
        Biquad lowCut;
        Biquad tonePre[2], tonePost[2];
        // HIGH CUT は 11 段の表をすべて動かしておき、隣り合う 2 つを混ぜる（ゆるやか・急 両方とも動かし、STEEP は 15 ms で混ぜる）
        Biquad hcG[kHcSteps][3], hcS[kHcSteps][3];
        double hcGain[2][kHcSteps] {};
        void reset()
        {
            cur.reset(); prev.reset(); dryP.prepareDry(); lowCut.reset();
            for (auto& b : tonePre) b.reset();
            for (auto& b : tonePost) b.reset();
            for (auto& r : hcG) for (auto& b : r) b.reset();
            for (auto& r : hcS) for (auto& b : r) b.reset();
        }
    };

    class Engine
    {
    public:
        static constexpr int maxCh = 2;
        static constexpr int kChunk = 64;   // ふだんの区切り。TONE・LOW CUT・THUMP がなめらかに動いている間だけ 1 サンプルごとに係数を作り直す

        Engine() { for (int s = 0; s < 5; ++s) { models[s] = kStyles[s]; buildStyleTables (s); } }

        // initialStyle: 今の STYLE。準備の中で無音を流して落ち着かせておく（音のスレッドでは重い処理をしない）
        void prepare (double sampleRate, int numChannels, int initialStyle = 0, float initialDrive = 0)
        {
            fs = sampleRate; nch = std::clamp (numChannels, 1, maxCh);
            for (auto& c : ch)
            {
                c.cur.prepare (fs, models[0], tabs[0]);
                c.prev.prepare (fs, models[0], tabs[0]);
                c.dryP.prepare (fs, models[0], tabs[0]); c.dryP.prepareDry();
                designHighCut (c);
            }

            curStyle = -1; prevStyle = -1; xfade = 0;
            smDrive = smOut = -1e9; smMix = -1; smSteep = -1;
            lastLow = lastTone = -1e9; lastQ = -1;
            smLowLog = smTone = smHc = smQ = smFac = -1e9;
            reset();
            const int st0 = std::clamp (initialStyle, 0, 4);
            for (int c = 0; c < nch; ++c) { ch[c].cur.setModel (models[st0], tabs[st0]); ch[c].cur.reset(); ch[c].cur.settle (1.0, initialDrive); }
            curStyle = st0;
        }
        void reset() { for (auto& c : ch) c.reset(); xfade = 0; }

        // 当てはめ用: STYLE のモデルを差し替える（音のスレッドからは呼ばない）
        void setStyleModel (int s, const StyleModel& m)
        {
            models[s] = m; buildStyleTables (s);
            if (s == curStyle) for (auto& c : ch) c.cur.setModel (m, tabs[s]);
        }
        const StyleModel& styleModel (int s) const { return models[s]; }
        void peaks (double* out) const { out[0] = ch[0].cur.peakCc; out[1] = ch[0].cur.peakSag; out[2] = ch[0].cur.peakCc2; }   // 当てはめの道具用

        // オーバーサンプリングの遅れ（サンプル、整数に丸めた値）。原音も同じだけ遅らせてあるので MIX はずれない
        int latencySamples() const
        {
            StyleProc t; t.prepare (fs, models[0], tabs[0]); t.prepareDry();
            double num = 0, den = 0;   // 低い音の山の重心
            for (int i = 0; i < 160; ++i)
            {
                const double y = t.dryAlign (i == 0 ? 1.0 : 0.0);
                num += i * y; den += y;
            }
            return den != 0 ? (int) std::lround (num / den) : 0;
        }

        // 0..10 の DRIVE（と PUNISH）の倍率（dB）
        double driveDb (const Knobs& k, int s) const
        {
            return models[s].gainA + models[s].gainB * (k.drive / 10.0) + (k.punish ? kPunishA[s] + kPunishB[s] * k.drive : 0.0);
        }
        // 本物の AUTO と同じ量だけ出口を下げる（GAIN MATCH が ON の間に使う）
        static double autoDb (const Knobs& k)
        {
            const int s = std::clamp (k.style, 0, 4);
            return k.punish ? -(kAutoPOff[s] + kAutoPSlope[s] * k.drive) : -kAutoN[s] * k.drive;
        }

        void process (float* const* io, int n, const Knobs& k)
        {
            float* p[maxCh];
            const double tTone = std::clamp ((double) k.tone, -1.0, 1.0);
            const double tLow = std::log (std::clamp ((double) k.lowCutHz, 20.0, 1000.0));
            for (int off = 0; off < n;)
            {
                const double v = (tLow - std::log (20.0)) / std::log (50.0);
                const double tQ = k.thump ? kThumpQ (v) : kLowCutQ (v);
                const double tFac = k.thump ? 1.0 : 0.1 + 0.9 * std::min (1.0, (std::exp (tLow) - 20.0) / 10.0);
                const bool moving = smTone != tTone || smLowLog != tLow || smQ != tQ || smFac != tFac;
                const int m = std::min (moving ? 1 : kChunk, n - off);
                for (int c = 0; c < nch; ++c) p[c] = io[c] + off;
                processChunk (p, m, k);
                off += m;
            }
        }

    private:
        double fs = 48000; int nch = 2;
        StyleModel models[5];
        float tabs[5][4][StyleProc::kTab + 2];
        Channel ch[maxCh];
        int curStyle = -1, prevStyle = -1, xfade = 0, xfadeLen = 1;
        double smDrive = 0, smOut = 0, smMix = 1, smSteep = 0, smKnob = -1;
        double prevDg = 1, prevIg = 1;
        double lastLow = -1, lastTone = -1, lastQ = -1, lastFac = -1, smFac = -1e9;
        double smLowLog = -1e9, smTone = -1e9, smHc = -1e9, smQ = -1e9;   // つまみのなめらか化（30 ms）

        void buildStyleTables (int s)
        {
            StyleProc::buildTable (tabs[s][0], models[s].kp);
            StyleProc::buildTable (tabs[s][1], models[s].kn);
            StyleProc::buildTable (tabs[s][2], models[s].kp2);
            StyleProc::buildTable (tabs[s][3], models[s].kn2);
        }

        void processChunk (float* const* io, int n, const Knobs& k)
        {
            const int want = std::clamp (k.style, 0, 4);
            // STYLE の切り替え。混ぜている途中なら終わるまで待つ（前の音が一瞬で消えないように）
            if (want != curStyle && xfade == 0)
            {
                for (int c = 0; c < nch; ++c)
                {
                    if (curStyle >= 0) ch[c].prev = ch[c].cur;           // 前のスタイルは状態ごと続けて鳴らし、消えていく
                    ch[c].cur.setModel (models[want], tabs[want]);   // 新しい方も状態を引き継ぐ（空から始めない）
                    if (curStyle < 0) ch[c].cur.reset();
                }
                xfade = curStyle >= 0 ? (int) (0.025 * fs) : 0;
                xfadeLen = std::max (1, xfade);
                prevStyle = curStyle; curStyle = want;
                if (prevStyle >= 0) { prevDg = std::pow (10.0, smDrive / 20.0); prevIg = std::pow (10.0, (models[prevStyle].g2A + models[prevStyle].g2B * smKnob / 10.0) / 20.0); }
                lastTone = -1e9;   // TONE の前後の配分は STYLE ごとに違う
            }
            const int st = curStyle;
            updateFilters (k, n, st);

            const double tDrive = driveDb (k, st);
            const double tOut = k.outputDb + k.extraGainDb;
            const double tMix = std::clamp ((double) k.mix, 0.0, 1.0);
            const double tSteep = k.steep ? 1.0 : 0.0;
            if (smDrive < -1e8) { smDrive = tDrive; smOut = tOut; smMix = tMix; smSteep = tSteep; smKnob = k.drive; }
            const double a = 1 - std::exp (-1.0 / (0.02 * fs));
            const double aSteep = 1 - std::exp (-1.0 / (0.015 * fs));
            const double pg = prevDg;   // 前の STYLE は切り替えた瞬間の倍率のまま消えていく

            const double hcTarget = std::clamp (std::log (k.highCutHz / 1000.0) / std::log (20.0), 0.0, 1.0) * (kHcSteps - 1);
            if (smHc < -1e8) smHc = hcTarget;
            const double aKnob = 1 - std::exp (-1.0 / (0.03 * fs));

            for (int i = 0; i < n; ++i)
            {
                smHc += aKnob * (hcTarget - smHc);
                const int hi0 = std::min ((int) smHc, kHcSteps - 2);
                const double hfr = smHc - hi0;
                smDrive += a * (tDrive - smDrive);
                smOut += a * (tOut - smOut);
                smMix += a * (tMix - smMix);
                smSteep += aSteep * (tSteep - smSteep);
                if (std::fabs (smSteep - tSteep) < 1e-6) smSteep = tSteep;
                smKnob += a * (k.drive - smKnob);
                const double dg = std::pow (10.0, smDrive / 20.0);
                const double ig = std::pow (10.0, (models[st].g2A + models[st].g2B * smKnob / 10.0) / 20.0);
                const double pig = prevIg;
                const double og = std::pow (10.0, smOut / 20.0);
                // 最初の 4 ms は新しい STYLE をまだ鳴らさない（係数が変わった直後の揺れを隠す）。残りをなめらかな曲線で混ぜる
                const int hold = (int) (0.004 * fs); const int el = xfadeLen - xfade;
                const double ph = el < hold ? 0.0 : (double) (el - hold) / std::max (1, xfadeLen - hold);
                const double xf = xfade > 0 ? 0.5 + 0.5 * std::cos (kPi * ph) : 0.0;
                for (int c = 0; c < nch; ++c)
                {
                    auto& C = ch[c];
                    double in = io[c][i];
                    if (! std::isfinite (in)) in = 0.0;   // NaN・無限大は無音として扱う
                    double x = C.lowCut.process (in);
                    x = C.tonePre[1].process (C.tonePre[0].process (x));
                    double w = C.cur.process (x, dg, ig);
                    if (xf > 0) w = w * (1 - xf) + C.prev.process (x, pg, pig) * xf;
                    w = C.tonePost[1].process (C.tonePost[0].process (w));
                    // HIGH CUT: ゆるやか・急の全段を動かし続け、使う 2 段を混ぜ、さらに STEEP で混ぜる
                    double g0 = 0, g1 = 0, s0 = 0, s1 = 0;
                    for (int s = 0; s < kHcSteps; ++s)
                    {
                        const double yg = C.hcG[s][2].process (C.hcG[s][1].process (C.hcG[s][0].process (w))) * C.hcGain[0][s];
                        const double ys = C.hcS[s][2].process (C.hcS[s][1].process (C.hcS[s][0].process (w))) * C.hcGain[1][s];
                        if (s == hi0) { g0 = yg; s0 = ys; } else if (s == hi0 + 1) { g1 = yg; s1 = ys; }
                    }
                    const double gw = g0 * (1 - hfr) + g1 * hfr, sw = s0 * (1 - hfr) + s1 * hfr;
                    w = gw * (1 - smSteep) + sw * smSteep;
                    const double dry = C.dryP.dryAlign (in);
                    double out = dry * (1 - smMix) + w * og * smMix;
                    if (! std::isfinite (out)) { C.reset(); out = 0.0; }   // 万一のときは状態を消して戻す
                    io[c][i] = (float) out;
                }
                if (xfade > 0) --xfade;
            }
        }

        void designHighCut (Channel& c)
        {
            for (int s = 0; s < kHcSteps; ++s)
            {
                const auto& g = kHcGentle[s];
                c.hcGain[0][s] = std::pow (10.0, g[0] / 20.0);
                c.hcG[s][0].designLp1 (g[1], fs);
                c.hcG[s][1].design (Biquad::highShelf, g[2], g[3], 0.707, fs);
                c.hcG[s][2].design (Biquad::peak, g[4], g[5], g[6], fs);
                const auto& t = kHcSteep[s];
                c.hcGain[1][s] = std::pow (10.0, t[0] / 20.0);
                c.hcS[s][0].design (Biquad::lowPass, std::min (t[1], fs * 0.49), 0, t[2], fs);
                c.hcS[s][1].design (Biquad::lowPass, std::min (t[3], fs * 0.49), 0, t[4], fs);
                c.hcS[s][2].design (Biquad::lowPass, std::min (t[5], fs * 0.49), 0, t[6], fs);
                if (s == kHcSteps - 1) { for (auto& b : c.hcG[s]) b.setIdentity(); for (auto& b : c.hcS[s]) b.setIdentity(); c.hcGain[0][s] = c.hcGain[1][s] = 1; }
            }
        }

        static void toneParams (double v01, double* out)   // v01: 0..1（0.5 が 0 dB）
        {
            const double pos = std::clamp (v01, 0.0, 1.0) * (kToneSteps - 1);
            const int i0 = std::min ((int) pos, kToneSteps - 2);
            const double fr = pos - i0;
            for (int j = 0; j < 5; ++j)
            {
                const bool isF = (j == 0 || j == 2);
                const double a = kTone[i0][j], b = kTone[i0 + 1][j];
                out[j] = isF ? std::exp (std::log (a) * (1 - fr) + std::log (b) * fr) : a * (1 - fr) + b * fr;
            }
        }

        void updateFilters (const Knobs& k, int n, int st)
        {
            const double ab = 1 - std::exp (-(double) n / (0.03 * fs));   // この区切りで 30 ms のなめらか化を進める量
            // LOW CUT と THUMP（Q もなめらかに変える）
            const double lcT = std::log (std::clamp ((double) k.lowCutHz, 20.0, 1000.0));
            smLowLog = smLowLog < -1e8 ? lcT : smLowLog + ab * (lcT - smLowLog);
            if (std::fabs (smLowLog - lcT) < 1e-4) smLowLog = lcT;
            const double lc = std::exp (smLowLog);
            const double v = std::log (lc / 20.0) / std::log (50.0);
            const double qT = k.thump ? kThumpQ (v) : kLowCutQ (v);
            smQ = smQ < -1e8 ? qT : smQ + ab * (qT - smQ);
            if (std::fabs (smQ - qT) < 1e-4) smQ = qT;
            const double facT = k.thump ? 1.0 : 0.1 + 0.9 * std::min (1.0, (lc - 20.0) / 10.0);
            smFac = smFac < -1e8 ? facT : smFac + ab * (facT - smFac);
            if (std::fabs (smFac - facT) < 1e-4) smFac = facT;
            if (std::fabs (lc - lastLow) > 1e-6 * lc || smQ != lastQ || smFac != lastFac)
            {
                // いちばん下（20 Hz）は本物どおり実質切らない（2 Hz）。THUMP が ON なら 20 Hz で山を作る。
                // 素通しに切り替えず、切れ目の位置（smFac）もなめらかに動かす（THUMP の ON/OFF でプチッとしない）
                const double fEff = lc * smFac;
                for (auto& c : ch) c.lowCut.design (Biquad::highPass, fEff, 0, smQ, fs);
                lastLow = lc; lastQ = smQ; lastFac = smFac;
            }
            // TONE
            const double tT = std::clamp ((double) k.tone, -1.0, 1.0);
            smTone = smTone < -1e8 ? tT : smTone + ab * (tT - smTone);
            if (std::fabs (smTone - tT) < 1e-4) smTone = tT;
            const double tn = smTone;
            if (tn != lastTone)
            {
                const double share = models[st].toneShare;
                const double tv = 0.5 + 0.5 * tn;
                double pp[5], qq[5];
                toneParams (0.5 + (1 - share) * (tv - 0.5), pp);
                toneParams (0.5 + share * (tv - 0.5), qq);
                const double gp = std::pow (10.0, pp[4] / 20), gq = std::pow (10.0, qq[4] / 20);
                for (auto& c : ch)
                {
                    c.tonePre[0].design (Biquad::lowShelf, pp[0], pp[1], 0.707, fs);
                    c.tonePre[1].design (Biquad::highShelf, pp[2], pp[3], 0.707, fs);
                    c.tonePre[1].b0 *= gp; c.tonePre[1].b1 *= gp; c.tonePre[1].b2 *= gp;
                    c.tonePost[0].design (Biquad::lowShelf, qq[0], qq[1], 0.707, fs);
                    c.tonePost[1].design (Biquad::highShelf, qq[2], qq[3], 0.707, fs);
                    c.tonePost[1].b0 *= gq; c.tonePost[1].b1 *= gq; c.tonePost[1].b2 *= gq;
                }
                lastTone = tn;
            }
        }
    };
}
