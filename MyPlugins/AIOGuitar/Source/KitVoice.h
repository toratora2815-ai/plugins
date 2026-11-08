#pragma once
// Plays the MY GUITAR recordings: the take recorded on the same string nearest to the note,
// re-pitched (bends, vibrato, slides and pitch wheel change the playback speed),
// soft / hard / palm-mute layers, down/up takes round-robin, small humanising differences,
// one note per string (a new note on the string chokes the old one).
#include "Articulations.h"
#include "SampleKit.h"

struct KitSound : public juce::SynthesiserSound
{
    explicit KitSound (const VoiceParams& p) : params (p) {}
    bool appliesToNote (int note) override
    {
        if (params.kit == nullptr) return false;
        const auto* kit = static_cast<const Kit::GuitarKit*> (params.kit);
        if (note == Kit::kBrushDownNote || note == Kit::kBrushUpNote)
            return kit->count (Kit::slotBrush) > 0;
        return kit->hasNotes();
    }
    bool appliesToChannel (int) override { return true; }
    const VoiceParams& params;
};

class KitVoice : public juce::SynthesiserVoice
{
public:
    explicit KitVoice (VoiceParams& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<KitSound*> (s) != nullptr; }

    int  getString() const noexcept { return isVoiceActive() ? string : 0; }
    void choke() noexcept { if (isVoiceActive() && chokeLeft < 0) chokeLeft = chokeLen = juce::jmax (1, (int) (getSampleRate() * 0.012)); }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        const auto* kit = static_cast<const Kit::GuitarKit*> (params.kit);
        if (kit == nullptr) { clearCurrentNote(); return; }

        const float vel = juce::jlimit (0.02f, 1.0f, velocity);
        art = params.articulation;
        brush = midiNote == Kit::kBrushDownNote || midiNote == Kit::kBrushUpNote;
        string = brush ? 0 : params.nextString;
        released = false;
        age = releaseAge = 0;
        chokeLeft = -1;
        vibPhase = 0.0f;
        wheel = juce::jlimit (-1.0f, 1.0f, ((float) pitchWheel - 8192.0f) / 8191.0f);
        curBend = targetBend();

        // palm mute: the recorded palm-mute layer when there is one, else a filter on the open note
        const float muteAmt = juce::jmax (params.effectiveMute(), art == artPalmMute ? 1.0f : 0.0f);
        const bool hasMute = kit->count (Kit::slotMute) > 0;
        usingMuteLayer = ! brush && hasMute && (muteAmt >= 0.5f || art == artDeadNote);

        int cand[8];
        int n = 0;
        if (brush)
        {
            const bool up = midiNote == Kit::kBrushUpNote;
            for (int i = 0; i < (int) kit->samples.size() && n < 8; ++i)
                if (kit->samples[(size_t) i].slot == Kit::slotBrush && kit->samples[(size_t) i].up == up) cand[n++] = i;
            if (n == 0)   // only one direction recorded: use the other
                for (int i = 0; i < (int) kit->samples.size() && n < 8; ++i)
                    if (kit->samples[(size_t) i].slot == Kit::slotBrush) cand[n++] = i;
        }
        else
        {
            int slot = Kit::slotSoft;
            if (usingMuteLayer) slot = Kit::slotMute;
            else
            {
                const bool hasSoft = kit->count (Kit::slotSoft) > 0, hasHard = kit->count (Kit::slotHard) > 0;
                if ((vel > 0.62f && hasHard) || ! hasSoft) slot = hasHard ? Kit::slotHard : Kit::slotMute;
            }
            n = Kit::candidates (*kit, slot, midiNote, string, cand, 8);
            // strummed: down-strokes use the down takes, up-strokes the up takes (when recorded)
            if (params.nextStrumUp >= 0 && n > 1)
            {
                int m = 0, keep[8];
                for (int i = 0; i < n; ++i) if (kit->samples[(size_t) cand[i]].up == (params.nextStrumUp == 1)) keep[m++] = cand[i];
                if (m > 0) { for (int i = 0; i < m; ++i) cand[i] = keep[i]; n = m; }
            }
        }
        if (n == 0) { clearCurrentNote(); return; }

        // round-robin over the takes (down / up), never the same take twice in a row on a string
        int pick = (int) (params.rrCounter++ % (unsigned) n);
        if (n > 1 && cand[pick] == params.lastTake[juce::jlimit (0, 6, string)]) pick = (pick + 1) % n;
        params.lastTake[juce::jlimit (0, 6, string)] = cand[pick];
        sample = &kit->samples[(size_t) cand[pick]];

        // pitch: from the measured root (pitch-corrected) or the nearest note (as recorded)
        const double hostSr = getSampleRate();
        if (brush) base = sample->sampleRate / hostSr;
        else
        {
            const double target = juce::MidiMessage::getMidiNoteInHertz (midiNote);
            const double root = params.kitTune ? (double) sample->rootHz : juce::MidiMessage::getMidiNoteInHertz (sample->rootNote);
            base = (target / root) * (sample->sampleRate / hostSr);
        }

        // humanise: a little level, brightness and start-point difference between hits
        const float h = params.humanize;
        const float rLevel = (rng.nextFloat() * 2.0f - 1.0f) * h, rTone = (rng.nextFloat() * 2.0f - 1.0f) * h;
        pos = (double) (rng.nextFloat() * h * 0.0006f * (float) sample->sampleRate);

        // velocity -> level (and brightness when only one picking layer was recorded)
        const float sens = params.kitVelSens;
        gain = sample->gain * (1.0f - sens * (1.0f - std::pow (vel, 1.4f)))
             * juce::Decibels::decibelsToGain (0.8f * rLevel);
        const bool oneLayer = kit->count (Kit::slotSoft) == 0 || kit->count (Kit::slotHard) == 0;
        const float bright = brush || usingMuteLayer || ! oneLayer ? 1.0f : juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * vel);
        toneCut = 1800.0f * std::pow (2.0f, bright * 3.5f) * std::pow (2.0f, 0.25f * rTone);   // 1.8 kHz .. 20 kHz

        dead = art == artDeadNote;
        lp[0] = lp[1] = 0.0f;
        env = 1.0f;
        lastRatio = brush ? 1.0 : std::pow (2.0, (curBend + articulationPitchSemis (art, params, 0.0f, -1.0f)) / 12.0);
        muteSm = brush || usingMuteLayer ? 0.0f : targetMute();
        updateCoeffs (0);
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { released = true; releaseAge = 0; updateCoeffs (0); }
        else { sample = nullptr; clearCurrentNote(); }
    }

    void pitchWheelMoved (int value) override { wheel = juce::jlimit (-1.0f, 1.0f, ((float) value - 8192.0f) / 8191.0f); }
    void controllerMoved (int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive() || sample == nullptr) return;

        auto* L = out.getWritePointer (0, start);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
        const float sr = (float) getSampleRate();
        constexpr int sub = 32;
        const float glideTau = VoiceParams::glideMs * 0.001f * sr;

        for (int p = 0; p < num; p += sub)
        {
            const int m = juce::jmin (sub, num - p);
            float semis = 0.0f;
            updateCoeffs (m);   // palm mute / CC1 move smoothly, not once per block
            if (! brush)
            {
                curBend += (targetBend() - curBend) * (1.0f - std::exp (-(float) m / glideTau));
                const float t = (float) age / sr;
                const float fade = juce::jlimit (0.0f, 1.0f, (t - params.vibDelay) / 0.4f);
                vibPhase += juce::MathConstants<float>::twoPi * params.vibRate * (float) m / sr;
                if (vibPhase > juce::MathConstants<float>::twoPi) vibPhase -= juce::MathConstants<float>::twoPi;
                const float vib = articulationVibrato (art, params) * fade * 0.5f * (1.0f - std::cos (vibPhase));
                semis = curBend + vib + articulationPitchSemis (art, params, t, released ? (float) releaseAge / sr : -1.0f);
            }
            // smooth the playback speed over the 32-sample step
            const double ratio = std::pow (2.0, semis / 12.0);
            const double rStep = (ratio - lastRatio) / m;

            for (int i = p; i < p + m; ++i)
            {
                lastRatio += rStep;
                float v = read (lastRatio) * gain;

                // tone / mute filtering (2 x one-pole)
                lp[0] += lpCoef * (v - lp[0]); lp[1] += lpCoef * (lp[0] - lp[1]); v = lp[1];

                env *= envMul;
                v *= env;
                if (chokeLeft >= 0)   // a new note on this string: fade out quickly
                {
                    v *= (float) chokeLeft / (float) chokeLen;
                    if (--chokeLeft < 0) { env = 0.0f; chokeLeft = 0; }
                }
                L[i] += v;
                if (R) R[i] += v;
            }
            lastRatio = ratio;
            age += m;
            if (released) releaseAge += m;
        }

        if (pos >= sample->audio.getNumSamples() - 2 || env < 1.0e-5f)
        {
            sample = nullptr;
            clearCurrentNote();
        }
    }

private:
    // 4-point Hermite interpolation
    inline float read (double ratio) noexcept
    {
        const auto& buf = sample->audio;
        const int n = buf.getNumSamples();
        const double posNow = pos;
        pos += base * ratio;
        if (posNow < 0.0 || posNow >= n - 2) return 0.0f;
        const int i = (int) posNow;
        const float f = (float) (posNow - i);
        const float* d = buf.getReadPointer (0);
        const float xm1 = i > 0 ? d[i - 1] : 0.0f, x0 = d[i], x1 = d[i + 1], x2 = i + 2 < n ? d[i + 2] : 0.0f;
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    float targetMute() const noexcept
    {
        return brush || usingMuteLayer ? 0.0f : juce::jmax (params.effectiveMute(), art == artPalmMute ? 0.85f : 0.0f);
    }

    // advance = samples since the last update (the palm-mute amount glides over ~20 ms)
    void updateCoeffs (int advance)
    {
        const float sr = (float) getSampleRate();
        if (advance > 0) muteSm += (targetMute() - muteSm) * (1.0f - std::exp (-(float) advance / (0.02f * sr)));
        const float m = muteSm;

        // brightness / emulated palm mute low-pass (the recorded mute layer needs none)
        float cut = toneCut;
        if (m > 0.0f) cut = juce::jmin (cut, 6000.0f * std::pow (900.0f / 6000.0f, m));
        if (dead && ! usingMuteLayer) cut = 650.0f;
        lpCoef = cut >= sr * 0.42f ? 1.0f : 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cut / sr);

        // extra decay: palm mute (continuous from "none" at m = 0), dead note, note-off release
        float t60 = 0.18f / (m * m + 0.18f / 1.0e4f);
        if (dead) t60 = 0.04f;
        if (released) t60 = juce::jmin (t60, art == artSlideOut ? 0.6f : params.release);
        envMul = std::pow (10.0f, -3.0f / (t60 * sr));
    }

    float targetBend() const noexcept
    {
        return wheel >= 0.0f ? wheel * params.bendUp : wheel * params.bendDown;
    }

    VoiceParams& params;
    juce::Random rng;
    const Kit::Sample* sample = nullptr;
    double pos = 0.0, base = 1.0, lastRatio = 1.0;
    int art = artNormal, age = 0, releaseAge = 0, string = 0, chokeLeft = -1, chokeLen = 1;
    bool brush = false, released = false, dead = false, usingMuteLayer = false;
    float wheel = 0.0f, curBend = 0.0f, vibPhase = 0.0f, gain = 1.0f, toneCut = 20000.0f;
    float lpCoef = 1.0f, envMul = 1.0f, env = 1.0f, muteSm = 0.0f;
    float lp[2] {};
};
