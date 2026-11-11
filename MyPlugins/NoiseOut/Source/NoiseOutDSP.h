#pragma once
// Noise Out の音の処理（JUCE を使わない素の C++ + Accelerate の FFT。tools/render_noiseout.cpp で単体テストできる）
//
// 流れ: 入口 → HUM（時間領域のノッチ。遅れなし）→ STFT（NOISE と ROOM の下げ量を 1 つにまとめてかける）→ OUTPUT
//   STFT: 48 kHz で 2048 点（サンプルレートに比例）、ホップ 1/8、√ハンの窓。遅れ = 2048 サンプル（モジュールの ON/OFF で変わらない）
//   左右は同じ下げ方（左右の平均の大きさで判定）なので定位が揺れない。DELTA = 遅らせた元の音 − 出口（HUM で取った分も入る）
//
// HUM  … 本物（RX 12 De-hum の Static）と同じ「RBJ のピーキング EQ（Q 1000）を f0 の倍音に並べる」形。1.5 Hz の直流カット付き。
//         AUTO は 1.5 kHz までの音から 50 / 60 Hz ±0.3 Hz の基本周波数を 0.01 Hz 単位で探し、見つからない間はノッチを 0 dB にする
// NOISE … 雑音の推定（ADAPTIVE = 静かな所を追う / LEARNED = LEARN で覚えた形）より大きい所だけを残し、小さい所を下げる
// ROOM  … 少し前に鳴った音から「今ごろ残っているはずの響き」を予想し（TAIL の長さの窓）、その割合だけ下げる
#include <vecLib/vDSP.h>    // Accelerate の FFT（Accelerate.h 全体は JUCE と名前がぶつかるので、使う部分だけ）
#include <vecLib/vForce.h>  // まとめて計算する log10 / exp
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace nzo
{
    constexpr int maxCh = 2;
    constexpr int numDisp = 128;                 // 画面に渡す帯の数（20 Hz〜20 kHz を対数で等分）
    inline float dispHz (int i) { return 20.0f * std::pow (1000.0f, ((float) i + 0.5f) / (float) numDisp); }
    constexpr int profileBins = 1025;            // LEARN で覚えた形の保存形式: 23.4375 Hz おき（48 kHz・2048 点のビン）
    constexpr float profileStepHz = 23.4375f;
    constexpr int maxHarm = 16;

    // 測定に合わせて決めた数値（tools/render_noiseout.cpp で T.名前=値 と上書きできる）
    struct Tuning
    {
        // ---- NOISE ----
        float nFreqOct = 0.432958f;     // 判定に使う大きさの周波数方向のならし（オクターブ幅）
        float nFreqMin = 1.0f;      // その最小（片側のビン数）
        float nAttMs = 9.27243f;        // 判定に使う大きさ: 上がる速さ
        float nRelMs = 0.165841f;       //                  下がる速さ
        float nTrkMs = 0.0204425f;      // ADAPTIVE: 推定に使う大きさのならし
        float nUpDbs = 1.5434f;        // ADAPTIVE: 推定が上がる速さの上限（dB/秒）
        float nUpProp = 0.0350593f;      // ADAPTIVE: 上がるときの割合（ホップごと）
        float nDownMs = 12.7653f;      // ADAPTIVE: 推定が下がる速さ
        float nBiasA = 15.1224f;        // しきい値 = 推定 + bias + THRESHOLD（ADAPTIVE）
        float nBiasL = 11.0f;        //                                     （LEARNED）
        float nSlope = 1.07035f;        // しきい値より下での下げ方（dB / dB）
        float nKnee = 0.583588f;        // 曲がりのやわらかさ（小さいほどゆるい）
        float nGainOct = 0.20462f;      // 下げ量の周波数方向のならし（オクターブ幅）
        float nGAttMs = 4.52117f;       // 下げ量が戻る（開く）速さ
        float nGRelMs = 72.533f;      // 下げ量が深くなる（閉じる）速さ
        float nThrScale = 1.3434f;     // THRESHOLD つまみの効き
        float nLowBias = 0.0f;         // 50 Hz でのしきい値のずらし（800 Hz で 0 へ、log で直線）
        float nHfW1 = 1.08766f, nHfW3 = 1.20652f, nHfW2 = 0.644587f;   // 下げ量の倍率: 6 kHz で 1 → 10.5 kHz で W1 → 14 kHz で W3 → 18 kHz から W2
        // ---- ROOM ----
        float rSmMs = 6.85757f;        // 大きさの時間方向のならし
        float rTdMs = 111.578f;        // 直接音から何 ms 後を響きとみなすか
        float rRate = 1.14038f;         // 予想の減り方: 60 dB を rRate × TAIL^rRateP 秒で
        float rRateP = 0.349069f;
        float rWinA = 0.894831f;        // 予想の窓の長さ = rWinA + rWinB × TAIL（秒）
        float rWinB = 0.249534f;
        float rDryK = 1.21114f;         // 包絡の何倍を超えたら新しく鳴った直接音とみなすか
        float rEnvDbs = 6.84909f;      // 立ち上がりを見分ける包絡の減る速さ（dB/秒）
        float rMu0 = 1.47492f, rMuP = 2.60103f;    // 響きの割合 q = min(1, μ·λ/P)、μ = rMu0·(REDUCTION/10)^rMuP
        float rA = 18.4317f;           // 下げ量 = min(rMaxDb, rA·(REDUCTION/10)·q'^γ)
        float rGamma = 1.0f;             // q' = (q − rQ0) / (1 − rQ0)
        float rQ0 = 0.316166f;
        float rMaxDb = 24.0f;       // いちばん深い下げ量
        float rBoostK = 1.1f;       // REDUCTION がマイナスのとき: 上げ量 = rBoostK·|R|·q
        float rGAttMs = 8.59955f;       // 下げ量がなくなる速さ
        float rGRelMs = 153.209f;      // 下げ量が深くなる速さ
        float rGainOct = 0.291189f;      // 下げ量の周波数方向のならし
        // ---- HUM ----
        float hQ = 1000.0f;         // ノッチの Q（本物の初期値）
        float hDcHz = 1.5f;         // 直流カット
    };
    inline Tuning& tuning() { static Tuning t; return t; }

    struct Knobs
    {
        bool humOn = false;
        int humFreq = 0;              // 0 = AUTO、1 = 50 Hz、2 = 60 Hz
        float humDepthDb = 60.0f;     // ノッチの深さ
        int humHarm = 8;              // 倍音の数（基本を含む）
        bool noiseOn = true;
        int noiseMode = 0;            // 0 = ADAPTIVE、1 = LEARNED
        int learnStart = 0;           // LEARN を押した回数（増えたら覚え始める。鳴っている所を 3 秒聴いたら自動で終わる）
        int learnStop = 0;            // 途中で止めた回数（増えたらそこで終わる）
        float noiseRedDb = 12.0f;     // いちばん深い下げ量
        float noiseThrDb = 0.0f;      // しきい値のずらし（+ でたくさん下げる）
        bool freezeAdapt = false;     // Plugin Link: Fader Pilot が歌っている間は推定を止める
        bool roomOn = false;
        float roomRedDb = 10.0f;      // −10..20（マイナスは響きを足す）
        float roomTail = 1.0f;        // 秒
        float outDb = 0.0f;
        bool delta = false;
    };

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

    // 周波数方向のならし（ビン k の前後を [lo[k], hi[k]] で平均。累積和で O(K)）
    struct FreqSmoother
    {
        std::vector<int> lo, hi;
        std::vector<double> cum;
        void prepare (int K, float oct, float minHalf)
        {
            lo.assign ((size_t) K, 0); hi.assign ((size_t) K, 0); cum.assign ((size_t) K + 1, 0.0);
            const double f = std::pow (2.0, (double) oct * 0.5);
            for (int k = 0; k < K; ++k)
            {
                int a = (int) std::floor ((double) k / f), b = (int) std::ceil ((double) k * f);
                const int m = (int) std::lround (minHalf);
                a = std::min (a, k - m); b = std::max (b, k + m);
                lo[(size_t) k] = std::max (0, a); hi[(size_t) k] = std::min (K - 1, b);
            }
        }
        void run (const float* in, float* out, int K)
        {
            cum[0] = 0.0;
            for (int k = 0; k < K; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + (double) in[k];
            for (int k = 0; k < K; ++k)
            {
                const int a = lo[(size_t) k], b = hi[(size_t) k];
                out[k] = (float) ((cum[(size_t) b + 1] - cum[(size_t) a]) / (double) (b - a + 1));
            }
        }
    };

    // 倍精度の双 2 次（とても細いノッチを低い周波数に置くので float では足りない）
    struct BiquadD
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[maxCh] {}, z2[maxCh] {};
        void reset() { for (int c = 0; c < maxCh; ++c) z1[c] = z2[c] = 0.0; }
        void setPeak (double sr, double f0, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * M_PI * f0 / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + al / A;
            b0 = (1.0 + al * A) / a0; b1 = -2.0 * cw / a0; b2 = (1.0 - al * A) / a0;
            a1 = -2.0 * cw / a0; a2 = (1.0 - al / A) / a0;
        }
        inline double tick (double x, int c)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
    };

    // ---------------------------------------------------------------
    // ハムの基本周波数を探す（AUTO）。1.5 kHz より上を落として間引き、2.7 秒分の FFT で 49.7〜50.3・59.7〜60.3 Hz の倍音の山の合計がいちばん大きい所。
    // 音のスレッドの山を小さくするため、0.25 秒ごとの解析を段に分けて進める（FFT → 中央値の表 16 回 → 判定。work() でブロックごとに 1 + 長さ/128 段）
    class HumFinder
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            decim = std::max (1, (int) std::lround (sr / 3000.0));
            dsr = sr / decim;
            M = 8192; while ((double) M / dsr < 2.4) M *= 2;
            fft.prepare (M);
            ring.assign ((size_t) M, 0.0f); frame.assign ((size_t) M, 0.0f);
            re.assign ((size_t) M / 2 + 1, 0.0f); im.assign ((size_t) M / 2 + 1, 0.0f); mag.assign ((size_t) M / 2 + 1, 0.0f);
            floorTab.assign ((size_t) M / 2 + 1, -240.0f); peakTab.assign ((size_t) M / 2 + 1, -240.0f);
            prepareNeededBins();
            win.resize ((size_t) M);
            for (int i = 0; i < M; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) M_PI * (float) i / (float) M);
            // 1.2 kHz の 4 次バターワース（2 段）
            for (int s = 0; s < 2; ++s)
            {
                const double q = s == 0 ? 0.5411961 : 1.3065630;
                const double w = 2.0 * M_PI * 1200.0 / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
                lp[s].b0 = (1.0 - cw) / 2.0 / a0; lp[s].b1 = (1.0 - cw) / a0; lp[s].b2 = lp[s].b0;
                lp[s].a1 = -2.0 * cw / a0; lp[s].a2 = (1.0 - al) / a0; lp[s].reset();
            }
            clearBuffers();
            found = false; f0 = 60.0; confidence = 0.0f;
        }
        // 聴いた音だけを忘れる（見つけた周波数は覚えておく。Logic の Reset = 再生開始・位置の移動のたび）
        void clearBuffers()
        {
            std::fill (ring.begin(), ring.end(), 0.0f);
            wp = 0; dcount = 0; sinceRun = 0; filled = 0; stage = 0; hits = 0; misses = 0; lastEst = 0.0;
            for (auto& b : lp) b.reset();
        }
        // 左右を足した音を 1 サンプルずつ（軽い。解析はしない）
        inline void push (float x)
        {
            double y = lp[1].tick (lp[0].tick ((double) x, 0), 0);
            if (++dcount < decim) return;
            dcount = 0;
            ring[(size_t) wp] = (float) y; wp = (wp + 1) % M;
            if (filled < M) ++filled;
            if (++sinceRun >= (int) (dsr * 0.25)) { sinceRun = 0; if (filled >= M / 2 && stage == 0) stage = 1; }
        }
        // 音のブロックごとに 1 回。active = HUM が ON で AUTO のとき（それ以外は解析しない）。
        // 1 段は 50 µs 前後。ブロックが長いほど 1 回で多く進める（128 サンプルごとに 1 段）
        void work (bool active, int blockSize)
        {
            if (stage == 0) return;
            if (! active) { stage = 0; return; }
            for (int n = 1 + blockSize / 128; n > 0 && stage != 0; --n)
            {
                if (stage == 1) { stageFft(); stage = 2; }
                else if (stage <= 1 + numChunks) { stageFloors (stage - 2); ++stage; }
                else { stageDecide(); stage = 0; }
            }
        }
        void seed (double f)
        {
            f0 = f; found = true; hits = 2; misses = 0;
        }
        bool found = false;
        double f0 = 60.0;
        float confidence = 0.0f;   // いちばん大きい倍音の山がまわりより何 dB 大きいか
    private:
        double binHz() const { return dsr / M; }
        void stageFft()
        {
            for (int i = 0; i < M; ++i) frame[(size_t) i] = ring[(size_t) ((wp + i) % M)] * win[(size_t) i];
            fft.forward (frame.data(), re.data(), im.data());
            const int H = M / 2;
            // 使うのは 8 倍音 + まわり 12 Hz（〜510 Hz）まで
            kMax = std::min (H - 2, (int) std::ceil ((62.0 * 8.0 + 14.0) / binHz()) + 2);
            for (int k = 0; k <= kMax + 1; ++k) mag[(size_t) k] = 10.0f * std::log10 (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k] + 1e-24f);
        }
        // 候補が通るビン（49.7h〜50.3h・59.7h〜60.3h Hz、h = 1..8）のまわり（±3〜±12 Hz）の中央値を、numChunks 回に分けて求める
        void prepareNeededBins()
        {
            const double bh = binHz();
            std::vector<char> mark ((size_t) M / 2 + 1, 0);
            const int lim = std::min (M / 2 - 2, (int) std::ceil ((62.0 * 8.0 + 14.0) / bh) + 2);
            for (int h = 1; h <= 8; ++h)
                for (int range = 0; range < 2; ++range)
                {
                    const int a = (int) std::floor ((range == 0 ? 49.7 : 59.7) * h / bh) - 3, b = (int) std::ceil ((range == 0 ? 50.3 : 60.3) * h / bh) + 3;
                    for (int c = std::max (2, a); c <= std::min (lim - 1, b); ++c) mark[(size_t) c] = 1;
                }
            needed.clear();
            for (int c = 0; c <= M / 2; ++c) if (mark[(size_t) c]) needed.push_back (c);
        }
        void stageFloors (int chunk)
        {
            const size_t n = needed.size(), a = n * (size_t) chunk / (size_t) numChunks, b = n * (size_t) (chunk + 1) / (size_t) numChunks;
            const double bh = binHz();
            for (size_t i = a; i < b; ++i)
            {
                const int c = needed[i];
                const double hz = c * bh;
                float v[72]; int m = 0;
                for (double off = 3.0; off <= 12.0 && m < 70; off += bh)
                    for (int sgn = -1; sgn <= 1; sgn += 2)
                    {
                        const int k = (int) std::lround ((hz + sgn * off) / bh);
                        if (k > 1 && k <= kMax) v[m++] = mag[(size_t) k];
                    }
                float r = -240.0f;
                if (m > 0) { std::nth_element (v, v + m / 2, v + m); r = v[m / 2]; }
                floorTab[(size_t) c] = r;
                peakTab[(size_t) c] = std::max (mag[(size_t) c], std::max (mag[(size_t) (c - 1)], mag[(size_t) (c + 1)]));
            }
        }
        float floorBin (int c) const { return floorTab[(size_t) c]; }
        float sharp (double hz) const
        {
            const int c = (int) std::lround (hz / binHz());
            if (c < 2 || c >= kMax) return 0.0f;
            return peakTab[(size_t) c] - floorTab[(size_t) c];
        }
        void stageDecide()
        {
            const double bh = binHz();
            // 電源の周波数の近く（±0.3 Hz。楽器の音程は入らない）だけを 0.01 Hz 刻みで。点数 = 倍音ごとの「細い山の高さ − まわりの中央値」（声の倍音は揺れて太るので低くなる）
            double bestF = 60.0; float bestS = -1e9f;
            for (int range = 0; range < 2; ++range)
                for (double f = range == 0 ? 49.7 : 59.7; f <= (range == 0 ? 50.3 : 60.3) + 1e-9; f += 0.01)
                {
                    float sc = 0.0f;
                    for (int h = 1; h <= 8; ++h) sc += std::max (0.0f, sharp (f * h));
                    if (sc > bestS) { bestS = sc; bestF = f; }
                }
            // 倍音ごとの山の高さと正確な位置（±0.6 Hz のいちばん大きいビンを放物線で補間）
            double wsum = 0.0, fsum = 0.0; int strong = 0; float conf = 0.0f, pkMax = -240.0f; bool lowOk = false;
            for (int h = 1; h <= 8; ++h)
            {
                const double hz = bestF * h;
                const int c = (int) std::lround (hz / bh);
                int best = c; const int span = std::max (1, (int) std::ceil (0.35 / bh));
                for (int k = c - span; k <= c + span; ++k) if (k > 1 && k < kMax && mag[(size_t) k] > mag[(size_t) best]) best = k;
                if (best <= 1 || best >= kMax) continue;
                // 本当の山（両隣より高い所）だけを使う。窓の外の別の音（楽器）のすそは、窓の端で両隣より低くなるので外れる
                if (mag[(size_t) best] < mag[(size_t) (best - 1)] || mag[(size_t) best] < mag[(size_t) (best + 1)]) continue;
                const float a = mag[(size_t) (best - 1)], b = mag[(size_t) best], cc = mag[(size_t) (best + 1)];
                const float den = a - 2.0f * b + cc;
                const float d = std::abs (den) > 1e-9f ? 0.5f * (a - cc) / den : 0.0f;
                const double ex = ((double) best + (double) std::clamp (d, -0.5f, 0.5f)) * bh;
                const float pk = b - 0.25f * (a - cc) * d;
                const float snr = pk - floorBin (std::clamp (c, 2, kMax - 1));
                if (snr > 12.0f) { ++strong; const double w = (double) snr * h; wsum += w; fsum += w * ex / h; if (h <= 2) lowOk = true; pkMax = std::max (pkMax, pk); }
                conf = std::max (conf, snr);
            }
            confidence = conf;
            // ハムとみなす条件: 倍音の山が 2 本以上（か 1 本がとても強い）、基本か 2 倍（50/60 か 100/120 Hz）に山がある（強い倍音が 4 本以上ならなくてよい）、
            // 0.25 秒ごとの推定が 3 回続けて 0.08 Hz 以内・大きさ ±4 dB 以内にそろう（電源のハムは揺れない。声や響きの倍音は動き、響きの尾は減っていくので外れる）
            const double est = wsum > 0.0 ? fsum / wsum : 0.0;
            const bool inMains = std::abs (est - 50.0) <= 0.32 || std::abs (est - 60.0) <= 0.32;   // 細かく求めた値も電源の近くだけ（近くの楽器の音を拾わない）
            const bool ok = wsum > 0.0 && (strong >= 2 || conf > 24.0f) && (lowOk || strong >= 4) && inMains;   // 低い倍音のないジー音は、強い倍音 4 本以上で
            if (ok && ! found)
            {
                hits = std::abs (est - lastEst) < 0.08 && std::abs (pkMax - lastPk) < 4.0f ? hits + 1 : 0;
                lastEst = est; lastPk = pkMax;
                if (hits >= 2) { f0 = est; found = true; misses = 0; }
                else f0 = est;
            }
            else if (ok && found && std::abs (est - f0) < 0.5)
            {
                f0 += (est - f0) * 0.3; misses = 0;     // 見つけている間はゆっくり追う
            }
            else
            {
                if (! found) { hits = 0; lastEst = 0.0; }
                else if (++misses >= 8) { found = false; hits = 0; lastEst = 0.0; }   // 2 秒続けて見つからなければ外す
            }
        }
        double sr = 48000.0, dsr = 3000.0, lastEst = 0.0;
        float lastPk = -240.0f;
        int decim = 16, M = 8192, wp = 0, dcount = 0, sinceRun = 0, filled = 0, hits = 0, misses = 0, stage = 0, kMax = 1;
        RealFft fft;
        std::vector<float> ring, frame, re, im, mag, win, floorTab, peakTab;
        std::vector<int> needed;
        static constexpr int numChunks = 16;
        BiquadD lp[2];
    };

    // ---------------------------------------------------------------
    class Engine
    {
    public:
        void prepare (double sampleRate, int numChannels)
        {
            sr = sampleRate;
            nCh = std::clamp (numChannels, 1, maxCh);
            const double ratio = sr / 48000.0;
            N = 2048;
            if (ratio > 1.5) N = 4096;
            if (ratio > 3.0) N = 8192;
            if (ratio < 0.7) N = 1024;
            H = N / 8;
            K = N / 2 + 1;
            hopSec = (double) H / sr;
            binHz = (float) (sr / N);
            fft.prepare (N);
            win.resize ((size_t) N);
            for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * (float) M_PI * (float) i / (float) N));
            olaScale = 1.0f / ((float) N / (float) H * 0.5f);   // √ハン × √ハン = ハン、重ねた和 = N/H/2
            for (int c = 0; c < maxCh; ++c)
            {
                inFrame[c].assign ((size_t) N, 0.0f); acc[c].assign ((size_t) N, 0.0f); outBuf[c].assign ((size_t) H, 0.0f);
                dry[c].assign ((size_t) N, 0.0f); humOut[c].assign ((size_t) H, 0.0f);
                re[c].assign ((size_t) K, 0.0f); im[c].assign ((size_t) K, 0.0f);
            }
            tmp.assign ((size_t) N, 0.0f);
            auto z = [&] (std::vector<float>& v, float val = 0.0f) { v.assign ((size_t) K, val); };
            z (P); z (Pf); z (Ls); z (trk); z (nz, 200.0f); z (gN); z (gNs); z (gRs); z (G, 1.0f); z (work); z (work2);
            z (learnSum); z (profBins, -200.0f); z (thrArr, -200.0f);
            prepareSmoothers();
            // ROOM の履歴（いちばん長い TAIL 4 秒の窓 + 予備）
            histLen = (int) std::ceil ((tuning().rTdMs * 0.001 + tuning().rWinA + tuning().rWinB * 4.0 * 1.05) / hopSec) + 4;
            prepareRoomBands();
            hum.prepare (sr);
            // ADAPTIVE の推定は prepare でだけ忘れる（Reset = 再生開始・位置の移動では覚えたまま）
            std::fill (Ls.begin(), Ls.end(), -200.0f);
            std::fill (trk.begin(), trk.end(), -200.0f);
            std::fill (nz.begin(), nz.end(), 200.0f);
            nzStarted = false;
            std::fill (gNs.begin(), gNs.end(), 0.0f);
            mixNoise = mixRoom = 0.0f; humMix = 0.0; deltaMix = 0.0;
            hw.fill (0.0);
            if (profileValid.load()) rebuildProfileBins();
            for (int i = 0; i < numDisp; ++i) { dispIn[(size_t) i].store (-200.0f); dispThr[(size_t) i].store (-200.0f); dispGain[(size_t) i].store (0.0f); }
            reset();
        }

        void reset()
        {
            for (int c = 0; c < maxCh; ++c)
            {
                std::fill (inFrame[c].begin(), inFrame[c].end(), 0.0f);
                std::fill (acc[c].begin(), acc[c].end(), 0.0f);
                std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
                std::fill (dry[c].begin(), dry[c].end(), 0.0f);
            }
            pos = 0; dryPos = 0;
            std::fill (gRs.begin(), gRs.end(), 0.0f);
            std::fill (G.begin(), G.end(), 1.0f);
            std::fill (hist.begin(), hist.end(), 0.0f);
            std::fill (Pb.begin(), Pb.end(), 0.0f); std::fill (Penv.begin(), Penv.end(), 0.0f);
            std::fill (gB.begin(), gB.end(), 0.0f); std::fill (gBs.begin(), gBs.end(), 0.0f);
            std::fill (dqSize.begin(), dqSize.end(), 0); std::fill (dqHead.begin(), dqHead.end(), 0);
            frameNo = 0; lastW1 = -1; lastR = -1.0f; framesSinceRebuild = 1000;
            for (auto& b : notch) b.reset();
            dcz.fill (0.0); dcx.fill (0.0);
            hum.clearBuffers();                 // 見つけたハムの周波数は覚えたまま
            outGain = -1.0f;
            // 覚えている途中だったら、そこから数え直す（LEARN は押したまま）
            std::fill (learnSum.begin(), learnSum.end(), 0.0f);
            learnFrames = 0;
        }

        int latencySamples() const { return N; }

        // ---- LEARN で覚えた形（保存形式 = 23.4375 Hz おき 1025 点の dB）----
        // 音のスレッドの外から入れる: setProfile → 次のフレームで反映
        void setProfile (const float* db, int n)
        {
            // 保存用の形（atomic）にすぐ書く → 音を 1 度も流さずに保存しても、入れた形のまま保存される
            for (int j = 0; j < profileBins; ++j) publishedProfile[(size_t) j].store (j < n ? db[j] : -200.0f, std::memory_order_relaxed);
            profileValid.store (true);
            profilePending.store (true);
        }
        void clearProfile() { profileValid.store (false); profilePending.store (true); }
        bool hasProfile() const { return profileValid.load(); }
        // 覚えた形を読む（保存用）。音のスレッドが書き終えた版だけ
        void getProfile (std::array<float, profileBins>& out) const { for (int j = 0; j < profileBins; ++j) out[(size_t) j] = publishedProfile[(size_t) j].load (std::memory_order_relaxed); }
        std::atomic<int> profileVersion { 0 };     // LEARN が終わるたびに +1（プロセッサーが見て保存・モード切替）

        // ---- 画面用 ----
        std::array<std::atomic<float>, numDisp> dispIn, dispThr, dispGain;
        std::atomic<float> humF0 { 60.0f }, humConf { 0.0f };
        std::atomic<bool> humFound { false };
        std::atomic<bool> learning { false }, adaptFrozen { false };
        std::atomic<float> noiseFloorDb { -120.0f };   // 推定した雑音の大きさ（全体、dBFS）
        std::atomic<float> learnProgress { 0.0f };     // LEARN の進み（0..1）
        static constexpr double learnSeconds = 3.0;
        // 前に見つけたハムの周波数から始める（曲を開いた直後の 1.5 秒を待たない）。どのスレッドからでもよい
        void seedHum (double f0) { if (f0 > 40.0 && f0 < 70.0) seedHumPending.store (f0); }

        void process (float* const* ch, int n, const Knobs& k)
        {
            // 前のプラグインから NaN・無限大が来たら 0 に（推定や HUM の状態を壊さない）
            for (int c = 0; c < nCh; ++c)
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (ch[c][i])) ch[c][i] = 0.0f;
            // LEARN: 押した回数が増えたら覚え始め、止めた回数が増えたら終わる。鳴っている所を learnSeconds 聴いたら自動で終わる
            if (k.learnStart != seenLearnStart)
            {
                seenLearnStart = k.learnStart;
                std::fill (learnSum.begin(), learnSum.end(), 0.0f); learnFrames = 0;
                learnActive = true;
            }
            if (k.learnStop != seenLearnStop)
            {
                seenLearnStop = k.learnStop;
                if (learnActive && learnFrames > 0) finishLearn();
                learnActive = false;
            }
            learning.store (learnActive, std::memory_order_relaxed);
            if (const double sd = seedHumPending.exchange (0.0); sd > 0.0) hum.seed (sd);
            hum.work (k.humOn && k.humFreq == 0, n);
            knobs = k;
            const double dStep = 1.0 - std::exp (-1.0 / (0.015 * sr));   // DELTA は 15 ms で入れ替える
            const double dTarget = k.delta ? 1.0 : 0.0;
            const float targetOut = std::pow (10.0f, k.outDb / 20.0f);
            if (outGain < 0.0f) outGain = targetOut;
            const float outStep = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));

            // HUM: ノッチの周波数・深さを 32 サンプルごとに作り直す
            int done = 0;
            while (done < n)
            {
                const int m = std::min (n - done, H - pos);
                // 入口（HUM の前）を DELTA 用に遅らせて保存しつつ、HUM をかけて STFT の入口へ
                for (int i = 0; i < m; ++i)
                {
                    float mono = 0.0f;
                    for (int c = 0; c < nCh; ++c) mono += ch[c][done + i];
                    hum.push (mono / (float) nCh);
                }
                humBlock (ch, done, m, k);
                for (int i = 0; i < m; ++i)
                {
                    deltaMix += (dTarget - deltaMix) * dStep;
                    if (std::abs (deltaMix - dTarget) < 1e-6) deltaMix = dTarget;   // 端まで届かせる（取った音に普通の出口が残らない）
                    deltaRamp[(size_t) i] = (float) deltaMix;
                }
                for (int c = 0; c < nCh; ++c)
                {
                    float* x = ch[c] + done;
                    float* fin = inFrame[c].data() + (N - H) + pos;
                    const float* ob = outBuf[c].data() + pos;
                    for (int i = 0; i < m; ++i)
                    {
                        fin[i] = humOut[c][(size_t) i];
                        // 遅らせた元の音
                        const int dp = (dryPos + i) % N;
                        const float d = dry[c][(size_t) dp];
                        dry[c][(size_t) dp] = x[i];
                        // DELTA: 普通の出口 ob と「取った音」d − ob を混ぜて入れ替える（y = ob + m·(d − 2·ob)）
                        const float mm = deltaRamp[(size_t) i];
                        x[i] = ob[i] + mm * (d - 2.0f * ob[i]);
                    }
                }
                for (int i = 0; i < m; ++i) outGain += (targetOut - outGain) * outStep, outGainBuf[(size_t) i] = outGain;
                for (int c = 0; c < nCh; ++c) { float* x = ch[c] + done; for (int i = 0; i < m; ++i) x[i] *= outGainBuf[(size_t) i]; }
                dryPos = (dryPos + m) % N;
                pos += m; done += m;
                if (pos == H) { doFrame(); pos = 0; }
            }
        }

        double sampleRate() const { return sr; }

    private:
        void prepareSmoothers()
        {
            const auto& t = tuning();
            lowBias.assign ((size_t) K, 0.0f); hfW.assign ((size_t) K, 1.0f);
            for (int kk = 0; kk < K; ++kk)
            {
                const float f = std::max (1.0f, (float) kk * binHz);
                const float lb = std::clamp (std::log2 (800.0f / f) / std::log2 (16.0f), 0.0f, 1.0f);
                lowBias[(size_t) kk] = t.nLowBias * lb;
                const float pts[4][2] = { { 6000.0f, 1.0f }, { 10500.0f, t.nHfW1 }, { 14000.0f, t.nHfW3 }, { 18000.0f, t.nHfW2 } };
                float w = 1.0f;
                if (f >= pts[3][0]) w = pts[3][1];
                else for (int i = 0; i < 3; ++i)
                    if (f >= pts[i][0] && f < pts[i + 1][0])
                    {
                        const float a = std::log2 (f / pts[i][0]) / std::log2 (pts[i + 1][0] / pts[i][0]);
                        w = pts[i][1] + (pts[i + 1][1] - pts[i][1]) * a;
                    }
                hfW[(size_t) kk] = w;
            }
            smN.prepare (K, t.nFreqOct, t.nFreqMin);
            smNG.prepare (K, t.nGainOct, 0.0f);
            smRG.prepare (K, t.rGainOct, 0.0f);
        }
        inline float coef (float ms) const { return ms <= 0.0f ? 1.0f : 1.0f - std::exp (-(float) hopSec / (ms * 0.001f)); }

        // ---------- HUM（時間領域）----------
        void humBlock (float* const* ch, int off, int m, const Knobs& k)
        {
            const auto& t = tuning();
            // 目標の基本周波数・深さ（ON/OFF は humMix で 40 ms かけて入れ替える。ノッチの深さはそのまま）
            const double f0 = k.humFreq == 1 ? 50.0 : (k.humFreq == 2 ? 60.0 : hum.f0);
            double depth = (double) k.humDepthDb;
            if (k.humFreq == 0 && ! hum.found) depth = 0.0;      // AUTO で見つからない間は何もしない
            humF0.store ((float) f0, std::memory_order_relaxed);
            humFound.store (hum.found, std::memory_order_relaxed);
            humConf.store (hum.confidence, std::memory_order_relaxed);
            const int nh = std::clamp (k.humHarm, 1, maxHarm);
            const double dcA = std::exp (-2.0 * M_PI * t.hDcHz / sr);
            if (! k.humOn && humMix <= 0.0)
            {
                for (int c = 0; c < nCh; ++c) std::memcpy (humOut[c].data(), ch[c] + off, sizeof (float) * (size_t) m);
                // 止まっている間も直流カットの状態は追っておく（入れた瞬間にポンと鳴らない）
                for (int c = 0; c < nCh; ++c) for (int i = 0; i < m; ++i) { const double x = ch[c][off + i]; const size_t cc = (size_t) c; dcz[cc] = x - dcx[cc] + dcA * dcz[cc]; dcx[cc] = x; }
                return;
            }
            const double kMix = 1.0 - std::exp (-1.0 / (0.04 * sr));        // ON/OFF・倍音の数: 40 ms（1 サンプルごと）
            int i = 0;
            while (i < m)
            {
                if (humCount <= 0)
                {
                    humCount = 32;
                    const double step = 1.0 - std::exp (-32.0 / (0.05 * sr));     // 深さ・周波数: 50 ms（32 サンプルごとに係数を作り直す）
                    humDepthNow += (depth - humDepthNow) * step;
                    humMixTarget = k.humOn ? 1.0 : 0.0;
                    if (! k.humOn && humMix < 1e-4)
                    {
                        // 完全に切れた → ノッチの記憶を消す（次に入れたとき古い状態から鳴らない）
                        humMix = 0.0;
                        for (auto& bq : notch) bq.reset();
                        hw.fill (0.0);
                    }
                    if (std::abs (f0 - humF0Now) > 2.0) humF0Now = f0; else humF0Now += (f0 - humF0Now) * step;
                    usedHarm = 0;
                    for (int h = 0; h < maxHarm; ++h)
                    {
                        hwTarget[(size_t) h] = (k.humOn || humMix > 0.0) && h < nh ? 1.0 : 0.0;
                        if (hwTarget[(size_t) h] == 0.0 && hw[(size_t) h] < 1e-4) { hw[(size_t) h] = 0.0; notch[(size_t) h].reset(); continue; }
                        usedHarm = h + 1;
                        const double fh = humF0Now * (h + 1);
                        if (fh < sr * 0.45) notch[(size_t) h].setPeak (sr, fh, t.hQ, -humDepthNow);
                        else { notch[(size_t) h].b0 = 1; notch[(size_t) h].b1 = notch[(size_t) h].b2 = notch[(size_t) h].a1 = notch[(size_t) h].a2 = 0; }
                    }
                }
                const int m2 = std::min (m - i, humCount);
                // 混ぜる量は 1 サンプルずつなめらかに（32 サンプルの階段にしない）
                bool hwMoving = false;
                for (int h = 0; h < usedHarm; ++h) hwMoving = hwMoving || hw[(size_t) h] != hwTarget[(size_t) h];
                for (int j = 0; j < m2; ++j)
                {
                    humMix += (humMixTarget - humMix) * kMix;
                    if (std::abs (humMix - humMixTarget) < 1e-7) humMix = humMixTarget;
                    mixRamp[(size_t) j] = humMix;
                    if (hwMoving)
                        for (int h = 0; h < usedHarm; ++h)
                        {
                            double& w = hw[(size_t) h];
                            w += (hwTarget[(size_t) h] - w) * kMix;
                            if (std::abs (w - hwTarget[(size_t) h]) < 1e-7) w = hwTarget[(size_t) h];
                            hwRamp[(size_t) h][(size_t) j] = w;
                        }
                }
                for (int c = 0; c < nCh; ++c)
                {
                    const float* x = ch[c] + off + i;
                    float* y = humOut[c].data() + i;
                    const size_t cc = (size_t) c;
                    for (int j = 0; j < m2; ++j)
                    {
                        const double in = x[j];
                        // 直流カット（1 次）→ ノッチ（倍音ごとの効きで混ぜる）→ 元の音と混ぜる
                        const double d = in - dcx[cc] + dcA * dcz[cc]; dcx[cc] = in; dcz[cc] = d;
                        double v = d;
                        for (int h = 0; h < usedHarm; ++h)
                        {
                            const double w = hwMoving ? hwRamp[(size_t) h][(size_t) j] : hw[(size_t) h];
                            if (w <= 0.0) continue;
                            const double nv = notch[(size_t) h].tick (v, c);
                            v = w >= 1.0 ? nv : v + (nv - v) * w;
                        }
                        y[j] = (float) (in + (v - in) * mixRamp[(size_t) j]);
                    }
                }
                i += m2; humCount -= m2;
            }
        }

        // ---------- STFT の 1 フレーム ----------
        void doFrame()
        {
            const auto& t = tuning();
            // 分析
            for (int c = 0; c < nCh; ++c)
            {
                vDSP_vmul (inFrame[c].data(), 1, win.data(), 1, tmp.data(), 1, (vDSP_Length) N);
                fft.forward (tmp.data(), re[c].data(), im[c].data());
            }
            const float pScale = 1.0f / ((float) N * (float) N * 0.25f);   // 窓のエネルギーで正規化（だいたい dBFS）
            for (int kk = 0; kk < K; ++kk)
            {
                float s = 0.0f;
                for (int c = 0; c < nCh; ++c) s += re[c][(size_t) kk] * re[c][(size_t) kk] + im[c][(size_t) kk] * im[c][(size_t) kk];
                P[(size_t) kk] = s / (float) nCh * pScale + 1e-20f;
            }
            if (profilePending.exchange (false)) applyPendingProfile();

            noiseFrame (t);
            roomFrame (t);

            // まとめ（ON/OFF は 30 ms でなめらかに）
            const float mStep = coef (30.0f);
            mixNoise += ((knobs.noiseOn ? 1.0f : 0.0f) - mixNoise) * mStep;
            mixRoom += ((knobs.roomOn ? 1.0f : 0.0f) - mixRoom) * mStep;
            constexpr float ln10 = 2.302585093f;
            for (int kk = 0; kk < K; ++kk)
                G[(size_t) kk] = std::max (-80.0f, mixNoise * gNs[(size_t) kk] + mixRoom * gRs[(size_t) kk]) * (ln10 / 20.0f);
            vvexpf (G.data(), G.data(), &K);                                   // G = 10^(dB/20)
            if (++dispCount >= 4) { dispCount = 0; publishDisplay(); }        // 画面用は 4 フレームに 1 回（約 21 ms）

            // 合成
            for (int c = 0; c < nCh; ++c)
            {
                vDSP_vmul (re[c].data(), 1, G.data(), 1, re[c].data(), 1, (vDSP_Length) K);
                vDSP_vmul (im[c].data(), 1, G.data(), 1, im[c].data(), 1, (vDSP_Length) K);
                fft.inverse (re[c].data(), im[c].data(), tmp.data());
                vDSP_vmul (tmp.data(), 1, win.data(), 1, tmp.data(), 1, (vDSP_Length) N);
                vDSP_vsma (tmp.data(), 1, &olaScale, acc[c].data(), 1, acc[c].data(), 1, (vDSP_Length) N);
                std::memcpy (outBuf[c].data(), acc[c].data(), sizeof (float) * (size_t) H);
                std::memmove (acc[c].data(), acc[c].data() + H, sizeof (float) * (size_t) (N - H));
                std::fill (acc[c].begin() + (N - H), acc[c].end(), 0.0f);
                std::memmove (inFrame[c].data(), inFrame[c].data() + H, sizeof (float) * (size_t) (N - H));
            }
        }

        // ---------- NOISE ----------
        void noiseFrame (const Tuning& t)
        {
            smN.run (P.data(), Pf.data(), K);
            const float aA = coef (t.nAttMs), aR = coef (t.nRelMs), aT = coef (t.nTrkMs), aD = coef (t.nDownMs);
            const float upMax = t.nUpDbs * (float) hopSec;
            const bool learned = knobs.noiseMode == 1 && profileValid.load (std::memory_order_relaxed);
            const bool freeze = knobs.freezeAdapt;
            adaptFrozen.store (freeze, std::memory_order_relaxed);
            if (learnActive)
            {
                double tot = 0.0;
                for (int kk = 1; kk < K; ++kk) tot += P[(size_t) kk];
                if (tot > 1e-13)   // デジタルの無音は覚えない
                {
                    for (int kk = 0; kk < K; ++kk) learnSum[(size_t) kk] += Pf[(size_t) kk];
                    ++learnFrames;
                }
                learnProgress.store ((float) std::min (1.0, learnFrames * hopSec / learnSeconds), std::memory_order_relaxed);
                if (learnFrames * hopSec >= learnSeconds)
                {
                    finishLearn();
                    learnActive = false;
                    learning.store (false, std::memory_order_relaxed);
                }
            }
            const float bias = (learned ? t.nBiasL : t.nBiasA) + knobs.noiseThrDb * t.nThrScale;
            const float R = std::max (0.0f, knobs.noiseRedDb);
            constexpr float ln10 = 2.302585093f;
            const bool startedNow = nzStarted || ! freeze;                     // このフレームで推定が使えるか
            vvlog10f (work2.data(), Pf.data(), &K);                            // work2 = log10(Pf)
            const float b = std::max (0.02f, t.nKnee);
            for (int kk = 0; kk < K; ++kk)
            {
                const float L = 10.0f * work2[(size_t) kk];
                float& s = Ls[(size_t) kk];
                s += (L - s) * (L > s ? aA : aR);
                float& tr = trk[(size_t) kk];
                tr += (L - tr) * aT;
                if (! nzStarted) { tr = L; }
                float& e = nz[(size_t) kk];
                if (! nzStarted) { if (! freeze) e = L + 10.0f; }   // 推定は止められていない最初のフレームから始める
                else if (! freeze)
                {
                    if (tr < e) e += (tr - e) * aD;
                    else e += std::min ((tr - e) * t.nUpProp, upMax);
                }
                // まだ推定が始まっていない（曲の頭から Fader Pilot が「歌っている」）間は、ADAPTIVE では下げない
                const float th = (learned ? profBins[(size_t) kk] : (startedNow ? e : -400.0f)) + bias + lowBias[(size_t) kk];
                thrArr[(size_t) kk] = th;
                // しきい値より x dB 大きい → 自然な曲線 −(1/b)·10log10(1 + 10^(−b·a·x/10)) の 10^(…) の指数（自然対数）を先に作る
                const float x = s - th;
                work[(size_t) kk] = std::min (30.0f, -b * t.nSlope * x / 10.0f) * ln10;   // 10^30 まで（float があふれない）
            }
            if (! freeze) nzStarted = true;
            vvexpf (work.data(), work.data(), &K);
            for (int kk = 0; kk < K; ++kk) work[(size_t) kk] += 1.0f;
            vvlog10f (work.data(), work.data(), &K);
            for (int kk = 0; kk < K; ++kk)
                work[(size_t) kk] = std::max (-R, -(10.0f / b) * work[(size_t) kk]) * hfW[(size_t) kk];   // R で頭打ち、高域の倍率
            smNG.run (work.data(), gN.data(), K);
            const float gA = coef (t.nGAttMs), gR_ = coef (t.nGRelMs);
            for (int kk = 0; kk < K; ++kk)
            {
                float& g = gNs[(size_t) kk];
                const float v = gN[(size_t) kk];
                g += (v - g) * (v > g ? gA : gR_);
            }
        }

        void finishLearn()
        {
            // 覚えた形 = 平均の大きさ（dB）
            for (int kk = 0; kk < K; ++kk) work[(size_t) kk] = 10.0f * std::log10 (learnSum[(size_t) kk] / (float) learnFrames + 1e-20f);
            // 保存形式へ（23.4375 Hz おき）
            for (int j = 0; j < profileBins; ++j)
            {
                const float f = (float) j * profileStepHz;
                const float x = f / binHz;
                const int a = std::clamp ((int) std::floor (x), 0, K - 1), b = std::min (a + 1, K - 1);
                const float fr = std::clamp (x - (float) a, 0.0f, 1.0f);
                publishedProfile[(size_t) j].store (work[(size_t) a] * (1.0f - fr) + work[(size_t) b] * fr, std::memory_order_relaxed);
            }
            for (int kk = 0; kk < K; ++kk) profBins[(size_t) kk] = work[(size_t) kk];
            profileValid.store (true);
            profilePending.store (false);
            profileVersion.fetch_add (1);
            learnFrames = 0;
        }
        void applyPendingProfile()
        {
            if (! profileValid.load()) { std::fill (profBins.begin(), profBins.end(), -200.0f); return; }
            rebuildProfileBins();
        }
        // 保存形式（23.4375 Hz おき）→ 今のビン
        void rebuildProfileBins()
        {
            for (int kk = 0; kk < K; ++kk)
            {
                const float x = (float) kk * binHz / profileStepHz;
                const int a = std::clamp ((int) std::floor (x), 0, profileBins - 1), b = std::min (a + 1, profileBins - 1);
                const float fr = std::clamp (x - (float) a, 0.0f, 1.0f);
                profBins[(size_t) kk] = publishedProfile[(size_t) a].load (std::memory_order_relaxed) * (1.0f - fr)
                                      + publishedProfile[(size_t) b].load (std::memory_order_relaxed) * fr;
            }
        }

        // ---------- ROOM ----------
        // 1/12 オクターブの帯ごとに: 少し前（Td より前、窓 W の中）の「直接音」の大きさを、予想の減り方（TAIL）で減らしながら
        // いちばん大きいものを今の響きの予想 λ とする（窓の中の最大は帯ごとの単調な列で求める）。響きの割合 q = μ·λ / 今の大きさ
        void prepareRoomBands()
        {
            // 帯の境目: 0〜40 Hz を 1 本、その上を 1/12 オクターブ（ナイキストまで）
            rbEdge.clear();
            rbEdge.push_back (0);
            double f = 40.0;
            while (f < sr * 0.5)
            {
                const int k = (int) std::lround (f / binHz);
                if (k > rbEdge.back() && k < K) rbEdge.push_back (k);
                f *= std::pow (2.0, 1.0 / 12.0);
            }
            rbEdge.push_back (K);
            numRB = (int) rbEdge.size() - 1;
            rbCenter.assign ((size_t) numRB, 0.0f);
            for (int b = 0; b < numRB; ++b) rbCenter[(size_t) b] = 0.5f * (float) (rbEdge[(size_t) b] + rbEdge[(size_t) b + 1] - 1);
            // ビン → 帯（中心どうしの直線補間）
            binBand.assign ((size_t) K, 0); binFrac.assign ((size_t) K, 0.0f);
            for (int kk = 0, b = 0; kk < K; ++kk)
            {
                while (b + 1 < numRB && rbCenter[(size_t) b + 1] <= (float) kk) ++b;
                binBand[(size_t) kk] = b;
                binFrac[(size_t) kk] = b + 1 < numRB ? std::clamp (((float) kk - rbCenter[(size_t) b]) / std::max (1e-3f, rbCenter[(size_t) b + 1] - rbCenter[(size_t) b]), 0.0f, 1.0f) : 0.0f;
            }
            const size_t nb = (size_t) numRB;
            Pb.assign (nb, 0.0f); Penv.assign (nb, 0.0f); gB.assign (nb, 0.0f); gBs.assign (nb, 0.0f);
            hist.assign ((size_t) histLen * nb, 0.0f);
            dqS.assign ((size_t) histLen * nb, 0); dqV.assign ((size_t) histLen * nb, 0.0f);
            dqHead.assign (nb, 0); dqSize.assign (nb, 0);
        }
        inline void dqPushBack (int b, int64_t sIdx, float v, float r)
        {
            const size_t base = (size_t) b * (size_t) histLen;
            int& head = dqHead[(size_t) b]; int& sz = dqSize[(size_t) b];
            while (sz > 0)
            {
                const int bi = (head + sz - 1) % histLen;
                if (dqV[base + (size_t) bi] - r * (float) (sIdx - dqS[base + (size_t) bi]) <= v) --sz; else break;
            }
            if (sz >= histLen) { head = (head + 1) % histLen; --sz; }
            const int ni = (head + sz) % histLen;
            dqS[base + (size_t) ni] = sIdx; dqV[base + (size_t) ni] = v; ++sz;
        }
        inline void dqPopOld (int b, int64_t oldest)
        {
            const size_t base = (size_t) b * (size_t) histLen;
            int& head = dqHead[(size_t) b]; int& sz = dqSize[(size_t) b];
            while (sz > 0 && dqS[base + (size_t) head] < oldest) { head = (head + 1) % histLen; --sz; }
        }
        void roomFrame (const Tuning& t)
        {
            // NOISE で下げた後の大きさを帯に（NOISE → ROOM の順につないだのと同じ）
            const float aS = coef (t.rSmMs);
            constexpr float ln10 = 2.302585093f;
            for (int kk = 0; kk < K; ++kk) work[(size_t) kk] = mixNoise * gNs[(size_t) kk] * (ln10 / 10.0f);
            vvexpf (work.data(), work.data(), &K);                             // NOISE の下げ量（パワー）
            for (int b = 0; b < numRB; ++b)
            {
                double e = 0.0;
                for (int kk = rbEdge[(size_t) b]; kk < rbEdge[(size_t) b + 1]; ++kk)
                    e += (double) P[(size_t) kk] * (double) work[(size_t) kk];
                const float v = (float) (e / (double) std::max (1, rbEdge[(size_t) b + 1] - rbEdge[(size_t) b]));
                Pb[(size_t) b] += (v - Pb[(size_t) b]) * aS;
            }
            const float tail = std::clamp (knobs.roomTail, 0.3f, 6.0f);
            const float r = (float) (60.0 / (t.rRate * std::pow ((double) tail, (double) t.rRateP)) * hopSec);   // 予想が 1 フレームで減る dB
            const int fd = std::max (1, (int) std::lround (t.rTdMs * 0.001 / hopSec));
            const int W = std::clamp ((int) std::lround ((t.rWinA + t.rWinB * tail) / hopSec), 1, histLen - fd - 2);
            ++framesSinceRebuild;
            if ((W != lastW1 || fd != lastFd || std::abs (r - lastR) > 1e-6f) && framesSinceRebuild >= 16)
            {
                // 窓・減り方が変わった（TAIL を動かした）→ 履歴から並べ直す
                framesSinceRebuild = 0;
                lastW1 = W; lastFd = fd; lastR = r;
                std::fill (dqSize.begin(), dqSize.end(), 0); std::fill (dqHead.begin(), dqHead.end(), 0);
                for (int64_t sIdx = std::max<int64_t> (0, frameNo - fd - W); sIdx <= frameNo - fd; ++sIdx)
                {
                    if (sIdx < 0) continue;
                    const float* h = hist.data() + (size_t) (sIdx % histLen) * (size_t) numRB;
                    for (int b = 0; b < numRB; ++b) dqPushBack (b, sIdx, 10.0f * std::log10 (h[b] + 1e-30f), r);
                }
            }
            else if (frameNo - lastFd >= 0)
            {
                const int64_t sIdx = frameNo - lastFd;
                const float* h = hist.data() + (size_t) (sIdx % histLen) * (size_t) numRB;
                for (int b = 0; b < numRB; ++b)
                {
                    dqPushBack (b, sIdx, 10.0f * std::log10 (h[b] + 1e-30f), lastR);
                    dqPopOld (b, frameNo - lastFd - lastW1);
                }
            }

            const float R = knobs.roomRedDb;
            // REDUCTION が大きいほど「響き」とみなす割合も、下げる深さも増える（本物: 声では R² に、響きだけの所では R に比例）
            const float r10 = std::max (0.0f, R) / 10.0f;
            const float mu = t.rMu0 * std::pow (r10, t.rMuP);
            const float q0 = std::clamp (t.rQ0, 0.0f, 0.95f);
            float* slot = hist.data() + (size_t) (frameNo % histLen) * (size_t) numRB;
            const float gAb = coef (t.rGAttMs), gRb = coef (t.rGRelMs);
            const float envDecay = std::pow (10.0f, -t.rEnvDbs * (float) hopSec / 10.0f);
            for (int b = 0; b < numRB; ++b)
            {
                float lamDb = -300.0f;
                if (dqSize[(size_t) b] > 0)
                {
                    const size_t i = (size_t) b * (size_t) histLen + (size_t) dqHead[(size_t) b];
                    lamDb = dqV[i] - lastR * (float) (frameNo - dqS[i]);
                }
                const float lam = std::pow (10.0f, lamDb / 10.0f);
                const float q = std::min (1.0f, (R >= 0.0f ? mu : t.rMu0) * lam / std::max (1e-20f, Pb[(size_t) b]));
                const float qq = std::max (0.0f, (q - q0) / (1.0f - q0));
                float g;
                if (R >= 0.0f) g = -std::min (t.rMaxDb, t.rA * r10 * std::pow (qq, t.rGamma));
                else g = t.rBoostK * (-R) * qq;
                // 帯ごとの下げ量を時間方向にならす（上がる = 戻る速さ rGAttMs、下がる = 深くなる速さ rGRelMs）
                float& gs = gBs[(size_t) b];
                gs += (g - gs) * (g > gs ? gAb : gRb);
                gB[(size_t) b] = gs;
                // 履歴に入れる「直接音」: ゆっくり減る包絡（rEnvDbs dB/秒）を rDryK 倍より超えた所（新しく鳴った所）だけ。
                // 響きの尾は包絡より下なので入らない（入れると尾が自分を響きと見なし続け、本物が止める所でも下げ続けた）
                float d = Pb[(size_t) b];
                {
                    float& env = Penv[(size_t) b];
                    const float prevEnv = env * envDecay;
                    d = d > t.rDryK * prevEnv ? d : 0.0f;
                    env = std::max (Pb[(size_t) b], prevEnv);
                }
                slot[b] = d;
            }
            ++frameNo;
            // 帯 → ビン
            for (int kk = 0; kk < K; ++kk)
            {
                const int b = binBand[(size_t) kk];
                const float fr = binFrac[(size_t) kk];
                work[(size_t) kk] = b + 1 < numRB ? gB[(size_t) b] * (1.0f - fr) + gB[(size_t) b + 1] * fr : gB[(size_t) b];
            }
            smRG.run (work.data(), gRs.data(), K);
        }

        void publishDisplay()
        {
            // 帯ごと: 入口の大きさ・しきい値・下げ量（ゆっくりならして画面へ）
            double nsum = 0.0;
            for (int i = 0; i < numDisp; ++i)
            {
                const float f0 = 20.0f * std::pow (1000.0f, (float) i / (float) numDisp);
                const float f1 = 20.0f * std::pow (1000.0f, (float) (i + 1) / (float) numDisp);
                int a = (int) std::floor (f0 / binHz), b = (int) std::ceil (f1 / binHz);
                a = std::clamp (a, 1, K - 1); b = std::clamp (b, a + 1, K);
                double pin = 0.0, thr = 0.0, gg = 0.0;
                for (int kk = a; kk < b; ++kk)
                {
                    pin += P[(size_t) kk]; thr += std::pow (10.0, (double) thrArr[(size_t) kk] / 10.0);
                    gg += (double) G[(size_t) kk] * G[(size_t) kk];
                }
                const float nb = (float) (b - a);
                const float inDb = 10.0f * std::log10 ((float) pin / nb + 1e-20f);
                const float thDb = 10.0f * std::log10 ((float) thr / nb + 1e-20f);
                const float gDb = 10.0f * std::log10 ((float) gg / nb + 1e-20f);
                auto upd = [] (std::atomic<float>& v, float x, float k) { const float o = v.load (std::memory_order_relaxed); v.store (o + (x - o) * k, std::memory_order_relaxed); };
                upd (dispIn[(size_t) i], inDb, 0.5f);
                upd (dispThr[(size_t) i], thDb, 0.5f);
                upd (dispGain[(size_t) i], gDb, 0.5f);
                nsum += thr;
            }
            noiseFloorDb.store ((float) (10.0 * std::log10 (nsum + 1e-30)), std::memory_order_relaxed);
        }

        double sr = 48000.0, hopSec = 256.0 / 48000.0;
        int nCh = 2, N = 2048, H = 256, K = 1025, pos = 0, dryPos = 0;
        float binHz = 23.4375f, olaScale = 0.25f;
        RealFft fft;
        std::vector<float> win, tmp;
        std::vector<float> inFrame[maxCh], acc[maxCh], outBuf[maxCh], dry[maxCh], re[maxCh], im[maxCh];
        std::vector<float> humOut[maxCh];
        std::array<float, 4096> outGainBuf {}, deltaRamp {};
        double deltaMix = 0.0;
        std::vector<float> P, Pf, Ls, trk, nz, gN, gNs, gRs, G, work, work2, learnSum, profBins, thrArr, lowBias, hfW;
        // ROOM（帯ごと）
        std::vector<int> rbEdge, binBand, dqHead, dqSize;
        std::vector<float> rbCenter, binFrac, Pb, Penv, gB, gBs, hist, dqV;
        std::vector<int64_t> dqS;
        int numRB = 1, histLen = 1, lastW1 = -1, lastFd = -1, framesSinceRebuild = 1000;
        float lastR = -1.0f;
        int64_t frameNo = 0;
        bool nzStarted = false, learnActive = false;
        int learnFrames = 0, seenLearnStart = 0, seenLearnStop = 0;
        FreqSmoother smN, smNG, smRG;
        Knobs knobs;
        float mixNoise = 0.0f, mixRoom = 0.0f, outGain = -1.0f;
        int dispCount = 0;
        // HUM
        HumFinder hum;
        std::array<BiquadD, maxHarm> notch;
        std::array<double, maxCh> dcz {}, dcx {};
        double humDepthNow = 0.0, humF0Now = 60.0, humMix = 0.0, humMixTarget = 0.0;
        std::array<double, maxHarm> hw {}, hwTarget {};   // 倍音ごとの効き（0..1。倍音の数を変えたときになめらかに）
        std::array<double, 32> mixRamp {};
        std::array<std::array<double, 32>, maxHarm> hwRamp {};
        int humCount = 0, usedHarm = 0;
        // LEARN の形（保存形式。メッセージスレッドの setProfile も音のスレッドの finishLearn もここへ書く）
        std::array<std::atomic<float>, profileBins> publishedProfile {};
        std::atomic<bool> profileValid { false }, profilePending { false };
        std::atomic<double> seedHumPending { 0.0 };
    };
}
