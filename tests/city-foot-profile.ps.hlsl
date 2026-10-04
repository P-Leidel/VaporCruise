// Probe the shipped compositor with an opaque city and empty emission masks.
// The native fixture binds the same resource slots as the renderer.
#include "scene-panorama.hlsli"
float4 main(float4 pixel : SV_Position) : SV_Target {
    const float elevations[10] = {.012, .020, .034, .040, 0, .24, .31, .45, .14, .14};
    const uint row = (uint)pixel.y;
    float2 p = float2((pixel.x/256.0-.5)*.90, elevations[row]);
    if (row >= 8) p.x = .10 + (pixel.x-128.5)/256.0*.90;
    float2 screenPixel = float2(resolution.x*.5+p.x*resolution.y,
        resolution.y*(.5-camera.w-p.y));
    return float4(row >= 5 && row < 8 ? AtmosphereColor(screenPixel)
        : PanoramaBackgroundColor(screenPixel), 1);
}
