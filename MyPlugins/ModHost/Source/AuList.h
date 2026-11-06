// この Mac に入っている AU エフェクトの一覧（JUCE を使わず AudioToolbox から直接。読み込みはしないので一瞬で終わる）
#pragma once
#include <string>
#include <vector>

namespace mh
{
struct AuEntry
{
    std::string name, manufacturer;
    std::string identifier;   // JUCE の PluginDescription::fileOrIdentifier と同じ形 "AudioUnit:Effects/aufx,xxxx,yyyy"
    bool isMusicEffect = false;
};

// selfSubType / selfManu = 自分自身（入れ子にしない）
std::vector<AuEntry> listAuEffects (const char* selfSubType, const char* selfManu);
}
