#include "app/Window.h"
#include "app/frame-loop.h"
#include <wtsapi32.h>
#include <iostream>
#include <array>
#include <string_view>
using namespace vaporcruise::app;
int main() {
    unsigned lifecycle=0,resizes=0,commands=0,limits=0; bool ordering=true,watchPaint=false,paintReset=true; Window window;
    Window::Callbacks callbacks;
    callbacks.onEvent=[&](const WindowEvent& event) {
        if(const auto* changed=std::get_if<LifecycleChanged>(&event)) {
            ++lifecycle;if(watchPaint)paintReset=changed->resetTiming;
        }
        if(const auto* resize=std::get_if<ResizeRequested>(&event)) {
            ++resizes;const auto state=window.State();ordering &= resize->width==state.width && resize->height==state.height;
        }
        if(std::holds_alternative<SceneCommand>(event))++commands;
        if(const auto* limit=std::get_if<FrameLimitRequested>(&event)) {
            ++limits; window.SetFrameLimitStatus(limit->enabled);
        }
    };
    if(!window.Create(GetModuleHandleW(nullptr),callbacks))return 1;
    const auto hwnd=window.Handle();
    // Catch disconnected keyboard/menu control, repeated toggles, and stale UI.
    const auto menu=GetSystemMenu(hwnd,FALSE); UINT capCommand=0;
    for(int position=0; position<GetMenuItemCount(menu); ++position) {
        wchar_t label[128]{}; GetMenuStringW(menu,position,label,128,MF_BYPOSITION);
        if(std::wstring_view(label).find(L"60 FPS")!=std::wstring_view::npos)capCommand=GetMenuItemID(menu,position);
    }
    if(capCommand==0 || capCommand>=0xF000 || (GetMenuState(menu,capCommand,MF_BYCOMMAND)&MF_CHECKED))return 17;
    SendMessageW(hwnd,WM_KEYDOWN,'L',0); SendMessageW(hwnd,WM_KEYDOWN,'L',1ull<<30);
    wchar_t title[128]{};GetWindowTextW(hwnd,title,128);
    if(limits!=1 || !(GetMenuState(menu,capCommand,MF_BYCOMMAND)&MF_CHECKED)
       ||std::wstring_view(title).find(L"60 FPS")==std::wstring_view::npos)return 18;
    SendMessageW(hwnd,WM_SYSCOMMAND,capCommand,0);GetWindowTextW(hwnd,title,128);
    if(limits!=2 || (GetMenuState(menu,capCommand,MF_BYCOMMAND)&MF_CHECKED)
       ||std::wstring_view(title).find(L"Display refresh")==std::wstring_view::npos)return 19;
    window.SetFrameLimitStatus(true,false);
    SendMessageW(hwnd,WM_KEYDOWN,'L',0);SendMessageW(hwnd,WM_SYSCOMMAND,capCommand,0);
    if(limits!=2 || !(GetMenuState(menu,capCommand,MF_BYCOMMAND)&MF_GRAYED)
       ||!(GetMenuState(menu,capCommand,MF_BYCOMMAND)&MF_CHECKED))return 20;
    window.SetFrameLimitStatus(false);
    auto before=lifecycle;
    SendMessageW(hwnd,WM_ENTERSIZEMOVE,0,0);if(!window.State().inSizeMove || lifecycle==before)return 2;
    const auto resizeBefore=resizes;SendMessageW(hwnd,WM_SIZE,SIZE_RESTORED,0);
    if(resizes!=resizeBefore)return 3;
    SendMessageW(hwnd,WM_EXITSIZEMOVE,0,0);if(window.State().inSizeMove || resizes!=resizeBefore+1)return 4;
    SendMessageW(hwnd,WM_SIZE,SIZE_MINIMIZED,0);if(!window.State().minimized)return 5;
    SendMessageW(hwnd,WM_SIZE,SIZE_RESTORED,0);if(window.State().minimized)return 6;
    SendMessageW(hwnd,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);if(!window.State().sessionSuspended)return 7;
    SendMessageW(hwnd,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);if(window.State().sessionSuspended)return 8;
    SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);if(!window.State().powerSuspended)return 9;
    SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);if(window.State().powerSuspended)return 10;
    RECT rect{};GetWindowRect(hwnd,&rect);before=lifecycle;
    SendMessageW(hwnd,WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&rect));if(lifecycle==before)return 11;
    for(int key:std::array<int,3>{'P','H',VK_SPACE}) {SendMessageW(hwnd,WM_KEYDOWN,key,0);SendMessageW(hwnd,WM_KEYDOWN,key,1ull<<30);}
    SendMessageW(hwnd,WM_KEYDOWN,'C',0);
    if(commands!=3 || !ordering)return 12;
    if(!window.SetFullscreen(true) || !window.State().fullscreen || !window.SetFullscreen(false))return 13;
    if(!window.SetFullscreen(true))return 21;
    SendMessageW(hwnd,WM_KEYDOWN,'L',0);
    if(limits!=3 || !(GetMenuState(GetSystemMenu(hwnd,FALSE),capCommand,MF_BYCOMMAND)&MF_CHECKED))return 22;
    if(!window.SetFullscreen(false))return 23;
    before=lifecycle;SendMessageW(hwnd,WM_SHOWWINDOW,TRUE,0);SendMessageW(hwnd,WM_SHOWWINDOW,FALSE,0);if(lifecycle<before+2)return 14;
    watchPaint=true;SendMessageW(hwnd,WM_PAINT,0,0);watchPaint=false;
    bool failed=false;
    if(paintReset){std::cerr<<"FAIL: native paint resets timing\n";failed=true;}
    // Exercise actual visibility without activating this test window.
    SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE);
    window.Show(SW_SHOWNORMAL);
    if(window.State().hidden || !IsWindowVisible(hwnd)){std::cerr<<"FAIL: normal show visibility\n";failed=true;}
    window.Show(SW_HIDE);
    if(!window.State().hidden || IsWindowVisible(hwnd)){std::cerr<<"FAIL: actual hide visibility\n";failed=true;}
    window.Show(SW_SHOWMINNOACTIVE);window.Show(SW_RESTORE);
    if(window.State().hidden || window.State().minimized){std::cerr<<"FAIL: actual restore visibility\n";failed=true;}
    // Native style visibility must be authoritative even without a show notification.
    window.Show(SW_HIDE);
    SetWindowLongPtrW(hwnd,GWL_STYLE,GetWindowLongPtrW(hwnd,GWL_STYLE)|WS_VISIBLE);
    if(window.State().hidden){std::cerr<<"FAIL: visible native style remains hidden\n";failed=true;}
    window.Show(SW_HIDE);
    SendMessageW(hwnd,WM_CLOSE,0,0);MSG msg{};bool quit=false;
    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))if(msg.message==WM_QUIT)quit=true;
    if(!quit)return 15;
    if(failed)return 16;
    std::cout<<"PASS: typed Win32 lifecycle, resize, scene commands, paint, visibility and quit\n";
}
