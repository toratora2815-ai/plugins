#pragma once
// 音色の型（COLOR）の表。この Mac の中のファイルから読む（公開リポジトリには入れない）
//   ~/Library/Application Support/MyPlugins/LumenRooms/profiles.txt
//   1 行 = "R 2 1 g0 g1 ... g10"（R = リバーブ用 / E = 反射用、グループ、番号、11 帯域の dB）
#include "RoomEngine.h"
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace lumen
{
struct ProfileTable
{
    static constexpr int kCount = 81;   // 0 = なし、1..80 = (グループ-1)*10 + 番号
    std::array<std::array<float, GraphicEQ::B>, kCount> rev {}, er {};
    std::array<bool, kCount> hasRev {}, hasEr {};

    static std::string defaultPath()
    {
        const char* home = std::getenv ("HOME");
        return std::string (home ? home : "") + "/Library/Application Support/MyPlugins/LumenRooms/profiles.txt";
    }
    static int indexOf (int group, int number)
    {
        return (group >= 1 && group <= 8 && number >= 1 && number <= 10) ? (group - 1) * 10 + number : 0;
    }

    int load (const std::string& path)
    {
        std::ifstream in (path);
        int n = 0;
        std::string line;
        while (std::getline (in, line))
        {
            std::istringstream ss (line);
            std::string kind; int g = 0, i = 0;
            if (! (ss >> kind >> g >> i)) continue;
            const int idx = indexOf (g, i);
            if (idx == 0) continue;
            std::array<float, GraphicEQ::B> v {};
            bool ok = true;
            for (auto& x : v) if (! (ss >> x)) ok = false;
            if (! ok) continue;
            if (kind == "R") { rev[(size_t) idx] = v; hasRev[(size_t) idx] = true; ++n; }
            if (kind == "E") { er[(size_t) idx] = v; hasEr[(size_t) idx] = true; ++n; }
        }
        return n;
    }
    const float* revGains (int idx) const { return idx > 0 && idx < kCount && hasRev[(size_t) idx] ? rev[(size_t) idx].data() : nullptr; }
    const float* erGains (int idx) const { return idx > 0 && idx < kCount && hasEr[(size_t) idx] ? er[(size_t) idx].data() : nullptr; }
};
} // namespace lumen
