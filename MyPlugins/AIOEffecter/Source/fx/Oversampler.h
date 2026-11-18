#pragma once
// 4 倍オーバーサンプリング（歪ませる部品用）。JUCE なし。
//
// 2 倍のハーフバンドを 2 段（IIR のオールパスを 2 列に並べる形。楕円フィルターの係数）:
//   1 段目（元 → 2 倍）: 係数 8 個、通過は元の 0.46 fs まで、阻止 −99 dB
//   2 段目（2 倍 → 4 倍）: 係数 4 個、阻止 −116 dB
// 遅れは低い音で上げ下げ合わせて約 5 サンプル（位相は周波数で少し変わる）。申告はしない（元の速さに比べて小さい）。
// ドライと混ぜる部品は、ドライも同じ 4 倍の中で混ぜてから下げる（ずれない）。
// 係数は build/aiofx/os/hiir.py で計算（Laurent de Soras の方法）
namespace aiofx
{
template <int NC>
struct Polyphase2x
{
    static_assert (NC % 2 == 0, "even number of coefficients");
    const float* c = nullptr;
    float mem[NC + 2] {};
    void reset() { for (auto& m : mem) m = 0.0f; }
    // a = 0 列目の入力、b = 1 列目の入力。終わると両方とも出力
    inline void run (float& a, float& b)
    {
        for (int i = 0; i < NC; i += 2)
        {
            const float t0 = (a - mem[i + 2]) * c[i] + mem[i];
            mem[i] = a; a = t0;
            const float t1 = (b - mem[i + 3]) * c[i + 1] + mem[i + 1];
            mem[i + 1] = b; b = t1;
        }
        mem[NC] = a; mem[NC + 1] = b;
    }
    // 1 → 2
    inline void up (float x, float& o0, float& o1) { float a = x, b = x; run (a, b); o0 = a; o1 = b; }
    // 2 → 1
    inline float down (float i0, float i1) { float a = i1, b = i0; run (a, b); return 0.5f * (a + b); }
};

struct Oversampler4
{
    static constexpr int factor = 4;
    Oversampler4() { reset(); }
    void reset()
    {
        static const float c1[8] { 0.040633460924f, 0.150505129023f, 0.300757055992f, 0.460774504961f,
                                   0.609524314896f, 0.738503841119f, 0.849223810392f, 0.949742783705f };
        static const float c2[4] { 0.042454709865f, 0.170739850498f, 0.393319893190f, 0.745713588720f };
        u1.c = d1.c = c1; u2.c = d2.c = c2;
        u1.reset(); u2.reset(); d1.reset(); d2.reset();
    }
    // 元の 1 サンプル → 4 倍の 4 サンプル
    inline void up (float x, float* o)
    {
        float a, b; u1.up (x, a, b);
        u2.up (a, o[0], o[1]);
        u2.up (b, o[2], o[3]);
    }
    // 4 倍の 4 サンプル → 元の 1 サンプル
    inline float down (const float* o)
    {
        const float a = d2.down (o[0], o[1]);
        const float b = d2.down (o[2], o[3]);
        return d1.down (a, b);
    }
private:
    Polyphase2x<8> u1, d1;
    Polyphase2x<4> u2, d2;
};
} // namespace aiofx
