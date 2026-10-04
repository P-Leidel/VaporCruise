#pragma once
#include <string>
#include <vector>
namespace vaporcruise::app {
struct MeasureOptions {
    std::wstring path;
    unsigned width = 1920, height = 1080, palette = 0, presentInterval = 1, frameLimit = 0;
    double warmupSeconds = 30, durationSeconds = 300;
    bool fullscreen = false, gpuDetail = false;
};
bool ParseMeasureOptions(const std::vector<std::wstring>& args, MeasureOptions& result, std::wstring& error);
}
