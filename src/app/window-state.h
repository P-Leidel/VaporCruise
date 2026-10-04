#pragma once

#include <cstdint>

namespace vaporcruise::app {

// Plain lifecycle snapshot shared by the native window and pure loop policy.
struct WindowState {
    bool minimized = false;
    bool inSizeMove = false;
    bool fullscreen = false;
    bool cloaked = false;
    bool hidden = false;
    bool sessionSuspended = false;
    bool powerSuspended = false;
    uint32_t width = 0;
    uint32_t height = 0;
};

}  // namespace vaporcruise::app
