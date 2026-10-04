#include "app/run-plan.h"
namespace vaporcruise::app {
bool ParseRunPlan(const std::vector<std::wstring>& arguments, RunPlan& result, std::wstring& error) {
    error.clear();
    if (arguments.empty()) { result = InteractivePlan{}; return true; }
    bool capture = false, measurement = false;
    // Only keys select a mode. An output filename is never interpreted as a switch.
    for (size_t i = 0; i < arguments.size();) {
        const auto& key = arguments[i];
        capture |= key == L"--capture";
        measurement |= key == L"--measure";
        i += key == L"--fullscreen" || key == L"--gpu-detail" ? 1 : 2;
    }
    if (capture && measurement) { error = L"Capture and measurement modes cannot be combined."; return false; }
    if (measurement) {
        MeasurementPlan plan;
        if (!ParseMeasureOptions(arguments, plan.options, error)) return false;
        plan.pose.palette = plan.options.palette;
        result = std::move(plan);
        return true;
    }
    CapturePlan plan;
    if (!ParseCaptureOptions(arguments, plan.options, error)) return false;
    const auto& options = plan.options;
    plan.pose = {options.seed, {options.travelCell, options.phase}, options.cityLightPhase,
        options.palette, options.transition};
    result = std::move(plan);
    return true;
}
}
