#pragma once
// 奏法（キースイッチ）・弦の割り当て・全ボイス共通のパラメーター。
// 合成の弦モデルは 0.1.0 で削除した（本人「ゴミ」）。音は MY GUITAR の録音だけで鳴らす。
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <atomic>
#include <functional>

// Per-note articulations, chosen with keyswitches C0..B0 (MIDI 24..35, Logic naming)
// or with Logic's Articulation Set in the Piano Roll.
enum Articulation
{
    artNormal = 0, artPalmMute, artBendHalf, artBendFull, artBend1Half, artBendRelease,
    artPreBendRelease, artVibrato, artWideVibrato, artSlideIn, artSlideOut, artDeadNote,
    numArticulations
};

inline const char* articulationName (int a)
{
    static const char* names[] { "Normal", "Palm Mute", "Bend 1/2", "Bend Full", "Bend 1 1/2", "Bend & Release",
                                 "Pre-bend & Release", "Vibrato", "Wide Vibrato", "Slide In", "Slide Out", "Dead Note" };
    return juce::isPositiveAndBelow (a, (int) numArticulations) ? names[a] : "";
}

constexpr int kKeyswitchLow = 24;   // C0 in Logic (middle C = C3)

// Standard tuning, string 1 (high E) .. string 6 (low E). Index 0 unused.
constexpr int kOpenString[7] { 0, 64, 59, 55, 50, 45, 40 };
constexpr int kMaxFret = 22;

// Shared, block-rate parameters for all voices (written by the processor).
struct VoiceParams
{
    float release   = 0.12f;  // seconds after note-off
    float humanize  = 0.5f;   // 0..1 small random differences between hits
    float muteKnob  = 0.0f;   // 0..1 palm-mute amount (param)

    // --- pitch wheel
    float bendUp    = 2.0f;   // semitones at full wheel up
    float bendDown  = 2.0f;   // semitones at full wheel down
    static constexpr float glideMs = 30.0f;  // how fast the pitch follows the wheel

    const void* kit = nullptr;   // active Kit::GuitarKit (audio thread)
    bool  fixBuzz = true;        // FIX BUZZ: takes with a fixed buzz play the fixed version
    bool  kitTune = true;
    float kitVelSens = 0.7f;
    int   stringMode = 0;        // 0 auto, 1 MIDI channel 1-6 = string 1-6

    // --- articulations
    float artBendStart = 0.08f;  // s after the pick before a bend starts
    float artBendTime  = 0.15f;  // s to reach the bent pitch
    float artHold      = 0.30f;  // s held at the top before releasing (bend & release)
    float slideRange   = 3.0f;   // semitones for slide in / out
    float vibRate      = 5.5f;   // Hz
    float vibDelay     = 0.2f;   // s before vibrato fades in
    int   articulation = artNormal;   // latched by keyswitches (audio thread only)
    int   nextString   = 0;           // string chosen for the note being started (audio thread only)
    int   nextStrumUp  = -1;          // the note is part of a strum: 0 down-stroke take, 1 up-stroke take, -1 any
    int   nextLayerVel = 0;           // strummed: the stroke's velocity 1..127 chooses the layer (0 = the note's own)
    unsigned rrCounter = 0;           // round-robin for recorded takes (audio thread only)
    int   lastTake[7] { -1, -1, -1, -1, -1, -1, -1 };   // last take used per string (audio thread only)

    std::atomic<float> modWheel   { 0.0f }; // CC1 -> palm mute
    std::atomic<float> aftertouch { 0.0f }; // channel pressure -> vibrato
    std::atomic<int>   lastArticulation { 0 }, previewArticulation { -1 };

    float effectiveMute() const noexcept { return juce::jlimit (0.0f, 1.0f, muteKnob + modWheel.load()); }
};

//==============================================================================
// Pitch offset (semitones) of an articulation at time t after the pick.
// tRel = time since note-off (or < 0 while the key is held).
inline float articulationPitchSemis (int art, const VoiceParams& params, float t, float tRel) noexcept
{
    auto ease = [] (float x) { x = juce::jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); };  // a finger pushing a string
    const float bs = params.artBendStart, bt = juce::jmax (0.01f, params.artBendTime), hold = params.artHold;
    switch (art)
    {
        case artBendHalf:   return 1.0f * ease ((t - bs) / bt);
        case artBendFull:   return 2.0f * ease ((t - bs) / bt);
        case artBend1Half:  return 3.0f * ease ((t - bs) / bt);
        case artBendRelease:    return 2.0f * (ease ((t - bs) / bt) - ease ((t - bs - bt - hold) / bt));
        case artPreBendRelease: return 2.0f * (1.0f - ease ((t - bs - hold) / bt));
        case artSlideIn:
        {
            // slide up from below, quantised to frets (smoothed afterwards)
            const float x = juce::jlimit (0.0f, 1.0f, t / 0.09f);
            return -std::round (params.slideRange * (1.0f - x));
        }
        case artSlideOut:
            if (tRel < 0.0f) return 0.0f;
            return -std::round (juce::jlimit (0.0f, 1.0f, tRel / 0.3f) * params.slideRange * 2.0f);
        default: return 0.0f;
    }
}

// Guitar-style vibrato depth for an articulation (semitones, pitch goes up and back only)
inline float articulationVibrato (int art, const VoiceParams& params) noexcept
{
    float depth = params.aftertouch.load();
    if (art == artVibrato)     depth = juce::jmax (depth, 0.35f);
    if (art == artWideVibrato) depth = juce::jmax (depth, 0.9f);
    return depth;
}

//==============================================================================
// Which string plays a note. Like a guitarist's hand: stay near the current position,
// give each note of a chord its own string, open strings only for their own pitch.
struct StringAllocator
{
    double sampleRate = 44100.0;
    juce::int64 startedAt[7] {};   // clock when the string was last picked
    int  noteOn[7] {};             // note currently held on the string (-1 = none)
    float hand = 3.0f;             // fret the hand is around

    void reset() { for (int s = 0; s < 7; ++s) { startedAt[s] = -1000000; noteOn[s] = -1; } hand = 3.0f; }

    static int fretOn (int s, int note) noexcept { return note - kOpenString[s]; }

    int choose (int channel, int note, int mode, juce::int64 clock) const
    {
        if (channel >= 11 && channel <= 16)   // audition from the UI: that string
        {
            const int f = fretOn (channel - 10, note);
            if (f >= 0 && f <= 24) return channel - 10;
        }
        if (mode == 1 && channel >= 1 && channel <= 6)
        {
            const int f = fretOn (channel, note);
            if (f >= 0 && f <= 24) return channel;
        }
        const juce::int64 chordWindow = (juce::int64) (0.05 * sampleRate);
        int best = 0; float bestScore = 1.0e9f;
        for (int pass = 0; pass < 2 && best == 0; ++pass)
            for (int s = 1; s <= 6; ++s)
            {
                const int f = fretOn (s, note);
                if (f < 0 || f > kMaxFret) continue;
                const bool takenByChord = noteOn[s] >= 0 && clock - startedAt[s] < chordWindow;
                if (pass == 0 && takenByChord) continue;
                float score = f == 0 ? 1.5f : std::abs ((float) f - hand);
                if (f > 0 && hand < 5.0f && f > 12) score += 2.0f;   // don't jump up the neck from low positions
                if (score < bestScore) { bestScore = score; best = s; }
            }
        return best;
    }

    void picked (int s, int note, juce::int64 clock)
    {
        if (s < 1) return;
        startedAt[s] = clock;
        noteOn[s] = note;
        const int f = fretOn (s, note);
        if (f > 0) hand += (juce::jlimit (1.0f, 17.0f, (float) f) - hand) * 0.5f;
    }

    void released (int note) { for (int s = 1; s <= 6; ++s) if (noteOn[s] == note) noteOn[s] = -1; }
};

//==============================================================================
// Synthesiser that turns keyswitch notes (C0..B0) into per-note articulations and picks
// a string for every note (one note per string: a new note on a string chokes the old one).
// Keyswitches latch: they apply to every following note until the next keyswitch
// (Logic's Articulation Set sends one before each note, "Normal" for plain notes).
class GuitarSynth : public juce::Synthesiser
{
public:
    explicit GuitarSynth (VoiceParams& p) : params (p)
    {
        strings.reset();
        std::fill (std::begin (strumUpForNote), std::end (strumUpForNote), -1);
        std::fill (std::begin (strumVelForNote), std::end (strumVelForNote), 0);
        std::fill (std::begin (strumStringForNote), std::end (strumStringForNote), -1);
    }

    static bool isKeyswitch (int note) noexcept
    {
        return note >= kKeyswitchLow && note < kKeyswitchLow + numArticulations;
    }

    // set by the processor: chokes the voice that is sounding on string s
    std::function<void (int)> chokeString;
    // the strummer's "stop string s now" (at its place in the block): CC 102 on channel 16, value 100 + string.
    // Never leaves the plugin (it makes no MIDI)
    static constexpr int kChokeCC = 102;
    static juce::MidiMessage chokeMessage (int string) { return juce::MidiMessage::controllerEvent (16, kChokeCC, 100 + juce::jlimit (1, 6, string)); }
    std::function<bool (int)> isBrushNote;
    // SCORE: told about every note (latched articulation + the string chosen automatically);
    // may change the articulation and force a string for this note only
    std::function<void (int channel, int note, float velocity, int autoString, int& art, int& forcedString)> onNoteOn;
    std::function<void (int channel, int note)> onNoteOff;

    juce::int64 getClock() const noexcept { return clock; }

    // STRUM: which string each note of a chord would get (without picking anything), in the given order
    void predictStrings (const int* chordNotes, int count, const int* channels, int* outStrings) const
    {
        StringAllocator a = strings;
        for (int i = 0; i < count; ++i)
        {
            outStrings[i] = a.choose (channels[i], chordNotes[i], params.stringMode, clock);
            a.picked (outStrings[i], chordNotes[i], clock);
        }
    }
    int strumUpForNote[128];   // set by the strummer just before a note starts (-1 = not strummed)
    int strumVelForNote[128];  // ... and the stroke's velocity (0 = not strummed)
    int strumStringForNote[128];  // ... and the string the strummer chose (-1 = choose here)
    // the string `note` is held on right now (0 = none)
    int stringOf (int note) const noexcept { for (int s = 1; s <= 6; ++s) if (strings.noteOn[s] == note) return s; return 0; }

    void setCurrentPlaybackSampleRate (double sr) override
    {
        juce::Synthesiser::setCurrentPlaybackSampleRate (sr);
        strings.sampleRate = sr;
    }

    void handleMidiEvent (const juce::MidiMessage& m) override
    {
        if (m.isController() && m.getChannel() == 16 && m.getControllerNumber() == kChokeCC
            && m.getControllerValue() > 100 && m.getControllerValue() <= 106)
        {
            if (chokeString) chokeString (m.getControllerValue() - 100);
            return;
        }
        if (m.isNoteOnOrOff() && isKeyswitch (m.getNoteNumber()))
        {
            const int a = m.getNoteNumber() - kKeyswitchLow;
            if (m.isNoteOn())
            {
                held[a] = true;
                select (a);
            }
            else
            {
                if (! held[a]) select (a);   // some hosts send keyswitches as velocity-0 notes
                held[a] = false;
            }
            return;   // keyswitches never sound
        }

        juce::Synthesiser::handleMidiEvent (m);
    }

    void noteOn (int channel, int note, float velocity) override
    {
        const bool brush = isBrushNote && isBrushNote (note);
        int s = brush ? 0 : strings.choose (channel, note, params.stringMode, clock);
        if (juce::isPositiveAndBelow (note, 128))
        {
            const int ss = strumStringForNote[note];
            strumStringForNote[note] = -1;
            // strummed: the string the strummer chose (its order and the up-stroke rule count on it)
            if (! brush && ss >= 1 && ss <= 6 && StringAllocator::fretOn (ss, note) >= 0 && StringAllocator::fretOn (ss, note) <= kMaxFret) s = ss;
        }
        int art = params.articulation, forced = -1;
        if (onNoteOn) onNoteOn (channel, note, velocity, s, art, forced);
        if (! brush)
        {
            if (forced >= 1 && forced <= 6 && StringAllocator::fretOn (forced, note) >= 0) s = forced;
            if (s > 0 && chokeString) chokeString (s);
            strings.picked (s, note, clock);
        }
        else if (chokeString)
            for (int k = 1; k <= 6; ++k) chokeString (k);   // a muted brush stops whatever rings ("chak")
        params.nextString = s;
        params.nextStrumUp = juce::isPositiveAndBelow (note, 128) ? strumUpForNote[note] : -1;
        params.nextLayerVel = juce::isPositiveAndBelow (note, 128) ? strumVelForNote[note] : 0;
        if (juce::isPositiveAndBelow (note, 128)) { strumUpForNote[note] = -1; strumVelForNote[note] = 0; }
        const int latched = params.articulation;
        params.articulation = juce::jlimit (0, (int) numArticulations - 1, art);   // this note only
        juce::Synthesiser::noteOn (channel, note, velocity);
        params.articulation = latched;
    }

    void noteOff (int channel, int note, float velocity, bool allowTailOff) override
    {
        strings.released (note);
        if (onNoteOff) onNoteOff (channel, note);
        juce::Synthesiser::noteOff (channel, note, velocity, allowTailOff);
    }

    void select (int a)
    {
        params.articulation = a;
        params.lastArticulation.store (a);
    }

    void resetArticulation()
    {
        params.articulation = artNormal;
        params.lastArticulation.store (artNormal);
        std::fill (std::begin (held), std::end (held), false);
        std::fill (std::begin (strumStringForNote), std::end (strumStringForNote), -1);
        strings.reset();
    }

protected:
    void renderVoices (juce::AudioBuffer<float>& b, int start, int num) override
    {
        juce::Synthesiser::renderVoices (b, start, num);
        clock += num;
    }

private:
    VoiceParams& params;
    StringAllocator strings;
    juce::int64 clock = 0;
    bool held[numArticulations] {};
};
