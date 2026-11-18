#pragma once
// WINDMILL（0.8.0）: AMP の前の、揺れるペダル。「機種 ▾」で 2 つの型から選ぶ。
//  PHASE … 1974 年の定番のオレンジの 4 段フェイザーの回路をそのまま式にしたもの（DAFx 2024 の回路解析の値）。
//          段 = 47 nF と「24 kΩ ∥ JFET（2N5952: Vp −2.021 V、IS0 5.367 mA）」の全域通過（位相だけずらす）4 段、原音と半々で混ぜる。
//          JFET の抵抗はゲート電圧（揺れ）で決まる式のまま。揺れ = ゲートの三角波 −1.85 V ± 0.15 V（上り 65 %・下り 35 %）
//          → 段の境目 約 330 Hz〜3 kHz（周波数でまっすぐ動く＝上の方に長くいる）、切れ込みは約 140 Hz〜1.2 kHz と 800 Hz〜7 kHz。
//          本物の JFET は両端の電圧（＝信号）でも抵抗が変わり、強く弾くと濁る。式のまま入れると揺れのいちばん下（ほぼ切れる所）で
//          濁りが揺れに合わせて脈打ち、AMP の前では扱いにくかったので入れていない（2026-11-18 測定: −12 dBFS で倍音 −14 dB）。
//          COLOR = 帰還（4 段目の出口から 2 段目の − 入力へ戻す抵抗。0 = 帰還なしの初期の型、5 = 帰還ありの型（24 kΩ）、10 = 発振しない所まで）
//  VIBE  … ランプと光センサー（LDR）で揺らす 4 段（コンデンサーは 15 nF / 220 nF / 470 pF / 4.7 nF と不ぞろい = 切れ込みが不ぞろい）。
//          原音と混ぜる（CHORUS。切れ込みは浅め）。ランプは温まるのが速く冷めるのがゆっくり、LDR は明るくなるのが速く暗くなるのがゆっくり
//          → 速く上がってゆっくり戻る「うねり」。速くすると LDR が戻りきらず幅が狭まる（本物と同じ）。
//          COLOR = うねりの強さ（ランプ・LDR の非対称、段の左右の利得の差による音量の揺れ。5 = 本物どおり、0 = なめらか）
//  - SPEED（Hz、0.1〜10）か SYNC（4 小節〜1/16、小節は拍子どおり）。揺れは曲の位置に固定: SYNC は Logic の拍、Hz は再生開始・頭出しの瞬間だけ
//    曲の秒に合わせて、あとは自分で進む（SPEED をオートメーションで動かしても飛ばない。そのかわり Hz の SPEED を動かしている曲は、
//    再生を始めた位置で揺れの位置が変わる）。止まっている間は自分で回る。位置が飛んだ所は 10 ms でつなぐ。
//    VIBE のランプは休みの後、最初に音が来たサンプルでその位置に合わせる（ブロックの区切りに左右されない＝再生とバウンスが同じ）
//  - DEPTH: 揺れの幅と、混ぜる量（0 で素通し。幅だけ狭めると切れ込みが真ん中に止まって鼻声が残るため）
//  - 揺れは左右同じ（モノ）。左右が同じ音なら 1 回だけ計算してコピー
//  - 低い所（150 Hz 付近から下）は原音のまま（6 弦の基音が膨らんだり痩せたりすると、AMP の歪み量まで揺れて音量が息をするため）
//  - 音量: ON/OFF・DEPTH・COLOR で、揺れ数周期の平均の大きさがそろうように固定の量で補う（鳴っている音は追いかけない）
//  - 遅れ 0、オーバーサンプリングなし。段の境目は双一次変換の prewarp で合わせ、44.1〜192 kHz のどれでも同じ所
//  - mix-engineer 2026-11-18: 型は 2 つ・つまみ 3 つ＋SYNC、COLOR はどちらも「クセ」、DEPTH 0 で素通し、曲の位置に固定、モノ、低音を揺らさない
//  - DSP 検査 2026-11-18（1 回目「直してから」）: VIBE のランプを最初の音で合わせる、VIBE の COLOR を毎サンプル、型の切り替えは分け目を通った音とつなぐ、小節は拍子どおり。
//    2 回目「渡してよい」: ランプの「最初の音」は −80 dBFS から（AMP の残りで合わないように）、小節は Logic の最後の小節線から数える
// JUCE なし（数値テストを JUCE なしで回せるように）。
#include <cmath>
#include <cstring>
#include <algorithm>
#include <atomic>

namespace pedal
{
class PhaserDSP
{
public:
    enum Type { phase = 0, vibe, numTypes };
    static const char* typeName (int t) { static const char* n[] { "Phase", "Vibe" }; return n[clampi (t, 0, numTypes - 1)]; }

    // SYNC の音符（My Tremolo の表の遅い側。4 拍 = 1 小節）
    static constexpr int kNumRates = 7;
    static const char* rateName (int i) { static const char* n[] { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16" }; return n[clampi (i, 0, kNumRates - 1)]; }
    static double rateBeats (int i) { static const double b[] { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 }; return b[clampi (i, 0, kNumRates - 1)]; }
    static constexpr float kMinHz = 0.1f, kMaxHz = 10.0f;

    void prepare (double sampleRate, int /*maxBlock*/, int /*numChannels*/)
    {
        fs = sampleRate;
        smK = 1.0f - (float) std::exp (-1.0 / (0.03 * fs));    // DEPTH / COLOR glide (~30 ms)
        jumpLen = std::max (1, (int) (0.010 * fs));             // a jump of the song position is joined over 10 ms
        fadeOutLen = std::max (1, (int) (0.005 * fs));          // type change: 5 ms down, 10 ms up
        fadeInLen = std::max (1, (int) (0.010 * fs));
        splitG = (float) std::tan (kPi * kSplitHz / fs);       // the low band that stays dry (Butterworth low-pass)
        splitH = 1.0f / (1.0f + kSqrt2 * splitG + splitG * splitG);
        thetaPhase = (float) (1.0 / (2.0 * kC2 * fs));          // tan argument = thetaPhase * conductance
        for (int k = 0; k < 4; ++k) thetaVibe[k] = (float) (1.0 / (2.0 * kVibeC[k] * kVibeMismatch[k] * fs));
        vibeStep = std::max (1, (int) std::lround (fs / 12000.0));   // the lamp / LDR (slow) at ~12 kHz
        current = want; state = steady;
        depthS = depthT; colorS = colorT;
        updateVibeTimes();
        reset();
    }

    int latency() const { return 0; }

    // from silence (switched on, the order changed, a broken sample): the filters from rest, the lamp where the sweep is now,
    // the clock taken again from Logic at the next block
    void reset()
    {
        for (auto& c : ch) c = Chan {};
        jumpLeft = 0; jumpOffset = 0.0f;
        first = true;
        lampSettle = true;
        mkSnap = true;   // the level from the first sample on (a glide from the block's start would depend on the block size)
    }

    // a type of the "機種 ▾": the playing one fades out (5 ms), the next starts from rest (10 ms up)
    void setType (int t, bool snap = false)
    {
        t = clampi (t, 0, numTypes - 1);
        if (snap)
        {
            if (t != current || state != steady) { current = t; state = steady; clearFilters(); lampSettle = true; }
            want = t;
            return;
        }
        if (t == want) return;
        want = t;
        if (t == current && state == fadingOut) { state = fadingIn; pos = clampi ((int) std::lround ((double) pos * fadeInLen / fadeOutLen), 0, fadeInLen); return; }
        if (state == steady) { state = fadingOut; pos = fadeOutLen; }
        else if (state == fadingIn) { state = fadingOut; pos = clampi ((int) std::lround ((double) pos * fadeOutLen / fadeInLen), 0, fadeOutLen); }
    }
    int type() const { return current; }

    // depth 0..10, color 0..10, hz = SPEED when SYNC is off (0.1..10)
    void setParams (float depth, float color, float hzIn, bool snap = false)
    {
        depthT = clampf (depth, 0.0f, 10.0f);
        colorT = clampf (color, 0.0f, 10.0f);
        hz = clampf (hzIn, kMinHz, kMaxHz);
        if (snap) { depthS = depthT; colorS = colorT; hzNow = effectiveHz(); mk = makeup (current, depthT, colorT); updateVibeTimes(); lampSettle = true; }
    }

    // every host block: SYNC = Logic's beats (the note length rateIndex; a bar = beatsPerBar quarter notes, from the time signature);
    // off = Hz, set to the song's seconds when playback starts / jumps
    // lastBarPpq / barIndex: Logic's last bar line and its number when known (< 0 = not known: bars counted from the song's start)
    void setClock (bool syncIn, int rateIndex, bool hostPlaying, bool hasPosition, double ppq, double bpm, double timeSec, double beatsPerBar = 4.0,
                   double lastBarPpq = -1.0, long long barIndex = -1)
    {
        sync = syncIn; rate = clampi (rateIndex, 0, kNumRates - 1);
        playing = hostPlaying && hasPosition;
        hostPpq = ppq; hostBpm = bpm > 1.0 ? bpm : 120.0; hostTime = timeSec;
        barBeats = beatsPerBar > 0.5 && beatsPerBar < 64.0 ? beatsPerBar : 4.0;
        barStart = lastBarPpq; barIdx = barIndex;
        clockFresh = true;   // used by the first process() after this only (the rig may split a block)
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        if (numSamples <= 0) return;
        takeClock (numSamples);
        // one pass for both sides only while they are the same and so are their filters (once apart, each side keeps its own until a reset)
        const bool stereo = numChannels > 1 && data[1] != nullptr
                            && (std::memcmp (data[0], data[1], sizeof (float) * (size_t) numSamples) != 0
                                || std::memcmp (&ch[0], &ch[1], sizeof (Chan)) != 0);
        int done = 0;
        while (done < numSamples)
        {
            if (state == silentSwitch) { current = want; clearFilters(); lampSettle = true; mk = makeup (current, depthT, colorT); state = fadingIn; pos = 0; }
            int len = std::min (numSamples - done, kHold);   // through holds one part
            if (state == fadingOut) len = std::min (len, pos + 1);
            run (data, numChannels, done, len, stereo);
            if (state != steady)
                for (int i = done; i < done + len; ++i)
                {
                    float k = 1.0f;
                    if (state == fadingOut)     { k = (float) pos / (float) fadeOutLen; if (pos > 0) --pos; else state = silentSwitch; }
                    else if (state == fadingIn) { k = (float) pos / (float) fadeInLen; if (++pos >= fadeInLen) state = steady; }
                    k = k * k * (3.0f - 2.0f * k);
                    // fade the effect, not the guitar: between the crossover's sound (= DEPTH 0, the same phase as the pedal: no dip
                    // around 200 Hz) and the pedal
                    for (int c = 0; c < std::min (numChannels, 2); ++c)
                        data[c][i] = through[c][i - done] + (data[c][i] - through[c][i - done]) * k;
                }
            done += len;
        }
        shown.store (lastM);
    }

    float shownSweep() const { return shown.load(); }   // 0..1, where the sweep is (the panel's LED)
    float vibeOhms() const { return lastR; }             // tests: the photocells now
    void freezeSweep (float m) { frozen = m; }            // tests: hold the sweep at m (0..1); < 0 = move

    // ---- for the tests
    static float jfetConductance (float vgs, float vds)   // siemens (0 = off); symmetric: the lower terminal is the source
    {
        float vg = vgs, vd = vds;
        if (vd < 0.0f) { vg = vgs - vd; vd = -vd; }
        const float vov = vg - kVp;
        if (vov <= 0.0f) return 0.0f;
        if (vd < vov) return kOhmK * (vov - 0.5f * vd);                       // ohmic: Vp^2 / (2 IS0 (vov - vds/2)) as a resistance
        const float r = vov / kVp;
        return kIs0 * r * r * (1.0f + kLambda * vd) / std::max (vd, 1.0e-9f);   // saturation: id / vds
    }
    static float phaseBreakHz (float vgs) { return (float) ((1.0 / kR5 + jfetConductance (vgs, 0.0f)) / (2.0 * kPi * kC2)); }
    static float feedbackFor (float color) { const float c = clampf (color, 0.0f, 10.0f); return c <= 5.0f ? kFbBlock * c / 5.0f : kFbBlock + (kFbMax - kFbBlock) * (c - 5.0f) / 5.0f; }
    float makeupFor (int t, float depth, float color) const { return makeup (t, depth, color); }
    static void setSplitHz (float f) { kSplitHz = f; }   // tests (before prepare)
    static void setMakeupOff (bool b) { makeupOff = b; } // tests

private:
    struct Svf { float s1 = 0, s2 = 0; };
    struct Chan
    {
        float s[4] {};          // the four stages (TPT low-pass states)
        Svf a, lo, hi;          // the crossover (Linkwitz-Riley 24 dB: two Butterworth sections each way)
    };

    // ---- the circuit
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr float kSqrt2 = 1.41421356f;
    static constexpr float kVp = -2.021f, kIs0 = 5.367e-3f, kLambda = 4.0e-3f;   // 2N5952 (DAFx 2024, table 1)
    static constexpr float kOhmK = 2.0f * kIs0 / (kVp * kVp);                     // ohmic conductance per volt of overdrive
    static constexpr double kR5 = 24000.0, kC2 = 47.0e-9;                         // the shunt resistor and the stage capacitor
    static constexpr float kVgsCentre = -1.85f, kVgsSwing = 0.15f, kRise = 0.65f; // the LFO at the gates (offset 3.25 V vs Vref 5.1 V)
    static constexpr float kFbBlock = 0.4167f, kFbMax = 0.75f;                     // R4 / R28 = 10k / 24k; never near 1 (would ring)
    static inline float kSplitHz = 200.0f;                                          // below this the guitar stays dry
    // VIBE
    static constexpr double kVibeC[4] { 15.0e-9, 220.0e-9, 470.0e-12, 4.7e-9 };
    static constexpr double kVibeMismatch[4] { 1.0, 1.12, 0.90, 1.05 };            // each photocell sees the lamp a little differently
    static constexpr float kRMin = 3400.0f, kLdrGamma = 0.75f, kLdrG0 = 0.104f;     // bright 3.4 kΩ .. dark ~20 kΩ (at full depth)
    static constexpr float kVibeWet = 0.45f, kPhaseWet = 0.5f;

    static int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double frac (double x) { return x - std::floor (x); }

    // tan for the bilinear prewarp, cheap: Pade [3/2] up to 1 rad (0.12 % at 1 rad), then straight on (stages far above the band)
    static float warp (float th)
    {
        if (th <= 1.0f) { const float t2 = th * th; return th * (15.0f - t2) / (15.0f - 6.0f * t2); }
        return 1.5555556f + (th - 1.0f) * 3.4255f;
    }

    // the sweep shape (0..1): PHASE = the gate triangle (65 % up), VIBE = the oscillator's sine
    float shapeAt (double ph) const
    {
        if (current == phase) return ph < kRise ? (float) (ph / kRise) : (float) ((1.0 - ph) / (1.0 - kRise));
        return 0.5f + 0.5f * (float) std::sin (2.0 * kPi * ph);
    }

    void takeClock (int numSamples)
    {
        const double unit = unitBeats();
        if (clockFresh)
        {
            double target = cyc;
            bool set = false;
            if (sync && playing)
            {
                if (rate <= 2 && barStart >= 0.0 && hostPpq >= barStart - 0.5 * barBeats)
                {
                    // 4 / 2 / 1 bars from Logic's last bar line (a change of time signature moves the bar lines; the same as
                    // counting from the song's start while the signature stays)
                    const int nb = rate == 0 ? 4 : (rate == 1 ? 2 : 1);
                    const long long bi = barIdx >= 0 ? barIdx : (long long) std::floor (barStart / barBeats + 0.5);
                    const double inBar = (hostPpq - barStart) / barBeats;
                    target = frac (((double) (((bi % nb) + nb) % nb) + inBar) / (double) nb);
                }
                else target = frac (hostPpq / unit);
                set = true;
            }
            else if (! sync && playing)
            {
                // Hz: only when playback starts or the song position jumps (a locate, a cycle): the song's seconds
                const bool started = ! wasPlaying, jumped = std::abs (hostTime - expectTime) > 0.005;
                if (first || started || jumped) { target = frac (hostTime * hz); set = true; }
                expectTime = hostTime;
            }
            if (set)
            {
                const double d = std::abs (target - cyc), dist = std::min (d, 1.0 - d);
                if (! first && dist > 0.002) armJump (target);
                cyc = target;
            }
            wasPlaying = playing;
            clockFresh = false;
        }
        if (playing && ! sync) expectTime += numSamples / fs;
        first = false;
    }

    double unitBeats() const { return rate <= 2 ? rateBeats (rate) * barBeats / 4.0 : rateBeats (rate); }   // 4 / 2 / 1 bars follow the time signature
    float effectiveHz() const { return sync ? (float) (hostBpm / 60.0 / unitBeats()) : hz; }   // the sweeps per second now

    void armJump (double target)
    {
        const float from = shapeAt (cyc) + (jumpLeft > 0 ? jumpOffset * (float) jumpLeft / (float) jumpLen : 0.0f);
        jumpOffset = from - shapeAt (target);
        jumpLeft = jumpLen;
    }

    void clearFilters() { for (auto& c : ch) std::fill (c.s, c.s + 4, 0.0f); }   // (the crossover keeps running: it is not the effect)

    // COLOR on VIBE: 0 = smooth and even, 5 = as the lamp and the photocells really are, 10 = more
    void updateVibeTimes()
    {
        lampColor = colorS;
        const float asym = colorS / 5.0f;   // 0..2
        auto k = [this] (double t) { return 1.0f - (float) std::exp (-1.0 / (t * fs / vibeStep)); };
        lampHeat = k (0.015);
        lampCool = k (0.015 * (1.0 + 1.5 * asym));
        ldrOn = k (0.008);
        ldrOff = k (0.008 * (1.0 + 6.5 * asym));   // COLOR 5: dark again in ~60 ms
        lightExp = 1.0f + 0.6f * asym;
        vibeGc = 1.0f - 0.12f * asym;
    }

    // the level: the average loudness over a few sweeps the same as off. A fixed amount for the settings (never follows the playing),
    // measured on the user's DI (24 s, SPEED 1 Hz, the crossover at 200 Hz) without it: dB lost by the pedal,
    // rows DEPTH 0 / 1.5 / 3 / 5 / 7.5 / 10, columns COLOR 0 / 2.5 / 5 / 7.5 / 10
    static inline bool makeupOff = false;   // tests: measure without it
    float makeup (int t, float depth, float color) const
    {
        if (makeupOff) return 1.0f;
        static constexpr float kDepth[6] { 0.0f, 1.5f, 3.0f, 5.0f, 7.5f, 10.0f };
        static constexpr float kLost[2][6][5] {
            { { 0, 0, 0, 0, 0 }, { 2.59f, 2.52f, 2.45f, 2.36f, 2.13f }, { 4.18f, 3.89f, 3.43f, 2.78f, 1.56f },
              { 4.12f, 3.76f, 3.19f, 2.42f, 0.97f }, { 3.84f, 3.40f, 2.70f, 1.76f, 0.08f }, { 3.39f, 2.91f, 2.13f, 1.13f, -0.64f } },   // PHASE
            { { 0, 0, 0, 0, 0 }, { 2.62f, 2.41f, 2.26f, 2.16f, 2.10f }, { 4.75f, 4.51f, 4.41f, 4.40f, 4.44f },
              { 4.49f, 4.37f, 4.39f, 4.50f, 4.63f }, { 4.15f, 4.21f, 4.39f, 4.62f, 4.87f }, { 3.89f, 4.09f, 4.37f, 4.70f, 5.03f } } }; // VIBE
        const float d = clampf (depth, 0.0f, 10.0f), cc = clampf (color, 0.0f, 10.0f) / 2.5f;
        int r = 0; while (r < 4 && d > kDepth[r + 1]) ++r;
        const float fr = (d - kDepth[r]) / (kDepth[r + 1] - kDepth[r]);
        const int c0 = std::min (3, (int) cc); const float fc = cc - (float) c0;
        const auto& T = kLost[clampi (t, 0, 1)];
        const float a = T[r][c0] + (T[r][c0 + 1] - T[r][c0]) * fc, b = T[r + 1][c0] + (T[r + 1][c0 + 1] - T[r + 1][c0]) * fc;
        float db = a + (b - a) * fr + 0.3f * clampf (d / 1.5f, 0.0f, 1.0f);   // +0.3: the rows in between came out ~0.3 dB low
        // VIBE: faster than 1 Hz the photocells do not get back to dark, the sweep sits brighter and loses more (on the user's DI up to
        // ~2 dB at 4 Hz and up). Only 60 % of it is given back: on brighter playing the loss is smaller (a test DI came out 2 dB loud)
        if (t == vibe)
        {
            const float fcol = 0.6f * (cc <= 2.0f ? 0.85f + (2.0f - 0.85f) * cc / 2.0f : 2.0f + (1.52f - 2.0f) * (cc - 2.0f) / 2.0f);
            const float sp = clampf (std::log2 (std::max (hzNow, 0.01f)) / 2.0f, 0.0f, 1.0f);
            db += fcol * std::pow (d / 10.0f, 0.8f) * sp;
        }
        return std::pow (10.0f, db / 20.0f);
    }

    void run (float* const* data, int numChannels, int offset, int n, bool stereo)
    {
        const int nc = std::min (numChannels, 2);
        hzNow = effectiveHz();
        const double inc = (double) hzNow / fs;
        const float mkT = makeup (current, depthT, colorT);
        if (mkSnap) { mk = mkT; mkSnap = false; }
        for (int i = 0; i < n; ++i)
        {
            depthS += (depthT - depthS) * smK;
            colorS += (colorT - colorS) * smK;
            float m = frozen >= 0.0f ? frozen : shapeAt (cyc);
            if (jumpLeft > 0) { m = clampf (m + jumpOffset * (float) jumpLeft / (float) jumpLen, 0.0f, 1.0f); --jumpLeft; }
            cyc += inc; if (cyc >= 1.0) cyc -= std::floor (cyc);
            lastM = m;
            const float depthAmt = depthS / 10.0f;
            const float w = clampf (depthS / 3.0f, 0.0f, 1.0f);
            mk += (mkT - mk) * smK;

            if (current == phase)
            {
                const float vgs = kVgsCentre + kVgsSwing * depthAmt * (2.0f * m - 1.0f);
                const float g = warp (thetaPhase * ((float) (1.0 / kR5) + jfetConductance (vgs, 0.0f)));
                phG = g / (1.0f + g);
                const float a = feedbackFor (colorS);
                for (int c = 0; c < (stereo ? nc : 1); ++c)
                    data[c][offset + i] = phaseSample (ch[(size_t) c], data[c][offset + i], a, w, mk, through[c][i]);
            }
            else
            {
                vibeGc = 1.0f - 0.12f * (colorS / 5.0f);   // every sample: a jump of COLOR glides (no step at the block edges)
                if (lampSettle)
                {
                    // after a reset the lamp is set where the sweep is at the first sound, not at the start of the block
                    // (playback and bounce cut the blocks differently: the same song must sweep the same)
                    // (-80 dBFS: well above what a resting amp leaves in its filters, ~-115 dBFS at most, so it is the guitar that sets it)
                    const bool sound = std::abs (data[0][offset + i]) >= 1.0e-4f || (nc > 1 && std::abs (data[1][offset + i]) >= 1.0e-4f);
                    if (sound) { stepLamp (depthAmt, m); vibeCount = vibeStep; }
                }
                else if (vibeCount <= 0) { stepLamp (depthAmt, m); vibeCount = vibeStep; }
                --vibeCount;
                for (int c = 0; c < (stereo ? nc : 1); ++c)
                    data[c][offset + i] = vibeSample (ch[(size_t) c], data[c][offset + i], w, mk, through[c][i]);
            }
            if (! stereo && nc > 1) { data[1][offset + i] = data[0][offset + i]; through[1][i] = through[0][i]; }
        }
        if (! stereo && nc > 1) ch[1] = ch[0];
    }

    // the crossover: the low band stays dry, the high band goes through the pedal. Linkwitz-Riley (24 dB / octave):
    // low + high is flat (an all-pass), and at 100 Hz only ~1/10 of the guitar goes through the pedal
    void svf (Svf& f, float x, float& lp, float& hp) const
    {
        hp = (x - (kSqrt2 + splitG) * f.s1 - f.s2) * splitH;
        const float v1 = splitG * hp, bp = v1 + f.s1; f.s1 = bp + v1;
        const float v2 = splitG * bp; lp = v2 + f.s2; f.s2 = lp + v2;
    }
    void split (Chan& c, float x, float& low, float& high) const
    {
        float lp, hp, dummy;
        svf (c.a, x, lp, hp);
        svf (c.lo, lp, low, dummy);
        svf (c.hi, hp, dummy, high);
    }

    float phaseSample (Chan& c, float x, float a, float w, float mk, float& thru) const
    {
        float lo, xh;
        split (c, x, lo, xh);
        const float u1 = xh;
        const float G[4] { phG, phG, phG, phG };   // four matched stages (the same gate voltage)
        // stage 1
        float v = (u1 - c.s[0]) * G[0], lp = v + c.s[0]; c.s[0] = lp + v;
        float hp = u1 - lp;
        const float y1 = 2.0f * hp - u1;
        // stage 2 with the feedback from stage 4 (zero-delay: solved with stages 3 and 4)
        const float A3 = 1.0f - 2.0f * G[2], B3 = -2.0f * (1.0f - G[2]) * c.s[2];
        const float A4 = 1.0f - 2.0f * G[3], B4 = -2.0f * (1.0f - G[3]) * c.s[3];
        v = (y1 - c.s[1]) * G[1]; lp = v + c.s[1]; c.s[1] = lp + v;
        hp = y1 - lp;
        const float y2 = ((2.0f + a) * hp - y1 - a * (A4 * B3 + B4)) / (1.0f + a * A3 * A4);
        // stages 3 and 4
        v = (y2 - c.s[2]) * G[2]; lp = v + c.s[2]; c.s[2] = lp + v;
        hp = y2 - lp;
        const float y3 = 2.0f * hp - y2;
        v = (y3 - c.s[3]) * G[3]; lp = v + c.s[3]; c.s[3] = lp + v;
        hp = y3 - lp;
        const float y4 = 2.0f * hp - y3;
        const float wet = y4;
        thru = lo + xh;
        return lo + mk * ((1.0f - kPhaseWet * w) * xh + kPhaseWet * w * wet);
    }

    // the lamp and the photocells (shared by the four stages and both channels)
    void stepLamp (float depthAmt, float m)
    {
        if (std::abs (colorS - lampColor) > 1.0e-4f) updateVibeTimes();   // COLOR moving: the lamp's times follow it
        const float drive = 0.5f + depthAmt * (m - 0.5f);   // 0..1 (DEPTH 0: the lamp rests in the middle)
        const float p = drive * drive;                      // power
        const float light = [&] { return std::pow (std::max (lampT, 0.0f), lightExp); } ();
        if (lampSettle)
        {
            lampT = p; ldrG = std::pow (std::max (p, 0.0f), lightExp); lampSettle = false;
        }
        else
        {
            lampT += (p - lampT) * (p > lampT ? lampHeat : lampCool);
            ldrG += (light - ldrG) * (light > ldrG ? ldrOn : ldrOff);
        }
        const float r = kRMin * std::pow ((1.0f + kLdrG0) / (std::max (ldrG, 0.0f) + kLdrG0), kLdrGamma);
        lastR = r;
        for (int k = 0; k < 4; ++k)
        {
            const float g = warp (thetaVibe[k] / r);
            vibeG[k] = g / (1.0f + g);
        }
    }

    float vibeSample (Chan& c, float x, float w, float mk, float& thru) const
    {
        float lo, xh;
        split (c, x, lo, xh);
        float y = xh;
        for (int k = 0; k < 4; ++k)
        {
            const float v = (y - c.s[k]) * vibeG[k], lp = v + c.s[k];
            c.s[k] = lp + v;
            const float hp = y - lp;
            y = lp - vibeGc * hp;   // the phase splitter: emitter (low side) and collector (high side) not quite equal
        }
        thru = lo + xh;
        return lo + mk * ((1.0f - kVibeWet * w) * xh + kVibeWet * w * y);
    }

    static constexpr int kHold = 4096;
    double fs = 48000.0;
    float smK = 0.001f, depthT = 10.0f, depthS = 10.0f, colorT = 5.0f, colorS = 5.0f, hz = 1.0f, mk = 1.0f;
    float splitG = 0.01f, splitH = 1.0f, thetaPhase = 0.2f, thetaVibe[4] {};
    float hzNow = 1.0f;   // the sweep's speed in Hz (set by run() before makeup)
    float lastR = 10000.0f, phG = 0.1f, vibeG[4] {}, lampT = 0.25f, ldrG = 0.1f, lampHeat = 0.01f, lampCool = 0.01f, ldrOn = 0.01f, ldrOff = 0.01f, lightExp = 1.6f, vibeGc = 0.88f;
    int vibeStep = 4, vibeCount = 0;
    float lampColor = -1.0f;   // the COLOR the lamp's times were made for
    bool lampSettle = true, mkSnap = true;
    Chan ch[2];
    float through[2][kHold] {};   // the crossover's sound (low + high, = DEPTH 0) of this part: the type change fades from / to it
    // clock
    double cyc = 0.0, hostPpq = 0.0, hostBpm = 120.0, hostTime = 0.0, expectTime = 0.0, barBeats = 4.0, barStart = -1.0;
    long long barIdx = -1;
    int rate = 2, jumpLen = 480, jumpLeft = 0;
    float jumpOffset = 0.0f, lastM = 0.0f, frozen = -1.0f;
    bool sync = true, playing = false, wasPlaying = false, first = true, clockFresh = false;
    // type change
    enum State { steady, fadingOut, silentSwitch, fadingIn };
    State state = steady;
    int current = phase, want = phase, pos = 0, fadeOutLen = 240, fadeInLen = 480;
    std::atomic<float> shown { 0.0f };
};
} // namespace pedal
