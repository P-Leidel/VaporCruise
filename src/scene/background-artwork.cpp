#include "scene/background-artwork.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace vaporcruise::scene {
namespace {
constexpr unsigned kSamples = 4;
constexpr float kVerticalExtent = kBackgroundTop - kBackgroundBottom;
std::uint32_t Mix(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU;
    return value ^ (value >> 16);
}
BackgroundMip Empty(unsigned channels) {
    return {kBackgroundWidth, kBackgroundHeight, kBackgroundWidth * channels,
            std::vector<std::uint8_t>(kBackgroundWidth * kBackgroundHeight * channels)};
}
std::vector<BackgroundMip> Mips(BackgroundMip base, unsigned channels) {
    std::vector<BackgroundMip> result;
    result.push_back(std::move(base));
    while (result.back().width > 1 || result.back().height > 1) {
        const auto& previous = result.back();
        const unsigned width = std::max(1U, previous.width / 2), height = std::max(1U, previous.height / 2);
        BackgroundMip next{width, height, width * channels, std::vector<std::uint8_t>(width*height*channels)};
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
            for (unsigned channel = 0; channel < channels; ++channel) {
                unsigned sum = 0, count = 0;
                for (unsigned dy = 0; dy < 2 && y*2+dy < previous.height; ++dy)
                    for (unsigned dx = 0; dx < 2 && x*2+dx < previous.width; ++dx) {
                        sum += previous.pixels[(y*2+dy)*previous.rowPitch + (x*2+dx)*channels+channel]; ++count;
                    }
                next.pixels[y*next.rowPitch+x*channels+channel] = static_cast<std::uint8_t>((sum+count/2)/count);
            }
        result.push_back(std::move(next));
    }
    return result;
}
float MountainHeight(float x, unsigned layer) {
    // Original angular contours, authored in aspect-correct horizon coordinates.
    constexpr std::array<std::pair<float,float>, 23> points{{
        {-1.5f,.065f},{-1.34f,.095f},{-1.17f,.083f},{-1.02f,.112f},
        {-.88f,.074f},{-.73f,.099f},{-.58f,.048f},{-.45f,.08f},
        {-.31f,.034f},{-.20f,.024f},{-.09f,.019f},{0.f,.016f},
        {.12f,.023f},{.24f,.03f},{.37f,.048f},{.49f,.078f},
        {.64f,.063f},{.79f,.105f},{.94f,.072f},{1.08f,.098f},
        {1.22f,.067f},{1.36f,.104f},{1.5f,.062f}}};
    const float shift = layer == 1 ? .045f : layer == 2 ? -.06f : 0;
    x = std::clamp(x + shift, -1.5f, 1.5f);
    for (std::size_t i = 1; i < points.size(); ++i) if (x <= points[i].first) {
        const auto [xa, ya] = points[i-1]; const auto [xb, yb] = points[i];
        const float value = ya + (yb-ya)*(x-xa)/(xb-xa);
        // Smaller angular shoulders break up the long original triangles.
        // A central valley leaves the skyline and sunset legible.
        const float t = (x-xa)/(xb-xa);
        const float shoulder = std::max(0.f, 1 - std::abs(t-.38f)*4.4f);
        const float notch = std::max(0.f, 1 - std::abs(t-.73f)*7.0f);
        const float detail = (shoulder * .012f - notch * .007f) * std::min(1.f, std::abs(x)*3);
        return (value + detail) * (layer == 0 ? 1 : layer == 1 ? .70f : .39f);
    }
    return 0;
}
BackgroundMip Mountains() {
    auto coverage = Empty(4);
    for (unsigned x = 0; x < kBackgroundWidth; ++x) {
        float heights[3][kSamples]{};
        for (unsigned layer = 0; layer < 3; ++layer) for (unsigned sx = 0; sx < kSamples; ++sx)
            heights[layer][sx] = MountainHeight(((x+(sx+.5f)/kSamples)/kBackgroundWidth*2-1)*kBackgroundHalfWidth, layer);
        for (unsigned y = 0; y < kBackgroundHeight; ++y) for (unsigned layer = 0; layer < 3; ++layer) {
            unsigned inside = 0;
            for (unsigned sy = 0; sy < kSamples; ++sy) {
                const float above = kBackgroundTop - (y+(sy+.5f)/kSamples)/kBackgroundHeight*kVerticalExtent;
                for (unsigned sx = 0; sx < kSamples; ++sx) inside += above < heights[layer][sx];
            }
            coverage.pixels[y*coverage.rowPitch+x*4+layer] = static_cast<std::uint8_t>(inside*255/(kSamples*kSamples));
        }
    }
    return coverage;
}
float Noise(float x, float y) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    float u = x-ix, v = y-iy;
    u = u*u*(3-2*u); v = v*v*(3-2*v);
    const auto at = [](int a, int b) {
        return static_cast<float>(Mix(static_cast<std::uint32_t>(a)*73856093U ^
            static_cast<std::uint32_t>(b)*19349663U) & 65535) / 65535.f;
    };
    return std::lerp(std::lerp(at(ix,iy), at(ix+1,iy),u),
                     std::lerp(at(ix,iy+1), at(ix+1,iy+1),u),v);
}
BackgroundMip LandscapeDetail() {
    // RG: full-width sky wisps/stars. BA: mountain facets, addressed in the
    // existing panorama coordinates. Every expensive operation happens here,
    // once at startup; playback only samples this immutable mipmapped atlas.
    BackgroundMip result{kBackgroundWidth, kLandscapeDetailHeight, kBackgroundWidth*4,
        std::vector<std::uint8_t>(kBackgroundWidth*kLandscapeDetailHeight*4)};
    const auto byte = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value,0.f,1.f)*255.f+.5f);
    };
    for (unsigned y = 0; y < result.height; ++y) for (unsigned x = 0; x < result.width; ++x) {
        const float u = (x+.5f)/result.width, v = (y+.5f)/result.height;
        const float n = .55f*Noise(u*13+4,v*5+9) + .28f*Noise(u*31,v*12)
                      + .17f*Noise(u*77,v*29);
        const float lane = v - (.39f + .18f*std::sin(u*7.4f+.4f));
        const float cloud = std::exp(-lane*lane*24.f) * std::max(0.f,n-.29f) * 1.3f;
        const auto offset = static_cast<std::size_t>(y)*result.rowPitch+x*4;
        result.pixels[offset] = byte(cloud);
        // Slanted planar faces and secondary gullies follow the silhouettes.
        const float px = (u*2-1)*kBackgroundHalfWidth;
        const float py = kBackgroundTop-v*kVerticalExtent;
        for (unsigned layer = 0; layer < 2; ++layer) {
            const float top = MountainHeight(px,layer);
            const float depth = std::clamp((top-py)/std::max(top,.001f),0.f,1.f);
            const float faceCoord = px*(layer == 0 ? 13.f : 17.f)+depth*1.15f+layer*.37f;
            const float face = faceCoord-std::floor(faceCoord);
            float shade = face < .48f ? .25f+face*.65f : .80f-(face-.48f)*.35f;
            shade *= .55f+.45f*(1-depth);
            result.pixels[offset+2+layer] = byte(shade);
        }
    }
    // Non-tiled, seeded positions with varied brightness. Tiny soft footprints
    // survive mip filtering without a regular star grid or runtime twinkling.
    for (unsigned i = 0; i < 530; ++i) {
        const auto h = Mix(i+0x8d471U), j = Mix(h);
        const int x = 2 + h % (result.width-4), y = 2 + j % (result.height-4);
        const float brightness = .16f + .72f*static_cast<float>((j >> 16)&255)/255.f;
        const float radius = i % 23 == 0 ? 1.45f : .70f;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const float value = brightness*std::exp(-(dx*dx+dy*dy)/(radius*radius)*1.6f);
            const auto index = static_cast<std::size_t>(y+dy)*result.rowPitch+(x+dx)*4+1;
            result.pixels[index] = std::max(result.pixels[index], byte(value));
        }
    }
    return result;
}
struct BuildingSection {
    float left, right, gap;
};
BuildingSection Section(const BackgroundBuilding& b, float height) {
    const float v = height/b.height;
    // Narrow supports open genuine sky gaps at the city foot. The occupied
    // upper floors retain the original bounding widths and cluster positions.
    const float support = std::lerp(.36f, .5f, std::clamp((v-.12f)/.08f,0.f,1.f));
    if (b.landmark) {
        const float half = v < .35f ? support : v < .60f ? std::lerp(.5f,.38f,(v-.35f)/.25f)
                         : v < .86f ? std::lerp(.38f,.32f,(v-.60f)/.26f)
                         : std::lerp(.32f,.22f,(v-.86f)/.14f);
        return {-half, half, v > .61f ? .09f : 0.f};
    }
    if (b.crown == 0) {
        // Split crowns grow from a shared shaft; unequal prongs avoid a
        // repeating row of identical rectangular roofs.
        const float half = std::min(support, .5f-.16f*std::clamp((v-.2f)/.8f,0.f,1.f));
        return {-half, half, v > .64f ? .075f : 0.f};
    }
    if (b.crown == 1) {
        // Asymmetric inhabited setbacks and a cantilevered lower shoulder.
        return {v > .82f ? -.22f : v > .35f ? -.38f : -support,
                v > .82f ? .22f : v > .62f ? .30f : support, 0.f};
    }
    if (b.crown == 2) {
        const float taper = std::clamp((v-.20f)/.8f,0.f,1.f);
        return {std::max(-support,-.5f+.22f*taper), std::min(support,.5f-.24f*taper),0.f};
    }
    const float half = v < .74f ? std::min(support,.5f-.17f*v)
                               : std::lerp(.3742f,.025f,(v-.74f)/.26f);
    const float lean = .06f*v;
    return {-half+lean, half+lean,0.f};
}
bool Inside(const BackgroundBuilding& b, float x, float y) {
    if (y < 0 || y > b.height) return false;
    const float local = (x-b.center)/b.width, v = y/b.height;
    const auto section = Section(b,y);
    if (local < section.left || local > section.right ||
        (section.gap > 0 && std::abs(local) < section.gap)) return false;
    if (b.landmark) return v <= (local < 0 ? .97f+(local+.32f)*.10f : .91f-(local-.10f)*.25f);
    if (b.crown == 0) return v <= (local < 0 ? .99f : .88f);
    if (b.crown == 1) return v <= .94f+local*.25f;
    if (b.crown == 2) return v <= .91f+local*.25f;
    return true;
}
void City(BackgroundMip& coverage, BackgroundMip& steady, BackgroundMip& animated, unsigned layout) {
    const auto buildings = BackgroundBuildings(layout);
    for (unsigned index = 0; index < buildings.size(); ++index) {
        const auto& b = buildings[index];
        const auto pixelX = [](float x) { return (x/kCityHalfWidth+1)*.5f*kBackgroundWidth; };
        const auto pixelY = [](float y) { return (kBackgroundTop-y)/kVerticalExtent*kBackgroundHeight; };
        const int left = std::max(0, static_cast<int>(std::floor(pixelX(b.center-b.width*.5f))));
        const int right = std::min(static_cast<int>(kBackgroundWidth), static_cast<int>(std::ceil(pixelX(b.center+b.width*.5f))));
        const int top = std::max(0, static_cast<int>(std::floor(pixelY(b.height))));
        const int bottom = std::min(static_cast<int>(kBackgroundHeight), static_cast<int>(std::ceil(pixelY(0))));
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            unsigned body = 0, warm = 0, accent = 0, groups[4]{};
            for (unsigned sy = 0; sy < kSamples; ++sy) for (unsigned sx = 0; sx < kSamples; ++sx) {
                const float px = ((x+(sx+.5f)/kSamples)/kBackgroundWidth*2-1)*kCityHalfWidth;
                const float py = kBackgroundTop-(y+(sy+.5f)/kSamples)/kBackgroundHeight*kVerticalExtent;
                if (!Inside(b, px, py)) continue;
                ++body;
                if (py < .010f || py > b.height-.004f) continue;
                const auto section = Section(b,py);
                const float local = (px-b.center)/b.width;
                // Each occupied facade gets its own window bays. Both taper
                // and the open split use the same section as the silhouette.
                const float facadeLeft = section.gap > 0 && local > 0 ? section.gap : section.left;
                const float facadeRight = section.gap > 0 && local < 0 ? -section.gap : section.right;
                const float facade = std::clamp((local-facadeLeft)/(facadeRight-facadeLeft),0.f,1.f);
                const unsigned bays = std::max(1U, static_cast<unsigned>((facadeRight-facadeLeft)*b.width/.0048f));
                const float column = facade*bays;
                const float row = (py-.010f)/.0068f;
                const auto hash = Mix(index*9137U + static_cast<unsigned>(column)*131U + static_cast<unsigned>(row)*977U);
                const auto block = Mix(index*97U + static_cast<unsigned>(row)/3U*239U);
                const float cx = column-std::floor(column), ry = row-std::floor(row);
                const bool window = facade > .12f && facade < .88f &&
                    cx > .23f && cx < .67f && ry > .35f && ry < .53f
                    && block%5 < 2 && hash%100 < 18;
                // A few short fixtures attach to real roof/setback ledges.
                // They occupy separate patches, never connected shaft strips.
                const float ledgeHeight = b.landmark ? .61f : b.crown == 1 ? .62f
                    : b.crown == 0 ? .64f : b.crown == 2 ? .91f+local*.25f : .74f;
                const bool ledge = (index%7 == 2 || b.landmark) &&
                    py > b.height*ledgeHeight-.0012f && py < b.height*ledgeHeight &&
                    std::abs(facade-.48f)*(facadeRight-facadeLeft)*b.width < .0016f;
                const bool upperFixture = b.landmark && local < 0 &&
                    py > b.height*.77f-.0012f && py < b.height*.77f &&
                    std::abs(facade-.56f)*(facadeRight-facadeLeft)*b.width < .00125f;
                if (ledge || upperFixture) ++accent;
                else if (window) {
                    if (hash%11 == 0) ++groups[Mix(index*31U + static_cast<unsigned>(row)/3U)%4];
                    else ++warm;
                }
            }
            const auto offset = static_cast<std::size_t>(y)*kBackgroundWidth+static_cast<unsigned>(x);
            auto add = [](std::uint8_t& dest, unsigned count) {
                dest = std::max(dest, static_cast<std::uint8_t>(count*255/(kSamples*kSamples)));
            };
            add(coverage.pixels[offset*4+3], body);
            add(steady.pixels[offset*2], warm); add(steady.pixels[offset*2+1], accent);
            for (unsigned group = 0; group < 4; ++group) add(animated.pixels[offset*4+group], groups[group]);
        }
    }
    // Small local glow, clipped to the silhouette; no postprocessing or outline.
    for (auto* mask : {&steady, &animated}) {
        const auto source = mask->pixels;
        const unsigned channels = mask == &steady ? 2U : 4U;
        for (unsigned y = 1; y+1 < kBackgroundHeight; ++y) for (unsigned x = 1; x+1 < kBackgroundWidth; ++x) {
            const auto p = y*kBackgroundWidth+x;
            if (!coverage.pixels[p*4+3]) continue;
            for (unsigned c = 0; c < channels; ++c) {
                unsigned glow = 0;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                    glow += source[static_cast<std::size_t>(static_cast<int>(p)+dy*static_cast<int>(kBackgroundWidth)+dx)*channels+c];
                const unsigned emission = static_cast<unsigned>(source[p*channels+c]) + glow/24;
                // Architectural fixtures use lower energy than occupied
                // windows, preserving warm light as the city's visual anchor.
                const unsigned restrained = mask == &steady && c == 1 ? emission*28/100 : emission;
                mask->pixels[p*channels+c] = static_cast<std::uint8_t>(std::min(
                    static_cast<unsigned>(coverage.pixels[p*4+3]),restrained));
            }
        }
    }
}
BackgroundArtwork Generate() {
    BackgroundArtwork result{{4, {}}, {2, {}}, {4, {}}, Mips(LandscapeDetail(),4)};
    const auto mountain = Mountains();
    for (unsigned layout = 0; layout < kBackgroundLayouts; ++layout) {
        auto coverage = mountain; auto steady = Empty(2); auto animated = Empty(4);
        City(coverage, steady, animated, layout);
        result.coverage.slices.push_back(Mips(std::move(coverage), 4));
        result.steadyEmission.slices.push_back(Mips(std::move(steady), 2));
        result.animatedEmission.slices.push_back(Mips(std::move(animated), 4));
    }
    return result;
}
} // namespace
unsigned BackgroundLayout(float aspect) { return aspect >= 1.5f ? 0 : aspect >= 1.f ? 1 : aspect >= .7f ? 2 : 3; }
std::array<BackgroundBuilding, 36> BackgroundBuildings(unsigned layout) {
    constexpr std::array<float,kBackgroundLayouts> spacing{1, .82f, .66f, .53f};
    std::array<BackgroundBuilding, 36> buildings{};
    for (unsigned i = 0; i < buildings.size(); ++i) {
        const auto hash = Mix(i+713U);
        const float center = -.405f+i*(.81f/35) + (static_cast<int>((hash>>9)%9)-4)*.0014f;
        const float cluster = std::max(0.f,1-std::abs(center+.19f)/.14f)*.024f
                            + std::max(0.f,1-std::abs(center-.19f)/.15f)*.034f;
        buildings[i] = {center*spacing[std::min(layout,3U)],
            .018f+(hash%5)*.003f, .024f+(hash%7)*.006f+cluster, hash%4, i == 27};
        if (i == 27) { buildings[i].width = .03f; buildings[i].height = .139f; }
    }
    return buildings;
}
float BackgroundCityHorizontalOffset(float aspect) {
    // Translate the narrow layout, preserving every building's dimensions.
    // Keep the landmark right of center even at the minimum capture aspect.
    const auto landmark = BackgroundBuildings(3)[27];
    const float target = std::min(landmark.center,
        std::min(aspect * .25f, aspect * .5f - landmark.width * .5f - .0005f));
    return std::max(0.f, landmark.center - target);
}
std::size_t BackgroundArtwork::GpuBytes() const {
    std::size_t bytes = 0;
    for (const auto* mask : {&coverage, &steadyEmission, &animatedEmission})
        for (const auto& slice : mask->slices) for (const auto& mip : slice) bytes += mip.pixels.size();
    for (const auto& mip : landscapeDetail) bytes += mip.pixels.size();
    return bytes;
}
const BackgroundArtwork& GetBackgroundArtwork() { static const auto artwork = Generate(); return artwork; }
bool IsValidBackgroundArtwork(const BackgroundArtwork& art) {
    const auto validMips=[](const std::vector<BackgroundMip>& mips,unsigned width,unsigned height,unsigned channels) {
        if(mips.size()!=kBackgroundMipLevels)return false;
        for(const auto& mip:mips) {
            if(mip.width!=width || mip.height!=height || mip.rowPitch!=width*channels ||
                mip.pixels.size()!=static_cast<std::size_t>(width)*height*channels)return false;
            width=std::max(1U,width/2);height=std::max(1U,height/2);
        }
        return true;
    };
    const std::array<const BackgroundMask*,3> masks{&art.coverage,&art.steadyEmission,&art.animatedEmission};
    for(unsigned i=0;i<masks.size();++i) {
        const auto& mask=*masks[i];const unsigned channels=i==1?2U:4U;
        if(mask.channels!=channels || mask.slices.size()!=kBackgroundLayouts)return false;
        for(const auto& slice:mask.slices)if(!validMips(slice,kBackgroundWidth,kBackgroundHeight,channels))return false;
    }
    return validMips(art.landscapeDetail,kBackgroundWidth,kLandscapeDetailHeight,4) && art.GpuBytes()<=kBackgroundByteBudget;
}
} // namespace vaporcruise::scene
