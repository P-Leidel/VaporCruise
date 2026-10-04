#pragma once

#include <cmath>
#include <cstdint>

namespace vaporcruise::common {

// Scene colors store linear-light RGB and linear, unpremultiplied alpha.
// Decode authored sRGB colors once on input; blend/light in linear space;
// encode RGB once on final display output to a non-sRGB UNORM render target.
// An sRGB render target would perform that final encoding instead. Alpha is
// never transfer-encoded. SceneComposite performs the final RGB encoding.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

inline float SrgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f :
        std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

inline float LinearToSrgb(float linear) {
    return linear <= 0.0031308f ? linear * 12.92f :
        1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

inline Color ColorFromSrgbHex(std::uint32_t hex) {
    return {SrgbToLinear(static_cast<float>((hex >> 16) & 255) / 255.0f),
            SrgbToLinear(static_cast<float>((hex >> 8) & 255) / 255.0f),
            SrgbToLinear(static_cast<float>(hex & 255) / 255.0f), 1.0f};
}

}  // namespace vaporcruise::common
