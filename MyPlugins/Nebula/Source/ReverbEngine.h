#pragma once
// JUCEに依存しないリバーブ本体（FDN方式）
#include <vector>
#include <cmath>
#include <algorithm>

namespace nebula
{
constexpr float kPi = 3.14159265358979f;

struct DelayLine
{
    std::vector<float> buf;
    int w = 0, mask = 0;

    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    float read (float d) const   // d >= 1、線形補間
    {
        float rp = (float) w - d;
        if (rp < 0.0f) rp += (float) buf.size();
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * f;
    }
};

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, float fs) { a = std::exp (-2.0f * kPi * fc / fs); }
    float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

struct Allpass
{
    DelayLine dl; int len = 1; float g = 0.65f;
    void prepare (float ms, float fs) { len = std::max (1, (int) (ms * 0.001f * fs)); dl.prepare (len + 2); }
    float process (float x)
    {
        const float d = dl.readInt (len);
        const float v = x - g * d;
        dl.push (v);
        return d + g * v;
    }
};

// 2ヘッドのクロスフェード式ピッチシフター（+1オクターブ）
struct PitchShifter
{
    DelayLine dl; float W = 1.0f, phase = 0.0f;
    void prepare (float fs) { W = 0.07f * fs; dl.prepare ((int) W + 8); phase = 0.0f; }
    float process (float x, float ratio)
    {
        dl.push (x);
        phase += (ratio - 1.0f) / W;
        if (phase >= 1.0f) phase -= 1.0f;
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float s1 = std::sin (kPi * phase), s2 = std::sin (kPi * p2);
        return dl.read (1.0f + (1.0f - phase) * W) * s1 * s1
             + dl.read (1.0f + (1.0f - p2) * W) * s2 * s2;
    }
};

class CinematicReverb
{
public:
    static constexpr int N = 8;

    // パラメーター（ブロックごとに更新）
    float decaySec = 8.0f, size = 1.5f, preDelayMs = 40.0f, dampHz = 6000.0f;
    float modAmount = 0.4f, shimmer = 0.0f, width = 1.0f, lowCutHz = 120.0f;
    bool freeze = false;

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < N; ++i)
        {
            lines[i].prepare ((int) (0.26f * fs));
            lfoPhase[i] = (float) i / N;
            lfoInc[i] = (0.07f + 0.11f * (float) i) / fs;
            damp[i].reset();
        }
        const float apL[4] = { 4.3f, 7.9f, 11.3f, 15.7f }, apR[4] = { 4.7f, 8.3f, 12.1f, 16.9f };
        for (int k = 0; k < 4; ++k) { diffL[k].prepare (apL[k], fs); diffR[k].prepare (apR[k], fs); }
        preL.prepare ((int) (0.3f * fs)); preR.prepare ((int) (0.3f * fs));
        shifter.prepare (fs);
        shimHP.reset(); shimHP.setCutoff (350.0f, fs);
        shimLP.reset(); shimLP.setCutoff (9000.0f, fs);
        lowL.reset(); lowR.reset();
        sizeSmoothed = size;
        sizeCoef = 1.0f - std::exp (-1.0f / (0.4f * fs));
        lastWet = 0.0f;
        update();
    }

    // パラメーター変更後に呼ぶ
    void update()
    {
        for (int i = 0; i < N; ++i)
        {
            const float lenSec = baseMs[i] * size * 0.001f;
            gain[i] = freeze ? 1.0f : std::pow (10.0f, -3.0f * lenSec / std::max (0.1f, decaySec));
            damp[i].setCutoff (dampHz, fs);
        }
        lowL.setCutoff (lowCutHz, fs); lowR.setCutoff (lowCutHz, fs);
        preSamples = std::max (1, (int) (preDelayMs * 0.001f * fs));
        modDepth = modAmount * 0.0025f * fs;   // 最大 ±2.5ms
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        preL.push (inL); preR.push (inR);
        float xL = preL.readInt (preSamples), xR = preR.readInt (preSamples);
        for (int k = 0; k < 4; ++k) { xL = diffL[k].process (xL); xR = diffR[k].process (xR); }

        float shim = shimLP.lp (shimHP.hp (shifter.process (lastWet, 2.0f)));
        shim = std::tanh (shim * shimmer * 1.2f);

        if (freeze) { xL = xR = 0.0f; shim = 0.0f; }

        sizeSmoothed += (size - sizeSmoothed) * sizeCoef;

        float o[N], sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            lfoPhase[i] += lfoInc[i];
            if (lfoPhase[i] >= 1.0f) lfoPhase[i] -= 1.0f;
            const float m = std::sin (2.0f * kPi * lfoPhase[i]) * modDepth;
            const float d = baseMs[i] * 0.001f * fs * sizeSmoothed + modDepth + m + 2.0f;
            o[i] = lines[i].read (d);
            if (! freeze) o[i] = damp[i].lp (o[i]);
            sum += o[i];
        }

        const float h = sum * (2.0f / N);   // ハウスホルダー行列（エネルギー保存）
        for (int i = 0; i < N; ++i)
        {
            const float in = ((i & 1) ? xR : xL) * ((i & 2) ? -0.5f : 0.5f) + shim * 0.25f;
            lines[i].push ((o[i] - h) * gain[i] + in);
        }

        float wL = 0.0f, wR = 0.0f;
        for (int i = 0; i < N; ++i) { wL += o[i] * signL[i]; wR += o[i] * signR[i]; }
        wL *= 0.45f; wR *= 0.45f;
        lastWet = 0.5f * (wL + wR);

        wL = lowL.hp (wL); wR = lowR.hp (wR);
        const float mid = 0.5f * (wL + wR), side = 0.5f * (wL - wR) * width;
        outL = mid + side; outR = mid - side;
    }

private:
    float fs = 48000.0f;
    const float baseMs[N] = { 31.7f, 37.3f, 43.1f, 49.9f, 57.7f, 64.3f, 71.9f, 79.3f };
    const float signL[N]  = { 1, 1, -1, -1, 1, 1, -1, -1 };
    const float signR[N]  = { 1, -1, 1, -1, 1, -1, 1, -1 };
    DelayLine lines[N], preL, preR;
    OnePole damp[N], lowL, lowR, shimHP, shimLP;
    Allpass diffL[4], diffR[4];
    PitchShifter shifter;
    float gain[N] {}, lfoPhase[N] {}, lfoInc[N] {};
    float sizeSmoothed = 1.5f, sizeCoef = 0.001f, modDepth = 0.0f, lastWet = 0.0f;
    int preSamples = 1;
};
} // namespace nebula
