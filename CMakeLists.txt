cmake_minimum_required(VERSION 3.24)

project(VaporCruise VERSION 1.0.0 LANGUAGES CXX RC)

include(cmake/source-identity.cmake)
vaporcruise_read_source_identity("${CMAKE_SOURCE_DIR}" VAPORCRUISE_SOURCE_REVISION VAPORCRUISE_SOURCE_STATE)

if(NOT WIN32)
    message(FATAL_ERROR "VaporCruise is a Windows-only project (see docs/current/build-and-verification.md).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE VAPORCRUISE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)

# HLSL -> bytecode-embedded-as-a-C-header, used by Release builds of
# render/ShaderSource.cpp. Release ships no shader source or runtime compile
# dependency. Debug builds compile the .hlsl
# below directly at startup instead (see VAPORCRUISE_SHADER_SOURCE_DIR).
find_program(FXC_EXECUTABLE
    NAMES fxc
    PATHS
        "$ENV{WindowsSdkDir}bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
)
if(NOT FXC_EXECUTABLE)
    file(GLOB VAPORCRUISE_FXC_CANDIDATES
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
    )
    if(VAPORCRUISE_FXC_CANDIDATES)
        list(GET VAPORCRUISE_FXC_CANDIDATES -1 FXC_EXECUTABLE)
    endif()
endif()
if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe (Windows SDK HLSL compiler) not found; required to embed shader bytecode for release builds.")
endif()

set(VAPORCRUISE_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
set(VAPORCRUISE_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${VAPORCRUISE_GENERATED_SHADER_DIR}")

include(cmake/shader-programs.cmake)

add_executable(VaporCruise WIN32 ${VAPORCRUISE_SOURCES})
add_dependencies(VaporCruise VaporCruiseShaders)

target_include_directories(VaporCruise PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${VAPORCRUISE_GENERATED_SHADER_DIR}"
)

target_compile_definitions(VaporCruise PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # Changed configure identity forces recompilation after the release commit.
    VAPORCRUISE_VERSION="${PROJECT_VERSION}"
    VAPORCRUISE_SOURCE_REVISION="${VAPORCRUISE_SOURCE_REVISION}"
    VAPORCRUISE_SOURCE_STATE="${VAPORCRUISE_SOURCE_STATE}"
    VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}"
)

target_link_libraries(VaporCruise PRIVATE
    d3d11
    dxgi
    d3dcompiler
    user32
    gdi32
    dwmapi
    wtsapi32
    windowscodecs
    ole32
    shell32
)

if(MSVC)
    target_compile_options(VaporCruise PRIVATE /W4 /permissive-)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        # Make CMake's default explicit so build identity records the actual CRT.
        set_property(TARGET VaporCruise PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

set_target_properties(VaporCruise PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(RegionalColorTests tests/regional-color-tests.cpp src/scene/background-artwork.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(RegionalColorTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME regional-color COMMAND RegionalColorTests)
    add_executable(RegionalShaderTests tests/regional-shader-tests.cpp src/scene/background-artwork.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(RegionalShaderTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(RegionalShaderTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        VAPORCRUISE_REGIONAL_SHADER=L"${CMAKE_SOURCE_DIR}/tests/regional-color.ps.hlsl"
        VAPORCRUISE_REGIONAL_DIRECTORY=L"${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(RegionalShaderTests PRIVATE d3d11 d3dcompiler)
    add_test(NAME regional-shader COMMAND RegionalShaderTests)
    set_tests_properties(regional-shader PROPERTIES TIMEOUT 30)
    add_executable(RenderRecoveryTests tests/render-recovery-tests.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(RenderRecoveryTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(RenderRecoveryTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME render-recovery COMMAND RenderRecoveryTests)
    add_executable(RunPlanTests tests/run-plan-tests.cpp src/app/run-plan.cpp
        src/app/capture-options.cpp src/app/measure-options.cpp src/app/option-values.cpp)
    target_include_directories(RunPlanTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME run-plan COMMAND RunPlanTests)
    add_executable(SceneTerrainTests tests/scene-terrain-tests.cpp src/scene/SceneState.cpp src/scene/terrain-window.cpp src/scene/terrain.cpp)
    target_include_directories(SceneTerrainTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME scene-terrain COMMAND SceneTerrainTests)
    add_executable(ScenePoseTests tests/scene-pose-tests.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(ScenePoseTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME scene-pose COMMAND ScenePoseTests)
    add_executable(ShaderBindingTests tests/shader-binding-tests.cpp src/render/ShaderSource.cpp)
    add_dependencies(ShaderBindingTests VaporCruiseShaders)
    target_include_directories(ShaderBindingTests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(ShaderBindingTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(ShaderBindingTests PRIVATE d3dcompiler)
    add_test(NAME shader-bindings COMMAND ShaderBindingTests)
    add_executable(GridLineProfileTests tests/grid-line-profile-tests.cpp)
    target_include_directories(GridLineProfileTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(GridLineProfileTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        VAPORCRUISE_GRID_PROFILE_SHADER=L"${CMAKE_SOURCE_DIR}/tests/grid-line-profile.ps.hlsl"
        VAPORCRUISE_GRID_SHADER_DIRECTORY=L"${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(GridLineProfileTests PRIVATE d3d11 d3dcompiler)
    add_test(NAME grid-line-profile COMMAND GridLineProfileTests)
    set_tests_properties(grid-line-profile PROPERTIES TIMEOUT 30)
    add_executable(CityFootProfileTests tests/city-foot-profile-tests.cpp)
    target_include_directories(CityFootProfileTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(CityFootProfileTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        VAPORCRUISE_CITY_FOOT_SHADER=L"${CMAKE_SOURCE_DIR}/tests/city-foot-profile.ps.hlsl"
        VAPORCRUISE_CITY_FOOT_DIRECTORY=L"${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(CityFootProfileTests PRIVATE d3d11 d3dcompiler)
    add_test(NAME city-foot-profile COMMAND CityFootProfileTests)
    set_tests_properties(city-foot-profile PROPERTIES TIMEOUT 30)
    add_executable(CameraProjectionTests tests/camera-projection-tests.cpp src/render/camera-projection.cpp)
    target_include_directories(CameraProjectionTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME camera-projection COMMAND CameraProjectionTests)
    add_executable(SceneFrameTests tests/scene-frame-tests.cpp src/scene/background-artwork.cpp)
    target_include_directories(SceneFrameTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME scene-frame COMMAND SceneFrameTests)
    add_executable(WindowEventsTests tests/window-events-tests.cpp src/app/Window.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(WindowEventsTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(WindowEventsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(WindowEventsTests PRIVATE user32 gdi32 dwmapi wtsapi32)
    add_test(NAME window-events COMMAND WindowEventsTests)
    add_executable(WindowMessageTests tests/window-message-tests.cpp src/app/Window.cpp)
    target_include_directories(WindowMessageTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(WindowMessageTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(WindowMessageTests PRIVATE user32 gdi32 dwmapi wtsapi32)
    add_test(NAME window-message COMMAND WindowMessageTests)
    set_tests_properties(window-message PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_executable(BackgroundArtworkTests tests/background-artwork-tests.cpp src/scene/background-artwork.cpp)
    target_include_directories(BackgroundArtworkTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME background-artwork COMMAND BackgroundArtworkTests)
    add_executable(TerrainLifecycleFixture tests/terrain-lifecycle-fixture.cpp
        src/render/background-resources.cpp src/scene/background-artwork.cpp
        src/render/Renderer.cpp src/render/d3d11-frame-probe.cpp src/render/measurement-export.cpp
        src/render/ShaderSource.cpp src/render/shader-program.cpp src/render/terrain-renderer.cpp src/render/camera-projection.cpp
        src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    add_dependencies(TerrainLifecycleFixture VaporCruiseShaders)
    target_include_directories(TerrainLifecycleFixture PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(TerrainLifecycleFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(TerrainLifecycleFixture PRIVATE d3d11 dxgi d3dcompiler user32 windowscodecs ole32)
    set_target_properties(TerrainLifecycleFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    get_target_property(binding_sources TerrainLifecycleFixture SOURCES)
    list(REMOVE_ITEM binding_sources tests/terrain-lifecycle-fixture.cpp)
    add_executable(RenderRecoveryFixture tests/render-recovery-fixture.cpp ${binding_sources})
    add_dependencies(RenderRecoveryFixture VaporCruiseShaders)
    target_include_directories(RenderRecoveryFixture PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(RenderRecoveryFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX VAPORCRUISE_RENDER_DIAGNOSTICS VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(RenderRecoveryFixture PRIVATE d3d11 dxgi d3dcompiler user32 windowscodecs ole32)
    set_target_properties(RenderRecoveryFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_executable(CameraPoseFixture tests/camera-pose-fixture.cpp ${binding_sources})
    add_dependencies(CameraPoseFixture VaporCruiseShaders)
    target_include_directories(CameraPoseFixture PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(CameraPoseFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(CameraPoseFixture PRIVATE d3d11 dxgi d3dcompiler user32 windowscodecs ole32)
    set_target_properties(CameraPoseFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_executable(ShaderBindingFixture tests/shader-binding-fixture.cpp ${binding_sources})
    add_dependencies(ShaderBindingFixture VaporCruiseShaders)
    target_include_directories(ShaderBindingFixture PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(ShaderBindingFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(ShaderBindingFixture PRIVATE d3d11 dxgi d3dcompiler user32 windowscodecs ole32)
    set_target_properties(ShaderBindingFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_executable(TerrainUploadTests tests/terrain-upload-tests.cpp)
    target_include_directories(TerrainUploadTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME terrain-upload COMMAND TerrainUploadTests)
    add_executable(TerrainRendererTests tests/terrain-renderer-tests.cpp src/render/terrain-renderer.cpp
        src/render/ShaderSource.cpp src/render/shader-program.cpp src/render/camera-projection.cpp src/scene/terrain.cpp)
    add_dependencies(TerrainRendererTests VaporCruiseShaders)
    target_include_directories(TerrainRendererTests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(TerrainRendererTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VAPORCRUISE_RENDER_DIAGNOSTICS VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(TerrainRendererTests PRIVATE d3d11 d3dcompiler)
    add_test(NAME terrain-renderer COMMAND TerrainRendererTests)
    add_executable(TerrainWindowTests tests/terrain-window-tests.cpp src/scene/terrain-window.cpp src/scene/terrain.cpp)
    target_include_directories(TerrainWindowTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME terrain-window COMMAND TerrainWindowTests)
    add_executable(TerrainTests tests/terrain-tests.cpp src/scene/terrain.cpp)
    target_include_directories(TerrainTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME terrain COMMAND TerrainTests)
    add_executable(ShaderFailureFixture tests/shader-failure-fixture.cpp src/render/ShaderSource.cpp)
    target_include_directories(ShaderFailureFixture PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(ShaderFailureFixture PRIVATE _DEBUG VAPORCRUISE_SHADER_SOURCE_DIR="${CMAKE_BINARY_DIR}/shader-fixture")
    # This fixture always exercises the Debug runtime compiler, including
    # when a Release build builds test utilities. Match its forced _DEBUG CRT.
    set_property(TARGET ShaderFailureFixture PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebug)
    target_link_libraries(ShaderFailureFixture PRIVATE d3dcompiler user32)
    add_executable(FrameProbeTests tests/frame-probe-tests.cpp src/render/d3d11-frame-probe.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(FrameProbeTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(FrameProbeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FrameProbeTests PRIVATE user32)
    add_test(NAME frame-probe COMMAND FrameProbeTests)
    add_executable(MeasurementExportTests tests/measurement-export-tests.cpp src/render/measurement-export.cpp)
    target_include_directories(MeasurementExportTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME measurement-export COMMAND MeasurementExportTests)
    add_executable(PerformanceBufferTests tests/performance-buffer-tests.cpp)
    target_include_directories(PerformanceBufferTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME performance-buffer COMMAND PerformanceBufferTests)
    add_executable(MeasureOptionsTests tests/measure-options-tests.cpp src/app/measure-options.cpp src/app/option-values.cpp)
    target_include_directories(MeasureOptionsTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME measure-options COMMAND MeasureOptionsTests)
    add_executable(CaptureOptionsTests tests/capture-options-tests.cpp src/app/capture-options.cpp src/app/option-values.cpp)
    target_include_directories(CaptureOptionsTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME capture-options COMMAND CaptureOptionsTests)
    add_executable(SceneStateTests tests/scene-state-tests.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(SceneStateTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(SceneStateTests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME scene-state COMMAND SceneStateTests)
    add_executable(FrameLoopTests tests/frame-loop-tests.cpp src/app/frame-loop.cpp src/scene/SceneState.cpp src/scene/terrain.cpp src/scene/terrain-window.cpp)
    target_include_directories(FrameLoopTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(FrameLoopTests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME frame-loop COMMAND FrameLoopTests)
    add_executable(FrameDeadlineTests tests/frame-deadline-tests.cpp
        src/app/win32-frame-loop.cpp src/app/Window.cpp ${binding_sources})
    add_dependencies(FrameDeadlineTests VaporCruiseShaders)
    target_include_directories(FrameDeadlineTests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${VAPORCRUISE_GENERATED_SHADER_DIR}")
    target_compile_definitions(FrameDeadlineTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        VAPORCRUISE_SHADER_SOURCE_DIR="${VAPORCRUISE_SHADER_DIR}")
    target_link_libraries(FrameDeadlineTests PRIVATE d3d11 dxgi d3dcompiler user32 gdi32 dwmapi wtsapi32 windowscodecs ole32)
    add_test(NAME frame-deadline COMMAND FrameDeadlineTests)
    set_tests_properties(frame-deadline PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
endif()

# Optional verification paths do not make Python a core build dependency.
option(VAPORCRUISE_CPU_VERIFICATION "Register Python/PowerShell verification regressions" OFF)
option(VAPORCRUISE_GPU_TESTS "Register actual GPU visual/lifecycle diagnostics" OFF)
if(BUILD_TESTING)
    find_program(VAPORCRUISE_POWERSHELL NAMES pwsh powershell REQUIRED)
    set_target_properties(ShaderFailureFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME shader-compilation-failure
        COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-shader-failure-fixture.ps1"
                -Fixture "$<TARGET_FILE:ShaderFailureFixture>"
                -OutputRoot "${CMAKE_BINARY_DIR}/evidence/shader-failure")
    set_tests_properties(shader-compilation-failure PROPERTIES TIMEOUT 45)
    add_test(NAME build-wrapper
        COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/build-wrapper-tests.ps1")
    add_test(NAME package-release
        COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/package-release-tests.ps1"
                -Executable "$<TARGET_FILE:VaporCruise>" -CMakeExecutable "${CMAKE_COMMAND}")
    set_tests_properties(build-wrapper package-release PROPERTIES TIMEOUT 60)
    add_executable(ArtworkMetadataFixture tests/artwork-metadata-fixture.cpp src/scene/background-artwork.cpp src/render/camera-projection.cpp)
    target_include_directories(ArtworkMetadataFixture PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_executable(TerrainHeightFixture tests/terrain-height-fixture.cpp src/scene/terrain.cpp)
    target_include_directories(TerrainHeightFixture PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_executable(MeasurementOutputFixture tests/measurement-output-fixture.cpp)
    target_compile_definitions(MeasurementOutputFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(MeasurementOutputFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(ArtworkMetadataFixture TerrainHeightFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(VAPORCRUISE_CPU_VERIFICATION OR VAPORCRUISE_GPU_TESTS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
    endif()
    if(VAPORCRUISE_CPU_VERIFICATION)
        add_test(NAME shader-binding-comparison COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/shader-binding-comparison-tests.py")
        add_test(NAME background-repeat-consensus COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/background-repeat-consensus-tests.py")
        add_test(NAME render-recovery-comparison COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/render-recovery-comparison-tests.py")
        add_test(NAME camera-projection-python COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/camera-projection-python-tests.py")
        add_test(NAME terrain-filter-evidence COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/terrain-filter-evidence-tests.py")
        add_test(NAME performance-analysis COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/performance-analysis-tests.py")
        foreach(SCRIPT python-runtime verification-harness visual-suite visual-verification)
            set(SCRIPT_ARGS)
            if(SCRIPT STREQUAL "verification-harness")
                list(APPEND SCRIPT_ARGS -ArtworkMetadataFixture "$<TARGET_FILE:ArtworkMetadataFixture>")
            endif()
            add_test(NAME "${SCRIPT}" COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/${SCRIPT}-tests.ps1" -PythonExecutable "${Python3_EXECUTABLE}" ${SCRIPT_ARGS})
            set_tests_properties("${SCRIPT}" PROPERTIES LABELS cpu-verification TIMEOUT 60)
        endforeach()
        set_tests_properties(visual-verification PROPERTIES TIMEOUT 180)
        add_test(NAME measurement-output-contract COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/measurement-output-contract-tests.ps1" -Fixture "$<TARGET_FILE:MeasurementOutputFixture>" -PythonExecutable "${Python3_EXECUTABLE}")
        set_tests_properties(measurement-output-contract PROPERTIES LABELS cpu-verification TIMEOUT 30)
        set_tests_properties(shader-binding-comparison background-repeat-consensus render-recovery-comparison camera-projection-python
            terrain-filter-evidence performance-analysis PROPERTIES LABELS cpu-verification TIMEOUT 60)
    endif()
    if(VAPORCRUISE_GPU_TESTS)
        add_test(NAME gpu-terrain-accounting COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/run-terrain-accounting-fixture.ps1" -Fixture "$<TARGET_FILE:RenderRecoveryFixture>" -OutputRoot "${CMAKE_BINARY_DIR}/evidence/gpu/$<CONFIG>" CONFIGURATIONS Debug)
        set_tests_properties(gpu-terrain-accounting PROPERTIES LABELS gpu TIMEOUT 90 RUN_SERIAL TRUE)
        add_test(NAME gpu-render-recovery COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/run-render-recovery-fixture.ps1" -Fixture "$<TARGET_FILE:RenderRecoveryFixture>" -PythonExecutable "${Python3_EXECUTABLE}" -OutputRoot "${CMAKE_BINARY_DIR}/evidence/gpu/$<CONFIG>")
        set_tests_properties(gpu-render-recovery PROPERTIES LABELS gpu TIMEOUT 1500 RUN_SERIAL TRUE)
        add_test(NAME gpu-bindings COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/run-shader-binding-fixture.ps1" -Fixture "$<TARGET_FILE:ShaderBindingFixture>" -PythonExecutable "${Python3_EXECUTABLE}" -OutputRoot "${CMAKE_BINARY_DIR}/evidence/gpu/$<CONFIG>")
        set_tests_properties(gpu-bindings PROPERTIES LABELS gpu TIMEOUT 600 RUN_SERIAL TRUE)
        foreach(MODE lifecycle visual)
            add_test(NAME "gpu-${MODE}" COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-gpu-verification.ps1" -Mode "${MODE}"
                -Executable "$<TARGET_FILE:VaporCruise>" -Fixture "$<TARGET_FILE:TerrainLifecycleFixture>"
                -PythonExecutable "${Python3_EXECUTABLE}" -OutputRoot "${CMAKE_BINARY_DIR}/evidence/gpu/$<CONFIG>")
            set_tests_properties("gpu-${MODE}" PROPERTIES LABELS gpu TIMEOUT 600 RUN_SERIAL TRUE)
        endforeach()
        set_tests_properties(gpu-lifecycle PROPERTIES TIMEOUT 60)
        add_test(NAME gpu-rim COMMAND "${VAPORCRUISE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/run-gpu-verification.ps1" -Mode rim
            -Executable "$<TARGET_FILE:VaporCruise>" -PythonExecutable "${Python3_EXECUTABLE}"
            -OutputRoot "${CMAKE_BINARY_DIR}/evidence/gpu/$<CONFIG>" CONFIGURATIONS Debug)
        set_tests_properties(gpu-rim PROPERTIES LABELS gpu TIMEOUT 90 RUN_SERIAL TRUE)
    endif()
endif()
add_custom_command(TARGET VaporCruise POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DCONFIGURATION=$<CONFIG>" "-DEXECUTABLE=$<TARGET_FILE:VaporCruise>"
        "-DMARKER=$<TARGET_FILE_DIR:VaporCruise>/vaporcruise-debug-diagnostics.json"
        "-DBUILD_MARKER=$<TARGET_FILE_DIR:VaporCruise>/vaporcruise-build.json"
        "-DPROJECT_VERSION=${PROJECT_VERSION}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DSOURCE_REVISION=${VAPORCRUISE_SOURCE_REVISION}"
        "-DSOURCE_STATE=${VAPORCRUISE_SOURCE_STATE}"
        "-DARCHITECTURE=${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
        "-DRUNTIME=$<TARGET_GENEX_EVAL:VaporCruise,$<TARGET_PROPERTY:VaporCruise,MSVC_RUNTIME_LIBRARY>>"
        -P "${CMAKE_SOURCE_DIR}/cmake/write-diagnostic-marker.cmake"
    VERBATIM)
