#pragma once
#include "render/shader-program.h"
#include "render/shader-bindings.h"
#include "render/ShaderSource.h"
#include "render/scene-frame.h"
#include <wrl/client.h>
// Check exact D3D state independently of driver-dependent MSAA pixel rounding.
inline bool CheckExplicitShaderBindings() {
    using namespace vaporcruise::render;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return false;
    ComPtr<ID3D11Buffer> frame,section;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(FrameConstantsGPU);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&bd,nullptr,&frame)))return false;
    bd.ByteWidth=16;if(FAILED(device->CreateBuffer(&bd,nullptr,&section)))return false;
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=4;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device->CreateTexture2D(&td,nullptr,&texture)) || FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)))return false;
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;if(FAILED(device->CreateSamplerState(&sd,&sampler)))return false;
    std::vector<std::uint8_t> code;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    if(!LoadShaderBytecode(ShaderProgramId::TerrainVertex,code) || FAILED(device->CreateVertexShader(code.data(),code.size(),nullptr,&vs)))return false;
    if(!LoadShaderBytecode(ShaderProgramId::TerrainPixel,code) || FAILED(device->CreatePixelShader(code.data(),code.size(),nullptr,&ps)))return false;
    const BackgroundShaderInputs background{{view.Get(),view.Get(),view.Get()},view.Get(),sampler.Get()};
    auto verify=[&](bool terrain) {
        ComPtr<ID3D11VertexShader> actualVS;ComPtr<ID3D11PixelShader> actualPS;
        context->VSGetShader(&actualVS,nullptr,nullptr);context->PSGetShader(&actualPS,nullptr,nullptr);
        if(actualVS.Get()!=vs.Get() || actualPS.Get()!=ps.Get())return false;
        ComPtr<ID3D11Buffer> cb;context->PSGetConstantBuffers(bindings::Frame,1,&cb);if(cb.Get()!=frame.Get())return false;
        if(terrain) {
            cb.Reset();context->VSGetConstantBuffers(bindings::Frame,1,&cb);if(cb.Get()!=frame.Get())return false;
            cb.Reset();context->VSGetConstantBuffers(bindings::Section,1,&cb);if(cb.Get()!=section.Get())return false;
        }
        if(terrain) return true;
        for(unsigned slot:{bindings::Coverage,bindings::Steady,bindings::Animated,bindings::Detail}) {
            ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(slot,1,&actual);if(actual.Get()!=view.Get())return false;
        }
        for(unsigned slot:{bindings::PanoramaSampler}) {
            ComPtr<ID3D11SamplerState> actual;context->PSGetSamplers(slot,1,&actual);if(actual.Get()!=sampler.Get())return false;
        }
        return true;
    };
    context->ClearState();BindTerrainProgram(context.Get(),{vs.Get(),ps.Get(),frame.Get(),section.Get()});
    if(!verify(true))return false;
    context->ClearState();BindCompositeProgram(context.Get(),{vs.Get(),ps.Get(),frame.Get(),background});
    return verify(false);
}
