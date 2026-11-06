// Forge Strip と Forge Mixer のやりとり（Plugin Link の掲示板を使う。共通部品は変えずに独自の種類番号で）
#pragma once
#include "PluginLinkJuce.h"

namespace fslink
{
    // 種類の番号（PluginLink.h の一覧の外。名前は "?" になるが、FROM は使わないので問題ない）
    constexpr plink::Kind kindStrip = (plink::Kind) 200;
    constexpr plink::Kind kindMixer = (plink::Kind) 201;

    // Forge Strip が出す数値
    namespace strip
    {
        enum { token = 0,      // その Strip の印（1〜2^23 の整数）
               gainDb = 1,     // MASTER の OUTPUT（dB）
               pan = 2,        // -1..1
               width = 3,      // -100..100
               levelDb = 4,    // 出口の音量（ピーク、dB）
               lowDb = 5, midDb = 6, highDb = 7,   // 出口の帯域ごとの音量（200 Hz 以下 / 中 / 4 kHz 以上）
               mute = 8 };
    }
    // Forge Mixer が出す指示（いま動かしている Strip だけ、最大 5 つ）
    namespace mixer
    {
        constexpr int maxCommands = 5;
        enum { stride = 4 };   // [token, gainDb, pan, width] × 5（0〜19）
        enum { count = 20 };   // いくつ書いたか
    }
}
