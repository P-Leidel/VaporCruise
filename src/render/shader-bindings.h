#pragma once
#include "../shaders/shader-bindings.def"
#include <d3dcommon.h>
#include <string_view>
namespace vaporcruise::render {
namespace bindings {
inline constexpr unsigned Frame=VC_FRAME_SLOT, Section=VC_SECTION_SLOT,
    Coverage=VC_COVERAGE_SLOT, Steady=VC_STEADY_SLOT, Animated=VC_ANIMATED_SLOT,
    Detail=VC_DETAIL_SLOT,
    PanoramaSampler=VC_PANORAMA_SAMPLER_SLOT;
}
inline bool ValidateShaderBinding(std::string_view name,unsigned slot,D3D_SHADER_INPUT_TYPE type) {
    if(type==D3D_SIT_CBUFFER) return (name=="FrameConstants" && slot==bindings::Frame) || (name=="TerrainDraw" && slot==bindings::Section);
    if(type==D3D_SIT_SAMPLER) return (name=="panoramaSampler" && slot==bindings::PanoramaSampler);
    if(type==D3D_SIT_TEXTURE) return (name=="panoramaCoverage" && slot==bindings::Coverage) || (name=="panoramaSteady" && slot==bindings::Steady) ||
        (name=="panoramaAnimated" && slot==bindings::Animated) || (name=="landscapeDetail" && slot==bindings::Detail);
    return false;
}
}
