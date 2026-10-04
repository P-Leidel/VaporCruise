#include "render/scene-frame.h"
#if __has_include("scene/regional-color.h")
#include "scene/regional-color.h"
#define HAS_REGIONAL_COLOR 1
#endif
#include <cmath>
#include <cstring>
#include <iostream>
using namespace vaporcruise;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { ++failures;std::cerr<<"FAIL: "<<message<<'\n'; } };
    check(sizeof(render::FrameConstantsGPU)==592,"frame appends regional constants after the preserved 288-byte prefix");
#ifdef HAS_REGIONAL_COLOR
    for(const std::int64_t boundary : {32LL,128LL,(1LL<<55)+128,scene::kMaxTravelCell-255}) {
        const auto a=scene::BuildRegionalColorField(123,{boundary-1,.75});
        const auto repeat=scene::BuildRegionalColorField(123,{boundary-1,.75});
        const auto b=scene::BuildRegionalColorField(123,{boundary,.0});
        check(std::memcmp(&a,&repeat,sizeof(a))==0,"same full route address reproduces the field");
        const auto shift=(boundary%128==0)?1:0;
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)
            check(a.knots[(row+shift)*3+col]==b.knots[row*3+col],"overlapping region windows retain absolute-address knots");
        for(const auto& knot:b.knots) {
            check(std::abs(knot[0]+knot[1]+knot[2]-1)<1e-6,"regional hue weights form a convex mixture");
            check(knot[0]>=0&&knot[1]>=0&&knot[2]>=0&&knot[3]>=0&&knot[3]<=1,"finite bounded hue and mineral weights");
        }
        check(b.control[0]>=0&&b.control[0]<128,"only a bounded integer remainder reaches the GPU");
    }
    const auto origin=scene::BuildRegionalColorField(123,{0,0});
    const auto distant=scene::BuildRegionalColorField(123,{1LL<<55,0});
    const auto reseeded=scene::BuildRegionalColorField(124,{0,0});
    check(origin.knots!=distant.knots,"addresses beyond double precision do not alias origin");
    check(origin.knots!=reseeded.knots,"terrain seed changes regional geology");
    unsigned eligible=0;
    for(std::int64_t region=0;region<64;++region) {
        const auto field=scene::BuildRegionalColorField(123,{region*128,0});
        for(const auto& knot:field.knots)eligible+=knot[3]>0?1:0;
    }
    check(eligible>0&&eligible<64*15/3,"mineral eligibility is sparse without disappearing from the route");
    check(origin.control[1]==1.f/128&&origin.control[2]==.12f&&origin.control[3]==0,"field exposes shared spacing, grid strength and a reserved zero");
    const auto atMaximum=scene::BuildRegionalColorField(123,{scene::kMaxTravelCell,.75});
    check(atMaximum.control[0]==127.75f,"maximum route retains its bounded fractional phase");
    const auto justBefore=scene::BuildRegionalColorField(123,{127,.999999999});
    check(justBefore.control[0]<=128&&std::isfinite(justBefore.control[0]),"float rounding at rollover remains within bounded support");
    check(offsetof(render::FrameConstantsGPU,regionalControl)==288&&offsetof(render::FrameConstantsGPU,regionalHue)==304&&offsetof(render::FrameConstantsGPU,regionalKnots)==352,"regional ABI offsets");
    scene::SceneState state;
    for(int palette=0;palette<3;++palette) {
        const auto frame=render::PackSceneFrame(state.Current(),0,1,1280,720);
        for(const auto& hue:frame.regionalHue)
            check(std::abs(.2126f*hue[0]+.7152f*hue[1]+.0722f*hue[2]-1)<1e-6,"palette targets have unit linear luminance");
        state.NextPalette();state.Advance(.8,true);
    }
#else
    check(false,"shared world-anchored regional field is available");
#endif
    std::cout<<(failures?"FAIL":"PASS")<<": regional field and frame contract ("<<failures<<")\n";
    return failures?1:0;
}
