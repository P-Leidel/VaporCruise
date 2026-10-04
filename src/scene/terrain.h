#pragma once
#include "scene/cruise-position.h"
#include "scene/scene-geometry.h"
#include <span>
namespace vaporcruise::scene {
// The two triangles in a cell start at its upper-left and upper-right vertex.
// Store each neighboring cell's crease on those provoking vertices.
struct TerrainVertex { float x, y, z; float foldRight = 0, foldLeft = 0; };
static_assert(sizeof(TerrainVertex) == 20);
bool TerrainCellFold(float a, float b, float c, float d);
float TerrainHeight(std::uint64_t seed, int lateralCell, std::int64_t depthCell);
bool BuildTerrainRows(std::uint64_t seed, std::int64_t sectionIndex, unsigned firstRow, unsigned rowCount, std::span<TerrainVertex> vertices);
bool BuildTerrainIndices(std::span<std::uint32_t> indices);
}
