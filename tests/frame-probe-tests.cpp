#include "render/d3d11-frame-probe.h"
#include "scene/SceneState.h"
#include <cmath>
#include <iostream>
#include <array>
#include <cstdlib>
#include <new>
using namespace vaporcruise::render;
namespace {
size_t allocations = 0;
int failures = 0;
double now = 10; unsigned clocks = 0;
double Clock() { ++clocks; return now; }
void Check(bool ok, const char* name) { if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; } }
bool Near(double a, double b) { return std::abs(a-b) < 1e-8; }
struct Backend : FrameQueryBackend {
    std::array<QueryResult, kFrameQuerySlots> results{};
    unsigned begins = 0, ends = 0, polls = 0, creates = 0, releases = 0, marks = 0;
    bool detailed = false;
    void SetDetailEnabled(bool enabled) override { detailed=enabled; }
    void Mark(size_t, FrameGpuMark) override { ++marks; }
    bool createOk = true;
    bool Create(ID3D11Device*, ID3D11DeviceContext*, std::wstring& error) override {
        ++creates; if (!createOk) error=L"query creation fixture"; return createOk;
    }
    void Release() override { ++releases; }
    void Begin(size_t) override { ++begins; }
    void End(size_t) override { ++ends; }
    QueryResult Poll(size_t slot) override { ++polls; return results[slot]; }
};
vaporcruise::scene::SceneParams Params() {
    static const auto params=[] { vaporcruise::scene::SceneState scene; return scene.Current(); }();
    return params;
}
void Frame(D3D11FrameProbe& probe, bool presentOk = true) {
    now = 10; probe.FrameStart(10,125); probe.BeginAttempt(Params(),.5f); probe.BeginGpu();
    probe.TerrainPrepared(36028797018963971,1125899906842624,4); probe.EndGpu();
    now = 10.003; probe.BeforePresent(); now = 10.008; probe.AfterPresent(presentOk);
}
void DelayedAndFinal() {
    Backend backend; D3D11FrameProbe probe(32,backend,Clock); std::wstring error;
    Check(probe.AttachDevice(nullptr,nullptr,error), "fake backend attaches");
    Frame(probe); Frame(probe); Frame(probe); Frame(probe);
    Check(backend.polls == 0, "four attempt delay avoids premature polling");
    backend.results[1] = {QueryStatus::Ready,2.5}; Frame(probe);
    Check(probe.Rows()[0].gpuMs == 2.5 && std::isnan(probe.Rows()[1].gpuMs), "delayed result updates original row only");
    backend.results[2] = {QueryStatus::Unavailable,0};
    probe.FinalPoll(); const auto metadata = probe.BuildMetadata({},1,2,3);
    Check(metadata.queryUnavailable == 4 && metadata.querySlots == 16, "final pending and failed samples accounted once");
    const auto polls = backend.polls; probe.FinalPoll(); probe.DetachDevice(); probe.DetachDevice();
    Check(backend.polls == polls && probe.BuildMetadata({},0,0,0).queryUnavailable == 4, "final poll and detach cannot recount retired samples");
    Check(Near(probe.Rows()[0].cpuActiveMs,3) && Near(probe.Rows()[0].presentMs,5), "CPU includes scene scope and Present remains separate");
    Check(probe.Rows()[0].travelCell == 36028797018963971 && probe.Rows()[0].terrainRowsBuilt == 4, "exact travel and post-prepare work retained");
}
void CapacityAndRing() {
    Backend backend; D3D11FrameProbe bounded(2,backend,Clock); std::wstring error; bounded.AttachDevice(nullptr,nullptr,error);
    Frame(bounded); Frame(bounded); const auto count=clocks; Frame(bounded);
    Check(bounded.Rows().size()==2 && bounded.BuildMetadata({},0,0,0).dropped==1 && backend.begins==2 && clocks==count, "dropped row has no scope or clock work");
    Backend ring; D3D11FrameProbe probe(32,ring,Clock); probe.AttachDevice(nullptr,nullptr,error);
    for (unsigned i=0;i<17;++i) Frame(probe);
    Check(probe.Rows().size()==17 && ring.begins==16 && probe.BuildMetadata({},0,0,0).queryUnavailable==1, "occupied ring cannot overwrite pending row");
    ring.results[1]={QueryStatus::Ready,6}; probe.FinalPoll();
    Check(probe.Rows()[0].gpuMs==6 && std::isnan(probe.Rows()[16].gpuMs) && probe.BuildMetadata({},0,0,0).queryUnavailable==16, "ring exhaustion and pending rows counted distinctly");
}
void AbortAndReplacement() {
    Backend backend; D3D11FrameProbe probe(16,backend,Clock); std::wstring error; probe.AttachDevice(nullptr,nullptr,error);
    for (unsigned stage=0;stage<4;++stage) {
        probe.FrameStart(10,0); probe.BeginAttempt(Params(),0);
        if (stage>0) probe.BeginGpu();
        probe.TerrainPrepared(123,3,stage);
        if (stage>1) probe.EndGpu();
        if (stage==3) { now=10.003; probe.BeforePresent(); now=10.008; probe.AfterPresent(false); }
        else { now=10.004; probe.AbortAttempt(); }
        probe.AbortAttempt();
    }
    Check(probe.Rows().size()==4 && probe.BuildMetadata({},0,0,0).queryUnavailable==4, "every aborted attempt counted once");
    Check(Near(probe.Rows()[0].cpuActiveMs,4) && probe.Rows()[2].terrainRowsBuilt==2 && Near(probe.Rows()[3].presentMs,5), "partial failure retains known CPU terrain and Present work");
    for (const auto& row : probe.Rows()) Check(std::isnan(row.gpuMs),"failed GPU stays blank");
    Frame(probe); probe.DetachDevice(); probe.DetachDevice();
    Check(probe.BuildMetadata({},0,0,0).queryUnavailable==5, "detach counts old pending query once");
    backend.results.fill({QueryStatus::Pending,0}); probe.AttachDevice(nullptr,nullptr,error); Frame(probe);
    backend.results[6]={QueryStatus::Ready,8}; probe.FinalPoll();
    Check(std::isnan(probe.Rows()[4].gpuMs) && probe.Rows()[5].gpuMs==8 && backend.creates==2, "replacement preserves rows but never publishes old result");
    Backend broken; broken.createOk=false; D3D11FrameProbe failure(1,broken,Clock);
    Check(!failure.AttachDevice(nullptr,nullptr,error) && broken.releases>=1 && !error.empty(), "partial query creation cleans up and fails");
}
void RawResults() {
    TimestampQuerySample sample{S_OK,S_OK,S_OK,false,1000,10,12};
    Check(DecodeTimestampSample(sample).status==QueryStatus::Ready && DecodeTimestampSample(sample).milliseconds==2,"timestamp units from frequency");
    sample.disjointStatus=S_FALSE; Check(DecodeTimestampSample(sample).status==QueryStatus::Pending,"pending disjoint is not zero");
    sample.disjointStatus=S_OK; sample.startStatus=S_FALSE; Check(DecodeTimestampSample(sample).status==QueryStatus::Pending,"pending timestamp is not zero");
    sample.startStatus=E_FAIL; Check(DecodeTimestampSample(sample).status==QueryStatus::Unavailable,"failed timestamp unavailable");
    sample.startStatus=S_OK; sample.disjoint=true; Check(DecodeTimestampSample(sample).status==QueryStatus::Unavailable,"disjoint unavailable");
    sample.disjoint=false; sample.frequency=0; Check(DecodeTimestampSample(sample).status==QueryStatus::Unavailable,"zero frequency unavailable");
    sample.frequency=1000; sample.end=9; Check(DecodeTimestampSample(sample).status==QueryStatus::Unavailable,"reversed timestamps unavailable");
}
QueryResult DetailResult() {
    QueryResult result{QueryStatus::Ready,10};
    result.detailStatus=QueryStatus::Ready; result.passMilliseconds={1,2,3,4}; return result;
}
void DetailedFrame(D3D11FrameProbe& probe, bool presentOk=true, bool resolveExecuted=true) {
    now=10; probe.FrameStart(10,125); auto params=Params();
    probe.BeginAttempt(params,.5f); probe.BeginGpu();
    probe.GpuMark(FrameGpuMark::UploadsComplete); probe.GpuMark(FrameGpuMark::TerrainComplete);
    probe.GpuMark(FrameGpuMark::CompositeComplete); probe.ResolveExecuted(resolveExecuted); probe.EndGpu();
    now=10.003; probe.BeforePresent(); now=10.008; probe.AfterPresent(presentOk);
}
void DetailedRawResults() {
    DetailedTimestampQuerySample sample;
    sample.total={S_OK,S_OK,S_OK,false,1000,10,20};
    sample.markStatuses={S_OK,S_OK,S_OK}; sample.marks={11,13,16}; sample.marksComplete=true;
    auto decoded=DecodeDetailedTimestampSample(sample);
    Check(decoded.status==QueryStatus::Ready && decoded.detailStatus==QueryStatus::Ready
        && decoded.milliseconds==10 && decoded.passMilliseconds==std::array<double,4>{1,2,3,4},
        "four contiguous timestamp intervals sum exactly to unchanged GPU total");
    sample.markStatuses[1]=S_FALSE; decoded=DecodeDetailedTimestampSample(sample);
    Check(decoded.status==QueryStatus::Ready && decoded.detailStatus==QueryStatus::Pending,
        "pending intermediate timestamp preserves valid total and delays detail");
    sample.markStatuses[1]=E_FAIL; decoded=DecodeDetailedTimestampSample(sample);
    Check(decoded.status==QueryStatus::Ready && decoded.detailStatus==QueryStatus::Unavailable,
        "failed detail leaves unchanged total available");
    for(double value:decoded.passMilliseconds) Check(std::isnan(value),"invalid detail never publishes partial intervals");
    sample.markStatuses[1]=S_OK; sample.marks[1]=10;
    Check(DecodeDetailedTimestampSample(sample).detailStatus==QueryStatus::Unavailable,"reversed intermediate timestamp rejected");
    sample.marks={11,13,21}; Check(DecodeDetailedTimestampSample(sample).detailStatus==QueryStatus::Unavailable,"mark beyond end rejected");
    sample.marks={11,13,16}; sample.total.disjoint=true; decoded=DecodeDetailedTimestampSample(sample);
    Check(decoded.status==QueryStatus::Unavailable && decoded.detailStatus==QueryStatus::Unavailable,"shared disjoint invalidates total and all detail");
    sample.total.disjoint=false; sample.total.frequency=0;
    Check(DecodeDetailedTimestampSample(sample).detailStatus==QueryStatus::Unavailable,"zero frequency invalidates detail");
    sample.total.frequency=1000; sample.marksComplete=false; decoded=DecodeDetailedTimestampSample(sample);
    Check(decoded.status==QueryStatus::Ready && decoded.detailStatus==QueryStatus::Unavailable,"missing marker leaves valid total independent");
}
void DetailedLifecycle() {
    Backend backend; D3D11FrameProbe probe(32,backend,Clock,true); std::wstring error;
    Check(backend.detailed && probe.AttachDevice(nullptr,nullptr,error),"detail query backend configured only by explicit opt-in");
    probe.FrameStart(0,-10); probe.BeginAttempt(Params(),0); probe.BeginGpu();
    probe.GpuMark(FrameGpuMark::UploadsComplete); probe.EndGpu(); probe.BeforePresent(); probe.AfterPresent(true);
    Check(probe.DetailedRows().empty() && backend.begins==0 && backend.marks==0,"detailed warmup does no query work or retained rows");
    auto allocationStart=allocations;
    for(unsigned i=0;i<4;++i) DetailedFrame(probe);
    Check(backend.polls==0 && backend.marks==12 && allocations==allocationStart,"detailed frame marks are delayed and allocate no per-frame memory");
    backend.results[1]=DetailResult(); DetailedFrame(probe);
    Check(probe.DetailedRows().size()==probe.Rows().size() && probe.DetailedRows()[0].timestampMs==probe.Rows()[0].timestampMs,
        "detail rows retain exact legacy row join");
    const auto& first=probe.DetailedRows()[0];
    Check(first.status==GpuDetailStatus::Ready && first.passMilliseconds==std::array<double,4>{1,2,3,4}
        && first.resolveExecuted,
        "detailed result and actual resolve publish to original row");
    backend.results[2]=DetailResult(); backend.results[2].detailStatus=QueryStatus::Pending;
    probe.FinalPoll();
    Check(probe.Rows()[1].gpuMs==10 && probe.DetailedRows()[1].status==GpuDetailStatus::FinalPending,
        "final pending detail retains independently available total");
    Check(probe.BuildMetadata({},0,0,0).queryUnavailable==3,"available total with pending detail is not counted as unavailable");
    for(double value:probe.DetailedRows()[1].passMilliseconds) Check(std::isnan(value),"final partial detail stays blank");
    const auto polls=backend.polls; probe.FinalPoll(); probe.DetachDevice(); probe.DetachDevice();
    Check(backend.polls==polls,"detail final poll and repeated detach never recount retired slots");
    Backend late; D3D11FrameProbe pending(8,late,Clock,true); pending.AttachDevice(nullptr,nullptr,error);
    for(unsigned i=0;i<4;++i) DetailedFrame(pending);
    late.results[1]=DetailResult(); late.results[1].detailStatus=QueryStatus::Pending; DetailedFrame(pending);
    Check(pending.Rows()[0].gpuMs==10 && pending.DetailedRows()[0].status==GpuDetailStatus::Pending,
        "legacy total publishes while its detail slot remains owned");
    late.results[1]=DetailResult(); DetailedFrame(pending); pending.FinalPoll();
    Check(pending.DetailedRows()[0].status==GpuDetailStatus::Ready && pending.Rows()[0].gpuMs==10
        && pending.BuildMetadata({},0,0,0).queryUnavailable==5,
        "later detail resolves original row without recounting valid total");
    Backend ring; D3D11FrameProbe exhausted(32,ring,Clock,true); exhausted.AttachDevice(nullptr,nullptr,error);
    for(unsigned i=0;i<17;++i) DetailedFrame(exhausted);
    Check(ring.begins==16 && ring.marks==48 && exhausted.DetailedRows()[16].status==GpuDetailStatus::NoSlot,
        "detailed exhausted slot never overwrites pending legacy row");
    exhausted.DetachDevice(); const auto generation=exhausted.DetailedRows()[0].deviceGeneration;
    Check(exhausted.DetailedRows()[0].status==GpuDetailStatus::Detached,"pending old-device detail retired on detach");
    ring.results.fill({}); exhausted.AttachDevice(nullptr,nullptr,error); DetailedFrame(exhausted);
    ring.results[2]=DetailResult(); exhausted.FinalPoll();
    Check(exhausted.DetailedRows()[17].status==GpuDetailStatus::Ready
        && exhausted.DetailedRows()[17].deviceGeneration>generation
        && std::isnan(exhausted.Rows()[0].gpuMs),"recovery detail cannot publish an old-device result");
    Backend abort; D3D11FrameProbe partial(8,abort,Clock,true); partial.AttachDevice(nullptr,nullptr,error);
    for(unsigned stage=0;stage<4;++stage){
        partial.FrameStart(10,stage); partial.BeginAttempt(Params(),0);
        if(stage>0) partial.BeginGpu();
        if(stage>1) partial.GpuMark(FrameGpuMark::UploadsComplete);
        if(stage>2){partial.GpuMark(FrameGpuMark::TerrainComplete);partial.EndGpu();}
        partial.AbortAttempt(); partial.AbortAttempt();
    }
    DetailedFrame(partial,false);
    for(const auto& row:partial.DetailedRows()) Check(row.status==GpuDetailStatus::Aborted,"every partial/Present failure detail stays explicitly aborted");
    Check(partial.BuildMetadata({},0,0,0).queryUnavailable==5,"detail abort accounts unavailable total once");
    Backend badMarks; D3D11FrameProbe ordering(3,badMarks,Clock,true); ordering.AttachDevice(nullptr,nullptr,error);
    for(unsigned stage=0;stage<3;++stage){
        ordering.FrameStart(10,stage); ordering.BeginAttempt(Params(),0); ordering.BeginGpu();
        if(stage==0){ordering.GpuMark(FrameGpuMark::UploadsComplete); ordering.GpuMark(FrameGpuMark::UploadsComplete);}
        if(stage==1) ordering.GpuMark(FrameGpuMark::TerrainComplete);
        if(stage==2) ordering.GpuMark(FrameGpuMark::UploadsComplete);
        ordering.EndGpu(); ordering.BeforePresent(); ordering.AfterPresent(true);
    }
    badMarks.results.fill(DetailResult()); ordering.FinalPoll();
    for(size_t i=0;i<3;++i) Check(ordering.Rows()[i].gpuMs==10 && ordering.DetailedRows()[i].status==GpuDetailStatus::Unavailable,
        "duplicate/out-of-order/missing markers blank detail without erasing total");
    Backend defaults; D3D11FrameProbe totalOnly(2,defaults,Clock); totalOnly.AttachDevice(nullptr,nullptr,error);
    Frame(totalOnly); totalOnly.GpuMark(FrameGpuMark::UploadsComplete);
    Check(!defaults.detailed && defaults.marks==0 && totalOnly.DetailedRows().empty(),"default measurement has no detail queries or buffer");
    Check(GpuQueryObjectsPerSlot(false)*kFrameQuerySlots==48 && GpuQueryObjectsPerSlot(true)*kFrameQuerySlots==96,
        "timestamp allocation topology is 48 default and 96 detailed queries");
}
void DetailedResolveState() {
    Backend backend; D3D11FrameProbe probe(8,backend,Clock,true); std::wstring error;
    probe.AttachDevice(nullptr,nullptr,error);
    DetailedFrame(probe,true,false);
    probe.ResolveExecuted(true);
    DetailedFrame(probe,true,true);
    Check(!probe.DetailedRows()[0].resolveExecuted && probe.DetailedRows()[1].resolveExecuted,
        "1x and 4x resolve annotations stay with their actual attempt; out-of-attempt annotation ignored");
    const auto generation=probe.DetailedRows()[0].deviceGeneration;
    probe.DetachDevice(); probe.AttachDevice(nullptr,nullptr,error); DetailedFrame(probe,true,false);
    Check(!probe.DetailedRows()[0].resolveExecuted && probe.DetailedRows()[1].resolveExecuted
        && !probe.DetailedRows()[2].resolveExecuted && probe.DetailedRows()[2].deviceGeneration>generation,
        "resolve policy changes after device recreation never rewrite previous rows");
    backend.results.fill(DetailResult()); probe.FinalPoll();
    Check(!probe.DetailedRows()[2].resolveExecuted && probe.DetailedRows()[2].status==GpuDetailStatus::Ready,
        "valid empty resolve timestamp interval retains explicit 1x no-resolve flag");
}
void SnapshotAndNoOp() {
    Backend backend; D3D11FrameProbe probe(2,backend,Clock);
    WNDCLASSW klass{}; klass.lpfnWndProc=DefWindowProcW; klass.hInstance=GetModuleHandleW(nullptr); klass.lpszClassName=L"FrameProbeSnapshotTests";
    RegisterClassW(&klass); auto hwnd=CreateWindowW(klass.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,klass.hInstance,nullptr);
    Check(hwnd!=nullptr,"snapshot hidden window created"); probe.SnapshotContext(hwnd,12.5);
    const auto before=probe.BuildMetadata({},1,2,3); DestroyWindow(hwnd);
    const auto after=probe.BuildMetadata({},1,2,3);
    Check(after.measurementStartSeconds==12.5 && after.display.has_value()==before.display.has_value() && after.acLineStatus==before.acLineStatus,"starting snapshot retained after window destruction");
    if (before.display) Check(after.display->device==before.display->device,"starting monitor retained");
    UnregisterClassW(klass.lpszClassName,klass.hInstance);
    NullFrameProbe noop; const auto params=Params(); std::wstring error;
    const auto allocated=allocations; const auto sampled=clocks;
    for (unsigned i=0;i<100;++i) { noop.AttachDevice(nullptr,nullptr,error); noop.FrameStart(10,0); noop.BeginAttempt(params,0); noop.BeginGpu(); noop.GpuMark(FrameGpuMark::UploadsComplete); noop.ResolveExecuted((i%2)!=0); noop.TerrainPrepared(0,0,0); noop.EndGpu(); noop.BeforePresent(); noop.AfterPresent(true); noop.AbortAttempt(); noop.DetachDevice(); }
    Check(allocations==allocated && clocks==sampled,"no-op has no allocation or clock sampling");
    probe.FrameStart(0,-10); probe.BeginAttempt(params,0); probe.BeginGpu(); probe.EndGpu(); probe.BeforePresent(); probe.AfterPresent(true);
    Check(probe.Rows().empty(),"warmup eligibility admits no rows");
}
}
void* operator new(std::size_t n) { ++allocations; if (auto p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
int main() { try { DelayedAndFinal(); CapacityAndRing(); AbortAndReplacement(); RawResults(); DetailedRawResults(); DetailedLifecycle(); DetailedResolveState(); SnapshotAndNoOp(); } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } return failures ? 1 : 0; }
