#include "render/d3d11-frame-probe.h"
#include "scene/SceneState.h"
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <algorithm>
namespace vaporcruise::render {
namespace {
double ClockSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
class D3D11QueryBackend final : public FrameQueryBackend {
public:
    void SetDetailEnabled(bool value) override { detailed_=value; }
    bool Create(ID3D11Device* device, ID3D11DeviceContext* context, std::wstring& error) override {
        Release();
        if (!device || !context) { error = L"VaporCruise: GPU timing device/context unavailable."; return false; }
        context_ = context;
        for (auto& slot : slots_) {
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            auto hr=device->CreateQuery(&desc,&slot.disjoint);
            if (SUCCEEDED(hr)) { desc.Query=D3D11_QUERY_TIMESTAMP; hr=device->CreateQuery(&desc,&slot.start); }
            if (SUCCEEDED(hr)) hr=device->CreateQuery(&desc,&slot.end);
            if (detailed_) for (auto& mark : slot.marks) {
                if (SUCCEEDED(hr)) hr=device->CreateQuery(&desc,&mark);
            }
            if (FAILED(hr)) {
                wchar_t message[128]; swprintf_s(message,L"VaporCruise: GPU timing query creation failed (HRESULT 0x%08X).",static_cast<unsigned>(hr));
                error=message; Release(); return false;
            }
        }
        return true;
    }
    void Release() override {
        for (auto& slot : slots_) {
            slot.disjoint.Reset(); slot.start.Reset(); slot.end.Reset();
            for (auto& mark : slot.marks) mark.Reset();
            slot.markCount=0;
        }
        context_.Reset();
    }
    void Begin(size_t i) override {
        slots_[i].markCount=0;
        context_->Begin(slots_[i].disjoint.Get()); context_->End(slots_[i].start.Get());
    }
    void Mark(size_t i, FrameGpuMark mark) override {
        const size_t point=static_cast<size_t>(mark);
        if (!detailed_ || point>=slots_[i].marks.size() || point!=slots_[i].markCount) return;
        context_->End(slots_[i].marks[point].Get()); ++slots_[i].markCount;
    }
    void End(size_t i) override { context_->End(slots_[i].end.Get()); context_->End(slots_[i].disjoint.Get()); }
    QueryResult Poll(size_t i) override {
        auto& slot=slots_[i]; TimestampQuerySample sample; D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
        sample.disjointStatus=context_->GetData(slot.disjoint.Get(),&data,sizeof(data),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (sample.disjointStatus==S_OK && !data.Disjoint && data.Frequency) {
            sample.startStatus=context_->GetData(slot.start.Get(),&sample.start,sizeof(sample.start),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            sample.endStatus=context_->GetData(slot.end.Get(),&sample.end,sizeof(sample.end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        }
        sample.disjoint=data.Disjoint!=FALSE; sample.frequency=data.Frequency;
        if (!detailed_) return DecodeTimestampSample(sample);
        DetailedTimestampQuerySample detail; detail.total=sample; detail.marksComplete=slot.markCount==slot.marks.size();
        // Never poll an unissued mark. The existing total remains independently readable.
        if (detail.marksComplete && DecodeTimestampSample(sample).status==QueryStatus::Ready) {
            for (size_t point=0; point<slot.marks.size(); ++point)
                detail.markStatuses[point]=context_->GetData(slot.marks[point].Get(),&detail.marks[point],sizeof(detail.marks[point]),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        }
        return DecodeDetailedTimestampSample(detail);
    }
private:
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint,start,end;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>,3> marks;
        size_t markCount=0;
    };
    std::array<Slot,kFrameQuerySlots> slots_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    bool detailed_=false;
};
}
QueryResult DecodeTimestampSample(const TimestampQuerySample& s) {
    if (FAILED(s.disjointStatus)) return {QueryStatus::Unavailable,0};
    if (s.disjointStatus==S_FALSE) return {};
    if (s.disjoint || !s.frequency || FAILED(s.startStatus) || FAILED(s.endStatus)) return {QueryStatus::Unavailable,0};
    if (s.startStatus==S_FALSE || s.endStatus==S_FALSE) return {};
    if (s.end<s.start) return {QueryStatus::Unavailable,0};
    return {QueryStatus::Ready,1000.0*static_cast<double>(s.end-s.start)/static_cast<double>(s.frequency)};
}
QueryResult DecodeDetailedTimestampSample(const DetailedTimestampQuerySample& sample) {
    auto result=DecodeTimestampSample(sample.total);
    result.detailStatus=result.status;
    if (result.status!=QueryStatus::Ready) return result;
    result.detailStatus=QueryStatus::Unavailable;
    if (!sample.marksComplete) return result;
    for (const auto status : sample.markStatuses) if (FAILED(status)) return result;
    for (const auto status : sample.markStatuses) if (status==S_FALSE) { result.detailStatus=QueryStatus::Pending; return result; }
    for (const auto status : sample.markStatuses) if (status!=S_OK) return result;
    const std::array<UINT64,5> points{sample.total.start,sample.marks[0],sample.marks[1],sample.marks[2],sample.total.end};
    for (size_t point=1; point<points.size(); ++point) if (points[point]<points[point-1]) return result;
    for (size_t pass=0; pass<result.passMilliseconds.size(); ++pass)
        result.passMilliseconds[pass]=1000.0*static_cast<double>(points[pass+1]-points[pass])/static_cast<double>(sample.total.frequency);
    result.detailStatus=QueryStatus::Ready;
    return result;
}
std::string MeasurementUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if (size<=0) return {};
    std::string result(static_cast<size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
D3D11FrameProbe::D3D11FrameProbe(size_t capacity,bool gpuDetail)
    : ownedBackend_(std::make_unique<D3D11QueryBackend>()), backend_(*ownedBackend_), clock_(ClockSeconds), gpuDetail_(gpuDetail) {
    rows_.Configure(capacity); if (gpuDetail_) detailRows_.resize(capacity); backend_.SetDetailEnabled(gpuDetail_);
}
D3D11FrameProbe::D3D11FrameProbe(size_t capacity,FrameQueryBackend& backend,double (*clock)(),bool gpuDetail)
    : backend_(backend), clock_(clock), gpuDetail_(gpuDetail) {
    rows_.Configure(capacity); if (gpuDetail_) detailRows_.resize(capacity); backend_.SetDetailEnabled(gpuDetail_);
}
D3D11FrameProbe::~D3D11FrameProbe() { DetachDevice(); }
bool D3D11FrameProbe::AttachDevice(ID3D11Device* device, ID3D11DeviceContext* context, std::wstring& error) {
    DetachDevice();
    if (!backend_.Create(device,context,error)) { backend_.Release(); return false; }
    attached_=true; ++deviceGeneration_; return true;
}
void D3D11FrameProbe::DetachDevice() {
    AbortAttempt();
    for (auto& slot : slots_) {
        if (slot.row!=PerformanceBuffer::InvalidIndex) {
            if (!slot.totalResolved) ++unavailable_;
            if (gpuDetail_ && detailRows_[slot.row].status==GpuDetailStatus::Pending) detailRows_[slot.row].status=GpuDetailStatus::Detached;
        }
        slot.row=PerformanceBuffer::InvalidIndex;
    }
    if (attached_) backend_.Release();
    attached_=false; eligible_=false;
}
void D3D11FrameProbe::FrameStart(double start, double timestamp) {
    start_=start; timestamp_=timestamp; eligible_=std::isfinite(start) && start>0;
}
void D3D11FrameProbe::BeginAttempt(const scene::SceneParams& params,float hint) {
    if (!eligible_) return;
    eligible_=false; ++sequence_; Poll(false);
    activeRow_=rows_.Add({timestamp_,0,0});
    if (activeRow_==PerformanceBuffer::InvalidIndex) return;
    auto& row=rows_.At(activeRow_);
    row.gridPhase=static_cast<float>(params.gridPhase); row.cityLightPhase=static_cast<float>(params.cityLightPhase);
    row.hintOpacity=hint; row.gridR=params.palette.grid.r; row.gridG=params.palette.grid.g; row.gridB=params.palette.grid.b;
    if (gpuDetail_) {
        auto& detail=detailRows_[activeRow_]; detail.timestampMs=timestamp_; detail.deviceGeneration=deviceGeneration_;
    }
    gpuBegun_=gpuEnded_=beforePresent_=activeUnavailable_=false;
    activeMarks_=0; activeDetailInvalid_=false;
    const size_t index=static_cast<size_t>(sequence_%slots_.size());
    if (attached_ && slots_[index].row==PerformanceBuffer::InvalidIndex) activeSlot_=index;
    else {
        activeSlot_=PerformanceBuffer::InvalidIndex; ++unavailable_; activeUnavailable_=true;
        if (gpuDetail_) detailRows_[activeRow_].status=GpuDetailStatus::NoSlot;
    }
}
void D3D11FrameProbe::BeginGpu() {
    if (activeSlot_==PerformanceBuffer::InvalidIndex || gpuBegun_) return;
    slots_[activeSlot_]={activeRow_,sequence_}; backend_.Begin(activeSlot_); gpuBegun_=true;
}
void D3D11FrameProbe::GpuMark(FrameGpuMark mark) {
    if (!gpuDetail_ || !gpuBegun_ || gpuEnded_ || activeSlot_==PerformanceBuffer::InvalidIndex) return;
    if (activeDetailInvalid_ || static_cast<unsigned>(mark)!=activeMarks_ || activeMarks_>=3) { activeDetailInvalid_=true; return; }
    backend_.Mark(activeSlot_,mark); ++activeMarks_;
}
void D3D11FrameProbe::ResolveExecuted(bool value) {
    if (gpuDetail_ && activeRow_!=PerformanceBuffer::InvalidIndex) detailRows_[activeRow_].resolveExecuted=value;
}
void D3D11FrameProbe::TerrainPrepared(std::int64_t cell,std::int64_t section,unsigned built) {
    if (activeRow_==PerformanceBuffer::InvalidIndex) return;
    auto& row=rows_.At(activeRow_); row.travelCell=cell; row.terrainSection=section; row.terrainRowsBuilt=built;
}
void D3D11FrameProbe::EndGpu() {
    if (gpuBegun_ && !gpuEnded_) {
        slots_[activeSlot_].detailComplete=activeMarks_==3 && !activeDetailInvalid_;
        backend_.End(activeSlot_); gpuEnded_=true;
    }
}
void D3D11FrameProbe::BeforePresent() {
    if (activeRow_==PerformanceBuffer::InvalidIndex) return;
    presentStart_=clock_(); rows_.At(activeRow_).cpuActiveMs=(presentStart_-start_)*1000; beforePresent_=true;
}
void D3D11FrameProbe::AfterPresent(bool succeeded) {
    if (activeRow_==PerformanceBuffer::InvalidIndex) return;
    if (beforePresent_) rows_.At(activeRow_).presentMs=(clock_()-presentStart_)*1000;
    if (!succeeded) { AbortAttempt(); return; }
    activeRow_=activeSlot_=PerformanceBuffer::InvalidIndex;
}
void D3D11FrameProbe::AbortAttempt() {
    if (activeRow_==PerformanceBuffer::InvalidIndex) return;
    if (!beforePresent_) rows_.At(activeRow_).cpuActiveMs=(clock_()-start_)*1000;
    EndGpu();
    if (gpuDetail_) detailRows_[activeRow_].status=GpuDetailStatus::Aborted;
    if (!activeUnavailable_) ++unavailable_;
    if (activeSlot_!=PerformanceBuffer::InvalidIndex) slots_[activeSlot_].row=PerformanceBuffer::InvalidIndex;
    activeRow_=activeSlot_=PerformanceBuffer::InvalidIndex;
}
void D3D11FrameProbe::Poll(bool final) {
    for (auto& slot : slots_) {
        if (slot.row==PerformanceBuffer::InvalidIndex || (!final && sequence_-slot.sequence<4)) continue;
        const auto result=backend_.Poll(static_cast<size_t>(&slot-slots_.data()));
        if (!slot.totalResolved) {
            if (result.status==QueryStatus::Ready && std::isfinite(result.milliseconds) && result.milliseconds>=0) {
                rows_.SetGpu(slot.row,result.milliseconds);
                if (gpuDetail_) detailRows_[slot.row].gpuTotalMs=result.milliseconds;
                slot.totalResolved=true;
            } else if (result.status!=QueryStatus::Pending || final) { ++unavailable_; slot.totalResolved=true; }
        }
        if (gpuDetail_) {
            auto& detail=detailRows_[slot.row];
            if (detail.status==GpuDetailStatus::Pending) {
                if (!slot.detailComplete || result.status==QueryStatus::Unavailable ||
                    (result.status==QueryStatus::Ready && (!std::isfinite(result.milliseconds) || result.milliseconds<0)) ||
                    result.detailStatus==QueryStatus::Unavailable) detail.status=GpuDetailStatus::Unavailable;
                else if (result.status==QueryStatus::Ready && result.detailStatus==QueryStatus::Ready) {
                    double sum=0; bool valid=true;
                    for (double value : result.passMilliseconds) { valid &= std::isfinite(value) && value>=0; sum+=value; }
                    valid &= std::isfinite(sum) && std::abs(sum-result.milliseconds)<=1e-8*std::max(1.0,result.milliseconds);
                    if (valid) { detail.passMilliseconds=result.passMilliseconds; detail.status=GpuDetailStatus::Ready; }
                    else detail.status=GpuDetailStatus::Unavailable;
                } else if (final) detail.status=GpuDetailStatus::FinalPending;
            }
            if (slot.totalResolved && detail.status!=GpuDetailStatus::Pending) slot.row=PerformanceBuffer::InvalidIndex;
        } else if (slot.totalResolved) slot.row=PerformanceBuffer::InvalidIndex;
    }
}
void D3D11FrameProbe::FinalPoll() { AbortAttempt(); Poll(true); }
void D3D11FrameProbe::SnapshotContext(HWND hwnd,double start) {
    sessionStart_=start; display_.reset(); acLineStatus_.reset();
    MONITORINFOEXW monitor{}; monitor.cbSize=sizeof(monitor); DEVMODEW mode{}; mode.dmSize=sizeof(mode);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor) && EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode))
        display_=DisplayMeasurementContext{MeasurementUtf8(monitor.szDevice),mode.dmDisplayFrequency,mode.dmPelsWidth,mode.dmPelsHeight};
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power)) acLineStatus_=power.ACLineStatus;
}
MeasurementMetadata D3D11FrameProbe::BuildMetadata(const RenderMeasurementContext& context,double warmup,double duration,double startup) const {
    MeasurementMetadata metadata; static_cast<RenderMeasurementContext&>(metadata)=context;
    metadata.display=display_; metadata.acLineStatus=acLineStatus_; metadata.measurementStartSeconds=sessionStart_;
    metadata.warmupSeconds=warmup; metadata.captureSeconds=duration; metadata.startupMs=startup;
    metadata.dropped=rows_.Dropped(); metadata.capacity=rows_.Capacity(); metadata.queryUnavailable=unavailable_; metadata.querySlots=kFrameQuerySlots;
    return metadata;
}
}
