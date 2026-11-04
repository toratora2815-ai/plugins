#pragma once
// 声 → MIDI の心臓部。JUCE に頼らない素の C++ なので、単体テストでもそのまま使える。
//  - MELODY: 鼻歌の音の高さ（YIN 法）を測って、音階に合わせた MIDI ノートにする
//  - DRUMS : 口ドラムの「鳴った瞬間」を見つけて、低い音・高い音の割合で KICK / SNARE / HAT に分ける
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hum
{

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
    void setLP (double fs, double f, double q)
    {
        const double w = 2 * M_PI * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHP (double fs, double f, double q)
    {
        const double w = 2 * M_PI * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// 1 つのスレッドが書き、別の 1 つが読むだけの輪（音の処理 → 画面）
template <typename T, int N>
struct Ring
{
    std::array<T, N> d {};
    std::atomic<int> w { 0 }, r { 0 };
    bool push (const T& v)
    {
        const int wi = w.load (std::memory_order_relaxed), nx = (wi + 1) % N;
        if (nx == r.load (std::memory_order_acquire)) return false;
        d[(size_t) wi] = v;
        w.store (nx, std::memory_order_release);
        return true;
    }
    bool pop (T& v)
    {
        const int ri = r.load (std::memory_order_relaxed);
        if (ri == w.load (std::memory_order_acquire)) return false;
        v = d[(size_t) ri];
        r.store ((ri + 1) % N, std::memory_order_release);
        return true;
    }
};

struct MidiSink
{
    virtual ~MidiSink() = default;
    virtual void send (uint8_t status, uint8_t d1, uint8_t d2) = 0;
};

//==============================================================================
// 音階（キーからの半音のずれ）
enum Scale { scChromatic, scMajor, scMinor, scHarmonicMinor, scDorian, scMixolydian, scMajorPenta, scMinorPenta, scBlues, scNumScales };

inline uint16_t scaleMask (int s)
{
    auto m = [] (std::initializer_list<int> l) { uint16_t r = 0; for (int i : l) r |= (uint16_t) (1u << i); return r; };
    switch (s)
    {
        case scMajor:         return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case scMinor:         return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case scHarmonicMinor: return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case scDorian:        return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case scMixolydian:    return m ({ 0, 2, 4, 5, 7, 9, 10 });
        case scMajorPenta:    return m ({ 0, 2, 4, 7, 9 });
        case scMinorPenta:    return m ({ 0, 3, 5, 7, 10 });
        case scBlues:         return m ({ 0, 3, 5, 6, 7, 10 });
        default:            return 0x0fff;
    }
}

inline bool inScale (int note, int key, int scale)
{
    return (scaleMask (scale) >> (((note - key) % 12 + 12) % 12)) & 1;
}

// 一番近い音階の音（同じ距離なら低い方）
inline int snapToScale (float m, int key, int scale)
{
    const int base = (int) std::lround (m);
    int best = base; float bestD = 1e9f;
    for (int k = -6; k <= 6; ++k)
    {
        const int n = base + k;
        if (! inScale (n, key, scale)) continue;
        const float dd = std::abs (m - (float) n);
        if (dd < bestD - 1e-4f) { bestD = dd; best = n; }
    }
    return best;
}

//==============================================================================
struct Params
{
    std::atomic<bool>  enabled { true };
    std::atomic<int>   mode { 0 };           // 0 = MELODY, 1 = DRUMS
    std::atomic<int>   key { 0 };            // 0 = C
    std::atomic<int>   scale { scMajor };
    std::atomic<int>   octave { 0 };         // -2..+2
    std::atomic<float> gateDb { -45.0f };    // これより小さい音は無視
    std::atomic<float> glueMs { 35.0f };     // 同じ高さが何 ms 続いたら音にするか
    std::atomic<float> holdMs { 70.0f };     // 声が切れて何 ms で音を止めるか
    std::atomic<bool>  dynamicVel { true };
    std::atomic<bool>  bend { false };       // 音程の細かいゆれをピッチベンドで送る（±2 半音）
    std::atomic<int>   channel { 1 };        // 1..16
    std::atomic<float> drumSens { 9.0f };    // 何 dB 急に大きくなったら「叩いた」とみなすか（小さいほど敏感）
    std::atomic<int>   drumNote[3];
    std::atomic<float> proto[3][2];          // KICK / SNARE / HAT の特徴（低音の割合・高音の割合、log10）

    Params()
    {
        const int notes[3] = { 36, 38, 42 };
        for (int i = 0; i < 3; ++i) drumNote[i].store (notes[i]);
        resetProtos();
    }
    static constexpr float defaultProto[3][2] = { { -0.4f, -2.5f }, { -1.9f, -0.9f }, { -2.5f, -0.06f } };
    void resetProto (int c) { proto[c][0].store (defaultProto[c][0]); proto[c][1].store (defaultProto[c][1]); }
    void resetProtos() { for (int c = 0; c < 3; ++c) resetProto (c); }
};

struct Frame
{
    float pitch = -1;   // 歌っている高さ（MIDI 番号の小数、-1 = 声なし）
    float level = -100; // dBFS
    int   note = -1;    // 鳴らしているノート
};

struct Hit { int cls; int velocity; };

//==============================================================================
class Engine
{
public:
    Params p;
    Ring<Frame, 4096> frames;   // 画面用（約 2.7 ms ごと）
    Ring<Hit, 256>    hits;     // 画面用（ドラム）
    std::atomic<int>  currentNote { -1 };
    std::atomic<int>  learnClass { -1 };  // 0..2 = その音を覚えている途中
    std::atomic<int>  learnCount { 0 };
    std::atomic<bool> panicRequest { false };
    static constexpr int learnShots = 4;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        D = std::max (1, (int) std::lround (fs / 12000.0));
        fsd = fs / D;
        W = (int) std::lround (fsd * 0.016);              // 16 ms の窓
        tauMin = std::max (2, (int) (fsd / 1200.0));
        tauMax = (int) std::ceil (fsd / 70.0);              // 70 Hz まで
        span = W + tauMax + 2;
        ring.assign ((size_t) span * 2, 0.0f);
        ringPos = 0;
        hopD = std::max (1, (int) std::lround (fsd * 0.0027));
        hopCount = 0; decimCount = 0;
        diff.assign ((size_t) tauMax + 2, 0.0f);
        cmnd.assign ((size_t) tauMax + 2, 0.0f);

        hpIn.setHP (fs, 50.0, 0.707); hpIn.reset();
        const double fc = std::min (4000.0, 0.42 * fsd);
        aa1.setLP (fs, fc, 0.5412); aa2.setLP (fs, fc, 1.3066); aa1.reset(); aa2.reset();
        lowF.setLP (fs, 150.0, 0.707); lowF2.setLP (fs, 150.0, 0.707);
        highF.setHP (fs, 7000.0, 0.707); highF2.setHP (fs, 7000.0, 0.707);
        hbF.setHP (fs, 3000.0, 0.707); hbF.reset(); hFast = hSlow = 0;
        bCoef = std::exp (-1.0 / (fs * 0.008)); bLow = bHigh = bAll = 0;
        lowF.reset(); lowF2.reset(); highF.reset(); highF2.reset();

        levelCoef = std::exp (-1.0 / (fs * 0.010));
        fastAtt = std::exp (-1.0 / (fs * 0.0003)); fastRel = std::exp (-1.0 / (fs * 0.010));
        slowAtt = std::exp (-1.0 / (fs * 0.015));  slowRel = std::exp (-1.0 / (fs * 0.060));
        ms2 = 0; fastEnv = slowEnv = 0;
        resetMelody(); resetDrums();
    }

    void process (const float* x, int n, MidiSink& out)
    {
        if (panicRequest.exchange (false)) allOff (out);

        const bool on = p.enabled.load();
        const int mode = p.mode.load();
        if (! on || mode != lastMode) { stopMelodyNote (out); lastMode = mode; }
        const int ch = std::clamp (p.channel.load(), 1, 16) - 1;

        for (int i = 0; i < n; ++i)
        {
            const float xh = hpIn.process (x[i]);
            ms2 = levelCoef * ms2 + (1 - levelCoef) * (double) xh * xh;

            if (on && mode == 1) drumSample (xh, out, ch);
            tickDrumOffs (out);

            // 12 kHz 前後に間引いて音程を測る
            const float a = aa2.process (aa1.process (xh));
            if (++decimCount >= D)
            {
                decimCount = 0;
                ring[(size_t) ringPos] = ring[(size_t) (ringPos + span)] = a;
                ringPos = (ringPos + 1) % span;
                if (++hopCount >= hopD)
                {
                    hopCount = 0;
                    melodyFrame (on && mode == 0, out, ch);
                }
            }
        }
    }

    void allOff (MidiSink& out)
    {
        stopMelodyNote (out);
        for (int c = 0; c < 16; ++c)
        {
            out.send ((uint8_t) (0xB0 | c), 123, 0);
            out.send ((uint8_t) (0xE0 | c), 0, 64);
        }
        for (auto& d : drumOff) d.samplesLeft = 0;
    }

    // ---- テスト用：最後に測った音程の情報 ----
    float lastPitch() const { return lastF0Midi; }

private:
    double fs = 48000, fsd = 12000;
    int D = 4, W = 192, tauMin = 10, tauMax = 172, span = 400, hopD = 32;
    std::vector<float> ring, diff, cmnd;
    int ringPos = 0, hopCount = 0, decimCount = 0;
    Biquad hpIn, aa1, aa2, lowF, lowF2, highF, highF2;
    double levelCoef = 0, ms2 = 0;
    int lastMode = 0;

    // ---------------- MELODY ----------------
    int curNote = -1, curChan = 0, pendCand = -1;
    float pendMs = 0, unvoicedMs = 0, peakSinceStart = -100;
    float runPeak = -100, dipMin = 0; bool dipped = false;
    float bendSmooth = 0; int lastBendSent = 8192, bendTick = 0;
    float lastF0Midi = -1;

    void resetMelody()
    {
        curNote = -1; pendCand = -1; pendMs = 0; unvoicedMs = 1e6f; peakSinceStart = -100;
        runPeak = -100; dipped = false; bendSmooth = 0; lastBendSent = 8192; currentNote.store (-1);
    }

    float levelDb() const { return 10.0f * (float) std::log10 (ms2 + 1e-12); }

    // YIN：周期を探す。見つかれば周波数、だめなら -1。aper = 周期性の悪さ（0 に近いほどきれい）
    float yin (float& aper)
    {
        const float* buf = ring.data() + ringPos;   // 古い → 新しい の順に span 個
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0;
            const float* a = buf; const float* b = buf + tau;
            for (int j = 0; j < W; ++j) { const float d = a[j] - b[j]; s += d * d; }
            diff[(size_t) tau] = s;
        }
        cmnd[0] = 1;
        float run = 0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += diff[(size_t) tau];
            cmnd[(size_t) tau] = run > 0 ? diff[(size_t) tau] * (float) tau / run : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0)
        {
            float mv = 1e9f;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < mv) { mv = cmnd[(size_t) tau]; best = tau; }
        }
        aper = cmnd[(size_t) best];
        float t = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float y0 = cmnd[(size_t) best - 1], y1 = cmnd[(size_t) best], y2 = cmnd[(size_t) best + 1];
            const float den = y0 - 2 * y1 + y2;
            if (std::abs (den) > 1e-9f) t += std::clamp (0.5f * (y0 - y2) / den, -0.5f, 0.5f);
        }
        return (float) fsd / t;
    }

    void noteOn (MidiSink& out, int note, int vel, int ch)
    {
        note = std::clamp (note, 0, 127);
        out.send ((uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) std::clamp (vel, 1, 127));
        curNote = note; curChan = ch; currentNote.store (note);
    }
    void noteOff (MidiSink& out, int note, int ch) { out.send ((uint8_t) (0x80 | ch), (uint8_t) note, 0); }

    void stopMelodyNote (MidiSink& out)
    {
        if (curNote >= 0) noteOff (out, curNote, curChan);
        if (lastBendSent != 8192) { out.send ((uint8_t) (0xE0 | curChan), 0, 64); lastBendSent = 8192; }
        curNote = -1; currentNote.store (-1); bendSmooth = 0;
    }

    int velocityFor (float db) const
    {
        if (! p.dynamicVel.load()) return 100;
        const float t = std::clamp ((db - p.gateDb.load()) / 36.0f, 0.0f, 1.0f);
        return (int) std::lround (24 + t * 103);
    }

    void melodyFrame (bool active, MidiSink& out, int ch)
    {
        const float dt = (float) (hopD * 1000.0 / fsd);
        const float lv = levelDb();
        const float gate = p.gateDb.load();
        Frame fr; fr.level = lv;

        if (! active)
        {
            fr.note = -1; frames.push (fr);
            return;
        }

        float aper = 1, m = -1;
        if (lv > gate)
        {
            const float f0 = yin (aper);
            if (aper < 0.22f && f0 > 65 && f0 < 1300)
                m = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        }
        lastF0Midi = m;
        fr.pitch = m;

        const int key = p.key.load(), sc = p.scale.load();
        const float mm = m + 12.0f * (float) std::clamp (p.octave.load(), -3, 3);
        int cand = -1;
        if (m > 0)
        {
            cand = snapToScale (mm, key, sc);
            // 2 つの音のちょうど間で行ったり来たりしないよう、今の音を少しひいきする
            if (curNote >= 0 && cand != curNote && inScale (curNote, key, sc)
                && std::abs (mm - (float) curNote) < std::abs (mm - (float) cand) + 0.35f)
                cand = curNote;
        }

        if (cand >= 0)
        {
            if (unvoicedMs > 0) { if (curNote < 0) peakSinceStart = lv; }
            peakSinceStart = std::max (peakSinceStart, lv);
            if (cand == pendCand) pendMs += dt; else { pendCand = cand; pendMs = dt; }
            unvoicedMs = 0;
        }
        else
        {
            unvoicedMs += dt; pendCand = -1; pendMs = 0;
        }

        const float glue = p.glueMs.load(), hold = p.holdMs.load();

        if (curNote < 0)
        {
            if (pendCand >= 0 && pendMs >= glue)
            {
                sendCenterBend (out, ch);
                noteOn (out, pendCand, velocityFor (peakSinceStart), ch);
                runPeak = lv; dipped = false; dipMin = lv;
            }
        }
        else
        {
            // 音量のくぼみ → また大きく = 言い直した（同じ高さでも打ち直す）
            runPeak = std::max (runPeak, lv);
            if (lv < runPeak - 10.0f) { dipped = true; dipMin = std::min (dipMin, lv); }
            else if (! dipped) dipMin = lv;

            if (unvoicedMs >= hold)
            {
                stopMelodyNote (out);
            }
            else if (pendCand >= 0 && pendCand != curNote && pendMs >= glue)
            {
                const int old = curNote, oldCh = curChan;
                noteOn (out, pendCand, velocityFor (lv), ch);   // 先に次の音 → レガート（つながる）
                noteOff (out, old, oldCh);
                bendSmooth = 0;
                runPeak = lv; dipped = false; dipMin = lv;
            }
            else if (dipped && cand >= 0 && lv > dipMin + 8.0f)
            {
                const int note = curNote, oldCh = curChan;
                noteOff (out, note, oldCh);
                noteOn (out, note, velocityFor (runPeak), ch);   // 言い直しは前の山の大きさで
                runPeak = lv; dipped = false; dipMin = lv;
            }

            if (curNote >= 0 && p.bend.load() && m > 0)
            {
                const float semis = std::clamp (mm - (float) curNote, -2.0f, 2.0f);
                bendSmooth += 0.35f * (semis - bendSmooth);
                if (++bendTick >= 3)
                {
                    bendTick = 0;
                    const int v = std::clamp ((int) std::lround (8192 + bendSmooth / 2.0f * 8191), 0, 16383);
                    if (std::abs (v - lastBendSent) > 24)
                    {
                        out.send ((uint8_t) (0xE0 | curChan), (uint8_t) (v & 127), (uint8_t) (v >> 7));
                        lastBendSent = v;
                    }
                }
            }
        }
        fr.note = curNote;
        frames.push (fr);
    }

    void sendCenterBend (MidiSink& out, int ch)
    {
        if (lastBendSent != 8192) { out.send ((uint8_t) (0xE0 | ch), 0, 64); lastBendSent = 8192; }
        bendSmooth = 0;
    }

    // ---------------- DRUMS ----------------
    double hFast = 0, hSlow = 0, bLow = 0, bHigh = 0, bAll = 0, bCoef = 0;
    Biquad hbF;
    double fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
    bool capturing = false; int capN = 0, capLen = 0, refractory = 0;
    double eLow = 0, eHigh = 0, eAll = 0; float capPeak = 0;
    struct PendingOff { int note = 0, ch = 0, samplesLeft = 0; };
    std::array<PendingOff, 3> drumOff {};
    std::vector<std::array<float, 2>> learnBuf;

    void resetDrums()
    {
        capturing = false; refractory = 0; learnBuf.clear();
        for (auto& d : drumOff) d.samplesLeft = 0;
    }

    void tickDrumOffs (MidiSink& out)
    {
        for (auto& d : drumOff)
            if (d.samplesLeft > 0 && --d.samplesLeft == 0)
                out.send ((uint8_t) (0x80 | d.ch), (uint8_t) d.note, 0);
    }

    void drumSample (float xh, MidiSink& out, int ch)
    {
        const double a = std::abs (xh);
        fastEnv = a > fastEnv ? a + (fastEnv - a) * fastAtt : a + (fastEnv - a) * fastRel;
        slowEnv = a > slowEnv ? a + (slowEnv - a) * slowAtt : a + (slowEnv - a) * slowRel;
        const float lo = lowF2.process (lowF.process (xh));
        const float hi = highF2.process (highF.process (xh));
        // 高い所だけの包絡も見る（キックの余韻の上に乗った小さいハイハットも拾う）
        const float hb = hbF.process (xh);
        const double ah = std::abs (hb);
        hFast = ah > hFast ? ah + (hFast - ah) * fastAtt : ah + (hFast - ah) * fastRel;
        hSlow = ah > hSlow ? ah + (hSlow - ah) * slowAtt : ah + (hSlow - ah) * slowRel;
        if (refractory > 0) --refractory;

        if (! capturing)
        {
            // 鳴る直前の「残っている音」（キックの余韻など）。新しく足された分だけで聞き分けるため
            bLow  = bCoef * bLow  + (1 - bCoef) * (double) lo * lo;
            bHigh = bCoef * bHigh + (1 - bCoef) * (double) hi * hi;
            bAll  = bCoef * bAll  + (1 - bCoef) * (double) xh * xh;
        }
        if (capturing)
        {
            eLow += (double) lo * lo; eHigh += (double) hi * hi; eAll += (double) xh * xh;
            capPeak = std::max (capPeak, (float) a);
            if (++capN >= capLen) { capturing = false; classify (out, ch); }
            return;
        }

        const double fDb = 20 * std::log10 (fastEnv + 1e-9), sDb = 20 * std::log10 (slowEnv + 1e-9);
        const double hfDb = 20 * std::log10 (hFast + 1e-9), hsDb = 20 * std::log10 (hSlow + 1e-9);
        const float g = p.gateDb.load() + 6.0f, sens = p.drumSens.load();
        if (refractory == 0 && ((fDb > g && fDb - sDb > sens) || (hfDb > g - 6.0f && hfDb - hsDb > sens + 3.0f)))
        {
            capturing = true; capN = 0; capLen = (int) (fs * 0.014);
            eLow = eHigh = eAll = 0; capPeak = (float) a;
            refractory = (int) (fs * 0.075);
        }
    }

public:
    // 低音（150 Hz 以下）と高音（5 kHz 以上）の割合（log10）
    std::array<float, 2> lastFeatures {};
    int classifyFeatures (float fl, float fh) const
    {
        int best = 0; float bd = 1e9f;
        for (int c = 0; c < 3; ++c)
        {
            const float d0 = fl - p.proto[c][0].load(), d1 = fh - p.proto[c][1].load();
            const float d = 0.25f * d0 * d0 + d1 * d1;   // 高音の割合のほうを重く見る
            if (d < bd) { bd = d; best = c; }
        }
        return best;
    }

private:
    void classify (MidiSink& out, int ch)
    {
        const double n = (double) capLen;
        const double l = std::max (0.0, eLow - bLow * n), h = std::max (0.0, eHigh - bHigh * n);
        const double tot = std::max (eAll - bAll * n, 0.0) + 1e-12;
        const float fl = (float) std::log10 (std::min (1.0, l / tot) + 3e-3), fh = (float) std::log10 (std::min (1.0, h / tot) + 3e-3);
        lastFeatures = { fl, fh };
        int cls = classifyFeatures (fl, fh);

        const int lc = learnClass.load();
        if (lc >= 0 && lc < 3)
        {
            cls = lc;
            learnBuf.push_back ({ fl, fh });
            learnCount.store ((int) learnBuf.size());
            if ((int) learnBuf.size() >= learnShots)
            {
                float s0 = 0, s1 = 0;
                for (auto& v : learnBuf) { s0 += v[0]; s1 += v[1]; }
                p.proto[lc][0].store (s0 / (float) learnBuf.size());
                p.proto[lc][1].store (s1 / (float) learnBuf.size());
                learnBuf.clear();
                learnClass.store (-1);
            }
        }
        else if (! learnBuf.empty()) learnBuf.clear();

        const float pk = 20.0f * std::log10 (capPeak + 1e-9f);
        int vel = 100;
        if (p.dynamicVel.load())
            vel = (int) std::lround (30 + 97 * std::clamp ((pk - (p.gateDb.load() + 6.0f)) / 36.0f, 0.0f, 1.0f));
        const int note = std::clamp (p.drumNote[cls].load(), 0, 127);
        auto& d = drumOff[(size_t) cls];
        if (d.samplesLeft > 0) out.send ((uint8_t) (0x80 | d.ch), (uint8_t) d.note, 0);
        out.send ((uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) vel);
        d = { note, ch, (int) (fs * 0.08) };
        hits.push ({ cls, vel });
    }
};

} // namespace hum
