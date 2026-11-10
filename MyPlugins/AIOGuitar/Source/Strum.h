#pragma once
// STRUM: chords (2+ notes starting together) are played string by string, like a pick across
// the strings. Only the sound is spread: the MIDI timing is never rewritten, the first string
// sounds on the grid and the others follow (no added latency).
//   direction: by hand (SCORE ↓/↑ or Logic's D1 / D#1 keyswitch) or AUTO from the position
//   down = low string -> high string, up = high -> low on the top UP STRINGS strings only
//   (the lower strings keep ringing from the previous chord). A chord played by hand comes in a few ms apart,
//   often low string first: when its higher strings arrive, the low ones that already started stop quickly
//   (12 ms) and those still waiting are left out, so an up-stroke rings on its top strings only.
//   The strings of a chord are decided here (SCORE's string first, then each note on its own free string) and the
//   synth plays them there, so the order and the top-strings rule work on the strings that really ring. A single
//   note is left to the synth (it knows the notes released just before it); if it turns out to be the start of a
//   hand-played chord, the string it really took is read back from the synth
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
        curTable = table; curHavePpq = havePpq; curTableIsScore = tableIsScore; curPpq0 = ppq0; curPpqPerSample = ppqPerSample;

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

            if (m.isController() && m.getChannel() == 16 && m.getControllerNumber() == GuitarSynth::kChokeCC)
                continue;   // the same message from outside would stop a string: only the strummer sends it
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
                    synth.strumStringForNote[msg.getNoteNumber()] = stringForDelayed[msg.getNoteNumber()];
                    stringForDelayed[msg.getNoteNumber()] = -1;
                    synth.strumVelForNote[msg.getNoteNumber()] = velForDelayed[msg.getNoteNumber()];
                    upForDelayed[msg.getNoteNumber()] = -1;
                    velForDelayed[msg.getNoteNumber()] = 0;
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
        for (auto& v : velForDelayed) v = 0;
        for (auto& v : stringForDelayed) v = -1;
        for (auto& v : origPpq) v = -1.0;
        group.active = false;
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
                if (juce::isPositiveAndBelow (nn, 128))
                {
                    synth.strumUpForNote[nn] = upForDelayed[nn]; upForDelayed[nn] = -1;
                    synth.strumStringForNote[nn] = stringForDelayed[nn]; stringForDelayed[nn] = -1;
                    synth.strumVelForNote[nn] = velForDelayed[nn]; velForDelayed[nn] = 0;
                }
                out.addEvent (q.msg, pos);
                queue[(size_t) i] = queue[(size_t) --queueCount];
                continue;
            }
            ++i;
        }
    }

    // play one note of the strum: now, or queued `delay` samples later
    void playStrung (int pos, int ch, int nn, int vel, int k, int dir, int delay, double ppq, int str, GuitarSynth& synth)
    {
        const float gainDb = -1.0f * (float) k - (dir == 1 ? 2.5f : 0.0f);
        const int v = juce::jlimit (1, 127, (int) std::lround (vel * juce::Decibels::decibelsToGain (gainDb)));
        synth.strumUpForNote[nn] = -1;   // set when the note actually starts (below / queue)
        origPpq[nn] = ppq;
        auto on = juce::MidiMessage::noteOn (ch, nn, (juce::uint8) v);
        if (delay <= 0)
        {
            pullOffBefore (ch, nn, clock + pos, true, pos);
            synth.strumUpForNote[nn] = dir;
            synth.strumVelForNote[nn] = vel;   // the layer (soft / medium / hard) follows the whole stroke, not the string's level
            synth.strumStringForNote[nn] = str;
            out.addEvent (on, pos);
            delayOf[ch - 1][nn] = -1;
        }
        else
        {
            pullOffBefore (ch, nn, clock + pos + delay, false, pos);
            upForDelayed[nn] = dir;
            stringForDelayed[nn] = str;
            velForDelayed[nn] = vel;
            enqueue (clock + pos + delay, on);
            delayOf[ch - 1][nn] = delay;
        }
        group.last = juce::jmax (group.last, clock + pos + juce::jmax (0, delay));
    }

    void emitChord (int pos, const int* notes, const int* vel, const int* chs, int count, const Settings& set, GuitarSynth& synth,
                    const Score::ApplyTable* table, bool havePpq, double ppq0, double ppqPerSample, bool tableIsScore)
    {
        const double ppq = havePpq ? ppq0 + pos * ppqPerSample : -1.0;
        const juce::int64 t = clock + pos;
        for (int i = 0; i < count; ++i) keyDirFor[notes[i]] = pendingDir;

        // a chord played by hand reaches us a few ms apart: notes within 25 ms join the strum that has begun
        if (set.on && group.active && t > group.t0 && t - group.t0 < (juce::int64) (0.025 * sr))
        {
            joinGroup (pos, notes, vel, chs, count, synth);
            return;
        }

        releaseWaitingStrings (pos, synth);
        group.active = false;
        if (! set.on)   // STRUM off: as it is
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
        // the string each note is played on: SCORE's as it is; in a chord the others each on a free string (the nearest
        // to the allocator's choice). -1 = the synth chooses (a single note, or a chord note with no free string)
        int force[16];
        {
            int used[16], nUsed = 0;
            bool fixedK[16] {};
            for (int k = 0; k < count; ++k)
                if (const int ss = scoreString (ppq, sorted[k]); ss > 0) { strs[k] = force[k] = ss; fixedK[k] = true; used[nUsed++] = ss; }
            for (int k = count - 1; k >= 0; --k)   // the highest note first: the top of the chord (its melody) keeps its string
            {
                if (fixedK[k]) continue;
                force[k] = -1;
                if (count < 2) continue;
                if (const int fs = nearestFree (sorted[k], strs[k], used, nUsed); fs > 0) { strs[k] = force[k] = fs; used[nUsed++] = fs; }
            }
        }
        int order[16];
        for (int i = 0; i < count; ++i) order[i] = i;
        std::sort (order, order + count, [&] (int a, int b) { return dir == 0 ? strs[a] > strs[b] : strs[a] < strs[b]; });

        const int maxUp = set.upStrings >= 6 ? 16 : set.upStrings;
        float strongest = 0.0f;
        for (int i = 0; i < count; ++i) strongest = juce::jmax (strongest, vel[i] / 127.0f);
        // stronger = a little faster
        const float speed = (dir == 0 ? set.msPerString : set.msPerString * 0.7f) * (1.15f - 0.3f * strongest);

        group = {};
        group.active = true; group.t0 = t; group.last = t; group.dir = dir; group.maxUp = maxUp;
        group.speedSamples = speed * 0.001f * (float) sr;
        group.humanize = set.humanize; group.ppq = ppq;

        int played = 0;
        for (int k = 0; k < count; ++k)
        {
            const int i = idx[order[k]];
            const int nn = notes[i], ch = juce::jlimit (1, 16, chs[i]), str = strs[order[k]], fs = force[order[k]];
            if (dir == 1 && (played >= maxUp || (count > 1 && fs < 0)))   // up-stroke: only the top strings (that can ring)
            {
                dropped[ch - 1][nn] = true;
                if (onDroppedNoteOn) onDroppedNoteOn (ch, nn, vel[i], ppq, str, keyDirFor[nn]);
                continue;
            }
            const float jitter = (rng.nextFloat() * 2.0f - 1.0f) * 1.5f * set.humanize;
            const int delay = (count < 2 || played == 0) ? 0 : juce::jmax (0, (int) std::lround ((played * speed + jitter) * 0.001 * sr));
            playStrung (pos, ch, nn, vel[i], played, dir, delay, ppq, fs, synth);
            if (count < 2) synth.strumUpForNote[nn] = -1;   // a single note keeps alternating down / up takes
            addMember (str, nn, ch, vel[i], fs > 0, count < 2, clock + pos + delay);
            ++played;
        }
    }

    // notes arriving shortly after the strum began: placed after the strings already playing, at least one
    // string's step apart (the first note could not wait for them, so the order is the order they came in)
    void joinGroup (int pos, const int* notes, const int* vel, const int* chs, int count, GuitarSynth& synth)
    {
        const juce::int64 t = clock + pos;
        int idx[16];
        for (int i = 0; i < count; ++i) idx[i] = i;
        std::sort (idx, idx + count, [&] (int a, int b) { return group.dir == 0 ? notes[a] < notes[b] : notes[a] > notes[b]; });

        // 0) the strings this strum's notes really use. A note the synth chose itself: already sounding (an earlier
        //    block) -> ask the synth; still in this block's events -> tell it the string we count on (a single note's
        //    guess). Notes that start later keep the guess
        for (int g = 0; g < group.count; ++g)
        {
            auto& mb = group.m[g];
            if (mb.known) continue;
            if (mb.startAt < clock)
            {
                if (const int a = synth.stringOf (mb.note); a > 0) { mb.str = a; mb.known = true; }
            }
            else if (mb.startAt <= t && mb.guessOk)
            {
                synth.strumStringForNote[mb.note] = mb.str;
                mb.known = true;
            }
        }

        // 1) where each new note would go: SCORE's string, else the allocator's choice
        int jStr[16];
        bool jScore[16];
        for (int j = 0; j < count; ++j)
        {
            int nn = notes[idx[j]], ch = juce::jlimit (1, 16, chs[idx[j]]), str = 0;
            synth.predictStrings (&nn, 1, &ch, &str);
            const int ss = scoreString (curHavePpq ? curPpq0 + pos * curPpqPerSample : -1.0, nn);
            jScore[j] = ss > 0;
            jStr[j] = ss > 0 ? ss : str;
        }

        // 2) up-stroke: only the top strings of the whole chord ring. Ranked high string first (earlier notes win
        //    a tie); strings below the top N: started ones stop quickly, waiting ones and new ones are left out
        bool keepJoiner[16];
        for (int j = 0; j < count; ++j) keepJoiner[j] = true;
        if (group.dir == 1)
        {
            struct E { int str, src, k; } e[32];
            int ne = 0;
            for (int g = 0; g < group.count; ++g) e[ne++] = { group.m[g].str, 0, g };
            for (int j = 0; j < count; ++j) e[ne++] = { jStr[j], 1, j };
            std::stable_sort (e, e + ne, [] (const E& a, const E& b) { return a.str < b.str; });
            bool keepMember[16];
            for (int g = 0; g < 16; ++g) keepMember[g] = true;
            for (int r = group.maxUp; r < ne; ++r) (e[r].src == 0 ? keepMember[e[r].k] : keepJoiner[e[r].k]) = false;
            for (int g = group.count - 1; g >= 0; --g)
                if (! keepMember[g]) stopMember (g, pos);
        }

        // 3) the new notes that ring: each on a free string (the nearest to where it would go; SCORE's as it is).
        //    None free: left out of an up-stroke, the synth's choice in a down-stroke
        int jForce[16], used[32], nUsed = 0;
        for (int g = 0; g < group.count; ++g) used[nUsed++] = group.m[g].str;   // the strings that stay
        for (int j = 0; j < count; ++j)
        {
            jForce[j] = -1;
            if (! keepJoiner[j]) continue;
            if (jScore[j]) { jForce[j] = jStr[j]; if (nUsed < 32) used[nUsed++] = jStr[j]; continue; }
            if (const int fs = nearestFree (notes[idx[j]], jStr[j], used, nUsed); fs > 0)
            {
                jStr[j] = jForce[j] = fs;
                if (nUsed < 32) used[nUsed++] = fs;
            }
            else if (group.dir == 1)
            {
                // no free string: of the notes lower than this one whose string can play it, the lowest gives up its
                // string (an up-stroke keeps its top, the melody); none: this note is left out
                const int nnj = notes[idx[j]];
                int low = -1;
                for (int g = 0; g < group.count; ++g)
                {
                    const int f = StringAllocator::fretOn (group.m[g].str, nnj);
                    if (group.m[g].note < nnj && f >= 0 && f <= kMaxFret && (low < 0 || group.m[g].note < group.m[low].note)) low = g;
                }
                if (low >= 0)
                {
                    const int s2 = group.m[low].str;
                    stopMember (low, pos);
                    jStr[j] = jForce[j] = s2;
                }
                else keepJoiner[j] = false;
            }
        }

        for (int j = 0; j < count; ++j)
        {
            const int i = idx[j];
            const int nn = notes[i], ch = juce::jlimit (1, 16, chs[i]);
            if (! keepJoiner[j])
            {
                dropped[ch - 1][nn] = true;
                if (onDroppedNoteOn) onDroppedNoteOn (ch, nn, vel[i], group.ppq, jStr[j], keyDirFor[nn]);
                continue;
            }
            const float jitter = (rng.nextFloat() * 2.0f - 1.0f) * 1.5f * group.humanize * 0.001f * (float) sr;
            // at most 8 ms apart: with a slow STRUM setting, quick single notes must not be pushed late
            const float step = juce::jmin (group.speedSamples, 0.008f * (float) sr);
            const juce::int64 at = juce::jmax (t, group.last + (juce::int64) std::llround (step + jitter));
            playStrung (pos, ch, nn, vel[i], group.count, group.dir, (int) (at - t), group.ppq, jForce[j], synth);
            addMember (jStr[j], nn, ch, vel[i], jForce[j] > 0, false, at);
        }
    }

    // a free string for `note` (not in used[]): `want` if it is free, else the nearest one. 0 = none
    static int nearestFree (int note, int want, const int* used, int nUsed)
    {
        auto ok = [&] (int s)
        {
            if (s < 1 || s > 6) return false;
            for (int u = 0; u < nUsed; ++u) if (used[u] == s) return false;
            const int f = StringAllocator::fretOn (s, note);
            return f >= 0 && f <= kMaxFret;
        };
        if (ok (want)) return want;
        for (int d = 1; d <= 5; ++d)
            for (int c : { want - d, want + d })
                if (ok (c)) return c;
        return 0;
    }

    // known: the synth plays it on `str` (we told it); guessOk: a single note whose `str` is the allocator's own guess
    void addMember (int str, int nn, int ch, int v, bool known, bool guessOk, juce::int64 startAt)
    {
        if (group.count >= 16) return;
        group.m[group.count++] = { str, nn, ch, v, known, guessOk, startAt };
    }

    // a string of this strum that must not ring after all: still waiting -> left out (it never sounds);
    // already started -> stopped quickly at `pos` (the synth's 12 ms choke)
    void stopMember (int g, int pos)
    {
        const int nn = group.m[g].note, ch = group.m[g].ch, str = group.m[g].str;
        bool wasWaiting = false, offWaiting = false;
        juce::int64 onTime = 0;
        for (int i = 0; i < queueCount; ++i)
            if (const auto& q = queue[(size_t) i]; q.msg.isNoteOn() && q.msg.getChannel() == ch && q.msg.getNoteNumber() == nn)
            {
                onTime = q.time; wasWaiting = true;
                queue[(size_t) i] = queue[(size_t) --queueCount];
                break;
            }
        // its own note-off, if it came already (queued after it; an older note's off brought before it stays)
        if (wasWaiting)
            for (int i = 0; i < queueCount; ++i)
                if (const auto& q = queue[(size_t) i]; q.msg.isNoteOff() && q.msg.getChannel() == ch && q.msg.getNoteNumber() == nn && q.time > onTime)
                {
                    offWaiting = true;
                    queue[(size_t) i] = queue[(size_t) --queueCount];
                    break;
                }
        if (wasWaiting)
        {
            delayOf[ch - 1][nn] = -1;
            if (juce::isPositiveAndBelow (nn, 128)) { upForDelayed[nn] = -1; velForDelayed[nn] = 0; stringForDelayed[nn] = -1; origPpq[nn] = -1.0; }
            if (onDroppedNoteOn) onDroppedNoteOn (ch, nn, group.m[g].vel, group.ppq, str, keyDirFor[nn]);
            if (offWaiting) { if (onDroppedNoteOff) onDroppedNoteOff (ch, nn, group.ppq); }
            else dropped[ch - 1][nn] = true;   // Logic's note-off for it is swallowed
        }
        else if (group.m[g].known) out.addEvent (GuitarSynth::chokeMessage (str), pos);
        // (not known: it sounded, the synth chose its string and it was let go already - it only rings out)
        for (int k = g; k + 1 < group.count; ++k) group.m[k] = group.m[k + 1];
        --group.count;
    }

    struct Group
    {
        bool active = false;
        juce::int64 t0 = 0, last = 0;
        int dir = 0, maxUp = 4, count = 0;
        struct Member { int str = 0, note = 0, ch = 1, vel = 0; bool known = false, guessOk = false; juce::int64 startAt = 0; };
        Member m[16];   // the notes of this strum and their strings
        float speedSamples = 0.0f, humanize = 0.0f;
        double ppq = -1.0;
    };
    Group group;

private:
    double sr = 44100.0;
    juce::int64 clock = 0;
    std::array<Delayed, 512> queue;
    int queueCount = 0;
    int delayOf[16][128] {};
    bool dropped[16][128] {};
    int upForDelayed[128] { };
    int stringForDelayed[128] { };   // the string the strummer chose, for notes that start later
    const Score::ApplyTable* curTable = nullptr;   // this block's (for SCORE's strings)
    bool curHavePpq = false, curTableIsScore = false;
    double curPpq0 = 0.0, curPpqPerSample = 0.0;

    // the string SCORE sets for this note at this position (-1: none, or it cannot play it)
    int scoreString (double ppq, int note) const
    {
        if (curTable == nullptr || ! curHavePpq || ppq < 0.0) return -1;
        const auto* e = curTable->find (Score::toTick (ppq), note);
        if (e == nullptr) return -1;
        const int s = curTableIsScore ? e->effString : e->ovString;
        if (s < 1 || s > 6) return -1;
        const int f = StringAllocator::fretOn (s, note);
        return f >= 0 && f <= kMaxFret ? s : -1;
    }
    int velForDelayed[128] { };   // the stroke's velocity (chooses the layer), for strings that start later
    double origPpq[128] {};
    int pendingDir = -1;
    int keyDirFor[128] {};
    juce::Random rng;
    juce::MidiBuffer out;

public:
    Strummer()
    {
        for (auto& v : upForDelayed) v = -1;
        for (auto& v : velForDelayed) v = 0;
        for (auto& v : stringForDelayed) v = -1;
        for (auto& v : keyDirFor) v = -1;
        for (auto& v : origPpq) v = -1.0;
        for (auto& d : delayOf) for (auto& v : d) v = -1;
    }
};
