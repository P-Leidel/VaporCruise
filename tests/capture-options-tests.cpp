#include "app/capture-options.h"
#include <iostream>
#include <vector>
using namespace vaporcruise::app;
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* name) {
        if (!value) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    CaptureOptions options;
    std::wstring error;
    for (const auto* retired : {L"--interior", L"--interior-transition", L"--cabin-seconds"})
        check(!ParseCaptureOptions({L"--capture", L"a.png", retired, L"0"}, options, error)
            && !error.empty(), "removed car capture controls are rejected");
    check(ParseCaptureOptions({}, options, error) && options.path.empty(), "no flags keep interactive startup");
    check(ParseCaptureOptions({L"--capture", L"a space/image.png", L"--width", L"720", L"--height", L"1280",
        L"--palette", L"2", L"--phase", L"0.25", L"--transition", L"0.5", L"--hint", L"1", L"--dpi", L"144"}, options, error)
        && options.path == L"a space/image.png" && options.width == 720 && options.height == 1280
        && options.palette == 2 && options.phase == .25f && options.transition == .5f
        && options.hint == 1 && options.dpi == 144, "deterministic capture inputs reach the renderer unchanged");
    const std::vector<std::vector<std::wstring>> invalid = {
        {L"--capture"}, {L"--capture", L""}, {L"--capture", L"a.jpg"},
        {L"--width", L"720"}, {L"--capture", L"a.png", L"--bogus", L"0"},
        {L"--capture", L"a.png", L"--width", L"0"}, {L"--capture", L"a.png", L"--height", L"99999"},
        {L"--capture", L"a.png", L"--width", L"640px"}, {L"--capture", L"a.png", L"--width", L"2.5"},
        {L"--capture", L"a.png", L"--palette", L"3"}, {L"--capture", L"a.png", L"--phase", L"nan"},
        {L"--capture", L"a.png", L"--phase", L"inf"}, {L"--capture", L"a.png", L"--phase", L"1"},
        {L"--capture", L"a.png", L"--transition", L"-1"}, {L"--capture", L"a.png", L"--hint", L"2"},
        {L"--capture", L"a.png", L"--dpi", L"0"}, {L"--capture", L"a.png", L"--width", L"640", L"--width", L"800"}
    };
    for (const auto& args : invalid)
        check(!ParseCaptureOptions(args, options, error) && !error.empty(), "invalid flags fail with a diagnostic");
    check(ParseCaptureOptions({L"--capture", L"a.png", L"--seed", L"18446744073709551615",
        L"--travel-cell", L"9223372036854774783", L"--phase", L"0.9999999"}, options, error),
        "exact full-width integer capture arguments accepted");
    check(options.seed == 18446744073709551615ULL && options.travelCell == 9223372036854774783LL,
        "capture integers never round through floating point");
    for (const auto* bad : {L"18446744073709551616", L"-1", L"+1", L"1.0", L"1e2", L" 1", L"1 ", L"1x"})
        check(!ParseCaptureOptions({L"--capture", L"a.png", L"--seed", bad}, options, error), "bad seed rejected");
    for (const auto* bad : {L"9223372036854774784", L"-1", L"+1", L"1.0", L"1e2", L" 1", L"1 ", L"1x"})
        check(!ParseCaptureOptions({L"--capture", L"a.png", L"--travel-cell", bad}, options, error), "bad cell rejected");
    check(!ParseCaptureOptions({L"--capture", L"a.png", L"--seed", L"1", L"--seed", L"2"}, options, error),
        "duplicate seed rejected");
    check(!ParseCaptureOptions({L"--seed", L"1"}, options, error), "pose flags require explicit capture");
    check(ParseCaptureOptions({L"--capture", L"a.png"}, options, error) && options.cityLightPhase == 0,
          "default deterministic city phase");
    check(ParseCaptureOptions({L"--capture", L"a.png", L"--city-light-phase", L".25"}, options, error)
          && options.cityLightPhase == .25, "explicit light phase");
    for (const auto* bad : {L"-1", L"1", L"nan", L"inf", L".2x"})
        check(!ParseCaptureOptions({L"--capture", L"a.png", L"--city-light-phase", bad}, options, error),
              "invalid city phase rejected");
    check(!ParseCaptureOptions({L"--capture", L"a.png", L"--city-light-phase", L".1", L"--city-light-phase", L".2"}, options, error),
          "duplicate city phase rejected");
    check(!ParseCaptureOptions({L"--capture", L"a.png", L"--city-light-phase"}, options, error), "missing city phase rejected");
    return failures ? 1 : 0;
}
