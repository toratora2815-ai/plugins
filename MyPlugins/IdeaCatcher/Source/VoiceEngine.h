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

    // 口ドラム 6 音: KICK / SNARE / HAT / OPEN HAT / TOM / CLAP
    static constexpr int numDrums = 6, numFeat = 6;
    std::atomic<int>   drumNote[numDrums];
    std::atomic<float> proto[numDrums][numFeat];   // 6 つの帯域の割合（log10）
    std::atomic<float> protoSus[numDrums];         // 鳴り始めから 30〜45 ms の残り方（dB、ハイハットの開き/閉じに使う）
    std::atomic<bool>  has[numDrums];              // 使える音か（OPEN HAT / TOM / CLAP は LEARN してから）

    Params()
    {
        const int notes[numDrums] = { 36, 38, 42, 46, 45, 39 };
        for (int i = 0; i < numDrums; ++i) drumNote[i].store (notes[i]);
        resetProtos();
    }
    // 最初の聞き分け（作った音で覚えさせた値）
    static constexpr float defaultProto[3][numFeat] = { { -0.36f, -0.28f, -1.52f, -2.38f, -2.39f, -2.17f },   // KICK
                                                                  { -1.95f, -1.65f, -1.40f, -0.62f, -0.32f, -0.90f },   // SNARE
                                                                  { -2.52f, -2.52f, -2.51f, -2.23f, -1.19f, -0.03f } }; // HAT
    static constexpr float defaultSus[3] = { -12.0f, -12.0f, -24.0f };
    void resetProto (int c)
    {
        for (int k = 0; k < numFeat; ++k) proto[c][k].store (c < 3 ? defaultProto[c][k] : 0.0f);
        protoSus[c].store (c < 3 ? defaultSus[c] : -4.0f);
        has[c].store (c < 3);
    }
    void resetProtos() { for (int c = 0; c < numDrums; ++c) resetProto (c); }
};

struct Frame
{
    float pitch = -1;   // 歌っている高さ（MIDI 番号の小数、-1 = 声なし）
    float level = -100; // dBFS
    int   note = -1;    // 鳴らしているノート
};

struct Hit { int cls; int velocity; };

// 記録用の MIDI（t = 本当に鳴り始めたと思われるサンプル位置。kind 1 = 直前のノートの高さを d1 に直す）
struct CapEvent { int64_t t; uint8_t st, d1, d2, kind; };

//==============================================================================
class Engine
{
public:
    Params p;
    Ring<Frame, 4096> frames;   // 画面用（約 2.7 ms ごと）
    Ring<Hit, 256>    hits;     // 画面用（ドラム）
    Ring<CapEvent, 8192> capture;  // 録り逃し防止の記録用
    std::atomic<int64_t> clockNow { 0 };  // 今まで処理したサンプル数
    double sampleRate() const { return fs; }
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
        setupBands();
        hbF.setHP (fs, 3000.0, 0.707); hbF.reset(); hFast = hSlow = 0;
        bCoef = std::exp (-1.0 / (fs * 0.008));

        levelCoef = std::exp (-1.0 / (fs * 0.010));
        fastAtt = std::exp (-1.0 / (fs * 0.0003)); fastRel = std::exp (-1.0 / (fs * 0.010));
        slowAtt = std::exp (-1.0 / (fs * 0.015));  slowRel = std::exp (-1.0 / (fs * 0.060));
        ms2 = 0; fastEnv = slowEnv = 0; clock = 0; lastCapT = 0;
        resetMelody(); resetDrums();
    }

    void process (const float* x, int n, MidiSink& out)
    {
        if (panicRequest.exchange (false)) allOff (out);

        const bool on = p.enabled.load();
        const int mode = p.mode.load();
        if (! on || mode != lastMode) { stopMelodyNote (out); lastMode = mode; }
        const int ch = std::clamp (p.channel.load(), 1, 16) - 1;

        for (int i = 0; i < n; ++i, ++clock)
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
        clockNow.store (clock);
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
    int64_t clock = 0, lastCapT = 0;
    void emit (MidiSink& out, uint8_t st, uint8_t d1, uint8_t d2, int64_t t)
    {
        out.send (st, d1, d2);
        // 記録の時刻は前後しないように（言い直しと音の切り替えが重なったときなど）
        t = std::clamp (t, lastCapT, clock);
        lastCapT = t;
        capture.push ({ t, st, d1, d2, 0 });
    }

    double fs = 48000, fsd = 12000;
    int D = 4, W = 192, tauMin = 10, tauMax = 172, span = 400, hopD = 32;
    std::vector<float> ring, diff, cmnd;
    int ringPos = 0, hopCount = 0, decimCount = 0;
    Biquad hpIn, aa1, aa2;
    double levelCoef = 0, ms2 = 0;
    int lastMode = 0;

    // ---------------- MELODY ----------------
    int curNote = -1, curChan = 0, pendCand = -1;
    float pendMs = 0, unvoicedMs = 0, peakSinceStart = -100;
    float runPeak = -100, dipMin = 0; bool dipped = false;
    float bendSmooth = 0; int lastBendSent = 8192, bendTick = 0;
    float lastF0Midi = -1;
    std::array<float, 5> rawHist {};      // 直近 5 回の生の音程（-1 = 声なし）
    std::array<float, 8> filtHist {};     // ならした音程（動いているかを見る）
    int rawPos = 0, filtPos = 0;
    int64_t pendStart = 0, unvoicedStart = 0, gateCross = 0, noteStartAt = 0;
    bool phraseFirst = false;             // 句の最初の音（まだ直してよい）
    bool aboveGate = false;

    void resetMelody()
    {
        curNote = -1; pendCand = -1; pendMs = 0; unvoicedMs = 1e6f; peakSinceStart = -100;
        runPeak = -100; dipped = false; bendSmooth = 0; lastBendSent = 8192; currentNote.store (-1);
        rawHist.fill (-1); filtHist.fill (-1); rawPos = filtPos = 0; phraseFirst = false; aboveGate = false;
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

    void noteOn (MidiSink& out, int note, int vel, int ch, int64_t t)
    {
        note = std::clamp (note, 0, 127);
        emit (out, (uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) std::clamp (vel, 1, 127), t);
        curNote = note; curChan = ch; currentNote.store (note); noteStartAt = clock;
    }
    void noteOff (MidiSink& out, int note, int ch, int64_t t) { emit (out, (uint8_t) (0x80 | ch), (uint8_t) note, 0, t); }

    void stopMelodyNote (MidiSink& out, int64_t t = -1)
    {
        if (t < 0) t = clock;
        if (curNote >= 0) noteOff (out, curNote, curChan, t);
        if (lastBendSent != 8192) { emit (out, (uint8_t) (0xE0 | curChan), 0, 64, t); lastBendSent = 8192; }
        curNote = -1; currentNote.store (-1); bendSmooth = 0; phraseFirst = false;
    }

    int velocityFor (float db) const
    {
        if (! p.dynamicVel.load()) return 100;
        const float t = std::clamp ((db - p.gateDb.load()) / 36.0f, 0.0f, 1.0f);
        return (int) std::lround (24 + t * 103);
    }

    // 声の高さを 5 回分の真ん中の値でならす（一瞬の外れやオクターブの飛びを消す）
    float medianPitch() const
    {
        std::array<float, 5> v {}; int n = 0;
        for (float x : rawHist) if (x > 0) v[(size_t) n++] = x;
        if (n < 3) return -1;
        std::sort (v.begin(), v.begin() + n);
        return n % 2 ? v[(size_t) n / 2] : 0.5f * (v[(size_t) n / 2 - 1] + v[(size_t) n / 2]);
    }

    void melodyFrame (bool active, MidiSink& out, int ch)
    {
        const float dt = (float) (hopD * 1000.0 / fsd);
        const float lv = levelDb();
        const float gate = p.gateDb.load();
        Frame fr; fr.level = lv;

        // 声が GATE を越えた瞬間（句の始まりの時刻に使う）
        if (lv > gate && ! aboveGate) gateCross = clock;
        aboveGate = lv > gate;

        if (! active)
        {
            fr.note = -1; frames.push (fr);
            return;
        }

        float aper = 1, raw = -1;
        if (lv > gate)
        {
            const float f0 = yin (aper);
            // 鳴っている途中は少し甘く（子音や息で途切れにくく）
            const float thr = curNote >= 0 ? 0.35f : 0.22f;
            if (aper < thr && f0 > 65 && f0 < 1300)
                raw = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        }
        rawHist[(size_t) rawPos] = raw; rawPos = (rawPos + 1) % (int) rawHist.size();
        const float m = raw > 0 ? medianPitch() : -1.0f;
        lastF0Midi = m;
        fr.pitch = m;

        // 動いているか（約 16 ms で 0.4 半音以上 = 滑っている途中。そこでは音を変えない）
        const float older = filtHist[(size_t) filtPos];
        filtHist[(size_t) filtPos] = m; filtPos = (filtPos + 1) % (int) filtHist.size();
        const float back = filtHist[(size_t) ((filtPos + 1) % (int) filtHist.size())];
        (void) older;
        const bool moving = m > 0 && back > 0 && std::abs (m - back) > 0.4f;

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
            if (unvoicedMs > 0 && curNote < 0) peakSinceStart = lv;
            peakSinceStart = std::max (peakSinceStart, lv);
            if (cand == pendCand && ! (moving && cand != curNote)) pendMs += dt;
            else if (cand != pendCand) { pendCand = cand; pendMs = moving && curNote >= 0 ? 0 : dt; pendStart = clock; }
            unvoicedMs = 0;
        }
        else
        {
            if (unvoicedMs <= 0.0f) unvoicedStart = clock;
            unvoicedMs += dt; pendCand = -1; pendMs = 0;
        }

        const float glue = p.glueMs.load(), hold = p.holdMs.load();
        const int64_t lag = analysisLag();

        if (curNote < 0)
        {
            // 句の最初は速く鳴らす（違っていたら、あとで高さだけ直す）
            if (pendCand >= 0 && pendMs >= std::min (glue, 8.0f))
            {
                sendCenterBend (out, ch);
                const int64_t t0 = std::max (gateCross - (int64_t) (fs * 0.003), clock - lag - (int64_t) (fs * 0.03));
                noteOn (out, pendCand, velocityFor (peakSinceStart), ch, t0);
                phraseFirst = true;
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
                stopMelodyNote (out, unvoicedStart - lag);
            }
            else if (pendCand >= 0 && pendCand != curNote && pendMs >= glue)
            {
                const int old = curNote, oldCh = curChan;
                if (phraseFirst && clock - noteStartAt < (int64_t) (fs * 0.12))
                {
                    // 出だしのしゃくりで違う高さになっていた → 鳴らし直し、記録では最初から正しい高さに
                    out.send ((uint8_t) (0x90 | ch), (uint8_t) pendCand, (uint8_t) velocityFor (peakSinceStart));
                    out.send ((uint8_t) (0x80 | oldCh), (uint8_t) old, 0);
                    capture.push ({ 0, (uint8_t) (0x90 | ch), (uint8_t) pendCand, 0, 1 });
                    curNote = pendCand; curChan = ch; currentNote.store (pendCand);
                }
                else
                {
                    const int64_t t = pendStart - lag;
                    noteOn (out, pendCand, velocityFor (lv), ch, t);   // 先に次の音 → レガート（つながる）
                    noteOff (out, old, oldCh, t);
                }
                phraseFirst = false;
                bendSmooth = 0;
                runPeak = lv; dipped = false; dipMin = lv;
            }
            else if (dipped && cand >= 0 && lv > dipMin + 8.0f)
            {
                const int note = curNote, oldCh = curChan;
                const int64_t t = clock - (int64_t) (fs * 0.012);
                noteOff (out, note, oldCh, t);
                noteOn (out, note, velocityFor (runPeak), ch, t);   // 言い直しは前の山の大きさで
                phraseFirst = false;
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
                        emit (out, (uint8_t) (0xE0 | curChan), (uint8_t) (v & 127), (uint8_t) (v >> 7), clock - lag);
                        lastBendSent = v;
                    }
                }
            }
        }
        fr.note = curNote;
        frames.push (fr);
    }

    // 音程を測る窓の真ん中まで ＋ ならし分の遅れ（サンプル）
    int64_t analysisLag() const { return (int64_t) ((span * 0.5 + hopD * 2.0) * D); }

    void sendCenterBend (MidiSink& out, int ch)
    {
        if (lastBendSent != 8192) { emit (out, (uint8_t) (0xE0 | ch), 0, 64, clock); lastBendSent = 8192; }
        bendSmooth = 0;
    }

    // ---------------- DRUMS ----------------
    static constexpr int ND = Params::numDrums, NF = Params::numFeat;
    double hFast = 0, hSlow = 0, bCoef = 0;
    Biquad hbF;
    std::array<Biquad, NF> bandA, bandB;          // 6 つの帯域（〜150 / 〜400 / 〜1.2k / 〜3k / 〜7k / 7k〜）
    std::array<double, NF> base {}, eBand {};     // 鳴る直前の残り / 鳴ってからの量
    double fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
    int capPhase = 0;                             // 0 = 待ち、1 = 14 ms 測る、2 = ハイハットの残り方を 45 ms まで見る
    int64_t capStart = 0; int capN = 0, capLen = 0, susLen = 0, refractory = 0;
    double eEarlyHi = 0, eLateHi = 0; float capPeak = 0;
    int pendingCls = -1;
    std::array<float, NF> pendingFeat {};
    struct PendingOff { int note = 0, ch = 0, samplesLeft = 0; int64_t offAt = 0; };
    std::array<PendingOff, ND> drumOff {};
    struct Shot { std::array<float, NF> f; float sus; };
    std::vector<Shot> learnBuf;

    void setupBands()
    {
        const double edges[NF + 1] = { 0, 150, 400, 1200, 3000, 7000, 0 };
        for (int k = 0; k < NF; ++k)
        {
            if (k == 0)           { bandA[0].setLP (fs, 150, 0.707); bandB[0].setLP (fs, 150, 0.707); }
            else if (k == NF - 1) { bandA[(size_t) k].setHP (fs, 7000, 0.707); bandB[(size_t) k].setHP (fs, 7000, 0.707); }
            else { bandA[(size_t) k].setHP (fs, edges[k], 0.707); bandB[(size_t) k].setLP (fs, std::min (edges[k + 1], fs * 0.45), 0.707); }
            bandA[(size_t) k].reset(); bandB[(size_t) k].reset();
        }
        base.fill (0); eBand.fill (0);
    }

    void resetDrums()
    {
        capPhase = 0; refractory = 0; learnBuf.clear(); pendingCls = -1;
        for (auto& d : drumOff) d.samplesLeft = 0;
    }

    void tickDrumOffs (MidiSink& out)
    {
        for (auto& d : drumOff)
            if (d.samplesLeft > 0 && --d.samplesLeft == 0)
                emit (out, (uint8_t) (0x80 | d.ch), (uint8_t) d.note, 0, d.offAt);
    }

    void drumSample (float xh, MidiSink& out, int ch)
    {
        const double a = std::abs (xh);
        fastEnv = a > fastEnv ? a + (fastEnv - a) * fastAtt : a + (fastEnv - a) * fastRel;
        slowEnv = a > slowEnv ? a + (slowEnv - a) * slowAtt : a + (slowEnv - a) * slowRel;
        // 高い所だけの包絡も見る（キックの余韻の上に乗った小さいハイハットも拾う）
        const float hb = hbF.process (xh);
        const double ah = std::abs (hb);
        hFast = ah > hFast ? ah + (hFast - ah) * fastAtt : ah + (hFast - ah) * fastRel;
        hSlow = ah > hSlow ? ah + (hSlow - ah) * slowAtt : ah + (hSlow - ah) * slowRel;
        if (refractory > 0) --refractory;

        std::array<double, NF> e2 {};
        for (size_t k = 0; k < (size_t) NF; ++k)
        {
            const float y = bandB[k].process (bandA[k].process (xh));
            e2[k] = (double) y * y;
        }

        if (capPhase == 0)
        {
            // 鳴る直前の「残っている音」（キックの余韻など）。新しく足された分だけで聞き分けるため
            for (size_t k = 0; k < (size_t) NF; ++k) base[k] = bCoef * base[k] + (1 - bCoef) * e2[k];

            const double fDb = 20 * std::log10 (fastEnv + 1e-9), sDb = 20 * std::log10 (slowEnv + 1e-9);
            const double hfDb = 20 * std::log10 (hFast + 1e-9), hsDb = 20 * std::log10 (hSlow + 1e-9);
            const float g = p.gateDb.load() + 6.0f, sens = p.drumSens.load();
            if (refractory == 0 && ((fDb > g && fDb - sDb > sens) || (hfDb > g - 6.0f && hfDb - hsDb > sens + 3.0f)))
            {
                capPhase = 1; capStart = clock; capN = 0;
                capLen = (int) (fs * 0.014); susLen = (int) (fs * 0.045);
                eBand.fill (0); eEarlyHi = eLateHi = 0; capPeak = (float) a;
                refractory = (int) (fs * 0.075);
            }
            return;
        }

        ++capN;
        capPeak = std::max (capPeak, (float) a);
        const double hiNow = std::max (0.0, e2[4] + e2[5] - base[4] - base[5]);
        if (capPhase == 1)
        {
            for (size_t k = 0; k < (size_t) NF; ++k) eBand[k] += e2[k];
            eEarlyHi += hiNow;
            if (capN >= capLen) firstLook (out, ch);
        }
        else
        {
            if (capN >= (int) (fs * 0.030)) eLateHi += hiNow;
            if (capN >= susLen) secondLook (out, ch);
        }
    }

public:
    std::array<float, NF> lastFeatures {};
    float lastSustain = 0;

    // 特徴がいちばん近い音（使える音だけ）。HAT と OPEN HAT は同じ組として返す（2）
    int nearestClass (const std::array<float, NF>& f) const
    {
        int best = 0; float bd = 1e9f;
        for (int c = 0; c < ND; ++c)
        {
            if (! p.has[c].load()) continue;
            float d = 0;
            for (int k = 0; k < NF; ++k) { const float x = f[(size_t) k] - p.proto[c][k].load(); d += x * x; }
            if (d < bd) { bd = d; best = c; }
        }
        return best == 3 ? 2 : best;
    }
    bool openHatInUse() const { return p.has[3].load() && p.has[2].load(); }
    int hatBySustain (float sus) const
    {
        const float thr = 0.5f * (p.protoSus[2].load() + p.protoSus[3].load());
        return sus > thr ? 3 : 2;
    }

private:
    void firstLook (MidiSink& out, int ch)
    {
        const double n = (double) capLen;
        std::array<double, NF> e {};
        double tot = 1e-12;
        for (size_t k = 0; k < (size_t) NF; ++k) { e[k] = std::max (0.0, eBand[k] - base[k] * n); tot += e[k]; }
        for (size_t k = 0; k < (size_t) NF; ++k) pendingFeat[k] = (float) std::log10 (e[k] / tot + 3e-3);
        lastFeatures = pendingFeat;

        const int lc = learnClass.load();
        pendingCls = (lc >= 0 && lc < ND) ? lc : nearestClass (pendingFeat);
        const bool hatFamily = pendingCls == 2 || pendingCls == 3;
        if (hatFamily && (openHatInUse() || lc == 2 || lc == 3))
        {
            capPhase = 2;   // 開いたハイハットか、もう少し聞いて決める
            return;
        }
        capPhase = 0;
        fire (out, ch, pendingCls, 0.0f, false);
    }

    void secondLook (MidiSink& out, int ch)
    {
        capPhase = 0;
        const double early = eEarlyHi / std::max (1, capLen) + 1e-12;
        const double late  = eLateHi / std::max (1, susLen - (int) (fs * 0.030)) + 1e-12;
        const float sus = (float) (10.0 * std::log10 (late / early));
        lastSustain = sus;
        const int lc = learnClass.load();
        const int cls = (lc >= 0) ? lc : hatBySustain (sus);
        fire (out, ch, cls, sus, true);
    }

    void fire (MidiSink& out, int ch, int cls, float sus, bool hasSus)
    {
        const int lc = learnClass.load();
        if (lc >= 0 && lc < ND)
        {
            cls = lc;
            learnBuf.push_back ({ pendingFeat, sus });
            learnCount.store ((int) learnBuf.size());
            if ((int) learnBuf.size() >= learnShots)
            {
                std::array<float, NF> m {}; float ms = 0;
                for (auto& v : learnBuf) { for (size_t k = 0; k < (size_t) NF; ++k) m[k] += v.f[k]; ms += v.sus; }
                const float inv = 1.0f / (float) learnBuf.size();
                for (int k = 0; k < NF; ++k) p.proto[lc][k].store (m[(size_t) k] * inv);
                if (hasSus) p.protoSus[lc].store (ms * inv);
                p.has[lc].store (true);
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
        const int64_t t0 = capStart - (int64_t) (fs * 0.002);
        if (d.samplesLeft > 0) emit (out, (uint8_t) (0x80 | d.ch), (uint8_t) d.note, 0, t0);
        emit (out, (uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) vel, t0);
        const double len = cls == 3 ? 0.30 : 0.08;
        d = { note, ch, (int) (fs * len) - (int) (clock - capStart), t0 + (int64_t) (fs * len) };
        if (d.samplesLeft < 1) d.samplesLeft = 1;
        hits.push ({ cls, vel });
    }
};

} // namespace hum
