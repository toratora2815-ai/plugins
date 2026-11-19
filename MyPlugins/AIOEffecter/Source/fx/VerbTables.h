#pragma once
// 自動で作った表（build/aiofx/vfit/gentable_verb.py）。手で直さない。
// Guitar Rig 7 Pro の記録を、響き系の共通エンジン（VerbEngine.h）に合わせ込んだ結果
#include "VerbEngine.h"

namespace aiofx
{
inline const VerbSpec& specRaum()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.59834f, 1.0f, 0.5073f, 51.719f, 1.0f, 0.92375f, 0.14668f, 0.19229f, 0.59028f, 2.4f, 3.737f, 0.3f, -0.3f, 1.0f, 4.4116f, 1.5934f, 1.4432f, 1.9668f, 1.0128f, -15.424f, 1.0f, 0.0f, 0.0f, 8.179f, 2.3979f, 3.1761f, -0.1549f, 3.699f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 16;
        d.knobDef[0] = 0.5f;
        { auto& t = d.knobs[0]; t.points = 3; t.steps = true; t.nParams = 17;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 6; t.params[16] = 13;
          t.xs[0] = 0.0f; t.vals[0][0] = 14.105f; t.vals[0][1] = 0.40741f; t.vals[0][2] = 0.46309f; t.vals[0][3] = 27.918f; t.vals[0][4] = 0.82503f; t.vals[0][5] = 0.16777f; t.vals[0][6] = -0.042755f; t.vals[0][7] = 0.77453f; t.vals[0][8] = 3.7936f; t.vals[0][9] = 2.0174f; t.vals[0][10] = 0.82573f; t.vals[0][11] = 1.7302f; t.vals[0][12] = 4.3902f; t.vals[0][13] = 1.0083f; t.vals[0][14] = -17.801f; t.vals[0][15] = 1.0398f; t.vals[0][16] = 0.35845f;
          t.xs[1] = 0.5f; t.vals[1][0] = 8.179f; t.vals[1][1] = 0.59834f; t.vals[1][2] = 0.5073f; t.vals[1][3] = 51.719f; t.vals[1][4] = 0.92375f; t.vals[1][5] = 0.14668f; t.vals[1][6] = 0.19229f; t.vals[1][7] = 0.59028f; t.vals[1][8] = 3.737f; t.vals[1][9] = 1.5934f; t.vals[1][10] = 1.4432f; t.vals[1][11] = 1.9668f; t.vals[1][12] = 4.4116f; t.vals[1][13] = 1.0128f; t.vals[1][14] = -15.424f; t.vals[1][15] = 1.0f; t.vals[1][16] = 0.3f;
          t.xs[2] = 1.0f; t.vals[2][0] = 9.645f; t.vals[2][1] = 0.1367f; t.vals[2][2] = 0.11784f; t.vals[2][3] = 2.5793f; t.vals[2][4] = 1.2235f; t.vals[2][5] = -0.32012f; t.vals[2][6] = 0.37461f; t.vals[2][7] = -0.0090865f; t.vals[2][8] = 4.1992f; t.vals[2][9] = -1.6799f; t.vals[2][10] = 0.024966f; t.vals[2][11] = 5.4161f; t.vals[2][12] = 3.782f; t.vals[2][13] = 0.92172f; t.vals[2][14] = -21.375f; t.vals[2][15] = 6.0222f; t.vals[2][16] = 0.19494f;
        }
        d.knobDef[1] = 0.0f;
        { auto& t = d.knobs[1]; t.points = 2; t.steps = true; t.nParams = 5;
          t.params[0] = 2; t.params[1] = 4; t.params[2] = 7; t.params[3] = 8; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.7206f; t.vals[0][1] = 0.54271f; t.vals[0][2] = 0.83324f; t.vals[0][3] = 0.15248f; t.vals[0][4] = -15.572f;
          t.xs[1] = 1.0f; t.vals[1][0] = 0.84399f; t.vals[1][1] = 0.54131f; t.vals[1][2] = 0.79648f; t.vals[1][3] = 0.12156f; t.vals[1][4] = -12.948f;
        }
        d.knobDef[2] = 0.0f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 0;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 19.958f;
          t.xs[2] = 0.5f; t.vals[2][0] = 79.958f;
          t.xs[3] = 0.75f; t.vals[3][0] = 559.96f;
          t.xs[4] = 1.0f; t.vals[4][0] = 2000.0f;
        }
        d.knobDef[3] = 0.0f;
        d.knobDef[4] = 0.5f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 4; t.params[2] = 5; t.params[3] = 8; t.params[4] = 9; t.params[5] = 10; t.params[6] = 21; t.params[7] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.81036f; t.vals[0][1] = 0.38405f; t.vals[0][2] = 44.26f; t.vals[0][3] = 0.1828f; t.vals[0][4] = -0.074241f; t.vals[0][5] = -0.49562f; t.vals[0][6] = -11.234f; t.vals[0][7] = 8.6003f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.88034f; t.vals[1][1] = 0.44254f; t.vals[1][2] = 48.461f; t.vals[1][3] = 0.16357f; t.vals[1][4] = 0.11348f; t.vals[1][5] = 0.080124f; t.vals[1][6] = -13.672f; t.vals[1][7] = 8.5366f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.92375f; t.vals[2][1] = 0.5073f; t.vals[2][2] = 51.719f; t.vals[2][3] = 0.14668f; t.vals[2][4] = 0.19229f; t.vals[2][5] = 0.59028f; t.vals[2][6] = -15.424f; t.vals[2][7] = 8.179f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.92375f; t.vals[3][1] = 0.5073f; t.vals[3][2] = 51.719f; t.vals[3][3] = 0.14668f; t.vals[3][4] = 0.19229f; t.vals[3][5] = 0.59028f; t.vals[3][6] = -15.424f; t.vals[3][7] = 8.179f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.91607f; t.vals[4][1] = 0.61201f; t.vals[4][2] = 50.742f; t.vals[4][3] = 0.18201f; t.vals[4][4] = 0.024406f; t.vals[4][5] = 0.64048f; t.vals[4][6] = -16.926f; t.vals[4][7] = 8.2054f;
        }
        d.knobDef[5] = 0.25f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 5;
          t.params[0] = 2; t.params[1] = 3; t.params[2] = 4; t.params[3] = 21; t.params[4] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.59834f; t.vals[0][1] = 1.0f; t.vals[0][2] = 0.5073f; t.vals[0][3] = -15.424f; t.vals[0][4] = 8.179f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.59834f; t.vals[1][1] = 1.0f; t.vals[1][2] = 0.5073f; t.vals[1][3] = -15.424f; t.vals[1][4] = 8.179f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.59833f; t.vals[2][1] = 1.0001f; t.vals[2][2] = 0.50699f; t.vals[2][3] = -15.449f; t.vals[2][4] = 8.1783f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.59773f; t.vals[3][1] = 1.0002f; t.vals[3][2] = 0.49473f; t.vals[3][3] = -15.946f; t.vals[3][4] = 8.2213f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.41361f; t.vals[4][1] = 0.8589f; t.vals[4][2] = 0.42098f; t.vals[4][3] = -15.786f; t.vals[4][4] = 7.6123f;
        }
        d.knobDef[6] = 0.315f;
        { auto& t = d.knobs[6]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 4; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.61753f; t.vals[0][1] = 0.14991f; t.vals[0][2] = -0.58891f; t.vals[0][3] = 0.45286f; t.vals[0][4] = -17.441f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.050606f; t.vals[1][1] = 0.18533f; t.vals[1][2] = 0.3446f; t.vals[1][3] = 0.55714f; t.vals[1][4] = -16.346f;
          t.xs[2] = 0.315f; t.vals[2][0] = 0.14668f; t.vals[2][1] = 0.19229f; t.vals[2][2] = 0.59028f; t.vals[2][3] = 0.5073f; t.vals[2][4] = -15.424f;
          t.xs[3] = 0.5f; t.vals[3][0] = 0.44739f; t.vals[3][1] = 0.052755f; t.vals[3][2] = 1.2095f; t.vals[3][3] = 0.39997f; t.vals[3][4] = -12.281f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.93058f; t.vals[4][1] = -0.37987f; t.vals[4][2] = 2.0f; t.vals[4][3] = 0.21463f; t.vals[4][4] = -7.665f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.5078f; t.vals[5][1] = -1.1414f; t.vals[5][2] = 1.9922f; t.vals[5][3] = 0.060891f; t.vals[5][4] = -0.5076f;
        }
        d.knobDef[7] = 0.25f;
        { auto& t = d.knobs[7]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 10; t.params[1] = 12; t.params[2] = 21; t.params[3] = 19;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.8478f; t.vals[0][1] = 3.8858f; t.vals[0][2] = -15.319f; t.vals[0][3] = 2.2336f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.46427f; t.vals[1][1] = 3.6437f; t.vals[1][2] = -15.335f; t.vals[1][3] = 2.0959f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.34417f; t.vals[2][1] = 3.9914f; t.vals[2][2] = -15.301f; t.vals[2][3] = 1.7862f;
          t.xs[3] = 0.75f; t.vals[3][0] = -1.019f; t.vals[3][1] = 3.565f; t.vals[3][2] = -15.221f; t.vals[3][3] = 1.3006f;
          t.xs[4] = 1.0f; t.vals[4][0] = -0.85515f; t.vals[4][1] = 3.2259f; t.vals[4][2] = -15.642f; t.vals[4][3] = -1.5997f;
        }
        d.knobDef[8] = 0.0f;
        { auto& t = d.knobs[8]; t.points = 5; t.steps = false; t.nParams = 3;
          t.params[0] = 15; t.params[1] = 17; t.params[2] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.0f; t.vals[0][1] = 1.4375f; t.vals[0][2] = -15.258f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.0f; t.vals[1][1] = -3.9689f; t.vals[1][2] = -15.25f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.0f; t.vals[2][1] = -9.2567f; t.vals[2][2] = -15.247f;
          t.xs[3] = 0.75f; t.vals[3][0] = 1.0f; t.vals[3][1] = -16.37f; t.vals[3][2] = -15.268f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.0f; t.vals[4][1] = -24.678f; t.vals[4][2] = -15.298f;
        }
        d.knobDef[9] = 1.0f;
        { auto& t = d.knobs[9]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 16; t.params[1] = 19; t.params[2] = 18; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 2.9653f; t.vals[0][1] = 3.7591f; t.vals[0][2] = 3.0479f; t.vals[0][3] = -15.242f;
          t.xs[1] = 0.25f; t.vals[1][0] = 4.0078f; t.vals[1][1] = -24.253f; t.vals[1][2] = 1.4955f; t.vals[1][3] = -15.274f;
          t.xs[2] = 0.5f; t.vals[2][0] = 3.88f; t.vals[2][1] = 0.63596f; t.vals[2][2] = 1.7553f; t.vals[2][3] = -15.295f;
          t.xs[3] = 0.75f; t.vals[3][0] = 4.3334f; t.vals[3][1] = -0.2702f; t.vals[3][2] = 1.9617f; t.vals[3][3] = -15.235f;
          t.xs[4] = 1.0f; t.vals[4][0] = 4.4067f; t.vals[4][1] = 2.0759f; t.vals[4][2] = 1.4834f; t.vals[4][3] = -15.341f;
        }
        d.knobDef[10] = 0.25f;
        d.knobDef[11] = 0.25f;
        d.knobDef[12] = 1.0f;
        d.knobDef[13] = 0.195f;
        d.knobDef[14] = 0.0f;
        d.knobDef[15] = 0.0f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specVintageVerb()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.36319f, 1.0f, 0.577f, 56.237f, 1.0f, 1.1634f, 0.029889f, 0.038449f, 0.70579f, 2.4f, 3.295f, 0.3f, -0.3f, 1.0f, 4.0702f, -15.327f, 0.42843f, -5.3539f, 0.99783f, -16.328f, 1.0f, 0.0f, 0.0f, 4.0454e-07f, 2.0999f, 3.0825f, -0.43814f, 3.6484f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 2;
        d.knobDef[0] = 0.28571f;
        { auto& t = d.knobs[0]; t.points = 8; t.steps = true; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.252e-07f; t.vals[0][1] = 0.51322f; t.vals[0][2] = 0.58225f; t.vals[0][3] = 60.333f; t.vals[0][4] = 1.3516f; t.vals[0][5] = 0.084387f; t.vals[0][6] = 0.76566f; t.vals[0][7] = -0.50614f; t.vals[0][8] = 3.8547f; t.vals[0][9] = -16.691f; t.vals[0][10] = 1.4234f; t.vals[0][11] = -0.4385f; t.vals[0][12] = 4.0757f; t.vals[0][13] = 0.94388f; t.vals[0][14] = -14.632f; t.vals[0][15] = 1.834f; t.vals[0][16] = 4.1249f; t.vals[0][17] = -0.91112f; t.vals[0][18] = 3.8944f; t.vals[0][19] = 0.93422f;
          t.xs[1] = 0.14286f; t.vals[1][0] = 6.3444e-05f; t.vals[1][1] = 0.017801f; t.vals[1][2] = 0.39889f; t.vals[1][3] = 51.597f; t.vals[1][4] = 0.6892f; t.vals[1][5] = 0.51033f; t.vals[1][6] = 0.14012f; t.vals[1][7] = -1.852f; t.vals[1][8] = 3.6751f; t.vals[1][9] = -25.302f; t.vals[1][10] = -6.3221f; t.vals[1][11] = -3.8652f; t.vals[1][12] = 4.0813f; t.vals[1][13] = 1.0086f; t.vals[1][14] = -9.174f; t.vals[1][15] = 1.9082f; t.vals[1][16] = 3.0385f; t.vals[1][17] = -0.51345f; t.vals[1][18] = 3.6486f; t.vals[1][19] = 1.5101f;
          t.xs[2] = 0.28571f; t.vals[2][0] = 1.5734e-05f; t.vals[2][1] = 0.36309f; t.vals[2][2] = 0.57946f; t.vals[2][3] = 56.485f; t.vals[2][4] = 1.1634f; t.vals[2][5] = 0.028712f; t.vals[2][6] = 0.039852f; t.vals[2][7] = 0.71018f; t.vals[2][8] = 3.2935f; t.vals[2][9] = -15.309f; t.vals[2][10] = 0.49617f; t.vals[2][11] = -5.4025f; t.vals[2][12] = 4.0665f; t.vals[2][13] = 0.99456f; t.vals[2][14] = -16.367f; t.vals[2][15] = 2.0995f; t.vals[2][16] = 3.0817f; t.vals[2][17] = -0.43808f; t.vals[2][18] = 3.6485f; t.vals[2][19] = 0.99981f;
          t.xs[3] = 0.42857f; t.vals[3][0] = 0.0031225f; t.vals[3][1] = 0.10907f; t.vals[3][2] = 0.54988f; t.vals[3][3] = 45.362f; t.vals[3][4] = 1.1735f; t.vals[3][5] = 0.33604f; t.vals[3][6] = 0.4828f; t.vals[3][7] = 0.26484f; t.vals[3][8] = 2.9689f; t.vals[3][9] = -21.42f; t.vals[3][10] = -1.9595f; t.vals[3][11] = -4.6192f; t.vals[3][12] = 4.0601f; t.vals[3][13] = 0.9776f; t.vals[3][14] = -16.587f; t.vals[3][15] = 1.9705f; t.vals[3][16] = 3.9727f; t.vals[3][17] = -0.49758f; t.vals[3][18] = 3.6262f; t.vals[3][19] = 1.337f;
          t.xs[4] = 0.57143f; t.vals[4][0] = 2.2658f; t.vals[4][1] = 0.24846f; t.vals[4][2] = 0.6547f; t.vals[4][3] = 69.668f; t.vals[4][4] = 1.0868f; t.vals[4][5] = 0.32289f; t.vals[4][6] = 2.0772f; t.vals[4][7] = 0.45325f; t.vals[4][8] = 2.7557f; t.vals[4][9] = -7.666f; t.vals[4][10] = 0.83961f; t.vals[4][11] = -13.088f; t.vals[4][12] = 3.9575f; t.vals[4][13] = 0.95833f; t.vals[4][14] = -15.139f; t.vals[4][15] = 2.1874f; t.vals[4][16] = 2.9882f; t.vals[4][17] = 0.49748f; t.vals[4][18] = 3.6849f; t.vals[4][19] = 0.98084f;
          t.xs[5] = 0.71429f; t.vals[5][0] = 5.4517e-07f; t.vals[5][1] = 0.40168f; t.vals[5][2] = 0.70841f; t.vals[5][3] = 80.109f; t.vals[5][4] = 0.7493f; t.vals[5][5] = 0.47639f; t.vals[5][6] = 1.1791f; t.vals[5][7] = 0.28221f; t.vals[5][8] = 3.4008f; t.vals[5][9] = -14.8f; t.vals[5][10] = 1.3841f; t.vals[5][11] = -9.9498f; t.vals[5][12] = 4.3326f; t.vals[5][13] = 1.0036f; t.vals[5][14] = -11.051f; t.vals[5][15] = 2.1717f; t.vals[5][16] = 3.2513f; t.vals[5][17] = -0.22309f; t.vals[5][18] = 3.4989f; t.vals[5][19] = 0.96659f;
          t.xs[6] = 0.85714f; t.vals[6][0] = 22.453f; t.vals[6][1] = 0.19096f; t.vals[6][2] = 0.64083f; t.vals[6][3] = 149.6f; t.vals[6][4] = 0.67619f; t.vals[6][5] = 0.44862f; t.vals[6][6] = 4.4174e-05f; t.vals[6][7] = 0.86717f; t.vals[6][8] = 3.1416f; t.vals[6][9] = -2.8292f; t.vals[6][10] = 6.2462f; t.vals[6][11] = -6.3417f; t.vals[6][12] = 4.149f; t.vals[6][13] = 0.25669f; t.vals[6][14] = -16.798f; t.vals[6][15] = 2.221f; t.vals[6][16] = 1.8034f; t.vals[6][17] = 0.36708f; t.vals[6][18] = 3.9578f; t.vals[6][19] = 1.5233f;
          t.xs[7] = 1.0f; t.vals[7][0] = 3.3452f; t.vals[7][1] = 0.15486f; t.vals[7][2] = 1.0687f; t.vals[7][3] = 71.622f; t.vals[7][4] = 1.1801f; t.vals[7][5] = 0.39568f; t.vals[7][6] = 0.98277f; t.vals[7][7] = -0.43683f; t.vals[7][8] = 3.2435f; t.vals[7][9] = -20.566f; t.vals[7][10] = -5.5453f; t.vals[7][11] = -14.362f; t.vals[7][12] = 3.4729f; t.vals[7][13] = 0.12433f; t.vals[7][14] = -12.591f; t.vals[7][15] = 2.3504f; t.vals[7][16] = 3.9229f; t.vals[7][17] = 0.52173f; t.vals[7][18] = 3.7397f; t.vals[7][19] = 1.5446f;
        }
        d.knobDef[1] = 0.09f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specReflektor()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.0022102f, 1.0f, 0.34608f, 147.17f, 1.0f, 2.0792f, 0.016877f, 0.20591f, -0.33287f, 2.4f, 2.9304f, 0.3f, -0.3f, 1.0f, 4.2528f, 4.8456f, -18.82f, -5.0085f, 0.41313f, -9.7497f, 1.0f, 0.0f, 0.0f, 1e-06f, 2.3979f, 3.4569f, 0.89645f, 3.699f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 16;
        d.knobDef[0] = 0.265f;
        d.knobDef[1] = 1.0f;
        d.knobDef[2] = 0.3333f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 8; t.params[2] = 9; t.params[3] = 10; t.params[4] = 4; t.params[5] = 5; t.params[6] = 21; t.params[7] = 19;
          t.xs[0] = 0.0f; t.vals[0][0] = 2.2644f; t.vals[0][1] = -0.14394f; t.vals[0][2] = -0.37292f; t.vals[0][3] = -0.70785f; t.vals[0][4] = 0.42159f; t.vals[0][5] = 140.65f; t.vals[0][6] = -8.9011f; t.vals[0][7] = -4.665f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.925f; t.vals[1][1] = -0.016966f; t.vals[1][2] = 0.076688f; t.vals[1][3] = -0.3721f; t.vals[1][4] = 0.37071f; t.vals[1][5] = 128.83f; t.vals[1][6] = -9.7786f; t.vals[1][7] = -5.0299f;
          t.xs[2] = 0.3333f; t.vals[2][0] = 2.0792f; t.vals[2][1] = 0.016877f; t.vals[2][2] = 0.20591f; t.vals[2][3] = -0.33287f; t.vals[2][4] = 0.34608f; t.vals[2][5] = 147.17f; t.vals[2][6] = -9.7497f; t.vals[2][7] = -5.0085f;
          t.xs[3] = 0.5f; t.vals[3][0] = 1.5862f; t.vals[3][1] = 0.0791f; t.vals[3][2] = 0.31563f; t.vals[3][3] = -0.17231f; t.vals[3][4] = 0.37387f; t.vals[3][5] = 149.91f; t.vals[3][6] = -10.167f; t.vals[3][7] = -6.4906f;
          t.xs[4] = 0.75f; t.vals[4][0] = 1.5451f; t.vals[4][1] = 0.17306f; t.vals[4][2] = 0.31754f; t.vals[4][3] = -0.14136f; t.vals[4][4] = 0.32034f; t.vals[4][5] = 139.3f; t.vals[4][6] = -9.1541f; t.vals[4][7] = -8.2339f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.5771f; t.vals[5][1] = 0.24983f; t.vals[5][2] = 0.24451f; t.vals[5][3] = -0.066897f; t.vals[5][4] = 0.29702f; t.vals[5][5] = 153.33f; t.vals[5][6] = -8.6746f; t.vals[5][7] = -12.077f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 5; t.steps = false; t.nParams = 11;
          t.params[0] = 4; t.params[1] = 6; t.params[2] = 5; t.params[3] = 2; t.params[4] = 17; t.params[5] = 18; t.params[6] = 19; t.params[7] = 16; t.params[8] = 8; t.params[9] = 10; t.params[10] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.35705f; t.vals[0][1] = 1.0094f; t.vals[0][2] = 144.88f; t.vals[0][3] = 0.016996f; t.vals[0][4] = 4.162f; t.vals[0][5] = -19.786f; t.vals[0][6] = -5.2664f; t.vals[0][7] = 4.2497f; t.vals[0][8] = 0.013318f; t.vals[0][9] = -0.30033f; t.vals[0][10] = -9.7077f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.4404f; t.vals[1][1] = 0.72379f; t.vals[1][2] = 180.85f; t.vals[1][3] = 0.092886f; t.vals[1][4] = 12.474f; t.vals[1][5] = -29.698f; t.vals[1][6] = -9.8953f; t.vals[1][7] = 4.0052f; t.vals[1][8] = 0.048579f; t.vals[1][9] = -0.44381f; t.vals[1][10] = -11.012f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.36105f; t.vals[2][1] = 0.80559f; t.vals[2][2] = 193.96f; t.vals[2][3] = 0.13893f; t.vals[2][4] = 23.248f; t.vals[2][5] = -29.489f; t.vals[2][6] = -14.439f; t.vals[2][7] = 3.9696f; t.vals[2][8] = 0.026523f; t.vals[2][9] = -0.18291f; t.vals[2][10] = -18.556f;
          t.xs[3] = 0.75f; t.vals[3][0] = 1.1536f; t.vals[3][1] = 1.3852f; t.vals[3][2] = 179.47f; t.vals[3][3] = 0.35816f; t.vals[3][4] = 24.0f; t.vals[3][5] = -30.0f; t.vals[3][6] = -27.01f; t.vals[3][7] = 4.3477f; t.vals[3][8] = -0.082569f; t.vals[3][9] = 0.2497f; t.vals[3][10] = -27.218f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.29333f; t.vals[4][1] = 0.88181f; t.vals[4][2] = 184.17f; t.vals[4][3] = 0.0021256f; t.vals[4][4] = 21.305f; t.vals[4][5] = -29.999f; t.vals[4][6] = -16.969f; t.vals[4][7] = 4.3819f; t.vals[4][8] = -0.35668f; t.vals[4][9] = 0.12351f; t.vals[4][10] = -15.419f;
        }
        d.knobDef[4] = 0.6644f;
        { auto& t = d.knobs[4]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 4; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.23795f; t.vals[0][1] = -1.1793f; t.vals[0][2] = -1.0711f; t.vals[0][3] = 0.32f; t.vals[0][4] = -5.959f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.26844f; t.vals[1][1] = -0.52129f; t.vals[1][2] = -0.44015f; t.vals[1][3] = 0.47911f; t.vals[1][4] = -9.6946f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.1107f; t.vals[2][1] = -0.20429f; t.vals[2][2] = -0.43024f; t.vals[2][3] = 0.37571f; t.vals[2][4] = -9.1208f;
          t.xs[3] = 0.6644f; t.vals[3][0] = 0.016877f; t.vals[3][1] = 0.20591f; t.vals[3][2] = -0.33287f; t.vals[3][3] = 0.34608f; t.vals[3][4] = -9.7497f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.034451f; t.vals[4][1] = 0.1687f; t.vals[4][2] = -0.30466f; t.vals[4][3] = 0.34542f; t.vals[4][4] = -9.8566f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.076948f; t.vals[5][1] = 0.4268f; t.vals[5][2] = -0.24295f; t.vals[5][3] = 0.33757f; t.vals[5][4] = -10.175f;
        }
        d.knobDef[5] = 0.5f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 5;
          t.params[0] = 25; t.params[1] = 4; t.params[2] = 5; t.params[3] = 8; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1e-06f; t.vals[0][1] = 0.39777f; t.vals[0][2] = 146.94f; t.vals[0][3] = -0.046855f; t.vals[0][4] = -9.8572f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1e-06f; t.vals[1][1] = 0.38307f; t.vals[1][2] = 146.94f; t.vals[1][3] = -0.013144f; t.vals[1][4] = -9.831f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1e-06f; t.vals[2][1] = 0.34629f; t.vals[2][2] = 147.15f; t.vals[2][3] = 0.017026f; t.vals[2][4] = -9.7506f;
          t.xs[3] = 0.75f; t.vals[3][0] = 1e-06f; t.vals[3][1] = 0.26877f; t.vals[3][2] = 143.53f; t.vals[3][3] = 0.016297f; t.vals[3][4] = -10.27f;
          t.xs[4] = 1.0f; t.vals[4][0] = 17.146f; t.vals[4][1] = 0.34608f; t.vals[4][2] = 147.17f; t.vals[4][3] = 0.016877f; t.vals[4][4] = -12.386f;
        }
        d.knobDef[6] = 0.0f;
        { auto& t = d.knobs[6]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 0;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 32.062f;
          t.xs[2] = 0.5f; t.vals[2][0] = 119.21f;
          t.xs[3] = 0.75f; t.vals[3][0] = 356.06f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1000.0f;
        }
        d.knobDef[7] = 0.0f;
        d.knobDef[8] = 0.5f;
        { auto& t = d.knobs[8]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 17; t.params[1] = 26; t.params[2] = 9; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 4.3683f; t.vals[0][1] = 2.6877f; t.vals[0][2] = 0.17812f; t.vals[0][3] = -9.9711f;
          t.xs[1] = 0.25f; t.vals[1][0] = 3.8741f; t.vals[1][1] = 2.6723f; t.vals[1][2] = 0.22755f; t.vals[1][3] = -9.8771f;
          t.xs[2] = 0.5f; t.vals[2][0] = 4.2132f; t.vals[2][1] = 2.4222f; t.vals[2][2] = 0.19514f; t.vals[2][3] = -9.6798f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.9383f; t.vals[3][1] = 2.2032f; t.vals[3][2] = 0.86006f; t.vals[3][3] = -9.7624f;
          t.xs[4] = 1.0f; t.vals[4][0] = 3.0314f; t.vals[4][1] = 2.1618f; t.vals[4][2] = 1.5566f; t.vals[4][3] = -9.9223f;
        }
        d.knobDef[9] = 0.7686f;
        d.knobDef[10] = 0.5f;
        { auto& t = d.knobs[10]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 18; t.params[1] = 27; t.params[2] = 28; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -11.809f; t.vals[0][1] = 2.8282f; t.vals[0][2] = -0.17762f; t.vals[0][3] = -10.203f;
          t.xs[1] = 0.25f; t.vals[1][0] = -4.659f; t.vals[1][1] = 2.9039f; t.vals[1][2] = -0.45156f; t.vals[1][3] = -9.8157f;
          t.xs[2] = 0.5f; t.vals[2][0] = -20.799f; t.vals[2][1] = 3.457f; t.vals[2][2] = 0.94773f; t.vals[2][3] = -9.7496f;
          t.xs[3] = 0.75f; t.vals[3][0] = -19.907f; t.vals[3][1] = 3.5154f; t.vals[3][2] = 0.72089f; t.vals[3][3] = -7.304f;
          t.xs[4] = 1.0f; t.vals[4][0] = -26.018f; t.vals[4][1] = 3.7709f; t.vals[4][2] = 0.47281f; t.vals[4][3] = -1.3237f;
        }
        d.knobDef[11] = 0.5f;
        d.knobDef[12] = 0.5f;
        d.knobDef[13] = 0.5f;
        { auto& t = d.knobs[13]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 19; t.params[1] = 29; t.params[2] = 10; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -13.229f; t.vals[0][1] = 3.5091f; t.vals[0][2] = -0.28136f; t.vals[0][3] = -6.7031f;
          t.xs[1] = 0.25f; t.vals[1][0] = -9.5811f; t.vals[1][1] = 3.5105f; t.vals[1][2] = -0.38353f; t.vals[1][3] = -7.4149f;
          t.xs[2] = 0.5f; t.vals[2][0] = -4.9375f; t.vals[2][1] = 3.7148f; t.vals[2][2] = -0.30889f; t.vals[2][3] = -9.8843f;
          t.xs[3] = 0.75f; t.vals[3][0] = -4.5029f; t.vals[3][1] = 3.77f; t.vals[3][2] = 0.071301f; t.vals[3][3] = -11.132f;
          t.xs[4] = 1.0f; t.vals[4][0] = -4.3719f; t.vals[4][1] = 3.7822f; t.vals[4][2] = 0.45665f; t.vals[4][3] = -12.132f;
        }
        d.knobDef[14] = 0.4215f;
        d.knobDef[15] = 0.5985f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specLittleReflektor()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.42449f, 1.0f, 0.35268f, 100.12f, 1.0f, 1.87f, 0.41969f, -0.097325f, -0.49081f, 2.4f, 3.4339f, 0.3f, -0.3f, 1.0f, 4.49f, 0.98143f, -0.42834f, -2.9731f, 0.96376f, -7.2798f, 1.0f, 0.0f, 0.0f, 1e-06f, 2.1595f, 3.35f, -0.1128f, 3.5328f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 4;
        d.knobDef[0] = 0.57143f;
        { auto& t = d.knobs[0]; t.points = 8; t.steps = true; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 1e-06f; t.vals[0][1] = 0.50991f; t.vals[0][2] = 0.41069f; t.vals[0][3] = 116.18f; t.vals[0][4] = 1.9757f; t.vals[0][5] = -0.41098f; t.vals[0][6] = 0.077925f; t.vals[0][7] = -1.1105f; t.vals[0][8] = 3.2747f; t.vals[0][9] = 5.5102f; t.vals[0][10] = -1.0686f; t.vals[0][11] = -1.8348f; t.vals[0][12] = 4.49f; t.vals[0][13] = 0.56394f; t.vals[0][14] = -6.0412f; t.vals[0][15] = 1.8551f; t.vals[0][16] = 3.4716f; t.vals[0][17] = 0.2127f; t.vals[0][18] = 3.7472f; t.vals[0][19] = 1.3202f;
          t.xs[1] = 0.14286f; t.vals[1][0] = 1e-06f; t.vals[1][1] = 0.73618f; t.vals[1][2] = 0.64866f; t.vals[1][3] = 97.157f; t.vals[1][4] = 2.1031f; t.vals[1][5] = -0.1328f; t.vals[1][6] = -0.11634f; t.vals[1][7] = -0.047284f; t.vals[1][8] = 3.1323f; t.vals[1][9] = 1.0453f; t.vals[1][10] = 0.58477f; t.vals[1][11] = -0.68814f; t.vals[1][12] = 4.49f; t.vals[1][13] = 1.0678f; t.vals[1][14] = -11.406f; t.vals[1][15] = 2.0733f; t.vals[1][16] = 3.0906f; t.vals[1][17] = 0.35787f; t.vals[1][18] = 3.8434f; t.vals[1][19] = 0.9659f;
          t.xs[2] = 0.28571f; t.vals[2][0] = 0.0017483f; t.vals[2][1] = 0.38867f; t.vals[2][2] = 0.53867f; t.vals[2][3] = 90.44f; t.vals[2][4] = 1.8223f; t.vals[2][5] = 0.13479f; t.vals[2][6] = 0.064112f; t.vals[2][7] = -1.1054f; t.vals[2][8] = 3.535f; t.vals[2][9] = -9.8349f; t.vals[2][10] = -0.030437f; t.vals[2][11] = 2.0269f; t.vals[2][12] = 4.2184f; t.vals[2][13] = 0.9664f; t.vals[2][14] = -10.704f; t.vals[2][15] = 1.9932f; t.vals[2][16] = 3.7405f; t.vals[2][17] = -0.26217f; t.vals[2][18] = 3.927f; t.vals[2][19] = 0.9756f;
          t.xs[3] = 0.42857f; t.vals[3][0] = 1e-06f; t.vals[3][1] = 0.48793f; t.vals[3][2] = 0.35397f; t.vals[3][3] = 118.73f; t.vals[3][4] = 1.7525f; t.vals[3][5] = 0.24021f; t.vals[3][6] = 0.064847f; t.vals[3][7] = -0.70592f; t.vals[3][8] = 3.5189f; t.vals[3][9] = 3.716f; t.vals[3][10] = 0.39437f; t.vals[3][11] = -8.8849f; t.vals[3][12] = 4.49f; t.vals[3][13] = 0.46635f; t.vals[3][14] = -7.195f; t.vals[3][15] = 2.0891f; t.vals[3][16] = 3.1704f; t.vals[3][17] = -0.97485f; t.vals[3][18] = 3.9015f; t.vals[3][19] = 1.048f;
          t.xs[4] = 0.57143f; t.vals[4][0] = 1e-06f; t.vals[4][1] = 0.42449f; t.vals[4][2] = 0.35268f; t.vals[4][3] = 100.12f; t.vals[4][4] = 1.87f; t.vals[4][5] = 0.41969f; t.vals[4][6] = -0.097325f; t.vals[4][7] = -0.49081f; t.vals[4][8] = 3.4339f; t.vals[4][9] = 0.98143f; t.vals[4][10] = -0.42834f; t.vals[4][11] = -2.9731f; t.vals[4][12] = 4.49f; t.vals[4][13] = 0.96376f; t.vals[4][14] = -7.2649f; t.vals[4][15] = 2.1595f; t.vals[4][16] = 3.35f; t.vals[4][17] = -0.1128f; t.vals[4][18] = 3.5328f; t.vals[4][19] = 1.0f;
          t.xs[5] = 0.71429f; t.vals[5][0] = 1e-06f; t.vals[5][1] = 0.70168f; t.vals[5][2] = 0.44977f; t.vals[5][3] = 134.35f; t.vals[5][4] = 1.85f; t.vals[5][5] = 0.17522f; t.vals[5][6] = -0.29045f; t.vals[5][7] = 0.16476f; t.vals[5][8] = 3.1005f; t.vals[5][9] = -0.0090646f; t.vals[5][10] = -4.0479f; t.vals[5][11] = 4.164f; t.vals[5][12] = 4.3724f; t.vals[5][13] = 0.94119f; t.vals[5][14] = -8.213f; t.vals[5][15] = 2.1962f; t.vals[5][16] = 3.7615f; t.vals[5][17] = -0.089323f; t.vals[5][18] = 3.4592f; t.vals[5][19] = 0.98676f;
          t.xs[6] = 0.85714f; t.vals[6][0] = 1e-06f; t.vals[6][1] = 0.47949f; t.vals[6][2] = 0.30424f; t.vals[6][3] = 103.83f; t.vals[6][4] = 1.8875f; t.vals[6][5] = 0.74675f; t.vals[6][6] = 0.11995f; t.vals[6][7] = -0.80047f; t.vals[6][8] = 3.4174f; t.vals[6][9] = 3.507f; t.vals[6][10] = -5.9554f; t.vals[6][11] = -11.6f; t.vals[6][12] = 4.4377f; t.vals[6][13] = 0.41081f; t.vals[6][14] = -11.025f; t.vals[6][15] = 2.1993f; t.vals[6][16] = 3.469f; t.vals[6][17] = 0.52215f; t.vals[6][18] = 3.8943f; t.vals[6][19] = 2.2231f;
          t.xs[7] = 1.0f; t.vals[7][0] = 5.3765f; t.vals[7][1] = 0.35044f; t.vals[7][2] = 0.45174f; t.vals[7][3] = 97.634f; t.vals[7][4] = 2.3589f; t.vals[7][5] = 1.0397f; t.vals[7][6] = 0.15615f; t.vals[7][7] = 1.3325f; t.vals[7][8] = 3.309f; t.vals[7][9] = 2.5137f; t.vals[7][10] = -1.4603f; t.vals[7][11] = -14.046f; t.vals[7][12] = 4.49f; t.vals[7][13] = 0.94901f; t.vals[7][14] = -6.3134f; t.vals[7][15] = 1.9962f; t.vals[7][16] = 3.2699f; t.vals[7][17] = -0.10519f; t.vals[7][18] = 3.5724f; t.vals[7][19] = 2.0771f;
        }
        d.knobDef[1] = 0.6144f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 4; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.2133f; t.vals[0][1] = 0.10483f; t.vals[0][2] = -0.1505f; t.vals[0][3] = 0.61438f; t.vals[0][4] = -8.9865f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.050876f; t.vals[1][1] = 0.032797f; t.vals[1][2] = -0.13128f; t.vals[1][3] = 0.56268f; t.vals[1][4] = -8.9373f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.19802f; t.vals[2][1] = -0.004694f; t.vals[2][2] = -0.28252f; t.vals[2][3] = 0.43588f; t.vals[2][4] = -8.0654f;
          t.xs[3] = 0.6144f; t.vals[3][0] = 0.41969f; t.vals[3][1] = -0.097325f; t.vals[3][2] = -0.49081f; t.vals[3][3] = 0.35268f; t.vals[3][4] = -7.2798f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.58974f; t.vals[4][1] = -0.029703f; t.vals[4][2] = -0.55543f; t.vals[4][3] = 0.28908f; t.vals[4][4] = -6.6865f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.7311f; t.vals[5][1] = 0.062277f; t.vals[5][2] = -0.45695f; t.vals[5][3] = 0.24529f; t.vals[5][4] = -6.3879f;
        }
        d.knobDef[2] = 0.155f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 4;
          t.params[0] = 15; t.params[1] = 17; t.params[2] = 26; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.0f; t.vals[0][1] = 0.68102f; t.vals[0][2] = 2.1581f; t.vals[0][3] = -7.2442f;
          t.xs[1] = 0.155f; t.vals[1][0] = 1.0f; t.vals[1][1] = 0.98143f; t.vals[1][2] = 2.1595f; t.vals[1][3] = -7.2798f;
          t.xs[2] = 0.25f; t.vals[2][0] = 1.0f; t.vals[2][1] = 1.149f; t.vals[2][2] = 2.1606f; t.vals[2][3] = -7.2831f;
          t.xs[3] = 0.5f; t.vals[3][0] = 1.0f; t.vals[3][1] = -29.993f; t.vals[3][2] = 1.6274f; t.vals[3][3] = -7.1672f;
          t.xs[4] = 0.75f; t.vals[4][0] = 1.0f; t.vals[4][1] = -30.0f; t.vals[4][2] = 2.0727f; t.vals[4][3] = -6.9991f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.0f; t.vals[5][1] = -30.0f; t.vals[5][2] = 2.5067f; t.vals[5][3] = -6.641f;
        }
        d.knobDef[3] = 0.2204f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specTraktorReverb()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.49579f, 1.0f, 0.16761f, 36.436f, 1.0f, 0.82233f, 0.7138f, -1.2116f, -1.3447f, 2.4f, 3.179f, 0.3f, -0.3f, 1.0f, 4.4936f, -6.8556f, -2.008f, 1.0839f, 0.73057f, 1.4359f, 1.0f, 0.0f, 0.0f, 0.0089085f, 2.3979f, 3.1761f, -0.1549f, 3.699f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 5;
        d.knobDef[0] = 0.5f;
        { auto& t = d.knobs[0]; t.points = 5; t.steps = false; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 8; t.params[2] = 9; t.params[3] = 10; t.params[4] = 4; t.params[5] = 5; t.params[6] = 21; t.params[7] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.75853f; t.vals[0][1] = -0.13667f; t.vals[0][2] = -0.77371f; t.vals[0][3] = -0.58079f; t.vals[0][4] = 0.32907f; t.vals[0][5] = 112.27f; t.vals[0][6] = -2.4541f; t.vals[0][7] = 0.011849f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.2242f; t.vals[1][1] = 0.11963f; t.vals[1][2] = -0.93805f; t.vals[1][3] = -0.80527f; t.vals[1][4] = 0.31846f; t.vals[1][5] = 71.171f; t.vals[1][6] = -1.6056f; t.vals[1][7] = 0.43089f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.82121f; t.vals[2][1] = 0.71348f; t.vals[2][2] = -1.212f; t.vals[2][3] = -1.3448f; t.vals[2][4] = 0.16678f; t.vals[2][5] = 36.997f; t.vals[2][6] = 1.4794f; t.vals[2][7] = 2.1074e-07f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.6792f; t.vals[3][1] = 0.88817f; t.vals[3][2] = -0.87251f; t.vals[3][3] = -0.89879f; t.vals[3][4] = 0.1162f; t.vals[3][5] = 34.297f; t.vals[3][6] = 2.1162f; t.vals[3][7] = 0.0031127f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.71399f; t.vals[4][1] = 1.0371f; t.vals[4][2] = -0.72465f; t.vals[4][3] = -0.37655f; t.vals[4][4] = 0.060564f; t.vals[4][5] = 37.055f; t.vals[4][6] = 3.4765f; t.vals[4][7] = 0.0024897f;
        }
        d.knobDef[1] = 0.3644f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 7;
          t.params[0] = 16; t.params[1] = 19; t.params[2] = 29; t.params[3] = 10; t.params[4] = 12; t.params[5] = 21; t.params[6] = 4;
          t.xs[0] = 0.0f; t.vals[0][0] = 4.4936f; t.vals[0][1] = -5.8101f; t.vals[0][2] = 2.6401f; t.vals[0][3] = -1.9686f; t.vals[0][4] = 2.7f; t.vals[0][5] = 0.73631f; t.vals[0][6] = 0.17021f;
          t.xs[1] = 0.25f; t.vals[1][0] = 4.4936f; t.vals[1][1] = -1.3527f; t.vals[1][2] = 2.9735f; t.vals[1][3] = -1.5677f; t.vals[1][4] = 3.0056f; t.vals[1][5] = 1.1583f; t.vals[1][6] = 0.18795f;
          t.xs[2] = 0.3644f; t.vals[2][0] = 4.4936f; t.vals[2][1] = 1.0839f; t.vals[2][2] = 3.699f; t.vals[2][3] = -1.3447f; t.vals[2][4] = 3.179f; t.vals[2][5] = 1.4359f; t.vals[2][6] = 0.16761f;
          t.xs[3] = 0.5f; t.vals[3][0] = 4.4936f; t.vals[3][1] = 2.5599f; t.vals[3][2] = 2.8105f; t.vals[3][3] = -0.84454f; t.vals[3][4] = 3.2179f; t.vals[3][5] = 1.2213f; t.vals[3][6] = 0.1843f;
          t.xs[4] = 0.75f; t.vals[4][0] = 4.4936f; t.vals[4][1] = 8.3935f; t.vals[4][2] = 3.9569f; t.vals[4][3] = 1.9984f; t.vals[4][4] = 3.8234f; t.vals[4][5] = 2.7346f; t.vals[4][6] = 0.075559f;
          t.xs[5] = 1.0f; t.vals[5][0] = 4.4936f; t.vals[5][1] = 10.165f; t.vals[5][2] = 3.955f; t.vals[5][3] = 1.9986f; t.vals[5][4] = 3.667f; t.vals[5][5] = 2.8387f; t.vals[5][6] = 0.020168f;
        }
        d.knobDef[2] = 0.6676f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 15; t.params[1] = 17; t.params[2] = 26; t.params[3] = 9; t.params[4] = 11; t.params[5] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.0f; t.vals[0][1] = -0.56219f; t.vals[0][2] = 2.1356f; t.vals[0][3] = 0.51163f; t.vals[0][4] = 2.6562f; t.vals[0][5] = 1.4432f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.0f; t.vals[1][1] = -2.2702f; t.vals[1][2] = 2.1129f; t.vals[1][3] = 0.17547f; t.vals[1][4] = 2.9397f; t.vals[1][5] = 1.4343f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.0f; t.vals[2][1] = -3.2677f; t.vals[2][2] = 2.3226f; t.vals[2][3] = -1.782f; t.vals[2][4] = 1.9373f; t.vals[2][5] = 1.4027f;
          t.xs[3] = 0.6676f; t.vals[3][0] = 1.0f; t.vals[3][1] = -6.8556f; t.vals[3][2] = 2.3979f; t.vals[3][3] = -1.2116f; t.vals[3][4] = 2.4f; t.vals[3][5] = 1.4359f;
          t.xs[4] = 0.75f; t.vals[4][0] = 1.0f; t.vals[4][1] = -4.4927f; t.vals[4][2] = 2.4689f; t.vals[4][3] = -3.1089f; t.vals[4][4] = 2.0279f; t.vals[4][5] = 1.3333f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.0f; t.vals[5][1] = -4.7058f; t.vals[5][2] = 2.6565f; t.vals[5][3] = -3.6888f; t.vals[5][4] = 2.1596f; t.vals[5][5] = 1.2695f;
        }
        d.knobDef[3] = 0.2686f;
        d.knobDef[4] = 0.0f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specRC24()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.74832f, 1.0f, 0.50387f, 28.563f, 1.0f, 0.52664f, -0.14093f, 0.15301f, 0.46125f, 2.4f, 2.9366f, 0.3f, -0.3f, 1.0f, 3.9947f, 2.2124f, 0.98678f, 6.5817f, 0.96056f, -12.24f, 1.0f, 0.0f, 0.0f, 29.891f, 2.3982f, 3.1732f, -0.1547f, 3.7f, 0.0f, 1850.0f, 3.8684f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 14;
        d.knobDef[0] = 0.5f;
        { auto& t = d.knobs[0]; t.points = 3; t.steps = true; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 32;
          t.xs[0] = 0.0f; t.vals[0][0] = 35.891f; t.vals[0][1] = 0.83191f; t.vals[0][2] = 0.5196f; t.vals[0][3] = 29.054f; t.vals[0][4] = 0.54892f; t.vals[0][5] = -0.16937f; t.vals[0][6] = 0.54746f; t.vals[0][7] = 0.57583f; t.vals[0][8] = 2.9578f; t.vals[0][9] = 1.6889f; t.vals[0][10] = 3.225f; t.vals[0][11] = 9.3184f; t.vals[0][12] = 3.9507f; t.vals[0][13] = 0.98088f; t.vals[0][14] = -13.292f; t.vals[0][15] = 2.2919f; t.vals[0][16] = 3.1441f; t.vals[0][17] = 0.17957f; t.vals[0][18] = 3.7243f; t.vals[0][19] = 3.8598f;
          t.xs[1] = 0.5f; t.vals[1][0] = 29.891f; t.vals[1][1] = 0.74832f; t.vals[1][2] = 0.50387f; t.vals[1][3] = 28.563f; t.vals[1][4] = 0.52664f; t.vals[1][5] = -0.14093f; t.vals[1][6] = 0.15301f; t.vals[1][7] = 0.46125f; t.vals[1][8] = 2.9366f; t.vals[1][9] = 2.2124f; t.vals[1][10] = 0.98678f; t.vals[1][11] = 6.5817f; t.vals[1][12] = 3.9947f; t.vals[1][13] = 0.96056f; t.vals[1][14] = -12.237f; t.vals[1][15] = 2.3982f; t.vals[1][16] = 3.1732f; t.vals[1][17] = -0.1547f; t.vals[1][18] = 3.7f; t.vals[1][19] = 3.8684f;
          t.xs[2] = 1.0f; t.vals[2][0] = 51.067f; t.vals[2][1] = 0.54389f; t.vals[2][2] = 0.54801f; t.vals[2][3] = 33.166f; t.vals[2][4] = 0.49808f; t.vals[2][5] = -0.11433f; t.vals[2][6] = 0.092211f; t.vals[2][7] = 0.12682f; t.vals[2][8] = 2.7434f; t.vals[2][9] = 2.1335f; t.vals[2][10] = 1.54f; t.vals[2][11] = 7.4719f; t.vals[2][12] = 3.9621f; t.vals[2][13] = 0.9843f; t.vals[2][14] = -12.005f; t.vals[2][15] = 2.157f; t.vals[2][16] = 2.9482f; t.vals[2][17] = 0.34164f; t.vals[2][18] = 3.6841f; t.vals[2][19] = 3.865f;
        }
        d.knobDef[1] = 0.2018f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 0; t.params[1] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f; t.vals[0][1] = 4.0371f;
          t.xs[1] = 0.2018f; t.vals[1][0] = 0.0f; t.vals[1][1] = 29.891f;
          t.xs[2] = 0.25f; t.vals[2][0] = 6.1667f; t.vals[2][1] = 29.891f;
          t.xs[3] = 0.5f; t.vals[3][0] = 38.167f; t.vals[3][1] = 29.891f;
          t.xs[4] = 0.75f; t.vals[4][0] = 70.167f; t.vals[4][1] = 29.891f;
          t.xs[5] = 1.0f; t.vals[5][0] = 102.17f; t.vals[5][1] = 29.891f;
        }
        d.knobDef[2] = 0.5179f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 4; t.params[1] = 5; t.params[2] = 25; t.params[3] = 2; t.params[4] = 21; t.params[5] = 8;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.62109f; t.vals[0][1] = 29.921f; t.vals[0][2] = 30.489f; t.vals[0][3] = 0.84859f; t.vals[0][4] = -12.545f; t.vals[0][5] = -0.15461f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.59745f; t.vals[1][1] = 29.28f; t.vals[1][2] = 30.302f; t.vals[1][3] = 0.84304f; t.vals[1][4] = -12.03f; t.vals[1][5] = -0.15399f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.50766f; t.vals[2][1] = 29.322f; t.vals[2][2] = 29.914f; t.vals[2][3] = 0.82861f; t.vals[2][4] = -12.168f; t.vals[2][5] = -0.13907f;
          t.xs[3] = 0.5179f; t.vals[3][0] = 0.50387f; t.vals[3][1] = 28.563f; t.vals[3][2] = 29.891f; t.vals[3][3] = 0.74832f; t.vals[3][4] = -12.24f; t.vals[3][5] = -0.14093f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.054728f; t.vals[4][1] = 13.714f; t.vals[4][2] = 4.7733f; t.vals[4][3] = 0.61167f; t.vals[4][4] = -11.418f; t.vals[4][5] = -0.15245f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.030506f; t.vals[5][1] = 77.647f; t.vals[5][2] = 99.348f; t.vals[5][3] = 0.11536f; t.vals[5][4] = -11.09f; t.vals[5][5] = -0.090326f;
        }
        d.knobDef[3] = 0.4551f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 4; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.18106f; t.vals[0][1] = 0.32727f; t.vals[0][2] = 0.6271f; t.vals[0][3] = 0.53454f; t.vals[0][4] = -12.702f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.15348f; t.vals[1][1] = 0.19905f; t.vals[1][2] = 0.52352f; t.vals[1][3] = 0.51065f; t.vals[1][4] = -12.377f;
          t.xs[2] = 0.4551f; t.vals[2][0] = -0.14093f; t.vals[2][1] = 0.15301f; t.vals[2][2] = 0.46125f; t.vals[2][3] = 0.50387f; t.vals[2][4] = -12.24f;
          t.xs[3] = 0.5f; t.vals[3][0] = -0.15252f; t.vals[3][1] = 0.20936f; t.vals[3][2] = 0.51635f; t.vals[3][3] = 0.51347f; t.vals[3][4] = -12.382f;
          t.xs[4] = 0.75f; t.vals[4][0] = -0.15831f; t.vals[4][1] = 0.16406f; t.vals[4][2] = 0.53018f; t.vals[4][3] = 0.50485f; t.vals[4][4] = -12.355f;
          t.xs[5] = 1.0f; t.vals[5][0] = -0.18716f; t.vals[5][1] = 0.2837f; t.vals[5][2] = 0.64342f; t.vals[5][3] = 0.52031f; t.vals[5][4] = -12.647f;
        }
        d.knobDef[4] = 1.0f;
        d.knobDef[5] = 0.0f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 5;
          t.params[0] = 9; t.params[1] = 11; t.params[2] = 17; t.params[3] = 26; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.43311f; t.vals[0][1] = 1.9661f; t.vals[0][2] = 1.8648f; t.vals[0][3] = 2.4829f; t.vals[0][4] = -12.24f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.78372f; t.vals[1][1] = 3.0057f; t.vals[1][2] = 6.2239f; t.vals[1][3] = 1.5495f; t.vals[1][4] = -12.485f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.8404f; t.vals[2][1] = 3.1452f; t.vals[2][2] = -2.1549f; t.vals[2][3] = 3.3573f; t.vals[2][4] = -12.294f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.5097f; t.vals[3][1] = 3.2076f; t.vals[3][2] = 0.50949f; t.vals[3][3] = 1.768f; t.vals[3][4] = -13.712f;
          t.xs[4] = 1.0f; t.vals[4][0] = 4.0f; t.vals[4][1] = 3.2509f; t.vals[4][2] = 5.1943f; t.vals[4][3] = 2.2443f; t.vals[4][4] = -13.748f;
        }
        d.knobDef[6] = 0.1783f;
        { auto& t = d.knobs[6]; t.points = 6; t.steps = false; t.nParams = 4;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.1937f; t.vals[0][1] = 0.17189f; t.vals[0][2] = 0.15668f; t.vals[0][3] = -11.761f;
          t.xs[1] = 0.1783f; t.vals[1][0] = -0.14093f; t.vals[1][1] = 0.15301f; t.vals[1][2] = 0.46125f; t.vals[1][3] = -12.24f;
          t.xs[2] = 0.25f; t.vals[2][0] = -0.1196f; t.vals[2][1] = 0.1391f; t.vals[2][2] = 0.55562f; t.vals[2][3] = -12.363f;
          t.xs[3] = 0.5f; t.vals[3][0] = 0.21573f; t.vals[3][1] = -0.37309f; t.vals[3][2] = 1.62f; t.vals[3][3] = -12.214f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.81987f; t.vals[4][1] = -2.098f; t.vals[4][2] = 2.0f; t.vals[4][3] = -7.8504f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.3185f; t.vals[5][1] = -2.9013f; t.vals[5][2] = 2.0f; t.vals[5][3] = -3.4493f;
        }
        d.knobDef[7] = 0.6078f;
        { auto& t = d.knobs[7]; t.points = 6; t.steps = false; t.nParams = 3;
          t.params[0] = 11; t.params[1] = 9; t.params[2] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.8365f; t.vals[0][1] = 0.36886f; t.vals[0][2] = -12.209f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.8125f; t.vals[1][1] = 0.40339f; t.vals[1][2] = -12.225f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.9807f; t.vals[2][1] = 0.27874f; t.vals[2][2] = -12.226f;
          t.xs[3] = 0.6078f; t.vals[3][0] = 2.4f; t.vals[3][1] = 0.15301f; t.vals[3][2] = -12.24f;
          t.xs[4] = 0.75f; t.vals[4][0] = 2.8348f; t.vals[4][1] = 0.28694f; t.vals[4][2] = -12.301f;
          t.xs[5] = 1.0f; t.vals[5][0] = 2.8221f; t.vals[5][1] = 0.32384f; t.vals[5][2] = -12.308f;
        }
        d.knobDef[8] = 0.7668f;
        { auto& t = d.knobs[8]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 16; t.params[1] = 10; t.params[2] = 12; t.params[3] = 19; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 3.9099f; t.vals[0][1] = -0.086408f; t.vals[0][2] = 2.7f; t.vals[0][3] = 8.8253f; t.vals[0][4] = -11.88f;
          t.xs[1] = 0.25f; t.vals[1][0] = 3.9098f; t.vals[1][1] = -0.082551f; t.vals[1][2] = 2.7f; t.vals[1][3] = 8.8177f; t.vals[1][4] = -11.883f;
          t.xs[2] = 0.5f; t.vals[2][0] = 3.9509f; t.vals[2][1] = 0.16387f; t.vals[2][2] = 2.7001f; t.vals[2][3] = 7.7785f; t.vals[2][4] = -12.104f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.9902f; t.vals[3][1] = 0.43783f; t.vals[3][2] = 2.9082f; t.vals[3][3] = 6.7245f; t.vals[3][4] = -12.248f;
          t.xs[4] = 0.7668f; t.vals[4][0] = 3.9947f; t.vals[4][1] = 0.46125f; t.vals[4][2] = 2.9366f; t.vals[4][3] = 6.5817f; t.vals[4][4] = -12.24f;
          t.xs[5] = 1.0f; t.vals[5][0] = 4.0026f; t.vals[5][1] = 0.53822f; t.vals[5][2] = 3.0202f; t.vals[5][3] = 6.3408f; t.vals[5][4] = -12.249f;
        }
        d.knobDef[9] = 1.0f;
        { auto& t = d.knobs[9]; t.points = 5; t.steps = false; t.nParams = 6;
          t.params[0] = 17; t.params[1] = 18; t.params[2] = 19; t.params[3] = 29; t.params[4] = 10; t.params[5] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 2.4768f; t.vals[0][1] = 1.8176f; t.vals[0][2] = 4.6406f; t.vals[0][3] = 3.737f; t.vals[0][4] = 0.32991f; t.vals[0][5] = -12.72f;
          t.xs[1] = 0.25f; t.vals[1][0] = 2.4768f; t.vals[1][1] = 1.8176f; t.vals[1][2] = 4.6406f; t.vals[1][3] = 3.737f; t.vals[1][4] = 0.32991f; t.vals[1][5] = -12.72f;
          t.xs[2] = 0.5f; t.vals[2][0] = 2.1372f; t.vals[2][1] = 1.04f; t.vals[2][2] = 7.0091f; t.vals[2][3] = 3.7259f; t.vals[2][4] = 0.46739f; t.vals[2][5] = -12.19f;
          t.xs[3] = 0.75f; t.vals[3][0] = 2.1372f; t.vals[3][1] = 1.04f; t.vals[3][2] = 7.0091f; t.vals[3][3] = 3.7259f; t.vals[3][4] = 0.46739f; t.vals[3][5] = -12.19f;
          t.xs[4] = 1.0f; t.vals[4][0] = 2.1372f; t.vals[4][1] = 1.04f; t.vals[4][2] = 7.0091f; t.vals[4][3] = 3.7259f; t.vals[4][4] = 0.46739f; t.vals[4][5] = -12.19f;
        }
        d.knobDef[10] = 0.14f;
        d.knobDef[11] = 0.5f;
        d.knobDef[12] = 0.5f;
        d.knobDef[13] = 0.0f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specRC48()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.1256f, 1.0f, 0.22616f, 154.61f, 1.0f, 2.5238f, 0.77841f, 1.972f, -1.5576f, 2.4f, 3.4693f, 0.3f, -0.3f, 1.0f, 4.2659f, -0.12279f, -0.5559f, -3.4353f, 0.95563f, 3.6413f, 1.0f, 0.0f, 0.0f, 177.86f, 1.9048f, 3.1761f, -0.1549f, 3.5812f, 0.0f, 1850.0f, 4.2975f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 16;
        d.knobDef[0] = 1.0f;
        { auto& t = d.knobs[0]; t.points = 2; t.steps = true; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 32;
          t.xs[0] = 0.0f; t.vals[0][0] = 175.29f; t.vals[0][1] = 0.16246f; t.vals[0][2] = 0.068046f; t.vals[0][3] = 188.45f; t.vals[0][4] = 2.322f; t.vals[0][5] = 0.99999f; t.vals[0][6] = 0.89662f; t.vals[0][7] = -1.7325f; t.vals[0][8] = 3.2423f; t.vals[0][9] = -1.4445f; t.vals[0][10] = 6.9648f; t.vals[0][11] = -1.2704f; t.vals[0][12] = 4.3168f; t.vals[0][13] = 0.77434f; t.vals[0][14] = 4.5714f; t.vals[0][15] = 2.5408f; t.vals[0][16] = 2.5551f; t.vals[0][17] = 1.2647f; t.vals[0][18] = 3.2324f; t.vals[0][19] = 4.2975f;
          t.xs[1] = 1.0f; t.vals[1][0] = 177.86f; t.vals[1][1] = 0.1256f; t.vals[1][2] = 0.22619f; t.vals[1][3] = 154.61f; t.vals[1][4] = 2.5238f; t.vals[1][5] = 0.77837f; t.vals[1][6] = 1.972f; t.vals[1][7] = -1.5578f; t.vals[1][8] = 3.4693f; t.vals[1][9] = -0.12213f; t.vals[1][10] = -0.55611f; t.vals[1][11] = -3.4362f; t.vals[1][12] = 4.2659f; t.vals[1][13] = 0.95563f; t.vals[1][14] = 3.64f; t.vals[1][15] = 1.9048f; t.vals[1][16] = 3.1761f; t.vals[1][17] = -0.1549f; t.vals[1][18] = 3.5812f; t.vals[1][19] = 4.2975f;
        }
        d.knobDef[1] = 0.0f;
        { auto& t = d.knobs[1]; t.points = 2; t.steps = true; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 8; t.params[2] = 9; t.params[3] = 10; t.params[4] = 4; t.params[5] = 5; t.params[6] = 25; t.params[7] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 2.5238f; t.vals[0][1] = 0.77833f; t.vals[0][2] = 1.9721f; t.vals[0][3] = -1.5586f; t.vals[0][4] = 0.22658f; t.vals[0][5] = 154.6f; t.vals[0][6] = 177.86f; t.vals[0][7] = 3.6332f;
          t.xs[1] = 1.0f; t.vals[1][0] = 2.5296f; t.vals[1][1] = 0.90942f; t.vals[1][2] = 1.7669f; t.vals[1][3] = -1.7555f; t.vals[1][4] = 0.21628f; t.vals[1][5] = 154.26f; t.vals[1][6] = 186.84f; t.vals[1][7] = 4.5435f;
        }
        d.knobDef[2] = 0.235f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 0; t.params[1] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f; t.vals[0][1] = 47.002f;
          t.xs[1] = 0.235f; t.vals[1][0] = 0.0f; t.vals[1][1] = 177.86f;
          t.xs[2] = 0.25f; t.vals[2][0] = 7.6667f; t.vals[2][1] = 177.86f;
          t.xs[3] = 0.5f; t.vals[3][0] = 25.979f; t.vals[3][1] = 177.86f;
          t.xs[4] = 0.75f; t.vals[4][0] = 25.979f; t.vals[4][1] = 177.86f;
          t.xs[5] = 1.0f; t.vals[5][0] = 25.979f; t.vals[5][1] = 177.86f;
        }
        d.knobDef[3] = 0.735f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = false; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 8; t.params[2] = 9; t.params[3] = 10; t.params[4] = 4; t.params[5] = 5; t.params[6] = 21; t.params[7] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.932f; t.vals[0][1] = -0.046991f; t.vals[0][2] = -1.9319f; t.vals[0][3] = -3.5607f; t.vals[0][4] = 0.1814f; t.vals[0][5] = 126.61f; t.vals[0][6] = 12.012f; t.vals[0][7] = 125.07f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.5081f; t.vals[1][1] = 0.36066f; t.vals[1][2] = 1.9484f; t.vals[1][3] = -1.6968f; t.vals[1][4] = 0.20158f; t.vals[1][5] = 223.48f; t.vals[1][6] = 4.2296f; t.vals[1][7] = 143.1f;
          t.xs[2] = 0.5f; t.vals[2][0] = 2.6783f; t.vals[2][1] = 0.63683f; t.vals[2][2] = 1.7696f; t.vals[2][3] = -1.6507f; t.vals[2][4] = 0.17009f; t.vals[2][5] = 176.47f; t.vals[2][6] = 4.3816f; t.vals[2][7] = 160.44f;
          t.xs[3] = 0.735f; t.vals[3][0] = 2.5238f; t.vals[3][1] = 0.77841f; t.vals[3][2] = 1.972f; t.vals[3][3] = -1.5576f; t.vals[3][4] = 0.22616f; t.vals[3][5] = 154.61f; t.vals[3][6] = 3.6413f; t.vals[3][7] = 177.86f;
          t.xs[4] = 0.75f; t.vals[4][0] = 2.5238f; t.vals[4][1] = 0.77842f; t.vals[4][2] = 1.972f; t.vals[4][3] = -1.5576f; t.vals[4][4] = 0.22619f; t.vals[4][5] = 154.61f; t.vals[4][6] = 3.6148f; t.vals[4][7] = 178.94f;
          t.xs[5] = 1.0f; t.vals[5][0] = 2.5463f; t.vals[5][1] = 0.88877f; t.vals[5][2] = 1.84f; t.vals[5][3] = -1.4533f; t.vals[5][4] = 0.22964f; t.vals[5][5] = 145.43f; t.vals[5][6] = 3.5256f; t.vals[5][7] = 196.54f;
        }
        d.knobDef[4] = 0.67f;
        { auto& t = d.knobs[4]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 4; t.params[1] = 5; t.params[2] = 2; t.params[3] = 21; t.params[4] = 6; t.params[5] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.22616f; t.vals[0][1] = 154.61f; t.vals[0][2] = 0.1256f; t.vals[0][3] = 4.7895f; t.vals[0][4] = 1.0f; t.vals[0][5] = 118.69f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.22239f; t.vals[1][1] = 84.654f; t.vals[1][2] = 0.052328f; t.vals[1][3] = -1.1907f; t.vals[1][4] = 1.7033f; t.vals[1][5] = 127.19f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.21244f; t.vals[2][1] = 153.37f; t.vals[2][2] = 0.12665f; t.vals[2][3] = 2.7096f; t.vals[2][4] = 1.1144f; t.vals[2][5] = 169.79f;
          t.xs[3] = 0.67f; t.vals[3][0] = 0.22616f; t.vals[3][1] = 154.61f; t.vals[3][2] = 0.1256f; t.vals[3][3] = 3.6413f; t.vals[3][4] = 1.0f; t.vals[3][5] = 177.86f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.22612f; t.vals[4][1] = 154.61f; t.vals[4][2] = 0.1256f; t.vals[4][3] = 3.4196f; t.vals[4][4] = 1.0002f; t.vals[4][5] = 177.87f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.22617f; t.vals[5][1] = 154.61f; t.vals[5][2] = 0.1256f; t.vals[5][3] = 3.3238f; t.vals[5][4] = 1.0f; t.vals[5][5] = 203.83f;
        }
        d.knobDef[5] = 0.815f;
        { auto& t = d.knobs[5]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 5; t.params[1] = 7; t.params[2] = 4; t.params[3] = 21; t.params[4] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 99.646f; t.vals[0][1] = 2.4983f; t.vals[0][2] = 0.12324f; t.vals[0][3] = 4.4262f; t.vals[0][4] = 143.35f;
          t.xs[1] = 0.25f; t.vals[1][0] = 201.02f; t.vals[1][1] = 2.3488f; t.vals[1][2] = 0.13509f; t.vals[1][3] = 4.1915f; t.vals[1][4] = 160.23f;
          t.xs[2] = 0.5f; t.vals[2][0] = 217.13f; t.vals[2][1] = 2.5326f; t.vals[2][2] = 0.18613f; t.vals[2][3] = 3.9823f; t.vals[2][4] = 166.06f;
          t.xs[3] = 0.75f; t.vals[3][0] = 154.61f; t.vals[3][1] = 2.5238f; t.vals[3][2] = 0.22616f; t.vals[3][3] = 3.6544f; t.vals[3][4] = 175.69f;
          t.xs[4] = 0.815f; t.vals[4][0] = 154.61f; t.vals[4][1] = 2.5238f; t.vals[4][2] = 0.22616f; t.vals[4][3] = 3.6413f; t.vals[4][4] = 177.86f;
          t.xs[5] = 1.0f; t.vals[5][0] = 154.58f; t.vals[5][1] = 2.5285f; t.vals[5][2] = 0.24174f; t.vals[5][3] = 3.5252f; t.vals[5][4] = 183.98f;
        }
        d.knobDef[6] = 0.615f;
        { auto& t = d.knobs[6]; t.points = 6; t.steps = false; t.nParams = 4;
          t.params[0] = 2; t.params[1] = 3; t.params[2] = 4; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.1256f; t.vals[0][1] = 1.0001f; t.vals[0][2] = 0.24379f; t.vals[0][3] = 3.6127f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.1256f; t.vals[1][1] = 0.99995f; t.vals[1][2] = 0.23163f; t.vals[1][3] = 3.6122f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.1256f; t.vals[2][1] = 1.0f; t.vals[2][2] = 0.22616f; t.vals[2][3] = 3.6216f;
          t.xs[3] = 0.615f; t.vals[3][0] = 0.1256f; t.vals[3][1] = 1.0f; t.vals[3][2] = 0.22616f; t.vals[3][3] = 3.6413f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.1256f; t.vals[4][1] = 1.0f; t.vals[4][2] = 0.22616f; t.vals[4][3] = 3.6779f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.1256f; t.vals[5][1] = 1.0f; t.vals[5][2] = 0.22616f; t.vals[5][3] = 3.7859f;
        }
        d.knobDef[7] = 0.725f;
        { auto& t = d.knobs[7]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 9; t.params[1] = 11; t.params[2] = 17; t.params[3] = 26; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.7853f; t.vals[0][1] = 2.5549f; t.vals[0][2] = -0.072648f; t.vals[0][3] = 1.8021f; t.vals[0][4] = 3.6328f;
          t.xs[1] = 0.25f; t.vals[1][0] = -1.7853f; t.vals[1][1] = 2.5549f; t.vals[1][2] = -0.072648f; t.vals[1][3] = 1.8021f; t.vals[1][4] = 3.6328f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.401f; t.vals[2][1] = 2.9031f; t.vals[2][2] = 1.0053f; t.vals[2][3] = 1.6306f; t.vals[2][4] = 3.6037f;
          t.xs[3] = 0.725f; t.vals[3][0] = 1.972f; t.vals[3][1] = 2.4f; t.vals[3][2] = -0.12279f; t.vals[3][3] = 1.9048f; t.vals[3][4] = 3.6413f;
          t.xs[4] = 0.75f; t.vals[4][0] = 2.0416f; t.vals[4][1] = 2.4651f; t.vals[4][2] = 0.024162f; t.vals[4][3] = 1.7546f; t.vals[4][4] = 3.6254f;
          t.xs[5] = 1.0f; t.vals[5][0] = 2.0416f; t.vals[5][1] = 2.4651f; t.vals[5][2] = 0.024162f; t.vals[5][3] = 1.7546f; t.vals[5][4] = 3.6254f;
        }
        d.knobDef[8] = 0.79f;
        { auto& t = d.knobs[8]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.16407f; t.vals[0][1] = -0.11524f; t.vals[0][2] = -0.041504f; t.vals[0][3] = 1.5887f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.070383f; t.vals[1][1] = -0.042534f; t.vals[1][2] = -0.28596f; t.vals[1][3] = 1.6999f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.22967f; t.vals[2][1] = 0.34939f; t.vals[2][2] = -0.77961f; t.vals[2][3] = 1.6557f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.69464f; t.vals[3][1] = 1.7839f; t.vals[3][2] = -1.5703f; t.vals[3][3] = 3.3264f;
          t.xs[4] = 0.79f; t.vals[4][0] = 0.77841f; t.vals[4][1] = 1.972f; t.vals[4][2] = -1.5576f; t.vals[4][3] = 3.6413f;
        }
        d.knobDef[9] = 0.71f;
        { auto& t = d.knobs[9]; t.points = 6; t.steps = false; t.nParams = 3;
          t.params[0] = 11; t.params[1] = 9; t.params[2] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 3.3f; t.vals[0][1] = 2.0879f; t.vals[0][2] = 4.6544f;
          t.xs[1] = 0.25f; t.vals[1][0] = 3.3f; t.vals[1][1] = 2.1174f; t.vals[1][2] = 4.5925f;
          t.xs[2] = 0.5f; t.vals[2][0] = 3.3f; t.vals[2][1] = 1.882f; t.vals[2][2] = 3.9717f;
          t.xs[3] = 0.71f; t.vals[3][0] = 2.4f; t.vals[3][1] = 1.972f; t.vals[3][2] = 3.6413f;
          t.xs[4] = 0.75f; t.vals[4][0] = 2.0317f; t.vals[4][1] = 2.6993f; t.vals[4][2] = 3.4247f;
          t.xs[5] = 1.0f; t.vals[5][0] = 2.9966f; t.vals[5][1] = -0.37846f; t.vals[5][2] = 3.6358f;
        }
        d.knobDef[10] = 0.73f;
        { auto& t = d.knobs[10]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 16; t.params[1] = 10; t.params[2] = 12; t.params[3] = 19; t.params[4] = 32; t.params[5] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 4.2653f; t.vals[0][1] = -1.3666f; t.vals[0][2] = 3.2794f; t.vals[0][3] = -8.1758f; t.vals[0][4] = 4.4925f; t.vals[0][5] = -1.6584f;
          t.xs[1] = 0.25f; t.vals[1][0] = 4.3122f; t.vals[1][1] = -1.4046f; t.vals[1][2] = 3.2933f; t.vals[1][3] = -10.465f; t.vals[1][4] = 4.4925f; t.vals[1][5] = -0.060413f;
          t.xs[2] = 0.5f; t.vals[2][0] = 4.2229f; t.vals[2][1] = -1.3447f; t.vals[2][2] = 3.4106f; t.vals[2][3] = -6.9358f; t.vals[2][4] = 4.4925f; t.vals[2][5] = 3.4945f;
          t.xs[3] = 0.73f; t.vals[3][0] = 4.2659f; t.vals[3][1] = -1.5576f; t.vals[3][2] = 3.4693f; t.vals[3][3] = -3.4353f; t.vals[3][4] = 4.2975f; t.vals[3][5] = 3.6413f;
          t.xs[4] = 0.75f; t.vals[4][0] = 4.2649f; t.vals[4][1] = -1.688f; t.vals[4][2] = 3.5191f; t.vals[4][3] = -3.124f; t.vals[4][4] = 4.4925f; t.vals[4][5] = 3.6261f;
          t.xs[5] = 1.0f; t.vals[5][0] = 4.2835f; t.vals[5][1] = -1.6783f; t.vals[5][2] = 3.5121f; t.vals[5][3] = -2.607f; t.vals[5][4] = 4.4925f; t.vals[5][5] = 3.6044f;
        }
        d.knobDef[11] = 0.5f;
        { auto& t = d.knobs[11]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 10; t.params[1] = 12; t.params[2] = 21; t.params[3] = 19;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.5809f; t.vals[0][1] = 2.9597f; t.vals[0][2] = 1.3005f; t.vals[0][3] = -2.855f;
          t.xs[1] = 0.25f; t.vals[1][0] = -2.587f; t.vals[1][1] = 2.9804f; t.vals[1][2] = 1.3872f; t.vals[1][3] = -2.9368f;
          t.xs[2] = 0.5f; t.vals[2][0] = -1.6993f; t.vals[2][1] = 3.5249f; t.vals[2][2] = 3.6255f; t.vals[2][3] = -3.2356f;
          t.xs[3] = 0.75f; t.vals[3][0] = -0.11956f; t.vals[3][1] = 2.8095f; t.vals[3][2] = 3.6625f; t.vals[3][3] = -0.22474f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.8493f; t.vals[4][1] = 3.9612f; t.vals[4][2] = 3.5424f; t.vals[4][3] = -0.29617f;
        }
        d.knobDef[12] = 0.5f;
        { auto& t = d.knobs[12]; t.points = 5; t.steps = false; t.nParams = 5;
          t.params[0] = 17; t.params[1] = 26; t.params[2] = 19; t.params[3] = 29; t.params[4] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.0013944f; t.vals[0][1] = 2.0712f; t.vals[0][2] = -4.5235f; t.vals[0][3] = 3.6068f; t.vals[0][4] = 0.31685f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.017954f; t.vals[1][1] = 2.5013f; t.vals[1][2] = -4.2321f; t.vals[1][3] = 3.5946f; t.vals[1][4] = 2.2486f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.12241f; t.vals[2][1] = 1.9048f; t.vals[2][2] = -3.4353f; t.vals[2][3] = 3.5812f; t.vals[2][4] = 3.6412f;
          t.xs[3] = 0.75f; t.vals[3][0] = -0.31915f; t.vals[3][1] = 1.9046f; t.vals[3][2] = -3.3484f; t.vals[3][3] = 3.5748f; t.vals[3][4] = 5.0034f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.010153f; t.vals[4][1] = 1.9081f; t.vals[4][2] = -3.1762f; t.vals[4][3] = 3.5715f; t.vals[4][4] = 6.1182f;
        }
        d.knobDef[13] = 0.5f;
        d.knobDef[14] = 0.5f;
        d.knobDef[15] = 0.5f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specIceVerb()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.45948f, 1.0f, 0.060552f, 35.505f, 1.0f, 0.89973f, 0.68418f, -0.18888f, 0.44355f, 2.4f, 3.5549f, 0.3f, -0.3f, 2.7101f, 2.9346f, -9.191f, -2.1261f, 3.5908f, 0.92819f, 13.461f, 1.0f, 0.0f, 0.0f, 11.604f, 2.398f, 3.1761f, -0.15491f, 3.699f, 0.0f, 1850.0f, 4.2099f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 6;
        d.knobDef[0] = 0.52422f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 9;
          t.params[0] = 15; t.params[1] = 16; t.params[2] = 32; t.params[3] = 18; t.params[4] = 27; t.params[5] = 28; t.params[6] = 17; t.params[7] = 19; t.params[8] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.2153f; t.vals[0][1] = 3.035f; t.vals[0][2] = 4.305f; t.vals[0][3] = -29.975f; t.vals[0][4] = 3.4748f; t.vals[0][5] = -0.98103f; t.vals[0][6] = 21.681f; t.vals[0][7] = 6.1217f; t.vals[0][8] = -19.37f;
          t.xs[1] = 0.25f; t.vals[1][0] = 2.401f; t.vals[1][1] = 2.5009f; t.vals[1][2] = 4.4815f; t.vals[1][3] = -17.128f; t.vals[1][4] = 3.2493f; t.vals[1][5] = -0.90931f; t.vals[1][6] = 6.8423f; t.vals[1][7] = 3.1476f; t.vals[1][8] = 15.094f;
          t.xs[2] = 0.5f; t.vals[2][0] = 2.7455f; t.vals[2][1] = 3.0512f; t.vals[2][2] = 4.3948f; t.vals[2][3] = -25.558f; t.vals[2][4] = 3.4548f; t.vals[2][5] = -0.80604f; t.vals[2][6] = -15.031f; t.vals[2][7] = 0.15513f; t.vals[2][8] = 28.615f;
          t.xs[3] = 0.52422f; t.vals[3][0] = 2.7101f; t.vals[3][1] = 2.9346f; t.vals[3][2] = 4.2099f; t.vals[3][3] = -2.1261f; t.vals[3][4] = 3.1761f; t.vals[3][5] = -0.15491f; t.vals[3][6] = -9.191f; t.vals[3][7] = 3.5908f; t.vals[3][8] = 13.461f;
          t.xs[4] = 0.75f; t.vals[4][0] = 3.0954f; t.vals[4][1] = 4.1747f; t.vals[4][2] = 4.3194f; t.vals[4][3] = 8.7787f; t.vals[4][4] = 3.4103f; t.vals[4][5] = -0.16731f; t.vals[4][6] = 0.34101f; t.vals[4][7] = -10.969f; t.vals[4][8] = 2.769f;
          t.xs[5] = 1.0f; t.vals[5][0] = 3.5983f; t.vals[5][1] = 4.279f; t.vals[5][2] = 4.1557f; t.vals[5][3] = -21.943f; t.vals[5][4] = 2.7109f; t.vals[5][5] = -0.64473f; t.vals[5][6] = 22.459f; t.vals[5][7] = 4.6084f; t.vals[5][8] = 11.495f;
        }
        d.knobDef[1] = 0.37943f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 5;
          t.params[0] = 8; t.params[1] = 9; t.params[2] = 10; t.params[3] = 21; t.params[4] = 4;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.11933f; t.vals[0][1] = 0.98847f; t.vals[0][2] = 1.6334f; t.vals[0][3] = 13.559f; t.vals[0][4] = 0.044121f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.46832f; t.vals[1][1] = 0.71781f; t.vals[1][2] = 1.8428f; t.vals[1][3] = 13.206f; t.vals[1][4] = 0.054382f;
          t.xs[2] = 0.37943f; t.vals[2][0] = 0.68418f; t.vals[2][1] = -0.18888f; t.vals[2][2] = 0.44355f; t.vals[2][3] = 13.461f; t.vals[2][4] = 0.060552f;
          t.xs[3] = 0.5f; t.vals[3][0] = 1.0195f; t.vals[3][1] = -2.0877f; t.vals[3][2] = -2.1131f; t.vals[3][3] = 15.756f; t.vals[3][4] = 0.053829f;
          t.xs[4] = 0.75f; t.vals[4][0] = 1.5782f; t.vals[4][1] = -3.3212f; t.vals[4][2] = -4.9999f; t.vals[4][3] = 18.28f; t.vals[4][4] = 0.024483f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.4206f; t.vals[5][1] = -1.1943f; t.vals[5][2] = -5.0f; t.vals[5][3] = 18.024f; t.vals[5][4] = 0.0064527f;
        }
        d.knobDef[2] = 1.0f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 32;
          t.xs[0] = 0.0f; t.vals[0][0] = 6.2075f; t.vals[0][1] = 0.84588f; t.vals[0][2] = 0.88604f; t.vals[0][3] = 65.46f; t.vals[0][4] = 0.57933f; t.vals[0][5] = -0.44068f; t.vals[0][6] = -0.84784f; t.vals[0][7] = -1.5293f; t.vals[0][8] = 3.7267f; t.vals[0][9] = -6.4167f; t.vals[0][10] = -1.372f; t.vals[0][11] = 4.5485f; t.vals[0][12] = 2.887f; t.vals[0][13] = 1.1409f; t.vals[0][14] = 9.0702f; t.vals[0][15] = 2.4426f; t.vals[0][16] = 3.2706f; t.vals[0][17] = 0.91808f; t.vals[0][18] = 3.8313f; t.vals[0][19] = 4.2704f;
          t.xs[1] = 0.25f; t.vals[1][0] = 11.896f; t.vals[1][1] = 0.43095f; t.vals[1][2] = 0.29723f; t.vals[1][3] = 28.679f; t.vals[1][4] = 0.83719f; t.vals[1][5] = 0.13538f; t.vals[1][6] = -0.2902f; t.vals[1][7] = 0.28503f; t.vals[1][8] = 3.5728f; t.vals[1][9] = -8.9699f; t.vals[1][10] = -0.51999f; t.vals[1][11] = 3.9091f; t.vals[1][12] = 2.8786f; t.vals[1][13] = 0.88406f; t.vals[1][14] = 13.569f; t.vals[1][15] = 2.4375f; t.vals[1][16] = 3.2778f; t.vals[1][17] = -0.17256f; t.vals[1][18] = 3.7581f; t.vals[1][19] = 4.2215f;
          t.xs[2] = 0.5f; t.vals[2][0] = 10.202f; t.vals[2][1] = 0.45724f; t.vals[2][2] = 0.055218f; t.vals[2][3] = 35.142f; t.vals[2][4] = 0.83788f; t.vals[2][5] = 0.44745f; t.vals[2][6] = -0.40506f; t.vals[2][7] = 0.068331f; t.vals[2][8] = 3.6055f; t.vals[2][9] = -7.9825f; t.vals[2][10] = 1.1242f; t.vals[2][11] = 2.7375f; t.vals[2][12] = 2.8334f; t.vals[2][13] = 0.85954f; t.vals[2][14] = 13.966f; t.vals[2][15] = 2.4289f; t.vals[2][16] = 3.2459f; t.vals[2][17] = -0.15658f; t.vals[2][18] = 3.7558f; t.vals[2][19] = 4.216f;
          t.xs[3] = 0.75f; t.vals[3][0] = 7.8323f; t.vals[3][1] = 0.43611f; t.vals[3][2] = 0.1138f; t.vals[3][3] = 31.024f; t.vals[3][4] = 1.0177f; t.vals[3][5] = 0.56505f; t.vals[3][6] = -0.16773f; t.vals[3][7] = 0.41753f; t.vals[3][8] = 3.5709f; t.vals[3][9] = -8.8742f; t.vals[3][10] = -1.4375f; t.vals[3][11] = 5.5869f; t.vals[3][12] = 2.8859f; t.vals[3][13] = 0.9022f; t.vals[3][14] = 14.286f; t.vals[3][15] = 2.4351f; t.vals[3][16] = 3.2724f; t.vals[3][17] = -0.17018f; t.vals[3][18] = 3.7546f; t.vals[3][19] = 4.2183f;
          t.xs[4] = 1.0f; t.vals[4][0] = 11.604f; t.vals[4][1] = 0.45948f; t.vals[4][2] = 0.060552f; t.vals[4][3] = 35.505f; t.vals[4][4] = 0.89973f; t.vals[4][5] = 0.68418f; t.vals[4][6] = -0.18888f; t.vals[4][7] = 0.44355f; t.vals[4][8] = 3.5549f; t.vals[4][9] = -9.191f; t.vals[4][10] = -2.1261f; t.vals[4][11] = 3.5908f; t.vals[4][12] = 2.9346f; t.vals[4][13] = 0.92819f; t.vals[4][14] = 13.461f; t.vals[4][15] = 2.398f; t.vals[4][16] = 3.1761f; t.vals[4][17] = -0.15491f; t.vals[4][18] = 3.699f; t.vals[4][19] = 4.2099f;
        }
        d.knobDef[3] = 1.0f;
        d.knobDef[4] = 0.0f;
        d.knobDef[5] = 0.0f;
        return d;
    }();
    return s;
}

inline const VerbSpec& specOctaverb()
{
    static const VerbSpec s = [] {
        VerbSpec d {};
        const float b[vpCount] { 0.0f, 0.0f, 0.041234f, 1.0f, 0.61834f, 13.7f, 1.0f, 1.7051f, -0.053089f, -0.75633f, -1.8268f, 2.4f, 3.7967f, 0.3f, -0.3f, 1.3101f, 4.4757f, 6.818f, 0.6547f, -1.6644f, 0.7046f, -4.4458f, 1.0f, 0.0f, 0.0f, 14.309f, 2.0996f, 3.22f, -0.13852f, 3.5544f, 0.0f, 1850.0f, 4.4f };
        for (int i = 0; i < vpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 10;
        d.knobDef[0] = 0.42857f;
        { auto& t = d.knobs[0]; t.points = 8; t.steps = true; t.nParams = 20;
          t.params[0] = 25; t.params[1] = 2; t.params[2] = 4; t.params[3] = 5; t.params[4] = 7; t.params[5] = 8; t.params[6] = 9; t.params[7] = 10; t.params[8] = 12; t.params[9] = 17; t.params[10] = 18; t.params[11] = 19; t.params[12] = 16; t.params[13] = 20; t.params[14] = 21; t.params[15] = 26; t.params[16] = 27; t.params[17] = 28; t.params[18] = 29; t.params[19] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 13.732f; t.vals[0][1] = 0.13778f; t.vals[0][2] = 0.65492f; t.vals[0][3] = 14.929f; t.vals[0][4] = 1.078f; t.vals[0][5] = 0.027766f; t.vals[0][6] = -0.69201f; t.vals[0][7] = -1.268f; t.vals[0][8] = 3.8392f; t.vals[0][9] = 3.8038f; t.vals[0][10] = 3.8624f; t.vals[0][11] = 1.8344f; t.vals[0][12] = 4.3211f; t.vals[0][13] = 0.9599f; t.vals[0][14] = -12.544f; t.vals[0][15] = 2.4146f; t.vals[0][16] = 3.3726f; t.vals[0][17] = -0.14044f; t.vals[0][18] = 3.6831f; t.vals[0][19] = 1.2312f;
          t.xs[1] = 0.14286f; t.vals[1][0] = 15.382f; t.vals[1][1] = 0.08727f; t.vals[1][2] = 0.74106f; t.vals[1][3] = 15.855f; t.vals[1][4] = 2.1352f; t.vals[1][5] = 0.017421f; t.vals[1][6] = -0.54436f; t.vals[1][7] = -1.0661f; t.vals[1][8] = 3.8933f; t.vals[1][9] = 0.14417f; t.vals[1][10] = 0.8526f; t.vals[1][11] = 0.38753f; t.vals[1][12] = 4.418f; t.vals[1][13] = 0.57654f; t.vals[1][14] = -6.1037f; t.vals[1][15] = 2.3477f; t.vals[1][16] = 3.2289f; t.vals[1][17] = 0.46699f; t.vals[1][18] = 3.4605f; t.vals[1][19] = 1.5127f;
          t.xs[2] = 0.28571f; t.vals[2][0] = 16.906f; t.vals[2][1] = 0.012054f; t.vals[2][2] = 0.48743f; t.vals[2][3] = 14.435f; t.vals[2][4] = 1.7531f; t.vals[2][5] = -0.15781f; t.vals[2][6] = -0.3772f; t.vals[2][7] = -1.7318f; t.vals[2][8] = 3.765f; t.vals[2][9] = 4.5069f; t.vals[2][10] = 2.1996f; t.vals[2][11] = 1.5765f; t.vals[2][12] = 4.49f; t.vals[2][13] = 0.51442f; t.vals[2][14] = -4.073f; t.vals[2][15] = 2.6189f; t.vals[2][16] = 3.4459f; t.vals[2][17] = -0.1924f; t.vals[2][18] = 3.5552f; t.vals[2][19] = 1.2779f;
          t.xs[3] = 0.42857f; t.vals[3][0] = 14.79f; t.vals[3][1] = 0.041222f; t.vals[3][2] = 0.6081f; t.vals[3][3] = 13.76f; t.vals[3][4] = 1.7013f; t.vals[3][5] = -0.055045f; t.vals[3][6] = -0.7547f; t.vals[3][7] = -1.8248f; t.vals[3][8] = 3.7979f; t.vals[3][9] = 6.7982f; t.vals[3][10] = 0.66606f; t.vals[3][11] = -1.6415f; t.vals[3][12] = 4.4757f; t.vals[3][13] = 0.7147f; t.vals[3][14] = -4.4579f; t.vals[3][15] = 2.0991f; t.vals[3][16] = 3.2204f; t.vals[3][17] = -0.13831f; t.vals[3][18] = 3.5545f; t.vals[3][19] = 1.0158f;
          t.xs[4] = 0.57143f; t.vals[4][0] = 27.418f; t.vals[4][1] = 0.053147f; t.vals[4][2] = 0.85106f; t.vals[4][3] = 17.721f; t.vals[4][4] = 1.8591f; t.vals[4][5] = 0.25661f; t.vals[4][6] = -0.27123f; t.vals[4][7] = -1.0181f; t.vals[4][8] = 3.865f; t.vals[4][9] = -3.5858f; t.vals[4][10] = -1.3743f; t.vals[4][11] = 0.59402f; t.vals[4][12] = 4.4757f; t.vals[4][13] = 0.62176f; t.vals[4][14] = -11.005f; t.vals[4][15] = 2.1893f; t.vals[4][16] = 3.2609f; t.vals[4][17] = -0.14904f; t.vals[4][18] = 3.5046f; t.vals[4][19] = 2.1784f;
          t.xs[5] = 0.71429f; t.vals[5][0] = 21.522f; t.vals[5][1] = 0.026162f; t.vals[5][2] = 1.1098f; t.vals[5][3] = 13.289f; t.vals[5][4] = 1.5598f; t.vals[5][5] = 0.17135f; t.vals[5][6] = -0.69614f; t.vals[5][7] = -1.7669f; t.vals[5][8] = 3.6053f; t.vals[5][9] = 4.7568f; t.vals[5][10] = 2.2161f; t.vals[5][11] = -0.16354f; t.vals[5][12] = 4.49f; t.vals[5][13] = 0.80443f; t.vals[5][14] = -12.37f; t.vals[5][15] = 2.0452f; t.vals[5][16] = 3.2679f; t.vals[5][17] = 0.50845f; t.vals[5][18] = 3.3748f; t.vals[5][19] = 2.3903f;
          t.xs[6] = 0.85714f; t.vals[6][0] = 30.125f; t.vals[6][1] = 0.016969f; t.vals[6][2] = 1.4203f; t.vals[6][3] = 21.517f; t.vals[6][4] = 1.6102f; t.vals[6][5] = 0.35437f; t.vals[6][6] = -0.42933f; t.vals[6][7] = -1.0968f; t.vals[6][8] = 3.7628f; t.vals[6][9] = -3.5406f; t.vals[6][10] = -0.59651f; t.vals[6][11] = -1.1432f; t.vals[6][12] = 4.2819f; t.vals[6][13] = 0.56034f; t.vals[6][14] = -15.735f; t.vals[6][15] = 2.2083f; t.vals[6][16] = 3.0731f; t.vals[6][17] = 0.065316f; t.vals[6][18] = 3.3708f; t.vals[6][19] = 3.4572f;
          t.xs[7] = 1.0f; t.vals[7][0] = 27.206f; t.vals[7][1] = 0.14026f; t.vals[7][2] = 0.84142f; t.vals[7][3] = 16.728f; t.vals[7][4] = 2.133f; t.vals[7][5] = -0.042773f; t.vals[7][6] = 0.34744f; t.vals[7][7] = -0.24444f; t.vals[7][8] = 3.8689f; t.vals[7][9] = -0.023955f; t.vals[7][10] = -3.2588f; t.vals[7][11] = -0.11395f; t.vals[7][12] = 4.3275f; t.vals[7][13] = 0.88968f; t.vals[7][14] = -10.196f; t.vals[7][15] = 2.5945f; t.vals[7][16] = 3.2415f; t.vals[7][17] = -0.73846f; t.vals[7][18] = 3.5537f; t.vals[7][19] = 1.8445f;
        }
        d.knobDef[1] = 0.5f;
        { auto& t = d.knobs[1]; t.points = 5; t.steps = false; t.nParams = 8;
          t.params[0] = 7; t.params[1] = 8; t.params[2] = 9; t.params[3] = 10; t.params[4] = 4; t.params[5] = 5; t.params[6] = 21; t.params[7] = 25;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.2528f; t.vals[0][1] = -0.18486f; t.vals[0][2] = -1.375f; t.vals[0][3] = -3.2439f; t.vals[0][4] = 0.56367f; t.vals[0][5] = 9.9597f; t.vals[0][6] = -1.277f; t.vals[0][7] = 16.012f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.3075f; t.vals[1][1] = -0.18055f; t.vals[1][2] = -0.71856f; t.vals[1][3] = -2.1306f; t.vals[1][4] = 0.69839f; t.vals[1][5] = 13.719f; t.vals[1][6] = -4.3682f; t.vals[1][7] = 16.471f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.7051f; t.vals[2][1] = -0.05309f; t.vals[2][2] = -0.75633f; t.vals[2][3] = -1.8268f; t.vals[2][4] = 0.61834f; t.vals[2][5] = 13.7f; t.vals[2][6] = -4.4452f; t.vals[2][7] = 14.85f;
          t.xs[3] = 0.75f; t.vals[3][0] = 1.7017f; t.vals[3][1] = 0.082007f; t.vals[3][2] = -1.0619f; t.vals[3][3] = -2.1422f; t.vals[3][4] = 0.55275f; t.vals[3][5] = 13.949f; t.vals[3][6] = -4.4859f; t.vals[3][7] = 15.206f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.492f; t.vals[4][1] = 0.10249f; t.vals[4][2] = -0.98493f; t.vals[4][3] = -2.0085f; t.vals[4][4] = 0.63745f; t.vals[4][5] = 17.053f; t.vals[4][6] = -5.9178f; t.vals[4][7] = 14.835f;
        }
        d.knobDef[2] = 0.5f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 4;
          t.params[0] = 10; t.params[1] = 12; t.params[2] = 19; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.9999f; t.vals[0][1] = 4.1384f; t.vals[0][2] = 1.371f; t.vals[0][3] = -4.4677f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.33214f; t.vals[1][1] = 3.4788f; t.vals[1][2] = 0.73737f; t.vals[1][3] = -4.4511f;
          t.xs[2] = 0.5f; t.vals[2][0] = -1.8671f; t.vals[2][1] = 3.8129f; t.vals[2][2] = -1.6602f; t.vals[2][3] = -4.4496f;
          t.xs[3] = 0.75f; t.vals[3][0] = -2.4757f; t.vals[3][1] = 3.7702f; t.vals[3][2] = -5.0424f; t.vals[3][3] = -4.445f;
          t.xs[4] = 1.0f; t.vals[4][0] = -2.0542f; t.vals[4][1] = 3.4014f; t.vals[4][2] = -8.4576f; t.vals[4][3] = -4.7672f;
        }
        d.knobDef[3] = 0.39505f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = false; t.nParams = 4;
          t.params[0] = 9; t.params[1] = 11; t.params[2] = 17; t.params[3] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.1908f; t.vals[0][1] = 2.3255f; t.vals[0][2] = 5.9735f; t.vals[0][3] = -4.5717f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.85463f; t.vals[1][1] = 2.5555f; t.vals[1][2] = 5.2888f; t.vals[1][3] = -4.4755f;
          t.xs[2] = 0.39505f; t.vals[2][0] = -0.75633f; t.vals[2][1] = 2.4f; t.vals[2][2] = 6.818f; t.vals[2][3] = -4.4458f;
          t.xs[3] = 0.5f; t.vals[3][0] = -0.62328f; t.vals[3][1] = 2.2478f; t.vals[3][2] = 7.544f; t.vals[3][3] = -4.4312f;
          t.xs[4] = 0.75f; t.vals[4][0] = -0.256f; t.vals[4][1] = 2.1213f; t.vals[4][2] = 9.2071f; t.vals[4][3] = -4.3684f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.00391f; t.vals[5][1] = 2.1367f; t.vals[5][2] = 10.045f; t.vals[5][3] = -4.2274f;
        }
        d.knobDef[4] = 0.5f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 7;
          t.params[0] = 17; t.params[1] = 26; t.params[2] = 18; t.params[3] = 19; t.params[4] = 29; t.params[5] = 15; t.params[6] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.23769f; t.vals[0][1] = 1.8588f; t.vals[0][2] = 5.2971f; t.vals[0][3] = 2.1126f; t.vals[0][4] = 3.5961f; t.vals[0][5] = 1.5038f; t.vals[0][6] = -8.3656f;
          t.xs[1] = 0.25f; t.vals[1][0] = 5.5853f; t.vals[1][1] = 1.8807f; t.vals[1][2] = 3.8568f; t.vals[1][3] = 1.4002f; t.vals[1][4] = 3.4904f; t.vals[1][5] = 1.1681f; t.vals[1][6] = -7.6478f;
          t.xs[2] = 0.5f; t.vals[2][0] = 6.818f; t.vals[2][1] = 2.0996f; t.vals[2][2] = 0.65471f; t.vals[2][3] = -1.6644f; t.vals[2][4] = 3.5544f; t.vals[2][5] = 1.3101f; t.vals[2][6] = -4.446f;
          t.xs[3] = 0.75f; t.vals[3][0] = 6.6237f; t.vals[3][1] = 2.0932f; t.vals[3][2] = 0.94029f; t.vals[3][3] = 2.0375f; t.vals[3][4] = 3.1617f; t.vals[3][5] = 1.2339f; t.vals[3][6] = -4.3001f;
          t.xs[4] = 1.0f; t.vals[4][0] = 6.3627f; t.vals[4][1] = 2.0525f; t.vals[4][2] = -0.81979f; t.vals[4][3] = 5.2874f; t.vals[4][4] = 3.0792f; t.vals[4][5] = 1.2215f; t.vals[4][6] = -3.7938f;
        }
        d.knobDef[5] = 0.5f;
        d.knobDef[6] = 0.24167f;
        d.knobDef[7] = 0.5f;
        d.knobDef[8] = 1.0f;
        d.knobDef[9] = 0.0f;
        return d;
    }();
    return s;
}

} // namespace aiofx
