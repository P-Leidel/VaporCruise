#include "app/frame-loop.h"
namespace vaporcruise::app {
FrameStep FrameLoop::Step(FrameLoopHost& host, FrameMode mode) {
    const bool warmup = mode == FrameMode::Warmup;
    if (!host.DrainMessages()) return FrameStep::Stopped;
    if (!warmup && measurement_.enabled && host.NowSeconds() - measurement_.startSeconds >=
        measurement_.warmupSeconds + measurement_.durationSeconds) return FrameStep::Finished;

    bool suspended = false;
    if (!warmup) {
        suspended = IsSuspended(host.State());
        if (suspended || wasSuspended_) clock_.Reset();
        if (wasSuspended_ && !suspended) dirty_ = true;
        wasSuspended_ = suspended;
    }
    if (resizePending_ && !suspended) {
        resizePending_ = false;
        const auto resized=host.Resize(pendingWidth_, pendingHeight_);
        if (resized.outcome==render::RenderOutcome::Failed) return Fail(host.LastError());
        if (resized.outcome==render::RenderOutcome::Recovered) { dirty_=true; clock_.Reset(); return FrameStep::Continue; }
    }
    if (!warmup) {
        const auto snapshot = scene_.Current();
        if (!NeedsFrame(suspended, paused_, dirty_, snapshot.paletteTransitionActive,
                        hints_.Active())) {
            clock_.Reset(); latencyTimeouts_ = 0;
            return host.WaitIdle() ? FrameStep::Continue : Fail(L"VaporCruise: idle message wait failed.");
        }
    }

    const auto resources = host.Resources();
    if (!resources.handle) return Fail(L"VaporCruise: missing frame latency handle.");
    if (readiness_.MustWait(resources.generation, resources.handle)) {
        switch (host.WaitForLatency(resources.handle)) {
        case LatencyResult::Messages: return FrameStep::Continue;
        case LatencyResult::Timeout:
            clock_.Reset();
            if (++latencyTimeouts_ < 3) return FrameStep::Continue;
            return Fail(L"VaporCruise: frame latency signal timed out repeatedly.");
        case LatencyResult::Failed: return Fail(L"VaporCruise: frame latency wait failed.");
        case LatencyResult::Ready:
            latencyTimeouts_ = 0; readiness_.Acquired(); break;
        }
    }
    // A wake can coincide with input. Dispatch before scene/time sampling;
    // skips retain readiness until a submission or resource replacement.
    if (!host.DrainMessages()) return FrameStep::Stopped;
    if (resizePending_ || (!warmup && IsSuspended(host.State()))) {
        if (!warmup) clock_.Reset();
        return FrameStep::Continue;
    }
    double cpuStart = 0, timestampMs = 0;
    if (!warmup) {
        const double sampleTime = host.NowSeconds();
        if (measurement_.enabled && sampleTime - measurement_.startSeconds >=
            measurement_.warmupSeconds + measurement_.durationSeconds) return FrameStep::Finished;
        if (frameLimit60_) {
            const auto current = scene_.Current();
            if (!NeedsFrame(false, paused_, dirty_, current.paletteTransitionActive,
                            hints_.Active())) {
                clock_.Reset(); latencyTimeouts_ = 0;
                return host.WaitIdle() ? FrameStep::Continue : Fail(L"VaporCruise: idle message wait failed.");
            }
            if (admissionValid_ && sampleTime < lastAdmissionTime_ + 1.0 / 60.0) {
                double deadline = lastAdmissionTime_ + 1.0 / 60.0;
                if (measurement_.enabled) deadline = std::min(deadline,
                    measurement_.startSeconds + measurement_.warmupSeconds + measurement_.durationSeconds);
                // Neither timer nor message wakes consume retained DXGI credit.
                // Dispatch and reevaluate policy/lifecycle/cutoff on the next Step.
                return host.WaitUntil(deadline) == DeadlineResult::Failed
                    ? Fail(L"VaporCruise: frame limit deadline wait failed.") : FrameStep::Continue;
            }
        }
        lastAdmissionTime_ = sampleTime; admissionValid_ = true;
        const double captureElapsed = sampleTime - measurement_.startSeconds - measurement_.warmupSeconds;
        cpuStart = measurement_.enabled && captureElapsed >= 0 ? sampleTime : 0;
        timestampMs = captureElapsed * 1000;
        host.FrameStart(cpuStart,timestampMs);
        const double seconds = clock_.Sample(sampleTime);
        scene_.Advance(seconds, paused_); hints_.Advance(seconds);
    }
    if (warmup) host.FrameStart(0,0);
    const auto snapshot = scene_.PrepareFrame();
    const auto result = host.Draw({snapshot, hints_.Opacity(), host.UiScale()});
    readiness_.Submitted();
    if (result.outcome==render::RenderOutcome::Failed) return Fail(host.LastError());
    const bool recovered = result.outcome==render::RenderOutcome::Recovered;
    if (warmup) {
        ++warmAttempts_;
        if (!recovered) return FrameStep::Warmed;
        if (warmAttempts_ >= 4) return Fail(L"VaporCruise: startup warm draw retry limit reached.");
    } else {
        dirty_ = recovered;
        if (recovered) clock_.Reset();
    }
    return FrameStep::Continue;
}
}
