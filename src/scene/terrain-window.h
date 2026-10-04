#pragma once
#include "scene/terrain.h"
#include <array>
namespace vaporcruise::scene {
struct TerrainSectionSlot {
    std::int64_t sectionIndex = 0;
    std::uint64_t revision = 0;
    bool active = false, complete = false;
    std::array<TerrainVertex, kSectionVertexCount> vertices{};
};
class TerrainWindow {
public:
    bool Reset(std::uint64_t seed, CruisePosition position);
    bool Update(std::uint64_t seed, CruisePosition position, unsigned rowBudget = 4);
    std::span<const TerrainSectionSlot> Slots() const { return slots_; }
    unsigned RowsBuiltLastUpdate() const { return rowsBuilt_; }
private:
    std::array<TerrainSectionSlot, kTerrainSlotCount> slots_{};
    std::uint64_t seed_ = 0;
    std::int64_t currentSection_ = 0;
    size_t stagingSlot_ = kActiveTerrainSections;
    unsigned stagingRow_ = kTerrainRows, rowsBuilt_ = 0;
    bool initialized_ = false;
};
}
