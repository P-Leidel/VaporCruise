#pragma once
#include "app/capture-options.h"
#include "app/measure-options.h"
#include "scene/scene-pose.h"
#include <variant>
namespace vaporcruise::app {
struct InteractivePlan {};
struct CapturePlan { CaptureOptions options; scene::ScenePose pose; };
struct MeasurementPlan { MeasureOptions options; scene::ScenePose pose; };
using RunPlan = std::variant<InteractivePlan, CapturePlan, MeasurementPlan>;
bool ParseRunPlan(const std::vector<std::wstring>& arguments, RunPlan& result, std::wstring& error);
}
