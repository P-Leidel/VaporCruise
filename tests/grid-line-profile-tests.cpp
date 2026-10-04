#include "render/scene-frame.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
class ShaderIncludes final : public ID3DInclude {
    std::filesystem::path directory_;
public:
    explicit ShaderIncludes(std::filesystem::path directory):directory_(std::move(directory)) {}
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID* bytes,UINT* size) override {
        std::ifstream file(directory_/name,std::ios::binary|std::ios::ate);
        if(!file)return E_FAIL;
        *size=static_cast<UINT>(file.tellg());file.seekg(0);
        auto* content=new char[*size];file.read(content,*size);*bytes=content;return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID bytes) override {delete[] static_cast<const char*>(bytes);return S_OK;}
};
void Require(HRESULT result,const char* action) {
    if(FAILED(result)) throw std::runtime_error(action);
}
ComPtr<ID3DBlob> Compile(const char* source,const wchar_t* file,const char* target,const std::filesystem::path& directory) {
    ComPtr<ID3DBlob> code,error;
    ShaderIncludes includes(directory);
    HRESULT result=file ? D3DCompileFromFile(file,nullptr,&includes,
        "main",target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error)
        : D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",target,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());
    Require(result,"compile real HLSL probe");return code;
}
}
int wmain(int argc,wchar_t** argv) try {
    constexpr UINT width=512,height=72;
    // An optional archived shader directory makes pre-change and mutation runs
    // exercise the same native behavioral assertions without editing the repo.
    const std::filesystem::path shaderDirectory=argc==2?argv[1]:VAPORCRUISE_GRID_SHADER_DIRECTORY;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context),"create WARP device");
    auto vsCode=Compile("float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1);}",nullptr,"vs_5_0",shaderDirectory);
    auto psCode=Compile(nullptr,VAPORCRUISE_GRID_PROFILE_SHADER,"ps_5_0",shaderDirectory);
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    Require(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs),"create vertex shader");
    Require(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps),"create pixel shader");
    D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=1;td.ArraySize=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;ComPtr<ID3D11RenderTargetView> view;
    Require(device->CreateTexture2D(&td,nullptr,&target),"create float target");
    Require(device->CreateRenderTargetView(target.Get(),nullptr,&view),"create render view");
    td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Require(device->CreateTexture2D(&td,nullptr,&readback),"create readback");
    vaporcruise::render::FrameConstantsGPU frame{};
    frame.scene.grid={.2f,0,0,1};frame.scene.gridGlow={0,.2f,0,1};
    frame.scene.road={0,0,.2f,1};frame.resolution[0]=1280;frame.resolution[1]=720;
    frame.camera[0]=3.2f;frame.camera[1]=.8f;frame.camera[2]=.5f;frame.camera[3]=1.f/6;
    frame.cameraDepth[0]=.1f;frame.cameraDepth[1]=640;frame.cameraDepth[2]=512;frame.cameraDepth[3]=640;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(frame);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{&frame,0,0};ComPtr<ID3D11Buffer> cb;
    Require(device->CreateBuffer(&bd,&data,&cb),"create constants");
    auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);
    auto* renderView=view.Get();context->OMSetRenderTargets(1,&renderView,nullptr);
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    auto render=[&] {
        context->UpdateSubresource(cb.Get(),0,nullptr,&frame,0,0);context->Draw(3,0);
        context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"read emitted profiles");
        std::vector<std::array<float,4>> result(width*height);
        for(UINT y=0;y<height;++y)std::memcpy(result.data()+y*width,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,width*sizeof(result[0]));
        context->Unmap(readback.Get(),0);return result;
    };
    auto pixels=render();
    auto sample=[&](UINT x,UINT y,UINT channel){return pixels[y*width+x][channel];};
    unsigned failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
    for(UINT row:{16U,48U}) {
        std::array<float,2> low{1,1},high{};
        for(UINT tile=0;tile<8;++tile)for(UINT channel=0;channel<2;++channel) {
            float value=sample(tile*64+32,row,channel);
            low[channel]=std::min(low[channel],value);high[channel]=std::max(high[channel],value);
            check(std::isfinite(value)&&value>.001f,"every resolved direction emits core and halo");
        }
        std::cout<<(row==16?"square":"crease")<<" peaks core "<<low[0]<<".."<<high[0]<<", halo "<<low[1]<<".."<<high[1]<<'\n';
        for(UINT channel=0;channel<2;++channel)check(low[channel]>.95f*high[channel],"rotating a resolved grid line keeps its core and halo strength");
    }
    // One cell lit, one cell dark; source coordinates remain unscaled in the scene.
    for(UINT period=0;period<4;++period) {
        float lit=sample(period*128+32,68,2),gap=sample(period*128+96,68,2);
        check(lit>.2f,"every two-cell period contains a bright one-cell dash");
        check(gap<.05f,"shorter dashes keep a dark one-cell gap");
        std::cout<<"dash "<<period<<" lit "<<lit<<", gap "<<gap<<'\n';
    }
    frame.reservedProjection=3;pixels=render();
    for(UINT row:{16U,48U}) {
        float low=1,high=0;
        for(UINT tile=0;tile<8;++tile) {
            float halo=sample(tile*64+32,row,1);
            low=std::min(low,halo);high=std::max(high,halo);
        }
        std::cout<<(row==16?"square":"crease")<<" halo at 3 pixels "<<low<<".."<<high<<'\n';
        check(low>.005f&&low>.95f*high,"all resolved directions retain the same off-center halo");
    }
    frame.reservedProjection=0;pixels=render();const auto reference=pixels;
    for(float sectionOffset:{-32.f,32.f,64.f}) {
        frame.cityHorizontalOffset=sectionOffset;pixels=render();float difference=0;
        for(UINT x=0;x<width;++x)difference=std::max(difference,std::abs(sample(x,68,2)-reference[68*width+x][2]));
        std::cout<<"section offset "<<sectionOffset<<" dash maximum delta "<<difference<<'\n';
        check(difference<.0005f,"dash phase matches at 32-cell section resets");
    }
    frame.panorama[3]=128;frame.cityHorizontalOffset=0;pixels=render();const auto compressedReference=pixels;
    float compressedMean=0;
    for(UINT x=0;x<width;++x)compressedMean+=sample(x,68,2)/width;
    // Half of the linear road emission (.2*.68) should remain visible, whose
    // sRGB value is about .289. Phase stability must not hide the markings.
    std::cout<<"whole-period dash footprint mean blue "<<compressedMean<<'\n';
    check(compressedMean>.285f&&compressedMean<.294f,"compressed dashes retain their visible half-lit average");
    float maximumPhaseDelta=0;
    for(float phase:{.25f,.5f,.75f,1.f,1.5f}) {
        frame.cityHorizontalOffset=phase;pixels=render();
        for(UINT x=0;x<width;++x)maximumPhaseDelta=std::max(maximumPhaseDelta,std::abs(sample(x,68,2)-compressedReference[68*width+x][2]));
    }
    std::cout<<"whole-period dash footprint maximum phase delta "<<maximumPhaseDelta<<'\n';
    check(maximumPhaseDelta<.0005f,"a pixel covering a full dash period has a stable half-lit average");
    std::cout<<(failures?"FAIL":"PASS")<<": actual HLSL resolved grid profiles and dash cadence ("<<failures<<")\n";
    return failures?1:0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
