#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <bit>
namespace vaporcruise::scene {
inline constexpr unsigned kBackgroundWidth = 1024, kBackgroundHeight = 128;
inline constexpr unsigned kBackgroundMipLevels = std::bit_width(kBackgroundWidth);
inline constexpr unsigned kBackgroundLayouts = 4;
inline constexpr unsigned kLandscapeDetailHeight = 256;
inline constexpr std::size_t kBackgroundByteBudget = 9 * 1024 * 1024;
inline constexpr float kBackgroundHalfWidth = 1.5f, kBackgroundBottom = -.012f, kBackgroundTop = .19f;
inline constexpr float kCityHalfWidth = .52f;
struct BackgroundMip {
    unsigned width, height, rowPitch;
    std::vector<std::uint8_t> pixels;
};
struct BackgroundMask {
    unsigned channels;
    std::vector<std::vector<BackgroundMip>> slices;
};
struct BackgroundArtwork {
    BackgroundMask coverage, steadyEmission, animatedEmission;
    std::vector<BackgroundMip> landscapeDetail;
    std::size_t GpuBytes() const;
};
struct BackgroundBuilding {
    float center, width, height;
    unsigned crown;
    bool landmark;
};
unsigned BackgroundLayout(float aspect);
float BackgroundCityHorizontalOffset(float aspect);
std::array<BackgroundBuilding, 36> BackgroundBuildings(unsigned layout);
bool IsValidBackgroundArtwork(const BackgroundArtwork&);
const BackgroundArtwork& GetBackgroundArtwork();
} // namespace vaporcruise::scene
