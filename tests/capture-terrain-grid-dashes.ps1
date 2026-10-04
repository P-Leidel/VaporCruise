param([string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$Motion)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$refinementFrames = [Collections.Generic.List[object]]::new()
function Add-RefinementFrame([string]$Name, [int]$Palette, [int]$Width = 1280,
    [int]$Height = 720, [string]$Cell = '0', [double]$Phase = .25,
    [string]$Seed = '6215337262362608466', [double]$Fog = 0) {
    $refinementFrames.Add(@{Name=$Name; Width=$Width; Height=$Height; Palette=$Palette;
        Hint=0; Dpi=96; Cell=$Cell; Phase=$Phase; Seed=$Seed;
        CityLightPhase=$Fog; Transition=0})
}
$refinementNames = @('magenta','cyan','amber')
foreach ($refinementPalette in 0..2) {
    $refinementName = $refinementNames[$refinementPalette]
    Add-RefinementFrame $refinementName $refinementPalette
    # Match the supplied full-screen reference dimensions.
    Add-RefinementFrame ($refinementName+'-large') $refinementPalette 2560 1397
    Add-RefinementFrame ($refinementName+'-small') $refinementPalette 480 320
    Add-RefinementFrame ($refinementName+'-late-fog') $refinementPalette 481 320 '0' .25 '6215337262362608466' .95
}
Add-RefinementFrame 'cyan-route-320' 1 1280 720 '320'
Add-RefinementFrame 'cyan-route-3200' 1 1280 720 '3200'
Add-RefinementFrame 'cyan-route-far' 1 1280 720 '36028797018963971'
Add-RefinementFrame 'cyan-seed-42' 1 1280 720 '320' .25 '42'
Add-RefinementFrame 'cyan-portrait' 1 720 1280
Add-RefinementFrame 'cyan-ultrawide' 1 2560 1080
if ($Motion) {
    # Four seconds of actual route/fog poses at the unchanged 1.5 cells/s.
    foreach ($refinementIndex in 0..47) {
        $refinementSeconds = $refinementIndex / 12.0
        $refinementTravel = .25 + $refinementSeconds*1.5
        $refinementCell = [long][Math]::Floor($refinementTravel)
        Add-RefinementFrame ('motion-cyan-{0:d2}' -f $refinementIndex) 1 1280 720 ([string]$refinementCell) ($refinementTravel-$refinementCell) '6215337262362608466' ($refinementSeconds/42.0)
    }
}
Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
    -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $refinementFrames.ToArray()
