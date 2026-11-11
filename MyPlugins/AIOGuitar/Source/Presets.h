#pragma once
#include <vector>
#include <utility>
#include "Rig.h"

// Factory presets. Values are in real parameter units.
// Parameters not listed fall back to their default value.
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& getFactoryPresets()
{
    // pedals: Cotton Comp = cc*, DRIVE A = pd* (pdModel: 0 Porch Drive, 1 Wool Fuzz, 2 Hearth Drive, 3 Brass Boost, 4 Barn Dist; default Porch),
    //         DRIVE B = wf* (wfModel, default Wool Fuzz), TREM = tr*
    //   trRate 0 1/2, 1 1/4, 2 1/4T, 3 1/8, 4 1/8T, 5 1/16, 6 1/16T, 7 1/32; trShape 0 Sine, 1 Triangle, 2 Vintage, 3 Soft Square, 4 Square
    // amp: ampModel 0 American Clean, 1 Tweed, 2 British Chime, 3 British Crunch, 4 Modern High Gain
    // cab: 0 1x12 Open, 1 2x12 Open, 2 4x12 Closed, 3 4x12 Modern, 4 4x10 Open; mic 0 Dynamic, 1 Condenser, 2 Ribbon
    static const std::vector<Preset> presets
    {
        { "Init", {} },

        { "Clean Sparkle", {
            { "ampOn", 1 }, { "ampModel", 0 }, { "ampGain", 4 }, { "ampBass", 5 }, { "ampMid", 4.5f }, { "ampTreble", 6 },
            { "ampPresence", 6 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 4 }, { "cabMicType", 1 }, { "cabPos", 2 }, { "cabDist", 2 } } },

        { "Comp Clean Funk", {
            { "ccOn", 1 }, { "ccComp", 5 }, { "ccLevel", 0 }, { "ccBlend", 80 },
            { "ampOn", 1 }, { "ampModel", 0 }, { "ampGain", 3 }, { "ampBass", 4 }, { "ampMid", 5 }, { "ampTreble", 6.5f },
            { "ampPresence", 6 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMicType", 0 }, { "cabPos", 2 }, { "cabDist", 1 } } },

        { "Chime Pop", {
            { "ampOn", 1 }, { "ampModel", 2 }, { "ampGain", 5 }, { "ampBass", 4.5f }, { "ampMid", 5 }, { "ampTreble", 5.5f },
            { "ampPresence", 5 }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMicType", 1 }, { "cabPos", 3 }, { "cabDist", 3 } } },

        { "Crunch + Drive", {
            { "pdOn", 1 }, { "pdGain", 3 }, { "pdTone", 6 }, { "pdLevel", 0 },
            { "ampOn", 1 }, { "ampModel", 3 }, { "ampGain", 5 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 6 },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 2 } } },

        { "Metal Rhythm", {
            { "gtRelease", 0.05f },
            { "pdOn", 1 }, { "pdGain", 0.5f }, { "pdTone", 6.5f }, { "pdLevel", 6 },
            { "ampOn", 1 }, { "ampModel", 4 }, { "ampGain", 6 }, { "ampBass", 6 }, { "ampMid", 4 }, { "ampTreble", 6 },
            { "ampPresence", 6 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 3 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 1 } } },

        { "Lead Solo", {
            { "gtRelease", 0.2f },
            { "ccOn", 1 }, { "ccComp", 4 }, { "ccBlend", 70 },
            { "pdOn", 1 }, { "pdGain", 5 }, { "pdTone", 5.5f }, { "pdLevel", 0 },
            { "ampOn", 1 }, { "ampModel", 3 }, { "ampGain", 7 }, { "ampBass", 5 }, { "ampMid", 6.5f }, { "ampTreble", 5.5f },
            { "ampPresence", 5.5f }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 2 }, { "cabPos", 3 }, { "cabDist", 3 } } },

        { "Fuzz Garage", {
            { "wfOn", 1 }, { "wfSustain", 7 }, { "wfTone", 4.5f }, { "wfVolume", -10 },
            { "ampOn", 1 }, { "ampModel", 1 }, { "ampGain", 3 }, { "ampBass", 5.5f }, { "ampMid", 5.5f }, { "ampTreble", 5 },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMicType", 0 }, { "cabPos", 4.5f }, { "cabDist", 2 } } },

        { "Fuzz into Drive", {
            { "fxChain", (float) rig::fromOldPedalOrder (true) },   // Comp > Fuzz > Drive > Amp > Trem
            { "wfOn", 1 }, { "wfSustain", 5 }, { "wfTone", 6 }, { "wfVolume", -14 },
            { "pdOn", 1 }, { "pdGain", 2 }, { "pdTone", 5 }, { "pdLevel", -3 },
            { "ampOn", 1 }, { "ampModel", 3 }, { "ampGain", 4 }, { "ampBass", 5 }, { "ampMid", 6 }, { "ampTreble", 5.5f },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 2 } } },

        // the comp AFTER the drive: the distorted notes come out even (a lead that sits in front)
        { "Even Lead (Drive > Comp)", {
            { "fxChain", (float) rig::indexOf ({ rig::blkDrive, rig::blkComp, rig::blkFuzz, rig::blkAmp, rig::blkTrem }) },
            { "gtRelease", 0.2f },
            { "pdOn", 1 }, { "pdGain", 6 }, { "pdTone", 5.5f }, { "pdLevel", -3 },
            { "ccOn", 1 }, { "ccComp", 5 }, { "ccLevel", 0 }, { "ccBlend", 85 },
            { "ampOn", 1 }, { "ampModel", 2 }, { "ampGain", 5 }, { "ampBass", 4.5f }, { "ampMid", 6 }, { "ampTreble", 5 },
            { "ampPresence", 5 }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 2 }, { "cabPos", 3 }, { "cabDist", 2 } } },

        // 0.4.0: added at the end so that the numbers of the presets above do not change
        // the amp's own tremolo: a clean amp, the TREM right after it (where a real amp has it)
        { "Vintage Amp Trem", {
            { "trOn", 1 }, { "trDepth", 70 }, { "trRate", 4 }, { "trShape", 2 }, { "trSync", 1 },
            { "ampOn", 1 }, { "ampModel", 1 }, { "ampGain", 3.5f }, { "ampBass", 5 }, { "ampMid", 5 }, { "ampTreble", 5.5f },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 0 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 2 } } },
        // 0.6.0: the new pedals in DRIVE A / B (added at the end: the numbers of the presets above stay)
        // the mid-forward overdrive pushing a crunchy amp: tight low end, the mids in front
        { "Hearth Crunch Rhythm", {
            { "pdModel", 2 }, { "pdOn", 1 }, { "pdGain", 2 }, { "pdTone", 5.5f }, { "pdLevel", 0 },
            { "ampOn", 1 }, { "ampModel", 3 }, { "ampGain", 6 }, { "ampBass", 4.5f }, { "ampMid", 6 }, { "ampTreble", 6 },
            { "ampPresence", 5.5f }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 2 } } },

        // the treble booster into a chimey amp: a lead that cuts and sings
        { "Brass Boost Lead", {
            { "gtRelease", 0.2f },
            { "pdModel", 3 }, { "pdOn", 1 }, { "pdGain", 6 }, { "pdTone", 7.5f }, { "pdLevel", -6 },
            { "ampOn", 1 }, { "ampModel", 2 }, { "ampGain", 6 }, { "ampBass", 5 }, { "ampMid", 5.5f }, { "ampTreble", 5 },
            { "ampPresence", 5 }, { "ampMaster", 6 },
            { "cabOn", 1 }, { "cabType", 1 }, { "cabMicType", 2 }, { "cabPos", 3 }, { "cabDist", 2 } } },

        // the hard distortion into a clean amp: alternative / grunge chords
        { "Barn Dist Grunge", {
            { "pdModel", 4 }, { "pdOn", 1 }, { "pdGain", 6.5f }, { "pdTone", 4.5f }, { "pdLevel", -11 },
            { "ampOn", 1 }, { "ampModel", 0 }, { "ampGain", 4 }, { "ampBass", 5.5f }, { "ampMid", 5 }, { "ampTreble", 5.5f },
            { "ampPresence", 5 }, { "ampMaster", 5 },
            { "cabOn", 1 }, { "cabType", 2 }, { "cabMicType", 0 }, { "cabPos", 3 }, { "cabDist", 2 } } }
    };
    return presets;
}
