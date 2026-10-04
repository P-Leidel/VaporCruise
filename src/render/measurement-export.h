#pragma once
#include "render/performance-buffer.h"
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <array>
namespace vaporcruise::render {
struct RenderMeasurementContext {
    std::string adapter;
    unsigned width = 0, height = 0, renderSamples = 1, presentInterval = 1;
    std::uint64_t terrainSeed = 0, resourceGeneration = 0;
};
struct DisplayMeasurementContext {
    std::string device;
    unsigned refreshHz = 0, width = 0, height = 0;
};
struct MeasurementMetadata : RenderMeasurementContext {
    std::optional<DisplayMeasurementContext> display;
    std::optional<unsigned> acLineStatus;
    unsigned frameLimitFps = 0;
    double warmupSeconds = 0, captureSeconds = 0, startupMs = 0, measurementStartSeconds = 0;
    size_t dropped = 0, capacity = 0, queryUnavailable = 0, querySlots = 0;
};
enum class GpuDetailStatus { Pending, Ready, Unavailable, Aborted, NoSlot, Detached, FinalPending };
struct GpuDetailMeasurement {
    double timestampMs = 0;
    std::uint64_t deviceGeneration = 0;
    GpuDetailStatus status = GpuDetailStatus::Pending;
    double gpuTotalMs = std::numeric_limits<double>::quiet_NaN();
    std::array<double,4> passMilliseconds{
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
    bool resolveExecuted = false;
};
bool SerializeMeasurements(std::ostream& csv, std::ostream& json,
    std::span<const FrameMeasurement> rows, const MeasurementMetadata& metadata);
bool WriteMeasurements(const std::filesystem::path& path, std::span<const FrameMeasurement> rows,
    const MeasurementMetadata& metadata, std::wstring& error);
bool SerializeGpuDetails(std::ostream& csv, std::ostream& json,
    std::span<const GpuDetailMeasurement> rows, const MeasurementMetadata& metadata);
// The original CSV/JSON contract remains unchanged; these are opt-in sidecars.
bool WriteGpuDetails(const std::filesystem::path& measurementPath, std::span<const GpuDetailMeasurement> rows,
    const MeasurementMetadata& metadata, std::wstring& error);
}
