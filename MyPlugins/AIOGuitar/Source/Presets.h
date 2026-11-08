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
    // pedals: Porch Drive = pd*, Wool Fuzz = wf* (same sound as the stand-alone plugins)
    static const std::vector<Preset> presets
    {
        { "Init", {} },

        { "Clean Sparkle", {
            { "ampOn", 1 }, { "ampChannel", 0 }, { "ampGain", 4 }, { "ampBass", 5 }, { "ampMid", 4.5f }, { "ampTreble", 6.5f },
            { "ampPresence", 6 }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMic", 6 },
            { "revMix", 0.25f }, { "revSize", 0.6f } } },

        { "Crunch + Drive", {
            { "pdOn", 1 }, { "pdGain", 3 }, { "pdTone", 6 }, { "pdLevel", 0 },
            { "ampOn", 1 }, { "ampChannel", 1 }, { "ampGain", 5 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 6 },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMic", 5 },
            { "revMix", 0.12f }, { "revSize", 0.45f } } },

        { "Metal Rhythm", {
            { "gtRelease", 0.05f },
            { "pdOn", 1 }, { "pdGain", 0.5f }, { "pdTone", 6.5f }, { "pdLevel", 6 },
            { "ampOn", 1 }, { "ampChannel", 2 }, { "ampGain", 7 }, { "ampBass", 6 }, { "ampMid", 4 }, { "ampTreble", 6.5f },
            { "ampPresence", 6 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMic", 5 },
            { "revMix", 0.06f }, { "revSize", 0.35f } } },

        { "Lead Solo", {
            { "gtRelease", 0.2f },
            { "pdOn", 1 }, { "pdGain", 5 }, { "pdTone", 5.5f }, { "pdLevel", 0 },
            { "ampOn", 1 }, { "ampChannel", 2 }, { "ampGain", 6 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 5.5f },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMic", 5 },
            { "revMix", 0.3f }, { "revSize", 0.75f } } },

        { "Fuzz Garage", {
            { "wfOn", 1 }, { "wfSustain", 7 }, { "wfTone", 4.5f }, { "wfVolume", -10 },
            { "ampOn", 1 }, { "ampChannel", 1 }, { "ampGain", 3.5f }, { "ampBass", 5.5f }, { "ampMid", 5.5f }, { "ampTreble", 5 },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMic", 4.5f },
            { "revMix", 0.18f }, { "revSize", 0.5f } } },

        { "Fuzz into Drive", {
            { "pedalOrder", 1 },
            { "wfOn", 1 }, { "wfSustain", 5 }, { "wfTone", 6 }, { "wfVolume", -14 },
            { "pdOn", 1 }, { "pdGain", 2 }, { "pdTone", 5 }, { "pdLevel", -3 },
            { "ampOn", 1 }, { "ampChannel", 1 }, { "ampGain", 4 }, { "ampBass", 5 }, { "ampMid", 6 }, { "ampTreble", 5.5f },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMic", 5 },
            { "revMix", 0.15f }, { "revSize", 0.5f } } },
    };
    return presets;
}
