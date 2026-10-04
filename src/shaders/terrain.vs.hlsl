#include "shader-bindings.hlsli"
#include "camera-projection.hlsli"
#include "terrain-settling.hlsli"
cbuffer TerrainDraw : register(VC_SECTION_REGISTER) { float4 sectionTranslation; };
struct TerrainInput { float3 position : POSITION; float2 folds : TEXCOORD0; };
struct TerrainOutput {
    float4 position : SV_Position;
    float3 surface : TEXCOORD0;
    float2 material : TEXCOORD1;
    nointerpolation float3 folds : TEXCOORD2;
};
TerrainOutput main(TerrainInput input) {
    TerrainOutput output;
    float3 surface = input.position + float3(0, 0, sectionTranslation.x);
    float z = surface.z;
    // Material fading cannot hide opaque peaks against the panorama. Settle
    // the outer terrain into the plain before the shortest active mesh extent
    // (512 units), so recycled sections enter below the horizon.
    surface.y *= TerrainHeightFade(surface.x,z);
    output.position = ProjectCamera(surface);
    output.surface = surface;
    output.material = input.position.xz * camera.z;
    // The first vertices are a and b respectively. Their material sum lets
    // the pixel shader identify b-c without wrapping at integer endpoints.
    output.folds = float3(input.folds, output.material.x + output.material.y);
    return output;
}
