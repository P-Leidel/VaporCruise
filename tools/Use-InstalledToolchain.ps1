# Dot-source to import the installed Visual Studio x64 developer environment.
# CMake and Ninja must already be installed; this script downloads nothing.
$VaporCruiseInstalledVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $VaporCruiseInstalledVswhere -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere.exe is missing. Install Visual Studio or Build Tools with Desktop development with C++.'
}
$VaporCruiseInstalledPath = & $VaporCruiseInstalledVswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $VaporCruiseInstalledPath) {
    throw 'No installed Visual Studio C++ x64 toolchain was found.'
}
$VaporCruiseInstalledDevCmd = Join-Path $VaporCruiseInstalledPath 'Common7/Tools/VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $VaporCruiseInstalledDevCmd -PathType Leaf)) {
    throw "Visual Studio developer environment is missing: $VaporCruiseInstalledDevCmd"
}
$VaporCruiseInstalledEnvironment = & $env:ComSpec /d /s /c `
    "`"$VaporCruiseInstalledDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) {
    throw 'Visual Studio x64 developer environment initialization failed.'
}
foreach ($VaporCruiseInstalledEntry in $VaporCruiseInstalledEnvironment) {
    if ($VaporCruiseInstalledEntry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
foreach ($VaporCruiseInstalledCommand in @('cl.exe', 'rc.exe', 'fxc.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')) {
    if (-not (Get-Command $VaporCruiseInstalledCommand -CommandType Application -ErrorAction SilentlyContinue)) {
        throw "Required build tool is not on PATH: $VaporCruiseInstalledCommand. Install the Windows SDK, CMake and Ninja; see docs/current/build-and-verification.md."
    }
}
Write-Host "Using installed Visual Studio x64 toolchain: $VaporCruiseInstalledPath"
