#pragma once
// Retro Retro の DISTORT / DIGITAL / SPACE / LOSS（JUCE なし）
#include "RetroRetroDSP.h"
#include "RetroDistTables.h"   // 歪み 9 種類の数値（build/rclab の当てはめで自動生成）

namespace rr
{
    //==============================================================================
    // DISTORT: 9 種類。前の EQ → 4 倍 →（入口の大きさで利得を下げる）→ 頭打ちの形 → 1 倍 → 後ろの利得
    inline const char* distTypeName (int i)
    {
        static const char* n[] = { "Tube Pair", "Iron Core", "Crunch", "Heavy", "Zap", "Air Pressure", "Tape", "Classic Pre", "Pocket Recorder" };
        return n[std::max (0, std::min (8, i))];
    }
    constexpr int kNumDistTypes = 9;

    // 頭打ちの形（u: 入力、d: 型の数値）
    inline float distShape (float u, const DistModel& d)
    {
        // 非対称の tanh（bias で片側だけ早く潰れる）＋硬さ（hard: 0 = tanh、1 = ほぼ角ばった頭打ち）＋折り返し
        const float b = d.bias;
        float v = u + b;
        float s;
        if (d.hard > 0.001f)
        {
            const float p = 2.0f + 14.0f * d.hard;   // x / (1+|x|^p)^(1/p)
            s = v / std::pow (1.0f + std::pow (std::abs (v), p), 1.0f / p);
        }
        else s = std::tanh (v);
        if (d.fold > 0.001f) s = lerp (s, std::sin (v * 1.5707963f), d.fold);
        const float s0 = d.hard > 0.001f ? b / std::pow (1.0f + std::pow (std::abs (b), 2.0f + 14.0f * d.hard), 1.0f / (2.0f + 14.0f * d.hard)) : std::tanh (b);
        const float s0f = d.fold > 0.001f ? lerp (s0, std::sin (b * 1.5707963f), d.fold) : s0;
        return s - s0f;
    }

    struct Distort
    {
        double sr = 48000;
        std::array<mytrem::Oversampler4, maxCh> os;
        std::array<std::array<Biquad, 3>, maxCh> pre;    // 前: 低域シェルフ・高域シェルフ・低域カット
        std::array<Biquad, maxCh> post;                  // 後ろ: 高域カット
        std::array<Biquad, maxCh> dc;
        std::array<Envelope, maxCh> env;
        std::array<DelayLine, maxCh> dryDl, bandDl;      // 元の音・帯域の中の音を 4 倍の遅れ（28）にそろえる
        std::array<BandFocus, maxCh> focus;
        std::array<Biquad, maxCh> cutLo, cutHi;          // Cut モード: 変わった分だけ切る
        Flux flux;
        int curType = -1; float curAmt = -1;
        OnePole amtSm, mixSm;
        static constexpr int kLat = 28;
        static constexpr float kBase = 0.7f;   // 量 0 の音量（本物は 9 種類とも +0.7〜1.0 dB）
        static constexpr float kHead = 0.5f;   // 頭打ちの形の手前で −6 dB（量 0 でもごくわずかに色が付く = 本物の 3 次 −50 dB 前後）

        void prepare (double s)
        {
            sr = s;
            for (auto& o : os) { o.prepare(); o.reset(); }
            for (auto& d : dryDl) d.prepare (64);
            for (auto& d : bandDl) d.prepare (64);
            for (auto& e : env) e.set (3.0f, 60.0f, sr * 4);
            for (auto& d : dc) d.highpass1 (8.0, sr);
            flux.prepare (sr, 53); amtSm.setTime (20.0f, sr); mixSm.setTime (15.0f, sr); mixSm.y = 1.0f;
            curType = -1; curAmt = -1;
        }
        void reset()
        {
            for (auto& o : os) o.reset();
            for (auto& d : dryDl) d.reset();
            for (auto& d : bandDl) d.reset();
            for (auto& c : pre) for (auto& b : c) b.reset();
            for (auto& b : post) b.reset();
            for (auto& b : dc) b.reset();
            for (auto& e : env) e.y = 0;
            for (auto& b : cutLo) b.reset(); for (auto& b : cutHi) b.reset(); for (auto& f : focus) f.reset();
        }

        void setFilters (int type, float a)
        {
            const auto& d = distModels()[(size_t) type];
            for (int c = 0; c < maxCh; ++c)
            {
                pre[(size_t) c][0].lowShelf (d.lsHz, 0.7, d.lsDb * a, sr);
                pre[(size_t) c][1].highShelf (d.hsHz, 0.7, d.hsDb * a, sr);
                if (d.hpHz * a > 5.0f) pre[(size_t) c][2].highpass (std::max (5.0f, d.hpHz * a), 0.6, sr); else pre[(size_t) c][2].setIdentity();
                const float lp = d.lpHz > 0 ? d.lpHz * std::pow (2.0f, -d.lpOct * a) : 0.0f;
                if (lp > 0 && lp < sr * 0.45) post[(size_t) c].lowpass (lp, 0.6, sr); else post[(size_t) c].setIdentity();
            }
        }

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const float* envNorm)
        {
            const int type = std::max (0, std::min (kNumDistTypes - 1, k.distType));
            const auto& d = distModels()[(size_t) type];
            for (int c = 0; c < nCh; ++c)
            {
                focus[(size_t) c].set (k.distLo, k.distHi, sr);
                if (k.distCut) { cutLo[(size_t) c].highpass (std::max (10.0f, knobHz (k.distLo)), 0.7, sr); cutHi[(size_t) c].lowpass (std::min (knobHz (k.distHi), (float) sr * 0.45f), 0.7, sr); }
            }
            const bool useFocus = ! k.distCut;
            for (int i = 0; i < n; ++i)
            {
                const float a = amtSm.process (clamp01 (amt * flux.next (k.distFlux, envNorm[i], sr)));
                if (type != curType || std::abs (a - curAmt) > 0.002f) { setFilters (type, a); curType = type; curAmt = a; }
                const float driveDb = d.drive1 * a + d.drive2 * a * a + k.distOverload * d.overloadDb * a;
                const float g = dbToGain (driveDb);
                const float mixV = mixSm.process (k.distMix);
                const float wShape = std::min (1.0f, a / 0.08f);   // 量 0 では形を通さない（どの種類でも素通し＋0.7 dB）
                const float outG = dbToGain (kBase + d.out1 * a + d.out2 * a * a - k.distOverload * d.overloadDb * a * 0.6f) / std::max (1e-3f, g);
                const float thr = dbToGain (d.compThrDb);
                for (int c = 0; c < nCh; ++c)
                {
                    const float x = io[c][i];
                    const float b = useFocus ? focus[(size_t) c].band (x) : x;
                    dryDl[(size_t) c].push (x); bandDl[(size_t) c].push (b);
                    float u = b;
                    for (auto& f : pre[(size_t) c]) u = f.process (u);
                    float up[4]; os[(size_t) c].up (u * g, up);
                    for (auto& v : up)
                    {
                        // 入口の大きさで利得を下げる（本物は大きい音ほど出口が小さくなる）
                        const float e = env[(size_t) c].process (std::abs (v));
                        float gr = 1.0f;
                        if (e > thr) gr = std::pow (thr / e, d.compRatio * a);
                        const float sh = distShape (v * gr * kHead, d) * d.shapeOut / kHead;
                        v = wShape >= 1.0f ? sh : lerp (v, sh, wShape);
                    }
                    float y = os[(size_t) c].down (up);
                    y = post[(size_t) c].process (y);
                    y = dc[(size_t) c].process (y) * outG;
                    const float xd = dryDl[(size_t) c].tap (kLat);
                    if (useFocus)
                    {
                        // Focus: 帯域の中だけ歪ませる（帯域の外は元のまま、遅れだけそろえる）
                        const float bd = bandDl[(size_t) c].tap (kLat);
                        io[c][i] = xd + mixV * (y - bd);
                    }
                    else
                    {
                        const float diff = y - xd;
                        io[c][i] = xd + mixV * cutHi[(size_t) c].process (cutLo[(size_t) c].process (diff));
                    }
                }
            }
        }
    };

    //==============================================================================
    // DIGITAL: サンプルレートを下げる（RATE）とビット数を下げる（BITS）。RATE<>BITS で配分
    struct Digital
    {
        double sr = 48000;
        std::array<float, maxCh> held { 0, 0 }; double ph = 0; Rng rng { 5 };
        std::array<Biquad, maxCh> smooth, aa;
        std::array<BandFocus, maxCh> focus;
        std::array<Biquad, maxCh> cutLo, cutHi;
        Flux flux; OnePole amtSm; float jitter = 0; int sputCnt = 0; float sputBits = 0;
        float lastFs = -1, lastSmooth = -1;

        void prepare (double s) { sr = s; flux.prepare (sr, 61); amtSm.setTime (20.0f, sr); lastFs = -1; }
        void reset() { held = { 0, 0 }; ph = 0; for (auto& b : smooth) b.reset(); for (auto& b : aa) b.reset(); for (auto& b : cutLo) b.reset(); for (auto& b : cutHi) b.reset(); for (auto& f : focus) f.reset(); }

        // 量 → 新しいサンプルレート（48 kHz で測った値。ほかのレートでも同じ Hz）
        static float rateHz (float a)
        {
            static const std::array<float, 8> xs { 0, 0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 0.85f, 1.0f };
            static const std::array<float, 8> ys { 15.6f, 15.1f, 14.37f, 13.67f, 12.64f, 11.79f, 11.15f, 10.46f };   // log2(Hz)。0 % は 48 kHz より上 = 下げない
            return std::pow (2.0f, interp (xs, ys, a));
        }
        // 量 → 段の数（μ-law で圧縮してから等間隔に丸める）
        static float bitLevels (float a)
        {
            static const std::array<float, 7> xs { 0, 0.2f, 0.3f, 0.5f, 0.7f, 0.85f, 1.0f };
            static const std::array<float, 7> ys { 16.0f, 12.0f, 7.5f, 4.6f, 3.4f, 2.4f, 1.3f };   // ビット
            return std::pow (2.0f, interp (xs, ys, a));
        }
        static inline float muEnc (float x) { const float mu = 255.0f; return (x < 0 ? -1.0f : 1.0f) * std::log1p (mu * std::min (1.0f, std::abs (x))) / std::log1p (mu); }
        static inline float muDec (float y) { const float mu = 255.0f; return (y < 0 ? -1.0f : 1.0f) * (std::pow (1.0f + mu, std::abs (y)) - 1.0f) / mu; }

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const float* envNorm)
        {
            for (int c = 0; c < nCh; ++c)
            {
                focus[(size_t) c].set (k.digLo, k.digHi, sr);
                if (k.digCut) { cutLo[(size_t) c].highpass (std::max (10.0f, knobHz (k.digLo)), 0.7, sr); cutHi[(size_t) c].lowpass (std::min (knobHz (k.digHi), (float) sr * 0.45f), 0.7, sr); }
            }
            const float rateShare = std::min (1.0f, 2.0f * (1.0f - k.digRateBits)), bitShare = std::min (1.0f, 2.0f * k.digRateBits);
            const float driveG = dbToGain (k.digDrive * 24.0f);
            for (int i = 0; i < n; ++i)
            {
                const float a = amtSm.process (clamp01 (amt * flux.next (k.digFlux, envNorm[i], sr)));
                // 量 0〜10 % は今のサンプルレートから表の 10 % の値へ（対数で）。0 % ならどのレートでも下げない
                const float ar = a * rateShare;
                const float fsNew = ar >= 0.1f ? std::min ((float) sr, rateHz (ar))
                                               : std::min ((float) sr, std::exp (lerp (std::log ((float) sr * 1.02f), std::log (rateHz (0.1f)), ar / 0.1f)));
                if (std::abs (fsNew - lastFs) > 1.0f || std::abs (k.digSmooth - lastSmooth) > 0.002f)
                {
                    lastFs = fsNew; lastSmooth = k.digSmooth;
                    // SMOOTH: 段の角を丸める（新しいサンプルレートの半分より下で切る）
                    const float sm = k.digSmooth;
                    for (int c = 0; c < maxCh; ++c)
                    {
                        if (sm > 0.01f && fsNew < sr * 0.98) smooth[(size_t) c].lowpass (std::min ((float) sr * 0.45f, fsNew * (0.5f + 2.5f * (1.0f - sm))), 0.6, sr);
                        else smooth[(size_t) c].setIdentity();
                    }
                }
                // SPUTTER: ビット数と段の間隔がときどきふらつく
                if (--sputCnt <= 0) { sputCnt = (int) (sr * (0.02 + 0.12 * rng.uni())); sputBits = k.digSputter * (rng.uni() * 2.5f) * (a > 0.01f ? 1.0f : 0.0f); }
                float bits = std::log2 (bitLevels (a * bitShare)) - sputBits * bitShare;
                bits = std::max (0.6f, bits);
                const float levels = std::pow (2.0f, bits);
                // 段の時間（少しふらつく = 本物も単純な割り算ではない）
                ph += fsNew / sr * (1.0 + 0.04 * k.digSputter * rng.bi());
                const bool take = ph >= 1.0 || fsNew >= sr * 0.999f;
                if (take) ph -= std::floor (ph);
                for (int c = 0; c < nCh; ++c)
                {
                    const float x = io[c][i];
                    const float b = k.digCut ? x : focus[(size_t) c].band (x);
                    if (take)
                    {
                        float v = b * driveG;
                        if (driveG > 1.01f) v = std::tanh (v) / std::tanh (driveG) * 1.0f;
                        if (levels < 60000.0f)
                        {
                            const float q = 1.0f / (levels * 0.5f);
                            v = muDec (std::round (muEnc (v) / q) * q);
                        }
                        held[(size_t) c] = driveG > 1.01f ? v * std::tanh (driveG) / driveG : v;
                    }
                    const float y = smooth[(size_t) c].process (held[(size_t) c]);
                    if (k.digCut)
                    {
                        const float diff = y - x;
                        io[c][i] = x + k.digMix * cutHi[(size_t) c].process (cutLo[(size_t) c].process (diff));
                    }
                    else io[c][i] = x + k.digMix * (y - b);
                }
            }
        }
    };

    //==============================================================================
    // SPACE: 8 本の遅れの輪（FDN）の残響。10 種類は測った長さ・最初の遅れ・音色・左右の似かた・量に合わせた
    struct SpaceType
    {
        const char* name;
        float preMs, t60, hpHz, lpHz, dampHz, size, diffuse, lrCorr, energyDb, spring;
    };
    inline const std::array<SpaceType, 10>& spaceTypes()
    {
        static const std::array<SpaceType, 10> t {{
            { "Wide Hall",     18.0f, 1.80f, 300.0f, 14000.0f, 6000.0f, 1.00f, 0.75f, 0.00f, -0.4f, 0 },
            { "Small Room",     3.5f, 0.24f,  80.0f, 16000.0f, 9000.0f, 0.30f, 0.70f, 0.00f, -5.7f, 0 },
            { "Booth",          2.3f, 0.60f, 120.0f, 13000.0f, 7000.0f, 0.40f, 0.70f, 0.00f, -7.5f, 0 },
            { "Slap Room",     23.0f, 0.46f, 100.0f,  9000.0f, 5000.0f, 0.45f, 0.55f, 0.00f, -7.5f, 0 },
            { "Dark Plate",     1.7f, 1.90f,  60.0f,  8500.0f, 4000.0f, 0.70f, 0.85f, 0.00f, -7.5f, 0 },
            { "Warm Room",      1.6f, 1.40f,  60.0f,  5000.0f, 2500.0f, 0.60f, 0.80f, 0.00f, -8.7f, 0 },
            { "Echo Chamber",  31.0f, 0.68f, 200.0f, 12000.0f, 5000.0f, 0.80f, 0.50f, 0.24f, -7.6f, 0 },
            { "Wood Room",      4.1f, 0.62f, 150.0f, 14000.0f, 6000.0f, 0.42f, 0.70f, 0.00f, -6.7f, 0 },
            { "Old Spring",     0.0f, 1.06f, 100.0f,  5000.0f, 3500.0f, 0.55f, 0.60f, 0.47f, -7.4f, 1 },
            { "Radio Spring",   0.0f, 1.70f, 260.0f,  2400.0f, 2000.0f, 0.65f, 0.60f, 0.34f, -7.3f, 1 },
        }};
        return t;
    }
    constexpr int kNumSpaceTypes = 10;

    struct Space
    {
        double sr = 48000;
        static constexpr int N = 8;
        std::array<DelayLine, N> line; std::array<float, N> len {}; std::array<OnePole, N> damp; std::array<float, N> fbG {};
        std::array<Biquad, N> dampLp;
        DelayLine preL, preR;
        std::array<std::array<float, 4>, maxCh> apState {};
        std::array<DelayLine, 8> ap; std::array<int, 8> apLen {};
        std::array<Biquad, maxCh> hp, lp, loC, hiC;
        std::array<std::array<DelayLine, 6>, maxCh> spr; // ばねの「ビヨン」（全通過を何段も）
        Flux flux; Envelope duckEnv; OnePole amtSm;
        int curType = -1; float curDecay = -1, sDecay = -1; float typeGain = 1;
        Rng rng { 9 };
        double lfoPh = 0;

        void prepare (double s)
        {
            sr = s;
            for (auto& l : line) l.prepare ((int) (0.25 * sr));
            preL.prepare ((int) (0.35 * sr)); preR.prepare ((int) (0.35 * sr));
            for (auto& a : ap) a.prepare ((int) (0.02 * sr));
            for (auto& c : spr) for (auto& a : c) a.prepare ((int) (0.01 * sr));
            flux.prepare (sr, 71); duckEnv.set (5.0f, 250.0f, sr); amtSm.setTime (20.0f, sr);
            curType = -1;
        }
        void reset()
        {
            for (auto& l : line) l.reset(); preL.reset(); preR.reset(); for (auto& a : ap) a.reset();
            for (auto& c : spr) for (auto& a : c) a.reset();
            for (auto& d : damp) d.y = 0; for (auto& b : dampLp) b.reset();
            for (auto& b : hp) b.reset(); for (auto& b : lp) b.reset(); for (auto& b : loC) b.reset(); for (auto& b : hiC) b.reset();
            sDecay = -1;
        }

        // DECAY だけ変える（遅れの長さは変えない = 回してもプチッとしない。フィルターの中身も残す）
        void setDecay (float decayKnob)
        {
            curDecay = decayKnob;
            const auto& st = spaceTypes()[(size_t) curType];
            // 長さは本物の DECAY（初期値 80 %）で測った値。DECAY で 0.25〜1.6 倍
            const float t60 = st.t60 * std::pow (2.0f, (decayKnob - 0.8f) * 3.0f);
            for (int i = 0; i < N; ++i)
            {
                const float z1 = dampLp[(size_t) i].z1, z2 = dampLp[(size_t) i].z2;
                fbG[(size_t) i] = std::pow (10.0f, -3.0f * len[(size_t) i] / (float) sr / std::max (0.05f, t60));
                // 高い音ほど速く消える（dampHz より上は 2.5 倍速く）
                const float gHi = std::pow (10.0f, -3.0f * len[(size_t) i] / (float) sr / std::max (0.03f, t60 / 2.5f));
                dampLp[(size_t) i].highShelf1 (st.dampHz, gainToDb (gHi / fbG[(size_t) i]), sr);
                dampLp[(size_t) i].z1 = z1; dampLp[(size_t) i].z2 = z2;
            }
        }

        // 種類を決める（遅れの長さが変わるので、切り替えは Engine が 2 つの Space を混ぜて行う）
        void setType (int t, float decayKnob, float hiKnob, float loKnob)
        {
            curType = t;
            const auto& st = spaceTypes()[(size_t) t];
            static const float base[N] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0533f, 0.0617f, 0.0683f, 0.0797f };
            for (int i = 0; i < N; ++i) len[(size_t) i] = (float) (base[i] * (0.35 + 0.9 * st.size) * sr);
            setDecay (decayKnob); sDecay = decayKnob;
            static const float apMs[8] = { 4.77f, 3.59f, 2.73f, 1.83f, 5.11f, 3.97f, 2.31f, 1.57f };
            for (int i = 0; i < 8; ++i) apLen[(size_t) i] = (int) (apMs[i] * 0.001 * sr * (0.5 + st.size));
            for (int c = 0; c < maxCh; ++c)
            {
                hp[(size_t) c].highpass (st.hpHz, 0.7, sr);
                lp[(size_t) c].lowpass (std::min (st.lpHz, (float) sr * 0.45f), st.spring > 0 ? 1.1 : 0.7, sr);
                loC[(size_t) c].highpass (std::max (10.0f, knobHz (loKnob)), 0.7, sr);
                hiC[(size_t) c].lowpass (std::min (knobHz (hiKnob), (float) sr * 0.45f), 0.7, sr);
            }
            typeGain = dbToGain (st.energyDb) * spaceGainTrim()[(size_t) t];
        }
        static const std::array<float, 10>& spaceGainTrim()
        {
            // 量合わせ（tools/render で IR のエネルギーを測って、本物の量にそろえた倍率）
            static std::array<float, 10> g { RR_SPACE_TRIM };   // const にしない（量合わせの道具が書き換える）
            return g;
        }

        // 1 サンプル: in = 入力（左右）、out に足すのはウェットだけ
        inline void tick (float inL, float inR, float& wl, float& wr, const SpaceType& st, float preMs)
        {
            preL.push (inL); preR.push (inR);
            const float pd = (float) ((st.preMs + preMs) * 0.001 * sr);
            float a = preL.read (pd), b = preR.read (pd);
            // 入口の拡散（全通過 4 段 × 左右）
            for (int j = 0; j < 4; ++j)
            {
                auto& d1 = ap[(size_t) j]; auto& d2 = ap[(size_t) (j + 4)];
                const float g = 0.6f * st.diffuse;
                const float da = d1.tap (apLen[(size_t) j]), db = d2.tap (apLen[(size_t) (j + 4)]);
                const float va = a + g * da, vb = b + g * db;
                d1.push (va); d2.push (vb);
                a = da - g * va; b = db - g * vb;
            }
            // FDN（アダマール 8×8）
            std::array<float, N> o {};
            lfoPh += 0.37 / sr; if (lfoPh > 1) lfoPh -= 1;
            for (int i = 0; i < N; ++i)
            {
                const float mod = (float) (std::sin (2 * kPi * (lfoPh + i * 0.125)) * 0.0004 * sr * st.size);
                o[(size_t) i] = dampLp[(size_t) i].process (line[(size_t) i].read (len[(size_t) i] + mod)) * fbG[(size_t) i];
            }
            std::array<float, N> h = o;
            for (int s = 1; s < N; s <<= 1)
                for (int i = 0; i < N; i += 2 * s)
                    for (int j = i; j < i + s; ++j) { const float u = h[(size_t) j], v = h[(size_t) (j + s)]; h[(size_t) j] = u + v; h[(size_t) (j + s)] = u - v; }
            const float sc = 1.0f / std::sqrt ((float) N);
            for (int i = 0; i < N; ++i) line[(size_t) i].push (h[(size_t) i] * sc + (i % 2 == 0 ? a : b) * 0.5f);
            // 拡散した入口の音もすぐ出す（最初の反射。これがないと響きの立ち上がりが 20 ms 遅れる）
            wl = (o[0] + o[2] - o[4] + o[6]) * 0.5f + a * st.diffuse * 0.6f;
            wr = (o[1] - o[3] + o[5] + o[7]) * 0.5f + b * st.diffuse * 0.6f;
            if (st.lrCorr > 0) { const float m = 0.5f * (wl + wr); wl = lerp (wl, m, st.lrCorr); wr = lerp (wr, m, st.lrCorr); }
        }

        void process (float** io, int nCh, int n, const Knobs& k, float amt, const float* envNorm, const float* inEnvDb)
        {
            const int t = std::max (0, std::min (kNumSpaceTypes - 1, k.spaceType));
            if (t != curType) setType (t, k.spaceDecay, k.spaceHi, k.spaceLo);
            if (sDecay < 0) { sDecay = k.spaceDecay; setDecay (sDecay); }
            for (int c = 0; c < maxCh; ++c)
            {
                loC[(size_t) c].highpass (std::max (10.0f, knobHz (k.spaceLo)), 0.7, sr);
                hiC[(size_t) c].lowpass (std::min (knobHz (k.spaceHi), (float) sr * 0.45f), 0.7, sr);
            }
            const auto& st = spaceTypes()[(size_t) t];
            const float panL = std::min (1.0f, 2.0f * (1.0f - k.spacePan)), panR = std::min (1.0f, 2.0f * k.spacePan);
            const float decA = (float) std::exp (-32.0 / (0.06 * sr));
            for (int i = 0; i < n; ++i)
            {
                // DECAY は 32 サンプルごとに少しずつ近づける
                if ((i & 31) == 0 && std::abs (k.spaceDecay - sDecay) > 1e-4f)
                {
                    sDecay = k.spaceDecay + (sDecay - k.spaceDecay) * decA;
                    if (std::abs (sDecay - k.spaceDecay) < 2e-4f) sDecay = k.spaceDecay;
                    setDecay (sDecay);
                }
                const float a = amtSm.process (clamp01 (amt * flux.next (k.spaceFlux, envNorm[i], sr)));
                const float xl = io[0][i], xr = nCh > 1 ? io[1][i] : xl;
                float wl, wr;
                tick (hp[0].process (xl), hp[1].process (xr), wl, wr, st, k.spacePre * 250.0f);
                wl = lp[0].process (wl); wr = lp[1].process (wr);
                if (st.spring > 0)
                {
                    // ばね: 全通過を何段か通して「ビヨン」とした色を付ける
                    for (int c = 0; c < 2; ++c)
                    {
                        float& w = c == 0 ? wl : wr;
                        for (int j = 0; j < 6; ++j)
                        {
                            auto& d = spr[(size_t) c][(size_t) j]; const int L = (int) ((23 + j * 7 + c * 3) * sr / 48000.0); const float g = 0.55f;   // 48 kHz で決めた長さ
                            const float dd = d.tap (L), v = w + g * dd; d.push (v); w = dd - g * v;
                        }
                    }
                }
                wl = hiC[0].process (loC[0].process (wl)); wr = hiC[1].process (loC[1].process (wr));
                // 幅（M/S）とパン
                const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * k.spaceWidth;
                wl = (m + s) * panL; wr = (m - s) * panR;
                // DUCK: 入力が鳴っている間は響きを下げる
                const float de = duckEnv.process (dbToGain (inEnvDb[i]));
                const float duckG = 1.0f - k.spaceDuck * std::min (1.0f, de * 4.0f) * 0.9f;
                // 量: ウェットは 50 % で最大、そこから上は元の音が減る（測った形）
                const float wet = std::min (1.0f, 2.0f * a) * typeGain * duckG, dry = std::min (1.0f, 2.0f * (1.0f - a));
                io[0][i] = xl * dry + wl * wet;
                if (nCh > 1) io[1][i] = xr * dry + wr * wet;
            }
        }
    };
}
