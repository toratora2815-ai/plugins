#pragma once
#include <vector>
#include <utility>

// Factory presets. Values are in real parameter units.
// Parameters not listed fall back to their default value.
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets
    {
        { "Init", {} },

        { "Clean Sparkle", {
            
            { "compOn", 1 }, { "compThresh", -26 }, { "compRatio", 4 }, { "compAttack", 8 }, { "compRelease", 150 }, { "compMakeup", 6 },
            { "ampOn", 1 }, { "ampChannel", 0 }, { "ampGain", 4 }, { "ampBass", 5 }, { "ampMid", 4.5f }, { "ampTreble", 6.5f },
            { "ampPresence", 6 }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMic", 6 },
            { "revMix", 0.25f }, { "revSize", 0.6f } } },

        { "Funk Comp Clean", {
            { "gtRelease", 0.06f }, 
            { "compOn", 1 }, { "compThresh", -32 }, { "compRatio", 8 }, { "compAttack", 2 }, { "compRelease", 80 }, { "compMakeup", 10 },
            { "ampOn", 1 }, { "ampChannel", 0 }, { "ampGain", 3 }, { "ampBass", 4 }, { "ampMid", 5.5f }, { "ampTreble", 7 },
            { "ampPresence", 6.5f }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMic", 6.5f },
            { "revMix", 0.12f }, { "revSize", 0.35f } } },

        { "Crunch Rock", {
            
            { "drvOn", 1 }, { "drvType", 0 }, { "drvDrive", 3 }, { "drvTone", 6 }, { "drvLevel", 6 },
            { "ampOn", 1 }, { "ampChannel", 1 }, { "ampGain", 6 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 6 },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMic", 5 },
            { "revMix", 0.12f }, { "revSize", 0.45f } } },

        { "Metal Rhythm", {
            { "gtRelease", 0.05f },
            { "gateThresh", -52 },
            { "drvOn", 1 }, { "drvType", 0 }, { "drvDrive", 0.5f }, { "drvTone", 6 }, { "drvLevel", 7.5f },
            { "ampOn", 1 }, { "ampChannel", 2 }, { "ampGain", 7 }, { "ampBass", 6 }, { "ampMid", 4 }, { "ampTreble", 6.5f },
            { "ampPresence", 6 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMic", 5 },
            { "revMix", 0.06f }, { "revSize", 0.35f } } },

        { "Lead Solo", {
            { "gtRelease", 0.2f },
            { "compOn", 1 }, { "compThresh", -18 }, { "compRatio", 3 }, { "compAttack", 10 }, { "compRelease", 200 }, { "compMakeup", 3 },
            { "drvOn", 1 }, { "drvType", 1 }, { "drvDrive", 4 }, { "drvTone", 5.5f }, { "drvLevel", 6 },
            { "ampOn", 1 }, { "ampChannel", 2 }, { "ampGain", 6 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 5.5f },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMic", 5 },
            { "revMix", 0.3f }, { "revSize", 0.75f } } },

        { "Fuzz Garage", {
            
            { "drvOn", 1 }, { "drvType", 2 }, { "drvDrive", 7 }, { "drvTone", 4.5f }, { "drvLevel", 5 },
            { "ampOn", 1 }, { "ampChannel", 1 }, { "ampGain", 3.5f }, { "ampBass", 5.5f }, { "ampMid", 5.5f }, { "ampTreble", 5 },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMic", 4.5f },
            { "revMix", 0.18f }, { "revSize", 0.5f } } },
    };
    return presets;
}
