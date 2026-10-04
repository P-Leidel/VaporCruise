#include "scene/SceneState.h"
#include "scene/scene-frame-snapshot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vaporcruise::scene {
namespace {
using common::Color;
using common::ColorFromSrgbHex;

ScenePalette MakePalette(std::array<std::uint32_t, 7> hex, std::uint32_t accent,
                         std::uint32_t gridGlow, std::uint32_t road,
                         std::array<std::uint32_t,3> regional) {
    // Fifteen semantic roles use fourteen colors; haze reuses the far ridge.
    return {ColorFromSrgbHex(hex[0]), ColorFromSrgbHex(hex[1]), ColorFromSrgbHex(hex[2]),
            ColorFromSrgbHex(hex[3]), ColorFromSrgbHex(hex[4]), ColorFromSrgbHex(hex[5]),
            ColorFromSrgbHex(hex[6]), ColorFromSrgbHex(hex[2]),
            ColorFromSrgbHex(0xFFD49B), ColorFromSrgbHex(accent),
            ColorFromSrgbHex(gridGlow), ColorFromSrgbHex(road),
            ColorFromSrgbHex(regional[0]),ColorFromSrgbHex(regional[1]),ColorFromSrgbHex(regional[2])};
}
const std::array<ScenePalette, 3> kPalettes = {
    MakePalette({0x090619, 0x080512, 0x39204F, 0x160D29, 0xED62CF, 0xFFD08C, 0xF36AB8}, 0x83E5DC, 0xB735CF, 0xFFE1F5, {0xD875C2,0xA47ED6,0xBD81CD}),
    MakePalette({0x06121E, 0x050D17, 0x184453, 0x0B2331, 0x67DDEB, 0xFFE1B0, 0x91DAD8}, 0xECA0BD, 0x229DC5, 0xDDFBFF, {0x69C9C4,0x739FD8,0x74B5CB}),
    MakePalette({0x140B1D, 0x0D0714, 0x4A2943, 0x241225, 0xF7AD73, 0xFFE0A0, 0xEF877D}, 0xA8B8F3, 0xD26C54, 0xFFF0D2, {0xE7AC73,0xD88591,0xDD9980}),
};
constexpr double kTransitionSeconds = 0.8;
constexpr double kGridCellsPerSecond = 1.5;

Color Blend(Color a, Color b, float weight) {
    return {a.r + (b.r - a.r) * weight, a.g + (b.g - a.g) * weight,
            a.b + (b.b - a.b) * weight, a.a + (b.a - a.a) * weight};
}
ScenePalette Blend(const ScenePalette& a, const ScenePalette& b, float weight) {
    return {Blend(a.sky, b.sky, weight), Blend(a.ground, b.ground, weight),
            Blend(a.farRidge, b.farRidge, weight), Blend(a.nearRidge, b.nearRidge, weight),
            Blend(a.grid, b.grid, weight), Blend(a.sunTop, b.sunTop, weight),
            Blend(a.sunBottom, b.sunBottom, weight), Blend(a.haze, b.haze, weight),
            Blend(a.cityWarm, b.cityWarm, weight), Blend(a.highlight, b.highlight, weight),
            Blend(a.gridGlow, b.gridGlow, weight), Blend(a.road, b.road, weight),
            Blend(a.regionBase,b.regionBase,weight),Blend(a.regionShift,b.regionShift,weight),
            Blend(a.regionMineral,b.regionMineral,weight)};
}
}

SceneState::SceneState(std::uint64_t terrainSeed) { current_.palette = kPalettes[0]; current_.terrainSeed = terrainSeed; }
SceneState::~SceneState() = default;
SceneState::SceneState(SceneState&&) noexcept = default;
SceneState& SceneState::operator=(SceneState&&) noexcept = default;
SceneFrameSnapshot SceneState::PrepareFrame() {
    if (!terrain_) terrain_ = std::make_unique<TerrainWindow>();
    if (resetTerrain_) terrain_->Reset(current_.terrainSeed, current_.cruise);
    else terrain_->Update(current_.terrainSeed, current_.cruise, 4);
    resetTerrain_ = false;
    return {Current(), terrain_->Slots(), terrain_->RowsBuiltLastUpdate()};
}
bool SceneState::SetPose(const ScenePose& pose) {
    const auto progressValid = [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; };
    if (!IsValidCruisePosition(pose.cruise) || !std::isfinite(pose.cityLightPhase)
        || pose.cityLightPhase < 0 || pose.cityLightPhase >= 1
        || pose.palette >= kPalettes.size() || !progressValid(pose.paletteProgress))
        return false;

    SceneState replacement(pose.seed);
    replacement.current_.cruise = pose.cruise;
    replacement.current_.cityLightPhase = pose.cityLightPhase;
    replacement.paletteIndex_ = static_cast<int>(pose.palette);
    replacement.current_.palette = kPalettes[pose.palette];
    if (pose.paletteProgress > 0) {
        replacement.NextPalette();
        replacement.Advance(pose.paletteProgress * kTransitionSeconds, true);
    }
    const bool changed = current_.terrainSeed != pose.seed ||
        current_.cruise.cell != pose.cruise.cell || current_.cruise.phase != pose.cruise.phase;
    paletteIndex_ = replacement.paletteIndex_;
    current_ = replacement.current_;
    transitionStart_ = replacement.transitionStart_;
    transitionSeconds_ = replacement.transitionSeconds_;
    resetTerrain_ = resetTerrain_ || changed;
    return true;
}
bool SceneState::SetCruisePosition(CruisePosition position) {
    if (!IsValidCruisePosition(position)) return false;
    resetTerrain_ = resetTerrain_ || current_.cruise.cell != position.cell || current_.cruise.phase != position.phase;
    current_.cruise = position;
    return true;
}

bool SceneState::SetCityLightPhase(double phase) {
    if (!std::isfinite(phase) || phase < 0 || phase >= 1) return false;
    current_.cityLightPhase = phase;
    return true;
}
void SceneState::NextPalette() {
    paletteIndex_ = (paletteIndex_ + 1) % static_cast<int>(kPalettes.size());
    transitionStart_ = current_.palette;
    transitionSeconds_ = 0.0;
    current_.paletteTransitionActive = true;
}

void SceneState::Advance(double seconds, bool cruisePaused) {
    if (!std::isfinite(seconds) || seconds <= 0.0) { return; }
    if (!cruisePaused) {
        current_.cityLightPhase += std::fmod(seconds, 42.0) / 42.0;
        current_.cityLightPhase -= std::floor(current_.cityLightPhase);
        // Validate before converting to int64 or changing either pose component.
        const double step = seconds * kGridCellsPerSecond;
        if (std::isfinite(step) && step < 9223372036854775808.0) {
            const double integral = std::floor(step);
            const auto cells = static_cast<std::int64_t>(integral);
            const double fraction = current_.cruise.phase + (step - integral);
            const std::int64_t carry = fraction >= 1.0 ? 1 : 0;
            const auto available = kMaxTravelCell - current_.cruise.cell;
            if (cells <= available && carry <= available - cells) {
                current_.cruise.cell += cells + carry;
                current_.cruise.phase = fraction - static_cast<double>(carry);
            }
        }
    }
    if (current_.paletteTransitionActive) {
        transitionSeconds_ = std::min(transitionSeconds_ + seconds, kTransitionSeconds);
        if (transitionSeconds_ >= kTransitionSeconds) {
            current_.palette = kPalettes[paletteIndex_];
            current_.paletteTransitionActive = false;
        } else {
            const double t = transitionSeconds_ / kTransitionSeconds;
            const float weight = static_cast<float>(t * t * (3.0 - 2.0 * t));
            current_.palette = Blend(transitionStart_, kPalettes[paletteIndex_], weight);
        }
    }
}

SceneParams SceneState::Current() const {
    auto snapshot = current_;
    snapshot.gridPhase = snapshot.cruise.phase;
    return snapshot;
}
}  // namespace vaporcruise::scene
