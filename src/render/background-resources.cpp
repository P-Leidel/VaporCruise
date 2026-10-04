#include "render/background-resources.h"
#include <new>
namespace vaporcruise::render {
HRESULT BackgroundResources::Initialize(ID3D11Device* device, const scene::BackgroundArtwork& art) {
    Release();
    if (!device || !scene::IsValidBackgroundArtwork(art)) return E_INVALIDARG;
    const std::array<const scene::BackgroundMask*,3> masks{&art.coverage,&art.steadyEmission,&art.animatedEmission};
    const std::array<DXGI_FORMAT,3> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
    try {
        for (unsigned i = 0; i < masks.size(); ++i) {
            const auto& mask = *masks[i];
            std::array<D3D11_SUBRESOURCE_DATA,scene::kBackgroundLayouts * scene::kBackgroundMipLevels> data{};
            for (unsigned slice = 0; slice < scene::kBackgroundLayouts; ++slice) {
                for (unsigned mip = 0; mip < scene::kBackgroundMipLevels; ++mip) {
                    const auto& source = mask.slices[slice][mip];
                    data[slice*scene::kBackgroundMipLevels+mip] = {source.pixels.data(), source.rowPitch, 0};
                }
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = scene::kBackgroundWidth; desc.Height = scene::kBackgroundHeight; desc.MipLevels = scene::kBackgroundMipLevels; desc.ArraySize = scene::kBackgroundLayouts;
            desc.Format = formats[i]; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT result = device->CreateTexture2D(&desc, data.data(), &texture);
            if (SUCCEEDED(result)) result = device->CreateShaderResourceView(texture.Get(), nullptr, &views_[i]);
            if (FAILED(result)) { Release(); return result; }
        }
        std::array<D3D11_SUBRESOURCE_DATA,scene::kBackgroundMipLevels> detailData{};
        for (unsigned mip = 0; mip < scene::kBackgroundMipLevels; ++mip) {
            const auto& source = art.landscapeDetail[mip];
            detailData[mip] = {source.pixels.data(),source.rowPitch,0};
        }
        D3D11_TEXTURE2D_DESC detailDesc{};
        detailDesc.Width = scene::kBackgroundWidth; detailDesc.Height = scene::kLandscapeDetailHeight;
        detailDesc.MipLevels = scene::kBackgroundMipLevels; detailDesc.ArraySize = 1;
        detailDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; detailDesc.SampleDesc.Count = 1;
        detailDesc.Usage = D3D11_USAGE_IMMUTABLE; detailDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> detailTexture;
        HRESULT detailResult = device->CreateTexture2D(&detailDesc,detailData.data(),&detailTexture);
        if (SUCCEEDED(detailResult)) detailResult = device->CreateShaderResourceView(detailTexture.Get(),nullptr,&detail_);
        if (FAILED(detailResult)) { Release(); return detailResult; }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        const HRESULT result = device->CreateSamplerState(&sampler, &sampler_);
        if (FAILED(result)) { Release(); return result; }
        bytes_ = art.GpuBytes();
        return S_OK;
    } catch (const std::bad_alloc&) { Release(); return E_OUTOFMEMORY; }
}
void BackgroundResources::Release() { for (auto& view : views_) view.Reset(); detail_.Reset(); sampler_.Reset(); bytes_ = 0; }
}
