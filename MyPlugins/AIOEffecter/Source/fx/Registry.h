#pragma once
// 部品の一覧。番号（type）は保存に使うので、並びは変えずに後ろへ足すだけにする（0 = 空き）。
#include "Module.h"
#include "NoiseGate.h"
#include "NoiseReduction.h"
#include "TubeComp.h"
#include "Limiter.h"
#include "StudioReverb.h"
#include "SpringReverb.h"
#include "TwinDelay.h"

namespace aiofx
{
struct Entry
{
    const ModuleInfo& (*info)();
    std::unique_ptr<Module> (*make)();
};

template <typename T> std::unique_ptr<Module> makeModule() { return std::make_unique<T>(); }

inline const std::vector<Entry>& registry()
{
    // 1 番から。0 は空き
    static const std::vector<Entry> r {
        { &NoiseGate::info,      &makeModule<NoiseGate> },        // 1
        { &NoiseReduction::info, &makeModule<NoiseReduction> },   // 2
        { &TubeComp::info,       &makeModule<TubeComp> },         // 3
        { &Limiter::info,        &makeModule<Limiter> },          // 4
        { &StudioReverb::info,   &makeModule<StudioReverb> },     // 5
        { &SpringReverb::info,   &makeModule<SpringReverb> },     // 6
        { &TwinDelay::info,      &makeModule<TwinDelay> },        // 7
    };
    return r;
}

inline int numTypes() { return (int) registry().size(); }
inline const ModuleInfo* infoFor (int type)
{
    if (type < 1 || type > numTypes()) return nullptr;
    return &registry()[(size_t) type - 1].info();
}
inline std::unique_ptr<Module> makeType (int type)
{
    if (type < 1 || type > numTypes()) return nullptr;
    return registry()[(size_t) type - 1].make();
}
inline int typeForId (const std::string& id)
{
    for (int t = 1; t <= numTypes(); ++t)
        if (id == infoFor (t)->id) return t;
    return 0;
}
} // namespace aiofx
