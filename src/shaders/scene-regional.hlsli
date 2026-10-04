#ifndef SCENE_REGIONAL
#define SCENE_REGIONAL
#include "scene-frame.hlsli"
// Quintic interpolation keeps the value and both first/second derivatives
// continuous when a full-address CPU knot window advances by one region.
float2 RegionalBlend(float2 t) { return t*t*t*(t*(t*6-15)+10); }
float RegionalInfluence(float3 surface) {
    return (1-smoothstep(480,700,surface.z))
        * (1-smoothstep(200,250,abs(surface.x)));
}
// xyz: convex hue weights; w: mineral eligibility. Influence is separate so
// callers can fade their own accents without treating a faded weight as a hue.
float4 RegionalSample(float3 surface) {
    float2 coordinate=(surface.xz*camera.z+float2(0,regionalControl.x))*regionalControl.y;
    coordinate=clamp(coordinate+float2(1,0),float2(0,0),float2(1.999999,3.999999));
    uint2 cell=(uint2)floor(coordinate);
    float2 t=RegionalBlend(frac(coordinate));
    uint index=cell.y*3+cell.x;
    return lerp(lerp(regionalKnots[index],regionalKnots[index+1],t.x),
        lerp(regionalKnots[index+3],regionalKnots[index+4],t.x),t.y);
}
float3 RegionalHue(float3 weights) {
    return regionalHue[0].rgb*weights.x+regionalHue[1].rgb*weights.y+regionalHue[2].rgb*weights.z;
}
float3 RegionalTint(float3 base,float3 surface,float strength) {
    float luminance=dot(base,float3(.2126,.7152,.0722));
    float3 hue=RegionalHue(RegionalSample(surface).xyz);
    return lerp(base,luminance*hue,strength*RegionalInfluence(surface));
}
#endif
