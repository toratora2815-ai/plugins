#pragma once
// 色付け（STYLE）の数値。build/tremlab/fit で市販トレモロの 7 種類の色付けに当てはめた結果（gentab.py で自動生成）
#include "TremoloStyle.h"
namespace mytrem
{
    constexpr int kNumStyles = 8;
    inline StyleParams styleParams (int i)
    {
        StyleParams p;
        switch (i)
        {
            case 0: p.digital = 1; break;
            case 1: p.s1 = 0.744559f; p.b1 = 0.004973f; p.s2 = 0.671794f; p.b2 = 0.00488697f; p.hpHz = 11.0574f; p.hpQ = 0.749822f; break;  // Smooth（測った A）
            case 2: p.s1 = 0.77959f; p.a1 = -0.101186f; p.b1 = 0.100801f; p.s2 = 2.31466f; p.b2 = 0.0638427f; p.hpHz = 8.96943f; p.hpQ = 0.83386f; break;  // Even（測った B）
            case 3: p.compThrDb = -17.6975f; p.compRatio = 2.25028f; p.compKneeDb = 25.6689f; p.compAttMs = 7.61213f; p.compRelMs = 3.85901f; p.compMakeupDb = 0.203997f; p.s1 = 4.2212f; p.s2 = 4.80648f; p.hpHz = 11.0f; break;  // Squeeze（測った C）
            case 4: p.compThrDb = -16.4706f; p.compRatio = 2.41559f; p.compKneeDb = 28.86f; p.compAttMs = 19.0164f; p.compRelMs = 9.34004f; p.scShelfHz = 4956.87f; p.scShelfDb = 11.7456f; p.compMakeupDb = -0.430842f; p.s1 = 4.1352f; p.s2 = 5.03457f; p.lowShelfHz = 1418.06f; p.lowShelfDb = 0.184867f; break;  // Dense（測った G）
            case 5: p.preDb = -0.066922f; p.emphHz = 4692.94f; p.emphDb = 15.6668f; p.s1 = 0.396835f; p.b1 = 0.0558786f; p.s2 = 2.11227f; p.hpHz = 11.0848f; p.lowShelfHz = 2865.87f; p.lowShelfDb = 0.317297f; break;  // Crunch（測った F）
            case 6: p.preDb = 6.38163f; p.s1 = 1.59518f; p.a1 = -0.163455f; p.b1 = 0.755542f; p.s2 = 1.66892f; p.b2 = 0.658656f; p.hpHz = 6.9943f; p.trimDb = -9.13401f; break;  // Grit（測った E）
            case 7: p.compThrDb = -71.7261f; p.compRatio = 17.0013f; p.compKneeDb = 29.4485f; p.compAttMs = 525.214f; p.compRelMs = 1.41494f; p.scHpHz = 3520.39f; p.scShelfHz = 14632.5f; p.scShelfDb = 13.083f; p.compMakeupDb = -0.804956f; p.s1 = 21.1679f; p.hpHz = 11.0f; break;  // Pump（測った D）
            default: p.digital = 1; break;
        }
        return p;
    }
}
