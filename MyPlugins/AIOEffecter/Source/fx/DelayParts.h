#pragma once
// ディレイ系の部品（DelayKit の部品を組み合わせる）。2026-11-19〜
#include "DelayKit.h"
#include "TwinDelay.h"   // 音符の表（SYNC）
#include "SpringReverb.h"   // TAPE HEADS の中のスプリング
#include "VerbEngine.h"     // Svf（TPT の状態変数フィルター）

namespace aiofx
{
// ---- WARP DELAY（Guitar Rig の Psyche Delay を測って合わせ込み）----
// 測った結果（cap/verb/PsycheDelay、インパルスのこだま）:
//   TIME L = 5.02 × 10^(2.6 × つまみ) ms（0 → 5 ms、0.5 → 100 ms、1 → 2 秒）。ST TIME で R = L × (1 − 0.5 × つまみ)
//   FEEDBACK = くり返しの大きさそのまま（0.25 → −12 dB、0.5 → −6 dB）。CROSS ON（本物の初期値）で左右へ交互に（ピンポン）
//   WET はエネルギーで混ぜる（ドライ cos、ウェット sin）。こだまは帯域を削らない（平ら）
//   REVERSE: こだまを逆回し（窓 = TIME）、ST REVERSE: 右だけ逆回し。PITCH: くり返すたびに音程が変わる（0.5 で変化なし、0 / 1 で ±1 オクターブ）、
//   DETUNE: くり返し（2 回目から）を少しずらした 2 つに分ける（0.5 で変化なし。最初のこだまはそのまま）。PITCH・DETUNE・REVERSE の細かい動きは大まか（細かい測定で詰める）
struct WarpDelay final : Module
{
    static float timeMs (float v) { return 5.0225f * std::pow (10.0f, 2.6f * std::clamp (v, 0.0f, 1.0f)); }
    static std::string timeText (float v, const float* all)
    {
        if (all != nullptr && all[10] >= 0.5f) return TwinDelay::noteName (v);
        return fmtTime (timeMs (v) * 0.001f);
    }
    static float pitchSemis (float v) { return (std::clamp (v, 0.0f, 1.0f) - 0.5f) * 24.0f; }
    static float detuneCents (float v) { return std::abs (std::clamp (v, 0.0f, 1.0f) - 0.5f) * 2.0f * 30.0f; }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "warpdly", "WARP DELAY", "DELAY",
            "Delay that can play its echoes backwards (REVERSE) or shift their pitch with every repeat (PITCH).",
            {
                { "TIME",       0.78751f, 0, [] (float v) { return fmtTime (timeMs (v) * 0.001f); }, &timeText },
                { "FEEDBACK",   0.35096f, 0, [] (float v) { return fmtPct (v); } },
                { "WET",        0.21763f, 0, [] (float v) { return fmtPct (v); } },
                { "ST TIME",    0.61f,    0, [] (float v) { return fmtPct (v); } },
                { "CROSS",      1.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "REVERSE",    0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "ST REVERSE", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "DETUNE",     0.5f,     0, [] (float v) { return fmt (detuneCents (v), 0, " ct"); } },
                { "ST DETUNE",  0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "PITCH",      0.5f,     0, [] (float v) { return fmt (pitchSemis (v), 1, " st"); } },
                { "SYNC",       0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (2.2 * sr) + 64);
        for (auto* s : { &sTimeL, &sTimeR }) s->prepare (sr, 0.08);
        for (auto* s : { &sFb, &sWet, &sDry, &sDet, &sRev, &sRevR, &sCross }) s->prepare (sr, 0.02);
        sPitch.prepare (sr, 0.1);   // 音程は 100 ms でなめらかに（一気に ±1 オクターブ動かすと読み口の速さが急に変わってザラッとしていた）
        for (auto& t : tap) t.prepare (sr, 0.04);
        for (auto& d : dcb) d.prepare (sr, 5.0);
        recalc (true);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& d : dcb) d.reset();
        for (auto& g : grain) g.ph = 0.0f;
        for (auto& g : det) g.ph = 0.0f;
        for (auto& g : ptap) g.ph = 0.0f;
        fbL = fbR = 0.0f;
        for (auto& t : tap) t.snapNow();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 11 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float tl = sTimeL.next(), tr = sTimeR.next(), fb = sFb.next(), wet = sWet.next(), dry = sDry.next();
            const float pit = sPitch.next(), dt = sDet.next(), rv = sRev.next(), rvR = sRevR.next(), cx = sCross.next();
            const float inL = L[s], inR = R[s];
            // 輪: CROSS ON は反対側の出口を入れる（ピンポン）。切り替えは混ぜ具合で
            const float backL = fbL + cx * (fbR - fbL), backR = fbR + cx * (fbL - fbR);
            // FEEDBACK 100 % で減らない輪に音を足し続けても大きくなりすぎないよう、輪の入口は 0 dBFS までは素通し・その上はなめらかに ±2 で頭打ち。
            // 戻りには直流止め 5 Hz（FEEDBACK 100 % で直流が −40 → −6 dBFS まで育っていた: DSP 検査 0.4.0）
            line[0].write (flush (softKnee2 (inL + fb * dcb[0].process (backL))));
            line[1].write (flush (softKnee2 (inR + fb * dcb[1].process (backR))));
            float yl = readTap (0, tl, rv, pit);
            float yr = readTap (1, tr, std::max (rv, rvR), pit);
            // DETUNE は輪に戻す音だけ（2 回目から）: 少し上と少し下に音程を変えた 2 つを混ぜる
            if (dt > 0.1f)
            {
                const float win = (float) (0.03 * sr), ca = std::pow (2.0f, dt / 1200.0f), cb = 1.0f / ca;
                fbL = 0.5f * (det[0].processAt (line[0], ca, win, tl) + det[1].processAt (line[0], cb, win, tl));
                fbR = 0.5f * (det[2].processAt (line[1], stDetune ? cb : ca, win, tr) + det[3].processAt (line[1], stDetune ? ca : cb, win, tr));
                fbL += (1.0f - std::min (1.0f, dt / 3.0f)) * (yl - fbL); fbR += (1.0f - std::min (1.0f, dt / 3.0f)) * (yr - fbR);
            }
            else { fbL = yl; fbR = yr; }
            L[s] = dry * inL + wet * yl;
            R[s] = dry * inR + wet * yr;
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    // 1 チャンネルのこだま: 普通 / 逆回し（窓 = TIME）を混ぜ、音程（PITCH）
    float readTap (int c, float tSamp, float rv, float pit)
    {
        const auto& d = line[(size_t) c];
        float y = tap[c].read (d);   // 普通の読みは、長さが変わったら 40 ms のクロスフェード（すべらせない）
        if (rv > 1.0e-4f)
        {
            const float yr = grain[(size_t) c].process (d, 1.0f, std::max (64.0f, tSamp), true);
            y += rv * (yr - y);
        }
        if (std::abs (pit) > 0.01f)
        {
            // 音程を変えた読み（窓 60 ms）。輪の中なので、くり返すたびに積み重なる
            const float win = (float) (0.06 * sr);
            const float yp = ptap[(size_t) c].processAt (d, std::pow (2.0f, pit / 12.0f), win, tSamp);
            y += std::min (1.0f, std::abs (pit) / 0.5f) * (yp - y);
        }
        return y;
    }
    void recalc (bool snap)
    {
        float tlm = timeMs (k[0]);
        if (k[10] >= 0.5f) tlm = (float) (TwinDelay::noteQuarters (k[0]) * 60000.0 / std::max (20.0, tempo));
        tlm = std::min (tlm, 2000.0f);
        const float trm = tlm * (1.0f - 0.5f * k[3]);
        const float wetG = std::sin (1.5707963f * k[2]), dryG = std::cos (1.5707963f * k[2]);
        sTimeL.set (tlm * 0.001f * (float) sr, snap); sTimeR.set (trm * 0.001f * (float) sr, snap);
        tap[0].set (tlm * 0.001f * (float) sr, snap); tap[1].set (trm * 0.001f * (float) sr, snap);
        // FEEDBACK 100 % で輪の利得はちょうど 1（4 点補間の大きさの最大は 1.0、直流止めは (1 + R) / 2 で 1 以下）。鳴り続けて大きくはならない
        sFb.set (std::clamp (k[1], 0.0f, 1.0f), snap); sWet.set (wetG, snap); sDry.set (dryG, snap);
        sPitch.set (pitchSemis (k[9]), snap); sDet.set (detuneCents (k[7]), snap);
        sRev.set (k[5] >= 0.5f ? 1.0f : 0.0f, snap); sRevR.set (k[6] >= 0.5f ? 1.0f : 0.0f, snap);
        sCross.set (k[4] >= 0.5f ? 1.0f : 0.0f, snap); stDetune = k[8] >= 0.5f;
    }

    // 音程を変える読み口（Grain2 と同じ考え。遅れの中心を TIME に置く）
    struct PitchTap
    {
        float ph = 0.0f;
        float processAt (const FracDelay& d, float ratio, float win, float center)
        {
            ph += (1.0f - ratio) / win; ph -= std::floor (ph);
            float y = 0.0f;
            for (int q = 0; q < 2; ++q)
            {
                float p = ph + 0.5f * (float) q; p -= std::floor (p);
                const float dl = std::max (2.0f, center - 0.5f * win + p * win);
                const float w = std::sin ((float) kPiD * p);
                y += w * w * d.read (dl);
            }
            return y;
        }
    };

    double sr = 48000.0, tempo = 120.0;
    float k[11] { 0.78751f, 0.35096f, 0.21763f, 0.61f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f };
    std::array<FracDelay, 2> line;
    std::array<Grain2, 2> grain;
    std::array<PitchTap, 4> det;
    std::array<PitchTap, 2> ptap;
    Smooth sTimeL, sTimeR, sFb, sWet, sDry, sPitch, sDet, sRev, sRevR, sCross;
    XTap tap[2];
    DcBlock dcb[2];
    float fbL = 0.0f, fbR = 0.0f;
    bool stDetune = false;
};

// ---- BBD ECHO（Guitar Rig の Delay Man を測って合わせ込み）----
// 測った結果（cap/verb/DelayMan、ノイズの塊のこだま）:
//   INPUT = ドライにもウェットにもかかる音量 −20 + 40 × つまみ dB（初期値 0.604 で +4.2 dB）
//   TIME = 28 + 975.7 × つまみ ms。MIX はドライ (1 − m)、ウェット m（直線）。ウェット 100 % のこだま = 入口の −10.9 dB（INPUT のあと）
//   TREBLE = こだまの 6 次のローパス（0 / .25 / .5 / .75 / 1 → 0.6 / 1.55 / 3.5 / 8 / 14 kHz）と、こだまの音量（−8〜+12 dB）。BASS = 400 Hz の低い棚（0 で −17 dB、1 で +3 dB）。
//   本物はモノ（左右のこだまが同じ）→ 揺れも左右で同じにする
//   FEEDBACK = 輪の大きさ 1.26 × つまみ^2.4（0.75 → −4 dB / 回、1 → +2 dB / 回で発振）。小さい音ほど輪の中で速く消える
//   （昔の機材のコンパンダーの癖。輪の中の音の山が −27 dBFS より小さいと 1 dB につき 2.2 dB 余分に下げる）。発振はやわらかい頭打ちで止める
//   DEPTH・MODE（CHORUS / VIBRATO）・RATE は遅れを揺らす（深さ 8 ms × DEPTH）。揺れの速さ・ACCEL（時間を変えたときの追いかけ）は大まか
struct BbdEcho final : Module
{
    static float inDb (float v) { return -20.0f + 40.0f * v; }
    static float timeMs (float v) { return 28.0f + 975.7f * std::clamp (v, 0.0f, 1.0f); }
    static float trebleHz (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 2.778f, 3.190f, 3.544f, 3.903f, 4.146f };
        return std::pow (10.0f, interpTable (xs, ys, v));
    }
    static float bassDb (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { -17.3f, -9.4f, -2.8f, 1.2f, 2.6f };
        return interpTable (xs, ys, v);
    }
    // TREBLE はこだまの音量も変える（ノイズの塊の 63〜500 Hz、初期値との差）
    static float trebleDb (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { -8.1f, -3.9f, 1.1f, 6.3f, 11.8f };
        return interpTable (xs, ys, v);
    }
    static float chorusHz (float v) { return 0.1f * std::pow (30.0f, v); }
    static float vibratoHz (float v) { return std::pow (10.0f, v); }
    static std::string timeText (float v, const float* all)
    {
        if (all != nullptr && all[11] >= 0.5f) return TwinDelay::noteName (v);
        return fmt (timeMs (v), 0, " ms");
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "bbdecho", "BBD ECHO", "DELAY",
            "Dark analog bucket-brigade echo with chorus / vibrato. High FEEDBACK runs away into oscillation.",
            {
                { "INPUT",     0.60417f, 0, [] (float v) { return fmt (inDb (v), 1, " dB"); } },
                { "TIME",      0.52726f, 0, [] (float v) { return fmt (timeMs (v), 0, " ms"); }, &timeText },
                { "FEEDBACK",  0.43712f, 0, [] (float v) { return fmtPct (v); } },
                { "MIX",       0.18f,    0, [] (float v) { return fmtPct (v); } },
                { "TREBLE",    0.44258f, 0, [] (float v) { return fmtHz (trebleHz (v)); } },
                { "BASS",      0.64727f, 0, [] (float v) { return fmt (bassDb (v) - bassDb (0.64727f), 1, " dB"); } },
                { "DEPTH",     0.16f,    0, [] (float v) { return fmtPct (v); } },
                { "MODE",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "VIBRATO" : "CHORUS"); } },
                { "CHORUS RT", 0.18867f, 0, [] (float v) { return fmt (chorusHz (v), 2, " Hz"); } },
                { "VIBRATO RT",0.5f,     0, [] (float v) { return fmt (vibratoHz (v), 2, " Hz"); } },
                { "ACCEL",     0.50781f, 0, [] (float v) { return fmtPct (v); } },
                { "SYNC",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (1.1 * sr) + 64);
        for (auto* s : { &sIn, &sFb, &sWet, &sDry, &sDepth }) s->prepare (sr, 0.02);
        sTime.prepare (sr, 0.05);
        for (auto* q : { &sTreb, &sBass }) q->prepare (sr, 0.03);
        for (auto& d : dcb) d.prepare (sr, 10.0);
        envA = tauCoef (sr, 0.01); envR = tauCoef (sr, 0.1);
        recalc (true); tone (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : sh) f.reset();
        for (auto& d : dcb) d.reset();
        env[0] = env[1] = 0.0f; back[0] = back[1] = 0.0f;
        lfo.setPhase (0.0);
        tRead = sTime.cur;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 12 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& f : lp) f.step();
            for (auto& f : sh) f.step();
            const float g = sIn.next(), fb = sFb.next(), wet = sWet.next(), dry = sDry.next(), dep = sDepth.next();
            // 読む位置が逆回りしないように、遅れの伸びは 1 サンプルあたり 0.9 まで・縮みは 3 まで（音程 0.1〜4 倍。本物の BBD は逆回りしない: DSP 検査 0.4.0）
            tRead += std::clamp (sTime.next() - tRead, -3.0f, 0.9f);
            const float t = tRead;
            const float m1 = lfo.next();
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][s] * g;
                // 輪の中の音の大きさ（コンパンダーの癖: 小さい音ほど余分に下げる）
                const float b = dcb[c].process (back[c]);   // 戻りに直流止め 10 Hz（本物の BBD はコンデンサーで直流が切れる。発振の 2〜3 割が 20 Hz 以下だった: DSP 検査 0.4.0）
                const float a = std::abs (b);
                env[c] = a > env[c] ? a + (env[c] - a) * envA : a + (env[c] - a) * envR;
                const float lv = levelDb (env[c] * 1.414f);
                const float ex = dbToGain (-2.2f * std::max (0.0f, -27.0f - lv));
                line[(size_t) c].write (flush (x + satSoft (fb * ex * b * 2.0f) * 0.5f));
                const float d = t + dep * m1;
                float y = line[(size_t) c].read (std::max (2.0f, d));
                y = lp[(size_t) (3 * c + 2)].process (lp[(size_t) (3 * c + 1)].process (lp[(size_t) (3 * c)].process (y)));
                y = sh[(size_t) c].process (y);
                back[c] = y;
                io[c][s] = dry * x + wet * y;
            }
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    void recalc (bool snap)
    {
        float tms = timeMs (k[1]);
        if (k[11] >= 0.5f) tms = std::min (1000.0f, (float) (TwinDelay::noteQuarters (k[1]) * 60000.0 / std::max (20.0, tempo)));
        const float accelTau = 0.03f + 0.6f * (1.0f - k[10]) * (1.0f - k[10]);
        sTime.prepare (sr, accelTau);
        sTime.set (tms * 0.001f * (float) sr, snap);
        sIn.set (dbToGain (inDb (k[0])), snap);
        sFb.set (1.26f * std::pow (std::clamp (k[2], 0.0f, 1.0f), 2.4f), snap);
        sWet.set (std::clamp (k[3], 0.0f, 1.0f) * 0.2851f * dbToGain (trebleDb (k[4]) - trebleDb (0.44258f)), snap);
        sDry.set (1.0f - std::clamp (k[3], 0.0f, 1.0f), snap);
        sDepth.set (8.0e-3f * (float) sr * k[6], snap);
        const bool vib = k[7] >= 0.5f;
        lfo.setRate (sr, vib ? vibratoHz (k[9]) : chorusHz (k[8]));
        sTreb.set (std::log (std::min (trebleHz (k[4]), 0.45f * (float) sr)), snap);
        sBass.set (bassDb (k[5]) - bassDb (0.64727f), snap);
    }
    // 6 次のバターワース（Q 0.518 / 0.707 / 1.932）と 400 Hz の低い棚（傾き S 0.5 の RBJ の棚 = Q 1 / (√A + 1 / √A)）。
    // TPT の状態変数フィルターで、なめらかにしたつまみから 32 サンプルごとに（係数を動かしてもプチッとしにくい。伝達関数は前の 2 次フィルターと同じ）
    void tone (bool snap)
    {
        float tl = sTreb.cur, bd = sBass.cur;
        if (! snap) { for (int i = 0; i < 32; ++i) { tl = sTreb.next(); bd = sBass.next(); } }
        if (! snap && same (tl, lastTone[0]) && same (bd, lastTone[1]))
        {
            // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
            for (auto& f : lp) f.ramp (true, 1.0f);
            for (auto& f : sh) f.ramp (true, 1.0f);
            return;
        }
        lastTone[0] = tl; lastTone[1] = bd;
        static const double qs[3] { 0.5176, 0.7071, 1.9319 };
        const double fc = std::exp ((double) tl), A = std::pow (10.0, (double) bd / 40.0), qsh = 1.0 / (std::sqrt (A) + 1.0 / std::sqrt (A));
        for (int c = 0; c < 2; ++c)
        {
            for (int j = 0; j < 3; ++j) { lp[(size_t) (3 * c + j)].lp (sr, fc, qs[j]); lp[(size_t) (3 * c + j)].ramp (snap, 32.0f); }
            sh[(size_t) c].lowShelf (sr, 400.0, bd, qsh); sh[(size_t) c].ramp (snap, 32.0f);
        }
    }

    double sr = 48000.0, tempo = 120.0;
    float k[12] { 0.60417f, 0.52726f, 0.43712f, 0.18f, 0.44258f, 0.64727f, 0.16f, 0.0f, 0.18867f, 0.5f, 0.50781f, 0.0f };
    std::array<FracDelay, 2> line;
    std::array<Svf, 6> lp;
    std::array<Svf, 2> sh;
    Phasor lfo;
    Smooth sIn, sFb, sWet, sDry, sDepth, sTime, sTreb, sBass;
    DcBlock dcb[2];
    float env[2] {}, back[2] {}, envA = 0.0f, envR = 0.0f, lastTone[2] { -99.0f, -99.0f }, tRead = 2.0f;
    unsigned ctl = 0;
};

// ---- WIDE DELAY（Guitar Rig の Quad Delay を測って合わせ込み）----
// 測った結果（cap/verb/QuadDelay、ノイズの塊のこだま。左右の力を平均）:
//   TIME = 5.02 × 10^(2.6 × つまみ) ms（WARP DELAY と同じ）。WET はエネルギーで混ぜる（ドライ cos、ウェット sin）
//   こだまの音色: TREBLE = 1 次のローパス（0 / .25 / .5 / .75 / 1 → 110 / 300 / 830 / 2900 / 11700 Hz）、BASS = 1 次のハイパス（2500 / 800 / 148 / 30 / 10 Hz）。輪の中にもかかる
//   FEEDBACK: 輪の大きさ = つまみ × 0.91（× 音色 × 揺れの分）。500 Hz で 0.25 → −17.3、0.5 → −11.3、1 → −5.3 dB / 回
//   MOD DEPTH で左右の遅れが別々に揺れる（初期値で左右の相関 0.15、こだまが約 3 dB 小さくなる）。DIFFUSION ON: 輪の中でこだまをにじませる。INVERT: ウェットの極性を反転
struct WideDelay final : Module
{
    static float timeMs (float v) { return 5.0225f * std::pow (10.0f, 2.6f * std::clamp (v, 0.0f, 1.0f)); }
    static float lpHz (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 2.041f, 2.477f, 2.919f, 3.462f, 4.068f }; return std::pow (10.0f, interpTable (xs, ys, v)); }
    static float hpHz (float v) { static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 3.398f, 2.903f, 2.170f, 1.477f, 1.0f }; return std::pow (10.0f, interpTable (xs, ys, v)); }
    static float modHz (float v) { return 0.05f * std::pow (100.0f, v); }
    static std::string timeText (float v, const float* all)
    {
        if (all != nullptr && all[9] >= 0.5f) return TwinDelay::noteName (v);
        return fmtTime (timeMs (v) * 0.001f);
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "widedly", "WIDE DELAY", "DELAY",
            "Wide, modulated stereo delay. DIFFUSION smears the repeats into a cloud, TREBLE / BASS shape them.",
            {
                { "TIME",      0.83781f, 0, [] (float v) { return fmtTime (timeMs (v) * 0.001f); }, &timeText },
                { "FEEDBACK",  0.5f,     0, [] (float v) { return fmtPct (v); } },
                { "WET",       0.53333f, 0, [] (float v) { return fmtPct (v); } },
                { "DIFFUSION", 0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "INVERT",    0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "TREBLE",    0.5f,     0, [] (float v) { return fmtHz (lpHz (v)); } },
                { "BASS",      0.5f,     0, [] (float v) { return fmtHz (hpHz (v)); } },
                { "MOD RATE",  0.45556f, 0, [] (float v) { return fmt (modHz (v), 2, " Hz"); } },
                { "MOD DEPTH", 0.22223f, 0, [] (float v) { return fmtPct (v); } },
                { "SYNC",      0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (2.2 * sr) + 64);
        static const float apMs[4] { 61.0f, 97.0f, 139.0f, 191.0f };   // 本物の DIFFUSION は長くにじむ（1.5 秒後も同じくらいの大きさ）
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 4; ++j) { ap[c][j].prepare ((int) (apMs[j] * (c == 0 ? 1.0f : 1.13f) * 0.001 * sr) + 8); apLen[c][j] = (int) (apMs[j] * (c == 0 ? 1.0f : 1.13f) * 0.001 * sr); }
        for (auto& t : tap) t.prepare (sr, 0.04);
        for (auto* q : { &sFb, &sWet, &sDry, &sDepth, &sDiff, &sPol, &sLoss }) q->prepare (sr, 0.02);
        for (auto* q : { &sLp, &sHp }) q->prepare (sr, 0.03);
        recalc (true); tone (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& c : ap) for (auto& a : c) a.reset();
        for (auto& f : lpf) f.reset();
        for (auto& f : hpf) f.reset();
        back[0] = back[1] = 0.0f;
        lfo[0].setPhase (0.0); lfo[1].setPhase (2.2);
        for (auto& t : tap) t.snapNow();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 10 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (int c = 0; c < 2; ++c) { lpf[c].step(); hpf[c].step(); }
            const float fb = sFb.next(), wet = sWet.next(), dry = sDry.next(), dep = sDepth.next(), df = sDiff.next(), pol = sPol.next(), loss = sLoss.next();
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][s];
                line[(size_t) c].write (flush (x + fb * back[c]));
                // 長さが変わったら 2 つの読み口を 40 ms でクロスフェード（揺れは両方に）
                float y = tap[c].read (line[(size_t) c], dep * lfo[c].next());
                y = hpf[c].process (lpf[c].process (y)) * loss;
                // にじみ（オールパス 4 段）: いつも回して、DIFFUSION の ON / OFF は混ぜ具合で（古い中身が急に出ない）
                float u = y;
                for (int j = 0; j < 4; ++j)
                {
                    auto& a = ap[c][j];
                    const float dd = a.readInt (apLen[c][j]);
                    const float w = u + 0.7f * dd;
                    a.write (flush (w));
                    u = dd - 0.7f * w;
                }
                y += df * (u - y);
                back[c] = y;
                io[c][s] = dry * x + pol * wet * y;
            }
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    void recalc (bool snap)
    {
        float tms = timeMs (k[0]);
        if (k[9] >= 0.5f) tms = std::min (2000.0f, (float) (TwinDelay::noteQuarters (k[0]) * 60000.0 / std::max (20.0, tempo)));
        for (auto& t : tap) t.set (tms * 0.001f * (float) sr, snap);
        sFb.set (0.91f * std::clamp (k[1], 0.0f, 1.0f), snap);
        sWet.set (std::sin (1.5707963f * k[2]), snap); sDry.set (std::cos (1.5707963f * k[2]), snap);
        sDiff.set (k[3] >= 0.5f ? 1.0f : 0.0f, snap); sPol.set (k[4] >= 0.5f ? -1.0f : 1.0f, snap);
        sDepth.set (5.0e-3f * (float) sr * k[8], snap);
        sLoss.set (dbToGain (-3.3f * std::min (1.0f, k[8] / 0.15f)), snap);
        for (auto& p : lfo) p.setRate (sr, modHz (k[7]));
        sLp.set (std::log (std::min (lpHz (k[5]), 0.45f * (float) sr)), snap); sHp.set (std::log (hpHz (k[6])), snap);
    }
    // TREBLE（1 次のローパス）・BASS（1 次のハイパス）は TPT の形で、なめらかにした周波数から 32 サンプルごとに
    void tone (bool snap)
    {
        float lpL = sLp.cur, hpL = sHp.cur;
        if (! snap) { for (int i = 0; i < 32; ++i) { lpL = sLp.next(); hpL = sHp.next(); } }
        if (! snap && same (lpL, lastLp) && same (hpL, lastHp)) return;
        lastLp = lpL; lastHp = hpL;
        for (int c = 0; c < 2; ++c)
        {
            lpf[c].setOnePoleLP (sr, std::exp (lpL)); hpf[c].setOnePoleHP (sr, std::exp (hpL));   // 合わせ込んだときの OnePole と同じ伝達関数
            lpf[c].ramp (snap, 32); hpf[c].ramp (snap, 32);
        }
    }

    double sr = 48000.0, tempo = 120.0;
    float k[10] { 0.83781f, 0.5f, 0.53333f, 0.0f, 0.0f, 0.5f, 0.5f, 0.45556f, 0.22223f, 0.0f };
    std::array<FracDelay, 2> line;
    FracDelay ap[2][4];
    int apLen[2][4] {};
    XTap tap[2];
    TptShelf1 lpf[2], hpf[2];
    Phasor lfo[2];
    Smooth sFb, sWet, sDry, sDepth, sDiff, sPol, sLoss, sLp, sHp;
    float back[2] {}, lastLp = -1.0f, lastHp = -1.0f;
    unsigned ctl = 0;
};

// ---- CLUB DELAY（Guitar Rig の Traktor Delay を測って合わせ込み）----
// 測った結果（cap/verb/TraktorDelay。本物の初期値は MUTE ON なので、MUTE OFF で測り直した cap/list7・list9）:
//   TIME（テンポに合わせる。120 BPM で測った）: つまみを 16 等分した段ごとに 4 / 4 / 1.5 / 1.5 / 1 / 1 / 1 / 0.75 / 0.75 / 0.75 / 0.5 / 0.5 / 0.25 / 0.25 / 0.25 / 0.125 / 0.125 拍。
//   SPREAD ON（初期値）で右は 1 段長い音符（0.125 → 0.125、0.25 → 0.5、0.5 → 0.75、0.75 → 1、1 → 1.5、1.5 → 2、4 → 4）。FREE ON は ms（0 → 2 秒、0.75 → 63 ms、1 → 33 ms、右は 1.5 倍）
//   FEEDBACK: 輪の大きさ 0 / 0.18 / 0.25 / 0.5 / 0.75 / 1 → 0 / 0.257 / 0.343 / 0.62 / 0.84 / 1.0（1 で減らない）
//   FILTER: 0.5 で素通し。左へ回すと 1 次のローパス（0.25 で 1.4 kHz、0 で 25 Hz）、右へ回すと 1 次のハイパス（0.75 で 150 Hz、1 で 5 kHz）。輪の中にもかかる
//   MIX: ドライ min(1, 2 (1 − m))²、ウェット 0.25 / 0.5 / 0.75 / 1 → −8.0 / −3.4 / −1.4 / −0.9 dB。FREEZE: 入口を閉じて、今の中身を減らさずに回し続ける
struct ClubDelay final : Module
{
    static float noteQ (float v)
    {
        static const float q[17] { 4.0f, 4.0f, 1.5f, 1.5f, 1.0f, 1.0f, 1.0f, 0.75f, 0.75f, 0.75f, 0.5f, 0.5f, 0.25f, 0.25f, 0.25f, 0.125f, 0.125f };
        return q[std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 16.0f), 0, 16)];
    }
    static float nextQ (float q)
    {
        if (q <= 0.125f) return 0.125f;
        if (q <= 0.25f) return 0.5f;
        if (q <= 0.5f) return 0.75f;
        if (q <= 0.75f) return 1.0f;
        if (q <= 1.0f) return 1.5f;
        if (q <= 1.5f) return 2.0f;
        return 4.0f;
    }
    static const char* noteName (float q)
    {
        if (q >= 4.0f) return "1 BAR";
        if (q >= 2.0f) return "1/2";
        if (q >= 1.5f) return "1/4.";
        if (q >= 1.0f) return "1/4";
        if (q >= 0.75f) return "1/8.";
        if (q >= 0.5f) return "1/8";
        if (q >= 0.25f) return "1/16";
        return "1/32";
    }
    static float freeMs (float v)
    {
        static const float xs[5] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[5] { 3.302f, 2.805f, 2.308f, 1.796f, 1.512f };
        return std::pow (10.0f, interpTable (xs, ys, v));
    }
    static std::string timeText (float v, const float* all)
    {
        if (all != nullptr && all[4] >= 0.5f) return fmt (freeMs (v), 0, " ms");
        return noteName (noteQ (v));
    }
    static float fbGain (float v)
    {
        static const float xs[6] { 0.0f, 0.18351f, 0.25f, 0.5f, 0.75f, 1.0f }, ys[6] { 0.0f, 0.257f, 0.343f, 0.62f, 0.84f, 1.0f };
        return interpTable (xs, ys, v);
    }
    static float wetDb (float v)
    {
        static const float xs[6] { 0.0f, 0.25f, 0.36436f, 0.5f, 0.75f, 1.0f }, ys[6] { -60.0f, -8.0f, -5.4f, -3.4f, -1.4f, -0.9f };
        return interpTable (xs, ys, v);
    }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "clubdly", "CLUB DELAY", "DELAY",
            "Tempo-synced DJ delay. FILTER: left = darker, right = thinner. FREEZE holds the current echoes.",
            {
                { "TIME",     0.5f,     0, [] (float v) { return std::string (noteName (noteQ (v))); }, &timeText },
                { "FEEDBACK", 0.18351f, 0, [] (float v) { return fmtPct (v); } },
                { "FILTER",   0.5f,     0, [] (float v) { return v < 0.49f ? std::string ("LP ") + fmtPct ((0.5f - v) * 2.0f) : v > 0.51f ? std::string ("HP ") + fmtPct ((v - 0.5f) * 2.0f) : std::string ("OFF"); } },
                { "MIX",      0.36436f, 0, [] (float v) { return fmtPct (v); } },
                { "FREE",     0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "FREEZE",   0.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "SPREAD",   1.0f,     2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (4.2 * sr) + 64);
        for (auto& t : tap) t.prepare (sr, 0.04);
        for (auto* q : { &sFb, &sWet, &sDry, &sIn, &sMLp, &sMHp }) q->prepare (sr, 0.02);
        for (auto* q : { &sLp, &sHp }) q->prepare (sr, 0.03);
        for (auto& d : dcb) d.prepare (sr, 5.0);
        recalc (true); tone (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& f : lpf) f.reset();
        for (auto& f : hpf) f.reset();
        for (auto& d : dcb) d.reset();
        back[0] = back[1] = 0.0f;
        for (auto& t : tap) t.snapNow();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 7 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void process (float* L, float* R, int n) override
    {
        float* io[2] { L, R };
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (int c = 0; c < 2; ++c) { lpf[c].step(); hpf[c].step(); }
            const float fb = sFb.next(), wet = sWet.next(), dry = sDry.next(), in = sIn.next(), mLp = sMLp.next(), mHp = sMHp.next();
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][s];
                // FEEDBACK 100 % で足し続けても大きくなりすぎない（0 dBFS までは素通し、その上はなめらかに ±2 で頭打ち）。戻りに直流止め 5 Hz
                line[(size_t) c].write (flush (softKnee2 (in * x + fb * dcb[c].process (back[c]))));
                float y = tap[c].read (line[(size_t) c]);   // 長さが変わったら 40 ms のクロスフェード
                // FILTER: ローパスとハイパスはいつも回して、効き具合を混ぜる（真ん中で両方 0 = 素通し。入る／外れるでプチッとしない）
                y += mLp * (lpf[c].process (y) - y);
                y += mHp * (hpf[c].process (y) - y);
                back[c] = y;
                io[c][s] = dry * x + wet * y;
            }
        }
    }
    double tailSeconds() const override { return 20.0; }

private:
    void recalc (bool snap)
    {
        const float bpmQ = (float) (60000.0 / std::max (20.0, tempo));
        float tl, tr;
        if (k[4] >= 0.5f) { tl = freeMs (k[0]); tr = tl * (k[6] >= 0.5f ? 1.5f : 1.0f); }
        else { const float q = noteQ (k[0]); tl = q * bpmQ; tr = (k[6] >= 0.5f ? nextQ (q) : q) * bpmQ; }
        tl = std::min (tl, 4000.0f); tr = std::min (tr, 4000.0f);
        tap[0].set (tl * 0.001f * (float) sr, snap); tap[1].set (tr * 0.001f * (float) sr, snap);
        const bool frz = k[5] >= 0.5f;
        sFb.set (frz ? 1.0f : fbGain (k[1]), snap);
        sIn.set (frz ? 0.0f : 1.0f, snap);
        static const float fx[6] { 0.0f, 0.18351f, 0.25f, 0.5f, 0.75f, 1.0f }, fy[6] { 0.0f, 0.0f, -0.8f, 2.2f, 1.9f, 1.4f };
        sWet.set (dbToGain (wetDb (k[3]) + interpTable (fx, fy, k[1])), snap);
        const float dd = std::min (1.0f, 2.0f * (1.0f - k[3])); sDry.set (dd * dd, snap);
        const float f = k[2];
        // ローパスは 0.495 から下、ハイパスは 0.505 から上（測った式）。外れている側の周波数は端（0.495 / 0.505 の値 = ほぼ素通し）に置く
        // （作ってから一度も回していないと 1 Hz のままで、初めて左へ回した瞬間にこだまが 20 ms −8 dB 落ちていた: DSP 検査 0.4.0）
        sMLp.set (f < 0.495f ? 1.0f : 0.0f, snap); sMHp.set (f > 0.505f ? 1.0f : 0.0f, snap);
        const float lpHz = std::min (std::pow (10.0f, 1.398f + 6.99f * std::min (f, 0.495f)), 0.45f * (float) sr);
        const float hpHz = std::pow (10.0f, 0.653f + (std::max (f, 0.505f) - 0.5f) / 0.5f * 3.046f);
        sLp.set (std::log (lpHz), snap); sHp.set (std::log (hpHz), snap);
    }
    void tone (bool snap)
    {
        float lpL = sLp.cur, hpL = sHp.cur;
        if (! snap) { for (int i = 0; i < 32; ++i) { lpL = sLp.next(); hpL = sHp.next(); } }
        if (! snap && same (lpL, lastLp) && same (hpL, lastHp)) return;
        lastLp = lpL; lastHp = hpL;
        for (int c = 0; c < 2; ++c)
        {
            lpf[c].setOnePoleLP (sr, std::exp (lpL)); hpf[c].setOnePoleHP (sr, std::exp (hpL));   // 合わせ込んだときの OnePole と同じ伝達関数
            lpf[c].ramp (snap, 32); hpf[c].ramp (snap, 32);
        }
    }

    double sr = 48000.0, tempo = 120.0;
    float k[7] { 0.5f, 0.18351f, 0.5f, 0.36436f, 0.0f, 0.0f, 1.0f };
    std::array<FracDelay, 2> line;
    XTap tap[2];
    TptShelf1 lpf[2], hpf[2];
    DcBlock dcb[2];
    Smooth sFb, sWet, sDry, sIn, sMLp, sMHp, sLp, sHp;
    float back[2] {}, lastLp = -1.0f, lastHp = -1.0f;
    unsigned ctl = 0;
};

// ---- STYLE DELAY（Guitar Rig の Replika GR を測って合わせ込み）----
// 測った結果（cap/verb/ReplikaGR、ノイズの塊）: TIME MODE NOTE（初期値）で TIME は 1/64・1/32・1/16・1/8・1/4・1/2・1 小節の 7 段（= 0.0625 × 2^round(8 × つまみ) 拍）、
//   DOTTED は 1.5 倍、TRIPLET は 2/3。MS は大まか（0.5275 で 1052 ms）。STEREO MODE 1（初期値）で左右を ±3.75 ms ずらす、0 = モノ、2 = 右は 2 倍（ピンポン）
//   MIX はドライ min(1, 2 (1 − m))、ウェット min(1, 2 m)。FEEDBACK = 輪の大きさ 1.2 × つまみ（0.83 から上は発振、やわらかい頭打ち）
//   STYLE: MODERN = 平ら / ANALOG = −4 dB・5 kHz から暗い / TAPE = 10 kHz から / DIGITAL = 7 kHz から / DIFFUSE = にじむ。DEPTH・RATE・CHARACTER は大まか
//   LO CUT = 12 dB/oct のハイパス（0 / .5 / 1 → 15 / 120 / 1500 Hz）、HI CUT = 12 dB/oct のローパス（0 / .5 / .845 / 1 → 150 / 9000 / 18000 / 22000 Hz）。DUCK: 入る音が大きい間ウェットを下げる
struct StyleDelay final : Module
{
    static float noteQ (float v, float mode)
    {
        float q = 0.0625f * std::pow (2.0f, (float) std::min (6L, std::lround (std::clamp (v, 0.0f, 1.0f) * 8.0f)));
        const int m = std::clamp ((int) std::lround (mode * 3.0f), 0, 3);
        if (m == 2) q *= 1.5f; else if (m == 3) q *= 2.0f / 3.0f;
        return q;
    }
    static float msTime (float v) { return 10.0f * std::pow (200.0f, std::clamp (v, 0.0f, 1.0f)) * 1.0f; }
    static std::string timeText (float v, const float* all)
    {
        const float mode = all != nullptr ? all[2] : 1.0f / 3.0f;
        if (std::lround (mode * 3.0f) == 0) return fmt (msTime (v), 0, " ms");
        return fmt (noteQ (v, mode), 3, " beat");
    }
    static float hiHz (float v) { static const float xs[4] { 0.0f, 0.5f, 0.845f, 1.0f }, ys[4] { 2.176f, 3.954f, 4.255f, 4.342f }; return std::pow (10.0f, interpTable (xs, ys, v)); }
    static float loHz (float v) { static const float xs[3] { 0.0f, 0.5f, 1.0f }, ys[3] { 1.176f, 2.079f, 3.176f }; return std::pow (10.0f, interpTable (xs, ys, v)); }
    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "styledly", "STYLE DELAY", "DELAY",
            "Five delay styles in one: MODERN, ANALOG, TAPE, DIGITAL, DIFFUSE. DUCK lowers the echoes while you play.",
            {
                { "MIX",       0.065f,  0, [] (float v) { return fmtPct (v); } },
                { "TIME",      0.5275f, 0, [] (float v) { return fmt (noteQ (v, 1.0f / 3.0f), 3, " beat"); }, &timeText },
                { "TIME MODE", 1.0f / 3.0f, 4, [] (float v) { static const char* n[4] { "MS", "NOTE", "DOTTED", "TRIPLET" }; return std::string (n[std::clamp ((int) std::lround (v * 3.0f), 0, 3)]); } },
                { "FEEDBACK",  0.22917f, 0, [] (float v) { return fmtPct (v); } },
                { "STYLE",     0.0f,    5, [] (float v) { static const char* n[5] { "MODERN", "ANALOG", "TAPE", "DIGITAL", "DIFFUSE" }; return std::string (n[std::clamp ((int) std::lround (v * 4.0f), 0, 4)]); } },
                { "DEPTH",     0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "RATE",      0.15f,   0, [] (float v) { return fmt (0.1f * std::pow (50.0f, v), 2, " Hz"); } },
                { "CHARACTER", 0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "STEREO",    0.5f,    3, [] (float v) { static const char* n[3] { "MONO", "WIDE", "PING-PONG" }; return std::string (n[std::clamp ((int) std::lround (v * 2.0f), 0, 2)]); } },
                { "WIDTH",     0.5f,    0, [] (float v) { return fmtPct (v); } },
                { "PAN",       0.5f,    0, [] (float v) { return v < 0.49f ? fmt ((0.5f - v) * 200.0f, 0, " L") : v > 0.51f ? fmt ((v - 0.5f) * 200.0f, 0, " R") : std::string ("C"); } },
                { "LO CUT",    0.015f,  0, [] (float v) { return fmtHz (loHz (v)); } },
                { "HI CUT",    0.845f,  0, [] (float v) { return fmtHz (hiHz (v)); } },
                { "DUCK",      0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "DUCK SENS", 0.0f,    0, [] (float v) { return fmtPct (v); } },
                { "DUCK REL",  0.0f,    0, [] (float v) { return fmt (50.0f + 950.0f * v, 0, " ms"); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (6.2 * sr) + 64);
        static const float apMs[4] { 7.0f, 11.0f, 17.0f, 23.0f };
        for (int c = 0; c < 2; ++c) for (int j = 0; j < 4; ++j) ap[c][j].prepare ((int) (apMs[j] * 3.2 * 0.001 * sr) + 8);
        for (auto& t : tap) t.prepare (sr, 0.04);
        for (auto* q : { &sFb, &sWet, &sDry, &sDepth, &sPanL, &sPanR, &sWid, &sDrive, &sChr, &sCrush, &sDiff, &sDiffG, &sLfoMix, &sWobMix, &sPP }) q->prepare (sr, 0.02);
        for (auto& d : dcb) d.prepare (sr, 5.0);
        for (auto* q : { &sLp, &sHp, &sApScale }) q->prepare (sr, 0.03);
        envA = tauCoef (sr, 0.005);
        wf.prepare (sr, 0.7, 7.0);
        recalc (true); tone (true); reset();
    }
    void reset() override
    {
        for (auto& d : line) d.reset();
        for (auto& c : ap) for (auto& a : c) a.reset();
        for (auto& c : flt) for (auto& f : c) f.reset();
        for (auto& d : dcb) d.reset();
        back[0] = back[1] = 0.0f; env = 0.0f; duckG = 1.0f; hold[0] = hold[1] = 0.0f; holdN = 0;
        lfo[0].setPhase (0.0); lfo[1].setPhase (1.9);
        for (auto& t : tap) t.snapNow();
        wf.reset();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 16 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; recalc (false); } }
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            if ((ctl++ & 31) == 0) tone (false);
            for (auto& c : flt) for (auto& f : c) f.step();
            const float fb = sFb.next(), wet = sWet.next(), dry = sDry.next(), dep = sDepth.next();
            const float pl = sPanL.next(), pr = sPanR.next(), wd = sWid.next(), drive = sDrive.next(), chr = sChr.next(), crushMix = sCrush.next();
            const float dfMix = sDiff.next(), dg = sDiffG.next(), lfoMix = sLfoMix.next(), wobMix = sWobMix.next(), pp = sPP.next(), apS = sApScale.next();
            // ダッキング: 入る音の大きさで、ウェットを下げる
            const float a = std::max (std::abs (L[s]), std::abs (R[s]));
            env = a > env ? a + (env - a) * envA : a + (env - a) * duckRel;
            const float over = std::max (0.0f, levelDb (env) - duckThr);
            const float gT = dbToGain (-std::min (40.0f * duckAmt, over * duckAmt * 2.0f));
            duckG = gT + (duckG - gT) * 0.999f;
            float y[2];
            // スタイルの揺れ: MODERN・ANALOG・DIGITAL は LFO、TAPE はテープの揺れ。どちらもいつも回して、混ぜ具合で入れ替える
            const float wob = wobMix * wf.next (dep * 2.0f, dep * 0.3f);
            if (++holdN >= holdLen) holdN = 0;
            const float mono = 0.5f * (L[s] + R[s]);
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? L[s] : R[s];
                // ピンポン（STEREO 2）: 入口は左だけ、戻りは反対側から。切り替えは混ぜ具合で
                const float in = x + pp * ((c == 0 ? mono : 0.0f) - x);
                const float fbIn = back[c] + pp * (back[1 - c] - back[c]);
                // CHARACTER の歪み: なめらかに歪み始める satSoft（±2 で飽和）を CHARACTER の 0〜25 % で混ぜ始め、そこから drive を上げる。
                // CHARACTER 0 なら素通し。softKnee2 は 0 dBFS から上の安全のための頭打ち（FEEDBACK 120 % の発振を +6 dBFS で止める）
                const float u = in + fb * dcb[c].process (fbIn);   // 戻りに直流止め 5 Hz（LO CUT 0・FEEDBACK 100 % で 20 Hz より下がたまっていた: DSP 検査 0.4.0 の 2 回目）
                const float sat = 2.0f * satSoft (0.5f * u * drive) / std::max (1.0f, drive);
                line[(size_t) c].write (flush (softKnee2 (u + chr * (sat - u))));
                const float m = lfoMix * dep * lfo[c].next();
                float v = tap[c].read (line[(size_t) c], m + wob);   // 長さが変わったら 40 ms のクロスフェード
                v = colour (c, v, crushMix, dfMix, dg, apS);
                v = flt[c][2].process (flt[c][1].process (flt[c][0].process (v)));
                back[c] = v; y[c] = v;
            }
            // 広がり・パン
            const float mm = 0.5f * (y[0] + y[1]), sd = 0.5f * (y[0] - y[1]) * wd;
            // こだまの出口は 0 dBFS までは素通し、その上はなめらかに +6 dBFS で頭打ち（FEEDBACK 83 % から上の発振で、WIDTH を上げると +10 dBFS を超えていた）
            const float yl = softKnee2 ((mm + sd) * pl), yr = softKnee2 ((mm - sd) * pr);
            L[s] = dry * L[s] + wet * duckG * yl;
            R[s] = dry * R[s] + wet * duckG * yr;
        }
    }
    double tailSeconds() const override { return 12.0; }

private:
    // スタイルの色付け（ビット落とし・にじみ）。どちらもいつも計算して、混ぜ具合で入れる（スタイルを変えても古い中身が急に出ない）
    float colour (int c, float v, float crushMix, float dfMix, float dg, float apS)
    {
        // DIGITAL: 粗い時間（holdLen サンプルごとに値を保つ）・粗い段（CHARACTER で強く）
        if (holdN == 0) hold[c] = std::round (v * crushQ) / crushQ;
        v += crushMix * (hold[c] - v);
        // DIFFUSE: オールパス 4 段（長さは DEPTH で伸び縮み。端数の長さをなめらかに）
        static const float apMs[4] { 7.0f, 11.0f, 17.0f, 23.0f };
        float u = v;
        for (int j = 0; j < 4; ++j)
        {
            auto& a = ap[c][j];
            const float dd = a.read (std::max (1.0f, apMs[j] * apS * 0.001f * (float) sr));
            const float w = u + dg * dd;
            a.write (flush (w));
            u = dd - dg * w;
        }
        return v + dfMix * (u - v);
    }
    void recalc (bool snap)
    {
        const float bpmQ = (float) (60000.0 / std::max (20.0, tempo));
        const int tm = std::clamp ((int) std::lround (k[2] * 3.0f), 0, 3);
        float t = tm == 0 ? msTime (k[1]) : noteQ (k[1], k[2]) * bpmQ;
        t = std::min (t, 6000.0f);
        const int sm = std::clamp ((int) std::lround (k[8] * 2.0f), 0, 2);
        const bool pingpong = sm == 2;
        const float off = sm == 1 ? 3.75f : 0.0f;
        float tlm = t - off, trm = t + off;
        if (pingpong) { tlm = t; trm = t; }
        tap[0].set (std::max (1.0f, tlm) * 0.001f * (float) sr, snap); tap[1].set (std::max (1.0f, trm) * 0.001f * (float) sr, snap);
        sPP.set (pingpong ? 1.0f : 0.0f, snap);
        sFb.set (1.2f * std::clamp (k[3], 0.0f, 1.0f), snap);
        const int style = std::clamp ((int) std::lround (k[4] * 4.0f), 0, 4);
        static const float styleDb[5] { 0.0f, -4.0f, 0.0f, -0.5f, -4.0f };
        const float m = std::clamp (k[0], 0.0f, 1.0f);
        sWet.set (std::min (1.0f, 2.0f * m) * dbToGain (styleDb[style]), snap);
        sDry.set (std::min (1.0f, 2.0f * (1.0f - m)), snap);
        sDepth.set (3.0e-3f * (float) sr * k[5], snap);
        sLfoMix.set ((style == 0 || style == 1 || style == 3) ? 1.0f : 0.0f, snap); sWobMix.set (style == 2 ? 1.0f : 0.0f, snap);
        for (auto& p : lfo) p.setRate (sr, 0.1f * std::pow (50.0f, k[6]));
        sDrive.set ((style == 0 || style == 2) ? 1.0f + 6.0f * k[7] : 1.0f, snap);
        sChr.set ((style == 0 || style == 2) ? std::min (1.0f, 4.0f * k[7]) : 0.0f, snap);
        const float crush = style == 3 ? k[7] : 0.0f;
        sCrush.set (crush > 0.0f ? 1.0f : 0.0f, snap);
        holdLen = 1 + (int) (crush * 6.0f); crushQ = std::pow (2.0f, 14.0f - 8.0f * crush);
        sDiff.set (style == 4 ? 1.0f : 0.0f, snap);
        sDiffG.set (0.5f + 0.3f * (style == 4 ? k[7] : 0.5f), snap);
        sApScale.set (1.0f + 2.0f * k[5], snap);
        // パン（ウェット）・広がり
        sPanL.set (std::min (1.0f, 2.0f * (1.0f - k[10])), snap); sPanR.set (std::min (1.0f, 2.0f * k[10]), snap);
        sWid.set (2.0f * k[9], snap);
        // 音色: スタイルのローパス（ANALOG 5 k、TAPE 10 k、DIGITAL 7 k）と HI CUT の低いほう、LO CUT。なめらかにした周波数から 32 サンプルごとに
        static const float styleHz[5] { 30000.0f, 5000.0f, 10000.0f, 7000.0f, 30000.0f };
        sLp.set (std::log (std::min ({ hiHz (k[12]), styleHz[style], 0.45f * (float) sr })), snap);
        sHp.set (std::log (loHz (k[11])), snap);
        // ダッキング
        duckAmt = k[13]; duckThr = -50.0f + 40.0f * (1.0f - k[14]); duckRel = tauCoef (sr, 0.05 + 0.95 * k[15]);
    }
    // 12 dB/oct のローパス 2 段（Q 0.707 と 0.541、2 段目は 1.4 倍の周波数）とハイパス（Q 0.707）。TPT の状態変数フィルター（係数を動かしてもプチッとしにくい）
    void tone (bool snap)
    {
        float lpL = sLp.cur, hpL = sHp.cur;
        if (! snap) { for (int i = 0; i < 32; ++i) { lpL = sLp.next(); hpL = sHp.next(); } }
        if (! snap && same (lpL, lastLp) && same (hpL, lastHp))
        {
            // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
            for (auto& c : flt) for (auto& f : c) f.ramp (true, 1.0f);
            return;
        }
        lastLp = lpL; lastHp = hpL;
        const double lp = std::exp ((double) lpL), hp = std::exp ((double) hpL);
        for (int c = 0; c < 2; ++c)
        {
            flt[c][0].lp (sr, lp, 0.7071); flt[c][1].lp (sr, std::min (lp * 1.4, 0.45 * sr), 0.5412); flt[c][2].hp (sr, hp, 0.7071);
            for (auto& f : flt[c]) f.ramp (snap, 32.0f);
        }
    }

    double sr = 48000.0, tempo = 120.0;
    float k[16] { 0.065f, 0.5275f, 1.0f / 3.0f, 0.22917f, 0.0f, 0.0f, 0.15f, 0.0f, 0.5f, 0.5f, 0.5f, 0.015f, 0.845f, 0.0f, 0.0f, 0.0f };
    std::array<FracDelay, 2> line;
    FracDelay ap[2][4];
    XTap tap[2];
    Svf flt[2][3];
    Phasor lfo[2];
    WowFlutter wf;
    Smooth sFb, sWet, sDry, sDepth, sPanL, sPanR, sWid, sDrive, sChr, sCrush, sDiff, sDiffG, sLfoMix, sWobMix, sPP, sLp, sHp, sApScale;
    DcBlock dcb[2];
    float back[2] {}, env = 0.0f, envA = 0.0f, duckRel = 0.0f, duckG = 1.0f, duckAmt = 0.0f, duckThr = -10.0f;
    float crushQ = 16384.0f, hold[2] {}, lastLp = -1.0f, lastHp = -1.0f;
    int holdLen = 1, holdN = 0;
    unsigned ctl = 0;
};

// ---- TAPE HEADS（Guitar Rig の Tape Echo を測って合わせ込み）----
// 測った結果（cap/verb/TapeEcho。インパルスの返りから「スプリングだけ」の記録（ECHO VOL 0）を引いて、こだまだけを取り出した）:
//   左右で別々のテープの輪（入口も左右別）。ヘッド A・B の時間 = 基本 × 番号（1〜4、0 = 使わない。本物は 5 以上も 4 と同じ）
//   基本: SYNC ON で音符 7 段（SPEED の位置で 1/8・1/16 付点・1/8 3 連・1/16・1/32 付点・1/16 3 連・1/32。切り替わりの位置も測った）、
//         OFF で 240 × 2^(−2 SPEED) ms（0 → 240 ms、1 → 60 ms）
//   左右の混ぜ方: 位置 L = HEAD MIX × (1 − STEREO)、R = HEAD MIX + (1 − HEAD MIX) × STEREO。位置 p で A は (1 − p)、B は p（直線）
//   戻り = そのチャンネルの出口（A と B を混ぜて輪の音色を通したもの）× 1.2552 × FEEDBACK（帯域の山で。0.8 を超えると発振して、録音の頭打ちで止まる）
//   輪の中の音色（くり返すたびに重なる）: 2 次のハイパス 54.6 Hz（Q 1.05）・2 次のローパス 7.88 kHz（Q 0.3）・
//     ECHO BASS（HP2 + g·LP2 の形。0 で 250 Hz、0.25 で 92 Hz に谷が来て、上げると谷が消えて低音が増える。5 点の表）・ECHO TREB（零点 2042 Hz の 1 次の棚 −16〜+21 dB）
//   テープの速さ: 速いほど高い音が残る（輪の中の 1 次の棚、零点 1919 Hz、−6.43 dB × log2(基本 / 166.7 ms)。SYNC でも FREE でも同じ。残り 0.13 dB）
//   出口の音色（1 回だけ）: 1 次のハイパス 162 Hz・BASS（1 次の棚、中点 159 Hz、−11〜+11 dB）・TREBLE（1 次の棚、中点 4.46 kHz、−19〜+19 dB）
//   こだまの大きさ ∝ ECHO VOL（直線）。HEADROOM = 録音の頭打ち tanh(d x) / d、d = 7.1 × e^(−4.02 HEADROOM)（インパルスとノイズの塊の 2 つで合わせた）
//   WARBLE = 揺れの深さ 2.3 ms × e^(7.5 (WARBLE − 1))（初期値 0.63 で 0.15 ms、1 で 2.3 ms）。ドライには 20 Hz の 1 次のハイパス（本物と同じ）
//   スプリング（REV）は入口の左右を足したものから、こだまと並べて鳴らす。0.1.0 の SPRING を中で使う（SIZE・BASS・LOW / HIGH DAMP を合わせ込み、
//     2.2 kHz の谷と 6 kHz から上の補正）。REV VOL ∝ 大きさ（直線）。REV TIME の長さ（1 kHz）0.28 / 0.71 / 3.3 / 8.5 / 10.2 秒
//   本物の 20 本のつまみのうち DROPOUTS・NOISE・ACCELERATION・SPRING LENGTH は 16 本に収めるため外した（本物の初期値のまま。古っぽさは Retro Retro の仕事）
//   合い具合（初期値と、つまみ 1 本ずつ）: こだまの帯域ごとの大きさ 2 乗平均 0.54 dB（ECHO BASS の谷の近くは 2 dB まで）

struct TapeHeads final : Module
{
    static double syncBeats (float s)
    {
        if (s < 0.10376f) return 0.5;          // 1/8
        if (s <= 0.25f) return 0.375;          // 1/16 付点
        if (s < 0.39624f) return 1.0 / 3.0;    // 1/8 3 連
        if (s < 0.60376f) return 0.25;         // 1/16
        if (s < 0.75f) return 0.1875;          // 1/32 付点
        if (s < 0.89624f) return 1.0 / 6.0;    // 1/16 3 連
        return 0.125;                          // 1/32
    }
    static std::string syncName (float s)
    {
        if (s < 0.10376f) return "1/8";
        if (s <= 0.25f) return "1/16 D";
        if (s < 0.39624f) return "1/8 T";
        if (s < 0.60376f) return "1/16";
        if (s < 0.75f) return "1/32 D";
        if (s < 0.89624f) return "1/16 T";
        return "1/32";
    }
    static float freeMs (float s) { return 240.0f * std::pow (2.0f, -2.0f * std::clamp (s, 0.0f, 1.0f)); }
    static int headN (float v) { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 4.0f), 0, 4); }
    static std::string headText (float v) { return headN (v) == 0 ? std::string ("OFF") : std::to_string (headN (v)); }
    static std::string speedText (float v, const float* all)
    {
        if (all != nullptr && all[5] >= 0.5f) return syncName (v);
        return fmt (freeMs (v), 0, " ms");
    }
    static float lin5 (const float* x, const float* y, float v)
    {
        v = std::clamp (v, x[0], x[4]);
        for (int i = 0; i < 4; ++i) if (v <= x[i + 1]) return y[i] + (v - x[i]) / (x[i + 1] - x[i]) * (y[i + 1] - y[i]);
        return y[4];
    }
    static float trebDb (float v) { static const float x[5] { 0, 0.25f, 0.44258f, 0.75f, 1 }, y[5] { -16.32f, -7.30f, 0.0f, 12.10f, 21.12f }; return lin5 (x, y, v); }
    static float bassOutDb (float v) { static const float x[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, y[5] { -11.26f, -7.67f, 0.0f, 7.72f, 11.44f }; return lin5 (x, y, v); }
    static float trebOutDb (float v) { static const float x[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, y[5] { -19.45f, -12.21f, 0.0f, 11.74f, 19.31f }; return lin5 (x, y, v); }
    // REV TIME → 響きの長さ（1 kHz、秒）。本物の 5 点を対数でつなぐ
    static float revT60 (float v) { static const float x[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, y[5] { std::log (0.28f), std::log (0.71f), std::log (3.27f), std::log (8.52f), std::log (10.19f) }; return std::exp (lin5 (x, y, v)); }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "tapeheads", "TAPE HEADS", "DELAY",
            "Two-head tape echo with a spring reverb. Pick the heads (1-4), blend them with HEAD MIX / STEREO, WARBLE wobbles the tape.",
            {
                { "HEAD A",    0.5f,      5, &headText },
                { "HEAD B",    0.75f,     5, &headText },
                { "HEAD MIX",  1.0f,      0, [] (float v) { return fmtPct (v); } },
                { "STEREO",    0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "SPEED",     0.357143f, 0, [] (float v) { return fmt (freeMs (v), 0, " ms"); }, &speedText },
                { "SYNC",      1.0f,      2, [] (float v) { return std::string (v >= 0.5f ? "ON" : "OFF"); } },
                { "FEEDBACK",  0.24454f,  0, [] (float v) { return fmtPct (v); } },
                { "ECHO VOL",  0.42718f,  0, [] (float v) { return fmtPct (v); } },
                { "ECHO BASS", 0.41836f,  0, [] (float v) { return fmtPct (v); } },
                { "ECHO TREB", 0.44258f,  0, [] (float v) { return fmt (trebDb (v), 1, " dB"); } },
                { "BASS",      0.5f,      0, [] (float v) { return fmt (bassOutDb (v), 1, " dB"); } },
                { "TREBLE",    0.5f,      0, [] (float v) { return fmt (trebOutDb (v), 1, " dB"); } },
                { "WARBLE",    0.63086f,  0, [] (float v) { return fmtPct (v); } },
                { "HEADROOM",  0.5f,      0, [] (float v) { return fmtPct (v); } },
                { "REV VOL",   0.125f,    0, [] (float v) { return fmtPct (v); } },
                { "REV TIME",  0.5f,      0, [] (float v) { return fmt (revT60 (v), 2, " s"); } },
            } };
        return i;
    }
    TapeHeads() { for (int i = 0; i < 16; ++i) k[i] = info().knobs[(size_t) i].def; }
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate;
        for (auto& t : tape) t.prepare ((int) (4.1 * sr) + 64);
        sBase.prepare (sr, 0.12);   // テープの速さの追いかけ（SPEED を変えると音程がすべる。本物の ACCELERATION の初期値ぐらい）
        for (auto* q : { &sFb, &sEcho, &sRev, &sDrive, &sWow, &sGA[0], &sGA[1], &sGB[0], &sGB[1] }) q->prepare (sr, 0.02);
        for (auto* q : { &sTb, &sTt, &sB, &sT }) q->prepare (sr, 0.03);
        wf.prepare (sr, 0.9, 8.5);
        fadeStep = 1.0f / (float) (0.03 * sr);
        // スプリング（MIX 100 % = 響きだけ。合わせ込んだ値）
        spring.setKnob (1, 1.0f); spring.setKnob (2, 0.29f); spring.setKnob (3, 0.542f); spring.setKnob (4, lowDamp (revTab (k[15]))); spring.setKnob (5, 0.2f);
        sRevT.prepare (sr, 0.3); sRevT.set (std::log (revTab (k[15])), true);
        spring.setT60Override (revTab (k[15]));
        spring.prepare (sr, maxBlock);
        bufL.assign ((size_t) std::max (1, maxBlock), 0.0f); bufR.assign ((size_t) std::max (1, maxBlock), 0.0f);
        for (int c = 0; c < 2; ++c) { post1[c].setPeak (sr, 2200.0, 1.4, -3.0); post2[c].setHighShelf (sr, 6000.0, 1.0); dryHp[c].set (sr, 20.0, 0.0, 1.0); dryHp[c].ramp (true); }
        for (int h = 0; h < 2; ++h) { cur[h] = prev[h] = pend[h] = headN (k[h]); fade[h] = 1.0f; }
        targets (true); baseRead = sBase.cur; tone (true); reset();
    }
    void reset() override
    {
        for (auto& t : tape) t.reset();
        spring.reset();
        for (int c = 0; c < 2; ++c)
        {
            back[c] = 0.0f; hp2[c].reset(); lp2[c].reset(); bass[c].reset(); treb[c].reset(); spd[c].reset(); outHp[c].reset(); outLo[c].reset(); outHi[c].reset();
            post1[c].reset(); post2[c].reset(); dryHp[c].reset();
        }
        for (int h = 0; h < 2; ++h) { cur[h] = prev[h] = pend[h]; fade[h] = 1.0f; }
        wf.reset(); ctl = 0;
        baseRead = sBase.cur;
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 16 || same (k[i], v)) return;
        k[i] = v;
        if (i <= 1) pend[i] = headN (v);
        targets (false);
    }
    void setTempo (double bpm) override { if (bpm > 1.0 && std::abs (bpm - tempo) > 1.0e-6) { tempo = bpm; targets (false); } }
    void process (float* L, float* R, int n) override
    {
        for (int off = 0; off < n; off += (int) bufL.size())
        {
            const int m = std::min ((int) bufL.size(), n - off);
            for (int i = 0; i < m; ++i) { bufL[(size_t) i] = L[off + i]; bufR[(size_t) i] = R[off + i]; }
            // スプリングの長さ（REV TIME）は 0.3 秒でなめらかに
            for (int j0 = 0; j0 < m; j0 += 32)   // 32 サンプルずつ渡す（ブロックが大きいと 1 回の段差が大きくなってプチッとしていた: DSP 検査 0.4.0）
            {
                const int mm = std::min (32, m - j0);
                for (int j = 0; j < mm; ++j) sRevT.next();
                const float T = std::exp (sRevT.cur);
                spring.setT60Override (T);
                spring.setKnob (4, lowDamp (T));
                spring.process (bufL.data() + j0, bufR.data() + j0, mm);
            }
            for (int i = 0; i < m; ++i)
            {
                if ((ctl++ & 31) == 0) tone (false);
                for (int c = 0; c < 2; ++c) { hp2[c].step(); lp2[c].step(); bass[c].step(); treb[c].step(); spd[c].step(); outHp[c].step(); outLo[c].step(); outHi[c].step(); }
                const int s = off + i;
                // テープの速さのすべり: 読む位置が逆回りしないように、いちばん遠いヘッドの遅れの伸びを 1 サンプルあたり 0.9・縮みを 3 までに（DSP 検査 0.4.0）
                const float nmax = (float) std::max ({ 1, cur[0], cur[1], prev[0], prev[1] });
                baseRead += std::clamp (sBase.next() - baseRead, -3.0f / nmax, 0.9f / nmax);
                const float base = baseRead, fb = sFb.next(), eg = sEcho.next(), rg = sRev.next(), d = sDrive.next();
                const float wob = wf.next (sWow.next(), sWow.cur * 0.15f);
                float io[2] { L[s], R[s] };
                const float sp[2] { bufL[(size_t) i], bufR[(size_t) i] };
                for (int c = 0; c < 2; ++c)
                {
                    const float x = io[c];
                    tape[c].write (flush (std::tanh (d * (x + back[c])) / d));   // 録音の頭打ち（HEADROOM）
                    const float a = head (c, 0, base, wob), b = head (c, 1, base, wob);
                    float y = sGA[c].next() * a + sGB[c].next() * b;
                    y = spd[c].process (treb[c].process (bass[c].process (lp2[c].process (hp2[c].process (y)))));
                    back[c] = flush (fb * y);
                    // こだまの出口: 1（0 dBFS）までは素通し、その上はなめらかに 2（+6 dBFS）で頭打ち（音色を上げて発振させたときの安全。+12 dBFS だと張り付いて大きすぎた）
                    const float e = outClip (eg * outHi[c].process (outLo[c].process (outHp[c].process (y))));
                    io[c] = dryHp[c].process (x) + e + rg * post2[c].process (post1[c].process (sp[c]));
                }
                L[s] = io[0]; R[s] = io[1];
                for (int h = 0; h < 2; ++h)
                    if (fade[h] < 1.0f) { fade[h] = std::min (1.0f, fade[h] + fadeStep); if (fade[h] >= 1.0f) prev[h] = cur[h]; }
                    else if (pend[h] != cur[h]) { prev[h] = cur[h]; cur[h] = pend[h]; fade[h] = 0.0f; }   // ヘッドの切り替えは 30 ms のクロスフェード
            }
        }
    }
    double tailSeconds() const override { return 30.0; }

private:
    // 中の SPRING の LOW DAMP: 低い音の消え方（1 周ごとの持ち上げ）も長さに合わせて弱める。初期値の +0.32 dB / 周 × (4.19 / T)（本物は長くしても帯域ごとの比があまり変わらない）
    static float lowDamp (float T) { return std::clamp ((0.7f - 0.322f * 4.19f / std::max (0.05f, T)) / 1.9f, 0.0f, 1.0f); }
    // REV TIME → 中の SPRING の T60（表の値）。合わせ込んだ初期値（表 4.19 秒）で本物の 3.27 秒（1 kHz）になる比で
    // （中の SPRING は長くすると 1 kHz の長さが表どおりに伸びきらないので、本物と比べた補正を掛ける: 0 / .25 / .5 / .75 / 1 → × 0.82 / 0.97 / 1 / 1.22 / 1.33）
    static float revTab (float v)
    {
        static const float x[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, c[5] { 0.82f, 0.97f, 1.0f, 1.22f, 1.33f };
        return 4.19f * revT60 (v) / 3.27f * lin5 (x, c, v);
    }
    static inline float outClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 1.0f) return x;
        const float o = 1.0f + std::tanh (a - 1.0f);
        return x < 0.0f ? -o : o;
    }
    inline float head (int c, int h, float base, float wob) const
    {
        auto rd = [&] (int nn) { return nn > 0 ? tape[c].read (std::max (2.0f, (float) nn * base + wob)) : 0.0f; };
        const float v = rd (cur[h]);
        if (fade[h] >= 1.0f) return v;
        const float pv = rd (prev[h]);
        return pv + fade[h] * (v - pv);
    }
    void targets (bool snap)
    {
        const double sec = k[5] >= 0.5f ? syncBeats (k[4]) * 60.0 / std::max (20.0, tempo) : freeMs (k[4]) * 0.001;
        const float maxBase = (float) (tape[0].mask - 16) / 4.0f;
        sBase.set (std::clamp ((float) (sec * sr), 2.0f, std::max (2.0f, maxBase)), snap);
        const float hm = std::clamp (k[2], 0.0f, 1.0f), st = std::clamp (k[3], 0.0f, 1.0f);
        const float pos[2] { hm * (1.0f - st), hm + (1.0f - hm) * st };
        for (int c = 0; c < 2; ++c) { sGA[c].set (1.0f - pos[c], snap); sGB[c].set (pos[c], snap); }
        sFb.set (1.2552f * std::clamp (k[6], 0.0f, 1.0f), snap);
        sEcho.set (0.9249f * std::clamp (k[7], 0.0f, 1.0f), snap);
        sDrive.set (7.1f * std::exp (-4.02f * std::clamp (k[13], 0.0f, 1.0f)), snap);
        sWow.set (2.3e-3f * (float) sr * std::exp (7.5f * (std::clamp (k[12], 0.0f, 1.0f) - 1.0f)), snap);
        // スプリングの量: REV VOL に比例。REV TIME ごとの量の補正（本物の響きの量と比べた差、0 / .25 / .5 / .75 / 1 → +0.7 / +0.5 / 0 / −1.3 / −0.8 dB）
        {
            static const float x[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, c[5] { 0.7f, 0.5f, 0.0f, -1.3f, -0.8f };
            sRev.set (0.995f * dbToGain (lin5 (x, c, k[15])) * std::clamp (k[14], 0.0f, 1.0f), snap);
        }
        sTb.set (k[8], snap); sTt.set (k[9], snap); sB.set (k[10], snap); sT.set (k[11], snap);
        sRevT.set (std::log (revTab (k[15])), snap);
    }
    // 音色のフィルター（なめらかにしたつまみから。32 サンプルごと、動いている間だけ計算し直す）
    void tone (bool snap)
    {
        const float tb = sTb.next32(), tt = sTt.next32(), b = sB.next32(), t = sT.next32();
        const float bms = baseRead * 1000.0f / (float) sr;   // 今のテープの速さ（基本の長さ ms）
        if (! snap && same (tb, lastTone[0]) && same (tt, lastTone[1]) && same (b, lastTone[2]) && same (t, lastTone[3]) && std::abs (bms - lastTone[4]) < 0.01f)
        {
            // 変わっていなければ、前のつなぎ（32 サンプル）を目標ちょうどで止める（Svf は止めないと同じ歩みで係数が流れ続けて、数秒後に壊れていた: 2026-11-19 自分の検査）
            for (int c = 0; c < 2; ++c) { hp2[c].ramp (true, 1.0f); lp2[c].ramp (true, 1.0f); bass[c].ramp (true, 1.0f); }
            return;
        }
        lastTone[0] = tb; lastTone[1] = tt; lastTone[2] = b; lastTone[3] = t; lastTone[4] = bms;
        const double Gs = std::pow (10.0, std::clamp (-6.43 * std::log2 (std::max (1.0f, bms) / 166.67), -15.0, 15.0) / 20.0);
        static const float bx[5] { 0, 0.25f, 0.41836f, 0.75f, 1 };
        static const float bf[5] { std::log (1312.5f), std::log (268.6f), std::log (208.27f), std::log (370.76f), std::log (156.13f) };
        static const float bq[5] { 0.3259f, 0.2904f, 0.5921f, 0.4873f, 0.4458f };
        static const float bg[5] { 0.0432f, 0.1168f, -0.0699f, -1.5788f, -2.6553f };
        const double f0 = std::exp (lin5 (bx, bf, tb)), q0 = lin5 (bx, bq, tb), g0 = lin5 (bx, bg, tb);
        const double G = std::pow (10.0, trebDb (tt) / 20.0), Gb = std::pow (10.0, bassOutDb (b) / 20.0), Gt = std::pow (10.0, trebOutDb (t) / 20.0);
        for (int c = 0; c < 2; ++c)
        {
            hp2[c].hp (sr, 54.59, 1.0538); lp2[c].lp (sr, 7879.6, 0.3);
            bass[c].tg = Svf::tanw (sr, f0); bass[c].tk = (float) (1.0 / q0); bass[c].tm0 = 1.0f; bass[c].tm1 = -bass[c].tk; bass[c].tm2 = (float) (g0 - 1.0);
            treb[c].set (sr, G * 2042.23, 1.0, G, 2042.23);
            spd[c].set (sr, Gs * 1919.0, 1.0, Gs, 1919.0);
            outHp[c].set (sr, 162.29, 0.0, 1.0);
            outLo[c].set (sr, 158.61 / std::sqrt (Gb), Gb, 1.0, 158.61);
            outHi[c].set (sr, 4462.9 * std::sqrt (Gt), 1.0, Gt, 4462.9);
            for (auto* f : { &hp2[c], &lp2[c], &bass[c] }) f->ramp (snap, 32.0f);
            for (auto* f : { &treb[c], &spd[c], &outHp[c], &outLo[c], &outHi[c] }) f->ramp (snap, 32);
        }
    }
    struct Smooth32 : Smooth { float next32() { for (int i = 0; i < 32; ++i) next(); return cur; } };

    double sr = 48000.0, tempo = 120.0;
    float k[16] {};
    FracDelay tape[2];
    SpringReverb spring;
    std::vector<float> bufL, bufR;
    WowFlutter wf;
    Svf hp2[2], lp2[2], bass[2];
    TptShelf1 treb[2], spd[2], outHp[2], outLo[2], outHi[2], dryHp[2];
    Biquad post1[2], post2[2];
    Smooth sBase, sFb, sEcho, sRev, sDrive, sWow, sGA[2], sGB[2], sRevT;
    Smooth32 sTb, sTt, sB, sT;
    float lastTone[5] { -1, -1, -1, -1, -1 };
    float back[2] {}, fade[2] { 1.0f, 1.0f }, fadeStep = 0.001f, baseRead = 8000.0f;
    int cur[2] { 2, 3 }, prev[2] { 2, 3 }, pend[2] { 2, 3 };
    unsigned ctl = 0;
};
} // namespace aiofx
