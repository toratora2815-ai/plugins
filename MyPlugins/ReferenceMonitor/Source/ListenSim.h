#pragma once
// LISTEN AS: スマホ・ノートPC・イヤホン・Bluetooth スピーカー・車・クラブの「聞こえ方」をまねる（JUCE 非依存）。
// 自分の曲にも参照曲にも同じようにかかるので、「スマホで聴いても参照曲くらい低音が感じられるか」を確かめられる。
// どの環境も、ピンクノイズで測った音量（LUFS）が素の状態と同じになるように自動で補正する（大きく聞こえる方が良く聞こえる錯覚を防ぐ）。
#include "LoudnessMeter.h"
#include <cstdint>

namespace refdsp
{

// RBJ のクックブック式
inline Biquad designBiquad (int type, double fs, double f, double q, double gainDb) noexcept
{
    enum { lp, hp, peak, lowShelf, highShelf };
    Biquad b;
    f = std::min (f, fs * 0.45);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
    const double alpha = sw / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case lp:   b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case hp:   b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case peak: b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case lowShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;       a1 = -2 * ((A - 1) + (A + 1) * cw);    a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case highShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;       a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        default: break;
    }
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
    return b;
}

struct SimPreset
{
    const char* name;
    const char* hint;       // 何を聴けばいいか（画面に出す）
    float hpHz; int hpStages;
    float lpHz;
    float lsHz, lsDb;
    float pk1Hz, pk1Db, pk1Q;
    float pk2Hz, pk2Db, pk2Q;
    float hsHz, hsDb;
    float width;            // 1 = そのまま、0 = モノ
    float balanceDb;        // + = 右が大きい
    bool  limiter;          // 小さいスピーカーの保護リミッター
    float noiseDb;          // ロードノイズ（dBFS RMS）。-200 = なし
};

class ListenSim
{
public:
    static constexpr int kNumPresets = 7;

    static const SimPreset& preset (int i) noexcept
    {
        static const SimPreset p[kNumPresets] = {
            { "OFF", "",
              0, 0, 0,  0, 0,  0, 0, 1,  0, 0, 1,  0, 0,  1.0f, 0, false, -200 },
            { "PHONE", "PHONE: no bass below ~300 Hz. Can you still feel the kick and bass (their upper harmonics)? Is the vocal clear and in front?",
              330, 2, 13000,  0, 0,  900, 3, 1.0f,  3500, 4, 1.5f,  0, 0,  0.2f, 0, true, -200 },
            { "LAPTOP", "LAPTOP: thin low end, boxy mids. Does the mix still sound full? Are the vocal and snare balanced?",
              180, 2, 16000,  0, 0,  900, 2, 1.0f,  4000, 3, 1.2f,  10000, -3,  0.35f, 0, true, -200 },
            { "EARBUDS", "EARBUDS: loose fit, so the sub drops off and the 5-7 kHz area gets edgy. Check sibilance (s / t) and harsh cymbals.",
              60, 1, 0,  120, 2,  2500, -2, 1.0f,  6000, 4, 2.0f,  12000, -3,  1.0f, 0, false, -200 },
            { "BT SPEAKER", "BT SPEAKER: mono, with a boomy port around 110 Hz. Does the bass get muddy? Does anything disappear in mono?",
              70, 2, 15000,  0, 0,  110, 5, 1.2f,  2500, 2, 1.0f,  0, 0,  0.0f, 0, true, -200 },
            { "CAR", "CAR: big cabin bass, road noise, and you sit close to the right speaker. Is the vocal still clear over the noise? Is the low end too heavy?",
              30, 1, 0,  70, 7,  250, -3, 1.0f,  2500, -2, 1.0f,  8000, -3,  0.6f, 2.0f, false, -44 },
            { "CLUB", "CLUB: mono, huge sub, soft top. Do the kick and bass hit together without fighting? Does the groove survive in mono?",
              30, 2, 0,  60, 6,  250, -2, 1.0f,  0, 0, 1,  9000, -3,  0.0f, 0, false, -200 },
        };
        return p[juce_clamp (i)];
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // 各環境の音量補正をピンクノイズで測る
        for (int i = 0; i < kNumPresets; ++i)
            makeupDb[(size_t) i] = i == 0 ? 0.0f : calibrate (i);
        // ロードノイズ（ブラウンノイズ）
        noiseA = std::exp (-2.0 * kPi * 150.0 / fs);
        current = target.load();
        load (current);
        fade = 1.0f;
    }

    std::atomic<int> target { 0 };
    float getMakeupDb (int i) const noexcept { return makeupDb[(size_t) juce_clamp (i)]; }
    int getCurrent() const noexcept { return current; }

    inline void process (float& l, float& r) noexcept
    {
        // 切り替えは 15ms で消して、入れ替えて、15ms で戻す
        const int t = target.load (std::memory_order_relaxed);
        if (t != current)
        {
            fade -= fadeStep();
            if (fade <= 0.0f) { fade = 0.0f; current = juce_clamp (t); load (current); }
        }
        else if (fade < 1.0f)
            fade = std::min (1.0f, fade + fadeStep());

        if (current == 0) { l *= fade; r *= fade; return; }
        const SimPreset& p = preset (current);

        double L = l, R = r;
        for (int s = 0; s < nStages; ++s) { L = st[0][(size_t) s].process (L); R = st[1][(size_t) s].process (R); }

        // ステレオ幅とバランス
        const double mid = 0.5 * (L + R), side = 0.5 * (L - R) * p.width;
        L = (mid + side) * balL;
        R = (mid - side) * balR;

        if (p.limiter)
        {
            // 小さいスピーカーのリミッター（-14dBFS から 4:1、アタック 3ms・リリース 150ms）
            const double lvl = std::max (std::abs (L), std::abs (R));
            env = lvl > env ? env + (lvl - env) * atk : env + (lvl - env) * rel;
            const double envDb = 20.0 * std::log10 (env + 1.0e-9);
            const double over = envDb - (-14.0);
            if (over > 0.0)
            {
                const double g = std::pow (10.0, -over * 0.75 / 20.0);
                L *= g; R *= g;
            }
        }

        const double mk = makeupGain;
        L *= mk; R *= mk;

        if (noiseGain > 0.0)
        {
            // 左右で別のノイズ（広がりのある走行音）
            for (int ch = 0; ch < 2; ++ch)
            {
                rng = rng * 1664525u + 1013904223u;
                const double w = ((double) (rng >> 8) / 8388608.0) - 1.0;
                nz1[ch] = w + (nz1[ch] - w) * noiseA;
                nz2[ch] = nz1[ch] + (nz2[ch] - nz1[ch]) * noiseA;
            }
            L += nz2[0] * noiseGain;
            R += nz2[1] * noiseGain;
        }

        l = (float) L * fade;
        r = (float) R * fade;
    }

private:
    static int juce_clamp (int i) noexcept { return i < 0 ? 0 : (i >= kNumPresets ? kNumPresets - 1 : i); }
    float fadeStep() const noexcept { return (float) (1.0 / (fs * 0.015)); }

    // EQ の段を組み立てる（音量補正とノイズは別）
    static int buildStages (const SimPreset& p, double fs, std::array<Biquad, 8>& out)
    {
        enum { lp, hp, peak, lowShelf, highShelf };
        int n = 0;
        for (int i = 0; i < p.hpStages; ++i) out[(size_t) n++] = designBiquad (hp, fs, p.hpHz, 0.7071, 0);
        if (p.lpHz > 0)  out[(size_t) n++] = designBiquad (lp, fs, p.lpHz, 0.7071, 0);
        if (p.lsDb != 0) out[(size_t) n++] = designBiquad (lowShelf, fs, p.lsHz, 0.7071, p.lsDb);
        if (p.pk1Db != 0) out[(size_t) n++] = designBiquad (peak, fs, p.pk1Hz, p.pk1Q, p.pk1Db);
        if (p.pk2Db != 0) out[(size_t) n++] = designBiquad (peak, fs, p.pk2Hz, p.pk2Q, p.pk2Db);
        if (p.hsDb != 0) out[(size_t) n++] = designBiquad (highShelf, fs, p.hsHz, 0.7071, p.hsDb);
        return n;
    }

    void load (int i)
    {
        const SimPreset& p = preset (i);
        std::array<Biquad, 8> stages {};
        nStages = buildStages (p, fs, stages);
        st[0] = stages; st[1] = stages;
        const double b = std::pow (10.0, p.balanceDb / 40.0);
        balL = 1.0 / b; balR = b;
        makeupGain = std::pow (10.0, makeupDb[(size_t) i] / 20.0);
        env = 0.0;
        atk = 1.0 - std::exp (-1.0 / (fs * 0.003));
        rel = 1.0 - std::exp (-1.0 / (fs * 0.150));
        // ブラウンノイズ（白色雑音を 150Hz で2回丸めたもの）の RMS を測って、指定の dBFS に合わせる
        noiseGain = p.noiseDb > -100.0f ? std::pow (10.0, p.noiseDb / 20.0) / noiseRms() : 0.0;
        nz1[0] = nz1[1] = nz2[0] = nz2[1] = 0.0;
    }

    double noiseRms() const
    {
        const double a = std::exp (-2.0 * kPi * 150.0 / fs);
        uint32_t s = 12345u;
        double z1 = 0, z2 = 0, acc = 0;
        const int n = (int) (fs * 2.0);
        for (int i = 0; i < n; ++i)
        {
            s = s * 1664525u + 1013904223u;
            const double w = ((double) (s >> 8) / 8388608.0) - 1.0;
            z1 = w + (z1 - w) * a;
            z2 = z1 + (z2 - z1) * a;
            if (i > n / 4) acc += z2 * z2;
        }
        return std::sqrt (acc / (n - n / 4 - 1)) + 1.0e-12;
    }

    // ピンクノイズ（真ん中寄りのステレオ）を通して、素の状態との LUFS の差を測る
    float calibrate (int i) const
    {
        const SimPreset& p = preset (i);
        std::array<Biquad, 8> stL {}, stR {};
        const int n = buildStages (p, fs, stL);
        stR = stL;
        const double b = std::pow (10.0, p.balanceDb / 40.0);

        LoudnessMeter inM, outM;
        inM.prepare (fs); outM.prepare (fs);
        const int len = (int) (fs * 4.0), blk = 512;
        std::vector<float> iL ((size_t) blk), iR ((size_t) blk), oL ((size_t) blk), oR ((size_t) blk);
        uint32_t s = 777u;
        double pk[3][7] = {};   // Paul Kellet のピンクノイズ（3系統: 共通・左・右）
        auto pink = [&] (int k)
        {
            s = s * 1664525u + 1013904223u;
            const double w = (((double) (s >> 8) / 8388608.0) - 1.0) * 0.1;
            double* c = pk[k];
            c[0] = 0.99886 * c[0] + w * 0.0555179; c[1] = 0.99332 * c[1] + w * 0.0750759;
            c[2] = 0.96900 * c[2] + w * 0.1538520; c[3] = 0.86650 * c[3] + w * 0.3104856;
            c[4] = 0.55000 * c[4] + w * 0.5329522; c[5] = -0.7616 * c[5] - w * 0.0168980;
            const double out = c[0] + c[1] + c[2] + c[3] + c[4] + c[5] + c[6] + w * 0.5362;
            c[6] = w * 0.115926;
            return out;
        };
        for (int pos = 0; pos < len; pos += blk)
        {
            for (int j = 0; j < blk; ++j)
            {
                const double m = pink (0), a = pink (1) * 0.4, c = pink (2) * 0.4;
                double L = m + a, R = m + c;
                iL[(size_t) j] = (float) L; iR[(size_t) j] = (float) R;
                for (int k = 0; k < n; ++k) { L = stL[(size_t) k].process (L); R = stR[(size_t) k].process (R); }
                const double mid = 0.5 * (L + R), side = 0.5 * (L - R) * p.width;
                oL[(size_t) j] = (float) ((mid + side) / b);
                oR[(size_t) j] = (float) ((mid - side) * b);
            }
            inM.process (iL.data(), iR.data(), blk);
            outM.process (oL.data(), oR.data(), blk);
        }
        const float d = inM.integrated.load() - outM.integrated.load();
        return std::isfinite (d) ? std::max (-24.0f, std::min (24.0f, d)) : 0.0f;
    }

    double fs = 48000.0;
    int current = 0, nStages = 0;
    float fade = 1.0f;
    std::array<float, kNumPresets> makeupDb {};
    std::array<std::array<Biquad, 8>, 2> st {};
    double balL = 1, balR = 1, makeupGain = 1, env = 0, atk = 0, rel = 0;
    double noiseGain = 0, noiseA = 0, nz1[2] {}, nz2[2] {};
    uint32_t rng = 987654321u;
};

} // namespace refdsp
