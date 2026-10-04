#pragma once
#include "scene/cruise-position.h"
#include <array>
#include <optional>
#include <iosfwd>
namespace vaporcruise::render {
struct CameraProjection {
    float height=3.2f, focalScale=.8f;
    double horizonFraction=1.0/3.0;
    float nearPlane=.1f, farPlane=640.f, cellSize=2.f, minimumTerrainReach=512.f;
};
inline constexpr CameraProjection DefaultCameraProjection() { return {}; }
std::array<double,3> ProjectWorld(const CameraProjection&, double width, double height, double x, double y, double z);
std::optional<std::array<double,2>> UnprojectGround(const CameraProjection&, double width, double height, double pixelX, double pixelY);
float SectionTranslation(std::int64_t section, scene::CruisePosition position);
void SerializeCamera(std::ostream&,const CameraProjection&);
}
