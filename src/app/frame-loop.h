#pragma once

#include <algorithm>
#include <cmath>

#include "app/window-state.h"
#include "app/window-events.h"
#include "scene/SceneState.h"
#include "render/render-input.h"
#include "render/render-result.h"

namespace vaporcruise::app {

inline bool IsSuspended(const WindowState& state) {
    return state.hidden || state.minimized || state.inSizeMove || state.cloaked ||
           state.sessionSuspended || state.powerSuspended || state.width == 0 || state.height == 0;
}

// Pure scheduling policy; focus is deliberately absent because visible windows
// retain their normal cadence even when another application has focus.
inline bool NeedsFrame(bool suspended, bool cruisePaused, bool dirty,
                       bool paletteActive, bool hintActive) {
    return !suspended && (!cruisePaused || dirty || paletteActive || hintActive);
}

// A DXGI wait consumes a readiness signal. Skipping a frame must retain that
// credit until a submission attempt or a different resource/handle identity.
class FrameReadiness {
public:
    bool MustWait(uint64_t generation, uintptr_t handle) {
        if (generation != generation_ || handle != handle_) {
            generation_ = generation;
            handle_ = handle;
            ready_ = false;
        }
        return !ready_;
    }
    void Acquired() { ready_ = true; }
    void Submitted() { ready_ = false; }
private:
    uint64_t generation_ = 0;
    uintptr_t handle_ = 0;
    bool ready_ = false;
};

class FrameClock {
public:
    void Reset() { valid_ = false; }
    double Sample(double now) {
        if (!std::isfinite(now)) { Reset(); return 0.0; }
        const double delta = valid_ && now >= last_ ? now - last_ : 0.0;
        last_ = now;
        valid_ = true;
        return delta;
    }
private:
    double last_ = 0.0;
    bool valid_ = false;
};

class HintTimeline {
public:
    void Show() { elapsed_ = 0.0; }
    void Advance(double seconds) {
        if (std::isfinite(seconds) && seconds > 0.0) {
            elapsed_ = std::min(5.0, elapsed_ + seconds);
        }
    }
    bool Active() const { return elapsed_ < 5.0; }
    float Opacity() const {
        return static_cast<float>(std::clamp((5.0 - elapsed_) / 0.5, 0.0, 1.0));
    }
private:
    double elapsed_ = 5.0;
};

struct FrameResources { uint64_t generation; uintptr_t handle; };
enum class LatencyResult { Ready, Messages, Timeout, Failed };
enum class DeadlineResult { Elapsed, Messages, Failed };
enum class FrameMode { Warmup, Live };
enum class FrameStep { Continue, Warmed, Finished, Stopped, Failed };
struct MeasurementSession {
    bool enabled = false;
    double startSeconds = 0, warmupSeconds = 0, durationSeconds = 0;
};

// Native operations only. FrameLoop owns their order and all scheduling state.
class FrameLoopHost {
public:
    virtual ~FrameLoopHost() = default;
    virtual bool DrainMessages() = 0;
    virtual WindowState State() const = 0;
    virtual double NowSeconds() const = 0;
    virtual float UiScale() const = 0;
    virtual FrameResources Resources() const = 0;
    virtual LatencyResult WaitForLatency(uintptr_t handle) = 0;
    virtual DeadlineResult WaitUntil(double deadlineSeconds) = 0;
    virtual bool WaitIdle() = 0;
    virtual render::RenderResult Resize(uint32_t width, uint32_t height) = 0;
    virtual void FrameStart(double cpuStartSeconds, double timestampMs) = 0;
    virtual render::RenderResult Draw(const render::RenderInput& input) = 0;
    virtual const wchar_t* LastError() const = 0;
};

class FrameLoop {
public:
    explicit FrameLoop(scene::SceneState& scene) : scene_(scene) {}
    FrameStep Step(FrameLoopHost& host, FrameMode mode = FrameMode::Live);
    void BeginLive(MeasurementSession measurement = {}) {
        measurement_ = measurement; clock_.Reset(); wasSuspended_ = false; admissionValid_ = false;
    }
    void OnWindowEvent(const WindowEvent& event) {
        if(const auto* lifecycle=std::get_if<LifecycleChanged>(&event)) { if(lifecycle->resetTiming) ResetTiming(); RequestRedraw(); }
        else if(const auto* size=std::get_if<ResizeRequested>(&event)) RequestResize(size->width,size->height);
        else if(const auto* limit=std::get_if<FrameLimitRequested>(&event)) SetFrameLimit60(limit->enabled);
        else switch(std::get<SceneCommand>(event)) {
        case SceneCommand::NextPalette: NextPalette(); break;
        case SceneCommand::TogglePause: TogglePause(); break;
        case SceneCommand::ShowHints: ShowHints(); break;
        }
    }
    void RequestResize(uint32_t width, uint32_t height) {
        pendingWidth_ = width; pendingHeight_ = height; resizePending_ = true; dirty_ = true;
    }
    void RequestRedraw() { dirty_ = true; }
    void ResetTiming() { clock_.Reset(); }
    void TogglePause() { paused_ = !paused_; ResetTiming(); RequestRedraw(); }
    void NextPalette() { scene_.NextPalette(); RequestRedraw(); }
    void ShowHints() { hints_.Show(); RequestRedraw(); }
    void SetFrameLimit60(bool enabled) {
        if (frameLimit60_ == enabled) return;
        frameLimit60_ = enabled; ShowHints();
    }
    bool FrameLimit60() const { return frameLimit60_; }
    const wchar_t* LastError() const { return error_; }
private:
    FrameStep Fail(const wchar_t* message) { error_ = message; return FrameStep::Failed; }
    scene::SceneState& scene_;
    FrameClock clock_;
    FrameReadiness readiness_;
    HintTimeline hints_;
    MeasurementSession measurement_;
    // Animation resets must not let repeated input bypass the admission cap.
    double lastAdmissionTime_ = 0;
    bool frameLimit60_ = false, admissionValid_ = false;
    bool paused_ = false, dirty_ = true, resizePending_ = false, wasSuspended_ = false;
    uint32_t pendingWidth_ = 0, pendingHeight_ = 0;
    unsigned latencyTimeouts_ = 0, warmAttempts_ = 0;
    const wchar_t* error_ = L"";
};

}  // namespace vaporcruise::app
