#pragma once
#include "scene/cruise-position.h"
#include "render/camera-projection.h"
namespace vaporcruise::scene {
inline constexpr float kTerrainCellSize = render::DefaultCameraProjection().cellSize;
inline constexpr unsigned kSectionDepthCells = 32, kTerrainWidthCells = 128;
inline constexpr unsigned kTerrainRows = kSectionDepthCells + 1;
inline constexpr unsigned kTerrainColumns = kTerrainWidthCells + 1;
inline constexpr unsigned kSectionVertexCount = kTerrainColumns * kTerrainRows;
inline constexpr unsigned kSectionIndexCount = kTerrainWidthCells * kSectionDepthCells * 6;
inline constexpr unsigned kActiveTerrainSections = 10;
inline constexpr unsigned kTerrainSlotCount = kActiveTerrainSections + 1;
inline constexpr unsigned kTerrainSectionsAhead = kActiveTerrainSections - 1;
inline double FarGroundDistance(CruisePosition position) {
    return (kTerrainSectionsAhead * kSectionDepthCells - position.cell % kSectionDepthCells
            - position.phase) * kTerrainCellSize;
}
}
