#include <Windows.h>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string_view>
// Test-only stand-in for the external measurement process; creates real outputs.
int wmain(int argc,wchar_t** argv) {
    std::filesystem::path csv;
    unsigned frameLimit=0;
    unsigned duration=1;
    for (int i=1;i+1<argc;++i) if (std::wstring_view(argv[i])==L"--measure") csv=argv[++i];
    for (int i=1;i+1<argc;++i) if (std::wstring_view(argv[i])==L"--frame-limit")
        frameLimit=std::wstring_view(argv[i+1])==L"60"?60:0;
    for (int i=1;i+1<argc;++i) if (std::wstring_view(argv[i])==L"--duration")
        duration=static_cast<unsigned>(std::wcstoul(argv[i+1],nullptr,10));
    if (csv.empty()) return 2;
    wchar_t modeBuffer[128]{};
    if (GetEnvironmentVariableW(L"VAPORCRUISE_FIXTURE_COMPLETION",modeBuffer,128)>=128) return 3;
    const std::wstring_view mode=modeBuffer;
    std::string seconds=std::to_string(duration), frames="1";
    bool includeDuration=true,includeFrames=true;
    if (mode==L"premature") seconds="0.25";
    else if (mode==L"precision-rounding") seconds="0.9999999999";
    else if (mode==L"just-short") seconds="0.999999999";
    else if (mode==L"zero-frames") frames="0";
    else if (mode==L"negative-frames") frames="-1";
    else if (mode==L"fractional-frames") frames="0.5";
    else if (mode==L"nonfinite-duration") seconds="NaN";
    else if (mode==L"infinite-duration") seconds="Infinity";
    else if (mode==L"string-duration") seconds="\"1\"";
    else if (mode==L"null-duration") seconds="null";
    else if (mode==L"bool-duration") seconds="true";
    else if (mode==L"string-frames") frames="\"1\"";
    else if (mode==L"bool-frames") frames="true";
    else if (mode==L"missing-duration") includeDuration=false;
    else if (mode==L"missing-frames") includeFrames=false;
    else if (!mode.empty() && mode!=L"legacy") return 3;
    std::ofstream(csv) << (mode==L"zero-frames" ? "fixture\n" : "fixture\n1\n");
    if (GetEnvironmentVariableW(L"VAPORCRUISE_FIXTURE_OMIT_JSON",nullptr,0)==0)
    {
        std::ofstream metadata(csv.wstring()+L".json");
        metadata << "{\"fixture\":true";
        if (mode!=L"legacy") metadata << ",\"frame_limit_fps\":" << frameLimit;
        if (includeDuration) metadata << ",\"capture_seconds\":" << seconds;
        if (includeFrames) metadata << ",\"retained_frames\":" << frames;
        metadata << "}\n";
    }
    Sleep(300);
    return 0;
}
