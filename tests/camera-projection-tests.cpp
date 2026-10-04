#include "render/camera-projection.h"
#include "scene/scene-geometry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vaporcruise;
int main() {
    int failures = 0;
    auto check = [&](bool ok) { if (!ok) ++failures; };
    const auto c = render::DefaultCameraProjection();
    for (auto size : {std::array<double,2>{1280,720}, {480,320}, {720,1280}, {2560,1080}})
        for (double z : {.1, 2., 400., 480., 512., 640.}) for (double x : {-100., 0., 7.}) {
            auto p = render::ProjectWorld(c,size[0],size[1],x,0,z);
            auto q = render::UnprojectGround(c,size[0],size[1],p[0],p[1]);
            check(q && std::abs((*q)[0]-x)<1e-6 && std::abs((*q)[1]-z)<1e-6);
            check(std::abs(p[0]-(size[0]*.5+size[1]*c.focalScale*x/z-.5))<1e-6);
            check(std::abs(p[1]-(size[1]*c.horizonFraction+size[1]*c.focalScale*c.height/z-.5))<1e-6);
        }
    check(!render::UnprojectGround(c,1280,720,0,720*c.horizonFraction-.5));
    check(!render::UnprojectGround(c,1280,720,0,0));
    for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        try { (void)render::ProjectWorld(c,invalid,720,0,0,1); check(false); } catch (const std::invalid_argument&) {}
    }
    for (std::int64_t cell : {31LL,32LL,1087LL,1088LL,36028797018963971LL}) for(double phase : {0.,.25,.9999}) {
        const auto section = cell / scene::kSectionDepthCells + 2;
        const float expected = static_cast<float>(section*scene::kSectionDepthCells-cell)*scene::kTerrainCellSize-static_cast<float>(phase*scene::kTerrainCellSize);
        check(render::SectionTranslation(section,{cell,phase})==expected);
    }
    check(c.height==3.2f && c.focalScale==.8f && c.nearPlane==.1f && c.farPlane==640.f && c.cellSize==2.f && c.minimumTerrainReach==512.f);
    auto custom=c; custom.height=5; custom.focalScale=.6f; custom.horizonFraction=.4f;
    auto p=render::ProjectWorld(custom,500,900,3,0,40);
    auto q=render::UnprojectGround(custom,500,900,p[0],p[1]); check(q && std::abs((*q)[1]-40)<1e-6);
    std::cout << (failures ? "FAIL" : "PASS") << ": camera round trips, custom data, invalid inputs, integer section translation (" << failures << ")\n";
    return failures ? 1 : 0;
}
