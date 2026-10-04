#include "shader-binding-state-tests.h"
#include "render/Renderer.h"
#include "scene/scene-pose.h"
#include <filesystem>
#include <iostream>
using namespace vaporcruise;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    if(!CheckExplicitShaderBindings()) {std::cerr<<"FAIL: explicit pass binding state\n";return 5;}
    std::filesystem::path output(argv[1]);std::filesystem::create_directories(output);
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"VaporCruiseBindings";
    if(!RegisterClassW(&cls))return 3;
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"Bindings",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,cls.hInstance,nullptr);
    if(!hwnd)return 4;
    bool ok=true;
    {
        render::Renderer renderer;scene::SceneState state;
        ok=renderer.Initialize(hwnd,1280,720);
        unsigned index=0;
        for(auto size:{std::array<unsigned,2>{1280,720},{480,320},{720,1280},{2560,1080}})
            for(unsigned palette=0;palette<3;++palette)for(double blend:{0.,.5}) {
                scene::ScenePose pose;pose.palette=palette;pose.paletteProgress=blend;pose.cruise={36028797018963971LL,.25};
                ok=ok && state.SetPose(pose) && (renderer.Resize(size[0],size[1]).outcome!=render::RenderOutcome::Failed);
                renderer.ClearBindingsBeforePassesForTesting(false);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-control.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-control2.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-control3.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-control4.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-control5.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                renderer.ClearBindingsBeforePassesForTesting(true);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-cleared.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-cleared2.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-cleared3.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-cleared4.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/(std::to_wstring(index)+L"-cleared5.png")).wstring()).outcome==render::RenderOutcome::Drawn);
                ++index;if(!ok)break;
            }
        // Device recreation must produce complete inputs as well.
        ok=ok && renderer.Initialize(hwnd,1280,720);

        renderer.ClearBindingsBeforePassesForTesting(false);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-control.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-control2.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-control3.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-control4.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-control5.png").wstring()).outcome==render::RenderOutcome::Drawn);
        renderer.ClearBindingsBeforePassesForTesting(true);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-cleared.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-cleared2.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-cleared3.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-cleared4.png").wstring()).outcome==render::RenderOutcome::Drawn);
        ok=ok && (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"recreated-cleared5.png").wstring()).outcome==render::RenderOutcome::Drawn);
        if(!ok)std::wcerr<<renderer.LastError()<<L'\n';
    }
    DestroyWindow(hwnd);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return ok?0:1;
}
