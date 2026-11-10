#pragma once
// EFFECTS の TREM（トレモロ）。My Tremolo（MyPlugins/MyTremolo）の揺れ方の計算と形の表をそのまま使う小型版。
// 2026-11-10 本人「My Tremolo ができたら愛用ギターにも入れて」→ ミックスエンジニア係「形を変えれば価値あり」→ 本人「小型版」:
//   つまみは DEPTH / RATE / SHAPE（5 種類）＋SYNC だけ。色付け・4 倍オーバーサンプリング・頭打ちはなし（遅れ 0。音色は AMP の仕事）。
//   RATE は SYNC 中は音符の長さ（Logic の拍に合わせる。止まっている間は Logic のテンポで自分で回る）、SYNC OFF・単体アプリは Hz。
// JUCE なし（数値テストを JUCE なしで回せるように）。My Tremolo の Source を変えたら AIO Guitar もビルドし直す。
#include "MyTremoloDSP.h"

namespace pedal
{
    class TremDSP
    {
    public:
        // RATE（SYNC 中）の選択肢と、My Tremolo のリズムの番号
        static constexpr int kNumRates = 8;
        static const char* rateName (int i) { static const char* n[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return n[clampi (i, 0, kNumRates - 1)]; }
        static int rateRhythm (int i) { static const int r[] = { 3, 6, 7, 9, 10, 12, 13, 14 }; return r[clampi (i, 0, kNumRates - 1)]; }
        // SHAPE の選択肢と、My Tremolo の形の番号（Vintage = 昔のアンプのトレモロふうの Vintage 3）
        static constexpr int kNumShapes = 5;
        static const char* shapeName (int i) { static const char* n[] = { "Sine", "Triangle", "Vintage", "Soft Square", "Square" }; return n[clampi (i, 0, kNumShapes - 1)]; }
        static int shapeTable (int i) { static const int s[] = { 0, 1, 12, 3, 2 }; return s[clampi (i, 0, kNumShapes - 1)]; }

        void prepare (double sampleRate, int /*maxBlock*/, int /*numChannels*/)
        {
            fs = sampleRate;
            smC = (float) std::exp (-1.0 / (0.02 * fs));
            declickLen = std::max (1, (int) (0.01 * fs));
            reset();
        }

        // 止まっていたのが鳴り始めた・壊れた値が来た: 深さは今の値から、時計は次のブロックで取り直す
        void reset()
        {
            depthS = depthT; lastGain = 1.0f; declickArm = false; declickLeft = 0; first = true;
        }

        int latency() const { return 0; }

        // a = DEPTH（%）、b = RATE の番号（SYNC 中）、c = SHAPE の番号
        void setParams (float depthPct, float rateIndex, float shapeIndex, bool snap = false)
        {
            depthT = clampf (depthPct / 100.0f, 0.0f, 1.0f);
            const int r = clampi ((int) std::lround (rateIndex), 0, kNumRates - 1), s = clampi ((int) std::lround (shapeIndex), 0, kNumShapes - 1);
            if ((r != rate || s != shape) && ! first && ! snap) declickArm = true;   // 形・速さを変えた瞬間の段差をならす
            rate = r; shape = s;
            if (snap) depthS = depthT;
        }

        // 毎ブロック: 時計。sync = Logic に合わせる（音符の長さ）、hz = SYNC OFF のときの速さ
        void setClock (bool syncIn, float hzIn, bool hostPlaying, bool hasPosition, double ppq, double hostBpm)
        {
            if (syncIn != sync && ! first) declickArm = true;   // SYNC の ON / OFF で揺れの位置が飛ぶ所をつなぐ
            sync = syncIn; hz = clampf (hzIn, 0.1f, 40.0f);
            playing = hostPlaying && hasPosition; hostPpq = ppq; bpm = hostBpm > 1.0 ? hostBpm : 120.0;
            clockFresh = true;   // Logic の位置を使うのは、このあと最初の process だけ（Rig がブロックを分けて呼ぶため）
        }

        void process (float* const* data, int numChannels, int numSamples)
        {
            const int rhythm = sync ? rateRhythm (rate) : 6;               // SYNC OFF は 1 拍 = 1 回で、拍の速さを Hz にする
            const double unit = mytrem::rhythmBeats (rhythm);
            const double perSample = sync ? bpm / 60.0 / fs : (double) hz / fs;
            const float* tab = mytrem::kShapes[shapeTable (shape)];

            if (first && ! (sync && playing))
                beat = sync ? hostPpq : 0.0;   // 止まっている間・SYNC OFF でも、休止の後は前の続きからではなく Logic の今の位置（Hz なら頭）から
            if (sync && playing && clockFresh)
            {
                if (! first && std::abs (hostPpq - beat) > 0.002 * unit) declickArm = true;   // 頭出し・ループ・テンポの途中変更
                beat = hostPpq;
            }
            clockFresh = false;
            first = false;

            for (int i = 0; i < numSamples; ++i)
            {
                depthS = smC * depthS + (1.0f - smC) * depthT;
                float g = mytrem::Modulator::gain (beat, rhythm, noEvents, unit, tab, depthS, 0.0f, 0.0, 0.0, 0.0);
                beat += perSample;
                // つなぎ: 飛んだ瞬間の差を覚えて 10 ms でまっすぐ 0 に（My Tremolo と同じ）
                if (declickArm) { declickArm = false; dcOffset = lastGain - g; declickLeft = declickLen; }
                if (declickLeft > 0) { g += dcOffset * (float) declickLeft / (float) declickLen; --declickLeft; g = clampf (g, 0.0f, 1.0f); }   // つなぎの間に角が来ても 0〜1 の外へ出さない
                lastGain = g;
                for (int c = 0; c < numChannels; ++c) data[c][i] *= g;
            }
            if (! (sync && playing) && beat > 1.0e6) beat = std::fmod (beat, 48.0);
        }

    private:
        static int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
        static float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        double fs = 48000;
        float smC = 0.999f;
        float depthT = 0.6f, depthS = 0.6f, lastGain = 1.0f, hz = 5.5f, dcOffset = 0.0f;
        int rate = 3, shape = 2, declickLeft = 0, declickLen = 480;
        bool sync = true, playing = false, first = true, declickArm = false, clockFresh = false;
        double beat = 0, hostPpq = 0, bpm = 120;
        mytrem::EventList noEvents;   // パターンは使わない
    };
}
