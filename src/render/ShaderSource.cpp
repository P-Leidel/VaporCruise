#include "render/ShaderSource.h"

#include <Windows.h>

#ifdef _DEBUG
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#else
// The fxc-generated headers declare `const BYTE g_...[]` arrays, so
// Windows.h (BYTE's home) must be included first.
#include "embedded-shaders.h"
#endif

namespace vaporcruise::render {

#ifdef _DEBUG

namespace {

bool CompileFromFile(const char* relativePath, const char* entryPoint, const char* target,
                      std::vector<uint8_t>& outBytecode) {
    std::filesystem::path sourceDirectory(VAPORCRUISE_SHADER_SOURCE_DIR);
    const DWORD length=GetEnvironmentVariableW(L"VAPORCRUISE_SHADER_SOURCE_OVERRIDE", nullptr, 0);
    if (length != 0) {
        std::vector<wchar_t> overridePath(length);
        const DWORD copied=GetEnvironmentVariableW(L"VAPORCRUISE_SHADER_SOURCE_OVERRIDE", overridePath.data(), length);
        if (copied == 0 || copied >= length) return false;
        sourceDirectory=overridePath.data();
    }
    const auto fullPath=sourceDirectory / relativePath;

    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(fullPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                     entryPoint, target, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                     &bytecode, &errors);
    if (FAILED(hr)) {
        // Preserve compiler details for hidden captures/fixtures. Main reports
        // the failed stage with a dialog only for ordinary interactive runs.
        const char* errorText = errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "(no compiler error text available)";
        OutputDebugStringA(errorText);
        fprintf(stderr, "VaporCruise: shader compile failed -- %s\n%s\n", relativePath, errorText);
        return false;
    }

    const uint8_t* begin = static_cast<const uint8_t*>(bytecode->GetBufferPointer());
    outBytecode.assign(begin, begin + bytecode->GetBufferSize());
    return true;
}

}  // namespace

#endif
bool LoadShaderBytecode(ShaderProgramId id,std::vector<std::uint8_t>& outBytecode) {
#ifdef _DEBUG
    for(const auto& program:ShaderPrograms()) if(program.id==id)
        return CompileFromFile(program.source,program.entry,program.profile,outBytecode);
#else
    switch(id) {
#define VC_PROGRAM(id,source,profile,entry,header,symbol,deps) case ShaderProgramId::id: outBytecode.assign(symbol,symbol+sizeof(symbol));return true;
#include "render/shader-programs.def"
#undef VC_PROGRAM
    }
#endif
    outBytecode.clear();return false;
}
bool LoadFullscreenVertexShaderBytecode(std::vector<uint8_t>& out) { return LoadShaderBytecode(ShaderProgramId::FullscreenVertex,out); }
bool LoadSceneCompositePixelShaderBytecode(std::vector<uint8_t>& out) { return LoadShaderBytecode(ShaderProgramId::SceneCompositePixel,out); }
bool LoadTerrainVertexShaderBytecode(std::vector<uint8_t>& out) { return LoadShaderBytecode(ShaderProgramId::TerrainVertex,out); }
bool LoadTerrainPixelShaderBytecode(std::vector<uint8_t>& out) { return LoadShaderBytecode(ShaderProgramId::TerrainPixel,out); }
} // namespace vaporcruise::render
