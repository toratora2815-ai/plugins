#pragma once
// Sample Sculptor の「1 打ずつの形」の処理（元は Hit Sculptor）（JUCE なし。tools/test_engine.cpp で単体テストできる）
//
// 1 打ごとに「鳴り始め」を見つけて、
//   ATTACK … 立ち上がり（最初の数 ms）を強く／弱く
//   LENGTH … 左: 時間で短く切る（T60 = 2.5 s → 30 ms）、右: 減っていく尾を持ち上げて長く
//   ROOM   … 左: 打った瞬間より静かな所（部屋の響き）を下げてカラカラに、右: 小さな部屋の響きを足す
// を行う。DRIVE（4 倍オーバーサンプリング）と OUTPUT はプラグイン側。
#include <cmath>
#include <algorithm>
#include <atomic>
#include <array>
#include <vector>

namespace hs
{
    struct Settings
    {
        float attack = 0.0f;   // -1..1
        float snapMs = 15.0f;  // 1..50  立ち上がりとみなす長さ
        float length = 0.0f;   // -1..1
        float room   = 0.0f;   // -1..1
        float size   = 0.4f;   // 0..1
        float tone   = 0.5f;   // 0..1
        float mix    = 1.0f;   // 0..1
    };

    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    inline float ampDb (float a)     { return 20.0f * std::log10 (std::max (a, 1.0e-9f)); }
    inline float powDb (float p)     { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
    inline float coefMs (double ms, double fs) { return (float) std::exp (-1.0 / (std::max (0.001, ms) * 0.001 * fs)); }

    // 小さな部屋（8 本のディレイのフィードバック網）
    class Room
    {
    public:
        static constexpr int N = 8;

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            size_t need = (size_t) (fs * 0.12) + 8;
            len = 1;
            while (len < need) len <<= 1;
            mask = len - 1;
            for (auto& b : buf) b.assign (len, 0.0f);
            preL.assign (len, 0.0f);
            preR.assign (len, 0.0f);
            w = 0;
            lp.fill (0.0f);
            hpL1 = hpL2 = hpR1 = hpR2 = 0.0f;
            hpA = 1.0f - std::exp (-2.0f * 3.14159265f * 120.0f / (float) fs);
            set (0.4f, 0.5f);
            for (int i = 0; i < N; ++i) cur[i] = tgt[i];
            curPre = tgtPre;
        }

        void set (float size, float tone)
        {
            static constexpr float baseMs[N] = { 5.3f, 7.9f, 10.1f, 12.7f, 15.1f, 18.3f, 21.7f, 25.9f };
            size = std::clamp (size, 0.0f, 1.0f);
            const float scale = 0.35f + 1.65f * size;
            const float t60 = 0.12f + 1.8f * std::pow (size, 1.3f);
            for (int i = 0; i < N; ++i)
            {
                tgt[i] = (float) (baseMs[i] * scale * 0.001 * fs);
                fb[i] = std::pow (10.0f, -3.0f * tgt[i] / (t60 * (float) fs));
            }
            tgtPre = (float) ((1.0 + 10.0 * size) * 0.001 * fs);
            const float fc = 1200.0f * std::pow (12.0f, std::clamp (tone, 0.0f, 1.0f));
            dampA = 1.0f - std::exp (-2.0f * 3.14159265f * fc / (float) fs);
        }

        // 今の設定へすぐ合わせる（書き出しの頭で、大きさが動かないように）
        void snap() { for (int i = 0; i < N; ++i) cur[i] = tgt[i]; curPre = tgtPre; }

        void process (float inL, float inR, float& outL, float& outR)
        {
            // 低音は響かせない（キックの響きがこもらないように）
            hpL1 += hpA * (inL - hpL1); float l = inL - hpL1; hpL2 += hpA * (l - hpL2); l -= hpL2;
            hpR1 += hpA * (inR - hpR1); float r = inR - hpR1; hpR2 += hpA * (r - hpR2); r -= hpR2;

            preL[w] = l; preR[w] = r;
            curPre += (tgtPre - curPre) * 0.0005f;
            const size_t pd = (size_t) curPre;
            l = preL[(w - pd) & mask];
            r = preR[(w - pd) & mask];

            float v[N];
            for (int i = 0; i < N; ++i)
            {
                cur[i] += (tgt[i] - cur[i]) * 0.0005f;
                const float pos = (float) w - cur[i] + (float) len;
                const size_t i0 = (size_t) pos;
                const float fr = pos - (float) i0;
                const float a = buf[i][i0 & mask], b = buf[i][(i0 + 1) & mask];
                float x = a + (b - a) * fr;
                lp[i] += dampA * (x - lp[i]);
                v[i] = lp[i];
            }
            outL = 0.5f * (v[0] + v[2] - v[4] - v[6]);
            outR = 0.5f * (v[1] + v[3] - v[5] - v[7]);

            for (int i = 0; i < N; ++i) v[i] *= fb[i];
            for (int h = 1; h < N; h <<= 1)
                for (int i = 0; i < N; i += h * 2)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = v[j], b = v[j + h];
                        v[j] = a + b; v[j + h] = a - b;
                    }
            const float norm = 0.35355339f;   // 1/sqrt(8)
            for (int i = 0; i < N; ++i)
            {
                const float in = (i & 1) ? r : l;
                buf[i][w] = v[i] * norm + in * ((i & 2) ? -0.5f : 0.5f);
            }
            w = (w + 1) & mask;
        }

    private:
        double fs = 48000.0;
        size_t len = 1, mask = 0, w = 0;
        std::array<std::vector<float>, N> buf;
        std::vector<float> preL, preR;
        float cur[N] {}, tgt[N] {}, fb[N] {};
        float curPre = 0, tgtPre = 0;
        std::array<float, N> lp {};
        float dampA = 0.5f, hpA = 0.01f, hpL1 = 0, hpL2 = 0, hpR1 = 0, hpR2 = 0;
    };

    class Engine
    {
    public:
        static constexpr int dispLen = 1000;   // 画面の波形（1 打の最初の約 1 秒）

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            room.prepare (fs);
            eF = eS = eD = 0.0f;
            holdLeft = 0;
            rms = 0.0f;
            sinceHit = 1 << 30;
            hitPeakR = 1.0e-12f;
            gA = gS = 1.0f;
            decim = std::max (1, (int) std::lround (fs * 1.0 / dispLen));
            relF = coefMs (100.0, fs);
            attD = coefMs (10.0, fs);
            rmsA = 1.0f - coefMs (10.0, fs);
            upA = 1.0f - coefMs (0.25, fs);
            downA = 1.0f - coefMs (4.0, fs);
            attSm = 1.0f - coefMs (0.2, fs);
            parSm = 1.0f - coefMs (20.0, fs);
            holdSamples = (int) (0.025 * fs);
            refractory = (int) (0.03 * fs);
            sm = target;
            setSettings (target);
        }

        void setSettings (const Settings& s)
        {
            target = s;
            room.set (s.size, s.tone);
            attS = coefMs (std::clamp (s.snapMs, 1.0f, 50.0f), fs);
        }

        void process (float* const* ch, int numCh, int n)
        {
            numCh = std::min (numCh, 2);
            if (numCh <= 0) return;
            const Settings& t = target;
            for (int i = 0; i < n; ++i)
            {
                // つまみの値をなめらかに
                sm.attack += (t.attack - sm.attack) * parSm;
                sm.length += (t.length - sm.length) * parSm;
                sm.room   += (t.room - sm.room) * parSm;
                sm.mix    += (t.mix - sm.mix) * parSm;
                sm.snapMs = t.snapMs;

                const float inL = ch[0][i];
                const float inR = numCh > 1 ? ch[1][i] : inL;
                const float x = std::max (std::abs (inL), std::abs (inR));

                // 速い包絡（すぐ上がり、25 ms 止めてから下がる → 低音の波のゆれを拾わない）
                if (x >= eF) { eF = x; holdLeft = holdSamples; }
                else if (holdLeft > 0) --holdLeft;
                else eF *= relF;
                // 遅い包絡（速い包絡を SNAP の時間で追いかける。下がるときはそのまま）
                eS = eF > eS ? eF + (eS - eF) * attS : eF;
                eD = eF > eD ? eF + (eD - eF) * attD : eF;   // 鳴り始めの検出用（10 ms 固定）
                rms += (x * x - rms) * rmsA;
                const float rDb = powDb (rms);

                // 鳴り始め
                if (sinceHit < (1 << 30)) ++sinceHit;
                if (eF > 0.003f && ampDb (eF / std::max (eD, 1.0e-9f)) > 5.0f && sinceHit > refractory)
                {
                    sinceHit = 0;
                    hitPeakR = rms;
                    hitCount.fetch_add (1, std::memory_order_relaxed);
                    if (capPos.load (std::memory_order_relaxed) > (int) (0.12 * dispLen))
                    {
                        capPos.store (0, std::memory_order_relaxed);
                        capCount = 0; capIn = capOut = 0.0f;
                        capId.fetch_add (1, std::memory_order_relaxed);
                    }
                }
                const double tMs = sinceHit * 1000.0 / fs;
                if (tMs < 50.0) hitPeakR = std::max (hitPeakR, rms);
                const float peakDb = powDb (hitPeakR);

                // ATTACK（-18〜+18 dB）
                const float trDb = std::clamp (ampDb (eF / std::max (eS, 1.0e-9f)), 0.0f, 24.0f);
                const float attDb = eF > 1.0e-4f ? sm.attack * 0.75f * trDb : 0.0f;

                // LENGTH / ROOM（打ってからの時間と、打った瞬間との音量差で）
                float bodyDb = 0.0f;
                if (sm.length < -0.001f)
                {
                    const double a = -sm.length;
                    const double t60 = 2500.0 * std::pow (0.012, a);
                    const double hold = 5.0;
                    if (tMs > hold && sinceHit < (1 << 29)) bodyDb += (float) std::max (-90.0, -60.0 * (tMs - hold) / t60);
                }
                else if (sm.length > 0.001f)
                {
                    const double holdL = sm.snapMs + 10.0;
                    if (tMs > holdL && sinceHit < (1 << 29))
                    {
                        const float b = sm.length;
                        const float floorW = std::clamp ((rDb + 66.0f) / 12.0f, 0.0f, 1.0f);   // 無音のノイズは持ち上げない
                        const float ramp = (float) std::min (1.0, (tMs - holdL) / 20.0);
                        bodyDb += std::clamp ((peakDb - rDb) * 0.75f * b, 0.0f, 24.0f * b) * floorW * ramp;
                    }
                }
                if (sm.room < -0.001f && sinceHit < (1 << 29))
                {
                    const float c = -sm.room;
                    if (tMs > sm.snapMs + 8.0)
                    {
                        const float thr = peakDb - (30.0f - 20.0f * c);
                        if (rDb < thr) bodyDb += std::max ((rDb - thr) * 3.0f * c, -60.0f * c);
                    }
                }

                const float tA = dbToGain (attDb), tS = dbToGain (bodyDb);
                gA += (tA - gA) * attSm;
                gS += (tS - gS) * (tS > gS ? upA : downA);
                const float g = gA * gS;
                lastGainDb.store (ampDb (g), std::memory_order_relaxed);

                float yL = inL * g, yR = inR * g;
                float wl = 0, wr = 0;
                room.process (yL, yR, wl, wr);
                const float wet = std::pow (std::max (0.0f, sm.room), 1.3f) * 1.9f;
                yL += wl * wet; yR += wr * wet;

                const float m = sm.mix;
                const float oL = inL + (yL - inL) * m;
                const float oR = inR + (yR - inR) * m;
                ch[0][i] = oL;
                if (numCh > 1) ch[1][i] = oR;

                // 画面用（1 打の最初の約 1 秒）
                capIn = std::max (capIn, x);
                capOut = std::max (capOut, std::max (std::abs (oL), std::abs (oR)));
                if (++capCount >= decim)
                {
                    const int p = capPos.load (std::memory_order_relaxed);
                    if (p < dispLen)
                    {
                        dispIn[p].store (capIn, std::memory_order_relaxed);
                        dispOut[p].store (capOut, std::memory_order_relaxed);
                        capPos.store (p + 1, std::memory_order_release);
                    }
                    capCount = 0; capIn = capOut = 0.0f;
                }
            }
        }

        // 画面から読む
        std::array<std::atomic<float>, dispLen> dispIn {}, dispOut {};
        std::atomic<int> capPos { dispLen + 1 }, capId { 0 }, hitCount { 0 };
        std::atomic<float> lastGainDb { 0.0f };

    private:
        double fs = 48000.0;
        Settings target, sm;
        Room room;
        float eF = 0, eS = 0, eD = 0, rms = 0, hitPeakR = 1.0e-12f;
        float relF = 0.999f, attS = 0.99f, attD = 0.99f, rmsA = 0.01f;
        float upA = 0.1f, downA = 0.01f, attSm = 0.1f, parSm = 0.001f;
        float gA = 1, gS = 1;
        int holdLeft = 0, holdSamples = 1000, refractory = 1000, sinceHit = 1 << 30;
        int decim = 48, capCount = 0;
        float capIn = 0, capOut = 0;
    };
}
