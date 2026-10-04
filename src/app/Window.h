#pragma once

#include <Windows.h>

#include <cstdint>
#include <functional>

#include "app/window-state.h"
#include "app/window-events.h"

namespace vaporcruise::app {

// Borderless, resizable, drag-to-move Win32 window. Owns no rendering state;
// reports typed lifecycle/resize/input events so app/render/scene stay
// decoupled. Events a caller reacts to as they
// happen (a completed resize, a keypress) are push callbacks; state a caller
// only ever polls (minimized/mid-resize/fullscreen/cloaked/size) is pull, via
// State().
class Window {
public:
    struct Callbacks {
        std::function<void(const WindowEvent&)> onEvent;
#ifdef _DEBUG
        // Debug-only: lets main.cpp deliberately force device removal to
        // validate Renderer's bounded recovery path end-to-end. Compiled out of
        // Release builds entirely; it is not a shipping user control.
        std::function<void()> onSimulateDeviceLoss;
#endif
    };

    ~Window();
    bool Create(HINSTANCE hInstance, Callbacks callbacks);
    void Show(int showCommand);
    bool SetClientSize(uint32_t width, uint32_t height);
    bool SetFullscreen(bool enabled);
    void SetFrameLimitStatus(bool enabled, bool allowChanges = true);

    HWND Handle() const { return hwnd_; }
    WindowState State() const;

private:
    static LRESULT CALLBACK WindowProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void NotifyResize();
    void Emit(const WindowEvent& event) { if(callbacks_.onEvent)callbacks_.onEvent(event); }
    void RefreshCursor();
    void RefreshFrameLimitStatus();

    HWND hwnd_ = nullptr;
    Callbacks callbacks_;

    bool isMinimized_ = false;
    bool isInSizeMove_ = false;
    bool isFullscreen_ = false;
    bool sessionLocked_ = false;
    bool sessionDisconnected_ = false;
    bool powerSuspended_ = false;
    bool sessionNotificationsRegistered_ = false;
    bool frameLimit60_ = false, frameLimitChangesAllowed_ = true;
    WINDOWPLACEMENT savedWindowPlacement_{sizeof(WINDOWPLACEMENT)};
    UINT fullscreenRestoreFlags_ = 0;
};

}  // namespace vaporcruise::app
