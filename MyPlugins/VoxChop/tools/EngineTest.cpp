// Vox Chop の音の計算の数値テスト（JUCE なし）
//   clang++ -std=c++17 -O2 -I../Source EngineTest.cpp -o /tmp/voxtest && /tmp/voxtest
// 作り物の声（声帯のパルス → 響きの山 3 つ）で、高さ・響き・息・かすれ・ロボット・切り分けを確かめる
#include "VoxEngine.h"
#include <chrono>
#include <cstdio>

using namespace vox;

static int fails = 0;
#define CHECK(cond, ...) do { std::printf ("%s  ", (cond) ? "ok  " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! (cond)) ++fails; } while (0)

static double fsG = 48000.0;

// 母音 1 つ（f0 Hz、長さ秒、ビブラート cents、響きの山の倍率）
static void vowel (Buf& out, double f0, double sec, double vibCents, double formantScale, float amp = 0.3f)
{
    const size_t n = (size_t) (sec * fsG);
    Buf src (n);
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fsG;
        const double f = f0 * std::pow (2.0, vibCents / 1200.0 * std::sin (2 * pi * 5.5 * t));
        ph += f / fsG;
        if (ph >= 1.0) ph -= 1.0;
        // 声帯のパルス（開いて閉じる形）
        src[i] = (float) (ph < 0.6 ? 0.5 - 0.5 * std::cos (pi * ph / 0.6) : std::cos (pi * (ph - 0.6) / 0.8)) - 0.45f;
    }
    // 響きの山（共鳴 3 つを直列）
    Buf y = src;
    const double F[3] = { 700, 1220, 2600 }, BW[3] = { 80, 90, 120 };
    for (int k = 0; k < 3; ++k)
    {
        const double f = F[k] * formantScale, r = std::exp (-pi * BW[k] / fsG), w = 2 * pi * f / fsG;
        const double a1 = -2 * r * std::cos (w), a2 = r * r, g = 1 - r;
        double y1 = 0, y2 = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double v = g * y[i] - a1 * y1 - a2 * y2;
            y2 = y1; y1 = v; y[i] = (float) v;
        }
    }
    float pk = 0;
    for (auto v : y) pk = std::max (pk, std::abs (v));
    // 出だし・終わり 15 ms
    const size_t fd = (size_t) (0.015 * fsG);
    for (size_t i = 0; i < n; ++i)
    {
        float e = 1.0f;
        if (i < fd) e = (float) i / fd;
        if (n - i < fd) e = (float) (n - i) / fd;
        out.push_back (y[i] / pk * amp * e);
    }
}
static void silence (Buf& out, double sec) { out.insert (out.end(), (size_t) (sec * fsG), 0.0f); }
static void noise (Buf& out, double sec, float amp)
{
    std::mt19937 r (5);
    std::normal_distribution<float> g (0, 1);
    auto hp = Biquad::highpass (fsG, 2500, 0.7);
    const size_t n = (size_t) (sec * fsG);
    for (size_t i = 0; i < n; ++i)
    {
        float e = std::min (1.0f, std::min ((float) i, (float) (n - i)) / (float) (0.01 * fsG));
        out.push_back (hp.process (g (r)) * amp * e);
    }
}

static double rmsOf (const Buf& x, size_t a = 0, size_t b = (size_t) -1)
{
    b = std::min (b, x.size());
    double e = 0;
    for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
    return std::sqrt (e / std::max<size_t> (1, b - a));
}
static double db (double v) { return 20 * std::log10 (v + 1e-12); }

// 響きの山の移動量: 平均パワースペクトル → 250 Hz でならす → 対数周波数でずらして一番そろう倍率
static std::vector<double> envLog (const Buf& x, size_t a, size_t b)
{
    FFT fft (12); const int N = 4096; std::vector<double> P (N / 2, 0.0); std::vector<std::complex<float>> X (N);
    for (size_t s = a; s + N <= b; s += N / 4) {
        for (int k = 0; k < N; ++k) X[k] = x[s + k] * (float) (0.5 - 0.5 * std::cos (2 * pi * k / N));
        fft.transform (X.data(), false);
        for (int k = 0; k < N / 2; ++k) P[k] += std::norm (X[k]); }
    const int B = (int) (250.0 / (fsG / N)); std::vector<double> S (N / 2);
    for (int k = 0; k < N / 2; ++k) { double s = 0; int c = 0; for (int j = std::max (0, k - B); j <= std::min (N / 2 - 1, k + B); ++j) { s += P[j]; ++c; } S[k] = s / c; }
    // 対数周波数 200 Hz〜5 kHz を 400 点
    std::vector<double> L (400);
    for (int i = 0; i < 400; ++i) { const double hz = 200 * std::pow (25.0, i / 399.0); const double kb = hz / (fsG / N); const int k0 = (int) kb; const double f = kb - k0; L[i] = 10 * std::log10 (S[k0] * (1 - f) + S[k0 + 1] * f + 1e-20); }
    return L;
}
static double formantRatioAt (const Buf& in, size_t ia, const Buf& out, size_t oa, size_t len)
{
    auto A = envLog (in, ia, ia + len), Bv = envLog (out, oa, oa + len);
    double best = 0, bestR = 1; const double stepOct = std::log2 (25.0) / 399.0;
    for (int sh = -60; sh <= 60; ++sh) {   // ±60 点 ≒ ±0.7 オクターブ
        // 300 Hz〜3.5 kHz の所だけで比べる
        double ma = 0, mb = 0; int c = 0;
        for (int i = 0; i < 400; ++i) { const int j = i + sh; const double hz = 200 * std::pow (25.0, i / 399.0); if (j < 0 || j >= 400 || hz < 300 || hz > 3500) continue; ma += A[i]; mb += Bv[j]; ++c; }
        ma /= c; mb /= c; double num = 0, da = 0, db2 = 0;
        for (int i = 0; i < 400; ++i) { const int j = i + sh; const double hz = 200 * std::pow (25.0, i / 399.0); if (j < 0 || j >= 400 || hz < 300 || hz > 3500) continue; num += (A[i] - ma) * (Bv[j] - mb); da += (A[i] - ma) * (A[i] - ma); db2 += (Bv[j] - mb) * (Bv[j] - mb); }
        const double r = num / std::sqrt (da * db2 + 1e-20); if (r > best) { best = r; bestR = std::pow (2.0, sh * stepOct); } }
    return bestR;
}

static double formantRatio (const Buf& in, const Buf& out, size_t a, size_t b) { return formantRatioAt (in, a, out, a, b - a); }

// 高さのばらつき（cents）
static double f0SpreadCents (const Analysis& a)
{
    std::vector<double> c;
    for (auto p : a.period) if (p > 0) c.push_back (1200 * std::log2 (a.fs / p / a.medianF0));
    if (c.empty()) return 0;
    double m = 0; for (auto v : c) m += v; m /= c.size();
    double s = 0; for (auto v : c) s += (v - m) * (v - m);
    return std::sqrt (s / c.size());
}

// 決まった高さの成分の強さ（相関）
static double bandEnergy (const Buf& x, size_t a, size_t b, double hz)
{
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) { const double w = 2 * pi * hz * i / fsG; re += x[i] * std::cos (w); im += x[i] * std::sin (w); }
    return std::sqrt (re * re + im * im) / (b - a);
}

static bool finiteAll (const Buf& x) { for (auto v : x) if (! std::isfinite (v)) return false; return true; }

int main()
{
    for (double fs : { 48000.0, 44100.0 })
    {
        fsG = fs;
        std::printf ("\n===== %.0f Hz =====\n", fs);
        // 「あー」0.6 s・間 0.2 s・「えー」0.4 s・「しっ」0.12 s・「おー」0.7 s（男性 120 Hz 前後）
        Buf x;
        silence (x, 0.1);
        vowel (x, 120, 0.6, 40, 1.0);
        silence (x, 0.2);
        vowel (x, 135, 0.4, 30, 1.0);
        silence (x, 0.06);
        noise (x, 0.12, 0.08);
        vowel (x, 110, 0.7, 50, 1.0);
        silence (x, 0.15);
        const size_t v1a = (size_t) (0.15 * fs), v1b = (size_t) (0.65 * fs);   // 1 つ目の母音の中

        auto t0 = std::chrono::steady_clock::now();
        const auto an = analyse (x, fs);
        auto t1 = std::chrono::steady_clock::now();
        CHECK (std::abs (an.medianF0 - 120) < 8, "高さを測る: 真ん中 %.1f Hz（120 前後のはず）, 山 %zu 個, 声の割合 %.0f%%, %.1f ms",
               an.medianF0, an.marks.size(), an.voicedShare() * 100, std::chrono::duration<double, std::milli> (t1 - t0).count());
        {
            // 1 つ目の母音の山の間隔 ≒ 400 サンプル（48k / 120）
            std::vector<double> gaps;
            for (size_t i = 1; i < an.marks.size(); ++i) if (an.marks[i - 1] > v1a && an.marks[i] < v1b) gaps.push_back (an.marks[i] - an.marks[i - 1]);
            double m = 0; for (auto g : gaps) m += g; m /= std::max<size_t> (1, gaps.size());
            CHECK (std::abs (m - fs / 120) < fs / 120 * 0.06, "山の間隔 %.1f（%.1f のはず）", m, fs / 120);
        }

        // そのまま（粒に分けてつなぎ直すだけ）でほぼ元に戻るか
        {
            VoiceSettings s; s.shift = 0.0001f;
            const auto r = renderVoice (x, an, s);
            double e = 0, d = 0;
            for (size_t i = v1a; i < v1b; ++i) { e += (double) x[i] * x[i]; d += (double) (x[i] - r.audio[i]) * (x[i] - r.audio[i]); }
            CHECK (10 * std::log10 (e / d) > 12, "つなぎ直しの誤差 SNR %.1f dB（12 dB 以上）", 10 * std::log10 (e / d));
            CHECK (r.audio.size() == x.size(), "長さが変わらない");
        }

        struct Case { const char* name; VoiceSettings s; double wantF0, wantCentroidRatio; };
        auto vs = [] (float sh, float fo, float br = 0, float gr = 0, float ro = 0, float al = 0)
        { VoiceSettings s; s.shift = sh; s.formant = fo; s.breath = br; s.grit = gr; s.robot = ro; s.alien = al; return s; };
                const Case cases[] = {
            { "高さ +7",               vs (7, 0),   120 * std::pow (2.0, 7 / 12.0), 1.0 },
            { "響き +3（体が小さい）", vs (0, 3),   120, std::pow (2.0, 3 / 12.0) },
            { "男→女 +8 / +3",         vs (8, 3),   120 * std::pow (2.0, 8 / 12.0), std::pow (2.0, 3 / 12.0) },
            { "男→子ども +12 / +5",    vs (12, 5),  240, std::pow (2.0, 5 / 12.0) },
            { "低く -8 / -3",          vs (-8, -3), 120 * std::pow (2.0, -8 / 12.0), std::pow (2.0, -3 / 12.0) },
            { "巨人 0 / -6",           vs (0, -6),  120, std::pow (2.0, -6 / 12.0) },
        };
        for (auto& c : cases)
        {
            auto t2 = std::chrono::steady_clock::now();
            const auto r = renderVoice (x, an, c.s);
            auto t3 = std::chrono::steady_clock::now();
            const auto ao = analyse (r.audio, fs);
            const double cr = formantRatio (x, r.audio, v1a, v1b);
            const double lv = db (rmsOf (kWeighted (r.audio, fs), v1a, v1b) / rmsOf (kWeighted (x, fs), v1a, v1b));   // 耳の感じ方に近い音量
            CHECK (finiteAll (r.audio) && std::abs (ao.medianF0 - c.wantF0) < c.wantF0 * 0.04
                   && std::abs (std::log (cr / c.wantCentroidRatio)) < 0.07 && std::abs (lv) < 2.5,
                   "%-22s 高さ %.1f Hz（%.1f）, 響き ×%.2f（×%.2f）, 音量 %+.1f dB, %.0f ms",
                   c.name, ao.medianF0, c.wantF0, cr, c.wantCentroidRatio, lv, std::chrono::duration<double, std::milli> (t3 - t2).count());
            // 作り直した後の山の位置が、測り直した高さと合っているか（鍵盤で弾くときに使う）
            CHECK (r.an.medianF0 > 0 && std::abs (r.an.medianF0 - ao.medianF0) < ao.medianF0 * 0.05,
                   "    作り直し後の山の高さ %.1f Hz（測り直し %.1f）", r.an.medianF0, ao.medianF0);
        }
        {
            const auto r = renderVoice (x, an, vs (0, 0, 1.0f));
            const auto ao = analyse (r.audio, fs);
            CHECK (ao.voicedShare() < an.voicedShare() * 0.35, "BREATH 100%%（ささやき）: 声の割合 %.0f%% → %.0f%%, 音量 %+.1f dB",
                   an.voicedShare() * 100, ao.voicedShare() * 100, db (rmsOf (r.audio, v1a, v1b) / rmsOf (x, v1a, v1b)));
            CHECK (r.an.marks.empty(), "    ささやきは鍵盤で弾くとき声として扱わない");
        }
        {
            const auto r = renderVoice (x, an, vs (0, 0, 0, 1.0f));
            const double sub0 = bandEnergy (x, v1a, v1b, 60), sub1 = bandEnergy (r.audio, v1a, v1b, 60);
            CHECK (finiteAll (r.audio) && sub1 > sub0 * 3, "GRIT 100%%: 半分の高さ（60 Hz）の成分 ×%.1f, 音量 %+.1f dB",
                   sub1 / std::max (sub0, 1e-9), db (rmsOf (r.audio, v1a, v1b) / rmsOf (x, v1a, v1b)));
        }
        {
            const auto r = renderVoice (x, an, vs (0, 0, 0, 0, 1.0f));
            const auto ao = analyse (r.audio, fs);
            CHECK (f0SpreadCents (ao) < 20 && f0SpreadCents (an) > 25, "ROBOT 100%%: 高さのばらつき %.0f cents → %.0f cents",
                   f0SpreadCents (an), f0SpreadCents (ao));
        }
        {
            const auto r = renderVoice (x, an, vs (3, 2, 0, 0, 0, 1.0f));
            CHECK (finiteAll (r.audio) && rmsOf (r.audio) > rmsOf (x) * 0.4, "ALIEN 100%%: 音量 %+.1f dB",
                   db (rmsOf (r.audio) / rmsOf (x)));
        }
        {
            VoiceSettings s; s.lowCut = 400; s.highCut = 3000;
            const auto r = renderVoice (x, an, s);
            CHECK (bandEnergy (r.audio, v1a, v1b, 120) < bandEnergy (x, v1a, v1b, 120) * 0.25, "LOW CUT 400 Hz で 120 Hz が下がる");
        }

        // 切り分け: あー / えー / しっ+おー（しっ と おー は続いているので 1 つか 2 つ）
        for (float sens : { 0.2f, 0.5f, 0.9f })
        {
            const auto cuts = autoChop (x, fs, sens);
            std::printf ("      SENS %.1f: %zu 片  ", sens, cuts.size());
            for (auto& c : cuts) std::printf ("[%.2f-%.2f] ", c.start / fs, c.end / fs);
            std::printf ("\n");
            CHECK (cuts.size() >= 3 && cuts.size() <= 6 && std::abs (cuts[0].start / fs - 0.09) < 0.03, "    言葉の切れ目で切れる");
        }
        {
            const auto g = gridChop (x, fs, 0.25, 0.0);
            CHECK (g.size() >= 7 && g.size() <= 9, "GRID 0.25 s: %zu 片", g.size());
        }

        // 鍵盤で弾く（粒を並べ直す）: FORMANT KEEP あり = 長さそのまま高さだけ、なし = 早回し
        {
            const auto r = renderVoice (x, an, vs (0.0001f, 0));
            const long a = (long) (0.1 * fs), b = (long) (0.7 * fs);
            const double ratio = std::pow (2.0, 5 / 12.0);
            for (bool keep : { true, false })
            {
                GrainPlayer gp;
                gp.reset ((double) a);
                Source src { r.audio.data(), a, b, 0.002 * fs };
                struct Noop { double ratio; bool keep; GrainCtl control (double, float, bool) { GrainCtl c; c.pitchRatio = (float) ratio; c.formantRate = keep ? 1.0f : (float) ratio; return c; } void emitted (double, double, bool) {} } ctl { ratio, keep };
                Buf out;
                const double adv = keep ? 1.0 : ratio;
                Buf blk (256);
                while (gp.position() < (double) b && out.size() < (size_t) (2 * fs))
                {
                    std::fill (blk.begin(), blk.end(), 0.0f);
                    gp.process (src, r.an, 1.0, adv, blk.data(), 256, ctl);
                    out.insert (out.end(), blk.begin(), blk.end());
                }
                const auto ao = analyse (out, fs);
                const double wantLen = (b - a) / adv / fs;
                const double fr = formantRatioAt (r.audio, (size_t) (0.15 * fs), out, (size_t) (0.05 * fs), (size_t) (0.2 * fs));
                CHECK (std::abs (ao.medianF0 - 120 * ratio) < 120 * ratio * 0.04 && std::abs (out.size() / fs - wantLen) < 0.02
                       && std::abs (std::log (fr / (keep ? 1.0 : ratio))) < 0.1,
                       "鍵盤 +5 半音 FORMANT KEEP %s: 高さ %.1f Hz（%.1f）, 長さ %.2f s（%.2f）, 響き ×%.2f（%s）",
                       keep ? "ON " : "OFF", ao.medianF0, 120 * ratio, out.size() / fs, wantLen,
                       fr,
                       keep ? "1.00 のはず" : "1.33 のはず");
            }
        }
    }

    // 検査係の指摘の確認
    {
        fsG = 48000.0;
        const double fs = fsG;
        Buf x;
        silence (x, 0.1);
        vowel (x, 120, 1.0, 40, 1.0);
        silence (x, 0.2);
        const auto an = analyse (x, fs);
        struct K { double ratio; bool keep; GrainCtl control (double, float, bool) { GrainCtl c; c.pitchRatio = (float) ratio; c.formantRate = keep ? 1.0f : (float) ratio; return c; } void emitted (double, double, bool) {} };
        auto play = [&] (const Buf& src, const Analysis& a, long from, long to, double semis, bool keep, bool loop, double seconds)
        {
            const double ratio = std::pow (2.0, semis / 12.0);
            GrainPlayer gp;
            gp.reset ((double) from, false);
            if (loop) gp.setLoop ((double) from, (double) to);
            gp.setLevelFollow (keep && std::abs (ratio - 1.0) > 0.01);
            Source s = loop ? Source { src.data(), 0, (long) src.size(), 0.0 } : Source { src.data(), from, to, 0.002 * fs };
            K ctl { ratio, keep };
            Buf out ((size_t) (seconds * fs), 0.0f);
            gp.process (s, a, 1.0, keep ? 1.0 : ratio, out.data(), (int) out.size(), ctl);
            return out;
        };
        // 鍵盤の高さを変えても音量がそろう（KEEP ON）
        const long a = (long) (0.2 * fs), b = (long) (1.0 * fs);
        const Buf ref = play (x, an, a, b, 0, true, false, 0.7);
        const double r0 = rmsOf (ref, (size_t) (0.1 * fs), (size_t) (0.6 * fs));
        for (double st : { 12.0, 24.0, -12.0 })
        {
            const Buf o = play (x, an, a, b, st, true, false, 0.7);
            const double d = db (rmsOf (o, (size_t) (0.1 * fs), (size_t) (0.6 * fs)) / r0);
            CHECK (std::abs (d) < 1.5, "鍵盤 KEEP ON %+.0f 半音の音量 %+.1f dB（0 半音と比べて ±1.5 以内）", st, d);
        }
        // LOOP のつなぎ目で途切れない（0.25 秒の片を 2 秒）
        for (double st : { 0.0, 7.0 })
            for (bool keep : { true, false })
            {
                const long la = (long) (0.4 * fs), lb = (long) (0.65 * fs);
                const Buf o = play (x, an, la, lb, st, keep, true, 2.0);
                const size_t win = (size_t) (0.001 * fs);
                std::vector<double> r;
                for (size_t i = (size_t) (0.05 * fs); i + win < o.size(); i += win) r.push_back (db (rmsOf (o, i, i + win)));
                std::vector<double> sorted = r;
                std::sort (sorted.begin(), sorted.end());
                const double med = sorted[sorted.size() / 2];
                int dips = 0;
                for (auto v : r) dips += v < med - 12.0;
                CHECK (dips == 0, "LOOP %+.0f 半音 KEEP %s: 12 dB 以上のくぼみ %d 個（0 のはず）", st, keep ? "ON " : "OFF", dips);
            }
        // DC のずれ＋BREATH＋LOW CUT で無音にならない
        {
            Buf xd = x;
            for (auto& v : xd) v += 0.01f;
            const auto ad = analyse (xd, fs);
            VoiceSettings s; s.breath = 0.5f; s.lowCut = 80.0f;
            const auto r = renderVoice (xd, ad, s);
            size_t zeros = 0;
            for (size_t i = (size_t) (0.15 * fs); i < (size_t) (1.05 * fs); ++i) zeros += r.audio[i] == 0.0f;
            CHECK (zeros < 100 && finiteAll (r.audio), "DC のずれ＋BREATH 50%%＋LOW CUT: 声の所でちょうど 0 のサンプル %zu 個", zeros);
        }
        // BREATH 75 %（GHOST）でも鍵盤の高さが効く
        {
            VoiceSettings s; s.breath = 0.75f;
            const auto r = renderVoice (x, an, s);
            const Buf o = play (r.audio, r.an, a, b, 5, true, false, 0.7);
            const auto ao = analyse (o, fs);
            CHECK (std::abs (ao.medianF0 - 120 * std::pow (2.0, 5 / 12.0)) < 8, "BREATH 75%% で鍵盤 +5 半音: %.1f Hz（160 のはず）", ao.medianF0);
        }
        // BREATH 100 % は低い所が少ない
        {
            VoiceSettings s; s.breath = 1.0f;
            const auto r = renderVoice (x, an, s);
            auto lowShare = [&] (const Buf& y)
            {
                auto lp = Biquad::lowpass (fs, 300, 0.7071);
                Buf l = y;
                for (auto& v : l) v = lp.process (v);
                return rmsOf (l, (size_t) (0.2 * fs), (size_t) fs) / rmsOf (y, (size_t) (0.2 * fs), (size_t) fs);
            };
            CHECK (lowShare (r.audio) < 0.4, "BREATH 100%%: 300 Hz より下の割合 %.0f%%（元 %.0f%%）", lowShare (r.audio) * 100, lowShare (x) * 100);
        }
        // やめる合図ですぐ戻る
        {
            std::atomic<bool> stop { true };
            auto t0 = std::chrono::steady_clock::now();
            Buf big;
            for (int i = 0; i < 40; ++i) vowel (big, 120, 2.0, 30, 1.0);
            const auto ab = analyse (big, fs, &stop);
            VoiceSettings s; s.shift = 5; s.breath = 0.5f;
            renderVoice (big, an, s, 1, &stop);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            CHECK (ms < 400, "やめる合図で analyse・renderVoice がすぐ戻る（%.0f ms、作る時間込み）", ms);
            (void) ab;
        }
    }

    // 長い歌（3 分）の計算時間
    {
        fsG = 48000.0;
        Buf x;
        for (int i = 0; i < 90; ++i) { vowel (x, 110 + (i % 7) * 12, 1.4, 40, 1.0); silence (x, 0.6); }
        auto t0 = std::chrono::steady_clock::now();
        const auto an = analyse (x, fsG);
        auto t1 = std::chrono::steady_clock::now();
        VoiceSettings s; s.shift = 8; s.formant = 3; s.breath = 0.3f; s.grit = 0.3f;
        const auto r = renderVoice (x, an, s);
        auto t2 = std::chrono::steady_clock::now();
        const double ta = std::chrono::duration<double> (t1 - t0).count(), tr = std::chrono::duration<double> (t2 - t1).count();
        CHECK (ta + tr < 6.0, "3 分の声: 測る %.2f s + 作り直す %.2f s", ta, tr);
    }
    std::printf ("\n%s（失敗 %d）\n", fails == 0 ? "全部 OK" : "失敗あり", fails);
    return fails == 0 ? 0 : 1;
}
