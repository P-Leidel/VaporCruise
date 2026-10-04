#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "render/render-outcome.h"
namespace vaporcruise::render {
struct RenderResult {
    RenderOutcome outcome = RenderOutcome::Drawn;
    const wchar_t* stage = L"";
    HRESULT hr = S_OK;
    static RenderResult Complete() { return {}; }
    static RenderResult Failure(const wchar_t* stage, HRESULT hr) { return {RenderOutcome::Failed,stage,hr}; }
};
}
