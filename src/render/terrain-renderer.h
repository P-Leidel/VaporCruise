#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "scene/scene-frame-snapshot.h"
#include "render/terrain-upload-state.h"
#include "render/shader-program.h"
#include <array>
#include <memory>
#include <string>
namespace vaporcruise::render {
class TerrainRenderer {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    bool Prepare(const scene::SceneFrameSnapshot& frame);
    void Draw(const scene::SceneFrameSnapshot& frame, ID3D11Buffer* frameConstants);
    void Release();
    const std::wstring& LastError() const { return error_; }
    HRESULT LastResult() const { return result_; }
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
    using FailureHook = HRESULT (*)(const wchar_t*, void*);
    void SetFailureHook(FailureHook hook, void* data) { failureHook_=hook; failureData_=data; }
    const auto& UploadsForTesting() const { return uploadCounts_; }
#endif
private:
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
    FailureHook failureHook_=nullptr;
    void* failureData_=nullptr;
    std::array<unsigned, scene::kTerrainSlotCount> uploadCounts_{};
#endif
    bool Fail(const wchar_t* stage, HRESULT result);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_, drawConstants_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, scene::kTerrainSlotCount> vertices_;
    TerrainUploadState uploaded_;
    HRESULT result_ = S_OK;
    std::wstring error_;
};
}
