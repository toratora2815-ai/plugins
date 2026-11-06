#include "AuList.h"
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>

namespace
{
    std::string osTypeToString (OSType t)
    {
        char c[5] = { (char) (t >> 24), (char) (t >> 16), (char) (t >> 8), (char) t, 0 };
        return c;
    }
    OSType stringToOSType (const char* s)
    {
        OSType t = 0;
        for (int i = 0; i < 4; ++i) t = (t << 8) | (OSType) (unsigned char) (s[i] ? s[i] : ' ');
        return t;
    }
    std::string cfToStd (CFStringRef s)
    {
        if (s == nullptr) return {};
        char buf[512] = {};
        if (CFStringGetCString (s, buf, sizeof (buf), kCFStringEncodingUTF8)) return buf;
        return {};
    }
}

std::vector<mh::AuEntry> mh::listAuEffects (const char* selfSubType, const char* selfManu)
{
    std::vector<AuEntry> out;
    const OSType selfSub = stringToOSType (selfSubType), selfM = stringToOSType (selfManu);

    for (OSType type : { (OSType) kAudioUnitType_Effect, (OSType) kAudioUnitType_MusicEffect })
    {
        AudioComponentDescription want {};
        want.componentType = type;
        AudioComponent comp = nullptr;
        while ((comp = AudioComponentFindNext (comp, &want)) != nullptr)
        {
            AudioComponentDescription d {};
            if (AudioComponentGetDescription (comp, &d) != noErr) continue;
            if (d.componentSubType == selfSub && d.componentManufacturer == selfM) continue;

            AuEntry e;
            CFStringRef cf = nullptr;
            if (AudioComponentCopyName (comp, &cf) == noErr && cf != nullptr)
            {
                const std::string full = cfToStd (cf);
                CFRelease (cf);
                const auto colon = full.find (':');
                if (colon != std::string::npos)
                {
                    e.manufacturer = full.substr (0, colon);
                    e.name = full.substr (colon + 1);
                    while (! e.name.empty() && e.name.front() == ' ') e.name.erase (e.name.begin());
                }
                else e.name = full;
            }
            if (e.name.empty()) e.name = osTypeToString (d.componentSubType);
            if (e.manufacturer.empty()) e.manufacturer = osTypeToString (d.componentManufacturer);

            e.isMusicEffect = type == kAudioUnitType_MusicEffect;
            e.identifier = "AudioUnit:Effects/" + osTypeToString (d.componentType) + "," + osTypeToString (d.componentSubType)
                         + "," + osTypeToString (d.componentManufacturer);
            out.push_back (std::move (e));
        }
    }

    std::sort (out.begin(), out.end(), [] (const AuEntry& a, const AuEntry& b)
    {
        if (a.manufacturer != b.manufacturer) return a.manufacturer < b.manufacturer;
        return a.name < b.name;
    });
    return out;
}
