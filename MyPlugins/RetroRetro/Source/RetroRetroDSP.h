#pragma once
// Retro Retro の音の処理（JUCE なし）。
// 本人の Mac にある市販の「古っぽくする」プラグインを build/rclab で測り、量・速さ・音色・長さの数値を合わせた。
// 相手の録音データ（ノイズの素材・リバーブの IR・プリセット）は使っていない。ノイズもリバーブも全部ここで計算して作る。
//
// 並び: IN GAIN → IN WIDTH → NOISE(前) → WOBBLE → DISTORT(4 倍) → DIGITAL → SPACE → LOSS(MAGNETIC → CODEC) → NOISE(後)
//       → ECHO（輪の中に小さい WOBBLE / DISTORT / DIGITAL / SPACE / LOSS）→ VARISPEED → EQ → COMPRESS → OUT WIDTH → OUT GAIN → 頭打ち
// 遅れ: 4 倍オーバーサンプリング 28 サンプル ＋ CODEC の窓（48 kHz で 256）。つまみに関係なくいつも同じ。
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <array>
#include <complex>
#include <algorithm>
#include "TremoloStyle.h"   // My Tremolo の 4 倍オーバーサンプラー（Halfband・Oversampler4）を共用

namespace rr
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int maxCh = 2;

    inline float dbToGain (float d) { return std::pow (10.0f, d * 0.05f); }
    inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }
    inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
    // つまみ 0..1 → 20 Hz〜20 kHz（測った本物の FILTER・CUT と同じ対応）
    inline float knobHz (float v) { return 20.0f * std::pow (1000.0f, clamp01 (v)); }

    // 表を直線で補間（x は昇順）
    template <size_t N>
    inline float interp (const std::array<float, N>& xs, const std::array<float, N>& ys, float x)
    {
        if (x <= xs[0]) return ys[0];
        for (size_t i = 1; i < N; ++i)
            if (x <= xs[i]) return lerp (ys[i - 1], ys[i], (x - xs[i - 1]) / (xs[i] - xs[i - 1]));
        return ys[N - 1];
    }

    struct Rng
    {
        uint32_t s = 0x12345678u;
        explicit Rng (uint32_t seed = 1) : s (seed * 2654435761u + 1u) {}
        inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        inline float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
        inline float bi() { return uni() * 2.0f - 1.0f; }                              // −1..1
        inline float gauss() { float a = 0; for (int i = 0; i < 4; ++i) a += uni(); return (a - 2.0f) * 1.7320508f; }
    };

    //==============================================================================
    // 2 次 IIR（RBJ）。DF2T
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        inline float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
        void set (double B0, double B1, double B2, double A0, double A1, double A2)
        {
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }
        static double w0 (double f, double sr) { return 2 * kPi * std::min (f, sr * 0.49) / sr; }
        void lowpass (double f, double q, double sr)  { const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
        void highpass (double f, double q, double sr) { const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q); set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
        void bandpass (double f, double q, double sr) { const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q); set (al, 0, -al, 1 + al, -2 * c, 1 - al); }
        void peak (double f, double q, double gdb, double sr)
        {
            const double A = std::pow (10.0, gdb / 40), w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q);
            set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
        }
        void lowShelf (double f, double q, double gdb, double sr)
        {
            const double A = std::pow (10.0, gdb / 40), w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q), sa = 2 * std::sqrt (A) * al;
            set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                 (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
        }
        void highShelf (double f, double q, double gdb, double sr)
        {
            const double A = std::pow (10.0, gdb / 40), w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2 * q), sa = 2 * std::sqrt (A) * al;
            set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                 (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
        }
        // 1 次（双一次変換）
        void lowpass1 (double f, double sr) { const double k = std::tan (kPi * std::min (f, sr * 0.49) / sr); set (k, k, 0, k + 1, k - 1, 0); }
        void highpass1 (double f, double sr) { const double k = std::tan (kPi * std::min (f, sr * 0.49) / sr); set (1, -1, 0, k + 1, k - 1, 0); }
        // 1 次シェルフ（H(s) = (s + g·w)/(s + w) と (g·s + w)/(s + w) を双一次変換）
        void lowShelf1 (double f, double gdb, double sr)
        {
            const double g = std::pow (10.0, gdb / 20), k = std::tan (kPi * std::min (f, sr * 0.49) / sr);
            set (1 + g * k, g * k - 1, 0, 1 + k, k - 1, 0);
        }
        void highShelf1 (double f, double gdb, double sr)
        {
            const double g = std::pow (10.0, gdb / 20), k = std::tan (kPi * std::min (f, sr * 0.49) / sr);
            set (g + k, k - g, 0, 1 + k, k - 1, 0);
        }
        // 周波数 f での大きさ（dB）
        double magDb (double f, double sr) const
        {
            const double w = 2 * kPi * f / sr;
            const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2 * w);
            const auto num = (double) b0 + (double) b1 * z1c + (double) b2 * z2c;
            const auto den = 1.0 + (double) a1 * z1c + (double) a2 * z2c;
            return 20 * std::log10 (std::max (1e-12, std::abs (num / den)));
        }
    };

    struct OnePole   // 1 次の平滑（係数は時間から）
    {
        float y = 0, a = 0.5f;
        void setTime (float ms, double sr) { a = (float) std::exp (-1.0 / (std::max (0.01f, ms) * 0.001 * sr)); }
        inline float process (float x) { y = x + a * (y - x); return y; }
    };

    // 上がりと下がりで時間が違うエンベロープ（dB ではなく大きさ）
    struct Envelope
    {
        float y = 0, att = 0.9f, rel = 0.99f;
        void set (float attMs, float relMs, double sr)
        {
            att = (float) std::exp (-1.0 / (std::max (0.01f, attMs) * 0.001 * sr));
            rel = (float) std::exp (-1.0 / (std::max (0.01f, relMs) * 0.001 * sr));
        }
        inline float process (float x) { const float a = x > y ? att : rel; y = x + a * (y - x); return y; }
    };

    // 小数の遅れが読めるリング（3 次補間）
    struct DelayLine
    {
        std::vector<float> buf; int mask = 0, w = 0;
        void prepare (int maxSamples)
        {
            int n = 1; while (n < maxSamples + 4) n <<= 1;
            buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        inline float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
        inline float read (float d) const   // d = 何サンプル前（0 = いま入れた音）
        {
            d = std::max (0.0f, std::min (d, (float) (mask - 4)));
            const int i = (int) d; const float f = d - (float) i;
            const float xm1 = tap (i - 1 < 0 ? 0 : i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
        int size() const { return mask + 1; }
    };

    // 傾き（dB/オクターブ）を 1 次シェルフ 5 段で作る。中心 1 kHz は 0 dB
    struct Tilt
    {
        std::array<Biquad, 5> s;
        void set (float dbPerOct, double sr)
        {
            // 63・250・1k・4k・16k の境目に、2 オクターブ分（= 2 × 傾き）ずつのシェルフ
            const float f[5] = { 125.0f, 500.0f, 2000.0f, 8000.0f, 32.0f };
            for (int i = 0; i < 4; ++i) s[(size_t) i].highShelf1 (f[i], 2.0f * dbPerOct, sr);
            s[4].lowShelf1 (f[4], -2.0f * dbPerOct, sr);
            // 1 kHz で 0 dB にそろえる
            double g = 0; for (auto& b : s) g += b.magDb (1000.0, sr);
            const float k = dbToGain ((float) -g);
            s[0].b0 *= k; s[0].b1 *= k; s[0].b2 *= k;
        }
        void reset() { for (auto& b : s) b.reset(); }
        inline float process (float x) { for (auto& b : s) x = b.process (x); return x; }
    };

    //==============================================================================
    // 揺れの元（LFO）。MORPH 0 = サイン、0.5 = 三角、1 = なめらかなランダム。WARP は形を前後にゆがめる（0.5 でそのまま）
    struct Lfo
    {
        double phase = 0; Rng rng { 7 }; float rPrev = 0, rNext = 0;
        void reset (double ph = 0) { phase = ph; }
        inline float shape (double ph, float morph, float warp)
        {
            // WARP: 位相を折り曲げる（0.5 で直線）
            const double wp = 0.08 + 0.84 * (double) warp;
            const double p = ph < wp ? 0.5 * ph / wp : 0.5 + 0.5 * (ph - wp) / (1 - wp);
            const float sn = (float) std::sin (2 * kPi * p);
            const float tr = (float) (p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4);
            const float rnd = rPrev + (rNext - rPrev) * (float) (0.5 - 0.5 * std::cos (kPi * p));
            if (morph <= 0.5f) return lerp (sn, tr, morph * 2.0f);
            return lerp (tr, rnd * 0.5f, (morph - 0.5f) * 2.0f);
        }
        // hz で進めて今の値（−1..1）
        inline float next (double hz, double sr, float morph, float warp)
        {
            phase += hz / sr;
            if (phase >= 1.0) { phase -= std::floor (phase); rPrev = rNext; rNext = rng.bi(); }
            return shape (phase, morph, warp);
        }
    };

    // FLUX: 量をゆっくりランダムに揺らす（＋入力の大きさに少し追従）。戻り値は量に掛ける倍率
    struct Flux
    {
        Rng rng { 11 }; float cur = 1, target = 1; int cnt = 0; OnePole sm;
        void prepare (double sr, uint32_t seed) { rng = Rng (seed); sm.setTime (120.0f, sr); }
        inline float next (float flux, float envNorm, double sr)
        {
            if (flux <= 0.0001f) return 1.0f;
            if (--cnt <= 0) { cnt = (int) (sr * (0.15 + 0.5 * rng.uni())); target = rng.uni(); }
            cur = sm.process (target);
            const float m = (1.0f - flux * 0.75f * (1.0f - cur)) * (1.0f + flux * 0.4f * (std::min (envNorm, 1.5f) - 0.6f));
            return std::max (0.0f, m);
        }
    };

    // 帯域を絞る（Focus = 指定の帯域だけに効かせる / Cut = 効果で変わった分だけを帯域で切る）
    struct BandFocus
    {
        Biquad lo, hi; float loHz = -1, hiHz = -1; bool active = false;
        void set (float loKnob, float hiKnob, double sr)
        {
            const float l = knobHz (loKnob), h = knobHz (hiKnob);
            if (std::abs (l - loHz) < 0.01f && std::abs (h - hiHz) < 0.01f) return;
            loHz = l; hiHz = h;
            active = loKnob > 0.002f || hiKnob < 0.998f;
            lo.highpass (std::max (10.0f, l), 0.7071, sr); hi.lowpass (std::min (h, (float) sr * 0.45f), 0.7071, sr);
        }
        void reset() { lo.reset(); hi.reset(); }
        inline float band (float x) { return active ? hi.process (lo.process (x)) : x; }
    };

    //==============================================================================
    // 音符の長さ（拍）。"4/1D" = 4 小節の付点、"1/8T" = 8 分の 3 連
    inline double noteBeats (const char* s)
    {
        int a = 0, b = 0; char m = 0;
        const char* p = s;
        while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
        if (*p == '/') ++p;
        while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
        m = *p;
        double beats = 4.0 * a / std::max (1, b);
        if (m == 'D') beats *= 1.5; else if (m == 'T') beats *= 2.0 / 3.0;
        return beats;
    }
    inline const char* const* wowNotes() { static const char* n[] = { "4/1D","4/1","2/1D","4/1T","2/1","1/1D","2/1T","1/1","1/2D","1/1T","1/2","1/4D","1/2T","1/4","1/8D","1/4T","1/8","1/16D","1/8T","1/16" }; return n; }
    constexpr int kNumWowNotes = 20;
    inline const char* const* flutterNotes() { static const char* n[] = { "1/8","1/16D","1/8T","1/16","1/32D","1/16T","1/32","1/64D","1/32T","1/64","1/128D" }; return n; }
    constexpr int kNumFlutterNotes = 11;
    inline const char* const* magNotes() { static const char* n[] = { "1/8","1/16D","1/8T","1/16","1/32D","1/16T","1/32","1/64D","1/32T","1/64","1/128D","1/64T","1/128" }; return n; }
    constexpr int kNumMagNotes = 13;
    inline const char* const* echoNotes() { static const char* n[] = { "1/32","1/16T","1/32D","1/16","1/8T","1/16D","1/8","1/4T","1/8D","1/4","1/2T","1/4D","1/2","1/1T","1/2D","1/1","2/1T","1/1D","2/1" }; return n; }
    constexpr int kNumEchoNotes = 19;
    inline const char* const* resyncNotes() { static const char* n[] = { "1/16","1/8","1/4","1/2","1/1","2/1" }; return n; }
    constexpr int kNumResyncNotes = 6;

    //==============================================================================
    // つまみ（全部 0..1 か選択肢の番号。本物と同じ並び・同じ初期値）
    struct Knobs
    {
        float magnitude = 1.0f;
        // MASTER
        float inGain = 0.5f, inWidth = 0.3017f, outWidth = 0.5f, outGain = 0.5f, mix = 1.0f;
        int varispeed = 0, resync = 2; float varispeedMix = 1.0f;
        bool eqOn = true; float cutLo = 0.1581f, cutHi = 0.7991f; bool cutLoHard = false, cutHiHard = false;
        float tone = 0.3545f; int toneMode = 1;
        float compress = 0.3987f;
        // NOISE
        bool noiseOn = true; float noiseAmt = 0.1238f; int noiseType = 0; float noiseTone = 0.688f, noisePrePost = 1.0f, noiseFollow = 0.0977f,
              noiseDuck = 0.0f, noiseFlux = 0.0f, noiseLo = 0.0f, noiseHi = 1.0f;
        // WOBBLE
        bool wobOn = true; float wobAmt = 0.4123f, wowFlutter = 0.1042f, wowRate = 0.0805f; int wowNote = 7; bool wowSync = false, wowStereo = false;
        float flutterRate = 0.294f; int flutterNote = 6; bool flutterSync = false;
        float wobMorph = 0.7105f, wobWarp = 0.6842f, wobMix = 1.0f, wobFlux = 0.441f, wobLo = 0.0f, wobHi = 1.0f;
        // DISTORT
        bool distOn = true; float distAmt = 0.25f; int distType = 6; float distOverload = 0.2902f, distMix = 1.0f, distFlux = 0.0741f, distLo = 0.0f, distHi = 1.0f; bool distCut = false;
        // DIGITAL
        bool digOn = true; float digAmt = 0.0619f, digRateBits = 0.1491f, digSmooth = 0.1022f, digSputter = 0.3028f, digDrive = 0.0f, digMix = 1.0f, digFlux = 0.0f, digLo = 0.0f, digHi = 1.0f; bool digCut = true;
        // SPACE
        bool spaceOn = true; float spaceAmt = 0.1f; int spaceType = 5; float spaceDecay = 0.8f, spacePan = 0.5f, spaceWidth = 1.0f, spacePre = 0.0f, spaceDuck = 0.0f, spaceFlux = 0.0f, spaceLo = 0.0f, spaceHi = 1.0f;
        // LOSS
        bool lossOn = true; float lossAmt = 0.15f, lossXfade = 0.2813f, lossDrop = 0.0f; bool wearStereo = true;
        float magRate = 0.5f; int magNote = 3; bool magSync = false; float magMorph = 0.0f, magWarp = 0.5f, magVolFilter = 0.0f;
        int codecDropMode = 0; float codecStretch = 0.0f, codecBlur = 0.0f, codecMix = 1.0f; bool codecCut = true; float lossFlux = 0.1f, lossLo = 0.0f, lossHi = 1.0f;
        // ECHO
        bool echoOn = true; float echoAmt = 0.15f; bool echoSolo = false, echoClear = false; float echoFb = 0.1697f, echoTimeL = 0.5f, echoTimeR = 0.5f;
        int echoNoteL = 6, echoNoteR = 6; bool echoSyncL = true, echoSyncR = true, echoLink = true;
        float echoScrub = 0.5f, echoWidth = 0.7487f, echoPingPong = 0.3093f, echoDuck = 0.4152f, echoSmooth = 0.0f,
              grainAmt = 0.0f, grainPitch = 0.5f; bool grainRev = false, grainInv = false;
        float echoFlux = 0.1042f, echoLo = 0.2863f, echoHi = 0.7479f,
              echoWob = 0.0f, echoDist = 0.2366f, echoDig = 0.0f, echoSpace = 0.1161f, echoLoss = 0.0f;
        float noiseLinkGain = 1.0f;   // Plugin Link（Fader Pilot が歌っている間のノイズの倍率）
    };

    struct Transport { double bpm = 120; bool playing = false; double ppq = 0; bool hasPos = false; };

    //==============================================================================
    // NOISE: 計算で作るノイズ 10 種類（測った帯域の形・左右の似かた・ゆっくりした揺れ・プチプチの数に合わせた）
    struct NoiseType
    {
        const char* name;
        std::array<float, 9> spec;   // 100,200,500,1k,2k,4k,8k,12k,16k Hz の強さ（dB、TONE 50 %）
        float rmsDb;                 // AMOUNT 100 %・TONE 50 % の大きさ
        float lrCorr;                // 左右の似かた
        float slowAm;                // ゆっくりした音量の揺れ（0..1）
        float crackleRate;           // プチプチ / 秒
        float crackleDb;             // プチプチの大きさ（地のノイズから）
        float humHz;                 // ハム（0 = なし）
    };
    inline const std::array<NoiseType, 10>& noiseTypes()
    {
        static const std::array<NoiseType, 10> t {{
            { "Tape Hiss",     { -57,-59,-65,-68,-67,-66,-66,-67,-70 }, -23.4f, 0.20f, 0.02f, 0,   0,  0 },
            { "Bright Hiss",   { -61,-61,-62,-62,-62,-62,-64,-67,-72 }, -21.8f, 0.00f, 0.02f, 0,   0,  0 },
            { "Room Tone",     { -43,-46,-53,-59,-62,-64,-67,-70,-73 }, -20.7f, 0.24f, 0.03f, 0,   0,  0 },
            { "Cassette",      { -48,-48,-52,-55,-60,-68,-79,-84,-88 }, -19.8f, 0.00f, 0.10f, 0,   0,  0 },
            { "Dusty Vinyl",   { -48,-46,-57,-60,-64,-69,-71,-76,-81 }, -21.1f, 0.61f, 0.25f, 3,  24,  0 },
            { "Vinyl Crackle", { -44,-53,-63,-64,-64,-68,-79,-85,-90 }, -23.2f, 0.72f, 0.40f, 28, 22,  0 },
            { "Rumble",        { -42,-52,-69,-72,-77,-80,-82,-83,-84 }, -19.5f, 0.11f, 0.55f, 0,   0,  0 },
            { "Hum & Hiss",    { -50,-59,-64,-64,-65,-66,-66,-66,-67 }, -22.9f, 0.00f, 0.02f, 0,   0, 60 },
            { "Radio",         { -51,-50,-51,-53,-63,-65,-74,-77,-82 }, -19.5f, 0.00f, 0.03f, 0,   0,  0 },
            { "Telephone",     { -72,-48,-47,-58,-63,-114,-120,-122,-124 }, -18.7f, 0.00f, 0.20f, 0, 0, 0 },
        }};
        return t;
    }

    struct NoiseGen
    {
        double sr = 48000;
        int type = -1;
        std::array<std::array<Biquad, 10>, maxCh> eq;   // 9 帯域＋直流カット
        std::array<Tilt, maxCh> tilt; float tiltNow = 99;
        std::array<BandFocus, maxCh> cut;
        Rng rng { 3 }; float typeGain = 1;
        std::array<float, maxCh> clickEnv { 0, 0 }; std::array<Biquad, maxCh> clickHp;
        float am = 1, amTarget = 1; int amCnt = 0; OnePole amSm;
        double humPh = 0;
        float silentFor = 0, gateGain = 0;

        std::array<std::array<Biquad, 10>, 10> designs;   // 10 種類分を prepare で先に作っておく（音の処理の中で計算しない）
        std::array<float, 10> gains {};
        void prepare (double s)
        {
            sr = s; type = -1; tiltNow = 99; amSm.setTime (200.0f, sr); for (auto& h : clickHp) h.highpass (900, 0.7, sr);
            for (int t = 0; t < 10; ++t) computeType (t, designs[(size_t) t], gains[(size_t) t]);
        }
        void reset() { for (auto& c : eq) for (auto& b : c) b.reset(); for (auto& t : tilt) t.reset(); for (auto& c : cut) c.reset(); }

        // 種類を切り替える（係数だけ入れ替え、フィルターの中身は残す）
        void setType (int t)
        {
            if (t == type) return;
            type = t;
            for (auto& c : eq)
                for (int j = 0; j < 10; ++j) { const float z1 = c[(size_t) j].z1, z2 = c[(size_t) j].z2; c[(size_t) j] = designs[(size_t) t][(size_t) j]; c[(size_t) j].z1 = z1; c[(size_t) j].z2 = z2; }
            typeGain = gains[(size_t) t];
        }
        void computeType (int t, std::array<Biquad, 10>& out, float& gainOut)
        {
            const auto& nt = noiseTypes()[(size_t) t];
            static const float fc[9] = { 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 16000 };
            // 9 個の山の大きさを、測った形に合うまで直す（白色ノイズ → その形）
            float mean = 0; for (float v : nt.spec) mean += v; mean /= 9;
            std::array<float, 9> g {}; for (int i = 0; i < 9; ++i) g[(size_t) i] = nt.spec[(size_t) i] - mean;
            std::array<Biquad, 10> b;
            for (int it = 0; it < 12; ++it)
            {
                for (int i = 0; i < 9; ++i)
                {
                    const double q = i < 3 ? 0.9 : 1.1;
                    if (i == 0) b[0].lowShelf (140, 0.7, g[0], sr);
                    else if (i == 8) b[8].highShelf (std::min (14000.0, sr * 0.4), 0.7, g[8], sr);
                    else b[(size_t) i].peak (std::min ((double) fc[i], sr * 0.42), q, g[(size_t) i], sr);
                }
                for (int i = 0; i < 9; ++i)
                {
                    double m = 0; for (int j = 0; j < 9; ++j) m += b[(size_t) j].magDb (std::min ((double) fc[i], sr * 0.45), sr);
                    g[(size_t) i] += 0.7f * (float) ((nt.spec[(size_t) i] - mean) - m);
                    g[(size_t) i] = std::max (-60.0f, std::min (40.0f, g[(size_t) i]));
                }
            }
            b[9].highpass (30, 0.7, sr);
            out = b; for (auto& x : out) x.reset();
            // 大きさ: 白色ノイズを通したときの大きさを測って、表の rmsDb にそろえる（80 Hz より上で測った値）
            Rng r (99); std::array<Biquad, 10> bb = b; Biquad hp; hp.highpass (80, 0.7, sr); hp.reset();
            double e = 0; const int n = (int) sr;
            for (int i = 0; i < n; ++i) { float x = r.gauss(); for (auto& f : bb) x = f.process (x); x = hp.process (x); if (i > n / 4) e += (double) x * x; }
            const float rmsNow = (float) std::sqrt (e / (n - n / 4));
            gainOut = dbToGain (nt.rmsDb) / std::max (1e-6f, rmsNow);
        }

        // AMOUNT → 大きさ（dB、TONE 69 % で測った）
        static float amountDb (float a)
        {
            static const std::array<float, 6> xs { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
            static const std::array<float, 6> ys { -100.0f, -67.3f, -47.8f, -32.3f, -24.8f, -20.4f };
            return interp (xs, ys, a) + 20.4f;   // 100 % で 0 dB（型ごとの大きさは typeGain）
        }

        // 1 サンプル分のノイズ（左右）を足す
        inline void render (float* out, int nCh, float amount, float toneKnob, float follow, float duck, float envDb,
                            float loKnob, float hiKnob, float fluxMul, bool inputSilent, float linkGain = 1.0f)
        {
            const auto& nt = noiseTypes()[(size_t) type];
            const float slope = (toneKnob - 0.5f) * 7.2f;
            if (std::abs (slope - tiltNow) > 0.05f) { for (int c = 0; c < nCh; ++c) tilt[(size_t) c].set (slope, sr); tiltNow = slope; }
            for (int c = 0; c < nCh; ++c) cut[(size_t) c].set (loKnob, hiKnob, sr);

            // 入力が止まって 1.5 秒でノイズも止める（本物と同じ。トラックが鳴っていない所でサーッと鳴り続けない）
            silentFor = inputSilent ? silentFor + (float) (1.0 / sr) : 0.0f;
            const float gTarget = silentFor > 1.5f ? 0.0f : 1.0f;
            gateGain += (gTarget - gateGain) * (gTarget > gateGain ? 0.002f : 0.0002f);

            // FOLLOW / DUCK（入力の大きさで、ノイズの音量を dB で動かす）
            float gDb = follow * std::max (-70.0f, std::min (6.0f, envDb + 27.0f));
            gDb -= duck * std::max (0.0f, std::min (23.0f, envDb + 40.0f));
            const float g = dbToGain (amountDb (amount * fluxMul) + gDb) * typeGain * gateGain * linkGain;
            if (g < 1e-7f) return;

            // ゆっくりした音量の揺れ
            if (--amCnt <= 0) { amCnt = (int) (sr * (0.05 + 0.25 * rng.uni())); amTarget = 1.0f + nt.slowAm * 1.6f * rng.bi(); }
            am = amSm.process (amTarget);

            const float common = rng.gauss();
            float hum = 0;
            if (nt.humHz > 0) { humPh += nt.humHz / sr; if (humPh > 1) humPh -= 1; hum = 0.5f * (float) (std::sin (2 * kPi * humPh) + 0.5 * std::sin (4 * kPi * humPh) + 0.3 * std::sin (6 * kPi * humPh)); }
            // プチプチ（ポアソン）。大きさはべき分布で、たまに大きいのが来る
            float click = 0;
            if (nt.crackleRate > 0 && rng.uni() < nt.crackleRate / sr)
            {
                const float u = std::max (0.02f, rng.uni());
                click = dbToGain (nt.crackleDb) * std::min (8.0f, 0.25f / std::pow (u, 0.7f)) * (rng.uni() < 0.5f ? -1.0f : 1.0f);
            }
            const float k = std::sqrt (std::max (0.0f, nt.lrCorr));
            for (int c = 0; c < nCh; ++c)
            {
                float x = k * common + std::sqrt (1.0f - k * k) * rng.gauss();
                x += hum;
                for (auto& f : eq[(size_t) c]) x = f.process (x);
                x *= am;
                if (click != 0 || clickEnv[(size_t) c] != 0)
                {
                    clickEnv[(size_t) c] = clickEnv[(size_t) c] * 0.86f + click * (c == 0 ? 1.0f : 0.8f + 0.2f * k);
                    if (std::abs (clickEnv[(size_t) c]) < 1e-6f) clickEnv[(size_t) c] = 0;
                    x += clickHp[(size_t) c].process (clickEnv[(size_t) c] * rng.bi()) * 0.06f;
                }
                x = tilt[(size_t) c].process (x);
                x = cut[(size_t) c].band (x);
                out[c] += x * g;
            }
        }
    };

    //==============================================================================
    // WOBBLE: テープの揺れ（WOW = ゆっくり、FLUTTER = 速い）。遅らせる時間を揺らして音程を揺らす
    struct Wobble
    {
        double sr = 48000;
        std::array<DelayLine, maxCh> dl;
        Lfo wowL, wowR, flL, flR;
        std::array<BandFocus, maxCh> focus;
        Flux flux;
        OnePole depthSm, wowDSm, flDSm;
        float center = -1;

        void prepare (double s)
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (0.25 * sr));
            wowR.reset (0.25); flR.reset (0.31); wowR.rng = Rng (17); flR.rng = Rng (23);
            flux.prepare (sr, 41); depthSm.setTime (30.0f, sr); wowDSm.setTime (60.0f, sr); flDSm.setTime (60.0f, sr); wowDSm.y = -1;
        }
        void reset() { for (auto& d : dl) d.reset(); for (auto& f : focus) f.reset(); center = -1; }

        // AMOUNT → 深さの倍率（測った揺れの大きさの比）
        static float depthScale (float a)
        {
            static const std::array<float, 5> xs { 0, 0.25f, 0.5f, 0.75f, 1 };
            static const std::array<float, 5> ys { 0, 0.117f, 0.227f, 0.45f, 1 };
            return interp (xs, ys, a);
        }
        static double wowHz (float r) { return 0.25 * std::pow (16.0, (double) r); }                 // 0.25〜4 Hz
        static double flutterHz (float r) { return 6.0 * std::pow (20.0 / 6.0, std::pow ((double) r, 1.12)); }   // 6〜20 Hz

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const Transport& tr, const float* envNorm)
        {
            const float wowG = std::min (1.0f, 2.0f * (1.0f - k.wowFlutter)), flG = std::min (1.0f, 2.0f * k.wowFlutter);
            const double wHz = k.wowSync ? tr.bpm / 60.0 / noteBeats (wowNotes()[k.wowNote]) : wowHz (k.wowRate);
            const double fHz = k.flutterSync ? tr.bpm / 60.0 / noteBeats (flutterNotes()[k.flutterNote]) : flutterHz (k.flutterRate);
            // 揺れ幅（遅らせる時間の最大、秒）: WOW 50 ms（0.5 Hz より遅いと浅く）、FLUTTER は 6 Hz で 7.2 ms（測った音程の揺れから）
            const double wowD = 0.050 * std::min (1.0, std::max (0.4, (wHz - 0.1) / 0.4));
            const double flD = 0.0072 * std::pow (6.0 / std::max (1.0, fHz), 0.65);
            for (int c = 0; c < nCh; ++c) focus[(size_t) c].set (k.wobLo, k.wobHi, sr);
            for (int i = 0; i < n; ++i)
            {
                const float fm = flux.next (k.wobFlux, envNorm[i], sr);
                const float d = depthSm.process (depthScale (clamp01 (amt * fm)));
                const float wl = wowL.next (wHz, sr, k.wobMorph, k.wobWarp), fl = flL.next (fHz, sr, k.wobMorph, k.wobWarp);
                float wr = wl, fr = fl;
                if (wowDSm.y < 0) { wowDSm.y = (float) (wowD * wowG); flDSm.y = (float) (flD * flG); }
                const float wd = wowDSm.process ((float) (wowD * wowG)), fd = flDSm.process ((float) (flD * flG));   // RATE・SYNC で揺れ幅が飛ばない（1 サンプルに 1 回）
                if (k.wowStereo) { wr = wowR.next (wHz, sr, k.wobMorph, k.wobWarp); fr = flR.next (fHz, sr, k.wobMorph, k.wobWarp); }
                // 揺れの中心（d × 幅）は 1 サンプルあたり 0.02 サンプルまでしか動かさない（量・MAGNITUDE を急に変えても音程が飛ばない）
                const float centerT = d * (float) sr * (wd + fd);
                if (center < 0) center = centerT;
                center += std::max (-0.02f, std::min (0.02f, centerT - center));
                for (int c = 0; c < nCh; ++c)
                {
                    const float w = c == 0 ? wl : wr, f = c == 0 ? fl : fr;
                    const float x = io[c][i];
                    const float b = focus[(size_t) c].band (x);
                    dl[(size_t) c].push (b);
                    // 遅らせる時間はいつも 0 以上（AMOUNT 0 なら遅れなし＝元の音のまま）
                    const float del = std::max (0.0f, center + d * (float) sr * (wd * w + fd * f));
                    const float wet = dl[(size_t) c].read (del);
                    io[c][i] = x + k.wobMix * (wet - b);
                }
            }
        }
    };
}
