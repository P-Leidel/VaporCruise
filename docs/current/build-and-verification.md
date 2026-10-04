# Build and verification

These commands run from the repository root in PowerShell. They are source-checkout tools; the portable application requires neither Python nor a compiler. Run instructions and controls are in the [public README](../../README.md).

## Installed toolchain and core checks

Start with a clean source checkout and install Visual Studio 2022 or 2026, or its Build Tools, with **Desktop development with C++**, the MSVC x64 tools, and a Windows 10/11 SDK. Install CMake 3.25 or newer and Ninja, and make their executables available on PATH. The source requires CMake 3.24; the checked-in preset schema requires 3.25. PowerShell 5.1 or 7 runs the verification scripts. Python is needed only for optional verification.

Use an x64 Visual Studio developer shell, or import the registered installation into the current PowerShell session with the helper below. The helper finds the latest installed C++ toolchain with `vswhere`, invokes `VsDevCmd.bat` for an x64 host and target, and checks `cl.exe`, `rc.exe`, `fxc.exe`, CMake, CTest, and Ninja. It downloads nothing. Microsoft's [command-line build guide](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170) and [vswhere examples](https://github.com/microsoft/vswhere/wiki/Find-VC) describe the environment setup.

```powershell
. ./tools/Use-InstalledToolchain.ps1
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug
cmake --build --preset windows-ninja-release
ctest --preset windows-ninja-release
./build/ninja/bin/Release/VaporCruise.exe
```

The `windows-ninja` preset uses Ninja Multi-Config and the installed tools from the developer environment. It configures `build/ninja` and selects the static MSVC runtime (`/MT` for Release, `/MTd` for Debug). Its CTest presets print failing test output and fail if no tests are registered. All project source dependencies are included in the checkout; this path does not require a project-local compiler bundle or a pre-existing build directory. Four parallel compile jobs bound compiler memory use; adjust `CMAKE_BUILD_PARALLEL_LEVEL` for your host.

Debug reads repository shader sources and tries the optional D3D11 debug layer, falling back when the layer is absent. SDK `fxc.exe` is required at build time; Release embeds the compiled shader bytecode. A registered Visual Studio 2026 installation with CMake 4.2 or newer can also use the `windows`, `windows-debug`, and `windows-release` presets and `ctest --test-dir build -C Debug` / `-C Release --output-on-failure`. Those presets select the static runtime too. For Visual Studio 2022 project files, use:

```powershell
cmake -S . -B build/vs2022 -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
cmake --build build/vs2022 --config Debug --parallel 4
ctest --test-dir build/vs2022 -C Debug --output-on-failure
cmake --build build/vs2022 --config Release --parallel 4
ctest --test-dir build/vs2022 -C Release --output-on-failure
```

## Windows CI

The [Windows workflow](../../.github/workflows/windows-ci.yml) runs on pushes, pull requests, and manual dispatch. Separate Debug and Release jobs use the hosted `windows-2022` image, installed MSVC/SDK/CMake/Ninja, the `windows-ninja` preset, and the static runtime. Python 3.12 with NumPy and Pillow enables `VAPORCRUISE_CPU_VERIFICATION`; `VAPORCRUISE_GPU_TESTS` stays OFF. The existing shader profile and terrain renderer tests explicitly use headless WARP software rendering. The workflow does not run visual GPU captures or publish a release. The official [runner image inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md) lists its installed tools.

Checkout and Python setup use official actions pinned to the commits of [checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1) and [setup-python v7.0.0](https://github.com/actions/setup-python/releases/tag/v7.0.0), verified on 2026-10-04. Runner images and Python packages can update independently. A successful local source build verifies that source snapshot and local environment; a hosted CI result must be recorded separately after the workflow runs on GitHub.

## Portable package

```powershell
$taskReleaseRevision = git rev-parse HEAD
./tools/package-release.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -ExpectedVersion '1.0.0' -ExpectedRevision $taskReleaseRevision
```

Commit all release source and inputs in their own Git checkout, then configure and build Release from that clean commit. Development builds can use dirty or unversioned source, but packaging requires a clean checkout and a build from its current full commit. Reconfigure and rebuild after changing the source revision.

The adjacent `vaporcruise-build.json` sidecar must identify VaporCruise, version `1.0.0`, the selected clean source commit, Release, x64, and the static `MultiThreaded` MSVC runtime, with a SHA-256 matching the executable. The version must match the CMake project; `-ExpectedVersion` and `-ExpectedRevision` provide explicit candidate checks. CMake emits this build-only identity after linking, including when `BUILD_TESTING` is OFF. Rebuild if an older executable lacks the sidecar. The identity is recorded in the manifests and is not shipped in the ZIP.

The script creates a unique directory under `build/release`, stages the executable, README, license, notices, and four previews, then verifies an independent extraction by exact file list, size, and SHA-256. It also rechecks source cleanliness and input hashes during packaging. Its outputs include `VaporCruise-1.0.0-windows-x64.zip`, the local `package-manifest.json`, a publishable `release-manifest.json` with the version/tag/commit and file hashes, and `SHA256SUMS`. The public manifest contains filenames rather than local machine paths and records that the executable is unsigned. Independent launch and acceptance remain separate checks; follow the [candidate checklist](v1-launch-checklist.md).

## Optional image and GPU verification

Image checks require Python 3 with NumPy and Pillow. Pass `-PythonExecutable 'C:/path with spaces/python.exe'`, set `VAPORCRUISE_PYTHON`, or use Python on PATH. Explicit and environment selections are validated without fallback; required imports are checked before captures or benchmarks begin.

The [visual suite manifest](../../tests/visual-suite-manifest.json) declares current and historical suites. Use a fresh output directory for each run:

```powershell
./tools/run-visual-verification.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -OutputDirectory build/evidence/visual-fresh -PythonExecutable 'C:/path/to/python.exe'
```

Select an explicit suite with `-Suite`; comparison suites require their declared `-BaselineDirectory`. Historical thresholds preserve earlier designs and do not certify or block current acceptance. Capture manifests record completion, executable hashes, expected sequences, and native camera metadata. Historical fixtures may explicitly select `--legacy-camera`; current checks require camera metadata. The source fixtures specify capture schemas and comparison limits; historical comparison utilities need caller-supplied baselines.

Generate deterministic GPU previews without showing the app:

```powershell
./tests/capture-neon-landscape.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -OutputDirectory build/evidence/neon-preview -Motion
./tools/capture-visuals.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -Motion
./tests/capture-motion.ps1
```

For an exact native pose, use `--seed 6215337262362608466 --travel-cell 320 --phase .25 --capture snapshot.png`. Seed and travel cell are decimal integers. Add `--city-light-phase .25` to select a fixed point in the shared 42-second city-light, sun, and fog cycle; deterministic captures default to `0.0`. Motion previews assemble sampled GPU snapshots and do not measure live display cadence.

Optional CTest groups default to OFF. After the default build, enable them explicitly:

```powershell
. ./tools/Use-InstalledToolchain.ps1
cmake --preset windows-ninja -DVAPORCRUISE_CPU_VERIFICATION=ON -DVAPORCRUISE_GPU_TESTS=ON -DPython3_EXECUTABLE='C:/path/to/python.exe'
cmake --build --preset windows-ninja-debug
cmake --build --preset windows-ninja-release
ctest --test-dir build/ninja -C Debug -L cpu-verification --output-on-failure
ctest --test-dir build/ninja -C Debug -L gpu --output-on-failure
ctest --test-dir build/ninja -C Release -L gpu --output-on-failure
```

Enabling these options persists in that build directory. Core application tests and optional suites have different scopes; record the selected options and configuration with results.

## Performance and resource evidence

Record the host, durations, frame distributions, display gaps and executable hash for each measurement. Submitted frames, GPU intervals, and displayed cadence are distinct measurements. Old runs do not establish the behavior of a changed executable or other hardware.

Use `-GpuDetail` with [measure-performance.ps1](../../tools/measure-performance.ps1), or native `--gpu-detail`, for uploads/setup, terrain, composite, and resolve intervals. Separate `.gpu.csv` and `.gpu.json` files use schema 2 after interior removal; the main measurement CSV is unchanged. Use native `--frame-limit 60` or wrapper `-FrameLimit 60` to measure the optional cap; the default is display refresh (`0`).

Ordinary playback CPU accounting avoids frame-query instrumentation:

```powershell
./tools/measure-thread-cpu.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -OutputDirectory build/evidence/thread-cpu-fresh
```

The user-deferred two-hour resource soak remains unperformed. Reproduce it later with a fresh output directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/measure-performance.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe -OutputDirectory build/evidence/performance/user-two-hour-soak -Runs 1 -CaptureSeconds 7200 -WarmupSeconds 30 -SkipPresentMon
```

This mode displays the app without requesting focus and records bounded CPU/GPU diagnostics and resource samples. It does not collect ETW displayed-frame timing. Observe visual continuity during the run; capture-buffer overhead and missing/dropped samples are recorded separately.

Real sleep/resume, session lock/unlock, mixed-DPI monitor changes, real driver removal, and clean-machine launch remain separate acceptance checks. Synthetic lifecycle tests do not establish those scenarios.
