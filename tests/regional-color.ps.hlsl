// Probe the production grid/background helper, retaining the old behavior for RED.
#define main TerrainMain
#include "terrain.ps.hlsl"
#undef main
float4 main(float4 pixel : SV_Position) : SV_Target {
    float3 surface=float3((pixel.x-128.5)*2.0,12,reservedProjection);
    uint mode=(uint)pixel.y;
#ifdef SCENE_TERRAIN_ACCENTS
    // Evaluate derivatives on all lanes with one continuous height ramp.
    float3 accentSurface=float3(cityHorizontalOffset,pixel.x*max(panorama.w,.05),reservedProjection);
    float3 accent=TerrainSurfaceAccent(accentSurface,normalize(mode==5?float3(.8,.6,-.1):float3(.1,.9,-.35)));
    if(mode==5||mode==6) return float4(accent,1);
#else
    if(mode==5||mode==6) return float4(0,0,0,1);
#endif
#ifdef SCENE_REGIONAL
    if(mode==0) return float4(RegionalTint(grid.rgb,surface,regionalControl.z),1);
    if(mode==1) return float4(RegionalTint(gridGlow.rgb,surface,regionalControl.z),1);
    if(mode==2) return float4(GridRadiance(float2(.6,.4),60,surface),1);
    if(mode==3) return RegionalSample(surface);
    return float4(RegionalInfluence(surface),0,0,1);
#else
    if(mode==0) return float4(grid.rgb,1);
    if(mode==1) return float4(gridGlow.rgb,1);
    if(mode==2) return float4(GridRadiance(float2(.6,.4),60),1);
    return float4(0,0,0,1);
#endif
}
