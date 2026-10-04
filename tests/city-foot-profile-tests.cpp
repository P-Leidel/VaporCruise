#include "render/scene-frame.h"
#include "render/shader-bindings.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
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
void Require(HRESULT result,const char* action) {if(FAILED(result))throw std::runtime_error(action);}
ComPtr<ID3DBlob> Compile(const char* source,const wchar_t* file,const char* target,const std::filesystem::path& directory) {
    ComPtr<ID3DBlob> code,error;ShaderIncludes includes(directory);
    HRESULT result=file?D3DCompileFromFile(file,nullptr,&includes,"main",target,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error)
        :D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",target,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());
    Require(result,"compile shipped city compositor");return code;
}
}
int wmain(int argc,wchar_t** argv) try {
    constexpr UINT width=256,height=10;
    const std::filesystem::path directory=argc==2?argv[1]:VAPORCRUISE_CITY_FOOT_DIRECTORY;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context),"create WARP device");
    auto vsCode=Compile("float4 main(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}",nullptr,"vs_5_0",directory);
    auto psCode=Compile(nullptr,VAPORCRUISE_CITY_FOOT_SHADER,"ps_5_0",directory);
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
    std::array<ComPtr<ID3D11ShaderResourceView>,4> masks;
    for(unsigned i=0;i<masks.size();++i) {
        D3D11_TEXTURE2D_DESC maskDesc{};maskDesc.Width=maskDesc.Height=maskDesc.MipLevels=maskDesc.ArraySize=1;
        maskDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;maskDesc.SampleDesc.Count=1;
        maskDesc.Usage=D3D11_USAGE_IMMUTABLE;maskDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<float,4> pixel{0,0,0,i==0?1.f:0.f};D3D11_SUBRESOURCE_DATA maskData{pixel.data(),sizeof(pixel),0};
        ComPtr<ID3D11Texture2D> texture;Require(device->CreateTexture2D(&maskDesc,&maskData,&texture),"create opaque/empty mask");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=maskDesc.Format;
        srv.ViewDimension=i<3?D3D11_SRV_DIMENSION_TEXTURE2DARRAY:D3D11_SRV_DIMENSION_TEXTURE2D;
        if(i<3){srv.Texture2DArray.MipLevels=1;srv.Texture2DArray.ArraySize=1;}else srv.Texture2D.MipLevels=1;
        Require(device->CreateShaderResourceView(texture.Get(),&srv,&masks[i]),"create mask view");
    }
    using namespace vaporcruise::render;
    for(auto entry:{std::pair{bindings::Coverage,0U},{bindings::Steady,1U},{bindings::Animated,2U},{bindings::Detail,3U}}) {
        auto* srv=masks[entry.second].Get();context->PSSetShaderResources(entry.first,1,&srv);
    }
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;Require(device->CreateSamplerState(&sd,&sampler),"create sampler");
    auto* sampleState=sampler.Get();context->PSSetSamplers(bindings::PanoramaSampler,1,&sampleState);
    FrameConstantsGPU frame{};frame.scene.ground={.001f,.001f,.001f,1};
    frame.scene.sky={.016f,.03f,.025f,1};frame.resolution[0]=1280;frame.resolution[1]=720;
    frame.camera[0]=3.2f;frame.camera[1]=.8f;frame.camera[2]=.5f;frame.camera[3]=1.f/6;
    frame.cameraDepth[3]=640;frame.panoramaExtents[0]=3;frame.panoramaExtents[1]=.202f;
    frame.panoramaExtents[2]=.19f;frame.panoramaExtents[3]=1.04f;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(frame);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{&frame,0,0};ComPtr<ID3D11Buffer> cb;
    Require(device->CreateBuffer(&bd,&data,&cb),"create constants");
    auto* constants=cb.Get();context->PSSetConstantBuffers(bindings::Frame,1,&constants);
    auto* renderView=view.Get();context->OMSetRenderTargets(1,&renderView,nullptr);
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    auto render=[&] {
        context->UpdateSubresource(cb.Get(),0,nullptr,&frame,0,0);context->Draw(3,0);
        context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"read city profiles");
        std::vector<std::array<float,4>> values(width*height);
        for(UINT y=0;y<height;++y)std::memcpy(values.data()+y*width,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,width*sizeof(values[0]));
        context->Unmap(readback.Get(),0);return values;
    };
    unsigned failures=0;auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
    const float body=.001f*.8f*.55f,mist=.016f*.65f;
    auto values=render();float low=1,high=0;bool footBounded=true;
    for(UINT x=0;x<width;++x) {
        float amount=(values[x][0]-body)/(mist-body);low=std::min(low,amount);high=std::max(high,amount);
        footBounded=footBounded&&std::isfinite(amount)&&amount>=0&&amount<=1.001f;
    }
    std::cout<<"opaque foot extinction at .012: "<<low<<".."<<high<<'\n';
    check(footBounded,"foot stays bounded between facade and shared mist");
    check(low>.44f,"even gaps in moving wisps settle the opaque city foot into distant mist");
    check(high-low>.08f,"city foot integration retains spatially broken height and drift");
    const auto reference=values;frame.scene.cityLightPhase=.25f;values=render();float drift=0;
    for(UINT x=0;x<width;++x)drift+=std::abs(values[x][0]-reference[x][0])/width;
    std::cout<<"quarter-cycle foot drift: "<<drift<<'\n';check(drift>.0005f,"city foot has visible moving extinction");
    // SunTop affects the disc/corona, but never the shared extinction color.
    // An opaque city must block it at its foot as well as its higher facades;
    // fading silhouette coverage would leak even the low disc's warm corona.
    frame.scene.cityLightPhase=0;values=render();auto dark=values;
    frame.scene.sunTop={1,1,1,1};values=render();float leak=0;
    for(UINT i=0;i<4*width;++i)for(UINT c=0;c<3;++c)leak=std::max(leak,std::abs(values[i][c]-dark[i][c]));
    std::cout<<"opaque city sun/corona leakage: "<<leak<<'\n';
    check(leak<.000002f,"opaque city feet and higher facades cannot become transparent to the sun");
    frame.scene.sunBottom=frame.scene.sunTop={};
    frame.scene.cityLightPhase=.9999f;const auto before=render();frame.scene.cityLightPhase=.0001f;const auto after=render();float wrap=0;
    for(UINT i=0;i<width*5;++i)for(UINT c=0;c<3;++c)wrap=std::max(wrap,std::abs(before[i][c]-after[i][c]));
    std::cout<<"42-second wrap maximum linear delta: "<<wrap<<'\n';check(wrap<.0001f,"foot settlement wraps continuously with existing fog clock");
    frame.scene.cityLightPhase=.63f;
    for(float screenHeight:{320.f,720.f,1397.f,7680.f}) {
        frame.resolution[1]=screenHeight;values=render();float join=0;
        for(UINT x=0;x<width;++x)join=std::max(join,std::abs(values[4*width+x][0]-mist));
        std::cout<<"height "<<screenHeight<<" horizon join delta: "<<join<<'\n';
        check(join<.000002f,"city foot reaches the shared sky/ground horizon color");
    }
    frame.resolution[1]=720;
    // A frozen sun, globally breathing sky, excessive exposure, or a phase
    // discontinuity must fail these probes of the actual atmosphere shader.
    const std::array<std::pair<std::uint32_t,std::uint32_t>,3> solarPalettes{{
        {0xFFD08C,0xF36AB8},{0xFFE1B0,0x91DAD8},{0xFFE0A0,0xEF877D}}};
    for(unsigned palette=0;palette<solarPalettes.size();++palette) {
        frame.scene.sunTop=vaporcruise::common::ColorFromSrgbHex(solarPalettes[palette].first);
        frame.scene.sunBottom=vaporcruise::common::ColorFromSrgbHex(solarPalettes[palette].second);
        frame.scene.cityLightPhase=0;const auto still=render();
        frame.scene.cityLightPhase=.25f;const auto bright=render();
        frame.scene.cityLightPhase=.75f;const auto dim=render();
        frame.scene.cityLightPhase=.5f;const auto colorShift=render();
        float coronaDrift=0,colorDrift=0,maximumDrift=0,outsideDrift=0;
        for(UINT x=0;x<width;++x)for(UINT c=0;c<3;++c) {
            coronaDrift=std::max(coronaDrift,std::abs(bright[6*width+x][c]-dim[6*width+x][c]));
            colorDrift=std::max(colorDrift,std::abs(colorShift[5*width+x][c]-still[5*width+x][c]));
            for(const auto* phase:{&bright,&dim,&colorShift}) {
                for(UINT row:{5U,6U})maximumDrift=std::max(maximumDrift,std::abs((*phase)[row*width+x][c]-still[row*width+x][c]));
                outsideDrift=std::max(outsideDrift,std::abs((*phase)[7*width+x][c]-still[7*width+x][c]));
            }
        }
        std::cout<<"solar palette "<<palette<<": corona drift="<<coronaDrift
            <<", color drift="<<colorDrift<<", max="<<maximumDrift<<", outside="<<outsideDrift<<'\n';
        check(coronaDrift>.001f,"the sun corona changes gently across the shared clock");
        check(colorDrift>.0005f,"the sun disc has a subtle smooth color response");
        check(maximumDrift<.035f,"solar changes remain subtle rather than changing exposure");
        check(outsideDrift<.0000001f,"solar animation cannot breathe across the rest of the sky");
        frame.scene.cityLightPhase=.9999f;const auto solarBefore=render();
        frame.scene.cityLightPhase=.0001f;const auto solarAfter=render();float solarWrap=0;
        for(UINT i=5*width;i<8*width;++i)for(UINT c=0;c<3;++c)
            solarWrap=std::max(solarWrap,std::abs(solarBefore[i][c]-solarAfter[i][c]));
        std::cout<<"solar palette "<<palette<<" wrap="<<solarWrap<<'\n';
        check(solarWrap<.0001f,"solar color and glow wrap smoothly at forty-two seconds");
    }
    // Equal half-coverage edges face in opposite directions. Their atmospheric
    // transmission is identical at the sampled center; only directional rim
    // light may distinguish them. A symmetric neon outline fails this guard.
    const auto edgeMask=[&](bool reverse) {
        D3D11_TEXTURE2D_DESC edgeDesc{};edgeDesc.Width=2;edgeDesc.Height=edgeDesc.MipLevels=edgeDesc.ArraySize=1;
        edgeDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;edgeDesc.SampleDesc.Count=1;
        edgeDesc.Usage=D3D11_USAGE_IMMUTABLE;edgeDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const std::array<std::array<float,4>,2> pixels{{{0,0,0,reverse?1.f:0.f},{0,0,0,reverse?0.f:1.f}}};
        D3D11_SUBRESOURCE_DATA edgeData{pixels.data(),sizeof(pixels),0};ComPtr<ID3D11Texture2D> texture;
        Require(device->CreateTexture2D(&edgeDesc,&edgeData,&texture),"create opposed city edges");
        D3D11_SHADER_RESOURCE_VIEW_DESC edgeView{};edgeView.Format=edgeDesc.Format;
        edgeView.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;edgeView.Texture2DArray.MipLevels=1;edgeView.Texture2DArray.ArraySize=1;
        ComPtr<ID3D11ShaderResourceView> result;Require(device->CreateShaderResourceView(texture.Get(),&edgeView,&result),"create city edge view");return result;
    };
    frame.scene={};frame.cityHorizontalOffset=-.10f;
    const auto directionalResponse=[&](float phase) {
        frame.scene.cityLightPhase=phase;
        std::array<float,2> responses{};
        for(unsigned edge=0;edge<2;++edge) {
            auto coverage=edgeMask(edge!=0);auto* resource=coverage.Get();context->PSSetShaderResources(bindings::Coverage,1,&resource);
            frame.scene.sunTop={};const auto unlit=render();frame.scene.sunTop={1,1,1,1};const auto lit=render();
            responses[edge]=lit[8*width+128][0]-unlit[8*width+128][0];
        }
        return responses[0]-responses[1];
    };
    const float facingDifference=directionalResponse(0);
    std::cout<<"sun-facing edge versus away edge="<<facingDifference<<'\n';
    check(facingDifference>.00001f,"city rim lights the sun-facing edge instead of outlining both sides");
    check(facingDifference<.0003f,"directional city rim remains a restrained highlight");
    const float brightRim=directionalResponse(.25f),dimRim=directionalResponse(.75f);
    const float rimGain=brightRim/std::max(dimRim,.0000001f);
    std::cout<<"directional rim quarter/three-quarter solar gain="<<rimGain<<'\n';
    check(rimGain>1.08f&&rimGain<1.13f,
          "above-fog skyline highlights follow the same gentle solar cycle");
    std::cout<<(failures?"FAIL":"PASS")<<": shipped city compositor foot profiles ("<<failures<<")\n";
    return failures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
