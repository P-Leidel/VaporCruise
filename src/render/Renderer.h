#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include "render/frame-probe.h"
#include "render/render-input.h"
#include "render/render-recovery.h"
#include "render/measurement-export.h"
#include "render/terrain-renderer.h"
#include "render/background-resources.h"

#include "common/Color.h"
#include "render/scene-frame.h"

namespace vaporcruise::render {

// Owns the D3D11 device, flip-discard swap chain, back-buffer render target
// view, depth buffer, background pass and streamed terrain pass.
class Renderer {
public:
    ~Renderer();
    explicit Renderer(FrameProbe& probe = DefaultFrameProbe()) : probe_(probe) {}
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // Returns false if the device/swap chain/shaders could not be created;
    // the caller should treat this as unrecoverable.
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);

    // Returns an explicit completed, recovered or failed resize result;
    // the caller should treat that as unrecoverable, same as RenderFrame.
    RenderResult Resize(uint32_t width, uint32_t height);


    // Main owns message-aware pacing. This method only draws/presents;
    // it never waits on the latency handle. Recovered means no frame was drawn.
    RenderResult RenderFrame(const RenderInput& input);
    void SetPresentInterval(unsigned interval) { presentInterval_ = interval; }
    RenderMeasurementContext MeasurementContext() const;
    // Draws the same shader to the actual swap-chain backbuffer, without
    // presenting, then performs a one-time GPU readback and PNG encoding.
    RenderResult CaptureFrame(const RenderInput& input, const std::wstring& path);
    // GPU diagnostic only; no application switch enables this.
    void ClearBindingsBeforePassesForTesting(bool value) { clearBindingsForTesting_=value; }
    HANDLE LatencyHandle() const { return frameLatencyWaitableObject_; }
    uint64_t ResourceGeneration() const { return resourceGeneration_; }
    const std::wstring& LastError() const { return lastError_; }

#ifdef _DEBUG
    // Makes the next RenderFrame() call take the same bounded recovery
    // path a real DXGI_ERROR_DEVICE_REMOVED/RESET would, so Debug
    // builds can validate recovery end-to-end without crashing the GPU driver --
    // D3D11 has no supported API to force a real device removal, and
    // deliberately triggering a real TDR risks hanging the display for
    // other apps too. Not reachable in Release builds.
    void SimulateDeviceLoss() { simulateDeviceLossNextFrame_ = true; }
#endif

#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
    using FailureHook = HRESULT (*)(const wchar_t*, void*);
    void SetFailureHook(FailureHook hook, void* data) {
        failureHook_=hook; failureData_=data; terrainRenderer_.SetFailureHook(hook,data);
    }
    const auto& TerrainUploadsForTesting() const { return terrainRenderer_.UploadsForTesting(); }
#endif
private:
    HRESULT Inject(const wchar_t* stage) const {
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
        if(failureHook_)return failureHook_(stage,failureData_);
#else
        (void)stage;
#endif
        return S_OK;
    }
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
    FailureHook failureHook_=nullptr;
    void* failureData_=nullptr;
#endif
    void ClearShaderBindingsForTesting();
    bool clearBindingsForTesting_=false;
    bool CreateDeviceAndSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    bool CreateRenderTargetView();
    bool CreateShaders();
    bool CreateConstantBuffer();
    bool DrawFrame(const RenderInput& input, bool present);
    bool ResizeAttempt(uint32_t width, uint32_t height);
    struct CapturePixels { std::vector<BYTE> bytes; UINT rowPitch=0; };
    bool Readback(const RenderInput& input, CapturePixels& pixels);
    bool EncodeCapture(const CapturePixels& pixels, const std::wstring& path);
    RenderResult Recover(RenderResult result);
    void ReleaseSizeDependentResources();
    void ReleaseDeviceResources();
    bool Fail(const wchar_t* stage, HRESULT hr, const std::wstring& diagnostic = {});

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> multisampleColor_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> backgroundDepthState_;
    TerrainRenderer terrainRenderer_;
    BackgroundResources backgroundResources_;
    std::uint64_t terrainSeed_ = scene::kDefaultTerrainSeed;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> sceneCompositePixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> frameConstantBuffer_;
    HANDLE frameLatencyWaitableObject_ = nullptr;

    HWND hwnd_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    UINT renderSamples_ = 1;
    FrameProjection projection_;
    std::wstring lastError_;
    uint64_t resourceGeneration_ = 0;
    RecoveryBudget recoveryBudget_;
    RenderResult lastFailure_;
    FrameProbe& probe_;
    unsigned presentInterval_ = 1;
    std::wstring adapterDescription_;
#ifdef _DEBUG
    bool simulateDeviceLossNextFrame_ = false;
#endif
};

}  // namespace vaporcruise::render
