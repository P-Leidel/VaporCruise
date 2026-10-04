#pragma once
#include "render/frame-probe.h"
#include "render/measurement-export.h"
#include <Windows.h>
#include <d3d11.h>
#include <array>
#include <memory>
namespace vaporcruise::render {
enum class QueryStatus { Pending, Unavailable, Ready };
struct QueryResult {
    QueryStatus status = QueryStatus::Pending;
    double milliseconds = 0;
    QueryStatus detailStatus = QueryStatus::Pending;
    std::array<double,4> passMilliseconds{
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
};
struct TimestampQuerySample {
    HRESULT disjointStatus = S_FALSE, startStatus = S_FALSE, endStatus = S_FALSE;
    bool disjoint = false;
    UINT64 frequency = 0, start = 0, end = 0;
};
QueryResult DecodeTimestampSample(const TimestampQuerySample& sample);
struct DetailedTimestampQuerySample {
    TimestampQuerySample total;
    std::array<HRESULT,3> markStatuses{S_FALSE,S_FALSE,S_FALSE};
    std::array<UINT64,3> marks{};
    bool marksComplete = false;
};
QueryResult DecodeDetailedTimestampSample(const DetailedTimestampQuerySample& sample);
class FrameQueryBackend {
public:
    virtual ~FrameQueryBackend() = default;
    virtual void SetDetailEnabled(bool) {}
    virtual bool Create(ID3D11Device*, ID3D11DeviceContext*, std::wstring& error) = 0;
    virtual void Release() = 0;
    virtual void Begin(size_t slot) = 0;
    virtual void End(size_t slot) = 0;
    virtual void Mark(size_t, FrameGpuMark) {}
    virtual QueryResult Poll(size_t slot) = 0;
};
class D3D11FrameProbe final : public FrameProbe {
public:
    explicit D3D11FrameProbe(size_t capacity, bool gpuDetail = false);
    D3D11FrameProbe(size_t capacity, FrameQueryBackend& backend, double (*clock)(), bool gpuDetail = false);
    ~D3D11FrameProbe() override;
    bool Enabled() const override { return true; }
    bool AttachDevice(ID3D11Device*, ID3D11DeviceContext*, std::wstring& error) override;
    void DetachDevice() override;
    void FrameStart(double cpuStartSeconds, double timestampMs) override;
    void BeginAttempt(const scene::SceneParams&, float hintOpacity) override;
    void BeginGpu() override;
    void GpuMark(FrameGpuMark) override;
    void ResolveExecuted(bool) override;
    void TerrainPrepared(std::int64_t cell, std::int64_t section, unsigned rowsBuilt) override;
    void EndGpu() override;
    void BeforePresent() override;
    void AfterPresent(bool succeeded) override;
    void AbortAttempt() override;
    void SnapshotContext(HWND hwnd, double startSeconds);
    void FinalPoll();
    std::span<const FrameMeasurement> Rows() const { return rows_.Rows(); }
    std::span<const GpuDetailMeasurement> DetailedRows() const {
        return gpuDetail_ ? std::span<const GpuDetailMeasurement>{detailRows_.data(),rows_.Rows().size()} : std::span<const GpuDetailMeasurement>{};
    }
    MeasurementMetadata BuildMetadata(const RenderMeasurementContext&, double warmup, double duration, double startupMs) const;
private:
    void Poll(bool final);
    struct Slot {
        size_t row = PerformanceBuffer::InvalidIndex;
        std::uint64_t sequence = 0;
        bool totalResolved = false, detailComplete = false;
    };
    std::unique_ptr<FrameQueryBackend> ownedBackend_;
    FrameQueryBackend& backend_;
    double (*clock_)();
    PerformanceBuffer rows_;
    std::vector<GpuDetailMeasurement> detailRows_;
    bool gpuDetail_ = false, activeDetailInvalid_ = false;
    unsigned activeMarks_ = 0;
    std::uint64_t deviceGeneration_ = 0;
    std::array<Slot,kFrameQuerySlots> slots_{};
    std::uint64_t sequence_ = 0;
    size_t unavailable_ = 0, activeRow_ = PerformanceBuffer::InvalidIndex, activeSlot_ = PerformanceBuffer::InvalidIndex;
    double start_ = 0, timestamp_ = 0, presentStart_ = 0;
    bool eligible_ = false, attached_ = false, gpuBegun_ = false, gpuEnded_ = false, beforePresent_ = false, activeUnavailable_ = false;
    double sessionStart_ = 0;
    std::optional<DisplayMeasurementContext> display_;
    std::optional<unsigned> acLineStatus_;
};
std::string MeasurementUtf8(const std::wstring& text);
}
