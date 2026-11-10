#pragma once
// ビビり補正のテスト用: 弦のモデルで「きれいな音」と「途中で縦の振れが育ってフレットに当たる音」を作る
// （tools/BuzzTest.cpp と tools/RenderTest.cpp で使う。JUCE なし）
#include <vector>
#include <complex>
#include <random>
#include <string>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace buzzsynth
{
constexpr double kPi = 3.14159265358979323846;

struct NoteSpec
{
    double sr = 48000.0;
    double f0 = 110.0;
    double len = 6.0;          // seconds
    double t60 = 6.0;          // fundamental
    double hiT60 = 0.35;       // at 5 kHz
    double B = 1.0e-4;         // inharmonicity
    double pluckPos = 0.15;
    double beat = 0.25;        // second polarisation amount (beating)
    double attackHard = 0.0;   // 0..1 extra bright attack
    double twoStage = 0.0;     // part of every partial that decays 3x slower (fast at first, then slow)
    double noiseDb = -84.0, humDb = -200.0;
    double muteAt = -1.0;      // hand mute at this time (s), -1 none
    // buzz
    double buzzAt = -1.0;      // first contact (s), -1 none
    double swell = 0.5;        // the vertical motion grows by this much (x) around the contact
    double buzzAmt = 1.0;
    double barrier = 0.97;     // where the fret is, against the vertical swing at buzzAt (lower = it keeps hitting for longer)
    unsigned seed = 1;
};

struct Note { std::vector<float> clean, buzzed; int trueOnset = -1; };

// the pickup signal of a string: partials with two polarisations each (beating), damped, inharmonic
Note makeNote (const NoteSpec& p)
{
    std::mt19937 rng (p.seed);
    std::uniform_real_distribution<double> U (0.0, 1.0);
    const int n = (int) (p.len * p.sr);
    Note r;
    r.clean.assign ((size_t) n, 0.0f);
    std::vector<double> disp ((size_t) n, 0.0);   // displacement-ish (for the contact)
    const double a1 = 6.9078 / p.t60, a5k = 6.9078 / p.hiT60;
    const double bq = (a5k - a1) / (5000.0 * 5000.0 - p.f0 * p.f0);
    for (int k = 1; ; ++k)
    {
        const double f = k * p.f0 * std::sqrt (1.0 + p.B * k * k) / std::sqrt (1.0 + p.B);
        if (f > 0.45 * p.sr || f > 16000.0) break;
        const double A = std::sin (kPi * k * p.pluckPos) / std::pow ((double) k, 1.0) * (U (rng) < 0.5 ? -1 : 1);
        const double al = a1 + bq * (f * f - p.f0 * p.f0);
        const double ph1 = U (rng) * 2 * kPi, ph2 = U (rng) * 2 * kPi;
        const double c = p.beat * (0.3 + 0.7 * U (rng));
        const double df = (0.2 + 1.3 * U (rng)) * (U (rng) < 0.5 ? -1 : 1);
        const double al2 = al * (0.8 + 0.5 * U (rng));
        const double atk = p.attackHard * std::min (1.0, f / 3000.0);   // bright attack, gone in ~60 ms
        const double w1 = 2 * kPi * f / p.sr, w2 = 2 * kPi * (f + df) / p.sr;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / p.sr;
            const double e = std::exp (-al * t) * (1.0 + 3.0 * atk * std::exp (-t / 0.02));
            const double e2 = (1.0 - p.twoStage) * e + p.twoStage * std::exp (-al * t / 3.0);
            const double v = A * (e2 * std::cos (w1 * i + ph1) + c * std::exp (-al2 * t) * std::cos (w2 * i + ph2));
            r.clean[(size_t) i] += (float) v;
            disp[(size_t) i] += A / k * std::exp (-al * t) * std::sin (w1 * i + ph1);
        }
    }
    // pick click
    {
        std::normal_distribution<double> N (0.0, 1.0);
        double lp = 0;
        for (int i = 0; i < (int) (0.003 * p.sr); ++i)
        {
            const double v = N (rng) * (0.3 + p.attackHard);
            const double hp = v - lp; lp += 0.3 * (v - lp);
            r.clean[(size_t) i] += (float) (hp * (1.0 - i / (0.003 * p.sr)));
        }
    }
    // scale: peak about -6 dBFS
    float pk = 0; for (auto v : r.clean) pk = std::max (pk, std::abs (v));
    const float g = 0.5f / std::max (1e-9f, pk);
    for (auto& v : r.clean) v *= g;
    double dpk = 0; for (auto v : disp) dpk = std::max (dpk, std::abs (v));
    for (auto& v : disp) v /= dpk;

    r.buzzed = r.clean;
    if (p.buzzAt > 0)
    {
        // the vertical motion grows from 0.35 s before the first contact (the pickup sees it too)
        const double ts = p.buzzAt - 0.35;
        auto swellAt = [&] (double t) { const double u = std::clamp ((t - ts) / 0.5, 0.0, 1.0); return 1.0 + p.swell * u * u * (3 - 2 * u); };
        // the VERTICAL part of the motion (the one that can hit a fret) starts small and grows as the plane of the
        // vibration turns: it reaches the fret (the barrier) at buzzAt, never before
        auto vertAt = [&] (double t) { const double u = std::clamp ((t - (p.buzzAt - 0.6)) / 0.7, 0.0, 1.0); return 0.01 + 1.1 * u * u * (3 - 2 * u); };
        const int ib = (int) (p.buzzAt * p.sr);
        double env = 0; for (int i = ib - (int) (0.02 * p.sr); i < ib + (int) (0.02 * p.sr); ++i) env = std::max (env, -disp[(size_t) i] * vertAt (i / p.sr));
        const double barrier = env * p.barrier;
        // contact force -> a rattle: two damped resonances (fret / neck) + the click itself
        double y1 = 0, y2 = 0, z1 = 0, z2 = 0, prevF = 0;
        const double fr1 = 3400.0, fr2 = 6100.0, d1 = std::exp (-2 * kPi * 500 / p.sr), d2 = std::exp (-2 * kPi * 900 / p.sr);
        const double c1 = 2 * d1 * std::cos (2 * kPi * fr1 / p.sr), c2 = 2 * d2 * std::cos (2 * kPi * fr2 / p.sr);
        int first = -1;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / p.sr;
            const double s = swellAt (t);
            if (t > ts) r.buzzed[(size_t) i] = (float) (r.clean[(size_t) i] * s);
            const double d = -disp[(size_t) i] * vertAt (t);
            // how hard it hits: the part that goes past the fret, relative to the fret's height (so the rattle follows the string's level)
            const double F = d > barrier ? barrier * std::pow ((d - barrier) / barrier, 1.5) * 150.0 * p.buzzAmt : 0.0;
            if (F > 0 && first < 0) first = i;
            const double x = F - prevF; prevF = F;
            const double ny1 = x + c1 * y1 - d1 * d1 * y2; y2 = y1; y1 = ny1;
            const double ny2 = x + c2 * z1 - d2 * d2 * z2; z2 = z1; z1 = ny2;
            r.buzzed[(size_t) i] += (float) (0.5 * (0.6 * y1 + 0.4 * z1 + 2.0 * x) * g);
        }
        r.trueOnset = first;
    }
    // noise floor, hum, hand mute (all versions alike)
    std::normal_distribution<double> N (0.0, 1.0);
    const double nA = std::pow (10.0, p.noiseDb / 20.0), hA = std::pow (10.0, p.humDb / 20.0);
    std::vector<double> mute ((size_t) n, 1.0);
    if (p.muteAt > 0)
        for (int i = (int) (p.muteAt * p.sr); i < n; ++i) mute[(size_t) i] = std::exp (-(i / p.sr - p.muteAt) * 6.9 / 0.06);
    for (int i = 0; i < n; ++i)
    {
        const double nz = nA * N (rng) + hA * (std::sin (2 * kPi * 50 * i / p.sr) + 0.3 * std::sin (2 * kPi * 150 * i / p.sr));
        double click = 0;
        if (p.muteAt > 0) { const double dt = i / p.sr - p.muteAt; if (dt >= 0 && dt < 0.012) click = 0.02 * N (rng) * (1 - dt / 0.012); }
        r.clean[(size_t) i]  = (float) (r.clean[(size_t) i] * mute[(size_t) i] + nz + click);
        r.buzzed[(size_t) i] = (float) (r.buzzed[(size_t) i] * mute[(size_t) i] + nz + click);
    }
    // like the slicer: 3 ms fade in, 80 ms fade out
    for (auto* v : { &r.clean, &r.buzzed })
    {
        const int fin = (int) (0.003 * p.sr), fout = (int) (0.08 * p.sr);
        for (int i = 0; i < fin; ++i) (*v)[(size_t) i] *= (float) i / fin;
        for (int i = 0; i < fout; ++i) (*v)[(size_t) (n - 1 - i)] *= (float) i / fout;
    }
    return r;
}

void writeWav (const std::string& path, const std::vector<float>& x, double sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + (uint32_t) x.size() * 4); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (3); u16 (1); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (32);
    f.write ("data", 4); u32 ((uint32_t) x.size() * 4);
    f.write ((const char*) x.data(), (std::streamsize) (x.size() * 4));
}

// energy (dB) above / below a frequency in [a, b) with a one-shot DFT-free filter: 4th-order high-pass
double bandDb (const std::vector<float>& x, int a, int b, double sr, double fc, bool high)
{
    a = std::max (0, a); b = std::min ((int) x.size(), b);
    if (b <= a) return -200;
    // two cascaded 2nd-order Butterworth sections (bilinear)
    const double K = std::tan (kPi * fc / sr), q = 0.70710678;
    const double norm = 1 / (1 + K / q + K * K);
    double b0, b1, b2;
    if (high) { b0 = norm; b1 = -2 * norm; b2 = norm; } else { b0 = K * K * norm; b1 = 2 * b0; b2 = b0; }
    const double a1 = 2 * (K * K - 1) * norm, a2 = (1 - K / q + K * K) * norm;
    double s1[4] {}, s2[4] {}, e = 0;
    const int start = std::max (0, a - (int) (0.05 * sr));
    for (int i = start; i < b; ++i)
    {
        double v = x[(size_t) i];
        double y = b0 * v + b1 * s1[0] + b2 * s1[1] - a1 * s1[2] - a2 * s1[3];
        s1[1] = s1[0]; s1[0] = v; s1[3] = s1[2]; s1[2] = y; v = y;
        y = b0 * v + b1 * s2[0] + b2 * s2[1] - a1 * s2[2] - a2 * s2[3];
        s2[1] = s2[0]; s2[0] = v; s2[3] = s2[2]; s2[2] = y;
        if (i >= a) e += y * y;
    }
    return 10 * std::log10 (e / (b - a) + 1e-30);
}
double rmsDb (const std::vector<float>& x, int a, int b)
{
    a = std::max (0, a); b = std::min ((int) x.size(), b);
    double e = 0; for (int i = a; i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
    return 10 * std::log10 (e / std::max (1, b - a) + 1e-30);
}
// the largest "click": 2nd difference compared with the 100 ms before
// the largest "click" (2nd difference) around `at`, compared with the clean string at the same place
double clickRatioDb (const std::vector<float>& x, const std::vector<float>& ref, int at, int halfWin)
{
    auto d2 = [] (const std::vector<float>& v, int i) { return std::abs ((double) v[(size_t) i] - 2.0 * v[(size_t) (i - 1)] + v[(size_t) (i - 2)]); };
    double a = 0, b = 0;
    for (int i = std::max (2, at - halfWin); i < std::min ((int) x.size(), at + halfWin); ++i) { a = std::max (a, d2 (x, i)); b = std::max (b, d2 (ref, i)); }
    return 20 * std::log10 ((a + 1e-12) / (b + 1e-12));
}

// events that are NOT a buzz (from the DSP inspection 2026-11-10): clicks, slow hand mutes, a lifted finger (the open
// string rings on), the pick touching the string again; both polarisations start in phase
struct Gen
{
    double sr = 48000, f0 = 110, len = 6, t60 = 5, hiT60 = 0.6, B = 1e-4, pluck = 0.15;
    // second polarisation: amplitude c, detune: proportional (cents) or random (Hz)
    double c = 0.3, cents = 0.0, randHz = 0.0, decay2 = 1.0;   // decay2: alpha2 = alpha * decay2
    double twoStage = 0.0;
    double glideCents = 0.0, glideTau = 0.1;
    double noiseDb = -84, humDb = -200;           // hum: 60 Hz with harmonics up to 4 kHz (single coil)
    double clickAt = -1, clickDb = -30, clickMs = 0.0;  // 0 = one sample
    double muteAt = -1, muteTau = 0.05;           // fall time constant (s)
    double releaseAt = -1, openDb = -12, fretSemis = 5;  // finger lifted: the open string rings
    double repickAt = -1, repickDb = -18;         // pick touches the ringing string again
    unsigned seed = 1;
};

std::vector<float> gen (const Gen& p)
{
    std::mt19937 rng (p.seed);
    std::uniform_real_distribution<double> U (0, 1);
    std::normal_distribution<double> N (0, 1);
    const int n = (int) (p.len * p.sr);
    std::vector<double> y ((size_t) n, 0.0);
    auto addString = [&] (double f0, double startT, double amp, double brightAtk)
    {
        const double a1 = 6.9078 / p.t60, a5k = 6.9078 / p.hiT60;
        const double bq = (a5k - a1) / (5000.0 * 5000.0 - f0 * f0);
        const int i0 = (int) (startT * p.sr);
        for (int k = 1; ; ++k)
        {
            const double f = k * f0 * std::sqrt (1.0 + p.B * k * k) / std::sqrt (1.0 + p.B);
            if (f > 0.45 * p.sr || f > 16000.0) break;
            const double A = amp * std::sin (kPi * k * p.pluck) / k * (1 + brightAtk * std::min (1.0, f / 3000.0));
            const double al = std::max (0.05, a1 + bq * (f * f - f0 * f0));
            double f2 = f * std::pow (2.0, p.cents / 1200.0) + p.randHz * (U (rng) * 2 - 1);
            const double ph = U (rng) * 2 * kPi;
            // both polarisations start in phase (one pluck)
            std::complex<double> z1 = std::polar (1.0, ph), z2 = std::polar (1.0, ph);
            const double d1 = std::exp (-al / p.sr), d1s = std::exp (-al / 3.0 / p.sr), d2 = std::exp (-al * p.decay2 / p.sr);
            double e1 = 1, e1s = 1, e2 = 1;
            const bool glide = p.glideCents != 0.0;
            double phase1 = ph, phase2 = ph;
            const std::complex<double> w1 = std::polar (1.0, 2 * kPi * f / p.sr), w2 = std::polar (1.0, 2 * kPi * f2 / p.sr);
            for (int i = i0; i < n; ++i)
            {
                double v1, v2;
                if (glide)
                {
                    const double t = (i - i0) / p.sr;
                    const double g = std::pow (2.0, p.glideCents * std::exp (-t / p.glideTau) / 1200.0);
                    phase1 += 2 * kPi * f * g / p.sr; phase2 += 2 * kPi * f2 * g / p.sr;
                    v1 = std::cos (phase1); v2 = std::cos (phase2);
                }
                else { v1 = z1.real(); v2 = z2.real(); z1 *= w1; z2 *= w2; }
                const double env = (1 - p.twoStage) * e1 + p.twoStage * e1s;
                y[(size_t) i] += A * (env * v1 + p.c * e2 * v2);
                e1 *= d1; e1s *= d1s; e2 *= d2;
                if (! glide && ((i - i0) & 1023) == 0) { z1 /= std::abs (z1); z2 /= std::abs (z2); }
            }
        }
        // pick click
        double lp = 0;
        for (int i = 0; i < (int) (0.003 * p.sr) && i0 + i < n; ++i)
        {
            const double v = N (rng) * 0.3 * amp; const double hp = v - lp; lp += 0.3 * (v - lp);
            y[(size_t) (i0 + i)] += hp * (1.0 - i / (0.003 * p.sr));
        }
    };
    addString (p.f0, 0.0, 1.0, 0.0);
    double pk = 0; for (auto v : y) pk = std::max (pk, std::abs (v));
    const double g = 0.5 / std::max (1e-9, pk);
    for (auto& v : y) v *= g;
    // events (in the scaled domain)
    if (p.releaseAt > 0)
    {
        // the fretted note dies in ~20 ms, the open string (lower) rings on
        const int ir = (int) (p.releaseAt * p.sr);
        for (int i = ir; i < n; ++i) y[(size_t) i] *= std::exp (-(i - ir) / p.sr / 0.02);
        std::vector<double> keep = y;
        std::fill (y.begin(), y.end(), 0.0);
        const double amp = 0.5 * std::pow (10.0, p.openDb / 20.0);
        // reuse addString on a fresh buffer
        addString (p.f0 / std::pow (2.0, p.fretSemis / 12.0), p.releaseAt, 1.0, 0.5);
        double pk2 = 0; for (int i = ir; i < n; ++i) pk2 = std::max (pk2, std::abs (y[(size_t) i]));
        for (int i = 0; i < n; ++i) y[(size_t) i] = keep[(size_t) i] + (i >= ir ? y[(size_t) i] * amp / std::max (1e-9, pk2) : 0.0);
        // fret click
        for (int i = ir; i < std::min (n, ir + (int) (0.004 * p.sr)); ++i) y[(size_t) i] += 0.01 * N (rng);
    }
    if (p.repickAt > 0)
    {
        std::vector<double> keep = y;
        std::fill (y.begin(), y.end(), 0.0);
        addString (p.f0, p.repickAt, 1.0, 1.0);
        const int ir = (int) (p.repickAt * p.sr);
        double pk2 = 0; for (int i = ir; i < n; ++i) pk2 = std::max (pk2, std::abs (y[(size_t) i]));
        const double amp = 0.5 * std::pow (10.0, p.repickDb / 20.0);
        for (int i = 0; i < n; ++i) y[(size_t) i] = keep[(size_t) i] + (i >= ir ? y[(size_t) i] * amp / std::max (1e-9, pk2) : 0.0);
    }
    if (p.muteAt > 0)
    {
        const int im = (int) (p.muteAt * p.sr);
        for (int i = im; i < n; ++i) y[(size_t) i] *= std::exp (-(i - im) / p.sr / p.muteTau);
        for (int i = im; i < std::min (n, im + (int) (0.01 * p.sr)); ++i) y[(size_t) i] += 0.004 * N (rng);
    }
    if (p.clickAt > 0)
    {
        const int ic = (int) (p.clickAt * p.sr);
        const double a = 0.5 * std::pow (10.0, p.clickDb / 20.0);
        const int len = std::max (1, (int) (p.clickMs * 0.001 * p.sr));
        for (int i = 0; i < len && ic + i < n; ++i) y[(size_t) (ic + i)] += a * (len == 1 ? 1.0 : N (rng) * (1.0 - (double) i / len));
    }
    const double nA = std::pow (10.0, p.noiseDb / 20.0), hA = std::pow (10.0, p.humDb / 20.0);
    for (int i = 0; i < n; ++i)
    {
        double hum = 0;
        if (p.humDb > -150)
            for (int h = 1; h * 60 < 4000; ++h) hum += std::sin (2 * kPi * 60 * h * i / p.sr) / h;
        y[(size_t) i] += nA * N (rng) + hA * hum;
    }
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) y[(size_t) i];
    const int fin = (int) (0.003 * p.sr), fout = std::min (n / 3, (int) (0.08 * p.sr));
    for (int i = 0; i < fin; ++i) x[(size_t) i] *= (float) i / fin;
    for (int i = 0; i < fout; ++i) x[(size_t) (n - 1 - i)] *= (float) i / fout;
    return x;
}


// a slowly beating string (the two polarisations a few cents apart), optionally fast-then-slow; `fund` = its fundamental
std::vector<float> beatingString (double sr, double f0, double len, double cents, double c, double twoStage, unsigned seed, std::vector<float>* fund)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> U (0, 1);
    const int n = (int) (len * sr);
    std::vector<double> y ((size_t) n, 0.0), fu ((size_t) n, 0.0);
    const double a1 = 6.9078 / 5.0, a5k = 6.9078 / 0.6, bq = (a5k - a1) / (5000.0 * 5000.0 - f0 * f0);
    for (int k = 1; ; ++k)
    {
        const double f = k * f0 * std::sqrt (1 + 1e-4 * k * k) / std::sqrt (1 + 1e-4);
        if (f > 0.45 * sr) break;
        const double A = std::sin (kPi * k * 0.15) / k, al = std::max (0.05, a1 + bq * (f * f - f0 * f0));
        const double f2 = f * std::pow (2.0, cents / 1200.0), ph = U (rng) * 2 * kPi;
        std::complex<double> z1 = std::polar (1.0, ph), z2 = z1;
        const std::complex<double> w1 = std::polar (1.0, 2 * kPi * f / sr), w2 = std::polar (1.0, 2 * kPi * f2 / sr);
        const double d1 = std::exp (-al / sr), d1s = std::exp (-al / 3 / sr);
        double e1 = 1, e1s = 1;
        for (int i = 0; i < n; ++i)
        {
            const double env = (1 - twoStage) * e1 + twoStage * e1s;
            y[(size_t) i] += A * env * (z1.real() + c * z2.real());
            if (k == 1) fu[(size_t) i] = env * z1.real();
            z1 *= w1; z2 *= w2; e1 *= d1; e1s *= d1s;
            if ((i & 1023) == 0) { z1 /= std::abs (z1); z2 /= std::abs (z2); }
        }
    }
    double pk = 0; for (auto v : y) pk = std::max (pk, std::abs (v));
    std::vector<float> x ((size_t) n);
    std::normal_distribution<double> N (0, 1);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * y[(size_t) i] / pk + 6e-5 * N (rng));
    if (fund) { fund->resize ((size_t) n); for (int i = 0; i < n; ++i) (*fund)[(size_t) i] = (float) (fu[(size_t) i]); }
    const int fin = (int) (0.003 * sr), fout = (int) (0.08 * sr);
    for (int i = 0; i < fin; ++i) x[(size_t) i] *= (float) i / fin;
    for (int i = 0; i < fout; ++i) x[(size_t) (n - 1 - i)] *= (float) i / fout;
    return x;
}

} // namespace buzzsynth
