#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <random>

// テイクの試し聴き。Logic を開かずに、アプリの中の簡単な音で鳴らす。
// マイク用とは別に、Mac の標準の出力を使う（鳴らすときだけ開く）。
class Preview : private juce::AudioIODeviceCallback
{
public:
    struct Note { double s, e; int n, vel; };

    ~Preview() override { stop(); out.closeAudioDevice(); }

    void play (std::vector<Note> notes, bool drums, juce::String id)
    {
        stop();
        if (notes.empty()) return;
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.s < b.s; });
        const double t0 = notes.front().s;
        for (auto& n : notes) { n.s -= t0; n.e -= t0; }
        seq = std::move (notes);
        isDrums = drums;
        playingId = id;
        if (out.getCurrentAudioDevice() == nullptr)
            out.initialise (0, 2, nullptr, true);
        pos = 0; next = 0; voices.clear();
        playing = true;
        out.addAudioCallback (this);
    }

    void stop()
    {
        if (! playing.load() && playingId.isEmpty()) return;
        out.removeAudioCallback (this);
        playing = false;
        playingId.clear();
    }

    bool isPlaying() const { return playing.load(); }
    const juce::String& currentId() const { return playingId; }
    double progress() const { return seq.empty() ? 0 : juce::jlimit (0.0, 1.0, (double) pos.load() / std::max (1.0, length() * sr)); }

private:
    juce::AudioDeviceManager out;
    std::vector<Note> seq;
    bool isDrums = false;
    juce::String playingId;
    std::atomic<bool> playing { false };
    std::atomic<int64_t> pos { 0 };
    size_t next = 0;
    double sr = 48000;
    std::minstd_rand rng { 1 };

    struct Voice { int n; double vel; int64_t start, end; double ph = 0, ph2 = 0, lp = 0, hp = 0, hpPrev = 0; };
    std::vector<Voice> voices;

    double length() const { double l = 0; for (auto& n : seq) l = std::max (l, n.e); return l + 0.4; }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { sr = d->getCurrentSampleRate(); voices.reserve (64); }
    void audioDeviceStopped() override {}

    float noise() { return (float) ((double) rng() / (double) rng.max() * 2.0 - 1.0); }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outs, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) juce::FloatVectorOperations::clear (outs[c], n);
        if (! playing.load()) return;
        const int64_t endAt = (int64_t) (length() * sr);
        int64_t p = pos.load();
        for (int i = 0; i < n; ++i, ++p)
        {
            while (next < seq.size() && (int64_t) (seq[next].s * sr) <= p)
            {
                const auto& s = seq[next++];
                if (voices.size() < 48)
                    voices.push_back ({ s.n, s.vel / 127.0, p, (int64_t) (s.e * sr) });
            }
            double mix = 0;
            for (auto& v : voices) mix += isDrums ? drum (v, p) : tone (v, p);
            voices.erase (std::remove_if (voices.begin(), voices.end(), [&] (const Voice& v)
            {
                const double age = (double) (p - v.start) / sr;
                return isDrums ? age > 0.6 : p > v.end + (int64_t) (0.15 * sr);
            }), voices.end());
            const float s = (float) std::tanh (mix * 0.6);
            for (int c = 0; c < numOut; ++c) if (outs[c] != nullptr) outs[c][i] = s;
        }
        pos = p;
        if (p > endAt) { playing = false; }
    }

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
