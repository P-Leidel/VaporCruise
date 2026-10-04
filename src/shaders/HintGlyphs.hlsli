// Original VaporCruise 5x7 bitmap glyphs. No font file or third-party asset.
static const uint2 kGlyphs[26] = {
    uint2(1033774u, 17969u), // A
    uint2(1001022u, 31281u), // B
    uint2(541199u, 15888u), // C
    uint2(575038u, 31281u), // D
    uint2(999967u, 32272u), // E
    uint2(999967u, 16912u), // F
    uint2(639503u, 15921u), // G
    uint2(1033777u, 17969u), // H
    uint2(135310u, 14468u), // I
    uint2(67655u, 12882u), // J
    uint2(807505u, 18004u), // K
    uint2(541200u, 32272u), // L
    uint2(710513u, 17969u), // M
    uint2(714545u, 18035u), // N
    uint2(575022u, 14897u), // O
    uint2(1001022u, 16912u), // P
    uint2(575022u, 13909u), // Q
    uint2(1001022u, 18004u), // R
    uint2(475663u, 30753u), // S
    uint2(135327u, 4228u), // T
    uint2(575025u, 14897u), // U
    uint2(575025u, 4433u), // V
    uint2(706097u, 18293u), // W
    uint2(141873u, 17962u), // X
    uint2(141873u, 4228u), // Y
    uint2(133183u, 32264u), // Z
};
// P PALETTE   F FULLSCREEN
static const uint kFirst[24] = {15,26,15,0,11,4,19,19,4,26,26,26,5,26,5,20,11,11,18,2,17,4,4,13};
static const uint kFirstLength = 24;
static const uint kSecond[34] = {18, 15, 0, 2, 4, 26, 15, 0, 20, 18, 4, 26, 26, 26, 4, 18, 2, 26, 22, 8, 13, 3, 14, 22, 26, 26, 26, 7, 26, 7, 8, 13, 19, 18};
static const uint kSecondLength = 34;
// L FRAME LIMIT   ALT SPACE MENU
static const uint kThird[30] = {11,26,5,17,0,12,4,26,11,8,12,8,19,26,26,26,0,11,19,26,18,15,0,2,4,26,12,4,13,20};
static const uint kThirdLength = 30;

float Glyph(uint character, int2 pixel) {
    if (character >= 26 || pixel.x < 0 || pixel.x >= 5 || pixel.y < 0 || pixel.y >= 7) return 0;
    uint bits = pixel.y < 4 ? kGlyphs[min(character, 25u)].x : kGlyphs[min(character, 25u)].y;
    uint shift = (uint(pixel.y) & 3u) * 5u + 4u - uint(pixel.x);
    return float((bits >> shift) & 1u);
}
float3 HintOverlay(float2 pixel, float3 background) {
    float3 color = background;
    [branch] if (hintOpacity > 0) {
        float scale = max(1.0, min(floor(2.0 * uiScale + .5), floor((resolution.x - 32.0) / (kSecondLength * 6.0 + 16.0))));
        float2 boxSize = float2(kSecondLength * 6 + 16, 38) * scale;
        float2 origin = float2(floor((resolution.x - boxSize.x) * .5), resolution.y - boxSize.y - 18 * scale);
        float2 local = (pixel - origin) / scale;
        if (all(local >= 0) && all(local < boxSize / scale)) {
            color = lerp(background, ground.rgb * .55, .82 * hintOpacity);
            int row = local.y >= 26 ? 2 : (local.y >= 14 ? 1 : 0);
            int2 glyphPixel = int2(floor(local - float2(8, 3 + row * 12)));
            uint index = uint(max(glyphPixel.x, 0)) / 6u;
            uint count = row == 0 ? kFirstLength : (row == 1 ? kSecondLength : kThirdLength);
            if (glyphPixel.x >= 0 && index < count) {
                uint character = row == 0 ? kFirst[min(index, kFirstLength - 1u)] :
                    (row == 1 ? kSecond[min(index, kSecondLength - 1u)] : kThird[min(index, kThirdLength - 1u)]);
                float ink = Glyph(character, int2(uint(max(glyphPixel.x, 0)) % 6u, glyphPixel.y));
                color = lerp(color, lerp(float3(.62, .67, .72), grid.rgb, .16), ink * hintOpacity);
            }
        }
    }
    return color;
}
