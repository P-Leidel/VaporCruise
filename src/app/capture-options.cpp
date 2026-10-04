#include "app/capture-options.h"
#include "app/option-values.h"
#include <cmath>
#include <filesystem>
#include <set>
namespace vaporcruise::app {
bool ParseCaptureOptions(const std::vector<std::wstring>& args,
                         CaptureOptions& result, std::wstring& error) {
    result = {};
    error.clear();
    if (args.empty()) return true;
    CaptureOptions parsed;
    std::set<std::wstring> seen;
    auto fail = [&]() { error = L"Invalid capture arguments. Use --capture file.png [--width 1280 --height 720 --palette 0..2 --seed unsigned-integer --travel-cell nonnegative-integer --phase 0..<1 --city-light-phase 0..<1 --transition 0..1 --hint 0..1 --dpi 96]."; return false; };
    for (size_t i = 0; i < args.size(); i += 2) {
        if (i + 1 >= args.size() || !seen.insert(args[i]).second) return fail();
        const auto& key = args[i];
        const auto& value = args[i + 1];
        if (key == L"--capture") {
            if (value.empty() || std::filesystem::path(value).extension() != L".png") return fail();
            parsed.path = value;
            continue;
        }
        if (key == L"--seed" || key == L"--travel-cell") {
            std::uint64_t integer = 0;
            const auto maximum = key == L"--seed" ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(scene::kMaxTravelCell);
            if (!ParseUnsignedDecimal(value, maximum, integer)) return fail();
            if (key == L"--seed") parsed.seed = integer;
            else parsed.travelCell = static_cast<std::int64_t>(integer);
            continue;
        }
        double number = 0;
        if (!ParseFiniteNumber(value, number)) return fail();
        if (key == L"--width" || key == L"--height") {
            if (number < 240 || number > 7680 || std::floor(number) != number) return fail();
            (key == L"--width" ? parsed.width : parsed.height) = static_cast<unsigned>(number);
        } else if (key == L"--palette") {
            if (number < 0 || number > 2 || std::floor(number) != number) return fail();
            parsed.palette = static_cast<unsigned>(number);
        } else if (key == L"--city-light-phase") {
            if (number < 0 || number >= 1) return fail();
            parsed.cityLightPhase = number;
        } else if (key == L"--phase") {
            if (number < 0 || number >= 1) return fail();
            parsed.phase = number;
            if (parsed.phase >= 1) return fail();
        } else if (key == L"--transition" || key == L"--hint") {
            if (number < 0 || number > 1) return fail();
            (key == L"--transition" ? parsed.transition : parsed.hint) = static_cast<float>(number);
        } else if (key == L"--dpi") {
            if (number < 48 || number > 384) return fail();
            parsed.dpi = static_cast<float>(number);
        } else return fail();
    }
    if (parsed.path.empty()) return fail();
    result = parsed;
    return true;
}
}
