#pragma once
#include <cstdint>
#include <string>
namespace vaporcruise::app {
bool ParseUnsignedDecimal(const std::wstring& text, std::uint64_t maximum, std::uint64_t& result);
bool ParseFiniteNumber(const std::wstring& text, double& result);
}
