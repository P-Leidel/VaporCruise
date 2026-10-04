#pragma once
#include <d3d11.h>
#include <array>
namespace vaporcruise::render {
// Borrowed synchronous views; resource owners outlive the draw.
struct BackgroundShaderInputs {
    std::array<ID3D11ShaderResourceView*,3> panorama{};
    ID3D11ShaderResourceView* detail=nullptr;
    ID3D11SamplerState* sampler=nullptr;
};
struct TerrainProgramInputs {
    ID3D11VertexShader* vertex=nullptr; ID3D11PixelShader* pixel=nullptr;
    ID3D11Buffer* frame=nullptr; ID3D11Buffer* section=nullptr;
};
struct CompositeProgramInputs {
    ID3D11VertexShader* vertex=nullptr; ID3D11PixelShader* pixel=nullptr;
    ID3D11Buffer* frame=nullptr;
    BackgroundShaderInputs background;
};
void BindTerrainProgram(ID3D11DeviceContext*,const TerrainProgramInputs&);
void BindCompositeProgram(ID3D11DeviceContext*,const CompositeProgramInputs&);
}
