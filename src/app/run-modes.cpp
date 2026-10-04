#include "app/run-modes.h"
#include "app/Window.h"
#include "app/win32-frame-loop.h"
#include "render/d3d11-frame-probe.h"
#include "render/measurement-export.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

namespace vaporcruise::app {
int ReportRunFailure(HWND window, int showCommand, const wchar_t* message) {
    fwprintf(stderr, L"%ls\n", message);
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
    if (showCommand != SW_HIDE) MessageBoxW(window, message, L"VaporCruise", MB_OK | MB_ICONERROR);
    return 1;
}
namespace {
double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::uint64_t InteractiveSeed() {
    try {
        std::random_device entropy;
        return (static_cast<std::uint64_t>(entropy()) << 32) ^ entropy();
    } catch (const std::exception&) { return scene::kDefaultTerrainSeed; }
}
// Callback targets outlive Window; the selected probe outlives the entire session.
struct Session {
    render::Renderer renderer;
    scene::SceneState scene;
    FrameLoop loop;
    Window window;
    Win32FrameLoopHost host;
    int diagnosticShow;
    bool frameLimitChangesAllowed = true;

    Session(render::FrameProbe& probe, std::uint64_t seed, int diagnostic)
        : renderer(probe), scene(seed), loop(scene), host(window, renderer, probe), diagnosticShow(diagnostic) {}
    bool Create(HINSTANCE instance) {
        Window::Callbacks callbacks;
        // Dispatch only coalesces work; no D3D or waiting inside WndProc.
        callbacks.onEvent = [&](const WindowEvent& event) {
            if (std::holds_alternative<FrameLimitRequested>(event)) {
                if (!frameLimitChangesAllowed) return;
                loop.OnWindowEvent(event);
                window.SetFrameLimitStatus(loop.FrameLimit60(), frameLimitChangesAllowed);
            } else loop.OnWindowEvent(event);
        };
#ifdef _DEBUG
        callbacks.onSimulateDeviceLoss = [&]() { renderer.SimulateDeviceLoss(); loop.RequestRedraw(); };
#endif
        return window.Create(instance, std::move(callbacks));
    }
    int Fail(const wchar_t* message) { return ReportRunFailure(window.Handle(), diagnosticShow, message); }
    int CreateFailure() {
        return Fail(L"VaporCruise: window/session notification initialization failed. See debugger output for the stage.");
    }
    bool InitializeRenderer() {
        const auto state = window.State();
        return renderer.Initialize(window.Handle(), state.width, state.height);
    }
    FrameStep Warmup() {
        loop.ShowHints();
        FrameStep step;
        do { step = loop.Step(host, FrameMode::Warmup); } while (step == FrameStep::Continue);
        return step;
    }
    FrameStep Live() {
        FrameStep step;
        do { step = loop.Step(host); } while (step == FrameStep::Continue);
        return step;
    }
};
}

int RunInteractive(const RunContext& context, const InteractivePlan&) {
    render::NullFrameProbe probe;
    Session session(probe, InteractiveSeed(), context.showCommand);
    if (!session.Create(context.instance)) return session.CreateFailure();
    if (!session.InitializeRenderer()) return session.Fail(session.renderer.LastError().c_str());
    const auto warmup = session.Warmup();
    if (warmup == FrameStep::Failed) return session.Fail(session.loop.LastError());
    if (warmup == FrameStep::Stopped) return session.host.ExitCode();
    session.window.Show(context.showCommand);
    session.loop.BeginLive();
    const auto step = session.Live();
    if (step == FrameStep::Failed) return session.Fail(session.loop.LastError());
    return step == FrameStep::Stopped ? session.host.ExitCode() : 0;
}

int RunCapture(const RunContext& context, const CapturePlan& plan) {
    render::NullFrameProbe probe;
    Session session(probe, plan.pose.seed, SW_HIDE);
    if (!session.scene.SetPose(plan.pose)) return session.Fail(L"Invalid capture scene pose.");
    if (!session.Create(context.instance)) return session.CreateFailure();
    if (!session.renderer.Initialize(session.window.Handle(), plan.options.width, plan.options.height))
        return session.Fail(session.renderer.LastError().c_str());
    return session.renderer.CaptureFrame({session.scene.PrepareFrame(), plan.options.hint, plan.options.dpi / 96.0f},plan.options.path).outcome==render::RenderOutcome::Drawn ? 0 : session.Fail(session.renderer.LastError().c_str());
}

int RunMeasurement(const RunContext& context, const MeasurementPlan& plan) {
    const auto& options = plan.options;
    render::D3D11FrameProbe probe(static_cast<size_t>(std::min(2000000.0, options.durationSeconds * 300.0)), options.gpuDetail);
    Session session(probe, plan.pose.seed, SW_HIDE);
    session.renderer.SetPresentInterval(options.presentInterval);
    session.frameLimitChangesAllowed = false;
    session.loop.SetFrameLimit60(options.frameLimit == 60);
    session.window.SetFrameLimitStatus(options.frameLimit == 60, false);
    if (!session.scene.SetPose(plan.pose)) return session.Fail(L"Invalid measurement scene pose.");
    if (!session.Create(context.instance)) return session.CreateFailure();
    if (!session.window.SetClientSize(options.width, options.height))
        return session.Fail(L"VaporCruise: requested measurement client size could not be established.");
    if (options.fullscreen && !session.window.SetFullscreen(true))
        return session.Fail(L"VaporCruise: measurement fullscreen could not be established.");
    if (!session.InitializeRenderer()) return session.Fail(session.renderer.LastError().c_str());
    const auto warmup = session.Warmup();
    if (warmup == FrameStep::Failed) return session.Fail(session.loop.LastError());
    if (warmup == FrameStep::Stopped) return session.host.ExitCode();
    session.window.Show(context.showCommand);
    const double start = NowSeconds();
    probe.SnapshotContext(session.window.Handle(), start);
    const double startupMs = (start - context.startupStartSeconds) * 1000;
    session.loop.BeginLive({true, start, options.warmupSeconds, options.durationSeconds});
    const auto step = session.Live();
    if (step == FrameStep::Failed) return session.Fail(session.loop.LastError());
    probe.FinalPoll();
    auto metadata = probe.BuildMetadata(session.renderer.MeasurementContext(), options.warmupSeconds,
        std::clamp(NowSeconds() - start - options.warmupSeconds, 0.0, options.durationSeconds), startupMs);
    metadata.frameLimitFps = options.frameLimit;
    std::wstring error;
    if (!render::WriteMeasurements(options.path, probe.Rows(), metadata, error)) return session.Fail(error.c_str());
    if (options.gpuDetail && !render::WriteGpuDetails(options.path, probe.DetailedRows(), metadata, error)) return session.Fail(error.c_str());
    return step == FrameStep::Stopped ? session.host.ExitCode() : 0;
}
}
