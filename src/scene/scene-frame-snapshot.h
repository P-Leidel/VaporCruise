#pragma once
#include "scene/SceneState.h"
#include "scene/terrain-window.h"
#include <span>
namespace vaporcruise::scene {
// Borrowed for one synchronous draw/capture transaction. Do not mutate or
// prepare the owning scene until consumption finishes; never retain this span.
struct SceneFrameSnapshot {
    SceneParams scene;
    std::span<const TerrainSectionSlot> terrainSlots;
    unsigned rowsBuilt = 0;
};
}
