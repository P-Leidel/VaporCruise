#pragma once
#include <d3d11.h>
#include "render/shader-program.h"
#include <wrl/client.h>
#include <array>
#include "scene/background-artwork.h"
namespace vaporcruise::render {
class BackgroundResources {
public:
    HRESULT Initialize(ID3D11Device* device, const scene::BackgroundArtwork& artwork);
    BackgroundShaderInputs Inputs() const { return {{views_[0].Get(),views_[1].Get(),views_[2].Get()},detail_.Get(),sampler_.Get()}; }
    void Release();
    std::size_t GpuBytes() const { return bytes_; }
private:
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,3> views_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> detail_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    std::size_t bytes_ = 0;
};
}
