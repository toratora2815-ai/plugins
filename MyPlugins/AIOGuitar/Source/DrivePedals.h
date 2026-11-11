#pragma once
// AIO Guitar の DRIVE A / B で選べる歪みのうち、AIO Guitar だけのもの（0.6.0）。どれも定番の回路をそのまま式にしたオリジナル:
//  - HEARTH DRIVE … 中域が前に出るオーバードライブ（TS 系）。TS808 の回路: クリップ段（51k＋DRIVE 500k、51 pF、帰還のダイオード 2 本、
//                   アースの 4.7k＋0.047 µF = 720 Hz より上だけ増幅）→ 1k＋0.22 µF → トーン段（20k、220 Ω＋0.22 µF、帰還 1k）
//  - BRASS BOOST  … トレブルブースター。ゲルマニウム・トランジスタ 1 石（エミッター 3.9k＋47 µF、コレクター 10k）。入口のコンデンサーで
//                   低音を切ってから増幅。上下で非対称に歪み、強く弾くとエミッターの電圧がずれて少し潰れる（本物と同じ動き）
//  - BARN DIST    … RAT 系ディストーション。オペアンプ（遅い LM308 相当: 1 MHz・0.3 V/µs）の増幅は、アース側 47 Ω＋2.2 µF と
//                   560 Ω＋4.7 µF、帰還 DIST 100k＋100 pF。そのあと 1k とダイオード 2 本でアースへ逃がす硬いクリップ → フィルター（1.5k＋100k、3.3 nF）
// 3 台とも Porch Drive / Wool Fuzz と同じ 4 倍オーバーサンプリング（同じ設定 = 同じ遅れ。DRIVE A / B で機種を替えても遅れは変わらない）。
// 電圧の換算: デジタルの 1.0 = 1.5 V（AIO の録音は 1 音 −21 dB RMS。測った TS 系と同じ大きさから歪み始めるように合わせた）。
// TONE は 40 ms かけてなめらかに動かし、動いている間は 1 サンプルごとにフィルターを作り直す（ブロックの大きさで速さが変わらない。
// 1 次のフィルターは TPT 型 = 係数を変えても出力が跳ばない）。DSP 検査 2026-11-12 の指摘で直した
// LEVEL は dB。0 dB のときの大きさは、AIO の録音を GAIN 5・TONE 5 で通したときに PORCH DRIVE の初期値とだいたい同じ（kTrimDb、実測で決めた）。
#include <JuceHeader.h>
#include <cmath>

namespace pedal
{
namespace circ
{
    // first-order sections from an analog prototype (bilinear, prewarped at fc)
    struct OnePole
    {
        float b0 = 1, b1 = 0, a1 = 0, z = 0;
        // H(s) = (k1 s + k0) / (s/wc + 1) ... set from (num1, num0, den1, den0) of an analog H(s) = (n1 s + n0) / (d1 s + d0)
        void setAnalog (double n1, double n0, double d1, double d0, double fs, double fWarp)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, 0.45 * fs, fWarp);
            const double K = w / std::tan (w / (2.0 * fs));
            const double A0 = d1 * K + d0, A1 = d0 - d1 * K;
            b0 = (float) ((n1 * K + n0) / A0); b1 = (float) ((n0 - n1 * K) / A0); a1 = (float) (A1 / A0);
        }
        void lowpass (double fc, double fs)  { setAnalog (0.0, 1.0, 1.0 / (juce::MathConstants<double>::twoPi * fc), 1.0, fs, fc); }
        void highpass (double fc, double fs) { setAnalog (1.0 / (juce::MathConstants<double>::twoPi * fc), 0.0, 1.0 / (juce::MathConstants<double>::twoPi * fc), 1.0, fs, fc); }
        float process (float x) { const float y = b0 * x + z; z = b1 * x - a1 * y; return y; }
        void reset() { z = 0; }
    };

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), bilinear prewarped at fWarp
        void setAnalog (double n2, double n1, double n0, double d2, double d1, double d0, double fs, double fWarp)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, 0.45 * fs, fWarp);
            const double K = w / std::tan (w / (2.0 * fs)), K2 = K * K;
            const double A0 = d2 * K2 + d1 * K + d0;
            b0 = (float) ((n2 * K2 + n1 * K + n0) / A0);
            b1 = (float) ((2.0 * n0 - 2.0 * n2 * K2) / A0);
            b2 = (float) ((n2 * K2 - n1 * K + n0) / A0);
            a1 = (float) ((2.0 * d0 - 2.0 * d2 * K2) / A0);
            a2 = (float) ((d2 * K2 - d1 * K + d0) / A0);
        }
        // the same with the bilinear constant K = w / tan (w / 2fs) computed once (the tone moves every sample while it is turned)
        void setAnalogK (double n2, double n1, double n0, double d2, double d1, double d0, double K)
        {
            const double K2 = K * K, A0 = d2 * K2 + d1 * K + d0;
            b0 = (float) ((n2 * K2 + n1 * K + n0) / A0);
            b1 = (float) ((2.0 * n0 - 2.0 * n2 * K2) / A0);
            b2 = (float) ((n2 * K2 - n1 * K + n0) / A0);
            a1 = (float) ((2.0 * d0 - 2.0 * d2 * K2) / A0);
            a2 = (float) ((d2 * K2 - d1 * K + d0) / A0);
        }
        static double bilinearK (double fs, double fWarp)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, 0.45 * fs, fWarp);
            return w / std::tan (w / (2.0 * fs));
        }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };

    // a pair of silicon diodes back to back (1N914 / 1N4148)
    constexpr double kIs = 2.52e-9, kNVt = 1.752 * 0.02585;

    // first order, topology-preserving (TPT): the state is the capacitor's voltage, so a moving cutoff never makes the output jump
    struct TptOnePole
    {
        float G = 0.5f, s = 0;
        void setCutoff (double fc, double fs)
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1.0, 0.45 * fs, fc) / fs);
            G = (float) (g / (1.0 + g));
        }
        float lp (float x) { const float v = (x - s) * G, y = v + s; s = y + v; return y; }
        float hp (float x) { return x - lp (x); }
        void reset() { s = 0; }
    };

    // RAT clipper (1k into two 1N914 to ground) as a table: v = f(x) for the op-amp side voltage x, and its integral F
    // for first-order antiderivative anti-aliasing (ADAA). Built once (about 130 kB), shared by every instance
    struct DiodeClipTable
    {
        static constexpr int N = 8192;
        static constexpr double X = 8.0, R = 1.0e3;   // -8 .. +8 V (the op-amp swings +-4 V)
        double h = 2.0 * X / N;
        std::vector<double> f, F;
        DiodeClipTable() : f ((size_t) N + 1), F ((size_t) N + 1)
        {
            double v = -0.7;
            for (int k = 0; k <= N; ++k)
            {
                const double x = -X + k * h;
                for (int it = 0; it < 60; ++it)   // (x - v) / R = 2 Is sinh (v / nVt), solved all the way
                {
                    const double e = std::exp (juce::jlimit (-60.0, 60.0, v / kNVt)), ei = 1.0 / e;
                    const double g = (x - v) / R - kIs * (e - ei), gp = -1.0 / R - kIs * (e + ei) / kNVt;
                    const double st = juce::jlimit (-0.05, 0.05, -g / gp);
                    v += st;
                    if (std::abs (st) < 1.0e-13) break;
                }
                f[(size_t) k] = v;
            }
            F[0] = 0.0;
            for (int k = 0; k < N; ++k) F[(size_t) k + 1] = F[(size_t) k] + 0.5 * h * (f[(size_t) k] + f[(size_t) k + 1]);   // exact for the piecewise-linear f
        }
        static const DiodeClipTable& get() { static const DiodeClipTable t; return t; }
        void at (double x, double& fx, double& Fx) const
        {
            if (! (x > -X)) { fx = f[0]; Fx = F[0] + fx * (x + X); return; }   // also NaN: never an index outside the table
            if (x >= X)  { fx = f[(size_t) N]; Fx = F[(size_t) N] + fx * (x - X); return; }
            const double p = (x + X) / h;
            const int k = juce::jmin (N - 1, (int) p);
            const double u = p - k, f0 = f[(size_t) k], f1 = f[(size_t) k + 1];
            fx = f0 + (f1 - f0) * u;
            Fx = F[(size_t) k] + h * u * (f0 + 0.5 * (f1 - f0) * u);
        }
        double value (double x) const { double a, b; at (x, a, b); return a; }
    };
}

//==============================================================================
// shared frame: 4x oversampling, smoothed gains, per channel state
class CircuitPedal
{
public:
    static constexpr int maxCh = 2;
    virtual ~CircuitPedal() = default;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate;
        numCh = juce::jlimit (1, maxCh, numChannels);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing ((size_t) maxBlock);
        driveBuf.assign ((size_t) maxBlock * 4 + 16, 0.0f);
        toneBuf.assign ((size_t) maxBlock + 16, 0.5f);
        drive.reset (sampleRate * 4.0, 0.03);
        outGain.reset (sampleRate, 0.03);
        tone.reset (sampleRate, 0.04);   // the same speed in any block size
        drive.setCurrentAndTargetValue (driveFromKnob (0.5f));
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimDb()));
        tone.setCurrentAndTargetValue (0.5f);
        prepareCircuit();
        tonePre (0.5f); tonePost (0.5f);
        reset();
    }

    void reset()
    {
        resetCircuit();
        if (oversampling) oversampling->reset();
    }

    int latency() const { return oversampling ? (int) std::round (oversampling->getLatencyInSamples()) : 0; }

    // gain / tone 0..10, level dB (0 dB = about as loud as PORCH DRIVE's defaults)
    void setParams (float gain, float toneKnob, float levelDb, bool snap = false)
    {
        const float k = juce::jlimit (0.0f, 10.0f, gain) / 10.0f;
        const float d = driveFromKnob (k);
        const float o = juce::Decibels::decibelsToGain (levelDb + trimDb() + levelCompDb (k));
        const float t = juce::jlimit (0.0f, 10.0f, toneKnob) / 10.0f;
        if (snap) { drive.setCurrentAndTargetValue (d); outGain.setCurrentAndTargetValue (o); tone.setCurrentAndTargetValue (t); tonePre (t); tonePost (t); }
        else      { drive.setTargetValue (d);           outGain.setTargetValue (o);           tone.setTargetValue (t); }
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = juce::jmin (numChannels, numCh);
        // base rate, before the oversampling (the tone, while it moves, is set again for every sample)
        const bool toneMoving = tone.isSmoothing();
        const int tb = juce::jmin (numSamples, (int) toneBuf.size());
        for (int n = 0; n < numSamples; ++n)
        {
            if (toneMoving)
            {
                const float t = tone.getNextValue();
                if (n < tb) toneBuf[(size_t) n] = t;
                tonePre (t);
            }
            for (int ch = 0; ch < nc; ++ch) data[ch][n] = preBase (ch, data[ch][n] * voltsPerUnit);
        }

        juce::dsp::AudioBlock<float> block (data, (size_t) nc, (size_t) numSamples);
        auto os = oversampling->processSamplesUp (block);
        const size_t osN = os.getNumSamples();
        const size_t m = juce::jmin (osN, driveBuf.size());
        for (size_t n = 0; n < m; ++n) driveBuf[n] = drive.getNextValue();
        for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
            runOS ((int) ch, os.getChannelPointer (ch), (int) osN, driveBuf.data(), (int) m);
        oversampling->processSamplesDown (block);

        const float back = 1.0f / voltsPerUnit;
        for (int n = 0; n < numSamples; ++n)
        {
            if (toneMoving) tonePost (toneBuf[(size_t) juce::jmin (n, tb - 1)]);
            const float g = outGain.getNextValue() * back;
            for (int ch = 0; ch < nc; ++ch) data[ch][n] = postBase (ch, data[ch][n]) * g;
        }
    }

    float voltsPerUnit = 1.5f;   // how many volts the digital 1.0 is at the input (fitted to a measured TS-type drive: the same onset of clipping)

protected:
    virtual float driveFromKnob (float k) const = 0;      // 0..1 -> what the circuit needs (ohms, gain ...)
    virtual float trimDb() const = 0;
    virtual float levelCompDb (float) const { return 0.0f; }   // a pedal whose GAIN also makes it much louder can take some of that back
    virtual void prepareCircuit() = 0;
    virtual void resetCircuit() = 0;
    virtual void tonePre (float) {}                          // TONE 0..1 for the filters before the oversampling (every channel)
    virtual void tonePost (float) {}                         // ... and after it
    virtual float preBase (int ch, float x) = 0;             // base rate, before the oversampling
    virtual void runOS (int ch, float* d, int n, const float* driveSmoothed, int haveSmoothed) = 0;
    virtual float postBase (int ch, float x) = 0;            // base rate, after

    double sr = 48000.0;
    int numCh = 2;
    juce::SmoothedValue<float> drive, outGain, tone;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> driveBuf, toneBuf;
    float driveAt (const float* d, int have, int n) const { return n < have ? d[n] : (have > 0 ? d[have - 1] : drive.getCurrentValue()); }
};

//==============================================================================
// HEARTH DRIVE (TS 系): the TS808 circuit
class HearthDriveDSP : public CircuitPedal
{
protected:
    static constexpr double R5 = 51.0e3, C4 = 51.0e-12, R6 = 4.7e3, C5 = 0.047e-6;
    static constexpr double R8 = 1.0e3, C3 = 0.22e-6, R9 = 10.0e3, R10 = 1.0e3, R11 = 220.0, C6 = 0.22e-6, RT = 20.0e3;

    // DRIVE: even steps in dB (the feedback resistor grows x9.5 over the turn: about +20 dB at the top)
    float driveFromKnob (float k) const override { return (float) (R5 * std::pow (10.0, 0.98 * k)); }   // Rf = 51k + drive pot
    float trimDb() const override { return kTrimDb; }

    void prepareCircuit() override
    {
        const double fs4 = sr * 4.0;
        for (int ch = 0; ch < maxCh; ++ch)
        {
            inHP[ch].highpass (15.9, sr);   // 1 µF into 10k
            // ground leg current per volt: sC5 / (1 + s R6 C5)
            leg[ch].setAnalog (C5, 0.0, R6 * C5, 1.0, fs4, 720.0);
            outHP[ch].highpass (1.6, sr);   // 1 µF + 1k + the 100k level pot
        }
        gC = C4 * fs4;   // backward Euler
        toneK = circ::Biquad::bilinearK (sr, 1000.0);
    }

    void resetCircuit() override
    {
        for (int ch = 0; ch < maxCh; ++ch) { inHP[ch].reset(); leg[ch].reset(); outHP[ch].reset(); toneF[ch].reset(); vf[ch] = 0; }
    }

    // the tone stage (20k pot, linear): the (-) input side of the pot = Rb, the clip side = Ra.
    // A real TS's tone does most of its work in the last third of the turn: the knob here is spread so every part of the turn moves it
    void tonePost (float tKnob) override
    {
        const double t = tKnob;
        const double Rb = RT * std::pow (1.0 - t, 2.2) + 1.0, Ra = RT - (Rb - 1.0) + 1.0;
        const double Ga = 1.0 / Ra, Gb = 1.0 / Rb, G8 = 1.0 / R8, G9 = 1.0 / R9, Gs = Ga + Gb;
        // H(s) = G8 [Gs (1 + s R11 C6) + s C6 (1 + R10 Gb)] / ((G8 + G9 + s C3)(Gs (1 + s R11 C6) + s C6) + Ga s C6)
        const double n1 = G8 * (Gs * R11 * C6 + C6 * (1.0 + R10 * Gb)), n0 = G8 * Gs;
        const double e1 = Gs * R11 * C6 + C6, e0 = Gs;   // (Gs (1 + s R11 C6) + s C6) = e1 s + e0
        const double d2 = C3 * e1, d1 = (G8 + G9) * e1 + C3 * e0 + Ga * C6, d0 = (G8 + G9) * e0;
        for (int ch = 0; ch < maxCh; ++ch) toneF[ch].setAnalogK (0.0, n1, n0, d2, d1, d0, toneK);   // the state (z1, z2) stays
    }

    float preBase (int ch, float x) override { return inHP[ch].process (x); }

    void runOS (int ch, float* d, int n, const float* dr, int have) override
    {
        double v = vf[ch];
        auto& lg = leg[ch];
        for (int i = 0; i < n; ++i)
        {
            const double vin = d[i];
            const double ig = lg.process ((float) vin);           // the current the ground leg pulls through the feedback
            const double Gf = 1.0 / driveAt (dr, have, i);
            // feedback node: ig = v/Rf + C4 dv/dt + Id(v); backward Euler for C4, Newton for the diodes
            // (up to 8 steps: crossing zero from a clipped half can need more than 4 of the 0.1 V steps)
            const double vPrev = v;
            for (int it = 0; it < 8; ++it)
            {
                const double e = std::exp (juce::jlimit (-40.0, 40.0, v / circ::kNVt)), ei = 1.0 / e;
                const double id = circ::kIs * (e - ei), gd = circ::kIs * (e + ei) / circ::kNVt;
                const double f = ig - v * Gf - gC * (v - vPrev) - id;
                const double fp = -Gf - gC - gd;
                const double step = juce::jlimit (-0.1, 0.1, -f / fp);
                v += step;
                if (std::abs (step) < 1.0e-7) break;
            }
            d[i] = (float) (vin + v);   // the op-amp output: input + the voltage across the feedback
        }
        vf[ch] = v;
    }

    float postBase (int ch, float x) override { return outHP[ch].process (toneF[ch].process (x)); }

    static constexpr float kTrimDb = 2.6f;
    circ::OnePole inHP[maxCh], leg[maxCh], outHP[maxCh];
    circ::Biquad toneF[maxCh];
    double vf[maxCh] {}, gC = 0, toneK = 1.0;
};

//==============================================================================
// BRASS BOOST (treble booster): one germanium transistor, common emitter
class BrassBoostDSP : public CircuitPedal
{
protected:
    static constexpr double Vcc = 9.0, Rc = 10.0e3, Re = 3.9e3, Ce = 47.0e-6, beta = 80.0, Vsat = 0.15;
    static constexpr double Vb0 = 9.0 * 68.0 / 538.0;     // the 470k / 68k divider
    static constexpr double Is = 5.0e-6, nVt = 1.3 * 0.02585;

    // GAIN: how hot the guitar hits the transistor (like the guitar's volume knob / a hotter pickup)
    float driveFromKnob (float k) const override { return juce::Decibels::decibelsToGain (-30.0f + 30.0f * k); }
    float trimDb() const override { return kTrimDb; }
    // a real booster gets ~20 dB louder as it is hit harder; half of that is taken back so GAIN is mostly "how dirty"
    float levelCompDb (float k) const override { return -0.5f * (30.0f * k - 15.0f); }

    void prepareCircuit() override
    {
        // the quiet operating point: Ve / Re = (1 + 1/beta) Ic(Ve), with the same soft saturation as runOS (bisection: always lands)
        double lo = 0.0, hi = Vb0;
        for (int it = 0; it < 80; ++it)
        {
            const double ve = 0.5 * (lo + hi);
            const double f = ve / Re - collector (Vb0, ve) * (1.0 + 1.0 / beta);   // grows with ve
            (f > 0.0 ? hi : lo) = ve;
        }
        veQ = 0.5 * (lo + hi);
        icQ = collector (Vb0, veQ);
        vcQ = Vcc - icQ * Rc;
        const double fs4 = sr * 4.0;
        kE = 1.0 / (Ce * fs4);
        for (int ch = 0; ch < maxCh; ++ch)
        {
            miller[ch].lowpass (12000.0, fs4);
            outHP[ch].highpass (16.0, sr);      // 10 nF into the amp's 1M
        }
    }

    void resetCircuit() override
    {
        for (int ch = 0; ch < maxCh; ++ch) { inHP[ch].reset(); miller[ch].reset(); outHP[ch].reset(); ve[ch] = veQ; }
    }

    // collector current: the exponential, softly limited where the collector meets the emitter (saturation)
    static double collector (double vb, double e)
    {
        const double ic = Is * std::exp (juce::jlimit (-60.0, 30.0, (vb - e) / nVt));
        const double icMax = juce::jmax (1.0e-9, (Vcc - e - Vsat) / Rc);
        const double r = ic / icMax, r2 = r * r;
        return ic / std::sqrt (std::sqrt (1.0 + r2 * r2));
    }

    // TONE = the input capacitor: 0 full range (80 Hz) .. 10 the classic treble boost (3.2 kHz)
    void tonePre (float t) override
    {
        const double fc = 80.0 * std::pow (40.0, (double) t);
        for (int ch = 0; ch < maxCh; ++ch) inHP[ch].setCutoff (fc, sr);
    }

    float preBase (int ch, float x) override { return inHP[ch].hp (x); }

    void runOS (int ch, float* d, int n, const float* dr, int have) override
    {
        double e = ve[ch];
        for (int i = 0; i < n; ++i)
        {
            const double vb = Vb0 + d[i] * driveAt (dr, have, i);
            const double ic = collector (vb, e);   // saturation: the collector cannot go below the emitter (soft corner)
            e += kE * (ic * (1.0 + 1.0 / beta) - e / Re);   // the emitter capacitor (0.18 s): the bias moves when it is hit hard
            e = juce::jlimit (0.0, Vcc, e);
            const double vc = Vcc - ic * Rc;
            d[i] = miller[ch].process ((float) (vcQ - vc));   // inverting stage: + at the input pulls the collector down
        }
        ve[ch] = e;
    }

    float postBase (int ch, float x) override { return outHP[ch].process (x); }

    static constexpr float kTrimDb = -9.6f;
    circ::TptOnePole inHP[maxCh];
    circ::OnePole miller[maxCh], outHP[maxCh];
    double ve[maxCh] {}, veQ = 1.0, icQ = 0, vcQ = 6.0, kE = 0;
};

//==============================================================================
// BARN DIST (RAT 系): a slow op-amp with a lot of gain, then two diodes to ground, then the filter
class BarnDistDSP : public CircuitPedal
{
protected:
    static constexpr double R1 = 47.0, C1 = 2.2e-6, R2 = 560.0, C2 = 4.7e-6, Cf = 100.0e-12, Rd = 100.0e3;
    static constexpr double ft = 1.0e6, slew = 0.3e6, rail = 4.0, Rclip = 1.0e3, Rfil = 1.5e3, Cfil = 3.3e-9, RfilPot = 100.0e3;

    // DIST: the 100k pot (log taper: a tenth of the way at half turn)
    float driveFromKnob (float k) const override
    {
        const double t = (std::exp (4.394 * k) - 1.0) / (std::exp (4.394) - 1.0);
        return (float) (Rd * t + 10.0);
    }
    float trimDb() const override { return kTrimDb; }

    void prepareCircuit() override
    {
        const double fs4 = sr * 4.0;
        fs4c = fs4;
        for (int ch = 0; ch < maxCh; ++ch)
        {
            inHP[ch].highpass (7.0, sr);
            leg1[ch].setAnalog (C1, 0.0, R1 * C1, 1.0, fs4, 1539.0);   // current per volt: s C1 / (1 + s R1 C1)
            leg2[ch].setAnalog (C2, 0.0, R2 * C2, 1.0, fs4, 60.0);
            couple[ch].highpass (20.0, fs4);                            // 4.7 µF into the clipper
        }
        (void) circ::DiodeClipTable::get();   // built here, never on the audio thread
    }

    void resetCircuit() override
    {
        for (int ch = 0; ch < maxCh; ++ch)
        {
            inHP[ch].reset(); leg1[ch].reset(); leg2[ch].reset(); fbLP[ch] = 0; gbw[ch] = 0; slewY[ch] = 0;
            couple[ch].reset(); filt[ch].reset(); xPrev[ch] = 0; FPrev[ch] = 0;
            double fx; circ::DiodeClipTable::get().at (0.0, fx, FPrev[ch]);
        }
        rdNow = -1.0;
    }

    // TONE = the filter, turned the friendly way: 10 = open (32 kHz), 0 = closed (475 Hz)
    void tonePost (float t) override
    {
        const double r = RfilPot * (1.0 - (double) t);
        const double fc = 1.0 / (juce::MathConstants<double>::twoPi * (Rfil + r) * Cfil);
        for (int ch = 0; ch < maxCh; ++ch) filt[ch].setCutoff (fc, sr);
    }

    float preBase (int ch, float x) override { return inHP[ch].process (x); }

    void runOS (int ch, float* d, int n, const float* dr, int have) override
    {
        const double twoPiT = juce::MathConstants<double>::twoPi / fs4c, maxStep = slew / fs4c;
        const auto& clip = circ::DiodeClipTable::get();
        double lp = fbLP[ch], g = gbw[ch], y = slewY[ch], x0 = xPrev[ch], F0 = FPrev[ch];
        for (int i = 0; i < n; ++i)
        {
            const double vin = d[i];
            const double rd = driveAt (dr, have, i);
            if (std::abs (rd - rdNow) > 1.0e-4 * rdNow)
            {
                rdNow = rd;
                aF = std::exp (-1.0 / (fs4c * rd * Cf));                                     // Rd || 100 pF
                aG = std::exp (-twoPiT * ft / (1.0 + rd / (R1 * R2 / (R1 + R2))));           // gain-bandwidth / gain
            }
            // feedback: the ground legs pull a current that flows through Rd || 100 pF
            const double i12 = leg1[ch].process ((float) vin) + leg2[ch].process ((float) vin);
            lp = i12 + aF * (lp - i12);
            const double vamp = vin + rd * lp;
            // the op-amp can only give gain-bandwidth / gain
            g = vamp + aG * (g - vamp);
            // output swing (rails) and slew rate
            const double q = (g / rail) * (g / rail);
            const double r = g / std::sqrt (std::sqrt (1.0 + q * q));
            y += juce::jlimit (-maxStep, maxStep, r - y);
            // 1k into the two diodes to ground, solved exactly (the table), with first-order antiderivative
            // anti-aliasing: the average of the clipper between the last sample and this one (much less fold-back of the hard edge)
            const double x = couple[ch].process ((float) y);
            double fx, Fx;
            clip.at (x, fx, Fx);
            const double dx = x - x0;
            d[i] = (float) (std::abs (dx) > 1.0e-6 ? (Fx - F0) / dx : clip.value (0.5 * (x + x0)));
            x0 = x; F0 = Fx;
        }
        fbLP[ch] = lp; gbw[ch] = g; slewY[ch] = y; xPrev[ch] = x0; FPrev[ch] = F0;
    }

    float postBase (int ch, float x) override { return filt[ch].lp (x); }

    static constexpr float kTrimDb = -0.5f;
    double fs4c = 192000.0, rdNow = -1.0, aF = 0.0, aG = 0.0;
    circ::OnePole inHP[maxCh], leg1[maxCh], leg2[maxCh], couple[maxCh];
    circ::TptOnePole filt[maxCh];
    double fbLP[maxCh] {}, gbw[maxCh] {}, slewY[maxCh] {}, xPrev[maxCh] {}, FPrev[maxCh] {};
};
} // namespace pedal
