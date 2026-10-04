#include "shader-bindings.hlsli"
#include "scene-background.hlsli"
Texture2DArray<float4> panoramaCoverage : register(VC_COVERAGE_REGISTER);
Texture2DArray<float2> panoramaSteady : register(VC_STEADY_REGISTER);
Texture2DArray<float4> panoramaAnimated : register(VC_ANIMATED_REGISTER);
Texture2D<float4> landscapeDetail : register(VC_DETAIL_REGISTER);
SamplerState panoramaSampler : register(VC_PANORAMA_SAMPLER_REGISTER);
float3 PanoramaBackgroundColor(float2 screenPixel) {
    float3 atmosphere = AtmosphereColor(screenPixel);
    float2 p = float2((screenPixel.x-resolution.x*.5)/resolution.y,
                     .5-screenPixel.y/resolution.y-camera.w);
    float2 skyUV = float2(p.x/3.0+.5,1-p.y/.45);
    float2 skyDetail = landscapeDetail.SampleLevel(panoramaSampler,skyUV,
        max(0,log2(512.0/resolution.y))).rg;
    float skyWindow = smoothstep(.045,.18,p.y)
        * smoothstep(.16,.24,length(p-float2(0,.16)))
        * smoothstep(0,.025,skyUV.x) * (1-smoothstep(.975,1,skyUV.x));
    atmosphere += skyWindow * (lerp(farRidge.rgb,grid.rgb,.12)*skyDetail.r*.18
        + lerp(float3(.60,.65,.80),highlight.rgb,.38)*skyDetail.g*.40);
    float3 color = atmosphere;
    // Clamp sampling continues the outer ridges on superwide displays.
    [branch] if (p.y >= panoramaExtents.z-panoramaExtents.y && p.y <= panoramaExtents.z) {
        float2 uv = float2(p.x/panoramaExtents.x+.5, (panoramaExtents.z-p.y)/panoramaExtents.y);
        float2 cityUV = float2((p.x+cityHorizontalOffset)/panoramaExtents.w+.5,uv.y);
        float layout = panorama.x;
        float lod = panorama.y;
        float3 mountains = panoramaCoverage.SampleLevel(panoramaSampler,float3(uv,0),lod).rgb;
        float2 facets = landscapeDetail.SampleLevel(panoramaSampler,uv,panorama.w).ba;
        float cityLod = panorama.z;
        float city = panoramaCoverage.SampleLevel(panoramaSampler,float3(cityUV,layout),cityLod).a;
        float2 steady = panoramaSteady.SampleLevel(panoramaSampler,float3(cityUV,layout),cityLod);
        float4 animated = panoramaAnimated.SampleLevel(panoramaSampler,float3(cityUV,layout),cityLod);
        float join = smoothstep(.006,.032,p.y);
        float3 farMountain = farRidge.rgb*lerp(.30,.72,facets.x);
        float3 middleMountain = lerp(nearRidge.rgb,farRidge.rgb,.22)*lerp(.42,.90,facets.y);
        float3 nearMountain = nearRidge.rgb*lerp(.40,.78,facets.y);
        // Warm aerial perspective separates distant facets without brightening
        // the foreground grid or adding another screen-space horizon ribbon.
        float depthVeil = 1-smoothstep(.025,.15,p.y);
        float3 distantMist = haze.rgb*.25 + sunBottom.rgb*.035;
        farMountain = lerp(farMountain, distantMist, depthVeil*.18);
        middleMountain = lerp(middleMountain, distantMist, depthVeil*.07);
        float3 cityBody = lerp(ground.rgb*.8,nearRidge.rgb*.38,.45);
        // A shared straight base made overlapping towers read as a black shelf
        // above the road. Settle only their lowest opaque facades into the same
        // extinction color as the distant land. Broken heights drift with the
        // existing fog clock; coverage still blocks the sun at every elevation.
        float footPhase = 6.28318530718 * cityLightPhase;
        float footHeight = max(.018, .024 + .006*sin(p.x*11.0-footPhase)
            + .003*sin(p.x*27.0+2*footPhase));
        float footMist = .88 * (1-smoothstep(.002,footHeight,p.y));
        cityBody = lerp(cityBody,HorizonMistColor(screenPixel),footMist);
        // The coverage gradient points into the silhouette; invert its screen
        // x component to find the exterior edge in angular coordinates. Only
        // edges facing the sun receive its restrained, coherent moving light.
        float2 slope = float2(ddx(city),ddy(city));
        float rim = saturate(abs(slope.x)+abs(slope.y));
        float2 sunOffset = float2(-p.x,.16-p.y);
        float facing = saturate(dot(float2(-slope.x,slope.y),sunOffset)
            / max(length(slope)*length(sunOffset),.000001));
        float backlight = exp2(-p.x*p.x*90.0);
        SolarLight solar = CurrentSolarLight(length(sunOffset));
        cityBody += solar.top * solar.glow * .024 * rim * backlight * facing;
        color = lerp(atmosphere, farMountain, mountains.r*join);
        color = lerp(color, middleMountain, mountains.g*join);
        color = lerp(color, nearMountain, mountains.b*join);
        color = lerp(color, cityBody, city);
        // Fixed coverage weights, independently filtered; never interpolate group IDs.
        float4 weights = .94 + .06*sin(6.28318530718*(cityLightPhase+float4(0,.19,.47,.73)));
        float3 windowColor = lerp(cityWarm.rgb,sunBottom.rgb,.65);
        // Warm occupied windows anchor the skyline; the slow groups carry
        // complementary light without changing the baked masks or sampling.
        color += join*(windowColor*steady.r*.42
            + lerp(windowColor,highlight.rgb,.65)*dot(animated,weights)*.46
            + highlight.rgb*steady.g*.56);
    }
    // Fog after all silhouette composition, including filtered city coverage
    // just below the horizon, so it cannot cut a dark seam through the mist.
    float3 fogged = color;
    if (p.y >= 0) {
        fogged = ApplyHorizonHaze(color, screenPixel);
    } else {
        float z = camera.y * camera.x / max(-p.y, .0001);
        float x = p.x * z / camera.y;
        fogged = ApplyGroundMist(color, screenPixel, float3(x, 0, z));
    }
    return fogged;
}
