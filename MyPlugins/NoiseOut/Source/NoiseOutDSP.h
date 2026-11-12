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
        // 本物の「Optimize for: Music」「Filter: Gentle」（TYPE）。Dialogue / Surgical からのずらし
        float nMusBias = -1.32843f, nMusUp = 0.768888f, nMusRel = 4.69877f, nMusLow = 2.66207f;   // Music: しきい値・推定の上がる速さの倍率・下げ量の戻りの倍率・低域のしきい値
        float nMusSlope = 0.951391f, nMusKnee = 0.874902f;   // Music: 下げ方の曲線（傾き・曲がり）の倍率
        float nGenOct = 5.5206f, nGenBias = -2.04157f, nGenHf = 0.702801f, nGenRel = 0.994672f;   // Gentle: 下げ量の周波数のならしの倍率・しきい値・高域の倍率をならす量（0..1）・戻りの倍率
        // ---- ROOM ----
        float rSmMs = 1.24902f;        // 大きさの時間方向のならし
        float rTdMs = 66.1684f;        // 直接音から何 ms 後を響きとみなすか
        float rRate = 0.550481f;         // 予想の減り方: 60 dB を rRate × TAIL^rRateP 秒で
        float rRateP = 0.633087f;
        float rWinA = 0.894831f;        // 予想の窓の長さ = rWinA + rWinB × TAIL（秒）
        float rWinB = 0.249534f;
        float rDryK = 1.21114f;         // 包絡の何倍を超えたら新しく鳴った直接音とみなすか
        float rEnvDbs = 6.84909f;      // 立ち上がりを見分ける包絡の減る速さ（dB/秒）
        float rMu0 = 2.3642f, rMuP = 2.20117f;    // 響きの割合 q = min(1, μ·λ/P)、μ = rMu0·(REDUCTION/10)^rMuP
        float rA = 23.3217f;           // 下げ量 = min(rMaxDb, rA·(REDUCTION/10)·q'^γ)
        float rGamma = 1.10287f;             // q' = (q − rQ0) / (1 − rQ0)
        float rQ0 = 0.316166f;
        float rMaxDb = 24.0f;       // いちばん深い下げ量
        float rBoostK = 1.34373f;       // REDUCTION がマイナスのとき: 上げ量 = rBoostK·|R|·q
        float rGAttMs = 3.51546f;       // 下げ量がなくなる速さ
        float rGRelMs = 47.9393f;      // 下げ量が深くなる速さ
        float rGainOct = 0.0805288f;      // 下げ量の周波数方向のならし
        float rSrcMix = 0.0f;       // 予想の元: 0 = 新しく鳴った所だけ、1 = 聞こえている大きさそのまま（両方を混ぜられる）
        float rAP = 0.837289f;           // 下げる深さの REDUCTION への効き: rA·(R/10)^rAP
        float rX1 = 596.455f, rX2 = 1488.85f, rX3 = 3999.37f, rXW = 0.238444f;   // 帯域ごとの強さの境目（Hz）と、なだらかさ（オクターブ）
        float rBandP = 5.47144f;        // 帯域ごとの強さ s（0..10、初期 6）→ μ の倍率 (s/6)^rBandP
        float rSmK = 0.15f;         // SMOOTHING（0..10、初期 9）→ 下げ量の時間のならしの倍率 exp(rSmK·(SMOOTHING−9))
        float rEst = 1.0f;          // 予想の仕方: 0 = 新しく鳴った所の最大を減らしていく、1 = 足し算（λ = a·λ + c·直接音、直接音 = 大きさ − κ·λ）
        float rHc = 0.0428444f, rHk = 0.820543f;   // 1 のときの c と κ
        float rLaw = 1.0f;          // 下げ方: 0 = q' = (q − q0)/(1 − q0)、1 = log10(q) のロジスティック
        float rLc = 1.04973f, rLk = 2.15107f;   // 1 のときの中心（log10 q）と傾き
        // ---- MOUTH（口のピチャ音）----
        float mFcBase = 1452.06f;    // 見分け・直す高域の境目（FREQ SKEW 0 のとき）。SKEW ±10 で 1/4〜4 倍
        float mThr0 = 29.5538f;        // ピチャ音とみなす「まわりより何 dB 大きいか」（SENSITIVITY 0 のとき）
        float mThrMin = 13.1841f;      // SENSITIVITY 10 のときのしきい値（dB）。上げても、歌・声のふつうの高域の揺れはピチャ音とみなさない（本物は感度 10 でも歌を −0.01 dB しか変えない）
        float mThrP = 0.476296f;         // その間の曲線: しきい値 = mThr0 − (mThr0 − mThrMin)·(SENSITIVITY/10)^mThrP（0〜10 のどこでも少しずつ効く）
        float mMaxMs = 4.39157f;        // これより長いものはピチャ音でない（子音など）
        float mBaseMs = 32.1405f;      // まわりの大きさのならし
        float mMargMs = 0.253916f;       // 直す範囲の前後の余白
        float mDepth = 1.0f;        // 直す深さ（1 = 境目より上を全部取る）
        float mRepHz = 957.195f;      // 直すときに残す低い音の境目（SKEW ±10 で 1/2〜2 倍）
        float mEndDb = 19.3741f;       // ピチャ音の終わり = 自分の山からこれだけ下がった所
        // ---- BREATH（息）----
        float bFlat = 0.28886f;        // 息とみなす平らさ（スペクトルの幾何平均 / 算術平均、500〜6000 Hz）
        float bSensK = 0.0183382f;       // SENSITIVITY 1 あたり平らさの基準を下げる量
        float bCmin = 915.144f, bCmax = 3433.7f;   // 息の重心の範囲（Hz）
        float bLoRatio = 0.249514f;     // 250 Hz 未満の割合がこれより小さい
        float bDv = -2.49382f;           // 声の大きさより何 dB 小さいか（0 = 声より大きくなければよい）
        float bDf = 4.24041f;           // 雑音の推定より何 dB 大きいか
        float bMinMs = 79.621f;      // これより短い（子音）は息でない
        float bAttMs = 11.2759f, bRelMs = 36.6764f;    // 下げ始め・戻りのならし
        float bSat = 26.3947f;         // いちばん深く下げても息の端は残る（本物 −100 dB で −26 dB 前後）
        // ---- PLOSIVE（吹かれ）----
        float pThr = 0.0618932f;          // 低域が「ふだん＋高い帯との差」より何 dB 出たら吹かれか（SENSITIVITY 5）
        float pSensK = 0.104817f;        // SENSITIVITY 1 あたり下げる dB
        float pRatioDb = -0.563319f;      // 低域と中域（500〜4000 Hz）の差の基準
        float pSlope = 2.16864f;        // 出た量 1 dB あたり下げる dB（STRENGTH 5）
        float pMaxDb = 75.8497f;       // STRENGTH 10 のいちばん深い下げ量
        float pBaseMs = 10.6567f;     // 低域のふだんの大きさのならし
        float pRelMs = 97.6417f;       // 戻り
        float pAhead = 6.52825f;   // 先の何フレームまでの最大で下げるか
        float pWidth = 0.195219f;        // 境目（FREQUENCY LIMIT）からのなだらかさ（オクターブ）
        float pStrP = 1.67682f;         // STRENGTH の効き方 (STRENGTH/5)^pStrP
        // ---- HUM ----
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
        bool noiseMusic = false;      // 本物の「Optimize for: Music」
        bool noiseGentle = false;     // 本物の「Filter: Gentle」
        bool freezeAdapt = false;     // Plugin Link: Fader Pilot が歌っている間は推定を止める
        bool roomOn = false;
        float roomRedDb = 10.0f;      // −10..20（マイナスは響きを足す）
        float roomTail = 1.0f;        // 秒
        float roomSmooth = 9.0f;      // 0..10（下げ量のなめらかさ）
        float roomBand[4] { 6.0f, 6.0f, 6.0f, 6.0f };   // 帯域ごとの強さ（低・中低・中高・高、0..10）
        float humWidthQ = 1000.0f;    // HUM の Q（DETAIL の WIDTH）
        bool mouthOn = false;
        float mouthSens = 4.0f;       // 0..10
        float mouthSkew = 0.0f;       // −10..10（低い側・高い側）
        bool breathOn = false;
        float breathGainDb = -12.0f;  // −30..0
        float breathSens = 5.0f;      // 0..10
        bool plosiveOn = false;
        float plosiveStrength = 5.0f; // 0..10
        float plosiveSens = 5.0f;     // 0..10
        float plosiveFreqHz = 200.0f; // 50..500
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
            // 先読み: 息・吹かれの判断のため 16 ホップ（48 kHz で 4096 = 85 ms）。うち 1 ホップは MOUTH の先読み、残り 15 ホップはフレームを貯めて遅らせる
            LA = 16 * H;
            dryLen = N + LA;
            for (int c = 0; c < maxCh; ++c)
            {
                inFrame[c].assign ((size_t) N, 0.0f); acc[c].assign ((size_t) N, 0.0f); outBuf[c].assign ((size_t) H, 0.0f);
                dry[c].assign ((size_t) dryLen, 0.0f); humOut[c].assign ((size_t) H, 0.0f); mouthOut[c].assign ((size_t) H, 0.0f);
                re[c].assign ((size_t) K, 0.0f); im[c].assign ((size_t) K, 0.0f);
                for (int r = 0; r < ringN; ++r) { ringRe[r][c].assign ((size_t) K, 0.0f); ringIm[r][c].assign ((size_t) K, 0.0f); }
            }
            for (int r = 0; r < ringN; ++r) ringG[r].assign ((size_t) K, 0.0f);
            prepareMouth();
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
            pos = 0; dryPos = 0; frameCount = 0;
            for (int r = 0; r < ringN; ++r) { for (int c = 0; c < maxCh; ++c) { std::fill (ringRe[r][c].begin(), ringRe[r][c].end(), 0.0f); std::fill (ringIm[r][c].begin(), ringIm[r][c].end(), 0.0f); }
                                              std::fill (ringG[r].begin(), ringG[r].end(), 0.0f); feats[r] = Feat {}; }
            breathRunPast = 0; breathGainNow = 0.0f; plosDepthNow = 0.0f; plosBase = -200.0f; breathFloor = -120.0f; breathVoice = -120.0f;
            resetMouth();
            std::fill (gRs.begin(), gRs.end(), 0.0f);
            std::fill (G.begin(), G.end(), 1.0f);
            std::fill (hist.begin(), hist.end(), 0.0f);
            std::fill (Pb.begin(), Pb.end(), 0.0f); std::fill (Penv.begin(), Penv.end(), 0.0f); std::fill (lamH.begin(), lamH.end(), 0.0f);
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

        int latencySamples() const { return N + LA; }

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
        // VOICE FIX の様子（画面用）: 直したピチャ音の数（増えていくだけ）、いま息・吹かれを下げている量（dB、0 以上）
        std::atomic<int> mouthClicks { 0 };
        std::atomic<float> breathCutDb { 0.0f }, plosCutDb { 0.0f };
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
                mouthBlock (m, k);
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
                        fin[i] = mouthOut[c][(size_t) i];
                        // 遅らせた元の音（全体の遅れ N + LA）
                        const int dp = (dryPos + i) % dryLen;
                        const float d = dry[c][(size_t) dp];
                        dry[c][(size_t) dp] = x[i];
                        // DELTA: 普通の出口 ob と「取った音」d − ob を混ぜて入れ替える（y = ob + m·(d − 2·ob)）
                        const float mm = deltaRamp[(size_t) i];
                        x[i] = ob[i] + mm * (d - 2.0f * ob[i]);
                    }
                }
                for (int i = 0; i < m; ++i) outGain += (targetOut - outGain) * outStep, outGainBuf[(size_t) i] = outGain;
                for (int c = 0; c < nCh; ++c) { float* x = ch[c] + done; for (int i = 0; i < m; ++i) x[i] *= outGainBuf[(size_t) i]; }
                dryPos = (dryPos + m) % dryLen;
                pos += m; done += m;
                if (pos == H) { doFrame(); pos = 0; }
            }
        }

        double sampleRate() const { return sr; }

    private:
        void prepareSmoothers()
        {
            const auto& t = tuning();
            lowBias.assign ((size_t) K, 0.0f); lowShape.assign ((size_t) K, 0.0f); hfW.assign ((size_t) K, 1.0f);
            for (int kk = 0; kk < K; ++kk)
            {
                const float f = std::max (1.0f, (float) kk * binHz);
                const float lb = std::clamp (std::log2 (800.0f / f) / std::log2 (16.0f), 0.0f, 1.0f);
                lowBias[(size_t) kk] = t.nLowBias * lb;
                lowShape[(size_t) kk] = lb;
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
            smNGg.prepare (K, t.nGainOct * t.nGenOct, 0.0f);
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
                    const double logQT = std::log (std::clamp ((double) k.humWidthQ, 300.0, 2000.0));   // つまみの範囲（300〜2000）と同じ
                    if (humLogQ < 0.0 || humMix <= 0.0) humLogQ = logQT; else humLogQ += (logQT - humLogQ) * step;
                    const double qNow = std::exp (humLogQ);
                    usedHarm = 0;
                    for (int h = 0; h < maxHarm; ++h)
                    {
                        hwTarget[(size_t) h] = (k.humOn || humMix > 0.0) && h < nh ? 1.0 : 0.0;
                        if (hwTarget[(size_t) h] == 0.0 && hw[(size_t) h] < 1e-4) { hw[(size_t) h] = 0.0; notch[(size_t) h].reset(); continue; }
                        usedHarm = h + 1;
                        const double fh = humF0Now * (h + 1);
                        if (fh < sr * 0.45) notch[(size_t) h].setPeak (sr, fh, qNow, -humDepthNow);
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
        // 分析したフレームは NOISE・ROOM の下げ量と一緒に輪（ringN 枚）に入れ、15 フレーム後に取り出して、
        // その間に分かった先の様子（息・吹かれ）の下げ量を足してから音に戻す
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
                float sum = 0.0f;
                for (int c = 0; c < nCh; ++c) sum += re[c][(size_t) kk] * re[c][(size_t) kk] + im[c][(size_t) kk] * im[c][(size_t) kk];
                P[(size_t) kk] = sum / (float) nCh * pScale + 1e-20f;
            }
            if (profilePending.exchange (false)) applyPendingProfile();

            noiseFrame (t);
            roomFrame (t);

            // まとめ（ON/OFF は 30 ms でなめらかに）
            const float mStep = coef (30.0f);
            mixNoise += ((knobs.noiseOn ? 1.0f : 0.0f) - mixNoise) * mStep;
            mixRoom += ((knobs.roomOn ? 1.0f : 0.0f) - mixRoom) * mStep;
            const int slot = (int) (frameCount % ringN);
            for (int c = 0; c < nCh; ++c)
            {
                std::memcpy (ringRe[slot][c].data(), re[c].data(), sizeof (float) * (size_t) K);
                std::memcpy (ringIm[slot][c].data(), im[c].data(), sizeof (float) * (size_t) K);
            }
            for (int kk = 0; kk < K; ++kk) ringG[slot][(size_t) kk] = mixNoise * gNs[(size_t) kk] + mixRoom * gRs[(size_t) kk];
            feats[slot] = computeFeat (t);

            // 15 フレーム前のフレームを音に戻す
            const bool ready = frameCount >= ringN - 1;
            const int so = (int) ((frameCount + 1) % ringN);   // いちばん古いフレーム
            if (ready)
            {
                voiceFixFrame (t, so);   // work = 息・吹かれの下げ量（dB）
                constexpr float ln10 = 2.302585093f;
                for (int kk = 0; kk < K; ++kk)
                    G[(size_t) kk] = std::max (-80.0f, ringG[so][(size_t) kk] + work[(size_t) kk]) * (ln10 / 20.0f);
                vvexpf (G.data(), G.data(), &K);
            }
            if (++dispCount >= 4) { dispCount = 0; publishDisplay(); }        // 画面用は 4 フレームに 1 回（約 21 ms）

            // 合成
            for (int c = 0; c < nCh; ++c)
            {
                if (ready)
                {
                    vDSP_vmul (ringRe[so][c].data(), 1, G.data(), 1, re[c].data(), 1, (vDSP_Length) K);
                    vDSP_vmul (ringIm[so][c].data(), 1, G.data(), 1, im[c].data(), 1, (vDSP_Length) K);
                    fft.inverse (re[c].data(), im[c].data(), tmp.data());
                    vDSP_vmul (tmp.data(), 1, win.data(), 1, tmp.data(), 1, (vDSP_Length) N);
                    vDSP_vsma (tmp.data(), 1, &olaScale, acc[c].data(), 1, acc[c].data(), 1, (vDSP_Length) N);
                }
                std::memcpy (outBuf[c].data(), acc[c].data(), sizeof (float) * (size_t) H);
                std::memmove (acc[c].data(), acc[c].data() + H, sizeof (float) * (size_t) (N - H));
                std::fill (acc[c].begin() + (N - H), acc[c].end(), 0.0f);
                std::memmove (inFrame[c].data(), inFrame[c].data() + H, sizeof (float) * (size_t) (N - H));
            }
            ++frameCount;
        }

        // ---------- 息・吹かれの手がかり（分析したフレームごと）----------
        struct Feat
        {
            float lvl = -200.0f;    // 300〜6000 Hz の大きさ（dB）
            float lo = -200.0f;     // 250 Hz 未満
            float flat = 0.0f;      // 500〜6000 Hz の平らさ（0..1）
            float cent = 0.0f;      // 300〜8000 Hz の重心（Hz）
            float plo = -200.0f;    // 吹かれの境目より下（なだらかな重み）
            float pmid = -200.0f;   // 500〜4000 Hz
            bool cand = false;      // 息らしい
        };
        Feat computeFeat (const Tuning& t)
        {
            Feat f;
            double e = 0.0, lo = 0.0, cs = 0.0, cw = 0.0, lsum = 0.0, asum = 0.0, pl = 0.0, pm = 0.0; int nf = 0;
            const float flim = std::clamp (knobs.plosiveFreqHz, 30.0f, 1000.0f);
            for (int kk = 1; kk < K; ++kk)
            {
                const float fq = (float) kk * binHz; const double v = P[(size_t) kk];
                if (fq >= 300.0f && fq < 6000.0f) e += v;
                if (fq < 250.0f) lo += v;
                if (fq >= 300.0f && fq < 8000.0f) { cs += v * fq; cw += v; }
                if (fq >= 500.0f && fq < 6000.0f) { lsum += std::log (v + 1e-30); asum += v; ++nf; }
                if (fq >= 500.0f && fq < 4000.0f) pm += v;
                if (fq < flim * 2.0f) pl += v * plosW[(size_t) kk];
            }
            f.lvl = 10.0f * (float) std::log10 (e + 1e-30);
            f.lo = 10.0f * (float) std::log10 (lo + 1e-30);
            f.cent = cw > 0.0 ? (float) (cs / cw) : 0.0f;
            f.flat = (nf > 0 && asum > 0.0) ? (float) (std::exp (lsum / nf) / (asum / nf)) : 0.0f;
            f.plo = 10.0f * (float) std::log10 (pl + 1e-30);
            f.pmid = 10.0f * (float) std::log10 (pm + 1e-30);
            // 息らしさ（平らで、重心が中ほど、低い音が少なく、声より小さく、雑音よりは大きい）
            const float flatTh = t.bFlat - (knobs.breathSens - 5.0f) * t.bSensK;
            const float loFrac = (float) (lo / (e + lo + 1e-30));
            f.cand = f.flat > flatTh && f.cent > t.bCmin && f.cent < t.bCmax && loFrac < t.bLoRatio
                  && f.lvl < breathVoice - t.bDv && f.lvl > breathFloor + t.bDf;
            // 声の大きさ（息らしくない大きいフレームの山、ゆっくり下がる）と雑音の大きさ（谷を追う）
            if (! f.cand && f.lvl > breathFloor + 10.0f) breathVoice = std::max (breathVoice - (float) (3.0 * hopSec), f.lvl);
            else breathVoice -= (float) (3.0 * hopSec);
            if (f.lvl < breathFloor) breathFloor += (f.lvl - breathFloor) * coef (40.0f);
            else breathFloor += std::min ((f.lvl - breathFloor) * coef (2000.0f), (float) (3.0 * hopSec));
            return f;
        }

        // 取り出すフレーム so の、息・吹かれの下げ量（dB、ビンごと）を work に
        void voiceFixFrame (const Tuning& t, int so)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            const Feat& f0 = feats[so];
            // ---- 息: so から後ろ（先読み）と前（数えてある）に続く「息らしい」フレームの長さ ----
            int ahead = 0;
            if (f0.cand)
                for (int i = 1; i < ringN; ++i)
                {
                    const int sl = (so + i) % ringN;
                    if (sl == (int) ((frameCount + 1) % ringN)) break;
                    if (! feats[sl].cand) break;
                    ++ahead;
                }
            const float runMs = (float) ((breathRunPast + 1 + ahead) * hopSec * 1000.0);
            const bool isBreath = knobs.breathOn && f0.cand && runMs >= t.bMinMs;
            breathRunPast = f0.cand ? breathRunPast + 1 : 0;
            // 深くしても本物は −26 dB 前後で止まる（tanh で頭打ち）
            const float bTarget = isBreath ? -t.bSat * std::tanh (std::max (0.0f, -knobs.breathGainDb) / std::max (1.0f, t.bSat)) : 0.0f;
            breathGainNow += (bTarget - breathGainNow) * (bTarget < breathGainNow ? coef (t.bAttMs) : coef (t.bRelMs));
            // ---- 吹かれ: 低域がふだん（と中域）よりどれだけ出たか。少し先まで見て一番大きい所で下げる ----
            float ex = 0.0f;
            for (int i = 0; i <= (int) t.pAhead && i < ringN - 1; ++i)
            {
                const Feat& fi = feats[(so + i) % ringN];
                const float th = t.pThr - (knobs.plosiveSens - 5.0f) * t.pSensK;
                ex = std::max (ex, fi.plo - std::max (plosBase, fi.pmid + t.pRatioDb) - th);
            }
            // ふだんの低域（ゆっくり上がる・すぐ下がる）
            plosBase += (f0.plo - plosBase) * (f0.plo > plosBase ? coef (t.pBaseMs) : coef (30.0f));
            const float st = std::clamp (knobs.plosiveStrength, 0.0f, 10.0f);
            const float stK = std::pow (st / 5.0f, t.pStrP);
            const float depth = knobs.plosiveOn && st > 0.0f ? std::min (t.pMaxDb * stK * 0.5f, std::max (0.0f, ex) * t.pSlope * stK) : 0.0f;
            plosDepthNow = depth > plosDepthNow ? depth : plosDepthNow + (depth - plosDepthNow) * coef (t.pRelMs);
            // ビンごとに
            const float bg = breathGainNow, pd = plosDepthNow;
            breathCutDb.store (-bg, std::memory_order_relaxed);
            plosCutDb.store (std::min (pd, 80.0f), std::memory_order_relaxed);   // 実際の下げは全体で −80 dB 止まり（画面も同じに）
            if (bg < -0.01f || pd > 0.01f)
                for (int kk = 0; kk < K; ++kk) work[(size_t) kk] = bg - pd * plosW[(size_t) kk];
        }
        void preparePlosiveWeights()
        {
            const auto& t = tuning();
            const float flim = std::clamp (knobs.plosiveFreqHz, 30.0f, 1000.0f);
            for (int kk = 0; kk < K; ++kk)
            {
                const float fq = std::max (1.0f, (float) kk * binHz);
                const float x = std::log2 (fq / flim) / std::max (0.05f, t.pWidth);
                plosW[(size_t) kk] = x <= 0.0f ? 1.0f : std::max (0.0f, 1.0f - x);   // 境目まで 1、そこから pWidth オクターブで 0
            }
            lastPlosF = flim;
        }

        // ---------- MOUTH（口のピチャ音。時間のまま、HUM の後・STFT の前。先読み H サンプル）----------
        // 高域（FREQ SKEW で決まる境目より上）の短い山（mMaxMs 未満）がまわりより大きく出たら、その間の高域だけを抜く
        void prepareMouth()
        {
            mLa = H;
            mFirLen = 2 * (int) std::lround (127.0 * sr / 48000.0) + 1;  // 直線位相の低域フィルター（48 kHz で 255 タップ）
            mFirLen = std::min (mFirLen, 2 * mLa - 1);                     // 半分の長さ ≤ 先読み（まだ来ていない音を読まない）
            // 輪: 読む所（出す所 − FIR の半分）から印を付ける先（今 + 余白）までが入る大きさ。
            // 前は mLa + 512 で固定にしていて、約 290 kHz より上で FIR が輪の外を読んだ（Logic にないレートだが、どのレートでも安全に）
            mRing = mLa + mFirLen + 512;
            for (int c = 0; c < maxCh; ++c) mBuf[c].assign ((size_t) mRing, 0.0f);
            mMask.assign ((size_t) mRing, 0.0f);
            mFir.assign ((size_t) mFirLen, 0.0f);
            lastSkew = -999.0f;
            plosW.assign ((size_t) K, 0.0f); lastPlosF = -1.0f;
            resetMouth();
        }
        void resetMouth()
        {
            for (int c = 0; c < maxCh; ++c) std::fill (mBuf[c].begin(), mBuf[c].end(), 0.0f);
            std::fill (mMask.begin(), mMask.end(), 0.0f);
            mW = 0; mHp.reset(); mEnv = 0.0; mBase = 1e-12; mInClick = false; mClickStart = 0; mN = 0; mHold = 0;
        }
        void designMouth (float skew)
        {
            const auto& t = tuning();
            mFc = std::clamp (t.mFcBase * std::pow (2.0f, skew / 5.0f), 300.0f, (float) (sr * 0.4));
            // 検出用の 2 次ハイパス
            const double w = 2.0 * M_PI * mFc / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.707), a0 = 1.0 + al;
            mHp.b0 = (1.0 + cw) / 2.0 / a0; mHp.b1 = -(1.0 + cw) / a0; mHp.b2 = mHp.b0; mHp.a1 = -2.0 * cw / a0; mHp.a2 = (1.0 - al) / a0;
            // 直す用の直線位相ローパス（窓付き sinc、ハン窓）
            mFcRep = std::clamp (t.mRepHz * std::pow (2.0f, skew / 10.0f), 100.0f, (float) (sr * 0.4));
            const int h = mFirLen / 2; double sum = 0.0;
            for (int i = 0; i < mFirLen; ++i)
            {
                const double n = i - h, fcn = mFcRep / sr;
                const double sinc = n == 0 ? 2.0 * fcn : std::sin (2.0 * M_PI * fcn * n) / (M_PI * n);
                const double wv = 0.5 - 0.5 * std::cos (2.0 * M_PI * (i + 0.5) / mFirLen);
                mFir[(size_t) i] = (float) (sinc * wv); sum += sinc * wv;
            }
            for (auto& v : mFir) v = (float) (v / sum);
            lastSkew = skew;
        }
        void mouthBlock (int m, const Knobs& k)
        {
            const auto& t = tuning();
            if (std::abs (k.mouthSkew - lastSkew) > 1e-3f)
            {
                const float jump = lastSkew > -900.0f ? std::abs (k.mouthSkew - lastSkew) : 0.0f;
                designMouth (k.mouthSkew);
                if (jump > 0.0f)
                {
                    // 大きく動かしたほど長く（±10 → ∓10 で 0.1 秒）。その間に「まわりの大きさ」が新しい帯域に追いつく
                    mHold = std::max (mHold, (int) (0.1 * sr * std::min (1.0f, jump / 5.0f)));
                    if (jump > 1.0f) mInClick = false;
                }
            }
            if (std::abs (k.plosiveFreqHz - lastPlosF) > 1e-3f) preparePlosiveWeights();
            const double aEnv = 1.0 - std::exp (-1.0 / (0.0002 * sr)), rEnv = 1.0 - std::exp (-1.0 / (0.0006 * sr));
            const double aBase = 1.0 - std::exp (-1.0 / (t.mBaseMs * 0.001 * sr));
            const double sensN = std::clamp ((double) k.mouthSens, 0.0, 10.0) / 10.0;
            const double thr = std::pow (10.0, ((double) t.mThr0 - ((double) t.mThr0 - (double) t.mThrMin) * std::pow (sensN, (double) t.mThrP)) / 10.0);
            const int marg = std::max (1, (int) (t.mMargMs * 0.001 * sr));
            // ピチャ音の長さの上限。先読み（mLa）より長いと頭がもう出てしまっていて直せないので、先読みに収まる長さまで（32 kHz で効く）
            const int maxLen = std::min ((int) (t.mMaxMs * 0.001 * sr), mLa - marg - 1);
            const int h = mFirLen / 2;
            const double endRatio = std::pow (10.0, -t.mEndDb / 10.0);
            for (int i = 0; i < m; ++i)
            {
                // 入口を輪に（先読みのため）
                float mono = 0.0f;
                for (int c = 0; c < nCh; ++c) { mBuf[c][(size_t) mW] = humOut[c][(size_t) i]; mono += humOut[c][(size_t) i]; }
                mono /= (float) nCh;
                mMask[(size_t) ((mN + marg) % mRing)] = 0.0f;   // これから印を付けうるいちばん先（前の周回の古い値を消す）
                // 見分け: 高域の短い山 / まわり（ゆっくり）
                const double hp = mHp.tick ((double) mono, 0);
                const double e = hp * hp;
                mEnv += (e - mEnv) * (e > mEnv ? aEnv : rEnv);
                const bool above = k.mouthOn && mHold == 0 && mEnv > mBase * thr && mEnv > 1e-10;
                if (mHold > 0) --mHold;
                if (! above || ! mInClick) mBase += (mEnv - mBase) * aBase;   // ピチャ音の間はまわりを止める
                mBase = std::max (mBase, 1e-12);
                if (above && ! mInClick) { mInClick = true; mClickStart = mN; mPeak = mEnv; }
                if (mInClick) mPeak = std::max (mPeak, mEnv);
                // 終わり = まわりの基準より下がった、か、自分の山から mEndDb 下がった（大きいピチャ音でも短く測る）
                const bool stillIn = above && mEnv > mPeak * endRatio;
                if (mInClick && (! stillIn || mN - mClickStart > maxLen))
                {
                    const int64_t len = mN - mClickStart;
                    if (len <= maxLen)   // 短い → ピチャ音。始まり−余白〜終わり+余白に印（なめらかな窓）
                    {
                        mouthClicks.fetch_add (1, std::memory_order_relaxed);
                        const int64_t a = mClickStart - marg, b = mN + marg;
                        for (int64_t n = a; n <= b; ++n)
                        {
                            if (mN - n >= mLa || n > mN + marg) continue;   // もう出してしまった所・先すぎる所は触らない
                            const double edge = std::min ((double) (n - a), (double) (b - n)) / (double) marg;
                            const float w = (float) (t.mDepth * (edge >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (M_PI * edge)));
                            float& mk = mMask[(size_t) (((n % mRing) + mRing) % mRing)];
                            mk = std::max (mk, w);
                        }
                    }
                    mInClick = false;
                }
                // 出口: mLa 前のサンプル − 印 × その高域（直線位相の LP で分けた残り）
                const int64_t outN = mN - mLa;
                for (int c = 0; c < nCh; ++c)
                {
                    const int idx = (int) (((outN % mRing) + mRing) % mRing);
                    const float x0 = outN >= 0 ? mBuf[c][(size_t) idx] : 0.0f;
                    const float mk = outN >= 0 ? mMask[(size_t) idx] : 0.0f;
                    float y = x0;
                    if (mk > 0.0f)
                    {
                        // FIR は左右対称なので、outN − h から順に並んだ輪の中身との内積（輪の端で 2 つに分ける）。
                        // 1 タップごとに割り算で輪の位置を求めていたら、192 kHz（1017 タップ）で 1 ブロックの持ち時間を超えた
                        const int start = (int) ((((outN - h) % mRing) + mRing) % mRing);
                        const int n1 = std::min (mFirLen, mRing - start);
                        float a1 = 0.0f, a2 = 0.0f;
                        vDSP_dotpr (mBuf[c].data() + start, 1, mFir.data(), 1, &a1, (vDSP_Length) n1);
                        if (n1 < mFirLen) vDSP_dotpr (mBuf[c].data(), 1, mFir.data() + n1, 1, &a2, (vDSP_Length) (mFirLen - n1));
                        y = x0 - mk * (x0 - (a1 + a2));
                    }
                    mouthOut[c][(size_t) i] = y;
                }
                mW = (mW + 1) % mRing; ++mN;
            }
        }

        // ---------- NOISE ----------
        void noiseFrame (const Tuning& t)
        {
            smN.run (P.data(), Pf.data(), K);
            const float aA = coef (t.nAttMs), aR = coef (t.nRelMs), aT = coef (t.nTrkMs), aD = coef (t.nDownMs);
            const bool mus = knobs.noiseMusic, gen = knobs.noiseGentle;
            const float upMax = t.nUpDbs * (mus ? t.nMusUp : 1.0f) * (float) hopSec;
            const bool learned = knobs.noiseMode == 1 && profileValid.load (std::memory_order_relaxed);
            // デジタルの無音（曲の頭・先読みの始まり）では推定を始めない・動かさない（無音の高さから始めると、ゆっくりしか上がらず効かなくなる）
            double ptot = 0.0;
            for (int kk = 1; kk < K; ++kk) ptot += P[(size_t) kk];
            const bool silent = ptot < 1e-13;
            const bool freeze = knobs.freezeAdapt || silent;
            adaptFrozen.store (knobs.freezeAdapt, std::memory_order_relaxed);
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
            const float bias = (learned ? t.nBiasL : t.nBiasA) + knobs.noiseThrDb * t.nThrScale + (mus ? t.nMusBias : 0.0f) + (gen ? t.nGenBias : 0.0f);
            const float R = std::max (0.0f, knobs.noiseRedDb);
            constexpr float ln10 = 2.302585093f;
            const bool startedNow = nzStarted || ! freeze;                     // このフレームで推定が使えるか
            vvlog10f (work2.data(), Pf.data(), &K);                            // work2 = log10(Pf)
            const float b = std::max (0.02f, t.nKnee * (mus ? t.nMusKnee : 1.0f));
            const float slopeN = t.nSlope * (mus ? t.nMusSlope : 1.0f);
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
                const float th = (learned ? profBins[(size_t) kk] : (startedNow ? e : -400.0f)) + bias + lowBias[(size_t) kk] + (mus ? t.nMusLow * lowShape[(size_t) kk] : 0.0f);
                thrArr[(size_t) kk] = th;
                // しきい値より x dB 大きい → 自然な曲線 −(1/b)·10log10(1 + 10^(−b·a·x/10)) の 10^(…) の指数（自然対数）を先に作る
                const float x = s - th;
                work[(size_t) kk] = std::min (30.0f, -b * slopeN * x / 10.0f) * ln10;   // 10^30 まで（float があふれない）
            }
            if (! freeze) nzStarted = true;
            vvexpf (work.data(), work.data(), &K);
            for (int kk = 0; kk < K; ++kk) work[(size_t) kk] += 1.0f;
            vvlog10f (work.data(), work.data(), &K);
            for (int kk = 0; kk < K; ++kk)
                work[(size_t) kk] = std::max (-R, -(10.0f / b) * work[(size_t) kk]) * (gen ? hfW[(size_t) kk] + (1.0f - hfW[(size_t) kk]) * t.nGenHf : hfW[(size_t) kk]);   // R で頭打ち、高域の倍率
            (gen ? smNGg : smNG).run (work.data(), gN.data(), K);
            const float gA = coef (t.nGAttMs), gR_ = coef (t.nGRelMs * (mus ? t.nMusRel : 1.0f) * (gen ? t.nGenRel : 1.0f));
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
            Pb.assign (nb, 0.0f); Penv.assign (nb, 0.0f); gB.assign (nb, 0.0f); gBs.assign (nb, 0.0f); bandMul.assign (nb, 1.0f); lamH.assign (nb, 0.0f);
            for (auto& v : lastBand) v = -1.0f;
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
            const bool habets = t.rEst > 0.5f;   // 足し算の予想（1.1.0 から）。前の方式（窓の中の最大を減らしていく）の並べ直しは、その方式のときだけ
            const int W = std::clamp ((int) std::lround ((t.rWinA + t.rWinB * tail) / hopSec), 1, histLen - fd - 2);
            ++framesSinceRebuild;
            if (! habets && (W != lastW1 || fd != lastFd || std::abs (r - lastR) > 1e-6f) && framesSinceRebuild >= 16)
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
            else if (! habets && frameNo - lastFd >= 0)
            {
                const int64_t sIdx = frameNo - lastFd;
                const float* h = hist.data() + (size_t) (sIdx % histLen) * (size_t) numRB;
                for (int b = 0; b < numRB; ++b)
                {
                    dqPushBack (b, sIdx, 10.0f * std::log10 (h[b] + 1e-30f), lastR);
                    dqPopOld (b, frameNo - lastFd - lastW1);
                }
            }

            // 足し算の予想（rEst = 1）: λ(t) = a·λ(t−1) + c·D(t − fd)
            if (habets && frameNo - fd >= 0)
            {
                const float aH = std::pow (10.0f, -r / 10.0f);
                const float* h = hist.data() + (size_t) ((frameNo - fd) % histLen) * (size_t) numRB;
                for (int b = 0; b < numRB; ++b) lamH[(size_t) b] = aH * lamH[(size_t) b] + t.rHc * h[b];
            }
            const float R = knobs.roomRedDb;
            // 帯域ごとの強さ（つまみ 4 本。境目はなだらかに）を帯に配る
            if (std::abs (knobs.roomBand[0] - lastBand[0]) + std::abs (knobs.roomBand[1] - lastBand[1])
                + std::abs (knobs.roomBand[2] - lastBand[2]) + std::abs (knobs.roomBand[3] - lastBand[3]) > 1e-4f)
            {
                for (int i = 0; i < 4; ++i) lastBand[i] = knobs.roomBand[i];
                const float xs[3] = { t.rX1, t.rX2, t.rX3 };
                for (int b = 0; b < numRB; ++b)
                {
                    const float f = std::max (20.0f, rbCenter[(size_t) b] * binHz);
                    // 4 つの帯の重み。below[i] = 境目 i より下にいる割合（ロジスティックで隣となだらかにつなぐ）
                    float below[3];
                    for (int i = 0; i < 3; ++i) below[i] = 1.0f / (1.0f + std::exp (std::log2 (f / xs[i]) / std::max (0.05f, t.rXW) * 4.0f));
                    const float w[4] = { below[0], std::max (0.0f, below[1] - below[0]), std::max (0.0f, below[2] - below[1]), 1.0f - below[2] };
                    float wsum = 0.0f, accW = 0.0f;
                    for (int i = 0; i < 4; ++i) { accW += w[i] * std::pow (std::clamp (knobs.roomBand[i], 0.0f, 10.0f) / 6.0f, t.rBandP); wsum += w[i]; }
                    bandMul[(size_t) b] = wsum > 0.0f ? accW / wsum : 1.0f;
                }
            }
            // REDUCTION が大きいほど「響き」とみなす割合も、下げる深さも増える（本物: 声では R² に、響きだけの所では R に比例）
            const float r10 = std::max (0.0f, R) / 10.0f;
            const float mu = t.rMu0 * std::pow (r10, t.rMuP);
            const float depthA = t.rA * std::pow (r10, t.rAP);
            const float q0 = std::clamp (t.rQ0, 0.0f, 0.95f);
            float* slot = hist.data() + (size_t) (frameNo % histLen) * (size_t) numRB;
            const float smScale = std::exp (t.rSmK * (std::clamp (knobs.roomSmooth, 0.0f, 10.0f) - 9.0f));
            const float gAb = coef (t.rGAttMs * smScale), gRb = coef (t.rGRelMs * smScale);
            const float envDecay = std::pow (10.0f, -t.rEnvDbs * (float) hopSec / 10.0f);
            for (int b = 0; b < numRB; ++b)
            {
                float lam = 0.0f;
                if (habets) lam = lamH[(size_t) b];
                else if (dqSize[(size_t) b] > 0)
                {
                    const size_t i = (size_t) b * (size_t) histLen + (size_t) dqHead[(size_t) b];
                    lam = std::pow (10.0f, (dqV[i] - lastR * (float) (frameNo - dqS[i])) / 10.0f);
                }
                const float ratio = (R >= 0.0f ? mu : t.rMu0) * bandMul[(size_t) b] * lam / std::max (1e-20f, Pb[(size_t) b]);
                float qq;
                if (t.rLaw > 0.5f) qq = 1.0f / (1.0f + std::exp (-t.rLk * (std::log10 (ratio + 1e-12f) - t.rLc)));   // log10 のロジスティック
                else qq = std::max (0.0f, (std::min (1.0f, ratio) - q0) / (1.0f - q0));
                float g;
                if (R >= 0.0f) g = -std::min (t.rMaxDb, depthA * std::pow (qq, t.rGamma));
                else g = t.rBoostK * (-R) * qq;
                // 帯ごとの下げ量を時間方向にならす（上がる = 戻る速さ rGAttMs、下がる = 深くなる速さ rGRelMs）
                float& gs = gBs[(size_t) b];
                gs += (g - gs) * (g > gs ? gAb : gRb);
                gB[(size_t) b] = gs;
                // 履歴に入れる「直接音」: ゆっくり減る包絡（rEnvDbs dB/秒）を rDryK 倍より超えた所（新しく鳴った所）だけ。
                // 響きの尾は包絡より下なので入らない（入れると尾が自分を響きと見なし続け、本物が止める所でも下げ続けた）
                float d = Pb[(size_t) b];
                if (habets) d = std::max (0.0f, d - t.rHk * lam);    // 予想した響きを引いた残り = 直接音
                else
                {
                    float& env = Penv[(size_t) b];
                    const float prevEnv = env * envDecay;
                    d = d > t.rDryK * prevEnv ? d : 0.0f;
                    env = std::max (Pb[(size_t) b], prevEnv);
                }
                slot[b] = t.rSrcMix * Pb[(size_t) b] + (1.0f - t.rSrcMix) * d;
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
        // 先読み（息・吹かれ）と MOUTH
        static constexpr int ringN = 16;
        int LA = 4096, dryLen = 6144;
        int64_t frameCount = 0;
        std::vector<float> ringRe[ringN][maxCh], ringIm[ringN][maxCh], ringG[ringN], mouthOut[maxCh], mBuf[maxCh], mMask, mFir, plosW;
        Feat feats[ringN];
        int breathRunPast = 0;
        float breathGainNow = 0.0f, plosDepthNow = 0.0f, plosBase = -200.0f, breathFloor = -120.0f, breathVoice = -120.0f, lastPlosF = -1.0f;
        int mLa = 256, mRing = 384, mFirLen = 63, mW = 0;
        int64_t mN = 0, mClickStart = 0;
        BiquadD mHp;
        double mEnv = 0.0, mBase = 1e-12, mPeak = 0.0;
        bool mInClick = false;
        int mHold = 0;   // FREQ SKEW を動かした直後、見分けを止めるサンプル数（見る帯域が変わっても「まわりの大きさ」が前の帯域のままで、ふつうの声をピチャ音とみなしたため）
        float mFc = 2000.0f, mFcRep = 500.0f, lastSkew = -999.0f;
        double deltaMix = 0.0;
        std::vector<float> P, Pf, Ls, trk, nz, gN, gNs, gRs, G, work, work2, learnSum, profBins, thrArr, lowBias, lowShape, hfW;
        // ROOM（帯ごと）
        std::vector<int> rbEdge, binBand, dqHead, dqSize;
        std::vector<float> rbCenter, binFrac, Pb, Penv, gB, gBs, hist, dqV, bandMul, lamH;
        float lastBand[4] { -1.0f, -1.0f, -1.0f, -1.0f };
        std::vector<int64_t> dqS;
        int numRB = 1, histLen = 1, lastW1 = -1, lastFd = -1, framesSinceRebuild = 1000;
        float lastR = -1.0f;
        int64_t frameNo = 0;
        bool nzStarted = false, learnActive = false;
        int learnFrames = 0, seenLearnStart = 0, seenLearnStop = 0;
        FreqSmoother smN, smNG, smNGg, smRG;
        Knobs knobs;
        float mixNoise = 0.0f, mixRoom = 0.0f, outGain = -1.0f;
        int dispCount = 0;
        // HUM
        HumFinder hum;
        std::array<BiquadD, maxHarm> notch;
        std::array<double, maxCh> dcz {}, dcx {};
        double humDepthNow = 0.0, humF0Now = 60.0, humMix = 0.0, humMixTarget = 0.0;
        double humLogQ = -1.0;   // WIDTH（Q）の log。50 ms でならす（急に変えると細いノッチの係数が跳んでプチッと鳴った）。負 = まだ決めていない
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
