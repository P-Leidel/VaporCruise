#include "render/shader-bindings.h"
#include "render/ShaderSource.h"
#include "render/scene-frame.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <iostream>
#include <set>
#include <string>
using namespace vaporcruise::render;
int main() {
    int failures=0;
    auto check=[&](bool ok) { if(!ok) ++failures; };
    check(bindings::Frame==0 && bindings::Section==1 && bindings::Detail==6);
    std::set<std::string> sources;
    for(const auto& program:ShaderPrograms()) {
        check(sources.insert(program.source).second);
        std::vector<std::uint8_t> code;
        check(LoadShaderBytecode(program.id,code));
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflect;
        if(FAILED(D3DReflect(code.data(),code.size(),IID_PPV_ARGS(&reflect)))) { check(false);continue; }
        D3D11_SHADER_DESC desc{};reflect->GetDesc(&desc);
        for(UINT i=0;i<desc.BoundResources;++i) {
            D3D11_SHADER_INPUT_BIND_DESC input{};reflect->GetResourceBindingDesc(i,&input);
            check(ValidateShaderBinding(input.Name,input.BindPoint,input.Type));
        }
        auto* cb=reflect->GetConstantBufferByName("FrameConstants");
        if (std::string(program.source)=="terrain.vs.hlsl") {
            auto* section=reflect->GetConstantBufferByName("TerrainDraw");
            D3D11_SHADER_BUFFER_DESC sectionDesc{};
            D3D11_SHADER_VARIABLE_DESC translationDesc{};
            check(SUCCEEDED(section->GetDesc(&sectionDesc)) && sectionDesc.Size==16);
            check(SUCCEEDED(section->GetVariableByName("sectionTranslation")->GetDesc(&translationDesc)) && translationDesc.StartOffset==0 && translationDesc.Size==16);
        }
        D3D11_SHADER_BUFFER_DESC bd{};
        if(SUCCEEDED(cb->GetDesc(&bd))) {
            check(bd.Size==sizeof(FrameConstantsGPU));
            for(auto field:{std::pair{"gridGlow",offsetof(FrameConstantsGPU,scene)+offsetof(SceneConstants,gridGlow)},
                {"road",offsetof(FrameConstantsGPU,scene)+offsetof(SceneConstants,road)},
                {"resolution",offsetof(FrameConstantsGPU,resolution)},
                {"camera",offsetof(FrameConstantsGPU,camera)},{"cameraDepth",offsetof(FrameConstantsGPU,cameraDepth)},
                {"panoramaExtents",offsetof(FrameConstantsGPU,panoramaExtents)},
                {"regionalControl",offsetof(FrameConstantsGPU,regionalControl)},
                {"regionalHue",offsetof(FrameConstantsGPU,regionalHue)},
                {"regionalKnots",offsetof(FrameConstantsGPU,regionalKnots)}}) {
                D3D11_SHADER_VARIABLE_DESC vd{};
                check(SUCCEEDED(cb->GetVariableByName(field.first)->GetDesc(&vd)) && vd.StartOffset==field.second);
            }
        }
    }
    check(sources.contains("terrain.vs.hlsl") && sources.contains("terrain.ps.hlsl"));
    // Compile disposable valid and invalid programs, then validate reflected declarations.
    for(const auto& source : {std::string("Texture2D<float4> landscapeDetail : register(t")+std::to_string(bindings::Detail)+"); float4 main(float4 p:SV_Position):SV_Target {return landscapeDetail.Load(int3(0,0,0));}",
        std::string("Texture2D<float4> landscapeDetail : register(t4); float4 main(float4 p:SV_Position):SV_Target {return landscapeDetail.Load(int3(0,0,0));}")}) {
        Microsoft::WRL::ComPtr<ID3DBlob> code,error;
        if(FAILED(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error))) {check(false);continue;}
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        check(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflection))));
        D3D11_SHADER_INPUT_BIND_DESC input{};reflection->GetResourceBindingDesc(0,&input);
        check(ValidateShaderBinding(input.Name,input.BindPoint,input.Type)==(input.BindPoint==bindings::Detail));
    }
    check(!ValidateShaderBinding("landscapeDetail",4,D3D_SIT_TEXTURE));
    check(!ValidateShaderBinding("FrameConstants",1,D3D_SIT_CBUFFER));
    std::cout << (failures?"FAIL":"PASS") << ": program catalog and reflected bindings/layout ("<<failures<<")\n";
    return failures?1:0;
}
