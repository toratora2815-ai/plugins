#pragma once
// ビビり補正（0.4.0〜）: 録った 1 音の中で、少したってから弦がフレットに当たって鳴る「ビビり」を見つけて、
// 増えた分だけ帯域ごとに下げる（上げない）。音を作り直さないので、基音・うなり・雑音の床は本物のまま残る。
//  1. 見つける: 1/6 オクターブの帯域ごとに音量（10 ms ごと）を測る。弾いて 0.2 秒より後で、高い帯域（1.5 kHz か
//     3 倍音より上）が「直前 0.35 秒の減り方を延ばした線」から 9 dB 以上はね上がり、高い帯域の半分以上が同時に
//     6 dB 以上はみ出し、雑音の床より 12 dB 以上大きいことが 4 回中 3 回続いたらビビり（うなりは帯域ごとに
//     ばらばらに揺れるので、半分以上が同時にはみ出すことはない）。直後に全体が 15 dB 落ちるのは手のミュート
//  2. 直す: ビビりの手前まで（最大 0.8 秒、新しいほど重く）で帯域ごとに減り方の線を引き、ビビり以降は
//     「線＋その帯域のふだんの揺れ（2〜6 dB）」を超えた分だけ下げる。下げるのはすぐ、戻すのはゆっくり（1 フレームに
//     1.3 dB まで）。基音のあたり（3 倍音まで）は 12 dB、高い帯域は 40 dB までしか下げない
//  3. ビビりの手前までは録った音そのまま（1 サンプルも変えない）
// JUCE なし（tools/BuzzTest.cpp で単体テスト）。
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace buzz
{
constexpr int kVersion = 1;   // 直し方を変えたら上げる（保存した音は直した結果ごと残すので、昔の曲の音は変わらない）

struct Report
{
    bool  found = false;     // ビビりがあった
    int   start = -1;        // ビビりの始まり（だいたい、サンプル）
    int   cleanEnd = -1;     // ここまではきれい（線はここより前で引く）
    int   fixFrom = -1;      // 直した音がここから変わる（ここより前は録った音そのまま）
    int   fixEnd = -1;       // ここより後も録った音そのまま（ビビりが止まって 0.6 秒たったところ）
    float changeDb = -200.0f;   // 直した分（元の音との差）の大きさ。直したところの元の音に対して dB
    float riseDb = 0.0f;     // 高い帯域が線からはね上がった量
    float cutDb = 0.0f;      // 直し始めの 0.5 秒で、高い帯域を平均どれだけ下げたか（マイナス）
    bool  strong = false;    // 下げても尾にビビりの手触りが残りそう（同じ場所のきれいな録音の尾を借りるほうがいい）
    float closestFrac = 0.0f, closestX = 0.0f;   // テスト用: ビビりでない音で、いちばんビビりに近かったフレーム
};

namespace detail
{
    struct FFT
    {
        explicit FFT (int size) : n (size)
        {
            int lg = 0; while ((1 << lg) < n) ++lg;
            rev.resize ((size_t) n);
            for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[(size_t) i] = r; }
            tw.resize ((size_t) n / 2);
            for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * 3.14159265358979323846 * i / n));
        }
        void run (std::complex<float>* d, bool inverse) const
        {
            for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (d[i], d[rev[(size_t) i]]);
            for (int len = 2; len <= n; len <<= 1)
            {
                const int half = len / 2, step = n / len;
                for (int i = 0; i < n; i += len)
                    for (int j = 0; j < half; ++j)
                    {
                        auto w = tw[(size_t) (j * step)];
                        if (inverse) w = std::conj (w);
                        const auto u = d[i + j], v = d[i + j + half] * w;
                        d[i + j] = u + v; d[i + j + half] = u - v;
                    }
            }
            if (inverse) { const float s = 1.0f / (float) n; for (int i = 0; i < n; ++i) d[i] *= s; }
        }
        int n;
        std::vector<int> rev;
        std::vector<std::complex<float>> tw;
    };

    // the frames: hop N/4, frame f starts at f*H - N + H (every sample is in 4 frames)
    struct Grid
    {
        Grid (int numSamples, double sr) : n (numSamples)
        {
            N = sr > 33000.0 ? 2048 : 1024;   // about 43 ms
            H = N / 4;
            F = (n + N - H + H - 1) / H;
        }
        int start (int f) const { return f * H - N + H; }
        double centreSec (int f, double sr) const { return (start (f) + N / 2) / sr; }
        int n, N, H, F;
    };

    struct Bands
    {
        std::vector<int> lo, hi;       // bins
        std::vector<double> centre;    // Hz
        int size() const { return (int) lo.size(); }
    };
    inline Bands makeBands (int N, double sr)
    {
        Bands b;
        const double df = sr / N, top = std::min (16000.0, 0.47 * sr);
        int k0 = std::max (1, (int) std::ceil (40.0 / df));
        double e = 40.0;
        while (true)
        {
            double e2 = e * std::pow (2.0, 1.0 / 6.0);
            int k1 = (int) std::ceil (e2 / df) - 1;
            while (k1 - k0 + 1 < 2) { e2 *= std::pow (2.0, 1.0 / 6.0); k1 = (int) std::ceil (e2 / df) - 1; }   // at least 2 bins
            if (e2 > top) break;
            b.lo.push_back (k0); b.hi.push_back (k1);
            b.centre.push_back (std::sqrt (std::max (e, df * k0) * e2));
            k0 = k1 + 1; e = e2;
        }
        return b;
    }

    inline float hann (int i, int N) { return 0.5f - 0.5f * (float) std::cos (2.0 * 3.14159265358979323846 * i / N); }

    inline void spectrum (const float* x, int n, const Grid& g, int f, const FFT& fft, std::vector<std::complex<float>>& buf)
    {
        const int s = g.start (f);
        for (int i = 0; i < g.N; ++i)
        {
            const int k = s + i;
            buf[(size_t) i] = { k >= 0 && k < n ? x[k] * hann (i, g.N) : 0.0f, 0.0f };
        }
        fft.run (buf.data(), false);
    }

    // least-squares line through y[a..b] (frame index as x), optionally weighted towards the end
    struct Line { double at = 0, slope = 0; int x0 = 0; double value (int f) const { return at + slope * (f - x0); } };
    inline Line fitLine (const std::vector<float>& y, int a, int b, bool recentHeavier)
    {
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = a; i <= b; ++i)
        {
            const double w = recentHeavier && b > a ? 1.0 + 2.0 * (i - a) / (double) (b - a) : 1.0;
            const double x = i - b;
            sw += w; sx += w * x; sy += w * y[(size_t) i]; sxx += w * x * x; sxy += w * x * y[(size_t) i];
        }
        Line l; l.x0 = b;
        const double den = sw * sxx - sx * sx;
        l.slope = den > 1e-12 ? (sw * sxy - sx * sy) / den : 0.0;
        l.at = (sy - l.slope * sx) / std::max (1e-12, sw);
        return l;
    }

    inline double toPow (double db) { return std::pow (10.0, 0.1 * db); }
    inline double toDb (double p) { return 10.0 * std::log10 (p + 1e-30); }

    // everything the finder and the fixer share
    struct Analysis
    {
        Analysis (const float* x, int numSamples, double rate, double f0)
            : sr (rate), g (numSamples, rate), fft (g.N), bands (makeBands (g.N, rate))
        {
            const int B = bands.size();
            L.assign ((size_t) B, std::vector<float> ((size_t) g.F, -300.0f));
            total.assign ((size_t) g.F, -300.0f);
            std::vector<std::complex<float>> buf ((size_t) g.N);
            for (int f = 0; f < g.F; ++f)
            {
                spectrum (x, numSamples, g, f, fft, buf);
                double all = 0;
                for (int b = 0; b < B; ++b)
                {
                    double e = 0;
                    for (int k = bands.lo[(size_t) b]; k <= bands.hi[(size_t) b]; ++k) e += std::norm (buf[(size_t) k]);
                    L[(size_t) b][(size_t) f] = (float) toDb (e);
                    all += e;
                }
                total[(size_t) f] = (float) toDb (all);
            }
            fa = 0; while (fa < g.F && g.centreSec (fa, sr) < 0.08) ++fa;
            fs = 0; while (fs < g.F && g.centreSec (fs, sr) < 0.20) ++fs;
            fe = g.F - 1; while (fe > 0 && g.start (fe) + g.N > numSamples - (int) (0.12 * sr)) --fe;

            // the noise floor of each band: its quietest 10 % (a short note never reaches its floor: then this is
            // simply higher, which only makes the finder and the fixer more careful)
            noise.assign ((size_t) B, -300.0f);
            for (int b = 0; b < B; ++b)
            {
                std::vector<float> v;
                for (int f = fa; f <= fe; ++f) v.push_back (L[(size_t) b][(size_t) f]);
                if (v.empty()) continue;
                std::sort (v.begin(), v.end());
                noise[(size_t) b] = v[v.size() / 10];
            }
            const double hiLo = std::max (1500.0, 3.0 * f0), hiHi = std::min (12000.0, 0.45 * sr);
            double best = 1e30;
            for (int b = 0; b < B; ++b)
            {
                if (bands.centre[(size_t) b] >= hiLo && bands.centre[(size_t) b] <= hiHi) hb.push_back (b);
                lowTop = bands.centre[(size_t) b] <= 3.2 * f0 ? b : lowTop;
                const double d = std::abs (std::log (bands.centre[(size_t) b] / f0));
                if (d < best) { best = d; fBand = b; }
            }
            // the note itself: its lowest bands together (up to the 3rd harmonic)
            low.assign ((size_t) g.F, -300.0f);
            for (int f = 0; f < g.F; ++f)
            {
                double e = 0;
                for (int b = 0; b <= lowTop; ++b) e += toPow (L[(size_t) b][(size_t) f]);
                low[(size_t) f] = (float) toDb (e);
            }
        }

        double sr;
        Grid g;
        FFT fft;
        Bands bands;
        std::vector<std::vector<float>> L;   // [band][frame] dB
        std::vector<float> total, noise, low;
        std::vector<int> hb;                 // the high bands
        int fa = 0, fs = 0, fe = 0, lowTop = -1, fBand = 0;
    };

    // band gains (dB) -> bin gains (straight lines in dB between band centres, over log frequency)
    inline void bandsToBins (const Bands& bands, const std::vector<double>& db, int N, double sr, std::vector<float>& bin)
    {
        const int half = N / 2, B = bands.size();
        bin.assign ((size_t) half + 1, 1.0f);
        const double df = sr / N;
        int b = 0;
        for (int k = 0; k <= half; ++k)
        {
            const double hz = k * df;
            while (b + 1 < B && bands.centre[(size_t) (b + 1)] < hz) ++b;
            double v;
            if (hz <= bands.centre.front()) v = db.front();
            else if (b + 1 >= B) v = db.back();
            else
            {
                const double c0 = bands.centre[(size_t) b], c1 = bands.centre[(size_t) (b + 1)];
                const double t = std::clamp (std::log (hz / c0) / std::log (c1 / c0), 0.0, 1.0);
                v = db[(size_t) b] * (1.0 - t) + db[(size_t) (b + 1)] * t;
            }
            bin[(size_t) k] = (float) std::pow (10.0, v / 20.0);
        }
    }

    // short-time Fourier: frames from `first`, gains (from setGains) on frames >= `gainFrom`, overlap-added into acc
    template <typename SetGains>
    inline void olaWithGains (const float* x, int n, const Grid& g, const FFT& fft, int first, int gainFrom, SetGains&& setGains, std::vector<float>& acc)
    {
        const int N = g.N, half = N / 2;
        acc.assign ((size_t) n, 0.0f);
        std::vector<float> bin;
        std::vector<std::complex<float>> buf ((size_t) N);
        for (int f = first; f < g.F; ++f)
        {
            spectrum (x, n, g, f, fft, buf);
            if (f >= gainFrom)
            {
                setGains (f, bin);
                for (int k = 0; k <= half; ++k) buf[(size_t) k] *= bin[(size_t) k];
                for (int k = 1; k < half; ++k) buf[(size_t) (N - k)] = std::conj (buf[(size_t) k]);
            }
            fft.run (buf.data(), true);
            const int s0 = g.start (f);
            for (int i = 0; i < N; ++i)
            {
                const int k = s0 + i;
                if (k >= 0 && k < n) acc[(size_t) k] += buf[(size_t) i].real() * hann (i, N) / 1.5f;
            }
        }
    }
}

//==============================================================================
// x: one note as sliced (starts ~3 ms before the pick), f0: its measured pitch
inline Report detect (const detail::Analysis& a)
{
    using namespace detail;
    Report r;
    const auto& hb = a.hb;
    if ((int) hb.size() < 4 || a.fe - a.fs < 6) return r;
    constexpr int G = 3, W = 32;
    const double maxFall = -1.0;   // dB per frame (~ -95 dB/s: a line cannot fall faster)
    std::vector<Line> lines (hb.size());

    for (int f = a.fs; f <= a.fe; ++f)
    {
        const int w1 = f - G, w0 = std::max (a.fa, w1 - W);
        if (w1 - w0 + 1 < 8) continue;
        for (size_t j = 0; j < hb.size(); ++j)
        {
            lines[j] = fitLine (a.L[(size_t) hb[j]], w0, w1, false);
            lines[j].slope = std::max (maxFall, lines[j].slope);
        }
        auto measure = [&] (int ff, double& X, double& frac, double& above)
        {
            double P = 0, Q = 0, Nn = 0; int out = 0;
            for (size_t j = 0; j < hb.size(); ++j)
            {
                const int b = hb[j];
                const double l = a.L[(size_t) b][(size_t) ff], base = std::max (lines[j].value (ff), (double) a.noise[(size_t) b]);
                P += toPow (l); Q += toPow (base); Nn += toPow (a.noise[(size_t) b]);
                if (l - base > 6.0) ++out;
            }
            X = toDb (P) - toDb (Q); frac = out / (double) hb.size(); above = toDb (P) - toDb (Nn);
        };
        // nearly all high bands a little over their lines, most of them clearly, or half of them a lot
        // (beating moves one band at a time: in strongly beating clean notes at most a third of them at once)
        auto isBuzz = [&] (int ff) { double X, fr, ab; measure (ff, X, fr, ab); return ab > 12.0 && ((fr >= 0.75 && X > 4.0) || (fr >= 0.6 && X > 6.0) || (fr >= 0.5 && X > 9.0)); };
       #ifdef BUZZ_DIAG
        { double X, fr, ab; measure (f, X, fr, ab); if (a.g.centreSec (f, a.sr) > BUZZ_DIAG - 0.15 && a.g.centreSec (f, a.sr) < BUZZ_DIAG + 0.3)
            std::printf ("      t %.3f  X %5.1f  frac %.2f  above %5.1f  total %6.1f\n", a.g.centreSec (f, a.sr), X, fr, ab, a.total[(size_t) f]); }
       #endif
        // something starts here (most high bands a little over their lines): the lines stay where they were
        // before it (a buzz that grows over 50-100 ms would otherwise pull them up with it)
        double X0, fr0, ab0;
        measure (f, X0, fr0, ab0);
        // the note itself (its lowest bands, and the fundamental's band): through a buzz it goes on as before
        // (or swells slowly). A hand mute or a lifted finger makes it fall (the note stops, or turns into the
        // open string), the pick touching the string again makes it jump: neither is a buzz
        const Line lowLine = fitLine (a.low, w0, w1, false), fLine = fitLine (a.L[(size_t) a.fBand], w0, w1, false);
        const int ahead = std::min (a.g.F - 1, f + (int) (0.3 * a.sr / a.g.H));
        bool falls = false, fundFalls = false, jumps = false;
        for (int k = f; k <= ahead; ++k)
        {
            if (a.low[(size_t) k] - lowLine.value (k) < -10.0) falls = true;
            if (a.L[(size_t) a.fBand][(size_t) k] - fLine.value (k) < -12.0) fundFalls = true;
            if (k <= f + 4 && a.low[(size_t) k] - lowLine.value (k) > 4.0) jumps = true;
        }
        // only the fundamental's band falls (a lifted finger, or a hard buzz cancelling it): not judged here, look on after it
        if (fundFalls && ! falls) jumps = true;
        if (! falls && ! jumps && ab0 > 12.0 && fr0 + X0 / 30.0 > r.closestFrac + r.closestX / 30.0) { r.closestFrac = (float) fr0; r.closestX = (float) X0; }
        if (! (X0 > 3.0 && fr0 >= 0.4)) continue;
       #ifdef BUZZ_DIAG
        std::printf ("      candidate t %.3f X %.1f frac %.2f falls %d jumps %d\n", a.g.centreSec (f, a.sr), X0, fr0, (int) falls, (int) jumps);
       #endif
        if (falls) return r;                                                    // the note ends here
        if (jumps) { f += (int) (0.3 * a.sr / a.g.H); continue; }               // picked again (or the fundamental gone): look on after it
       #ifdef BUZZ_DIAG
        for (int k = 0; k <= 13 && f + k < a.g.F; ++k) { double X, fr, ab; measure (f + k, X, fr, ab); std::printf ("        frozen t %.3f X %5.1f frac %.2f above %5.1f\n", a.g.centreSec (f + k, a.sr), X, fr, ab); }
       #endif
        // within ~0.1 s it must clearly be a buzz in 3 of 4 frames that are 2 hops apart (one click, however loud,
        // is in 4 neighbouring frames only: at most 2 of these): it lasts 60 ms or more
        int conf = -1;
        for (int k = 0; k <= 10 && conf < 0; ++k)
        {
            int hits = 0;
            for (int j = 0; j < 4 && f + k + 2 * j < a.g.F; ++j) if (isBuzz (f + k + 2 * j)) ++hits;
            if (hits >= 3 && isBuzz (f + k)) conf = f + k;
        }
        if (conf < 0) continue;

        int on = f;
        while (on - 1 > w1) { double X, fr, ab; measure (on - 1, X, fr, ab); if (X > 2.0) --on; else break; }
        double X, fr, ab; measure (conf, X, fr, ab);
        r.found = true;
        r.start = std::max (0, a.g.start (on) + a.g.N / 2);
        // the first frame that shows it may have started a little before its own window: two hops of room
        r.cleanEnd = std::max (0, a.g.start (on) - 2 * a.g.H);
        r.riseDb = (float) X;
        return r;
    }
    return r;
}

inline Report detect (const float* x, int n, double sr, double f0)
{
    if (x == nullptr || n < (int) (0.5 * sr) || ! (f0 > 20.0)) return {};
    detail::Analysis a (x, n, sr, f0);
    return detect (a);
}

//==============================================================================
// out: the fixed note (same length). Returns false when there is nothing to fix.
inline bool repair (const float* x, int n, double sr, double f0, Report& r, std::vector<float>& out)
{
    using namespace detail;
    out.clear();
    if (x == nullptr || n < (int) (0.5 * sr) || ! (f0 > 20.0)) return false;
    Analysis a (x, n, sr, f0);
    if (! r.found) r = detect (a);
    if (! r.found) return false;
    const auto& g = a.g;
    const int B = a.bands.size();

    // the first frame that shows the buzz
    int on = 0; while (on < g.F && g.start (on) < r.cleanEnd) ++on;
    const int r1 = on - 1, r0 = std::max (a.fa, r1 - (int) (0.8 * sr / g.H));
    if (r1 - r0 + 1 < 8) return false;

    // each band: where it would have gone (line + its usual wobble), never under its noise floor
    const double minFall = -3.0 * g.H / sr;   // at least 3 dB/s down
    std::vector<Line> line ((size_t) B);
    std::vector<double> margin ((size_t) B);
    for (int b = 0; b < B; ++b)
    {
        auto& y = a.L[(size_t) b];
        line[(size_t) b] = fitLine (y, r0, r1, true);
        line[(size_t) b].slope = std::clamp (line[(size_t) b].slope, -2.0, minFall);
        std::vector<double> res;
        for (int f = r0; f <= r1; ++f) res.push_back (y[(size_t) f] - line[(size_t) b].value (f));
        std::sort (res.begin(), res.end());
        margin[(size_t) b] = std::clamp (res[(size_t) (res.size() * 9 / 10)], 2.0, 6.0);
    }

    auto allowedAt = [&] (int b, int f) { return std::max (line[(size_t) b].value (f) + margin[(size_t) b], (double) a.noise[(size_t) b] + 3.0); };

    // only while it buzzes: a frame buzzes when 40 % of the high bands are 6 dB over where they would be (and all of
    // them together 3 dB). The cut holds 0.3 s after the last buzzing frame, then goes back to nothing over 0.3 s
    // (a string that stops hitting the fret rings on as recorded: its own beating and decay, not the line's)
    std::vector<float> engage ((size_t) g.F, 0.0f);
    int lastBuzzing = on;
    {
        const int hold = (int) (0.3 * sr / g.H), rel = std::max (1, (int) (0.3 * sr / g.H));
        int left = hold;
        double e = 1.0;
        lastBuzzing = on;
        for (int f = on; f < g.F; ++f)
        {
            double P = 0, Q = 0; int out = 0;
            for (int b : a.hb)
            {
                const double al = allowedAt (b, f), l = a.L[(size_t) b][(size_t) f];
                P += toPow (l); Q += toPow (al);
                if (l - al > 6.0) ++out;
            }
            const bool buzzing = out >= 0.4 * (double) a.hb.size() && toDb (P) - toDb (Q) > 3.0;
            if (buzzing) { left = hold; e = 1.0; lastBuzzing = f; }
            else if (left > 0) --left;
            else e = std::max (0.0, e - 1.0 / rel);
            engage[(size_t) f] = (float) e;
            if (buzzing || e > 0.0) r.fixEnd = g.start (f) + g.N;
        }
    }

    // the cut of each band and frame (dB): down at once, back up slowly, times `engage`. The note's own lowest
    // bands (to the 3rd harmonic) at most 6 dB (the swell that comes with a buzz), the highs up to 40 dB
    std::vector<std::vector<float>> cut ((size_t) B, std::vector<float> ((size_t) g.F, 0.0f));
    for (int b = 0; b < B; ++b)
    {
        const double floorDb = b <= a.lowTop ? -6.0 : -40.0;
        double prev = 0.0;
        for (int f = on; f < g.F; ++f)
        {
            const double want = std::max (floorDb, std::min (0.0, allowedAt (b, f) - a.L[(size_t) b][(size_t) f]));
            // back up slowly while the band is still over (no pumping), quickly once it is not (a short noise
            // does not leave the highs down for long)
            prev = std::min (want, prev + (want > -0.5 ? 4.0 : 1.3));
            cut[(size_t) b][(size_t) f] = (float) (prev * engage[(size_t) f]);
        }
    }
    {
        double s = 0; int c = 0;
        for (int f = on; f < std::min (g.F, on + (int) (0.5 * sr / g.H)); ++f)
            for (int b : a.hb) { s += cut[(size_t) b][(size_t) f]; ++c; }
        r.cutDb = c > 0 ? (float) (s / c) : 0.0f;
        // strong: cut a lot, for a while (a short burst is simply lowered; a long one would keep some rattle)
        r.strong = r.cutDb < -15.0f && (lastBuzzing - on) * g.H > (int) (0.4 * sr);
    }

    // overlap-add with the cut spread over the bins
    std::vector<double> db ((size_t) B);
    std::vector<float> acc;
    olaWithGains (x, n, a.g, a.fft, std::max (0, on - 4), on, [&] (int f, std::vector<float>& binGain)
    {
        for (int b = 0; b < B; ++b) db[(size_t) b] = cut[(size_t) b][(size_t) f];
        bandsToBins (a.bands, db, g.N, sr, binGain);
    }, acc);
    r.fixFrom = g.start (on);
    out.assign (x, x + n);
    // from the first changed frame on: the overlap-add (before it, and after the last changed frame, the
    // recording itself, sample for sample)
    const int to = r.fixEnd > 0 ? std::min (n, r.fixEnd) : n;
    double ed = 0, ex = 0;
    for (int i = std::max (0, r.fixFrom); i < to; ++i)
    {
        out[(size_t) i] = acc[(size_t) i];
        const double d = (double) out[(size_t) i] - x[i];
        ed += d * d; ex += (double) x[i] * x[i];
    }
    r.changeDb = (float) (toDb (ed) - toDb (ex));
    if (r.changeDb < -40.0f) { out.clear(); return false; }   // found, but it changes nothing you could hear: leave it
    return true;
}

// detect + fix in one go (one analysis). out is empty when there is nothing to fix
inline Report check (const float* x, int n, double sr, double f0, std::vector<float>& out)
{
    Report r;
    if (! repair (x, n, sr, f0, r, out)) out.clear();
    return r;
}

//==============================================================================
// A strong buzz: the tail of a clean take of the same note (same string and fret; another layer or the other
// stroke direction) instead. The clean start of the buzzing take is kept up to `splice`; from there the donor
// takes over at the moment where it sounds most like the buzzing take did (same balance of bands), brought to
// the same pitch (both were measured), level and phase (the fundamental lined up), with a 40 ms crossfade.
// The donor's tail is a real string: its beating, decay and noise floor are real.
struct Take
{
    const float* x = nullptr;
    int n = 0;
    double sr = 44100.0, f0 = 0.0;
    int cleanUntil = 0;   // the take is clean before this (its own buzz, or n)
};

inline bool borrowTail (const Take& bad, int splice, const Take& donor, std::vector<float>& out, float* quality = nullptr)
{
    using namespace detail;
    out.clear();
    if (bad.x == nullptr || donor.x == nullptr || ! (bad.f0 > 20.0) || ! (donor.f0 > 20.0)) return false;
    if (std::abs (bad.sr - donor.sr) > 1.0 || std::abs (std::log2 (donor.f0 / bad.f0)) > 0.025) return false;   // same note (30 cents), same rate
    const double sr = bad.sr;
    if (splice < (int) (0.15 * sr) || splice > bad.n) return false;

    // the donor at the bad take's pitch
    const double speed = donor.f0 / bad.f0;   // donor samples per output sample
    std::vector<float> d;
    {
        const int m = (int) ((donor.n - 3) / speed);
        d.resize ((size_t) std::max (0, m));
        for (int i = 0; i < m; ++i)
        {
            const double pos = i * speed;
            const int k = (int) pos; const float t = (float) (pos - k);
            const float xm1 = k > 0 ? donor.x[k - 1] : 0.0f, x0 = donor.x[k], x1 = donor.x[k + 1], x2 = donor.x[k + 2];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            d[(size_t) i] = ((c3 * t + c2) * t + c1) * t + x0;
        }
    }
    const int dClean = (int) std::min<double> ((double) d.size(), donor.cleanUntil / speed);
    if (dClean < (int) (0.6 * sr)) return false;

    // where in the donor: the frame whose band balance (up to 6 kHz) is closest to the bad take just before the splice
    Analysis A (bad.x, std::min (bad.n, splice), sr, bad.f0), D (d.data(), dClean, sr, bad.f0);
    int fb = A.g.F - 1; while (fb > 0 && A.g.start (fb) + A.g.N > splice) --fb;
    if (fb < A.fa + 2) return false;
    std::vector<double> want ((size_t) A.bands.size());
    std::vector<bool> use ((size_t) A.bands.size());
    int used = 0;
    double top = -300.0;
    for (int b = 0; b < A.bands.size(); ++b)
    {
        double e = 0; for (int f = fb - 2; f <= fb; ++f) e += toPow (A.L[(size_t) b][(size_t) f]);
        want[(size_t) b] = toDb (e / 3.0);
        top = std::max (top, want[(size_t) b]);
    }
    // the bands that carry the note (the noise floor of a take cut at the splice is not known: 45 dB under the loudest)
    for (int b = 0; b < A.bands.size(); ++b)
    {
        use[(size_t) b] = A.bands.centre[(size_t) b] < 6000.0 && want[(size_t) b] > top - 45.0;
        if (use[(size_t) b]) ++used;
    }
    if (used < 3) return false;
    int bestF = -1; double bestDist = 1e30, bestG = 0;
    for (int f = D.fa + 1; f <= D.fe - 4; ++f)
    {
        double mean = 0; int c = 0;
        for (int b = 0; b < A.bands.size(); ++b) if (use[(size_t) b]) { mean += want[(size_t) b] - D.L[(size_t) b][(size_t) f]; ++c; }
        mean /= c;
        double dist = 0;
        for (int b = 0; b < A.bands.size(); ++b) if (use[(size_t) b]) { const double e = want[(size_t) b] - D.L[(size_t) b][(size_t) f] - mean; dist += e * e; }
        dist /= c;
        // the same tone, and if possible the same level (a quiet moment turned up would turn its noise floor up too)
        dist += 0.02 * mean * mean;
        if (std::abs (mean) <= 12.0 && dist < bestDist) { bestDist = dist; bestF = f; bestG = mean; }
    }
    if (bestF < 0) return false;

    // the donor's tone at that moment brought to the bad take's (each band; cut up to 12 dB, lift up to 6 dB;
    // bands the bad take had nearly nothing in are only cut): the tail then goes on from the same tone
    {
        const int B = A.bands.size();
        std::vector<double> have ((size_t) B), eq ((size_t) B);
        double mean = 0; int c = 0;
        for (int b = 0; b < B; ++b)
        {
            double e = 0; for (int f = bestF - 1; f <= bestF + 1; ++f) e += toPow (D.L[(size_t) b][(size_t) f]);
            have[(size_t) b] = toDb (e / 3.0);
            if (use[(size_t) b]) { mean += want[(size_t) b] - have[(size_t) b]; ++c; }
        }
        mean /= std::max (1, c);
        for (int b = 0; b < B; ++b)
        {
            double v = want[(size_t) b] - have[(size_t) b] - mean;
            if (! use[(size_t) b]) v = std::min (0.0, v);
            eq[(size_t) b] = std::clamp (v, -12.0, 6.0);
        }
        std::vector<double> sm (eq);
        for (int b = 1; b + 1 < B; ++b) sm[(size_t) b] = 0.25 * eq[(size_t) (b - 1)] + 0.5 * eq[(size_t) b] + 0.25 * eq[(size_t) (b + 1)];
        const Grid gd ((int) d.size(), sr);
        std::vector<float> bin, eqd;
        bandsToBins (A.bands, sm, gd.N, sr, bin);
        olaWithGains (d.data(), (int) d.size(), gd, A.fft, 0, 0, [&] (int, std::vector<float>& b2) { b2 = bin; }, eqd);
        d.swap (eqd);
    }

    // the matching moment in the donor (frame centre) lines up with the bad take's frame centre
    const int badC = A.g.start (fb) + A.g.N / 2, donC = D.g.start (bestF) + D.g.N / 2;
    int offset = donC - badC;   // donor index = bad index + offset

    // fine: the waveforms lined up (+- one period) over the 3 periods before the splice
    const int T = (int) std::lround (sr / bad.f0);
    const int L = std::max (3 * T, (int) (0.03 * sr));
    const int x0 = splice - L;
    double bestR = -2.0; int bestLag = 0;
    for (int lag = -T; lag <= T; ++lag)
    {
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = x0; i < splice; ++i)
        {
            const int k = i + offset + lag;
            if (k < 0 || k >= (int) d.size()) { sxy = 0; sxx = 1; syy = 1; break; }
            sxy += (double) bad.x[i] * d[(size_t) k]; sxx += (double) bad.x[i] * bad.x[i]; syy += (double) d[(size_t) k] * d[(size_t) k];
        }
        const double rr = sxy / std::sqrt (sxx * syy + 1e-30);
        if (rr > bestR) { bestR = rr; bestLag = lag; }
    }
    offset += bestLag;
    if (quality != nullptr) *quality = (float) bestR;
    if (bestR < 0.6) return false;   // too different to hide the join

    // level over the same stretch
    double eb = 0, ed = 0;
    for (int i = x0; i < splice; ++i) { eb += (double) bad.x[i] * bad.x[i]; const double v = d[(size_t) (i + offset)]; ed += v * v; }
    const float G = (float) std::sqrt (eb / std::max (1e-30, ed));
    if (! (G > 0.25f && G < 4.0f)) return false;   // within 12 dB
    (void) bestG;

    const int xf = (int) (0.04 * sr);
    const int len = (int) d.size() - offset;
    if (len <= splice) return false;
    out.resize ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        const float dv = i + offset >= 0 && i + offset < (int) d.size() ? G * d[(size_t) (i + offset)] : 0.0f;
        if (i < splice - xf) out[(size_t) i] = bad.x[i];
        else if (i < splice)
        {
            const float u = (float) (i - (splice - xf)) / (float) xf, w = u * u * (3.0f - 2.0f * u);
            out[(size_t) i] = bad.x[i] * (1.0f - w) + dv * w;
        }
        else out[(size_t) i] = dv;
    }
    return true;
}
} // namespace buzz
