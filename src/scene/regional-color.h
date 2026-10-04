#pragma once
#include "scene/cruise-position.h"
#include <array>
#include <cstdint>

namespace vaporcruise::scene {
struct RegionalColorField {
    // Route remainder + phase (cells), inverse knot spacing, grid strength, reserved.
    std::array<float,4> control{};
    // Five forward rows, each with lateral columns at -128, 0, +128 cells.
    // xyz convex palette weights; w sparse mineral eligibility.
    std::array<std::array<float,4>,15> knots{};
};
inline RegionalColorField BuildRegionalColorField(std::uint64_t seed,CruisePosition position) {
    RegionalColorField field;
    field.control={static_cast<float>(position.cell%128+position.phase),1.f/128,.12f,0.f};
    // Mix the full integer region address before any conversion to float.
    const auto mix=[](std::uint64_t value) {
        value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
        value=(value^(value>>27))*0x94d049bb133111ebULL;
        return value^(value>>31);
    };
    const auto first=static_cast<std::uint64_t>(position.cell)/128;
    for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col) {
        const auto address=mix(first+row)^mix(seed)^mix(0x726567696f6eULL+col);
        const auto hue=mix(address);
        // Nonzero base/shift weights avoid abrupt saturated hue islands.
        const float base=.25f+static_cast<float>(hue&65535)/65535;
        const float shift=.25f+static_cast<float>((hue>>16)&65535)/65535;
        const float mineral=.08f+static_cast<float>((hue>>32)&65535)/65535*.22f;
        const float sum=base+shift+mineral;
        const float eligible=(mix(address^0x6d696e6572616cULL)&7)==0?1.f:0.f;
        field.knots[row*3+col]={base/sum,shift/sum,mineral/sum,eligible};
    }
    return field;
}
}
