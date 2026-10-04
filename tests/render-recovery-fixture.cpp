#include "render/Renderer.h"
#include "render/d3d11-frame-probe.h"
#include "scene/scene-pose.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
using namespace vaporcruise;
namespace {
struct Injection {
    const wchar_t* stage=L""; HRESULT hr=DXGI_ERROR_DEVICE_RESET;
    unsigned remaining=1, hits=0, recreations=0, skip=0; bool failRecreation=false;
    static HRESULT Hook(const wchar_t* stage,void* data) {
        auto& self=*static_cast<Injection*>(data);
        if(std::wstring_view(stage)==L"resource recreation") {
            ++self.recreations;
            if(self.failRecreation)return DXGI_ERROR_DEVICE_REMOVED;
        }
        if(std::wstring_view(stage)==self.stage && self.skip) {--self.skip;return S_OK;}
        if(std::wstring_view(stage)==self.stage && self.remaining) {--self.remaining;++self.hits;return self.hr;}
        return S_OK;
    }
};
class QueryCreationFailureBackend final : public render::FrameQueryBackend {
public:
    explicit QueryCreationFailureBackend(bool succeedFirst=false) : succeedFirst_(succeedFirst) {}
    bool Create(ID3D11Device*, ID3D11DeviceContext*, std::wstring& error) override {
        if(++creations_==1 && succeedFirst_) return true;
        error=L"GPU timing query creation failed (HRESULT 0x8007000E).";
        return false;
    }
    void Release() override {}
    void Begin(size_t) override {}
    void End(size_t) override {}
    render::QueryResult Poll(size_t) override { return {}; }
private:
    bool succeedFirst_;
    unsigned creations_=0;
};
uint64_t TerrainHash(const scene::SceneFrameSnapshot& frame) {
    uint64_t hash=1469598103934665603ULL;
    for(const auto& slot:frame.terrainSlots) {
        hash=(hash^slot.revision)*1099511628211ULL;
        const auto* bytes=reinterpret_cast<const unsigned char*>(slot.vertices.data());
        for(size_t i=0;i<sizeof(slot.vertices);++i)hash=(hash^bytes[i])*1099511628211ULL;
    }
    return hash;
}
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 && argc!=3)return 2;
    const bool terrainOnly=argc==3 && std::wstring_view(argv[2])==L"--terrain-only";
    const bool queryFailureOnly=argc==3 && std::wstring_view(argv[2])==L"--query-failure-only";
    const std::filesystem::path output(argv[1]);
    if(std::filesystem::exists(output))return 3;
    std::filesystem::create_directories(output);
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"VaporCruiseRecoveryFixture";
    if(!RegisterClassW(&cls))return 4;
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"Recovery",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,cls.hInstance,nullptr);
    if(!hwnd)return 5;
    int failures=0;
    auto check=[&](bool ok,const char* msg){if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}};
    if(queryFailureOnly) {
        std::ofstream diagnostic(output/"query-failure.json");
        diagnostic<<"{\"cases\":[";
        for(bool recovery:{false,true}) {
            QueryCreationFailureBackend backend(recovery);
            render::D3D11FrameProbe probe(1,backend,Now);
            render::Renderer renderer(probe);
            if(recovery) {
                check(renderer.Initialize(hwnd,640,360),"query recreation fixture initializes");
                Injection loss;loss.stage=L"swap chain resize";
                renderer.SetFailureHook(Injection::Hook,&loss);
                const auto result=renderer.Resize(800,600);
                check(result.outcome==render::RenderOutcome::Failed && result.hr==E_FAIL,"query recreation failure remains terminal with boundary classification");
                check(std::wstring_view(result.stage)==L"measurement query creation","query recreation keeps stable boundary stage");
                check(loss.recreations==1 && renderer.ResourceGeneration()==1,"failed query recreation neither retries nor certifies a new generation");
                renderer.SetFailureHook(nullptr,nullptr);
            } else {
                check(!renderer.Initialize(hwnd,640,360),"query creation failure remains terminal");
                check(renderer.ResourceGeneration()==0,"failed query creation does not certify a resource generation");
            }
            check(renderer.LastError().find(L"0x8007000E")!=std::wstring::npos,recovery?"query recreation preserves backend HRESULT diagnostic":"query creation preserves backend HRESULT diagnostic");
            check(renderer.LastError().find(L"0x80004005")!=std::wstring::npos,"query failure retains boundary HRESULT diagnostic");
            if(recovery)diagnostic<<',';
            diagnostic<<"{\"recovery\":"<<(recovery?"true":"false")<<",\"error\":\""<<render::MeasurementUtf8(renderer.LastError())<<"\"}";
        }
        diagnostic<<"],\"passed\":"<<(failures?"false":"true")<<"}\n";
        DestroyWindow(hwnd);UnregisterClassW(cls.lpszClassName,cls.hInstance);
        return failures?1:0;
    }
    std::ofstream report(output/"attempts.json");report<<"{\"cases\":[";
    std::ofstream outcomes(output/"outcomes.json");outcomes<<"[";unsigned outcomeCount=0;
    auto record=[&](const wchar_t* stage,render::RenderResult result,const Injection& injection) {
        if(outcomeCount++)outcomes<<',';
        std::wstring ws(stage),failureStage(result.stage);
        outcomes<<"{\"stage\":\""<<render::MeasurementUtf8(ws)<<"\",\"outcome\":"<<static_cast<int>(result.outcome)
                <<",\"failure_stage\":\""<<render::MeasurementUtf8(failureStage)<<"\",\"hr\":"<<result.hr
                <<",\"recreations\":"<<injection.recreations<<",\"injected_hits\":"<<injection.hits<<"}";
    };
    unsigned index=0;
    {
        render::D3D11FrameProbe probe(1024); render::Renderer renderer(probe);scene::SceneState state;
        check(renderer.Initialize(hwnd,1280,720),"initialization");
#ifdef _DEBUG
        if(terrainOnly) {
            state.SetCruisePosition({31,.75});
            for(unsigned expected:{363u,4u}) {
                const auto frame=state.PrepareFrame();
                check(frame.rowsBuilt==expected,"nonzero preparation before simulated loss");
                const auto rowCount=probe.Rows().size();
                probe.FrameStart(Now(),expected);
                renderer.SimulateDeviceLoss();
                const auto simulated=renderer.RenderFrame({frame,0,1});
                check(simulated.outcome==render::RenderOutcome::Recovered,"simulated loss recovers");
                check(probe.Rows().size()==rowCount+1,"simulated loss admits measured preparation");
                if(probe.Rows().size()==rowCount+1)
                    check(probe.Rows().back().terrainRowsBuilt==expected,"simulated loss records CPU rows once");
                check(renderer.CaptureFrame({frame,0,1},(output/(L"simulated-"+std::to_wstring(expected)+L".png")).wstring()).outcome==render::RenderOutcome::Drawn,"recovery borrows same prepared frame");
                state.Advance(.25);
            }
            std::wstring error;
            check(render::WriteMeasurements(output/L"measurement.csv",probe.Rows(),probe.BuildMetadata(renderer.MeasurementContext(),0,1,0),error),"accounting export");
            outcomes<<"]\n";outcomes.close();
            report<<"],\"completion\":"<<(failures?"false":"true")<<",\"passed\":"<<(failures?"false":"true")<<"}\n";report.close();
            DestroyWindow(hwnd);UnregisterClassW(cls.lpszClassName,cls.hInstance);
            return failures?1:0;
        }
#endif
        // Successful Present is the only operation that resets consecutive recovery budget.
        auto present=[&] {
            check(WaitForSingleObject(renderer.LatencyHandle(),2000)==WAIT_OBJECT_0,"latency");
            probe.FrameStart(Now(),index);
            return renderer.RenderFrame({state.PrepareFrame(),.7f,1.5f});
        };
        for(const auto* stage:{L"scene constant upload",L"terrain preparation",L"terrain draw",L"frame present",L"swap chain resize"}) {
            Injection injection;injection.stage=stage;
            renderer.SetFailureHook(Injection::Hook,&injection);
            const auto before=renderer.ResourceGeneration(); const auto rows=probe.Rows().size();
            auto result=std::wstring_view(stage)==L"swap chain resize"?renderer.Resize(800,600):present();
            record(stage,result,injection);
            check(result.outcome==render::RenderOutcome::Recovered && injection.hits==1 && injection.recreations==1,"live stage recovers once");
            check(renderer.ResourceGeneration()>before && renderer.LatencyHandle()!=nullptr,"readiness replaced");
            renderer.SetFailureHook(nullptr,nullptr);
            if(std::wstring_view(stage)!=L"swap chain resize") {
                const auto unavailable=probe.BuildMetadata({},0,0,0).queryUnavailable;
                probe.AbortAttempt();probe.FinalPoll();
                check(probe.Rows().size()==rows+1 && probe.BuildMetadata({},0,0,0).queryUnavailable==unavailable,"measurement abort counted once");
                check(!std::isfinite(probe.Rows().back().gpuMs),"failed GPU stays blank");
            }
            check(present().outcome==render::RenderOutcome::Drawn,"successful Present resets live budget");
        }
        const wchar_t* captureStages[]={L"scene constant upload",L"terrain preparation",L"terrain draw",L"capture backbuffer query",L"capture staging texture",L"capture GPU readback"};
        for(auto size:{std::array<unsigned,2>{1280,720},{480,320},{720,1280}})
          for(unsigned poseIndex=0;poseIndex<3;++poseIndex) {
            scene::ScenePose pose;pose.palette=poseIndex;
            pose.paletteProgress=poseIndex? .5:0;
            pose.cruise={poseIndex==2?36028797018963971LL:31LL,.25};pose.cityLightPhase=.37;
            check(state.SetPose(pose),"pose installation");
            renderer.SetFailureHook(nullptr,nullptr);check(renderer.Resize(size[0],size[1]).outcome==render::RenderOutcome::Drawn,"resize");
            for(unsigned stageIndex=0;stageIndex<6;++stageIndex) {
                if(index)report<<',';
                report<<"{\"index\":"<<index<<",\"width\":"<<size[0]<<",\"height\":"<<size[1]<<",\"palette\":"<<poseIndex
                      <<",\"seed\":\""<<pose.seed<<"\",\"palette_progress\":"<<pose.paletteProgress<<",\"travel_cell\":\""<<pose.cruise.cell<<"\",\"travel_phase\":0.25,\"city_phase\":0.37,\"hint\":0.7,\"ui_scale\":1.5,\"stage_index\":"<<stageIndex<<",\"attempts\":[";
                // Fixed independent references for each stage; never pool
                // captures from different injection stages for acceptance.
                for(unsigned sample=0;sample<(size[0]==480?29u:5u);++sample) {
                    renderer.SetFailureHook(nullptr,nullptr);check(present().outcome==render::RenderOutcome::Drawn,"reset budget before pair");
                    const render::RenderInput input{state.PrepareFrame(),.7f,1.5f};
                    const auto prefix=std::to_wstring(index)+L"-"+std::to_wstring(sample);
                    check(renderer.CaptureFrame(input,(output/(prefix+L"-control.png")).wstring()).outcome==render::RenderOutcome::Drawn,"control capture");
                    Injection injection;injection.stage=captureStages[stageIndex];injection.hr=sample%2?DXGI_ERROR_DEVICE_REMOVED:DXGI_ERROR_DEVICE_RESET;
                    renderer.SetFailureHook(Injection::Hook,&injection);
                    const auto result=renderer.CaptureFrame(input,(output/(prefix+L"-recovered.png")).wstring());
                    check(result.outcome==render::RenderOutcome::Drawn && injection.hits==1 && injection.recreations==1,"same-pose capture retry");
                    renderer.SetFailureHook(nullptr,nullptr);
                    if(sample)report<<',';
                    report<<"{\"outcome\":"<<static_cast<int>(result.outcome)<<",\"hr\":"<<result.hr<<",\"loss_hr\":"<<injection.hr<<",\"recreations\":"<<injection.recreations<<"}";
                }
                report<<"]}";++index;
            }
        }
        // Keep one partially prepared CPU window stable through complete device replacement.
        std::ofstream terrainReport(output/"terrain-ownership.json");
        terrainReport<<"{\"cases\":[";
        unsigned terrainCases=0;
        for(bool partial:{false,true}) for(const auto* loss:{L"terrain vertex upload",L"capture GPU readback",L"frame present"}) {
            renderer.SetFailureHook(nullptr,nullptr);
            check(renderer.RenderFrame({state.PrepareFrame(),0,1}).outcome==render::RenderOutcome::Drawn,"terrain budget reset");
            scene::ScenePose terrainPose;terrainPose.cruise={31,.75};check(state.SetPose(terrainPose),"terrain fixture pose");
            state.PrepareFrame();
            if(partial)state.Advance(.25);
            const render::RenderInput input{state.PrepareFrame(),0,1};
            const auto hash=TerrainHash(input.frame);unsigned complete=0;
            for(const auto& slot:input.frame.terrainSlots)if(slot.complete)++complete;
            check(input.frame.rowsBuilt==(partial?4u:0u),"exact original preparation rows");
            check(renderer.Initialize(hwnd,640,360),"fresh upload device");
            const auto originalGeneration=renderer.ResourceGeneration();
            Injection lossInjection;lossInjection.stage=loss;lossInjection.skip=std::wstring_view(loss)==L"terrain vertex upload"?3:0;
            renderer.SetFailureHook(Injection::Hook,&lossInjection);
            const auto path=output/(L"ownership-"+std::to_wstring(terrainCases)+L".png");
            render::RenderResult result;
            if(std::wstring_view(loss)==L"frame present") {
                probe.FrameStart(Now(),terrainCases);
                result=renderer.RenderFrame(input);
                check(result.outcome==render::RenderOutcome::Recovered,"Present loss keeps prepared CPU snapshot");
                check(probe.Rows().back().terrainRowsBuilt==input.frame.rowsBuilt,"failed Present accounts original CPU rows once");
                renderer.SetFailureHook(nullptr,nullptr);
                result=renderer.CaptureFrame(input,path.wstring());
            } else result=renderer.CaptureFrame(input,path.wstring());
            check(result.outcome==render::RenderOutcome::Drawn && lossInjection.recreations==1,"prepared snapshot recovery completes");
            check(TerrainHash(input.frame)==hash,"CPU vertices and revisions unchanged by recovery");
            unsigned uploaded=0;
            for(size_t i=0;i<input.frame.terrainSlots.size();++i) {
                const auto count=renderer.TerrainUploadsForTesting()[i];
                check(count==(input.frame.terrainSlots[i].complete?1u:0u),"new device uploads each complete slot once");uploaded+=count;
            }
            renderer.SetFailureHook(nullptr,nullptr);
            check(renderer.CaptureFrame(input,path.wstring()).outcome==render::RenderOutcome::Drawn,"same snapshot second draw");
            unsigned repeatedUploads=0;for(auto count:renderer.TerrainUploadsForTesting())repeatedUploads+=count;
            check(repeatedUploads==uploaded && uploaded==complete,"unchanged draw performs zero uploads");
            if(terrainCases++)terrainReport<<',';
            terrainReport<<"{\"stage\":\""<<render::MeasurementUtf8(loss)<<"\",\"generation_before\":"<<originalGeneration<<",\"generation_after\":"<<renderer.ResourceGeneration()<<",\"seed\":\""<<input.frame.scene.terrainSeed<<"\",\"cell\":\""<<input.frame.scene.cruise.cell<<"\",\"phase\":"<<input.frame.scene.cruise.phase<<",\"palette\":0,\"palette_progress\":0,\"city_phase\":"<<input.frame.scene.cityLightPhase<<",\"hint\":0,\"ui_scale\":1,\"width\":640,\"height\":360,\"outcome\":"<<static_cast<int>(result.outcome)<<",\"hr\":"<<result.hr<<",\"partial\":"<<(partial?"true":"false")<<",\"original_rows\":"<<input.frame.rowsBuilt
                <<",\"recovery_rows\":0,\"complete_slots\":"<<complete<<",\"uploaded_slots\":"<<uploaded
                <<",\"hash_before\":\""<<hash<<"\",\"hash_after\":\""<<TerrainHash(input.frame)<<"\",\"preparations\":1}";
        }
        terrainReport<<"],\"completion\":"<<(failures?"false":"true")<<"}";
        // Capture second loss and recreation loss fail with their exact stage, never encode.
        for(unsigned scenario=0;scenario<4;++scenario) {
            renderer.SetFailureHook(nullptr,nullptr);check(present().outcome==render::RenderOutcome::Drawn,"reset failure case");
            Injection injection;injection.stage=L"capture GPU readback";
            if(scenario==0)injection.remaining=2;
            if(scenario==1)injection.failRecreation=true;
            if(scenario==2)injection.hr=E_INVALIDARG;
            if(scenario==3){injection.stage=L"capture PNG encoding";injection.hr=E_FAIL;}
            renderer.SetFailureHook(Injection::Hook,&injection);
            const auto path=output/(L"failed-"+std::to_wstring(scenario)+L".png");
            const auto result=renderer.CaptureFrame({state.PrepareFrame(),0,1},path.wstring());
            record(injection.stage,result,injection);
            check(result.outcome==render::RenderOutcome::Failed,"capture failure terminal");
            check(std::wstring_view(result.stage)==(scenario==1?L"resource recreation":injection.stage),"failure stage preserved");
            check(injection.recreations==(scenario<2?1u:0u) && !std::filesystem::exists(path),"no second recreation or partial image");
            renderer.SetFailureHook(nullptr,nullptr);
            if(scenario==1)check(renderer.Initialize(hwnd,1280,720),"restore terminal recreation fixture");
        }
        // An actual WIC destination failure, without injection.
        check(renderer.CaptureFrame({state.PrepareFrame(),0,1},(output/L"missing-parent"/L"image.png").wstring()).outcome==render::RenderOutcome::Failed,"actual encoding failure");
        check(present().outcome==render::RenderOutcome::Drawn,"reset live limit");
        Injection repeated;repeated.stage=L"scene constant upload";repeated.remaining=4;
        renderer.SetFailureHook(Injection::Hook,&repeated);
        for(unsigned i=0;i<3;++i)check(present().outcome==render::RenderOutcome::Recovered,"live recovery permitted");
        check(present().outcome==render::RenderOutcome::Failed && repeated.recreations==3,"fourth live loss rejected");
        renderer.SetFailureHook(nullptr,nullptr);probe.FinalPoll();
        const auto metadata=probe.BuildMetadata(renderer.MeasurementContext(),0,1,0);
        check(metadata.dropped==0,"recovery fixture retains every measured row");
        std::wstring error;check(render::WriteMeasurements(output/L"measurement.csv",probe.Rows(),metadata,error),"recovery measurement export");
    }
    outcomes<<"]\n";outcomes.close();
    report<<"],\"completion\":"<<(failures?"false":"true")<<",\"passed\":"<<(failures?"false":"true")<<"}\n";report.close();
    DestroyWindow(hwnd);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return failures?1:0;
}
