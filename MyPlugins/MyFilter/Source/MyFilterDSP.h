#pragma once
// My Filter の音の処理（JUCE なし）。
// 本人の Mac にある市販のフィルター（aufx FF1 / FF2・SToy）をオフラインで測って、フィルターの形・レゾナンス・周波数・MIX・
// 色付け（My Tremolo と同じ測った 7 種類）を同じ動きに合わせ込んだ。測り方と数値の出どころは NOTES.md と build/fflab（コミットしない）。
//
// 流れ（1 チャンネル、4 倍の速さ）:
//   INPUT → 4 倍 → [色付けの前半] → フィルター 1 →（直列なら）フィルター 2 /（並列なら）1 と 2 を足す → OUTPUT → [色付けの後半] → 1 倍
//   → 元の音（遅らせて）と MIX（cos / sin で混ぜる）
// 動かし方（MOD の元、0..1）: LFO / RHYTHM / ENVELOPE / RANDOM / STEP / ADSR
//   周波数 = FREQ × 2^(MOD × FREQ MOD × 元)。20 Hz〜19.6 kHz で止める（本物と同じ）
#include "TremoloStyle.h"
#include "TremoloShapes.h"
#include "TremoloStyleTables.h"
#include "MyTremoloDSP.h"   // Pattern・EventList・CustomShape・Modulator（揺れの時計）を共用
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace myfilt
{
    using mytrem::kPi;

    enum Shape { kLP = 0, kBP, kHP, kBR };
    enum Mode { kLFO = 0, kRhythm, kEnvelope, kRandom, kStep, kADSR, kNumModes };
    inline const char* modeName (int i) { static const char* n[] = { "LFO", "RHYTHM", "ENVELOPE", "RANDOM", "STEP", "ADSR" }; return (i >= 0 && i < kNumModes) ? n[i] : "?"; }

    constexpr float kMinHz = 20.0f, kMaxHz = 19600.0f;   // 本物は動かしても 19.6 kHz で止まる
    inline float freqFromNorm (float v) { return 20.0f * std::pow (1000.0f, v); }   // つまみ 0..1 → 20 Hz〜20 kHz（本物と同じ対応）

    //==============================================================================
    // RES（0..1）→ 山の高さ P（dB）。BANDPASS の山の高さを 0.02 刻みで測った（build/fflab/g2）。
    // K = P² が決まると、形ごとの Q と倍率が決まる（下の Section）。0.95 から上は自分で鳴り出す（発振）
    inline double resPeakDb (double r)
    {
        static const double t[] = { 0.0, 0.10, 0.25, 0.35, 0.50, 0.60, 0.70, 0.85, 0.95, 1.10, 1.20, 1.30, 1.45, 1.65, 1.90, 2.20,
                                    2.50, 2.75, 3.05, 3.30, 3.60, 3.90, 4.15, 4.45, 4.70, 5.00, 5.40, 5.80, 6.20, 6.60, 7.00, 7.41,
                                    7.81, 8.23, 8.65, 9.09, 9.56, 10.10, 11.00, 12.22, 13.50, 14.66, 15.89, 17.15, 18.31, 19.48, 20.66, 21.68 };   // r = 0, 0.02 … 0.94
        constexpr int n = (int) (sizeof (t) / sizeof (t[0]));
        if (r <= 0) return 0.0;
        const double x = r / 0.02;
        if (x < n - 1) { const int i = (int) x; return t[i] + (t[i + 1] - t[i]) * (x - i); }
        // 0.94〜0.96: 本物は 0.96 で約 39 dB（ほぼ鳴りっぱなし）→ 指数的に上げる
        const double over = (r - 0.94) / 0.02;
        return t[n - 1] + 17.0 * over * over;
    }
    constexpr double kOscStart = 0.965;

    // 4〜8 極のときだけ、強いレゾナンス（0.82 から上）で全体の倍率が 2 極より下がり、約 −24 dB で止まる（本物を測った癖。build/fflab/g3）。
    // 山の鋭さ（K）は 2 極と同じ。戻り値 = 2 極の倍率 1/√K に足す dB（0 以下）
    inline double multiPoleExtraDb (double r, int n)
    {
        if (n <= 1 || r <= 0.82) return 0.0;
        // r = 0.82, 0.84, 0.86, 0.88, 0.90 の通過域の高さ（dB）と、その先の止まる高さ
        static const double t2[] = { -14.4, -16.2, -18.8, -21.2, -23.7 }, f2 = -24.1;
        static const double t3[] = { -14.3, -16.6, -20.5, -22.6, -23.7 }, f3 = -23.8;
        static const double t4[] = { -14.2, -17.1, -22.3, -23.6, -23.6 }, f4 = -23.6;
        const double* t = n == 2 ? t2 : (n == 3 ? t3 : t4);
        const double fl = n == 2 ? f2 : (n == 3 ? f3 : f4);
        const double x = (std::min (r, kOscStart) - 0.82) / 0.02;
        const double want = x >= 4.0 ? fl : t[(int) x] + (t[(int) x + 1] - t[(int) x]) * (x - (int) x);
        const double g0 = -resPeakDb (std::min (r, kOscStart));
        return std::min (0.0, want - g0);
    }   // ここから上は自分で鳴る（本物: 0.96 はゆっくり消える、0.98 は鳴りっぱなし）

    // 高い周波数ほど山が鋭くなる分（減衰 R = 1/2Q から引く）。本物の BANDPASS をレゾナンス 0 / 0.4 / 0.7 / 0.85、
    // 632 Hz〜20 kHz で測ると、5 kHz で約 0.009、20 kHz で約 0.022（r 0.85 だと 5 kHz から上でほぼ鳴りっぱなしになる）
    inline double dampShift (double hz) { return 0.0235 * std::tanh (hz / 12000.0); }

    // 引く量は、もともとの減衰が小さい（レゾナンスが強い）ほど控えめに（本物は r 0.85 でも 20 kHz まで鳴りっぱなしにはならず、山が +4〜11 dB 高くなるだけ）
    inline double effDamp (double R, double c)
    {
        if (R <= 0.0) return R - c * 0.0;   // 発振の設定はそのまま
        const double cut = c * (1.0 - std::exp (-R / 0.022));
        return R * std::max (0.02, 1.0 - cut / R);
    }

    //==============================================================================
    // 2 次の段（TPT の状態変数フィルター）。4 倍の速さで回す
    struct Section
    {
        double s1 = 0, s2 = 0;
        void reset() { s1 = s2 = 0; }
        // g = tan(π fc / fs)、R = 1/(2Q)（負なら発振）、lim = 帯域の状態の天井（やわらかく止める）
        inline void tick (double x, double g, double R, double lim, double& lp, double& bp, double& hp)
        {
            const double den = 1.0 + 2.0 * R * g + g * g;
            hp = (x - (2.0 * R + g) * s1 - s2) / den;
            bp = g * hp + s1;
            s1 = g * hp + bp;
            if (R < 0.0)
                s1 = std::max (-lim, std::min (lim, s1));   // 発振中: 天井で止める（毎周期ほんの少し伸びる分だけ削れるので、ほぼ正弦波のまま）
            else
            {
                // 状態の天井: v / (1 + (v/lim)^4)^(1/4)（小さい所はほぼ素通し）
                const double q = s1 / lim, q2 = q * q;
                s1 /= std::sqrt (std::sqrt (1.0 + q2 * q2));
            }
            lp = g * bp + s2;
            s2 = g * bp + lp;
            if (! std::isfinite (s1) || ! std::isfinite (s2)) { s1 = s2 = 0; }
        }
    };

    // 1 つのフィルター（2/4/6/8 極 = 2 次の段 1〜4 つ）。形・Q・倍率の決まりは本物を測って確かめた（NOTES.md）:
    //  - LP / HP: 段ごとの Q は「段の山の高さ = K^(1/n)」になる Q（K = 1 のとき 0.707）。全体の倍率は 1/√K
    //  - BP: 段ごとに山 0 dB に正規化した帯域通過、Q = K^(1/n)。全体の倍率は √K
    //  - BR: 段ごとに 1 − 正規化 BP（Q = K^(1/n)）
    struct Filter
    {
        Section sec[4];
        int n = 1, shape = kLP;
        double R = 0.7071, lim = 4.0, gainAll = 1.0, qg = 1.0, perGain = 1.0;
        float lastRes = -1.0f; int lastN = -1, lastShape = -1;
        void reset() { for (auto& s : sec) s.reset(); }

        void setRes (float res, int poles, int shp)
        {
            const int nn = std::max (1, std::min (4, poles / 2));
            if (std::abs (res - lastRes) < 1e-5f && nn == lastN && shp == lastShape) return;
            if (lastN > 0) for (int i = lastN; i < nn; ++i) sec[i].reset();   // 増えた段は空から始める（前に使っていた時の古い状態でプツッと鳴らないように）
            lastRes = res; lastN = nn; lastShape = shp; n = nn; shape = shp;
            const double r = std::max (0.0, std::min (1.0, (double) res));
            const double pDb = resPeakDb (std::min (r, kOscStart));
            const double K = std::pow (10.0, pDb / 10.0);
            const double kn = std::pow (K, 1.0 / n);               // 段ごとの山の高さ
            if (shp == kLP || shp == kHP)
            {
                const double p2 = kn * kn;
                const double q2 = 0.5 * (p2 + std::sqrt (std::max (0.0, p2 * p2 - p2)));
                const double q = std::sqrt (std::max (0.5, q2));
                R = 0.5 / q;
                gainAll = 1.0 / std::sqrt (K);
            }
            else
            {
                R = 0.5 / kn;
                gainAll = std::sqrt (K);
            }
            if (shp != kBR) gainAll *= std::pow (10.0, multiPoleExtraDb (r, n) / 20.0);
            qg = 2.0 * R;   // 正規化に使う 1/Q
            perGain = std::pow (gainAll, 1.0 / n);
            // 発振: 0.965 から上は減衰を負にして、天井で止める（本物は 1.0 で約 0.9 の正弦波）
            if (r > kOscStart) R = -0.004 * (r - kOscStart) / (1.0 - kOscStart) - 1.0e-4;
            // 天井（出口で約 +12 dB 分の余裕。強いレゾナンスでは 0.9 くらいまで下げる）
            const double L = r <= 0.9 ? 4.0 : (r >= 0.97 ? 0.9 : 4.0 * std::pow (0.9 / 4.0, (r - 0.9) / 0.07));
            // 段の帯域の状態の天井 = 出口で L になる大きさ（LP / HP: 出口 = lp × 段の倍率、BP: bp × (1/Q) × 段の倍率、BR: 入口 − bp/Q）
            if (shp == kLP || shp == kHP) lim = L / std::max (1e-6, perGain);
            else if (shp == kBP) lim = L * kn / std::max (1e-6, perGain);
            else lim = L * kn;
        }

        // c = 高い周波数ほど減衰が減る分（dampShift）
        inline double process (double x, double g, double c)
        {
            double y = x;
            const double Re = effDamp (R, c);
            for (int i = 0; i < n; ++i)
            {
                double lp, bp, hp;
                sec[i].tick (y, g, Re, lim, lp, bp, hp);
                switch (shape)
                {
                    case kLP: y = lp; break;
                    case kHP: y = hp; break;
                    case kBP: y = bp * qg; break;
                    default:  y = y - bp * qg; break;   // 1 − 正規化 BP
                }
                if (shape != kBR) y *= perGain;
            }
            return y;
        }
    };

    //==============================================================================
    struct Knobs
    {
        float inDb = 0, outDb = 0, mix = 1;           // mix 0..1
        int style = 1;                                // 0 = DIGITAL、1..7 = 色付け（Clean, Fat, Squash, Dirt, Crunch, Shred, Pump）
        int filters = 1;                              // 1 か 2
        bool parallel = false;
        float freq[2] { 0.7993f, 0.5478f };           // 0..1（20 Hz〜20 kHz）
        float res[2] { 0, 0 };                        // 0..1
        int shape[2] { kLP, kBP };
        int poles[2] { 2, 2 };
        float gainDb[2] { 0, 0 };                     // ±24
        float freqModOct[2] { 8, 8 };                 // ±10
        float resMod[2] { 0, 0 };                     // ±1
        float levelMod[2] { 0, 0 };                   // ±1
        float mod = 0;                                // 0..1（MOD つまみ）
        int mode = kEnvelope;
        float lfoHz = 0.25f;
        int shapeIdx = 0;                             // 揺れの形（mytrem::kNumShapes = 自分の形）
        int rhythm = 9;                               // 1/8
        float tempoBpm = 120; bool sync = true;
        float groove = 0;                             // −0.25..+0.25
        float thresholdDb = -20, envGainDb = 12, attackMs = 570, releaseMs = 550;   // 本物の初期状態: 時定数 約 125 ms / 120 ms（表示は 99 % に届く時間）
        float trigDb = -20;                           // STEP / ADSR のきっかけ
        bool trigger = false;                         // 手で押すきっかけ
        float aMs = 10, dMs = 300, sustain = 0.5f, rMs = 500;
        float lrOffsetOct = 0; bool lrFlip = false;   // 左右（モノ → ステレオのときだけ）
        float smoothing = 1; int smoothMode = 1;
        float duck = 1;                               // Plugin Link（歌っている間の MOD の倍率）
    };

    struct Transport { bool hostPlaying = false, hasPosition = false; double ppq = 0, hostBpm = 120; };

    //==============================================================================
    class Engine
    {
    public:
        static constexpr int maxCh = 2;

        void prepare (double sampleRate, int numInCh, int numOutCh)
        {
            fs = sampleRate; fs4 = fs * 4;
            nIn = std::max (1, std::min (maxCh, numInCh));
            nOut = std::max (1, std::min (maxCh, numOutCh));
            for (auto& o : os) { o.prepare(); o.reset(); }
            for (auto& c : flt) for (auto& f : c) f.reset();
            curStyle = -1; fadeLeft = 0;
            lfoPhase = 0; beat = 0; first = true;
            env = 0; peak = 0; blkMax = 0; blkCount = 0;
            rnd = 0x12345678u; rPrev = rNext = 0.5f; lastStep = -1; stepT = 1; stepFrom = stepTo = 0.5f; trigArmed = true;
            adsr = 0; adsrStage = 0; gateOn = false;
            for (auto& m : modS) m = 0;
            inS = -1; outS = -1; mixS = -1;
            for (auto& g : gS) g = 1;
            appInit = false; swGain = 1.0f; swPos = 1.0f;
            const int lat = latencySamples();
            for (auto& d : dry) { d.assign ((size_t) lat, 0.0f); }   // 輪の大きさ = 遅れ
            dryPos = 0;
            patternDirty.store (true); customDirty.store (true); lastSmooth = -1; lastMode = -1;
        }

        int latencySamples() const { return (int) mytrem::Oversampler4::latency() + 1; }   // 4 倍の上げ下げの山は実測 29 サンプル（重心 28.6〜29.1。DSP 検査係が 44.1〜96 kHz・ブロック 1〜2048 で確認）

        void setPattern (const mytrem::Pattern& p) { const Spin sl (lock); pendingPattern = p; patternDirty.store (true); }
        void setCustomShape (const mytrem::CustomShape& c) { const Spin sl (lock); pendingCustom = c; customDirty.store (true); }

        // 画面の表示用
        std::atomic<float> shownMod { 0 };            // 今の元 0..1
        std::atomic<float> shownHz1 { 1000 }, shownHz2 { 1000 };
        std::atomic<float> shownInDb { -100 };
        std::atomic<float> shownPhase { 0 };          // RHYTHM のパターン内の位置
        std::atomic<float> shownRes1 { 0 }, shownRes2 { 0 };

        void process (float* const* ch, int numSamples, const Knobs& k, const Transport& tr)
        {
            pickUpEdits (k);
            if (curStyle < 0) setStyle (k.style, false);
            else if (k.style != curStyle && fadeLeft == 0) setStyle (k.style, true);

            const double bpm = std::max (1.0, std::min (999.0, k.sync ? tr.hostBpm : (double) k.tempoBpm));
            const double bps = bpm / 60.0 / fs;
            const bool locked = k.sync && tr.hostPlaying && tr.hasPosition;
            if (locked) beat = tr.ppq;
            else if (first) beat = 0;
            first = false;

            const float inT = std::pow (10.0f, k.inDb / 20.0f), outT = std::pow (10.0f, k.outDb / 20.0f);
            if (inS < 0)
            {
                inS = inT; outS = outT; mixS = k.mix; modAmtS = k.mod * k.duck; lrOffS = k.lrOffsetOct;
                for (int f = 0; f < 2; ++f)
                {
                    gS[f] = std::pow (10.0f, k.gainDb[f] / 20.0f); freqKnobS[f] = std::log2 (freqFromNorm (k.freq[f]));
                    resS[f] = k.res[f]; rmodS[f] = k.resMod[f]; lmodS[f] = k.levelMod[f]; fmodS[f] = k.freqModOct[f];
                }
            }
            const float smC = (float) std::exp (-1.0 / (0.02 * fs));
            const float thrLin = std::pow (10.0f, k.thresholdDb / 20.0f);
            const float envG = std::pow (10.0f, k.envGainDb / 20.0f);
            const float trigLin = std::pow (10.0f, k.trigDb / 20.0f);
            const float attC = (float) std::exp (-4.6 / (std::max (0.1f, k.attackMs) * 1e-3 * fs));
            const float relC = (float) std::exp (-4.6 / (std::max (0.1f, k.releaseMs) * 1e-3 * fs));
            const int blkLen = std::max (1, (int) std::lround (fs * (32.0 / 48000.0)));
            const float pkC = (float) std::exp (-(double) blkLen / (0.5e-3 * fs));
            const double unit = mytrem::Modulator::unitFor (k.rhythm, pattern);
            const float* tab = k.shapeIdx >= 0 && k.shapeIdx < mytrem::kNumShapes ? mytrem::kShapes[k.shapeIdx] : customTab.data();
            const int fadeLen = (int) (0.015 * fs);
            const bool monoToStereo = nIn == 1 && nOut == 2;
            const int nFwant = k.filters >= 2 ? 2 : 1;
            const float modAmtT = k.mod * k.duck;
            const float swStep = (float) (1.0 / (0.006 * fs));   // 形・極・フィルターの数・つなぎ方を替える時: 通った音を 6 ms で下げ → 替える → 6 ms で戻す（cos の形でなめらかに）
            if (! appInit) { applyStructure (k, nFwant, false); appInit = true; }

            const float gT[2] { std::pow (10.0f, k.gainDb[0] / 20.0f), std::pow (10.0f, k.gainDb[1] / 20.0f) };
            const float fT[2] { std::log2 (freqFromNorm (k.freq[0])), std::log2 (freqFromNorm (k.freq[1])) };
            // 段の設定（レゾナンスが変わった時だけ計算し直す）
            float resNow[2][2] {};
            float inPk = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                inS = smC * inS + (1 - smC) * inT;
                outS = smC * outS + (1 - smC) * outT;
                mixS = smC * mixS + (1 - smC) * k.mix;
                modAmtS = smC * modAmtS + (1 - smC) * modAmtT;
                lrOffS = smC * lrOffS + (1 - smC) * k.lrOffsetOct;
                for (int f = 0; f < 2; ++f)
                {
                    gS[f] = smC * gS[f] + (1 - smC) * gT[f];
                    freqKnobS[f] = smC * freqKnobS[f] + (1 - smC) * fT[f];
                    resS[f] = smC * resS[f] + (1 - smC) * k.res[f];
                    rmodS[f] = smC * rmodS[f] + (1 - smC) * k.resMod[f];
                    lmodS[f] = smC * lmodS[f] + (1 - smC) * k.levelMod[f];
                    fmodS[f] = smC * fmodS[f] + (1 - smC) * k.freqModOct[f];
                }
                // 形・極・フィルターの数・つなぎ方の切り替え（段の作りが変わるので、そのままだとプツッと鳴る）
                if (structureDiffers (k, nFwant))
                {
                    swPos -= swStep;
                    if (swPos <= 0.0f) { swPos = -16.0f * swStep; applyStructure (k, nFwant, true); }   // 切り替えた後 16 サンプルは 0 のまま（1 倍に戻すフィルターの遅れの間に、切り替えの段差が出てこないように）
                }
                else if (swPos < 1.0f) swPos = std::min (1.0f, swPos + swStep);
                swGain = 0.5f - 0.5f * std::cos ((float) kPi * std::max (0.0f, swPos));
                const int nF = appFilters;
                const float modAmt = modAmtS;

                // 入力の大きさ（INPUT の前、両チャンネルの大きいほう）: 0.67 ms ごとの最大値 → 下がる時だけ 0.5 ms でならす（My Tremolo と同じ測り方）
                float a = 0;
                for (int c = 0; c < nIn; ++c) a = std::max (a, std::abs (ch[c][i]));
                inPk = std::max (inPk, a);
                blkMax = std::max (blkMax, a);
                if (++blkCount >= blkLen) { peak = blkMax > peak ? blkMax : pkC * peak + (1 - pkC) * blkMax; blkMax = 0; blkCount = 0; }

                // 元（0..1）
                const double b = beat;
                beat += bps;
                float m = 0;
                switch (k.mode)
                {
                    case kLFO:
                    {
                        lfoPhase += (double) k.lfoHz / fs;
                        lfoPhase -= std::floor (lfoPhase);
                        m = mytrem::Modulator::shapeAt (tab, lfoPhase);
                        break;
                    }
                    case kRhythm:
                    {
                        const double ramp = 0.0015 * fs * bps;
                        m = mytrem::Modulator::gain (b, k.rhythm, events, unit, tab, 1.0f, 0.0f, 0.0, 0.0, k.groove, ramp);
                        break;
                    }
                    case kEnvelope:
                    {
                        // 本物の決まり（初期状態の −20 dB・GAIN ×4 で、音量 −26〜0 dB の開き方・戻り方を比べた）:
                        // 入力の大きさ（ほぼ瞬時）→ しきい値を超えた分 × GAIN → 0..1 で止める → その量を ATTACK / RELEASE でならす
                        // （ならす前に止めるので、大きな音でも開き方・戻り方の速さは同じ）
                        const float dd = std::max (0.0f, peak - thrLin);
                        const float tgt = std::min (1.0f, dd / 0.25f * envG);
                        env = tgt > env ? attC * env + (1 - attC) * tgt : relC * env + (1 - relC) * tgt;
                        m = env;
                        break;
                    }
                    case kRandom:
                    {
                        // 1 回（RHYTHM の長さ、GROOVE 込み）ごとに新しい値。SMOOTH で前の値からなめらかに
                        const double t = mytrem::Modulator::grooveWarp (b, unit, k.groove);
                        const long long step = (long long) std::floor (t / unit);
                        if (step != lastStep) { rPrev = lastStep < 0 ? nextRand() : rNext; rNext = nextRand(); lastStep = step; }
                        const double ph = t / unit - (double) step;
                        m = glide (rPrev, rNext, (float) ph, k.smoothing, k.smoothMode);
                        break;
                    }
                    case kStep:
                    {
                        // きっかけ（入力がしきい値を上に越えた・TRIGGER）ごとに新しい値へ。SMOOTH × 200 ms でなめらかに
                        const bool over = peak > trigLin || k.trigger;
                        if (over && trigArmed) { stepFrom = stepNow; stepTo = nextRand(); stepT = 0; trigArmed = false; }
                        if (! over && peak < trigLin * 0.7f) trigArmed = true;   // 3 dB 下がったら次を受け付ける
                        const float glideS = std::max (1.0e-4f, k.smoothing * 0.2f);
                        stepT = std::min (1.0f, stepT + (float) (1.0 / (glideS * fs)));
                        stepNow = k.smoothing < 0.001f ? stepTo : stepFrom + (stepTo - stepFrom) * mytrem::CustomShape::curve (stepT, k.smoothMode);
                        m = stepNow;
                        break;
                    }
                    default:   // ADSR
                    {
                        const bool want = k.trigger || (gateOn ? peak > trigLin * 0.7f : peak > trigLin);
                        if (want && ! gateOn) adsrStage = 1;           // 押した: ATTACK から
                        if (! want && gateOn) adsrStage = 4;           // 離した: RELEASE
                        gateOn = want;
                        if (adsrStage == 1)
                        {
                            adsr += (float) (1.0 / (std::max (0.1f, k.aMs) * 1e-3 * fs));
                            if (adsr >= 1.0f) { adsr = 1.0f; adsrStage = 2; }
                        }
                        else if (adsrStage == 2 || adsrStage == 3)
                        {
                            const float c = (float) std::exp (-4.6 / (std::max (0.1f, k.dMs) * 1e-3 * fs));
                            adsr = k.sustain + (adsr - k.sustain) * c;
                        }
                        else if (adsrStage == 4)
                        {
                            const float c = (float) std::exp (-4.6 / (std::max (0.1f, k.rMs) * 1e-3 * fs));
                            adsr *= c;
                            if (adsr < 1e-5f) { adsr = 0; adsrStage = 0; }
                        }
                        m = adsr;
                        break;
                    }
                }
                m = std::max (0.0f, std::min (1.0f, m));
                // 元の段差（RANDOM の SMOOTH 0、STEP・パターンの切り替え）は 1 ms だけならす（周波数の段差でプツッと鳴らないように）
                const float mc = (float) std::exp (-1.0 / (0.001 * fs));
                for (int c = 0; c < 2; ++c)
                {
                    const float mm = (c == 1 && monoToStereo && k.lrFlip) ? 1.0f - m : m;
                    modS[c] = mc * modS[c] + (1 - mc) * mm;
                }

                // 周波数・レゾナンス（チャンネル × フィルター）
                double g[2][2], dsh[2][2];
                bool osc = false;
                for (int c = 0; c < nOut; ++c)
                    for (int f = 0; f < nF; ++f)
                    {
                        const float mv = modS[c];
                        float oct = modAmt * fmodS[f] * mv;
                        if (c == 1 && monoToStereo && nF == 1) oct += lrOffS;
                        const float hzLog = freqKnobS[f] + oct;
                        const float hz = std::max (kMinHz, std::min (kMaxHz, std::exp2 (hzLog)));
                        g[c][f] = std::tan (kPi * (double) hz / fs4);
                        dsh[c][f] = dampShift (hz);
                        if (c == 0) (f == 0 ? shownHz1 : shownHz2).store (hz);
                        resNow[c][f] = std::max (0.0f, std::min (1.0f, resS[f] + modAmt * rmodS[f] * mv));
                        flt[c][f].setRes (resNow[c][f], appPoles[f], appShape[f]);
                        if (resNow[c][f] > (float) kOscStart) osc = true;
                    }

                // レベル（LEVEL MOD: + = 元が小さい所で下げる、− = 元が大きい所で下げる）
                float lvl[2][2];
                for (int c = 0; c < nOut; ++c)
                    for (int f = 0; f < nF; ++f)
                    {
                        const float lm = lmodS[f] * modAmt, mv = modS[c];
                        lvl[c][f] = gS[f] * (lm >= 0 ? 1.0f - lm * (1.0f - mv) : 1.0f + lm * mv);
                    }

                // 音
                float src[2];
                for (int c = 0; c < nOut; ++c) src[c] = ch[std::min (c, nIn - 1)][i];
                const float wet = std::sin (0.5f * (float) kPi * mixS), dryAmt = std::cos (0.5f * (float) kPi * mixS);
                for (int c = 0; c < nOut; ++c)
                {
                    float o4[4];
                    os[c].up (src[c] * inS, o4);
                    const float wOld = fadeLeft > 0 ? (float) fadeLeft / (float) fadeLen : 0.0f;
                    const float wOldOut = wOld;
                    for (int q = 0; q < 4; ++q)
                        o4[q] = path (c, o4[q], g[c], dsh[c], lvl[c], nF, appParallel, monoToStereo, wOld, osc);
                    const float w = os[c].down (o4);
                    // 元の音（同じだけ遅らせる）
                    auto& d = dry[(size_t) c];
                    const float dv = d[(size_t) dryPos];
                    d[(size_t) dryPos] = src[c];
                    // DIGITAL: 本物と同じく出口（元の速さ）で ±0.988 に硬く止める（4 倍の所だけで止めると、戻す時のはね返りで 1.5 まで出る）
                    // 色付けを替えて重ねている間は、止める量も重ねる（DIGITAL ⇄ ほかの切り替えで段差が出ないように）
                    const float wcl = std::max (-0.988f, std::min (0.988f, w));
                    const float dNew = curStyle == 0 ? 1.0f : 0.0f, dOld = oldStyle == 0 ? 1.0f : 0.0f;
                    const float dw = fadeLeft > 0 ? dNew * (1.0f - wOldOut) + dOld * wOldOut : dNew;
                    const float wc = w + (wcl - w) * dw;
                    ch[c][i] = std::max (-4.0f, std::min (4.0f, wc * swGain)) * wet + dv * dryAmt;   // 念のための天井（+12 dBFS）は通った音だけに
                }
                dryPos = (dryPos + 1) % (int) dry[0].size();
                if (fadeLeft > 0) --fadeLeft;
            }
            // 自走が長く続いたら拍を折り返す
            if (! locked && beat > 1.0e6) beat -= std::floor (beat / 64.0) * 64.0;

            shownMod.store (modS[0]);
            shownInDb.store (20.0f * std::log10 (inPk + 1e-9f));
            shownRes1.store (resNow[0][0]); shownRes2.store (appFilters > 1 ? resNow[0][1] : 0.0f);
            {
                const double len = k.rhythm == mytrem::kRhythmPattern ? events.length : std::max (4.0, mytrem::rhythmBeats (k.rhythm));
                shownPhase.store ((float) (mytrem::Modulator::floorMod (beat, len) / len));
            }
        }

        // 画面に描く用: フィルター 1 つの振幅特性（dB）。shape・poles・res・周波数から（4 倍の速さの双一次の式で）
        static float responseDb (float hz, float fcHz, float res, int shape, int poles, double fsHi)
        {
            Filter f; f.setRes (res, poles, shape);
            const double g = std::tan (kPi * (double) std::max (kMinHz, std::min (kMaxHz, fcHz)) / fsHi);
            // s = (1/g)·(z−1)/(z+1) を周波数 hz で: s = j·tan(π hz / fs)/g
            const double w = std::tan (kPi * (double) hz / fsHi) / g;
            const double R = std::max (2e-4, effDamp (f.R, dampShift (fcHz)));
            // 1 段の式
            const double re = 1.0 - w * w, im = 2.0 * R * w;
            const double den = std::sqrt (re * re + im * im);
            double mag;
            switch (shape)
            {
                case kLP: mag = 1.0 / den; break;
                case kHP: mag = w * w / den; break;
                case kBP: mag = (f.qg * w) / den; break;
                default:  mag = std::abs (1.0 - w * w + (2.0 * R - f.qg) * 0.0) / den; break;
            }
            double total = std::pow (mag, f.n);
            if (shape != kBR) total *= f.gainAll;
            return (float) (20.0 * std::log10 (std::max (1e-9, total)));
        }

    private:
        struct SpinLock { std::atomic_flag f = ATOMIC_FLAG_INIT; void lock() { while (f.test_and_set (std::memory_order_acquire)) {} } bool tryLock() { return ! f.test_and_set (std::memory_order_acquire); } void unlock() { f.clear (std::memory_order_release); } };
        struct Spin { SpinLock& l; explicit Spin (SpinLock& s) : l (s) { l.lock(); } ~Spin() { l.unlock(); } };

        double fs = 48000, fs4 = 192000;
        int nIn = 2, nOut = 2;
        mytrem::Oversampler4 os[maxCh];
        mytrem::StyleChannel style[maxCh], styleOld[maxCh];
        Filter flt[maxCh][2];
        int curStyle = -1, oldStyle = -1, fadeLeft = 0;

        double lfoPhase = 0, beat = 0;
        bool first = true;
        float env = 0, peak = 0, blkMax = 0;
        int blkCount = 0;
        uint32_t rnd = 0x12345678u;
        float rPrev = 0.5f, rNext = 0.5f;
        long long lastStep = -1;
        float stepT = 1, stepFrom = 0.5f, stepTo = 0.5f, stepNow = 0.5f;
        bool trigArmed = true;
        float adsr = 0; int adsrStage = 0; bool gateOn = false;
        float modS[2] { 0, 0 };
        float inS = -1, outS = -1, mixS = -1;
        float gS[2] { 1, 1 };
        float freqKnobS[2] { 10, 10 };
        float resS[2] { 0, 0 }, rmodS[2] { 0, 0 }, lmodS[2] { 0, 0 }, fmodS[2] { 8, 8 }, modAmtS = 0, lrOffS = 0;
        // 今使っている段の作り（切り替えは swGain を 0 にしてから）
        int appShape[2] { kLP, kBP }, appPoles[2] { 2, 2 }, appFilters = 1;
        bool appParallel = false, appInit = false;
        float swGain = 1.0f, swPos = 1.0f;
        bool structureDiffers (const Knobs& k, int nFwant) const
        {
            if (nFwant != appFilters || (nFwant > 1 && k.parallel != appParallel)) return true;
            for (int f = 0; f < nFwant; ++f) if (k.shape[f] != appShape[f] || k.poles[f] != appPoles[f]) return true;
            return false;
        }
        void applyStructure (const Knobs& k, int nFwant, bool resetChanged)
        {
            for (int f = 0; f < 2; ++f)
            {
                // 形が変わった・2 つ目を使い始めた時だけ空にする（極の数だけなら、残る段はそのまま・増えた段は setRes が空にする）
                const bool changed = k.shape[f] != appShape[f] || (f == 1 && nFwant > appFilters);
                if (resetChanged && changed) for (int c = 0; c < maxCh; ++c) { flt[c][f].reset(); flt[c][f].lastN = -1; }
                appShape[f] = k.shape[f]; appPoles[f] = k.poles[f];
            }
            appFilters = nFwant; appParallel = k.parallel;
        }
        std::vector<float> dry[maxCh];
        int dryPos = 0;

        SpinLock lock;
        mytrem::Pattern pattern, pendingPattern;
        mytrem::EventList events;
        std::atomic<bool> patternDirty { true };
        mytrem::CustomShape custom, pendingCustom;
        std::atomic<bool> customDirty { true };
        std::array<float, mytrem::kShapeLen> customTab {};
        float lastSmooth = -1; int lastMode = -1;

        uint32_t nrnd = 0x9e3779b9u;
        inline float noise() { nrnd = nrnd * 1664525u + 1013904223u; return ((float) (nrnd >> 9) * (1.0f / 8388608.0f) - 0.5f) * 2.0e-7f; }
        float nextRand() { rnd = rnd * 1664525u + 1013904223u; return (float) ((rnd >> 8) & 0xFFFFFF) / 16777215.0f; }

        static float glide (float from, float to, float ph, float smoothing, int mode)
        {
            const float sm = std::max (0.0f, std::min (1.0f, smoothing));
            if (sm <= 0.0f) return to;
            const float u = (ph - (1.0f - sm)) / sm;
            if (u <= 0.0f) return from;
            return from + (to - from) * mytrem::CustomShape::curve (std::min (1.0f, u), mode);
        }

        // 1 サンプル（4 倍の速さ）: 色付けの前半 → フィルター → OUTPUT → 色付けの後半。
        // 色付けを替えた直後（wOld > 0）は前後の色付けを 15 ms 重ねる（フィルターは 1 回だけ通す）
        inline float path (int c, float x, const double* g, const double* d, const float* lvl, int nF, bool parallel, bool m2s, float wOld, bool osc)
        {
            float v = style[c].front (x);
            if (wOld > 0.0f) v = v * (1.0f - wOld) + styleOld[c].front (x) * wOld;
            double y;
            Filter* f = flt[c];
            if (osc) v += noise();   // 発振する設定（RES 0.965 から上）の時だけ、−140 dB のごく小さな揺らぎ（本物と同じく無音からでも鳴り出すように）
            if (nF == 1) y = f[0].process (v, g[0], d[0]) * lvl[0];
            else if (m2s) y = c == 0 ? f[0].process (v, g[0], d[0]) * lvl[0] : f[1].process (v, g[1], d[1]) * lvl[1];   // モノ → ステレオ: 1 を左、2 を右
            else if (parallel) y = f[0].process (v, g[0], d[0]) * lvl[0] + f[1].process (v, g[1], d[1]) * lvl[1];
            else y = f[1].process (f[0].process (v, g[0], d[0]) * lvl[0], g[1], d[1]) * lvl[1];
            const float yo = (float) y * outS;
            float out = style[c].back (yo);
            if (wOld > 0.0f) out = out * (1.0f - wOld) + styleOld[c].back (yo) * wOld;
            return out;
        }

        void pickUpEdits (const Knobs& k)
        {
            bool shapeChanged = false;
            if (patternDirty.load() || customDirty.load())
            {
                if (lock.tryLock())
                {
                    if (patternDirty.exchange (false)) { pattern = pendingPattern; events.build (pattern); }
                    if (customDirty.exchange (false)) { custom = pendingCustom; shapeChanged = true; }
                    lock.unlock();
                }
            }
            if (shapeChanged || k.smoothing != lastSmooth || k.smoothMode != lastMode)
            {
                lastSmooth = k.smoothing; lastMode = k.smoothMode;
                custom.render (customTab.data(), mytrem::kShapeLen, k.smoothing, k.smoothMode);
            }
        }

        void setStyle (int s, bool crossfade)
        {
            if (crossfade) { for (int c = 0; c < maxCh; ++c) styleOld[c] = style[c]; fadeLeft = (int) (0.015 * fs); }
            oldStyle = curStyle < 0 ? s : curStyle;
            curStyle = s;
            const auto sp = mytrem::styleParams (s);
            for (int c = 0; c < maxCh; ++c)
            {
                style[c].prepare (fs4, sp);
                if (crossfade)
                {
                    style[c].hp.z1 = styleOld[c].hp.z1; style[c].hp.z2 = styleOld[c].hp.z2;
                    style[c].lowShelf.z = styleOld[c].lowShelf.z;
                    style[c].deemph.z = styleOld[c].deemph.z;
                }
            }
        }
    };
}
