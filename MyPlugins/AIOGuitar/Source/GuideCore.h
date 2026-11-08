#pragma once
// 録音ガイド（単体アプリだけ）の中身: 入力の受け取り、録る順番、1 音ずつの切り出し、判定。
// 判定は取り込み（SampleKit の slice / measurePitch）と同じ関数を使う。
// だめにするのは「半音以上のずれ・割れ・小さすぎ・前の弦の鳴り残り・2 回弾き・短すぎ」だけ。セントのずれなどは警告。
#include <JuceHeader.h>
#include "SampleKit.h"
#include <vector>
#include <deque>

namespace Guide
{
//==============================================================================
// audio thread -> message thread: the guitar input (mono), lock-free
struct InputTap
{
    std::atomic<bool> active { false };
    std::atomic<float> peak { 0.0f };    // since the UI last read it
    std::atomic<int> channels { 0 };
    std::atomic<int> useChannel { -1 };  // -1 = automatic (the one the guitar is on), else 0 / 1 ...
    std::atomic<int> usedChannel { 0 };  // what automatic picked
    std::atomic<bool> overflow { false };

    // only the stand-alone app needs the buffer (the AU never records)
    void allocate() { if (buf.empty()) buf.assign ((size_t) size, 0.0f); }

    void push (const float* const* ch, int numCh, int n)   // audio thread
    {
        channels.store (numCh);
        if (! active.load() || numCh <= 0 || buf.empty()) return;

        // which channel: a guitar in input 1 of a 2-input interface must not be averaged with input 2's hiss
        for (int c = 0; c < juce::jmin (numCh, 8); ++c)
        {
            float m = 0.0f;
            for (int i = 0; i < n; ++i) m = juce::jmax (m, std::abs (ch[c][i]));
            chPeak[c] = juce::jmax (m, chPeak[c] * std::pow (0.9999f, (float) n));
        }
        int use = useChannel.load();
        if (use < 0 || use >= numCh)
        {
            use = juce::jlimit (0, numCh - 1, autoCh);
            for (int c = 0; c < juce::jmin (numCh, 8); ++c)
                if (chPeak[c] > 3.0f * chPeak[use] && chPeak[c] > 0.003f) use = c;   // switch only to a clearly louder one
            autoCh = use;
        }
        usedChannel.store (use);
        const float* x = ch[use];

        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 + n2 < n) overflow.store (true);   // the UI stopped reading: part of the recording is lost
        float pk = 0.0f;
        for (int i = 0; i < n1; ++i) { buf[(size_t) (s1 + i)] = x[i]; pk = juce::jmax (pk, std::abs (x[i])); }
        for (int i = 0; i < n2; ++i) { buf[(size_t) (s2 + i)] = x[n1 + i]; pk = juce::jmax (pk, std::abs (x[n1 + i])); }
        fifo.finishedWrite (n1 + n2);
        if (pk > peak.load()) peak.store (pk);
    }

    int pull (float* dst, int maxN)   // message thread
    {
        if (buf.empty()) return 0;
        int s1, n1, s2, n2;
        fifo.prepareToRead (juce::jmin (maxN, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy (buf.begin() + s1, buf.begin() + s1 + n1, dst);
        std::copy (buf.begin() + s2, buf.begin() + s2 + n2, dst + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
    void clear() { fifo.reset(); }

private:
    static constexpr int size = 1 << 21;   // ~44 s at 48 kHz
    juce::AbstractFifo fifo { size };
    std::vector<float> buf;
    float chPeak[8] {};
    int autoCh = 0;
};

//==============================================================================
// one thing to play
struct Step
{
    int slot = Kit::slotSoft;
    int capo = 0;       // fret the capo is on (the note is the open string with the capo)
    int string = 6;     // 1..6, 0 = brushing (all strings)
    bool up = false;
    int expectedNote() const { return string >= 1 ? Kit::kOpen[string] + capo : -1; }
};

// the same order as README: capo 0/3/6/9/12 (+15 on strings 3-1), low string -> high, down then up;
// palm mute: strings 6-4 at 0/3/6/9; brushing: down, up x 4
inline std::vector<Step> buildSteps (int slot)
{
    std::vector<Step> v;
    if (slot == Kit::slotBrush)
    {
        for (int k = 0; k < 8; ++k) v.push_back ({ slot, 0, 0, (k % 2) == 1 });
        return v;
    }
    const bool mute = slot == Kit::slotMute;
    const int positions[] { 0, 3, 6, 9, 12 };
    for (int pos : positions)
    {
        if (mute && pos > 9) break;
        for (int s = 6; s >= (mute ? 4 : 1); --s)
            for (int up = 0; up < 2; ++up) v.push_back ({ slot, pos, s, up == 1 });
    }
    if (! mute)
        for (int s = 3; s >= 1; --s)
            for (int up = 0; up < 2; ++up) v.push_back ({ slot, 15, s, up == 1 });
    return v;
}

//==============================================================================
// cuts the incoming signal into takes: a pick starts one, it ends when the note dies away
// (or is muted), or when the next pick comes (play on without muting)
class TakeDetector
{
public:
    void prepare (double sampleRate, bool brushing)
    {
        sr = sampleRate;
        brush = brushing;
        frame = juce::jmax (1, (int) (sr * 0.005));
        pre.assign ((size_t) (sr * 0.015), 0.0f);   // 15 ms before the pick (the slicer adds its own fade-in)
        prePos = 0; acc = 0; accN = 0;
        recording = false; take.clear(); env.clear();
        floorDb = -90.0f; quietFrames = 0;
        finished.clear();
    }

    void feed (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float v = x[i];
            pre[(size_t) prePos] = v; prePos = (prePos + 1) % (int) pre.size();
            if (recording) take.push_back (v);
            acc += v * v; ++accN;
            if (accN >= frame) { onFrame ((float) (10.0 * std::log10 (acc / accN + 1e-14))); acc = 0; accN = 0; }
        }
    }

    float noiseFloorDb() const { return floorDb; }
    bool isRecording() const { return recording; }
    std::deque<std::vector<float>> finished;   // takes ready to judge

private:
    void onFrame (float db)
    {
        env.push_back (db);
        if (env.size() > 200) env.pop_front();   // 1 s of history
        const int nEnv = (int) env.size();
        float minPrev = nEnv < 11 ? -200.0f : db;   // no history yet = silence before
        for (int k = 2; k <= 11 && k <= nEnv; ++k) minPrev = juce::jmin (minPrev, env[(size_t) (nEnv - k)]);
        float maxPrev = -200.0f;
        for (int k = 12; k <= 60 && k <= nEnv; ++k) maxPrev = juce::jmax (maxPrev, env[(size_t) (nEnv - k)]);

        const bool loud = db > juce::jmax (floorDb + 15.0f, -55.0f);
        const bool attack = loud && db - minPrev > 9.0f;

        if (! recording)
        {
            // noise floor: follows quiet frames quickly down, slowly up
            if (! loud) floorDb = db < floorDb ? db : floorDb + 0.02f * (db - floorDb);
            if (attack) start();
            return;
        }

        sinceStart += 1;
        peakDb = juce::jmax (peakDb, db);
        // the next pick while this one still rings
        if (attack && sinceStart > (brush ? 20 : 24) && db > maxPrev - 1.0f)
        {
            end ((int) take.size() - frame);
            start();
            return;
        }
        const bool quiet = db < peakDb - 45.0f || db < floorDb + 6.0f;
        quietFrames = quiet ? quietFrames + 1 : 0;
        const int maxFrames = (int) ((brush ? 1.5 : 12.0) / 0.005);
        if (quietFrames > 50 || sinceStart > maxFrames || (brush && sinceStart > 60 && quietFrames > 10))
            end ((int) take.size());
    }

    void start()
    {
        recording = true;
        sinceStart = 0; peakDb = -200.0f; quietFrames = 0;
        take.clear();
        // 15 ms before the pick
        for (size_t k = 0; k < pre.size(); ++k) take.push_back (pre[(size_t) ((prePos + (int) k) % (int) pre.size())]);
    }

    void end (int length)
    {
        recording = false;
        if (length > 0)
        {
            take.resize ((size_t) length);
            // a little silence after, as the slicer expects
            take.insert (take.end(), (size_t) (sr * 0.05), 0.0f);
            finished.push_back (std::move (take));
        }
        take = {};
    }

    double sr = 48000.0;
    bool brush = false, recording = false;
    int frame = 240, prePos = 0, accN = 0, sinceStart = 0, quietFrames = 0;
    double acc = 0;
    float floorDb = -90.0f, peakDb = -200.0f;
    std::vector<float> pre, take;
    std::deque<float> env;
};

//==============================================================================
struct Result
{
    bool ok = false;
    juce::String reason;          // one short reason when not ok
    juce::StringArray warnings;   // shown at the end
    Kit::Sample sample;
    float peakDb = -200.0f;
    bool wrongNote = false;       // pitch says another string / capo position
};

inline float energyAt (const std::vector<float>& x, int a, int b, double sr, double hz)
{
    a = juce::jmax (0, a); b = juce::jmin ((int) x.size(), b);
    if (b - a < 64 || hz <= 0) return -200.0f;
    const double w = juce::MathConstants<double>::twoPi * hz / sr;
    double re = 0, im = 0;
    for (int i = a; i < b; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - a) / (b - a - 1));
        re += x[(size_t) i] * win * std::cos (w * i); im -= x[(size_t) i] * win * std::sin (w * i);
    }
    return (float) (10.0 * std::log10 (re * re + im * im + 1e-20));
}

// prevNote: the note played just before (-1 none) - its string should be muted by now.
// softRmsDb: for HARD picking, the SOFT take of the same note (0 = none)
inline Result judge (const std::vector<float>& take, double sr, const Step& st, int prevNote, float softRmsDb)
{
    Result r;
    juce::AudioBuffer<float> b (1, (int) take.size());
    for (size_t i = 0; i < take.size(); ++i) b.setSample (0, (int) i, take[i]);
    const float pk = b.getMagnitude (0, b.getNumSamples());
    r.peakDb = juce::Decibels::gainToDecibels (pk, -200.0f);
    if (r.peakDb >= -1.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"音が割れています（インターフェイスの入力つまみを下げて）")); return r; }
    if (r.peakDb < -40.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"音が小さすぎます（入力つまみを上げて）")); return r; }

    const bool brush = st.slot == Kit::slotBrush;

    // picked twice in a flash (a "flam"): the pick's click comes twice. The click is in the highs
    // (sample-to-sample difference), where a ringing string is quiet; 5 ms frames, 30-200 ms after the first
    if (! brush)
    {
        const int fr = juce::jmax (1, (int) (sr * 0.005));
        std::vector<float> hf;
        for (int i = 1; i + fr <= (int) take.size(); i += fr)
        {
            double e = 0;
            for (int k = 0; k < fr; ++k) { const float d = take[(size_t) (i + k)] - take[(size_t) (i + k - 1)]; e += d * d; }
            hf.push_back ((float) (10.0 * std::log10 (e / fr + 1e-16)));
        }
        int first = -1; float hfPeak = -200.0f;
        for (int i = 0; i < juce::jmin ((int) hf.size(), 12); ++i) if (hf[(size_t) i] > hfPeak) { hfPeak = hf[(size_t) i]; first = i; }
        if (first >= 0)
            for (int i = first + 6; i < juce::jmin ((int) hf.size(), first + 40); ++i)
            {
                float before = -200.0f;
                for (int k = 2; k <= 6; ++k) before = juce::jmax (before, hf[(size_t) (i - k)]);
                if (hf[(size_t) i] - before > 8.0f && hf[(size_t) i] > hfPeak - 10.0f)
                {
                    r.reason = juce::String (juce::CharPointer_UTF8 (u8"2 回鳴りました（1 回だけ弾いて）"));
                    return r;
                }
            }
    }
    // the previous string still ringing (checked before the pitch: it is what confuses it)
    if (! brush && prevNote >= 0 && prevNote != st.expectedNote() && st.slot != Kit::slotMute)
    {
        const int a = (int) (0.15 * sr), e = (int) (0.6 * sr);
        const float rel = energyAt (take, a, e, sr, juce::MidiMessage::getMidiNoteInHertz (prevNote))
                        - energyAt (take, a, e, sr, juce::MidiMessage::getMidiNoteInHertz (st.expectedNote()));
        if (rel > -24.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"前の弦が鳴っています（ミュートしてからもう一回）")); return r; }
        if (rel > -32.0f) r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"前の弦の鳴り残りが少しある音があります")));
    }

    auto res = Kit::slice (b, sr, st.slot);
    if (res.samples.size() > 1) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"2 回鳴りました（1 回だけ弾いて）")); return r; }
    if (res.samples.empty())
    {
        r.reason = brush || res.unpitched == 0 ? juce::String (juce::CharPointer_UTF8 (u8"短すぎます（鳴り終わるまで伸ばして）"))
                                               : juce::String (juce::CharPointer_UTF8 (u8"音程が取れません（はっきり弾いて）"));
        return r;
    }
    auto s = std::move (res.samples.front());
    const double len = s.audio.getNumSamples() / s.sampleRate;
    s.slot = st.slot;
    s.up = st.up;

    if (! brush)
    {
        const int want = st.expectedNote();
        const int d = s.rootNote - want;
        if (std::abs (d) == 12 || std::abs (d) == 24)
        {
            // octave slips are a detection error on weak fundamentals, not a wrong note
            s.rootHz = (float) (s.rootHz * std::pow (2.0, -d / 12.0));
            s.rootNote = want;
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"オクターブ違いに測れた音があります（そのまま使います）")));
        }
        else if (d != 0)
        {
            r.wrongNote = true;
            r.reason = juce::String (juce::CharPointer_UTF8 (u8"狙いより ")) + (d > 0 ? "+" : "") + juce::String (d)
                     + juce::String (juce::CharPointer_UTF8 (u8" 半音ずれています（弦かカポの位置を確認）"));
            return r;
        }
        if (std::abs (s.cents) > 25.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"音程が ")) + juce::String (s.cents, 0)
                            + juce::String (juce::CharPointer_UTF8 (u8" セントずれた音があります（Pitch Correct で直ります）")));
        if (len < (st.slot == Kit::slotMute ? 0.15 : 0.4))
        {
            r.reason = juce::String (juce::CharPointer_UTF8 (u8"短すぎます（鳴り終わるまで伸ばして）"));
            return r;
        }
        s.string = st.string;
        s.fret = st.capo;
        if (st.slot == Kit::slotHard && softRmsDb < 0.0f
            && juce::Decibels::gainToDecibels (s.rawRms) - softRmsDb < 4.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"HARD が SOFT とあまり変わらない音があります（もっと強く弾くと差が出ます）")));
    }
    r.sample = std::move (s);
    r.ok = true;
    return r;
}
} // namespace Guide
