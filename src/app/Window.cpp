#include "app/Window.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

namespace vaporcruise::app {

namespace {
constexpr wchar_t kWindowClassName[] = L"VaporCruiseWindowClass";
constexpr wchar_t kWindowTitle[] = L"VaporCruise";
constexpr UINT kFrameLimitCommand = 0x1000;
constexpr int kResizeBorderPx = 8;
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;
constexpr int kMinWidth = 480;
constexpr int kMinHeight = 320;
}  // namespace

Window::~Window() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

bool Window::Create(HINSTANCE hInstance, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Window::WindowProcStatic;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        OutputDebugStringW(L"VaporCruise: window class registration failed\n");
        return false;
    }

    // WS_POPUP + WS_THICKFRAME + WS_CAPTION/WS_SYSMENU/min/max, combined with
    // WM_NCCALCSIZE returning an empty non-client area below, is the standard
    // technique for a window with no visible chrome that still gets the OS's
    // native resize/aero-snap/shadow behavior through custom nonclient handling.
    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                         WS_MAXIMIZEBOX | WS_MINIMIZEBOX;

    hwnd_ = CreateWindowExW(
        0, kWindowClassName, kWindowTitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) {
        OutputDebugStringW(L"VaporCruise: window creation failed\n");
        return false;
    }

    const HMENU menu = GetSystemMenu(hwnd_, FALSE);
    if (!menu || !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu, MF_STRING, kFrameLimitCommand, L"Limit to 60 FPS\tL")) {
        OutputDebugStringW(L"VaporCruise: frame limit menu initialization failed\n");
        return false;
    }
    RefreshFrameLimitStatus();

    sessionNotificationsRegistered_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (!sessionNotificationsRegistered_) {
        OutputDebugStringW(L"VaporCruise: WTS session notification registration failed\n");
        return false;
    }

    // Forces Windows to re-run WM_NCCALCSIZE now that the window (and its
    // WndProc binding) fully exists, so the caption/border are actually
    // suppressed rather than whatever the very first, pre-binding pass
    // resolved to.
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    return true;
}

void Window::Show(int showCommand) { ShowWindow(hwnd_, showCommand); }

void Window::SetFrameLimitStatus(bool enabled, bool allowChanges) {
    if (frameLimit60_ == enabled && frameLimitChangesAllowed_ == allowChanges) return;
    frameLimit60_ = enabled; frameLimitChangesAllowed_ = allowChanges;
    if (hwnd_) RefreshFrameLimitStatus();
}

void Window::RefreshFrameLimitStatus() {
    const HMENU menu = GetSystemMenu(hwnd_, FALSE);
    CheckMenuItem(menu, kFrameLimitCommand, MF_BYCOMMAND | (frameLimit60_ ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, kFrameLimitCommand, MF_BYCOMMAND | (frameLimitChangesAllowed_ ? MF_ENABLED : MF_GRAYED));
    SetWindowTextW(hwnd_, frameLimit60_ ? L"VaporCruise - 60 FPS cap" : L"VaporCruise - Display refresh");
}

bool Window::SetClientSize(uint32_t width, uint32_t height) {
    RECT requested{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectExForDpi(&requested, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)),
        FALSE, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), GetDpiForWindow(hwnd_))) return false;
    constexpr UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (!SetWindowPos(hwnd_, nullptr, 0, 0, requested.right - requested.left, requested.bottom - requested.top, flags)) return false;
    // WM_NCCALCSIZE deliberately removes chrome in this borderless shell.
    // Correct the style-based estimate from actual geometry, then verify it.
    RECT client{}, outer{};
    if (!GetClientRect(hwnd_, &client) || !GetWindowRect(hwnd_, &outer)) return false;
    const LONG clientWidth = client.right - client.left, clientHeight = client.bottom - client.top;
    if (clientWidth != static_cast<LONG>(width) || clientHeight != static_cast<LONG>(height)) {
        if (!SetWindowPos(hwnd_, nullptr, 0, 0, outer.right - outer.left + static_cast<int>(width) - clientWidth,
            outer.bottom - outer.top + static_cast<int>(height) - clientHeight, flags)) return false;
    }
    return GetClientRect(hwnd_, &client) && client.right - client.left == static_cast<LONG>(width)
        && client.bottom - client.top == static_cast<LONG>(height);
}

LRESULT CALLBACK Window::WindowProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;

    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SETCURSOR:
            // Scope the fullscreen cursor to our client. Leave windowed drag
            // and resize shapes to DefWindowProc; never alter ShowCursor counts.
            if (isFullscreen_ && reinterpret_cast<HWND>(wParam) == hwnd &&
                LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;

        case WM_NCCALCSIZE: {
            if (wParam) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                if (!isFullscreen_ && IsZoomed(hwnd)) {
                    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
                    // Windows can still inflate the maximized outer rectangle
                    // by its invisible resize frame after WM_GETMINMAXINFO.
                    // Use the proposed rectangle's monitor when moving/DPI
                    // changing, and keep those frame pixels out of the client.
                    if (GetMonitorInfoW(MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
                        RECT client{};
                        if (IntersectRect(&client, &params->rgrc[0], &monitorInfo.rcWork))
                            params->rgrc[0] = client;
                    }
                }
                // Normal/fullscreen client = whole window; no chrome is painted.
                return 0;
            }
            break;
        }

        case WM_NCHITTEST: {
            if (isFullscreen_) {
                return HTCLIENT;
            }
            // A maximized window can be dragged/restored through its caption,
            // but its work-area edges must not behave as resize borders.
            if (IsZoomed(hwnd)) return HTCAPTION;

            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT windowRect{};
            GetWindowRect(hwnd, &windowRect);

            const bool left = cursor.x < windowRect.left + kResizeBorderPx;
            const bool right = cursor.x >= windowRect.right - kResizeBorderPx;
            const bool top = cursor.y < windowRect.top + kResizeBorderPx;
            const bool bottom = cursor.y >= windowRect.bottom - kResizeBorderPx;

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;

            // No title bar exists, so the rest of the client area drags the
            // window (confirmed drag-to-move-anywhere decision).
            return HTCAPTION;
        }

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = kMinWidth;
            info->ptMinTrackSize.y = kMinHeight;
            if (!isFullscreen_) {
                MONITORINFO monitorInfo{sizeof(MONITORINFO)};
                if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
                    // Our entire outer rectangle is the client. Do not retain
                    // the styled window's invisible maximized frame overhang.
                    const RECT& work = monitorInfo.rcWork;
                    const RECT& monitor = monitorInfo.rcMonitor;
                    info->ptMaxPosition = {work.left - monitor.left, work.top - monitor.top};
                    info->ptMaxSize = {work.right - work.left, work.bottom - work.top};
                }
            }
            return 0;
        }

        case WM_WINDOWPOSCHANGING:
            if (fullscreenRestoreFlags_ != 0) {
                auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
                if ((fullscreenRestoreFlags_ & SWP_HIDEWINDOW) != 0)
                    position->flags &= ~SWP_SHOWWINDOW;
                position->flags |= fullscreenRestoreFlags_;
            }
            break;

        case WM_ENTERSIZEMOVE:
            isInSizeMove_ = true;
            Emit(LifecycleChanged{});
            return 0;

        case WM_EXITSIZEMOVE: {
            isInSizeMove_ = false;
            Emit(LifecycleChanged{});
            NotifyResize();
            return 0;
        }

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                isMinimized_ = true;
            } else {
                isMinimized_ = false;
                // An interactive drag reports its own end via WM_EXITSIZEMOVE;
                // this branch covers programmatic resizes (e.g. the fullscreen
                // toggle) that never enter that modal loop.
                if (!isInSizeMove_) {
                    NotifyResize();
                }
            }
            Emit(LifecycleChanged{});
            return 0;
        }

        case WM_DPICHANGED: {
            auto* suggestedRect = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr,
                         suggestedRect->left, suggestedRect->top,
                         suggestedRect->right - suggestedRect->left,
                         suggestedRect->bottom - suggestedRect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Emit(LifecycleChanged{});
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == kFrameLimitCommand) {
                if (frameLimitChangesAllowed_) Emit(FrameLimitRequested{!frameLimit60_});
                return 0;
            }
            break;

        case WM_KEYDOWN: {
            // Bit 30 is the previous key state: toggles respond once per press.
            if ((static_cast<ULONG_PTR>(lParam) & (1ull << 30)) != 0) return 0;
            switch (wParam) {
                case 'L':
                    if (frameLimitChangesAllowed_) Emit(FrameLimitRequested{!frameLimit60_});
                    return 0;
                case 'P':
                    Emit(SceneCommand::NextPalette);
                    return 0;
                case 'F':
                    SetFullscreen(!isFullscreen_);
                    return 0;
                case VK_SPACE:
                    Emit(SceneCommand::TogglePause);
                    return 0;
                case 'H':
                    Emit(SceneCommand::ShowHints);
                    return 0;
                case VK_ESCAPE:
                    if (isFullscreen_) {
                        SetFullscreen(false);
                    }
                    return 0;
#ifdef _DEBUG
                case 'X':
                    if (callbacks_.onSimulateDeviceLoss) callbacks_.onSimulateDeviceLoss();
                    return 0;
#endif
                default:
                    break;
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            Emit(LifecycleChanged{false});
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SHOWWINDOW:
            Emit(LifecycleChanged{});
            break;

        case WM_DWMCOMPOSITIONCHANGED:
            Emit(LifecycleChanged{});
            break;

        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK) sessionLocked_ = true;
            if (wParam == WTS_SESSION_UNLOCK) sessionLocked_ = false;
            if (wParam == WTS_CONSOLE_DISCONNECT || wParam == WTS_REMOTE_DISCONNECT)
                sessionDisconnected_ = true;
            if (wParam == WTS_CONSOLE_CONNECT || wParam == WTS_REMOTE_CONNECT)
                sessionDisconnected_ = false;
            Emit(LifecycleChanged{});
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) powerSuspended_ = true;
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
                powerSuspended_ = false;
            Emit(LifecycleChanged{});
            return TRUE;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (sessionNotificationsRegistered_) {
                WTSUnRegisterSessionNotification(hwnd);
                sessionNotificationsRegistered_ = false;
            }
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            break;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

WindowState Window::State() const {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);

    // Cloak is a visibility signal, not a reliable session lock notification.
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd_, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

    WindowState state;
    state.minimized = isMinimized_;
    state.inSizeMove = isInSizeMove_;
    state.fullscreen = isFullscreen_;
    state.cloaked = cloaked != FALSE;
    state.hidden = IsWindowVisible(hwnd_) == FALSE;
    state.sessionSuspended = sessionLocked_ || sessionDisconnected_;
    state.powerSuspended = powerSuspended_;
    state.width = static_cast<uint32_t>(clientRect.right - clientRect.left);
    state.height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
    return state;
}

void Window::NotifyResize() {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    Emit(ResizeRequested{static_cast<uint32_t>(clientRect.right-clientRect.left),
                         static_cast<uint32_t>(clientRect.bottom-clientRect.top)});
}

void Window::RefreshCursor() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const HWND owner = WindowFromPoint(cursor);
    if (!owner) return;

    // A stationary pointer may be outside our restored rectangle. Ask the
    // actual window underneath for its normal policy, rather than forcing an
    // arrow over another app. Bound cross-thread calls if that app is hung.
    constexpr UINT flags = SMTO_ABORTIFHUNG | SMTO_BLOCK;
    constexpr UINT timeoutMs = 50;
    DWORD_PTR hit = 0;
    if (!SendMessageTimeoutW(owner, WM_NCHITTEST, 0, MAKELPARAM(cursor.x, cursor.y),
                             flags, timeoutMs, &hit)) return;
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(owner, WM_SETCURSOR, reinterpret_cast<WPARAM>(owner),
                        MAKELPARAM(hit, WM_MOUSEMOVE), flags, timeoutMs, &ignored);
}

bool Window::SetFullscreen(bool enabled) {
    if (!hwnd_ || !IsWindow(hwnd_)) return false;
    if (enabled == isFullscreen_) return true;
    if (enabled) {
        WINDOWPLACEMENT original{sizeof(WINDOWPLACEMENT)};
        if (!GetWindowPlacement(hwnd_, &original)) return false;
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
        // Fullscreen is a separate state. Clear the native maximize bit so a
        // later SetWindowPlacement actually reapplies the saved maximize state
        // and retains its independent normal restore rectangle.
        const LONG_PTR originalStyle = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        isFullscreen_ = true;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, originalStyle & ~WS_MAXIMIZE);
        const RECT& destination = monitorInfo.rcMonitor;
        if (!SetWindowPos(hwnd_, HWND_TOP, destination.left, destination.top,
            destination.right - destination.left, destination.bottom - destination.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE)) {
            SetWindowLongPtrW(hwnd_, GWL_STYLE, originalStyle);
            isFullscreen_ = false;
            return false;
        }
        savedWindowPlacement_ = original;
    } else {
        // WINDOWPLACEMENT uses workspace coordinates. Keep it paired with
        // SetWindowPlacement, including taskbars on the top/left and native
        // normal/maximized restore metadata, without revealing or activating
        // a hidden/nonforeground window.
        fullscreenRestoreFlags_ = SWP_NOACTIVATE;
        if (!IsWindowVisible(hwnd_)) fullscreenRestoreFlags_ |= SWP_HIDEWINDOW;
        WINDOWPLACEMENT placement = savedWindowPlacement_;
        const bool restoreMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
        // SW_SHOWNORMAL/SW_SHOWMAXIMIZED activate independently of the later
        // WINDOWPOS flags. Restore normal placement with the nonactivating
        // show command, then reapply maximize through nonactivating geometry.
        placement.showCmd = placement.showCmd == SW_SHOWMINIMIZED
            ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
        isFullscreen_ = false;
        const BOOL placementRestored = SetWindowPlacement(hwnd_, &placement);
        BOOL restored = placementRestored;
        if (restored && restoreMaximized) {
            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            restored = GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            if (restored) {
                SetWindowLongPtrW(hwnd_, GWL_STYLE, GetWindowLongPtrW(hwnd_, GWL_STYLE) | WS_MAXIMIZE);
                const RECT& work = monitorInfo.rcWork;
                restored = SetWindowPos(hwnd_, nullptr, work.left, work.top,
                    work.right - work.left, work.bottom - work.top,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
            }
        }
        fullscreenRestoreFlags_ = 0;
        if (!restored) {
            // Once native placement has succeeded, a later maximize failure
            // leaves a windowed HWND and must retain the windowed state.
            if (!placementRestored) isFullscreen_ = true;
            return false;
        }
    }
    // Hidden capture/test windows do not own the pointer's visible policy.
    if (IsWindowVisible(hwnd_)) RefreshCursor();
    Emit(LifecycleChanged{});
    return true;
}

}  // namespace vaporcruise::app
