#include "scene/SceneState.h"
#include "scene/scene-pose.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
using namespace vaporcruise::scene;
namespace {
int failures = 0;
void Check(bool ok, const char* name) { if (!ok) { if (failures < 20) std::cerr << "FAIL: " << name << '\n'; ++failures; } }
bool Close(double a, double b) { return a == b; }
bool Same(const SceneParams& a, const SceneParams& b) {
    const std::array ca{a.palette.sky,a.palette.ground,a.palette.farRidge,a.palette.nearRidge,a.palette.grid,
        a.palette.sunTop,a.palette.sunBottom,a.palette.haze,a.palette.cityWarm,a.palette.highlight,
        a.palette.gridGlow,a.palette.road};
    const std::array cb{b.palette.sky,b.palette.ground,b.palette.farRidge,b.palette.nearRidge,b.palette.grid,
        b.palette.sunTop,b.palette.sunBottom,b.palette.haze,b.palette.cityWarm,b.palette.highlight,
        b.palette.gridGlow,b.palette.road};
    for (size_t i=0;i<ca.size();++i)
        if (!Close(ca[i].r,cb[i].r)||!Close(ca[i].g,cb[i].g)||!Close(ca[i].b,cb[i].b)||!Close(ca[i].a,cb[i].a)) return false;
    return a.terrainSeed==b.terrainSeed && a.cruise.cell==b.cruise.cell && Close(a.cruise.phase,b.cruise.phase)
        && Close(a.gridPhase,b.gridPhase) && Close(a.cityLightPhase,b.cityLightPhase)
        && a.paletteTransitionActive==b.paletteTransitionActive;
}
// Landed stage-3 capture setup, deliberately independent of SetPose.
SceneState Replay(const ScenePose& p) {
    SceneState s(p.seed);
    for(unsigned i=0;i<p.palette;++i) { s.NextPalette(); s.Advance(.8,true); }
    if(p.paletteProgress>0) {
        s.NextPalette();
        s.Advance(p.paletteProgress*.8,true);
    }
    Check(s.SetCruisePosition(p.cruise),"replay cruise");
    Check(s.SetCityLightPhase(p.cityLightPhase),"replay city");
    return s;
}
void Continue(SceneState& a, SceneState& b) {
    for(bool paused : {true,false}) {
        for(double dt : {.1,.35,.8,0.,-1.,std::numeric_limits<double>::quiet_NaN()}) {
            a.Advance(dt,paused); b.Advance(dt,paused); Check(Same(a.Current(),b.Current()),"continued clocks");
        }
        a.NextPalette(); b.NextPalette();
        a.Advance(.1,paused); b.Advance(.1,paused);
        a.NextPalette(); b.NextPalette();
        Check(Same(a.Current(),b.Current()),"interrupted start");
        a.Advance(.35,paused); b.Advance(.35,paused); Check(Same(a.Current(),b.Current()),"interrupted evolution");
        a.Advance(.8,paused); b.Advance(.8,paused); Check(Same(a.Current(),b.Current()),"settled wrap");
    }
}
}
int main() {
    size_t cases=0;
    for(unsigned palette=0;palette<3;++palette)
    for(double pp : {0.,.25,.5,1.}) for(double city : {0.,.25})
    for(auto cell : {0LL,31LL,32LL,36028797018963971LL}) {
        ScenePose p{18446744073709551615ULL,{cell,.25},city,palette,pp};
        SceneState actual(1); Check(actual.SetPose(p),"valid pose");
        auto expected=Replay(p); Check(Same(actual.Current(),expected.Current()),"replay equivalence");
        Continue(actual,expected); ++cases;
    }
    ScenePose good{42,{36028797018963971LL,.75},.9,2,.25};
    const auto reject=[&](ScenePose bad) {
        SceneState a,b; Check(a.SetPose(good)&&b.SetPose(good),"control pose");
        Check(!a.SetPose(bad),"invalid rejected"); Check(Same(a.Current(),b.Current()),"atomic snapshot"); Continue(a,b);
    };
    auto bad=good; bad.palette=3; reject(bad);
    for(double value : {-1.,1.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        bad=good; bad.paletteProgress=value; reject(bad);
        bad=good; bad.cityLightPhase=value; reject(bad); bad=good; bad.cruise.phase=value; reject(bad);
    }
    bad=good; bad.cityLightPhase=1; reject(bad); bad=good; bad.cruise.phase=1; reject(bad);
    bad=good; bad.cruise.cell=-1; reject(bad); bad=good; bad.cruise.cell=kMaxTravelCell+1; reject(bad);
    auto maximum=good; maximum.cruise.cell=kMaxTravelCell;
    SceneState s; Check(s.SetPose(maximum),"maximum cell");
    const auto before=s.Current(); s.Advance(.1,true);
    Check(s.Current().cruise.cell==before.cruise.cell && s.Current().cruise.phase==before.cruise.phase
        && s.Current().cityLightPhase==before.cityLightPhase,"pause freezes motion");
    std::cout << cases << " replay/continuation poses; " << failures << " failures\n";
    return failures?1:0;
}
