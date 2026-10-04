#include "scene-frame.hlsli"
#include "scene-panorama.hlsli"
#include "scene-output.hlsli"
struct PSInput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PSInput input) : SV_Target {
    return FinalOutput(PanoramaBackgroundColor(input.position.xy), input.position.xy);
}
