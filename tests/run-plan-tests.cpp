#include "app/run-plan.h"
#include "app/option-values.h"
#include <iostream>
#include <limits>
using namespace vaporcruise::app;
int main() {
    int failures=0;
    const auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; ++failures; } };
    RunPlan plan; std::wstring error;
    check(ParseRunPlan({},plan,error)&&std::holds_alternative<InteractivePlan>(plan),"interactive");
    check(ParseRunPlan({L"--capture",L"a.png"},plan,error),"minimal capture");
    auto capture=std::get<CapturePlan>(plan);
    check(capture.options.width==1280&&capture.options.height==720&&capture.pose.seed==vaporcruise::scene::kDefaultTerrainSeed
        &&capture.pose.paletteProgress==0,"capture defaults");
    check(ParseRunPlan({L"--palette",L"2",L"--capture",L"a space/image.png",L"--seed",L"18446744073709551615",
        L"--travel-cell",L"36028797018963971",L"--phase",L".25",L"--city-light-phase",L".75",
        L"--transition",L".25",
        L"--width",L"480",L"--height",L"320",L"--hint",L"1",L"--dpi",L"192"},plan,error),"full capture");
    capture=std::get<CapturePlan>(plan);
    check(capture.pose.seed==18446744073709551615ULL&&capture.pose.cruise.cell==36028797018963971LL
        &&capture.pose.cruise.phase==.25&&capture.pose.cityLightPhase==.75&&capture.pose.palette==2
        &&capture.pose.paletteProgress==.25&&capture.options.width==480&&capture.options.height==320
        &&capture.options.hint==1&&capture.options.dpi==192,"complete pose mapping");
    check(ParseRunPlan({L"--frame-limit",L"60",L"--measure",L"capped.csv"},plan,error)
        && std::get<MeasurementPlan>(plan).options.frameLimit == 60,"cap before mode remains measurement");
    check(!ParseRunPlan({L"--frame-limit",L"60"},plan,error),"frame cap requires measurement mode");
    check(!ParseRunPlan({L"--capture",L"shot.png",L"--frame-limit",L"60"},plan,error),"capture cannot select live pacing");
    check(ParseRunPlan({L"--measure",L"run.csv"},plan,error),"minimal measurement");
    check(ParseRunPlan({L"--gpu-detail",L"--measure",L"run.csv"},plan,error)
        && std::holds_alternative<MeasurementPlan>(plan) && std::get<MeasurementPlan>(plan).options.gpuDetail,
        "GPU detail before measure selects the measurement mode");
    check(!ParseRunPlan({L"--gpu-detail"},plan,error)
        && !ParseRunPlan({L"--gpu-detail",L"--capture",L"run.png"},plan,error),
        "GPU detail cannot select interactive or still capture");
    check(ParseRunPlan({L"--measure",L"run.csv"},plan,error),"measurement defaults restored");
    auto measure=std::get<MeasurementPlan>(plan);
    check(measure.options.width==1920&&measure.options.height==1080&&measure.options.warmupSeconds==30
        &&measure.options.durationSeconds==300&&measure.options.presentInterval==1&&!measure.options.fullscreen&&measure.options.frameLimit==0
        &&measure.pose.seed==vaporcruise::scene::kDefaultTerrainSeed&&measure.pose.palette==0,"measurement defaults");
    check(ParseRunPlan({L"--fullscreen",L"--palette",L"2",L"--measure",L"a space/run.csv",L"--duration",L"1",
        L"--warmup",L"0",L"--present-interval",L"0",L"--width",L"240",L"--height",L"7680"},plan,error),"reordered measurement");
    measure=std::get<MeasurementPlan>(plan);
    check(measure.pose.palette==2&&measure.pose.paletteProgress==0
        &&measure.pose.cruise.cell==0&&measure.pose.cityLightPhase==0&&measure.options.fullscreen
        &&measure.options.width==240&&measure.options.height==7680&&measure.options.durationSeconds==1
        &&measure.options.warmupSeconds==0&&measure.options.presentInterval==0,"measurement mapping");
    check(ParseRunPlan({L"--capture",L"--measure.png"},plan,error)&&std::holds_alternative<CapturePlan>(plan),"mode-looking capture value");
    check(ParseRunPlan({L"--measure",L"--capture.csv"},plan,error)&&std::holds_alternative<MeasurementPlan>(plan),"mode-looking measure value");
    const std::vector<std::vector<std::wstring>> invalid={
        {L"--capture",L"a.png",L"--measure",L"r.csv"},{L"--measure",L"r.csv",L"--capture",L"a.png"},
        {L"--capture"},{L"--measure"},{L"--capture",L"a.png",L"--capture",L"b.png"},
        {L"--measure",L"a.csv",L"--measure",L"b.csv"},{L"--fullscreen"},{L"--palette",L"1"},
        {L"--seed",L"1"},{L"--unknown",L"0"},{L"--capture",L"a.png",L"--fullscreen"},
        {L"--measure",L"r.csv",L"--fullscreen",L"--fullscreen"},{L"--measure",L"r.csv",L"--duration",L"nan"}};
    for(const auto& args:invalid) {
        check(!ParseRunPlan(args,plan,error)&&!error.empty(),"invalid plan rejected");
        check(std::holds_alternative<MeasurementPlan>(plan)&&std::get<MeasurementPlan>(plan).options.path==L"--capture.csv","failure preserves plan");
    }
    std::uint64_t integer=99;
    check(ParseUnsignedDecimal(L"18446744073709551615",std::numeric_limits<std::uint64_t>::max(),integer)
        &&integer==18446744073709551615ULL,"uint64 maximum");
    check(ParseUnsignedDecimal(L"0",0,integer)&&integer==0,"zero maximum");
    for(const auto* bad:{L"9",L"-1",L"+1",L"1.0",L"1e2",L" 1",L"1 ",L"1x",L""}) {
        integer=99; check(!ParseUnsignedDecimal(bad,2,integer)&&integer==99,"small maximum and strict decimal");
    }
    integer=99; check(!ParseUnsignedDecimal(L"18446744073709551616",std::numeric_limits<std::uint64_t>::max(),integer)&&integer==99,"integer overflow");
    double real=99;
    check(ParseFiniteNumber(L" 1e2",real)&&real==100,"preserve stod real syntax");
    for(const auto* bad:{L"nan",L"inf",L"-inf",L"1x",L"1 ",L""}) {
        real=99; check(!ParseFiniteNumber(bad,real)&&real==99,"finite full consumption");
    }
    std::cout<<failures<<" run-plan failures\n"; return failures?1:0;
}
