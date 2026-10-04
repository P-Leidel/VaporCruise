#pragma once
#include "scene/cruise-position.h"

namespace vaporcruise::scene {
// Progress zero selects a settled entry; positive progress blends to its successor.
struct ScenePose {
    std::uint64_t seed = kDefaultTerrainSeed;
    CruisePosition cruise{};
    double cityLightPhase = 0;
    unsigned palette = 0;
    double paletteProgress = 0;
};
}
