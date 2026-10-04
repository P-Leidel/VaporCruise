#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace vaporcruise::render {

enum class ShaderProgramId {
#define VC_PROGRAM(id,source,profile,entry,header,symbol,deps) id,
#include "render/shader-programs.def"
#undef VC_PROGRAM
};
struct ShaderProgramDescription { ShaderProgramId id; const char* source; const char* profile; const char* entry; const char* dependencies; };
inline constexpr auto ShaderPrograms() {
    return std::array{
#define VC_PROGRAM(id,source,profile,entry,header,symbol,deps) ShaderProgramDescription{ShaderProgramId::id,source,profile,entry,deps},
#include "render/shader-programs.def"
#undef VC_PROGRAM
    };
}
bool LoadShaderBytecode(ShaderProgramId id,std::vector<std::uint8_t>& bytecode);

// Loads the compiled bytecode for the four shaders used by VaporCruise's
// terrain and background/composition passes. Debug builds compile the loose .hlsl in
// src/shaders/ at startup via D3DCompileFromFile (edit-and-relaunch
// iteration). Debug diagnostics may use a process-local disposable source
// directory via VAPORCRUISE_SHADER_SOURCE_OVERRIDE; release builds return bytecode embedded at build time by
// fxc/CMake -- no shipped shader source, no runtime compiler dependency.
bool LoadFullscreenVertexShaderBytecode(std::vector<uint8_t>& outBytecode);
bool LoadSceneCompositePixelShaderBytecode(std::vector<uint8_t>& outBytecode);
bool LoadTerrainVertexShaderBytecode(std::vector<uint8_t>& outBytecode);
bool LoadTerrainPixelShaderBytecode(std::vector<uint8_t>& outBytecode);

}  // namespace vaporcruise::render
