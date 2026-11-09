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
        recording = false; take.clear(); env.clear(); envS.clear(); pw.clear(); hf.clear();
        x1 = x2 = 0.0f; hfAcc = 0; hfPeak = -200.0f; floorAtStart = -90.0f;
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
            const float d2 = v - 2.0f * x1 + x2;   // 2nd difference: the pick's click, not the string's body
            x2 = x1; x1 = v;
            hfAcc += d2 * d2;
            if (accN >= frame)
            {
                onFrame ((float) (10.0 * std::log10 (acc / accN + 1e-14)), (float) (10.0 * std::log10 (hfAcc / accN + 1e-16)));
                acc = 0; hfAcc = 0; accN = 0;
            }
        }
    }

    float noiseFloorDb() const { return floorDb; }
    bool isRecording() const { return recording; }
    std::deque<std::vector<float>> finished;   // takes ready to judge

private:
    void onFrame (float db, float hfDb)
    {
        env.push_back (db);
        if (env.size() > 200) env.pop_front();   // 1 s of history
        hf.push_back (hfDb);
        if (hf.size() > 200) hf.pop_front();
        // 30 ms average (in power): 5 ms frames follow the waveform of 50 Hz hum / low strings, this does not
        pw.push_back (std::pow (10.0f, db * 0.1f));
        if (pw.size() > 6) pw.pop_front();
        float m = 0; for (auto v : pw) m += v;
        envS.push_back (10.0f * std::log10 (m / (float) pw.size() + 1e-14f));
        if (envS.size() > 200) envS.pop_front();
        const int nEnv = (int) env.size();
        // the first 60 ms only learn the noise floor (hum present from the start is not a pick)
        if (! recording && nEnv <= 12) { floorDb = juce::jmin (floorDb < -89.0f ? db : floorDb, db); return; }
        float minPrev = db;
        for (int k = 2; k <= 11 && k <= nEnv; ++k) minPrev = juce::jmin (minPrev, env[(size_t) (nEnv - k)]);
        float maxPrev = -200.0f;
        for (int k = 12; k <= 60 && k <= nEnv; ++k) maxPrev = juce::jmax (maxPrev, env[(size_t) (nEnv - k)]);

        const bool loud = db > juce::jmax (floorDb + 15.0f, -55.0f);
        const bool attack = loud && db - minPrev > 9.0f;

        if (! recording)
        {
            // noise floor (hum included): follows the level quickly down, slowly up, whenever nothing is being picked
            if (! attack) floorDb = db < floorDb ? db : floorDb + 0.02f * (db - floorDb);
            if (attack) start();
            return;
        }

        sinceStart += 1;
        peakDb = juce::jmax (peakDb, db);
        hfPeak = juce::jmax (hfPeak, hfDb);
        // the next pick while this one still rings: the pick's click (a jump in the highs) AND the level going up.
        // The level alone is fooled by a long low note (5 ms frames follow its waveform, beating brings it back up)
        {
            const int nH = (int) hf.size(), nS = (int) envS.size();
            float hfBefore = -200.0f;
            for (int k = 2; k <= 8 && k <= nH; ++k) hfBefore = juce::jmax (hfBefore, hf[(size_t) (nH - k)]);
            const bool click = hfDb - hfBefore > 10.0f && hfDb > hfPeak - 15.0f;
            const bool rises = nS > 7 && envS.back() > envS[(size_t) (nS - 7)] + 2.0f;
            if (click && rises && sinceStart > (brush ? 20 : 24))
            {
                end ((int) take.size() - frame);
                start();
                return;
            }
        }
        // the note has died into a steady noise floor (hum, amp hiss): 500 ms that hardly change, down near the
        // noise measured before the pick (a beating note can look flat for a while far above it).
        // A ringing note keeps falling (a low E loses ~4 dB in 500 ms), so it is not cut
        bool flat = false;
        const int nS = (int) envS.size();
        if (nS >= 100 && envS.back() < peakDb - 20.0f && envS.back() < floorAtStart + 10.0f)
        {
            float lo = 200.0f, hi = -200.0f;
            for (int k = 1; k <= 100; ++k) { lo = juce::jmin (lo, envS[(size_t) (nS - k)]); hi = juce::jmax (hi, envS[(size_t) (nS - k)]); }
            flat = hi - lo < 1.5f;
        }
        const bool quiet = db < peakDb - 45.0f || db < floorDb + 6.0f || flat;
        quietFrames = quiet ? quietFrames + 1 : 0;
        const int maxFrames = (int) ((brush ? 1.5 : 12.0) / 0.005);
        if (quietFrames > 50 || sinceStart > maxFrames || (brush && sinceStart > 60 && quietFrames > 10))
            end ((int) take.size());
    }

    void start()
    {
        recording = true;
        sinceStart = 0; peakDb = -200.0f; hfPeak = -200.0f; quietFrames = 0;
        floorAtStart = floorDb;
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
    std::deque<float> env, envS, pw, hf;
    float x1 = 0.0f, x2 = 0.0f, hfPeak = -200.0f, floorAtStart = -90.0f;
    double hfAcc = 0;
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

inline float energyAt (const std::vector<float>& x, int a, int b, double sr, double hz);

// the strongest spot within +-cents of hz (a note 10-20 cents off must still be found)
inline float energyAround (const std::vector<float>& x, int a, int b, double sr, double hz, double cents = 50.0, int points = 11)
{
    float best = -200.0f;
    for (int k = 0; k < points; ++k)
    {
        const double c = -cents + 2.0 * cents * k / juce::jmax (1, points - 1);
        best = juce::jmax (best, energyAt (x, a, b, sr, hz * std::pow (2.0, c / 1200.0)));
    }
    return best;
}

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
// belowDb / aboveDb: loudness of the same note in the layer under / over this one (0 = not recorded):
// MEDIUM is compared with SOFT and HARD, HARD with MEDIUM (else SOFT)
inline Result judge (const std::vector<float>& take, double sr, const Step& st, int prevNote, float belowDb, float aboveDb = 0.0f)
{
    Result r;
    juce::AudioBuffer<float> b (1, (int) take.size());
    for (size_t i = 0; i < take.size(); ++i) b.setSample (0, (int) i, take[i]);
    const float pk = b.getMagnitude (0, b.getNumSamples());
    r.peakDb = juce::Decibels::gainToDecibels (pk, -200.0f);
    if (r.peakDb >= -1.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"音が割れています（インターフェイスの入力つまみを下げて）")); return r; }
    if (r.peakDb < -40.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"音が小さすぎます（入力つまみを上げて）")); return r; }

    const bool brush = st.slot == Kit::slotBrush;

    // it must start with a pick: the click in the highs within the first 40 ms, well above what came before
    if (! brush)
    {
        const int fr = juce::jmax (1, (int) (sr * 0.005));
        auto hfAt = [&] (int a0, int b0)
        {
            double e = 0; int cnt = 0;
            for (int i = juce::jmax (2, a0); i < juce::jmin ((int) take.size(), b0); ++i)
            { const float d = take[(size_t) i] - 2.0f * take[(size_t) (i - 1)] + take[(size_t) (i - 2)]; e += d * d; ++cnt; }
            return (float) (10.0 * std::log10 (e / juce::jmax (1, cnt) + 1e-16));
        };
        const float before = hfAt (0, (int) (0.008 * sr));
        float pickHf = -200.0f;
        for (int a0 = (int) (0.008 * sr); a0 < (int) (0.055 * sr); a0 += fr) pickHf = juce::jmax (pickHf, hfAt (a0, a0 + fr));
        if (pickHf - before < 10.0f)
        {
            r.reason = juce::String (juce::CharPointer_UTF8 (u8"弾いた瞬間が取れていません（もう一回）"));
            return r;
        }
    }

    // picked twice in a flash (a "flam"): the pick's click comes twice. The click is in the highs
    // (sample-to-sample difference), where a ringing string is quiet; 5 ms frames, 30-200 ms after the first
    if (! brush)
    {
        const int fr = juce::jmax (1, (int) (sr * 0.005));
        std::vector<float> hf;
        for (int i = 1; i + fr <= (int) take.size(); i += fr)
        {
            double e = 0;
            for (int k = 0; k < fr; ++k)
            {
                if (i + k < 2) continue;
                // 2nd difference: +12 dB/octave, so the pick's click stands far above the string's harmonics
                const float d = take[(size_t) (i + k)] - 2.0f * take[(size_t) (i + k - 1)] + take[(size_t) (i + k - 2)];
                e += d * d;
            }
            hf.push_back ((float) (10.0 * std::log10 (e / fr + 1e-16)));
        }
        int first = -1; float hfPeak = -200.0f;
        for (int i = 0; i < juce::jmin ((int) hf.size(), 12); ++i) if (hf[(size_t) i] > hfPeak) { hfPeak = hf[(size_t) i]; first = i; }
        if (first >= 0)
            for (int i = first + 6; i < juce::jmin ((int) hf.size(), first + 40); ++i)
            {
                float before = -200.0f;
                for (int k = 2; k <= 6; ++k) before = juce::jmax (before, hf[(size_t) (i - k)]);
                // a second pick clicks nearly as hard as the first, and out of a quieter moment
                if (hf[(size_t) i] - before > 10.0f && hf[(size_t) i] > hfPeak - 6.0f)
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
        const float rel = energyAround (take, a, e, sr, juce::MidiMessage::getMidiNoteInHertz (prevNote))
                        - energyAround (take, a, e, sr, juce::MidiMessage::getMidiNoteInHertz (st.expectedNote()));
        // nearly as loud as the new note = the previous string was not muted. Quieter = sympathetic ringing
        // (with a capo every string is open and rings along): only a warning
        if (rel > -12.0f) { r.reason = juce::String (juce::CharPointer_UTF8 (u8"前の弦が鳴っています（ミュートしてからもう一回）")); return r; }
        if (rel > -30.0f) r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"前の弦の鳴り残り（共鳴）が少しある音があります")));
    }

    // mains hum (50 Hz east / 60 Hz west Japan): below every guitar note, so easy to see. A warning only
    if (! brush && take.size() > (size_t) (0.7 * sr))
    {
        const int a = (int) (0.15 * sr), e = (int) (0.65 * sr);
        const float note = energyAround (take, a, e, sr, juce::MidiMessage::getMidiNoteInHertz (st.expectedNote()));
        const float hum = juce::jmax (energyAround (take, a, e, sr, 50.0, 30.0, 5), energyAround (take, a, e, sr, 60.0, 30.0, 5));
        if (hum - note > -35.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"ハム（ブーンという電源の雑音）が入っています。ギターの向きを変える、電源タップから離す、と減ります")));
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
        const float db = juce::Decibels::gainToDecibels (s.rawRms);
        if (st.slot == Kit::slotHard && belowDb < 0.0f && db - belowDb < 3.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"HARD が一つ下の層（MEDIUM か SOFT）とあまり変わらない音があります（もっと強く弾くと差が出ます）")));
        if (st.slot == Kit::slotMedium && belowDb < 0.0f && db - belowDb < 2.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"MEDIUM が SOFT とあまり変わらない音があります（少し強めに）")));
        if (st.slot == Kit::slotMedium && aboveDb < 0.0f && aboveDb - db < 2.0f)
            r.warnings.add (juce::String (juce::CharPointer_UTF8 (u8"MEDIUM が HARD とあまり変わらない音があります（少し弱めに）")));
    }
    r.sample = std::move (s);
    r.ok = true;
    return r;
}
} // namespace Guide
