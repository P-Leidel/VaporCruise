#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <span>
namespace vaporcruise::render {
struct FrameMeasurement {
    double timestampMs = 0, cpuActiveMs = 0, presentMs = 0;
    double gpuMs = std::numeric_limits<double>::quiet_NaN();
    float gridPhase = 0, hintOpacity = 0, cityLightPhase = 0;
    float gridR = 0, gridG = 0, gridB = 0;
    std::int64_t travelCell = 0, terrainSection = 0;
    unsigned terrainRowsBuilt = 0;
};
class PerformanceBuffer {
public:
    static constexpr size_t InvalidIndex = std::numeric_limits<size_t>::max();
    void Configure(size_t capacity) { rows_.clear(); rows_.resize(capacity); retained_ = 0; dropped_ = 0; }
    size_t Add(const FrameMeasurement& row) {
        if (retained_ >= rows_.size()) { ++dropped_; return InvalidIndex; }
        rows_[retained_] = row; return retained_++;
    }
    bool SetGpu(size_t index, double value) {
        if (index >= retained_) return false;
        rows_[index].gpuMs = value; return true;
    }
    FrameMeasurement& At(size_t index) { return rows_[index]; }
    std::span<const FrameMeasurement> Rows() const { return {rows_.data(), retained_}; }
    size_t Dropped() const { return dropped_; }
    size_t Capacity() const { return rows_.size(); }
private:
    std::vector<FrameMeasurement> rows_;
    size_t retained_ = 0, dropped_ = 0;
};
}
