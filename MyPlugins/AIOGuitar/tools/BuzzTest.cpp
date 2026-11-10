// ビビり補正（Source/BuzzFix.h）の数値テスト。JUCE なしで単体でビルドできる:
//   clang++ -std=c++17 -O2 -I Source tools/BuzzTest.cpp -o /tmp/BuzzTest && /tmp/BuzzTest [wav 出力フォルダ]
// 弦のモデルで「きれいな音」と「途中で縦の振れが育ってフレットに当たる音」を作り、
//  - きれいな音（うなり・強いピッキング・最後の手ミュート・ハム・短い音・高い音）を誤って直さないか
//  - ビビりの始まりを見つけられるか（見つけた位置がビビりより後ろにならないか）
//  - 直した音が、ビビらなかったときの音（同じ弦のモデル）に近いか、つなぎ目にプチがないか
// を確かめる。
#include "BuzzFix.h"
#include "BuzzSynth.h"
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <fstream>

namespace
{
using namespace buzzsynth;
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok) ++fails;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";

    // ---------- 1. clean notes must be left alone
    std::printf ("== clean notes (must not be touched) ==\n");
    {
        int falsePos = 0, total = 0;
        float worstFrac = 0, worstX = 0;
        const double notes[] { 73.4, 82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 440.0, 659.3, 987.8, 1318.5 };
        unsigned seed = 10;
        for (double f0 : notes)
            for (int v = 0; v < 8; ++v)
            {
                NoteSpec p;
                p.f0 = f0; p.seed = ++seed;
                p.sr = (v % 2) ? 44100.0 : 48000.0;
                p.len = v == 3 ? 0.5 : (v == 4 ? 10.0 : 5.0);
                p.beat = v == 1 ? 0.9 : 0.25;                 // strong beating
                p.attackHard = v == 2 ? 1.0 : 0.2;            // hard pick
                p.muteAt = v == 5 ? 1.6 : -1.0;               // hand mute
                p.twoStage = v == 6 ? 0.08 : (v == 7 ? 0.03 : 0.0);  // fast, then slow
                if (v == 7) { p.beat = 1.0; p.attackHard = 0.6; p.hiT60 = 0.8; }   // everything at once
                p.humDb = v == 4 ? -62.0 : -200.0;
                p.noiseDb = v == 4 ? -70.0 : -84.0;
                p.B = f0 < 200 ? 1.5e-4 : 3.0e-5;
                p.t60 = 3.0 + 4.0 * (v % 3) / 2.0;
                const auto nt = makeNote (p);
                const auto rep = buzz::detect (nt.clean.data(), (int) nt.clean.size(), p.sr, f0);
                ++total;
                if (rep.closestFrac + rep.closestX / 30.0f > worstFrac + worstX / 30.0f) { worstFrac = rep.closestFrac; worstX = rep.closestX; }
                if (rep.found)
                {
                    ++falsePos;
                    std::printf ("    false alarm: f0 %.1f variant %d at %.2f s (rise %.1f dB)\n", f0, v, rep.start / p.sr, rep.riseDb);
                }
            }
        std::printf ("    closest to a buzz: %.0f %% of the high bands over their lines, %.1f dB (a buzz: 75 %% and 4 dB, 60 %% and 6 dB, or 50 %% and 9 dB)\n", worstFrac * 100.0f, worstX);
        check (falsePos == 0, std::to_string (falsePos) + " of " + std::to_string (total) + " clean notes flagged");
    }

    // ---------- 1b. things that are not a buzz (DSP inspection 2026-11-10): clicks, slow hand mutes, a lifted finger
    std::printf ("== clicks, mutes, a lifted finger (must not be touched) ==\n");
    {
        int flagged = 0, total = 0;
        unsigned seed = 300;
        auto tryOne = [&] (const char* what, const Gen& p)
        {
            const auto x = gen (p);
            std::vector<float> out;
            const auto r = buzz::check (x.data(), (int) x.size(), p.sr, p.f0, out);
            ++total;
            if (! out.empty()) { ++flagged; std::printf ("    flagged: %s, f0 %.1f, at %.2f s\n", what, p.f0, r.start / p.sr); }
        };
        for (double f0 : { 110.0, 246.9, 659.3 })
            for (double db : { -20.0, -30.0 })
                for (double ms : { 0.0, 1.0 })
                { Gen p; p.f0 = f0; p.clickAt = 2.0; p.clickDb = db; p.clickMs = ms; p.seed = ++seed; tryOne ("click", p); }
        for (double f0 : { 110.0, 246.9 })
            for (double tau : { 0.05, 0.12, 0.25, 0.5 })
            { Gen p; p.f0 = f0; p.muteAt = 2.5; p.muteTau = tau; p.seed = ++seed; tryOne ("slow hand mute", p); }
        for (double f0 : { 146.8, 246.9, 392.0 })
            for (double odb : { -6.0, -18.0 })
            { Gen p; p.f0 = f0; p.releaseAt = 2.5; p.openDb = odb; p.seed = ++seed; tryOne ("finger lifted, open string", p); }
        check (flagged == 0, std::to_string (flagged) + " of " + std::to_string (total) + " flagged");
    }

    // ---------- 1c. a short rattle, then the (beating) string rings on: after the fix the tail is the string's own
    std::printf ("== a short rattle, then the string rings on ==\n");
    {
        double worst = 0; int fixedCount = 0, total = 0;
        unsigned seed = 1;
        const double sr = 48000;
        for (double f0 : { 82.4, 110.0, 146.8, 196.0, 246.9 })
            for (double cents : { 0.0, 3.0, 6.0, 12.0 })
                for (double two : { 0.0, 0.2 })
                {
                    std::vector<float> fund;
                    const auto clean = beatingString (sr, f0, 7.0, cents, cents > 0 ? 0.8 : 0.0, two, ++seed, &fund);
                    auto bz = clean;
                    double y1 = 0, y2 = 0, z1 = 0, z2 = 0, prevF = 0;
                    const double d1 = std::exp (-2 * kPi * 500 / sr), d2 = std::exp (-2 * kPi * 900 / sr);
                    const double c1 = 2 * d1 * std::cos (2 * kPi * 3400 / sr), c2 = 2 * d2 * std::cos (2 * kPi * 6100 / sr);
                    double fpk = 0; for (int i = (int) (1.0 * sr); i < (int) (1.1 * sr); ++i) fpk = std::max (fpk, (double) -fund[(size_t) i]);
                    for (int i = (int) (0.95 * sr); i < (int) (1.7 * sr); ++i)
                    {
                        const double t = i / sr;
                        const double env = t < 1.0 || t > 1.6 ? 0.0 : std::min (1.0, (t - 1.0) / 0.01) * std::min (1.0, (1.6 - t) / 0.05);
                        const double dd = -fund[(size_t) i] / fpk, F = dd > 0.9 ? (dd - 0.9) * 10 * env : 0.0, xx = F - prevF; prevF = F;
                        const double n1 = xx + c1 * y1 - d1 * d1 * y2; y2 = y1; y1 = n1;
                        const double n2 = xx + c2 * z1 - d2 * d2 * z2; z2 = z1; z1 = n2;
                        bz[(size_t) i] += (float) (1.0 * (0.6 * y1 + 0.4 * z1 + 2 * xx));
                    }
                    std::vector<float> out;
                    buzz::check (bz.data(), (int) bz.size(), sr, f0, out);
                    ++total;
                    if (out.empty()) continue;
                    ++fixedCount;
                    for (double t0 : { 2.0, 3.0, 4.0, 5.0 })
                    {
                        const int a = (int) (t0 * sr), b = a + (int) sr;
                        worst = std::max ({ worst, std::abs (rmsDb (out, a, b) - rmsDb (clean, a, b)),
                                            std::abs (bandDb (out, a, b, sr, 3.2 * f0, false) - bandDb (clean, a, b, sr, 3.2 * f0, false)) });
                    }
                }
        std::printf ("    %d of %d fixed; 2-6 s later, the most the fixed take differs from the string itself: %.1f dB\n", fixedCount, total, worst);
        check (worst < 1.5, "the tail after the rattle is left as it was");
    }

    // ---------- 2. buzzing notes: found, fixed, close to the clean string
    std::printf ("== buzzing notes ==\n");
    {
        struct Case { double f0, at, swell, amt, beat; double sr; };
        const Case cases[] {
            {  82.4, 2.0, 0.5, 1.0, 0.25, 48000 }, { 110.0, 1.5, 0.4, 1.0, 0.25, 48000 }, { 146.8, 2.2, 0.6, 0.6, 0.5, 44100 },
            { 196.0, 1.0, 0.5, 1.0, 0.25, 48000 }, { 246.9, 2.5, 0.3, 0.5, 0.25, 48000 }, { 329.6, 1.2, 0.5, 1.0, 0.6, 44100 },
            {  98.0, 0.6, 0.5, 1.0, 0.25, 48000 }, { 440.0, 1.8, 0.4, 0.8, 0.25, 48000 }, { 73.4, 3.0, 0.6, 1.0, 0.3, 48000 },
        };
        unsigned seed = 500;
        int idx = 0, borrowTries = 0, borrowOk = 0;
        for (const auto& c : cases)
        {
            NoteSpec p;
            p.f0 = c.f0; p.buzzAt = c.at; p.swell = c.swell; p.buzzAmt = c.amt; p.beat = c.beat; p.sr = c.sr;
            p.len = 6.0; p.seed = ++seed; p.B = c.f0 < 200 ? 1.5e-4 : 3.0e-5;
            const auto nt = makeNote (p);
            const int n = (int) nt.buzzed.size();
            const double sr = p.sr;
            {
                const int a0 = nt.trueOnset, b0 = std::min (n, a0 + (int) (0.5 * sr));
                std::printf ("  [%d] f0 %.1f Hz, contact at %.2f s; highs (>2.5k) +%.1f dB, level +%.1f dB over the clean string\n", idx, c.f0, nt.trueOnset / sr,
                             bandDb (nt.buzzed, a0, b0, sr, 2500, true) - bandDb (nt.clean, a0, b0, sr, 2500, true), rmsDb (nt.buzzed, a0, b0) - rmsDb (nt.clean, a0, b0));
            }
            auto rep = buzz::detect (nt.buzzed.data(), n, sr, c.f0);
            check (rep.found, "found");
            if (! rep.found) { ++idx; continue; }
            std::printf ("       start %.3f s (true %.3f), clean until %.3f s, rise %.1f dB\n",
                         rep.start / sr, nt.trueOnset / sr, rep.cleanEnd / sr, rep.riseDb);
            check (rep.cleanEnd <= nt.trueOnset, "clean part ends before the first contact");
            check (rep.cleanEnd >= nt.trueOnset - (int) (0.25 * sr), "and not more than 0.25 s before it");

            std::vector<float> fixedAudio;
            const bool ok = buzz::repair (nt.buzzed.data(), n, sr, c.f0, rep, fixedAudio);
            check (ok, "repaired (highs cut " + std::to_string ((int) std::lround (rep.cutDb)) + " dB" + (rep.strong ? ", strong" : "") + ", change " + std::to_string ((int) std::lround (rep.changeDb)) + " dB)");
            if (! ok) { ++idx; continue; }
            bool finite = true, same = true;
            for (auto v : fixedAudio) if (! std::isfinite (v) || std::abs (v) > 2.0f) finite = false;
            for (int i = 0; i < rep.fixFrom; ++i) if (fixedAudio[(size_t) i] != nt.buzzed[(size_t) i]) same = false;
            check (finite, "finite, no blow-up");
            check (same, "untouched before the hand-over");
            // the buzz is gone: highs over the next second like the clean string (the buzzed one is far above)
            const int a = nt.trueOnset, b = std::min (n, a + (int) sr);
            const double hiClean = bandDb (nt.clean, a, b, sr, 2500, true) - rmsDb (nt.clean, a, b);
            const double hiBuzz  = bandDb (nt.buzzed, a, b, sr, 2500, true) - rmsDb (nt.buzzed, a, b);
            const double hiFix   = bandDb (fixedAudio, a, b, sr, 2500, true) - rmsDb (fixedAudio, a, b);
            std::printf ("       highs vs whole (1 s after): clean %.1f  buzzed %.1f  fixed %.1f dB\n", hiClean, hiBuzz, hiFix);
            check (hiFix < hiClean + 4.0, "highs back near the clean string");
            {
                const int a2 = nt.trueOnset, b2 = std::min (n, a2 + (int) (0.15 * sr));
                const double c0 = bandDb (nt.clean, a2, b2, sr, 2500, true), bz = bandDb (nt.buzzed, a2, b2, sr, 2500, true), fx = bandDb (fixedAudio, a2, b2, sr, 2500, true);
                std::printf ("       highs in the first 0.15 s of the buzz, over the clean string: buzzed %+.1f  fixed %+.1f dB\n", bz - c0, fx - c0);
                check (fx - c0 < 6.0, "the start of the buzz is down too");
            }
            // level: like the clean string (times the swell that had begun before the hand-over)
            const double lvClean = rmsDb (nt.clean, a, b), lvFix = rmsDb (fixedAudio, a, b);
            std::printf ("       level 1 s after: clean %.1f  fixed %.1f dB\n", lvClean, lvFix);
            check (std::abs (lvFix - lvClean) < 4.0, "level near the clean string");
            // decay over the rest: no growth
            const double l1 = rmsDb (fixedAudio, a, a + (int) (0.5 * sr)), l2 = rmsDb (fixedAudio, a + (int) (1.5 * sr), a + (int) (2.0 * sr));
            check (l2 < l1, "keeps decaying");
            // no click at the hand-over
            // no click where the fix starts (only what is left of the buzz, compared with the clean string there)
            const double clk = clickRatioDb (fixedAudio, nt.clean, rep.fixFrom + (int) (0.015 * sr), (int) (0.015 * sr));
            const double clkB = clickRatioDb (nt.buzzed, nt.clean, nt.trueOnset + (int) (0.05 * sr), (int) (0.05 * sr));
            const double clkF = clickRatioDb (fixedAudio, nt.clean, nt.trueOnset + (int) (0.05 * sr), (int) (0.05 * sr));
            std::printf ("       sharpest edge vs the clean string: at the hand-over %+.1f dB; first 0.1 s of the buzz: buzzed %+.1f, fixed %+.1f dB\n", clk, clkB, clkF);
            check (clk < 3.0, "no click at the hand-over");
            // the other way: the tail of a clean take of the same note (another pick, 5 cents off, other phases)
            {
                NoteSpec q = p;
                q.buzzAt = -1.0; q.seed = p.seed + 1000; q.f0 = c.f0 * std::pow (2.0, 5.0 / 1200.0);
                q.attackHard = 0.5; q.pluckPos = 0.13; q.len = 7.0;
                const auto donor = makeNote (q);
                buzz::Take bad { nt.buzzed.data(), n, sr, c.f0, rep.cleanEnd }, don { donor.clean.data(), (int) donor.clean.size(), sr, q.f0, (int) donor.clean.size() };
                std::vector<float> borrowed;
                float qual = 0;
                const bool bok = buzz::borrowTail (bad, rep.cleanEnd, don, borrowed, &qual);
                // refusing is fine (the lowered take is kept): it is counted, and most must work
                std::printf ("       %s a clean tail (waveforms line up %d %%)\n", bok ? "borrowed" : "did not borrow", (int) std::lround (qual * 100));
                ++borrowTries; if (bok) ++borrowOk;
                if (bok)
                {
                    bool fin = true, same2 = true;
                    for (auto v : borrowed) if (! std::isfinite (v) || std::abs (v) > 2.0f) fin = false;
                    for (int i = 0; i < rep.cleanEnd - (int) (0.04 * sr); ++i) if (borrowed[(size_t) i] != nt.buzzed[(size_t) i]) same2 = false;
                    check (fin && same2, "finite, untouched before the crossfade");
                    const int b2 = std::min ((int) borrowed.size(), a + (int) sr);
                    const double hiB = bandDb (borrowed, a, b2, sr, 2500, true) - rmsDb (borrowed, a, b2);
                    const double lvB = rmsDb (borrowed, a, b2);
                    std::printf ("       borrowed: highs vs whole %.1f dB (clean %.1f), level %.1f dB (clean %.1f), length %.2f s\n", hiB, hiClean, lvB, lvClean, borrowed.size() / sr);
                    // another take: its own decay and beating, so a little more room for the level a second later
                    check (hiB < hiClean + 4.0 && std::abs (lvB - lvClean) < 6.0, "highs and level like the clean string");
                    const double ck = clickRatioDb (borrowed, nt.clean, rep.cleanEnd - (int) (0.02 * sr), (int) (0.03 * sr));
                    std::printf ("       borrowed: sharpest edge at the join vs the clean string %+.1f dB\n", ck);
                    check (ck < 3.0, "no click at the join");
                    if (! out.empty()) writeWav (out + "/buzz_" + std::to_string (idx) + "_borrowed.wav", borrowed, sr);
                }
            }
            if (! out.empty())
            {
                writeWav (out + "/buzz_" + std::to_string (idx) + "_orig.wav", nt.buzzed, sr);
                writeWav (out + "/buzz_" + std::to_string (idx) + "_fixed.wav", fixedAudio, sr);
                writeWav (out + "/buzz_" + std::to_string (idx) + "_clean.wav", nt.clean, sr);
            }
            ++idx;
        }
        check (borrowOk * 4 >= borrowTries * 3, std::to_string (borrowOk) + " of " + std::to_string (borrowTries) + " tails borrowed");
    }

    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
