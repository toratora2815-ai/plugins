#pragma once
// SCORE ページの譜面（タブ譜）。Logic と同期する:
//   Logic → プラグイン … 再生中に流れてきた MIDI（音・位置・Logic 側の奏法）で譜面を自動更新（Capture）
//   プラグイン → Logic … 画面で付けた奏法・弦（Override）は、次の再生から Logic の音にすぐ効く。
//                        奏法入りの MIDI をドラッグで Logic に戻すと、Logic 側に入ったものから印が消える
// 正しい音の並びはいつも Logic のリージョン。プラグインは音を足したり消したりしない（消えた音は次の再生で消える）。
#include <JuceHeader.h>
#include "Articulations.h"
#include <vector>
#include <map>
#include <algorithm>

namespace Score
{
constexpr int kTicks = 960;                 // ticks per quarter note
constexpr int kMatchTicks = kTicks / 32;    // a note within 1/32 beat and the same pitch is "the same note"

inline juce::int64 toTick (double ppq) { return (juce::int64) std::llround (ppq * kTicks); }

struct Note
{
    juce::int64 tick = 0, length = kTicks / 2;
    int note = 60, velocity = 100;
    int channel = 1;
    int logicArt = artNormal;   // articulation Logic sent (keyswitch / articulation set)
    int autoString = 0;         // string the plugin chose by itself
    juce::uint32 seenPass = 0;  // capture pass that last saw this note
};

// What the user set in the plugin for one note (-1 = leave as Logic has it)
struct Override
{
    int art = -1, string = -1;
    bool isEmpty() const { return art < 0 && string < 0; }
    bool operator== (const Override& o) const { return art == o.art && string == o.string; }
};

using Key = std::pair<juce::int64, int>;   // tick, pitch

//==============================================================================
// Audio-thread side: what to apply when a note starts (immutable snapshot, swapped in)
struct ApplyTable
{
    struct Entry
    {
        juce::int64 tick, length;
        int note, velocity;
        int effArt, effString;   // what the score says (preview / PLAY IN SYNC)
        int ovArt, ovString;     // plugin settings only (-1 = none) (FOLLOW LOGIC)
    };
    std::vector<Entry> entries;   // sorted by tick
    juce::int64 maxLength = 0, lastEnd = 0;   // longest note, end of the last note

    // during host playback only plugin overrides apply; during preview everything does
    const Entry* find (juce::int64 tick, int note) const
    {
        auto it = std::lower_bound (entries.begin(), entries.end(), tick - kMatchTicks,
                                    [] (const Entry& e, juce::int64 t) { return e.tick < t; });
        const Entry* best = nullptr;
        for (; it != entries.end() && it->tick <= tick + kMatchTicks; ++it)
            if (it->note == note && (best == nullptr || std::abs (it->tick - tick) < std::abs (best->tick - tick))) best = &*it;
        return best;
    }
};

//==============================================================================
// Events from the audio thread to the message thread (lock-free FIFO of PODs)
struct CaptureEvent
{
    enum Type : int { noteOn, noteOff, passStart, passEnd } type = noteOn;
    double ppq = 0.0;
    int note = 0, velocity = 0, channel = 1, logicArt = 0, autoString = 0;
};

class CaptureFifo
{
public:
    bool push (const CaptureEvent& e)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 < 1) return false;
        buf[(size_t) (n1 > 0 ? s1 : s2)] = e;
        fifo.finishedWrite (1);
        return true;
    }
    template <typename F> void popAll (F&& f)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) f (buf[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) f (buf[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }
private:
    static constexpr int size = 4096;
    juce::AbstractFifo fifo { size };
    std::array<CaptureEvent, size> buf;
};

//==============================================================================
// Message-thread model
class Model
{
public:
    std::vector<Note> notes;             // sorted by tick, then pitch
    std::map<Key, Override> overrides;   // keyed by the note's tick + pitch
    int orphaned = 0;                    // plugin settings whose note disappeared from Logic (last pass)
    double bpm = 120.0;
    int numerator = 4, denominator = 4;

    // ---- capture
    void handle (const CaptureEvent& e, int stringMode)
    {
        switch (e.type)
        {
            case CaptureEvent::passStart: passStartTick = toTick (e.ppq); ++pass; open.clear(); break;
            case CaptureEvent::passEnd:   endPass (toTick (e.ppq), ! e.velocity); break;   // velocity = 1: events were lost
            case CaptureEvent::noteOn:
            {
                const auto t = toTick (e.ppq);
                Note* n = findNote (t, e.note);
                if (n == nullptr)
                {
                    Note nn; nn.tick = t; nn.note = e.note;
                    notes.push_back (nn);
                    sort();
                    n = findNote (t, e.note);
                    changed = true;
                }
                if (n->velocity != e.velocity || n->logicArt != e.logicArt || n->channel != e.channel || n->autoString != e.autoString)
                    changed = true;
                n->tick = t;   // follow small moves in Logic
                n->velocity = e.velocity; n->channel = e.channel; n->logicArt = e.logicArt; n->autoString = e.autoString;
                n->seenPass = pass;
                open[e.note] = t;
                reflect (*n, stringMode);
                break;
            }
            case CaptureEvent::noteOff:
            {
                auto it = open.find (e.note);
                if (it == open.end()) break;
                if (auto* n = findNote (it->second, e.note))
                {
                    const auto len = juce::jmax ((juce::int64) 1, toTick (e.ppq) - n->tick);
                    if (std::abs (len - n->length) > kMatchTicks / 2) { n->length = len; changed = true; }
                }
                open.erase (it);
                break;
            }
        }
    }

    // a plugin setting that Logic now has too is no longer needed
    void reflect (const Note& n, int stringMode)
    {
        auto it = overrides.find ({ n.tick, n.note });
        if (it == overrides.end()) it = findOverride (n.tick, n.note);
        if (it == overrides.end()) return;
        Override o = it->second;
        const auto key = it->first;
        if (o.art >= 0 && o.art == n.logicArt) o.art = -1;
        if (o.string >= 1 && stringMode == 1 && n.channel == o.string) o.string = -1;
        overrides.erase (it);
        if (! o.isEmpty()) overrides[{ n.tick, n.note }] = o;   // re-key to the note's current tick
        if (! (o == Override {}) || key != Key { n.tick, n.note }) changed = true;
        else changed = true;
    }

    // notes in the played range that Logic did not send this time were deleted / moved in Logic
    void endPass (juce::int64 endTick, bool allowRemoval = true)
    {
        const auto a = passStartTick, b = endTick;
        if (b <= a || ! allowRemoval) { open.clear(); return; }   // events were lost: delete nothing
        const auto before = notes.size();
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n)
                     { return n.tick >= a && n.tick < b - kMatchTicks && n.seenPass != pass; }), notes.end());
        if (notes.size() != before) changed = true;
        // plugin settings whose note is gone are KEPT (a muted region must not wipe them);
        // they come back when the note does, or the user removes them ("消す")
        int lost = 0;
        juce::ignoreUnused (lost);
        orphaned = countOrphans();
        open.clear();
    }

    Note* findNote (juce::int64 tick, int note)
    {
        Note* best = nullptr;
        for (auto& n : notes)
            if (n.note == note && std::abs (n.tick - tick) <= kMatchTicks && (best == nullptr || std::abs (n.tick - tick) < std::abs (best->tick - tick)))
                best = &n;
        return best;
    }

    std::map<Key, Override>::iterator findOverride (juce::int64 tick, int note)
    {
        for (auto it = overrides.lower_bound ({ tick - kMatchTicks, -1 }); it != overrides.end() && it->first.first <= tick + kMatchTicks; ++it)
            if (it->first.second == note) return it;
        return overrides.end();
    }

    Override overrideFor (const Note& n) const
    {
        auto it = overrides.find ({ n.tick, n.note });
        return it != overrides.end() ? it->second : Override {};
    }
    int effectiveArt (const Note& n) const    { auto o = overrideFor (n); return o.art >= 0 ? o.art : n.logicArt; }
    int effectiveString (const Note& n) const { auto o = overrideFor (n); return o.string >= 1 ? o.string : n.autoString; }

    void setOverride (const Note& n, Override o)
    {
        if (o.art == n.logicArt) o.art = -1;
        if (o.string == n.autoString) o.string = -1;
        if (o.isEmpty()) overrides.erase ({ n.tick, n.note });
        else overrides[{ n.tick, n.note }] = o;
        changed = true;
    }

    int countOrphans()
    {
        int k = 0;
        for (auto& [key, o] : overrides) if (findNote (key.first, key.second) == nullptr) ++k;
        return k;
    }
    void removeOrphans()
    {
        for (auto it = overrides.begin(); it != overrides.end();)
            if (findNote (it->first.first, it->first.second) == nullptr) it = overrides.erase (it); else ++it;
        orphaned = 0; changed = true;
    }
    int pendingCount() { return (int) overrides.size() - countOrphans(); }

    void sort() { std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.tick != b.tick ? a.tick < b.tick : a.note < b.note; }); }

    std::shared_ptr<ApplyTable> buildTable() const
    {
        auto t = std::make_shared<ApplyTable>();
        t->entries.reserve (notes.size());
        for (auto& n : notes)
        {
            const auto o = overrideFor (n);
            t->entries.push_back ({ n.tick, n.length, n.note, n.velocity, effectiveArt (n), effectiveString (n), o.art, o.string });
            t->maxLength = juce::jmax (t->maxLength, n.length);
            t->lastEnd = juce::jmax (t->lastEnd, n.tick + n.length);
        }
        return t;
    }

    void clear() { notes.clear(); overrides.clear(); orphaned = 0; open.clear(); changed = true; }

    // ---- MIDI file (for dragging back into Logic, and importing)
    // Starts at the bar of the first note; keyswitches (C0..B0) right before the notes that need one.
    juce::MidiFile exportMidi (int stringMode, int& firstBar) const
    {
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (kTicks);
        juce::MidiMessageSequence seq;
        const juce::int64 barTicks = (juce::int64) kTicks * numerator * 4 / juce::jmax (1, denominator);
        const juce::int64 start = notes.empty() ? 0 : (notes.front().tick / barTicks) * barTicks;
        firstBar = (int) (start / barTicks) + 1;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator), 0);
        int latched = artNormal;
        for (auto& n : notes)
        {
            const double t = (double) (n.tick - start);
            const int art = effectiveArt (n);
            const int ch = stringMode == 1 ? juce::jlimit (1, 6, effectiveString (n)) : n.channel;
            if (art != latched || art != artNormal)
            {
                seq.addEvent (juce::MidiMessage::noteOn (ch, kKeyswitchLow + art, (juce::uint8) 100), juce::jmax (0.0, t - 2));
                seq.addEvent (juce::MidiMessage::noteOff (ch, kKeyswitchLow + art), juce::jmax (0.0, t - 1));
                latched = art;
            }
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity)), t);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.note), t + (double) n.length);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
        return mf;
    }

    // a MIDI file from disk (e.g. a region dragged out of Logic): keyswitches become Logic's articulation
    bool importMidi (const juce::File& f, int barOffset)
    {
        juce::FileInputStream in (f);
        juce::MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) return false;
        const int tpq = juce::jmax (1, (int) mf.getTimeFormat());
        notes.clear(); overrides.clear(); orphaned = 0;
        const juce::int64 barTicks = (juce::int64) kTicks * numerator * 4 / juce::jmax (1, denominator);
        for (int tr = 0; tr < mf.getNumTracks(); ++tr)
        {
            auto seq = *mf.getTrack (tr);
            seq.updateMatchedPairs();
            int latched = artNormal;
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto* ev = seq.getEventPointer (i);
                const auto& m = ev->message;
                if (m.isTempoMetaEvent() && m.getTimeStamp() == 0) bpm = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (! m.isNoteOn()) continue;
                if (GuitarSynthKeyswitch (m.getNoteNumber())) { latched = m.getNoteNumber() - kKeyswitchLow; continue; }
                Note n;
                n.tick = (juce::int64) std::llround (m.getTimeStamp() * kTicks / tpq) + barOffset * barTicks;
                n.note = m.getNoteNumber();
                n.velocity = m.getVelocity();
                n.channel = m.getChannel();
                n.logicArt = latched;
                const double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : m.getTimeStamp() + tpq / 2;
                n.length = juce::jmax ((juce::int64) 1, (juce::int64) std::llround ((off - m.getTimeStamp()) * kTicks / tpq));
                notes.push_back (n);
            }
        }
        sort();
        changed = true;
        return true;
    }

    // ---- state
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("SCORE");
        t.setProperty ("bpm", bpm, nullptr);
        t.setProperty ("num", numerator, nullptr);
        t.setProperty ("den", denominator, nullptr);
        juce::MemoryOutputStream mos;
        for (auto& n : notes)
        {
            mos.writeInt64 (n.tick); mos.writeInt64 (n.length);
            mos.writeByte ((char) n.note); mos.writeByte ((char) n.velocity); mos.writeByte ((char) n.channel);
            mos.writeByte ((char) n.logicArt); mos.writeByte ((char) n.autoString);
        }
        t.setProperty ("notes", mos.getMemoryBlock(), nullptr);
        for (auto& [k, o] : overrides)
        {
            juce::ValueTree c ("OV");
            c.setProperty ("t", (juce::int64) k.first, nullptr);
            c.setProperty ("n", k.second, nullptr);
            c.setProperty ("a", o.art, nullptr);
            c.setProperty ("s", o.string, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        clear();
        if (! t.hasType ("SCORE")) return;
        bpm = t.getProperty ("bpm", 120.0);
        numerator = t.getProperty ("num", 4);
        denominator = t.getProperty ("den", 4);
        if (const auto* mb = t.getProperty ("notes").getBinaryData())
        {
            juce::MemoryInputStream in (*mb, false);
            while (in.getNumBytesRemaining() >= 21)
            {
                Note n;
                n.tick = in.readInt64(); n.length = in.readInt64();
                n.note = (juce::uint8) in.readByte(); n.velocity = (juce::uint8) in.readByte(); n.channel = (juce::uint8) in.readByte();
                n.logicArt = (juce::uint8) in.readByte(); n.autoString = (juce::uint8) in.readByte();
                notes.push_back (n);
            }
        }
        for (auto c : t)
            if (c.hasType ("OV"))
                overrides[{ (juce::int64) c.getProperty ("t"), (int) c.getProperty ("n") }] = { (int) c.getProperty ("a", -1), (int) c.getProperty ("s", -1) };
        sort();
        changed = true;
    }

    bool changed = false;   // the UI / audio table needs updating

private:
    static bool GuitarSynthKeyswitch (int note) { return note >= kKeyswitchLow && note < kKeyswitchLow + numArticulations; }
    juce::uint32 pass = 0;
    juce::int64 passStartTick = 0;
    std::map<int, juce::int64> open;   // held notes during capture: pitch -> tick
};
} // namespace Score
