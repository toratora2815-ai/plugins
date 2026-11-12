#pragma once
// Fire Box の音の処理（JUCE なし。tools/fb_cli.cpp から単体テストできる）
//
// 流れ（1 チャンネルずつ。THICK だけはミッド／サイドなので 2 チャンネルまとめて）:
//   IN → BASS → THICK → BODY → COMP か OTT → SOFT CLIP / CRUNCH → MIX → LOUD → UNITY → OUT
// 数値の出どころは build/fuellab（測定と当てはめ）。表は Tables.h と Data/clip2d.bin。
//
// 遅れ（48 kHz のとき）: BODY の先読み 64 + COMP/OTT の先読み 1200 + LOUD の先読み 1200 = 2464 サンプル
// LIVE では COMP/OTT と LOUD の先読みを 256 に縮める（遅れ 576）。
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "Tables.h"

namespace firebox
{
    static constexpr double kPi = 3.14159265358979323846;

    inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
    inline double linToDb (double v)  { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

    // 頭打ち: |x| < K はそのまま、それより上は C に向かって指数で近づく
    inline double kclip (double x, double K, double C)
    {
        const double a = std::fabs (x);
        if (a < K) return x;
        const double w = std::max (C - K, 1.0e-12);
        const double y = K + w * (1.0 - std::exp (-(a - K) / w));
        return x < 0 ? -y : y;
    }

    //==================================================================== フィルター
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        inline double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        // RBJ cookbook
        void set (int kind, double f0, double Q, double gainDb, double sr)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f0 / sr, cw = std::cos (w), sw = std::sin (w), al = sw / (2 * Q);
            double B0, B1, B2, A0, A1, A2;
            if (kind == 0)      { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; }      // LP
            else if (kind == 1) { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; }   // HP
            else                { B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; } // peak
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
    };

    // 6 次の IIR（転置直接形 II、double）
    struct Iir6
    {
        double b[7] {1,0,0,0,0,0,0}, a[7] {1,0,0,0,0,0,0}, z[7] {};
        void reset() { for (auto& v : z) v = 0; }
        inline double process (double x)
        {
            const double y = b[0] * x + z[0];
            for (int k = 1; k < 6; ++k) z[k - 1] = b[k] * x - a[k] * y + z[k];
            z[5] = b[6] * x - a[6] * y;
            return y;
        }
    };

    // 48 kHz で測った z 領域の多項式を別のサンプルレートへ（双一次変換の往復。48 kHz では元のまま）
    // z^-1 ⇔ w の写像 z^-1 = (1-w)/(1+w) は自分自身の逆なので、同じ変換を 2 回使う
    inline void bilinTransform (const double* in, double* out, int N)
    {
        // out(w) = Σ in_j (1-w)^j (1+w)^(N-j)
        double res[8] = {};
        for (int j = 0; j <= N; ++j)
        {
            double p[8] = {1};   // (1-w)^j (1+w)^(N-j)
            int deg = 0;
            for (int k = 0; k < j; ++k) { for (int d = deg + 1; d > 0; --d) p[d] = p[d] - p[d - 1]; ++deg; }
            for (int k = 0; k < N - j; ++k) { for (int d = deg + 1; d > 0; --d) p[d] = p[d] + p[d - 1]; ++deg; }
            for (int d = 0; d <= N; ++d) res[d] += in[j] * p[d];
        }
        for (int d = 0; d <= N; ++d) out[d] = res[d];
    }
    inline void resampleIir (const double* b48, const double* a48, double* b, double* a, int N, double sr)
    {
        double bw[8], aw[8];
        bilinTransform (b48, bw, N); bilinTransform (a48, aw, N);
        const double r = sr / 48000.0;   // w = r * v
        double s = 1;
        for (int k = 0; k <= N; ++k) { bw[k] *= s; aw[k] *= s; s *= r; }
        bilinTransform (bw, b, N); bilinTransform (aw, a, N);
        const double n0 = a[0];
        for (int k = 0; k <= N; ++k) { b[k] /= n0; a[k] /= n0; }
    }

    //==================================================================== BASS
    struct Bass
    {
        Biquad lp1, lp2, pk1, pk2;
        double sr = 48000, curV = -1;
        void prepare (double s) { sr = s; lp1.set (0, 210, 1 / std::sqrt (3.0), 0, sr); lp2 = lp1; curV = -1; reset(); }
        void reset() { lp1.reset(); lp2.reset(); pk1.reset(); pk2.reset(); }
        void setAmount (double v)
        {
            if (v == curV) return;
            curV = v;
            const double G = std::max (0.0, 12 * v * v - 0.6);
            pk1.set (2, 54.375, 1.5, G, sr);
            pk2.set (2, 27.1875, 1 / std::sqrt (2.0), -0.8 * G, sr);
        }
        inline double process (double x, double v)
        {
            double u = lp1.process (x);
            const double c = std::clamp (u, -1.0, 1.0), c2 = c * c;
            u = lp2.process (c * c2 * (6.0 + 1.5 * c2));
            return pk2.process (pk1.process (x + std::min (v, 0.25) * u));
        }
    };

    //==================================================================== THICK（ミッド／サイド）
    struct Thick
    {
        Iir6 fm, fs;
        double pm[4] {1.00237, 0, 0.22323, -0.01604}, ps[4] {1.00237, 0, 0.22323, -0.01604}, tm = 0, ts = 0;
        double sr = 48000, curV = -1;
        void prepare (double s) { sr = s; curV = -1; reset(); }
        void reset() { fm.reset(); fs.reset(); }
        void setAmount (double v)
        {
            v = std::clamp (v, 0.0, 1.0);
            if (v == curV) return;
            curV = v;
            using namespace data;
            int k = 0;
            while (k < thickRows - 2 && thickV[k + 1] <= v) ++k;
            const double fr = (v - thickV[k]) / (thickV[k + 1] - thickV[k]);
            double B[7];
            for (int i = 0; i < 7; ++i) B[i] = (1 - fr) * thickB[k][i] + fr * thickB[k + 1][i];
            for (int i = 0; i < 4; ++i) { pm[i] = (1 - fr) * thickMid[k][i] + fr * thickMid[k + 1][i]; ps[i] = (1 - fr) * thickSide[k][i] + fr * thickSide[k + 1][i]; }
            double b[7], a[7];
            resampleIir (B, thickDen, b, a, 6, sr);
            for (int i = 0; i < 7; ++i) { fm.b[i] = fs.b[i] = b[i]; fm.a[i] = fs.a[i] = a[i]; }
            tm = std::tanh (pm[3]); ts = std::tanh (ps[3]);
        }
        static inline double curve (const double* p, double t3, double u) { return p[0] * u + p[1] * (std::tanh (p[2] * u + p[3]) - t3); }
        inline void process (double& L, double& R)
        {
            const double M = 0.5 * (L + R), S = 0.5 * (L - R);
            const double m = curve (pm, tm, fm.process (M)), s = curve (ps, ts, fs.process (S));
            L = m + s; R = m - s;
        }
    };

    //==================================================================== 遅らせるだけ
    struct Delay
    {
        std::vector<double> buf; int pos = 0, len = 0;
        void prepare (int n) { len = n; buf.assign ((size_t) std::max (n, 1), 0.0); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
        inline double process (double x)
        {
            if (len <= 0) return x;
            const double y = buf[(size_t) pos]; buf[(size_t) pos] = x; if (++pos >= len) pos = 0; return y;
        }
    };

    //==================================================================== 山（|x| の極大）ごとに動くゲイン
    // 山 p の直後のサンプルで「次の山 pn までの 1 サンプルあたりの増分」を決め、その 1 サンプルは止め、
    // 次のサンプルから pn まで足していく（FUEL の COMP / LOUD / OTT と同じ動き）。
    // 先読み LA サンプルの中に次の山がなければ、先読みの端を仮の山にする。
    struct KnotLine
    {
        int LA = 1200, cap = 0;
        std::vector<double> u, aU, tgt;   // 入力、|入力|、その場の目標
        std::vector<int64_t> knots; int kHead = 0, kTail = 0;   // 山の番号（リング）
        int64_t n = 0;
        void prepare (int la)
        {
            LA = std::max (2, la);
            cap = 1; while (cap < LA + 8) cap <<= 1;
            u.assign ((size_t) cap, 0.0); aU.assign ((size_t) cap, 0.0); tgt.assign ((size_t) cap, 0.0);
            knots.assign ((size_t) cap, 0); kHead = kTail = 0; n = 0; pseudo = -1;
        }
        inline void push (double x, double t)
        {
            const int m = cap - 1;
            u[(size_t) (n & m)] = x; aU[(size_t) (n & m)] = std::fabs (x); tgt[(size_t) (n & m)] = t;
            if (n >= 2)
            {
                const double a0 = aU[(size_t) ((n - 2) & m)], a1 = aU[(size_t) ((n - 1) & m)], a2 = aU[(size_t) (n & m)];
                if (a1 >= a0 && a1 > a2) { knots[(size_t) kTail] = n - 1; kTail = (kTail + 1) & m; }
            }
            ++n;
        }
        inline bool ready() const { return n > LA; }
        inline int64_t outIndex() const { return n - 1 - LA; }
        inline double inputAt (int64_t i) const { return u[(size_t) (i & (cap - 1))]; }
        inline double targetAt (int64_t i) const { return tgt[(size_t) (i & (cap - 1))]; }
        inline bool isKnot (int64_t i) const
        {
            // 山かどうかはリングの先頭（いちばん古い山）を見ればわかる
            return kHead != kTail && knots[(size_t) kHead] == i;
        }
        // i が山なら取り除いて次の山（なければ先読みの端）を返す
        inline int64_t popAndNext (int64_t i)
        {
            const int m = cap - 1;
            while (kHead != kTail && knots[(size_t) kHead] <= i) kHead = (kHead + 1) & m;
            if (kHead != kTail) { pseudo = -1; return knots[(size_t) kHead]; }
            pseudo = n - 1;   // 先読みの中に山がない（無音など）: 端を仮の山にして、着いたら計算し直す
            return pseudo;
        }
        // 山か、仮の山に着いたところ
        inline bool isEvent (int64_t i) const { return isKnot (i) || i == pseudo; }
        int64_t pseudo = -1;
    };

    //==================================================================== COMP（3.5:1、山ごと、自動メイクアップ）
    inline double compH (double d)
    {
        if (d < -9) return 0;
        if (d < 9)  return (d + 9) * (d + 9) / 36.0;
        if (d < 15) return d;
        if (d < 39) return d - (d - 15) * (d - 15) / 48.0;
        return 27;
    }
    struct Comp
    {
        KnotLine k; double s = 1, step = 0, M = 1, amt = 0;
        void prepare (int la) { k.prepare (la); s = M; step = 0; }
        void reset() { k.prepare (k.LA); s = M; step = 0; }
        inline double req (double x) const
        {
            const double L = linToDb (std::fabs (x));
            return M * dbToLin (-(1 - 1 / 3.5) * compH (L - (6 - amt)));
        }
        void setAmount (double a) { amt = a; M = dbToLin (0.375 * a); }
        inline double process (double x)
        {
            k.push (x, req (x));
            if (! k.ready()) return 0;
            const int64_t t = k.outIndex();
            if (k.isEvent (t - 1))
            {
                const int64_t pn = k.popAndNext (t - 1);
                const double r = k.targetAt (pn); const double gap = (double) (pn - (t - 1));
                double tg;
                if (r < s) tg = r;
                else
                {
                    const double q = 1 - s / M, q0 = 0.0246834;
                    double c = 1.0 / 123.0;
                    if (q < q0) c = std::max (c, std::pow (1 - q / q0, 1.93552));
                    tg = s + (r - s) * c;
                }
                step = (tg - s) / gap;
            }
            else s = std::min (s + step, 4.0 * M);
            return k.inputAt (t) * s;
        }
    };

    //==================================================================== LOUD（山ごとのリミッター、天井 1.0）
    struct Loud
    {
        KnotLine k; double s = 0, step = 0;
        void prepare (int la) { k.prepare (la); s = 0; step = 0; }
        void reset() { k.prepare (k.LA); s = 0; step = 0; }
        inline double process (double x)
        {
            const double a = std::fabs (x);
            k.push (x, a > 1.0 ? 1.0 - 1.0 / a : 0.0);
            if (! k.ready()) return 0;
            const int64_t t = k.outIndex();
            if (k.isEvent (t - 1))
            {
                const int64_t pn = k.popAndNext (t - 1);
                step = (k.targetAt (pn) - s) / (double) (pn - (t - 1));
            }
            else s = std::clamp (s + step, 0.0, 1.0);
            return std::clamp (k.inputAt (t) * (1 - s), -1.0, 1.0);
        }
    };

    //==================================================================== BODY（ゆっくり持ち上げる、64 サンプル先読み）
    // 形: H(d) d = 10.1756 - 入力 dB。最初 31.16 dB 幅で 2 次に立ち上がり、47.79 dB 幅で頭打ち
    struct Body
    {
        static constexpr double L1 = 10.1756, W1 = 31.1644, W2 = 47.7916;
        static inline void H (double L, double& h, double& dh)   // dh = -dH/dL（レベルが下がると増える向き）
        {
            const double d = L1 - L;
            if (d <= 0) { h = 0; dh = 0; }
            else if (d < W1) { h = d * d / (2 * W1); dh = d / W1; }
            else if (d < W1 + W2) { const double e = d - W1; h = W1 / 2 + e - e * e / (2 * W2); dh = 1 - e / W2; }
            else { h = W1 / 2 + W2 / 2; dh = 0; }
        }
        std::vector<double> ring; int LA = 64, pos = 0; double h = 0, sr = 48000;
        void prepare (double s) { sr = s; LA = std::max (1, (int) std::lround (64.0 * s / 48000.0)); ring.assign ((size_t) LA + 1, 0.0); pos = 0; h = 0; }
        void reset() { std::fill (ring.begin(), ring.end(), 0.0); pos = 0; h = 0; }
        static double slope (double b) { const double R = 1 + b * b * (4 + 0.06 * b * b); return 1 - 1 / R; }
        // 戻り値: LA サンプル遅れた入力に掛けたもの
        inline double process (double x, double b)
        {
            ring[(size_t) pos] = x; pos = (pos + 1) % (int) ring.size();
            const double out = ring[(size_t) pos];   // いちばん古い = LA 前
            double m = 0;
            for (double v : ring) m = std::max (m, std::fabs (v));
            double T, D; H (linToDb (m), T, D);
            if (T < h) h = T;
            else if (T > h)
            {
                const double d = T - h;
                const double st = std::min (d, (0.99229 * d + 4.4057 * D) / sr);
                h += st;
            }
            return out * dbToLin (slope (b) * h);
        }
    };

    //==================================================================== OTT（88 Hz / 2.5 kHz の 3 帯域、上げ下げ）
    // 各帯域: dB で動く検出器を 2 つ（持ち上げ用・下げ用）。|x| を dB にして（下限 -100 dB）、
    // 上がるときは速く（アタック）、下がるときはゆっくり（リリース）追う。ゲイン = P0 + U(持ち上げ用) - D(下げ用)
    struct OttBand
    {
        double eu = -100, ed = -100, cA = 0, cU = 0, cD = 0;
        double P0 = 0; float U[data::ottGrid] {}, Dn[data::ottGrid] {};
        void prepare (double sr, int b)
        {
            static constexpr double tU[3] = {2409.083, 2130.097, 1732.530};    // 48 kHz のサンプル数
            static constexpr double tD[3] = {8701.993, 11223.961, 3823.533};
            static constexpr double tA[3] = {46.42580, 11.24109, 4.18657};
            const double r = sr / 48000.0;
            cU = 1.0 / (tU[b] * r); cD = 1.0 / (tD[b] * r); cA = std::min (1.0, 1.0 / (tA[b] * r));
            eu = ed = -100;
        }
        // 量の補間をした曲線から、-40 dB を境に持ち上げ分と下げ分に分ける
        void setCurve (const float* c1, const float* c2, double fr)
        {
            using namespace data;
            const int i40 = (int) std::lround ((-40.0 - ottGridLo) / ottGridStep);
            P0 = c1[i40] + fr * (c2[i40] - c1[i40]);
            for (int i = 0; i < ottGrid; ++i)
            {
                const double c = c1[i] + fr * (c2[i] - c1[i]);
                U[i] = (float) (i < i40 ? std::max (c - P0, 0.0) : 0.0);
                Dn[i] = (float) (i > i40 ? std::max (P0 - c, 0.0) : 0.0);
            }
        }
        static inline double look (const float* t, double L)
        {
            using namespace data;
            double p = std::clamp ((L - ottGridLo) / ottGridStep, 0.0, (double) ottGrid - 1.001);
            const int i = (int) p; const double f = p - i;
            return t[i] + f * (t[i + 1] - t[i]);
        }
        inline double process (double x)
        {
            const double v = std::max (20.0 * std::log10 (std::fabs (x) + 1e-30), -100.0);
            eu += (v - eu) * (v > eu ? cA : cU);
            ed += (v - ed) * (v > ed ? cA : cD);
            return x * dbToLin (P0 + look (U, eu) - look (Dn, ed));
        }
    };
    struct Ott
    {
        // 88 Hz の分け方は測った形（Q の違う 2 段ずつ）、2.5 kHz は LR4
        Biquad lpA, lpB, hpA, hpB, aLa, aLb, aHa, aHb, lp2a, lp2b, hp2a, hp2b;
        OttBand b[3];
        Delay out;   // COMP と同じだけ遅らせる（OTT 自体は先読みなし）
        void prepare (int la, double sr)
        {
            const double q = 1 / std::sqrt (2.0);
            lpA.set (0, 86.3455, 0.8832, 0, sr); lpB.set (0, 80.8348, 0.6176, 0, sr);
            hpA.set (1, 90.8755, 0.6118, 0, sr); hpB.set (1, 88.688, 0.8763, 0, sr);
            aLa.set (0, 2500, q, 0, sr); aLb = aLa; aHa.set (1, 2500, q, 0, sr); aHb = aHa;
            lp2a = aLa; lp2b = aLa; hp2a = aHa; hp2b = aHa;
            reset();
            for (int i = 0; i < 3; ++i) b[i].prepare (sr, i);
            out.prepare (la);
        }
        void reset() { for (auto* f : { &lpA, &lpB, &hpA, &hpB, &aLa, &aLb, &aHa, &aHb, &lp2a, &lp2b, &hp2a, &hp2b }) f->reset(); }
        double curAmt = -1;
        void setAmount (double amtDb)
        {
            if (amtDb == curAmt) return;
            curAmt = amtDb;
            using namespace data;
            int k = 0;
            while (k < ottAmts - 2 && ottAmtDb[k + 1] <= amtDb) ++k;
            const double fr = std::clamp ((amtDb - ottAmtDb[k]) / (ottAmtDb[k + 1] - ottAmtDb[k]), 0.0, 1.0);
            for (int i = 0; i < 3; ++i) b[i].setCurve (ottCurve[k][i], ottCurve[k + 1][i], fr);
        }
        inline double process (double x)
        {
            const double lo0 = lpB.process (lpA.process (x)), hi1 = hpB.process (hpA.process (x));
            const double lo = aLb.process (aLa.process (lo0)) + aHb.process (aHa.process (lo0));
            const double mid = lp2b.process (lp2a.process (hi1)), high = hp2b.process (hp2a.process (hi1));
            return out.process (b[0].process (lo) + b[1].process (mid) + b[2].process (high));
        }
    };

    // OTT モードの MIX の元の音: FUEL は帯域分けフィルターを通した音（足すと全域通過）を使う
    struct XoSum
    {
        Biquad lpA, lpB, hpA, hpB, aLa, aLb, aHa, aHb, lp2a, lp2b, hp2a, hp2b;
        void prepare (double sr)
        {
            const double q = 1 / std::sqrt (2.0);
            lpA.set (0, 86.3455, 0.8832, 0, sr); lpB.set (0, 80.8348, 0.6176, 0, sr);
            hpA.set (1, 90.8755, 0.6118, 0, sr); hpB.set (1, 88.688, 0.8763, 0, sr);
            aLa.set (0, 2500, q, 0, sr); aLb = aLa; aHa.set (1, 2500, q, 0, sr); aHb = aHa;
            lp2a = aLa; lp2b = aLa; hp2a = aHa; hp2b = aHa;
        }
        inline double process (double x)
        {
            const double lo0 = lpB.process (lpA.process (x)), hi1 = hpB.process (hpA.process (x));
            return aLb.process (aLa.process (lo0)) + aHb.process (aHa.process (lo0)) + lp2b.process (lp2a.process (hi1)) + hp2b.process (hp2a.process (hi1));
        }
    };

    //==================================================================== SOFT CLIP / CRUNCH
    struct Clip
    {
        const float* table = nullptr;   // clip2d.bin（41×41×301、SOFT CLIP と CRUNCH を両方使うとき）
        double scG = 1, scK = 1, scC = 2;
        double cA = 0, cT = 0, cB = 1, cK = 1, cC = 2;
        int ci = 0, cj = 0; double cfi = 0, cfj = 0;
        float cur[data::clipX] {};
        void set (double sc, double cr, bool both)
        {
            scG = std::pow (10.0, 0.72 * sc); scK = std::pow (10.0, -0.48 * sc);
            scC = scK * (1 + std::min (std::pow (10.0, 1.2 * sc), std::pow (10.0, 0.22)));
            using namespace data;
            int k = std::clamp ((int) std::floor (cr * 100.0), 0, crunchRows - 2);
            const double fr = std::clamp ((cr - crunchTab[k][0]) / (crunchTab[k + 1][0] - crunchTab[k][0]), 0.0, 1.0);
            double p[5];
            for (int i = 0; i < 5; ++i) p[i] = (1 - fr) * crunchTab[k][i + 1] + fr * crunchTab[k + 1][i + 1];
            cA = p[0]; cT = p[1]; cB = p[2]; cK = p[3]; cC = p[3] + std::fabs (p[4]) + 1e-12;
            if (both && table != nullptr)
            {
                const int n = clipKnobs - 1;
                const double fs = std::clamp (sc, 0.0, 1.0) * n, fc = std::clamp (cr, 0.0, 1.0) * n;
                const int i = std::min ((int) fs, n - 1), j = std::min ((int) fc, n - 1);
                const double a = fs - i, b = fc - j;
                const float* c00 = table + ((size_t) i * clipKnobs + j) * clipX;
                const float* c01 = table + ((size_t) i * clipKnobs + j + 1) * clipX;
                const float* c10 = table + ((size_t) (i + 1) * clipKnobs + j) * clipX;
                const float* c11 = table + ((size_t) (i + 1) * clipKnobs + j + 1) * clipX;
                for (int x = 0; x < clipX; ++x)
                    cur[x] = (float) ((1 - a) * (1 - b) * c00[x] + a * (1 - b) * c10[x] + (1 - a) * b * c01[x] + a * b * c11[x]);
            }
        }
        inline double soft (double x) const { return kclip (scG * x, scK, scC); }
        inline double crunch (double x) const { return std::clamp (cA * x, -cT, cT) + kclip (cB * x, cK, cC); }
        inline double both (double x) const
        {
            // x の格子: 0、そして 1e-4〜32 を対数で 300 等分
            const double a = std::fabs (x);
            double y;
            if (a < 1e-4) y = a / 1e-4 * cur[1];
            else
            {
                double p = std::log (a / 1e-4) / std::log (32.0 / 1e-4) * 299.0;
                if (p >= 299.0) y = cur[300];
                else
                {
                    const int i = (int) p; const double f = p - i;
                    y = cur[i + 1] + f * (cur[i + 2] - cur[i + 1]);
                }
            }
            return x < 0 ? -y : y;
        }
    };

    //==================================================================== UNITY（4 秒の RMS 比、チャンネルごと）
    struct Unity
    {
        std::vector<float> rin, rout; int pos = 0, len = 1; double sIn = 0, sOut = 0, g = 1;
        void prepare (double sr) { len = std::max (1, (int) std::lround (4.0 * sr)); rin.assign ((size_t) len, 0); rout.assign ((size_t) len, 0); reset(); }
        void reset() { std::fill (rin.begin(), rin.end(), 0.0f); std::fill (rout.begin(), rout.end(), 0.0f); pos = 0; sIn = sOut = 0; g = 1; }
        inline double gain (double dry, double wet)
        {
            const float a = (float) (dry * dry), b = (float) (wet * wet);
            sIn += a - rin[(size_t) pos]; sOut += b - rout[(size_t) pos];
            rin[(size_t) pos] = a; rout[(size_t) pos] = b;
            if (++pos >= len) { pos = 0; double s1 = 0, s2 = 0; for (int i = 0; i < len; ++i) { s1 += rin[(size_t) i]; s2 += rout[(size_t) i]; } sIn = s1; sOut = s2; }
            if (sIn > 1e-14 && sOut > 1e-14) g = std::min (std::sqrt (sIn / sOut), 16.0);
            return g;
        }
    };

    //==================================================================== 全体
    struct Params
    {
        double inDb = 0, outDb = -0.1, bass = 0.15, thick = 0.13, body = 0, comp = 0, sc = 0, crunch = 0, mix = 1, loud = 0;
        bool bassOn = true, thickOn = true, bodyOn = true, compOn = true, ott = false, scOn = true, crunchOn = true, loudOn = true, unity = false;
    };

    // ON/OFF の切り替えを 15 ms でなめらかに
    struct Ramp
    {
        double v = 0, step = 1.0 / 720.0; bool target = false;
        void prepare (double sr, bool on) { step = 1.0 / std::max (1.0, 0.015 * sr); target = on; v = on ? 1 : 0; }
        inline void set (bool on) { target = on; }
        inline double next() { v = target ? std::min (1.0, v + step) : std::max (0.0, v - step); return v; }
    };

    class Engine
    {
    public:
        void setClipTable (const float* t) { for (auto& c : clip) c.table = t; }

        void prepare (double sampleRate, bool live, const Params& init = Params())
        {
            sr = sampleRate; isLive = live;
            const double r = sr / 48000.0;
            const int la = (int) std::lround ((live ? 256.0 : 1200.0) * r);
            for (int c = 0; c < 2; ++c)
            {
                bass[c].prepare (sr); body[c].prepare (sr); comp[c].prepare (la); ott[c].prepare (la, sr); xo[c].prepare (sr); loud[c].prepare (la); unity[c].prepare (sr);
            }
            thick.prepare (sr);
            latency = body[0].LA + la + la;
            for (int c = 0; c < 2; ++c) { dryD[c].prepare (latency - la); loudPass[c].prepare (la); bypD[c].prepare (latency); }
            ginRing.assign ((size_t) latency + 1, -1.0); ginPos = 0;
            rBass.prepare (sr, init.bassOn); rThick.prepare (sr, init.thickOn); rBody.prepare (sr, init.bodyOn); rComp.prepare (sr, init.compOn);
            rOtt.prepare (sr, init.compOn && init.ott); rDyn.prepare (sr, init.bodyOn || init.compOn); rSc.prepare (sr, init.scOn);
            rCr.prepare (sr, init.crunchOn); rLoud.prepare (sr, init.loudOn); rUnity.prepare (sr, init.unity); rByp.prepare (sr, false);
            prev = init; havePrev = false;
        }
        int getLatency() const { return latency; }

        // L, R をその場で書き換える（R が nullptr ならモノラル: L を両方に使って L だけ書く）
        void process (float* L, float* R, int n, const Params& p, bool bypass)
        {
            if (! havePrev) { prev = p; havePrev = true; }
            rComp.set (p.compOn);
            for (int k = 0; k < n; ++k) rComp.next();   // COMP の ON/OFF は量の変化として 1 ブロックごとに
            applyParams (p);
            rBass.set (p.bassOn); rThick.set (p.thickOn); rBody.set (p.bodyOn); rComp.set (p.compOn); rOtt.set (p.compOn && p.ott);
            rDyn.set (p.bodyOn || p.compOn); rSc.set (p.scOn); rCr.set (p.crunchOn); rLoud.set (p.loudOn); rUnity.set (p.unity); rByp.set (bypass);
            // IN / OUT / MIX / LOUD は 1 サンプルごとに直線で補間する
            const double gin0 = dbToLin (prev.inDb), gin1 = dbToLin (p.inDb), go0 = dbToLin (prev.outDb), go1 = dbToLin (p.outDb);
            const double gl0 = dbToLin (prev.loud * 24.0), gl1 = dbToLin (p.loud * 24.0), mx0 = prev.mix, mx1 = p.mix;
            const double inv = 1.0 / std::max (1, n);
            for (int i = 0; i < n; ++i)
            {
                const double f = (i + 1) * inv;
                const double gin = gin0 + (gin1 - gin0) * f, gout = go0 + (go1 - go0) * f, gl = gl0 + (gl1 - gl0) * f, mix = mx0 + (mx1 - mx0) * f;
                const double wB = rBass.next(), wT = rThick.next(), wBo = rBody.next(), wO = rOtt.next(), wD = rDyn.next();
                const double wS = rSc.next(), wR = rCr.next(), wL = rLoud.next(), wU = rUnity.next(), wY = rByp.next();
                const double raw[2] = { L[i], R ? R[i] : L[i] };
                if (ginRing[0] < 0) std::fill (ginRing.begin(), ginRing.end(), gin);
                ginRing[(size_t) ginPos] = gin; ginPos = (ginPos + 1) % (int) ginRing.size(); const double ginD = ginRing[(size_t) ginPos];
                double x[2] = { raw[0] * gin, raw[1] * gin };
                const double d[2] = { x[0], x[1] };
                for (int c = 0; c < 2; ++c) { const double b = bass[c].process (x[c], p.bass); x[c] += wB * (b - x[c]); }
                { double tl = x[0], tr = x[1]; thick.process (tl, tr); x[0] += wT * (tl - x[0]); x[1] += wT * (tr - x[1]); }
                for (int c = 0; c < 2; ++c)
                {
                    // BODY（遅れ LAb）→ COMP / OTT（遅れ LA）。どちらも常に動かしておき、切り替えは重ねて行う
                    const double yb = body[c].process (x[c], wBo * p.body);
                    const double vc = comp[c].process (yb), vo = ott[c].process (yb);
                    const double pass = comp[c].k.inputAt (comp[c].k.outIndex());
                    double y = pass + wD * (vc + wO * (vo - vc) - pass);
                    // SOFT CLIP / CRUNCH（両方なら合わせた形）
                    const double wBoth = wS * wR, wSo = wS * (1 - wR), wCr = (1 - wS) * wR, wNo = (1 - wS) * (1 - wR);
                    double yc = wNo * y;
                    if (wBoth > 0) yc += wBoth * clip[c].both (y);
                    if (wSo > 0)   yc += wSo * clip[c].soft (y);
                    if (wCr > 0)   yc += wCr * clip[c].crunch (y);
                    const double dxo = xo[c].process (d[c]);
                    const double dry = dryD[c].process (d[c] + wO * (dxo - d[c]));
                    y = (1 - mix) * dry + mix * yc;
                    // LOUD: 先読みには常に gl を掛けた音を入れ、外すときは素通しの遅れと重ねる
                    const double zl = loud[c].process (y * gl), zp = loudPass[c].process (y);
                    double z = zp + wL * (zl - zp);
                    // GAIN MATCH（unity）: IN の分はすぐ戻し（IN は歪ませる量だけ変える）、処理で変わった分を元の音（IN の前）に 4 秒平均で合わせる。
                    // ON の間は OUT でも音量を変えない（ほかのプラグインの GAIN MATCH と同じ。前は IN・OUT の分が残っていた。2026-11-12）
                    const double by = bypD[c].process (raw[c]);
                    const double zU = z / std::max (1.0e-6, ginD);
                    const double ug = unity[c].gain (by, zU);
                    z += wU * (zU * ug - z);
                    z *= gout + wU * (1.0 - gout);
                    const double o = z + wY * (by - z);
                    if (c == 0) L[i] = (float) o; else if (R) R[i] = (float) o;
                }
            }
            prev = p;
        }

    private:
        void applyParams (const Params& p)
        {
            for (int c = 0; c < 2; ++c)
            {
                bass[c].setAmount (p.bass);
                comp[c].setAmount (rComp.v * p.comp * 48.0);   // ON/OFF も 15 ms かけて量を動かす
                ott[c].setAmount (p.comp * 48.0);
                clip[c].set (p.sc, p.crunch, true);
            }
            thick.setAmount (p.thick);
        }

        double sr = 48000; bool isLive = false; int latency = 0;
        Bass bass[2]; Thick thick; Body body[2]; Comp comp[2]; Ott ott[2]; XoSum xo[2]; Clip clip[2]; Loud loud[2]; Unity unity[2];
        Delay dryD[2], loudPass[2], bypD[2];   // UNITY（GAIN MATCH）の基準は bypD（IN の前の元の音を全体の遅れだけ遅らせたもの）
        Ramp rBass, rThick, rBody, rComp, rOtt, rDyn, rSc, rCr, rLoud, rUnity, rByp;
        Params prev; bool havePrev = false; std::vector<double> ginRing; int ginPos = 0;
    };
}
