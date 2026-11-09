#pragma once
// Drum Transient — 音の処理（ドラム専用のトランジェント・リミッター）
//
// 流れ（1 サンプルごと、左右は LINK ON なら同じ量）:
//   1. PUSH … 入口で押し込む（0〜20 dB）
//   2. BODY … 胴（鳴り終わりの響き）を上げ下げする。遅い包絡と速い包絡の差 ＝「今は鳴り終わりか」で決める
//   3. 胴のリミッター（先読み 5 ms）… アタックの分は PUNCH の割合だけ見なかったことにする（＝アタックで胴まで下げない）。
//      CLIP の割合 × 12 dB までは下げずに残して、次のクリッパーに任せる（アタックの所だけ。伸びる低音は 3 割だけ）。
//      戻りは 2 段: 速い戻り（RELEASE で 1〜20 ms）と、ゆっくり溜まる分（最大 5 dB、40〜400 ms で戻る）。
//      手本（Uni-L の初期設定）をドラムループで測ると「天井を越えた分はその場で下げ、抜けたあと 4〜6 dB 残して 0.15 秒ほどで戻す」だった
//   4. クリッパー（4 倍オーバーサンプリング）… 胴のリミッターが残したアタックの先を天井で切る。
//      切ったことで出た歪みのうち LOW KEEP より下は捨てる（キックの基音を濁らせない。残った低域は次のリミッターが下げる）
//   5. 仕上げのリミッター（先読み 1.5 ms）… クリッパーが残した分（LIMIT 側の割合と低域）を天井まで下げる
//   6. 最後の安全クリップ（天井ちょうど。普通は働かない）
//
// 遅れ = 5 ms + オーバーサンプリングの分 + 1.5 ms（48 kHz で 373 サンプル）
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace dt
{
    struct Params
    {
        float pushDb = 6.0f;      // 0..20
        float punch = 0.6f;       // 0..1  アタックを胴のリミッターに見せない割合
        float body = 0.0f;        // -1..1 胴を下げる/上げる
        float clip = 0.4f;        // 0..1  0 = LIMIT だけ、1 = CLIP だけ
        float release = 0.5f;     // 0..1  TIGHT .. SMOOTH
        float lowKeepHz = 90.0f;  // 0 = OFF
        float ceilingDb = -0.3f;
        bool link = true;
        bool delta = false;
    };

    // 先読みリミッター用: 窓の中の最大値（単調キュー）と、その箱型平均
    class LookaheadSmoother
    {
    public:
        void prepare (int window)
        {
            w = juce::jmax (1, window);
            qv.assign ((size_t) (w + 2), 0.0f); qi.assign ((size_t) (w + 2), 0);
            box.assign ((size_t) w, 0.0f);
            reset();
        }
        void reset()
        {
            head = tail = 0; idx = 0; sum = 0.0; bpos = 0;
            std::fill (box.begin(), box.end(), 0.0f);
        }
        // r = 下げたい量（dB、0 以上）。戻り値 = 先読みでなめらかにした量（窓の中のどの値よりも、その音が出る時点で下回らない）
        float push (float r)
        {
            const int cap = (int) qv.size();
            // 最大値の窓は w + 1（下の箱型平均の w 個すべてが、その音のサンプルを含むように）
            while (head != tail && qv[(size_t) ((tail - 1 + cap) % cap)] <= r) tail = (tail - 1 + cap) % cap;
            qv[(size_t) tail] = r; qi[(size_t) tail] = idx; tail = (tail + 1) % cap;
            while (qi[(size_t) head] <= idx - (w + 1)) head = (head + 1) % cap;
            const float m = qv[(size_t) head];
            ++idx;
            if (idx > (1 << 30)) rebase();
            sum += (double) m - (double) box[(size_t) bpos];
            box[(size_t) bpos] = m;
            bpos = (bpos + 1) % w;
            if (bpos == 0) { double s = 0; for (auto v : box) s += v; sum = s; }   // 誤差がたまらないように
            return (float) juce::jmax (0.0, sum / w);
        }
    private:
        void rebase()
        {
            const int cap = (int) qv.size();
            for (int i = head; i != tail; i = (i + 1) % cap) qi[(size_t) i] -= (1 << 29);
            idx -= (1 << 29);
        }
        int w = 1, head = 0, tail = 0, idx = 0, bpos = 0;
        double sum = 0.0;
        std::vector<float> qv, box;
        std::vector<int> qi;
    };

    struct Delay
    {
        std::vector<float> buf; int pos = 0, len = 0;
        void prepare (int n) { len = juce::jmax (0, n); buf.assign ((size_t) juce::jmax (1, n), 0.0f); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline float process (float x)
        {
            if (len == 0) return x;
            const float y = buf[(size_t) pos]; buf[(size_t) pos] = x; pos = (pos + 1) % len; return y;
        }
    };

    struct Biquad   // 2 次の HPF（転置直接形 II）
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setHighpass (double fs, double f, double q)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + al;
            b0 = (1.0 + c) / 2.0 / a0; b1 = -(1.0 + c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
        }
        void reset() { z1 = z2 = 0; }
        inline float process (float x)
        {
            const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y;
        }
    };

    class Engine
    {
    public:
        static constexpr int maxCh = 2;
        // 合わせ込んだ値（手本を 2 つのドラムループ × +6/+12/+18 dB で測り、聞こえる音量の差 1 dB 以内）
        static constexpr float slowMaxDb = 5.0f;      // ゆっくり戻る分の上限
        static constexpr float chargeMs = 60.0f;      // ゆっくり戻る分が溜まる速さ
        static constexpr float excessMul = 1.0f;      // アタックの割引の強さ
        static constexpr float fastMinMs = 1.0f, fastMaxMs = 20.0f;    // 速い戻り（RELEASE 0..100）
        static constexpr float slowMinMs = 40.0f, slowMaxMs = 400.0f;  // ゆっくりの戻り（RELEASE 0..100）
        static constexpr float clipRangeDb = 12.0f;
        static constexpr float transDb = 6.0f;       // アタックがこれだけ（dB）立っていたら、クリッパーに任せる分を全部使う
        static constexpr float trFloor = 0.3f;       // アタックでない所（伸びる低音など）も、これだけはクリッパーに任せる
        static constexpr float kTop = 1.0f, kPunch = 0.1f;   // クリップの肩: 天井から PUNCH 100% で 0.9 倍（-0.9 dB）まで
        static constexpr double detHz = 5000.0;      // 検出の LPF（ハイハットに反応しすぎない）   // CLIP 100% でクリッパーに任せる dB

        void prepare (double sampleRate, int maxBlock, int numCh, const Params& initial)
        {
            cur = initial;
            fs = sampleRate; nCh = juce::jlimit (1, maxCh, numCh);
            blockMax = juce::jmax (16, maxBlock);
            laSlow = juce::jmax (1, (int) std::round (0.005 * fs));
            laFast = juce::jmax (1, (int) std::round (0.0015 * fs));

            os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) maxCh, 2,
                     juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            os->initProcessing ((size_t) blockMax);
            osLat = (int) std::round (os->getLatencyInSamples());

            work.setSize (maxCh, blockMax, false, true, true);
            for (int k = 0; k < numSlots; ++k) { slowLim[k].prepare (laSlow); fastLim[k].prepare (laFast); }
            for (int c = 0; c < maxCh; ++c)
            {
                dSlow[c].prepare (laSlow); dFast[c].prepare (laFast);
                dDry[c].prepare (latency());
            }
            dGain.prepare (latency());
            ceilBuf.assign ((size_t) blockMax, 0.0f); ceilLin.assign ((size_t) blockMax, 1.0f); gainWork.assign ((size_t) blockMax, 1.0f);
            dryWork.setSize (maxCh, blockMax, false, true, true);
            lastLowKeep = -1.0f;
            setLowKeep (initial.lowKeepHz > 0.0f ? initial.lowKeepHz : 90.0f);
            reset();
        }

        void reset()
        {
            for (int k = 0; k < numSlots; ++k)
            {
                slowLim[k].reset(); fastLim[k].reset();
                tFast[k] = tSlow[k] = sFast[k] = sSlow[k] = 1e-6f;
                rs[k] = rf[k] = rFast[k] = 0.0f; bodyDb[k] = 0.0f;
            }
            for (int c = 0; c < maxCh; ++c)
            {
                dSlow[c].reset(); dFast[c].reset(); dDry[c].reset(); lpz[c] = 0.0f;
                for (auto& h : hp[c]) h.reset();
            }
            dGain.reset();
            if (os) os->reset();
            pushLin.reset (fs, 0.03); pushLin.setCurrentAndTargetValue (std::pow (10.0f, cur.pushDb / 20.0f));
            clipAmt = cur.clip; ceilDb = cur.ceilingDb;
            linkMix = cur.link ? 1.0f : 0.0f; deltaMix = cur.delta ? 1.0f : 0.0f; hpMix = cur.lowKeepHz > 0.0f ? 1.0f : 0.0f;
        }

        int latency() const { return laSlow + osLat + laFast; }

        // メーター（画面用）。ブロック内の最大の下げ量 dB
        std::atomic<float> meterBody { 0.0f }, meterAttack { 0.0f }, meterOut { -100.0f };

        void process (float* const* ch, int numCh, int n, const Params& p)
        {
            numCh = juce::jlimit (1, maxCh, juce::jmin (numCh, nCh));
            float mBody = 0, mAtk = 0, mOut = 0;
            for (int off = 0; off < n; off += blockMax)
            {
                const int len = juce::jmin (blockMax, n - off);
                float* sub[maxCh] {};
                for (int c = 0; c < numCh; ++c) sub[c] = ch[c] + off;
                processChunk (sub, numCh, len, p, mBody, mAtk, mOut);
            }
            meterBody.store (mBody); meterAttack.store (mAtk);
            meterOut.store (mOut > 1e-9f ? 20.0f * std::log10 (mOut) : -100.0f);
        }

    private:
        static inline float toDb (float x) { return 20.0f * std::log10 (juce::jmax (x, 1e-9f)); }
        static inline float coef (double fs, double ms) { return (float) std::exp (-1.0 / (fs * ms * 0.001)); }

        void setLowKeep (float hz)
        {
            if (hz <= 0.0f || hz == lastLowKeep) return;   // OFF の間は前の周波数のまま通し続ける（hpMix で混ぜ替える）
            lastLowKeep = hz;
            const double fos = fs * 4.0;
            for (int c = 0; c < maxCh; ++c)
            {
                hp[c][0].setHighpass (fos, hz, 0.5411961);   // 4 次バターワース（2 段）
                hp[c][1].setHighpass (fos, hz, 1.3065630);
            }
        }

        // 下げ量の置き場（slot）: 0 = 左（モノラルはこれだけ）、1 = 右、2 = LINK（左右の大きい方）。
        // LINK の ON/OFF で飛ばないよう、3 つとも常に計算して、かける量を 20 ms で混ぜ替える
        static constexpr int numSlots = maxCh + 1, linkSlot = maxCh;

        void processChunk (float** ch, int numCh, int n, const Params& p, float& mBody, float& mAtk, float& mOut)
        {
            cur = p;
            pushLin.setTargetValue (std::pow (10.0f, juce::jlimit (0.0f, 24.0f, p.pushDb) / 20.0f));
            setLowKeep (p.lowKeepHz);
            const bool stereo = numCh > 1;
            const int slots[3] = { 0, 1, linkSlot };
            const int nS = stereo ? 3 : 1;

            // 包絡の係数
            const float aT = coef (fs, 0.1), rT = coef (fs, 8.0);       // アタック検出（速い）
            const float aTS = coef (fs, 12.0), rTS = coef (fs, 120.0);  // アタック検出（遅い）＝ アタックの長さ ≈ 12 ms
            const float aS = coef (fs, 1.0), rSF = coef (fs, 25.0), rSS = coef (fs, 250.0);   // 胴の検出
            const float lpc = 1.0f - (float) std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jmin (detHz, 0.45 * fs) / fs);   // ハイハットに反応しすぎないよう 5 kHz の LPF で検出
            const float bodySm = coef (fs, 5.0);
            const float rel = juce::jlimit (0.0f, 1.0f, p.release);
            const float relFast = coef (fs, fastMinMs * std::pow (fastMaxMs / fastMinMs, rel));
            const float relSlow = coef (fs, slowMinMs * std::pow (slowMaxMs / slowMinMs, rel));
            const float charge = coef (fs, chargeMs);
            const float relLim2 = coef (fs, 4.0 + 16.0 * rel);                   // 仕上げ 4..20 ms
            const float smooth20 = coef (fs, 20.0), smooth15 = coef (fs, 15.0);
            const float punch = juce::jlimit (0.0f, 1.0f, p.punch);
            const float body = juce::jlimit (-1.0f, 1.0f, p.body);
            const float kneeDb = 20.0f * std::log10 (kTop - kPunch * punch);   // 0 〜 -0.9 dB
            const float linkTarget = p.link ? 1.0f : 0.0f;

            // ---- 1〜3: PUSH・BODY・胴のリミッター（基本のサンプルレート）
            for (int i = 0; i < n; ++i)
            {
                const float g = pushLin.getNextValue();
                float in[maxCh] {}, x[maxCh] {};
                for (int c = 0; c < numCh; ++c)
                {
                    in[c] = std::isfinite (ch[c][i]) ? ch[c][i] : 0.0f;   // NaN・無限大は 0 に（前のプラグインが壊れていても道連れにならない）
                    x[c] = in[c] * g;
                    dryBuf[c] = dDry[c].process (in[c]);
                }
                gainBuf = dGain.process (g);
                linkMix = smooth20 * linkMix + (1 - smooth20) * linkTarget;
                ceilDb = smooth20 * ceilDb + (1 - smooth20) * p.ceilingDb;
                ceilBuf[(size_t) i] = ceilDb; ceilLin[(size_t) i] = std::pow (10.0f, ceilDb / 20.0f);

                // 検出
                float d[numSlots] {};
                for (int c = 0; c < numCh; ++c) { lpz[c] += lpc * (x[c] - lpz[c]); d[c] = std::abs (lpz[c]); }
                if (stereo) d[linkSlot] = juce::jmax (d[0], d[1]);

                float excess[numSlots] {};
                for (int j = 0; j < nS; ++j)
                {
                    const int k = slots[j]; const float v = d[k];
                    tFast[k] = v > tFast[k] ? aT * tFast[k] + (1 - aT) * v : rT * tFast[k] + (1 - rT) * v;
                    tSlow[k] = v > tSlow[k] ? aTS * tSlow[k] + (1 - aTS) * v : rTS * tSlow[k] + (1 - rTS) * v;
                    excess[k] = juce::jlimit (0.0f, 24.0f, toDb (tFast[k] + 1e-7f) - toDb (tSlow[k] + 1e-7f));
                    sFast[k] = v > sFast[k] ? aS * sFast[k] + (1 - aS) * v : rSF * sFast[k] + (1 - rSF) * v;
                    sSlow[k] = v > sSlow[k] ? aS * sSlow[k] + (1 - aS) * v : rSS * sSlow[k] + (1 - rSS) * v;
                    // 鳴り終わりほど 遅い包絡 > 速い包絡。その差（dB）に BODY を掛ける。入口が小さい所（-60 dB 未満）では弱める
                    float sus = juce::jlimit (0.0f, 24.0f, toDb (sSlow[k] + 1e-7f) - toDb (sFast[k] + 1e-7f));
                    if (sSlow[k] < 0.001f) sus *= sSlow[k] / 0.001f;
                    const float target = juce::jlimit (-12.0f, 12.0f, body * sus);
                    bodyDb[k] = bodySm * bodyDb[k] + (1 - bodySm) * target;
                }
                // BODY をかけた後のピーク（各 slot ごと）
                float peak[numSlots] {};
                float bodyApplied[maxCh] {};
                for (int c = 0; c < numCh; ++c)
                    bodyApplied[c] = stereo ? linkMix * bodyDb[linkSlot] + (1 - linkMix) * bodyDb[c] : bodyDb[0];
                for (int c = 0; c < numCh; ++c)
                {
                    if (bodyApplied[c] != 0.0f) x[c] *= std::pow (10.0f, bodyApplied[c] / 20.0f);
                    peak[c] = std::abs (x[c]);
                }
                if (stereo) peak[linkSlot] = juce::jmax (peak[0], peak[1]);

                float tot[numSlots] {};
                for (int j = 0; j < nS; ++j)
                {
                    const int k = slots[j];
                    // アタックの分（excess）を PUNCH の割合だけ割り引いて見る
                    // 胴のリミッターは、クリップの肩より下に収める（天井近くに張り付いた伸びる音を、肩でずっと削らない）
                    const float lvl = toDb (peak[k]) - punch * excessMul * excess[k] - kneeDb;
                    // クリッパーに任せる分は、アタックのところだけ（伸びる低音はクリップさせず、リミッターで下げる）
                    const float tr = juce::jmax (trFloor, juce::jmin (1.0f, excess[k] / transDb));
                    const float need = slowLim[k].push (juce::jmax (0.0f, lvl - ceilDb - clipAmt * clipRangeDb * tr));
                    const float sTarget = juce::jmin (need, slowMaxDb);
                    rs[k] = sTarget > rs[k] ? charge * rs[k] + (1 - charge) * sTarget
                                            : relSlow * rs[k] + (1 - relSlow) * sTarget;
                    // 速い分: 下げ量が増えたらすぐ、減るときは RELEASE の速さで
                    rf[k] = juce::jmax (need - rs[k], relFast * rf[k]);
                    tot[k] = rs[k] + rf[k];
                }
                for (int c = 0; c < numCh; ++c)
                {
                    const float t = stereo ? linkMix * tot[linkSlot] + (1 - linkMix) * tot[c] : tot[0];
                    const float y = dSlow[c].process (x[c]) * std::pow (10.0f, -t / 20.0f);
                    work.setSample (c, i, y);
                    dryWork.setSample (c, i, dryBuf[c]);
                    mBody = juce::jmax (mBody, t);
                    mAtk = juce::jmax (mAtk, std::abs (y) > 0 ? toDb (std::abs (y)) - ceilDb : 0.0f);
                }
                gainWork[(size_t) i] = gainBuf;
            }

            // ---- 4: クリッパー（4 倍）
            {
                juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
                auto up = os->processSamplesUp (blk);
                const int un = (int) up.getNumSamples();
                const float kn = juce::jlimit (0.3f, 1.0f, kTop - kPunch * punch);   // PUNCH を上げるほど、やわらかく切る（トゲトゲにしない）
                const float clipC = coef (fs * 4.0, 20.0), hpC = coef (fs * 4.0, 15.0);
                const float hpTarget = p.lowKeepHz > 0.0f ? 1.0f : 0.0f;
                float ca = clipAmt, hm = hpMix;
                for (int c = 0; c < numCh; ++c)
                {
                    float* u = up.getChannelPointer ((size_t) c);
                    ca = clipAmt; hm = hpMix;
                    for (int i = 0; i < un; ++i)
                    {
                        ca = clipC * ca + (1 - clipC) * p.clip;
                        hm = hpC * hm + (1 - hpC) * hpTarget;
                        const float T = ceilLin[(size_t) juce::jmin (n - 1, i >> 2)];
                        const float kT = kn * T, span = juce::jmax ((1.0f - kn) * T, 1e-6f);
                        const float v = u[i], a = std::abs (v);
                        float e = 0.0f;
                        if (a > kT)
                        {
                            const float s = kT + span * std::tanh ((a - kT) / span);
                            e = std::copysign (s, v) - v;
                        }
                        // LOW KEEP: HPF は OFF の間も通し続け、HPF あり／なしの誤差を 15 ms で混ぜ替える（切り替えでプチッとしない）
                        const float eh = hp[c][1].process (hp[c][0].process (e));
                        u[i] = v + juce::jmin (1.0f, ca * 4.0f) * (hm * eh + (1 - hm) * e);
                    }
                }
                clipAmt = ca; hpMix = hm;
                os->processSamplesDown (blk);
            }

            // ---- 5〜6: 仕上げのリミッターと安全クリップ、DELTA
            const float deltaTarget = p.delta ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float cdb = ceilBuf[(size_t) i], T = ceilLin[(size_t) i];
                float y[maxCh] {}, pv[numSlots] {};
                for (int c = 0; c < numCh; ++c) { y[c] = work.getSample (c, i); pv[c] = std::abs (y[c]); }
                if (stereo) pv[linkSlot] = juce::jmax (pv[0], pv[1]);
                for (int j = 0; j < nS; ++j)
                {
                    const int k = slots[j];
                    const float need = fastLim[k].push (juce::jmax (0.0f, toDb (pv[k]) - cdb));
                    rFast[k] = juce::jmax (need, relLim2 * rFast[k]);
                }
                deltaMix = smooth15 * deltaMix + (1 - smooth15) * deltaTarget;
                const float gD = juce::jmax (gainWork[(size_t) i], 1e-6f);
                for (int c = 0; c < numCh; ++c)
                {
                    const float r = stereo ? linkMix * rFast[linkSlot] + (1 - linkMix) * rFast[c] : rFast[0];
                    float o = dFast[c].process (y[c]) * std::pow (10.0f, -r / 20.0f);
                    o = juce::jlimit (-T, T, o);
                    mOut = juce::jmax (mOut, std::abs (o));
                    // DELTA: 削った分を、PUSH の分を戻した大きさで（入口と同じくらいの音量）。天井で切る
                    const float dl = juce::jlimit (-T, T, dryWork.getSample (c, i) - o / gD);
                    ch[c][i] = deltaMix * dl + (1 - deltaMix) * o;
                }
            }

            // 万一おかしな値が出たら、状態を全部捨ててやり直す
            for (int c = 0; c < numCh; ++c)
                if (! std::isfinite (ch[c][n - 1]) || ! std::isfinite (rs[0] + rf[0] + rFast[0] + rs[linkSlot] + rf[linkSlot]))
                {
                    for (int cc = 0; cc < numCh; ++cc) juce::FloatVectorOperations::clear (ch[cc], n);
                    reset();
                    break;
                }
        }

        double fs = 48000.0;
        int nCh = 2, blockMax = 512, laSlow = 240, laFast = 72, osLat = 0;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        juce::AudioBuffer<float> work;
        juce::AudioBuffer<float> dryWork;
        LookaheadSmoother slowLim[numSlots], fastLim[numSlots];
        Delay dSlow[maxCh], dFast[maxCh], dDry[maxCh], dGain;
        Biquad hp[maxCh][2];
        std::vector<float> ceilBuf, ceilLin, gainWork;
        float dryBuf[maxCh] {}, gainBuf = 1.0f;
        float tFast[numSlots] {}, tSlow[numSlots] {}, sFast[numSlots] {}, sSlow[numSlots] {}, lpz[maxCh] {};
        float rs[numSlots] {}, rf[numSlots] {}, rFast[numSlots] {}, bodyDb[numSlots] {};
        float clipAmt = 0.4f, ceilDb = -0.3f, lastLowKeep = -1.0f, linkMix = 1.0f, deltaMix = 0.0f, hpMix = 1.0f;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> pushLin { 1.0f };
        Params cur;
    };
}
