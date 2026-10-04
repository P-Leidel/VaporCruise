#include "app/frame-loop.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace vaporcruise;
using namespace vaporcruise::app;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct FakeHost : FrameLoopHost {
    WindowState state{};
    FrameResources resources{1,100};
    double now = 10;
    unsigned draws = 0, waits = 0, idles = 0, resizes = 0, drains = 0, deadlines = 0;
    bool running = true, resizeOk = true, drawOk = true, idleOk = true;
    render::RenderOutcome drawOutcome=render::RenderOutcome::Drawn, resizeOutcome=render::RenderOutcome::Drawn;
    LatencyResult waitResult = LatencyResult::Ready;
    DeadlineResult deadlineResult = DeadlineResult::Elapsed;
    double lastDeadline = 0;
    std::function<void(double)> onDeadline;
    std::vector<double> admissionTimes;
    std::function<void()> onWait, onDraw;
    std::function<void(unsigned)> onDrain;
    scene::SceneParams last;
    double cpuStart = 0, timestamp = 0;
    unsigned starts = 0;
    std::function<void()> onStart;
    std::vector<std::string> trace;
    FakeHost() { state.width = 640; state.height = 360; }
    bool DrainMessages() override {
        trace.push_back("drain"); ++drains;
        if (onDrain) onDrain(drains);
        return running;
    }
    WindowState State() const override { return state; }
    double NowSeconds() const override { return now; }
    float UiScale() const override { return 1.5f; }
    FrameResources Resources() const override { return resources; }
    LatencyResult WaitForLatency(uintptr_t handle) override {
        Check(handle == resources.handle, "wait uses current latency handle");
        ++waits; trace.push_back("wait");
        if (onWait) onWait();
        return waitResult;
    }
    bool WaitIdle() override { ++idles; trace.push_back("idle"); return idleOk; }
    DeadlineResult WaitUntil(double deadline) override {
        ++deadlines; lastDeadline = deadline; trace.push_back("deadline");
        Check(deadline > now, "cap arms only future deadlines");
        if (onDeadline) onDeadline(deadline);
        else if (deadlineResult == DeadlineResult::Elapsed) now = deadline;
        return deadlineResult;
    }
    render::RenderResult Resize(uint32_t width, uint32_t height) override {
        ++resizes; trace.push_back("resize"); state.width = width; state.height = height;
        return resizeOk?render::RenderResult{resizeOutcome,L"",S_OK}:render::RenderResult::Failure(L"resize",E_FAIL);
    }
    void FrameStart(double start, double ms) override {
        ++starts; trace.push_back("start"); cpuStart=start; timestamp=ms; admissionTimes.push_back(now);
        if (onStart) onStart();
    }
    render::RenderResult Draw(const render::RenderInput& input) override {
        Check(starts == draws+1, "frame start precedes prepared draw");
        Check(input.frame.terrainSlots.size()==11, "draw consumes prepared terrain");
        for(const auto& slot:input.frame.terrainSlots) if(slot.active) Check(slot.complete,"draw active slots complete");
        const auto& params=input.frame.scene; const auto scale=input.uiScale;
        Check(scale == 1.5f, "fresh UI scale reaches draw");
        ++draws; trace.push_back("draw"); last = params;
        if (onDraw) onDraw();
        return drawOk?render::RenderResult{drawOutcome,L"",S_OK}:render::RenderResult::Failure(L"draw",E_FAIL);
    }
    const wchar_t* LastError() const override { return L"fake stage failure"; }
};
void PauseAndFeedback() {
    scene::SceneState scene;
    FrameLoop loop(scene); FakeHost host;
    loop.BeginLive(); loop.TogglePause();
    Check(loop.Step(host) == FrameStep::Continue && host.draws == 1, "pause requests one draw");
    host.now += 10000;
    loop.Step(host);
    Check(host.draws == 1 && host.idles == 1, "stable pause idles without submission");
    const auto before = scene.Current();
    loop.NextPalette(); loop.ShowHints();
    for (unsigned i=0; i<70; ++i) { host.now += .1; loop.Step(host); }
    Check(!scene.Current().paletteTransitionActive,
          "paused transitions finish through production loop");
    Check(scene.Current().cruise.cell == before.cruise.cell && scene.Current().gridPhase == before.gridPhase &&
          scene.Current().cityLightPhase == before.cityLightPhase, "paused feedback freezes cruise and city");
    const auto draws = host.draws; loop.Step(host);
    Check(host.draws == draws, "settled hints and transitions return to idle");
    loop.TogglePause(); loop.Step(host);
    Check(scene.Current().gridPhase == before.gridPhase, "resume discards stopped time");
    host.now += .1; loop.Step(host);
    Check(scene.Current().gridPhase > before.gridPhase, "cruise advances after resume baseline");
}
void PostWaitResize() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    host.onDrain = [&](unsigned count) { if (count == 2) { loop.RequestResize(800,600); loop.RequestResize(900,700); } };
    Check(loop.Step(host) == FrameStep::Continue && host.draws == 0 && host.waits == 1,
          "post-wait resize skips submission");
    host.onDrain = {};
    loop.Step(host);
    Check(host.resizes == 1 && host.state.width == 900 && host.state.height == 700,
          "pending resize coalesces to latest dimensions");
    Check(host.waits == 1 && host.draws == 1, "ordinary resize retains consumed readiness");
    Check(host.trace == std::vector<std::string>{"drain","wait","drain","drain","resize","drain","start","draw"},
          "production ordering drains input before resize and draw");
}
void SuspendAndFreshInput() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    host.onDrain = [&](unsigned count) { if (count == 2) host.state.hidden = true; };
    loop.Step(host); Check(host.draws == 0, "post-wait suspension skips draw");
    host.onDrain = {}; loop.Step(host); Check(host.idles == 1, "hidden window idles");
    host.now += 10000; host.state.hidden = false; loop.Step(host);
    Check(host.waits == 1 && host.draws == 1 && scene.Current().gridPhase == 0,
          "resume retains readiness and discards suspended delta");
    host.onWait = [&]() { host.now += .2; loop.NextPalette(); };
    loop.Step(host);
    Check(host.last.paletteTransitionActive && host.last.gridPhase > 0,
          "post-pacing scene snapshot includes input and fresh elapsed time");
    for (auto member : {&WindowState::minimized,&WindowState::inSizeMove,&WindowState::cloaked,
                        &WindowState::sessionSuspended,&WindowState::powerSuspended}) {
        host.state.*member = true; const auto draws = host.draws; loop.Step(host);
        Check(host.draws == draws, "each suspension reason suppresses draws");
        host.state.*member = false; host.now += 10000; loop.Step(host);
    }
}
void RecoveryAndWarmup() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    host.state.hidden = true;
    host.onDraw = [&]() { ++host.resources.generation; host.drawOutcome=render::RenderOutcome::Recovered; };
    Check(loop.Step(host, FrameMode::Warmup) == FrameStep::Continue, "warm recovery is not warm completion");
    host.onDraw = {}; host.drawOutcome=render::RenderOutcome::Drawn;
    Check(loop.Step(host, FrameMode::Warmup) == FrameStep::Warmed && host.waits == 2,
          "warm-up acquires replacement readiness and draws while hidden");
    Check(scene.Current().gridPhase == 0, "warm-up does not advance scene");
    host.state.hidden = false; loop.BeginLive(); loop.TogglePause();
    host.onDraw = [&]() { ++host.resources.generation; host.drawOutcome=render::RenderOutcome::Recovered; host.resources.handle = 200; };
    loop.Step(host); const auto waits = host.waits;
    host.onDraw = {}; host.drawOutcome=render::RenderOutcome::Drawn; host.now += 10000; loop.Step(host);
    Check(host.waits == waits+1 && host.last.gridPhase == 0, "paused recovery retries replacement without advancing time");
    const auto draws = host.draws; loop.Step(host);
    Check(host.draws == draws, "successful recovered pause returns to idle");
    scene::SceneState s2; FrameLoop retries(s2); FakeHost failing;
    failing.onDraw = [&]() { ++failing.resources.generation; failing.drawOutcome=render::RenderOutcome::Recovered; };
    for (unsigned i=0;i<3;++i) Check(retries.Step(failing,FrameMode::Warmup) == FrameStep::Continue,"bounded warm retry");
    Check(retries.Step(failing,FrameMode::Warmup) == FrameStep::Failed, "fourth unsuccessful warm draw fails");
}
void FailuresAndCutoff() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    host.waitResult = LatencyResult::Timeout;
    Check(loop.Step(host) == FrameStep::Continue && loop.Step(host) == FrameStep::Continue &&
          loop.Step(host) == FrameStep::Failed, "three consecutive latency timeouts fail");
    for (unsigned failure=0; failure<5; ++failure) {
        scene::SceneState s; FrameLoop l(s); FakeHost h; l.BeginLive();
        if (failure == 0) h.resources.handle = 0;
        if (failure == 1) h.waitResult = LatencyResult::Failed;
        if (failure == 2) { l.RequestResize(800,600); h.resizeOk = false; }
        if (failure == 3) h.drawOk = false;
        if (failure == 4) { h.state.hidden = true; h.idleOk = false; }
        Check(l.Step(h) == FrameStep::Failed && l.LastError()[0] != 0, "fatal host/wait stage reported");
    }
    scene::SceneState measured; FrameLoop timed(measured); FakeHost capture;
    timed.BeginLive({true,10,1,2});
    capture.now = 10.5; timed.Step(capture);
    Check(capture.cpuStart == 0, "warm-up excluded from CPU measurement");
    capture.now = 11.5; timed.Step(capture);
    Check(capture.cpuStart == 11.5 && capture.timestamp == 500, "measurement timestamps preserved");
    capture.now = 13; const auto count = capture.draws;
    Check(timed.Step(capture) == FrameStep::Finished && capture.draws == count, "measurement cutoff precedes waits and draw");
    scene::SceneState stopped; FrameLoop quit(stopped); FakeHost closed;
    closed.onDrain = [&](unsigned n) { if (n == 2) closed.running = false; };
    Check(quit.Step(closed) == FrameStep::Stopped && closed.draws == 0, "post-wait quit prevents draw");
}
void ClockAndHints() {
    FrameClock clock;
    Check(clock.Sample(10) == 0 && clock.Sample(11) == 1, "clock advances normally");
    Check(clock.Sample(std::numeric_limits<double>::quiet_NaN()) == 0 && clock.Sample(100) == 0,
          "nonfinite time resets baseline");
    Check(clock.Sample(99) == 0, "backward clock has no negative delta");
    HintTimeline hints; hints.Show(); hints.Advance(4.75);
    Check(std::abs(hints.Opacity()-.5f) < 1e-6f, "hint fade preserved");
    hints.Advance(.25); Check(!hints.Active(), "hint timeline finite");
}
void WaitMessagesAndIdentity() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    host.waitResult = LatencyResult::Messages;
    Check(loop.Step(host) == FrameStep::Continue && host.draws == 0 && host.drains == 1,
          "message wake returns to dispatch without acquiring readiness");
    host.waitResult = LatencyResult::Timeout;
    loop.Step(host); loop.Step(host);
    host.waitResult = LatencyResult::Ready;
    Check(loop.Step(host) == FrameStep::Continue && host.draws == 1, "successful wait resets timeout counter");
    host.waitResult = LatencyResult::Timeout;
    Check(loop.Step(host) == FrameStep::Continue && loop.Step(host) == FrameStep::Continue,
          "new timeout sequence gets full retry budget");
    host.waitResult = LatencyResult::Ready;
    // Install a callback for only the post-wait dispatch.
    const auto postWait = host.drains + 2;
    host.onDrain = [&](unsigned n) { if (n == postWait) loop.RequestResize(800,600); };
    loop.Step(host); host.onDrain = {};
    const auto waits = host.waits;
    host.resources.handle = 200; // A changed handle alone invalidates retained readiness.
    loop.Step(host);
    Check(host.waits == waits+1, "handle replacement without generation change reacquires readiness");
    const auto draws = host.draws; host.state.width = 0;
    loop.Step(host); Check(host.draws == draws, "zero drawable size suspends submissions");
}
void WarmupInputAndQuit() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    host.state.hidden = true;
    host.onDrain = [&](unsigned n) { if (n == 2) loop.RequestResize(800,600); };
    Check(loop.Step(host,FrameMode::Warmup) == FrameStep::Continue && host.draws == 0,
          "warm-up post-wait resize skips draw");
    host.onDrain = {};
    Check(loop.Step(host,FrameMode::Warmup) == FrameStep::Warmed && host.waits == 1 && host.resizes == 1,
          "warm-up resize retains readiness");
    scene::SceneState s2; FrameLoop quit(s2); FakeHost h2;
    h2.onDrain = [&](unsigned n) { if (n == 2) h2.running = false; };
    Check(quit.Step(h2,FrameMode::Warmup) == FrameStep::Stopped && h2.draws == 0,
          "warm-up post-wait quit prevents submission");
    scene::SceneState s3; FrameLoop early(s3); FakeHost h3; h3.running = false;
    Check(early.Step(h3) == FrameStep::Stopped && h3.waits == 0, "initial quit precedes waits");
}
void MeasurementStartOrder() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.BeginLive({true,10,1,2});
    host.onStart=[&] { Check(scene.Current().gridPhase==0,"frame-start sees scene before advancement"); };
    host.now=10.5; loop.Step(host);
    Check(host.starts==1 && host.cpuStart==0 && host.timestamp==-500,"negative elapsed excludes warmup at existing sample point");
    host.now=11; loop.Step(host);
    Check(host.starts==2 && host.cpuStart==11 && host.timestamp==0 && host.last.gridPhase>0,"cutoff start hook precedes scene work");
    host.onStart={};
    host.state.hidden=true; loop.Step(host);
    Check(host.starts==2,"suspension has no start event");
    host.state.hidden=false; host.now=13;
    Check(loop.Step(host)==FrameStep::Finished && host.starts==2,"deadline has no start event");
    scene::SceneState warmScene; FrameLoop warm(warmScene); FakeHost warmHost;
    warm.Step(warmHost,FrameMode::Warmup);
    Check(warmHost.starts==1 && warmHost.cpuStart==0 && warmHost.timestamp==0,"startup warm draw clears measurement eligibility");
}
void MeasurementDeadlineAfterWait() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.BeginLive({true,10,1,2});
    host.now=12.9;
    host.onWait=[&] { host.now=13.5; };
    Check(loop.Step(host)==FrameStep::Finished && host.waits==1 && host.draws==0 && host.starts==0,
          "deadline crossed during latency wait prevents frame start and submission");
    Check(scene.Current().cruise.cell==0 && scene.Current().gridPhase==0,
          "deadline crossed during latency wait preserves scene motion");

    scene::SceneState warmScene; FrameLoop warm(warmScene); FakeHost warmHost;
    warm.BeginLive({true,10,1,2});
    warmHost.now=12.9; warmHost.onWait=[&] { warmHost.now=13.5; };
    Check(warm.Step(warmHost,FrameMode::Warmup)==FrameStep::Warmed && warmHost.draws==1,
          "measurement deadline does not suppress startup warm draw");
}
}
void ExplicitOutcomes() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.BeginLive(); loop.TogglePause(); host.drawOutcome=render::RenderOutcome::Recovered;
    loop.Step(host); host.drawOutcome=render::RenderOutcome::Drawn; host.now+=100;
    loop.Step(host); Check(host.draws==2,"recovered paused frame stays dirty without generation change");
    scene::SceneState s2; FrameLoop warm(s2); FakeHost h2;
    h2.onDraw=[&]{++h2.resources.generation;};
    Check(warm.Step(h2,FrameMode::Warmup)==FrameStep::Warmed,"drawn with changed generation is still drawn");
    scene::SceneState s3; FrameLoop limit(s3); FakeHost h3; h3.drawOutcome=render::RenderOutcome::Recovered;
    for(int i=0;i<3;++i) Check(limit.Step(h3,FrameMode::Warmup)==FrameStep::Continue,"warm recovery retries");
    Check(limit.Step(h3,FrameMode::Warmup)==FrameStep::Failed,"four warm attempts bounded");
    scene::SceneState s4; FrameLoop resize(s4); FakeHost h4; resize.BeginLive();
    resize.RequestResize(800,600); h4.resizeOutcome=render::RenderOutcome::Recovered;
    Check(resize.Step(h4)==FrameStep::Continue && h4.draws==0,"recovered resize defers draw");
    h4.resizeOutcome=render::RenderOutcome::Drawn; resize.Step(h4);
    Check(h4.draws==1,"recovered resize retains dirty frame");
}
void TypedEvents() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    loop.OnWindowEvent(SceneCommand::TogglePause);loop.Step(host);
    loop.OnWindowEvent(ResizeRequested{800,600});loop.OnWindowEvent(ResizeRequested{900,700});loop.Step(host);
    Check(host.resizes==1 && host.state.width==900,"event resize coalesces");
    loop.OnWindowEvent(SceneCommand::NextPalette);loop.OnWindowEvent(SceneCommand::ShowHints);
    host.now+=.1;loop.Step(host); const auto paintBefore=scene.Current(); host.now+=.1; loop.OnWindowEvent(LifecycleChanged{false});loop.Step(host);
    Check(scene.Current().palette.grid.r!=paintBefore.palette.grid.r || scene.Current().palette.grid.g!=paintBefore.palette.grid.g,"paint redraw retains animation timing");
    const auto before=scene.Current();host.now+=100;loop.OnWindowEvent(LifecycleChanged{});loop.Step(host);
    Check(scene.Current().gridPhase==before.gridPhase && scene.Current().paletteTransitionActive,"lifecycle resets paused feedback timing");
    host.onWait=[&]{host.state.powerSuspended=true;loop.OnWindowEvent(LifecycleChanged{});};
    const auto draws=host.draws;loop.Step(host);Check(host.draws==draws,"event during wait suspends draw");
    host.onWait={};host.state.powerSuspended=false;loop.OnWindowEvent(LifecycleChanged{});loop.Step(host);
    Check(host.draws==draws+1,"resume event retains dirty frame");
}
void SkippedTerrainPreparation() {
    scene::SceneState scene; scene.SetCruisePosition({31,.75});
    scene.PrepareFrame(); scene.Advance(.25); // Next preparation has four staging rows.
    FrameLoop loop(scene); FakeHost host; loop.BeginLive();
    host.waitResult = LatencyResult::Messages;
    loop.Step(host);
    auto frame=scene.PrepareFrame();
    Check(frame.rowsBuilt==4,"message-only wake does not prepare terrain");
    unsigned rows=4;
    for(int i=0;i<8;++i)rows+=scene.PrepareFrame().rowsBuilt;
    Check(rows==33,"message-only wake preserves all remaining staging work");
}
// These catch admission bypass, lost DXGI readiness, work before pacing,
// animation-reset coupling, and stale decisions after message wakes.
void OptionalCapAdmissions() {
    scene::SceneState defaultScene; FrameLoop display(defaultScene); FakeHost uncapped;
    display.BeginLive(); display.Step(uncapped); uncapped.now += .001; display.Step(uncapped);
    Check(!display.FrameLimit60() && uncapped.draws == 2 && uncapped.deadlines == 0,
          "default retains display pacing without deadline waits");
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.SetFrameLimit60(true); loop.BeginLive(); loop.Step(host);
    Check(host.draws == 1 && host.deadlines == 0, "first capped live frame is immediate");
    const auto before = scene.Current(); host.now += .001;
    loop.Step(host);
    Check(host.draws == 1 && host.starts == 1 && host.deadlines == 1 &&
          scene.Current().gridPhase == before.gridPhase,
          "cap wait precedes frame start and scene advancement");
    Check(std::abs(host.lastDeadline - 10.016666666666667) < 1e-12,
          "cap deadline permits at most sixty live admissions per second");
    loop.Step(host);
    Check(host.draws == 2 && host.waits == 2, "timer-only skip retains acquired DXGI credit");
    for (unsigned i=0; i<5; ++i) { loop.Step(host); loop.Step(host); }
    for (size_t i=1; i<host.admissionTimes.size(); ++i)
        Check(host.admissionTimes[i] - host.admissionTimes[i-1] >= .01666666666666,
              "capped admission intervals never fall below one sixtieth");
    host.now += 4; const auto draws = host.draws; loop.Step(host); loop.Step(host);
    Check(host.draws == draws+1, "late frame has no catch-up burst");
    scene::SceneState slowScene; FrameLoop slow(slowScene); FakeHost slowHost;
    slow.SetFrameLimit60(true); slow.BeginLive(); slow.Step(slowHost); slowHost.now += .001;
    slowHost.onWait = [&] { slowHost.now += .05; }; slow.Step(slowHost);
    Check(slowHost.deadlines == 0 && slowHost.draws == 2,
          "slower display readiness remains the limiting cadence");
}
void CapInputAndResourceIdentity() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.SetFrameLimit60(true); loop.BeginLive(); loop.Step(host); host.now += .001;
    bool disable = false;
    host.deadlineResult = DeadlineResult::Messages;
    host.onDeadline = [&](double) { disable = true; host.now += .001; };
    loop.Step(host);
    Check(host.draws == 1 && host.starts == 1, "cap message wake admits no work");
    host.onDrain = [&](unsigned) {
        if (disable) { disable = false; loop.OnWindowEvent(FrameLimitRequested{false}); loop.NextPalette(); }
    };
    loop.Step(host);
    Check(!loop.FrameLimit60() && host.draws == 2 && host.waits == 2 && host.last.paletteTransitionActive,
          "disable and palette input after cap wake use fresh policy and retain readiness");
    host.onDrain = {}; host.onDeadline = {}; host.deadlineResult = DeadlineResult::Elapsed;
    loop.SetFrameLimit60(true); host.now += .001; loop.Step(host);
    Check(host.draws == 2, "reenabling cap guards the most recent uncapped admission");
    ++host.resources.generation; host.resources.handle = 200; const auto waits = host.waits;
    loop.Step(host);
    Check(host.waits == waits+1 && host.draws == 3, "resource replacement discards retained cap-wait credit");
}
void CapLifecycleAndPause() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.SetFrameLimit60(true); loop.BeginLive(); loop.Step(host); host.now += .001;
    loop.OnWindowEvent(LifecycleChanged{}); loop.Step(host);
    Check(host.draws == 1, "lifecycle animation reset cannot bypass cap admission guard");
    const auto deadline = host.lastDeadline;
    host.now = 10.002; loop.RequestResize(800,600); loop.Step(host);
    Check(host.resizes == 1 && host.draws == 1 && host.lastDeadline == deadline && host.waits == 2,
          "resize coalesces while respecting cap and retaining readiness");
    host.state.hidden = true; const auto capWaits = host.deadlines;
    loop.Step(host);
    Check(host.idles == 1 && host.deadlines == capWaits, "suspension adds no periodic cap wake");
    host.now += 100; host.state.hidden = false; loop.Step(host);
    Check(host.draws == 2 && host.last.gridPhase == 0, "restoration discards animation gap without cap burst");
    loop.TogglePause(); host.now += .001; loop.Step(host); loop.Step(host);
    host.now += 6; loop.Step(host);
    const auto settledDraws = host.draws, settledWaits = host.deadlines; loop.Step(host);
    Check(host.draws == settledDraws && host.deadlines == settledWaits && host.idles == 2,
          "settled capped pause returns to message idle");
    const auto phase = scene.Current().gridPhase; loop.TogglePause(); loop.Step(host); loop.Step(host);
    Check(scene.Current().gridPhase == phase, "capped resume starts a fresh animation baseline");
}
void CapWaitEventsAndRecovery() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.SetFrameLimit60(true); loop.BeginLive(); loop.Step(host); host.now += .001;
    bool pending = false;
    host.deadlineResult = DeadlineResult::Messages;
    host.onDeadline = [&](double) { pending = true; };
    loop.Step(host);
    host.onDrain = [&](unsigned) { if (pending) { pending=false; host.running=false; } };
    Check(loop.Step(host) == FrameStep::Stopped && host.draws == 1,
          "quit arriving during cap wait prevents another frame");
    scene::SceneState recoverScene; FrameLoop recover(recoverScene); FakeHost recovered;
    recover.SetFrameLimit60(true); recover.BeginLive(); recovered.drawOutcome=render::RenderOutcome::Recovered;
    recovered.onDraw=[&] { ++recovered.resources.generation; recovered.resources.handle=200; };
    recover.Step(recovered); recovered.onDraw={}; recovered.drawOutcome=render::RenderOutcome::Drawn;
    recovered.now += .001; recover.Step(recovered);
    Check(recovered.draws == 1 && recovered.waits == 2 && recovered.deadlines == 1,
          "device recovery reacquires readiness without bypassing cap");
    recover.Step(recovered);
    Check(recovered.draws == 2 && recovered.last.gridPhase == 0,
          "capped recovery retry resets animation elapsed time");
}
void CapDeadlineCutoffAndFailure() {
    scene::SceneState scene; FrameLoop loop(scene); FakeHost host;
    loop.SetFrameLimit60(true); loop.BeginLive({true,10,0,1}); host.now=10.99; loop.Step(host);
    host.now=10.991; loop.Step(host);
    Check(std::abs(host.lastDeadline-11) < 1e-12 && host.draws == 1,
          "cap wait wakes at earlier measurement cutoff");
    Check(loop.Step(host) == FrameStep::Finished && host.draws == 1 && host.starts == 1,
          "cutoff reached during cap wait prevents frame start and submission");
    scene::SceneState failedScene; FrameLoop failed(failedScene); FakeHost failedHost;
    failed.SetFrameLimit60(true); failed.BeginLive(); failed.Step(failedHost); failedHost.now += .001;
    failedHost.deadlineResult = DeadlineResult::Failed;
    Check(failed.Step(failedHost) == FrameStep::Failed && failed.LastError()[0] != 0 && failedHost.starts == 1,
          "cap wait failure is terminal before frame work");
    scene::SceneState warmScene; FrameLoop warm(warmScene); FakeHost hidden;
    warm.SetFrameLimit60(true); hidden.state.hidden=true;
    Check(warm.Step(hidden,FrameMode::Warmup) == FrameStep::Warmed && hidden.deadlines == 0,
          "startup warm draw ignores cap and visibility");
    hidden.state.hidden=false; warm.BeginLive(); warm.Step(hidden);
    Check(hidden.draws == 2 && hidden.deadlines == 0, "BeginLive clears startup admission baseline");
}
int main() { OptionalCapAdmissions(); CapInputAndResourceIdentity(); CapLifecycleAndPause();
    CapWaitEventsAndRecovery(); CapDeadlineCutoffAndFailure();
    SkippedTerrainPreparation(); TypedEvents(); ExplicitOutcomes();
    PauseAndFeedback(); PostWaitResize(); SuspendAndFreshInput(); RecoveryAndWarmup();
    FailuresAndCutoff(); ClockAndHints(); WaitMessagesAndIdentity(); WarmupInputAndQuit(); MeasurementStartOrder(); MeasurementDeadlineAfterWait();
    if (!failures) std::cout << "PASS: production frame-loop orchestration and lifecycle scenarios\n";
    return failures ? 1 : 0;
}
