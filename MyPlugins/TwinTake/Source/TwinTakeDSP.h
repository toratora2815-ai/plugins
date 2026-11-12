#pragma once
// Twin Take の音の処理（JUCE なし）。1 本録りの歌・ギターから「別テイク」を作るダブラー。
//
// 考え方（2026-11-11、ミックスエンジニア係の案から）:
//   - ダブルは「音の頭（オンセット）」ごとに、タイミング（遅れ）・音程（数セント）・音色（ごく軽い傾き）・音量を選び直す。
//     選び直すのは次の音の頭の直前だけ。古い読み出しは頭の手前 5 ms で消し、新しい読み出しは頭の 1.5 ms 手前から始める
//     → 音の頭は 1 回だけ鳴る（二重にならない）。伸ばしている音の途中では揺れない
//   - 音程は読み出しの速さ（リサンプリング、16 タップの窓付き sinc）で付ける。粒をつなぐ方式ではないので、伸ばした音でもシュワシュワしない
//   - 長く伸ばして遅れが範囲を外れたときだけ、波形のそろう所を探して 30 ms でつなぎ直す（WSOLA）
//   - どの値も「曲の位置（オンセットの位置）＋TAKE＋トラック名」から決める → 再生のたび・書き出し・バッファの大きさが変わっても同じ音
//
// VOCAL : 原音はそのまま（遅れ 0）。ダブルは 1 / 2 / 4 本、原音より 12〜45 ms 後ろ。子音（サ行・タ行の頭）と息はダブルしない。ダブルの高域を少し下げる
// GUITAR: 原音を左・ダブルを右。原音を 40 ms 遅らせ（Logic に申告）、ダブルは原音の前後 10〜25 ms（0 付近は使わない）。LOW KEEP より下は原音をまん中に
//
// 1.1.0（2026-11-12、ミックスエンジニア係の決め方）:
//   - 語尾（TAIL）: ダブルが今読んでいる所の原音の大きさ（10 ms の窓）が、そこから「範囲の終わり」（VOCAL は今、GUITAR は原音を出している所 ＋10 ms）
//     までの一番小さい大きさより 4 dB 以上大きければ、超えた分だけ 1:1 で下げる（下げる 3 ms・戻す 10 ms）。原音が先に終わったのに、ダブルだけ残らない。
//     消したまま待つ形にはしない（音の頭がとれない歌い方で抜けるため）。GUITAR でダブルが前のときは何もしない
//   - 長い音の中の揺れ（DRIFT、VOCAL だけ）: 音の頭で決めた音程のずれのまわりを、その ±50%（最大 ±3 セント）で、1.5〜3 秒ごとの次の目標へなめらかに動く。
//     0 セントをまたがない（原音と同じ高さに近づくと、くし形の音色が止まって聞こえるため）。音の頭では 0 から始める
//   - DOUBLE ONLY: 原音を出さずダブルだけ（AUX のセンドに挿して、ダブルだけを加工する用）。切り替えは 20 ms
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace twin
{
    struct Params
    {
        int mode = 0;              // 0 = VOCAL, 1 = GUITAR
        int voices = 2;            // 1 / 2 / 4（VOCAL だけ。GUITAR はいつも 1）
        float doubleDb = -6.0f;    // ダブル 1 本の大きさ（原音に対して）。-60 以下は無音
        float loose = 0.3f;        // 0..1（TIGHT ⇄ LOOSE）
        float width = 0.8f;        // 0..1
        float lowKeepHz = 200.0f;  // これより下はダブルしない（20 未満 = OFF）
        int take = 1;              // 1..8（別のテイクを引き直す）
        bool mono = false;         // 聴くだけ（L+R）
        bool doubleOnly = false;   // 原音を出さずダブルだけ（AUX に挿して使う）
        bool tail = true;          // テスト用（語尾の処理を止めて比べる。プラグインからはいつも true）
        uint32_t trackSeed = 0;    // トラック名から（同じ設定でもトラックごとに違うテイクになる）
    };

    // 見るための数値（画面・テスト用）
    struct Stats
    {
        int64_t onsets = 0, forced = 0, jumps = 0;
        float lastDelayMs[4] = {}, lastCents[4] = {};
        float gate = 1.0f;         // 子音・息で閉じている割合（1 = 開いている）
        float driftMaxAbs = 0.0f;  // テスト用: 長い音の中の揺れのいちばん大きい幅（セント）
        float driftMinRatio = 1.0f;// テスト用: （音程のずれ ＋ 揺れ）÷ 音程のずれ のいちばん小さい値（0 より大きい = 0 セントをまたがない）
    };

    class Engine
    {
    public:
        static constexpr double guitarLatencyMs = 40.0;
        static constexpr int maxVoices = 4;

        Engine() { buildSinc(); }

        void prepare (double sampleRate)
        {
            fs = std::max (8000.0, sampleRate);
            int need = (int) std::ceil (fs * 0.35);
            int sz = 1; while (sz < need) sz <<= 1;
            buf.assign ((size_t) sz, 0.0f);
            gateBuf.assign ((size_t) sz, 1.0f);
            mask = sz - 1;
            auto a = [this] (double ms) { return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs))); };
            aFastAtt = a (0.3); aFastRel = a (10.0); aSlow = a (50.0); aPow3 = a (3.0);
            pkDecay = (float) std::exp (-1.0 / (3.0 * fs));
            aGateClose = a (0.5); aGateOpen = a (20.0);
            aSmooth = a (20.0); aSmoothSlow = a (40.0);
            tiltCoef = (float) (1.0 - std::exp (-2.0 * kPi * 800.0 / fs));
            airCoef = (float) (1.0 - std::exp (-2.0 * kPi * 7000.0 / fs));
            designHP (gateHP, 4000.0);
            guitarLat = (int) std::lround (guitarLatencyMs * 0.001 * fs);
            swStep = (float) (1.0 / (0.01 * fs));
            frameLen = std::max (1, (int) std::lround (0.001 * fs));
            halfWin = (int) std::lround (0.005 * fs);
            frameP.assign ((size_t) kFrames, 0.0f); lvlDb.assign ((size_t) kFrames, -120.0f); sufMin.assign ((size_t) kFrames, -120.0f);
            frameOf.assign ((size_t) sz, -1);
            aTailDown = a (3.0); aTailUp = a (10.0);
            reset();
        }

        void reset()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            std::fill (gateBuf.begin(), gateBuf.end(), 1.0f);
            w = 0;
            for (auto& v : voice) v = Voice();
            ef = es = eH = eA = pkPow = 0.0f; armed = true; lastOnset = -1000000000;
            expectTl = kNoTimeline; jumpW = INT64_MIN / 2;
            gateHP.z1 = gateHP.z2 = 0;
            for (auto& f : dblHP) f.z1 = f.z2 = 0;
            for (auto& f : xLP) f.z1 = f.z2 = 0;
            for (auto& f : xHP) f.z1 = f.z2 = 0;
            airState[0] = airState[1] = 0;
            dblGain = dblGainT; widthS = widthT; lowKeepS = lowKeepT; lkMix = lkMixT; monoS = monoT;
            for (int v = 0; v < maxVoices; ++v) voiceGain[v] = voiceOnT[v];
            designCross (lowKeepS);
            if (switching) { p.mode = nextMode; switching = false; applyVoiceTargets(); }
            swGain = 1.0f;
            for (int v = 0; v < maxVoices; ++v) voiceGain[v] = voiceOnT[v];
            snapPan();
            std::fill (frameP.begin(), frameP.end(), 0.0f); std::fill (lvlDb.begin(), lvlDb.end(), -120.0f); std::fill (sufMin.begin(), sufMin.end(), -120.0f);
            std::fill (frameOf.begin(), frameOf.end(), (int64_t) -1);
            frameCount = 0; frameAcc = 0; win10 = 0; curFrame = 0; tailEnd = -1;
            dryS = dryT;
            stats = Stats();
        }

        int latencySamples() const { return (switching ? nextMode : p.mode) == 1 ? guitarLat : 0; }   // FIX: 切りかえ予定のモードで申告
        const Stats& getStats() const { return stats; }

        void setParams (const Params& np)
        {
            const int activeMode = p.mode;
            p = np;
            if (np.mode != activeMode && w > 0) { switching = true; nextMode = np.mode; p.mode = activeMode; }   // FIX: 鳴らしている途中なら 10 ms で下げてから
            else if (np.mode != activeMode) { for (auto& v : voice) v = Voice(); armed = true; switching = false; }
            else switching = false;
            p.voices = p.voices >= 4 ? 4 : (p.voices >= 2 ? 2 : 1);
            p.loose = std::clamp (p.loose, 0.0f, 1.0f);
            p.width = std::clamp (p.width, 0.0f, 1.0f);
            dblGainT = p.doubleDb <= -60.0f ? 0.0f : std::pow (10.0f, p.doubleDb / 20.0f);
            widthT = p.width;
            lkMixT = p.lowKeepHz < 20.0f ? 0.0f : 1.0f;   // OFF はフィルターを外さず、混ぜる量で消す（周波数を 0 へ動かさない）
            if (p.lowKeepHz >= 20.0f) lowKeepT = std::clamp (p.lowKeepHz, 20.0f, 1000.0f);
            monoT = p.mono ? 1.0f : 0.0f;
            dryT = p.doubleOnly ? 0.0f : 1.0f;
            applyVoiceTargets();
        }
        void applyVoiceTargets()
        {
            const int nv = p.mode == 1 ? 1 : p.voices;
            for (int v = 0; v < maxVoices; ++v) voiceOnT[v] = v < nv ? 1.0f : 0.0f;
        }
        void snapPan()
        {
            const int nv = p.mode == 1 ? 1 : p.voices;
            for (int v = 0; v < maxVoices; ++v)
            {
                float pan;
                if (p.mode == 1) pan = widthS; else if (nv == 1) pan = 0.0f; else if (v < 2) pan = (v == 0 ? -1.0f : 1.0f) * widthS; else pan = (v == 2 ? -0.5f : 0.5f) * widthS;
                const float th = (pan + 1.0f) * (float) kPi * 0.25f, gN = nv == 4 ? 0.70710678f : 1.0f;
                panL[v] = gN * std::cos (th); panR[v] = gN * std::sin (th); gMono[v] = gN;
            }
        }

        // L・R をその場で書きかえる。numCh = 1 ならモノ（L だけ）。timeline = このブロックの頭の曲の位置（サンプル、分からなければ -1）
        void process (float* L, float* R, int numCh, int n, int64_t timeline)
        {
            // 再生位置が飛んだら（位置を変えた・ループの頭）、前の場所を読んでいるダブルを 5 ms で消して、音の頭の検出をやり直す
            if (timeline >= 0)
            {
                // 四捨五入・速さを変えた再生（Varispeed）で数サンプル揺れても飛んだとはみなさない（ブロック 1 つ＋10 ms より大きくずれたら）
                if (expectTl != kNoTimeline && std::llabs (timeline - expectTl) > (int64_t) std::max (n, lastN) + (int64_t) (0.01 * fs)) jumped();
                expectTl = timeline + n;
                lastN = n;
            }
            else expectTl = kNoTimeline;
            const bool stereoOut = numCh > 1 && R != nullptr;
            for (int i = 0; i < n; ++i)
            {
                if (switching) { swGain -= swStep; if (swGain <= 0.0f) { swGain = 0.0f; p.mode = nextMode; switching = false; for (auto& v : voice) v = Voice(); armed = true; applyVoiceTargets(); } }
                else if (swGain < 1.0f) swGain = std::min (1.0f, swGain + swStep);
                const bool guitar = p.mode == 1;
                const int nv = guitar ? 1 : p.voices;
                const float inL = L[i], inR = (numCh > 1 && R != nullptr) ? R[i] : inL;
                float m = 0.5f * (inL + inR);
                if (! (std::fabs (m) < 1.0e6f)) m = 0.0f;   // FIX: NaN・無限大・とんでもない値は中に入れない
                buf[(size_t) (w & mask)] = m;
                const int64_t tl = timeline >= 0 ? timeline + i : w;

                analyse (m, tl, guitar);

                // つまみのなめらかな追従
                dblGain += (dblGainT - dblGain) * aSmooth;
                widthS += (widthT - widthS) * aSmooth;
                monoS += (monoT - monoS) * aSmooth;
                dryS += (dryT - dryS) * aSmooth;   // DOUBLE ONLY: 原音の量（20 ms で出し入れ）
                lkMix += (lkMixT - lkMix) * aSmooth;
                lowKeepS += (lowKeepT - lowKeepS) * aSmoothSlow;
                if (((w & 31) == 0) && std::fabs (lowKeepS - designedHz) > 0.05f) designCross (lowKeepS);

                // ダブル
                float dL = 0, dR = 0, dM = 0;
                for (int v = 0; v < maxVoices; ++v)
                {
                    voiceGain[v] += (voiceOnT[v] - voiceGain[v]) * aSmooth;
                    if (voiceGain[v] < 1.0e-4f && voiceOnT[v] == 0.0f) { if (voice[(size_t) v].anyActive()) voice[(size_t) v] = Voice(); continue; }
                    const float y = runVoice (v, guitar);
                    float pan;
                    if (guitar) pan = widthS;
                    else if (nv == 1) pan = 0.0f;
                    else if (v < 2) pan = (v == 0 ? -1.0f : 1.0f) * widthS;
                    else pan = (v == 2 ? -0.5f : 0.5f) * widthS;
                    const float th = (pan + 1.0f) * (float) kPi * 0.25f;
                    const float gN = nv == 4 ? 0.70710678f : 1.0f;
                    panL[v] += (gN * std::cos (th) - panL[v]) * aSmooth;   // FIX
                    panR[v] += (gN * std::sin (th) - panR[v]) * aSmooth;
                    dL += y * voiceGain[v] * panL[v];
                    dR += y * voiceGain[v] * panR[v];
                    gMono[v] += (gN - gMono[v]) * aSmooth;
                    dM += y * voiceGain[v] * gMono[v];
                }
                if (! stereoOut) { dL = dM; dR = 0.0f; }   // モノのトラック（1 → 1）: ダブルは左右に分けず DOUBLE の大きさのまま
                // LOW KEEP より下はダブルしない
                { const float hL = dblHP[0].run (dL), hR = dblHP[1].run (dR); dL += (hL - dL) * lkMix; dR += (hR - dR) * lkMix; }
                // VOCAL: ダブルの高域を少し下げる（7 kHz より上を約 −3 dB）
                if (! guitar)
                {
                    airState[0] += (dL - airState[0]) * airCoef; airState[1] += (dR - airState[1]) * airCoef;
                    dL -= 0.3f * (dL - airState[0]); dR -= 0.3f * (dR - airState[1]);
                }
                dL *= dblGain; dR *= dblGain;

                const float od = guitar ? buf[(size_t) ((w - guitarLat) & mask)] : 0.0f;
                if (! stereoOut)
                {
                    // モノのトラック: 原音はそのまま（GUITAR は 40 ms 遅れ）、ダブルを足すだけ（パンの −3 / −6 dB をかけない）
                    L[i] = ((guitar ? od : inL) * dryS + dL) * swGain;
                    ++w;
                    continue;
                }
                float oL, oR;
                if (! guitar) { oL = inL * dryS + dL; oR = inR * dryS + dR; }
                else
                {
                    const float lo4 = xLP[1].run (xLP[0].run (od)), hi4 = xHP[1].run (xHP[0].run (od));   // LR4（足すと平ら）
                    const float th = (1.0f - widthS) * (float) kPi * 0.25f;   // 原音は左へ（pan = −WIDTH）
                    const float c = std::cos (th), sn = std::sin (th);
                    oL = (lo4 * (0.70710678f * lkMix + c * (1.0f - lkMix)) + hi4 * c) * dryS + dL;    // FIX: od と混ぜない（へこみなし）
                    oR = (lo4 * (0.70710678f * lkMix + sn * (1.0f - lkMix)) + hi4 * sn) * dryS + dR;
                }
                oL *= swGain; oR *= swGain;   // FIX: MODE 切りかえ
                if (monoS > 1.0e-4f) { const float mm = 0.5f * (oL + oR); oL += (mm - oL) * monoS; oR += (mm - oR) * monoS; }
                L[i] = oL; R[i] = oR;
                ++w;
            }
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr int kTaps = 16, kPhases = 512, kHalf = kTaps / 2;

        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        };
        struct Head
        {
            bool on = false;
            double pos = 0, rate = 1;
            double inStart = 0, inLen = 1;          // 読み出し位置で決めるフェードイン
            double outEnd = 1e300, outLen = 1;      // 読み出し位置で決めるフェードアウト（1e300 = まだ決まっていない）
            float amp = 1, gLo = 1, gHi = 1, lp = 0, gateSm = 1;
            int64_t keyTl = 0; int forced = 0; float cents = 0;
            // 長い音の中の揺れ（DRIFT）: cents のまわりを dFrom → dTo へ segLen サンプルかけて動く（driftA = 揺れの幅、0 = なし）
            float driftA = 0, dFrom = 0, dTo = 0; double segPos = 0, segLen = 1; uint64_t drs = 0; int rateCount = 0;
            // 語尾（TAIL）: 今の下げ量と目標（8 サンプルごとに決め直す）
            float tailG = 1, tailT = 1; int tailCount = 0;
        };
        // 読み出し 6 個・予定 8 個で足りる理由: いちばん長い遅れ（GUITAR 65 ms）＋ 手前 1.5 ms ＋ フェード < 音の頭の最短間隔 70 ms。
        // なので同時に生きているのは「消えかけ 1 ＋ 今の 1 ＋ つなぎ直し 1」まで、予定は 1 個まで（検査係の実測でも最大 2 個・1 個）。
        // 再生位置が飛び続ける極端な場合: 読み出しは最大 5 個で満杯にならない。一定で 8 ms より短い間隔で飛び続けると、後ろ側の GUITAR は
        // 予定 8 個が満杯になり、早く始まる予定から捨てる → その間ダブルは鳴らない（まだ鳴っていない予定なのでプチッとはしない。検査係の実測）
        // 遅れの範囲か、analyse() の 70 ms のどちらかを変えたら、ここを見直す
        struct Spawn { bool on = false; int64_t time = 0; Head h; };
        struct Voice
        {
            std::array<Head, 6> heads {};
            std::array<Spawn, 8> spawns {};
            bool anyActive() const
            {
                for (auto& h : heads) if (h.on) return true;
                for (auto& s : spawns) if (s.on) return true;
                return false;
            }
        };

        // ---- 解析（オンセット・子音と息） ----
        void analyse (float m, int64_t tl, bool guitar)
        {
            const float x2 = m * m;
            // 語尾（TAIL）用の原音の大きさ（1 ms ごと）。区切りは曲の位置にそろえる（止まっていた長さで区切りがずれると、再生のたびに語尾の下げ方が変わるため）
            frameOf[(size_t) (w & mask)] = curFrame;
            frameAcc += (double) x2;
            ++frameCount;
            if ((tl + 1) % frameLen == 0 || frameCount >= 4 * frameLen) frameDone (guitar);
            ef += (x2 - ef) * (x2 > ef ? aFastAtt : aFastRel);
            es += (x2 - es) * aSlow;
            eA += (x2 - eA) * aPow3;
            pkPow = std::max (eA, pkPow * pkDecay);
            // 子音・息（VOCAL だけ）: 4 kHz より上の割合が大きい所、小さくてざらざらした所
            float g = 1.0f;
            if (! guitar)
            {
                const float h = gateHP.run (m);
                eH += (h * h - eH) * aPow3;
                const float ratio = eH / (eA + 1.0e-12f);
                const float sib = std::clamp ((ratio - 0.30f) / 0.30f, 0.0f, 1.0f);
                const float rel = eA / (pkPow + 1.0e-12f);
                const float breath = (rel < 0.003f && ratio > 0.08f) ? std::clamp ((0.003f - rel) / 0.002f, 0.0f, 1.0f) : 0.0f;
                g = 1.0f - std::max (sib, breath);
            }
            gateBuf[(size_t) (w & mask)] = g;
            stats.gate += (g - stats.gate) * 1.0e-4f;

            const float rDb = 10.0f * std::log10 ((ef + 1.0e-14f) / (es + 1.0e-14f));
            if (rDb < 4.0f) armed = true;
            const bool loudEnough = ef > 1.0e-7f && ef > pkPow * 1.0e-3f;
            if (armed && rDb > 9.0f && loudEnough && (w - lastOnset) > (int64_t) (0.07 * fs))
            {
                armed = false; lastOnset = w;
                const int64_t back = (int64_t) (0.001 * fs);
                onset (w - back, tl - back, guitar);
            }
        }

        // 1 ms ぶん たまったら: 10 ms の窓の大きさ（dB）を記録し、「範囲の終わり」から前へ 100 ms ぶんの「そこから終わりまでの一番小さい値」を作り直す
        void frameDone (bool guitar)
        {
            const float pw = frameCount > 0 ? (float) (frameAcc / frameCount) : 0.0f;
            frameAcc = 0; frameCount = 0;
            const int64_t f = curFrame++;
            constexpr int64_t fm = kFrames - 1;
            win10 += (double) pw - (double) frameP[(size_t) ((f - 10) & fm)];
            if (win10 < 0) win10 = 0;
            frameP[(size_t) (f & fm)] = pw;
            lvlDb[(size_t) (f & fm)] = 10.0f * std::log10 ((float) std::max (win10 / 10.0, 1.0e-12));
            // 範囲の終わり: VOCAL は今、GUITAR は原音を出している所（40 ms 前）＋ 10 ms（測るのが 10 ms 遅れる分を打ち消す）
            const int64_t E = guitar ? f - (int64_t) std::lround (guitarLatencyMs - 10.0) : f;
            float mn = 1.0e9f;
            for (int64_t k = E; k > E - kSufLen && k >= 0; --k) { mn = std::min (mn, lvlDb[(size_t) (k & fm)]); sufMin[(size_t) (k & fm)] = mn; }
            tailEnd = E;
        }
        // この位置を読んでいるダブルの下げ量（1 = そのまま）。
        // 比べるのは「pos がまん中の 10 ms の窓」（= pos + 5 ms で終わる窓 fc）と、「fc から範囲の終わりまでの一番小さい値」。
        // （1.1.0 の最初の版は一番小さい値を pos の区切りから探していて、音の頭の直前の静かな所を拾い、ダブルの頭まで下げていた。DSP 検査係）
        float tailTarget (double pos) const
        {
            if (tailEnd < 0) return 1.0f;
            constexpr int64_t fm = kFrames - 1;
            const int64_t ip = (int64_t) std::floor (pos);
            const int64_t ic = std::min (ip + (int64_t) halfWin, w - 1);
            if (ip < w - mask || ic < 0) return 1.0f;
            const int64_t fc = std::min (frameOf[(size_t) (ic & mask)], tailEnd);
            if (fc < 0 || fc <= tailEnd - kSufLen) return 1.0f;
            const float ex = lvlDb[(size_t) (fc & fm)] - sufMin[(size_t) (fc & fm)] - 4.0f;
            return ex > 0.0f ? std::pow (10.0f, -ex / 20.0f) : 1.0f;
        }
        // 長い音の中の揺れ（1 サンプルぶん進める。音程は 32 サンプルごとに計算し直す）
        void driftStep (Head& h)
        {
            h.segPos += 1.0;
            if (h.segPos >= h.segLen)
            {
                h.segPos -= h.segLen; h.dFrom = h.dTo;
                Rnd rr { h.drs }; h.dTo = h.driftA * rr.bip(); h.segLen = (1.5 + 1.5 * rr.next()) * fs; h.drs = rr.s;
            }
            if (--h.rateCount <= 0)
            {
                h.rateCount = 32;
                const double u = std::clamp (h.segPos / h.segLen, 0.0, 1.0), sm = u * u * (3.0 - 2.0 * u);
                const double d = h.dFrom + (h.dTo - h.dFrom) * sm;
                h.rate = std::pow (2.0, ((double) h.cents + d) / 1200.0);
                stats.driftMaxAbs = std::max (stats.driftMaxAbs, (float) std::fabs (d));
                if (h.cents != 0.0f) stats.driftMinRatio = std::min (stats.driftMinRatio, (float) (((double) h.cents + d) / h.cents));
            }
        }

        // ---- 乱数（曲の位置・TAKE・トラック・声部から決まる） ----
        static uint64_t mix64 (uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }
        struct Rnd
        {
            uint64_t s;
            float next() { s = mix64 (s); return (float) ((s >> 40) * (1.0 / 16777216.0)); }   // 0..1
            float bip() { return next() * 2.0f - 1.0f; }
        };
        Rnd rndFor (int64_t keyTl, int v, int forced) const
        {
            const uint64_t q = (uint64_t) (keyTl >> 6);   // 64 サンプル（約 1.3 ms）単位
            uint64_t s = mix64 (q ^ ((uint64_t) (uint32_t) p.take << 40) ^ ((uint64_t) p.trackSeed << 8) ^ (uint64_t) (v * 0x1000193) ^ ((uint64_t) forced << 52));
            return Rnd { s };
        }

        // 新しい読み出しの設定を選ぶ（delay はサンプル）
        void choose (Head& h, Rnd& r, int v, bool guitar, double& delaySamples) const
        {
            const float L = p.loose;
            double dMs;
            float cents;
            const float centsMax = 2.0f + 7.0f * L;
            if (! guitar)
            {
                const float center = 15.0f + 10.0f * L + (v >= 2 ? 4.0f : 0.0f);
                const float spread = 2.0f + 12.0f * L;
                const float u = 0.5f * (r.bip() + r.bip());              // 三角分布（まん中が多い）
                dMs = std::clamp (center + spread * u, 12.0f, 45.0f);
                cents = centsMax * (0.25f + 0.75f * r.next()) * (r.next() < 0.5f ? -1.0f : 1.0f);
            }
            else
            {
                const float off = 10.0f + (6.0f + 9.0f * L) * r.next();
                // 前か後ろかは TAKE とトラックで決める（音ごとにくじで入れかえると、後ろへ跳んだ所で右だけ最大 47 ms 途切れたため）
                const float sign = guitarSide();
                dMs = guitarLatencyMs + sign * off;
                // 原音から離れる向きに流す（前にいるダブルは高め＝もっと前へ、後ろは低め＝もっと後ろへ）
                cents = centsMax * (0.25f + 0.75f * r.next()) * (sign < 0 ? 1.0f : -1.0f);
            }
            h.rate = std::pow (2.0, cents / 1200.0);
            h.cents = cents;
            // 長い音の中の揺れ（VOCAL だけ）: その音のずれの ±50%、最大 ±3 セント → 0 セントをまたがない。音の頭では 0 から
            h.driftA = guitar ? 0.0f : std::min (3.0f, 0.5f * std::fabs (cents));
            h.dFrom = 0.0f; h.segPos = 0.0; h.rateCount = 0;
            { Rnd rr { r.s ^ 0xD5F7A3C1ull }; h.dTo = h.driftA * rr.bip(); h.segLen = (1.5 + 1.5 * rr.next()) * fs; h.drs = rr.s; }
            h.tailG = h.tailT = 1.0f; h.tailCount = 0;
            const float ampDb = (0.5f + 2.0f * L) * r.bip();
            h.amp = std::pow (10.0f, ampDb / 20.0f);
            const float tiltDb = (0.5f + 1.5f * L) * r.bip();
            h.gLo = std::pow (10.0f, -tiltDb / 40.0f);
            h.gHi = std::pow (10.0f, tiltDb / 40.0f);
            delaySamples = dMs * 0.001 * fs;
            (void) v;
        }

        float guitarSide() const   // −1 = ダブルが原音より前、+1 = 後ろ
        {
            return (mix64 (((uint64_t) (uint32_t) p.take << 32) ^ (uint64_t) p.trackSeed ^ 0x5A17ull) & 1) ? 1.0f : -1.0f;
        }

        // 再生位置が飛んだとき（Logic で位置を変えた・ループの頭）:
        //   VOCAL  … 原音はすぐ切りかわるので、ダブルも 5 ms で消す（本物の 2 本録りでもループの切れ目で両方切りかわる。前の場所の音を残さない）。
        //             始まる前に原音が切りかわる予定は捨てる
        //   GUITAR … 原音は 40 ms 遅れて出るので、ダブルは「リングの飛んだ所（w − kHalf）まで読み終えるまで」鳴らす。
        //             すぐ消すと、ループの切れ目ごとに右だけ 15〜47 ms 抜けた（後ろ側のテイクは、切れ目の後 10〜25 ms だけ前の場所の続きが鳴る）
        // どちらも、飛んだ所より先（新しい場所）は読まない。今の音量より上げない
        void jumped()
        {
            const double cut = (double) w - kHalf;
            const double f = 0.005 * fs;
            const double keep = p.mode == 1 ? 1.0e18 : f;   // GUITAR は飛んだ所まで、VOCAL は 5 ms（MODE 切り替え中は、いま動いているモードで）
            auto limitTo = [&] (Head& h, double curPos, double untilSwitch)
            {
                const double end = std::max (std::min (cut, curPos + untilSwitch), curPos + 16.0);
                if (h.outEnd <= end) return;
                const double gNow = std::clamp ((h.outEnd - curPos) / h.outLen, 0.0, 1.0);
                double len = f;
                if (std::min (1.0, (end - curPos) / len) > gNow) len = (end - curPos) / std::max (gNow, 1.0e-3);
                h.outEnd = end; h.outLen = len;
            };
            for (auto& vc : voice)
            {
                for (auto& h : vc.heads) if (h.on) limitTo (h, h.pos, keep);
                for (auto& s : vc.spawns)
                    if (s.on)
                    {
                        const double untilSwitch = (double) (w - s.time) + keep;   // 始まってから原音が切りかわるまで
                        if (untilSwitch < 0.003 * fs || s.h.pos + 0.003 * fs > cut) { s.on = false; continue; }
                        limitTo (s.h, s.h.pos, untilSwitch);
                    }
            }
            ef = es = eA = eH = pkPow = 0.0f; armed = true;
            lastOnset = w - (int64_t) fs;
            jumpW = w;
            frameAcc = 0; frameCount = 0;
            ++stats.jumps;
        }

        void onset (int64_t O, int64_t tlO, bool guitar)
        {
            ++stats.onsets;
            const double pre = 0.0015 * fs, fadeOut = 0.005 * fs;
            const double cut = std::max ((double) O - pre, (double) jumpW);   // ここまでに古い読み出しを消す（飛んだ直後は、飛んだ所より前を読まない）
            const int nv = guitar ? 1 : p.voices;
            for (int v = 0; v < nv; ++v)
            {
                Voice& vc = voice[(size_t) v];
                // 古い読み出し（頭の手前を読んでいるもの）は、頭の手前で消す
                auto limit = [&] (Head& h, double curPos)
                {
                    if (curPos >= cut || h.outEnd <= cut) return;   // FIX: すでに cut までに消える予定なら触らない
                    const double gNow = std::clamp ((h.outEnd - curPos) / h.outLen, 0.0, 1.0);
                    double end = std::max (cut, curPos + 16.0), len = fadeOut;
                    if (std::min (1.0, (end - curPos) / len) > gNow) len = (end - curPos) / std::max (gNow, 1.0e-3);   // FIX: 今の音量より上げない
                    h.outEnd = end; h.outLen = len;
                };
                for (auto& h : vc.heads) if (h.on) limit (h, h.pos);
                for (auto& s : vc.spawns) if (s.on) limit (s.h, s.h.pos);
                // 新しい読み出し: 頭の 1.5 ms 手前から、選んだ遅れで始める
                Rnd r = rndFor (tlO, v, 0);
                Head nh; double d = 0;
                choose (nh, r, v, guitar, d);
                nh.on = true; nh.pos = cut; nh.inStart = cut; nh.inLen = pre; nh.keyTl = tlO; nh.forced = 0;
                const int64_t t = (int64_t) std::llround (cut + d);
                addSpawn (vc, nh, t);
                stats.lastDelayMs[v] = (float) (d / fs * 1000.0);
                stats.lastCents[v] = nh.cents;
            }
        }

        static void addSpawn (Voice& vc, const Head& h, int64_t t)
        {
            for (auto& s : vc.spawns) if (! s.on) { s.on = true; s.time = t; s.h = h; return; }
            // いっぱいなら、いちばん古いものと入れかえる
            auto it = std::min_element (vc.spawns.begin(), vc.spawns.end(), [] (const Spawn& a, const Spawn& b) { return a.time < b.time; });
            it->on = true; it->time = t; it->h = h;
        }
        static Head* freeHead (Voice& vc)
        {
            for (auto& h : vc.heads) if (! h.on) return &h;
            // いっぱいなら、いちばん消えかけのものを使う
            Head* best = &vc.heads[0];
            for (auto& h : vc.heads) if (h.outEnd < best->outEnd) best = &h;
            return best;
        }

        float runVoice (int v, bool guitar)
        {
            Voice& vc = voice[(size_t) v];
            // 始まる時刻になった読み出しを動かす
            for (auto& s : vc.spawns)
                if (s.on && s.time <= w)
                {
                    Head* h = freeHead (vc);
                    *h = s.h;
                    h->pos = s.h.pos + (double) (w - s.time) * s.h.rate;
                    s.on = false;
                }
            const double dMinAbs = (guitar ? 10.0 : 11.0) * 0.001 * fs;
            const double dMaxAbs = (guitar ? 70.0 : 60.0) * 0.001 * fs;
            const bool tailOn = p.tail && (! guitar || guitarSide() > 0.0f);   // 語尾は VOCAL と、GUITAR でダブルが後ろのとき
            float out = 0.0f;
            Head* current = nullptr;
            for (auto& h : vc.heads)
            {
                if (! h.on) continue;
                const double gin = std::clamp ((h.pos - h.inStart) / h.inLen, 0.0, 1.0);
                const double gout = std::clamp ((h.outEnd - h.pos) / h.outLen, 0.0, 1.0);
                if (h.pos >= h.outEnd) { h.on = false; continue; }
                float x = read (h.pos);
                h.lp += (x - h.lp) * tiltCoef;
                x = h.gLo * h.lp + h.gHi * (x - h.lp);
                if (! guitar)
                {
                    const int64_t ip = (int64_t) h.pos;
                    const int64_t la = (int64_t) (0.002 * fs);
                    const float gt = std::min (gateBuf[(size_t) (ip & mask)], gateBuf[(size_t) (std::min (ip + la, w) & mask)]);
                    h.gateSm += (gt - h.gateSm) * (gt < h.gateSm ? aGateClose : aGateOpen);
                    x *= h.gateSm;
                }
                if (tailOn)
                {
                    if (--h.tailCount <= 0) { h.tailCount = 8; h.tailT = tailTarget (h.pos); }
                    h.tailG += (h.tailT - h.tailG) * (h.tailT < h.tailG ? aTailDown : aTailUp);
                }
                else if (h.tailG < 1.0f)
                {   // GUITAR で TAKE を「後ろ → 前」に変えた時など: 下げたまま固まらないように 10 ms で戻す
                    h.tailT = 1.0f; h.tailG += (1.0f - h.tailG) * aTailUp; if (1.0f - h.tailG < 1.0e-6f) h.tailG = 1.0f;
                }
                out += x * h.amp * (float) (gin * gout) * h.tailG;
                h.pos += h.rate;
                if (h.driftA > 0.0f) driftStep (h);
                if (h.outEnd > 1e299 && (current == nullptr || h.inStart > current->inStart)) current = &h;
            }
            // 遅れが範囲を外れたら、波形のそろう所でつなぎ直す（伸ばした長い音だけ）
            if (current != nullptr)
            {
                const double delay = (double) w - current->pos;
                if (delay < dMinAbs || delay > dMaxAbs) forcedSplice (vc, *current, v, guitar, delay < dMinAbs);
            }
            return out;
        }

        void forcedSplice (Voice& vc, Head& old, int v, bool guitar, bool tooEarly)
        {
            ++stats.forced;
            const double X = 0.03 * fs;
            Head nh = old;
            nh.forced = old.forced + 1;
            // 音程は同じまま、範囲の反対の端へ戻す（伸ばしている途中で音程を変えない）
            double dMs;
            if (! guitar) dMs = tooEarly ? std::min (45.0, 26.0 + 14.0 * p.loose) : std::max (16.0, 15.0 + 2.0 * p.loose);
            else
            {
                const bool front = guitarSide() < 0.0f;   // 前にいるダブル（高め）
                const double off = 15.0 + 3.0 * p.loose;   // FIX: align の ±5 ms を足しても原音から 10 ms 以上
                dMs = guitarLatencyMs + (front ? -off : off);
            }
            double target = (double) w - dMs * 0.001 * fs;
            target += align (old.pos, target);
            nh.pos = target; nh.inStart = target; nh.inLen = X; nh.outEnd = 1e300; nh.lp = old.lp; nh.gateSm = old.gateSm;
            old.outEnd = old.pos + X * old.rate; old.outLen = X * old.rate;
            Head* h = freeHead (vc);
            *h = nh;
            stats.lastDelayMs[v] = (float) dMs;
        }

        // 古い読み出しがこれから読む所と、新しい候補の所が、いちばん似ているずれ（サンプル）
        double align (double posOld, double posNew) const
        {
            const int W = (int) (0.010 * fs), K = (int) (0.005 * fs);
            const int step = std::max (1, (int) std::lround (fs / 24000.0));   // FIX: 44.1k でも 2
            const int64_t po = (int64_t) posOld, pn = (int64_t) posNew;
            const int64_t limit = w - 2;
            auto corr = [&] (int k)
            {
                double c = 0, e1 = 0, e2 = 0;
                for (int i = 0; i < W; i += step)
                {
                    const int64_t a = po + i, b = pn + k + i;
                    if (a > limit || b > limit) break;
                    const float xa = buf[(size_t) (a & mask)], xb = buf[(size_t) (b & mask)];
                    c += (double) xa * xb; e1 += (double) xa * xa; e2 += (double) xb * xb;
                }
                return c / std::sqrt (e1 * e2 + 1e-20);
            };
            int best = 0; double bc = -2;
            for (int k = -K; k <= K; k += step) { const double c = corr (k); if (c > bc) { bc = c; best = k; } }
            for (int k = best - step + 1; k < best + step; ++k) { const double c = corr (k); if (c > bc) { bc = c; best = k; } }
            return (double) best;
        }

        // ---- 16 タップの窓付き sinc で、すき間の位置を読む ----
        float read (double pos) const
        {
            const int64_t ip = (int64_t) std::floor (pos);
            const double fr = pos - (double) ip;
            const double ph = fr * kPhases;
            const int pi = (int) ph;
            const float t = (float) (ph - pi);
            const float* k0 = &sinc[(size_t) pi * kTaps];
            const float* k1 = k0 + kTaps;
            float acc = 0.0f;
            const int64_t base = ip - kHalf + 1;
            for (int k = 0; k < kTaps; ++k)
            {
                const float c = k0[k] + (k1[k] - k0[k]) * t;
                acc += c * buf[(size_t) ((base + k) & mask)];
            }
            return acc;
        }
        void buildSinc()
        {
            sinc.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);
            const double cutoff = 0.95, beta = 8.0;
            auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
            const double i0b = bessel0 (beta);
            for (int ph = 0; ph <= kPhases; ++ph)
            {
                const double fr = (double) ph / kPhases;
                double sum = 0;
                for (int k = 0; k < kTaps; ++k)
                {
                    const double x = (double) (k - kHalf + 1) - fr;   // 読みたい位置からの距離
                    const double sx = x == 0.0 ? 1.0 : std::sin (kPi * cutoff * x) / (kPi * cutoff * x);
                    const double r = x / kHalf;
                    const double win = std::fabs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                    const double v = cutoff * sx * win;
                    sinc[(size_t) ph * kTaps + (size_t) k] = (float) v; sum += v;
                }
                for (int k = 0; k < kTaps; ++k) sinc[(size_t) ph * kTaps + (size_t) k] /= (float) sum;   // 直流で 1 に
            }
        }

        // ---- フィルター ----
        void designHP (Biquad& f, double hz) const
        {
            const double wv = 2 * kPi * std::min (hz, 0.45 * fs) / fs, c = std::cos (wv), al = std::sin (wv) / (2 * 0.70710678), a0 = 1 + al;
            f.b0 = (float) ((1 + c) / 2 / a0); f.b1 = (float) (-(1 + c) / a0); f.b2 = f.b0; f.a1 = (float) (-2 * c / a0); f.a2 = (float) ((1 - al) / a0);
        }
        void designLP (Biquad& f, double hz) const
        {
            const double wv = 2 * kPi * std::min (hz, 0.45 * fs) / fs, c = std::cos (wv), al = std::sin (wv) / (2 * 0.70710678), a0 = 1 + al;
            f.b0 = (float) ((1 - c) / 2 / a0); f.b1 = (float) ((1 - c) / a0); f.b2 = f.b0; f.a1 = (float) (-2 * c / a0); f.a2 = (float) ((1 - al) / a0);
        }
        void designCross (float hz)
        {
            designedHz = hz;
            hz = std::max (hz, 20.0f);
            auto keep = [] (Biquad& dst, const Biquad& src) { const float z1 = dst.z1, z2 = dst.z2; dst = src; dst.z1 = z1; dst.z2 = z2; };
            Biquad hp, lp; designHP (hp, hz); designLP (lp, hz);
            for (auto& f : dblHP) keep (f, hp);
            for (auto& f : xHP) keep (f, hp);
            for (auto& f : xLP) keep (f, lp);
        }

        Params p;
        double fs = 48000.0;
        std::vector<float> buf, gateBuf, sinc;
        int64_t mask = 0, w = 0;
        int guitarLat = 1920;
        std::array<Voice, maxVoices> voice {};
        float voiceGain[maxVoices] = {}, voiceOnT[maxVoices] = { 1, 1, 0, 0 };

        float ef = 0, es = 0, eH = 0, eA = 0, pkPow = 0;
        bool armed = true; int64_t lastOnset = -1000000000;
        float aFastAtt = 0, aFastRel = 0, aSlow = 0, aPow3 = 0, pkDecay = 0, aGateClose = 0, aGateOpen = 0, aSmooth = 0, aSmoothSlow = 0;
        float tiltCoef = 0, airCoef = 0, airState[2] = {};
        Biquad gateHP, dblHP[2], xLP[2], xHP[2];

        float dblGain = 0.5f, dblGainT = 0.5f, widthS = 0.8f, widthT = 0.8f, lowKeepS = 200, lowKeepT = 200, lkMix = 1, lkMixT = 1, monoS = 0, monoT = 0, designedHz = -1;
        float dryS = 1, dryT = 1;   // DOUBLE ONLY（原音の量）
        // 語尾（TAIL）用: 原音の大きさを 1 ms ごとに。lvlDb = 10 ms の窓（dB）、sufMin = そこから「範囲の終わり」までの一番小さい値
        static constexpr int kFrames = 1024, kSufLen = 100;
        std::vector<float> frameP, lvlDb, sufMin;
        int frameLen = 48, frameCount = 0, halfWin = 240; double frameAcc = 0, win10 = 0; int64_t curFrame = 0, tailEnd = -1;
        std::vector<int64_t> frameOf;   // サンプルごとの 1 ms の区切りの番号（buf と同じ輪）
        float aTailDown = 0, aTailUp = 0;
        Stats stats;
        bool switching = false; int nextMode = 0; float swGain = 1.0f, swStep = 0.002f;
        static constexpr int64_t kNoTimeline = INT64_MIN;
        int64_t expectTl = kNoTimeline, jumpW = INT64_MIN / 2;
        int lastN = 0;
        float gMono[maxVoices] = { 1, 1, 1, 1 };
        float panL[maxVoices] = { 0.7071f, 0.7071f, 0.7071f, 0.7071f }, panR[maxVoices] = { 0.7071f, 0.7071f, 0.7071f, 0.7071f };
    };
}
