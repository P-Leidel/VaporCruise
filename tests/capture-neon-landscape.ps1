param([string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$Quick, [switch]$Motion)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$taskFrames = [Collections.Generic.List[object]]::new()
function Add-LandscapeFrame([string]$Name, [int]$Palette, [double]$Fog = 0,
    [int]$Width = 1280, [int]$Height = 720, [string]$Cell = '0',
    [double]$Phase = .25, [string]$Seed = '6215337262362608466', [double]$Transition = 0) {
    $taskFrames.Add(@{Name=$Name; Width=$Width; Height=$Height; Palette=$Palette;
        Hint=0; Dpi=96; Cell=$Cell; Phase=$Phase; Seed=$Seed;
        CityLightPhase=$Fog; Transition=$Transition})
}
$taskNames = @('magenta','cyan','amber')
foreach ($taskPalette in 0..2) {
    Add-LandscapeFrame $taskNames[$taskPalette] $taskPalette
    Add-LandscapeFrame ($taskNames[$taskPalette]+'-fog-quarter') $taskPalette .25
    if (-not $Quick) {
        foreach ($taskPhase in @(.5,.75)) {
            Add-LandscapeFrame ($taskNames[$taskPalette]+"-fog-$taskPhase") $taskPalette $taskPhase
        }
        foreach ($taskCell in @('320','3200','36028797018963971')) {
            Add-LandscapeFrame ($taskNames[$taskPalette]+"-route-$taskCell") $taskPalette .15 1280 720 $taskCell
        }
        Add-LandscapeFrame ($taskNames[$taskPalette]+'-portrait') $taskPalette .25 720 1280
        Add-LandscapeFrame ($taskNames[$taskPalette]+'-small') $taskPalette .25 480 320
        Add-LandscapeFrame ($taskNames[$taskPalette]+'-ultrawide') $taskPalette .25 2560 1080
        Add-LandscapeFrame ($taskNames[$taskPalette]+'-blend') $taskPalette .25 1280 720 '0' .25 '6215337262362608466' .5
        Add-LandscapeFrame ($taskNames[$taskPalette]+'-seed-42') $taskPalette .25 1280 720 '320' .25 '42'
    }
}
Add-LandscapeFrame 'repeat' 0
foreach ($taskValue in @(@('9998',.9998),@('9999',.9999),@('0001',.0001),@('0002',.0002))) {
    Add-LandscapeFrame ('fog-'+$taskValue[0]) 0 $taskValue[1]
}
if (-not $Quick) {
    foreach ($taskValue in @(@('seam-control-before','31',.9997),@('seam-before','31',.9999),
                            @('seam-after','32',.0001),@('seam-control-after','32',.0003))) {
        Add-LandscapeFrame $taskValue[0] 0 .25 1280 720 $taskValue[1] $taskValue[2]
    }
    foreach ($taskValue in @(@('far-edge-before','1087',.9999),@('far-edge-after','1088',.0001))) {
        Add-LandscapeFrame $taskValue[0] 0 .25 1280 720 $taskValue[1] $taskValue[2]
    }
}
if ($Motion) {
    foreach ($taskPalette in 0..2) {
        foreach ($taskIndex in 0..47) {
            # Eight seconds at six samples/s. Fog and route move together.
            $taskSeconds = $taskIndex / 6.0
            $taskTravel = .25 + $taskSeconds*1.5
            $taskCell = [long][Math]::Floor($taskTravel)
            Add-LandscapeFrame ('motion-p{0}-{1:d2}' -f $taskPalette,$taskIndex) $taskPalette ($taskSeconds/42.0) 1280 720 ([string]$taskCell) ($taskTravel-$taskCell)
        }
    }
}
Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
    -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $taskFrames.ToArray()
