#pragma once
// 録った声（1 本の音）から、メロディー／ドラムの MIDI・キー・テンポを出す。JUCE なし（単体テスト可）。
//  - 声 → MIDI は Hum Keys と同じエンジン（VoiceEngine.h）を、録り終えた音に後からまとめてかける
//  - 1 回目は音階なしで音程を集めてキーを当て、2 回目はそのキーの音階に合わせてノートにする
//  - テンポは、音の出だしの並びの自己相関（何秒おきに出だしがそろうか）＋ 105 BPM あたりを少しひいき
#include "VoiceEngine.h"
#include "KeyFinder.h"
#include <map>
#include <memory>
#include <string>

namespace idea
{

struct Note
{
    double s = 0, e = 0;   // 秒
    int n = 60, vel = 100, ch = 0;   // ch は 0 始まり（ドラムは 9）
};

enum Kind { kMelody = 0, kDrums = 1, kBoth = 2 };

struct Result
{
    bool heard = false;        // 声が入っていたか
    double seconds = 0, peakDb = -100;
    std::vector<Note> melody, drums;
    hum::KeyGuess key;
    double bpm = 100;
    float tempoConf = 0;       // 0..1（0.3 未満はあてずっぽう）
    float voicedRatio = 0;     // 鳴っていた所のうち、音程がとれた割合（鼻歌らしさ）
    int kind = kMelody;
};

// 口ドラムの聞き分け（Hum Keys で LEARN した癖をそのまま使う。最初は Hum Keys と同じ値）
struct DrumVoice
{
    static constexpr int ND = hum::Params::numDrums, NF = hum::Params::numFeat;
    int notes[ND];
    float proto[ND][NF], sus[ND];
    bool has[ND];
    DrumVoice()
    {
        hum::Params d;
        for (int c = 0; c < ND; ++c)
        {
            notes[c] = d.drumNote[c].load(); sus[c] = d.protoSus[c].load(); has[c] = d.has[c].load();
            for (int k = 0; k < NF; ++k) proto[c][k] = d.proto[c][k].load();
        }
    }
    void applyTo (hum::Params& p) const
    {
        for (int c = 0; c < ND; ++c)
        {
            p.drumNote[c] = notes[c]; p.protoSus[c] = sus[c]; p.has[c] = has[c];
            for (int k = 0; k < NF; ++k) p.proto[c][k] = proto[c][k];
        }
    }
};

//==============================================================================
namespace detail
{
    struct NullSink : hum::MidiSink { void send (uint8_t, uint8_t, uint8_t) override {} };

    // 記録された MIDI（CapEvent）をノートの並びにする（Hum Keys の rebuildTakeNotes と同じ考え方）
    inline std::vector<Note> toNotes (const std::vector<hum::CapEvent>& ev, double fs, double endT)
    {
        std::vector<Note> out;
        std::map<int, size_t> open;
        long lastOn = -1;
        for (auto& c : ev)
        {
            const int st = c.st & 0xF0, ch = c.st & 0x0F, key = ch * 128 + c.d1;
            const double t = (double) c.t / fs;
            if (c.kind == 1)
            {
                if (lastOn >= 0)
                {
                    auto& n = out[(size_t) lastOn];
                    open.erase (n.ch * 128 + n.n);
                    n.n = c.d1;
                    open[n.ch * 128 + n.n] = (size_t) lastOn;
                }
                continue;
            }
            if (st == 0x90 && c.d2 > 0)
            {
                if (auto it = open.find (key); it != open.end()) out[it->second].e = t;
                out.push_back ({ t, -1.0, c.d1, c.d2, ch });
                lastOn = (long) out.size() - 1;
                open[key] = (size_t) lastOn;
            }
            else if (st == 0x80 || (st == 0x90 && c.d2 == 0))
            {
                if (auto it = open.find (key); it != open.end())
                {
                    out[it->second].e = std::max (t, out[it->second].s + 0.01);
                    open.erase (it);
                }
            }
        }
        std::vector<Note> keep;
        for (auto n : out)
        {
            if (n.e < 0) n.e = endT;
            if (n.e - n.s >= 0.03) keep.push_back (n);   // 30 ms 未満のかけらは捨てる
        }
        return keep;
    }

    struct RunStats { std::vector<float> pitches; int above = 0, voiced = 0; };

    inline std::vector<Note> run (hum::Engine& e, const std::vector<float>& x, double fs, RunStats* st)
    {
        NullSink sink;
        e.prepare (fs);
        std::vector<hum::CapEvent> ev;
        const float gate = e.p.gateDb.load();
        auto drain = [&]
        {
            hum::CapEvent c; while (e.capture.pop (c)) ev.push_back (c);
            hum::Frame f;
            while (e.frames.pop (f))
                if (st != nullptr && f.level > gate)
                {
                    ++st->above;
                    if (f.pitch > 0) { ++st->voiced; st->pitches.push_back (f.pitch); }
                }
            hum::Hit h; while (e.hits.pop (h)) {}
        };
        const int B = 512;
        for (size_t i = 0; i < x.size(); i += B)
        {
            e.process (x.data() + i, (int) std::min<size_t> (B, x.size() - i), sink);
            drain();
        }
        // 後ろに 0.6 秒の無音を足して、鳴りっぱなしの音を自然に止める
        std::vector<float> z ((size_t) B, 0.0f);
        for (int k = 0; k < (int) (fs * 0.6) / B; ++k) { e.process (z.data(), B, sink); drain(); }
        st = nullptr;
        e.allOff (sink);
        drain();
        return toNotes (ev, fs, (double) x.size() / fs);
    }
}

//==============================================================================
struct Tempo { double bpm = 100; float conf = 0; };

// onsets = (秒, 強さ)。出だしが 4 つ未満なら fallback のまま
inline Tempo findTempo (std::vector<std::pair<double, double>> onsets, double fallback)
{
    Tempo r; r.bpm = fallback;
    if (onsets.size() < 4) return r;
    std::sort (onsets.begin(), onsets.end());
    const double hop = 0.01, sig = 0.025;
    const double T = onsets.back().first - onsets.front().first + 0.5;
    const int N = (int) (T / hop) + 1;
    std::vector<double> s ((size_t) N, 0.0);
    for (auto& [t, w] : onsets)
    {
        const double c = (t - onsets.front().first + 0.1) / hop;
        for (int k = std::max (0, (int) (c - 4 * sig / hop)); k <= std::min (N - 1, (int) (c + 4 * sig / hop)); ++k)
        {
            const double d = ((double) k - c) * hop / sig;
            s[(size_t) k] += std::sqrt (std::max (0.05, w)) * std::exp (-0.5 * d * d);
        }
    }
    const int maxLag = std::min (N - 1, (int) (2.6 / hop));
    std::vector<double> A ((size_t) maxLag + 2, 0.0);
    for (int L = 0; L <= maxLag; ++L)
    {
        double a = 0;
        for (int k = 0; k + L < N; ++k) a += s[(size_t) k] * s[(size_t) (k + L)];
        A[(size_t) L] = a;
    }
    if (A[0] <= 0) return r;
    for (auto& a : A) a /= A[0];
    auto at = [&] (double lagSec)
    {
        const double x = lagSec / hop;
        const int i = (int) x;
        if (i < 0 || i + 1 > maxLag) return 0.0;
        const double f = x - i;
        return A[(size_t) i] * (1 - f) + A[(size_t) i + 1] * f;
    };
    double best = -1, bestBpm = fallback, bestA = 0;
    for (double bpm = 55; bpm <= 190; bpm += 0.25)
    {
        const double L = 60.0 / bpm;
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 105.0) / 0.8, 2.0));
        const double sc = (at (L) + 0.5 * at (2 * L) + 0.5 * at (L / 2)) * prior;   // 半拍にも出だしがそろうなら本物の拍らしい
        if (sc > best) { best = sc; bestBpm = bpm; bestA = at (L); }
    }
    r.bpm = std::round (bestBpm);
    r.conf = (float) std::clamp (bestA, 0.0, 1.0);
    return r;
}

//==============================================================================
// x = モノラルの録音。fallbackBpm = 出だしが少なくてテンポが分からないときの値
inline Result analyse (std::vector<float> x, double fs, const DrumVoice& dv, double fallbackBpm = 100)
{
    Result r;
    r.seconds = (double) x.size() / fs;
    float peak = 0;
    for (float v : x) peak = std::max (peak, std::abs (v));
    r.peakDb = 20.0 * std::log10 (peak + 1e-9);
    r.bpm = fallbackBpm;
    if (r.peakDb < -55 || x.size() < (size_t) (fs * 0.3)) return r;
    // マイクの音量に左右されないよう、一番大きい所を -3 dB にそろえる（上げるのは 40 dB まで）
    const float g = std::min (100.0f, 0.708f / peak);
    for (auto& v : x) v *= g;
    r.heard = true;

    auto e = std::make_unique<hum::Engine>();
    e->p.mode = 0; e->p.scale = hum::scChromatic; e->p.octave = 0; e->p.glueMs = 40.0f;
    detail::RunStats st;
    detail::run (*e, x, fs, &st);
    r.voicedRatio = st.above > 0 ? (float) st.voiced / (float) st.above : 0.0f;
    r.key = hum::guessKey (st.pitches, 64.0f);
    if (! r.key.ok) { r.key.key = 0; r.key.minor = false; r.key.octave = 0; }

    // 2 回目: キーの音階に合わせる
    e = std::make_unique<hum::Engine>();
    e->p.mode = 0; e->p.key = r.key.key; e->p.scale = r.key.minor ? hum::scMinor : hum::scMajor;
    e->p.octave = r.key.octave; e->p.glueMs = 40.0f;
    r.melody = detail::run (*e, x, fs, nullptr);

    // ドラム
    e = std::make_unique<hum::Engine>();
    e->p.mode = 1;
    dv.applyTo (e->p);
    e->p.channel = 10;
    r.drums = detail::run (*e, x, fs, nullptr);

    // 鼻歌か口ドラムか
    if (r.voicedRatio >= 0.5f || r.drums.size() < 3) r.kind = kMelody;
    else if (r.voicedRatio < 0.3f || r.melody.size() < 3) r.kind = kDrums;
    else r.kind = kBoth;

    std::vector<std::pair<double, double>> on;
    if (r.kind != kDrums) for (auto& n : r.melody) on.push_back ({ n.s, n.vel / 127.0 });
    if (r.kind != kMelody) for (auto& n : r.drums) on.push_back ({ n.s, n.vel / 127.0 });
    const auto t = findTempo (on, fallbackBpm);
    r.bpm = t.bpm; r.tempoConf = t.conf;
    return r;
}

//==============================================================================
// MIDI ファイル（SMF タイプ 1）を作る。最初の音が 1 小節目の頭に来る
struct MidiOptions
{
    int key = 0; bool minor = false;
    double bpm = 100;
    int kind = kMelody;
    int quantize = 0;   // 0 = しない, 1 = 16 分, 2 = 8 分
};

inline const char* keyName (int k)
{
    static const char* n[12] = { "C", "D♭", "D", "E♭", "E", "F", "F#", "G", "A♭", "A", "B♭", "B" };
    return n[((k % 12) + 12) % 12];
}

inline int keySharps (int key, bool minor)
{
    static const int sfMajor[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    return sfMajor[(((minor ? key + 3 : key) % 12) + 12) % 12];
}

inline std::vector<Note> includedNotes (const Result& r, int kind)
{
    std::vector<Note> v;
    if (kind != kDrums) v.insert (v.end(), r.melody.begin(), r.melody.end());
    if (kind != kMelody) v.insert (v.end(), r.drums.begin(), r.drums.end());
    return v;
}

// 秒 → 拍（最初の音 = 0 拍）。量子化もここで
struct Placed { double b0, b1; int n, vel, ch; };
inline std::vector<Placed> place (const Result& r, const MidiOptions& o)
{
    auto v = includedNotes (r, o.kind);
    std::vector<Placed> out;
    if (v.empty()) return out;
    double t0 = 1e9;
    for (auto& n : v) t0 = std::min (t0, n.s);
    const double grid = o.quantize == 1 ? 0.25 : o.quantize == 2 ? 0.5 : 0.0;
    for (auto& n : v)
    {
        double b0 = (n.s - t0) * o.bpm / 60.0, b1 = (n.e - t0) * o.bpm / 60.0;
        if (grid > 0)
        {
            const double q = std::round (b0 / grid) * grid;
            const double len = std::max (grid * 0.5, b1 - b0);
            b0 = q; b1 = q + (n.ch == 9 ? len : std::max (grid, std::round (len / grid) * grid));
        }
        out.push_back ({ b0, b1, n.n, n.vel, n.ch });
    }
    return out;
}

inline std::vector<uint8_t> buildMidiFile (const Result& r, const MidiOptions& o)
{
    const int ppq = 480;
    std::vector<uint8_t> f;
    auto be32 = [&] (std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back ((uint8_t) (x >> (8 * i))); };
    auto vlq = [] (std::vector<uint8_t>& v, uint32_t x)
    {
        uint8_t b[5]; int n = 0;
        b[n++] = (uint8_t) (x & 0x7f);
        while ((x >>= 7) != 0) b[n++] = (uint8_t) ((x & 0x7f) | 0x80);
        while (n > 0) v.push_back (b[--n]);
    };
    struct Ev { uint32_t tick; int order; std::vector<uint8_t> bytes; };
    auto track = [&] (std::vector<Ev> ev)
    {
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
        std::vector<uint8_t> t;
        uint32_t last = 0;
        for (auto& e : ev) { vlq (t, e.tick - last); last = e.tick; t.insert (t.end(), e.bytes.begin(), e.bytes.end()); }
        t.insert (t.end(), { 0x00, 0xFF, 0x2F, 0x00 });
        f.insert (f.end(), { 'M', 'T', 'r', 'k' });
        be32 (f, (uint32_t) t.size());
        f.insert (f.end(), t.begin(), t.end());
    };
    auto meta = [] (int type, const std::string& s)
    {
        std::vector<uint8_t> b { 0xFF, (uint8_t) type, (uint8_t) s.size() };
        b.insert (b.end(), s.begin(), s.end());
        return b;
    };

    const auto notes = place (r, o);
    const bool hasMel = std::any_of (notes.begin(), notes.end(), [] (const Placed& p) { return p.ch != 9; });
    const bool hasDrm = std::any_of (notes.begin(), notes.end(), [] (const Placed& p) { return p.ch == 9; });
    const int nTracks = 1 + (hasMel ? 1 : 0) + (hasDrm ? 1 : 0);

    f.insert (f.end(), { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, (uint8_t) nTracks, (uint8_t) (ppq >> 8), (uint8_t) (ppq & 0xff) });

    // 0 番トラック: テンポ・拍子・キー
    const uint32_t us = (uint32_t) std::lround (60000000.0 / std::clamp (o.bpm, 20.0, 400.0));
    track ({ { 0, 0, meta (3, "Idea Catcher") },
             { 0, 1, { 0xFF, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us } },
             { 0, 2, { 0xFF, 0x58, 0x04, 4, 2, 24, 8 } },
             { 0, 3, { 0xFF, 0x59, 0x02, (uint8_t) (int8_t) keySharps (o.key, o.minor), (uint8_t) (o.minor ? 1 : 0) } } });

    auto noteTrack = [&] (bool drums)
    {
        std::vector<Ev> ev { { 0, 0, meta (3, drums ? "Drums" : "Melody") } };
        for (auto& p : notes)
        {
            if ((p.ch == 9) != drums) continue;
            const uint32_t a = (uint32_t) std::lround (std::max (0.0, p.b0) * ppq);
            const uint32_t b = std::max (a + 1, (uint32_t) std::lround (std::max (0.0, p.b1) * ppq));
            ev.push_back ({ a, 2, { (uint8_t) (0x90 | p.ch), (uint8_t) p.n, (uint8_t) std::clamp (p.vel, 1, 127) } });
            ev.push_back ({ b, 1, { (uint8_t) (0x80 | p.ch), (uint8_t) p.n, 0 } });   // 同じ時刻なら先に止める
        }
        track (std::move (ev));
    };
    if (hasMel) noteTrack (false);
    if (hasDrm) noteTrack (true);
    return f;
}

} // namespace idea
