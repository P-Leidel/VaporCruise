// Exercises the production Debug shader loader using disposable sources.
#include "render/ShaderSource.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::wstring_view mode(argv[1]);
    if (mode != L"--expect-success" && mode != L"--expect-failure") return 2;
    std::vector<uint8_t> bytecode;
    const bool compiled=vaporcruise::render::LoadSceneCompositePixelShaderBytecode(bytecode);
    std::ofstream result{std::filesystem::path(argv[2])};
    result << "{\"compilation_succeeded\":" << (compiled ? "true" : "false")
           << ",\"bytecode_size\":" << bytecode.size() << "}\n";
    result.close();
    if (!result || compiled != (mode == L"--expect-success") || (compiled && bytecode.empty())) return 1;
    return 0;
}
