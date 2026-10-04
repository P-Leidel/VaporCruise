# VaporCruise

A calming native Windows synthwave drive: a dark highway, glowing terrain grid, distant futuristic skyline, drifting mist, baked stars, and a striped sunset. Three coordinated palettes transition smoothly through magenta, cyan, and amber. The landscape fills the window; car interiors have been removed.

Built with C++20, Win32, Direct3D 11, and HLSL. No audio or telemetry is included.

## Run

Extract a portable ZIP to a writable folder and run `VaporCruise.exe`. The Release executable embeds its shaders and uses the static MSVC runtime; source files and build tools are not required to run it.

Requires Windows 10 version 1703 or later, x64, and Direct3D 11 graphics. The reference host runs Windows 10 22H2; Windows 11 and other hardware have not been directly validated. Version 1.0.0 is a portable application; the release manifest identifies its exact source commit and executable/archive checksums.

| Key | Action |
| --- | --- |
| P | Cycle palettes |
| F | Toggle fullscreen/windowed |
| Space | Pause/resume cruise, fog, and city lights |
| H | Show control hints |
| L | Toggle the optional 60 FPS cap |
| Alt+Space | Open the native window menu, including the frame-limit choice |
| Esc | Leave fullscreen |
| Alt+F4 | Close |

Resize using the window edges. Ordinary maximize uses the monitor work area; fullscreen uses the whole monitor and restores the previous window placement. Fullscreen hides the pointer over the scene. Palette and hint transitions finish while motion is paused, then the settled scene returns to idle.

Playback follows display refresh by default. The optional cap limits submissions to at most 60 per second; actual presentation depends on the display and system. The title and window menu show the current choice, which lasts for the current process.

## Preview

The source checkout includes actual GPU captures in [magenta](docs/current/progress/visuals/landscape-effects-2026-10-04/magenta.png), [cyan](docs/current/progress/visuals/landscape-effects-2026-10-04/cyan.png), and [amber](docs/current/progress/visuals/landscape-effects-2026-10-04/amber.png), plus an [eight-second magenta motion preview](docs/current/progress/visuals/landscape-effects-2026-10-04/magenta-motion.gif). The [preview manifest](docs/current/progress/visuals/landscape-effects-2026-10-04/preview-manifest.json) records executable and image hashes.

In the portable ZIP, open `magenta.png`, `cyan.png`, `amber.png`, and `motion-preview.gif` alongside this README. These previews are sampled GPU snapshots and do not measure live frame cadence. The application does not load them.

## Build from source

Install Visual Studio Build Tools with the Desktop development with C++ workload and a Windows SDK, plus CMake 3.25 or newer and Ninja. From the repository root in PowerShell, run:

```powershell
. ./tools/Use-InstalledToolchain.ps1
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug --parallel 4
ctest --preset windows-ninja-debug
cmake --build --preset windows-ninja-release --parallel 4
ctest --preset windows-ninja-release
./build/ninja/bin/Release/VaporCruise.exe
```

Both configurations use the static MSVC runtime. Full setup, optional verification, capture, and performance commands are in [build and verification](docs/current/build-and-verification.md). Windows CI builds and tests both configurations; a hosted run is required to establish its result.

To create and verify a portable ZIP after building Release:

```powershell
./tools/package-release.ps1 -Executable build/ninja/bin/Release/VaporCruise.exe
```

The script requires a matching Release build identity from a clean committed source tree, verifies package integrity, and writes a versioned archive, source manifest, and SHA-256 checksums. Reconfigure and rebuild after committing source changes. Independent launch, performance, and release acceptance are separate checks.

## Status and license

The [documentation index](docs/README.md) covers building, verification, and release scope. The [release verification checklist](docs/current/v1-launch-checklist.md) records verification commands and remaining hardware coverage; [release notes](docs/current/v1-release-notes.md) describe the v1.0.0 scope.

Real sleep/resume, session locking, mixed-DPI multi-monitor behavior, real driver removal, and the two-hour soak remain unverified. Earlier performance results apply to their recorded executable hashes and hardware.

Code and project-authored procedural visuals are available under the [MIT license](LICENSE). See [third-party notices](THIRD-PARTY-NOTICES.md) for runtime and build-tool terms. Documentation links into `docs/` are intended for the source checkout; portable run instructions and preview filenames above are self-contained.
