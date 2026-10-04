#include <fstream>
#include <filesystem>
#include "render/Renderer.h"

#include "render/ShaderSource.h"
#include "render/d3d11-frame-probe.h"

#include <cstdio>
#include <vector>
#include <wincodec.h>
#include <cstddef>
#include <new>

namespace vaporcruise::render {

using Microsoft::WRL::ComPtr;

namespace {
// Logs the device-removal reason without
// introducing a logging subsystem the rest of the codebase doesn't have yet.
// Safe to call even when the device wasn't actually removed (e.g. a plain
// ResizeBuffers failure): GetDeviceRemovedReason() just reports S_OK.
void LogDeviceRemovedReason(ID3D11Device* device) {
    if (!device) {
        return;
    }
    const HRESULT reason = device->GetDeviceRemovedReason();
    wchar_t buffer[128];
    swprintf_s(buffer, L"VaporCruise: D3D11 device lost, GetDeviceRemovedReason=0x%08X\n",
               static_cast<unsigned int>(reason));
    OutputDebugStringW(buffer);
}

}  // namespace

Renderer::~Renderer() { ReleaseDeviceResources(); }

bool Renderer::Fail(const wchar_t* stage, HRESULT hr, const std::wstring& diagnostic) {
    wchar_t buffer[256];
    swprintf_s(buffer, L"VaporCruise: %s failed (HRESULT 0x%08X)", stage,
               static_cast<unsigned int>(hr));
    lastFailure_ = RenderResult::Failure(stage,hr);
    lastError_ = buffer;
    if (!diagnostic.empty()) { lastError_ += L": "; lastError_ += diagnostic; }
    OutputDebugStringW(lastError_.c_str());
    OutputDebugStringW(L"\n");
    return false;
}

bool Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    ReleaseDeviceResources();
    lastError_.clear();
    recoveryBudget_.Presented();
    hwnd_ = hwnd;
    return CreateDeviceAndSwapChain(hwnd, width, height);
}

bool Renderer::CreateDeviceAndSwapChain(HWND hwnd, uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    projection_ = BuildFrameProjection(static_cast<float>(width),static_cast<float>(height));

    UINT deviceFlags = 0;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel{};
    ComPtr<ID3D11Device> baseDevice;
    ComPtr<ID3D11DeviceContext> baseContext;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
        nullptr, 0, D3D11_SDK_VERSION,
        &baseDevice, &featureLevel, &baseContext);
#ifdef _DEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        OutputDebugStringW(L"VaporCruise: optional D3D11 debug layer unavailable; retrying hardware without it\n");
        baseDevice.Reset();
        baseContext.Reset();
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            deviceFlags & ~D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION,
            &baseDevice, &featureLevel, &baseContext);
    }
#endif
    if (FAILED(hr)) {
        return Fail(L"hardware device creation", hr);
    }

    device_ = baseDevice;
    context_ = baseContext;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) return Fail(L"DXGI device query", hr);
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return Fail(L"DXGI adapter query", hr);
    if (probe_.Enabled()) {
        DXGI_ADAPTER_DESC description{};
        if (SUCCEEDED(adapter->GetDesc(&description))) adapterDescription_ = description.Description;
    }
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return Fail(L"DXGI factory query", hr);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        device_.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) {
        return Fail(L"flip swap chain creation", hr);
    }

    // We drive borderless fullscreen ourselves; suppress DXGI's Alt+Enter handling.
    hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr)) return Fail(L"borderless window association", hr);

    hr = swapChain1.As(&swapChain_);
    if (FAILED(hr)) return Fail(L"latency swap chain query", hr);
    hr = swapChain_->SetMaximumFrameLatency(1);
    if (FAILED(hr)) return Fail(L"frame latency setup", hr);
    frameLatencyWaitableObject_ = swapChain_->GetFrameLatencyWaitableObject();
    if (!frameLatencyWaitableObject_) return Fail(L"frame latency handle creation", E_HANDLE);

    if (!CreateRenderTargetView() || !CreateShaders() || !CreateConstantBuffer()) return false;
    std::wstring queryError;
    if (!probe_.AttachDevice(device_.Get(),context_.Get(),queryError)) return Fail(L"measurement query creation",E_FAIL,queryError);
    try {
        const HRESULT artworkResult = backgroundResources_.Initialize(device_.Get(), scene::GetBackgroundArtwork());
        if (FAILED(artworkResult)) return Fail(L"background masks/views/sampler creation", artworkResult);
    } catch (const std::bad_alloc&) { return Fail(L"background artwork allocation", E_OUTOFMEMORY); }
    D3D11_DEPTH_STENCIL_DESC backgroundDepth{};
    backgroundDepth.DepthEnable = TRUE;
    backgroundDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    backgroundDepth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    HRESULT depthResult = device_->CreateDepthStencilState(&backgroundDepth, &backgroundDepthState_);
    if (FAILED(depthResult)) return Fail(L"background depth state", depthResult);
    if (!terrainRenderer_.Initialize(device_.Get(), context_.Get())) {
        return Fail(L"terrain preparation/resources",terrainRenderer_.LastResult());
    }
    ++resourceGeneration_;
    return true;
}

bool Renderer::CreateRenderTargetView() {
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_));
    if (FAILED(hr)) {
        return Fail(L"backbuffer query", hr);
    }
    // Analytic wire filtering cannot smooth the hard coverage boundary between
    // a tiny terrain face and the plain. Resolve multiple coverage/depth samples
    // while keeping the expensive pixel shaders at pixel frequency.
    renderSamples_ = 1;
    for (UINT samples : {4u, 2u}) {
        UINT colorLevels = 0, depthLevels = 0;
        hr = device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &colorLevels);
        if (FAILED(hr)) return Fail(L"color multisample support query", hr);
        hr = device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, samples, &depthLevels);
        if (FAILED(hr)) return Fail(L"depth multisample support query", hr);
        if (colorLevels && depthLevels) { renderSamples_ = samples; break; }
    }
    if (renderSamples_ > 1) {
        D3D11_TEXTURE2D_DESC color{};
        backBuffer_->GetDesc(&color);
        color.SampleDesc.Count = renderSamples_; color.SampleDesc.Quality = 0;
        // Keep the native MSAA target on the shader-readable resource path:
        // RT-only targets showed repeat differences at tiny mixed-coverage
        // pixels on the acceptance adapter. No SRV or extra pass is required.
        color.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; color.MiscFlags = 0;
        hr = device_->CreateTexture2D(&color, nullptr, &multisampleColor_);
        if (FAILED(hr)) return Fail(L"multisample color creation", hr);
    }
    hr = device_->CreateRenderTargetView(renderSamples_ > 1 ? multisampleColor_.Get() : backBuffer_.Get(), nullptr, &renderTargetView_);
    if (FAILED(hr)) return Fail(L"render target creation", hr);
    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width_; depth.Height = height_; depth.MipLevels = depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D32_FLOAT; depth.SampleDesc.Count = renderSamples_;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = device_->CreateTexture2D(&depth, nullptr, &depthTexture_);
    if (FAILED(hr)) return Fail(L"depth texture creation", hr);
    hr = device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, &depthView_);
    return SUCCEEDED(hr) || Fail(L"depth view creation", hr);
}

bool Renderer::CreateShaders() {
    std::vector<uint8_t> vsBytecode;
    if (!LoadFullscreenVertexShaderBytecode(vsBytecode)) {
        return Fail(L"vertex shader loading/compilation", E_FAIL);
    }
    HRESULT hr = device_->CreateVertexShader(vsBytecode.data(), vsBytecode.size(), nullptr,
                                              &fullscreenVertexShader_);
    if (FAILED(hr)) {
        return Fail(L"vertex shader creation", hr);
    }

    std::vector<uint8_t> psBytecode;
    if (!LoadSceneCompositePixelShaderBytecode(psBytecode)) {
        return Fail(L"pixel shader loading/compilation", E_FAIL);
    }
    hr = device_->CreatePixelShader(psBytecode.data(), psBytecode.size(), nullptr,
                                     &sceneCompositePixelShader_);
    return SUCCEEDED(hr) || Fail(L"pixel shader creation", hr);
}

bool Renderer::CreateConstantBuffer() {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(FrameConstantsGPU);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device_->CreateBuffer(&desc, nullptr, &frameConstantBuffer_);
    return SUCCEEDED(hr) || Fail(L"constant buffer creation", hr);
}

void Renderer::ReleaseSizeDependentResources() {
    renderTargetView_.Reset();
    multisampleColor_.Reset(); backBuffer_.Reset();
    depthView_.Reset(); depthTexture_.Reset();
}

RenderResult Renderer::Resize(uint32_t width, uint32_t height) {
    return Recover(ResizeAttempt(width,height)?RenderResult::Complete():lastFailure_);
}

bool Renderer::ResizeAttempt(uint32_t width, uint32_t height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return true;
    }
    if (width == width_ && height == height_) {
        return true;
    }

    width_ = width;
    height_ = height;
    projection_ = BuildFrameProjection(static_cast<float>(width),static_cast<float>(height));

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseSizeDependentResources();

    HRESULT hr = Inject(L"swap chain resize");
    if(SUCCEEDED(hr))hr = swapChain_->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN,
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr)) {
        return Fail(L"swap chain resize", hr);
    }

    return CreateRenderTargetView();
}

RenderMeasurementContext Renderer::MeasurementContext() const {
    return {MeasurementUtf8(adapterDescription_),width_,height_,renderSamples_,presentInterval_,terrainSeed_,resourceGeneration_};
}

RenderResult Renderer::RenderFrame(const RenderInput& input) {
    return Recover(DrawFrame(input,true)?RenderResult::Complete():lastFailure_);
}

bool Renderer::DrawFrame(const RenderInput& input, bool present) {
    const auto& sceneParams = input.frame.scene;
    terrainSeed_ = sceneParams.terrainSeed;
    if (!swapChain_) {
        return Fail(L"render without initialized swap chain", E_UNEXPECTED);
    }



    // The guard closes partial measurement attempts before any early return.
    struct AttemptScope {
        FrameProbe& probe;
        bool present;
        ~AttemptScope() { if (present) probe.AbortAttempt(); }
    } scope{probe_,present};
    if (present) {
        probe_.BeginAttempt(sceneParams,input.hintOpacity);
        // CPU preparation happened once before this synchronous render call,
        // including work on attempts that fail before the first GPU upload.
        probe_.TerrainPrepared(sceneParams.cruise.cell,
            sceneParams.cruise.cell / scene::kSectionDepthCells,input.frame.rowsBuilt);
    }

#ifdef _DEBUG
    if (simulateDeviceLossNextFrame_) {
        simulateDeviceLossNextFrame_ = false;
        return Fail(L"simulated device loss",DXGI_ERROR_DEVICE_REMOVED);
    }
#endif

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = Inject(L"scene constant upload");
    if(SUCCEEDED(hr))hr = context_->Map(frameConstantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        return Fail(L"scene constant upload", hr);
    }
    auto* gpu = static_cast<FrameConstantsGPU*>(mapped.pData);
    *gpu = PackSceneFrame(sceneParams, input.hintOpacity, input.uiScale, projection_);
    context_->Unmap(frameConstantBuffer_.Get(), 0);

    if (present) probe_.BeginGpu();
    // Preparation and uploads belong to both CPU and GPU measurement scopes.
    const HRESULT prepareInjection=Inject(L"terrain preparation");
    const bool prepared = SUCCEEDED(prepareInjection) && terrainRenderer_.Prepare(input.frame);
    if (!prepared) {
        hr = FAILED(prepareInjection)?prepareInjection:terrainRenderer_.LastResult();
        return Fail(L"terrain preparation",hr);
    }
    ID3D11RenderTargetView* rtv = renderTargetView_.Get();
    context_->OMSetRenderTargets(1, &rtv, depthView_.Get());
    context_->ClearDepthStencilView(depthView_.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    ID3D11Buffer* cb = frameConstantBuffer_.Get();
    if (present) probe_.GpuMark(FrameGpuMark::UploadsComplete);
    // Opaque terrain establishes depth first. The far-plane background then
    // shades only uncovered pixels, retaining atmosphere-only foreground fog.
    ClearShaderBindingsForTesting();
    hr=Inject(L"terrain draw");
    if(SUCCEEDED(hr)) { terrainRenderer_.Draw(input.frame,cb); hr=terrainRenderer_.LastResult(); }
    if (FAILED(hr)) {
        return Fail(L"terrain draw",hr);
    }
    if (present) probe_.GpuMark(FrameGpuMark::TerrainComplete);
    context_->OMSetDepthStencilState(backgroundDepthState_.Get(), 0);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->RSSetState(nullptr);
    ClearShaderBindingsForTesting();
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    BindCompositeProgram(context_.Get(),{fullscreenVertexShader_.Get(),sceneCompositePixelShader_.Get(),cb,backgroundResources_.Inputs()});
    context_->Draw(3, 0);
    if (present) probe_.GpuMark(FrameGpuMark::CompositeComplete);
    if (renderSamples_ > 1) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->ResolveSubresource(backBuffer_.Get(), 0, multisampleColor_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    if (present) {
        probe_.ResolveExecuted(renderSamples_>1);
        probe_.EndGpu();
    }
    if (!present) return true;
    probe_.BeforePresent();
    hr = Inject(L"frame present");
    if(SUCCEEDED(hr))hr = swapChain_->Present(presentInterval_, 0);
    probe_.AfterPresent(SUCCEEDED(hr));

    if (FAILED(hr)) return Fail(L"frame present", hr);
    recoveryBudget_.Presented();
    return true;
}

void Renderer::ClearShaderBindingsForTesting() {
    if(!clearBindingsForTesting_)return;
    ID3D11Buffer* buffers[2]{};
    ID3D11ShaderResourceView* views[7]{};
    ID3D11SamplerState* samplers[2]{};
    context_->VSSetShader(nullptr,nullptr,0);context_->PSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,2,buffers);context_->PSSetConstantBuffers(0,2,buffers);
    context_->PSSetShaderResources(0,7,views);context_->PSSetSamplers(0,2,samplers);
}

RenderResult Renderer::CaptureFrame(const RenderInput& input, const std::wstring& path) {
    const RenderInput immutable=input;
    CapturePixels pixels;
    return CaptureTransaction(immutable,[&](const RenderInput& retry) {
        return Readback(retry,pixels)?RenderResult::Complete():lastFailure_;
    },[&](RenderResult loss){return Recover(loss);},[&] {
        return EncodeCapture(pixels,path)?RenderResult::Complete():lastFailure_;
    });
}

bool Renderer::Readback(const RenderInput& input, CapturePixels& pixels) {
    if (!DrawFrame(input,false)) return false;
    ComPtr<ID3D11Texture2D> backBuffer, staging;
    HRESULT hr = Inject(L"capture backbuffer query");
    if(SUCCEEDED(hr))hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return Fail(L"capture backbuffer query", hr);
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = Inject(L"capture staging texture");
    if(SUCCEEDED(hr))hr = device_->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) return Fail(L"capture staging texture", hr);
    context_->CopyResource(staging.Get(), backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = Inject(L"capture GPU readback");
    if(SUCCEEDED(hr))hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return Fail(L"capture GPU readback", hr);
    // Own the complete image before unmapping or invoking WIC. No encoding on a failed attempt.
    try {
        pixels.rowPitch=mapped.RowPitch;
        pixels.bytes.assign(static_cast<const BYTE*>(mapped.pData),static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(mapped.RowPitch)*height_);
    } catch(const std::bad_alloc&) {
        context_->Unmap(staging.Get(),0); return Fail(L"capture CPU readback allocation",E_OUTOFMEMORY);
    }
    context_->Unmap(staging.Get(),0);
    return true;
}

bool Renderer::EncodeCapture(const CapturePixels& pixels, const std::wstring& path) {
    HRESULT hr=Inject(L"capture PNG encoding");
    if(FAILED(hr))return Fail(L"capture PNG encoding",hr);
    auto encode = [&]() -> HRESULT {
        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(result)) return result;
        ComPtr<IWICStream> stream;
        result = factory->CreateStream(&stream);
        if (FAILED(result)) return result;
        result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (FAILED(result)) return result;
        ComPtr<IWICBitmapEncoder> encoder;
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(result)) return result;
        result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(result)) return result;
        ComPtr<IWICBitmapFrameEncode> frame;
        result = encoder->CreateNewFrame(&frame, nullptr);
        if (FAILED(result)) return result;
        result = frame->Initialize(nullptr);
        if (FAILED(result)) return result;
        result = frame->SetSize(width_, height_);
        if (FAILED(result)) return result;
        // The built-in PNG encoder negotiates BGRA, while this swap chain is
        // RGBA. WIC converts the actual mapped GPU rows without channel swaps
        // in the renderer or assumptions about encoder-native formats.
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        result = frame->SetPixelFormat(&format);
        if (FAILED(result)) return result;
        ComPtr<IWICBitmap> source;
        result = factory->CreateBitmapFromMemory(width_, height_, GUID_WICPixelFormat32bppRGBA,
            pixels.rowPitch, static_cast<UINT>(pixels.bytes.size()), const_cast<BYTE*>(pixels.bytes.data()), &source);
        if (FAILED(result)) return result;
        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(&converter);
        if (FAILED(result)) return result;
        result = converter->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        if (FAILED(result)) return result;
        result = frame->WriteSource(converter.Get(), nullptr);
        if (FAILED(result)) return result;
        result = frame->Commit();
        if (FAILED(result)) return result;
        return encoder->Commit();
    };
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    hr = SUCCEEDED(com) || com == RPC_E_CHANGED_MODE ? encode() : com;
    if (SUCCEEDED(com)) CoUninitialize();
    if(FAILED(hr)) return Fail(L"capture PNG encoding",hr);
    const std::filesystem::path capturePath(path);
    const auto metadataPath=capturePath.parent_path()/(L"camera-"+capturePath.stem().wstring()+L".json");
    std::ofstream metadata(metadataPath,std::ios::binary);
    metadata << "{\"width\":" << width_ << ",\"height\":" << height_ << ",\"camera\":";
    SerializeCamera(metadata,DefaultCameraProjection());
    metadata << "}\n";metadata.flush();
    const bool written=static_cast<bool>(metadata);metadata.close();
    return (written && !metadata.fail()) || Fail(L"capture camera metadata write",E_FAIL);
}

RenderResult Renderer::Recover(RenderResult result) {
    auto recovered=RecoverAttempt(result,recoveryBudget_,[&] {
        probe_.AbortAttempt();
        LogDeviceRemovedReason(device_.Get());
        ReleaseDeviceResources();
        const auto injected=Inject(L"resource recreation");
        if(FAILED(injected)) { Fail(L"resource recreation",injected); return lastFailure_; }
        return CreateDeviceAndSwapChain(hwnd_,width_,height_)?RenderResult::Complete():lastFailure_;
    });
    // Preserve details already recorded by the failed attempt/recreation;
    // only synthesized recovery failures need a new boundary diagnostic.
    if(recovered.outcome==RenderOutcome::Failed &&
       (recovered.outcome!=lastFailure_.outcome || recovered.stage!=lastFailure_.stage || recovered.hr!=lastFailure_.hr))
        Fail(recovered.stage,recovered.hr);
    return recovered;
}

void Renderer::ReleaseDeviceResources() {
    probe_.DetachDevice();
    if (frameLatencyWaitableObject_) {
        if (!CloseHandle(frameLatencyWaitableObject_))
            OutputDebugStringW(L"VaporCruise: latency handle close failed\n");
        frameLatencyWaitableObject_ = nullptr;
    }
    ReleaseSizeDependentResources();
    backgroundDepthState_.Reset();
    terrainRenderer_.Release();
    backgroundResources_.Release();
    fullscreenVertexShader_.Reset();
    sceneCompositePixelShader_.Reset();
    frameConstantBuffer_.Reset();
    swapChain_.Reset();
    if (context_) {
        // Drop all owned resources/chain references first; then release
        // context bindings and force their deferred destruction before a
        // replacement flip chain is created for this HWND.
        context_->ClearState();
        context_->Flush();
    }
    context_.Reset();
    device_.Reset();
}

}  // namespace vaporcruise::render
