#include "app/measure-options.h"
#include <iostream>
using namespace vaporcruise::app;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; } };
    MeasureOptions options; std::wstring error;
    check(ParseMeasureOptions({L"--frame-limit",L"60",L"--measure",L"limited.csv"},options,error)
          && options.frameLimit == 60 && options.presentInterval == 1,
          "optional cap preserves VSync and parses before mode selector");
    check(ParseMeasureOptions({L"--measure",L"limited.csv",L"--frame-limit",L"0"},options,error)
          && options.frameLimit == 0, "explicit zero selects existing display-refresh pacing");
    for (const auto& args : std::vector<std::vector<std::wstring>>{
            {L"--measure",L"a.csv",L"--frame-limit"},
            {L"--measure",L"a.csv",L"--frame-limit",L"59"},
            {L"--measure",L"a.csv",L"--frame-limit",L"61"},
            {L"--measure",L"a.csv",L"--frame-limit",L"-1"},
            {L"--measure",L"a.csv",L"--frame-limit",L"nan"},
            {L"--measure",L"a.csv",L"--frame-limit",L"60",L"--frame-limit",L"0"}})
        check(!ParseMeasureOptions(args,options,error), "invalid or duplicate cap fails before launch");
    check(ParseMeasureOptions({L"--measure", L"a space/run.csv", L"--duration", L"5", L"--warmup", L"30",
        L"--width", L"1920", L"--height", L"1080", L"--palette", L"2", L"--present-interval", L"0", L"--fullscreen"}, options, error)
        && options.path == L"a space/run.csv" && options.durationSeconds == 5 && options.warmupSeconds == 30
        && options.width == 1920 && options.height == 1080 && options.palette == 2 && options.presentInterval == 0 && options.fullscreen,
        "bounded measurement options reach the live scheduler");
    check(ParseMeasureOptions({L"--measure", L"run.csv"}, options, error)
        && options.warmupSeconds == 30 && options.durationSeconds == 300 && options.presentInterval == 1 && !options.gpuDetail && options.frameLimit == 0,
        "minimal capture has reference warmup duration and synchronized interval");
    check(ParseMeasureOptions({L"--gpu-detail", L"--measure", L"run.csv"}, options, error) && options.gpuDetail,
        "GPU detail is a valueless opt-in before the mode selector");
    check(ParseMeasureOptions({L"--measure", L"run.csv", L"--gpu-detail", L"--duration", L"1"}, options, error)
        && options.gpuDetail && options.durationSeconds == 1, "GPU detail does not consume a following option");
    const std::vector<std::vector<std::wstring>> invalid = {
        {}, {L"--measure"}, {L"--measure", L""}, {L"--measure", L"run.png"},
        {L"--measure", L"run.csv", L"--capture", L"run.png"},
        {L"--duration", L"300"}, {L"--measure", L"run.csv", L"--bogus", L"0"},
        {L"--measure", L"run.csv", L"--duration", L"0"}, {L"--measure", L"run.csv", L"--duration", L"28801"},
        {L"--measure", L"run.csv", L"--warmup", L"-1"}, {L"--measure", L"run.csv", L"--warmup", L"601"},
        {L"--measure", L"run.csv", L"--duration", L"nan"}, {L"--measure", L"run.csv", L"--duration", L"inf"},
        {L"--measure", L"run.csv", L"--width", L"640px"}, {L"--measure", L"run.csv", L"--height", L"2.5"},
        {L"--measure", L"run.csv", L"--palette", L"3"}, {L"--measure", L"run.csv", L"--present-interval", L"2"},
        {L"--measure", L"run.csv", L"--fullscreen", L"--fullscreen"},
        {L"--measure", L"run.csv", L"--width", L"640", L"--width", L"800"},
        {L"--gpu-detail"}, {L"--measure", L"run.csv", L"--gpu-detail", L"--gpu-detail"},
        {L"--measure", L"run.csv", L"--gpu-detail", L"1"}
    };
    for (const auto& args : invalid) check(!ParseMeasureOptions(args, options, error) && !error.empty(), "malformed or mixed capture flags fail before window creation");
    return failures ? 1 : 0;
}
