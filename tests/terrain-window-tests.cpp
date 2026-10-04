#include "scene/terrain-window.h"
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
using namespace vaporcruise::scene;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) { if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; } };
    auto window = std::make_unique<TerrainWindow>();
    using TerrainVertices = std::array<TerrainVertex, kSectionVertexCount>;
    auto expectedVertices = std::make_unique<TerrainVertices>();
    auto sameVertices = [](const TerrainVertices& a, const TerrainVertices& b) {
        return std::memcmp(a.data(), b.data(), sizeof(a)) == 0;
    };
    check(window->Reset(123, {31, .75}), "initial window builds");
    std::map<std::int64_t, std::uint64_t> original;
    std::map<std::int64_t, std::unique_ptr<TerrainVertices>> originalVertices;
    for (const auto& slot : window->Slots()) {
        check(BuildTerrainRows(123, slot.sectionIndex, 0, kTerrainRows, *expectedVertices) && sameVertices(slot.vertices, *expectedVertices), "completed reset slots contain deterministic terrain");
        if (slot.active) originalVertices[slot.sectionIndex] = std::make_unique<TerrainVertices>(slot.vertices);
    }
    for (const auto& slot : window->Slots()) if (slot.active) { original[slot.sectionIndex] = slot.revision; check(slot.complete, "only complete sections are active"); }
    check(window->Slots().size() == 11 && original.size() == 10 && original.contains(-1) && original.contains(8), "ten active sections cover the initial neighborhood");
    check(window->Update(123, {32, .125}), "section crossing succeeds");
    unsigned retained = 0, active = 0;
    for (const auto& slot : window->Slots()) if (slot.active) {
        ++active;
        if (original.contains(slot.sectionIndex) && original[slot.sectionIndex] == slot.revision) ++retained;
        if (originalVertices.contains(slot.sectionIndex)) check(sameVertices(slot.vertices, *originalVertices[slot.sectionIndex]), "nine retained terrain meshes are unchanged");
        check(slot.complete && slot.sectionIndex >= 0 && slot.sectionIndex <= 9, "new visible neighborhood is complete");
    }
    check(active == 10 && retained == 9, "crossing preserves nine meshes");
    check(window->RowsBuiltLastUpdate() <= 4, "crossing uses bounded generation work");
    const TerrainSectionSlot* staging = nullptr;
    for (const auto& slot : window->Slots()) if (!slot.active) staging = &slot;
    const auto stagedRevision = staging->revision;
    for (int frame = 0; frame < 7; ++frame) {
        check(window->Update(123, {32, .125}), "partial staged section builds");
        check(!staging->complete && staging->revision == stagedRevision, "partial staging never publishes a terrain revision");
        for (const auto& slot : window->Slots()) if (slot.active && originalVertices.contains(slot.sectionIndex))
            check(sameVertices(slot.vertices, *originalVertices[slot.sectionIndex]), "partial staging preserves visible terrain");
    }
    check(window->Update(123, {32, .125}) && staging->complete && staging->revision == stagedRevision + 1 && BuildTerrainRows(123, staging->sectionIndex, 0, kTerrainRows, *expectedVertices) && sameVertices(staging->vertices, *expectedVertices), "last staged row publishes complete deterministic terrain under one revision");
    bool budgeted = true, allComplete = true;
    for (std::int64_t section = 2; section < 1002; ++section) {
        for (int frame = 0; frame < 10; ++frame) {
            budgeted &= window->Update(123, {(section - 1) * 32, .125}) && window->RowsBuiltLastUpdate() <= 4;
            for (const auto& slot : window->Slots()) if (slot.active) allComplete &= slot.complete;
        }
        budgeted &= window->Update(123, {section * 32, .125}) && window->RowsBuiltLastUpdate() <= 4;
    }
    check(budgeted && allComplete, "1000 crossings retain bounded complete storage");
    check(window->Update(123, {1LL << 55, 0}) && window->RowsBuiltLastUpdate() == 363, "large jump rebuilds only eleven sections");
    const auto expectedSection = (1LL << 55) / 32;
    for (const auto& slot : window->Slots()) if (slot.active)
        check(slot.sectionIndex >= expectedSection - 1 && slot.sectionIndex <= expectedSection + 8 && slot.complete, "far neighborhood is exact");
    const auto savedIndex = window->Slots()[0].sectionIndex;
    const auto savedRevision = window->Slots()[0].revision;
    check(!window->Update(123, {kMaxTravelCell + 1, 0}), "invalid pose rejected");
    check(window->Slots()[0].sectionIndex == savedIndex && window->Slots()[0].revision == savedRevision, "invalid pose preserves mesh identity");
    check(window->Reset(123, {0, 0}), "reset to same seeded route");
    std::vector<float> heights;
    for (const auto& slot : window->Slots()) for (const auto& vertex : slot.vertices) heights.push_back(vertex.y);
    auto sameHeights = [&]() {
        size_t i = 0;
        for (const auto& slot : window->Slots()) for (const auto& vertex : slot.vertices)
            if (vertex.y != heights[i++]) return false;
        return true;
    };
    check(window->Update(456, {0, 0}), "seed change resets bounded neighborhood");
    check(!sameHeights(), "different seed changes scenery across the complete window");
    check(window->Reset(123, {0, 0}) && sameHeights(), "seed regeneration is stable across the complete window");
    if (!failures) std::cout << "PASS: bounded section turnover, incremental staging, large jumps and route identity\n";
    return failures ? 1 : 0;
}
