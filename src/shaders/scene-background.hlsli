#include "camera-projection.hlsli"
#ifndef SCENE_BACKGROUND
#define SCENE_BACKGROUND
#include "scene-frame.hlsli"
#include "scene-grid.hlsli"
#include "scene-regional.hlsli"
// Shared neon profile: a bright core with a tight and a soft outer halo.
float NeonGlow(float distance, float width) {
    float d = abs(distance) / width;
    return .72 * exp2(-d * d * 3.0)
        + .24 * exp2(-d * d * .28)
        + .10 * exp2(-d * d * .035);
}
float3 GridRadiance(float2 emission, float viewDistance, float3 surface) {
    return RegionalTint(grid.rgb,surface,regionalControl.z) * emission.x
        + RegionalTint(gridGlow.rgb,surface,regionalControl.z) * emission.y * GridHaloVisibility(viewDistance);
}
struct SolarLight {
    float3 top, bottom;
    float glow;
};
SolarLight CurrentSolarLight(float distanceFromSun) {
    // The existing bounded clock freezes with the scene. Keep all changing
    // solar light inside a soft local footprint, leaving the wider sky stable.
    float t = 6.28318530718 * cityLightPhase;
    float footprint = 1-smoothstep(.21,.26,distanceFromSun);
    float hue = (1-cos(t))*footprint;
    SolarLight light;
    light.top = lerp(sunTop.rgb,sunBottom.rgb,.0125*hue);
    light.bottom = lerp(sunBottom.rgb,sunTop.rgb,.0075*hue);
    light.glow = 1 + .05*sin(t)*footprint;
    return light;
}
// Aspect-correct angular coordinates keep one atmosphere field on the distant
// ground and panorama. Integer harmonics make the bounded 42-second clock
// seamless, and the scene clock already freezes during pause and suspension.
float HorizonWisps(float2 screenPixel) {
    float2 p = float2((screenPixel.x-resolution.x*.5)/resolution.y,
                     .5-screenPixel.y/resolution.y-camera.w);
    float wisps = 0;
    [branch] if (p.y >= -.075 && p.y <= .115) {
        float t = 6.28318530718 * cityLightPhase;
        float flow = .18 * sin(t) + .06 * sin(2*t);
        float x = p.x - flow;
        float curl = .010*sin(x*4.8+t) + .005*sin(x*10.5-2*t);
        // Preserve a soft profile when a small window compresses angular fog
        // into just a few rows. Standard and larger windows keep their widths.
        float minimumWidth = 5.0 / resolution.y;
        float low = (p.y+.008-curl) / max(.012+.004*sin(x*3.1-t), minimumWidth);
        float middle = (p.y-.031-.010*sin(x*3.8-2*t)
                        -.004*sin(x*12+t)) / max(.010, minimumWidth);
        float high = (p.y-.063-.012*sin(x*3.2+t)
                      -.006*sin(x*7.8-2*t)) / max(.013, minimumWidth);
        // Unequal, smoothly broken lengths keep the layers from reading as
        // three uniform ribbons. Shapes drift and curl without sampled noise.
        float brokenLow = .02+.98*smoothstep(-.15,.75,
            .7*sin(x*6.4-t)+.3*sin(x*13.5+2*t));
        float brokenMiddle = .025+.975*smoothstep(-.1,.7,sin(x*8.5+2*t));
        float brokenHigh = smoothstep(-.1,.75,sin(x*5.8-2*t));
        float3 spread = float3((x+.30)/.95, (x-.52)/.72, (x+.65)/.62);
        float3 envelope = exp2(-spread*spread);
        float veil = brokenLow*exp2(-low*low*1.8)*envelope.x
            + .80*brokenMiddle*exp2(-middle*middle*1.8)*envelope.y
            + .60*brokenHigh*exp2(-high*high*2.0)*envelope.z;
        wisps = veil * smoothstep(-.075,-.045,p.y) * (1-smoothstep(.09,.115,p.y));
    }
    return wisps;
}
float3 WispRadiance(float amount) {
    // Pale scattered light is confined to the wisps, so the steady atmosphere
    // and black foreground do not brighten with the new moving layers.
    return lerp(gridGlow.rgb, road.rgb, .28) * (.052*amount);
}
float3 HorizonMistColor(float2 screenPixel) {
    float2 uv = screenPixel / resolution;
    float across = (uv.x-.5) * resolution.x / resolution.y;
    float cityGlow = exp2(-across*across*4.0);
    // One extinction color for land and sky. A little sunset light is localized
    // around the city instead of painting an additive ribbon across the road.
    return sky.rgb * .65 + haze.rgb * .05 + gridGlow.rgb * .004
        + sunBottom.rgb * (.001 + .004 * cityGlow);
}
float3 ApplyHorizonHaze(float3 color, float2 screenPixel) {
    float elevation = max(.5 - camera.w - screenPixel.y / resolution.y, 0);
    // A very narrow continuous join under the moving, locally broken wisps.
    float joinDistance = elevation / .009;
    float join = exp2(-joinDistance*joinDistance);
    float wisp = HorizonWisps(screenPixel);
    float veil = 1 - (1-join) * exp2(-1.05*wisp);
    return lerp(color, HorizonMistColor(screenPixel), veil) + WispRadiance(wisp)*veil;
}
float3 ApplyGroundMist(float3 color, float2 screenPixel, float3 surface) {
    // Depth, rather than screen height, controls the ground atmosphere. Nearby
    // ridges retain contrast even when their silhouettes cross the horizon.
    float distance = max(surface.z - 24, 0);
    float density = exp2(-max(surface.y, 0) / 18.0);
    float distant = smoothstep(30, 115, surface.z);
    float lowHeight = exp2(-max(surface.y, 0) / 5.5);
    float t = 6.28318530718 * cityLightPhase;
    // Broad, asymmetric low pockets pass through the valleys. Their curls
    // follow the bounded clock, with no per-frame noise or new texture sample.
    // Constant phase advection avoids an oscillating field's still points;
    // opposite sides have different curls but both receive passing pockets.
    float pocketCoordinate = abs(surface.x)*.105 + surface.z*.019 - 3*t;
    float pocket = smoothstep(-.1,.75,
        .65*sin(pocketCoordinate) + .35*sin(surface.x*.21-surface.z*.013+2*t));
    float pocketDepth = smoothstep(30,55,surface.z) * (1-smoothstep(160,280,surface.z));
    float roadClearance = lerp(.18,1,smoothstep(3,9,abs(surface.x)));
    // Keep the shared horizon field identical on the sky and flat far ground.
    // Road clearance applies only to local pockets, preventing a center seam.
    float wisp = (HorizonWisps(screenPixel)*distant + .95*pocket*pocketDepth*roadClearance)
        * lowHeight;
    float transmission = exp2(-distance * density / 290.0 - 1.05*wisp);
    // All terrain settles into the plain before its shortest finite boundary.
    transmission *= 1 - smoothstep(cameraDepth.z*(340.0/512.0), cameraDepth.z*(500.0/512.0), surface.z);
    return lerp(HorizonMistColor(screenPixel), color, transmission)
        + WispRadiance(wisp)*(1-transmission);
}
float3 AtmosphereColor(float2 screenPixel) {
    float2 uv = screenPixel / resolution;
    float2 p = float2((uv.x - .5) * resolution.x / resolution.y, .5 - uv.y);
    // Keep the sun and sky gradient at their existing offset above the horizon.
    p.y += .02 - camera.w;
    const float horizon = .02;
    float above = max(p.y - horizon, 0.0);
    float3 color = sky.rgb * .72;
    color += sunBottom.rgb * .018 * exp2(-above * 16.0);
    float2 sunPosition = p - float2(0, .18);
    const float radius = .14;
    float d = length(sunPosition);
    SolarLight solar = CurrentSolarLight(d);
    float pixel = 1.0 / resolution.y;
    color += solar.bottom * .12 * solar.glow * exp2(-dot(sunPosition, sunPosition) * 52.0);
    color += solar.top * .028 * solar.glow * exp2(-dot(sunPosition, sunPosition) * 12.0);
    float disc = 1.0 - smoothstep(radius - pixel, radius + pixel, d);
    // Upper disc stays whole. Eight lower cutouts grow wider toward its foot.
    float below = max(.195 - p.y, 0.0);
    float stripeCoordinate = below / .021;
    float gap = lerp(.13, .58, saturate(below / .16));
    float stripeDistance = abs(frac(stripeCoordinate + .5) - .5);
    float stripeAA = pixel / .021;
    float stripe = smoothstep(gap * .5 - stripeAA, gap * .5 + stripeAA, stripeDistance);
    if (p.y > .195) stripe = 1.0;
    float3 sun = lerp(solar.bottom, solar.top, smoothstep(.04, .32, p.y));
    color = lerp(color, sun, disc * stripe);
    // A local corona supplements the broad existing veil. Keep emission outside
    // the disc so its gradient and dark stripe cutouts retain their contrast.
    float coronaDistance = max(d - radius, 0.0) / .025;
    color += sun * .085 * solar.glow * exp2(-coronaDistance * coronaDistance * 2.0) * (1-disc);
    // Continue the flat ground beyond the finite terrain mesh's lateral edges.
    // Invert the terrain camera projection; gridPhase matches its local z cells.
    float belowHorizon = uv.y - (.5 - camera.w);
    float2 surface = UnprojectCameraGround(uv * resolution);
    float x = surface.x, z = surface.y;
    float2 continuationGrid = PlainGrid(float2(x, z) * camera.z + float2(0, gridPhase), float3(x, 0, z));
    float gridVisibility = DistantGridVisibility(z);
    if (belowHorizon > 0) {
        color = ground.rgb + GridRadiance(continuationGrid,
            length(float3(x, camera.x, z)),float3(x,0,z)) * gridVisibility;
    }
    return color;
}
#endif
