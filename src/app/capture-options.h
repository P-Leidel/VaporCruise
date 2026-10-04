#pragma once
#include <string>
#include <vector>
#include "scene/cruise-position.h"

namespace vaporcruise::app {
struct CaptureOptions {
    std::wstring path;
    unsigned width = 1280, height = 720;
    unsigned palette = 0;
    std::uint64_t seed = scene::kDefaultTerrainSeed;
    std::int64_t travelCell = 0;
    double phase = 0.0;
    double cityLightPhase = 0.0;
    float transition = 0.0f, hint = 0.0f, dpi = 96.0f;
};
// Empty arguments select ordinary interactive startup. Other arguments must
// describe an explicit one-shot capture; malformed input never opens a window.
bool ParseCaptureOptions(const std::vector<std::wstring>& args,
                         CaptureOptions& result, std::wstring& error);
}
