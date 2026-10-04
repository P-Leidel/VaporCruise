#include "app/win32-frame-loop.h"
#include <chrono>
#include <limits>
namespace vaporcruise::app {
Win32FrameLoopHost::~Win32FrameLoopHost() {
    if (deadlineTimer_) CloseHandle(deadlineTimer_);
}
bool Win32FrameLoopHost::DrainMessages() {
    MSG message{};
    while (running_ && PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            running_ = false; exitCode_ = static_cast<int>(message.wParam); break;
        }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return running_;
}
double Win32FrameLoopHost::NowSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
LatencyResult Win32FrameLoopHost::WaitForLatency(uintptr_t handle) {
    const HANDLE latency = reinterpret_cast<HANDLE>(handle);
    const DWORD result = MsgWaitForMultipleObjectsEx(1,&latency,1000,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if (result == WAIT_OBJECT_0) return LatencyResult::Ready;
    if (result == WAIT_OBJECT_0+1) return LatencyResult::Messages;
    if (result == WAIT_TIMEOUT) return LatencyResult::Timeout;
    return LatencyResult::Failed;
}
DeadlineResult Win32FrameLoopHost::WaitUntil(double deadlineSeconds) {
    if (!std::isfinite(deadlineSeconds)) return DeadlineResult::Failed;
    const double remaining = deadlineSeconds - NowSeconds();
    if (remaining <= 0) return DeadlineResult::Elapsed;
    if (!deadlineTimer_) {
        constexpr DWORD access = TIMER_MODIFY_STATE | SYNCHRONIZE;
        deadlineTimer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, access);
        // High-resolution timers start at Windows 10 1803; retain 1703 support.
        if (!deadlineTimer_) deadlineTimer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, access);
        if (!deadlineTimer_) return DeadlineResult::Failed;
    }
    const double ticks = std::ceil(remaining * 10000000.0);
    if (!std::isfinite(ticks) || ticks >= static_cast<double>(std::numeric_limits<LONGLONG>::max()))
        return DeadlineResult::Failed;
    LARGE_INTEGER due{};
    due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(ticks));
    if (!SetWaitableTimer(deadlineTimer_, &due, 0, nullptr, nullptr, FALSE)) return DeadlineResult::Failed;
    const DWORD result = MsgWaitForMultipleObjectsEx(1, &deadlineTimer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result == WAIT_OBJECT_0) return DeadlineResult::Elapsed;
    // No timer remains active while processing input or entering idle/suspension.
    if (!CancelWaitableTimer(deadlineTimer_)) return DeadlineResult::Failed;
    return result == WAIT_OBJECT_0 + 1 ? DeadlineResult::Messages : DeadlineResult::Failed;
}
bool Win32FrameLoopHost::WaitIdle() {
    const DWORD result = MsgWaitForMultipleObjectsEx(0,nullptr,250,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    return result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT;
}
}
