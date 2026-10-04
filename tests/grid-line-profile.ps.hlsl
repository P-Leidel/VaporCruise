// Exercise the shipped HLSL, including derivatives, rather than a CPU copy.
#define main TerrainMain
#include "terrain.ps.hlsl"
#undef main
float4 main(float4 pixel : SV_Position) : SV_Target {
    uint tile = (uint)pixel.x / 64;
    uint direction = tile & 3;
    float2 normals[4] = {float2(1,0),float2(0,1),
        float2(.707106781,.707106781),float2(.707106781,-.707106781)};
    float2 center = float2(tile*64+32.5,pixel.y < 32 ? 16.5 : 48.5);
    float distance = dot(pixel.xy-center,normals[direction]) + reservedProjection;
    float3 surface = float3(20,8,30);
    float2 cell = tile < 4 ? float2(distance/40,.5) : float2(.5,distance/40);
    if (pixel.y < 32) return float4(PlainGrid(cell,surface),0,1);
    TerrainInput input;
    input.position = float4(pixel.xy,0,1.0/30);
    input.surface = surface + float3((pixel.x-center.x)*.001,(pixel.y-center.y)*.001,0);
    input.material = float2(.5+distance/40,.5);
    input.folds = float3(1,1,0);
    if (pixel.y < 64) return TerrainMain(input,0);
    input.position.w = .1;
    input.surface = float3((pixel.x-256)*.000001,0,10+(pixel.y-68.5)*.001);
    input.material = float2(0,(pixel.x-.5)/64 * max(panorama.w,1) + cityHorizontalOffset);
    input.folds = 0;
    return TerrainMain(input,0);
}
