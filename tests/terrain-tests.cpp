#include "scene/terrain.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <queue>
#include <set>
#include <vector>
using namespace vaporcruise::scene;
namespace {
struct Composition {
    unsigned samples = 0, raised = 0, raisedLeft = 0, raisedRight = 0;
    unsigned broadMasses = 0, largestMass = 0;
    unsigned saddleProfiles = 0, terraces = 0, variedFacets = 0;
    unsigned elevatedNeighborhoods = 0, relievedNeighborhoods = 0, faceDents = 0;
    unsigned broadSmoothFaces = 0, smoothFaceSamples = 0;
    unsigned elevatedLeft = 0, elevatedRight = 0, smoothFaceLeft = 0, smoothFaceRight = 0;
    float maximumStep = 0;
    bool roadClear = true, lowShoulders = true, bounded = true;
};
Composition InspectComposition(std::uint64_t seed, std::int64_t firstDepth = 0) {
    constexpr unsigned rows = 1024, columns = kTerrainColumns;
    std::vector<float> heights(rows * columns);
    Composition result;
    for (unsigned z = 0; z < rows; ++z) for (unsigned column = 0; column < columns; ++column) {
        const int x = static_cast<int>(column) - 64;
        const float h = TerrainHeight(seed, x, firstDepth + z);
        heights[z * columns + column] = h;
        result.bounded &= std::isfinite(h) && h >= 0 && h <= 32;
        if (std::abs(x) <= 3) result.roadClear &= h == 0;
        else {
            ++result.samples;
            result.raised += h > .5f;
            result.raisedLeft += x < 0 && h > .5f;
            result.raisedRight += x > 0 && h > .5f;
        }
        if (std::abs(x) <= 6) result.lowShoulders &= h <= 2.25f;
    }
    // Replacing connected foothills with isolated rocks loses both occupied
    // area and broad four-neighbor components. Measure the resulting land,
    // without depending on placement tiles, hash values or shape selection.
    std::vector<bool> visited(heights.size());
    for (unsigned start = 0; start < heights.size(); ++start) {
        if (visited[start] || heights[start] <= .5f) continue;
        unsigned size = 0;
        std::queue<unsigned> pending;
        visited[start] = true; pending.push(start);
        while (!pending.empty()) {
            const unsigned index = pending.front(); pending.pop(); ++size;
            const unsigned z = index / columns, x = index % columns;
            const auto visit = [&](unsigned neighbor) {
                if (!visited[neighbor] && heights[neighbor] > .5f) {
                    visited[neighbor] = true; pending.push(neighbor);
                }
            };
            if (z) visit(index - columns);
            if (z + 1 < rows) visit(index + columns);
            if (x) visit(index - 1);
            if (x + 1 < columns) visit(index + 1);
        }
        result.largestMass = std::max(result.largestMass, size);
        result.broadMasses += size >= 400;
    }
    // A valley must fall and rise by at least one world unit without reaching
    // the plain. More identical single-apex pyramids cannot satisfy this.
    for (unsigned column = 4; column < columns - 4; ++column) {
        float summit = 0, valley = 0;
        bool descending = false, counted = false;
        for (unsigned z = 0; z < rows; ++z) {
            const float h = heights[z * columns + column];
            if (h <= .5f) { summit = valley = 0; descending = counted = false; continue; }
            if (!descending) {
                summit = std::max(summit, h);
                if (summit - h >= 1) { descending = true; valley = h; }
            } else {
                valley = std::min(valley, h);
                if (!counted && h - valley >= 1) { ++result.saddleProfiles; counted = true; }
            }
        }
    }
    for (unsigned z = 0; z + 1 < rows; ++z) for (unsigned x = 0; x + 1 < columns; ++x) {
        const unsigned index = z * columns + x;
        const float h = heights[index], dx = heights[index + 1] - h, dz = heights[index + columns] - h;
        result.maximumStep = std::max({result.maximumStep, std::abs(dx), std::abs(dz)});
        result.terraces += h > 1 && std::abs(dx) < .00001f && std::abs(dz) < .00001f
            && std::abs(heights[index + columns + 1] - h) < .00001f;
        result.variedFacets += h > .5f && std::abs(dx) > .1f && std::abs(dz) > .1f
            && std::abs(std::abs(dx) - std::abs(dz)) > .15f;
    }
    // Unequal x/z slopes can still describe one enormous smooth plane. Inspect
    // elevated neighborhoods away from the protected shoulder: meaningful
    // second differences show bent faces, rather than merely rotated pyramids.
    for (unsigned z = 2; z + 2 < rows; ++z) for (unsigned x = 2; x + 2 < columns; ++x) {
        if (std::abs(static_cast<int>(x) - 64) < 8) continue;
        const auto index = z * columns + x;
        const float h = heights[index];
        if (h <= 1 || heights[index-1] <= 1 || heights[index+1] <= 1
            || heights[index-columns] <= 1 || heights[index+columns] <= 1) continue;
        ++result.elevatedNeighborhoods;
        result.elevatedLeft += x < 64;
        result.elevatedRight += x > 64;
        const float curveX = heights[index-1] + heights[index+1] - 2*h;
        const float curveZ = heights[index-columns] + heights[index+columns] - 2*h;
        const float twist = heights[index+columns+1] + h - heights[index+1] - heights[index+columns];
        result.relievedNeighborhoods += std::max({std::abs(curveX), std::abs(curveZ), std::abs(twist)}) > .08f;
        // A local depression below opposing two-cell chords on both axes
        // catches pockets and small saddles; rotated smooth planes stay zero.
        const float dipX = (heights[index-2] + heights[index+2]) * .5f - h;
        const float dipZ = (heights[index-2*columns] + heights[index+2*columns]) * .5f - h;
        result.faceDents += dipX > .12f && dipZ > .12f;
    }
    // A high count of isolated dents can conceal long uninterrupted sheets.
    // Test four-by-twelve-cell faces against their corner-defined plane, then
    // count the union of their elevated samples so overlaps cannot inflate it.
    // The tolerance permits subtle low-poly planes and a gently curved road
    // shoulder, but dents/ridges of visible depth break the entire long face.
    constexpr unsigned faceWidth = 4, faceDepth = 12;
    std::vector<bool> smoothFace(heights.size());
    for (unsigned z = 0; z + faceDepth < rows; ++z) for (unsigned x = 0; x + faceWidth < columns; ++x) {
        if (std::abs(static_cast<int>(x) - 64) < 8) continue;
        const float origin = heights[z*columns+x];
        if (origin <= 1) continue;
        const float slopeX = (heights[z*columns+x+faceWidth] - origin) / faceWidth;
        const float slopeZ = (heights[(z+faceDepth)*columns+x] - origin) / faceDepth;
        bool planar = true;
        for (unsigned dz = 0; dz <= faceDepth && planar; ++dz) for (unsigned dx = 0; dx <= faceWidth; ++dx) {
            const float h = heights[(z+dz)*columns+x+dx];
            if (h <= 1 || std::abs(h - (origin + slopeX*dx + slopeZ*dz)) > .12f) { planar = false; break; }
        }
        if (!planar) continue;
        ++result.broadSmoothFaces;
        for (unsigned dz = 0; dz <= faceDepth; ++dz) for (unsigned dx = 0; dx <= faceWidth; ++dx)
            smoothFace[(z+dz)*columns+x+dx] = true;
    }
    result.smoothFaceSamples = static_cast<unsigned>(std::count(smoothFace.begin(), smoothFace.end(), true));
    for (unsigned index = 0; index < smoothFace.size(); ++index) if (smoothFace[index]) {
        result.smoothFaceLeft += index % columns < 64;
        result.smoothFaceRight += index % columns > 64;
    }
    return result;
}
}
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } };
    check(!TerrainCellFold(0, 0, 0, 0), "flat grid squares have no diagonal");
    check(!TerrainCellFold(1, 2, 3, 4), "sloped coplanar squares have no diagonal");
    check(TerrainCellFold(0, 0, 0, 2), "a ground-to-rock fold has a diagonal");
    check(TerrainCellFold(2, 3, 4, 2), "a ridge fold has a diagonal");
    bool flat = true, bounded = true, asymmetric = false;
    for (int z = 0; z < 256; ++z) {
        for (int x = -64; x <= 64; ++x) {
            const float h = TerrainHeight(123, x, z);
            bounded &= std::isfinite(h) && h >= 0 && h <= 48;
            if (std::abs(x) <= 3) flat &= h == 0;
            else asymmetric |= h != TerrainHeight(123, -x, z);
        }
    }
    check(flat, "flat road and inner shoulders"); check(bounded, "finite bounded elevations");
    check(asymmetric, "independent sides vary");
    for (const std::uint64_t seed : {123ULL, 456ULL, kDefaultTerrainSeed}) {
        const auto c = InspectComposition(seed);
        std::cout << "Composition seed=" << seed << ": raised=" << c.raised << '/' << c.samples
            << ", left=" << c.raisedLeft << ", right=" << c.raisedRight
            << ", broad masses=" << c.broadMasses << ", largest=" << c.largestMass
            << ", saddles=" << c.saddleProfiles << ", terraces=" << c.terraces
            << ", varied facets=" << c.variedFacets << ", max step=" << c.maximumStep
            << ", relieved=" << c.relievedNeighborhoods << '/' << c.elevatedNeighborhoods
            << ", face dents=" << c.faceDents << ", broad smooth faces=" << c.broadSmoothFaces
            << ", smooth face samples=" << c.smoothFaceSamples
            << ", smooth left=" << c.smoothFaceLeft << '/' << c.elevatedLeft
            << ", smooth right=" << c.smoothFaceRight << '/' << c.elevatedRight << '\n';
        check(c.roadClear, "connected terrain leaves the road and inner shoulders exactly flat");
        check(c.lowShoulders, "the first three shoulder columns remain low");
        check(c.bounded, "connected terrain has finite nonnegative bounded heights");
        check(c.raised > c.samples * .45, "elevated terrain occupies a substantial part of the land");
        check(c.raisedLeft > c.samples * .20 && c.raisedRight > c.samples * .20,
            "both independent sides contain substantial elevated terrain");
        check(c.raised < c.samples * .90, "terrain retains openings between larger groups");
        check(c.broadMasses >= 6 && c.largestMass >= 2000, "the route contains broad connected landforms");
        check(c.saddleProfiles >= 80, "connected groups contain distinct summits with elevated saddles");
        check(c.terraces >= 40, "larger forms include elevated flat terraces");
        check(c.variedFacets >= 4000, "larger forms retain varied unequal facet slopes");
        check(c.maximumStep <= 4.5f, "placement boundaries never cut broad forms into tall steps");
        check(c.relievedNeighborhoods > c.elevatedNeighborhoods * .60,
            "elevated faces contain frequent bends instead of broad smooth planes");
        check(c.faceDents > c.elevatedNeighborhoods * .14,
            "elevated faces contain local dents on both axes");
        check(c.smoothFaceSamples < c.elevatedNeighborhoods * .05,
            "long uninterrupted smooth faces occupy only a small minority of elevated terrain");
        check(c.smoothFaceLeft < c.elevatedLeft * .05 && c.smoothFaceRight < c.elevatedRight * .05,
            "each independent side keeps long uninterrupted smooth faces rare");
    }
    // Whole-route area can conceal an empty opening view. Sample the ground
    // cone visible in a 16:9 windshield over the first two section positions;
    // distant outer terrain cannot compensate for missing nearby landforms.
    for (const std::int64_t start : {0LL, 32LL}) {
        unsigned samples = 0, raisedLeft = 0, raisedRight = 0;
        for (int z = 5; z < 33; ++z) for (int x = 4; x <= std::min(32, z * 10 / 9); ++x) {
            samples += 2;
            raisedLeft += TerrainHeight(kDefaultTerrainSeed, -x, start + z) > .5f;
            raisedRight += TerrainHeight(kDefaultTerrainSeed, x, start + z) > .5f;
        }
        std::cout << "Early foreground cell=" << start << ": left=" << raisedLeft
            << ", right=" << raisedRight << ", samples=" << samples << '\n';
        check(raisedLeft > samples * .10 && raisedRight > samples * .10,
            "both sides of the opening view contain nearby terrain");
        check(raisedLeft + raisedRight > samples * .30,
            "the opening route has substantial nearby terrain presence");
    }
    std::vector<TerrainVertex> a(kSectionVertexCount), b(kSectionVertexCount), repeat(kSectionVertexCount);
    check(BuildTerrainRows(123, 17, 0, 33, a) && BuildTerrainRows(123, 18, 0, 33, b), "complete sections build");
    bool matchingFolds = true, flatFolds = true;
    unsigned markedFolds = 0;
    for (unsigned row = 0; row < kSectionDepthCells; ++row) for (unsigned col = 0; col < kTerrainWidthCells; ++col) {
        const auto index = row * kTerrainColumns + col;
        matchingFolds &= a[index].foldRight == a[index+1].foldLeft;
        markedFolds += a[index].foldRight > 0;
        if (a[index].y == 0 && a[index+1].y == 0 &&
            a[index+kTerrainColumns].y == 0 && a[index+kTerrainColumns+1].y == 0)
            flatFolds &= a[index].foldRight == 0;
    }
    check(matchingFolds && flatFolds && markedFolds > 20, "both rendered triangles share crease flags and flat cells stay square");
    bool seam = true;
    for (unsigned x = 0; x < 129; ++x) {
        const auto& end = a[32 * 129 + x]; const auto& start = b[x];
        seam &= end.x == start.x && end.y == start.y && 17 * 64 + end.z == 18 * 64 + start.z
            && end.foldRight == start.foldRight && end.foldLeft == start.foldLeft;
    }
    check(seam, "shared edge agrees in world coordinates");
    check(BuildTerrainRows(123, 17, 0, 33, repeat), "repeat builds");
    check(std::equal(a.begin(), a.end(), repeat.begin(), [](auto v, auto w) { return v.x == w.x && v.y == w.y && v.z == w.z; }), "seeded regeneration is identical");
    std::set<std::uint64_t> fingerprints;
    for (int section = 0; section < 128; ++section) {
        BuildTerrainRows(123, section, 0, 33, a);
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto& v : a) hash = (hash ^ std::bit_cast<std::uint32_t>(v.y)) * 1099511628211ULL;
        fingerprints.insert(hash);
    }
    check(fingerprints.size() == 128, "route sections do not cycle a template");
    const std::int64_t far = (1LL << 55);
    for (const std::uint64_t seed : {123ULL, 456ULL, kDefaultTerrainSeed}) {
        const auto c = InspectComposition(seed, far);
        std::cout << "Far smooth faces seed=" << seed << ": left=" << c.smoothFaceLeft << '/' << c.elevatedLeft
            << ", right=" << c.smoothFaceRight << '/' << c.elevatedRight
            << ", terraces=" << c.terraces << ", max step=" << c.maximumStep << '\n';
        check(c.smoothFaceLeft < c.elevatedLeft * .05 && c.smoothFaceRight < c.elevatedRight * .05,
            "both sides retain relief instead of long smooth sheets at far integer route positions");
        check(c.roadClear && c.lowShoulders && c.bounded,
            "far-route relief keeps protected flat road, low shoulders and bounded heights");
    }
    bool variedFar = false;
    for (int x = 4; x < 64; ++x) for (int z = 1; z < 32; ++z)
        variedFar |= TerrainHeight(123, x, far + z) != TerrainHeight(123, x, far);
    check(variedFar, "far integer coordinates retain adjacent detail");
    // Shared rows remain identical at early, placement/macro-crossing, far,
    // and maximum sections; broad forms must not acquire streaming seams.
    bool extendedSeams = true, farRoadClear = true;
    for (const std::int64_t section : std::array<std::int64_t, 7>{
        -1, 0, 2, 5, 6, far / kSectionDepthCells, kMaxTravelCell / kSectionDepthCells + 7}) {
        extendedSeams &= BuildTerrainRows(kDefaultTerrainSeed, section, 0, kTerrainRows, a)
            && BuildTerrainRows(kDefaultTerrainSeed, section + 1, 0, kTerrainRows, b);
        for (unsigned column = 0; column < kTerrainColumns; ++column) {
            const auto& end = a[kSectionDepthCells * kTerrainColumns + column];
            const auto& start = b[column];
            extendedSeams &= end.x == start.x && end.y == start.y
                && end.foldRight == start.foldRight && end.foldLeft == start.foldLeft;
            if (std::abs(static_cast<int>(column) - 64) <= 3)
                farRoadClear &= end.y == 0 && start.y == 0;
        }
    }
    check(extendedSeams, "broad formations share identical heights and crease flags across early and far section seams");
    check(farRoadClear, "road clearance survives negative sampling and far integer section seams");
    check(BuildTerrainRows(123, kMaxTravelCell / 32 + 8, 0, 33, a), "safe maximum pose has sampling headroom");
    for (auto& v : a) v = {0, -100, 0};
    check(BuildTerrainRows(123, 0, 7, 1, a), "one row builds incrementally");
    check(a[6 * 129].y == -100 && a[8 * 129].y == -100 && a[7 * 129].y >= 0, "row build touches only requested storage");
    check(!BuildTerrainRows(123, 0, 32, 2, a) && !BuildTerrainRows(123, 0, 0, 33, {}), "invalid row spans rejected");
    std::vector<std::uint32_t> indices(kSectionIndexCount);
    check(BuildTerrainIndices(indices), "index mesh builds");
    bool valid = true;
    for (size_t i = 0; i < indices.size(); i += 3)
        valid &= indices[i] < kSectionVertexCount && indices[i+1] < kSectionVertexCount && indices[i+2] < kSectionVertexCount
            && indices[i] != indices[i+1] && indices[i+1] != indices[i+2] && indices[i] != indices[i+2];
    check(valid, "all triangles are distinct and within the vertex buffer");
    check(!BuildTerrainIndices({}), "invalid index span rejected");
    if (!failures) std::cout << "PASS: flatness, bounds, seams, variation, distant precision, incremental rows and indices\n";
    return failures ? 1 : 0;
}
