#pragma once
// Retro Retro の LOSS / ECHO / VARISPEED / MASTER と、全部をつなぐ Engine（JUCE なし）
#include "RetroModules.h"

#ifndef RR_CODEC_FLOOR
 #define RR_CODEC_FLOOR 54.0f
 #define RR_CODEC_STEP 18.0f
 #define RR_CODEC_PHASE 1.2f
 #define RR_CODEC_PUMP 24.0f
#endif
namespace rr
{
    //==============================================================================
    // 小さい FFT（2 のべき乗、複素）
    struct FFT
    {
        int n = 0; std::vector<std::complex<float>> tw; std::vector<int> rev;
        void prepare (int size)
        {
            n = size; tw.resize ((size_t) n / 2); rev.resize ((size_t) n);
            for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2 * kPi * i / n));
            int bits = 0; while ((1 << bits) < n) ++bits;
            for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
        }
        void run (std::vector<std::complex<float>>& a, bool inverse) const
        {
            for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (a[(size_t) i], a[(size_t) rev[(size_t) i]]);
            for (int len = 2; len <= n; len <<= 1)
            {
                const int step = n / len;
                for (int i = 0; i < n; i += len)
                    for (int j = 0; j < len / 2; ++j)
                    {
                        auto w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                        const auto u = a[(size_t) (i + j)], v = a[(size_t) (i + j + len / 2)] * w;
                        a[(size_t) (i + j)] = u + v; a[(size_t) (i + j + len / 2)] = u - v;
                    }
            }
            if (inverse) for (auto& v : a) v /= (float) n;
        }
    };

    //==============================================================================
    // LOSS: MAGNETIC（テープの摩耗 = 音量か高域がゆらゆら、音切れ）と CODEC（圧縮音声のザラザラ）。
    // MODE XFADE: 0 = MAGNETIC だけ、0.5 = 両方、1 = CODEC だけ
    struct Loss
    {
        double sr = 48000;
        // CODEC（STFT。窓 N、4 分の 1 ずつ重ねる）
        int N = 256, hop = 64;
        FFT fft;
        std::array<std::vector<float>, maxCh> inBuf, outAcc;
        std::array<std::vector<std::complex<float>>, maxCh> prevSpec, frozen;
        std::vector<float> win;
        std::vector<std::complex<float>> work;
        int fill = 0, outPos = 0;
        Rng rng { 13 };
        int stretchLeft = 0, dropLeft = 0;
        float frameGain = 1.0f;   // 枠ごとの音量の上下（足りないビットで大きさが揺れる。本物は量 100 % でばらつき −5 dB）
        // MAGNETIC
        Lfo lfoL, lfoR; std::array<Biquad, maxCh> wearLp; float lastCut = -1; OnePole wearSm;
        float dropGain = 1, dropTarget = 1; int dropCnt = 0;
        std::array<BandFocus, maxCh> focus; std::array<Biquad, maxCh> cutLo, cutHi;
        Flux flux; OnePole amtSm;
        std::array<DelayLine, maxCh> alignDl;   // 帯域の外を CODEC の遅れ（N）にそろえる

        int latency() const { return N - 1; }   // 枠の最後のサンプルが出るまで N − 1

        void prepare (double s)
        {
            sr = s;
            N = sr > 140000 ? 1024 : sr > 70000 ? 512 : 256; hop = N / 4;
            fft.prepare (N);
            win.resize ((size_t) N);
            for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2 * kPi * i / N));   // √ハン（分けて・戻して 1 になる）
            for (int c = 0; c < maxCh; ++c)
            {
                inBuf[(size_t) c].assign ((size_t) N, 0.0f); outAcc[(size_t) c].assign ((size_t) N * 2, 0.0f);
                prevSpec[(size_t) c].assign ((size_t) N, {}); frozen[(size_t) c].assign ((size_t) N, {});
                alignDl[(size_t) c].prepare (N + 8);
            }
            work.assign ((size_t) N, {});
            fill = 0; outPos = 0;
            lfoR.reset (0.37); lfoR.rng = Rng (31);
            flux.prepare (sr, 83); amtSm.setTime (20.0f, sr); wearSm.setTime (8.0f, sr);
            lastCut = -1;
        }
        void reset()
        {
            for (int c = 0; c < maxCh; ++c)
            {
                std::fill (inBuf[(size_t) c].begin(), inBuf[(size_t) c].end(), 0.0f);
                std::fill (outAcc[(size_t) c].begin(), outAcc[(size_t) c].end(), 0.0f);
                alignDl[(size_t) c].reset(); wearLp[(size_t) c].reset();
                std::fill (prevSpec[(size_t) c].begin(), prevSpec[(size_t) c].end(), std::complex<float> {});
                std::fill (frozen[(size_t) c].begin(), frozen[(size_t) c].end(), std::complex<float> {});
                cutLo[(size_t) c].reset(); cutHi[(size_t) c].reset(); focus[(size_t) c].reset();
            }
            fill = 0; outPos = 0; dropLeft = 0; stretchLeft = 0; frameGain = 1.0f; dropGain = dropTarget = 1.0f; wearSm.y = 1.0f;
        }

        // CODEC の 1 枠（スペクトルを粗くする）
        void codecFrame (int c, float a, const Knobs& k)
        {
            auto& in = inBuf[(size_t) c];
            for (int i = 0; i < N; ++i) work[(size_t) i] = { in[(size_t) i] * win[(size_t) i], 0.0f };
            fft.run (work, false);
            const int half = N / 2;
            if (a > 0.0005f)
            {
                // 枠の中でいちばん大きい所から、量に応じて下を切る（ビット割り当ての足りない圧縮音声）
                float mx = 1e-9f; for (int i = 1; i < half; ++i) mx = std::max (mx, std::abs (work[(size_t) i]));
                const float floorDb = -60.0f + RR_CODEC_FLOOR * a;   // これより小さい帯域は消える
                const float stepDb = 1.0f + RR_CODEC_STEP * a * a;   // 大きさの段（粗いほど「ジャリ」）
                const int bandLimit = (int) (half * (1.0f - 0.55f * a * a));   // 高い方から削る
                for (int i = 1; i < half; ++i)
                {
                    auto& v = work[(size_t) i];
                    float m = std::abs (v);
                    float ldb = gainToDb (m / mx);
                    if (ldb < floorDb || i > bandLimit) m = 0;
                    else
                    {
                        ldb = std::round (ldb / stepDb) * stepDb;
                        m = mx * dbToGain (ldb);
                    }
                    float ph = std::arg (v);
                    // 小さめの帯域ほど位相も崩れる（ビットが足りない帯域の「シュワシュワ」）
                    if (m > 0 && ldb < -6.0f) ph += a * RR_CODEC_PHASE * rng.bi() * (float) kPi;
                    // BLUR: 前の枠と混ぜてにじませる
                    if (k.codecBlur > 0.001f)
                    {
                        const auto pv = prevSpec[(size_t) c][(size_t) i];
                        m = lerp (m, std::abs (pv), k.codecBlur * 0.85f);
                        ph = ph + k.codecBlur * 0.8f * rng.bi() * (float) kPi;
                    }
                    v = std::polar (m, ph);
                }
                // 音切れ（DROPOUTS）: Repeat = 前の枠をくり返す / FFT Freeze = 止めた枠を鳴らし続ける
                if (dropLeft > 0)
                {
                    for (int i = 1; i < half; ++i) work[(size_t) i] = k.codecDropMode == 0 ? prevSpec[(size_t) c][(size_t) i] : frozen[(size_t) c][(size_t) i];
                }
                else for (int i = 0; i < half; ++i) frozen[(size_t) c][(size_t) i] = work[(size_t) i];
                for (int i = 1; i < half; ++i) work[(size_t) (N - i)] = std::conj (work[(size_t) i]);
                work[(size_t) half] = {};
            }
            for (int i = 0; i < half; ++i) prevSpec[(size_t) c][(size_t) i] = work[(size_t) i];
            fft.run (work, true);
            auto& acc = outAcc[(size_t) c];
            const float fg = a > 0.0005f ? frameGain : 1.0f;
            for (int i = 0; i < N; ++i) acc[(size_t) i] += work[(size_t) i].real() * win[(size_t) i] * 0.5f * fg;   // √ハン × √ハン を 4 分の 1 ずつ重ねると 2 倍
        }

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const Transport& tr, const float* envNorm)
        {
            const float magShare = std::min (1.0f, 2.0f * (1.0f - k.lossXfade)), codShare = std::min (1.0f, 2.0f * k.lossXfade);
            const double rateHz = k.magSync ? tr.bpm / 60.0 / noteBeats (magNotes()[k.magNote]) : 0.5 * std::pow (60.0, (double) k.magRate);   // 0.5〜30 Hz
            for (int c = 0; c < nCh; ++c)
            {
                focus[(size_t) c].set (k.lossLo, k.lossHi, sr);
                if (k.codecCut) { cutLo[(size_t) c].highpass (std::max (10.0f, knobHz (k.lossLo)), 0.7, sr); cutHi[(size_t) c].lowpass (std::min (knobHz (k.lossHi), (float) sr * 0.45f), 0.7, sr); }
            }
            for (int i = 0; i < n; ++i)
            {
                const float a = amtSm.process (clamp01 (amt * flux.next (k.lossFlux, envNorm[i], sr)));
                const float am = a * magShare, ac = a * codShare;
                // --- CODEC（遅れ N）
                for (int c = 0; c < nCh; ++c)
                {
                    const float x = io[c][i];
                    alignDl[(size_t) c].push (x);
                    inBuf[(size_t) c][(size_t) (N - hop + (fill % hop))] = x;   // 後で枠を作るとき使う
                }
                // 入力を貯める（hop 分たまったら 1 枠）
                ++fill;
                if (fill % hop == 0)
                {
                    // STRETCH: 枠をときどき止めて同じ所をくり返す
                    const bool hold = stretchLeft > 0;
                    if (hold) --stretchLeft;
                    else if (k.codecStretch > 0.001f && ac > 0.01f && rng.uni() < k.codecStretch * 0.08f) stretchLeft = 1 + (int) (rng.uni() * 12 * k.codecStretch);
                    if (dropLeft > 0) --dropLeft;
                    else if (k.lossDrop > 0.001f && ac > 0.01f && rng.uni() < k.lossDrop * 0.025f) dropLeft = 2 + (int) (rng.uni() * 20 * k.lossDrop);
                    {
                        const float u = rng.uni();
                        frameGain = dbToGain (-RR_CODEC_PUMP * ac * ac * u * u) * (1.0f + 0.12f * ac);
                    }
                    for (int c = 0; c < nCh; ++c)
                    {
                        if (! hold) codecFrame (c, ac, k);
                        else
                        {
                            // 止めている間は前の枠をそのまま重ねる
                            for (int j = 0; j < N; ++j) work[(size_t) j] = prevSpec[(size_t) c][(size_t) std::min (j, N / 2)];
                            auto tmp = prevSpec[(size_t) c];
                            for (int j = 1; j < N / 2; ++j) { work[(size_t) j] = tmp[(size_t) j]; work[(size_t) (N - j)] = std::conj (tmp[(size_t) j]); }
                            work[0] = tmp[0]; work[(size_t) (N / 2)] = {};
                            fft.run (work, true);
                            auto& acc = outAcc[(size_t) c];
                            for (int j = 0; j < N; ++j) acc[(size_t) j] += work[(size_t) j].real() * win[(size_t) j] * 0.5f;
                        }
                        // 入力を hop 分ずらす
                        auto& in = inBuf[(size_t) c];
                        std::memmove (in.data(), in.data() + hop, sizeof (float) * (size_t) (N - hop));
                        std::fill (in.begin() + (N - hop), in.end(), 0.0f);
                    }
                    outPos = 0;
                }
                for (int c = 0; c < nCh; ++c)
                {
                    auto& acc = outAcc[(size_t) c];
                    float y = acc[(size_t) outPos];
                    const float xd = alignDl[(size_t) c].tap (N - 1);
                    // 枠を全部使い切ったら前へ詰める
                    if (outPos == hop - 1)
                    {
                        std::memmove (acc.data(), acc.data() + hop, sizeof (float) * (size_t) (acc.size() - (size_t) hop));
                        std::fill (acc.end() - hop, acc.end(), 0.0f);
                    }
                    if (ac <= 0.0005f && stretchLeft == 0 && dropLeft == 0) y = xd;   // 何もしない時は分けて戻した音と同じ（そのまま）
                    float codecOut;
                    if (k.codecCut) codecOut = xd + k.codecMix * cutHi[(size_t) c].process (cutLo[(size_t) c].process (y - xd));
                    else codecOut = xd + k.codecMix * (y - xd);
                    io[c][i] = codecOut;
                }
                outPos = (outPos + 1) % hop;

                // --- MAGNETIC（摩耗: 音量 or 高域が揺れる ＋ 音切れ）
                if (am > 0.0005f || k.lossDrop > 0.0005f)
                {
                    const float wl = lfoL.next (rateHz, sr, k.magMorph, k.magWarp);
                    const float wr = k.wearStereo ? lfoR.next (rateHz, sr, k.magMorph, k.magWarp) : wl;
                    // 音切れ（MAGNETIC 側）: ときどきスッと下がる
                    if (--dropCnt <= 0)
                    {
                        dropCnt = (int) (sr * (0.04 + 0.3 * rng.uni()));
                        dropTarget = (rng.uni() < k.lossDrop * 0.5f * a * 2.0f) ? dbToGain (-6.0f - 40.0f * k.lossDrop * rng.uni()) : 1.0f;
                    }
                    dropGain = wearSm.process (dropTarget);
                    for (int c = 0; c < nCh; ++c)
                    {
                        const float w = c == 0 ? wl : wr;
                        const float x = io[c][i];
                        const float b = focus[(size_t) c].band (x);
                        // 揺れ（0..1）: 平均 −1 dB、深さ ±0.22（測った値）
                        const float mod = 0.5f + 0.5f * w;
                        const float vol = 1.0f - am * (1.0f - k.magVolFilter) * (0.10f + 0.23f * w);   // 平均 −0.9 dB・ばらつき 16 %（測った値）
                        float y = b * vol;
                        if (k.magVolFilter > 0.001f)
                        {
                            const float cut = 20000.0f * std::pow (2.0f, -am * k.magVolFilter * (1.5f + 3.5f * mod));
                            wearLp[(size_t) c].lowpass1 (std::min (cut, (float) sr * 0.45f), sr);
                            y = wearLp[(size_t) c].process (y);
                        }
                        y *= lerp (1.0f, dropGain, magShare);
                        io[c][i] = x - b + y;
                    }
                }
            }
        }
    };

    //==============================================================================
    // ECHO: テープ風のディレイ。くり返しの中に小さい WOBBLE / DISTORT / DIGITAL / SPACE / LOSS
    struct Echo
    {
        double sr = 48000;
        std::array<DelayLine, maxCh> dl;
        std::array<Biquad, maxCh> lo, hi, wear;
        std::array<OnePole, maxCh> timeSm;
        std::array<std::array<DelayLine, 4>, maxCh> diff;
        std::array<float, maxCh> held { 0, 0 }; double holdPh = 0;
        Lfo wob; Envelope duckEnv; Flux flux; OnePole amtSm;
        // GRAINS（くり返しの音程を変える。2 つの読み口を交互に）
        std::array<DelayLine, maxCh> gdl; double gPh = 0;
        Rng rng { 21 };

        void prepare (double s)
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (4.2 * sr));
            for (auto& d : gdl) d.prepare ((int) (0.2 * sr));
            for (auto& c : diff) for (auto& d : c) d.prepare ((int) (0.02 * sr));
            for (auto& t : timeSm) { t.setTime (150.0f, sr); t.y = (float) (0.25 * sr); }
            duckEnv.set (10.0f, 300.0f, sr); flux.prepare (sr, 97); amtSm.setTime (20.0f, sr); drySm.setTime (10.0f, sr); drySm.y = 1.0f;
        }
        bool firstBlock = true;
        OnePole drySm;
        int clearPhase = 0; float clearG = 1.0f;   // CLEAR: 響きを 8 ms で下げる → 消す → 8 ms で戻す
        void requestClear() { if (clearPhase == 0) clearPhase = 1; }
        void reset()
        {
            for (auto& d : dl) d.reset(); for (auto& d : gdl) d.reset(); for (auto& c : diff) for (auto& d : c) d.reset(); held = { 0, 0 }; firstBlock = true;
            for (auto& b : lo) b.reset(); for (auto& b : hi) b.reset(); for (auto& b : wear) b.reset();
            duckEnv.y = 0; holdPh = 0; gPh = 0;
        }

        static float freeMs (float v)
        {
            static const std::array<float, 5> xs { 0, 0.25f, 0.5f, 0.75f, 1 };
            static const std::array<float, 5> ys { 3.5f, 6.74f, 8.27f, 9.72f, 11.13f };   // log2(ms): 12, 107, 309, 843, 2240 ms
            return std::pow (2.0f, interp (xs, ys, v));
        }

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const Transport& tr, const float* envNorm, const float* inEnvDb)
        {
            const float scrub = std::pow (2.0f, (k.echoScrub - 0.5f) * 1.0f);
            auto timeOf = [&] (bool sync, int note, float v)
            {
                double ms = sync ? 60000.0 / tr.bpm * noteBeats (echoNotes()[std::max (0, std::min (kNumEchoNotes - 1, note))]) : freeMs (v);
                return (float) std::min (4.0 * sr, ms * 0.001 * sr * scrub);
            };
            const float tL = timeOf (k.echoSyncL, k.echoNoteL, k.echoTimeL);
            const float tR = k.echoLink ? tL : timeOf (k.echoSyncR, k.echoNoteR, k.echoTimeR);
            if (firstBlock) { timeSm[0].y = tL; timeSm[1].y = tR; firstBlock = false; }   // 最初は今の時間から（ゆっくり近づけない）
            for (int c = 0; c < maxCh; ++c)
            {
                lo[(size_t) c].highpass (std::max (10.0f, knobHz (k.echoLo)), 0.6, sr);
                hi[(size_t) c].lowpass (std::min (knobHz (k.echoHi), (float) sr * 0.45f), 0.6, sr);
                wear[(size_t) c].lowpass (std::min ((float) sr * 0.45f, 16000.0f * std::pow (2.0f, -4.0f * k.echoLoss)), 0.7, sr);
            }
            // 量: 元の音と響きを同じパワーで入れ替える（cos / sin、測った形）
            // 99 % までは輪の利得 0.97 以下（必ず消える）。99〜100 % で 1.04 まで上げて、tanh で頭打ちしながら鳴り続ける（本物と同じ）
            float fb = k.echoFb < 0.99f ? 0.97f * k.echoFb : lerp (0.97f * 0.99f, 1.04f, (k.echoFb - 0.99f) / 0.01f);
            if (k.echoDig > 0.001f && k.echoFb < 0.99f) fb *= 1.0f - 0.25f * std::min (1.0f, k.echoDig * 3.0f);   // サンプルホールドは山を拾うと 1 周で +3 dB になりうる
            const float pp = k.echoPingPong, width = k.echoWidth;
            const float grainSemi = (k.grainPitch - 0.5f) * 24.0f;
            const double gRatio = std::pow (2.0, grainSemi / 12.0);
            const float digFs = Digital::rateHz (k.echoDig * 0.8f);
            for (int i = 0; i < n; ++i)
            {
                const float a = amtSm.process (clamp01 (amt * flux.next (k.echoFlux, envNorm[i], sr)));
                const float dryG = drySm.process (k.echoSolo ? 0.0f : std::cos (a * 1.5707963f) * (a >= 0.999f ? 0.0f : 1.0f));
                const float wetG = std::sin (a * 1.5707963f) * 1.3f;
                const float de = duckEnv.process (dbToGain (inEnvDb[i]));
                const float duckG = 1.0f - k.echoDuck * std::min (1.0f, de * 5.0f) * 0.9f;
                const float wm = k.echoWob > 0 ? wob.next (0.6 + 4.0 * k.echoWob, sr, 0.0f, 0.5f) * k.echoWob * (float) (0.004 * sr) : 0.0f;
                float tap[maxCh];
                for (int c = 0; c < maxCh; ++c)
                {
                    const float T = timeSm[(size_t) c].process (c == 0 ? tL : tR);
                    tap[c] = dl[(size_t) c].read (T + wm + 4.0f);
                }
                // GRAINS: 読み口 2 つを三角窓で交互に
                if (k.grainAmt > 0.001f)
                {
                    const double gl = 0.06 * sr;
                    gPh += (1.0 - gRatio) / gl * (k.grainRev ? -1.0 : 1.0);
                    gPh -= std::floor (gPh);
                    for (int c = 0; c < maxCh; ++c)
                    {
                        gdl[(size_t) c].push (tap[c]);
                        const float d1 = (float) (gPh * gl), d2 = (float) (std::fmod (gPh + 0.5, 1.0) * gl);
                        const float w1 = (float) (1.0 - std::abs (2.0 * gPh - 1.0)), w2 = 1.0f - w1;
                        float g = gdl[(size_t) c].read (k.grainRev ? (float) gl - d1 : d1) * w1 + gdl[(size_t) c].read (k.grainRev ? (float) gl - d2 : d2) * w2;
                        if (k.grainInv) g = -g;
                        tap[c] = lerp (tap[c], g, k.grainAmt);
                    }
                }
                // くり返しの中の加工
                // 拾う時刻を揺らして、くり返しの周期とそろわないように
                holdPh += digFs / sr * (1.0 + 0.35 * rng.bi()); const bool take = holdPh >= 1.0 || k.echoDig < 0.001f; if (take) holdPh -= std::floor (holdPh);
                float fbS[maxCh];
                for (int c = 0; c < maxCh; ++c)
                {
                    float v = tap[c];
                    if (k.echoDist > 0.001f) { const float g = dbToGain (18.0f * k.echoDist); v = std::tanh (v * g) / g; }   // 小さい音を持ち上げない（輪の利得が 1 を超えないように）
                    if (k.echoDig > 0.001f) { if (take) held[(size_t) c] = v; v = held[(size_t) c]; }
                    if (k.echoSpace > 0.001f)
                        for (int j = 0; j < 4; ++j)
                        {
                            auto& d = diff[(size_t) c][(size_t) j]; const int L = (int) ((0.0031 + 0.0023 * j + 0.0007 * c) * sr); const float g = 0.65f * k.echoSpace;
                            const float dd = d.tap (L), u = v + g * dd; d.push (u); v = dd - g * u;
                        }
                    if (k.echoLoss > 0.001f) v = wear[(size_t) c].process (v) * (1.0f - 0.25f * k.echoLoss * (0.5f + 0.5f * rng.bi()));
                    v = hi[(size_t) c].process (lo[(size_t) c].process (v));
                    fbS[c] = v;
                }
                // 左右: ピンポン（PINGPONG の分だけ反対側へ返す）
                const float inL = io[0][i], inR = nCh > 1 ? io[1][i] : inL;
                const float inM = 0.5f * (inL + inR);
                const float sendL = lerp (inL, inM * 2.0f * 0.7071f, pp), sendR = lerp (inR, 0.0f, pp);
                const float fL = lerp (fbS[0], fbS[1], pp) * fb, fR = lerp (fbS[1], fbS[0], pp) * fb;
                dl[0].push (std::tanh (sendL + fL));
                dl[1].push (std::tanh (sendR + fR));
                // 出口: 幅
                float wl = fbS[0], wr = fbS[1];
                const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * (width * 2.0f);
                wl = m + s; wr = m - s;
                if (clearPhase != 0)
                {
                    const float st = 1.0f / (float) (0.008 * sr);
                    if (clearPhase == 1) { clearG -= st; if (clearG <= 0.0f) { clearG = 0.0f; for (auto& d : dl) d.reset(); for (auto& d : gdl) d.reset(); for (auto& c : diff) for (auto& d : c) d.reset();
                                                                              for (auto& b : lo) b.reset(); for (auto& b : hi) b.reset(); for (auto& b : wear) b.reset(); held = { 0, 0 }; clearPhase = 2; } }
                    else { clearG += st; if (clearG >= 1.0f) { clearG = 1.0f; clearPhase = 0; } }
                }
                io[0][i] = inL * dryG + wl * wetG * duckG * clearG;
                if (nCh > 1) io[1][i] = inR * dryG + wr * wetG * duckG * clearG;
            }
        }
    };

    //==============================================================================
    // VARISPEED: 決まった長さごとに、テープを 1/2・1/4・1/8 の速さで回した音へ（低く・遅く）。
    // 読み口を 2 つ持ち、RESYNC のたびに古い読み口から新しい読み口へ 10 ms で混ぜる（プチッとしない）。ON/OFF も 10 ms で混ぜる
    struct Varispeed
    {
        double sr = 48000; std::array<DelayLine, maxCh> dl; double rbA = 0, rbB = 0, since = 0, speed = 0.5; float xf = 1; OnePole act;
        void prepare (double s) { sr = s; for (auto& d : dl) d.prepare ((int) (13.0 * sr)); act.setTime (10.0f, sr); reset(); }
        void reset() { for (auto& d : dl) d.reset(); rbA = rbB = since = 0; xf = 1; act.y = 0; }
        void process (float** io, int nCh, int n, const Knobs& k, const Transport& tr)
        {
            const float target = k.varispeed > 0 ? 1.0f : 0.0f;
            if (target == 0.0f && act.y < 1e-4f)
            {
                act.y = 0; rbA = rbB = since = 0; xf = 1;
                for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) dl[(size_t) c].push (io[c][i]);
                return;
            }
            if (k.varispeed > 0) speed = 1.0 / (double) (1 << k.varispeed);
            const double period = std::min (12.0 * sr, 60.0 / tr.bpm * noteBeats (resyncNotes()[std::max (0, std::min (kNumResyncNotes - 1, k.resync))]) * sr);
            const float step = 1.0f / (float) (0.01 * sr);
            for (int i = 0; i < n; ++i)
            {
                since += 1; rbA += 1.0 - speed; rbB += 1.0 - speed;
                if (since >= period && target > 0) { since = 0; rbB = rbA; rbA = 0; xf = 0; }
                xf = std::min (1.0f, xf + step);
                const float g = act.process (target) * k.varispeedMix;
                for (int c = 0; c < nCh; ++c)
                {
                    dl[(size_t) c].push (io[c][i]);
                    float slow = dl[(size_t) c].read ((float) rbA);
                    if (xf < 1.0f) slow = lerp (dl[(size_t) c].read ((float) rbB), slow, xf);
                    io[c][i] = lerp (io[c][i], slow, g);
                }
            }
        }
    };

    //==============================================================================
    // MASTER EQ: CUT LO / CUT HI ＋ TONE 4 種類。
    // 回すつまみ（CUT・TONE）は 30 ms でなめらかにして 32 サンプルごとに係数を作り直す。切替（Hard・MODE・ON）は古い EQ と 20 ms で混ぜる
    struct MasterEQ
    {
        using Chain = std::array<std::array<Biquad, 8>, maxCh>;
        double sr = 48000; Chain cur, old; float xf = 1;
        int disc[4] { -1, -1, -1, -1 }; int side = 0; float sLo = -1, sHi = -1, sTone = -1, dLo = -1, dHi = -1, dTone = -1;

        void prepare (double s) { sr = s; disc[0] = -1; sLo = -1; xf = 1; }
        void reset() { for (auto& c : cur) for (auto& b : c) b.reset(); for (auto& c : old) for (auto& b : c) b.reset(); xf = 1; }

        void design (std::array<Biquad, 8>& b, float cutLo, float cutHi, bool loHard, bool hiHard, float tone, int mode, bool on) const
        {
            for (auto& x : b) x.setIdentity();
            if (! on) return;
            const float lo = knobHz (cutLo), hi = knobHz (cutHi);
            if (cutLo > 0.001f)
            {
                if (loHard) { b[0].highpass (lo * 1.6f, 0.54, sr); b[1].highpass (lo * 1.6f, 1.8, sr); }
                else b[0].highpass1 (lo, sr);
            }
            if (cutHi < 0.999f)
            {
                if (hiHard) { b[2].lowpass (std::min (hi * 0.75f, (float) sr * 0.45f), 0.54, sr); b[3].lowpass (std::min (hi * 0.75f, (float) sr * 0.45f), 1.8, sr); }
                else b[2].lowpass1 (std::min (hi, (float) sr * 0.45f), sr);
            }
            const float t = 2.0f * tone - 1.0f;
            const float G = 12.0f * t * std::abs (t);   // 端で ±12 dB（測った曲がり方）
            switch (mode)
            {
                case 0: b[4].lowShelf1 (800, -G, sr); b[5].highShelf1 (800, G, sr); break;                                    // TILT
                case 1: b[4].peak (1000, 0.6, G * 1.93f, sr); b[5].setIdentity(); b[5].b0 = dbToGain (-G * 0.92f); break;   // MID
                case 2:                                                                                                  // FILTER 1（ラジオの帯域 ⇄ 400 Hz の凹み）
                    if (t >= 0) { b[4].highpass (40 + 300 * t * t, 0.9, sr); b[5].lowpass (20000 * std::pow (2.0f, -2.6f * t), 0.9, sr); b[6].peak (1000, 1.2, 5 * t, sr); }
                    else { b[4].peak (420, 2.0, 12 * t, sr); b[5].peak (2500, 1.0, -3 * t, sr); }
                    break;
                default:                                                                                                 // FILTER 2（電話の帯域 ⇄ 高域の山）
                    if (t >= 0) { b[4].highpass (60 + 400 * t, 1.0, sr); b[5].peak (2000, 2.0, 4 * t, sr); b[6].lowpass (16000 * std::pow (2.0f, -2.2f * t), 1.2, sr); b[7].peak (4000, 3.0, -10 * t, sr); }
                    else { b[4].peak (6000, 1.5, -6 * t, sr); b[5].peak (12000, 3.0, 4 * t, sr); }
                    break;
            }
        }
        void apply (bool keepStates)
        {
            std::array<Biquad, 8> b;
            design (b, sLo, sHi, disc[0] != 0, disc[1] != 0, sTone, disc[2], disc[3] != 0);
            for (auto& c : cur)
                for (int j = 0; j < 8; ++j)
                {
                    const float z1 = c[(size_t) j].z1, z2 = c[(size_t) j].z2;
                    c[(size_t) j] = b[(size_t) j];
                    if (keepStates) { c[(size_t) j].z1 = z1; c[(size_t) j].z2 = z2; }
                }
            dLo = sLo; dHi = sHi; dTone = sTone;
        }

        void process (float** io, int nCh, int n, const Knobs& k)
        {
            const int nd[4] = { k.cutLoHard ? 1 : 0, k.cutHiHard ? 1 : 0, k.toneMode, k.eqOn ? 1 : 0 };
            if (sLo < 0) { sLo = k.cutLo; sHi = k.cutHi; sTone = k.tone; std::memcpy (disc, nd, sizeof (disc)); side = (disc[2] >= 2 && sTone >= 0.5f) ? 1 : 0; apply (false); }
            if (std::memcmp (disc, nd, sizeof (disc)) != 0)
            {
                old = cur; xf = 0.0f;                     // 古い EQ を残して混ぜる
                std::memcpy (disc, nd, sizeof (disc));
                apply (false);
            }
            if (disc[3] == 0 && xf >= 1.0f) return;     // EQ OFF（混ぜ終わったら何もしない）
            const float a = (float) std::exp (-32.0 / (0.03 * sr));
            const float step = 1.0f / (float) (0.02 * sr);
            for (int i0 = 0; i0 < n; i0 += 32)
            {
                const int m = std::min (32, n - i0);
                sLo = k.cutLo + (sLo - k.cutLo) * a; sHi = k.cutHi + (sHi - k.cutHi) * a; sTone = k.tone + (sTone - k.tone) * a;
                // FILTER 1 / 2 は TONE の真ん中で作るフィルターの形が変わる → またいだら切替と同じに 20 ms で混ぜる
                const int newSide = (disc[2] >= 2 && sTone >= 0.5f) ? 1 : 0;
                if (newSide != side) { side = newSide; old = cur; xf = 0.0f; apply (false); }
                if (std::abs (sLo - dLo) > 1e-4f || std::abs (sHi - dHi) > 1e-4f || std::abs (sTone - dTone) > 1e-4f) apply (true);
                for (int i = i0; i < i0 + m; ++i)
                {
                    const float w = xf;
                    if (xf < 1.0f) xf = std::min (1.0f, xf + step);
                    for (int c = 0; c < nCh; ++c)
                    {
                        const float x = io[c][i];
                        float v = x; for (auto& b : cur[(size_t) c]) v = b.process (v);
                        if (w < 1.0f) { float o = x; for (auto& b : old[(size_t) c]) o = b.process (o); v = lerp (o, v, w); }
                        io[c][i] = v;
                    }
                }
            }
        }
    };

    struct Compressor
    {
        double sr = 48000; Envelope env; OnePole gs;
        void prepare (double s) { sr = s; env.set (4.0f, 90.0f, sr); gs.setTime (2.0f, sr); reset(); }
        void reset() { env.y = 0; gs.y = 0; }
        static float slope (float c)
        {
            static const std::array<float, 5> xs { 0, 0.25f, 0.5f, 0.75f, 1 };
            static const std::array<float, 5> ys { 0, 0.36f, 0.56f, 0.68f, 0.75f };
            return interp (xs, ys, c);
        }
        void process (float** io, int nCh, int n, float c)
        {
            if (c <= 0.0005f && std::abs (gs.y) < 0.01f) { gs.y = 0; return; }
            const float s = slope (c);
            for (int i = 0; i < n; ++i)
            {
                float p = 0; for (int ch = 0; ch < nCh; ++ch) p = std::max (p, io[ch][i] * io[ch][i]);
                const float e = std::sqrt (env.process (p) + 1e-20f);
                const float eDb = gainToDb (e);
                // 中心 −10.2 dB（検出は速く上がる 2 乗の平均）。小さい音の持ち上げは −45 dB から下で戻し、−65 dB で 0（ノイズや残響の尾を持ち上げすぎない）
                float gDb = -s * (eDb + 10.2f);
                if (eDb < -45.0f) gDb *= std::max (0.0f, 1.0f - (-45.0f - eDb) / 20.0f);
                gDb = std::min (24.0f, gDb);
                const float g = dbToGain (gs.process (gDb));
                for (int ch = 0; ch < nCh; ++ch) io[ch][i] *= g;
            }
        }
    };

    //==============================================================================
    // ON/OFF をなめらかに（15 ms）。OFF で消え切ったら処理を止め、次に ON にするとき中身を消してから始める（前の音が鳴らない）
    struct ModuleFade
    {
        OnePole sm; bool dirty = false;
        void prepare (double sr, bool on) { sm.setTime (15.0f, sr); sm.y = on ? 1.0f : 0.0f; dirty = ! on; }
        // 戻り値: 処理するか。needReset: 中身を消してから処理する
        bool begin (bool on, bool& needReset)
        {
            needReset = false;
            if (! on && sm.y < 1e-4f) { sm.y = 0; dirty = true; return false; }
            if (on && dirty) { needReset = true; dirty = false; }
            return true;
        }
    };

    //==============================================================================
    struct Engine
    {
        double sr = 48000;
        NoiseGen noisePre, noisePost; Wobble wobble; std::array<Distort, 2> dist; Digital digital; std::array<Space, 2> space; Loss loss; Echo echo; Varispeed vari; MasterEQ eq; Compressor comp;
        std::vector<float> envNorm, envDb; Envelope inEnv;
        std::array<std::vector<float>, maxCh> dryBuf, tmpA, tmpB; std::array<DelayLine, maxCh> dryDl;
        std::array<OnePole, 4> gainSm; OnePole inWSm, outWSm;
        ModuleFade fWob, fDig, fSpace, fEcho, fDistOn;
        int cap = 0;
        // DISTORT・SPACE の種類切替（2 つを混ぜる）
        int distCur = 0, distType[2] { -1, -1 }, distFade = -1;
        int spaceCur = 0, spaceTypeNow[2] { -1, -1 }, spaceFade = -1;
        bool prevClear = false;
        Flux noiseFlux;

        int latency() const { return Distort::kLat + loss.latency(); }

        void prepare (double s, int maxBlock)
        {
            sr = s;
            noisePre.prepare (sr); noisePost.prepare (sr); noisePost.rng = Rng (77);
            wobble.prepare (sr); for (auto& d : dist) d.prepare (sr); digital.prepare (sr); for (auto& sp : space) sp.prepare (sr);
            loss.prepare (sr); echo.prepare (sr); vari.prepare (sr); eq.prepare (sr); comp.prepare (sr);
            cap = std::max (64, maxBlock);
            envNorm.assign ((size_t) cap, 0.0f); envDb.assign ((size_t) cap, -100.0f);
            inEnv.set (5.0f, 120.0f, sr);
            for (auto* v : { &dryBuf, &tmpA, &tmpB }) for (auto& b : *v) b.assign ((size_t) cap, 0.0f);
            for (auto& d : dryDl) d.prepare (latency() + 16);
            for (auto& g : gainSm) g.setTime (20.0f, sr);
            inWSm.setTime (20.0f, sr); outWSm.setTime (20.0f, sr);
            noiseFlux.prepare (sr, 101); noiseFluxPost.prepare (sr, 103);
            for (auto* f : { &fWob, &fDig, &fSpace, &fEcho, &fDistOn }) f->prepare (sr, true);
            reset();
        }
        bool firstBlock = true;
        void reset()
        {
            firstBlock = true;
            noisePre.reset(); noisePost.reset(); wobble.reset(); for (auto& d : dist) d.reset(); digital.reset(); for (auto& sp : space) sp.reset();
            loss.reset(); echo.reset(); vari.reset(); eq.reset(); comp.reset();
            for (auto& d : dryDl) d.reset();
            distFade = spaceFade = -1;
        }

        void process (float** io, int nCh, int n, const Knobs& k, const Transport& tr)
        {
            // 準備した大きさより長いブロックは分けて処理する（音の処理の中でメモリを確保しない）
            if (n > cap)
            {
                for (int pos = 0; pos < n; pos += cap)
                {
                    float* sub[maxCh] = { io[0] + pos, nCh > 1 ? io[1] + pos : nullptr };
                    process (sub, nCh, std::min (cap, n - pos), k, tr);
                }
                return;
            }
            nCh = std::min (nCh, maxCh);
            const float M = k.magnitude;
            if (firstBlock)   // 最初のブロックは今のつまみの値から（0 からゆっくり近づけない = 頭で元の音が漏れない）
            {
                gainSm[0].y = dbToGain ((k.inGain - 0.5f) * 36.0f); gainSm[1].y = dbToGain ((k.outGain - 0.5f) * 36.0f); gainSm[2].y = k.mix;
                inWSm.y = k.inWidth; outWSm.y = k.outWidth;
                fWob.prepare (sr, k.wobOn); fDig.prepare (sr, k.digOn); fSpace.prepare (sr, k.spaceOn); fEcho.prepare (sr, k.echoOn); fDistOn.prepare (sr, k.distOn);
                distType[distCur] = k.distType; spaceTypeNow[spaceCur] = k.spaceType;
                firstBlock = false;
            }
            for (int c = 0; c < nCh; ++c) std::memcpy (dryBuf[(size_t) c].data(), io[c], sizeof (float) * (size_t) n);
            // IN GAIN（±18 dB）・IN WIDTH
            const float inG = dbToGain ((k.inGain - 0.5f) * 36.0f);
            for (int i = 0; i < n; ++i)
            {
                const float g = gainSm[0].process (inG);
                for (int c = 0; c < nCh; ++c) io[c][i] *= g;
            }
            applyWidth (io, nCh, n, k.inWidth, inWSm);
            // 入力の大きさ（FOLLOW・DUCK・FLUX 用）
            bool silent = true;
            for (int i = 0; i < n; ++i)
            {
                float p = 0; for (int c = 0; c < nCh; ++c) p = std::max (p, std::abs (io[c][i]));
                if (p > 1.0e-4f) silent = false;   // −80 dBFS
                const float e = inEnv.process (p);
                envDb[(size_t) i] = gainToDb (e);
                envNorm[(size_t) i] = std::min (2.0f, e / 0.125f);
            }
            // NOISE（前と後ろは別々のノイズ。ON/OFF は量でなめらかに）
            const float nPre = 1.0f - k.noisePrePost;
            addNoise (noisePre, io, nCh, n, k, k.noiseOn ? M * nPre : 0.0f, silent);

            faded (fWob, k.wobOn, io, nCh, n, [&] { wobble.reset(); }, [&] { wobble.process (io, nCh, n, k, M * k.wobAmt, tr, envNorm.data()); });
            processDistort (io, nCh, n, k, M);
            faded (fDig, k.digOn, io, nCh, n, [&] { digital.reset(); }, [&] { digital.process (io, nCh, n, k, M * k.digAmt, envNorm.data()); });
            processSpace (io, nCh, n, k, M);
            loss.process (io, nCh, n, k, k.lossOn ? M * k.lossAmt : 0.0f, tr, envNorm.data());
            addNoise (noisePost, io, nCh, n, k, k.noiseOn ? M * k.noisePrePost : 0.0f, silent);
            // ECHO CLEAR は押した瞬間だけ
            if (k.echoClear && ! prevClear) echo.requestClear();
            prevClear = k.echoClear;
            faded (fEcho, k.echoOn, io, nCh, n, [&] { echo.reset(); }, [&] { echo.process (io, nCh, n, k, M * k.echoAmt, tr, envNorm.data(), envDb.data()); });
            vari.process (io, nCh, n, k, tr);
            eq.process (io, nCh, n, k);
            comp.process (io, nCh, n, k.compress);
            applyWidth (io, nCh, n, k.outWidth, outWSm);
            const float outG = dbToGain ((k.outGain - 0.5f) * 36.0f);
            const int lat = latency();
            for (int i = 0; i < n; ++i)
            {
                const float g = gainSm[1].process (outG), mx = gainSm[2].process (k.mix);
                for (int c = 0; c < nCh; ++c)
                {
                    dryDl[(size_t) c].push (dryBuf[(size_t) c][(size_t) i]);
                    io[c][i] = lerp (dryDl[(size_t) c].tap (lat), softClip (io[c][i] * g), mx);
                }
            }
        }

        // 最後の頭打ち（0.7 から丸めて 1.0 で止まる）
        static inline float softClip (float v)
        {
            if (std::abs (v) <= 0.7f) return v;
            return (v > 0 ? 1.0f : -1.0f) * (0.7f + 0.3f * std::tanh ((std::abs (v) - 0.7f) / 0.3f));
        }

        // ON/OFF のなめらかな切替でモジュールを通す
        template <typename ResetFn, typename ProcFn>
        void faded (ModuleFade& f, bool on, float** io, int nCh, int n, ResetFn resetFn, ProcFn procFn)
        {
            bool needReset = false;
            if (! f.begin (on, needReset)) return;
            if (needReset) resetFn();
            const bool steady = on && f.sm.y > 0.9999f;
            if (! steady) for (int c = 0; c < nCh; ++c) std::memcpy (tmpA[(size_t) c].data(), io[c], sizeof (float) * (size_t) n);
            procFn();
            if (steady) return;
            const float target = on ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = f.sm.process (target);
                for (int c = 0; c < nCh; ++c) io[c][i] = tmpA[(size_t) c][(size_t) i] + g * (io[c][i] - tmpA[(size_t) c][(size_t) i]);
            }
        }

        // DISTORT: 遅れ（28）があるのでいつも通す。ON/OFF は MIX でなめらかに、種類の切替は 2 つを混ぜる（4 ms 待ってから 25 ms）
        void processDistort (float** io, int nCh, int n, const Knobs& k, float M)
        {
            const float amt = k.distOn ? M * k.distAmt : 0.0f;
            Knobs kd = k; kd.distMix = k.distOn ? k.distMix : 0.0f;
            if (k.distType != distType[distCur] && distFade < 0)
            {
                const int o = 1 - distCur;
                dist[(size_t) o].reset(); dist[(size_t) o].amtSm.y = dist[(size_t) distCur].amtSm.y; dist[(size_t) o].mixSm.y = dist[(size_t) distCur].mixSm.y;
                distType[o] = k.distType; distFade = 0;
            }
            if (distFade < 0)
            {
                kd.distType = distType[distCur];
                dist[(size_t) distCur].process (io, nCh, n, kd, amt, envNorm.data());
                return;
            }
            const int o = 1 - distCur;
            for (int c = 0; c < nCh; ++c) std::memcpy (tmpB[(size_t) c].data(), io[c], sizeof (float) * (size_t) n);
            float* other[maxCh] = { tmpB[0].data(), tmpB[1].data() };
            kd.distType = distType[distCur]; dist[(size_t) distCur].process (io, nCh, n, kd, amt, envNorm.data());
            kd.distType = distType[o];       dist[(size_t) o].process (other, nCh, n, kd, amt, envNorm.data());
            const int wait = (int) (0.004 * sr), len = (int) (0.025 * sr);
            for (int i = 0; i < n; ++i)
            {
                const float w = clamp01 ((float) (distFade - wait) / (float) len);
                const float wc = 0.5f - 0.5f * std::cos ((float) kPi * w);
                for (int c = 0; c < nCh; ++c) io[c][i] = lerp (io[c][i], other[c][i], wc);
                ++distFade;
            }
            if (distFade >= wait + len) { distCur = o; distFade = -1; }
        }

        // SPACE: 種類の切替は 2 つの Space を 40 ms で混ぜる（遅れの長さが違うので）
        void processSpace (float** io, int nCh, int n, const Knobs& k, float M)
        {
            if (k.spaceType != spaceTypeNow[spaceCur] && spaceFade < 0 && k.spaceOn)
            {
                const int o = 1 - spaceCur;
                space[(size_t) o].reset(); space[(size_t) o].amtSm.y = space[(size_t) spaceCur].amtSm.y;
                spaceTypeNow[o] = k.spaceType; spaceFade = 0;
            }
            faded (fSpace, k.spaceOn, io, nCh, n, [&] { space[(size_t) spaceCur].reset(); spaceTypeNow[spaceCur] = k.spaceType; spaceFade = -1; }, [&]
            {
                Knobs ks = k;
                if (spaceFade < 0) { ks.spaceType = spaceTypeNow[spaceCur]; space[(size_t) spaceCur].process (io, nCh, n, ks, M * k.spaceAmt, envNorm.data(), envDb.data()); return; }
                const int o = 1 - spaceCur;
                for (int c = 0; c < nCh; ++c) std::memcpy (tmpB[(size_t) c].data(), io[c], sizeof (float) * (size_t) n);
                float* other[maxCh] = { tmpB[0].data(), tmpB[1].data() };
                ks.spaceType = spaceTypeNow[spaceCur]; space[(size_t) spaceCur].process (io, nCh, n, ks, M * k.spaceAmt, envNorm.data(), envDb.data());
                ks.spaceType = spaceTypeNow[o];       space[(size_t) o].process (other, nCh, n, ks, M * k.spaceAmt, envNorm.data(), envDb.data());
                const int len = (int) (0.04 * sr);
                for (int i = 0; i < n; ++i)
                {
                    const float w = clamp01 ((float) spaceFade / (float) len);
                    for (int c = 0; c < nCh; ++c) io[c][i] = lerp (io[c][i], other[c][i], w);
                    ++spaceFade;
                }
                if (spaceFade >= len) { spaceCur = o; spaceFade = -1; }
            });
        }

        void addNoise (NoiseGen& g, float** io, int nCh, int n, const Knobs& k, float share, bool silent)
        {
            g.setType (std::max (0, std::min (9, k.noiseType)));
            if (k.noiseAmt * share <= 0.0f) { g.silentFor = 0; return; }   // OFF・量 0 は何も足さない
            float tmp[maxCh];
            for (int i = 0; i < n; ++i)
            {
                tmp[0] = tmp[1] = 0;
                const float fm = &g == &noisePre ? noiseFlux.next (k.noiseFlux, envNorm[(size_t) i], sr) : noiseFluxPost.next (k.noiseFlux, envNorm[(size_t) i], sr);
                g.render (tmp, nCh, k.noiseAmt * share, k.noiseTone, k.noiseFollow, k.noiseDuck, envDb[(size_t) i], k.noiseLo, k.noiseHi, fm, silent, k.noiseLinkGain);
                for (int c = 0; c < nCh; ++c) io[c][i] += tmp[c];
            }
        }
        Flux noiseFluxPost;

        // WIDTH: 0.5 でそのまま。下げると S を 0 へ、上げると S を最大 2 倍・M を 1/2 へ（測った形）。20 ms でなめらかに
        static void applyWidth (float** io, int nCh, int n, float w, OnePole& sm)
        {
            if (nCh < 2) return;
            if (std::abs (w - 0.5f) < 1e-4f && std::abs (sm.y - 0.5f) < 1e-4f) { sm.y = 0.5f; return; }
            for (int i = 0; i < n; ++i)
            {
                const float ws = sm.process (w);
                float sG, mG;
                if (ws <= 0.5f) { sG = 2.0f * ws; mG = 1.0f; }
                else { sG = 1.0f + (ws - 0.5f) * 2.0f; mG = 1.0f / sG; }
                const float m = 0.5f * (io[0][i] + io[1][i]) * mG, s = 0.5f * (io[0][i] - io[1][i]) * sG;
                io[0][i] = m + s; io[1][i] = m - s;
            }
        }
    };
}
