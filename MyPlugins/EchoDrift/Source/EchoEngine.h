// Echo Drift のエンジン（JUCE なし。tools/ の CLI とテストでも使う）
//
// 音の流れ（左右は別々のループ。Ping-Pong だけ左右を足した 1 本）:
//   入力 → 線 A（長さ T）→ 処理 P → 線 B（長さ T）→ 処理 P → 線 A へ戻る（× FEEDBACK）
//   出口の左右は A と B の途中から読む（奇数回目は A、偶数回目は B）。
//   こうすると「奇数回目は左 -4 ms・右 +4 ms、偶数回目は左 +6 ms・右 -6 ms」のような
//   交互のずれ（スタイルごとに測った値）を、ループの長さを変えずに作れる。
//   処理 P = 4 倍オーバーサンプリングの歪み → にじみ（オールパス）→ ループ内のフィルター → HIGH CUT
//   出口 = 色のフィルター（1 回目のエコーの音色 ÷ ループ 1 回分）→ LOW CUT
// 揺れ（ワウ・フラッター）は音程（セント）の形で作り、積分して読み出し位置をずらす。
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace echo
{
    constexpr double kPi = 3.14159265358979323846;

    // ---------------- 2 次フィルター（RBJ。tools の当てはめと同じ式） ----------------
    // type: 0 HP2 1 LP2 2 ピーク 3 ローシェルフ 4 ハイシェルフ 5 HP1 6 LP1
    struct BiqSpec { int type; float f, q, g; };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        void design (const BiqSpec& s, double fs)
        {
            const double f = std::min ((double) s.f, fs * 0.49);
            const double w = 2 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
            const double al = sw / (2 * std::max (0.05, (double) s.q)), A = std::pow (10.0, s.g / 40.0);
            double B0, B1, B2, A0, A1, A2;
            switch (s.type)
            {
                case 0: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
                case 1: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
                case 2: B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;
                case 3:
                {
                    const double sa = 2 * std::sqrt (A) * al;
                    B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                    A0 = (A + 1) + (A - 1) * cw + sa; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sa; break;
                }
                case 4:
                {
                    const double sa = 2 * std::sqrt (A) * al;
                    B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                    A0 = (A + 1) - (A - 1) * cw + sa; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sa; break;
                }
                case 5: { const double k = std::tan (w / 2); B0 = 1 / (1 + k); B1 = -B0; B2 = 0; A0 = 1; A1 = (k - 1) / (k + 1); A2 = 0; break; }
                case 6: { const double k = std::tan (w / 2); B0 = k / (1 + k); B1 = B0; B2 = 0; A0 = 1; A1 = (k - 1) / (k + 1); A2 = 0; break; }
                default: B0 = 1; B1 = B2 = 0; A0 = 1; A1 = A2 = 0; break;
            }
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
        inline float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0; }
        double magSq (double f, double fs) const
        {
            const double w = 2 * kPi * f / fs;
            const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
            const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
            const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
            return (nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di);
        }
    };

    struct Chain
    {
        static constexpr int maxN = 10;
        Biquad b[maxN];
        int n = 0;
        float gain = 1.0f;
        void set (const BiqSpec* s, int count, float gainDb, double fs)
        {
            n = std::min (count, maxN);
            for (int i = 0; i < n; ++i)
            {
                const double z1 = b[i].z1, z2 = b[i].z2;
                b[i].design (s[i], fs);
                b[i].z1 = z1; b[i].z2 = z2;
            }
            gain = (float) std::pow (10.0, gainDb / 20.0);
        }
        inline float process (float x) { for (int i = 0; i < n; ++i) x = b[i].process (x); return x * gain; }
        void reset() { for (auto& q : b) q.reset(); }
        double magDb (double f, double fs) const
        {
            double m = (double) gain * gain;
            for (int i = 0; i < n; ++i) m *= b[i].magSq (f, fs);
            return 10 * std::log10 (m + 1e-30);
        }
    };

    // ---------------- スタイル（Profiles.h で測った値から作る） ----------------
    struct Profile
    {
        const char* name;
        // 出口の色（1 回目のエコー ÷ ループ 1 回分）
        float colorGain; int nColor; BiqSpec color[8];
        // ループ 1 回分（くり返すたびに掛かる）
        float loopGain; int nLoop; BiqSpec loop[5];
        // 交互のずれ（ms）: 左の奇数回・左の偶数回・右の奇数回・右の偶数回
        float off[4];
        // 揺れ（セント）
        float sineRate, sineDepth, sinePhaseR;          // 正弦波（右の位相 度）
        float sqAmp, sqInterval, sqJitter, sqSmooth;    // 四角（＝読み出しが三角に動く）: 高さ・半周期 s・ばらつき 0..1・角のなまり s
        int sqRMode; float sqRAmp;                      // 右: 0 同じ 1 逆 2 別々（sqRAmp = 右の高さの倍率）
        float flAmp, flRate; int flSame;                // ランダムなフラッター（セント rms・帯域 Hz・左右同じなら 1）
        // 歪み: SATURATION 0 / 6 / 12 / 18 / 24 dB のときの、入れる倍率と戻す倍率（dB）。間は直線で結ぶ
        float satIn[5], satOut[5];
        float hard;     // 曲がり方の硬さ（y = x / (1 + |x|^k)^(1/k) の k）
        float bias;     // 偶数次（非対称）
        float compThr, compRatio;   // 歪みの前のコンプ（入れた後の大きさで dB）
        // にじみ（オールパス）
        float diffAmt;  // 0..1（0 = なし）
        float diffSize; // ms（いちばん長い段）
        float diffPre;  // ms（にじむ分だけ前に出す）
    };

    // ---------------- 遅延線（32 点の窓付き sinc 補間。高音が鈍らない） ----------------
    struct SincTable
    {
        static constexpr int taps = 32, phases = 512;
        float t[(phases + 1) * taps];
        SincTable()
        {
            auto bessel0 = [] (double x) { double s = 1, u = 1; for (int k = 1; k < 30; ++k) { u *= (x / (2 * k)) * (x / (2 * k)); s += u; } return s; };
            const double beta = 9.0, i0b = bessel0 (beta);
            for (int p = 0; p <= phases; ++p)
            {
                const double frac = (double) p / phases;
                double sum = 0;
                for (int k = 0; k < taps; ++k)
                {
                    // 読む位置 = 整数 + frac。タップ k は (k − 7) − frac だけ離れた所
                    const double x = (k - (taps / 2 - 1)) - frac;
                    const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                    const double r = x / (taps / 2.0);
                    const double w = std::abs (r) >= 1 ? 0.0 : bessel0 (beta * std::sqrt (1 - r * r)) / i0b;
                    t[p * taps + k] = (float) (sinc * w); sum += sinc * w;
                }
                for (int k = 0; k < taps; ++k) t[p * taps + k] = (float) (t[p * taps + k] / sum);
            }
        }
        static const SincTable& get() { static const SincTable s; return s; }
    };

    struct DelayLine
    {
        std::vector<float> buf;   // 終わりに taps 個の写しを置いて、折り返しの計算を省く
        int size = 0, w = 0;
        static constexpr int minDelay = SincTable::taps / 2 + 1;
        void prepare (int maxSamples)
        {
            size = maxSamples + 64;
            buf.assign ((size_t) (size + SincTable::taps), 0.0f); w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void write (float x)
        {
            buf[(size_t) w] = x;
            if (w < SincTable::taps) buf[(size_t) (size + w)] = x;
            if (++w >= size) w = 0;
        }
        // 整数の遅れ（1 以上）をそのまま読む（にじみ用）
        inline float readInt (int d) const
        {
            int i = w - std::max (1, std::min (d, size - 1));
            if (i < 0) i += size;
            return buf[(size_t) i];
        }
        // d = 何サンプル前（直前に書いた値が d = 1）。最小は minDelay
        inline float read (double d) const
        {
            d = std::max ((double) minDelay, std::min (d, (double) (size - SincTable::taps - 2)));
            const double pos = (double) w - d;              // 読む位置（小数）
            double fl = std::floor (pos);
            const float frac = (float) (pos - fl);
            int i0 = (int) fl - (SincTable::taps / 2 - 1);  // いちばん古いタップ
            i0 %= size; if (i0 < 0) i0 += size;
            const auto& T = SincTable::get();
            const int ph = (int) (frac * SincTable::phases + 0.5f);
            const float* c = T.t + ph * SincTable::taps;
            const float* x = buf.data() + i0;
            float s = 0;
            for (int k = 0; k < SincTable::taps; ++k) s += c[k] * x[k];
            return s;
        }
    };

    // ---------------- 4 倍オーバーサンプリング（FIR のハーフバンド 2 段） ----------------
    struct Halfband
    {
        std::vector<float> h;   // 長さ N = 4k+3
        int N = 0;
        void design (int n, double beta)
        {
            N = n; h.assign ((size_t) n, 0.0f);
            const int c = n / 2;
            auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
            const double i0b = bessel0 (beta);
            for (int i = 0; i < n; ++i)
            {
                const int m = i - c;
                const double sinc = m == 0 ? 0.5 : std::sin (kPi * m / 2) / (kPi * m);
                const double r = (double) m / c;
                h[(size_t) i] = (float) (sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b);
            }
            double s = 0; for (auto v : h) s += v;
            for (auto& v : h) v = (float) (v / s);
        }
    };
    struct Up2
    {
        const Halfband* f = nullptr; std::vector<float> x; int pos = 0, L = 0;
        void prepare (const Halfband& hb) { f = &hb; L = (hb.N + 1) / 2; x.assign ((size_t) (2 * L), 0.0f); pos = 0; }
        void reset() { std::fill (x.begin(), x.end(), 0.0f); pos = 0; }
        inline void process (float in, float& o0, float& o1)
        {
            pos = (pos == 0 ? L : pos) - 1;
            x[(size_t) pos] = x[(size_t) (pos + L)] = in;
            const float* hp = f->h.data(); const float* xp = x.data() + pos;
            float s0 = 0, s1 = 0;
            for (int j = 0; j < L; ++j) { s0 += hp[2 * j] * xp[j]; if (2 * j + 1 < f->N) s1 += hp[2 * j + 1] * xp[j]; }
            o0 = 2 * s0; o1 = 2 * s1;
        }
    };
    struct Down2
    {
        const Halfband* f = nullptr; std::vector<float> x; int pos = 0, N = 0;
        void prepare (const Halfband& hb) { f = &hb; N = hb.N; x.assign ((size_t) (2 * N), 0.0f); pos = 0; }
        void reset() { std::fill (x.begin(), x.end(), 0.0f); pos = 0; }
        inline void push (float v) { pos = (pos == 0 ? N : pos) - 1; x[(size_t) pos] = x[(size_t) (pos + N)] = v; }
        inline float process (float a, float b)
        {
            push (a); push (b);
            const float* hp = f->h.data(); const float* xp = x.data() + pos;
            float s = 0; for (int i = 0; i < N; ++i) s += hp[i] * xp[i];
            return s;
        }
    };

    // ---------------- 歪み ----------------
    // y = (f(x + b) − f(b)) / f'(b)、f(u) = u / (1 + |u|^k)^(1/k)。小さい音では倍率 1
    struct Shaper
    {
        float k = 2, bias = 0, y0 = 0, invSlope = 1;
        void set (float kk, float b)
        {
            k = std::max (0.6f, kk); bias = b;
            y0 = f (b);
            invSlope = std::pow (1.0f + std::pow (std::abs (b), k), 1.0f / k + 1.0f);
        }
        inline float f (float u) const { return u / std::pow (1.0f + std::pow (std::abs (u), k), 1.0f / k); }
        inline float operator() (float x) const { return (f (x + bias) - y0) * invSlope; }
    };

    struct Saturator
    {
        static constexpr int n1 = 95, n2 = 19;
        Up2 u1, u2; Down2 d1, d2;
        Shaper sh;
        float drive = 1, makeup = 1;
        float thr = 1e9f, slope = 0, env = 0, aAtt = 0.5f, aRel = 0.999f;
        void prepare (const Halfband& h1, const Halfband& h2, double fs)
        {
            u1.prepare (h1); d1.prepare (h1); u2.prepare (h2); d2.prepare (h2);
            aAtt = 1.0f - std::exp (-1.0f / (0.002f * (float) fs));
            aRel = std::exp (-1.0f / (0.15f * (float) fs));
        }
        void reset() { u1.reset(); u2.reset(); d1.reset(); d2.reset(); env = 0; }
        void set (float driveDb, float makeupDb, float k, float bias, float thrDb, float ratio)
        {
            drive = std::pow (10.0f, driveDb / 20.0f); makeup = std::pow (10.0f, makeupDb / 20.0f);
            sh.set (k, bias);
            thr = thrDb; slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        }
        // 遅れ（元のレートのサンプル）
        static double latency() { return (n1 - 1) / 2.0 + (n2 - 1) / 4.0; }
        inline float process (float x)
        {
            x *= drive;
            // コンプ（ピーク、6 dB のニー）
            const float ax = std::abs (x);
            env = ax > env ? env + (ax - env) * aAtt : env * aRel;
            if (slope > 0)
            {
                const float over = 20.0f * std::log10 (env + 1e-9f) - thr;
                float gr = 0;
                if (over >= 3.0f) gr = slope * over;
                else if (over > -3.0f) gr = slope * (over + 3.0f) * (over + 3.0f) / 12.0f;
                x *= std::pow (10.0f, -gr / 20.0f);
            }
            float a, b, c0, c1, c2, c3;
            u1.process (x, a, b);
            u2.process (a, c0, c1); u2.process (b, c2, c3);   // 2 段目は 1 本に順番に通す
            c0 = sh (c0); c1 = sh (c1); c2 = sh (c2); c3 = sh (c3);
            const float da = d2.process (c0, c1), db = d2.process (c2, c3);
            return d1.process (da, db) * makeup;
        }
    };

    // ---------------- にじみ（オールパス 4 段） ----------------
    struct Diffuser
    {
        DelayLine d[4];
        int len[4] {}; float g = 0;
        bool on = false;
        void prepare (double fs) { for (auto& x : d) x.prepare ((int) (fs * 0.2)); }
        void reset() { for (auto& x : d) x.reset(); }
        void set (float amt, float sizeMs, double fs, int seed)
        {
            on = amt > 0.001f;
            g = 0.75f * std::min (1.0f, amt);
            static const float r[2][4] = { { 1.0f, 0.68f, 0.47f, 0.31f }, { 0.93f, 0.73f, 0.43f, 0.29f } };
            for (int i = 0; i < 4; ++i) len[i] = std::max (1, (int) (sizeMs * 0.001 * fs * r[seed & 1][i]));
        }
        inline float process (float x)
        {
            if (! on) return x;
            for (int i = 0; i < 4; ++i)
            {
                const float z = d[i].readInt (len[i]);
                const float v = x + g * z;
                d[i].write (v);
                x = z - g * v;
            }
            return x;
        }
        // 平均の遅れ（ms。前に出す量の目安）
    };

    // ---------------- 揺れ ----------------
    struct Rng
    {
        uint32_t s = 1;
        inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f; }
        inline float gauss() { float a = 0; for (int i = 0; i < 4; ++i) a += next(); return (a - 2.0f) * 1.732f; }
    };

    struct ModGen
    {
        // 状態（左右）
        double phase = 0;
        float sqSign[2] { 1, -1 }, sqLeft[2] { 0, 0 }, sqOut[2] { 0, 0 };
        float fl1[2] {}, fl2[2] {};
        double off[2] {};   // 読み出しのずれ（サンプル、＋＝遅く）
        Rng rng[2];
        bool started = false;
        void reset()
        {
            phase = 0; sqSign[0] = 1; sqSign[1] = -1; sqLeft[0] = sqLeft[1] = 0; sqOut[0] = sqOut[1] = 0;
            fl1[0] = fl1[1] = fl2[0] = fl2[1] = 0; off[0] = off[1] = 0;
            rng[0].s = 0x12345u; rng[1].s = 0x9e3779u;
            started = false;
        }
        // 1 サンプル進めて、左右の読み出しのずれ（サンプル）を返す
        inline void tick (const Profile& p, double fs, float depth, double& oL, double& oR)
        {
            float cents[2] { 0, 0 };
            if (! started)
            {
                // 別々に揺れるときも、右は少し遅れて同じ向きから始める（左右がいきなり逆向きにならないように）
                started = true;
                // 最初は半分の長さ → 読み出し位置は 0 を中心に三角に動く（平均の遅れがずれない）
                sqSign[0] = sqSign[1] = 1; sqLeft[0] = sqLeft[1] = 0.5f * p.sqInterval;
                phase = 0.25;   // 正弦も、位置のずれが 0 を中心に動く位相から
            }
            if (p.sineDepth > 0)
            {
                phase += p.sineRate / fs; if (phase >= 1) phase -= 1;
                cents[0] += p.sineDepth * (float) std::sin (2 * kPi * phase);
                cents[1] += p.sineDepth * (float) std::sin (2 * kPi * phase + p.sinePhaseR * kPi / 180.0);
            }
            if (p.sqAmp > 0)
            {
                const int nCh = p.sqRMode == 2 ? 2 : 1;
                const float a = 1.0f - std::exp (-1.0f / (float) (std::max (0.0005f, p.sqSmooth) * fs));
                for (int c = 0; c < nCh; ++c)
                {
                    sqLeft[c] -= 1.0f / (float) fs;
                    if (sqLeft[c] <= 0)
                    {
                        sqSign[c] = -sqSign[c];
                        const float j = p.sqJitter * (rng[c].next() * 2 - 1);
                        sqLeft[c] += std::max (0.01f, p.sqInterval * (1 + j));
                    }
                    sqOut[c] += (sqSign[c] * p.sqAmp - sqOut[c]) * a;
                }
                cents[0] += sqOut[0];
                cents[1] += p.sqRMode == 0 ? sqOut[0] * p.sqRAmp : p.sqRMode == 1 ? -sqOut[0] * p.sqRAmp : sqOut[1] * p.sqRAmp;
            }
            if (p.flAmp > 0)
            {
                const float a = 1.0f - std::exp (-2.0f * (float) kPi * p.flRate / (float) fs);
                // 2 段の 1 次ローパスのノイズ。rms がおよそ flAmp になるように倍率をかける
                const float scale = p.flAmp * std::sqrt (2.0f / std::max (1e-6f, a)) * 0.9f;
                for (int c = 0; c < 2; ++c)
                {
                    if (c == 1 && p.flSame) { cents[1] += fl2[0] * scale; break; }
                    fl1[c] += (rng[c].gauss() - fl1[c]) * a;
                    fl2[c] += (fl1[c] - fl2[c]) * a;
                    cents[c] += fl2[c] * scale;
                }
            }
            const double leak = 1.0 - 1.0 / (30.0 * fs);
            for (int c = 0; c < 2; ++c)
            {
                // 音程 = 1 − 読み出し位置の変化 → 位置の変化 = 1 − 2^(cents/1200)
                off[c] = off[c] * leak + (1.0 - std::exp2 ((double) (cents[c] * depth) / 1200.0));
            }
            oL = off[0]; oR = off[1];
        }
    };

    // ---------------- ループ 1 回分の処理 P ----------------
    struct Pass
    {
        Saturator sat; Diffuser diff; Chain loop; Biquad hiCut;
        void prepare (double fs, const Halfband& h1, const Halfband& h2) { sat.prepare (h1, h2, fs); diff.prepare (fs); }
        void reset() { sat.reset(); diff.reset(); loop.reset(); hiCut.reset(); }
        inline float process (float x)
        {
            x = sat.process (x);
            x = diff.process (x);
            x = loop.process (x);
            x = hiCut.process (x);
            // くり返しが 100% を超えても、約 +6 dBFS で止める（測った参考と同じ天井）
            const float a = std::abs (x);
            if (a > 1.5f) x = std::copysign (1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f), x);
            return x;
        }
    };

    // ---------------- つまみ ----------------
    struct Params
    {
        int mode = 0;            // 0 Single 1 Dual 2 Ping-Pong 3 Rhythm
        double time1Ms = 250, time2Ms = 375;
        float feedback = 0.3f;   // 0..1.25
        float mix = 0.5f;        // 0..1
        float inputDb = 0, outputDb = 0;
        float saturationDb = 0;  // 0..24
        float lowCut = 0, highCut = 0;   // 0..1
        float groove = 0;        // -0.25..0.25（奇数回目を T の何割ずらすか）
        double feelMs = 0;       // 全部のエコーをずらす（ms）
        bool prime = false;
        int style = 1;
        double rhythmMs = 50; int repeats = 8; float decayDb = 0; double loopBeatsMs = 1000;
        float wobble = 1.0f;     // 揺れの量（1 = スタイルのまま）
    };

    // LOW CUT = 1 次ハイパス 2 段（出口）、HIGH CUT = 1 次ローパス（ループ内）。測った特性に当てはめた表
    inline void lowCutHz (float v, double& fa, double& fb)
    {
        static const double xs[6] = { 0.0, 0.1, 0.25, 0.5, 0.75, 1.0 };
        static const double a[6] = { 1.0, 1.0, 14.5, 95.4, 294.6, 546.7 }, b[6] = { 20.0, 88.3, 367.1, 873.1, 1175.9, 1542.0 };
        v = std::max (0.0f, std::min (1.0f, v));
        int i = 0; while (i < 4 && v > xs[i + 1]) ++i;
        const double t = (v - xs[i]) / (xs[i + 1] - xs[i]);
        fa = std::exp (std::log (a[i]) + t * (std::log (a[i + 1]) - std::log (a[i])));
        fb = std::exp (std::log (b[i]) + t * (std::log (b[i + 1]) - std::log (b[i])));
    }
    inline double highCutHz (float v) { return v <= 0.0f ? 0.0 : 24000.0 * std::exp (-1.22 * (double) v); }

    inline bool isPrime (int n) { if (n < 2) return false; for (int d = 2; d * d <= n; ++d) if (n % d == 0) return false; return true; }
    inline double primeMs (double ms)
    {
        if (ms < 2) return ms;
        const int c = (int) std::lround (ms);
        for (int k = 0; k < 2000; ++k) { if (isPrime (c - k)) return c - k; if (isPrime (c + k)) return c + k; }
        return ms;
    }
}

#include "Profiles.h"

namespace echo
{
    class Engine
    {
    public:
        static constexpr double maxDelaySec = 32.0;

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            h1.design (Saturator::n1, 10.0); h2.design (Saturator::n2, 8.0);
            const int maxS = (int) (fs * maxDelaySec) + 1024;
            for (int c = 0; c < 2; ++c)
            {
                A[c].prepare (maxS); B[c].prepare (maxS);
                pa[c].prepare (fs, h1, h2); pb[c].prepare (fs, h1, h2);
            }
            dryDelay.clear();
            configured = false;
            reset();
        }
        void reset()
        {
            for (int c = 0; c < 2; ++c)
            {
                A[c].reset(); B[c].reset(); pa[c].reset(); pb[c].reset(); color[c].reset(); lowCut[c].reset(); lowCut2[c].reset();
                tSm[c] = -1; tSm2[c] = -1;
            }
            mod.reset();
            rhythmSm = -1;
        }

        void set (const Params& p)
        {
            const bool styleChanged = ! configured || p.style != q.style;
            const bool filtChanged = styleChanged || p.highCut != q.highCut || p.lowCut != q.lowCut || p.saturationDb != q.saturationDb;
            q = p;
            prof = &profiles()[std::max (0, std::min (numProfiles() - 1, p.style))];
            if (filtChanged)
            {
                double lcA, lcB; lowCutHz (p.lowCut, lcA, lcB);
                const double hc = highCutHz (p.highCut);
                for (int c = 0; c < 2; ++c)
                {
                    color[c].set (prof->color, prof->nColor, prof->colorGain, fs);
                    if (p.lowCut > 0) { lowCut[c].design ({ 5, (float) lcA, 0.7f, 0 }, fs); lowCut2[c].design ({ 5, (float) lcB, 0.7f, 0 }, fs); }
                    else { lowCut[c].design ({ -1, 0, 0, 0 }, fs); lowCut2[c].design ({ -1, 0, 0, 0 }, fs); }
                    for (Pass* ps : { &pa[c], &pb[c] })
                    {
                        ps->loop.set (prof->loop, prof->nLoop, prof->loopGain, fs);
                        if (hc > 0) ps->hiCut.design ({ 6, (float) std::min (hc, fs * 0.45), 0.7f, 0 }, fs); else ps->hiCut.design ({ -1, 0, 0, 0 }, fs);
                        const float sd = std::max (0.0f, std::min (24.0f, p.saturationDb));
                        const int si = std::min (3, (int) (sd / 6.0f));
                        const float t = sd / 6.0f - (float) si;
                        const float dIn = prof->satIn[si] + t * (prof->satIn[si + 1] - prof->satIn[si]);
                        const float dOut = prof->satOut[si] + t * (prof->satOut[si + 1] - prof->satOut[si]);
                        ps->sat.set (dIn, dOut, prof->hard, prof->bias, prof->compThr, prof->compRatio);
                        ps->diff.set (prof->diffAmt, prof->diffSize, fs, c);
                    }
                }
                if (styleChanged && configured) { for (int c = 0; c < 2; ++c) { pa[c].diff.reset(); pb[c].diff.reset(); } }
            }
            configured = true;
            inGain = std::pow (10.0f, p.inputDb / 20.0f);
            outGain = std::pow (10.0f, p.outputDb / 20.0f);
            const float m = std::max (0.0f, std::min (1.0f, p.mix));
            dryG = std::cos (m * (float) kPi * 0.5f); wetG = std::sin (m * (float) kPi * 0.5f);
            if (m <= 0.0f) wetG = 0.0f;
            if (m >= 1.0f) dryG = 0.0f;
            // Rhythm の音量（くり返しの数と減り方から。全体の大きさがだいたい同じになるように）
            const int n = std::max (1, std::min (16, p.repeats));
            double s2 = 0;
            for (int k = 0; k < n; ++k)
            {
                rg[k] = (float) std::pow (10.0, -(n > 1 ? p.decayDb * k / (n - 1) : 0.0) / 20.0);
                s2 += (double) rg[k] * rg[k];
            }
            const float norm = (float) std::min (1.0, std::sqrt (2.0 / s2));
            for (int k = 0; k < n; ++k) rg[k] *= norm;
            nRep = n;
        }
        void setDuck (float g) { duck = g; }
        const Params& get() const { return q; }
        const Profile& profile() const { return *prof; }

        // その場で処理（L, R）
        void process (float* L, float* R, int n)
        {
            if (! configured) set (q);
            const double lat = Saturator::latency();
            const double t1 = std::max (0.0, q.prime ? primeMs (q.time1Ms) : q.time1Ms) * 0.001 * fs;
            const double t2 = std::max (0.0, q.prime ? primeMs (q.time2Ms) : q.time2Ms) * 0.001 * fs;
            const double feel = q.feelMs * 0.001 * fs;
            const double ms = 0.001 * fs;
            const Profile& P = *prof;
            const float fb = std::max (0.0f, std::min (1.25f, q.feedback));
            const double maxD = maxDelaySec * fs - 8;
            // 時間はなめらかに追う（テープのように音程が動く。約 0.12 s）
            const double aT = 1.0 - std::exp (-1.0 / (0.12 * fs));
            for (int c = 0; c < 2; ++c)
            {
                const double tgt = (q.mode == 1 && c == 1) ? t2 : t1;
                if (tSm[c] < 0) tSm[c] = tgt;
                if (tSm2[c] < 0) tSm2[c] = t2;
            }
            if (rhythmSm < 0) rhythmSm = q.rhythmMs * ms;

            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i] * inGain, inR = R[i] * inGain;
                double mL, mR;
                mod.tick (P, fs, q.wobble, mL, mR);
                float wet[2] { 0, 0 };
                if (q.mode == 0 || q.mode == 1)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const double tgt = (q.mode == 1 && c == 1) ? t2 : t1;
                        tSm[c] += (tgt - tSm[c]) * aT;
                        const double T = tSm[c], md = c == 0 ? mL : mR;
                        const double oOdd = P.off[c * 2] * ms + q.groove * T, oEven = P.off[c * 2 + 1] * ms;
                        const double base = T - lat + md;
                        const float aEnd = A[c].read (clampD (base, maxD));
                        const float bEnd = B[c].read (clampD (base, maxD));
                        const float tapA = A[c].read (clampD (base + oOdd + feel, maxD));
                        const float tapB = B[c].read (clampD (base + oEven + feel, maxD));
                        const float in = c == 0 ? inL : inR;
                        A[c].write (pa[c].process (in + fb * bEnd));
                        B[c].write (pb[c].process (fb * aEnd));
                        wet[c] = tapA + tapB;
                    }
                }
                else if (q.mode == 2)
                {
                    // Ping-Pong: 左右を足して 1 本。A（ECHO 1）→ 左、B（ECHO 2）→ 右、1 周で FEEDBACK 1 回
                    tSm[0] += (t1 - tSm[0]) * aT; tSm2[0] += (t2 - tSm2[0]) * aT;
                    const float mono = (inL + inR) * 0.70710678f;
                    const double a = tSm[0] - lat + mL, b = tSm2[0] - lat + mR;
                    const float aEnd = A[0].read (clampD (a, maxD));
                    const float bEnd = B[0].read (clampD (b, maxD));
                    const float tapA = A[0].read (clampD (a + feel, maxD));
                    const float tapB = B[0].read (clampD (b + feel, maxD));
                    A[0].write (pa[0].process (mono + fb * bEnd));
                    B[0].write (pb[0].process (aEnd));
                    wet[0] = tapA; wet[1] = tapB;
                }
                else
                {
                    // Rhythm: 線 A に「入力 + くり返し」を書き、RHYTHM の間隔で REPEATS 個読む。全体は 2 拍ごとにくり返す
                    rhythmSm += (q.rhythmMs * ms - rhythmSm) * aT;
                    const double loopLen = std::max (1.0, q.loopBeatsMs * ms);
                    for (int c = 0; c < 2; ++c)
                    {
                        const double md = c == 0 ? mL : mR;
                        const float back = fb * A[c].read (clampD (loopLen - lat + md, maxD));
                        const float in = c == 0 ? inL : inR;
                        A[c].write (pa[c].process (in + back));
                        float s = rg[0] * back;
                        for (int k = 1; k < nRep; ++k) s += rg[k] * A[c].read (clampD (k * rhythmSm - lat + md + feel, maxD));
                        wet[c] = s;
                    }
                }
                for (int c = 0; c < 2; ++c)
                {
                    float w = color[c].process (wet[c]);
                    w = lowCut2[c].process (lowCut[c].process (w));
                    wet[c] = w * duck;
                }
                L[i] = (inL * dryG + wet[0] * wetG) * outGain;
                R[i] = (inR * dryG + wet[1] * wetG) * outGain;
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { reset(); L[i] = R[i] = 0; }
            }
        }

        double colorMagDb (double f) const { return color[0].magDb (f, fs); }
        double loopMagDb (double f) const { return pa[0].loop.magDb (f, fs) + 10 * std::log10 (pa[0].hiCut.magSq (f, fs) + 1e-30); }
        double sampleRate() const { return fs; }

    private:
        static inline double clampD (double d, double maxD) { return std::max (1.0, std::min (d, maxD)); }
        double fs = 48000;
        Params q;
        const Profile* prof = nullptr;
        bool configured = false;
        Halfband h1, h2;
        DelayLine A[2], B[2];
        Pass pa[2], pb[2];
        Chain color[2];
        Biquad lowCut[2], lowCut2[2];
        ModGen mod;
        double tSm[2] { -1, -1 }, tSm2[2] { -1, -1 }, rhythmSm = -1;
        float inGain = 1, outGain = 1, dryG = 0.7f, wetG = 0.7f, duck = 1.0f;
        float rg[16] {}; int nRep = 8;
        std::vector<float> dryDelay;
    };
}
