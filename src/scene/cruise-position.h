#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace vaporcruise::scene {
struct CruisePosition { std::int64_t cell = 0; double phase = 0; };
inline constexpr std::int64_t kMaxTravelCell = std::numeric_limits<std::int64_t>::max() - 1024;
inline constexpr std::uint64_t kDefaultTerrainSeed = 0x5641504F525F4352ULL;
inline bool IsValidCruisePosition(CruisePosition p) {
    return p.cell >= 0 && p.cell <= kMaxTravelCell && std::isfinite(p.phase) && p.phase >= 0 && p.phase < 1;
}
}
