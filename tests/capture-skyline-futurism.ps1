param([string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$Motion)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$skylineFrames = [Collections.Generic.List[object]]::new()
function Add-SkylineFrame([string]$Name, [int]$Palette, [double]$Fog = 0,
    [int]$Width = 1280, [int]$Height = 720, [string]$Cell = '0', [double]$Phase = .25) {
    $skylineFrames.Add(@{Name=$Name; Width=$Width; Height=$Height; Palette=$Palette;
        Hint=0; Dpi=96; Cell=$Cell; Phase=$Phase; Seed='6215337262362608466';
        CityLightPhase=$Fog; Transition=0})
}
$skylineNames = @('magenta','cyan','amber')
foreach ($skylinePalette in 0..2) {
    $skylineName = $skylineNames[$skylinePalette]
    Add-SkylineFrame $skylineName $skylinePalette
    Add-SkylineFrame ($skylineName+'-large') $skylinePalette 0 2560 1397
    Add-SkylineFrame ($skylineName+'-small') $skylinePalette 0 480 320
    Add-SkylineFrame ($skylineName+'-late-fog-small') $skylinePalette .95 481 320
    Add-SkylineFrame ($skylineName+'-portrait') $skylinePalette .25 720 1280
    foreach ($skylinePhase in @(.25,.5,.75,.95)) {
        Add-SkylineFrame ($skylineName+'-fog-'+$skylinePhase) $skylinePalette $skylinePhase
    }
}
Add-SkylineFrame 'tall' 0 .25 480 2560
Add-SkylineFrame 'minimum-aspect' 0 .25 240 7680
Add-SkylineFrame 'ultrawide' 1 .25 2560 1080
Add-SkylineFrame 'superwide' 0 .25 5120 1440
if ($Motion) {
    foreach ($skylineIndex in 0..47) {
        $skylineSeconds = $skylineIndex / 6.0
        $skylineTravel = .25 + $skylineSeconds*1.5
        $skylineCell = [long][Math]::Floor($skylineTravel)
        Add-SkylineFrame ('motion-magenta-{0:d2}' -f $skylineIndex) 0 ($skylineSeconds/42.0) 1280 720 ([string]$skylineCell) ($skylineTravel-$skylineCell)
    }
}
Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
    -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $skylineFrames.ToArray()
