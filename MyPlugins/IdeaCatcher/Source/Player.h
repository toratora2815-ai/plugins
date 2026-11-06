#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <random>

// 手直しの画面の試し聴き：MIDI（簡単な音源）と、元の声を、同じ時間で鳴らす（声と MIDI の聴き比べ）。
// 録音用とは別に、Mac の標準の出力を鳴らすときだけ開く。
// 簡単な音源（やわらかい三角波）。Hum Keys から持ってきたもの
struct ToySynth
{
    struct Voice { int n; double vel; int64_t start, end; bool drum; double ph = 0, ph2 = 0, lp = 0, hp = 0, hpPrev = 0; };
    std::vector<Voice> voices;
    double sr = 48000;
    int64_t pos = 0;
    std::minstd_rand rng { 1 };

    void prepare (double rate) { sr = rate; voices.reserve (64); }
    void clear() { voices.clear(); }
    void start (int n, int vel, bool drum, double lengthSec)
    {
        if (voices.size() >= 48) voices.erase (voices.begin());
        voices.push_back ({ n, vel / 127.0, pos, pos + (int64_t) (lengthSec * sr), drum });
    }
    float next()
    {
        double mix = 0;
        for (auto& v : voices) mix += v.drum ? drum (v, pos) : tone (v, pos);
        if (! voices.empty() && (pos & 63) == 0)
            voices.erase (std::remove_if (voices.begin(), voices.end(), [&] (const Voice& v)
            {
                const double age = (double) (pos - v.start) / sr;
                return v.drum ? age > 0.6 : pos > v.end + (int64_t) (0.15 * sr);
            }), voices.end());
        ++pos;
        return (float) std::tanh (mix * 0.6);
    }

private:
    float noise() { return (float) ((double) rng() / (double) rng.max() * 2.0 - 1.0); }

    // メロディ: やわらかい三角波＋少し倍音
    double tone (Voice& v, int64_t p)
    {
        const double f = 440.0 * std::pow (2.0, (v.n - 69) / 12.0);
        v.ph += f / sr; if (v.ph >= 1) v.ph -= 1;
        const double tri = 4.0 * std::abs (v.ph - 0.5) - 1.0;
        v.ph2 += 2 * f / sr; if (v.ph2 >= 1) v.ph2 -= 1;
        const double t = (double) (p - v.start) / sr;
        double env = std::min (1.0, t / 0.006) * (0.65 + 0.35 * std::exp (-t / 0.25));
        if (p > v.end) env *= std::exp (-(double) (p - v.end) / (sr * 0.05));
        return (tri + 0.2 * std::sin (2 * M_PI * v.ph2)) * env * (0.25 + 0.35 * v.vel);
    }

    // ドラム: ノート番号で鳴らし分け（General MIDI の並び）
    double drum (Voice& v, int64_t p)
    {
        const double t = (double) (p - v.start) / sr;
        const double a = 0.3 + 0.7 * v.vel;
        auto hpNoise = [&] (double cut)
        {
            const double x = noise();
            const double k = std::exp (-2 * M_PI * cut / sr);
            v.hp = k * (v.hp + x - v.hpPrev); v.hpPrev = x;
            return v.hp;
        };
        switch (v.n)
        {
            case 35: case 36:   // キック
            {
                const double f = 48 + 120 * std::exp (-t / 0.03);
                v.ph += f / sr;
                return std::sin (2 * M_PI * v.ph) * std::exp (-t / 0.18) * a * 1.4;
            }
            case 41: case 43: case 45: case 47: case 48: case 50:   // タム
            {
                const double f = (80 + 6 * (v.n - 41)) * 1.6 + 60 * std::exp (-t / 0.05);
                v.ph += f / sr;
                return std::sin (2 * M_PI * v.ph) * std::exp (-t / 0.22) * a;
            }
            case 42: case 44:   return hpNoise (7000) * std::exp (-t / 0.03) * a * 0.9;   // 閉じたハイハット
            case 46:            return hpNoise (6500) * std::exp (-t / 0.25) * a * 0.7;   // 開いたハイハット
            case 39:            // クラップ（3 回の短い破裂）
            {
                double env = 0;
                for (int k = 0; k < 3; ++k) { const double tt = t - k * 0.01; if (tt >= 0) env += std::exp (-tt / (k == 2 ? 0.08 : 0.006)); }
                v.lp += 0.35 * (noise() - v.lp);
                return (noise() - v.lp) * env * a * 0.7;
            }
            default:            // スネアほか
            {
                v.ph += 190.0 / sr;
                return (0.5 * std::sin (2 * M_PI * v.ph) * std::exp (-t / 0.05) + hpNoise (1500) * std::exp (-t / 0.12)) * a;
            }
        }
    }
};

class Player : private juce::AudioIODeviceCallback
{
public:
    struct Note { double s, e; int n, vel; };   // 秒
    ~Player() override { stop(); out.closeAudioDevice(); }

    // voice = 声（fs で、notes と同じ 0 秒から）。withMidi / withVoice で鳴らすものを選ぶ
    void play (std::vector<Note> notes, std::vector<float> voice, double voiceFs, bool withMidi, bool withVoice)
    {
        stop();
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.s < b.s; });
        seq = withMidi ? std::move (notes) : std::vector<Note>();
        vox = withVoice ? std::move (voice) : std::vector<float>();
        vfs = voiceFs;
        if (seq.empty() && vox.empty()) return;
        if (out.getCurrentAudioDevice() == nullptr) out.initialise (0, 2, nullptr, true);
        pos = 0; next = 0; synth.clear();
        playing = true;
        out.addAudioCallback (this);
    }
    void stop()
    {
        if (! playing.load() && ! attached) return;
        out.removeAudioCallback (this);
        attached = false;
        playing = false;
    }
    bool isPlaying() const { return playing.load(); }
    double seconds() const { return (double) pos.load() / std::max (1.0, synth.sr); }

private:
    juce::AudioDeviceManager out;
    std::vector<Note> seq;
    std::vector<float> vox;
    double vfs = 48000;
    std::atomic<bool> playing { false };
    bool attached = false;
    std::atomic<int64_t> pos { 0 };
    size_t next = 0;
    ToySynth synth;

    double length() const
    {
        double l = vox.empty() ? 0.0 : (double) vox.size() / vfs;
        for (auto& n : seq) l = std::max (l, n.e);
        return l + 0.3;
    }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { synth.prepare (d->getCurrentSampleRate()); attached = true; }
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outs, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) juce::FloatVectorOperations::clear (outs[c], n);
        if (! playing.load()) return;
        const double sr = synth.sr;
        const int64_t endAt = (int64_t) (length() * sr);
        int64_t p = pos.load();
        const double ratio = vfs / sr;
        for (int i = 0; i < n; ++i, ++p)
        {
            while (next < seq.size() && (int64_t) (seq[next].s * sr) <= p)
            {
                const auto& s = seq[next++];
                synth.start (s.n, s.vel, false, s.e - s.s);
            }
            float v = seq.empty() ? 0.0f : synth.next() * 0.8f;
            if (! vox.empty())
            {
                const double x = (double) p * ratio;
                const auto k = (size_t) x;
                if (k + 1 < vox.size()) { const float f = (float) (x - (double) k); v += (vox[k] * (1 - f) + vox[k + 1] * f) * 0.8f; }
            }
            for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) outs[c][i] = v;
        }
        pos = p;
        if (p > endAt) playing = false;
    }
};
