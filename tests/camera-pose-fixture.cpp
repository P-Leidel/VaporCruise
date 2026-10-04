#include <Windows.h>
#include "render/Renderer.h"
#include "render/d3d11-frame-probe.h"
#include "scene/SceneState.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Batch the same public scene poses through the baseline and candidate renderers.
// Five raw captures per pose are retained for the strict median reference.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path output(argv[1]);
    if (std::filesystem::exists(output)) return 3;
    std::filesystem::create_directories(output);
    WNDCLASSW klass{};
    klass.lpfnWndProc = DefWindowProcW; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"VaporCruiseCameraPoseFixture";
    if (!RegisterClassW(&klass)) return 4;
    HWND hwnd = CreateWindowW(klass.lpszClassName, L"Camera pose fixture", WS_OVERLAPPEDWINDOW,
        0, 0, 480, 320, nullptr, nullptr, klass.hInstance, nullptr);
    if (!hwnd) return 5;
    int result = 0;
    {
        using namespace vaporcruise;
        render::D3D11FrameProbe probe(1);
        render::Renderer renderer(probe);
        scene::SceneState state;
        bool ok = renderer.Initialize(hwnd, 480, 320);
        std::ofstream manifest(output / "poses.json");
        manifest << "{\"width\":480,\"height\":320,\"captures_per_pose\":5,\"poses\":[";
        const double progress[] = {0, .25, .5, 1};
        unsigned count = 0;
        for (unsigned p=0; p<3 && ok; ++p)
            for (unsigned pp=0; pp<4 && ok; ++pp) {
                scene::ScenePose pose;
                pose.seed=6215337262362608466ULL; pose.cruise={36028797018963971LL,.25};
                pose.cityLightPhase=.25;
                pose.palette=p; pose.paletteProgress=progress[pp];
                ok = state.SetPose(pose);

                const auto name = "p"+std::to_string(p)+"-pp"+std::to_string(pp);
                if (count++) manifest << ',';
                manifest << "{\"name\":\"" << name << "\",\"palette\":" << p
                    << ",\"palette_progress\":" << progress[pp]
                    << ",\"seed\":\"6215337262362608466\",\"cell\":\"36028797018963971\",\"phase\":0.25,\"city_light_phase\":0.25}";
                for (unsigned frame=0; frame<5 && ok; ++frame)
                    ok = (renderer.CaptureFrame({state.PrepareFrame(),0,1},(output / (name+"-"+std::to_string(frame)+".png")).wstring()).outcome==render::RenderOutcome::Drawn);
            }
        manifest << "],\"completion\":" << (ok && count==12 ? "true" : "false") << "}\n";
        if (!ok || !manifest || count!=12) { std::wcerr << renderer.LastError() << '\n'; result=1; }
    }
    DestroyWindow(hwnd); UnregisterClassW(klass.lpszClassName,klass.hInstance);
    return result;
}
