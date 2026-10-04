#ifndef CAMERA_PROJECTION
#define CAMERA_PROJECTION
#include "scene-frame.hlsli"
float4 ProjectCamera(float3 surface) {
    float z=surface.z;
    float n=cameraDepth.x, f=cameraDepth.y;
    return float4(2*camera.y*surface.x/(resolution.x/resolution.y),
        2*(camera.y*(surface.y-camera.x)+camera.w*z),
        f*z/(f-n)-n*f/(f-n),z);
}
// Pixel is SV_Position (pixel centers); callers mask rays above the horizon.
float2 UnprojectCameraGround(float2 pixel) {
    float2 uv=pixel/resolution;
    float below=uv.y-(.5-camera.w);
    float z=camera.y*camera.x/max(below,.0001);
    float x=(uv.x-.5)*(resolution.x/resolution.y)*z/camera.y;
    return float2(x,z);
}
#endif
