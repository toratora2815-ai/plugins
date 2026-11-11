#pragma once
// My Crystal の音の処理（JUCE なし）。粒（スプライス）で音程を変え、逆再生もできるエコー。
// 本人の Mac にある市販のグラニュラー・エコーを測って同じ動きにした（build/crylab、NOTES.md）。
//
// 流れ（1 チャンネル）:
//   入口 ×INPUT → [ANALOG の 1 段目（4 倍）] ＋ くり返し（RECYCLE）→ 遅れの輪
//   → 粒 2 つ（遅れ d が音程に合わせて動き、窓の端でつなぎ直す）→ LOW CUT → HIGH CUT → (くり返しへ)
//   → GATE/DUCK・VOCAL DUCK → ×OUTPUT → [ANALOG の 2 段目（4 倍）/ DIGITAL は −0.1 dB で頭打ち] → MIX（cos / sin）
//
// 粒の決まり（測った本物と同じ）:
//   - 遅れ d は 1 サンプルごとに (1 − 音程比) 動く（逆再生は音程比をマイナスに）。窓は [lo, lo + SPLICE]、lo = max(DELAY, 9.75 ms)（出口で +0.58 ms）
//   - 窓から出たら SPLICE ちょうどだけ戻し、古い粒と新しい粒を直線でつなぐ（長さ = SMOOTHING、ただし 1 粒の長さの半分まで）
//   - 遅れが減っていく向き（音程を上げた順再生）は、古い粒が「今」に追いつく前に終わるよう、つなぎを短くする（+12 で約 5 ms）
//   - 音程 0 の順再生は遅れが動かない（ただのディレイ）
#include "TremoloStyle.h"        // My Tremolo の色付け（StyleChannel）・4 倍オーバーサンプラー・Biquad
#include "TremoloStyleTables.h"
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>

namespace mycry
{
    constexpr double kPi = 3.14159265358979323846;
    enum FbMode { kMixed = 0, kDual, kPingPong };
    enum DuckMode { kDuckOut = 0, kDuckRecycle, kDuckBoth };
    constexpr int kAnalogStyle = 1;          // My Tremolo の Smooth（= 本物の ANALOG と奇数倍音・音量の下がり方が ±0.5 dB で一致）

    // テンポ同期の長さ（拍）。SPLICE 7 段・DELAY 8 段（本物と同じ並び）
    constexpr int kNumSpliceNotes = 7, kNumDelayNotes = 8;
    inline double spliceNoteBeats (int i) { static const double b[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 }; return b[std::clamp (i, 0, kNumSpliceNotes - 1)]; }
    inline double delayNoteBeats (int i) { static const double b[] = { 0.0, 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 }; return b[std::clamp (i, 0, kNumDelayNotes - 1)]; }
    inline const char* spliceNoteName (int i) { static const char* n[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars" }; return n[std::clamp (i, 0, kNumSpliceNotes - 1)]; }
    inline const char* delayNoteName (int i) { static const char* n[] = { "0", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar" }; return n[std::clamp (i, 0, kNumDelayNotes - 1)]; }

    constexpr double kMaxSpliceMs = 4100.0, kMaxDelayMs = 4100.0;   // つまみは 2050 ms まで。テンポ同期の長い音符はここで止める

    struct Knobs
    {
        float inDb = 0, outDb = 0, mix = 0.5f;
        bool analog = true;
        float pitchCents = 0;                // ±3600
        bool reverse = true;
        float spliceMs = 500, delayMs = 0;   // 自由なとき
        bool sync = true;
        int spliceNote = 3, delayNote = 4;   // テンポ同期のとき
        float tempoBpm = 120;                // Logic のテンポが来ないとき
        float recycle = 0.21f;               // 0..1（1 回ごとの倍率そのもの）
        float thresholdDb = -40, gateDuckDb = 0;   // + = DUCK、− = GATE（dB）
        float pitchOffCents = 12;            // 左 −、右 +
        float spliceOffPct = 6, delayOffPct = 2;   // 右だけ短く
        float smoothMs = 20;
        float lowCutHz = 10, highCutHz = 10000;
        float attackMs = 100, releaseMs = 500;
        int duckMode = kDuckOut, fbMode = kMixed;
        float vocalDuck = 1.0f;              // Plugin Link（Fader Pilot が歌っている間のウェットの倍率）
    };

    struct Transport { double hostBpm = 120; bool hostPlaying = false, hasPosition = false; double ppq = 0; };

    //==============================================================================
    // 遅れの輪（2 の累乗の長さ）と直線補間（測った本物も直線補間: くり返すたびに 10 kHz で −0.7 dB ほど暗くなる所まで一致）
    struct Line
    {
        std::vector<float> b;
        int mask = 0, w = 0;
        void prepare (int minLen) { int n = 1; while (n < minLen) n <<= 1; b.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
        void reset() { std::fill (b.begin(), b.end(), 0.0f); w = 0; }
        inline void push (float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
        // 「最後に書いた所」から d サンプル前（d ≥ 1）
        inline float read (double d) const
        {
            const double pos = (double) (w - 1) - d;
            const double fl = std::floor (pos);
            const float f = (float) (pos - fl);
            const int i = (int) fl;
            const float x0 = b[(size_t) (i & mask)], x1 = b[(size_t) ((i + 1) & mask)];
            return x0 + (x1 - x0) * f;
        }
    };

    //==============================================================================
    // 2 次のバターワース（12 dB/oct）。TPT 型の SVF（係数を動かしても暴れない。応答は双一次の Biquad と同じ）
    struct SvfCoef
    {
        float a1 = 1, a2 = 0, a3 = 0;
        void set (double fs, double hz)
        {
            const double g = std::tan (kPi * std::min (hz, 0.45 * fs) / fs);
            const double k = 1.4142135623730951;
            const double b1 = 1.0 / (1.0 + g * (g + k));
            a1 = (float) b1; a2 = (float) (g * b1); a3 = (float) (g * g * b1);
        }
    };
    struct SvfState
    {
        float ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        // lowpass = true で低域通過、false で高域通過
        inline float tick (const SvfCoef& c, float x, bool lowpass)
        {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            return lowpass ? v2 : x - 1.4142135f * v1 - v2;
        }
    };
    // 1 次の直流カット（くり返しの輪の中に固定で 5 Hz。LOW CUT を 1 Hz にして音程を下げて回しても、直流・超低域がたまらない）
    struct DcCut
    {
        float x1 = 0, y1 = 0, R = 0.9993f;
        void set (double fs, double hz) { R = (float) std::exp (-2.0 * kPi * hz / fs); }
        void reset() { x1 = y1 = 0; }
        inline float process (float x) { const float y = 0.5f * (1.0f + R) * (x - x1) + R * y1; x1 = x; y1 = y; return y; }   // 高い周波数で 1 倍ちょうど（持ち上げない）
    };

    //==============================================================================
    // 粒 1 チャンネル分（遅れ d と、つないでいる途中の古い粒）
    struct Grains
    {
        double d = 0, dOld = 0;
        double fade = 1, fadeStep = 0;     // 0 → 1（新しい粒の割合）
        bool fading = false;
        bool placed = false;

        void reset() { fading = false; fade = 1; placed = false; }

        // 1 サンプル進める。rate = 1 − 音程比（サンプル / サンプル）、lo・S（サンプル）、smooth（サンプル）、floor（減っていく向きの下限）
        inline void step (double rate, double lo, double S, double smooth, double floorD, double maxD)
        {
            if (! placed) { d = lo + 0.95 * S; placed = true; }
            d += rate;
            if (fading)
            {
                dOld += rate;
                fade += fadeStep;
                if (fade >= 1.0) { fade = 1.0; fading = false; }
                // 古い粒が読めない所まで来たら、そこで終わり
                if (fading && (dOld < floorD * 0.5 || dOld > maxD)) { fading = false; fade = 1.0; }
            }
            const double hi = lo + S;
            // 遅れが減っていく向きは、つなぎが下限（floor）で終わるよう、早めに始める（+12 で約 5 ms、+24 で約 3 ms のつなぎ。測った本物）
            const double Tdec = rate < 0 ? decFade (rate, lo, S, smooth, floorD) : 0.0;
            const double trigLo = rate < 0 ? std::max (lo, floorD - rate * Tdec) : lo;
            // 動いている向きの端でだけつなぎ直す（反対側の端は、つまみで窓が大きく動いた時だけ）
            const double topEdge = rate < -1e-12 ? std::max (hi, trigLo + S) + 1.0 : hi;
            const double botEdge = rate > 1e-12 ? lo - 1.0 : trigLo;
            if (! fading && (d > topEdge || d < botEdge))
            {
                const double from = d;
                if (d > topEdge) d -= S * std::ceil ((d - hi) / S);
                else             d += S * std::ceil ((trigLo - d) / S);
                d = std::clamp (d, lo, std::max (hi, trigLo + S));
                startFade (from, rate, S, smooth, floorD);
            }
            // 安全のため（窓が急に動いた・つなぎの途中で外へ出た）
            d = std::clamp (d, floorD * 0.5, maxD);
        }
        // テンポ同期: 拍の区切りで粒を頭から（逆再生・音程を下げた順再生 = 窓の下端から、音程を上げた順再生 = 上端から）
        inline void restart (double rate, double lo, double S, double smooth, double floorD)
        {
            if (std::abs (rate) < 1e-9 || fading) return;
            const double to = rate > 0 ? lo : lo + S;
            if (std::abs (to - d) < 2.0) return;
            const double from = d;
            d = to;
            startFade (from, rate, S, smooth, floorD);
        }
        // 遅れが減っていく向きのつなぎの長さ: 余裕があれば SMOOTHING、なければ「5.2 ms ÷ |速さ|^0.57」（測った本物: +12 で 5.2 ms、+24 で 2.8 ms）
        double powAr = -1, powVal = 1;   // |速さ|^0.57 の覚え（毎サンプルの pow を避ける）
        double decFade (double rate, double lo, double S, double smooth, double floorD)
        {
            const double ar = std::max (1e-9, -rate);
            const double fsMs = floorD / 4.9;   // 1 ms のサンプル数
            if (std::abs (ar - powAr) > 1e-4 * ar) { powAr = ar; powVal = std::pow (ar, 0.57); }
            const double shortT = 5.2 * fsMs / powVal;
            double T = std::min (smooth, 0.5 * S / ar);
            T = std::min (T, std::max (shortT, (lo - floorD) / ar));
            return std::max (1.0, T);
        }
        inline void startFade (double from, double rate, double S, double smooth, double floorD)
        {
            const double ar = std::abs (rate);
            double T = smooth;
            if (ar > 1e-9) T = std::min (T, 0.5 * S / ar);                 // 1 粒の長さの半分まで
            if (rate < 0) T = std::min (T, std::max (1.0, (from - floorD) / ar));   // 古い粒が「今」に追いつく前に終わる
            T = std::max (1.0, T);
            dOld = from; fade = 0.0; fadeStep = 1.0 / T; fading = true;
        }
        inline float read (const Line& ln, double comp) const
        {
            const float y = ln.read (std::max (1.0, d - comp));
            if (! fading) return y;
            const float yo = ln.read (std::max (1.0, dOld - comp));
            const float a = (float) fade;
            return yo * (1.0f - a) + y * a;
        }
    };

    //==============================================================================
    class Engine
    {
    public:
        static constexpr int maxCh = 2;

        void prepare (double sampleRate, int numChannels)
        {
            fs = sampleRate;
            nCh = std::clamp (numChannels, 1, maxCh);
            const int len = (int) std::ceil ((kMaxDelayMs + 1.6 * kMaxSpliceMs + 200.0) * 1e-3 * fs) + 64;
            for (int c = 0; c < maxCh; ++c)
            {
                line[c].prepare (len);
                maxD = (double) (line[c].mask - 8);
                gr[c].reset();
                front[c].prepare (fs * 4, mytrem::styleParams (kAnalogStyle));
                back[c].prepare (fs * 4, mytrem::styleParams (kAnalogStyle));
                osF[c].prepare(); osF[c].reset(); osB[c].prepare(); osB[c].reset();
                hpS[c].reset(); lpS[c].reset(); dc[c].set (fs, 5.0); dc[c].reset();
                fbDelay[c].fill (0.0f);
            }
            first = true; cutCount = 0;
            cutC = std::exp (-1.0 / (0.025 * fs));
            modeStep = (float) (1.0 / (0.015 * fs)); anaStep = (float) (1.0 / (0.010 * fs));
            det = 0; holdLeft = 0; above = false; gdState = 0;
            smC = (float) std::exp (-1.0 / (0.02 * fs));
            slowC = std::exp (-1.0 / (0.03 * fs));
            holdLen = (int) std::lround (0.0045 * fs);
            detRel = (float) std::exp (-1.0 / (0.0122 * fs));
            lastGrid = -1;
        }

        void reset()
        {
            for (int c = 0; c < maxCh; ++c) { line[c].reset(); gr[c].reset(); front[c].reset(); back[c].reset(); osF[c].reset(); osB[c].reset(); hpS[c].reset(); lpS[c].reset(); dc[c].reset(); fbDelay[c].fill (0.0f); }
            first = true; det = 0; above = false; gdState = 0; lastGrid = -1;
        }

        // 画面の表示用
        std::atomic<float> shownPos[2] { { 0.95f }, { 0.95f } };   // 窓の中の位置 0..1（0 = 下端）
        std::atomic<float> shownInDb { -100 }, shownGateDb { 0 };
        std::atomic<float> shownWindowMs { 500 }, shownLoMs { 11 };
        std::atomic<bool> shownAbove { false };

        // ch: 入口と出口（その場で書き換える）。numCh チャンネル
        void process (float* const* ch, int numSamples, const Knobs& k, const Transport& tr)
        {
            const double bpmFree = std::clamp ((double) k.tempoBpm, 30.0, 240.0);
            const double bpm = k.sync && tr.hostBpm > 1.0 ? std::clamp (tr.hostBpm, 20.0, 999.0) : bpmFree;
            const double beatSamples = 60.0 / bpm * fs;

            // 長さ（サンプル）
            double Sms, Dms;
            if (k.sync) { Sms = spliceNoteBeats (k.spliceNote) * 60000.0 / bpm; Dms = delayNoteBeats (k.delayNote) * 60000.0 / bpm; }
            else { Sms = k.spliceMs; Dms = k.delayMs; }
            Sms = std::clamp (Sms, 1.0, kMaxSpliceMs);
            Dms = std::clamp (Dms, 0.0, kMaxDelayMs);
            const double Starget[2] = { Sms * 1e-3 * fs, Sms * 1e-3 * fs * std::max (0.01, 1.0 - k.spliceOffPct / 100.0) };
            const double Dtarget[2] = { Dms * 1e-3 * fs, Dms * 1e-3 * fs * std::max (0.0, 1.0 - k.delayOffPct / 100.0) };
            const double ratioT[2] = { std::exp2 (std::clamp ((double) k.pitchCents - k.pitchOffCents, -4800.0, 4800.0) / 1200.0),
                                       std::exp2 (std::clamp ((double) k.pitchCents + k.pitchOffCents, -4800.0, 4800.0) / 1200.0) };
            const double sgnT = k.reverse ? -1.0 : 1.0;   // 向きもならす（読む速さが +1 から −1 へ折れないように）
            const double smoothS = std::clamp ((double) k.smoothMs, 20.0, 2000.0) * 1e-3 * fs;
            // 窓の下端（測った本物を、元の音からの遅れで合わせた。2 段目の 4 倍の遅れ 0.58 ms を足すと本物と同じ 10.3 ms）
            const double floorD = 0.0049 * fs;
            const double minLo = 0.00975 * fs, loPad = 0.0;
            // 1 段目（4 倍）の遅れ 28 と、「書く前に読む」1 サンプルの分だけ手前を読む（くり返しの間隔がちょうど d になる）
            const double comp = (double) mytrem::Oversampler4::latency() + 1.0;

            const float inT = std::pow (10.0f, k.inDb / 20.0f), outT = std::pow (10.0f, k.outDb / 20.0f);
            const float wetT = (float) std::sin (0.5 * kPi * std::clamp (k.mix, 0.0f, 1.0f));
            const float dryT = (float) std::cos (0.5 * kPi * std::clamp (k.mix, 0.0f, 1.0f));
            const float recT = std::clamp (k.recycle, 0.0f, 1.0f);
            const float gdT = std::clamp (k.gateDuckDb, -60.0f, 60.0f);
            const float thr = std::pow (10.0f, k.thresholdDb / 20.0f), thrOff = thr * 0.5623413f;   // 切れるのは 5 dB 下（測った本物）
            const double attS = std::max (1.0, k.attackMs * 1e-3 * fs), relS = std::max (1.0, k.releaseMs * 1e-3 * fs);

            if (first)
            {
                inS = inT; outS = outT; wetS = wetT; dryS = dryT; recS = recT; gdS = gdT; vdS = k.vocalDuck;
                sgnS = sgnT; curMode = prevMode = k.fbMode; modeX = 1.0f; anaX = k.analog ? 1.0f : 0.0f;
                logLo = std::log (std::clamp ((double) k.lowCutHz, 1.0, 5000.0)); logHi = std::log (std::clamp ((double) k.highCutHz, 500.0, 20000.0));
                hpC.set (fs, std::exp (logLo)); lpC.set (fs, std::exp (logHi));
                for (int c = 0; c < 2; ++c) { S[c] = Starget[c]; D[c] = Dtarget[c]; ratio[c] = ratioT[c]; }
                // 右の粒は左より 18.2 ms 手前から始める（本物と同じ。左右が同時につなぎ直さない）
                gr[0].d = std::max (minLo, D[0]) + loPad + 0.95 * S[0]; gr[0].placed = true;
                gr[1].d = std::max (minLo, D[1]) + loPad + 0.95 * S[1] - 0.0182 * fs; gr[1].placed = true;
                first = false;
            }
            const double logLoT = std::log (std::clamp ((double) k.lowCutHz, 1.0, 5000.0));
            const double logHiT = std::log (std::clamp ((double) k.highCutHz, 500.0, 20000.0));
            if (k.fbMode != curMode) { prevMode = curMode; curMode = k.fbMode; modeX = 0.0f; }   // FEEDBACK を替えたら 15 ms で混ぜる
            const float anaT = k.analog ? 1.0f : 0.0f;

            const bool locked = k.sync && tr.hostPlaying && tr.hasPosition;
            const double spliceBeats = spliceNoteBeats (k.spliceNote);
            const float vdT = k.vocalDuck;
            float inPk = 0;
            const bool stereo = nCh > 1;

            for (int i = 0; i < numSamples; ++i)
            {
                inS = smC * inS + (1 - smC) * inT;   outS = smC * outS + (1 - smC) * outT;
                wetS = smC * wetS + (1 - smC) * wetT; dryS = smC * dryS + (1 - smC) * dryT;
                recS = smC * recS + (1 - smC) * recT; gdS = smC * gdS + (1 - smC) * gdT;
                vdS = smC * vdS + (1 - smC) * vdT;
                sgnS = slowC * sgnS + (1 - slowC) * sgnT;
                // LOW / HIGH CUT: 周波数を log で 25 ms ならし、16 サンプルごとに係数を作り直す
                logLo = cutC * logLo + (1 - cutC) * logLoT; logHi = cutC * logHi + (1 - cutC) * logHiT;
                if (++cutCount >= 16) { cutCount = 0; hpC.set (fs, std::exp (logLo)); lpC.set (fs, std::exp (logHi)); }
                if (modeX < 1.0f) modeX = std::min (1.0f, modeX + modeStep);
                if (anaX != anaT) anaX = anaT > anaX ? std::min (anaT, anaX + anaStep) : std::max (anaT, anaX - anaStep);
                for (int c = 0; c < 2; ++c)
                {
                    S[c] = slowC * S[c] + (1 - slowC) * Starget[c];
                    D[c] = slowC * D[c] + (1 - slowC) * Dtarget[c];
                    ratio[c] = slowC * ratio[c] + (1 - slowC) * ratioT[c];
                }

                const float xL = ch[0][i], xR = stereo ? ch[1][i] : xL;

                // GATE / DUCK の耳（入口の大きさ。INPUT の前、左右の大きいほう）: すぐ上がり、4.5 ms 待って 12 ms で下がる。5 dB の余裕
                const float a = std::max (std::abs (xL), std::abs (xR));
                inPk = std::max (inPk, a);
                if (a >= det) { det = a; holdLeft = holdLen; }
                else if (holdLeft > 0) --holdLeft;
                else det *= detRel;
                if (! above && det > thr) above = true;
                else if (above && det < thrOff) above = false;
                // 量は dB で直線に（ATTACK = 越えてから全部効くまで、RELEASE = 下がってから戻るまで）
                if (above) gdState = std::min (1.0, gdState + 1.0 / attS);
                else       gdState = std::max (0.0, gdState - 1.0 / relS);
                float redDb = 0;
                if (gdS > 0.01f) redDb = gdS * (float) gdState;            // DUCK: 鳴っている間だけ下げる
                else if (gdS < -0.01f) redDb = -gdS * (float) (1.0 - gdState);   // GATE: 鳴っていない間だけ下げる
                const float gdGain = redDb > 0.001f ? std::exp (-0.11512925f * redDb) : 1.0f;
                const float gOut = (k.duckMode == kDuckOut || k.duckMode == kDuckBoth) ? gdGain : 1.0f;
                const float gRec = (k.duckMode == kDuckRecycle || k.duckMode == kDuckBoth) ? gdGain : 1.0f;

                // テンポ同期: 粒を拍の区切りで頭から
                bool restartNow = false;
                if (locked)
                {
                    const double ppqNow = tr.ppq + (double) i / beatSamples;
                    const long long gidx = (long long) std::floor (ppqNow / spliceBeats + 1e-9);
                    if (lastGrid >= 0 && gidx != lastGrid) restartNow = true;
                    lastGrid = gidx;
                }
                else lastGrid = -1;

                // 粒を読む → LOW CUT → HIGH CUT
                float w[2];
                const int nUse = stereo ? 2 : 1;
                for (int c = 0; c < nUse; ++c)
                {
                    const double r = sgnS * ratio[c];
                    const double rate = 1.0 - r;
                    const double lo = std::max (minLo, D[c]) + loPad;
                    if (restartNow) gr[c].restart (rate, lo, S[c], smoothS, floorD);
                    gr[c].step (rate, lo, S[c], smoothS, floorD, maxD);
                    float y = gr[c].read (line[c], comp);
                    y = lpS[c].tick (lpC, hpS[c].tick (hpC, y, false), true);
                    w[c] = y;
                    wfb[c] = dc[c].process (y);   // くり返しに戻す分だけ直流カット
                }
                if (! stereo) { w[1] = w[0]; wfb[1] = wfb[0]; }

                // くり返し（RECYCLE）。Mixed = 左右を足して √2 で割る（本物と同じ）、Dual = 別々、Ping-Pong = 左 → 右（そのまま）→ 左（RECYCLE）
                // FEEDBACK を替えた直後は、前のつなぎ方と 15 ms 混ぜる
                float inW[2], fb[2];
                const float rec = recS * gRec;
                if (! stereo) { inW[0] = xL * inS; fb[0] = rec * wfb[0]; inW[1] = 0; fb[1] = 0; }
                else
                {
                    route (curMode, xL, xR, wfb, rec, gRec, inW, fb);
                    if (modeX < 1.0f)
                    {
                        float inO[2], fbO[2];
                        route (prevMode, xL, xR, wfb, rec, gRec, inO, fbO);
                        for (int c = 0; c < 2; ++c) { inW[c] = inO[c] + (inW[c] - inO[c]) * modeX; fb[c] = fbO[c] + (fb[c] - fbO[c]) * modeX; }
                    }
                    inW[0] *= inS; inW[1] *= inS;
                }

                // 遅れの輪に書く。ANALOG の 1 段目は入口の音にだけ（4 倍の速さで。くり返しの輪の中には入らない = 測った本物）。
                // 4 倍の上げ下げの遅れ 28 サンプルに合わせて、くり返しも 28 サンプル遅らせてから足す（輪の中は全部同じだけ遅れる）
                for (int c = 0; c < nUse; ++c)
                {
                    float o4[4];
                    osF[c].up (inW[c], o4);
                    for (auto& v : o4) { const float sh = front[c].front (v); v += (sh - v) * anaX; }   // 色付けは止めずに回し、ANALOG ⇄ DIGITAL は 10 ms で混ぜる
                    const float xin = osF[c].down (o4);
                    const float fbd = fbDelay[c][(size_t) fbPos];
                    fbDelay[c][(size_t) fbPos] = fb[c];
                    float v = xin + fbd;
                    { const float t = v * (1.0f / 3.0f), t2 = t * t; v = v / std::sqrt (std::sqrt (1.0f + t2 * t2)); }   // 輪の安全な天井（+9.5 dBFS。ふだんは素通し）
                    line[c].push (std::isfinite (v) ? v : 0.0f);
                }
                fbPos = (fbPos + 1) % kFbDelay;

                // 出口: GATE/DUCK・VOCAL DUCK → OUTPUT → ANALOG の 2 段目（DIGITAL は −0.1 dB で頭打ち）→ MIX
                for (int c = 0; c < nUse; ++c)
                {
                    float y = w[c] * gOut * vdS * outS;
                    float o4[4];
                    osB[c].up (y, o4);
                    // DIGITAL の頭打ちは 4 倍の中で（折り返しを減らす）。ANALOG の 2 段目も止めずに回し、10 ms で混ぜる
                    for (auto& v : o4) { const float sh = back[c].back (v), dg = std::clamp (v, -0.989f, 0.989f); v = dg + (sh - dg) * anaX; }
                    y = osB[c].down (o4);
                    if (anaX < 0.5f) y = std::clamp (y, -0.989f, 0.989f);
                    const float dry = c == 0 ? xL : xR;
                    float out = dry * dryS + std::clamp (y, -4.0f, 4.0f) * wetS;
                    ch[c][i] = std::isfinite (out) ? out : 0.0f;
                }
            }

            // 表示
            for (int c = 0; c < 2; ++c)
            {
                const double lo = std::max (minLo, D[c]) + loPad;
                shownPos[c].store ((float) std::clamp ((gr[c].d - lo) / std::max (1.0, S[c]), 0.0, 1.0));
            }
            shownInDb.store (20.0f * std::log10 (inPk + 1e-9f));
            shownWindowMs.store ((float) (S[0] / fs * 1000.0));
            shownLoMs.store ((float) ((std::max (minLo, D[0]) + loPad) / fs * 1000.0));
            shownAbove.store (above);
            {
                float redDb = 0;
                if (gdS > 0.01f) redDb = gdS * (float) gdState; else if (gdS < -0.01f) redDb = -gdS * (float) (1.0 - gdState);
                shownGateDb.store (-redDb);
            }
        }

    private:
        static void route (int mode, float xL, float xR, const float* w, float rec, float gRec, float* inW, float* fb)
        {
            if (mode == kPingPong)
            {
                inW[0] = 0.5f * (xL + xR); inW[1] = 0.0f;   // 本物は左だけを入れる。ステレオの右が消えないよう、左右を足して半分に
                fb[0] = rec * w[1]; fb[1] = w[0] * gRec;
            }
            else
            {
                inW[0] = xL; inW[1] = xR;
                if (mode == kMixed) { const float m = 0.70710678f * (w[0] + w[1]) * rec; fb[0] = fb[1] = m; }
                else { fb[0] = rec * w[0]; fb[1] = rec * w[1]; }
            }
        }

        double fs = 48000;
        int nCh = 2;
        Line line[maxCh];
        Grains gr[maxCh];
        mytrem::StyleChannel front[maxCh], back[maxCh];
        mytrem::Oversampler4 osF[maxCh], osB[maxCh];
        SvfCoef hpC, lpC;
        SvfState hpS[maxCh], lpS[maxCh];
        DcCut dc[maxCh];
        float wfb[maxCh] {};
        double logLo = 2.3, logHi = 9.2, cutC = 0.999;
        int cutCount = 0;
        int curMode = 0, prevMode = 0;
        float modeX = 1.0f, modeStep = 0.001f, anaX = 1.0f, anaStep = 0.002f;
        double sgnS = -1.0;
        static constexpr int kFbDelay = 28;   // = Oversampler4::latency()
        std::array<float, kFbDelay> fbDelay[maxCh] {};
        int fbPos = 0;
        double maxD = 1000;
        bool first = true;

        float inS = 1, outS = 1, wetS = 0, dryS = 1, recS = 0, gdS = 0, vdS = 1, smC = 0.99f;
        double S[2] { 1, 1 }, D[2] { 0, 0 }, ratio[2] { 1, 1 }, slowC = 0.999;

        float det = 0, detRel = 0.99f;
        int holdLeft = 0, holdLen = 216;
        bool above = false;
        double gdState = 0;
        long long lastGrid = -1;
    };
}
