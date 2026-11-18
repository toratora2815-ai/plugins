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
#include <array>

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
        int sqRMode; float sqRAmp;                      // 右: 0 同じ 1 逆 2 別々 3 同じ動きを sqRLag 秒遅れて（sqRAmp = 右の高さの倍率）
        float flAmp, flRate; int flSame;                // ランダムなフラッター（セント rms・帯域 flRate/4〜flRate Hz・左右同じなら 1）
        // 歪み: SATURATION 0 / 6 / 12 / 18 / 24 dB のときの、入れる倍率と戻す倍率（dB）。間は直線で結ぶ
        float satIn[5], satOut[5];
        float hard;     // 曲がり方の硬さ（y = x / (1 + |x|^k)^(1/k) の k）
        float bias;     // 偶数次（非対称）
        float compThr, compRatio;   // 歪みの前のコンプ（入れた後の大きさで dB）
        // にじみ（オールパス）
        // にじみ（出口で 1 回だけ。左右別のオールパス 4 段: 係数と長さ ms。係数がすべて 0 ならなし）
        float diffG[2][4], diffLen[2][4];
        float minMs;     // これより短いエコーにはならない（0 = 制限なし）
        float passTilt;  // 奇数回目に入る通り道（A）と偶数回目（B）の音量差 dB（A − B、split の残りの微調整）
        BiqSpec split[3]; // A と B の音色の差（A − B）。A に +半分、B に −半分（棚・ピークは逆の dB でちょうど打ち消すので A×B は変わらない）
        // 歪みの前に掛けて後で戻す EQ（周波数ごとの歪みやすさ）: 180 Hz ローシェルフ・900 Hz / 3.5 kHz ハイシェルフの dB、全体 dB
        float emph[4];
        float compAtt, compRel;   // 歪みの前のコンプの速さ（ms）
        int ppLoopDiff;           // Ping-Pong ではにじみをループの中に入れる（往復ごとに広がる。Verbed 以外のにじむスタイル）
        float sqRLag;             // sqRMode 3 の遅れ（秒）
        int modOut;               // 1 = 揺れは出口で 1 回だけ（Vibrato）。0 = ループの中（B から A へ戻る所と出口のタップ）
        float ceilDb;             // くり返しが 100% を超えたときの天井（dBFS 相当。参考の FEEDBACK 125% に合わせた値）
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
        // 入れる倍率・戻す倍率（SATURATION で変わる）は、目標へ 20 ms で追う（1.3.2。一気に切り替えるとプチッと鳴り、
        // 入れる所と戻す所が 4 倍のフィルターの遅れ（約 1 ms）だけずれて倍率が跳ねていた）。丸めでもう進まないなら目標ちょうど
        float drive = 1, makeup = 1, driveT = 1, makeupT = 1, aSm = 1;
        bool firstSet = true;
        float thr = 1e9f, slope = 0, env = 0, aAtt = 0.5f, aRel = 0.999f;
        double rate = 48000;
        void prepare (const Halfband& h1, const Halfband& h2, double fs)
        {
            u1.prepare (h1); d1.prepare (h1); u2.prepare (h2); d2.prepare (h2);
            rate = fs; setTimes (2.0f, 150.0f);
            aSm = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        }
        void setTimes (float attMs, float relMs)
        {
            aAtt = 1.0f - std::exp (-1.0f / (std::max (0.05f, attMs) * 0.001f * (float) rate));
            aRel = std::exp (-1.0f / (std::max (0.5f, relMs) * 0.001f * (float) rate));
        }
        void reset() { u1.reset(); u2.reset(); d1.reset(); d2.reset(); env = 0; drive = driveT; makeup = makeupT; }
        void set (float driveDb, float makeupDb, float k, float bias, float thrDb, float ratio)
        {
            driveT = std::pow (10.0f, driveDb / 20.0f); makeupT = std::pow (10.0f, makeupDb / 20.0f);
            if (firstSet) { drive = driveT; makeup = makeupT; firstSet = false; }
            sh.set (k, bias);
            thr = thrDb; slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        }
        bool settled() const { return drive == driveT && makeup == makeupT; }
        void snap() { drive = driveT; makeup = makeupT; }   // 使っていない間（Ping-Pong・Rhythm の pa[1]・pb）は目標にそろえておく
        static inline void glide (float& v, float t, float a)
        {
            const float nv = v + (t - v) * a;
            v = (nv == v || std::abs (t - nv) < 1.0e-7f * std::max (1.0f, std::abs (t))) ? t : nv;
        }
        // 遅れ（元のレートのサンプル）
        static double latency() { return (n1 - 1) / 2.0 + (n2 - 1) / 4.0; }
        inline float process (float x)
        {
            if (! settled()) { glide (drive, driveT, aSm); glide (makeup, makeupT, aSm); }
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
        float gs[4] {};
        void set (const float* gain, const float* lenMs, double fs)
        {
            on = false;
            for (int i = 0; i < 4; ++i)
            {
                gs[i] = gain[i]; on = on || gain[i] > 0.0005f;
                len[i] = std::max (1, (int) std::lround (lenMs[i] * 0.001 * fs));
            }
        }
        inline float process (float x)
        {
            if (! on) return x;
            for (int i = 0; i < 4; ++i)
            {
                const float z = d[i].readInt (len[i]);
                const float v = x + gs[i] * z;
                d[i].write (v);
                x = z - gs[i] * v;
            }
            return x;
        }
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
        float fl1[2] {}, fl2[2] {}, fl3[2] {};
        double off[2] {};   // 読み出しのずれ（サンプル、＋＝遅く）
        std::vector<float> hist; int histPos = 0;   // sqRMode 3 用（左の四角の過去）
        bool histFresh = true;   // まだ一度も使っていない（最初に使うとき、今の左の値で埋める）
        Rng rng[2];
        bool started = false;
        // 音の処理の中でメモリを確保しないよう、先に取っておく（1.3.2。Digital Chorus を最初に選んだときに 48 kHz で約 115 KB 確保していた）
        void prepareHist (int maxNeed) { hist.assign ((size_t) std::max (1, maxNeed), 0.0f); histPos = 0; histFresh = true; }
        void reset()
        {
            phase = 0; sqSign[0] = 1; sqSign[1] = -1; sqLeft[0] = sqLeft[1] = 0; sqOut[0] = sqOut[1] = 0;
            fl1[0] = fl1[1] = fl2[0] = fl2[1] = fl3[0] = fl3[1] = 0; off[0] = off[1] = 0;
            std::fill (hist.begin(), hist.end(), 0.0f); histPos = 0;
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
                if (p.sqRMode == 3)
                {
                    const int need = (int) (p.sqRLag * fs) + 2;
                    if ((int) hist.size() < need) { hist.assign ((size_t) need, sqOut[0]); histPos = 0; histFresh = false; }   // 合わせ込み用の差し替えスタイルだけ（本体は prepareHist で足りる）
                    else if (histFresh) { std::fill (hist.begin(), hist.end(), sqOut[0]); histPos = 0; histFresh = false; }
                    const int lag = need - 1;
                    hist[(size_t) histPos] = sqOut[0];
                    const int rp = (histPos - lag + (int) hist.size()) % (int) hist.size();
                    cents[1] += hist[(size_t) rp] * p.sqRAmp;
                    histPos = (histPos + 1) % (int) hist.size();
                }
                else
                    cents[1] += p.sqRMode == 0 ? sqOut[0] * p.sqRAmp : p.sqRMode == 1 ? -sqOut[0] * p.sqRAmp : sqOut[1] * p.sqRAmp;
            }
            if (p.flAmp > 0)
            {
                const float a = 1.0f - std::exp (-2.0f * (float) kPi * p.flRate / (float) fs);
                const float b = 1.0f - std::exp (-2.0f * (float) kPi * 0.25f * p.flRate / (float) fs);
                // 2 段の 1 次ローパスのノイズから、その 1/4 の速さの成分を引いた帯域のノイズ（参考はゆっくりした成分がない）。
                // rms がおよそ flAmp になるように倍率をかける（2.1 は測って合わせた値）
                const float scale = p.flAmp * std::sqrt (2.0f / std::max (1e-6f, a)) * 0.9f * 2.1f;
                for (int c = 0; c < 2; ++c)
                {
                    if (c == 1 && p.flSame) { cents[1] += (fl2[0] - fl3[0]) * scale; break; }
                    fl1[c] += (rng[c].gauss() - fl1[c]) * a;
                    fl2[c] += (fl1[c] - fl2[c]) * a;
                    fl3[c] += (fl2[c] - fl3[c]) * b;
                    cents[c] += (fl2[c] - fl3[c]) * scale;
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
        Saturator sat; Chain loop; Chain split; Biquad hiCut; Chain pre, post;
        Chain guard;      // 48 kHz 以外で、ループ利得が 48 kHz のときより上がる所だけ下げる（Engine::makeGuard。48 kHz では何もしない）
        Chain guardSub;   // 超低域の山を削る（FEEDBACK 100% 以下のときだけ。今は Verbed だけ。Engine::addSubBassCut）
        float ceil = 2.0f, knee = 1.5f;
        void setCeiling (float db) { ceil = std::pow (10.0f, db / 20.0f); knee = 0.75f * ceil; }
        void prepare (double fs, const Halfband& h1, const Halfband& h2) { sat.prepare (h1, h2, fs); }
        void reset() { sat.reset(); loop.reset(); split.reset(); hiCut.reset(); pre.reset(); post.reset(); guard.reset(); guardSub.reset(); }
        void setEmphasis (const float* e, double fs)
        {
            const BiqSpec a[3] = { { 3, 180.0f, 0.7f, e[0] }, { 4, 900.0f, 0.7f, e[1] }, { 4, 3500.0f, 0.7f, e[2] } };
            const BiqSpec b[3] = { { 3, 180.0f, 0.7f, -e[0] }, { 4, 900.0f, 0.7f, -e[1] }, { 4, 3500.0f, 0.7f, -e[2] } };
            pre.set (a, 3, e[3], fs); post.set (b, 3, -e[3], fs);
        }
        inline float process (float x)
        {
            x = post.process (sat.process (pre.process (x)));
            x = split.process (loop.process (x));
            x = hiCut.process (x);
            x = guard.process (x);
            x = guardSub.process (x);
            // くり返しが 100% を超えても、スタイルごとの天井で止める（参考の FEEDBACK 125% に合わせた高さ）。天井の 3/4 から丸める
            const float a = std::abs (x);
            if (a > knee) x = std::copysign (knee + (ceil - knee) * std::tanh ((a - knee) / (ceil - knee)), x);
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
        // 音符・テンポで決まる時間か（そのときは追いかけず、20 ms のクロスフェードですぐ切り替える。参考と同じ）
        bool sync1 = false, sync2 = false, syncR = false;
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
                outDiff[c].prepare (fs);
                if (c == 0) ppDiff.prepare (fs);
                outMod[c].prepare ((int) (fs * 0.05));
            }
            dryDelay.clear();
            // ループのフィルターは 48 kHz で合わせてある。ほかのレートでは 20 kHz 近くの形が変わり（高すぎる周波数は 0.49·fs に寄せる）、
            // ループ利得が 1 を超えて FEEDBACK 100% 未満でも鳴りっぱなしになるスタイルがあった（1.3.2、DSP 検査係: 44.1 kHz の FM Radio は 80% から
            // +10 dBFS、96 / 192 kHz の Vibrato は 60% から）。48 kHz より上がる所だけ下げるガードを、スタイルごとにここで作る
            for (int s = 0; s < numProfiles() && s < (int) onePassA.size(); ++s) makeGuard (profiles()[s], fs, guardSpec[(size_t) s], guardN[(size_t) s], subN[(size_t) s]);
            for (int s = 0; s < numProfiles() && s < (int) onePassA.size(); ++s)
                onePassA[(size_t) s] = onePassAlpha (profiles()[s], fs, guardSpec[(size_t) s].data(), guardN[(size_t) s]);
            if (useCustom) customA = onePassAlpha (custom, fs, nullptr, 0);
            int histNeed = 1;
            for (int s = 0; s < numProfiles(); ++s)
                if (profiles()[s].sqRMode == 3) histNeed = std::max (histNeed, (int) (profiles()[s].sqRLag * fs) + 2);
            mod.prepareHist (histNeed);
            modeStep = (float) (1.0 / (0.01 * fs));   // モードを替えるときの下げ・上げ（10 ms ずつ）
            configured = false; smInit = false;
            reset();
        }
        void reset()
        {
            if (pending && configured) { Params np = q; np.mode = pendMode; np.style = pendStyle; pending = false; applySet (np); }
            pending = false; modeG = 1.0f; modeFade = 0; holdN = 0;
            quietN = (int64_t) 1 << 40;   // 線は空になった（次のモード・スタイルの切替は待たずにすぐ）
            for (int c = 0; c < 2; ++c)
            {
                A[c].reset(); B[c].reset(); pa[c].reset(); pb[c].reset(); color[c].reset(); colorSplit[c].reset(); lowCut[c].reset(); lowCut2[c].reset(); outDiff[c].reset();
                tSm[c] = TimeState(); tSm2[c] = TimeState();
            }
            mod.reset(); ppDiff.reset(); outMod[0].reset(); outMod[1].reset();
            rhythmSm = TimeState();
            snapSmooth();
        }

        // つまみを入れる（毎ブロック呼ばれる）。鳴っている途中でモード・スタイルを替えるときは、エコーの音と線に書く音を 10 ms で下げ切り、
        // 15 ms 待ってから替え、10 ms で戻す（1.3.2 でモード、1.3.3 でスタイルも。そのまま替えると、フィルターの係数と読み出しの形が一気に変わって
        // プチッと鳴った。スタイルは最大 +14 dB）。線の中も入力も無音なら待たずにすぐ替える（止めている間に替えて再生したとき、
        // 1 回目のエコーが欠けないように）。モード・スタイル以外のつまみはすぐ入れる
        void set (const Params& p)
        {
            const bool change = configured && (p.mode != q.mode || p.style != q.style);
            if (! change || quietN >= quietNeed)
            {
                if (pending) { pending = false; holdN = 0; if (modeFade < 0) modeFade = 1; }   // 下げている途中で元に戻した（か、静かになった）
                applySet (p);
                return;
            }
            pending = true; pendMode = p.mode; pendStyle = p.style;
            modeFade = -1;
            Params keep = p; keep.mode = q.mode; keep.style = q.style;
            applySet (keep);
        }
        void applySet (const Params& p)
        {
            const bool enterPP = configured && p.mode == 2 && q.mode != 2;   // Ping-Pong のにじみ（ループの中）は、入るときに空にする（前の Ping-Pong の残り）
            const bool styleChanged = ! configured || p.style != q.style;
            const bool filtChanged = styleChanged || p.mode != q.mode || p.highCut != q.highCut || p.lowCut != q.lowCut || p.saturationDb != q.saturationDb
                                     || (p.feedback <= 1.0f) != (q.feedback <= 1.0f);   // 超低域の削り（guardSub）は FEEDBACK 100% 以下のときだけ
            q = p;
            prof = useCustom ? &custom : &profiles()[std::max (0, std::min (numProfiles() - 1, p.style))];
            if (filtChanged)
            {
                double lcA, lcB; lowCutHz (p.lowCut, lcA, lcB);
                const double hc = highCutHz (p.highCut);
                const float sd = std::max (0.0f, std::min (24.0f, p.saturationDb));
                const int si = std::min (3, (int) (sd / 6.0f));
                const float t = sd / 6.0f - (float) si;
                const float dIn = prof->satIn[si] + t * (prof->satIn[si + 1] - prof->satIn[si]);
                const float dOut = prof->satOut[si] + t * (prof->satOut[si + 1] - prof->satOut[si]);
                // SATURATION で上がる小さい音の倍率（入れる dB + 戻す dB）は、1 回目のエコーにだけ掛ける。
                // 本物は、くり返し 1 回ごとの減り方が SATURATION で変わらない（2026-11-18 に 31 スタイル × 0〜24 dB で測って確認。
                // 1 回目のエコーだけ最大 +12 dB 大きくなる）。前は処理 P の中の倍率がくり返すたびに掛かり、FEEDBACK 40〜60% でも鳴り止まなかった。
                // P は今のまま（1 回目の音・歪み方は同じ）にして、ループへ戻す量を SATURATION 0 dB との差の分だけ変える
                // （P を k 倍・天井も k 倍にして出口で 1/k 倍に戻すのと同じ計算）。動かしている間は、歪みの今の倍率（20 ms で追う）から
                // 1 サンプルずつ計算する（process の fbL）。止まっていれば fbKT ちょうど（SATURATION 0 dB なら 1 ＝前の版と同じ音）
                fbKT = std::pow (10.0f, ((prof->satIn[0] + prof->satOut[0]) - (dIn + dOut)) / 20.0f);
                fbG0 = std::pow (10.0f, (prof->satIn[0] + prof->satOut[0]) / 20.0f);
                for (int c = 0; c < 2; ++c)
                {
                    color[c].set (prof->color, prof->nColor, prof->colorGain, fs);
                    if (p.lowCut > 0) { lowCut[c].design ({ 5, (float) lcA, 0.7f, 0 }, fs); lowCut2[c].design ({ 5, (float) lcB, 0.7f, 0 }, fs); }
                    else { lowCut[c].design ({ -1, 0, 0, 0 }, fs); lowCut2[c].design ({ -1, 0, 0, 0 }, fs); }
                    for (Pass* ps : { &pa[c], &pb[c] })
                    {
                        ps->loop.set (prof->loop, prof->nLoop, prof->loopGain, fs);
                        if (hc > 0) ps->hiCut.design ({ 6, (float) std::min (hc, fs * 0.45), 0.7f, 0 }, fs); else ps->hiCut.design ({ -1, 0, 0, 0 }, fs);
                        ps->sat.set (dIn, dOut, prof->hard, prof->bias, prof->compThr, prof->compRatio);
                        if (styleChanged) ps->sat.snap();   // スタイルを替えたら倍率は一気に（20 ms で追うのは SATURATION のつまみだけ。前のスタイルの倍率から追うと最初の音が強く歪んだ）
                        {
                            const int gs = std::max (0, std::min (numProfiles() - 1, p.style));
                            const int ns = useCustom ? 0 : subN[(size_t) gs], nAll = useCustom ? 0 : guardN[(size_t) gs];
                            // 削る量は FEEDBACK 100% 以下のときだけ。100% を超えたら 0 dB（形はそのままで素通し）にして、前の版と同じふくらみ方にする
                            // （山を削ったまま 125% にすると 40〜80 Hz も一緒に育ち、本物より 9 dB 大きくなった）
                            BiqSpec sub[6]; for (int k = 0; k < ns; ++k) { sub[k] = guardSpec[(size_t) gs][(size_t) k]; if (p.feedback > 1.0f) sub[k].g = 0.0f; }
                            ps->guardSub.set (sub, ns, 0.0f, fs);
                            ps->guard.set (guardSpec[(size_t) gs].data() + ns, nAll - ns, 0.0f, fs);
                        }
                        ps->sat.setTimes (compAttOverride > 0 ? compAttOverride : prof->compAtt, compRelOverride > 0 ? compRelOverride : prof->compRel);
                        ps->setEmphasis (prof->emph, fs);
                        ps->setCeiling (ceilOverride > -90 ? ceilOverride : prof->ceilDb);
                    }
                }
                for (int c = 0; c < 2; ++c)
                {
                    outDiff[c].set (prof->diffG[c], prof->diffLen[c], fs);
                    if (c == 0) { ppDiff.set (prof->diffG[0], prof->diffLen[0], fs); if (styleChanged && configured) ppDiff.reset(); }
                    if (styleChanged && configured) outDiff[c].reset();
                    // 奇数回目（A）と偶数回目（B）の音量差。1 回目は A を通るので、色で戻す。
                    // Ping-Pong・Rhythm は 1 周で pa しか通らないので、A の差（+a）が 1 周ごとに積み上がる。積み上がってループ利得が
                    // 差を入れないときより上がるスタイルだけ a を小さくする（onePassA、prepare で計算。1.3.2: Telephone は a = 0.5 だと
                    // 3 kHz で +8.8 dB になり、FEEDBACK 40% から鳴りっぱなしだった。本物は 60% でも消える）。ほかのスタイルは前と同じ 0.5
                    const float a = p.mode >= 2 ? (useCustom ? customA : onePassA[(size_t) std::max (0, std::min (numProfiles() - 1, p.style))]) : 0.5f;
                    pa[c].loop.set (prof->loop, prof->nLoop, prof->loopGain + a * prof->passTilt, fs);
                    pb[c].loop.set (prof->loop, prof->nLoop, prof->loopGain - 0.5f * prof->passTilt, fs);
                    color[c].set (prof->color, prof->nColor, prof->colorGain - a * prof->passTilt, fs);
                    BiqSpec half[3], neg[3], negA[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        half[k] = prof->split[k]; half[k].g *= a; neg[k] = prof->split[k]; neg[k].g *= -0.5f;
                        negA[k] = prof->split[k]; negA[k].g *= -a;
                    }
                    pa[c].split.set (half, 3, 0.0f, fs);
                    pb[c].split.set (neg, 3, 0.0f, fs);
                    colorSplit[c].set (negA, 3, 0.0f, fs);
                }
            }
            configured = true;
            if (enterPP) ppDiff.reset();
            ppDiffInLoop = p.mode == 2 && prof->ppLoopDiff != 0 && outDiff[0].on;
            outLat = prof->modOut ? std::ceil (0.004 * fs) : 0.0;   // ±0.4 ms の揺れが入る余裕
            // 音量（INPUT / OUTPUT / MIX）は目標だけ決め、process で 1 サンプルずつなめらかに追う（ブロックごとの段でジジッと鳴らないように）
            inGainT = std::pow (10.0f, p.inputDb / 20.0f);
            outGainT = std::pow (10.0f, p.outputDb / 20.0f);
            const float m = std::max (0.0f, std::min (1.0f, p.mix));
            dryGT = std::cos (m * (float) kPi * 0.5f); wetGT = std::sin (m * (float) kPi * 0.5f);
            if (m <= 0.0f) wetGT = 0.0f;
            if (m >= 1.0f) dryGT = 0.0f;
            if (! smInit) { snapSmooth(); smInit = true; }
            // Rhythm の音量（くり返しの数と減り方から。全体の大きさがだいたい同じになるように）
            const int n = std::max (1, std::min (16, p.repeats));
            double s2 = 0;
            for (int k = 0; k < n; ++k)
            {
                rg[k] = (float) std::pow (10.0, -(n > 1 ? p.decayDb * k / (n - 1) : 0.0) / 20.0);
                s2 += (double) rg[k] * rg[k];
            }
            const float norm = (float) (1.0 / std::sqrt (2.0 * s2));   // 参考: 1 回目の大きさ × g / √(2Σg²)（左右を足した音に掛ける）
            for (int k = 0; k < n; ++k) rg[k] *= norm;
            nRep = n;
        }
        void setDuck (float g) { duck = g; }
        // 合わせ込み用: スタイルの中身を差し替える
        void overrideProfile (const Profile& p) { custom = p; useCustom = true; configured = false; customA = onePassAlpha (custom, fs, nullptr, 0); }
        float compAttOverride = 0, compRelOverride = 0;   // 合わせ込み用（0 = スタイルの値）
        float ceilOverride = -100;                          // 合わせ込み用（-100 = スタイルの値）
        const Params& get() const { return q; }
        const Profile& profile() const { return *prof; }

        // その場で処理（L, R）
        void process (float* L, float* R, int n)
        {
            if (! configured) applySet (q);
            const double lat = Saturator::latency();
            // スタイルで決まる値（ブロックの途中でスタイルを替えたら読み直す）
            double minT = 0, t1 = 0, t2 = 0;
            const Profile* Pp = prof;
            auto styleVals = [&]
            {
                Pp = prof; minT = prof->minMs;
                t1 = std::max (minT, q.prime ? primeMs (q.time1Ms) : q.time1Ms) * 0.001 * fs;
                t2 = std::max (minT, q.prime ? primeMs (q.time2Ms) : q.time2Ms) * 0.001 * fs;
            };
            styleVals();
            const double ms = 0.001 * fs;
            // 線の中が空になったとみなすまでの無音の長さ（いちばん長い読み出し＋FEEL・GROOVE でタップが遅れる分＋余裕 0.3 秒）
            quietNeed = (int64_t) (t1 + t2 + (q.mode == 3 ? std::max (1.0, q.loopBeatsMs * ms) + (double) nRep * q.rhythmMs * ms : 0.0)
                                   + std::abs (q.feelMs) * ms + 0.25 * std::max (t1, t2) + 0.3 * fs);
            const float fbT = std::max (0.0f, std::min (1.25f, q.feedback));   // FEEDBACK も 20 ms で追う（1.3.2。ブロックごとの段でプチッと鳴っていた）
            const double maxD = maxDelaySec * fs - 8;
            // 時間はなめらかに追う（テープのように音程が動く。参考を測って時定数 0.504 s の指数、全スタイル同じ）
            const double aT = 1.0 - std::exp (-1.0 / (0.504 * fs));
            const float xStep = (float) (1.0 / (0.02 * fs));
            // つまみのなめらかさ: 音量は 20 ms。FEEL・GROOVE（エコーを読む位置）は TIME と同じ 0.504 s の指数で追う
            // （位置が一気に跳ぶとプチッと鳴る。なめらかに動かすと、テープのように音程が少し動く）
            const float aG = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
            const double aF = aT;

            for (int i = 0; i < n; ++i)
            {
                follow (inGain, inGainT, aG); follow (outGain, outGainT, aG); follow (dryG, dryGT, aG); follow (wetG, wetGT, aG);
                follow (feelS, q.feelMs, aF); follow (grooveS, (double) q.groove, aF);
                if (modeFade != 0)
                {
                    if (modeFade < 0)
                    {
                        modeG -= modeStep;
                        if (modeG <= 0.0f)
                        {
                            modeG = 0.0f;
                            // 0 まで下げたら 15 ms 待ってから替える（出口の細いフィルターやにじみの響きが消えてから。DSP 検査係: Telephone・にじむスタイルのチッ）
                            if (++holdN >= (int) (0.015 * fs))
                            {
                                holdN = 0; modeFade = 1;
                                if (pending) { Params np = q; np.mode = pendMode; np.style = pendStyle; pending = false; applySet (np); styleVals(); }
                            }
                        }
                    }
                    else if ((modeG += modeStep) >= 1.0f) { modeG = 1.0f; modeFade = 0; }
                }
                follow (fbS, fbT, aG);
                // ループへ戻す量（SATURATION の持ち上げ分を引いた）。出口のタップには掛けない。
                // 歪みの倍率が動いている間は、その今の値から計算する（倍率と戻す量が同じ速さで動くので、エコーが膨らまない）
                const Saturator& s0 = pa[0].sat;
                const float fbL = fbS * (s0.settled() ? fbKT : fbG0 / (s0.drive * s0.makeup)) * modeG;
                const double feel = feelS * ms;
                const float inL = L[i] * inGain, inR = R[i] * inGain;
                double mL, mR;
                mod.tick (*Pp, fs, q.wobble, mL, mR);
                double oML = 0, oMR = 0;
                if (Pp->modOut) { oML = mL; oMR = mR; mL = mR = 0; }   // 出口で揺らすスタイルはループの中は揺らさない
                float wet[2] { 0, 0 };
                if (q.mode == 0 || q.mode == 1)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const bool second = q.mode == 1 && c == 1;
                        tSm[c].update (second ? t2 : t1, second ? q.sync2 : q.sync1, aT, xStep);
                        const double md = c == 0 ? mL : mR;
                        const double oOdd = Pp->off[c * 2] * ms + feel, oEven = Pp->off[c * 2 + 1] * ms + feel;
                        const auto& ts = tSm[c];
                        // 揺れは「B から A へ戻る所」と「出口のタップ」だけ（参考: 2 回目は 1 回目と同じ深さ、3 回目で T 離れた 2 つが重なる）
                        const float aEnd = readT (ts, [&] (double T) { return A[c].read (clampD (T - lat, maxD)); });
                        const float bEnd = readT (ts, [&] (double T) { return B[c].read (clampD (T - lat + md, maxD)); });
                        const float tapA = readT (ts, [&] (double T) { return A[c].read (clampD (T - lat + md + oOdd + grooveS * T - outLat, maxD)); });
                        const float tapB = readT (ts, [&] (double T) { return B[c].read (clampD (T - lat + md + oEven - outLat, maxD)); });
                        const float in = (c == 0 ? inL : inR) * modeG;
                        // モード・スタイルを替えるときは、線に書く音も下げ切る（つなぎ目が 1 周後にプチッと出ないように）。
                        // 歪みの前と後の両方で下げる（前だけだと強い歪みが音量を保って書く音が急に切れ、後だけだと歪みの中のフィルターが響いた）
                        A[c].write (pa[c].process (in + fbL * bEnd) * modeG);
                        B[c].write (pb[c].process (fbL * aEnd) * modeG);
                        wet[c] = tapA + tapB;
                    }
                }
                else if (q.mode == 2)
                {
                    // Ping-Pong: 左右を足して 1 本。A（ECHO 1）→ 左、B（ECHO 2）→ 右、1 周で FEEDBACK 1 回
                    tSm[0].update (t1, q.sync1, aT, xStep); tSm2[0].update (t2, q.sync2, aT, xStep);
                    const float mono = (inL + inR) * 0.70710678f * modeG;
                    const float aEnd = readT (tSm[0], [&] (double T) { return A[0].read (clampD (T - lat + mL, maxD)); });
                    const float bEnd = readT (tSm2[0], [&] (double T) { return B[0].read (clampD (T, maxD)); });   // B は処理なし（遅れを引かない）・揺らさない
                    // にじむスタイルは、Single と同じだけ前に出す（Digital のずれ −3.44 / +4.56 ms を引いた分）
                    const double shL = (Pp->off[0] + 3.44) * ms, shR = (Pp->off[2] - 4.56) * ms;
                    float tapA, tapB, toA;
                    if (ppDiffInLoop)
                    {
                        // にじみはループの中（A に書く前）。前に出す分も 1 往復ごとに（参考と同じく往復ごとに広がる）
                        const float aEnd2 = readT (tSm[0], [&] (double T) { return A[0].read (clampD (T - lat + mL + shL, maxD)); });
                        tapA = readT (tSm[0], [&] (double T) { return A[0].read (clampD (T - lat + mL + shL + feel - outLat, maxD)); });
                        tapB = readT (tSm2[0], [&] (double T) { return B[0].read (clampD (T + feel - outLat, maxD)); });
                        toA = ppDiff.process (pa[0].process (mono + fbL * bEnd)) * modeG;
                        B[0].write (aEnd2 * modeG);
                    }
                    else
                    {
                        tapA = readT (tSm[0], [&] (double T) { return A[0].read (clampD (T - lat + mL + feel + shL - outLat, maxD)); });
                        tapB = readT (tSm2[0], [&] (double T) { return B[0].read (clampD (T + feel + shR - outLat, maxD)); });
                        toA = pa[0].process (mono + fbL * bEnd) * modeG;
                        B[0].write (aEnd * modeG);   // 参考は 1 往復で処理 1 回（右へ渡る所では削れない）
                    }
                    A[0].write (toA);
                    wet[0] = tapA; wet[1] = tapB;
                    // 使っていない線には無音を書く（Single に戻したとき、Ping-Pong にする前のエコーがまた鳴らないように）。歪みの倍率もそろえておく
                    A[1].write (0.0f); B[1].write (0.0f);
                    pa[1].sat.snap(); pb[0].sat.snap(); pb[1].sat.snap();
                }
                else
                {
                    // Rhythm: 左右を足した音を線 A に「入力 + くり返し」で書き、RHYTHM の間隔で REPEATS 個読む。全体は 2 拍ごとにくり返す。
                    // 出口は左右とも同じ（参考と同じ）
                    rhythmSm.update (q.rhythmMs * ms, q.syncR, aT, xStep);
                    const double loopLen = std::max (1.0, q.loopBeatsMs * ms);
                    const float backRaw = A[0].read (clampD (loopLen - lat + mL, maxD));
                    const float back = fbL * backRaw;   // 別の文にして丸める（前の版と同じ丸め方。1 つの式にすると掛けて足すのがまとめられて少し違う）
                    const float inS = (inL + inR) * modeG;
                    A[0].write (pa[0].process (inS + back) * modeG);
                    float s = rg[0] * (fbS * backRaw);   // 出口のタップは前と同じ（ループへ戻す量だけ SATURATION の分を引く）
                    s += readT (rhythmSm, [&] (double RT)
                    {
                        float a = 0;
                        for (int k = 1; k < nRep; ++k) a += rg[k] * A[0].read (clampD (k * RT - lat + mL + feel - outLat, maxD));
                        return a;
                    });
                    wet[0] = wet[1] = s;
                    B[0].write (0.0f); A[1].write (0.0f); B[1].write (0.0f);   // 使っていない線（上の Ping-Pong と同じ）
                    pa[1].sat.snap(); pb[0].sat.snap(); pb[1].sat.snap();
                }
                {   // 入力もエコー（タップ）も −100 dBFS 未満が続いた長さ（モード・スタイルをすぐ替えてよいか）
                    const float act = std::max (std::max (std::abs (inL), std::abs (inR)), std::max (std::abs (wet[0]), std::abs (wet[1])));
                    quietN = act > 1.0e-5f ? 0 : quietN + 1;
                }
                for (int c = 0; c < 2; ++c)
                {
                    float w = colorSplit[c].process (color[c].process (wet[c] * modeG));   // モードを替えるときは出口のフィルターの手前で下げる
                    if (Pp->modOut)
                    {
                        // 出口の揺れ: 短い遅延線を outLat を中心に揺らす（その分ループの読み出しを早めてある）
                        outMod[c].write (w);
                        w = outMod[c].read (outLat + (c == 0 ? oML : oMR));
                    }
                    { const float wd = outDiff[c].process (w); if (! ppDiffInLoop) w = wd; }   // 使わない間も回す（戻したとき古い響きが出ないように）
                    w = lowCut2[c].process (lowCut[c].process (w));
                    wet[c] = w * duck * modeG;   // にじみ（outDiff）の後でも下げる
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
        template <class T> static inline void follow (T& v, T target, T a) { const T nv = v + (target - v) * a; v = (nv == v || std::abs (target - nv) < (T) 1.0e-7) ? target : nv; }
        void snapSmooth() { inGain = inGainT; outGain = outGainT; dryG = dryGT; wetG = wetGT; feelS = q.feelMs; grooveS = q.groove; fbS = std::max (0.0f, std::min (1.25f, q.feedback)); }
        double fs = 48000;
        Params q;
        const Profile* prof = nullptr;
        Profile custom {}; bool useCustom = false;
        std::array<float, 64> onePassA {};   // Ping-Pong・Rhythm で入れる奇数回目の差の量（スタイルごと、prepare で計算）
        float customA = 0.5f;
        // Ping-Pong・Rhythm（1 周で pa だけ）のループ利得（FEEDBACK 100%・小さい音・HIGH CUT なし）の最大が、
        // 奇数回目の差を入れないとき（と 0 dB の大きいほう）を超えない範囲で、いちばん 0.5 に近い量
        static float onePassAlpha (const Profile& p, double rate, const BiqSpec* gsp, int gn)
        {
            const double g0 = (double) p.satIn[0] + p.satOut[0];
            const double fTop = std::min (20000.0, 0.45 * rate);
            auto maxLoop = [&] (double a)
            {
                Chain lp; lp.set (p.loop, p.nLoop, p.loopGain + (float) (a * p.passTilt), rate);
                BiqSpec sp[3]; for (int k = 0; k < 3; ++k) { sp[k] = p.split[k]; sp[k].g = (float) (p.split[k].g * a); }
                Chain spc; spc.set (sp, 3, 0.0f, rate);
                Chain gc; gc.set (gsp, gn, 0.0f, rate);
                double mx = -1.0e9;
                for (double f = 20.0; f < fTop; f *= 1.02) mx = std::max (mx, lp.magDb (f, rate) + spc.magDb (f, rate) + gc.magDb (f, rate) + g0);
                return mx;
            };
            const double lim = std::max (0.0, maxLoop (0.0));
            if (maxLoop (0.5) <= lim) return 0.5f;
            double lo = 0.0, hi = 0.5;
            for (int it = 0; it < 16; ++it) { const double mid = 0.5 * (lo + hi); if (maxLoop (mid) > lim) hi = mid; else lo = mid; }
            return (float) lo;
        }
        std::array<std::array<BiqSpec, 6>, 64> guardSpec {};   // スタイルごとのガード（下げるピーク、最大 6 個。前の subN 個が超低域、残りがレート）
        std::array<int, 64> guardN {}, subN {};
        // Single のループ 1 回分（FEEDBACK 100%・小さい音・SATURATION 0 dB。奇数・偶数の差は 2 回で打ち消すので入れない）の利得の最大
        static double singleMax (const Profile& p, double rate, const BiqSpec* g, int ng, double fLo, double fHi, double& fAt)
        {
            const double g0 = (double) p.satIn[0] + p.satOut[0];
            Chain lp; lp.set (p.loop, p.nLoop, p.loopGain, rate);
            Chain gc; gc.set (g, ng, 0.0f, rate);
            double mx = -1.0e9; fAt = fLo;
            for (double f = fLo; f < fHi; f *= 1.01) { const double v = g0 + lp.magDb (f, rate) + gc.magDb (f, rate); if (v > mx) { mx = v; fAt = f; } }
            return mx;
        }
        // 超低域（40 Hz 未満）の山: ループ利得が 0 dB を 0.1 dB より超える所は、細いピーク（Q 6）で 0 dB の少し下まで削る（当てはめの癖）。
        // 1.3.2: Verbed は 30.6 Hz・Q 8.1 のシェルフで 29 Hz が +1.2 dB になり、FEEDBACK 88% から 29 Hz で鳴りっぱなしだった（本物は 90% で消える）。
        // シェルフの形そのもの（29 Hz の山と 40 Hz の谷）は残す。Q を下げて山をなくすと、FEEDBACK 125% で 40〜80 Hz が育って本物より 9 dB 大きくなった。
        // 今のスタイルで当てはまるのは Verbed だけ（ほかは 0.02 dB 以下）
        static void addSubBassCut (const Profile& p, double rate, std::array<BiqSpec, 6>& g, int& n)
        {
            for (int it = 0; it < 3 && n < 6; ++it)
            {
                double fAt; const double mx = singleMax (p, rate, g.data(), n, 20.0, 40.0, fAt);
                if (mx <= 0.1) break;
                g[(size_t) n++] = { 2, (float) fAt, 6.0f, (float) -(mx + 0.15) };
            }
        }
        // このレートでの最大が「48 kHz での最大（超低域を削った後）と 0 dB の大きいほう」を超えたら、その周波数を下げるピーク（Q 0.7、超えた分 +0.1 dB）を足す。
        // 48 kHz では超えないので何も足さない（超低域を削ったスタイル以外は、音がビット単位で前と同じ）
        static void makeGuard (const Profile& p, double rate, std::array<BiqSpec, 6>& g, int& n, int& nSub)
        {
            std::array<BiqSpec, 6> g48 {}; int n48 = 0; double f48;
            addSubBassCut (p, 48000.0, g48, n48);
            const double lim = std::max (0.0, singleMax (p, 48000.0, g48.data(), n48, 20.0, 0.45 * 48000.0, f48));
            n = 0;
            addSubBassCut (p, rate, g, n);
            nSub = n;
            while (n < 6)
            {
                double fAt; const double mx = singleMax (p, rate, g.data(), n, 20.0, std::min (40000.0, 0.45 * rate), fAt);
                if (mx <= lim + 0.005) break;
                g[(size_t) n++] = { 2, (float) fAt, 0.7f, (float) -(mx - lim + 0.1) };
            }
        }
        bool configured = false;
        Halfband h1, h2;
        DelayLine A[2], B[2];
        Pass pa[2], pb[2];
        Chain color[2], colorSplit[2];
        Diffuser outDiff[2], ppDiff;
        DelayLine outMod[2];
        double outLat = 0;   // 出口で揺らすスタイルの遅れ（サンプル）
        bool ppDiffInLoop = false;
        Biquad lowCut[2], lowCut2[2];
        ModGen mod;
        // 時間の状態: ms のつまみはなめらかに追い、音符・テンポは切り替え（old から cur へ xf でクロスフェード）
        struct TimeState
        {
            double cur = -1, old = 0; float xf = 0;
            inline void update (double target, bool jump, double a, float step)
            {
                if (cur < 0) { cur = target; xf = 0; return; }
                if (jump) { if (std::abs (target - cur) > 0.5) { old = cur; cur = target; xf = 1.0f; } }
                else cur += (target - cur) * a;
                if (xf > 0) xf = std::max (0.0f, xf - step);
            }
        };
        TimeState tSm[2], tSm2[2], rhythmSm;
        template <class F> static inline float readT (const TimeState& t, F&& f)
        {
            const float v = f (t.cur);
            return t.xf > 0 ? v + t.xf * (f (t.old) - v) : v;
        }
        float inGain = 1, outGain = 1, dryG = 0.7f, wetG = 0.7f, duck = 1.0f;
        float inGainT = 1, outGainT = 1, dryGT = 0.7f, wetGT = 0.7f;   // つまみの目標（inGain などがここへなめらかに追う）
        float fbKT = 1, fbG0 = 1;   // ループへ戻す量に掛ける倍率（SATURATION の持ち上げ分を引く）と、SATURATION 0 dB の小さい音の倍率
        float fbS = 0.3f;           // FEEDBACK（20 ms で追う）
        float modeG = 1.0f, modeStep = 0.002f;   // モードを替えるときの下げ・上げ（0..1）
        int modeFade = 0, holdN = 0;           // −1 下げている・+1 上げている、0 で待ったサンプル数
        bool pending = false; int pendMode = 0, pendStyle = 0;   // 下げ切ったら替えるモード・スタイル
        int64_t quietN = (int64_t) 1 << 40, quietNeed = 0;      // 入力もエコーも無音が続いたサンプル数、空とみなす長さ
        double feelS = 0, grooveS = 0;                                  // なめらかにした FEEL（ms）・GROOVE
        bool smInit = false;
        float rg[16] {}; int nRep = 8;
        std::vector<float> dryDelay;
    };
}
