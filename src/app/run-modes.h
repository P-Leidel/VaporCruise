#pragma once
#include <Windows.h>
#include "app/run-plan.h"
namespace vaporcruise::app {
struct RunContext { HINSTANCE instance; int showCommand; double startupStartSeconds; };
int RunInteractive(const RunContext&, const InteractivePlan&);
int RunCapture(const RunContext&, const CapturePlan&);
int RunMeasurement(const RunContext&, const MeasurementPlan&);
int ReportRunFailure(HWND window, int showCommand, const wchar_t* message);
}
