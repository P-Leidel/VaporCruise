# Dependency and asset notices

The portable release contains the VaporCruise executable, README, MIT license, this inventory, and four previews assembled from actual project GPU output (three palette PNGs and a magenta motion GIF). The executable includes compiled project-authored HLSL and the selected static Microsoft C/C++ runtime. Microsoft runtime distribution is governed by the applicable Visual Studio/MSVC license; the project's MIT license does not replace it. See Microsoft's [deployment guidance](https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp?view=msvc-170), [license directory](https://visualstudio.microsoft.com/license-terms/), and [Visual Studio 2026 distributable-code list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution). The distributor must have the applicable toolchain license. No SDK or compiler is redistributed.

The current scene shaders and control glyphs are authored within this project. No imported art, font file, texture pack, or external scene asset is required by the current executable. Recheck this inventory if later visual work introduces an external asset.

| Component | Use | Distributed in portable ZIP |
| --- | --- | --- |
| Win32, Direct3D 11, DXGI, DWM, WTS Windows APIs | Window, graphics, lifecycle | No Windows DLLs copied; supplied by Windows |
| Microsoft C/C++ runtime | Native support code selected by the portable preset | Statically linked into the executable |
| Windows SDK / Direct3D Shader Compiler (`fxc`) | Headers, libraries, shader compilation | No tools/SDK payloads copied |
| MSVC 14.51 toolset | C++20 compiler and linker | No toolchain payloads copied |
| CMake 4.2.3 and Ninja 1.13.0 | Build generation and execution | No |
| Portable MSVC acquisition helper | Local build-tool acquisition only | No |
| PresentMon | Development measurement only, if used | No |

Build tools are installed separately and are not included in source or application archives. See [build and verification](docs/current/build-and-verification.md) for prerequisites. Their licenses apply to the tools; they are not project MIT assets.

## City and mountain artwork

The distant skyline, sculpted landmark, three mountain contours, and window masks
are original procedural artwork authored in `src/scene/background-artwork.cpp`.
They are generated deterministically at initialization and ship under this
repository's MIT license. No external image, font, or runtime download is used.


## Source archive scope

This source snapshot includes the application, tests, build/verification tools, focused documentation and four previews from project GPU output. Removed interior artwork, generation prompts, historical reports and tool downloads are excluded. The executable does not load the previews.
