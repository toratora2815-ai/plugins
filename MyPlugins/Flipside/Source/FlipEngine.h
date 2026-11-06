#pragma once
// Flipside の逆再生（JUCE なし。tools/test_engine.cpp で単体テストできる）
//
// 入ってくる音を GRID の長さ（拍・小節）ごとの「枠」に区切り、枠がたまったら次の枠の時間に逆向きに鳴らす。
// 遅れ（レイテンシー）を枠 1 つ分にして Logic に伝えるので、逆向きの音は「もとの枠の位置」にぴったり収まる。
//   出力の c 番目（今の枠の j 番目）= 前の枠の (最後 - j) 番目
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <atomic>

namespace fl
{
    enum Pattern { all = 0, every2nd = 1, every4th = 2, off = 3 };

    struct Block
    {
        bool synced = false;     // Logic が再生中で、拍の位置がわかる
        double sliceLen = 24000; // 枠の長さ（サンプル）
        double phase = 0;        // ブロックの頭が、今の枠の何サンプル目か（synced のとき）
        int64_t index = 0;       // 今の枠の番号（曲の頭から。synced のとき）
        int latency = 24000;     // Logic に伝えている遅れ（サンプル）
        int pattern = all;
        float fadeMs = 5.0f;
        float mix = 1.0f;
    };

    class Engine
    {
    public:
        void prepare (double sampleRate, double maxSliceSec)
        {
            fs = sampleRate;
            maxLen = (int64_t) (maxSliceSec * fs) + 16;
            size_t need = (size_t) (maxLen * 2 + 8192);
            size = 1;
            while (size < need) size <<= 1;
            mask = size - 1;
            for (auto& b : buf) b.assign (size, 0.0f);
            c = 0; n0 = 0; prevLen = 0; phase = 0; idx = 0; prevIdx = -1; prevRev = false; gate = 0; started = false;
        }

        static bool reversedSlot (int pattern, int64_t index)
        {
            switch (pattern)
            {
                case all:      return true;
                case every2nd: return (index & 1) == 1;
                case every4th: return (index & 3) == 3;
                default:       return false;
            }
        }

        void process (float* const* ch, int numCh, int n, const Block& b)
        {
            numCh = std::min (numCh, 2);
            if (numCh <= 0) return;
            const double L = std::clamp (b.sliceLen, 16.0, (double) maxLen - 16);

            if (b.synced)
            {
                // 予想とずれていたら（再生開始・位置移動・テンポ変更）、ここで枠を切り直す
                if (! started || b.index != idx || std::abs (b.phase - phase) > 2.0)
                {
                    boundary (b.index, b.pattern);
                    phase = b.phase;
                }
            }
            else if (! started) { boundary (0, b.pattern); phase = 0; }
            started = true;

            const float F = (float) std::max (1.0, b.fadeMs * 0.001 * fs);
            const float gStep = 1.0f / F;
            const int64_t lat = std::clamp<int64_t> (b.latency, 0, maxLen);

            for (int i = 0; i < n; ++i)
            {
                if (phase >= L)
                {
                    phase -= L;
                    if (phase >= L) phase = 0;
                    boundary (idx + 1, b.pattern);
                }
                for (int k = 0; k < 2; ++k)
                    buf[k][(size_t) c & mask] = ch[std::min (k, numCh - 1)][i];

                const int64_t j = c - n0;
                float env = 0.0f;
                if (prevRev && j < prevLen)
                    env = std::min ({ 1.0f, (float) (j + 1) / F, (float) (prevLen - j) / F });
                const float target = prevRev ? 1.0f : 0.0f;
                gate = gate < target ? std::min (target, gate + gStep) : std::max (target, gate - gStep);
                const float dryG = 1.0f - b.mix * gate;
                const float revG = b.mix * env;

                for (int k = 0; k < numCh; ++k)
                {
                    const float dry = buf[k][(size_t) (c - lat) & mask];
                    const float rev = revG > 0.0f ? buf[k][(size_t) (n0 - 1 - j) & mask] : 0.0f;
                    ch[k][i] = dry * dryG + rev * revG;
                }
                ++c;
                phase += 1.0;
            }
            curIndex.store (idx, std::memory_order_relaxed);
            curPhase.store ((float) (phase / L), std::memory_order_relaxed);
        }

        // 画面用（今鳴っている枠＝前の枠の番号と、その中の位置 0..1）
        std::atomic<int64_t> curIndex { 0 };
        std::atomic<float> curPhase { 0.0f };

    private:
        void boundary (int64_t newIdx, int pattern)
        {
            prevLen = std::min<int64_t> (c - n0, maxLen);
            prevIdx = idx;
            prevRev = started && reversedSlot (pattern, prevIdx);
            n0 = c;
            idx = newIdx;
        }

        double fs = 48000;
        int64_t maxLen = 48000, c = 0, n0 = 0, prevLen = 0, idx = 0, prevIdx = -1;
        size_t size = 1, mask = 0;
        std::vector<float> buf[2];
        double phase = 0;
        bool prevRev = false, started = false;
        float gate = 0;
    };
}
