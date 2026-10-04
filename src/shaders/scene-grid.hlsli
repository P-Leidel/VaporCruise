#include "camera-projection.hlsli"
#ifndef SCENE_GRID
#define SCENE_GRID
float DistantGridVisibility(float z) {
    // Depth fog now supplies the final extinction. Keep a gentle emission
    // falloff here so resolvable rays reach the skyline instead of disappearing
    // into an empty strip before the ground itself reaches the mist.
    return exp2(-max(z - 40, 0) / 420.0);
}
float GridHaloVisibility(float viewDistance) {
    // Camera-to-surface distance also softens the glow toward the sides.
    // Full foreground glow, then half intensity per 58 world units.
    return exp2(-max(viewDistance - 8.0, 0.0) / 58.0);
}
float2 TentIntegral(float2 x, float2 halfWidth) {
    float2 u = clamp(x / halfWidth, -1.0, 1.0);
    return halfWidth * (.5 + u - .5 * u * abs(u));
}
float2 PeriodicLineIntegral(float2 x, float2 halfWidth) {
    float2 fraction = frac(x);
    return floor(x) * (2 * halfWidth) + min(fraction, halfWidth)
        + max(fraction - (1 - halfWidth), 0);
}
float2 PeriodicTentIntegral(float2 x, float2 halfWidth) {
    float2 fraction = frac(x);
    return floor(x) * halfWidth + TentIntegral(fraction, halfWidth)
        + TentIntegral(fraction - 1, halfWidth) - .5 * halfWidth;
}
float2 GridErf(float2 x) {
    // Smooth Gaussian coverage; maximum approximation error below 1.5e-7.
    float2 t = rcp(1 + .3275911 * abs(x));
    float2 polynomial = (((((1.061405429*t - 1.453152027)*t)
        + 1.421413741)*t - .284496736)*t + .254829592)*t;
    return sign(x) * (1 - polynomial * exp2(-x*x * 1.442695041));
}
float2 GaussianLineCoverage(float2 distance, float2 halfWidth, float2 footprint) {
    float2 scale = rcp(1.414213562 * .8 * footprint);
    return .5 * (GridErf((distance + halfWidth) * scale)
               - GridErf((distance - halfWidth) * scale));
}
// Gaussian reconstruction has no box-filter side lobes. In the compressed
// region eight harmonics suffice: higher frequencies are below display
// precision. Use the same Gaussian in closed form for narrow footprints.
void SmoothPeriodicGrid(float2 cell, float2 width, float2 glowWidth,
                        float2 footprint, out float2 core, out float2 glow) {
    core = 2 * width;
    glow = glowWidth;
    float2 phase = frac(cell);
    [unroll] for (int harmonic = 1; harmonic <= 8; ++harmonic) {
        float frequency = 3.141592654 * harmonic;
        // sigma = .8 footprints; matches GaussianLineCoverage below.
        float2 attenuation = exp2(-18.2257 * harmonic * harmonic * footprint * footprint);
        float2 wave = cos(2 * frequency * phase) * attenuation;
        float2 coreAngle = 2 * frequency * width;
        float2 glowAngle = frequency * glowWidth;
        float2 glowSinc = sin(glowAngle) / glowAngle;
        core += 4 * width * (sin(coreAngle) / coreAngle) * wave;
        glow += 2 * glowWidth * glowSinc * glowSinc * wave;
    }
    core = saturate(core);
    glow = saturate(glow);
}
// The background continuation and terrain plain use identical grid filtering.
// Keep coverage separate so the core and halo can carry independent colors.
void GridLineWidths(float2 pixelsPerCell, out float2 core, out float2 innerHalo, out float2 outerHalo) {
    core = min(.5, .025 * pixelsPerCell);
    innerHalo = min(2.7, .08 * pixelsPerCell);
    outerHalo = min(6.0, .20 * pixelsPerCell);
}
float GridPixelScale(float z) {
    // The wire is emissive in screen space. Project its source-cell scale by
    // depth once for every direction, rather than dotting a displacement with
    // the line normal: that dot can cancel on a perfectly visible slope edge.
    return resolution.y * camera.y / (z * camera.z);
}
float2 GridEmission(float core, float originalHalo, float outerHalo) {
    // Bright fine wires and a softer, separately colored family halo. Bound the
    // halo at intersections instead of whitening cores by adding crossed lines.
    return float2(.72 * core + 1.9 * .052 * originalHalo,
                  .36 * outerHalo * (1 - .55 * saturate(core)));
}
float2 PlainGrid(float2 cell, float3 surface) {
    // Different slopes stretch material coordinates by different amounts.
    // Their true screen gradients govern filtering; depth supplies the common
    // screen profile for every resolved wire direction.
    // A ground-to-slope edge must keep the same width on both faces, even when
    // one triangle is almost edge-on. The quotient rule avoids finite-difference
    // jumps from perspective interpolation on subpixel triangles.
    float q = rcp(surface.z);
    float2 numerator = cell * q;
    float2 dx = (ddx(numerator) - cell * ddx(q)) / q;
    float2 dy = (ddy(numerator) - cell * ddy(q)) / q;
    float2 rawFootprint = max(sqrt(dx*dx + dy*dy), .00001);
    float2 pixelsPerCell = GridPixelScale(surface.z).xx;
    // Integrated coverage preserves energy for subpixel stripes. Both resolved
    // and compressed cores use the same .8-pixel Gaussian; switching from a box
    // to a narrower Gaussian previously raised the peak at a ground/slope join.
    float2 filterFootprint = 2 * rawFootprint;
    float2 corePixels, innerPixels, outerPixels;
    GridLineWidths(pixelsPerCell, corePixels, innerPixels, outerPixels);
    // Resolved lines share a screen profile. As an axis becomes compressed,
    // converge smoothly to the original low-energy material average instead
    // of allowing overlapping screen halos to fill the black horizon.
    float2 compressed = smoothstep(.12, .20, rawFootprint);
    float2 coreWidth = max(.00001, lerp(min(.49, rawFootprint * corePixels), .025, compressed));
    float2 glowWidth = max(.00001, lerp(min(.49, rawFootprint * innerPixels), .08, compressed));
    float2 glow = saturate((PeriodicTentIntegral(cell + filterFootprint*.5, glowWidth)
        - PeriodicTentIntegral(cell - filterFootprint*.5, glowWidth)) / filterFootprint);
    float2 outerWidth = max(.00001, lerp(min(.49, rawFootprint * outerPixels), .20, compressed));
    float2 outerGlow = saturate((PeriodicTentIntegral(cell + filterFootprint*.5, outerWidth)
        - PeriodicTentIntegral(cell - filterFootprint*.5, outerWidth)) / filterFootprint);
    // Integrate across every crossed cell instead of fading resolvable rays
    // to their average as soon as the footprint exceeds .12 cells.
    float2 distance = abs(frac(cell + .5) - .5);
    // Below .20 cells/pixel the next two periodic lines capture all meaningful
    // Gaussian support. Above that, the Fourier series handles dense rows.
    float2 plainCore = GaussianLineCoverage(distance, coreWidth, rawFootprint)
        + GaussianLineCoverage(1-distance, coreWidth, rawFootprint);
    float2 smoothCore, smoothGlow;
    SmoothPeriodicGrid(cell, coreWidth, glowWidth, rawFootprint, smoothCore, smoothGlow);
    float2 reconstruction = smoothstep(.12, .20, rawFootprint);
    plainCore = lerp(plainCore, smoothCore, reconstruction);
    glow = lerp(glow, smoothGlow, reconstruction);
    plainCore = lerp(plainCore, 2 * coreWidth, smoothstep(.75, 2.0, rawFootprint));
    float2 axisResolved = 1 - smoothstep(.5, 1.5, rawFootprint);
    float2 plainGlow = lerp(glowWidth, glow, axisResolved);
    return GridEmission(1 - (1-plainCore.x)*(1-plainCore.y),
                        1 - (1-plainGlow.x)*(1-plainGlow.y),
                        1 - (1-outerGlow.x)*(1-outerGlow.y));
}
#endif
