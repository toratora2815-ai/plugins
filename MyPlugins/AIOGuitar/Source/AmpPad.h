#pragma once
// AMP より後ろにペダルを置いたとき、AMP の出口をどれだけ上げ下げするか（dB、＋は上げる）。機種 × GAIN × MASTER の表。
// ギターの DI（1 音 −21 dB RMS）を AMP に通すと出口が何 dB になるかを測って、DI と同じ大きさにそろえる量（−36〜＋12 dB）。
// AMP の出口は機種と GAIN・MASTER で DI より 12 dB 以上小さいことも、8 dB 大きいこともある。
// 自動生成: RenderTest --padtable（CAB なし、BASS / MIDDLE / TREBLE / PRESENCE は 5）。
namespace ampPad
{
    // [model][gain 0, 2.5, 5, 7.5, 10][master 0, 2.5, 5, 7.5, 10]   (DI -19.4 dB RMS)
    inline constexpr float db[5][5][5] {
        { // American Clean
            { 12.00f, 12.00f, 12.00f, 12.00f, 12.00f },
            { 12.00f, 12.00f, 10.66f, 6.75f, 2.95f },
            { 11.69f, 7.75f, 3.89f, 0.21f, -3.17f },
            { 6.30f, 2.44f, -1.25f, -4.60f, -7.43f },
            { 2.40f, -1.37f, -4.85f, -7.83f, -10.10f }
        },
        { // Tweed
            { 12.00f, 12.00f, 10.24f, 7.27f, 4.78f },
            { 8.99f, 6.06f, 3.75f, 2.09f, 1.02f },
            { 5.02f, 2.88f, 1.47f, 0.63f, 0.14f },
            { 3.39f, 1.77f, 0.77f, 0.19f, -0.12f },
            { 2.87f, 1.45f, 0.58f, 0.06f, -0.21f }
        },
        { // British Chime
            { 12.00f, 12.00f, 12.00f, 12.00f, 8.11f },
            { 12.00f, 10.54f, 6.68f, 2.99f, -0.38f },
            { 8.43f, 4.61f, 1.01f, -2.21f, -4.92f },
            { 4.25f, 0.66f, -2.56f, -5.25f, -7.33f },
            { 1.80f, -1.49f, -4.29f, -6.53f, -8.20f }
        },
        { // British Crunch
            { 12.00f, 12.00f, 12.00f, 12.00f, 12.00f },
            { 12.00f, 12.00f, 11.27f, 7.94f, 5.12f },
            { 12.00f, 10.32f, 6.92f, 4.10f, 2.01f },
            { 11.38f, 7.81f, 4.71f, 2.34f, 0.76f },
            { 10.18f, 6.76f, 3.90f, 1.78f, 0.41f }
        },
        { // Modern High Gain
            { 6.51f, 2.65f, -1.06f, -4.55f, -7.75f },
            { 2.48f, -0.84f, -3.81f, -6.53f, -9.08f },
            { 0.80f, -2.00f, -4.40f, -6.61f, -8.79f },
            { -0.01f, -2.44f, -4.46f, -6.37f, -8.35f },
            { -0.35f, -2.57f, -4.38f, -6.15f, -8.07f }
        }
    };

    inline float padDb (int model, float gain, float master)
    {
        const int m = model < 0 ? 0 : (model > 4 ? 4 : model);
        auto at = [] (float v, int& i0, float& t)
        {
            const float x = (v < 0.0f ? 0.0f : (v > 10.0f ? 10.0f : v)) / 2.5f;
            i0 = (int) x; if (i0 > 3) i0 = 3;
            t = x - (float) i0;
        };
        int g0, m0; float tg, tm;
        at (gain, g0, tg); at (master, m0, tm);
        const auto& d = db[m];
        const float a = d[g0][m0] + (d[g0][m0 + 1] - d[g0][m0]) * tm;
        const float b = d[g0 + 1][m0] + (d[g0 + 1][m0 + 1] - d[g0 + 1][m0]) * tm;
        return a + (b - a) * tg;
    }
}
