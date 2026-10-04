#pragma once
#include "app/frame-loop.h"
#include "app/Window.h"
#include "render/Renderer.h"
namespace vaporcruise::app {
class Win32FrameLoopHost final : public FrameLoopHost {
public:
    Win32FrameLoopHost(Window& window, render::Renderer& renderer, render::FrameProbe& probe = render::DefaultFrameProbe())
        : window_(window), renderer_(renderer), probe_(probe) {}
    ~Win32FrameLoopHost() override;
    Win32FrameLoopHost(const Win32FrameLoopHost&) = delete;
    Win32FrameLoopHost& operator=(const Win32FrameLoopHost&) = delete;
    bool DrainMessages() override;
    WindowState State() const override { return window_.State(); }
    double NowSeconds() const override;
    float UiScale() const override { return static_cast<float>(GetDpiForWindow(window_.Handle())) / 96.0f; }
    FrameResources Resources() const override {
        return {renderer_.ResourceGeneration(), reinterpret_cast<uintptr_t>(renderer_.LatencyHandle())};
    }
    LatencyResult WaitForLatency(uintptr_t handle) override;
    DeadlineResult WaitUntil(double deadlineSeconds) override;
    bool WaitIdle() override;
    render::RenderResult Resize(uint32_t width, uint32_t height) override { return renderer_.Resize(width,height); }
    void FrameStart(double start, double timestamp) override { probe_.FrameStart(start,timestamp); }
    render::RenderResult Draw(const render::RenderInput& input) override {
        return renderer_.RenderFrame(input);
    }
    const wchar_t* LastError() const override { return renderer_.LastError().c_str(); }
    int ExitCode() const { return exitCode_; }
private:
    Window& window_;
    render::Renderer& renderer_;
    render::FrameProbe& probe_;
    HANDLE deadlineTimer_ = nullptr;
    int exitCode_ = 0;
    bool running_ = true;
};
}
