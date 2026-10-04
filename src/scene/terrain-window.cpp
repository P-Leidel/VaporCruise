#include "scene/terrain-window.h"
#include <algorithm>
namespace vaporcruise::scene {
bool TerrainWindow::Reset(std::uint64_t seed, CruisePosition position) {
    if (!IsValidCruisePosition(position)) return false;
    seed_ = seed; currentSection_ = position.cell / kSectionDepthCells; rowsBuilt_ = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        slot.sectionIndex = currentSection_ - 1 + static_cast<std::int64_t>(i);
        BuildTerrainRows(seed, slot.sectionIndex, 0, kTerrainRows, slot.vertices);
        slot.active = i < kActiveTerrainSections; slot.complete = true; ++slot.revision;
        rowsBuilt_ += 33;
    }
    stagingSlot_ = kActiveTerrainSections; stagingRow_ = kTerrainRows; initialized_ = true;
    return true;
}
bool TerrainWindow::Update(std::uint64_t seed, CruisePosition position, unsigned rowBudget) {
    if (!IsValidCruisePosition(position)) return false;
    const auto section = position.cell / kSectionDepthCells;
    if (!initialized_ || seed != seed_ || section < currentSection_ || section > currentSection_ + 1)
        return Reset(seed, position);
    rowsBuilt_ = 0;
    if (section != currentSection_) {
        if (!slots_[stagingSlot_].complete) return Reset(seed, position);
        slots_[stagingSlot_].active = true;
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].active && slots_[i].sectionIndex == currentSection_ - 1) {
                stagingSlot_ = i;
                slots_[i].active = false; slots_[i].complete = false;
                slots_[i].sectionIndex = section + kTerrainSectionsAhead;
                stagingRow_ = 0;
                break;
            }
        }
        currentSection_ = section;
    }
    if (stagingRow_ < kTerrainRows) {
        const auto count = std::min(rowBudget, kTerrainRows - stagingRow_);
        auto& slot = slots_[stagingSlot_];
        BuildTerrainRows(seed_, slot.sectionIndex, stagingRow_, count, slot.vertices);
        stagingRow_ += count; rowsBuilt_ += count;
        if (stagingRow_ == kTerrainRows) {
            slot.complete = true; ++slot.revision;
        }
    }
    return true;
}
}
