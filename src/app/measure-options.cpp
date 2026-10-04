#include "app/measure-options.h"
#include "app/option-values.h"
#include <cmath>
#include <filesystem>
#include <set>
namespace vaporcruise::app {
bool ParseMeasureOptions(const std::vector<std::wstring>& args, MeasureOptions& result, std::wstring& error) {
    result = {}; error.clear();
    MeasureOptions parsed;
    std::set<std::wstring> seen;
    auto fail = [&]() { error = L"Invalid measurement arguments. Use --measure run.csv [--duration 300 --warmup 30 --width 1920 --height 1080 --palette 0..2 --present-interval 0..1 --frame-limit 0|60 --fullscreen --gpu-detail]."; return false; };
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& key = args[i];
        if (!seen.insert(key).second) return fail();
        if (key == L"--fullscreen") { parsed.fullscreen = true; continue; }
        if (key == L"--gpu-detail") { parsed.gpuDetail = true; continue; }
        if (++i >= args.size()) return fail();
        const auto& value = args[i];
        if (key == L"--measure") {
            if (value.empty() || std::filesystem::path(value).extension() != L".csv") return fail();
            parsed.path = value; continue;
        }
        double number = 0;
        if (!ParseFiniteNumber(value, number)) return fail();
        if (key == L"--width" || key == L"--height") {
            if (number < 240 || number > 7680 || std::floor(number) != number) return fail();
            (key == L"--width" ? parsed.width : parsed.height) = static_cast<unsigned>(number);
        } else if (key == L"--palette" || key == L"--present-interval") {
            if (number < 0 || number > (key == L"--palette" ? 2 : 1) || std::floor(number) != number) return fail();
            (key == L"--palette" ? parsed.palette : parsed.presentInterval) = static_cast<unsigned>(number);
        } else if (key == L"--frame-limit") {
            if (number != 0 && number != 60) return fail();
            parsed.frameLimit = static_cast<unsigned>(number);
        } else if (key == L"--duration") {
            if (number < 1 || number > 28800) return fail();
            parsed.durationSeconds = number;
        } else if (key == L"--warmup") {
            if (number < 0 || number > 600) return fail();
            parsed.warmupSeconds = number;
        } else return fail();
    }
    if (parsed.path.empty()) return fail();
    result = parsed; return true;
}
}
