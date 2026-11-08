#pragma once
// "MY GUITAR" sample kit: your own DI recordings, cut into single notes, pitch-measured,
// tuned and level-matched, then spread over the whole keyboard from the nearest recorded note.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Kit
{
enum Slot { slotSoft = 0, slotHard, slotMute, slotBrush, numSlots };
inline const char* slotName (int s) { static const char* n[] { "Soft picking", "Hard picking", "Palm mute", "Brushing" }; return n[s]; }
inline bool isPicked (int slot) { return slot != slotBrush; }

// standard tuning, string 1 (high E) .. 6 (low E); same as kOpenString in Articulations.h
constexpr int kOpen[7] { 0, 64, 59, 55, 50, 45, 40 };

constexpr int kBrushDownNote = 36;   // C1 in Logic (middle C = C3)
constexpr int kBrushUpNote   = 37;   // C#1

struct Sample
{
    juce::AudioBuffer<float> audio;   // mono, trimmed, NOT gain-adjusted
    double sampleRate = 44100.0;
    float  rootHz   = 0.0f;           // measured pitch (0 for brushing)
    int    rootNote = 0;              // nearest MIDI note
    float  cents    = 0.0f;           // measured tuning error vs equal temperament
    float  rawRms   = 0.0f;           // loudness of the attack + early sustain
    float  gain     = 1.0f;           // level-match gain (set by Kit::normalise)
    int    slot     = slotSoft;
    bool   up       = false;          // up-stroke (picked notes: 2nd take of a pair, brushing: up)
    int    string   = 0;              // 1..6, 0 = unknown
    int    fret     = -1;             // fret from the nut (a capo counts as frets), -1 = unknown

    bool isOpen() const { return string >= 1 && fret == 0; }
};

struct GuitarKit
{
    juce::String name { "My Guitar" };
    std::vector<Sample> samples;
    juce::ValueTree encoded;          // cached serialised form (FLAC), built once

    int count (int slot) const { return (int) std::count_if (samples.begin(), samples.end(), [slot] (const Sample& s) { return s.slot == slot; }); }
    bool hasNotes() const { return count (slotSoft) + count (slotHard) + count (slotMute) > 0; }
};

//==============================================================================
namespace detail
{
    inline float yinPitch (const std::vector<float>& x, int start, int end, double sr)
    {
        const int W = juce::jmin (4096, (int) (sr * 0.06));
        const int tauMin = (int) (sr / 1400.0), tauMax = (int) (sr / 60.0);
        if (start < 0 || start + W + tauMax + 2 >= end) return 0.0f;
        std::vector<float> d ((size_t) tauMax + 2, 1.0f);
        double run = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double s = 0;
            for (int i = 0; i < W; ++i) { const double v = x[(size_t) (start + i)] - x[(size_t) (start + i + tau)]; s += v * v; }
            run += s;
            d[(size_t) tau] = run > 0 ? (float) (s * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < 0.12f)
            {
                while (tau + 1 <= tauMax && d[(size_t) (tau + 1)] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            float mn = 1e9f;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (d[(size_t) tau] < mn) { mn = d[(size_t) tau]; best = tau; }
            if (mn > 0.3f) return 0.0f;
        }
        const float y0 = d[(size_t) (best - 1)], y1 = d[(size_t) best], y2 = d[(size_t) (best + 1)];
        const float dd = 0.5f * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-12f);
        const float f = (float) (sr / (best + juce::jlimit (-0.5f, 0.5f, dd)));
        return (f >= 60.0f && f <= 1400.0f) ? f : 0.0f;
    }

    // median pitch over several windows: robust against the pick transient and string beating
    inline float measurePitch (const std::vector<float>& x, int onset, int end, double sr, bool shortNotes)
    {
        std::vector<float> fs;
        static const double longT[] { 0.12, 0.25, 0.4, 0.6, 0.85 }, shortT[] { 0.03, 0.05, 0.075, 0.1, 0.14 };
        for (double t : (shortNotes ? shortT : longT))
        {
            const float f = yinPitch (x, onset + (int) (t * sr), end, sr);
            if (f > 0) fs.push_back (f);
        }
        if (fs.empty()) return 0.0f;
        std::sort (fs.begin(), fs.end());
        const float med = fs[fs.size() / 2];
        // average the windows that agree with the median (within 30 cents)
        double sum = 0; int n = 0;
        for (auto f : fs) if (std::abs (1200.0 * std::log2 (f / med)) < 30.0) { sum += std::log2 (f); ++n; }
        const float coarse = (float) std::pow (2.0, sum / juce::jmax (1, n));
        if (shortNotes) return coarse;

        // like a tuner: the fundamental itself. Periodicity (YIN) sits a few cents high on real
        // strings because their overtones are slightly sharp (string stiffness).
        const int a = onset + (int) (0.15 * sr);
        const int len = juce::jmin ((int) (0.6 * sr), end - a - (int) (0.05 * sr));
        if (len < (int) (0.15 * sr)) return coarse;
        auto magAt = [&] (double f)
        {
            const double w = 6.283185307179586 * f / sr;
            double re = 0, im = 0;
            for (int i = 0; i < len; ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (len - 1));
                const double v = x[(size_t) (a + i)] * win;
                re += v * std::cos (w * i); im -= v * std::sin (w * i);
            }
            return std::log (re * re + im * im + 1e-30);
        };
        const double step = std::pow (2.0, 1.0 / 1200.0);   // 1 cent
        double best = coarse, bestMag = -1e300, prev = 0, next = 0;
        std::vector<double> mags;
        for (int c = -25; c <= 25; ++c) mags.push_back (magAt (coarse * std::pow (step, c)));
        int bi = 0;
        for (int c = 0; c < (int) mags.size(); ++c) if (mags[(size_t) c] > bestMag) { bestMag = mags[(size_t) c]; bi = c; }
        if (bi == 0 || bi == (int) mags.size() - 1) return coarse;   // no clear peak: keep YIN
        prev = mags[(size_t) bi - 1]; next = mags[(size_t) bi + 1];
        const double dd = 0.5 * (prev - next) / (prev - 2 * bestMag + next + 1e-30);
        best = coarse * std::pow (step, bi - 25 + juce::jlimit (-0.5, 0.5, dd));
        return (float) best;
    }

    inline float rmsOf (const std::vector<float>& x, int a, int b)
    {
        a = juce::jmax (0, a); b = juce::jmin ((int) x.size(), b);
        double s = 0;
        for (int i = a; i < b; ++i) s += x[(size_t) i] * x[(size_t) i];
        return b > a ? (float) std::sqrt (s / (b - a)) : 0.0f;
    }
}

//==============================================================================
// Which string / fret each recorded note was played on, from the recording order:
// a position (capo or fret) is played low string -> high string, each string picked
// down then up (the up-stroke is optional). A pitch that goes DOWN starts the next position.
// Each run of strings is matched to the tuning; ties prefer ending on string 1
// (picked notes) or string 4 (palm mute: strings 6, 5, 4 are enough).
inline void assignStrings (std::vector<Sample>& v, int slot)
{
    // runs of the same pitch = one string (down, up, ...)
    struct Run { int note; size_t first, last; };
    std::vector<Run> runs;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (! runs.empty() && v[i].rootNote == runs.back().note) runs.back().last = i;
        else runs.push_back ({ v[i].rootNote, i, i });
    }
    for (auto& r : runs)
        for (size_t i = r.first; i <= r.last; ++i)
            v[i].up = ((i - r.first) % 2) == 1;

    // groups of rising runs = one position
    const int preferTop = slot == slotMute ? 4 : 1;
    size_t g0 = 0;
    while (g0 < runs.size())
    {
        size_t g1 = g0;
        while (g1 + 1 < runs.size() && runs[g1 + 1].note > runs[g1].note && g1 + 1 - g0 < 6) ++g1;
        const int k = (int) (g1 - g0 + 1);

        int bestTop = 0, bestCapo = 0; float bestErr = 1.0e9f;
        for (int top = 1; top + k - 1 <= 6; ++top)
        {
            // run j (0 = lowest) is on string top + k - 1 - j
            float mean = 0;
            for (int j = 0; j < k; ++j) mean += (float) (runs[g0 + (size_t) j].note - kOpen[top + k - 1 - j]);
            const int capo = (int) std::round (mean / (float) k);
            if (capo < 0 || capo > 24) continue;
            float err = 0;
            for (int j = 0; j < k; ++j) err += (float) std::abs (runs[g0 + (size_t) j].note - kOpen[top + k - 1 - j] - capo);
            err += 0.01f * (float) std::abs (top - preferTop);
            if (err < bestErr) { bestErr = err; bestTop = top; bestCapo = capo; }
        }
        if (bestTop > 0 && bestErr < 0.5f)
            for (int j = 0; j < k; ++j)
            {
                const int str = bestTop + k - 1 - j;
                for (size_t i = runs[g0 + (size_t) j].first; i <= runs[g0 + (size_t) j].last; ++i)
                {
                    v[i].string = str;
                    v[i].fret = v[i].rootNote - kOpen[str];
                }
            }
        juce::ignoreUnused (bestCapo);
        g0 = g1 + 1;
    }
}

//==============================================================================
// 88.2 / 96 / 192 kHz recordings are brought down to 44.1 / 48 kHz with a proper low-pass
// (playing them faster later would otherwise fold the pick noise back as grit).
inline void decimateToBelow60k (std::vector<float>& x, double& sr)
{
    while (sr > 60000.0 && x.size() > 256)
    {
        constexpr int M = 47, taps = 2 * M + 1;
        static const std::vector<float> h = []
        {
            std::vector<float> c (taps);
            const double fc = 0.225;   // of the input rate: 90 % of the new Nyquist
            double sum = 0;
            for (int n = 0; n < taps; ++n)
            {
                const double k = n - M;
                const double sinc = k == 0 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * k) / (juce::MathConstants<double>::pi * k);
                const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / (taps - 1)) + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * n / (taps - 1));
                c[(size_t) n] = (float) (sinc * w);
                sum += c[(size_t) n];
            }
            for (auto& v : c) v = (float) (v / sum);
            return c;
        }();
        std::vector<float> y (x.size() / 2);
        for (size_t i = 0; i < y.size(); ++i)
        {
            double acc = 0;
            const long centre = (long) (2 * i);
            for (int n = 0; n < taps; ++n)
            {
                const long idx = centre + n - M;
                if (idx >= 0 && idx < (long) x.size()) acc += h[(size_t) n] * x[(size_t) idx];
            }
            y[i] = (float) acc;
        }
        x.swap (y);
        sr *= 0.5;
    }
}

//==============================================================================
struct SliceResult
{
    std::vector<Sample> samples;
    int pitched = 0, unpitched = 0;
    juce::String message;
};

// Cut a recording of single notes (or brushing strokes) separated by short silences.
inline SliceResult slice (const juce::AudioBuffer<float>& in, double sr, int slot)
{
    SliceResult res;
    int len = juce::jmin (in.getNumSamples(), (int) (sr * 600.0));
    std::vector<float> x ((size_t) juce::jmax (0, len), 0.0f);
    for (int c = 0; c < in.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i)
            x[(size_t) i] += in.getSample (c, i) / (float) in.getNumChannels();
    decimateToBelow60k (x, sr);
    len = (int) x.size();
    double mean = 0; for (auto v : x) mean += v; mean /= juce::jmax (1, len);
    for (auto& v : x) v -= (float) mean;

    const int fr = juce::jmax (1, (int) (sr * 0.005));   // 5 ms frames
    const int nf = len / fr;
    if (nf < 40) { res.message = "Recording is too short."; return res; }
    std::vector<float> env ((size_t) nf);
    float peak = -200.0f;
    for (int i = 0; i < nf; ++i)
    {
        double s = 0;
        for (int k = 0; k < fr; ++k) { const float v = x[(size_t) (i * fr + k)]; s += v * v; }
        env[(size_t) i] = (float) (10.0 * std::log10 (s / fr + 1e-14));
        peak = juce::jmax (peak, env[(size_t) i]);
    }
    if (peak < -70.0f) { res.message = "The file is silent."; return res; }

    // onsets: a jump of 9 dB within 50 ms, at least 120 ms apart (brushing can be quick)
    const bool brushing = slot == slotBrush;
    const int minGap = brushing ? 24 : 40;
    std::vector<int> onsets;
    int last = -1000;
    if (env[0] > peak - 40.0f) { onsets.push_back (0); last = 0; }
    for (int i = 10; i < nf; ++i)
    {
        float minPrev = env[(size_t) (i - 1)];
        for (int k = 2; k <= 10; ++k) minPrev = juce::jmin (minPrev, env[(size_t) (i - k)]);
        // a new pick climbs back near the loudest level of the last 0.3 s; string beating
        // (a dip and recovery inside a ringing note) does not
        float maxPrev = -200.0f;
        for (int k = 11; k <= 60 && i - k >= 0; ++k) maxPrev = juce::jmax (maxPrev, env[(size_t) (i - k)]);
        if (env[(size_t) i] > peak - 40.0f && env[(size_t) i] - minPrev > 9.0f && i - last > minGap
            && (brushing || env[(size_t) i] > maxPrev - 1.0f))
        {
            int j = i;
            while (j > 0 && i - j < 10 && env[(size_t) (j - 1)] < env[(size_t) j] - 1.0f) --j;
            onsets.push_back (j * fr);
            last = i;
        }
    }
    if (onsets.empty()) { res.message = "No notes found. Leave a short silence between notes."; return res; }

    int upDown = 0;
    for (size_t k = 0; k < onsets.size(); ++k)
    {
        const int on = onsets[k];
        int end = k + 1 < onsets.size() ? onsets[k + 1] - (int) (sr * 0.005) : len;
        const int maxLen = (int) (sr * (brushing ? 1.5 : 12.0));
        end = juce::jmin (end, on + maxLen);
        if (end - on < (int) (sr * (brushing ? 0.05 : (slot == slotMute ? 0.15 : 0.3)))) continue;

        // trim the tail once it stays 60 dB below this note's peak (or into the next pick)
        float segPeak = -200.0f;
        for (int f = on / fr; f < end / fr; ++f) segPeak = juce::jmax (segPeak, env[(size_t) f]);
        int tail = end;
        for (int f = on / fr + 4; f < end / fr; ++f)
            if (env[(size_t) f] < segPeak - 60.0f) { tail = f * fr; break; }
        end = juce::jmax (on + (int) (sr * 0.05), tail);

        Sample s;
        s.slot = slot;
        s.sampleRate = sr;
        if (! brushing)
        {
            s.rootHz = detail::measurePitch (x, on, end, sr, slot == slotMute);
            if (s.rootHz <= 0.0f) { ++res.unpitched; continue; }
            const double midi = 69.0 + 12.0 * std::log2 (s.rootHz / 440.0);
            s.rootNote = (int) std::round (midi);
            s.cents = (float) ((midi - s.rootNote) * 100.0);
        }
        else
        {
            s.up = (upDown++ % 2) == 1;   // strokes alternate: down, up, down, up ...
        }
        s.rawRms = detail::rmsOf (x, on + (int) (sr * 0.01), on + (int) (sr * (brushing ? 0.12 : 0.35)));

        const int pre = (int) (sr * 0.003);
        const int a = juce::jmax (0, on - pre);
        s.audio.setSize (1, end - a);
        for (int i = a; i < end; ++i) s.audio.setSample (0, i - a, x[(size_t) i]);
        const int fin = juce::jmin (pre, s.audio.getNumSamples()), fout = juce::jmin ((int) (sr * 0.015), s.audio.getNumSamples());
        s.audio.applyGainRamp (0, 0, fin, 0.0f, 1.0f);
        s.audio.applyGainRamp (0, s.audio.getNumSamples() - fout, fout, 1.0f, 0.0f);
        res.samples.push_back (std::move (s));
        ++res.pitched;
    }

    if (! brushing)
        assignStrings (res.samples, slot);

    if (brushing)
        res.message = juce::String (res.pitched) + " strokes (" + juce::String ((res.pitched + 1) / 2) + " down / "
                    + juce::String (res.pitched / 2) + " up)";
    else
    {
        int lo = 128, hi = -1;
        for (auto& s : res.samples) { lo = juce::jmin (lo, s.rootNote); hi = juce::jmax (hi, s.rootNote); }
        res.message = res.samples.empty() ? juce::String ("No clear pitches found.")
                    : juce::String (res.pitched) + " notes, " + juce::MidiMessage::getMidiNoteName (lo, true, true, 3)
                      + " - " + juce::MidiMessage::getMidiNoteName (hi, true, true, 3);
        int known = 0;
        for (auto& s : res.samples) if (s.string > 0) ++known;
        if (! res.samples.empty() && known < (int) res.samples.size())
            res.message << ", " << ((int) res.samples.size() - known) << " without string";
        if (res.unpitched > 0) res.message << " (" << res.unpitched << " skipped)";
    }
    return res;
}

//==============================================================================
// Level matching: every note of a layer is brought to that layer's median loudness;
// the whole kit is then scaled so soft picking sits at a fixed level (hard picking and
// brushing keep their recorded level relative to it).
inline void normalise (GuitarKit& kit, bool matchLevels)
{
    auto median = [] (std::vector<float> v) { if (v.empty()) return 0.0f; std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
    float med[numSlots] {};
    for (int sl = 0; sl < numSlots; ++sl)
    {
        std::vector<float> r;
        for (auto& s : kit.samples) if (s.slot == sl && s.rawRms > 0) r.push_back (s.rawRms);
        med[sl] = median (r);
    }
    const float ref = med[slotSoft] > 0 ? med[slotSoft] : (med[slotHard] > 0 ? med[slotHard] : med[slotBrush]);
    const float target = juce::Decibels::decibelsToGain (med[slotSoft] > 0 ? -24.0f : -18.0f);
    const float global = ref > 0 ? target / ref : 1.0f;
    for (auto& s : kit.samples)
    {
        float g = global;
        if (matchLevels && s.rawRms > 0 && med[s.slot] > 0)
            g *= juce::jlimit (0.25f, 4.0f, med[s.slot] / s.rawRms);   // at most +/-12 dB
        s.gain = g;
    }
}

//==============================================================================
// Serialisation (FLAC inside a ValueTree) - stored in the project and in .aioguitar files
inline juce::ValueTree encode (const GuitarKit& kit)
{
    juce::ValueTree t ("KIT");
    t.setProperty ("name", kit.name, nullptr);
    juce::FlacAudioFormat flac;
    for (auto& s : kit.samples)
    {
        juce::MemoryBlock mb;
        {
            auto* mos = new juce::MemoryOutputStream (mb, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (mos, s.sampleRate, 1, 24, {}, 0));
            if (w == nullptr) { delete mos; continue; }
            w->writeFromAudioSampleBuffer (s.audio, 0, s.audio.getNumSamples());
        }
        juce::ValueTree c ("S");
        c.setProperty ("slot", s.slot, nullptr);
        c.setProperty ("up", s.up, nullptr);
        c.setProperty ("string", s.string, nullptr);
        c.setProperty ("fret", s.fret, nullptr);
        c.setProperty ("rootHz", s.rootHz, nullptr);
        c.setProperty ("rootNote", s.rootNote, nullptr);
        c.setProperty ("cents", s.cents, nullptr);
        c.setProperty ("rawRms", s.rawRms, nullptr);
        c.setProperty ("sr", s.sampleRate, nullptr);
        c.setProperty ("flac", mb, nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}

inline std::shared_ptr<GuitarKit> decode (const juce::ValueTree& t, bool matchLevels)
{
    if (! t.hasType ("KIT")) return {};
    auto kit = std::make_shared<GuitarKit>();
    kit->name = t.getProperty ("name", "My Guitar").toString();
    juce::FlacAudioFormat flac;
    for (auto c : t)
    {
        const auto* mb = c.getProperty ("flac").getBinaryData();
        if (mb == nullptr) continue;
        std::unique_ptr<juce::AudioFormatReader> rd (flac.createReaderFor (new juce::MemoryInputStream (*mb, false), true));
        if (rd == nullptr) continue;
        Sample s;
        s.audio.setSize (1, (int) rd->lengthInSamples);
        rd->read (&s.audio, 0, (int) rd->lengthInSamples, 0, true, false);
        s.slot = juce::jlimit (0, numSlots - 1, (int) c.getProperty ("slot", 0));
        s.up = c.getProperty ("up", false);
        s.string = juce::jlimit (0, 6, (int) c.getProperty ("string", 0));
        s.fret = juce::jlimit (-1, 30, (int) c.getProperty ("fret", -1));
        s.rootNote = juce::jlimit (0, 127, (int) c.getProperty ("rootNote", 0));
        s.rootHz = c.getProperty ("rootHz", 0.0f);
        if (! std::isfinite (s.rootHz) || s.rootHz < 20.0f || s.rootHz > 5000.0f)
            s.rootHz = (float) juce::MidiMessage::getMidiNoteInHertz (s.rootNote);
        s.cents = c.getProperty ("cents", 0.0f);
        s.rawRms = c.getProperty ("rawRms", 0.0f);
        s.sampleRate = juce::jlimit (8000.0, 384000.0, (double) c.getProperty ("sr", 44100.0));
        if (s.audio.getNumSamples() < 8) continue;
        kit->samples.push_back (std::move (s));
    }
    normalise (*kit, matchLevels);
    kit->encoded = t;
    return kit;
}

inline bool saveToFile (const GuitarKit& kit, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream fos (f);
    if (! fos.openedOk()) return false;
    juce::GZIPCompressorOutputStream gz (fos);
    (kit.encoded.isValid() ? kit.encoded : encode (kit)).writeToStream (gz);
    return true;
}

inline std::shared_ptr<GuitarKit> loadFromFile (const juce::File& f, bool matchLevels)
{
    juce::FileInputStream fis (f);
    if (! fis.openedOk()) return {};
    juce::GZIPDecompressorInputStream gz (fis);
    return decode (juce::ValueTree::readFromStream (gz), matchLevels);
}

// Recorded takes to play `note` on `string` (0 = any): same string if it was recorded,
// the nearest recorded pitch with pitch shifted DOWN preferred (up to 3 semitones),
// open-string recordings only for the open string itself. Ties (down + up takes) are
// all returned and used round-robin.
inline int candidates (const GuitarKit& kit, int slot, int note, int string, int* out, int maxOut)
{
    bool haveString = false;
    if (string >= 1)
        for (auto& s : kit.samples) if (s.slot == slot && s.string == string) { haveString = true; break; }
    const bool targetOpen = string >= 1 && note == kOpen[string];

    for (int pass = 0; pass < 2; ++pass)   // pass 1: allow open-string takes for fretted notes
    {
        int bestScore = 1 << 20, n = 0;
        for (int i = 0; i < (int) kit.samples.size(); ++i)
        {
            const auto& s = kit.samples[(size_t) i];
            if (s.slot != slot) continue;
            if (haveString && s.string != string) continue;
            if (pass == 0 && s.isOpen() && ! targetOpen) continue;
            const int d = s.rootNote - note;   // > 0: sample is above the note (shift down)
            int score = d >= 0 && d <= 3 ? d : 10 + std::abs (d) * 2;
            if (targetOpen && ! s.isOpen()) score += 1;   // the real open string if there is one
            if (score < bestScore) { bestScore = score; n = 0; }
            if (score == bestScore && n < maxOut) out[n++] = i;
        }
        if (n > 0) return n;
    }
    return 0;
}
} // namespace Kit
