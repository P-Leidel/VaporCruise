#pragma once
#include "scene/SceneState.h"
#include "scene/scene-geometry.h"
#include "scene/background-artwork.h"
#include "scene/regional-color.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <array>
namespace vaporcruise::render {
// CPU/HLSL ABI. All scene translation happens in PackSceneFrame below.
struct SceneConstants {
    common::Color sky, ground, farRidge, nearRidge;
    common::Color grid, sunTop, sunBottom, haze;
    common::Color cityWarm, highlight;
    common::Color gridGlow, road;
    float gridPhase, hintOpacity, uiScale, cityLightPhase;
};
struct alignas(16) FrameConstantsGPU {
    SceneConstants scene;
    float resolution[2];
    float cityHorizontalOffset, reservedProjection;
    float camera[4]; // Height, focal scale, material cells per world unit, clip horizon.
    float panorama[4]; // Layout, mountain LOD, city LOD, baked-detail LOD.
    float cameraDepth[4]; // Near, far, minimum reach, actual reach.
    float panoramaExtents[4]; // Mountain width, height, top, city width.
    float regionalControl[4];
    float regionalHue[3][4]; // Unit linear luminance base/shift/mineral targets.
    float regionalKnots[15][4];
};
static_assert(sizeof(common::Color) == 16);
static_assert(sizeof(SceneConstants) == 208);
static_assert(offsetof(SceneConstants, cityWarm) == 128);
static_assert(offsetof(SceneConstants, highlight) == 144);
static_assert(offsetof(SceneConstants, gridGlow) == 160);
static_assert(offsetof(SceneConstants, road) == 176);
static_assert(offsetof(SceneConstants, gridPhase) == 192);
static_assert(offsetof(SceneConstants, cityLightPhase) == 204);
static_assert(offsetof(FrameConstantsGPU, resolution) == 208);
static_assert(offsetof(FrameConstantsGPU, cityHorizontalOffset) == 216);
static_assert(offsetof(FrameConstantsGPU, reservedProjection) == 220);
static_assert(offsetof(FrameConstantsGPU, camera) == 224);
static_assert(offsetof(FrameConstantsGPU, panorama) == 240);
static_assert(offsetof(FrameConstantsGPU, cameraDepth) == 256);
static_assert(offsetof(FrameConstantsGPU, panoramaExtents) == 272);
static_assert(offsetof(FrameConstantsGPU,regionalControl)==288);
static_assert(offsetof(FrameConstantsGPU,regionalHue)==304);
static_assert(offsetof(FrameConstantsGPU,regionalKnots)==352);
static_assert(sizeof(FrameConstantsGPU) == 592);
// Resolution-dependent values are rebuilt only at initialization/resize.
struct FrameProjection {
    std::array<float,2> resolution{};
    float cityHorizontalOffset = 0;
    std::array<float,4> panorama{}, panoramaExtents{};
};
inline FrameProjection BuildFrameProjection(float width, float height) {
    FrameProjection projection;
    projection.resolution = {width,height};
    projection.cityHorizontalOffset = scene::BackgroundCityHorizontalOffset(width / height);
    const float mountainWidth = 2 * scene::kBackgroundHalfWidth;
    const float panoramaHeight = scene::kBackgroundTop - scene::kBackgroundBottom;
    const float cityWidth = 2 * scene::kCityHalfWidth;
    projection.panorama[0] = static_cast<float>(scene::BackgroundLayout(width / height));
    projection.panorama[1] = std::max(0.f,std::log2(std::max(scene::kBackgroundWidth / mountainWidth,
                                               scene::kBackgroundHeight / panoramaHeight) / height));
    projection.panorama[2] = std::max(0.f,std::log2(std::max(scene::kBackgroundWidth / cityWidth,
                                               scene::kBackgroundHeight / panoramaHeight) / height));
    projection.panorama[3] = std::max(0.f,std::log2(std::max(scene::kBackgroundWidth / mountainWidth,
                                               scene::kLandscapeDetailHeight / panoramaHeight) / height));
    projection.panoramaExtents = {mountainWidth,panoramaHeight,scene::kBackgroundTop,cityWidth};
    return projection;
}
inline FrameConstantsGPU PackSceneFrame(const scene::SceneParams& params, float hintOpacity,
                                       float uiScale, const FrameProjection& projection) {
    FrameConstantsGPU frame{};
    auto& packed = frame.scene;
    const auto& p = params.palette;
    packed.sky = p.sky; packed.ground = p.ground;
    packed.farRidge = p.farRidge; packed.nearRidge = p.nearRidge;
    packed.grid = p.grid; packed.sunTop = p.sunTop; packed.sunBottom = p.sunBottom;
    packed.haze = p.haze; packed.cityWarm = p.cityWarm; packed.highlight = p.highlight;
    packed.gridGlow = p.gridGlow; packed.road = p.road;
    packed.gridPhase = static_cast<float>(params.gridPhase);
    packed.cityLightPhase = static_cast<float>(params.cityLightPhase);
    packed.hintOpacity = hintOpacity; packed.uiScale = uiScale;
    std::copy(projection.resolution.begin(),projection.resolution.end(),frame.resolution);
    frame.cityHorizontalOffset = projection.cityHorizontalOffset;
    constexpr auto camera = DefaultCameraProjection();
    frame.camera[0] = camera.height; frame.camera[1] = camera.focalScale;
    frame.camera[2] = 1.0f / camera.cellSize;
    frame.camera[3] = static_cast<float>(.5 - camera.horizonFraction);
    frame.cameraDepth[0] = camera.nearPlane; frame.cameraDepth[1] = camera.farPlane;
    frame.cameraDepth[2] = camera.minimumTerrainReach;
    frame.cameraDepth[3] = static_cast<float>(scene::FarGroundDistance(params.cruise));
    std::copy(projection.panorama.begin(),projection.panorama.end(),frame.panorama);
    std::copy(projection.panoramaExtents.begin(),projection.panoramaExtents.end(),frame.panoramaExtents);
    const auto regional=scene::BuildRegionalColorField(params.terrainSeed,params.cruise);
    std::copy(regional.control.begin(),regional.control.end(),frame.regionalControl);
    for(unsigned i=0;i<15;++i)std::copy(regional.knots[i].begin(),regional.knots[i].end(),frame.regionalKnots[i]);
    const common::Color hues[]={p.regionBase,p.regionShift,p.regionMineral};
    for(unsigned i=0;i<3;++i) {
        const float luminance=.2126f*hues[i].r+.7152f*hues[i].g+.0722f*hues[i].b;
        const float inverse=luminance>0?1.f/luminance:0;
        frame.regionalHue[i][0]=luminance>0?hues[i].r*inverse:1;
        frame.regionalHue[i][1]=luminance>0?hues[i].g*inverse:1;
        frame.regionalHue[i][2]=luminance>0?hues[i].b*inverse:1;
    }
    return frame;
}
inline FrameConstantsGPU PackSceneFrame(const scene::SceneParams& params, float hintOpacity,
                                       float uiScale, float width, float height) {
    return PackSceneFrame(params,hintOpacity,uiScale,BuildFrameProjection(width,height));
}
}
