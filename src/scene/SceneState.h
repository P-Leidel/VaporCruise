#pragma once

#include <memory>
#include "common/Color.h"
#include "scene/cruise-position.h"
#include "scene/scene-pose.h"

namespace vaporcruise::scene {

// Linear-light RGB, linear alpha. See common/Color.h for the display policy.
struct ScenePalette {
    common::Color sky;
    common::Color ground;
    common::Color farRidge;
    common::Color nearRidge;
    common::Color grid;
    common::Color sunTop;
    common::Color sunBottom;
    common::Color haze;
    common::Color cityWarm;
    common::Color highlight; // Sparse complementary light: city accents and stars.
    common::Color gridGlow; // Diffuse light in the primary grid color family.
    common::Color road; // Pale road-marking cores, distinct from the wire grid.
    common::Color regionBase, regionShift, regionMineral; // Authored family geology hues.
};

struct SceneParams {
    ScenePalette palette;
    CruisePosition cruise;
    std::uint64_t terrainSeed = kDefaultTerrainSeed;
    double gridPhase = 0.0;  // [0, 1), in grid cells; shader adds it for forward travel.
    double cityLightPhase = 0.0;  // [0, 1), independent 42-second loop.
    bool paletteTransitionActive = false;
};

class TerrainWindow;
struct SceneFrameSnapshot;

// Independent cruise and palette clocks; no window/GPU dependencies.
class SceneState {
public:
    SceneState(std::uint64_t terrainSeed = kDefaultTerrainSeed);
    ~SceneState();
    SceneState(SceneState&&) noexcept;
    SceneState& operator=(SceneState&&) noexcept;
    SceneState(const SceneState&) = delete;
    SceneState& operator=(const SceneState&) = delete;
    SceneFrameSnapshot PrepareFrame();
    bool SetPose(const ScenePose& pose);
    bool SetCruisePosition(CruisePosition position);
    bool SetCityLightPhase(double phase);
    void NextPalette();
    // Negative/nonfinite/zero deltas are ignored. Pause freezes cruise and city lights.
    void Advance(double seconds, bool cruisePaused = false);
    SceneParams Current() const;

private:
    std::unique_ptr<TerrainWindow> terrain_;
    bool resetTerrain_ = false;
    int paletteIndex_ = 0;
    SceneParams current_;
    ScenePalette transitionStart_;
    double transitionSeconds_ = 0.0;
};

}  // namespace vaporcruise::scene
