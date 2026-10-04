#ifndef TERRAIN_SETTLING
#define TERRAIN_SETTLING
#include "scene-frame.hlsli"
float TerrainHeightFade(float x,float z) {
    return (1 - smoothstep(cameraDepth.z*(400.0/512.0), cameraDepth.z*(480.0/512.0), z))
         * (1 - smoothstep(108, 126, abs(x)));
}
#endif
