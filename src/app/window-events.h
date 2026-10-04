#pragma once
#include <cstdint>
#include <variant>
namespace vaporcruise::app {
struct LifecycleChanged { bool resetTiming=true; };
struct ResizeRequested { std::uint32_t width,height; };
enum class SceneCommand { NextPalette, TogglePause, ShowHints };
struct FrameLimitRequested { bool enabled; };
using WindowEvent=std::variant<LifecycleChanged,ResizeRequested,SceneCommand,FrameLimitRequested>;
}
