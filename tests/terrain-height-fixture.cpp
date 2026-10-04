// Reproducible producer for the native heights used by image-test fixtures.
#include "scene/terrain.h"
#include <array>
#include <iomanip>
#include <iostream>
int main() {
    constexpr std::uint64_t seed=6215337262362608466ULL;
    std::cout << "{\"seed\":\"" << seed << "\",\"samples\":[" << std::setprecision(9);
    bool first=true;
    for (const auto& point : std::array<std::array<int,2>,4>{{{-20,37},{-20,38},{-21,38},{-21,39}}}) {
        if (!first) std::cout << ',';
        first=false;
        std::cout << "{\"x\":" << point[0] << ",\"z\":" << point[1] << ",\"height\":"
                  << vaporcruise::scene::TerrainHeight(seed,point[0],point[1]) << '}';
    }
    std::cout << "]}\n";
}
