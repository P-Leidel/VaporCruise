# Dot-source this file before calling CMake or cl in the current PowerShell session.
$VaporCruiseRoot = Split-Path $PSScriptRoot -Parent
$VaporCruiseTools = Join-Path $VaporCruiseRoot 'build/tools'
$VaporCruiseMSVC = Join-Path $VaporCruiseTools 'msvc/VC/Tools/MSVC/14.51.36231'
$VaporCruiseSDK = Join-Path $VaporCruiseTools 'sdk/c'
$VaporCruiseSDKVersion = '10.0.26100.0'
$VaporCruiseCMake = Join-Path $VaporCruiseTools 'python/cmake/data/bin/cmake.exe'
$VaporCruiseNinja = Join-Path $VaporCruiseTools 'python/ninja-1.13.0.data/scripts/ninja.exe'
$VaporCruiseCompiler = Join-Path $VaporCruiseMSVC 'bin/Hostx64/x64/cl.exe'
$VaporCruiseFXC = Join-Path $VaporCruiseSDK "bin/$VaporCruiseSDKVersion/x64/fxc.exe"
foreach ($VaporCruiseRequired in @($VaporCruiseCMake, $VaporCruiseNinja, $VaporCruiseCompiler, $VaporCruiseFXC)) {
    if (-not (Test-Path -LiteralPath $VaporCruiseRequired -PathType Leaf)) {
        throw "Local toolchain missing: $VaporCruiseRequired. See docs/current/build-and-verification.md for the installed-toolchain build route."
    }
}
$env:PATH = "$VaporCruiseMSVC/bin/Hostx64/x64;$VaporCruiseSDK/bin/$VaporCruiseSDKVersion/x64;$(Split-Path $VaporCruiseCMake);$(Split-Path $VaporCruiseNinja);$env:PATH"
$env:INCLUDE = "$VaporCruiseMSVC/include;$VaporCruiseSDK/Include/$VaporCruiseSDKVersion/ucrt;$VaporCruiseSDK/Include/$VaporCruiseSDKVersion/shared;$VaporCruiseSDK/Include/$VaporCruiseSDKVersion/um;$VaporCruiseSDK/Include/$VaporCruiseSDKVersion/winrt;$VaporCruiseSDK/Include/$VaporCruiseSDKVersion/cppwinrt"
$env:LIB = "$VaporCruiseMSVC/lib/x64;$VaporCruiseSDK/ucrt/x64;$VaporCruiseSDK/um/x64"
$env:VCToolsInstallDir = "$VaporCruiseMSVC/"
$env:VCToolsVersion = '14.51.36231'
$env:WindowsSdkDir = "$VaporCruiseSDK/"
$env:WindowsSDKVersion = "$VaporCruiseSDKVersion\"
$env:VSCMD_ARG_HOST_ARCH = 'x64'
$env:VSCMD_ARG_TGT_ARCH = 'x64'
$VaporCruiseTemp = Join-Path $VaporCruiseTools 'temp'
New-Item -ItemType Directory -Force -Path $VaporCruiseTemp | Out-Null
$env:TEMP = $VaporCruiseTemp
$env:TMP = $VaporCruiseTemp
