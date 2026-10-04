#include "render/terrain-renderer.h"
#include "render/ShaderSource.h"
#include "render/shader-bindings.h"
#include <cstring>
#include <cstdio>
#include <vector>
namespace vaporcruise::render {
namespace { struct DrawConstants { float translation[4]; }; static_assert(sizeof(DrawConstants) == 16); }
bool TerrainRenderer::Fail(const wchar_t* stage, HRESULT result) {
    result_ = result;
    wchar_t text[256]; swprintf_s(text, L"VaporCruise: terrain %s failed (HRESULT 0x%08X)", stage, static_cast<unsigned>(result));
    error_ = text; OutputDebugStringW(text); return false;
}
bool TerrainRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    Release(); context_ = context; result_ = S_OK; error_.clear();
    std::vector<std::uint8_t> bytecode;
    if (!LoadTerrainVertexShaderBytecode(bytecode)) return Fail(L"vertex shader compilation", E_FAIL);
    HRESULT hr = device->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr, &vertexShader_);
    if (FAILED(hr)) return Fail(L"vertex shader creation", hr);
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    hr = device->CreateInputLayout(elements, 2, bytecode.data(), bytecode.size(), &layout_);
    if (FAILED(hr)) return Fail(L"input layout", hr);
    if (!LoadTerrainPixelShaderBytecode(bytecode)) return Fail(L"pixel shader compilation", E_FAIL);
    hr = device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, &pixelShader_);
    if (FAILED(hr)) return Fail(L"pixel shader creation", hr);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = scene::kSectionVertexCount * sizeof(scene::TerrainVertex);
    desc.Usage = D3D11_USAGE_DYNAMIC; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    for (auto& buffer : vertices_) {
        hr = device->CreateBuffer(&desc, nullptr, &buffer);
        if (FAILED(hr)) return Fail(L"vertex buffer", hr);
    }
    std::array<std::uint32_t, scene::kSectionIndexCount> indices;
    if (!scene::BuildTerrainIndices(indices)) return Fail(L"index generation", E_FAIL);
    desc = {}; desc.ByteWidth = sizeof(indices); desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = indices.data();
    hr = device->CreateBuffer(&desc, &data, &indexBuffer_);
    if (FAILED(hr)) return Fail(L"index buffer", hr);
    desc = {}; desc.ByteWidth = sizeof(DrawConstants); desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&desc, nullptr, &drawConstants_);
    if (FAILED(hr)) return Fail(L"draw constants", hr);
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
    hr = device->CreateDepthStencilState(&depth, &depthState_);
    if (FAILED(hr)) return Fail(L"depth state", hr);
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = TRUE;
    hr = device->CreateRasterizerState(&raster, &rasterState_);
    return SUCCEEDED(hr) || Fail(L"raster state", hr);
}
bool TerrainRenderer::Prepare(const scene::SceneFrameSnapshot& frame) {
    result_ = S_OK; error_.clear();
    if (!context_ || !scene::IsValidCruisePosition(frame.scene.cruise) || frame.terrainSlots.size()!=scene::kTerrainSlotCount)
        return Fail(L"cruise snapshot", E_INVALIDARG);
    const auto slots = frame.terrainSlots;
    for (const auto& slot : slots) if (slot.active && !slot.complete)
        return Fail(L"incomplete active terrain", E_INVALIDARG);
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!uploaded_.NeedsUpload(i, slots[i])) continue;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = S_OK;
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
        if(failureHook_) hr=failureHook_(L"terrain vertex upload",failureData_);
#endif
        if(SUCCEEDED(hr)) hr=context_->Map(vertices_[i].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return Fail(L"vertex upload", hr);
        std::memcpy(mapped.pData, slots[i].vertices.data(), sizeof(slots[i].vertices));
        context_->Unmap(vertices_[i].Get(), 0);
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
        ++uploadCounts_[i];
#endif
        uploaded_.Uploaded(i,slots[i].revision);
    }
    return true;
}
void TerrainRenderer::Draw(const scene::SceneFrameSnapshot& frame, ID3D11Buffer* frameConstants) {
    result_ = S_OK; error_.clear();
    if (!context_ || !scene::IsValidCruisePosition(frame.scene.cruise) || frame.terrainSlots.size()!=scene::kTerrainSlotCount) {
        Fail(L"cruise snapshot",E_INVALIDARG); return;
    }
    for (size_t i=0; i<frame.terrainSlots.size(); ++i) {
        const auto& slot=frame.terrainSlots[i];
        if (slot.active && (!slot.complete || uploaded_.NeedsUpload(i,slot))) {
            Fail(L"unprepared active terrain",E_INVALIDARG); return;
        }
    }
    context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetIndexBuffer(indexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
    BindTerrainProgram(context_.Get(),{vertexShader_.Get(),pixelShader_.Get(),frameConstants,drawConstants_.Get()});
    context_->OMSetDepthStencilState(depthState_.Get(), 0); context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->RSSetState(rasterState_.Get());
    const auto slots = frame.terrainSlots;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].active || !slots[i].complete) continue;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(drawConstants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { Fail(L"per-section upload", hr); return; }
        const DrawConstants constants{{SectionTranslation(slots[i].sectionIndex,frame.scene.cruise),0,0,0}};
        std::memcpy(mapped.pData, &constants, sizeof(constants)); context_->Unmap(drawConstants_.Get(), 0);
        ID3D11Buffer* draw = drawConstants_.Get(); context_->VSSetConstantBuffers(bindings::Section, 1, &draw);
        ID3D11Buffer* vertices = vertices_[i].Get(); const UINT stride = sizeof(scene::TerrainVertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
        context_->DrawIndexed(scene::kSectionIndexCount, 0, 0);
    }
}
void TerrainRenderer::Release() {
#ifdef VAPORCRUISE_RENDER_DIAGNOSTICS
    uploadCounts_.fill(0);
#endif
    for (auto& buffer : vertices_) buffer.Reset(); uploaded_.Reset();
    indexBuffer_.Reset(); drawConstants_.Reset(); layout_.Reset(); vertexShader_.Reset(); pixelShader_.Reset();
    depthState_.Reset(); rasterState_.Reset(); context_.Reset();
}
}
