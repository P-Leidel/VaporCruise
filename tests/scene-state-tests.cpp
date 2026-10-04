#include "scene/SceneState.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using vaporcruise::common::Color;
using vaporcruise::scene::ScenePalette;
using vaporcruise::scene::SceneState;

namespace {
int failures = 0;
void Check(bool condition, std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool Near(double actual, double expected, double tolerance = 2e-6) {
    return std::abs(actual - expected) <= tolerance;
}
std::array<Color, 15> Tokens(const ScenePalette& p) {
    return {p.sky, p.ground, p.farRidge, p.nearRidge, p.grid, p.sunTop, p.sunBottom, p.haze,
            p.cityWarm, p.highlight, p.gridGlow, p.road, p.regionBase, p.regionShift, p.regionMineral};
}
void SamePalette(const ScenePalette& actual, const ScenePalette& expected,
                 std::string_view message) {
    const auto a = Tokens(actual);
    const auto e = Tokens(expected);
    for (size_t i = 0; i < a.size(); ++i) {
        Check(Near(a[i].r, e[i].r) && Near(a[i].g, e[i].g) &&
              Near(a[i].b, e[i].b) && Near(a[i].a, e[i].a), message);
    }
}
// Fixture expectations use the standard sRGB transfer curve independently of production helpers.
float LinearByte(unsigned byte) {
    const double encoded = byte / 255.0;
    return static_cast<float>(encoded <= 0.04045 ? encoded / 12.92 :
        std::pow((encoded + 0.055) / 1.055, 2.4));
}
Color Fixture(unsigned hex) {
    return {LinearByte((hex >> 16) & 255), LinearByte((hex >> 8) & 255),
            LinearByte(hex & 255), 1.0f};
}
ScenePalette FixturePalette(std::array<unsigned, 7> hex, unsigned accent, unsigned glow, unsigned road, std::array<unsigned,3> regional) {
    return {Fixture(hex[0]), Fixture(hex[1]), Fixture(hex[2]), Fixture(hex[3]),
            Fixture(hex[4]), Fixture(hex[5]), Fixture(hex[6]), Fixture(hex[2]), Fixture(0xFFD49B),
            Fixture(accent), Fixture(glow), Fixture(road), Fixture(regional[0]), Fixture(regional[1]), Fixture(regional[2])};
}
const std::array<ScenePalette, 3> expected = {
    FixturePalette({0x090619, 0x080512, 0x39204F, 0x160D29, 0xED62CF, 0xFFD08C, 0xF36AB8}, 0x83E5DC, 0xB735CF, 0xFFE1F5, {0xD875C2,0xA47ED6,0xBD81CD}),
    FixturePalette({0x06121E, 0x050D17, 0x184453, 0x0B2331, 0x67DDEB, 0xFFE1B0, 0x91DAD8}, 0xECA0BD, 0x229DC5, 0xDDFBFF, {0x69C9C4,0x739FD8,0x74B5CB}),
    FixturePalette({0x140B1D, 0x0D0714, 0x4A2943, 0x241225, 0xF7AD73, 0xFFE0A0, 0xEF877D}, 0xA8B8F3, 0xD26C54, 0xFFF0D2, {0xE7AC73,0xD88591,0xDD9980}),
};
ScenePalette ExpectedBlend(const ScenePalette& from, const ScenePalette& to, float weight) {
    auto a = Tokens(from);
    const auto b = Tokens(to);
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = {a[i].r * (1 - weight) + b[i].r * weight,
                a[i].g * (1 - weight) + b[i].g * weight,
                a[i].b * (1 - weight) + b[i].b * weight, 1.0f};
    }
    return {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14]};
}
void PaletteCycle() {
    SceneState state;
    SamePalette(state.Current().palette, expected[0], "initial linear-light magenta");
    Check(!state.Current().paletteTransitionActive, "initial palette is idle");
    for (int step = 1; step <= 6; ++step) {
        const auto visible = state.Current().palette;
        state.NextPalette();
        SamePalette(state.Current().palette, visible, "palette command never jumps");
        Check(state.Current().paletteTransitionActive, "palette command marks transition active");
        state.Advance(0.8);
        SamePalette(state.Current().palette, expected[step % 3], "palette cycle wraps all fifteen roles");
        Check(!state.Current().paletteTransitionActive, "transition completes at exactly 0.8 seconds");
    }
}
void SmoothTransition() {
    SceneState state;
    state.NextPalette();
    state.Advance(0.2);
    SamePalette(state.Current().palette, ExpectedBlend(expected[0], expected[1], 0.15625f),
                "quarter transition uses smoothstep in linear light");
    state.Advance(0.2);
    SamePalette(state.Current().palette, ExpectedBlend(expected[0], expected[1], 0.5f),
                "half transition has equal linear-light weights");
    state.Advance(0.39);
    Check(state.Current().paletteTransitionActive, "transition stays active until duration");
    state.Advance(0.02);
    SamePalette(state.Current().palette, expected[1], "overshoot clamps to target");
    Check(!state.Current().paletteTransitionActive, "overshoot completes transition");
}
void RapidRetarget() {
    SceneState state;
    state.NextPalette();
    state.Advance(0.2);
    const auto visible = state.Current().palette;
    state.NextPalette();
    SamePalette(state.Current().palette, visible, "retarget begins at visible blend");
    state.Advance(0.4);
    SamePalette(state.Current().palette, ExpectedBlend(visible, expected[2], 0.5f),
                "retarget restarts full duration toward next palette");
    const auto secondVisible = state.Current().palette;
    state.NextPalette();
    SamePalette(state.Current().palette, secondVisible, "repeated retarget remains continuous");
    state.NextPalette();
    SamePalette(state.Current().palette, secondVisible, "zero-time repeated commands remain continuous");
    state.Advance(0.8);
    SamePalette(state.Current().palette, expected[1], "rapid commands cycle target index");
}
void PausedCruise() {
    SceneState state;
    state.Advance(0.125);
    const auto phase = state.Current().gridPhase;
    state.NextPalette();
    state.Advance(0.4, true);
    Check(state.Current().gridPhase == phase, "pause freezes phase exactly");
    SamePalette(state.Current().palette, ExpectedBlend(expected[0], expected[1], 0.5f),
                "pause allows visible palette transition");
    state.Advance(0.4, true);
    Check(!state.Current().paletteTransitionActive, "paused transition returns to idle");
    state.Advance(100.0, true);
    Check(state.Current().gridPhase == phase, "long paused interval cannot accumulate cruise time");
    state.Advance(0.125);
    Check(Near(state.Current().gridPhase, 0.375, 1e-12), "resume advances only supplied interval");
}
void InvalidDelta() {
    SceneState state;
    state.Advance(0.125);
    state.NextPalette();
    state.Advance(0.2);
    const auto before = state.Current();
    for (double delta : {-1.0, -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), 0.0}) {
        state.Advance(delta);
        Check(state.Current().gridPhase == before.gridPhase, "invalid/zero delta preserves phase");
        SamePalette(state.Current().palette, before.palette, "invalid/zero delta preserves visible color");
        Check(state.Current().paletteTransitionActive, "invalid delta preserves transition activity");
    }
    state.Advance(0.6);
    SamePalette(state.Current().palette, expected[1], "invalid deltas do not poison later advance");
}
void GridPrecision() {
    SceneState state;
    state.Advance(0.125);
    Check(Near(state.Current().gridPhase, 0.1875, 1e-12), "grid advances 1.5 cells per second");
    SceneState exact;
    exact.Advance(2.0 / 3.0);
    Check(exact.Current().gridPhase == 0.0, "one exact grid period wraps to zero");
    exact.Advance(2.0);
    Check(exact.Current().gridPhase == 0.0, "three exact grid periods wrap to zero");
    SceneState seam;
    seam.Advance(0.625);
    Check(Near(seam.Current().gridPhase, 0.9375, 1e-12), "phase just before seam");
    seam.Advance(0.0625);
    Check(Near(seam.Current().gridPhase, 0.03125, 1e-12), "phase crosses seam with exact modular distance");
    for (int frame = 0; frame < 8 * 60 * 60 * 60; ++frame) {
        state.Advance(1.0 / 60.0);
        if (!(state.Current().gridPhase >= 0.0 && state.Current().gridPhase < 1.0)) {
            Check(false, "eight-hour run keeps phase bounded"); break;
        }
    }
    Check(Near(state.Current().gridPhase, 0.1875, 1e-8), "eight-hour run preserves repeat phase");
    Check(state.Current().cruise.cell == 43200, "eight-hour run retains completed distance");
    state.Advance(1.0 / 60.0);
    Check(Near(state.Current().gridPhase, 0.2125, 1e-8), "eight-hour run retains per-frame motion precision");
    SceneState large;
    large.Advance(std::numeric_limits<double>::max());
    Check(std::isfinite(large.Current().gridPhase) && large.Current().gridPhase >= 0.0 &&
          large.Current().gridPhase < 1.0, "finite large delta never overflows phase");
}
void ContinuousTravel() {
    using vaporcruise::scene::kMaxTravelCell;
    SceneState state(123);
    Check(state.Current().terrainSeed == 123, "route seed retained");
    Check(state.SetCruisePosition({31, .9375}), "valid cruise pose accepted");
    state.Advance(.0625);
    Check(state.Current().cruise.cell == 32, "fraction wrap carries completed cell");
    Check(Near(state.Current().cruise.phase, .03125, 1e-12), "fraction carry preserves motion");
    const auto paused = state.Current().cruise;
    state.NextPalette(); state.Advance(.8, true);
    Check(state.Current().cruise.cell == paused.cell && state.Current().cruise.phase == paused.phase,
          "pause freezes the complete cruise position");
    Check(!state.Current().paletteTransitionActive, "paused palette completes");
    Check(state.SetCruisePosition({1LL << 55, 0}), "distant cruise pose accepted exactly");
    state.Advance(.125);
    Check(state.Current().cruise.cell == (1LL << 55) && Near(state.Current().cruise.phase, .1875, 1e-12),
          "far position retains small-step precision");
    const auto before = state.Current().cruise;
    for (const vaporcruise::scene::CruisePosition invalid : {
        vaporcruise::scene::CruisePosition{-1, 0}, {kMaxTravelCell + 1, 0}, {0, 1}, {0, -.01},
        {0, std::numeric_limits<double>::quiet_NaN()}}) {
        Check(!state.SetCruisePosition(invalid), "invalid pose rejected");
        Check(state.Current().cruise.cell == before.cell && state.Current().cruise.phase == before.phase,
              "rejected pose preserves route");
    }
    Check(state.SetCruisePosition({kMaxTravelCell, .75}), "maximum safe pose accepted");
    state.NextPalette(); state.Advance(1);
    Check(state.Current().cruise.cell == kMaxTravelCell && state.Current().cruise.phase == .75,
          "overflowing travel leaves full pose intact");
    Check(!state.Current().paletteTransitionActive, "overflow cannot block palette completion");
    state.Advance(std::numeric_limits<double>::max());
    Check(state.Current().cruise.cell == kMaxTravelCell && state.Current().cruise.phase == .75,
          "huge finite delta leaves full pose intact");
}
}
void CityLightClock() {
    SceneState state;
    Check(state.Current().cityLightPhase == 0, "city phase starts at zero");
    state.Advance(21);
    Check(Near(state.Current().cityLightPhase, .5, 1e-12), "42 second light period");
    state.Advance(21);
    Check(Near(state.Current().cityLightPhase, 0, 1e-12), "light period wraps");
    Check(state.SetCityLightPhase(.999), "bounded light phase setter");
    state.Advance(.084);
    Check(Near(state.Current().cityLightPhase, .001, 1e-12), "phase seam preserves remainder");
    for (double delta : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        const auto held = state.Current().cityLightPhase;
        state.Advance(delta);
        Check(state.Current().cityLightPhase == held, "invalid light delta ignored");
    }
    for (double phase : {-1.0, 1.0, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        const auto held = state.Current().cityLightPhase;
        Check(!state.SetCityLightPhase(phase) && state.Current().cityLightPhase == held,
              "invalid setter preserves phase");
    }
    state.Advance(std::numeric_limits<double>::max());
    Check(std::isfinite(state.Current().cityLightPhase) && state.Current().cityLightPhase >= 0 &&
          state.Current().cityLightPhase < 1, "huge finite light delta stays bounded");
    state.SetCityLightPhase(0);
    for (int i = 0; i < 3 * 24 * 60 * 60; ++i) state.Advance(1);
    Check(Near(state.Current().cityLightPhase, std::fmod(3 * 86400.0, 42.0) / 42.0, 1e-9),
          "three simulated days preserve light phase");
}
void PausedCityPalette() {
    SceneState state;
    state.SetCityLightPhase(.3);
    const auto held = state.Current().cruise;
    const auto first = state.Current().palette;
    Check(Near(first.cityWarm.r, Fixture(0xFFD49B).r), "linear warm window token");
    Check(Near(first.highlight.g, Fixture(0x83E5DC).g), "initial turquoise accent");
    state.NextPalette(); state.Advance(.4, true);
    Check(Near(state.Current().palette.highlight.r,
          (Fixture(0x83E5DC).r + Fixture(0xECA0BD).r) * .5), "city accent blends in linear light");
    state.Advance(.4, true);
    Check(!state.Current().paletteTransitionActive && state.Current().cityLightPhase == .3 &&
          state.Current().cruise.cell == held.cell && state.Current().cruise.phase == held.phase,
          "pause freezes cruise and lights while palette settles");
    state.NextPalette(); state.Advance(.8, true);
    Check(Near(state.Current().palette.highlight.b, Fixture(0xA8B8F3).b), "periwinkle accent");
    state.Advance(.42);
    Check(Near(state.Current().cityLightPhase, .31, 1e-12), "resume uses only fresh delta");
}
int main() {
    PaletteCycle(); SmoothTransition(); RapidRetarget(); PausedCruise(); InvalidDelta(); GridPrecision();
    ContinuousTravel(); CityLightClock(); PausedCityPalette();
    if (failures) { std::cerr << failures << " scene-state assertions failed\n"; return 1; }
    std::cout << "PASS: 6 scene-state scenarios (palette cycle, smooth blend, retarget, pause, invalid delta, 8h precision)\n";
    return 0;
}
