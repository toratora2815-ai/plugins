#include "Engine.h"

Engine::Engine()
{
    prepare (48000.0, 512);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    mixMeter.prepare (sr);
    refMeter.prepare (sr);
    mixSpec.prepare (sr);
    refSpec.prepare (sr);

    abSmooth.reset (sr, 0.02);
    abSmooth.setCurrentAndTargetValue (ab.load() ? 1.0f : 0.0f);
    for (auto* s : { &wSt, &wMono, &wSide, &wFull, &wLow, &wMid, &wHigh })
        s->reset (sr, 0.02);
    const int st = stereo.load(), bd = band.load();
    wSt.setCurrentAndTargetValue   (st == 0 ? 1.0f : 0.0f);
    wMono.setCurrentAndTargetValue (st == 1 ? 1.0f : 0.0f);
    wSide.setCurrentAndTargetValue (st == 2 ? 1.0f : 0.0f);
    wFull.setCurrentAndTargetValue (bd == 0 ? 1.0f : 0.0f);
    wLow.setCurrentAndTargetValue  (bd == 1 ? 1.0f : 0.0f);
    wMid.setCurrentAndTargetValue  (bd == 2 ? 1.0f : 0.0f);
    wHigh.setCurrentAndTargetValue (bd == 3 ? 1.0f : 0.0f);

    mixGain.reset (sr, 0.3); refGain.reset (sr, 0.3); outGain.reset (sr, 0.05);
    mixGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (mixGainDb.load(), -200.0f));
    refGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (refGainDb.load(), -200.0f));
    outGain.setCurrentAndTargetValue (0.0001f);   // 起動直後は無音から 50ms でフェードイン

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 2 };
    xoLow.prepare (spec);  xoLow.setCutoffFrequency (200.0f);
    xoHigh.prepare (spec); xoHigh.setCutoffFrequency (3000.0f);
    zeroBuf.clear();
    sim.prepare (sr);
}

// 配信サービスは「曲全体の音量」で上げ下げを決める。
// ファイルは読み込んだときに曲全体を測ってある。Logic・ライブは、計り始めてからの平均（SECTION）を使う
bool Engine::platformBasis (bool ref, float& lufs, float& peakDb) const noexcept
{
    if (ref && refSource.load() == sourceFile)
    {
        lufs = player.fileLufs.load();
        peakDb = player.filePeakDb.load();
        return lufs > -100.0f;
    }
    const auto& m = ref ? refMeter : mixMeter;
    lufs = m.gatedSeconds.load() >= 3.0f ? m.integrated.load() : refdsp::comparisonLoudness (m);
    peakDb = m.peakMax.load();
    return lufs > -100.0f;
}

float Engine::platformGainDb (int platform, bool ref) const noexcept
{
    float lufs = 0.0f, peak = 0.0f;
    if (! platformBasis (ref, lufs, peak)) return -200.0f;
    const auto& p = refdsp::streamingPlatform (platform);
    return refdsp::streamingGainDb (lufs, peak, p.targetLufs, p.allowUp);
}

void Engine::process (const float* mixL, const float* mixR, const float* liveL, const float* liveR,
                      float* outL, float* outR, int n) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    for (int off = 0; off < n; off += kChunk)
    {
        const int len = juce::jmin (kChunk, n - off);
        processChunk (mixL != nullptr ? mixL + off : nullptr, mixR != nullptr ? mixR + off : nullptr,
                      liveL != nullptr ? liveL + off : nullptr, liveR != nullptr ? liveR + off : nullptr,
                      outL + off, outR + off, len);
    }
}

// 1.5秒以上の無音のあとに音が来たら「新しく再生が始まった」とみなして計測をやり直す
void Engine::silenceWatch (float peak, int n, int& silentSamples, bool& armed, bool isRef) noexcept
{
    if (peak < 1.0e-4f)
    {
        silentSamples += n;
        if (silentSamples > (int) (sr * 1.5)) armed = true;
    }
    else
    {
        silentSamples = 0;
        if (armed)
        {
            armed = false;
            if (isRef) { refMeter.requestReset(); refSpec.requestReset(); }
            else       { mixMeter.requestReset(); mixSpec.requestReset(); }
        }
    }
}

static float peakOf (const float* a, const float* b, int n) noexcept
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (a[i]), std::abs (b[i]));
    return p;
}

void Engine::processChunk (const float* mixL, const float* mixR, const float* liveL, const float* liveR,
                           float* outL, float* outR, int n) noexcept
{
    const float* zero = zeroBuf.getReadPointer (0);
    if (mixL == nullptr) mixL = zero;
    if (mixR == nullptr) mixR = mixL;
    if (liveL == nullptr) liveL = zero;
    if (liveR == nullptr) liveR = liveL;

    float* refL = refBuf.getWritePointer (0);
    float* refR = refBuf.getWritePointer (1);

    const int source = refSource.load();
    if (source != lastSource) { refMeter.requestReset(); refSpec.requestReset(); lastSource = source; refSilent = 0; refArmed = false; }

    if (source == sourceFile)
        player.process (refL, refR, n, sr);
    else
    {
        std::copy (liveL, liveL + n, refL);
        std::copy (liveR, liveR + n, refR);
    }

    // ---- 計測（どちらを聴いていても両方測る）----
    const float mp = peakOf (mixL, mixR, n), rp = peakOf (refL, refR, n);
    mixPeakDb = juce::Decibels::gainToDecibels (mp, -120.0f);
    livePeakDb = juce::Decibels::gainToDecibels (peakOf (liveL, liveR, n), -120.0f);
    silenceWatch (mp, n, mixSilent, mixArmed, false);
    if (source == sourceLive) silenceWatch (rp, n, refSilent, refArmed, true);

    mixMeter.process (mixL, mixR, n);
    refMeter.process (refL, refR, n);
    mixSpec.push (mixL, mixR, n);
    refSpec.push (refL, refR, n);

    // ---- 音量合わせ ----
    const int lv = level.load();
    const float mixLufs = refdsp::comparisonLoudness (mixMeter);
    const float refLufs = refdsp::comparisonLoudness (refMeter);
    float mDb = 0.0f, rDb = 0.0f;
    if (lv == levelMatch)
    {
        if (mixLufs > -100.0f && refLufs > -100.0f)
            heldMatchDb = juce::jlimit (-30.0f, 30.0f, mixLufs - refLufs);
        rDb = heldMatchDb;
    }
    else if (lv >= levelStream)
    {
        const float gm = platformGainDb (lv - levelStream, false), gr = platformGainDb (lv - levelStream, true);
        if (gm > -100.0f) heldMixStreamDb = gm;
        if (gr > -100.0f) heldRefStreamDb = gr;
        mDb = heldMixStreamDb;
        rDb = heldRefStreamDb;
    }
    rDb += trimDb.load();
    mixGainDb = mDb;
    refGainDb = rDb;
    mixGain.setTargetValue (juce::Decibels::decibelsToGain (mDb, -200.0f));
    refGain.setTargetValue (juce::Decibels::decibelsToGain (rDb, -200.0f));

    float og = mute.load() ? 0.0f : juce::Decibels::decibelsToGain (volumeDb.load() - (dim.load() ? 20.0f : 0.0f), -100.0f);
    outGain.setTargetValue (juce::jmax (0.00001f, og));

    abSmooth.setTargetValue (ab.load() ? 1.0f : 0.0f);
    const int st = stereo.load(), bd = band.load();
    wSt.setTargetValue   (st == 0 ? 1.0f : 0.0f);
    wMono.setTargetValue (st == 1 ? 1.0f : 0.0f);
    wSide.setTargetValue (st == 2 ? 1.0f : 0.0f);
    wFull.setTargetValue (bd == 0 ? 1.0f : 0.0f);
    wLow.setTargetValue  (bd == 1 ? 1.0f : 0.0f);
    wMid.setTargetValue  (bd == 2 ? 1.0f : 0.0f);
    wHigh.setTargetValue (bd == 3 ? 1.0f : 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float gm = mixGain.getNextValue(), gr = refGain.getNextValue();
        const float x = abSmooth.getNextValue();
        float l = mixL[i] * gm * (1.0f - x) + refL[i] * gr * x;
        float r = mixR[i] * gm * (1.0f - x) + refR[i] * gr * x;
        sim.process (l, r);

        const float s0 = wSt.getNextValue(), s1 = wMono.getNextValue(), s2 = wSide.getNextValue();
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
        const float nl = s0 * l + s1 * mid + s2 * side;
        const float nr = s0 * r + s1 * mid + s2 * side;

        const float fu = wFull.getNextValue(), lo = wLow.getNextValue(), mi = wMid.getNextValue(), hi = wHigh.getNextValue();
        float in[2] = { nl, nr };
        for (int ch = 0; ch < 2; ++ch)
        {
            float low = 0, rest = 0, m = 0, high = 0;
            xoLow.processSample (ch, in[ch], low, rest);
            xoHigh.processSample (ch, rest, m, high);
            in[ch] = fu * in[ch] + lo * low + mi * m + hi * high;
        }

        const float g = outGain.getNextValue();
        outL[i] = in[0] * g;
        outR[i] = in[1] * g;
    }
}
