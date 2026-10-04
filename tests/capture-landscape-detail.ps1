param([string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$Quick, [switch]$Motion)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$detailFrames = [Collections.Generic.List[object]]::new()
function Add-DetailFrame([string]$Name, [int]$Palette, [double]$Fog = 0,
    [int]$Width = 1280, [int]$Height = 720, [string]$Cell = '0',
    [double]$Phase = .25, [string]$Seed = '6215337262362608466', [double]$Transition = 0) {
    $detailFrames.Add(@{Name=$Name; Width=$Width; Height=$Height; Palette=$Palette;
        Hint=0; Dpi=96; Cell=$Cell; Phase=$Phase; Seed=$Seed;
        CityLightPhase=$Fog; Transition=$Transition})
}
$detailNames = @('magenta','cyan','amber')
foreach ($detailPalette in 0..2) {
    Add-DetailFrame $detailNames[$detailPalette] $detailPalette
    Add-DetailFrame ($detailNames[$detailPalette]+'-fog-quarter') $detailPalette .25
    foreach ($detailSeconds in @(1,2,4)) {
        Add-DetailFrame ($detailNames[$detailPalette]+"-fog-${detailSeconds}s") $detailPalette ($detailSeconds/42.0)
    }
    # This late phase maximizes the central shared wisp. An odd width places
    # a pixel exactly on the road center, revealing a land/sky fog mismatch.
    Add-DetailFrame ($detailNames[$detailPalette]+'-late-fog-small') $detailPalette .95 481 320
    if (-not $Quick) {
        foreach ($detailCell in @('320','3200','36028797018963971')) {
            Add-DetailFrame ($detailNames[$detailPalette]+"-route-$detailCell") $detailPalette .15 1280 720 $detailCell
        }
        Add-DetailFrame ($detailNames[$detailPalette]+'-portrait') $detailPalette .25 720 1280
        Add-DetailFrame ($detailNames[$detailPalette]+'-small') $detailPalette .25 480 320
        Add-DetailFrame ($detailNames[$detailPalette]+'-ultrawide') $detailPalette .25 2560 1080
        Add-DetailFrame ($detailNames[$detailPalette]+'-blend') $detailPalette .25 1280 720 '0' .25 '6215337262362608466' .5
        Add-DetailFrame ($detailNames[$detailPalette]+'-seed-42') $detailPalette .25 1280 720 '320' .25 '42'
    }
}
Add-DetailFrame 'repeat' 0
foreach ($detailValue in @(@('9998',.9998),@('9999',.9999),@('0001',.0001),@('0002',.0002))) {
    Add-DetailFrame ('fog-'+$detailValue[0]) 0 $detailValue[1]
}
if (-not $Quick) {
    foreach ($detailValue in @(@('seam-control-before','31',.9997),@('seam-before','31',.9999),
                              @('seam-after','32',.0001),@('seam-control-after','32',.0003))) {
        Add-DetailFrame $detailValue[0] 0 .25 1280 720 $detailValue[1] $detailValue[2]
    }
    foreach ($detailValue in @(@('far-edge-before','1087',.9999),@('far-edge-after','1088',.0001))) {
        Add-DetailFrame $detailValue[0] 0 .25 1280 720 $detailValue[1] $detailValue[2]
    }
}
if ($Motion) {
    foreach ($detailPalette in 0..2) {
        foreach ($detailIndex in 0..47) {
            $detailSeconds = $detailIndex / 6.0
            $detailTravel = .25 + $detailSeconds*1.5
            $detailCell = [long][Math]::Floor($detailTravel)
            Add-DetailFrame ('motion-p{0}-{1:d2}' -f $detailPalette,$detailIndex) $detailPalette ($detailSeconds/42.0) 1280 720 ([string]$detailCell) ($detailTravel-$detailCell)
        }
    }
}
Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
    -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $detailFrames.ToArray()
