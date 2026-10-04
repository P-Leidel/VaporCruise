#include "render/camera-projection.h"
#include "scene/scene-geometry.h"
#include <cmath>
#include <stdexcept>
#include <ostream>
#include <iomanip>
#include <locale>
namespace vaporcruise::render {
namespace {
void Validate(const CameraProjection& c, double w, double h) {
    if (!std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0 ||
        !std::isfinite(c.height) || c.height<=0 || !std::isfinite(c.focalScale) || c.focalScale<=0 ||
        !std::isfinite(c.horizonFraction) || c.horizonFraction<=0 || c.horizonFraction>=1 ||
        !std::isfinite(c.nearPlane) || !std::isfinite(c.farPlane) || c.nearPlane<=0 || c.farPlane<=c.nearPlane ||
        !std::isfinite(c.cellSize) || c.cellSize<=0 || !std::isfinite(c.minimumTerrainReach) || c.minimumTerrainReach<=0)
        throw std::invalid_argument("Invalid camera projection");
}
}
std::array<double,3> ProjectWorld(const CameraProjection& c,double w,double h,double x,double y,double z) {
    Validate(c,w,h);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z<=0) throw std::invalid_argument("Invalid world point");
    const double n=c.nearPlane, f=c.farPlane;
    return {w*.5+h*c.focalScale*x/z-.5,h*c.horizonFraction+h*c.focalScale*(c.height-y)/z-.5,
        f/(f-n)-n*f/((f-n)*z)};
}
std::optional<std::array<double,2>> UnprojectGround(const CameraProjection& c,double w,double h,double px,double py) {
    Validate(c,w,h);
    if (!std::isfinite(px) || !std::isfinite(py)) throw std::invalid_argument("Invalid pixel");
    const double below=(py+.5)/h-c.horizonFraction;
    if (below<=0) return std::nullopt;
    const double z=static_cast<double>(c.focalScale)*c.height/below;
    return std::array<double,2>{(px+.5-w*.5)*z/(h*c.focalScale),z};
}
float SectionTranslation(std::int64_t section, scene::CruisePosition p) {
    // Internal generated section indices are bounded by CruisePosition's range.
    return static_cast<float>(section*scene::kSectionDepthCells-p.cell)*scene::kTerrainCellSize-
        static_cast<float>(p.phase*scene::kTerrainCellSize);
}
void SerializeCamera(std::ostream& out,const CameraProjection& c) {
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"height\":" << c.height << ",\"focal_scale\":" << c.focalScale
        << ",\"horizon_fraction\":" << c.horizonFraction << ",\"near\":" << c.nearPlane
        << ",\"far\":" << c.farPlane << ",\"cell_size\":" << c.cellSize
        << ",\"minimum_terrain_reach\":" << c.minimumTerrainReach << '}';
}
}
