#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace vaporcruise::scene { struct SceneParams; }
namespace vaporcruise::render {
inline constexpr size_t kFrameQuerySlots = 16;
enum class FrameGpuMark { UploadsComplete, TerrainComplete, CompositeComplete };
inline constexpr size_t GpuQueryObjectsPerSlot(bool detailed) { return detailed ? 6 : 3; }
class FrameProbe {
public:
    virtual ~FrameProbe() = default;
    virtual bool Enabled() const = 0;
    virtual bool AttachDevice(ID3D11Device*, ID3D11DeviceContext*, std::wstring& error) = 0;
    virtual void DetachDevice() = 0;
    virtual void FrameStart(double cpuStartSeconds, double timestampMs) = 0;
    virtual void BeginAttempt(const scene::SceneParams&, float hintOpacity) = 0;
    virtual void BeginGpu() = 0;
    virtual void GpuMark(FrameGpuMark) {}
    virtual void ResolveExecuted(bool) {}
    virtual void TerrainPrepared(std::int64_t cell, std::int64_t section, unsigned rowsBuilt) = 0;
    virtual void EndGpu() = 0;
    virtual void BeforePresent() = 0;
    virtual void AfterPresent(bool succeeded) = 0;
    virtual void AbortAttempt() = 0;
};
class NullFrameProbe final : public FrameProbe {
public:
    bool Enabled() const override { return false; }
    bool AttachDevice(ID3D11Device*, ID3D11DeviceContext*, std::wstring&) override { return true; }
    void DetachDevice() override {}
    void FrameStart(double, double) override {}
    void BeginAttempt(const scene::SceneParams&, float) override {}
    void BeginGpu() override {}
    void TerrainPrepared(std::int64_t, std::int64_t, unsigned) override {}
    void EndGpu() override {}
    void BeforePresent() override {}
    void AfterPresent(bool) override {}
    void AbortAttempt() override {}
};
inline FrameProbe& DefaultFrameProbe() { static NullFrameProbe probe; return probe; }
}
