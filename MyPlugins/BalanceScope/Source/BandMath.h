#pragma once
// 1/6 オクターブ帯域にまとめる計算と、「出すぎの帯域」を探す計算（画面と Plugin Link で共用。JUCE なし）
#include <vector>
#include <cmath>
#include <algorithm>
#include "Targets.h"

namespace bandmath
{
    inline std::vector<float> makeCenters (float fMin = 20.0f, float fMax = 20000.0f)
    {
        std::vector<float> c;
        for (float f = fMin; f <= fMax; f *= std::pow (2.0f, 1.0f / 6.0f)) c.push_back (f);
        return c;
    }

    // FFT の強さ（power、ビン数 = fftSize/2+1）→ 帯域ごとの dB（無いところは -300）
    inline std::vector<float> toBands (const std::vector<double>& power, double sampleRate, int fftSize,
                                       const std::vector<float>& centers)
    {
        const double bw = sampleRate / fftSize;
        const int maxBin = (int) power.size() - 1;
        std::vector<float> out (centers.size(), -300.0f);
        if (maxBin < 2) return out;

        for (size_t k = 0; k < centers.size(); ++k)
        {
            const double fc = centers[k];
            if (fc > sampleRate * 0.45) break;
            const double lo = fc * std::pow (2.0, -1.0 / 12.0), hi = fc * std::pow (2.0, 1.0 / 12.0);
            const int b0 = (int) std::ceil (lo / bw), b1 = std::min (maxBin, (int) std::floor (hi / bw));

            double density = 0.0;
            if (b1 >= b0 && b0 >= 1)
            {
                for (int b = b0; b <= b1; ++b) density += power[(size_t) b];
                density /= (b1 - b0 + 1);
            }
            else
            {
                // 低域でビンが帯域に入らないときは近くのビンを補間
                const double pos = fc / bw;
                const int i0 = std::clamp ((int) pos, 1, maxBin - 1);
                const double fr = std::clamp (pos - i0, 0.0, 1.0);
                density = power[(size_t) i0] * (1.0 - fr) + power[(size_t) i0 + 1] * fr;
            }
            out[k] = (float) (10.0 * std::log10 (density * (hi - lo) / bw + 1.0e-30));
        }
        return out;
    }

    // 目標カーブに合わせて全体の音量差を取り除く（100Hz〜8kHzの平均で揃える）
    inline void normalise (std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t)
    {
        double sum = 0.0; int n = 0;
        for (size_t k = 0; k < db.size(); ++k)
            if (centers[k] >= 100.0f && centers[k] <= 8000.0f && db[k] > -250.0f)
            { sum += db[k] - targetDb (t, centers[k]); ++n; }
        const float off = n > 0 ? (float) (sum / n) : 0.0f;
        for (auto& v : db) if (v > -250.0f) v -= off;
    }

    struct Excess
    {
        float excessDb = 0.0f;   // 許容幅を超えた量（いちばん大きい所。0 = なし）
        float lowHz = 0.0f, highHz = 0.0f, peakHz = 0.0f;
    };

    // 「許容幅を何 dB 超えているか」を帯域ごとに（となり合う 3 帯域＝半オクターブでならす。測れない所は -100）
    inline std::vector<float> overAmounts (const std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t,
                                           const std::vector<float>& refDb, float refTol)
    {
        const size_t n = std::min (db.size(), centers.size());
        std::vector<float> over (n, -100.0f);
        for (size_t k = 0; k < n; ++k)
        {
            if (db[k] < -250.0f) continue;
            if (! refDb.empty())
            {
                if (k >= refDb.size() || refDb[k] < -250.0f) continue;
                over[k] = db[k] - refDb[k] - refTol;
            }
            else over[k] = db[k] - targetDb (t, centers[k]) - targetTol (t, centers[k]);
        }
        // 細いギザギザに反応しないように
        std::vector<float> sm (n, -100.0f);
        for (size_t k = 1; k + 1 < n; ++k)
            if (over[k - 1] > -99.0f && over[k] > -99.0f && over[k + 1] > -99.0f)
                sm[k] = (over[k - 1] + over[k] + over[k + 1]) / 3.0f;
        return sm;
    }

    // sm（overAmounts の結果）の 150 Hz〜12 kHz で、いちばん大きい山を探す。
    // 山のすそ（超えている量が山の 1/3 より大きい所）を範囲にする。幅は最低 2/3 オクターブ、最大 2 オクターブ。
    inline Excess peakOf (const std::vector<float>& sm, const std::vector<float>& centers, float minPeak = 0.0f)
    {
        const size_t n = std::min (sm.size(), centers.size());
        Excess e;
        int best = -1;
        for (size_t k = 0; k < n; ++k)
            if (centers[k] >= 150.0f && centers[k] <= 12000.0f && sm[k] > minPeak && (best < 0 || sm[k] > sm[(size_t) best]))
                best = (int) k;
        if (best < 0) return e;

        const float peak = sm[(size_t) best];
        const float edge = peak / 3.0f;
        int a = best, b = best;
        while (a > 0 && sm[(size_t) a - 1] > edge && centers[(size_t) a - 1] >= 100.0f) --a;
        while (b + 1 < (int) n && sm[(size_t) b + 1] > edge && centers[(size_t) b + 1] <= 16000.0f) ++b;

        const float semi = std::pow (2.0f, 1.0f / 12.0f);   // 帯域の端（1/6 オクターブの半分）
        float lo = centers[(size_t) a] / semi, hi = centers[(size_t) b] * semi;
        const float pk = centers[(size_t) best];
        const float minHalf = std::pow (2.0f, 1.0f / 3.0f), maxHalf = 2.0f;
        lo = std::clamp (lo, pk / maxHalf, pk / minHalf);
        hi = std::clamp (hi, pk * minHalf, pk * maxHalf);

        e.excessDb = peak;
        e.lowHz = lo;
        e.highHz = hi;
        e.peakHz = pk;
        return e;
    }

    // いちばん出すぎの帯域。db は normalise 済み。refDb が空なら目標カーブと比べる。
    inline Excess findExcess (const std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t,
                              const std::vector<float>& refDb = {}, float refTol = 2.0f)
    {
        return peakOf (overAmounts (db, centers, t, refDb, refTol), centers);
    }

    // 上位 2 か所。2 番目は 1 番目の範囲（とその外 1/3 オクターブ）に重ならない所で、0.5 dB 以上超えているもの
    inline void findExcess2 (const std::vector<float>& db, const std::vector<float>& centers, const TargetCurve& t,
                             const std::vector<float>& refDb, Excess& first, Excess& second, float refTol = 2.0f)
    {
        auto sm = overAmounts (db, centers, t, refDb, refTol);
        first = peakOf (sm, centers);
        second = {};
        if (first.excessDb <= 0.0f) return;
        const float guard = std::pow (2.0f, 1.0f / 3.0f);
        for (size_t k = 0; k < sm.size() && k < centers.size(); ++k)
            if (centers[k] >= first.lowHz / guard && centers[k] <= first.highHz * guard) sm[k] = -100.0f;
        second = peakOf (sm, centers, 0.5f);
        // 2 番目の範囲が 1 番目に食い込まないように切る
        if (second.excessDb > 0.0f)
        {
            if (second.peakHz < first.peakHz) second.highHz = std::min (second.highHz, first.lowHz);
            else                              second.lowHz = std::max (second.lowHz, first.highHz);
            if (second.highHz <= second.lowHz * 1.05f) second = {};
        }
    }
}
