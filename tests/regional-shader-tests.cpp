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
    constexpr UINT width=257,height=8;
    // An optional archived shader directory makes pre-change and mutation runs
    // exercise the same native behavioral assertions without editing the repo.
    const std::filesystem::path shaderDirectory=argc==2?argv[1]:VAPORCRUISE_REGIONAL_DIRECTORY;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context),"create WARP device");
    auto vsCode=Compile("float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1);}",nullptr,"vs_5_0",shaderDirectory);
    auto psCode=Compile(nullptr,VAPORCRUISE_REGIONAL_SHADER,"ps_5_0",shaderDirectory);
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
    vaporcruise::scene::SceneState state;
    state.SetCruisePosition({127,.75});
    auto frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);
    frame.reservedProjection=90;
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
    unsigned failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
    auto luminance=[](const auto& c){return .2126f*c[0]+.7152f*c[1]+.0722f*c[2];};
    auto colorLuminance=[](const auto& c){return .2126f*c.r+.7152f*c.g+.0722f*c.b;};
    for(int palette=0;palette<12;++palette) {
        frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);frame.reservedProjection=90;
        auto pixels=render();
        for(UINT x=0;x<width;++x) {
            check(std::abs(luminance(pixels[x])-colorLuminance(frame.scene.grid))<1e-5f,"regional core preserves linear luminance");
            check(std::abs(luminance(pixels[width+x])-colorLuminance(frame.scene.gridGlow))<1e-5f,"regional halo preserves its own linear luminance");
            for(UINT mode=0;mode<3;++mode)for(UINT channel=0;channel<3;++channel)
                check(std::isfinite(pixels[mode*width+x][channel])&&pixels[mode*width+x][channel]<=1,"regional grid colors avoid new display clipping");
        }
        // Compare combined emission to a real untinted run, preserving the
        // production halo attenuation as well as the component luminances.
        const float regionalStrength=frame.regionalControl[2];
        frame.regionalControl[2]=0;const auto untinted=render();frame.regionalControl[2]=regionalStrength;
        for(UINT x=0;x<width;++x)
            check(std::abs(luminance(pixels[2*width+x])-luminance(untinted[2*width+x]))<1e-5f,"combined grid radiance retains original light and halo attenuation");
        check(std::abs(pixels[128][0]-pixels[160][0])>1e-5,"different supported world points vary in regional hue");
        const auto held=pixels;frame.scene.cityLightPhase=.49f;pixels=render();
        check(pixels==held,"regional hue and weights ignore the atmosphere clock");
        const auto old=frame;
        state.SetCruisePosition({128,0});
        frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);
        // .25 cells of travel shifts the same world point .5 world units closer.
        frame.reservedProjection=89.5f;pixels=render();
        float error=0;
        for(UINT row=0;row<4;++row)for(UINT channel=0;channel<4;++channel)
            error=std::max(error,std::abs(held[row*width+128][channel]-pixels[row*width+128][channel]));
        check(error<1e-5f,"the same world point retains hue at a 128-cell rollover");
        frame.reservedProjection=700;pixels=render();
        check(pixels[4*width+128][0]==0,"regional influence fades before depth support ends");
        check(std::abs(pixels[128][0]-frame.scene.grid.r)<1e-6,"unsupported distant continuation keeps its original color");
        frame.reservedProjection=90;pixels=render();
        check(pixels[4*width][0]==0&&pixels[4*width+128][0]>.99f,"regional influence fades beyond the lateral field");
        check(std::abs(pixels[4*width+240][0]-.529984f)<1e-5,"lateral influence fades smoothly within the 200 to 250 world unit margin");
        frame.reservedProjection=590;pixels=render();
        check(std::abs(pixels[4*width+128][0]-.5f)<1e-5,"depth influence reaches half weight midway through its supported fade");
        frame=old;
        state.SetCruisePosition({127,.75});
        if(palette%4==0)state.NextPalette();
        state.Advance(.2,true);
    }
    for(const std::int64_t boundary:{32LL,128LL,(1LL<<55)+128,vaporcruise::scene::kMaxTravelCell-255}) {
        state.SetCruisePosition({boundary-1,.75});
        frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);frame.reservedProjection=120;
        const auto before=render();
        state.SetCruisePosition({boundary,0});
        frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);frame.reservedProjection=119.5f;
        const auto after=render();float error=0;
        for(UINT mode:{0U,1U,2U,3U})for(UINT x=0;x<width;++x)for(UINT channel=0;channel<4;++channel)
            error=std::max(error,std::abs(before[mode*width+x][channel]-after[mode*width+x][channel]));
        check(error<1e-5,"production HLSL keeps world hue across section, region, large and maximum route boundaries");
    }
    frame=vaporcruise::render::PackSceneFrame(state.Current(),0,1,1280,720);
    frame.reservedProjection=90;frame.cityHorizontalOffset=20;
    // Only the sun is active: upper distant faces gain a tiny local light.
    for(auto& knot:frame.regionalKnots)knot[3]=0;
    auto pixels=render();
    check(pixels[6*width+128][0]>.0001f,"distant upper sun-facing terrain receives faint warmth");
    for(UINT x=0;x<width;++x)for(UINT channel=0;channel<3;++channel)
        check(pixels[6*width+x][channel]<=.007885f,"sunset surface radiance stays below its maximum gain");
    frame.cityHorizontalOffset=0;pixels=render();
    check(pixels[6*width+128][0]==0,"upper-face light protects the dark road and shoulders");
    frame.cityHorizontalOffset=20;frame.reservedProjection=260;pixels=render();
    check(pixels[6*width+128][0]==0,"upper-face warmth ends by 260 world units");
    // Isolate sparse mineral contours from sunset light.
    frame.reservedProjection=90;frame.scene.sunTop={};frame.scene.sunBottom={};
    frame.panorama[3]=0; // Probe-only height footprint, independent of artwork LOD.
    for(auto& knot:frame.regionalKnots)knot[3]=1;
    pixels=render();float seamPeak=0;
    for(UINT x=0;x<width;++x) {
        seamPeak=std::max(seamPeak,pixels[5*width+x][0]);
        check(luminance(pixels[5*width+x])<=.01001f,"mineral surface radiance has a bounded linear luminance");
    }
    check(seamPeak>.0001f,"eligible elevated slopes show resolved mineral height contours");
    // Sweep the production derivative filter with a continuous height ramp.
    // Assertions observe its light, without reproducing the coverage or
    // smoothstep expressions in a CPU implementation.
    std::array<float,10> footprintPeaks{};
    constexpr std::array<float,10> footprints{.08f,.10f,.12f,.14f,.16f,.18f,.20f,.219f,.22f,.24f};
    for(unsigned index=0;index<footprints.size();++index) {
        frame.panorama[3]=footprints[index];
        const auto filtered=render();
        for(UINT x=0;x<width;++x) {
            const float light=luminance(filtered[5*width+x]);
            check(std::isfinite(light)&&light>=0&&light<=.01001f,"intermediate mineral footprints keep finite bounded radiance");
            footprintPeaks[index]=std::max(footprintPeaks[index],light);
        }
        std::cout<<"MINERAL_FOOTPRINT footprint="<<footprints[index]<<" peak_luminance="<<footprintPeaks[index]<<'\n';
        if(index>0)check(footprintPeaks[index]<=footprintPeaks[index-1]+1e-5f,"mineral peaks decrease smoothly as detail becomes unresolved");
    }
    check(footprintPeaks[0]>.0001f&&footprintPeaks[1]>.5f*footprintPeaks[0],"partly resolved mineral detail remains visible near the start of suppression");
    check(footprintPeaks[2]<.8f*footprintPeaks[0]&&footprintPeaks[4]<.6f*footprintPeaks[2],"mineral suppression progressively dims intermediate footprints");
    check(footprintPeaks[6]>0&&footprintPeaks[6]<.1f*footprintPeaks[0],"nearly unresolved mineral detail fades to a faint residual before cutoff");
    check(footprintPeaks[8]<1e-7f&&footprintPeaks[9]==0,"mineral radiance vanishes at and beyond the derivative cutoff");
    frame.panorama[3]=4;const auto unresolved=render();
    for(UINT x=0;x<width;++x)check(unresolved[5*width+x][0]==0,"unresolved mineral contours emit no aliased light");
    frame.panorama[3]=0;
    frame.cityHorizontalOffset=0;const auto shoulder=render();
    for(UINT x=0;x<width;++x)check(shoulder[5*width+x][0]==0,"mineral contours protect the road and shoulders");
    frame.cityHorizontalOffset=20;
    frame.scene.cityLightPhase=.72f;
    const auto mineral=pixels;pixels=render();
    for(UINT x=0;x<width;++x)check(pixels[5*width+x]==mineral[5*width+x],"mineral positions do not drift with the atmosphere clock");
    for(auto& knot:frame.regionalKnots)knot[3]=0;pixels=render();
    for(UINT x=0;x<width;++x)check(pixels[5*width+x][0]==0,"ineligible regions have no mineral radiance");
    // Select a visibly eligible production field rather than forcing its knots.
    // Every height sample then represents the same world surface on both sides
    // of rollover: .25 cells of travel moves it .5 world units toward the camera.
    auto mineralParams=state.Current();
    mineralParams.cruise={127,.75};
    bool checkedMineralRollover=false;
    for(std::uint64_t seed=1;seed<=64&&!checkedMineralRollover;++seed) {
        mineralParams.terrainSeed=seed;
        frame=vaporcruise::render::PackSceneFrame(mineralParams,0,1,1280,720);
        frame.scene.sunTop={};frame.scene.sunBottom={};frame.cityHorizontalOffset=20;
        frame.panorama[3]=0;frame.reservedProjection=90;
        const auto before=render();float peak=0;
        for(UINT x=0;x<width;++x)peak=std::max(peak,luminance(before[5*width+x]));
        if(peak<=.0001f)continue;
        mineralParams.cruise={128,0};
        frame=vaporcruise::render::PackSceneFrame(mineralParams,0,1,1280,720);
        frame.scene.sunTop={};frame.scene.sunBottom={};frame.cityHorizontalOffset=20;
        frame.panorama[3]=0;frame.reservedProjection=89.5f;
        const auto after=render();float difference=0;
        for(UINT x=0;x<width;++x)for(UINT channel=0;channel<3;++channel)
            difference=std::max(difference,std::abs(before[5*width+x][channel]-after[5*width+x][channel]));
        check(difference<1e-5f,"visible mineral contours retain position and radiance at the same world surface through regional rollover");
        std::cout<<"MINERAL_ROLLOVER seed="<<seed<<" peak_luminance="<<peak<<" max_rgb_delta="<<difference<<'\n';
        checkedMineralRollover=true;
    }
    check(checkedMineralRollover,"rollover invariance checks a nonzero naturally eligible mineral contour");
    std::cout<<(failures?"FAIL":"PASS")<<": production HLSL regional luminance, world anchoring, support and terrain accents ("<<failures<<")\n";
    return failures?1:0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
