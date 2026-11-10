#pragma once
// 色付け（STYLE）の数値。build/tremlab/fit と fix2 で市販トレモロの 7 種類の色付けに当てはめた結果（fix2/gentab2.py で自動生成）
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
            case 3: p.compThrDb = -15.5295f; p.compRatio = 2.62448f; p.compKneeDb = 25.8069f; p.compAttMs = 4.54381f; p.compRelMs = 6.68917f; p.scHpHz = 14.2928f; p.scShelfHz = 16748.4f; p.scShelfDb = 0.0532345f; p.compMakeupDb = 0.0191977f; p.s1 = 15.625f; p.s2 = 12.8853f; p.hpHz = 11.0f; p.compDb = 2.0f; break;  // Squeeze（測った C）
            case 4: p.compThrDb = -15.0053f; p.compRatio = 2.66339f; p.compKneeDb = 25.8224f; p.compAttMs = 7.95258f; p.compRelMs = 13.3683f; p.scHpHz = 9.53802f; p.scShelfHz = 19995.7f; p.scShelfDb = 21.5035f; p.compMakeupDb = -0.129955f; p.s1 = 13.6051f; p.s2 = 13.2434f; p.lowShelfHz = 1418.06f; p.lowShelfDb = 0.184867f; p.compDb = 2.0f; break;  // Dense（測った G）
            case 5: p.preDb = 5.76849f; p.emphHz = 181.337f; p.emphDb = 4.00934f; p.compThrDb = -0.153636f; p.compRatio = 58.6481f; p.compKneeDb = 14.2107f; p.compAttMs = 0.0209409f; p.compRelMs = 0.0252972f; p.scHpHz = 9950.55f; p.scShelfHz = 3787.26f; p.scShelfDb = 25.5963f; p.compMakeupDb = -6.53192f; p.s1 = 0.564187f; p.b1 = -0.0971159f; p.s2 = 0.468404f; p.hpHz = 11.0848f; p.lowShelfHz = 19956.2f; p.lowShelfDb = 1.27642f; p.compDb = 2.0f; break;  // Crunch（測った F）
            case 6: p.preDb = 6.25258f; p.s1 = 1.40883f; p.a1 = 0.00102906f; p.b1 = 0.928392f; p.s2 = 1.41361f; p.b2 = 0.492458f; p.hpHz = 5.65155f; p.hpQ = 0.05f; p.trimDb = -7.92675f; break;  // Grit（測った E）
            case 7: p.compThrDb = -67.3486f; p.compRatio = 10.5692f; p.compKneeDb = 13.1552f; p.compAttMs = 5.77088f; p.compRelMs = 5.48904f; p.scHpHz = 10226.1f; p.scShelfHz = 16303.1f; p.scShelfDb = 6.78153f; p.compMakeupDb = -0.0596563f; p.s1 = 0.875924f; p.hpHz = 11.0f; p.compDb = 2.0f; p.compGainMs = 0.0152879f; break;  // Pump（測った D）
            default: p.digital = 1; break;
        }
        return p;
    }
}
