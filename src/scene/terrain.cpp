#include "scene/terrain.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace vaporcruise::scene {
namespace {
std::uint64_t Mix(std::uint64_t v) {
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}
struct Axis { std::int64_t cell; int offset; };
Axis Address(std::int64_t value, int scale) {
    auto cell = value / scale; auto remainder = value % scale;
    if (remainder < 0) { --cell; remainder += scale; }
    return {cell, static_cast<int>(remainder)};
}
float ClearanceRelief(std::uint64_t seed, int x, std::int64_t z) {
    // Address the envelope's lattice with integers before interpolation, so
    // its depressions retain adjacent detail even at the farthest route pose.
    const auto a = Address(x, 5), b = Address(z, 7);
    const float fx = a.offset / 5.f, fz = b.offset / 7.f;
    const auto sample = [&](int dx, int dz) {
        const auto hash = Mix(seed ^ Mix(static_cast<std::uint64_t>(a.cell + dx))
            ^ Mix(static_cast<std::uint64_t>(b.cell + dz) + 0xb7d2a891ULL));
        return static_cast<float>(hash & 65535) / 65535.f;
    };
    const float east = sample(1, 0), south = sample(0, 1);
    if (fx + fz < 1) return sample(0, 0)*(1-fx-fz) + east*fx + south*fz;
    return sample(1, 1)*(fx+fz-1) + east*(1-fz) + south*(1-fx);
}
float FacetedRelief(std::uint64_t seed, float x, float z, float spacing) {
    // Coordinates are local to one bounded formation, including at far-world
    // positions. A coarse triangular lattice bends whole groups of wires;
    // independent per-vertex jitter would turn them into isolated spikes.
    const float scaledX = x / spacing, scaledZ = z / spacing;
    const int cellX = static_cast<int>(std::floor(scaledX));
    const int cellZ = static_cast<int>(std::floor(scaledZ));
    const float fx = scaledX - cellX, fz = scaledZ - cellZ;
    const auto sample = [&](int dx, int dz) {
        const auto hash = Mix(seed ^ (static_cast<std::uint64_t>(cellX + dx) * 0x9e3779b97f4a7c15ULL)
            ^ (static_cast<std::uint64_t>(cellZ + dz) * 0xd1b54a32d192ed03ULL));
        return static_cast<float>(hash & 65535) / 65535.f;
    };
    const float b = sample(1, 0), c = sample(0, 1);
    if (fx + fz < 1) return sample(0, 0) * (1-fx-fz) + b*fx + c*fz;
    return sample(1, 1) * (fx+fz-1) + b*(1-fz) + c*(1-fx);
}
float Formations(std::uint64_t seed, int x, std::int64_t z, bool distant) {
    const int width = distant ? 32 : 16, depth = distant ? 64 : 32;
    const auto a = Address(x, width), b = Address(z, depth);
    const auto routeSeed = seed ^ (distant ? 0xa3519ULL : 0);
    float height = 0;
    // Inspect adjacent placement tiles, so a formation crossing a tile or mesh
    // section has exactly the same shape from either side. All distances stay
    // small integers even at far-world positions.
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const auto tileX = a.cell + dx, tileZ = b.cell + dz;
        const auto hash = Mix(seed ^ Mix(static_cast<std::uint64_t>(tileX))
            ^ Mix(static_cast<std::uint64_t>(tileZ) + 0x9e3779b97f4a7c15ULL));
        // Choose a whole placement's route character, never individual vertices.
        // These exact divisors address 192-cell stretches without multiplying
        // far-world depth coordinates or reducing them to floating point.
        const auto stretch = Address(tileZ, distant ? 3 : 6).cell;
        const auto stretchHash = Mix(routeSeed ^ Mix(static_cast<std::uint64_t>(stretch))
            ^ (tileX < 0 ? 0x6147ULL : 0xa817ULL));
        const bool calm = (stretchHash & 3) == 0;
        const unsigned occupancy = calm ? (distant ? 22937u : 6553u)
                                        : (distant ? 42598u : 45875u);
        bool occupied = (hash & 65535) <= occupancy;
        if (calm && !distant) {
            // Each pair of lateral near tiles retains one varied anchor, with
            // a ten-percent chance of its partner: about 55% near occupancy.
            // Distributing the quieter placements avoids long bare approaches
            // on one side while leaving openings between the connected groups.
            const auto pair = Address(tileX, 2);
            const auto anchor = Mix(routeSeed ^ Mix(static_cast<std::uint64_t>(pair.cell))
                ^ Mix(static_cast<std::uint64_t>(tileZ) + 0xd1b54a32d192ed03ULL));
            occupied |= pair.offset == static_cast<int>(anchor & 1);
        }
        if (!occupied) continue;
        const int centerX = distant ? 8 + static_cast<int>((hash >> 16) % 17)
                                    : 4 + static_cast<int>((hash >> 16) % 9);
        const int centerZ = distant ? 16 + static_cast<int>((hash >> 21) % 33)
                                    : 8 + static_cast<int>((hash >> 21) % 17);
        const auto absoluteX = std::abs(tileX * width + centerX);
        if (distant && absoluteX < 14) continue;
        const float localX = static_cast<float>(a.offset - dx * width - centerX);
        const float localZ = static_cast<float>(b.offset - dz * depth - centerZ);
        const float nearScale = calm ? .85f : 1.f;
        const float rx = distant ? 20.f + static_cast<float>((hash >> 27) % 8)
                                  : (10.f + static_cast<float>((hash >> 27) % 4)) * nearScale;
        const float rz = distant ? 36.f + static_cast<float>((hash >> 31) % 16)
                                  : (19.f + static_cast<float>((hash >> 31) % 8)) * nearScale;
        // Every lobe, including rotated and offset ones, is intersected with
        // this continuous footprint. rx <= 27 < width and rz <= 51 < depth.
        // A tile two or more addresses away therefore contributes exactly zero
        // even when its center is at the closest edge of its own tile. The
        // existing nine-tile search covers the complete support on both axes.
        const float support = 1 - std::max(std::abs(localX) / rx, std::abs(localZ) / rz);
        if (support <= 0) continue;
        constexpr std::array<float, 7> cosine{1.f, .9659258f, .9659258f, .8660254f, .8660254f, .7071068f, .7071068f};
        constexpr std::array<float, 7> sine{0.f, .2588190f, -.2588190f, .5f, -.5f, .7071068f, -.7071068f};
        const auto angle = static_cast<unsigned>((hash >> 43) % cosine.size());
        const float rawU = localX * cosine[angle] - localZ * sine[angle];
        const float rawV = localX * sine[angle] + localZ * cosine[angle];
        const float detail = FacetedRelief(hash ^ 0x8d12a34bULL, rawU, rawV, distant ? 6.f : 3.5f);
        // Small, connected deviations bend the long crests and face edges.
        // The unwarped footprint still bounds every displaced profile.
        const float warp = (detail - .5f) * (distant ? .4f : .3f);
        const float u = rawU + warp, v = rawV - warp * .7f;
        const float lean = ((hash >> 40) & 1) ? 1.f : -1.f;
        const float variation = static_cast<float>((hash >> 48) & 65535) / 65535.f;
        const float apex = distant ? 16.f + 8.f * variation
                                   : (4.5f + 3.5f * variation) * (calm ? .85f : 1.f);
        const auto peak = [&](float offsetX, float offsetZ, float radiusX, float radiusZ) {
            const float slopeWidth = (u - offsetX) * lean < 0 ? radiusX : radiusX * .72f;
            return 1 - std::abs(u - offsetX) / slopeWidth - std::abs(v - offsetZ) / radiusZ;
        };
        const unsigned shape = static_cast<unsigned>((hash >> 38) % 5);
        float profile;
        if (shape == 0) {
            // Three unequal summits share broad foothills and elevated saddles.
            profile = std::max({peak(-rx * .18f, -rz * .34f, rx * .70f, rz * .56f),
                .86f * peak(rx * .23f, rz * .25f, rx * .62f, rz * .57f),
                .64f * peak(-rx * .31f, rz * .55f, rx * .50f, rz * .43f)});
        } else if (shape == 1) {
            // Different flat tops and an offset lower bench form a terraced mesa.
            profile = std::max(std::min(.92f, peak(-rx * .13f, -rz * .08f, rx * .85f, rz * .85f) * 2.2f),
                std::min(.52f, peak(rx * .30f, rz * .38f, rx * .67f, rz * .65f) * 1.6f));
        } else if (shape == 2) {
            profile = std::max(peak(-rx * .26f, -rz * .28f, rx * .68f, rz * .67f),
                .78f * peak(rx * .33f, rz * .36f, rx * .58f, rz * .63f));
        } else if (shape == 3) {
            // A long crest has a shallow central pass, rather than one pyramid.
            profile = 1 - std::abs(u + rx * .12f) / (rx * .72f)
                - std::max(0.f, std::abs(v) - rz * .40f) / (rz * .53f);
            profile -= .24f * std::max(0.f, 1 - std::abs(v + rz * .08f) / (rz * .28f));
        } else {
            profile = std::max(peak(rx * .12f, -rz * .10f, rx * .82f, rz * .87f),
                .68f * peak(-rx * .42f, rz * .45f, rx * .48f, rz * .52f));
        }
        // Low shared skirts connect the summits without adding their heights.
        // The footprint cap brings every crest to zero before the search edge.
        // Cut shallow angular pockets after the mesa clamps, so flat tops can
        // also carry dents. Some lattice regions remain uncut true terraces.
        // Skirts retain at least 68% of their original positive height, keeping
        // shared foothills connected while bending their formerly flat faces.
        const float pocket = std::max(0.f, detail - .28f);
        const float skirt = (distant ? 4.2f : calm ? 1.7f : 2.6f) * support * (1 - .32f * detail);
        const float dent = (distant ? 4.f : 2.4f) * pocket * std::min(1.f, support * 4.f);
        const float crest = std::max(0.f, apex * std::max(0.f, std::min(profile, support * 2.f)) - dent);
        height = std::max(height, std::max(skirt, crest));
    }
    return height;
}
}
bool TerrainCellFold(float a, float b, float c, float d) {
    // Equal opposite-corner sums describe one plane. The mesh diagonal b-c
    // becomes a real crease only when the two triangle planes disagree.
    return std::abs(a + d - b - c) > .0001f;
}
float TerrainHeight(std::uint64_t seed, int lateralCell, std::int64_t depthCell) {
    const auto lateral = std::abs(static_cast<std::int64_t>(lateralCell));
    if (lateral <= 3) return 0;
    const float shoulderDistance = static_cast<float>(lateral - 3);
    const float clearance = .55f * shoulderDistance + .02f * shoulderDistance * shoulderDistance;
    // A constant depth-independent cap used to erase every formation dent
    // along whole mountainsides. Bend that bounding surface as well, without
    // lifting any point above the existing protected road-clearance envelope.
    const float reliefStrength = std::clamp((shoulderDistance - 3.f) * .08f, 0.f, .28f);
    const float variedClearance = clearance * (1 - reliefStrength * ClearanceRelief(seed, lateralCell, depthCell));
    return std::min(variedClearance, std::max(Formations(seed, lateralCell, depthCell, false),
                    Formations(seed ^ 0xa3519ULL, lateralCell, depthCell, true)));
}
bool BuildTerrainRows(std::uint64_t seed, std::int64_t sectionIndex, unsigned firstRow,
                      unsigned rowCount, std::span<TerrainVertex> vertices) {
    if (vertices.size() != kSectionVertexCount || firstRow > kTerrainRows || rowCount > kTerrainRows - firstRow
        || sectionIndex < -1 || sectionIndex > kMaxTravelCell / kSectionDepthCells + kTerrainSectionsAhead) return false;
    std::array<float, kTerrainColumns> heights{}, nextHeights{};
    const auto sampleRow = [&](std::int64_t depth, auto& destination) {
        for (unsigned column = 0; column < kTerrainColumns; ++column)
            destination[column] = TerrainHeight(seed,
                static_cast<int>(column) - static_cast<int>(kTerrainWidthCells / 2), depth);
    };
    if (rowCount) sampleRow(sectionIndex * kSectionDepthCells + firstRow, heights);
    for (unsigned row = firstRow; row < firstRow + rowCount; ++row) {
        const auto depth = sectionIndex * kSectionDepthCells + row;
        sampleRow(depth + 1, nextHeights);
        float previousFold = 0;
        for (unsigned column = 0; column < kTerrainColumns; ++column) {
            const int lateral = static_cast<int>(column) - static_cast<int>(kTerrainWidthCells / 2);
            const float fold = column < kTerrainWidthCells && TerrainCellFold(
                heights[column], heights[column+1], nextHeights[column], nextHeights[column+1]) ? 1.f : 0.f;
            vertices[row * kTerrainColumns + column] = {lateral * kTerrainCellSize,
                heights[column], row * kTerrainCellSize, fold, previousFold};
            previousFold = fold;
        }
        heights.swap(nextHeights);
    }
    return true;
}
bool BuildTerrainIndices(std::span<std::uint32_t> indices) {
    if (indices.size() != kSectionIndexCount) return false;
    size_t output = 0;
    for (unsigned row = 0; row < kSectionDepthCells; ++row) for (unsigned column = 0; column < kTerrainWidthCells; ++column) {
        const auto a = row * kTerrainColumns + column, b = a + 1, c = a + kTerrainColumns, d = c + 1;
        for (const auto index : {a, c, b, b, c, d}) indices[output++] = index;
    }
    return true;
}
}
