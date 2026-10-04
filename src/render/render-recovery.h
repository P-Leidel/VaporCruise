#pragma once
#include <dxgi.h>
#include "render/render-result.h"
namespace vaporcruise::render {
inline bool IsDeviceLoss(HRESULT hr) { return hr==DXGI_ERROR_DEVICE_REMOVED || hr==DXGI_ERROR_DEVICE_RESET; }
class RecoveryBudget {
public:
    bool TryRecovery() { if(attempts_>=3)return false; ++attempts_; return true; }
    void Presented() { attempts_=0; }
private: unsigned attempts_=0;
};
template<class Recreate> RenderResult RecoverAttempt(RenderResult result, RecoveryBudget& budget, Recreate recreate) {
    if(result.outcome!=RenderOutcome::Failed || !IsDeviceLoss(result.hr))return result;
    if(!budget.TryRecovery())return RenderResult::Failure(L"device recovery retry limit",result.hr);
    const auto created=recreate();
    return created.outcome==RenderOutcome::Failed?created:RenderResult{RenderOutcome::Recovered,L"",S_OK};
}
// A recovered attempt has produced no pixels. Only a complete readback may encode.
template<class Input,class Attempt,class Recover,class Encode>
RenderResult CaptureTransaction(const Input& input,Attempt attempt,Recover recover,Encode encode) {
    auto result=attempt(input);
    if(result.outcome==RenderOutcome::Failed && IsDeviceLoss(result.hr)) {
        result=recover(result);
        if(result.outcome==RenderOutcome::Recovered) result=attempt(input);
    }
    if(result.outcome!=RenderOutcome::Drawn)return result;
    return encode();
}
}
