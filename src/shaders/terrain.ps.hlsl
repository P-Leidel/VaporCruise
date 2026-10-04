#include "scene-frame.hlsli"
#include "scene-background.hlsli"
#include "scene-output.hlsli"
struct TerrainInput {
    float4 position : SV_Position;
    float3 surface : TEXCOORD0;
    float2 material : TEXCOORD1;
    nointerpolation float3 folds : TEXCOORD2;
};
float Coverage(float distance, float halfWidth, float footprint) {
    return saturate((min(distance + footprint * .5, halfWidth)
                   - max(distance - footprint * .5, -halfWidth)) / footprint);
}
#define SCENE_TERRAIN_ACCENTS
float3 TerrainSurfaceAccent(float3 surface,float3 normal) {
    float shoulders=smoothstep(6,10,abs(surface.x));
    float distant=smoothstep(40,70,surface.z)*(1-smoothstep(180,260,surface.z));
    float upper=smoothstep(1,8,surface.y)*smoothstep(.2,.75,abs(normal.y));
    float facing=saturate(dot(normal,normalize(float3(0,.65,-.76))));
    SolarLight solar=CurrentSolarLight(0);
    float3 warmth=solar.bottom*(.0075*solar.glow*distant*upper*facing*shoulders);
    float4 regional=RegionalSample(surface);
    // Bend a height contour using the world-anchored regional field, avoiding
    // a large floating route coordinate or an independent animation clock.
    float contour=surface.y-.55*sin(dot(regional.xyz,float3(1.1,3.7,5.2)))
        -.25*sin(surface.x*.04);
    float footprint=max(fwidth(contour),.0001);
    float distance=abs(frac(contour/3.4+.5)-.5)*3.4;
    float seam=Coverage(distance,.03,footprint)*(1-smoothstep(.08,.22,footprint));
    float slope=smoothstep(.12,.5,1-abs(normal.y));
    float eligible=smoothstep(.65,.95,regional.w)*RegionalInfluence(surface);
    float mineral=seam*slope*eligible*smoothstep(2,6,surface.y)*shoulders;
    // Unit-luminance mineral hue caps its light at .010. Overlaps select the
    // larger radiance per component; they never sum into bright intersections.
    return max(warmth,regionalHue[2].rgb*(.010*mineral));
}
float4 main(TerrainInput input, uint primitive : SV_PrimitiveID) : SV_Target {
    float2 cell = input.material;
    // Evaluate the actual b-c line in homogeneous screen coordinates. A frac
    // distance jumps at its endpoints; material-space widths also differ on
    // the two faces, making a straight crease look stepped during motion.
    float fold = (primitive & 1) == 0 ? input.folds.x : input.folds.y;
    float diagonalSum = input.folds.z + ((primitive & 1) == 0 ? 1 : 0);
    // SV_Position.w is reciprocal clip W: this numerator is affine in screen
    // space, so its normalized gradient gives the same pixel distance on both
    // sides of a fold, independent of perspective and triangle slope.
    float diagonal = (cell.x + cell.y - diagonalSum) * input.position.w;
    float2 diagonalGradient = float2(ddx(diagonal), ddy(diagonal));
    float gradientLength = max(length(diagonalGradient), .0000001);
    float diagonalDistance = abs(diagonal) / gradientLength;
    float2 tangent = float2(-diagonalGradient.y, diagonalGradient.x) / gradientLength;
    float reciprocalDepth = input.position.w;
    float cellNumerator = cell.x * reciprocalDepth;
    float2 cellGradient = (float2(ddx(cellNumerator), ddy(cellNumerator))
        - cell.x * float2(ddx(reciprocalDepth), ddy(reciprocalDepth))) / reciprocalDepth;
    // Density fading must agree too: measure sampling along the shared edge,
    // not across either of its differently sloped faces.
    float resolved = 1 - smoothstep(.12, .40, abs(dot(cellGradient, tangent)));
    // Share the square wires' depth scale, without a direction-dependent dot
    // product that can collapse the width of a visible crease to almost zero.
    float z = input.surface.z;
    float pixelsPerCell = GridPixelScale(z);
    float2 corePixels, innerPixels, outerPixels;
    GridLineWidths(pixelsPerCell.xx, corePixels, innerPixels, outerPixels);
    float coreWidth = corePixels.x;
    float glowWidth = max(.00001, outerPixels.x);
    // Creases use the square wires' Gaussian core and two-pixel integrated
    // tent halos. Perspective changes their projected width, not their light.
    float diagonalCore = GaussianLineCoverage(diagonalDistance.xx, coreWidth.xx, float2(1,1)).x;
    float diagonalGlow = saturate((TentIntegral(diagonalDistance + 1, glowWidth)
        - TentIntegral(diagonalDistance - 1, glowWidth)) * .5).x;
    float innerWidth = max(.00001, innerPixels.x);
    float diagonalOriginalGlow = saturate((TentIntegral(diagonalDistance + 1, innerWidth)
        - TentIntegral(diagonalDistance - 1, innerWidth)) * .5).x;
    float2 crease = fold * GridEmission(diagonalCore, diagonalOriginalGlow, diagonalGlow)
        * resolved * DistantGridVisibility(z);
    float x = input.surface.x;
    float xFootprint = max(fwidth(x), .0001);
    // Halving both period and length makes markings pass twice as often at the
    // unchanged material travel speed. Two cells divides a 32-cell mesh section.
    float dashCoordinate = input.material.y * .5;
    float dashFootprint = max(fwidth(dashCoordinate), .0001);
    // Distance wraps around the dash period, so the following dash's lower
    // cap can glow back into the preceding gap as well.
    float dashDistance = abs(frac(dashCoordinate + .25) - .5);
    // Integrate every crossed dash, so small distant markings approach their
    // half-lit average even when a pixel covers several complete periods.
    float dashPhase = dashCoordinate - .25;
    float dash = ((PeriodicLineIntegral(dashPhase + dashFootprint*.5, .25)
        - PeriodicLineIntegral(dashPhase - dashFootprint*.5, .25)) / dashFootprint).x;
    float edge = NeonGlow(abs(x) - 3, max(.035, xFootprint * .6));
    float centerWidth = max(.025, xFootprint * .6);
    float sideGlow = NeonGlow(x, centerWidth);
    // Evaluate distance to the whole dash, including its end caps. Matching
    // both axes in screen footprints gives the tips the same soft halo as the
    // sides despite depth foreshortening. Keep integrated coverage far away.
    float capDistance = max(dashDistance - .25, 0);
    float capWidth = max(dashFootprint * centerWidth / xFootprint, .00001);
    float roundedDash = NeonGlow(length(float2(x / centerWidth, capDistance / capWidth)), 1);
    float center = lerp(roundedDash, sideGlow * dash, smoothstep(.08, .22, dashFootprint));
    float3 normal = normalize(cross(ddx(input.surface), ddy(input.surface)));
    float light = .55 + .45 * abs(normal.y);
    float facet = saturate((1 - abs(normal.y)) * 2 + input.surface.y / 6);
    // Larger formations retain nearly black faces. Angular variation shows the
    // relief without filling the landscape with broad luminous colored walls.
    float facing = .72 + .28*saturate(dot(normal,normalize(float3(-.4,.8,-.3))));
    float3 color = lerp(ground.rgb*.82, nearRidge.rgb*.24, facet) * light * facing;
    color += TerrainSurfaceAccent(input.surface,normal);
    // Resolve each grid direction independently. Dense transverse rows converge
    // to their average, while the still-resolvable horizon rays remain visible.
    float plain = 1 - smoothstep(0, .5, input.surface.y);
    // One periodic filter for every surface. A compressed transverse direction
    // must not fade the still-resolvable longitudinal lines on a slope, and
    // filtering must not change as a triangle crosses the old .5-height blend.
    float2 squareGrid = PlainGrid(cell, input.surface) * DistantGridVisibility(input.surface.z);
    // A fold and a square edge are the same emissive grid. Adding them made
    // moving intersections flash almost white, especially at ground joins.
    float2 emission = max(squareGrid, crease);
    color += GridRadiance(emission, length(input.surface - float3(0, camera.x, 0)),input.surface);
    if (abs(x) < 3.05) {
        color = ground.rgb * .55;
        // A smooth, stationary sunset sheen on the flat pavement. World-space
        // falloff avoids animated noise and thin reflected stripes that alias.
        // Reserve the existing markings; the near road remains almost black.
        float sheen = exp2(-x*x*.32) * smoothstep(5, 45, input.surface.z)
            * (1-smoothstep(100, 260, input.surface.z));
        color += sunBottom.rgb * .012 * sheen * plain
            * (1-saturate((edge+center)*2));
    }
    // Pale marking cores sit within primary-colored halos; all palettes share
    // this profile and keep the pavement black between individual dashes.
    float roadEdgeCore = Coverage(abs(x)-3, .021, xFootprint);
    float roadCenterCore = Coverage(x, .018, xFootprint) * dash;
    color += (grid.rgb * .72*(edge+center)
        + road.rgb * .68*(roadEdgeCore+roadCenterCore)) * plain;
    color = ApplyGroundMist(color, input.position.xy, input.surface);
    return FinalOutput(color, input.position.xy);
}
