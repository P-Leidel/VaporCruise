#pragma once
#include "scene/terrain-window.h"
#include <array>
namespace vaporcruise::render {
class TerrainUploadState {
public:
    bool NeedsUpload(size_t index, const scene::TerrainSectionSlot& slot) const {
        return slot.complete && (!valid_[index] || revisions_[index] != slot.revision);
    }
    // Call only after a successful Map/copy/Unmap transaction.
    void Uploaded(size_t index, std::uint64_t revision) { revisions_[index]=revision; valid_[index]=true; }
    void Reset() { valid_.fill(false); }
private:
    std::array<std::uint64_t, scene::kTerrainSlotCount> revisions_{};
    std::array<bool, scene::kTerrainSlotCount> valid_{};
};
}
