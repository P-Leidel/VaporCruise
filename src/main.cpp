// Parse one typed run plan before creating any native application resources.
#include <Windows.h>
#include <shellapi.h>
#include <chrono>
#include <type_traits>
#include "app/run-modes.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    using namespace vaporcruise::app;
    const double startupStart = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int count = 0;
    auto values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) return ReportRunFailure(nullptr, SW_HIDE, L"VaporCruise: command line parsing failed.");
    std::vector<std::wstring> arguments;
    for (int i = 1; i < count; ++i) arguments.emplace_back(values[i]);
    LocalFree(values);
    RunPlan plan;
    std::wstring error;
    if (!ParseRunPlan(arguments, plan, error)) return ReportRunFailure(nullptr, SW_HIDE, error.c_str());
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const RunContext context{instance, showCommand, startupStart};
    return std::visit([&](const auto& mode) {
        using Mode = std::decay_t<decltype(mode)>;
        if constexpr (std::is_same_v<Mode, InteractivePlan>) return RunInteractive(context, mode);
        else if constexpr (std::is_same_v<Mode, CapturePlan>) return RunCapture(context, mode);
        else return RunMeasurement(context, mode);
    }, plan);
}
