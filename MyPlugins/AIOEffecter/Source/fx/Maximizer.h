#pragma once
// MAXIMIZER … 音圧を上げて頭を天井で止める（先読みのリミッター。Guitar Rig の Maximizer を測って合わせ込み、2026-11-19）
//
// 測った結果（build/aiofx/cap/dyn/Maximizer、ThresholdDb_* / CeilingDb_* / CharacterR_*）:
//   上げる量 = INPUT（−20〜+20 dB）− THRESHOLD（−20〜0 dB）→ 天井 0 dB で止める → CEILING（−20〜0 dB）へ
//   （初期値 THRESHOLD −5.1・CEILING −1 で小さい音は +4.1 dB、大きい音は −1 dBFS で止まる）
//   先読み 7.35 ms（353 サンプル @48 kHz。遅れとして申告）
//   CHARACTER（0〜10）: 小さいほど波形そのものを削る（0 で 0 dBFS の正弦波の 3 次の歪み −14 dB、1 で −30 dB、2.5 から上はきれい）
// しくみ（2026-11-19 DSP 検査のあと）:
//   先読みの窓 [今 − 353, 今] の山の最大 → 下げる量 → 353 サンプルの箱形の平均でならす（音が届く前に下げ終わる。天井を超えない）
//   → 戻りは 2 ms
//   CHARACTER が 2.5 より小さいと、リミッターの天井を最大 +6 dB 上げて、超えた分を 4 倍のやわらかいクリップで削る（荒さ）
//   荒さなしの道は 4 サンプルの整数の遅れ（4 倍のフィルターの位相で山が増えない）、荒さありの道は 4 倍のクリップ。20 ms で入れ替え
//   最後に 1.5 ms の短い先読み（同じ箱形の平均）で天井 1.0 を守る（4 倍から戻した波形の行き過ぎ。2026-11-19 DSP 検査 2 回目）
#include "DynKit.h"

namespace aiofx
{
struct Maximizer final : Module
{
    static constexpr double kLookSec = 353.0 / 48000.0;
    static float inDb (float v)   { return -20.0f + 40.0f * v; }
    static float thrDb (float v)  { return -20.0f + 20.0f * v; }
    static float ceilDb (float v) { return -20.0f + 20.0f * v; }
    static float charVal (float v) { return 10.0f * v; }

    static const ModuleInfo& info()
    {
        static const ModuleInfo i { "maximizer", "MAXIMIZER", "DYNAMICS",
            "Loudness maximizer: lower THRESHOLD to push it louder, CEILING is the highest peak. Low CHARACTER = harder, crunchier. Adds 9 ms of delay (reported to Logic).",
            {
                { "INPUT",     0.5f,   0, [] (float v) { return fmt (inDb (v), 1, " dB"); } },
                { "THRESHOLD", 0.745f, 0, [] (float v) { return fmt (thrDb (v), 1, " dB"); } },
                { "CEILING",   0.95f,  0, [] (float v) { return fmt (ceilDb (v), 1, " dB"); } },
                { "CHARACTER", 0.355f, 0, [] (float v) { return fmt (charVal (v), 1, ""); } },
            } };
        return i;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        look = std::max (1, (int) std::lround (kLookSec * sr));
        W = look + 3;   // 列の大きさ: 窓の look + 1 個 ＋ 古いものを捨てる前に足す 1 個 ＋ 空と満杯を見分ける 1 個
        buf.assign ((size_t) (look + 1) * 2, 0.0f);
        win.assign ((size_t) W, 0.0f);
        dq.assign ((size_t) W, 0);
        box.assign ((size_t) look, 1.0);
        drive.prepare (sr, 0.02); ceil.prepare (sr, 0.02); crunch.prepare (sr, 0.02); crunchMix.prepare (sr, 0.02);
        safe.prepare (sr, 0.0015);
        recalc (true);
        reset();
    }
    void reset() override
    {
        std::fill (buf.begin(), buf.end(), 0.0f); pos = 0;
        std::fill (win.begin(), win.end(), 0.0f); head = tail = 0; t = 0;
        std::fill (box.begin(), box.end(), 1.0); boxSum = (double) look; boxPos = 0;
        g = 1.0f; gr = 1.0f;
        for (auto& o : os) o.reset();
        for (auto& a : d4) for (auto& v : a) v = 0.0f;
        d4p = 0;
        safe.reset();
    }
    void setKnob (int i, float v) override
    {
        if (i < 0 || i >= 4 || same (k[i], v)) return;
        k[i] = v; recalc (false);
    }
    int latency() const override { return look + 4 + safe.L; }   // 先読み 353 ＋ 4 倍のクリップ 4 ＋ 天井を守る先読み 72（48 kHz）
    bool hasCeiling() const override { return true; }   // 天井を守る部品（ラックの reset でドライを混ぜない）
    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            const float d = drive.next(), c = ceil.next(), cr = crunch.next(), cm = crunchMix.next();
            const float head0 = dbToGain (6.0f * cr);   // 荒さの分だけリミッターの天井を上げる
            const float l = L[s] * d, r = R[s] * d;
            const float pk = std::max (std::abs (l), std::abs (r));
            // 窓 [t − look, t] の最大（出している音も入る）: 単調な列で O(1)
            win[(size_t) (t % W)] = pk;
            while (head != tail && win[(size_t) (dq[(size_t) ((tail - 1 + W) % W)] % W)] <= pk) tail = (tail - 1 + W) % W;
            dq[(size_t) tail] = t; tail = (tail + 1) % W;
            while (head != tail && dq[(size_t) head] < t - look) head = (head + 1) % W;
            const float wmax = win[(size_t) (dq[(size_t) head] % W)];
            ++t;
            const double target = wmax > head0 ? (double) head0 / wmax : 1.0;
            // 箱形の平均（look サンプル）: 山が出ていく時点では、平均した下げ量が必ずその山に足りている
            boxSum += target - box[(size_t) boxPos];
            box[(size_t) boxPos] = target;
            if (++boxPos >= look) boxPos = 0;
            const float avg = (float) (boxSum / look);
            g = avg < g ? avg : avg + (g - avg) * relA;   // 戻りだけ CHARACTER の速さで
            // 音は先読みの分だけ遅らせる
            float* b = buf.data();
            const int rp = (pos + 1) % (look + 1);
            b[2 * pos] = l; b[2 * pos + 1] = r;
            float ol = b[2 * rp] * g, orr = b[2 * rp + 1] * g;
            pos = rp;
            // 荒さ: 荒さなしの道 = 4 サンプルの整数の遅れ、荒さありの道 = 4 倍のやわらかいクリップ（フィルターはいつも回す）。20 ms で入れ替え
            {
                const float dl = d4[0][d4p], dr = d4[1][d4p];
                d4[0][d4p] = ol; d4[1][d4p] = orr; d4p = (d4p + 1) & 3;
                const float cl = clip4 (os[0], ol, 1.0f), cr2 = clip4 (os[1], orr, 1.0f);
                ol = dl + cm * (cl - dl); orr = dr + cm * (cr2 - dr);
            }
            // 天井を守る短い先読み（荒さありで 4 倍から戻した波形の山が天井を超える分。荒さなしでは何もしない）
            safe.process (ol, orr);
            // 念のための頭打ち（上の先読みで、ふだんは届かない）
            ol = std::clamp (ol, -1.0f, 1.0f); orr = std::clamp (orr, -1.0f, 1.0f);
            L[s] = ol * c; R[s] = orr * c;
            gr = g;
        }
        boxSum = 0.0; for (double v : box) boxSum += v;   // 小数の誤差がたまらないよう、ブロックごとに数え直す
        safe.recount();
    }
    float meter() const override { return std::clamp (-gainToDb (gr) / 24.0f, 0.0f, 1.0f); }

private:
    // 短い先読みのリミッター（窓の最大 → 箱形の平均 → 戻り 2 ms。天井 1.0）
    struct ShortLim
    {
        int L = 72, W = 75, pos = 0, head = 0, tail = 0, bp = 0;
        std::vector<float> bufL, bufR, win;
        std::vector<int64_t> dq;
        std::vector<double> box;
        double boxSum = 72.0;
        int64_t t = 0;
        float g = 1.0f, relA = 0.0f;
        void prepare (double sr, double sec)
        {
            L = std::max (1, (int) std::lround (sec * sr)); W = L + 3;
            bufL.assign ((size_t) L + 1, 0.0f); bufR = bufL;
            win.assign ((size_t) W, 0.0f); dq.assign ((size_t) W, 0); box.assign ((size_t) L, 1.0);
            relA = tauCoef (sr, 0.002);
            reset();
        }
        void reset()
        {
            std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f);
            std::fill (win.begin(), win.end(), 0.0f); std::fill (box.begin(), box.end(), 1.0);
            boxSum = (double) L; pos = head = tail = bp = 0; t = 0; g = 1.0f;
        }
        inline void process (float& l, float& r)
        {
            const float pk = std::max (std::abs (l), std::abs (r));
            win[(size_t) (t % W)] = pk;
            while (head != tail && win[(size_t) (dq[(size_t) ((tail - 1 + W) % W)] % W)] <= pk) tail = (tail - 1 + W) % W;
            dq[(size_t) tail] = t; tail = (tail + 1) % W;
            while (head != tail && dq[(size_t) head] < t - L) head = (head + 1) % W;
            const float wmax = win[(size_t) (dq[(size_t) head] % W)];
            ++t;
            const double target = wmax > 1.0f ? 1.0 / wmax : 1.0;
            boxSum += target - box[(size_t) bp]; box[(size_t) bp] = target;
            if (++bp >= L) bp = 0;
            const float avg = (float) (boxSum / L);
            g = avg < g ? avg : avg + (g - avg) * relA;
            const int rp = (pos + 1) % (L + 1);
            bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
            l = bufL[(size_t) rp] * g; r = bufR[(size_t) rp] * g;
            pos = rp;
        }
        void recount() { boxSum = 0.0; for (double v : box) boxSum += v; }
    };

    static float clip4 (Oversampler4& o, float x, float amt)
    {
        float u[4]; o.up (x, u);
        if (amt > 1.0e-4f)
            for (auto& v : u) v += amt * (v / std::pow (1.0f + std::pow (std::abs (v) * (1.0f / 0.94f), 8.0f), 0.125f) - v);   // 天井 0.94（−0.5 dB）
        return o.down (u);
    }
    void recalc (bool snap)
    {
        drive.set (dbToGain (inDb (k[0]) - thrDb (k[1])), snap);
        ceil.set (dbToGain (ceilDb (k[2])), snap);
        const float c = charVal (k[3]);
        crunch.set (std::clamp (1.0f - c / 2.5f, 0.0f, 1.0f), snap);
        crunchMix.set (c < 2.5f ? 1.0f : 0.0f, snap);
        relA = tauCoef (sr, 0.002);   // 戻り 2 ms（速く戻すと元の速さで波形に掛け算することになり折り返しが出る。荒さは 4 倍のクリップで作る）
    }
    double sr = 48000.0;
    float k[4] { 0.5f, 0.745f, 0.95f, 0.355f };
    int look = 353, pos = 0, W = 356, head = 0, tail = 0, boxPos = 0;
    std::vector<float> buf, win;
    std::vector<int64_t> dq;
    std::vector<double> box;
    double boxSum = 353.0;
    int64_t t = 0;
    float g = 1.0f, gr = 1.0f, relA = 0.0f;
    Smooth drive, ceil, crunch, crunchMix;
    Oversampler4 os[2];
    float d4[2][4] {};
    int d4p = 0;
    ShortLim safe;
};
} // namespace aiofx
