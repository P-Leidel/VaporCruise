#include "app/Window.h"

#include <windowsx.h>

#include <array>
#include <iostream>
#include <vector>

using vaporcruise::app::Window;

namespace {

// Keep native maximize/restore on a production HWND, but prevent the fixture
// from showing or activating it. All messages and mutations target this HWND.
constexpr wchar_t kHiddenFixtureProperty[] = L"VaporCruiseHiddenWindowFixture";

class HiddenWindowFixture {
public:
    bool Create() {
        if (!window.Create(GetModuleHandleW(nullptr), {})) return false;
        const HWND hwnd = window.Handle();
        originalExStyle_ = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, originalExStyle_ | WS_EX_NOACTIVATE);
        if (!SetPropW(hwnd, kHiddenFixtureProperty, this)) return false;
        originalProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeepHiddenProc)));
        return originalProc_ != nullptr;
    }

    ~HiddenWindowFixture() { RemoveNativeGuards(); }

    void RemoveNativeGuards() {
        const HWND hwnd = window.Handle();
        if (hwnd && IsWindow(hwnd)) {
            if (originalProc_) SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(originalProc_));
            originalProc_ = nullptr;
            RemovePropW(hwnd, kHiddenFixtureProperty);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, originalExStyle_);
        }
    }

    Window window;
    bool traceNative = false;

private:
    static LRESULT CALLBACK KeepHiddenProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* fixture = static_cast<HiddenWindowFixture*>(GetPropW(hwnd, kHiddenFixtureProperty));
        const bool trace = fixture->traceNative;
        if (trace && message == WM_NCCALCSIZE && wParam) {
            const auto& rect = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
            std::cout << "TRACE NCCALCSIZE zoomed=" << IsZoomed(hwnd) << " before=("
                << rect.left << ',' << rect.top << ")-(" << rect.right << ',' << rect.bottom << ")\n";
        }
        if (trace && message == WM_GETMINMAXINFO) {
            const auto& info = *reinterpret_cast<MINMAXINFO*>(lParam);
            std::cout << "TRACE MINMAX zoomed=" << IsZoomed(hwnd) << " before position=" << info.ptMaxPosition.x << ','
                << info.ptMaxPosition.y << " size=" << info.ptMaxSize.x << ',' << info.ptMaxSize.y << '\n';
        }
        if (message == WM_WINDOWPOSCHANGING) {
            auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
            position->flags &= ~SWP_SHOWWINDOW;
            position->flags |= SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOZORDER;
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        const LRESULT result = CallWindowProcW(fixture->originalProc_, hwnd, message, wParam, lParam);
        if (trace && message == WM_NCCALCSIZE && wParam) {
            const auto& rect = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
            std::cout << "TRACE NCCALCSIZE after=(" << rect.left << ',' << rect.top
                << ")-(" << rect.right << ',' << rect.bottom << ")\n";
        }
        if (trace && message == WM_GETMINMAXINFO) {
            const auto& info = *reinterpret_cast<MINMAXINFO*>(lParam);
            std::cout << "TRACE MINMAX after position=" << info.ptMaxPosition.x << ','
                << info.ptMaxPosition.y << " size=" << info.ptMaxSize.x << ',' << info.ptMaxSize.y << '\n';
        }
        return result;
    }

    WNDPROC originalProc_ = nullptr;
    LONG_PTR originalExStyle_ = 0;
};

bool Check(bool condition, const char* reason) {
    if (!condition) std::cerr << "FAIL: " << reason << '\n';
    return condition;
}

bool SameRect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

void PrintRect(const char* label, const RECT& rect) {
    std::cout << label << "=(" << rect.left << ',' << rect.top << ")-("
        << rect.right << ',' << rect.bottom << "); "
        << rect.right - rect.left << 'x' << rect.bottom - rect.top << '\n';
}

bool ClientScreenRect(HWND hwnd, RECT& rect) {
    if (!GetClientRect(hwnd, &rect)) return false;
    POINT origin{};
    if (!ClientToScreen(hwnd, &origin)) return false;
    OffsetRect(&rect, origin.x, origin.y);
    return true;
}

bool CheckClientBounds(HWND hwnd, const RECT& expected, const char* reason) {
    RECT actual{};
    if (!Check(ClientScreenRect(hwnd, actual), "GetClientRect/ClientToScreen succeeds")) return false;
    if (SameRect(actual, expected)) return true;
    PrintRect("Expected client", expected);
    PrintRect("Actual client", actual);
    return Check(false, reason);
}

bool CheckHidden(HWND hwnd) {
    return Check(!IsWindowVisible(hwnd), "native fixture stays hidden") &&
        Check(GetForegroundWindow() != hwnd, "native fixture never becomes foreground");
}

bool Place(HWND hwnd, const RECT& rect) {
    return Check(SetWindowPos(hwnd, nullptr, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED) != FALSE,
        "position owned hidden window") && CheckHidden(hwnd);
}

void Key(HWND hwnd, WPARAM key, bool repeat = false) {
    SendMessageW(hwnd, WM_KEYDOWN, key, repeat ? static_cast<LPARAM>(1ull << 30) : 0);
}

bool CheckHitTests(HWND hwnd, bool fullscreen, bool maximized) {
    RECT rect{};
    if (!Check(ClientScreenRect(hwnd, rect), "read hit-test bounds")) return false;
    const LONG middleX = rect.left + (rect.right - rect.left) / 2;
    const LONG middleY = rect.top + (rect.bottom - rect.top) / 2;
    struct Hit { POINT point; LRESULT normal; };
    const std::array<Hit, 9> hits{{
        {{rect.left + 1, rect.top + 1}, HTTOPLEFT},
        {{middleX, rect.top + 1}, HTTOP},
        {{rect.right - 2, rect.top + 1}, HTTOPRIGHT},
        {{rect.left + 1, middleY}, HTLEFT},
        {{middleX, middleY}, HTCAPTION},
        {{rect.right - 2, middleY}, HTRIGHT},
        {{rect.left + 1, rect.bottom - 2}, HTBOTTOMLEFT},
        {{middleX, rect.bottom - 2}, HTBOTTOM},
        {{rect.right - 2, rect.bottom - 2}, HTBOTTOMRIGHT},
    }};
    for (const auto& hit : hits) {
        const LRESULT expected = fullscreen ? HTCLIENT : (maximized ? HTCAPTION : hit.normal);
        const LRESULT actual = SendMessageW(hwnd, WM_NCHITTEST, 0,
            MAKELPARAM(hit.point.x, hit.point.y));
        if (actual != expected) {
            std::cerr << "Hit at (" << hit.point.x << ',' << hit.point.y << "): expected "
                << expected << ", actual " << actual << '\n';
            return Check(false, "native hit testing follows normal/maximized/fullscreen state");
        }
    }
    return true;
}

bool CheckFullscreenRoundTrip(Window& window, const MONITORINFO& monitor,
    const RECT& before, bool maximized, WPARAM exitKey) {
    const HWND hwnd = window.Handle();
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (!Check(GetWindowPlacement(hwnd, &placement) != FALSE, "save native normal placement")) return false;
    Key(hwnd, 'F');
    if (!Check(window.State().fullscreen, "F enters fullscreen") || !CheckHidden(hwnd) ||
        !CheckClientBounds(hwnd, monitor.rcMonitor, "fullscreen client covers the complete monitor") ||
        !CheckHitTests(hwnd, true, false)) return false;
    Key(hwnd, 'F', true);
    if (!Check(window.State().fullscreen, "held F does not toggle fullscreen")) return false;
    Key(hwnd, exitKey);
    if (!Check(!window.State().fullscreen, "F/Esc exits fullscreen") || !CheckHidden(hwnd) ||
        !Check((IsZoomed(hwnd) != FALSE) == maximized, "fullscreen restores native maximize state") ||
        !CheckClientBounds(hwnd, before, "fullscreen restores previous client bounds") ||
        !CheckHitTests(hwnd, false, maximized)) return false;
    WINDOWPLACEMENT restored{sizeof(WINDOWPLACEMENT)};
    if (!Check(GetWindowPlacement(hwnd, &restored) != FALSE, "read restored native placement")) return false;
    return Check(SameRect(restored.rcNormalPosition, placement.rcNormalPosition),
        "fullscreen preserves native normal restore placement");
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
    return TRUE;
}

bool CheckMonitor(Window& window, HMONITOR monitorHandle) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!Check(GetMonitorInfoW(monitorHandle, &monitor) != FALSE, "read native monitor work area")) return false;
    const HWND hwnd = window.Handle();
    const LONG width = (monitor.rcWork.right - monitor.rcWork.left) < 900 ? 480 : 800;
    const LONG height = (monitor.rcWork.bottom - monitor.rcWork.top) < 700 ? 320 : 600;
    const RECT normal{monitor.rcWork.left + 24, monitor.rcWork.top + 24,
        monitor.rcWork.left + 24 + width, monitor.rcWork.top + 24 + height};
    if (!Place(hwnd, normal) || !CheckClientBounds(hwnd, normal, "normal client remains borderless") ||
        !CheckHitTests(hwnd, false, false)) return false;
    PrintRect("Monitor", monitor.rcMonitor);
    PrintRect("Work area", monitor.rcWork);
    std::cout << "DPI=" << GetDpiForWindow(hwnd) << '\n';

    // SC_MAXIMIZE exercises the production policy used by native caption
    // double-click/maximize. The subclass only suppresses visibility/activation.
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    if (!Check(IsZoomed(hwnd) != FALSE, "native system maximize sets WS_MAXIMIZE") ||
        !CheckHidden(hwnd) ||
        !CheckClientBounds(hwnd, monitor.rcWork, "ordinary maximize fits the monitor work area") ||
        !CheckHitTests(hwnd, false, true)) return false;
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    if (!Check(IsZoomed(hwnd) == FALSE, "native restore clears WS_MAXIMIZE") || !CheckHidden(hwnd) ||
        !CheckClientBounds(hwnd, normal, "native restore returns to the normal bounds")) return false;

    for (const WPARAM exitKey : std::array<WPARAM, 2>{'F', VK_ESCAPE}) {
        if (!CheckFullscreenRoundTrip(window, monitor, normal, false, exitKey)) return false;
    }
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    for (const WPARAM exitKey : std::array<WPARAM, 2>{'F', VK_ESCAPE}) {
        if (!CheckFullscreenRoundTrip(window, monitor, monitor.rcWork, true, exitKey)) return false;
    }
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    if (!Check(IsZoomed(hwnd) == FALSE, "restore after maximized fullscreen clears WS_MAXIMIZE") ||
        !CheckHidden(hwnd) ||
        !CheckClientBounds(hwnd, normal, "restore after maximized fullscreen retains normal bounds")) return false;

    // Exercise the client geometry used for a snapped half-work-area without
    // sending desktop keyboard shortcuts or manipulating any other window.
    RECT snapped = monitor.rcWork;
    snapped.right = snapped.left + (snapped.right - snapped.left) / 2;
    if (!Place(hwnd, snapped) ||
        !CheckClientBounds(hwnd, snapped, "half-work-area placement remains borderless") ||
        !CheckHitTests(hwnd, false, false)) return false;
    return CheckFullscreenRoundTrip(window, monitor, snapped, false, VK_ESCAPE);
}

bool CheckHiddenRestorationWithoutSubclass() {
    // The visibility-suppressing fixture above permits native maximize. This
    // separate normal-window round trip proves production fullscreen restore
    // itself preserves hidden state and foreground ownership without that aid.
    Window window;
    if (!Check(window.Create(GetModuleHandleW(nullptr), {}), "create unmodified hidden production window")) return false;
    const HWND hwnd = window.Handle();
    RECT normal{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!Check(ClientScreenRect(hwnd, normal), "read hidden production client") ||
        !Check(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
            "read hidden production monitor")) return false;
    const HWND foreground = GetForegroundWindow();
    Key(hwnd, 'F');
    if (!Check(window.State().fullscreen, "unmodified hidden window enters fullscreen") ||
        !CheckHidden(hwnd) || !Check(GetForegroundWindow() == foreground, "hidden fullscreen preserves foreground owner") ||
        !CheckClientBounds(hwnd, monitor.rcMonitor, "hidden fullscreen covers full monitor")) return false;
    Key(hwnd, VK_ESCAPE);
    return Check(!window.State().fullscreen, "unmodified hidden window exits fullscreen") &&
        CheckHidden(hwnd) && Check(GetForegroundWindow() == foreground, "hidden restore preserves foreground owner") &&
        CheckClientBounds(hwnd, normal, "hidden restore retains client bounds without subclass");
}

bool CheckMaximizedHiddenRestorationWithoutSubclass() {
    HiddenWindowFixture fixture;
    if (!Check(fixture.Create(), "create hidden native-maximize fixture")) return false;
    const HWND hwnd = fixture.window.Handle();
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!Check(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
        "read native-maximize monitor")) return false;
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    if (!Check(IsZoomed(hwnd) != FALSE, "prepare native-maximized hidden window") || !CheckHidden(hwnd)) return false;
    // Native maximize preparation needs the guard. Remove both its subclass
    // and WS_EX_NOACTIVATE before fullscreen, so it cannot mask activation
    // caused by the production maximized restoration path.
    fixture.RemoveNativeGuards();
    const HWND foreground = GetForegroundWindow();
    for (const WPARAM exitKey : std::array<WPARAM, 2>{'F', VK_ESCAPE}) {
        if (!CheckFullscreenRoundTrip(fixture.window, monitor, monitor.rcWork, true, exitKey) ||
            !Check(GetForegroundWindow() == foreground, "maximized hidden restore preserves foreground without guards")) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char**) {
    if (!Check(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
        "configure production per-monitor DPI awareness")) return 1;
    std::vector<HMONITOR> monitors;
    if (!Check(EnumDisplayMonitors(nullptr, nullptr, &AddMonitor,
        reinterpret_cast<LPARAM>(&monitors)) != FALSE && !monitors.empty(), "enumerate available monitors")) return 1;
    HiddenWindowFixture fixture;
    fixture.traceNative = argc > 1;
    if (!Check(fixture.Create(), "create production owned hidden window")) return 1;
    for (const auto monitor : monitors) {
        if (!CheckMonitor(fixture.window, monitor)) return 1;
    }
    if (!CheckHiddenRestorationWithoutSubclass()) return 1;
    if (!CheckMaximizedHiddenRestorationWithoutSubclass()) return 1;
    std::cout << "PASS: production native maximize/restore, F/Esc placement, hit testing, "
        "and half-work-area geometry on " << monitors.size() << " monitor(s); fixture stayed hidden\n";
    return 0;
}
