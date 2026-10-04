#include "app/win32-frame-loop.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace vaporcruise;
using namespace vaporcruise::app;
namespace {
unsigned failures=0;
void Check(bool ok,const char* message) {
    if(!ok) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
}
double ThreadCpuSeconds() {
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user))return -1;
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return static_cast<double>(k.QuadPart+u.QuadPart)*1e-7;
}
}
int main() {
    // No window or D3D device is created: exercise the real native host wait.
    Window window; render::NullFrameProbe probe; render::Renderer renderer(probe);
    Win32FrameLoopHost host(window,renderer,probe);
    MSG message{}; PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
    const DWORD owner=GetCurrentThreadId();
    const double messageStart=host.NowSeconds();
    bool posted=false;
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        posted=PostThreadMessageW(owner,WM_APP+60,0,0)!=FALSE;
    });
    const auto messageResult=host.WaitUntil(messageStart+.5);
    sender.join();
    Check(posted && messageResult==DeadlineResult::Messages && host.NowSeconds()-messageStart<.4,
          "future deadline wakes promptly for owned-thread input");
    Check(host.DrainMessages(),"deadline wake leaves input for normal dispatch");
    const double cpuStart=ThreadCpuSeconds();
    const double timerStart=host.NowSeconds(),deadline=timerStart+.12;
    const auto timerResult=host.WaitUntil(deadline);
    const double timerEnd=host.NowSeconds(),cpuEnd=ThreadCpuSeconds();
    Check(timerResult==DeadlineResult::Elapsed && timerEnd>=deadline-.001,
          "rearmed one-shot deadline blocks until due after interrupted wait");
    Check(cpuStart>=0 && cpuEnd-cpuStart<.05,"deadline wait consumes negligible active thread CPU");
    Check(host.WaitUntil(host.NowSeconds()-1)==DeadlineResult::Elapsed,
          "already-due deadline returns without waiting");
    DWORD handlesBefore=0,handlesAfter=0;
    Check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"read initial process handle count");
    for(unsigned i=0;i<4;++i) {
        Win32FrameLoopHost temporary(window,renderer,probe);
        Check(temporary.WaitUntil(temporary.NowSeconds()+.001)==DeadlineResult::Elapsed,
              "temporary native wait completes");
    }
    Check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE && handlesAfter==handlesBefore,
          "native host releases each lazily allocated timer handle");
    if(!failures)std::cout<<"PASS: native message-responsive one-shot deadline wait without D3D initialization\n";
    return failures?1:0;
}
