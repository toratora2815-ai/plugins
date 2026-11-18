#pragma once
// 自動で作った表（build/aiofx/fit/gentable.py）。手で直さない。
// Guitar Rig 7 Pro の記録を、抑える系の共通エンジン（DynEngine.h）に合わせ込んだ結果
#include "DynEngine.h"

namespace aiofx
{
inline const DynSpec& specSupercharger()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -7.5845f, 2.7123f, 33.215f, -2.7f, -0.56f, 2.8134f, 0.0f, 1.0f, -30.0f, 0.0f, -17.0f, 0.25f, 0.0f, -5.0f, 0.5f, 2.0f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 6;
        d.knobDef[0] = 0.43333f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 0; t.params[1] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -18.032f; t.vals[0][1] = 18.064f;
          t.xs[1] = 0.25f; t.vals[1][0] = -10.503f; t.vals[1][1] = 10.445f;
          t.xs[2] = 0.43333f; t.vals[2][0] = 0.0f; t.vals[2][1] = 0.0f;
          t.xs[3] = 0.5f; t.vals[3][0] = 4.397f; t.vals[3][1] = -4.4758f;
          t.xs[4] = 0.75f; t.vals[4][0] = 20.101f; t.vals[4][1] = -20.189f;
          t.xs[5] = 1.0f; t.vals[5][0] = 36.116f; t.vals[5][1] = -35.89f;
        }
        d.knobDef[1] = 0.14444f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 1; t.params[1] = 2; t.params[2] = 3; t.params[3] = 6; t.params[4] = 4; t.params[5] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -13.452f; t.vals[0][1] = 0.12245f; t.vals[0][2] = 31.556f; t.vals[0][3] = 0.4997f; t.vals[0][4] = -2.5536f; t.vals[0][5] = -0.28862f;
          t.xs[1] = 0.14444f; t.vals[1][0] = -7.5845f; t.vals[1][1] = 2.7123f; t.vals[1][2] = 33.215f; t.vals[1][3] = 2.8134f; t.vals[1][4] = -2.7f; t.vals[1][5] = -0.56f;
          t.xs[2] = 0.25f; t.vals[2][0] = -14.562f; t.vals[2][1] = 2.3873f; t.vals[2][2] = 28.403f; t.vals[2][3] = 4.9697f; t.vals[2][4] = -3.1433f; t.vals[2][5] = -0.57115f;
          t.xs[3] = 0.5f; t.vals[3][0] = -22.315f; t.vals[3][1] = 2.7086f; t.vals[3][2] = 28.547f; t.vals[3][3] = 11.053f; t.vals[3][4] = -3.4406f; t.vals[3][5] = -0.65484f;
          t.xs[4] = 0.75f; t.vals[4][0] = -31.667f; t.vals[4][1] = 2.8195f; t.vals[4][2] = 29.833f; t.vals[4][3] = 17.157f; t.vals[4][4] = -3.6183f; t.vals[4][5] = -0.86969f;
          t.xs[5] = 1.0f; t.vals[5][0] = -60.668f; t.vals[5][1] = 2.638f; t.vals[5][2] = 30.388f; t.vals[5][3] = 38.432f; t.vals[5][4] = -3.6901f; t.vals[5][5] = -1.0718f;
        }
        d.knobDef[2] = 0.0f;
        { auto& t = d.knobs[2]; t.points = 2; t.steps = true; t.nParams = 4;
          t.params[0] = 4; t.params[1] = 5; t.params[2] = 1; t.params[3] = 2;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.9517f; t.vals[0][1] = -0.52961f; t.vals[0][2] = -7.6315f; t.vals[0][3] = 2.8089f;
          t.xs[1] = 1.0f; t.vals[1][0] = -0.98276f; t.vals[1][1] = -0.54251f; t.vals[1][2] = -15.377f; t.vals[1][3] = 6.9957f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 2; t.steps = true; t.nParams = 4;
          t.params[0] = 11; t.params[1] = 12; t.params[2] = 6; t.params[3] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -18.257f; t.vals[0][1] = -0.33253f; t.vals[0][2] = 2.7093f; t.vals[0][3] = 0.0023977f;
          t.xs[1] = 1.0f; t.vals[1][0] = -17.0f; t.vals[1][1] = 0.25f; t.vals[1][2] = -5.906f; t.vals[1][3] = 10.841f;
        }
        d.knobDef[4] = 0.0f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 2;
          t.params[0] = 7; t.params[1] = 8;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f; t.vals[0][1] = 1.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.0f; t.vals[1][1] = 1.0f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.49374f; t.vals[2][1] = 0.49901f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.75996f; t.vals[3][1] = 0.23773f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.9983f; t.vals[4][1] = 0.0002691f;
        }
        d.knobDef[5] = 0.5f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -20.103f;
          t.xs[1] = 0.25f; t.vals[1][0] = -10.105f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.1061f;
          t.xs[3] = 0.75f; t.vals[3][0] = 9.8931f;
          t.xs[4] = 1.0f; t.vals[4][0] = 19.893f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specSuperchargerGT()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -13.93f, 2.3391f, 29.361f, -4.4999f, -0.60415f, 4.5713f, 0.0f, 1.0f, -25.88f, -0.14169f, -22.8f, 0.078746f, 0.58394f, -3.0336f, 0.5578f, 2.0746f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 11;
        d.knobDef[0] = 0.43333f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 0; t.params[1] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -21.862f; t.vals[0][1] = 21.915f;
          t.xs[1] = 0.25f; t.vals[1][0] = -10.865f; t.vals[1][1] = 10.878f;
          t.xs[2] = 0.43333f; t.vals[2][0] = 0.0f; t.vals[2][1] = 0.0f;
          t.xs[3] = 0.5f; t.vals[3][0] = 4.198f; t.vals[3][1] = -4.1537f;
          t.xs[4] = 0.75f; t.vals[4][0] = 19.495f; t.vals[4][1] = -19.357f;
          t.xs[5] = 1.0f; t.vals[5][0] = 33.765f; t.vals[5][1] = -34.522f;
        }
        d.knobDef[1] = 1.0f;
        { auto& t = d.knobs[1]; t.points = 3; t.steps = true; t.nParams = 2;
          t.params[0] = 13; t.params[1] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 1.5467f; t.vals[0][1] = 1.6714f;
          t.xs[1] = 0.5f; t.vals[1][0] = 0.99209f; t.vals[1][1] = 3.1138f;
          t.xs[2] = 1.0f; t.vals[2][0] = 0.58394f; t.vals[2][1] = 4.5713f;
        }
        d.knobDef[2] = 0.0f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 3;
          t.params[0] = 11; t.params[1] = 12; t.params[2] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -14.96f; t.vals[0][1] = 0.012578f; t.vals[0][2] = 0.0033266f;
          t.xs[1] = 0.25f; t.vals[1][0] = -8.7715f; t.vals[1][1] = -0.0088615f; t.vals[1][2] = 2.0017f;
          t.xs[2] = 0.5f; t.vals[2][0] = 6.0474f; t.vals[2][1] = -0.0019538f; t.vals[2][2] = 4.0063f;
          t.xs[3] = 0.75f; t.vals[3][0] = 10.006f; t.vals[3][1] = 0.0014082f; t.vals[3][2] = 4.5039f;
          t.xs[4] = 1.0f; t.vals[4][0] = 14.021f; t.vals[4][1] = -0.00093777f; t.vals[4][2] = 5.0075f;
        }
        d.knobDef[3] = 1.0f;
        { auto& t = d.knobs[3]; t.points = 3; t.steps = true; t.nParams = 5;
          t.params[0] = 9; t.params[1] = 10; t.params[2] = 11; t.params[3] = 12; t.params[4] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -46.089f; t.vals[0][1] = -0.28284f; t.vals[0][2] = -30.091f; t.vals[0][3] = 0.14534f; t.vals[0][4] = 0.032982f;
          t.xs[1] = 0.5f; t.vals[1][0] = -40.273f; t.vals[1][1] = -0.13987f; t.vals[1][2] = -14.794f; t.vals[1][3] = 0.081136f; t.vals[1][4] = 1.0198f;
          t.xs[2] = 1.0f; t.vals[2][0] = -25.88f; t.vals[2][1] = -0.14169f; t.vals[2][2] = -22.8f; t.vals[2][3] = 0.078746f; t.vals[2][4] = 0.0f;
        }
        d.knobDef[4] = 0.26667f;
        { auto& t = d.knobs[4]; t.points = 6; t.steps = false; t.nParams = 6;
          t.params[0] = 1; t.params[1] = 2; t.params[2] = 3; t.params[3] = 6; t.params[4] = 4; t.params[5] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -7.9412f; t.vals[0][1] = 0.097089f; t.vals[0][2] = 59.161f; t.vals[0][3] = 0.53587f; t.vals[0][4] = -3.3542f; t.vals[0][5] = -0.14547f;
          t.xs[1] = 0.25f; t.vals[1][0] = -13.551f; t.vals[1][1] = 2.1865f; t.vals[1][2] = 28.544f; t.vals[1][3] = 4.1501f; t.vals[1][4] = -4.375f; t.vals[1][5] = -0.59416f;
          t.xs[2] = 0.26667f; t.vals[2][0] = -13.93f; t.vals[2][1] = 2.3391f; t.vals[2][2] = 29.361f; t.vals[2][3] = 4.5713f; t.vals[2][4] = -4.4999f; t.vals[2][5] = -0.60415f;
          t.xs[3] = 0.5f; t.vals[3][0] = -22.07f; t.vals[3][1] = 2.6738f; t.vals[3][2] = 31.129f; t.vals[3][3] = 8.8051f; t.vals[3][4] = -4.4847f; t.vals[3][5] = -0.66879f;
          t.xs[4] = 0.75f; t.vals[4][0] = -30.866f; t.vals[4][1] = 2.7479f; t.vals[4][2] = 28.833f; t.vals[4][3] = 16.682f; t.vals[4][4] = -4.4887f; t.vals[4][5] = -0.77748f;
          t.xs[5] = 1.0f; t.vals[5][0] = -60.85f; t.vals[5][1] = 2.6449f; t.vals[5][2] = 31.213f; t.vals[5][3] = 41.003f; t.vals[5][4] = -4.4279f; t.vals[5][5] = -0.78991f;
        }
        d.knobDef[5] = 0.36667f;
        { auto& t = d.knobs[5]; t.points = 6; t.steps = false; t.nParams = 3;
          t.params[0] = 4; t.params[1] = 1; t.params[2] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -4.484f; t.vals[0][1] = -13.175f; t.vals[0][2] = 4.3561f;
          t.xs[1] = 0.25f; t.vals[1][0] = -4.483f; t.vals[1][1] = -13.119f; t.vals[1][2] = 4.2193f;
          t.xs[2] = 0.36667f; t.vals[2][0] = -4.4999f; t.vals[2][1] = -13.93f; t.vals[2][2] = 4.5713f;
          t.xs[3] = 0.5f; t.vals[3][0] = -3.1217f; t.vals[3][1] = -14.781f; t.vals[3][2] = 4.9029f;
          t.xs[4] = 0.75f; t.vals[4][0] = -1.9298f; t.vals[4][1] = -17.122f; t.vals[4][2] = 3.4952f;
          t.xs[5] = 1.0f; t.vals[5][0] = -1.1082f; t.vals[5][1] = -22.328f; t.vals[5][2] = 2.7815f;
        }
        d.knobDef[6] = 0.44444f;
        { auto& t = d.knobs[6]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.4465f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.79864f;
          t.xs[2] = 0.44444f; t.vals[2][0] = -0.60415f;
          t.xs[3] = 0.5f; t.vals[3][0] = -0.55636f;
          t.xs[4] = 0.75f; t.vals[4][0] = -0.35143f;
          t.xs[5] = 1.0f; t.vals[5][0] = -0.16015f;
        }
        d.knobDef[7] = 0.0f;
        { auto& t = d.knobs[7]; t.points = 5; t.steps = false; t.nParams = 3;
          t.params[0] = 15; t.params[1] = 16; t.params[2] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.5578f; t.vals[0][1] = 2.0746f; t.vals[0][2] = 4.5713f;
          t.xs[1] = 0.25f; t.vals[1][0] = 2.3544f; t.vals[1][1] = 3.3049f; t.vals[1][2] = 4.0619f;
          t.xs[2] = 0.5f; t.vals[2][0] = 4.7554f; t.vals[2][1] = 4.5392f; t.vals[2][2] = 3.5326f;
          t.xs[3] = 0.75f; t.vals[3][0] = 6.9176f; t.vals[3][1] = 5.8769f; t.vals[3][2] = 3.0055f;
          t.xs[4] = 1.0f; t.vals[4][0] = 8.8016f; t.vals[4][1] = 7.0745f; t.vals[4][2] = 2.4861f;
        }
        d.knobDef[8] = 1.0f;
        { auto& t = d.knobs[8]; t.points = 3; t.steps = true; t.nParams = 3;
          t.params[0] = 15; t.params[1] = 16; t.params[2] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -3.9089f; t.vals[0][1] = 2.059f; t.vals[0][2] = 6.1037f;
          t.xs[1] = 0.5f; t.vals[1][0] = -0.63126f; t.vals[1][1] = -4.0542f; t.vals[1][2] = 5.4981f;
          t.xs[2] = 1.0f; t.vals[2][0] = 0.5578f; t.vals[2][1] = 2.0746f; t.vals[2][2] = 4.5713f;
        }
        d.knobDef[9] = 0.0f;
        { auto& t = d.knobs[9]; t.points = 5; t.steps = false; t.nParams = 2;
          t.params[0] = 7; t.params[1] = 8;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.0038331f; t.vals[0][1] = 1.0037f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.25028f; t.vals[1][1] = 0.74996f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.49495f; t.vals[2][1] = 0.5043f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.74383f; t.vals[3][1] = 0.25489f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.0001f; t.vals[4][1] = -0.00011593f;
        }
        d.knobDef[10] = 0.5f;
        { auto& t = d.knobs[10]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -19.998f;
          t.xs[1] = 0.25f; t.vals[1][0] = -9.998f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.0011255f;
          t.xs[3] = 0.75f; t.vals[3][0] = 10.001f;
          t.xs[4] = 1.0f; t.vals[4][0] = 20.0f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specVC76()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -41.941f, 4.0904f, 9.6484f, -4.4966f, -1.559f, 13.605f, 1.0f, 1.0f, -60.0f, 0.0f, 6.7167f, 0.69941f, 0.0f, -2.4026f, 0.03039f, 0.74763f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 6;
        d.knobDef[0] = 1.0f;
        { auto& t = d.knobs[0]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 0;
          t.xs[0] = 0.0f; t.vals[0][0] = -120.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = -39.043f;
          t.xs[2] = 0.5f; t.vals[2][0] = -24.208f;
          t.xs[3] = 0.75f; t.vals[3][0] = -11.899f;
          t.xs[4] = 1.0f; t.vals[4][0] = -0.097183f;
        }
        d.knobDef[1] = 1.0f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = true; t.nParams = 8;
          t.params[0] = 1; t.params[1] = 2; t.params[2] = 3; t.params[3] = 6; t.params[4] = 4; t.params[5] = 5; t.params[6] = 9; t.params[7] = 10;
          t.xs[0] = 0.0f; t.vals[0][0] = -20.573f; t.vals[0][1] = 5.2885f; t.vals[0][2] = 15.36f; t.vals[0][3] = 10.456f; t.vals[0][4] = -4.4862f; t.vals[0][5] = -1.9606f; t.vals[0][6] = -51.882f; t.vals[0][7] = 0.99944f;
          t.xs[1] = 0.2f; t.vals[1][0] = -43.712f; t.vals[1][1] = 2.2208f; t.vals[1][2] = 17.52f; t.vals[1][3] = 11.135f; t.vals[1][4] = -4.4281f; t.vals[1][5] = -0.47916f; t.vals[1][6] = -37.316f; t.vals[1][7] = 0.99575f;
          t.xs[2] = 0.4f; t.vals[2][0] = -42.571f; t.vals[2][1] = 2.6075f; t.vals[2][2] = 12.313f; t.vals[2][3] = 11.165f; t.vals[2][4] = -4.4639f; t.vals[2][5] = -0.45718f; t.vals[2][6] = -37.198f; t.vals[2][7] = 0.99475f;
          t.xs[3] = 0.6f; t.vals[3][0] = -42.256f; t.vals[3][1] = 2.6418f; t.vals[3][2] = 8.9448f; t.vals[3][3] = 11.107f; t.vals[3][4] = -4.3973f; t.vals[3][5] = -0.37741f; t.vals[3][6] = -12.441f; t.vals[3][7] = -0.42693f;
          t.xs[4] = 0.8f; t.vals[4][0] = -40.103f; t.vals[4][1] = 3.2484f; t.vals[4][2] = 11.014f; t.vals[4][3] = 11.125f; t.vals[4][4] = -4.4988f; t.vals[4][5] = -0.33898f; t.vals[4][6] = -8.6141f; t.vals[4][7] = -0.89078f;
          t.xs[5] = 1.0f; t.vals[5][0] = -41.351f; t.vals[5][1] = 4.9213f; t.vals[5][2] = 12.215f; t.vals[5][3] = 13.548f; t.vals[5][4] = -4.4944f; t.vals[5][5] = -1.5552f; t.vals[5][6] = -50.459f; t.vals[5][7] = 0.99944f;
        }
        d.knobDef[2] = 0.5f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 4;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.9517f;
          t.xs[1] = 0.25f; t.vals[1][0] = -3.1329f;
          t.xs[2] = 0.5f; t.vals[2][0] = -4.4966f;
          t.xs[3] = 0.75f; t.vals[3][0] = -4.4986f;
          t.xs[4] = 1.0f; t.vals[4][0] = -4.4993f;
        }
        d.knobDef[3] = 0.8959f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.53353f;
          t.xs[1] = 0.25f; t.vals[1][0] = -0.69028f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.88403f;
          t.xs[3] = 0.75f; t.vals[3][0] = -1.2026f;
          t.xs[4] = 0.8959f; t.vals[4][0] = -1.559f;
          t.xs[5] = 1.0f; t.vals[5][0] = -3.4999f;
        }
        d.knobDef[4] = 0.24f;
        { auto& t = d.knobs[4]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -97.338f;
          t.xs[1] = 0.24f; t.vals[1][0] = 0.0f;
          t.xs[2] = 0.25f; t.vals[2][0] = 0.96082f;
          t.xs[3] = 0.5f; t.vals[3][0] = 12.945f;
          t.xs[4] = 0.75f; t.vals[4][0] = 24.906f;
          t.xs[5] = 1.0f; t.vals[5][0] = 36.908f;
        }
        d.knobDef[5] = 1.0f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 7;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0030746f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.052229f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.10186f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.55303f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.0045f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specVC2A()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -25.501f, 2.8364f, 5.2228f, -1.9872f, -1.3837f, 8.4668f, 0.0f, 1.0f, -60.0f, 0.0f, -0.90127f, -0.0026837f, 0.0f, -3.9996f, 1.5905f, 2.0139f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.19237f, 0.19993f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 5;
        d.knobDef[0] = 0.324f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -39.81f;
          t.xs[1] = 0.25f; t.vals[1][0] = 4.1392f;
          t.xs[2] = 0.324f; t.vals[2][0] = 8.4668f;
          t.xs[3] = 0.5f; t.vals[3][0] = 16.684f;
          t.xs[4] = 0.75f; t.vals[4][0] = 27.831f;
          t.xs[5] = 1.0f; t.vals[5][0] = 40.206f;
        }
        d.knobDef[1] = 0.614f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 3;
          t.params[0] = 1; t.params[1] = 2; t.params[2] = 3;
          t.xs[0] = 0.0f; t.vals[0][0] = 9.2243f; t.vals[0][1] = 6.7011f; t.vals[0][2] = 14.524f;
          t.xs[1] = 0.25f; t.vals[1][0] = -7.8621f; t.vals[1][1] = 2.0062f; t.vals[1][2] = 1.9273e-05f;
          t.xs[2] = 0.5f; t.vals[2][0] = -20.628f; t.vals[2][1] = 2.6103f; t.vals[2][2] = 4.4914f;
          t.xs[3] = 0.614f; t.vals[3][0] = -25.501f; t.vals[3][1] = 2.8364f; t.vals[3][2] = 5.2228f;
          t.xs[4] = 0.75f; t.vals[4][0] = -31.259f; t.vals[4][1] = 2.9804f; t.vals[4][2] = 5.6707f;
          t.xs[5] = 1.0f; t.vals[5][0] = -43.904f; t.vals[5][1] = 2.2638f; t.vals[5][2] = 2.8141f;
        }
        d.knobDef[2] = 0.0f;
        { auto& t = d.knobs[2]; t.points = 2; t.steps = true; t.nParams = 5;
          t.params[0] = 1; t.params[1] = 2; t.params[2] = 3; t.params[3] = 4; t.params[4] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -25.498f; t.vals[0][1] = 2.8358f; t.vals[0][2] = 5.2535f; t.vals[0][3] = -1.9868f; t.vals[0][4] = -1.381f;
          t.xs[1] = 1.0f; t.vals[1][0] = -25.04f; t.vals[1][1] = 4.3968f; t.vals[1][2] = 6.3705f; t.vals[1][3] = -1.95f; t.vals[1][4] = -1.3873f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 7;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.00050622f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.050644f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.10183f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.55953f;
          t.xs[4] = 1.0f; t.vals[4][0] = 1.0154f;
        }
        d.knobDef[4] = 0.0f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 13;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.3014f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.3066f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.0104f;
          t.xs[4] = 1.0f; t.vals[4][0] = 3.1524f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specVC160()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -27.312f, 2.1079f, 3.2862f, -4.0244f, -2.8485f, 7.0093f, 0.14875f, 1.0f, -60.0f, 0.0f, -40.004f, 0.34173f, 0.0f, -1.4331f, 2.1085f, 2.3594f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 5;
        d.knobDef[0] = 0.40824f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = -52.96f;
          t.xs[1] = 0.25f; t.vals[1][0] = -37.183f;
          t.xs[2] = 0.40824f; t.vals[2][0] = -27.312f;
          t.xs[3] = 0.5f; t.vals[3][0] = -21.429f;
          t.xs[4] = 0.75f; t.vals[4][0] = -5.9728f;
          t.xs[5] = 1.0f; t.vals[5][0] = -1.5695f;
        }
        d.knobDef[1] = 0.5f;
        { auto& t = d.knobs[1]; t.points = 5; t.steps = false; t.nParams = 2;
          t.params[0] = 2; t.params[1] = 3;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.019016f; t.vals[0][1] = 59.929f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.0609f; t.vals[1][1] = 2.9663f;
          t.xs[2] = 0.5f; t.vals[2][0] = 2.0874f; t.vals[2][1] = 2.675f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.3596f; t.vals[3][1] = 0.85326f;
          t.xs[4] = 1.0f; t.vals[4][0] = 6.9522f; t.vals[4][1] = 1.2288f;
        }
        d.knobDef[2] = 0.64382f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -21.71f;
          t.xs[1] = 0.25f; t.vals[1][0] = -10.85f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.25379f;
          t.xs[3] = 0.64382f; t.vals[3][0] = 7.0093f;
          t.xs[4] = 0.75f; t.vals[4][0] = 12.007f;
          t.xs[5] = 1.0f; t.vals[5][0] = 23.736f;
        }
        d.knobDef[3] = 0.53f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 7;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.0048913f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.044877f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.09466f;
          t.xs[3] = 0.53f; t.vals[3][0] = 0.14875f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.54097f;
          t.xs[5] = 1.0f; t.vals[5][0] = 0.98579f;
        }
        d.knobDef[4] = 0.0f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 13;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 1.5685f;
          t.xs[2] = 0.5f; t.vals[2][0] = 1.8962f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.0088f;
          t.xs[4] = 1.0f; t.vals[4][0] = 3.144f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specSolidBuscomp()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -24.652f, 1.4687f, 11.19f, -1.6f, -1.0f, 0.22503f, 0.0f, 1.0f, -60.0f, 0.0f, -25.0f, 0.0f, 0.0f, -5.0f, 0.0f, 0.0f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 7;
        d.knobDef[0] = 0.404f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = -42.067f;
          t.xs[1] = 0.25f; t.vals[1][0] = -32.517f;
          t.xs[2] = 0.404f; t.vals[2][0] = -24.652f;
          t.xs[3] = 0.5f; t.vals[3][0] = -23.033f;
          t.xs[4] = 0.75f; t.vals[4][0] = -13.606f;
          t.xs[5] = 1.0f; t.vals[5][0] = -2.8645f;
        }
        d.knobDef[1] = 0.28862f;
        { auto& t = d.knobs[1]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -7.8504f;
          t.xs[1] = 0.25f; t.vals[1][0] = -1.3133f;
          t.xs[2] = 0.28862f; t.vals[2][0] = 0.22503f;
          t.xs[3] = 0.5f; t.vals[3][0] = 5.2825f;
          t.xs[4] = 0.75f; t.vals[4][0] = 11.849f;
          t.xs[5] = 1.0f; t.vals[5][0] = 18.418f;
        }
        d.knobDef[2] = 1.0f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = true; t.nParams = 1;
          t.params[0] = 4;
          t.xs[0] = 0.0f; t.vals[0][0] = -4.5f;
          t.xs[1] = 0.2f; t.vals[1][0] = -4.4999f;
          t.xs[2] = 0.4f; t.vals[2][0] = -4.4999f;
          t.xs[3] = 0.6f; t.vals[3][0] = -3.047f;
          t.xs[4] = 0.8f; t.vals[4][0] = -2.4267f;
          t.xs[5] = 1.0f; t.vals[5][0] = -1.8717f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 6; t.steps = true; t.nParams = 3;
          t.params[0] = 5; t.params[1] = 23; t.params[2] = 24;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.92219f; t.vals[0][1] = -0.87746f; t.vals[0][2] = 0.023019f;
          t.xs[1] = 0.2f; t.vals[1][0] = -0.7493f; t.vals[1][1] = -0.6133f; t.vals[1][2] = 0.030701f;
          t.xs[2] = 0.4f; t.vals[2][0] = -0.65075f; t.vals[2][1] = -0.47551f; t.vals[2][2] = 0.038914f;
          t.xs[3] = 0.6f; t.vals[3][0] = -0.49634f; t.vals[3][1] = 1.4827f; t.vals[3][2] = 0.076478f;
          t.xs[4] = 0.8f; t.vals[4][0] = -0.24442f; t.vals[4][1] = 1.486f; t.vals[4][2] = 0.12288f;
          t.xs[5] = 1.0f; t.vals[5][0] = -0.062731f; t.vals[5][1] = 1.4601f; t.vals[5][2] = 0.18224f;
        }
        d.knobDef[4] = 0.6f;
        { auto& t = d.knobs[4]; t.points = 6; t.steps = true; t.nParams = 2;
          t.params[0] = 2; t.params[1] = 3;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.76005f; t.vals[0][1] = 36.635f;
          t.xs[1] = 0.2f; t.vals[1][0] = 1.1964f; t.vals[1][1] = 29.78f;
          t.xs[2] = 0.4f; t.vals[2][0] = 1.5273f; t.vals[2][1] = 19.112f;
          t.xs[3] = 0.6f; t.vals[3][0] = 1.8725f; t.vals[3][1] = 17.885f;
          t.xs[4] = 0.8f; t.vals[4][0] = 2.1234f; t.vals[4][1] = 16.903f;
          t.xs[5] = 1.0f; t.vals[5][0] = 2.2305f; t.vals[5][1] = 0.12793f;
        }
        d.knobDef[5] = 0.0f;
        { auto& t = d.knobs[5]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 7;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.041149f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.21734f;
          t.xs[2] = 0.5f; t.vals[2][0] = 0.47336f;
          t.xs[3] = 0.75f; t.vals[3][0] = 0.72823f;
          t.xs[4] = 1.0f; t.vals[4][0] = 0.98202f;
        }
        d.knobDef[6] = 0.785f;
        { auto& t = d.knobs[6]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -120.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = -10.448f;
          t.xs[2] = 0.5f; t.vals[2][0] = -4.4212f;
          t.xs[3] = 0.75f; t.vals[3][0] = -0.89665f;
          t.xs[4] = 0.785f; t.vals[4][0] = 0.0f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.6031f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specSolidDynamics()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -42.193f, 0.83766f, 10.809f, -1.5191f, -1.3024f, 9.5576f, 0.0f, 1.0f, -60.0f, 0.0f, -51.918f, 0.99944f, 0.0f, -3.9999f, 0.32712f, 0.23686f, 100.0f, -30.657f, 4.9935f, 0.0f, -2.05f, -1.3052f, 0.0f, 0.0f, 0.0f, 0.0f, -0.52339f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 14;
        d.knobDef[0] = 0.39393f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 2; t.params[1] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.00067215f; t.vals[0][1] = 7.0373f;
          t.xs[1] = 0.25f; t.vals[1][0] = 0.54378f; t.vals[1][1] = 8.9873f;
          t.xs[2] = 0.39393f; t.vals[2][0] = 0.83766f; t.vals[2][1] = 9.5576f;
          t.xs[3] = 0.5f; t.vals[3][0] = 1.0467f; t.vals[3][1] = 9.8903f;
          t.xs[4] = 0.75f; t.vals[4][0] = 1.4946f; t.vals[4][1] = 10.481f;
          t.xs[5] = 1.0f; t.vals[5][0] = 1.8741f; t.vals[5][1] = 11.143f;
        }
        d.knobDef[1] = 1.0f;
        { auto& t = d.knobs[1]; t.points = 2; t.steps = true; t.nParams = 2;
          t.params[0] = 3; t.params[1] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = 35.351f; t.vals[0][1] = -33.114f;
          t.xs[1] = 1.0f; t.vals[1][0] = 10.605f; t.vals[1][1] = -42.218f;
        }
        d.knobDef[2] = 0.81f;
        { auto& t = d.knobs[2]; t.points = 6; t.steps = false; t.nParams = 2;
          t.params[0] = 1; t.params[1] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -15.84f; t.vals[0][1] = 6.963f;
          t.xs[1] = 0.25f; t.vals[1][0] = -24.078f; t.vals[1][1] = 7.7384f;
          t.xs[2] = 0.5f; t.vals[2][0] = -32.172f; t.vals[2][1] = 8.3898f;
          t.xs[3] = 0.75f; t.vals[3][0] = -40.295f; t.vals[3][1] = 9.2757f;
          t.xs[4] = 0.81f; t.vals[4][0] = -42.193f; t.vals[4][1] = 9.5576f;
          t.xs[5] = 1.0f; t.vals[5][0] = -48.536f; t.vals[5][1] = 11.075f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 2; t.steps = true; t.nParams = 3;
          t.params[0] = 5; t.params[1] = 23; t.params[2] = 24;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.3007f; t.vals[0][1] = 1.4994f; t.vals[0][2] = 0.0003998f;
          t.xs[1] = 1.0f; t.vals[1][0] = -0.54948f; t.vals[1][1] = 1.4959f; t.vals[1][2] = 0.00039887f;
        }
        d.knobDef[4] = 0.0f;
        { auto& t = d.knobs[4]; t.points = 2; t.steps = true; t.nParams = 3;
          t.params[0] = 4; t.params[1] = 14; t.params[2] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.5193f; t.vals[0][1] = -3.9997f; t.vals[0][2] = -42.207f;
          t.xs[1] = 1.0f; t.vals[1][0] = -3.0585f; t.vals[1][1] = -3.9999f; t.vals[1][2] = -44.334f;
        }
        d.knobDef[5] = 0.29f;
        { auto& t = d.knobs[5]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 5;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.8087f;
          t.xs[1] = 0.25f; t.vals[1][0] = -1.3672f;
          t.xs[2] = 0.29f; t.vals[2][0] = -1.3024f;
          t.xs[3] = 0.5f; t.vals[3][0] = -0.97438f;
          t.xs[4] = 0.75f; t.vals[4][0] = -0.64829f;
          t.xs[5] = 1.0f; t.vals[5][0] = -0.35651f;
        }
        d.knobDef[6] = 0.0f;
        { auto& t = d.knobs[6]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 20;
          t.xs[0] = 0.0f; t.vals[0][0] = 0.0f;
          t.xs[1] = 0.25f; t.vals[1][0] = 10.89f;
          t.xs[2] = 0.5f; t.vals[2][0] = 22.587f;
          t.xs[3] = 0.75f; t.vals[3][0] = 34.269f;
          t.xs[4] = 1.0f; t.vals[4][0] = 46.012f;
        }
        d.knobDef[7] = 0.75f;
        { auto& t = d.knobs[7]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 18;
          t.xs[0] = 0.0f; t.vals[0][0] = -119.3f;
          t.xs[1] = 0.25f; t.vals[1][0] = -119.16f;
          t.xs[2] = 0.5f; t.vals[2][0] = -48.113f;
          t.xs[3] = 0.75f; t.vals[3][0] = -30.655f;
          t.xs[4] = 1.0f; t.vals[4][0] = -15.055f;
        }
        d.knobDef[8] = 0.0f;
        { auto& t = d.knobs[8]; t.points = 2; t.steps = true; t.nParams = 1;
          t.params[0] = 19;
          t.xs[0] = 0.0f; t.vals[0][0] = 4.9935f;
          t.xs[1] = 1.0f; t.vals[1][0] = 1.0239f;
        }
        d.knobDef[9] = 0.0f;
        { auto& t = d.knobs[9]; t.points = 2; t.steps = true; t.nParams = 1;
          t.params[0] = 21;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.0502f;
          t.xs[1] = 1.0f; t.vals[1][0] = -4.4999f;
        }
        d.knobDef[10] = 0.5f;
        { auto& t = d.knobs[10]; t.points = 5; t.steps = false; t.nParams = 2;
          t.params[0] = 22; t.params[1] = 27;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.745f; t.vals[0][1] = -0.92276f;
          t.xs[1] = 0.25f; t.vals[1][0] = -1.612f; t.vals[1][1] = -0.69993f;
          t.xs[2] = 0.5f; t.vals[2][0] = -1.3052f; t.vals[2][1] = -0.52339f;
          t.xs[3] = 0.75f; t.vals[3][0] = -2.0119f; t.vals[3][1] = -0.22735f;
          t.xs[4] = 1.0f; t.vals[4][0] = -1.8598f; t.vals[4][1] = 0.077232f;
        }
        d.knobDef[11] = 0.74833f;
        { auto& t = d.knobs[11]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -104.66f;
          t.xs[1] = 0.25f; t.vals[1][0] = -50.909f;
          t.xs[2] = 0.5f; t.vals[2][0] = -15.203f;
          t.xs[3] = 0.74833f; t.vals[3][0] = 0.0f;
          t.xs[4] = 0.75f; t.vals[4][0] = 0.10257f;
          t.xs[5] = 1.0f; t.vals[5][0] = 15.405f;
        }
        d.knobDef[12] = 0.0f;
        { auto& t = d.knobs[12]; t.points = 5; t.steps = false; t.nParams = 3;
          t.params[0] = 6; t.params[1] = 2; t.params[2] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = 9.6016f; t.vals[0][1] = 0.83536f; t.vals[0][2] = -42.363f;
          t.xs[1] = 0.25f; t.vals[1][0] = 9.6078f; t.vals[1][1] = 0.83405f; t.vals[1][2] = -42.355f;
          t.xs[2] = 0.5f; t.vals[2][0] = 9.9585f; t.vals[2][1] = 0.71515f; t.vals[2][2] = -43.242f;
          t.xs[3] = 0.75f; t.vals[3][0] = 11.395f; t.vals[3][1] = 0.43934f; t.vals[3][2] = -45.518f;
          t.xs[4] = 1.0f; t.vals[4][0] = 16.876f; t.vals[4][1] = 0.14912f; t.vals[4][2] = -48.057f;
        }
        d.knobDef[13] = 1.0f;
        { auto& t = d.knobs[13]; t.points = 2; t.steps = true; t.nParams = 2;
          t.params[0] = 6; t.params[1] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.38008f; t.vals[0][1] = -32.489f;
          t.xs[1] = 1.0f; t.vals[1][0] = 9.6019f; t.vals[1][1] = -42.321f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specFastComp()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 2.1218f, -29.523f, 2.972f, 11.479f, -3.497f, -0.90831f, 22.62f, 0.0f, 1.0f, -60.0f, 0.0f, -10.423f, 0.00057834f, 0.0f, -2.5772f, 0.95522f, 1.6473f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 3;
        d.knobDef[0] = 0.62829f;
        { auto& t = d.knobs[0]; t.points = 6; t.steps = false; t.nParams = 1;
          t.params[0] = 0;
          t.xs[0] = 0.0f; t.vals[0][0] = -22.699f;
          t.xs[1] = 0.25f; t.vals[1][0] = -12.699f;
          t.xs[2] = 0.5f; t.vals[2][0] = -2.7842f;
          t.xs[3] = 0.62829f; t.vals[3][0] = 2.1218f;
          t.xs[4] = 0.75f; t.vals[4][0] = 6.8492f;
          t.xs[5] = 1.0f; t.vals[5][0] = 17.284f;
        }
        d.knobDef[1] = 0.5f;
        { auto& t = d.knobs[1]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -24.027f;
          t.xs[1] = 0.25f; t.vals[1][0] = -12.014f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.0068033f;
          t.xs[3] = 0.75f; t.vals[3][0] = 2.9933f;
          t.xs[4] = 1.0f; t.vals[4][0] = 5.9937f;
        }
        d.knobDef[2] = 0.5f;
        { auto& t = d.knobs[2]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 4;
          t.xs[0] = 0.0f; t.vals[0][0] = -4.4983f;
          t.xs[1] = 0.25f; t.vals[1][0] = -4.4917f;
          t.xs[2] = 0.5f; t.vals[2][0] = -3.497f;
          t.xs[3] = 0.75f; t.vals[3][0] = -3.2312f;
          t.xs[4] = 1.0f; t.vals[4][0] = -3.0722f;
        }
        return d;
    }();
    return s;
}

inline const DynSpec& specStompCompressor()
{
    static const DynSpec s = [] {
        DynSpec d {};
        const float b[dpCount] { 0.0f, -26.928f, 1.1154f, 0.21341f, -1.0115f, -1.1699f, 5.3206f, 0.0f, 1.0f, -60.0f, 0.0f, -9.9558f, 0.0059634f, 0.0f, -3.6546f, 0.082064f, 0.83403f, 100.0f, -120.0f, 0.0f, 0.0f, -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -3.0f };
        for (int i = 0; i < dpCount; ++i) d.base[i] = b[i];
        d.nKnobs = 5;
        d.knobDef[0] = 0.5f;
        { auto& t = d.knobs[0]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 26;
          t.xs[0] = 0.0f; t.vals[0][0] = -117.8f;
          t.xs[1] = 0.25f; t.vals[1][0] = -6.031f;
          t.xs[2] = 0.5f; t.vals[2][0] = -0.002689f;
          t.xs[3] = 0.75f; t.vals[3][0] = 3.5226f;
          t.xs[4] = 1.0f; t.vals[4][0] = 6.0221f;
        }
        d.knobDef[1] = 0.0f;
        { auto& t = d.knobs[1]; t.points = 2; t.steps = true; t.nParams = 5;
          t.params[0] = 0; t.params[1] = 1; t.params[2] = 2; t.params[3] = 3; t.params[4] = 6;
          t.xs[0] = 0.0f; t.vals[0][0] = -0.0017321f; t.vals[0][1] = -26.938f; t.vals[0][2] = 1.1154f; t.vals[0][3] = 0.094819f; t.vals[0][4] = 5.3219f;
          t.xs[1] = 1.0f; t.vals[1][0] = 13.441f; t.vals[1][1] = -40.185f; t.vals[1][2] = 0.99188f; t.vals[1][3] = 3.8172f; t.vals[1][4] = 18.045f;
        }
        d.knobDef[2] = 1.0f;
        { auto& t = d.knobs[2]; t.points = 2; t.steps = true; t.nParams = 6;
          t.params[0] = 4; t.params[1] = 1; t.params[2] = 2; t.params[3] = 3; t.params[4] = 9; t.params[5] = 10;
          t.xs[0] = 0.0f; t.vals[0][0] = -2.8989f; t.vals[0][1] = -23.902f; t.vals[0][2] = 3.9139f; t.vals[0][3] = 9.4291f; t.vals[0][4] = -2.5474f; t.vals[0][5] = 1.2293e-05f;
          t.xs[1] = 1.0f; t.vals[1][0] = -1.0139f; t.vals[1][1] = -26.932f; t.vals[1][2] = 1.1153f; t.vals[1][3] = 0.0871f; t.vals[1][4] = -48.134f; t.vals[1][5] = 0.99944f;
        }
        d.knobDef[3] = 0.0f;
        { auto& t = d.knobs[3]; t.points = 2; t.steps = true; t.nParams = 3;
          t.params[0] = 5; t.params[1] = 23; t.params[2] = 24;
          t.xs[0] = 0.0f; t.vals[0][0] = -1.1842f; t.vals[0][1] = 0.5403f; t.vals[0][2] = 0.013229f;
          t.xs[1] = 1.0f; t.vals[1][0] = 1.4305f; t.vals[1][1] = 1.4422f; t.vals[1][2] = 0.98668f;
        }
        d.knobDef[4] = 0.5f;
        { auto& t = d.knobs[4]; t.points = 5; t.steps = false; t.nParams = 1;
          t.params[0] = 1;
          t.xs[0] = 0.0f; t.vals[0][0] = -45.43f;
          t.xs[1] = 0.25f; t.vals[1][0] = -36.069f;
          t.xs[2] = 0.5f; t.vals[2][0] = -26.928f;
          t.xs[3] = 0.75f; t.vals[3][0] = -17.068f;
          t.xs[4] = 1.0f; t.vals[4][0] = -5.7001f;
        }
        return d;
    }();
    return s;
}

} // namespace aiofx
