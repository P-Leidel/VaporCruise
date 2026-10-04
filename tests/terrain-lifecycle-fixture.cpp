#include <Windows.h>
#include "render/Renderer.h"
#include "render/d3d11-frame-probe.h"
#include "scene/SceneState.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
using namespace vaporcruise;
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    WNDCLASSW klass{};
    klass.lpfnWndProc = DefWindowProcW; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"VaporCruiseTerrainLifecycleFixture";
    if (!RegisterClassW(&klass)) return 3;
    HWND hwnd = CreateWindowW(klass.lpszClassName, L"Terrain fixture", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 360, nullptr, nullptr, klass.hInstance, nullptr);
    if (!hwnd) return 4;
    int result = 0;
    {
        render::D3D11FrameProbe probe(64);
        render::Renderer renderer(probe);
        scene::SceneState state(scene::kDefaultTerrainSeed);
        state.SetCruisePosition({31, .999});
        state.SetCityLightPhase(.37);
        auto capture = [&](const wchar_t* name) { return (renderer.CaptureFrame({state.PrepareFrame(),1,1},(output / name).wstring()).outcome==render::RenderOutcome::Drawn); };
        bool ok = renderer.Initialize(hwnd, 640, 360);
        probe.SnapshotContext(hwnd,1);
        render::BackgroundResources invalidResources;
        ok = ok && FAILED(invalidResources.Initialize(nullptr, scene::GetBackgroundArtwork())) && invalidResources.GpuBytes() == 0;

        const auto before = renderer.ResourceGeneration();
        ok = ok && capture(L"before.png") && (renderer.Resize(0, 0).outcome!=render::RenderOutcome::Failed) && (renderer.Resize(1280, 720).outcome!=render::RenderOutcome::Failed) && capture(L"resized.png") &&
            (renderer.Resize(640, 360).outcome!=render::RenderOutcome::Failed) && capture(L"restored.png");
        ok = ok && probe.Rows().empty(); // Readback/capture never admits measured attempts.
        auto measuredFrames = [&]() {
            for (unsigned i=0;i<20;++i) {
                if (WaitForSingleObject(renderer.LatencyHandle(),1000)!=WAIT_OBJECT_0) return false;
                const auto start=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                probe.FrameStart(start,static_cast<double>(i));
                if (!(renderer.RenderFrame({state.PrepareFrame(),1,1}).outcome!=render::RenderOutcome::Failed)) return false;
            }
            return true;
        };
        ok = ok && measuredFrames() && probe.Rows().size()==20;
#ifdef _DEBUG
        const auto admitted=probe.Rows().size();
        renderer.SimulateDeviceLoss();
        ok = ok && (renderer.RenderFrame({state.PrepareFrame(),1,1}).outcome!=render::RenderOutcome::Failed) && capture(L"recovered.png");
        ok = ok && probe.Rows().size()==admitted && measuredFrames() && probe.Rows().size()==40;
#endif
        // Readback drains submitted GPU work before the final nonblocking query poll.
        ok = ok && capture(L"measured.png");
        probe.FinalPoll();
        unsigned available=0;
        for (const auto& row : probe.Rows()) {
            if (std::isfinite(row.gpuMs)) ++available;
            ok = ok && row.cpuActiveMs>=0 && row.presentMs>=0 && row.travelCell==31 && row.terrainRowsBuilt<=4;
        }
        std::wstring exportError;
        ok = ok && available>0 && render::WriteMeasurements(output / "measurement.csv",probe.Rows(),
            probe.BuildMetadata(renderer.MeasurementContext(),0,1,0),exportError);
        std::ofstream metadata(output / "lifecycle.json");
        metadata << "{\"before_generation\":" << before << ",\"after_generation\":" << renderer.ResourceGeneration()
#ifdef _DEBUG
            << ",\"debug_recovery\":true"
#else
            << ",\"debug_recovery\":false"
#endif
            << "}\n";
        if (!ok || !metadata) { std::wcerr << renderer.LastError() << '\n'; result = 1; }
    }
    DestroyWindow(hwnd); UnregisterClassW(klass.lpszClassName, klass.hInstance);
    return result;
}
