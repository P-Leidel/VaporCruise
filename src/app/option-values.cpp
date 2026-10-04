#include "app/option-values.h"
#include <cmath>
namespace vaporcruise::app {
bool ParseUnsignedDecimal(const std::wstring& text, std::uint64_t maximum, std::uint64_t& result) {
    if (text.empty()) return false;
    std::uint64_t number = 0;
    for (wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (number > maximum / 10 || (number == maximum / 10 && digit > maximum % 10)) return false;
        number = number * 10 + digit;
    }
    result = number;
    return true;
}
bool ParseFiniteNumber(const std::wstring& text, double& result) {
    size_t consumed = 0;
    double number = 0;
    try { number = std::stod(text, &consumed); } catch (...) { return false; }
    if (consumed != text.size() || !std::isfinite(number)) return false;
    result = number;
    return true;
}
}
