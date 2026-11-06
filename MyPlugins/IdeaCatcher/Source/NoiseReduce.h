#pragma once
// 録り終えた声のノイズ除去（JUCE なし、単体テスト可）。
//  1. 2048 点・1/4 ずつずらした窓で、細かい周波数に分ける（STFT）
//  2. 一番静かな 15% の所（声のない所）の平均を「ノイズの形」として覚える
//  3. 周波数ごとに、ノイズより十分大きい所は残し、ノイズくらいの所は下げる（下げすぎないよう floor まで）
//     ・下げ方は時間方向にならして（上げは速く、下げはゆっくり）、ピロピロした音（ミュージカルノイズ）を防ぐ
//  声のない所がほとんどない録音（ノイズの見積もりが声に近い）は、30 Hz より下を落とすだけ
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace nr
{

inline void fft (std::vector<std::complex<float>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * M_PI / (double) len * (inverse ? 1 : -1);
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (float) n;
}

struct Info
{
    bool applied = false;
    float noiseDb = -120;    // 見積もったノイズの大きさ（窓あたり、dBFS 相当）
    float voiceDb = -120;    // 鳴っている所（上位 50%）の大きさ
};

// strength: 0 = しない, 1 = ふつう（最大 -15 dB）, 2 = 強め（最大 -24 dB）
inline Info reduce (std::vector<float>& x, double fs, int strength)
{
    Info info;
    if (strength <= 0 || x.size() < (size_t) (fs * 0.5)) return info;
    const size_t N = 2048, hop = N / 4, bins = N / 2 + 1;
    const size_t frames = (x.size() + N) / hop + 1;
    std::vector<float> win (N);
    for (size_t i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * (float) std::cos (2 * M_PI * (double) i / (double) N);

    // 30 Hz より下のゴロゴロ（机・空調の振動）を先に落とす（低い男声の F1 = 44 Hz は残る。2 次のバターワース）
    {
        const double w = std::tan (M_PI * 30.0 / fs), k = std::sqrt (2.0), n = 1.0 / (1.0 + k * w + w * w);
        const double b0 = n, b1 = -2 * n, b2 = n, a1 = 2 * (w * w - 1) * n, a2 = (1 - k * w + w * w) * n;
        double z1 = 0, z2 = 0;
        for (auto& v : x)
        {
            const double y = b0 * v + z1;
            z1 = b1 * v - a1 * y + z2;
            z2 = b2 * v - a2 * y;
            v = (float) y;
        }
    }

    // 前後に N の 0 を足して、端まで同じように処理する
    std::vector<float> pad (x.size() + 2 * N + hop, 0.0f);
    std::copy (x.begin(), x.end(), pad.begin() + (long) N);

    // 1 回目: 窓ごとの大きさ
    std::vector<float> energy (frames, 0.0f);
    for (size_t f = 0; f < frames; ++f)
    {
        double e = 0;
        for (size_t i = 0; i < N && f * hop + i < pad.size(); ++i) { const double v = pad[f * hop + i] * win[i]; e += v * v; }
        energy[f] = (float) e;
    }
    // 端の 0 の所を除いて、静かな順に並べる
    std::vector<float> sorted;
    for (size_t f = N / hop; f + N / hop < frames; ++f) sorted.push_back (energy[f]);
    if (sorted.size() < 8) return info;
    std::sort (sorted.begin(), sorted.end());
    const float quietTh = sorted[sorted.size() * 15 / 100];
    const float median = sorted[sorted.size() / 2], loud = sorted[sorted.size() * 9 / 10];
    auto db = [] (double e) { return (float) (10.0 * std::log10 (e + 1e-20)); };
    info.noiseDb = db (sorted[sorted.size() * 7 / 100]);
    info.voiceDb = db (loud);
    // 静かな所でも声と 12 dB 以上の差がない＝ずっと鳴っている／ほとんど無音: 何もしない
    if (info.voiceDb - info.noiseDb < 12.0f || median <= 0) return info;

    // 2 回目: 静かな所の平均スペクトル＝ノイズの形
    std::vector<double> noise (bins, 0.0);
    int nq = 0;
    std::vector<std::complex<float>> buf (N);
    for (size_t f = N / hop; f + N / hop < frames; ++f)
    {
        if (energy[f] > quietTh) continue;
        for (size_t i = 0; i < N; ++i) buf[i] = pad[f * hop + i] * win[i];
        fft (buf, false);
        for (size_t k = 0; k < bins; ++k) noise[k] += std::norm (buf[k]);
        ++nq;
    }
    if (nq < 4) return info;
    for (auto& v : noise) v /= nq;
    // 周波数方向にも少しならす（5 本）
    std::vector<double> ns (noise);
    for (size_t k = 0; k < bins; ++k)
    {
        double s = 0; int c = 0;
        for (int d = -2; d <= 2; ++d)
        {
            const long j = (long) k + d;
            if (j >= 0 && j < (long) bins) { s += noise[(size_t) j]; ++c; }
        }
        ns[k] = s / c;
    }

    // 3 回目: 下げて戻す（重ね合わせ。ハン窓 2 回・1/4 ずらしで合計 1.5 倍になるので割る）
    const float floorG = strength >= 2 ? 0.063f : 0.178f;   // -24 dB / -15 dB
    const float over = strength >= 2 ? 2.5f : 1.8f;          // ノイズの何倍までを「ノイズ」とみなすか（パワー）
    const float hopSec = (float) hop / (float) fs;
    const float up = 1.0f - std::exp (-hopSec / 0.004f), down = 1.0f - std::exp (-hopSec / 0.08f);
    std::vector<float> g (bins, 1.0f), out (pad.size(), 0.0f);
    std::vector<double> pw (bins), psm (bins, 0.0);   // その窓のパワー（周波数方向にならす）と、時間方向にならしたもの
    for (size_t f = 0; f < frames; ++f)
    {
        if (f * hop + N > pad.size()) break;
        for (size_t i = 0; i < N; ++i) buf[i] = pad[f * hop + i] * win[i];
        fft (buf, false);
        for (size_t k = 0; k < bins; ++k) pw[k] = std::norm (buf[k]);
        for (size_t k = 0; k < bins; ++k)
        {
            // 雑音のパワーは窓ごとに大きくばらつくので、まわり 3 本と前の窓とならしてから比べる（4 倍以上に跳ねたとき＝音の出だしはすぐ追う）
            const double p3 = (k > 0 && k + 1 < bins) ? 0.25 * pw[k - 1] + 0.5 * pw[k] + 0.25 * pw[k + 1] : pw[k];
            psm[k] = p3 > 4.0 * psm[k] ? p3 : 0.55 * psm[k] + 0.45 * p3;
            const double p = psm[k];
            // パワーの引き算（ウィーナー風）。ノイズの over 倍以下なら floor へ
            const float target = (float) std::clamp (1.0 - over * ns[k] / (p + 1e-20), (double) floorG * floorG, 1.0);
            const float t = std::sqrt (target);
            g[k] += (t - g[k]) * (t > g[k] ? up : down);
        }
        // 周波数方向にもならして、孤立した残り（ピロピロ）を消す
        std::vector<float> gs (g);
        for (size_t k = 1; k + 1 < bins; ++k) gs[k] = 0.25f * g[k - 1] + 0.5f * g[k] + 0.25f * g[k + 1];
        for (size_t k = 0; k < bins; ++k)
        {
            buf[k] *= gs[k];
            if (k > 0 && k < N / 2) buf[N - k] = std::conj (buf[k]);
        }
        fft (buf, true);
        for (size_t i = 0; i < N; ++i) out[f * hop + i] += buf[i].real() * win[i] / 1.5f;
    }
    std::copy (out.begin() + (long) N, out.begin() + (long) (N + x.size()), x.begin());
    info.applied = true;
    return info;
}

} // namespace nr
