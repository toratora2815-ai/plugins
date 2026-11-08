#pragma once
// STRUM: chords (2+ notes starting together) are played string by string, like a pick across
// the strings. Only the sound is spread: the MIDI timing is never rewritten, the first string
// sounds on the grid and the others follow (no added latency).
//   direction: by hand (SCORE ↓/↑ or Logic's D1 / D#1 keyswitch) or AUTO from the position
//   down = low string -> high string, up = high -> low on the top UP STRINGS strings only
//   (the lower strings keep ringing from the previous chord)
#include "Articulations.h"
#include "Score.h"

class Strummer
{
public:
    struct Settings { bool on = false; float msPerString = 7.0f; int upStrings = 4; float humanize = 0.5f; };

    // notes left out of an up-stroke still exist in Logic: tell the SCORE capture about them
    std::function<void (int channel, int note, int velocity, double ppq, int string, int logicStrum)> onDroppedNoteOn;
    std::function<void (int channel, int note, double ppq)> onDroppedNoteOff;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        queueCount = 0;
        clock = 0;
        for (auto& d : delayOf) for (auto& v : d) v = -1;
        for (auto& d : dropped) for (auto& v : d) v = false;
        pendingDir = -1;
        out.ensureSize (16384);
    }

    // orig ppq of a delayed note (so SCORE capture / lookups see Logic's position, not the strum offset)
    double origPpqFor (int note) const { return juce::isPositiveAndBelow (note, 128) ? origPpq[note] : -1.0; }
    void clearOrig (int note) { if (juce::isPositiveAndBelow (note, 128)) origPpq[note] = -1.0; }
    // the D1 / D#1 that came with this note's chord (-1 none): what Logic says, for the SCORE capture
    int  logicStrumFor (int note) const { return juce::isPositiveAndBelow (note, 128) ? keyDirFor[note] : -1; }

    // ppq0 / ppqPerSample: position of sample 0 of this block (havePpq false: no transport -> AUTO = down)
    void process (juce::MidiBuffer& midi, int n, const Settings& set, GuitarSynth& synth, const Score::ApplyTable* table,
                  bool havePpq, double ppq0, double ppqPerSample, bool tableIsScore)
    {
        out.clear();

        // 1) gather this block's events; chords = note-ons at the same sample
        int pos = -1;
        int chordNotes[16], chordVel[16], chordCh[16];
        int chordCount = 0;
        auto flushChord = [&]
        {
            if (chordCount == 0) return;
            emitChord (pos, chordNotes, chordVel, chordCh, chordCount, set, synth, table, havePpq, ppq0, ppqPerSample, tableIsScore);
            chordCount = 0;
        };

        for (const auto meta : midi)
        {
            const auto& m = meta.getMessage();
            const int p = meta.samplePosition;
            if (p != pos) { flushChord(); pos = p; }

            if (m.isNoteOnOrOff() && Score::isStrumKey (m.getNoteNumber()))
            {
                if (m.isNoteOn()) pendingDir = m.getNoteNumber() == Score::kStrumUpKey ? 1 : 0;   // one chord, no latch
                continue;   // never sounds
            }
            const bool playable = m.isNoteOnOrOff() && m.getChannel() <= 10 && ! GuitarSynth::isKeyswitch (m.getNoteNumber())
                                  && m.getNoteNumber() != Kit::kBrushDownNote && m.getNoteNumber() != Kit::kBrushUpNote;
            if (playable && m.isNoteOn() && chordCount < 16)
            {
                chordNotes[chordCount] = m.getNoteNumber(); chordVel[chordCount] = m.getVelocity(); chordCh[chordCount] = m.getChannel();
                ++chordCount;
                continue;
            }
            if (playable && m.isNoteOff())
            {
                const int ch = juce::jlimit (1, 16, m.getChannel()) - 1, nn = m.getNoteNumber();
                if (dropped[ch][nn])   // its note-on was left out of an up-stroke
                {
                    dropped[ch][nn] = false;
                    if (onDroppedNoteOff) onDroppedNoteOff (ch + 1, nn, havePpq ? ppq0 + p * ppqPerSample : -1.0);
                    continue;
                }
                if (delayOf[ch][nn] > 0) { enqueue (clock + p + delayOf[ch][nn], m); delayOf[ch][nn] = -1; continue; }
            }
            out.addEvent (m, p);
        }
        flushChord();

        // 2) delayed strings that are due in this block
        for (int i = 0; i < queueCount;)
        {
            const auto t = queue[(size_t) i].time - clock;
            if (t < n)
            {
                const auto& msg = queue[(size_t) i].msg;
                if (msg.isNoteOn() && juce::isPositiveAndBelow (msg.getNoteNumber(), 128))
                {
                    synth.strumUpForNote[msg.getNoteNumber()] = upForDelayed[msg.getNoteNumber()];
                    upForDelayed[msg.getNoteNumber()] = -1;
                }
                out.addEvent (msg, (int) juce::jmax ((juce::int64) 0, t));
                queue[(size_t) i] = queue[(size_t) --queueCount];
            }
            else ++i;
        }
        midi.swapWith (out);
        clock += n;
    }

    // transport stopped / jumped: forget pending strings (their notes would hang otherwise)
    void flush (juce::MidiBuffer& midi)
    {
        for (int i = 0; i < queueCount; ++i)
            if (queue[(size_t) i].msg.isNoteOff()) midi.addEvent (queue[(size_t) i].msg, 0);
        queueCount = 0;
        for (auto& d : delayOf) for (auto& v : d) v = -1;
        for (auto& d : dropped) for (auto& v : d) v = false;
        for (auto& v : upForDelayed) v = -1;
        for (auto& v : origPpq) v = -1.0;
    }

private:
    struct Delayed { juce::int64 time; juce::MidiMessage msg; };

    void enqueue (juce::int64 time, const juce::MidiMessage& m)
    {
        if (queueCount < (int) queue.size()) queue[(size_t) queueCount++] = { time, m };
    }

    // a note-on at absolute time `t` must not be cut by an older note-off of the same note that was
    // delayed past it (down-stroke's last string vs the next up-stroke's first). Bring that off forward:
    // right now (before the on) when the on is played now, else just before the queued on.
    void pullOffBefore (int ch, int nn, juce::int64 t, bool onIsNow, int pos)
    {
        for (int i = 0; i < queueCount;)
        {
            auto& q = queue[(size_t) i];
            if (q.msg.isNoteOff() && q.msg.getChannel() == ch && q.msg.getNoteNumber() == nn && q.time >= t)
            {
                if (onIsNow) { out.addEvent (q.msg, pos); queue[(size_t) i] = queue[(size_t) --queueCount]; continue; }
                q.time = juce::jmax (clock + pos, t - 1);
            }
            ++i;
        }
    }

    // a new chord: strings of the previous one still waiting are played now (before it), so a slow
    // strum never ends after the next chord has started
    void releaseWaitingStrings (int pos, GuitarSynth& synth)
    {
        for (int i = 0; i < queueCount;)
        {
            auto& q = queue[(size_t) i];
            if (q.msg.isNoteOn() && q.time > clock + pos)
            {
                const int nn = q.msg.getNoteNumber();
                if (juce::isPositiveAndBelow (nn, 128)) { synth.strumUpForNote[nn] = upForDelayed[nn]; upForDelayed[nn] = -1; }
                out.addEvent (q.msg, pos);
                queue[(size_t) i] = queue[(size_t) --queueCount];
                continue;
            }
            ++i;
        }
    }

    void emitChord (int pos, const int* notes, const int* vel, const int* chs, int count, const Settings& set, GuitarSynth& synth,
                    const Score::ApplyTable* table, bool havePpq, double ppq0, double ppqPerSample, bool tableIsScore)
    {
        const double ppq = havePpq ? ppq0 + pos * ppqPerSample : -1.0;
        for (int i = 0; i < count; ++i) keyDirFor[notes[i]] = pendingDir;
        releaseWaitingStrings (pos, synth);
        if (count < 2 || ! set.on)   // a single note, or STRUM off: as it is
        {
            for (int i = 0; i < count; ++i)
            {
                pullOffBefore (juce::jlimit (1, 16, chs[i]), notes[i], clock + pos, true, pos);
                out.addEvent (juce::MidiMessage::noteOn (chs[i], notes[i], (juce::uint8) vel[i]), pos);
            }
            pendingDir = -1;
            return;
        }

        // direction: SCORE (by hand) > D1 / D#1 > AUTO
        int dir = -1;
        if (table != nullptr && havePpq)
            for (int i = 0; i < count && dir < 0; ++i)
                if (auto* e = table->find (Score::toTick (ppq), notes[i]))
                    dir = tableIsScore ? e->effStrum : e->ovStrum;
        if (dir < 0) dir = pendingDir;
        if (dir < 0) dir = havePpq ? Score::autoStrumDir (ppq, table != nullptr && table->barIs16 (Score::toTick (ppq))) : 0;
        pendingDir = -1;

        // order by string: down = low string (6) first, up = high string (1) first
        int idx[16], strs[16], sorted[16], sortedCh[16];
        for (int i = 0; i < count; ++i) idx[i] = i;
        std::sort (idx, idx + count, [&] (int a, int b) { return notes[a] < notes[b]; });   // the allocator expects low -> high
        for (int i = 0; i < count; ++i) { sorted[i] = notes[idx[i]]; sortedCh[i] = chs[idx[i]]; }
        synth.predictStrings (sorted, count, sortedCh, strs);   // each note with its own channel (MIDI Ch = String)
        int order[16];
        for (int i = 0; i < count; ++i) order[i] = i;
        std::sort (order, order + count, [&] (int a, int b) { return dir == 0 ? strs[a] > strs[b] : strs[a] < strs[b]; });

        const int maxUp = set.upStrings >= 6 ? 16 : set.upStrings;
        float strongest = 0.0f;
        for (int i = 0; i < count; ++i) strongest = juce::jmax (strongest, vel[i] / 127.0f);
        // stronger = a little faster
        const float speed = (dir == 0 ? set.msPerString : set.msPerString * 0.7f) * (1.15f - 0.3f * strongest);

        for (int k = 0; k < count; ++k)
        {
            const int i = idx[order[k]];
            const int nn = notes[i], ch = juce::jlimit (1, 16, chs[i]);
            if (dir == 1 && k >= maxUp)   // up-stroke: only the top strings
            {
                dropped[ch - 1][nn] = true;
                if (onDroppedNoteOn) onDroppedNoteOn (ch, nn, vel[i], ppq, strs[order[k]], keyDirFor[nn]);
                continue;
            }
            const float jitter = (rng.nextFloat() * 2.0f - 1.0f) * 1.5f * set.humanize;
            const int delay = k == 0 ? 0 : juce::jmax (0, (int) std::lround ((k * speed + jitter) * 0.001 * sr));
            const float gainDb = -1.0f * (float) k - (dir == 1 ? 2.5f : 0.0f);
            const int v = juce::jlimit (1, 127, (int) std::lround (vel[i] * juce::Decibels::decibelsToGain (gainDb)));
            synth.strumUpForNote[nn] = -1;   // set when the note actually starts (below / queue)
            origPpq[nn] = ppq;
            auto on = juce::MidiMessage::noteOn (ch, nn, (juce::uint8) v);
            if (delay == 0)
            {
                pullOffBefore (ch, nn, clock + pos, true, pos);
                synth.strumUpForNote[nn] = dir;
                out.addEvent (on, pos);
            }
            else
            {
                pullOffBefore (ch, nn, clock + pos + delay, false, pos);
                upForDelayed[nn] = dir;
                enqueue (clock + pos + delay, on);
                delayOf[ch - 1][nn] = delay;
            }
        }
    }

private:
    double sr = 44100.0;
    juce::int64 clock = 0;
    std::array<Delayed, 512> queue;
    int queueCount = 0;
    int delayOf[16][128] {};
    bool dropped[16][128] {};
    int upForDelayed[128] { };
    double origPpq[128] {};
    int pendingDir = -1;
    int keyDirFor[128] {};
    juce::Random rng;
    juce::MidiBuffer out;

public:
    Strummer()
    {
        for (auto& v : upForDelayed) v = -1;
        for (auto& v : keyDirFor) v = -1;
        for (auto& v : origPpq) v = -1.0;
        for (auto& d : delayOf) for (auto& v : d) v = -1;
    }
};
