#include "scene-frame.hlsli"
#include "scene-background.hlsli"
#include "HintGlyphs.hlsli"
float3 EncodeSrgb(float3 c) {
    c = max(c, 0.0);
    return lerp(12.92 * c, 1.055 * pow(c, 1.0 / 2.4) - .055, step(.0031308, c));
}
float4 FinalOutput(float3 color, float2 screenPixel) {
    // Atmosphere is composed per material before the UI overlay.
    color = HintOverlay(screenPixel, color);
    float3 encoded = EncodeSrgb(color);
    // A stationary integer pixel hash avoids both banding and temporal sparkle.
    uint2 pixel = uint2(screenPixel);
    uint hash = pixel.x * 1664525u + pixel.y * 1013904223u;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    float noise = float(hash & 1023u) / 1023.0 - .5;
    return float4(saturate(encoded + noise / 255.0), 1);
}
