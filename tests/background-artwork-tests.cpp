#include "scene/background-artwork.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace vaporcruise::scene;
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* text) { if (!value) { std::cerr << "FAIL: " << text << '\n'; ++failures; } };
    const auto& art = GetBackgroundArtwork();
    check(IsValidBackgroundArtwork(art), "trusted generated artwork satisfies public shape contract");
    auto malformed=art;malformed.coverage.channels=2;
    check(!IsValidBackgroundArtwork(malformed), "reject wrong mask channels");
    malformed=art;malformed.steadyEmission.slices.pop_back();
    check(!IsValidBackgroundArtwork(malformed), "reject missing layout");
    malformed=art;malformed.animatedEmission.slices[0].pop_back();
    check(!IsValidBackgroundArtwork(malformed), "reject missing mip");
    malformed=art;malformed.coverage.slices[0][0].rowPitch=1;
    check(!IsValidBackgroundArtwork(malformed), "reject invalid pitch");
    malformed=art;malformed.landscapeDetail[0].pixels.pop_back();
    check(!IsValidBackgroundArtwork(malformed), "reject truncated detail data");
    check(&art == &GetBackgroundArtwork(), "immutable artwork retained across calls");
    check(art.GpuBytes() <= kBackgroundByteBudget, "all baked artwork fits the fixed GPU storage budget");
    check(!art.landscapeDetail.empty(), "sky and mountain detail is baked once and retained");
    if (!art.landscapeDetail.empty()) {
        const auto& detail = art.landscapeDetail.front();
        std::size_t stars = 0, wisps = 0;
        for (std::size_t p = 0; p < detail.pixels.size(); p += 4) {
            stars += detail.pixels[p+1] > 32;
            wisps += detail.pixels[p] > 12;
        }
        check(stars > 40 && stars < detail.width * detail.height * .01, "sparse stars occupy less than one percent of the sky atlas");
        check(wisps > 1000, "baked wisps provide broad sky detail");
        check(art.landscapeDetail.back().width == 1 && art.landscapeDetail.back().height == 1,
              "sky detail has a complete mip chain for minification");
    }
    for (const auto* mask : {&art.coverage, &art.steadyEmission, &art.animatedEmission}) {
        check(mask->slices.size() == 4, "four fixed aspect layouts");
        for (const auto& slice : mask->slices) {
            check(slice.size() == 11, "full mip chain");
            for (const auto& mip : slice)
                check(mip.rowPitch == mip.width * mask->channels && mip.pixels.size() == mip.rowPitch * mip.height,
                      "all mip pitches and payload sizes agree");
        }
    }
    for (unsigned layout = 0; layout < 4; ++layout) {
        const auto buildings = BackgroundBuildings(layout);
        check(buildings.size() == 36, "broad 36 building skyline");
        check(std::count_if(buildings.begin(), buildings.end(), [](auto b) { return b.landmark; }) == 1,
              "exactly one landmark");
        const auto landmark = *std::find_if(buildings.begin(), buildings.end(), [](auto b) { return b.landmark; });
        check(landmark.center > 0 && landmark.height >= .12f, "dominant right-hand landmark");
        if (layout == 3) check(landmark.center + landmark.width * .5f < .28125f, "portrait landmark fully visible");
        for (unsigned i = 0; i < buildings.size(); ++i) {
            const auto original = BackgroundBuildings(0)[i];
            check(buildings[i].width == original.width && buildings[i].height == original.height,
                  "aspect layout changes centers without stretching shapes");
        }
        const auto& coverage = art.coverage.slices[layout][0].pixels;
        const auto& steady = art.steadyEmission.slices[layout][0].pixels;
        const auto& animated = art.animatedEmission.slices[layout][0].pixels;
        const auto bodyAt = [&](float x, float y) {
            const auto column = static_cast<unsigned>(std::clamp(
                static_cast<int>((x/kCityHalfWidth+1)*.5f*kBackgroundWidth), 0,
                static_cast<int>(kBackgroundWidth)-1));
            const auto row = static_cast<unsigned>(std::clamp(
                static_cast<int>((kBackgroundTop-y)/(kBackgroundTop-kBackgroundBottom)*kBackgroundHeight), 0,
                static_cast<int>(kBackgroundHeight)-1));
            return coverage[(row*kBackgroundWidth+column)*4+3];
        };
        // Removing the aperture or filling the skyline from one rectangle
        // would lose the two separate upper shafts of the distant landmark.
        for (const float height : {.70f, .76f, .82f}) {
            check(bodyAt(landmark.center, landmark.height*height) == 0,
                  "landmark upper shafts retain an open sky aperture in every aspect layout");
            check(bodyAt(landmark.center-landmark.width*.23f, landmark.height*height) > 200 &&
                  bodyAt(landmark.center+landmark.width*.23f, landmark.height*height) > 200,
                  "landmark aperture separates two substantial dark towers");
        }
        if (layout == 0) {
            // Slim lower supports interrupt the base instead of joining every
            // tower into the black plinth visible across the sunset.
            unsigned gapSamples = 0;
            for (unsigned sample = 0; sample < 800; ++sample)
                gapSamples += bodyAt(-.415f + sample*(.83f/800), .0035f) == 0;
            check(gapSamples > 800*.18f,
                  "separated building supports leave visible gaps across the distant city foot");
            const auto shaftWidth = [&](float height) {
                unsigned count = 0;
                const auto& tower = buildings[10];
                for (unsigned sample = 0; sample < 100; ++sample)
                    count += bodyAt(tower.center-tower.width*.5f+sample*(tower.width/100),
                                    tower.height*height) > 200;
                return count;
            };
            check(shaftWidth(.62f) + 10 < shaftWidth(.25f),
                  "prominent left shaft tapers through its body before the crown");
        }
        std::size_t lit = 0, warmEmission = 0, accentEmission = 0, groups = 0,
                    mountains[3]{}, bodyPixels = 0, brightPixels = 0;
        for (std::size_t p = 0; p < coverage.size() / 4; ++p) {
            for (unsigned channel = 0; channel < 3; ++channel) mountains[channel] += coverage[p*4+channel];
            lit += steady[p*2] + steady[p*2+1];
            warmEmission += steady[p*2]; accentEmission += steady[p*2+1];
            bodyPixels += coverage[p*4+3] > 200;
            brightPixels += steady[p*2] > 24 || steady[p*2+1] > 24 ||
                *std::max_element(animated.begin()+p*4, animated.begin()+p*4+4) > 24;
            for (unsigned channel = 0; channel < 4; ++channel) groups += animated[p*4+channel];
            if (coverage[p*4+3] == 0) {
                check(steady[p*2] == 0 && steady[p*2+1] == 0, "steady light confined to buildings");
                check(std::all_of(animated.begin()+p*4, animated.begin()+p*4+4, [](auto v) { return v == 0; }),
                      "animated light confined to buildings");
            }
            check(steady[p*2] <= coverage[p*4+3] && steady[p*2+1] <= coverage[p*4+3] &&
                  std::all_of(animated.begin()+p*4, animated.begin()+p*4+4,
                              [&](auto value) { return value <= coverage[p*4+3]; }),
                  "architectural lights and local glow respect antialiased silhouette coverage");
        }
        check(mountains[0] > mountains[1] && mountains[1] > mountains[2] && mountains[2] > 0,
              "three distinct mountain coverage layers");
        check(lit > groups && groups > 0, "most window emission steady, minority animated");
        check(warmEmission > accentEmission + groups,
              "ordinary warm windows remain dominant over architectural and animated accents");
        check(brightPixels > 20 && brightPixels < bodyPixels * .025,
              "city keeps compact highlights sparse across dark architecture");
        // Treat actual connected accent pixels as fixtures. A reintroduced
        // shaft stripe or linked ledges would exceed these compact extents.
        std::vector<bool> visited(kBackgroundWidth*kBackgroundHeight);
        unsigned fixtures = 0, upperLandmarkFixtures = 0;
        for (unsigned start = 0; start < visited.size(); ++start) {
            if (visited[start] || steady[start*2+1] <= 12) continue;
            std::vector<unsigned> pending{start}; visited[start] = true;
            unsigned left = kBackgroundWidth, right = 0, top = kBackgroundHeight, bottom = 0;
            bool upperLandmark = false;
            for (unsigned next = 0; next < pending.size(); ++next) {
                const auto p = pending[next], x = p%kBackgroundWidth, y = p/kBackgroundWidth;
                left = std::min(left,x); right = std::max(right,x);
                top = std::min(top,y); bottom = std::max(bottom,y);
                const float px = ((x+.5f)/kBackgroundWidth*2-1)*kCityHalfWidth;
                const float py = kBackgroundTop-(y+.5f)/kBackgroundHeight*(kBackgroundTop-kBackgroundBottom);
                upperLandmark |= py > .092f && std::abs(px-landmark.center) < landmark.width*.5f;
                const auto visit = [&](unsigned neighbor) {
                    if (!visited[neighbor] && steady[neighbor*2+1] > 12) {
                        visited[neighbor] = true; pending.push_back(neighbor);
                    }
                };
                if (x) visit(p-1);
                if (x+1 < kBackgroundWidth) visit(p+1);
                if (y) visit(p-kBackgroundWidth);
                if (y+1 < kBackgroundHeight) visit(p+kBackgroundWidth);
            }
            ++fixtures; upperLandmarkFixtures += upperLandmark;
            check(right-left+1 <= 8 && bottom-top+1 <= 5 && pending.size() <= 24,
                  "architectural accents are short separate fixtures instead of connected strips");
        }
        check(fixtures >= 3 && fixtures <= 12, "a few discrete architectural fixtures remain visible");
        check(upperLandmarkFixtures >= 1 && upperLandmarkFixtures <= 2,
              "landmark upper towers carry only a few compact highlights");
    }
    check(BackgroundLayout(2.37f) == 0 && BackgroundLayout(1.5f) == 0 && BackgroundLayout(1.f) == 1 &&
          BackgroundLayout(.7f) == 2 && BackgroundLayout(.5625f) == 3, "aspect layout selection");
    const auto landmark = BackgroundBuildings(3)[27];
    for (const float aspect : {.5625f, 480.f/2160, 480.f/2560, 240.f/7680}) {
        const float center = landmark.center - BackgroundCityHorizontalOffset(aspect);
        check(center > 0 && center + landmark.width*.5f < aspect*.5f &&
              center - landmark.width*.5f > -aspect*.5f,
              "landmark fully visible and right of center through supported tall aspects");
    }
    check(BackgroundCityHorizontalOffset(.5625f) == 0 && BackgroundCityHorizontalOffset(1.5f) == 0,
          "ordinary aspect composition unchanged");
    if (!failures) std::cout << "PASS: immutable panorama, 36 buildings, three mountains, aspect layouts, local emission and byte budget\n";
    return failures ? 1 : 0;
}
