#include "render/render-recovery.h"
#include "render/render-input.h"
#include <iostream>
#include <string_view>
using namespace vaporcruise::render;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
    RecoveryBudget budget;
    for(int i=0;i<3;++i) check(budget.TryRecovery(),"three recoveries permitted");
    check(!budget.TryRecovery(),"fourth rejected"); budget.Presented();
    check(budget.TryRecovery(),"Present resets budget");
    for(auto hr:{DXGI_ERROR_DEVICE_REMOVED,DXGI_ERROR_DEVICE_RESET,E_FAIL,E_INVALIDARG}) {
        RecoveryBudget b; int calls=0;
        auto result=RecoverAttempt(RenderResult::Failure(L"upload",hr),b,[&]{++calls;return RenderResult::Complete();});
        check(result.outcome==(IsDeviceLoss(hr)?RenderOutcome::Recovered:RenderOutcome::Failed),"classification");
        check(calls==(IsDeviceLoss(hr)?1:0),"one boundary recreation");
        if(!IsDeviceLoss(hr)) check(result.hr==hr && std::wstring_view(result.stage)==L"upload","origin preserved");
    }
    RecoveryBudget b; int calls=0;
    auto failed=RecoverAttempt(RenderResult::Failure(L"present",DXGI_ERROR_DEVICE_RESET),b,[&]{++calls;return RenderResult::Failure(L"recreation",DXGI_ERROR_DEVICE_REMOVED);});
    check(calls==1 && failed.outcome==RenderOutcome::Failed && std::wstring_view(failed.stage)==L"recreation","recreation loss terminal");
    vaporcruise::scene::SceneState state;
    state.SetCruisePosition({36028797018963971LL,.25});
    RenderInput input{state.PrepareFrame()};
    const auto* slots=input.frame.terrainSlots.data();
    const auto revision=slots[0].revision; input.frame.scene.cruise={36028797018963971LL,.25}; input.hintOpacity=.7f; input.uiScale=1.5f;
    for(int scenario=0;scenario<5;++scenario) {
        int attempts=0,recreates=0,encodes=0;
        auto result=CaptureTransaction(input,[&](const RenderInput& retry) {
            check(retry.frame.scene.cruise.cell==input.frame.scene.cruise.cell && retry.frame.scene.cruise.phase==input.frame.scene.cruise.phase && retry.hintOpacity==input.hintOpacity && retry.uiScale==input.uiScale,"same capture input");
            check(retry.frame.terrainSlots.data()==slots && retry.frame.terrainSlots[0].revision==revision,"capture retry borrows stable scene terrain");
            ++attempts;
            if(attempts==1 || scenario==1) return RenderResult::Failure(L"readback",scenario==3?E_FAIL:DXGI_ERROR_DEVICE_RESET);
            return RenderResult::Complete();
        },[&](RenderResult loss) {++recreates; return scenario==2?RenderResult::Failure(L"recreate",DXGI_ERROR_DEVICE_RESET):RenderResult{RenderOutcome::Recovered,L"",S_OK};},[&] {
            check(attempts==2,"encode after full second readback"); ++encodes;
            return scenario==4?RenderResult::Failure(L"encode",E_FAIL):RenderResult::Complete();
        });
        check(result.outcome==(scenario==0?RenderOutcome::Drawn:RenderOutcome::Failed),"capture transaction outcome");
        check(recreates==(scenario==3?0:1),"capture only recreates once");
        check(attempts==((scenario==2 || scenario==3)?1:2),"capture attempt count");
        check(encodes==((scenario==0 || scenario==4)?1:0),"no partial encode");
    }
    check(state.PrepareFrame().rowsBuilt==0,"capture transaction never rebuilds CPU terrain");
    return failures?1:0;
}
