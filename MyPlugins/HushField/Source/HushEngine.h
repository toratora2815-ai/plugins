#pragma once
// Hush Field の音の処理（JUCE を使わない素の C++ + Accelerate。単体でテストできる）
//
// 考え方（耳に痛い響きを「鳴った瞬間だけ」下げる動的なフィルター）:
//   1. 解析: 最新 N サンプル（48 kHz で 4096）を FFT し、20 Hz〜20 kHz を聴感に近い目盛りの K 本の帯に分けて dB にする
//   2. 検出: 細かくならした山なみ（fine）と、広くならした山なみ（coarse）の差 = 「まわりより飛び出た量」
//      DETAIL が大きいほど fine が細くなり、細い山にも反応する
//   3. 量: 飛び出た量 → 削る dB（soft はゆるく、hard は深く）。DEPTH と帯域カーブ（8 本）で強さが変わる
//   4. 時間: ATTACK / RELEASE で帯ごとにならす（低域・高域で倍率を変えられる＝TILT）
//   5. ステレオ: L/R または M/S。LINK で左右をそろえ、FOCUS で片側だけ効かせる
//   6. 適用: 削る量のカーブから最小位相（LINEAR PHASE では直線位相）の FIR を作り、
//      先読みした音にかける（ホップごとに作り直し、前のフィルターとクロスフェード）
//      遅延は 48 kHz で 2304（LINEAR PHASE 3456、LOW LATENCY 0）
#include <vecLib/vDSP.h>   // Accelerate の FFT だけ（Accelerate.h 全体は JUCE と名前がぶつかる）
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace hf
{
    static constexpr int numBands = 8;
    static constexpr int K = 1024;           // 検出の目盛りの数（20 Hz〜20 kHz、posToHz の目盛りで等間隔）
    static constexpr int displayK = 256;     // 画面に渡す点の数
    enum Shape { LowCut = 0, LowShelf, Bell, BandPass, BandReject, Tilt, HighShelf, HighCut, numShapes };

    // 周波数のつまみ 0..1 ⇔ Hz（20 Hz〜20 kHz。低域は log より少し詰まった、聴感に近い目盛り）
    inline float posToHz (float v) { return 20.0f + 169.9f * (std::pow (118.6f, v) - 1.0f); }
    inline float hzToPos (float hz) { return std::log (std::max (0.0f, hz - 20.0f) / 169.9f + 1.0f) / std::log (118.6f); }

    struct Band
    {
        bool used = false, enabled = true;
        int shape = Bell;
        float hz = 1000.0f;
        float q = 0.7f;          // 0.1..10
        float gain = 0.0f;       // -10..+10（効きの強弱。-10 でその帯域は処理しない）
        float slope = 5.0f;      // 0..10（カット・バンドパスの急さ）
        float focus = 0.5f;      // 0 = 左(Mid)だけ、0.5 = 両方、1 = 右(Side)だけ
    };

    struct Settings
    {
        bool hard = false;
        float depth = 5.0f, detail = 5.0f, attack = 5.0f, release = 3.0f;   // 0..10
        float detailTiltLow = 1.0f, detailTiltHigh = 1.0f;                   // 倍率 0.1..10
        float attackTiltLow = 1.0f, attackTiltHigh = 1.0f;
        float releaseTiltLow = 1.0f, releaseTiltHigh = 1.0f;
        bool midSide = false;
        float stereoLink = 1.0f;      // 0..1
        float stereoFocus = 0.5f;     // 0..1
        float maxCutDb = 1000.0f;     // これより深くは削らない（大きい値 = off）
        float mix = 1.0f, wetTrimDb = 0.0f, outTrimDb = 0.0f;
        bool delta = false, bypass = false;
        bool sidechain = false, sidechainListen = false;
        int quality = 0;              // 0 normal / 1 high / 2 ultra
        bool lowLatency = false, linearPhase = false;
        int bandSolo = -1;            // -1 = なし、0..7 = その帯域の範囲だけ聴く
        Band bands[numBands];
        Band linkBands[2];            // Plugin Link で Balance Scope が送ってきた「出すぎの帯域」（画面のカーブとは別に足す）
    };

    // 測定に合わせて決めた数値（本物と同じテスト音を通し、最小二乗で合わせた。tools/hf_cli.cpp で T.名前=値 と上書きできる）
    struct Tuning
    {
        // 検出（幅はオクターブ。ガウスの σ）
        float fineOct0 = 0.0910046f, fineOct10 = 0.00479534f;   // DETAIL 0 / 10 のときの fine の幅
        float coarseOct = 0.429463f, coarseMul = 0.0500924f;  // coarse = coarseOct + coarseMul * fine
        float emph = 9.92673f;                 // 解析前に高域を持ち上げる dB/oct（暗めの音を基準にする）
        // 量
        float thr0 = 16.478f, thr1 = 1.10472f, thrHard = 0.837506f;   // しきい値 dB = thr0 - thr1*d - hard?thrHard
        float slope = 1.77534f, gamma = 3.65086f, hardMul = 0.938177f;  // 傾き = slope * d^gamma * (hard ? hardMul : 1)
        float knee = 4.06048f;
        float lvlK = 0.498826f, lvlRef = -37.6825f;      // その帯域の大きさ（高域持ち上げ後）が lvlRef より大きいほど効く
        float detSlope = -0.708443f, hardDet = 1.07831f;  // DETAIL による傾きの増減、hard で DETAIL が高いとさらに深く
        float sat0 = 6.04381f, satDet = 8.67934f;     // 頭打ちの深さ（dB）= sat0 * (1 + satDet * det^2)
        // 時間（ms）。τ = base * exp(k * (knob - 5))
        float atkBase = 85.2986f, atkK = 0.529677f;
        float relBase = 150.924f, relK = 0.220234f;
    };
    inline Tuning& tuning() { static Tuning t; return t; }

    // ---------------------------------------------------------------
    // 実数 FFT（vDSP）。forward: x[N] → re/im[0..N/2]（そのままの DFT）、inverse: その逆（1/N 込み）
    class RealFft
    {
    public:
        ~RealFft() { if (setup) vDSP_destroy_fftsetup (setup); }
        void prepare (int n)
        {
            if (n == size) return;
            if (setup) vDSP_destroy_fftsetup (setup);
            size = n; log2n = 0; while ((1 << log2n) < n) ++log2n;
            setup = vDSP_create_fftsetup ((vDSP_Length) log2n, kFFTRadix2);
            sr.assign ((size_t) n / 2, 0.0f); si.assign ((size_t) n / 2, 0.0f);
        }
        int getSize() const { return size; }
        void forward (const float* x, float* re, float* im)
        {
            DSPSplitComplex s { sr.data(), si.data() };
            vDSP_ctoz ((const DSPComplex*) x, 2, &s, 1, (vDSP_Length) size / 2);
            vDSP_fft_zrip (setup, &s, 1, (vDSP_Length) log2n, kFFTDirection_Forward);
            const int h = size / 2;
            re[0] = sr[0] * 0.5f; im[0] = 0.0f;
            re[h] = si[0] * 0.5f; im[h] = 0.0f;
            for (int k = 1; k < h; ++k) { re[k] = sr[(size_t) k] * 0.5f; im[k] = si[(size_t) k] * 0.5f; }
        }
        void inverse (const float* re, const float* im, float* x)
        {
            const int h = size / 2;
            sr[0] = re[0]; si[0] = re[h];
            for (int k = 1; k < h; ++k) { sr[(size_t) k] = re[k]; si[(size_t) k] = im[k]; }
            DSPSplitComplex s { sr.data(), si.data() };
            vDSP_fft_zrip (setup, &s, 1, (vDSP_Length) log2n, kFFTDirection_Inverse);
            vDSP_ztoc (&s, 1, (DSPComplex*) x, 2, (vDSP_Length) h);
            const float sc = 1.0f / (float) size;
            vDSP_vsmul (x, 1, &sc, x, 1, (vDSP_Length) size);
        }
    private:
        FFTSetup setup = nullptr;
        int size = 0, log2n = 0;
        std::vector<float> sr, si;
    };

    // ---------------------------------------------------------------
    // 帯域カーブ 1 本の値（「効きの dB」。0 = ふつう、-10 で効かない、+10 でかなり強く。カットは -∞ 方向）
    inline float bandResponse (const Band& b, float hz)
    {
        const float w = std::max (hz, 1.0f) / std::max (b.hz, 1.0f);   // 周波数比
        const float lw = std::log2 (w);
        const float order = 1.0f + b.slope * 0.7f;                      // 6..48 dB/oct 相当
        auto hpDb = [&] (float ratio) { // ratio = f / fc。バターワース風のハイパス
            const float r2n = std::pow (std::max (ratio, 1.0e-4f), 2.0f * order);
            return 10.0f * std::log10 (r2n / (1.0f + r2n)); };
        auto lpDb = [&] (float ratio) { return hpDb (1.0f / std::max (ratio, 1.0e-4f)); };
        const float A = b.gain;
        switch (b.shape)
        {
            case LowCut:   return hpDb (w);
            case HighCut:  return lpDb (w);
            case Bell:
            {
                // ガウス形（Q が大きいほど細い）。幅（オクターブ）≈ 1.4 / Q
                const float bw = 1.4f / std::max (b.q, 0.05f);
                return A * std::exp (-0.5f * (lw * lw) / (bw * bw * 0.25f));
            }
            case LowShelf:
            case HighShelf:
            case Tilt:
            {
                const float s = 1.0f / (1.0f + std::exp (-lw * 4.0f * std::max (b.q, 0.1f)));  // 0→1（低→高）
                if (b.shape == LowShelf)  return A * (1.0f - s);
                if (b.shape == HighShelf) return A * s;
                return A * (2.0f * s - 1.0f);                                                  // 低で -A、高で +A
            }
            case BandPass:
            {
                const float half = 0.7f / std::max (b.q, 0.05f);         // 通す幅の半分（オクターブ）
                const float lo = hpDb (std::pow (2.0f, lw + half)), hi = lpDb (std::pow (2.0f, lw - half));
                return lo + hi + A;
            }
            case BandReject:
            {
                const float half = 0.7f / std::max (b.q, 0.05f);
                const float inside = std::max (0.0f, 1.0f - std::abs (lw) / half);
                const float edge = std::min (1.0f, inside * (1.0f + b.slope));
                return -60.0f * edge;
            }
            default: return 0.0f;
        }
    }

    // カーブの dB → DEPTH に掛ける倍率（+10 で約 1.33 倍、-5 で約 0.76 倍、-10 以下で 0）
    inline float curveToMul (float c)
    {
        if (c >= 0.0f) return std::pow (10.0f, c / 80.0f);
        if (c <= -10.0f) return 0.0f;
        return std::pow (1.0f + c / 10.0f, 0.4f);
    }

    // ---------------------------------------------------------------
    class HushEngine
    {
    public:
        static constexpr int maxCh = 2;

        void prepare (double sampleRate, int /*maxBlock*/)
        {
            sr = sampleRate;
            mult = sr > 100000.0 ? 4 : (sr > 50000.0 ? 2 : 1);
            const int ringLen = 1 << 16;
            for (auto& r : inRing) r.assign ((size_t) ringLen * (size_t) mult, 0.0f);
            for (auto& r : scRing) r.assign ((size_t) ringLen * (size_t) mult, 0.0f);
            ringMask = ringLen * mult - 1;
            configured = false;
            reset();
        }

        void reset()
        {
            for (auto& r : inRing) std::fill (r.begin(), r.end(), 0.0f);
            for (auto& r : scRing) std::fill (r.begin(), r.end(), 0.0f);
            writePos = 0; hopCount = 0;
            for (auto& c : state) c.fill (0.0f);
            for (auto& o : outFifo) std::fill (o.begin(), o.end(), 0.0f);
            fifoPos = 0;
            for (int c = 0; c < maxCh; ++c) { haveFilter[c] = false; }
            for (auto& a : shownRed) for (auto& v : a) v.store (0.0f);
        }

        // 今の設定に合わせて先に準備しておく（prepareToPlay から。最初のブロックで音のスレッドがメモリを確保しないように）。
        // process の最初でも同じ確認をするので、呼ばなくても音は同じ
        void preconfigure (const Settings& s) { configure (s); }

        // 遅延（サンプル）。設定（モード）が変わったら変わる
        int latencyFor (const Settings& s) const
        {
            if (s.lowLatency) return 0;
            return (2304 + (s.linearPhase ? 1152 : 0)) * mult;
        }

        // 画面用
        static constexpr int displayPoints = displayK;
        float shownSpectrum (int i) const { return shownSpec[(size_t) i].load(); }
        float shownReduction (int i) const { return std::max (shownRed[0][(size_t) i].load(), shownRed[1][(size_t) i].load()); }
        float shownReductionCh (int ch, int i) const { return shownRed[(size_t) ch][(size_t) i].load(); }
        static float displayHz (int i) { return posToHz ((i + 0.5f) / (float) displayK); }

        // main: in/out のチャンネル（1 か 2）、sc: サイドチェーン（なければ nullptr）
        void process (float* const* io, int numCh, const float* const* sc, int numScCh, int n, const Settings& s)
        {
            configure (s);
            const int chs = std::min (numCh, maxCh);
            for (int i = 0; i < n; ++i)
            {
                // 入力を L/R か M/S にして輪っかへ
                float a = io[0][i], b = chs > 1 ? io[1][i] : a;
                if (s.midSide) { const float m = 0.5f * (a + b), d = 0.5f * (a - b); a = m; b = d; }
                inRing[0][(size_t) writePos] = a; inRing[1][(size_t) writePos] = b;
                if (sc != nullptr && numScCh > 0)
                {
                    float sa = sc[0][i], sb = numScCh > 1 ? sc[1][i] : sa;
                    if (s.midSide) { const float m = 0.5f * (sa + sb), d = 0.5f * (sa - sb); sa = m; sb = d; }
                    scRing[0][(size_t) writePos] = sa; scRing[1][(size_t) writePos] = sb;
                }
                else { scRing[0][(size_t) writePos] = 0.0f; scRing[1][(size_t) writePos] = 0.0f; }
                writePos = (writePos + 1) & ringMask;

                float w0, w1;
                if (lowLat)
                {
                    // その場で直接たたみこみ（遅延 0）
                    if (++hopCount >= hop) { hopCount = 0; analyse (s); }
                    w0 = directConv (0); w1 = directConv (1);
                    xfadePos = std::min (xfadePos + 1, hop);
                }
                else
                {
                    if (++hopCount >= hop) { hopCount = 0; analyse (s); blockConvolve (); fifoPos = 0; }
                    w0 = outFifo[0][(size_t) fifoPos]; w1 = outFifo[1][(size_t) fifoPos];
                    ++fifoPos;
                }

                // ドライ（同じだけ遅らせる）
                const int dIdx = (writePos - 1 - latency) & ringMask;
                float d0 = inRing[0][(size_t) dIdx], d1 = inRing[1][(size_t) dIdx];
                if (s.sidechain && s.sidechainListen)
                {
                    const int sIdx = (writePos - 1 - latency) & ringMask;
                    w0 = d0 = scRing[0][(size_t) sIdx]; w1 = d1 = scRing[1][(size_t) sIdx];
                }

                float o0, o1;
                if (s.bypass) { o0 = d0; o1 = d1; }
                else if (s.bandSolo >= 0 && ! (s.sidechain && s.sidechainListen)) { o0 = w0; o1 = w1; }   // ソロはフィルター自体が帯域の形
                else
                {
                    const float wet0 = w0 * wetGain, wet1 = w1 * wetGain;
                    const float m0 = d0 + s.mix * (wet0 - d0), m1 = d1 + s.mix * (wet1 - d1);
                    if (s.delta) { o0 = d0 - m0; o1 = d1 - m1; } else { o0 = m0; o1 = m1; }
                    o0 *= outGain; o1 *= outGain;
                }
                if (s.midSide) { const float l = o0 + o1, r = o0 - o1; o0 = l; o1 = r; }
                io[0][i] = o0;
                if (numCh > 1) io[1][i] = o1;
            }
        }

    private:
        // ---------- 設定が変わったときの準備 ----------
        void configure (const Settings& s)
        {
            const int q = std::clamp (s.quality, 0, 2);
            const bool ll = s.lowLatency, lin = s.linearPhase && ! ll;
            if (configured && ll == lowLat && lin == linear && q == qual) { updateGains (s); return; }
            configured = true; lowLat = ll; linear = lin; qual = q;

            if (lowLat)
            {
                nAna = 2048 * mult; hop = 64 * mult; firLen = 512 * mult; nConv = 0;
                latency = 0; anaDelay = 0;
            }
            else
            {
                nAna = 4096 * mult; hop = (256 >> q) * mult;
                firLen = linear ? 2305 * mult : 2048 * mult;
                nConv = 4096 * mult;
                anaDelay = linear ? 1152 * mult : 0;
                latency = 2304 * mult + (linear ? 1152 * mult : 0);
                lookahead = 2304 * mult - hop + 1;  // z = x を lookahead 遅らせたもの（＋ホップの FIFO で合計 2304）
            }
            nDesign = lowLat ? 2048 * mult : 4096 * mult;
            fftAna.prepare (nAna);
            fftDesign.prepare (nDesign);
            if (nConv > 0) fftConv.prepare (nConv);

            window.resize ((size_t) nAna);
            for (int i = 0; i < nAna; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0 * M_PI * (i + 0.5) / nAna);
            winPow = 0.0f; for (float w : window) winPow += w * w;

            frame.assign ((size_t) nAna, 0.0f);
            re.assign ((size_t) std::max (nAna, nDesign) / 2 + 1, 0.0f);
            im.assign (re.size(), 0.0f);
            dbuf.assign ((size_t) nDesign, 0.0f);
            dre.assign ((size_t) nDesign / 2 + 1, 0.0f); dim.assign (dre.size(), 0.0f);
            if (nConv > 0)
            {
                cbuf.assign ((size_t) nConv, 0.0f);
                cre.assign ((size_t) nConv / 2 + 1, 0.0f); cim.assign (cre.size(), 0.0f);
                for (int c = 0; c < maxCh; ++c)
                {
                    filtRe[c][0].assign (cre.size(), 0.0f); filtIm[c][0].assign (cre.size(), 0.0f);
                    filtRe[c][1].assign (cre.size(), 0.0f); filtIm[c][1].assign (cre.size(), 0.0f);
                    outFifo[c].assign ((size_t) hop, 0.0f);
                }
                tmpA.assign ((size_t) nConv, 0.0f); tmpB.assign ((size_t) nConv, 0.0f);
            }
            for (int c = 0; c < maxCh; ++c)
            {
                fir[c][0].assign ((size_t) firLen, 0.0f); fir[c][1].assign ((size_t) firLen, 0.0f);
                fir[c][0][0] = fir[c][1][0] = 1.0f;
                haveFilter[c] = false; cur[c] = 0;
            }
            if (nConv > 0)
                for (int c = 0; c < maxCh; ++c)
                    for (int j = 0; j < 2; ++j)
                    {   // 恒等フィルター（直線位相のときは中央に 1）
                        std::fill (cbuf.begin(), cbuf.end(), 0.0f);
                        cbuf[linear ? (size_t) (firLen / 2) : 0] = 1.0f;
                        fftConv.forward (cbuf.data(), filtRe[c][j].data(), filtIm[c][j].data());
                    }
            fifoPos = 0; hopCount = 0; xfadePos = hop;

            // 帯 → 解析 FFT のビン（三角の重み）
            buildBandMap();
            // 削る量の帯 → 設計 FFT のビン（線形補間の位置）
            designPos.resize ((size_t) nDesign / 2 + 1);
            for (int b = 0; b <= nDesign / 2; ++b)
            {
                const float hz = (float) (b * sr / nDesign);
                designPos[(size_t) b] = std::clamp (hzToPos (std::max (hz, 20.0f)) * K - 0.5f, 0.0f, (float) (K - 1));
            }
            for (auto& c : state) c.fill (0.0f);
            updateGains (s);
        }

        void updateGains (const Settings& s)
        {
            wetGain = std::pow (10.0f, s.wetTrimDb / 20.0f);
            outGain = std::pow (10.0f, s.outTrimDb / 20.0f);
        }

        void buildBandMap()
        {
            log2Rel.resize (K); ptsPerOct.resize (K);
            for (int k = 0; k < K; ++k)
            {
                const float hz = posToHz ((k + 0.5f) / K);
                log2Rel[(size_t) k] = std::log2 (hz / 1000.0f);
                // 1 オクターブあたりの目盛りの数 = K * dv/dlog2(f)
                ptsPerOct[(size_t) k] = K * hz * (float) M_LN2 / (std::log (118.6f) * (hz - 20.0f + 169.9f));
            }
            const float binHz = (float) (sr / nAna);
            for (int k = 0; k < K; ++k)
            {
                const float c = posToHz ((k + 0.5f) / K);
                const float lo = posToHz ((float) k / K), hi = posToHz ((k + 1.0f) / K);
                const float halfW = std::max (0.5f * (hi - lo), binHz);    // 帯の幅（せまい低域はビン 1 本ぶん）
                auto& m = bandMap[(size_t) k];
                m.clear();
                const int b0 = std::max (1, (int) std::floor ((c - halfW) / binHz));
                const int b1 = std::min (nAna / 2 - 1, (int) std::ceil ((c + halfW) / binHz));
                float sum = 0.0f;
                for (int b = b0; b <= b1; ++b)
                {
                    const float w = std::max (0.0f, 1.0f - std::abs (b * binHz - c) / halfW);
                    if (w > 0.0f) { m.push_back ({ b, w }); sum += w; }
                }
                if (m.empty()) { m.push_back ({ std::clamp ((int) std::round (c / binHz), 1, nAna / 2 - 1), 1.0f }); sum = 1.0f; }
                for (auto& e : m) e.w /= sum;
            }
        }

        // ---------- 解析 → 削る量 → フィルター ----------
        void analyse (const Settings& s)
        {
            const Tuning& T = tuning();
            const bool useSc = s.sidechain;
            auto& red = redBuf;
            auto& curve = curveBuf;
            for (int k = 0; k < K; ++k)
            {
                const float hz = posToHz ((k + 0.5f) / K);
                float c0 = 0.0f, c1 = 0.0f;
                for (int b = 0; b < numBands + 2; ++b)
                {
                    const auto& bd = b < numBands ? s.bands[b] : s.linkBands[b - numBands];
                    if (! bd.used || ! bd.enabled) continue;
                    const float r = bandResponse (bd, hz);
                    c0 += r * std::min (1.0f, 2.0f * (1.0f - bd.focus));
                    c1 += r * std::min (1.0f, 2.0f * bd.focus);
                }
                curve[0][k] = c0; curve[1][k] = c1;
            }
            const float f0 = std::min (1.0f, 2.0f * (1.0f - s.stereoFocus)), f1 = std::min (1.0f, 2.0f * s.stereoFocus);

            if (s.bandSolo >= 0 && s.bandSolo < numBands && s.bands[s.bandSolo].used)
            {
                // ソロ: その帯域の形で入力をしぼって聴かせる（カーブ 0 dB で素通し、-10 以下はほぼ無音）
                for (int k = 0; k < K; ++k)
                {
                    const auto& bd = s.bands[s.bandSolo];
                    Band b = bd; if (b.shape == Bell || b.shape == BandPass) { b.shape = BandPass; b.gain = 0.0f; }
                    const float r = bandResponse (b, posToHz ((k + 0.5f) / K));
                    red[0][k] = red[1][k] = std::min (60.0f, -std::min (0.0f, r) * 2.0f);
                }
            }
            else
            {
                const float dKnob = std::clamp (s.depth / 10.0f, 0.0f, 1.0f);
                for (int ch = 0; ch < maxCh; ++ch)
                {
                    // 1) スペクトル
                    const auto& ring = useSc ? scRing[(size_t) ch] : inRing[(size_t) ch];
                    const int start = (writePos - nAna - anaDelay) & ringMask;
                    for (int i = 0; i < nAna; ++i) frame[(size_t) i] = ring[(size_t) ((start + i) & ringMask)] * window[(size_t) i];
                    fftAna.forward (frame.data(), re.data(), im.data());
                    float* db = work[0];
                    const float norm = 4.0f / winPow;
                    for (int k = 0; k < K; ++k)
                    {
                        float p = 0.0f;
                        for (const auto& e : bandMap[(size_t) k]) p += e.w * (re[(size_t) e.b] * re[(size_t) e.b] + im[(size_t) e.b] * im[(size_t) e.b]);
                        const float raw = 10.0f * std::log10 (p * norm + 1.0e-14f);
                        specRaw[ch][k] = raw;
                        db[k] = raw + T.emph * log2Rel[(size_t) k];
                    }
                    // 2) 山なみ（fine / coarse）
                    float* fine = work[1]; float* coarse = work[2]; float* rad = work[3];
                    const float det = std::clamp (s.detail / 10.0f, 0.0f, 1.0f);
                    const float fOct = T.fineOct0 * std::pow (T.fineOct10 / T.fineOct0, det);
                    for (int k = 0; k < K; ++k)
                    {
                        const float v = (k + 0.5f) / K;
                        const float tilt = std::exp (std::log (s.detailTiltLow) * (1.0f - v) + std::log (s.detailTiltHigh) * v);
                        rad[k] = fOct / std::max (0.1f, tilt) * ptsPerOct[(size_t) k];
                    }
                    blur3 (db, fine, rad);
                    for (int k = 0; k < K; ++k) rad[k] = (T.coarseOct + T.coarseMul * fOct) * ptsPerOct[(size_t) k];
                    blur3 (db, coarse, rad);
                    // 3) 量
                    const float wFocus = ch == 0 ? f0 : f1;
                    for (int k = 0; k < K; ++k)
                    {
                        const float d = dKnob * curveToMul (curve[ch][k]) * wFocus;
                        if (d <= 1.0e-4f) { red[ch][k] = 0.0f; continue; }
                        // 飛び出た量 ＋ その帯域の大きさ（明るい音・大きい音ほど効く）
                        const float p = fine[k] - coarse[k] + T.lvlK * std::clamp (coarse[k] - T.lvlRef, -40.0f, 40.0f);
                        const float raw = law (p, std::min (d, 1.6f), s.hard) * (1.0f + T.detSlope * det + (s.hard ? T.hardDet * det * det : 0.0f));
                        // とても大きな山でも、ある所で頭打ち（DETAIL が高いほど深くまで）
                        const float cap = T.sat0 * (1.0f + T.satDet * det * det);
                        red[ch][k] = cap * std::tanh (raw / cap);
                    }
                }
                // 4) 時間（ATTACK / RELEASE）
                const float hopMs = 1000.0f * hop / (float) sr;
                for (int k = 0; k < K; ++k)
                {
                    const float v = (k + 0.5f) / K;
                    const float ta = T.atkBase * std::exp (T.atkK * (s.attack - 5.0f))
                                   * std::exp (std::log (s.attackTiltLow) * (1.0f - v) + std::log (s.attackTiltHigh) * v);
                    const float tr = T.relBase * std::exp (T.relK * (s.release - 5.0f))
                                   * std::exp (std::log (s.releaseTiltLow) * (1.0f - v) + std::log (s.releaseTiltHigh) * v);
                    const float ca = std::exp (-hopMs / std::max (0.05f, ta));
                    const float cr = std::exp (-hopMs / std::max (0.05f, tr));
                    for (int ch = 0; ch < maxCh; ++ch)
                    {
                        float& st = state[(size_t) ch][(size_t) k];
                        const float target = red[ch][k];
                        st = target > st ? target + ca * (st - target) : target + cr * (st - target);
                    }
                }
                // 5) LINK と MAX CUT
                const float mc = s.maxCutDb;
                for (int k = 0; k < K; ++k)
                {
                    const float mx = std::max (state[0][(size_t) k], state[1][(size_t) k]);
                    for (int ch = 0; ch < maxCh; ++ch)
                    {
                        float r = state[(size_t) ch][(size_t) k];
                        r += s.stereoLink * (mx - r);
                        if (mc < 100.0f) r = mc <= 0.01f ? 0.0f : std::pow (std::pow (r, -5.0f) + std::pow (mc, -5.0f), -0.2f) * (r > 1.0e-6f ? 1.0f : 0.0f);
                        red[ch][k] = r;
                    }
                }
            }

            for (int i = 0; i < displayK; ++i)
            {
                const int k = (int) ((i + 0.5f) * K / displayK);
                for (int ch = 0; ch < maxCh; ++ch) shownRed[(size_t) ch][(size_t) i].store (red[ch][k]);
                shownSpec[(size_t) i].store (std::max (specRaw[0][k], specRaw[1][k]));
            }

            // 6) フィルターを作る
            for (int ch = 0; ch < maxCh; ++ch) designFilter (ch, red[ch]);
        }

        // 3 回の箱ならし ≈ ガウス（場所ごとに幅が違う。端は端の値をのばす）
        void blur3 (const float* in, float* out, const float* sigma)
        {
            float* a = work[4]; float* b = work[5];
            std::copy (in, in + K, a);
            for (int pass = 0; pass < 3; ++pass)
            {
                double acc = 0.0;
                prefix[0] = 0.0;
                for (int k = 0; k < K; ++k) { acc += a[k]; prefix[(size_t) k + 1] = acc; }
                for (int k = 0; k < K; ++k)
                {
                    const int r = std::max (0, (int) std::lround (sigma[k] - 0.5f));
                    const int lo = k - r, hi = k + r;
                    double sum = prefix[(size_t) std::min (hi, K - 1) + 1] - prefix[(size_t) std::max (lo, 0)];
                    if (hi > K - 1) sum += (double) (hi - (K - 1)) * a[K - 1];
                    if (lo < 0) sum += (double) (-lo) * a[0];
                    b[k] = (float) (sum / (2 * r + 1));
                }
                std::swap (a, b);
            }
            std::copy (a, a + K, out);
        }

        static float law (float p, float d, bool hard)
        {
            const Tuning& T = tuning();
            const float thr = T.thr0 - T.thr1 * d - (hard ? T.thrHard : 0.0f);
            const float sl = T.slope * std::pow (d, T.gamma) * (hard ? T.hardMul : 1.0f);
            const float x = (p - thr) / T.knee;
            const float sp = x > 20.0f ? x : std::log1p (std::exp (x));
            return std::min (60.0f, sl * T.knee * sp);
        }

        void designFilter (int ch, const float* redDb)
        {
            // 削る量（dB）→ 設計グリッドの振幅
            const int h = nDesign / 2;
            bool any = false;
            for (int b = 0; b <= h; ++b)
            {
                const float p = designPos[(size_t) b];
                const int i = (int) p; const float f = p - i;
                const float r = redDb[i] + (i + 1 < K ? f * (redDb[i + 1] - redDb[i]) : 0.0f);
                dre[(size_t) b] = -r * (float) (M_LN10 / 20.0);     // ln|G|
                if (r > 0.01f) any = true;
            }
            int next = 1 - cur[ch];
            auto& f = fir[ch][next];
            std::fill (f.begin(), f.end(), 0.0f);
            if (! any)
            {
                f[linear ? (size_t) (firLen / 2) : 0] = 1.0f;
            }
            else if (linear)
            {
                // ゼロ位相 → 中央へずらして窓
                for (int b = 0; b <= h; ++b) { dim[(size_t) b] = 0.0f; dre[(size_t) b] = std::exp (dre[(size_t) b]); }
                fftDesign.inverse (dre.data(), dim.data(), dbuf.data());
                const int half = firLen / 2;
                for (int j = -half; j <= half; ++j)
                {
                    const float w = 0.5f + 0.5f * std::cos (M_PI * j / (half + 1));
                    f[(size_t) (j + half)] = dbuf[(size_t) ((j + nDesign) % nDesign)] * w;
                }
            }
            else
            {
                // 最小位相（ケプストラム法）
                for (int b = 0; b <= h; ++b) dim[(size_t) b] = 0.0f;
                fftDesign.inverse (dre.data(), dim.data(), dbuf.data());    // 実ケプストラム
                for (int n = 1; n < h; ++n) dbuf[(size_t) n] *= 2.0f;
                for (int n = h + 1; n < nDesign; ++n) dbuf[(size_t) n] = 0.0f;
                fftDesign.forward (dbuf.data(), dre.data(), dim.data());
                for (int b = 0; b <= h; ++b)
                {
                    const float m = std::exp (dre[(size_t) b]);
                    const float ph = dim[(size_t) b];
                    dre[(size_t) b] = m * std::cos (ph); dim[(size_t) b] = m * std::sin (ph);
                }
                fftDesign.inverse (dre.data(), dim.data(), dbuf.data());
                const int fade = firLen / 4;
                for (int n = 0; n < firLen; ++n)
                {
                    float w = 1.0f;
                    if (n >= firLen - fade) w = 0.5f + 0.5f * std::cos (M_PI * (n - (firLen - fade)) / fade);
                    f[(size_t) n] = dbuf[(size_t) n] * w;
                }
            }
            if (nConv > 0)
            {
                std::fill (cbuf.begin(), cbuf.end(), 0.0f);
                std::copy (f.begin(), f.end(), cbuf.begin());
                fftConv.forward (cbuf.data(), filtRe[ch][next].data(), filtIm[ch][next].data());
            }
            cur[ch] = next;
            haveFilter[ch] = true;
            xfadePos = 0;
        }

        // ---------- 通常 / LINEAR PHASE: ホップごとに FFT でたたみこむ ----------
        void blockConvolve()
        {
            const int h = nConv / 2;
            for (int ch = 0; ch < maxCh; ++ch)
            {
                // z = x を lookahead 遅らせたもの。最新 nConv サンプル
                const int start = (writePos - nConv - lookahead) & ringMask;
                for (int i = 0; i < nConv; ++i) cbuf[(size_t) i] = inRing[(size_t) ch][(size_t) ((start + i) & ringMask)];
                fftConv.forward (cbuf.data(), cre.data(), cim.data());
                const int nw = cur[ch], od = 1 - nw;
                for (int j = 0; j < 2; ++j)
                {
                    const int idx = j == 0 ? od : nw;
                    auto& fr = filtRe[ch][idx]; auto& fi = filtIm[ch][idx];
                    auto& out = j == 0 ? tmpA : tmpB;
                    for (int b = 0; b <= h; ++b)
                    {
                        const float a = cre[(size_t) b], bb = cim[(size_t) b];
                        convRe[(size_t) b] = a * fr[(size_t) b] - bb * fi[(size_t) b];
                        convIm[(size_t) b] = a * fi[(size_t) b] + bb * fr[(size_t) b];
                    }
                    fftConv.inverse (convRe.data(), convIm.data(), out.data());
                }
                for (int i = 0; i < hop; ++i)
                {
                    const float t = (i + 0.5f) / hop;
                    outFifo[(size_t) ch][(size_t) i] = tmpA[(size_t) (nConv - hop + i)] * (1.0f - t) + tmpB[(size_t) (nConv - hop + i)] * t;
                }
            }
        }

        // ---------- LOW LATENCY: 直接たたみこみ ----------
        float directConv (int ch)
        {
            const auto& r = inRing[(size_t) ch];
            const auto& fn = fir[ch][cur[ch]]; const auto& fo = fir[ch][1 - cur[ch]];
            float yn = 0.0f, yo = 0.0f;
            int idx = (writePos - 1) & ringMask;
            for (int m = 0; m < firLen; ++m)
            {
                const float x = r[(size_t) idx];
                yn += fn[(size_t) m] * x; yo += fo[(size_t) m] * x;
                idx = (idx - 1) & ringMask;
            }
            const float t = std::min (1.0f, (xfadePos + 0.5f) / hop);
            return yo + t * (yn - yo);
        }

        // ---------- メンバー ----------
        double sr = 48000.0;
        int mult = 1;
        bool configured = false, lowLat = false, linear = false;
        int qual = 0;
        int nAna = 4096, nDesign = 4096, nConv = 4096, hop = 256, firLen = 2048;
        int latency = 2304, lookahead = 2048, anaDelay = 0;
        int ringMask = 0, writePos = 0, hopCount = 0, fifoPos = 0, xfadePos = 0;
        std::vector<float> inRing[maxCh], scRing[maxCh];
        std::vector<float> window, frame, re, im, dbuf, dre, dim, cbuf, cre, cim, tmpA, tmpB;
        std::vector<float> convRe = std::vector<float> (16384 + 1), convIm = std::vector<float> (16384 + 1);
        float winPow = 1.0f;
        RealFft fftAna, fftDesign, fftConv;
        struct W { int b; float w; };
        std::array<std::vector<W>, K> bandMap;
        std::vector<float> designPos;
        std::array<std::array<float, K>, maxCh> state {};
        std::vector<float> fir[maxCh][2];
        std::vector<float> filtRe[maxCh][2], filtIm[maxCh][2];
        int cur[maxCh] { 0, 0 };
        bool haveFilter[maxCh] { false, false };
        std::vector<float> outFifo[maxCh];
        float specRaw[maxCh][K] {};
        float redBuf[maxCh][K] {}, curveBuf[maxCh][K] {};
        float work[6][K] {};
        std::vector<double> prefix = std::vector<double> (K + 1);
        std::vector<float> log2Rel, ptsPerOct;
        float wetGain = 1.0f, outGain = 1.0f;
        std::array<std::atomic<float>, displayK> shownSpec {};
        std::array<std::array<std::atomic<float>, displayK>, maxCh> shownRed {};
    };
}
