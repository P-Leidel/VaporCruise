#include "render/terrain-renderer.h"
#include "render/scene-frame.h"
#include <iostream>
#include <string_view>
using namespace vaporcruise;
using Microsoft::WRL::ComPtr;
struct UploadProbe {
    unsigned terrain=0; bool reject=true;
    static HRESULT Hook(const wchar_t* stage,void* data) {
        auto& p=*static_cast<UploadProbe*>(data);
        if(std::wstring_view(stage)==L"terrain vertex upload") {++p.terrain;if(p.reject)return E_FAIL;}
        return S_OK;
    }
};
int main() {
    int failures=0;auto check=[&](bool ok,const char* msg){if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 2;
    render::TerrainRenderer renderer;
    check(renderer.Initialize(device.Get(),context.Get()),"initialize fixed resources");
    auto slots=std::make_unique<std::array<scene::TerrainSectionSlot,scene::kTerrainSlotCount>>();
    auto& s=(*slots)[0];s.complete=s.active=true;s.revision=1;
    check(scene::BuildTerrainRows(123,0,0,scene::kTerrainRows,s.vertices),"prepare CPU terrain");
    scene::SceneFrameSnapshot frame{};frame.terrainSlots=*slots;
    UploadProbe probe;renderer.SetFailureHook(UploadProbe::Hook,&probe);
    check(!renderer.Prepare(frame),"terrain Map failure leaves revision pending");
    check(renderer.LastResult()==E_FAIL && renderer.LastError().find(L"vertex upload")!=std::wstring::npos,"terrain upload retains failure diagnostic");
    check(renderer.UploadsForTesting()[0]==0,"failed Map does not count as upload");
    renderer.Draw(frame,nullptr);check(FAILED(renderer.LastResult()),"pending revision cannot draw");
    probe.reject=false;
    check(renderer.Prepare(frame),"retry uploads pending transaction");
    check(probe.terrain==2 && renderer.UploadsForTesting()[0]==1,"successful retry commits exactly once");
    check(renderer.Prepare(frame) && probe.terrain==2,"committed revision skips upload");
    ++s.revision;
    check(renderer.Prepare(frame) && probe.terrain==3,"changed terrain revision uploads");
    s.complete=s.active=false;++s.revision;
    check(renderer.Prepare(frame) && probe.terrain==3,"incomplete inactive slot does not upload");
    s.active=true;
    check(!renderer.Prepare(frame) && renderer.LastResult()==E_INVALIDARG && probe.terrain==3,"incomplete active slot rejects snapshot before upload");
    s.complete=true;
    check(renderer.Prepare(frame) && probe.terrain==4,"completed recycled slot uploads");
    render::FrameConstantsGPU constants{};D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{&constants,0,0};ComPtr<ID3D11Buffer> cb;
    check(SUCCEEDED(device->CreateBuffer(&bd,&data,&cb)),"create frame constants");
    context->ClearState();renderer.Draw(frame,cb.Get());check(SUCCEEDED(renderer.LastResult()),"terrain binds after cleared state");
    UINT stride=0,offset=0;ComPtr<ID3D11Buffer> vb;context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
    check(vb && stride==sizeof(scene::TerrainVertex) && offset==0,"terrain draw binds native vertex stride");
    D3D11_BUFFER_DESC resource{};if(vb)vb->GetDesc(&resource);
    check(resource.ByteWidth==scene::kSectionVertexCount*sizeof(scene::TerrainVertex) && resource.Usage==D3D11_USAGE_DYNAMIC,"terrain buffer reserves fixed capacity");
    ComPtr<ID3D11Buffer> actual;context->VSGetConstantBuffers(0,1,&actual);check(actual.Get()==cb.Get(),"terrain VS binds frame");
    actual.Reset();context->PSGetConstantBuffers(0,1,&actual);check(actual.Get()==cb.Get(),"terrain PS binds frame");
    actual.Reset();context->VSGetConstantBuffers(1,1,&actual);check(actual!=nullptr,"terrain VS binds section");
    ComPtr<ID3D11DepthStencilState> depth;UINT reference;context->OMGetDepthStencilState(&depth,&reference);
    D3D11_DEPTH_STENCIL_DESC dd{};if(depth)depth->GetDesc(&dd);check(depth && dd.DepthEnable && dd.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL,"terrain depth writes enabled");
    renderer.Release();check(renderer.Initialize(device.Get(),context.Get()),"recovery recreates fixed resources");
    check(renderer.UploadsForTesting()[0]==0,"release resets upload diagnostics");
    renderer.SetFailureHook(UploadProbe::Hook,&probe);probe.terrain=0;
    check(renderer.Prepare(frame) && probe.terrain==1,"recovery uploads unchanged CPU revision");
    for(size_t i=1;i<slots->size();++i)(*slots)[i].complete=true;
    check(renderer.Prepare(frame) && probe.terrain==scene::kTerrainSlotCount,"all eleven physical terrain buffers upload independently");
    check(renderer.Prepare(frame) && probe.terrain==scene::kTerrainSlotCount,"unchanged complete window performs no uploads");
    std::cout<<(failures?"FAIL":"PASS")<<": real WARP terrain upload/draw/recovery ("<<failures<<")\n";
    return failures?1:0;
}
