#include "shader-bindings.hlsli"
#ifndef SCENE_FRAME
#define SCENE_FRAME
// Screen y = .5 - camera.w: one third down, two thirds of ground below.

cbuffer FrameConstants : register(VC_FRAME_REGISTER) {
    float4 sky, ground, farRidge, nearRidge;
    float4 grid, sunTop, sunBottom, haze;
    float4 cityWarm, highlight;
    float4 gridGlow, road;
    float gridPhase, hintOpacity, uiScale, cityLightPhase;
    float2 resolution;
    float cityHorizontalOffset, reservedProjection;
    float4 camera;
    float4 panorama;
    float4 cameraDepth;
    float4 panoramaExtents;
    float4 regionalControl;
    float4 regionalHue[3];
    float4 regionalKnots[15];
};
#endif
