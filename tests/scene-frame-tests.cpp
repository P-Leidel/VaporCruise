#include "render/scene-frame.h"
#include <cmath>
#include <iostream>
#include <cstring>
using namespace vaporcruise;
namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool Near(float a, float b) { return std::abs(a-b) < 1e-6f; }
}
int main() {
    scene::SceneParams p;
    p.palette = {{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16},
                 {17,18,19,20},{21,22,23,24},{25,26,27,28},{29,30,31,32},
                 {33,34,35,36},{37,38,39,40},{41,42,43,44},{45,46,47,48}};
    p.gridPhase = .25; p.cityLightPhase = .75;
    p.cruise = {31,.999};
    auto frame = render::PackSceneFrame(p, .35f, 1.5f, 1920, 1080);
    const auto projection = render::BuildFrameProjection(1920,1080);
    const auto cached = render::PackSceneFrame(p,.35f,1.5f,projection);
    Check(std::memcmp(&frame,&cached,sizeof(frame)) == 0, "cached resolution projection matches direct packing");
    const common::Color colors[] = {frame.scene.sky,frame.scene.ground,frame.scene.farRidge,
        frame.scene.nearRidge,frame.scene.grid,frame.scene.sunTop,frame.scene.sunBottom,
        frame.scene.haze,frame.scene.cityWarm,frame.scene.highlight,frame.scene.gridGlow,frame.scene.road};
    for (unsigned i=0;i<12;++i) {
        Check(colors[i].r == 1+i*4 && colors[i].g == 2+i*4 &&
              colors[i].b == 3+i*4 && colors[i].a == 4+i*4, "named color packing preserves order");
    }
    Check(frame.scene.gridPhase == .25f && frame.scene.cityLightPhase == .75f, "bounded phases packed");
    Check(frame.scene.hintOpacity == .35f && frame.scene.uiScale == 1.5f, "overlay values packed");
    Check(frame.resolution[0] == 1920 && frame.resolution[1] == 1080, "resolution packed");
    Check(Near(frame.cameraDepth[3],512.002f), "finite terrain reach data");
    Check(frame.camera[0] == 3.2f && frame.camera[1] == .8f && frame.camera[2] == .5f, "camera/material scale shared");
    Check(frame.panorama[0] == 0 && frame.panorama[1] == 0 && frame.panorama[2] == 0, "wide full-resolution panorama");
    Check(Near(frame.panoramaExtents[0],3) && Near(frame.panoramaExtents[1],.202f) &&
          Near(frame.panoramaExtents[2],.19f) && Near(frame.panoramaExtents[3],1.04f), "artwork projection extents");
    for (const auto aspect : {1.5f,1.f,.7f,.5625f}) {
        auto f = render::PackSceneFrame(p,0,1,aspect*1000,1000);
        Check(f.panorama[0] == scene::BackgroundLayout(aspect), "production uses tested aspect selection");
    }
    auto tiny = render::PackSceneFrame(p,0,1,240,240);
    Check(tiny.panorama[1] > 1 && tiny.panorama[2] > tiny.panorama[1], "minification LOD uses artwork dimensions");
    // A 256-row facet atlas spans .202 view units: at 240px height its
    // footprint is 5.28 texels/pixel, requiring mip 2.40, not the mask's 1.40.
    Check(tiny.panorama[3] > 2.39f && tiny.panorama[3] < 2.41f,
          "baked mountain detail selects its own mip at small resolutions");
    p.cruise = {32,0};
    Check(Near(render::PackSceneFrame(p,0,1,640,360).cameraDepth[3],576), "terrain reach resets at section boundary");
    for (const auto threshold : {1.5f,1.f,.7f}) {
        const float below = std::nextafter(threshold,0.f);
        const float above = std::nextafter(threshold,2.f);
        Check(render::BuildFrameProjection(below,1).panorama[0] == scene::BackgroundLayout(below) &&
              render::BuildFrameProjection(above,1).panorama[0] == scene::BackgroundLayout(above),
              "layout boundaries select correctly on both sides");
    }
    Check(scene::kSectionVertexCount == 4257 && scene::kTerrainSlotCount == 11 &&
          scene::kActiveTerrainSections == 10, "existing streaming geometry preserved");
    Check(sizeof(render::FrameConstantsGPU) == 592 &&
          offsetof(render::SceneConstants,gridGlow) == 160 &&
          offsetof(render::SceneConstants,road) == 176 &&
          offsetof(render::FrameConstantsGPU,reservedProjection) == 220 &&
          offsetof(render::FrameConstantsGPU,panoramaExtents) == 272, "GPU ABI layout");
    if (!failures) std::cout << "PASS: scene packing, camera, terrain edge and panorama contract\n";
    return failures ? 1 : 0;
}
