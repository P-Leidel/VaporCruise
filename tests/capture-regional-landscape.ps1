param([string]$Executable='build/portable/bin/Release/VaporCruise.exe',
      [string]$OutputDirectory,
      [switch]$Motion,
      [string]$AdditionalFramesPath,
      [switch]$MatrixOnly,
      [switch]$RepeatReferencesOnly)
$ErrorActionPreference='Stop'
if ($RepeatReferencesOnly -and ($Motion -or $AdditionalFramesPath)) { throw 'RepeatReferencesOnly cannot include motion or additional poses.' }
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$regionalFrames=[Collections.Generic.List[object]]::new()
function Add-RegionalFrame([string]$Name,[int]$Palette,[string]$Seed,
    [string]$Cell='0',[double]$Phase=.25,[int]$Width=1280,[int]$Height=720,
    [double]$Transition=0,[double]$Light=0) {
    $regionalFrames.Add(@{Name=$Name;Width=$Width;Height=$Height;Palette=$Palette;
        Hint=0;Dpi=96;Cell=$Cell;Phase=$Phase;Seed=$Seed;
        CityLightPhase=$Light;Transition=$Transition})
}
$regionalNames=@('magenta','cyan','amber')
$regionalSeeds=@('6215337262362608466','18446744073709551615')
foreach ($regionalPalette in 0..2) {
    foreach ($regionalSeedIndex in 0..1) {
        $regionalPrefix=$regionalNames[$regionalPalette]+'-s'+$regionalSeedIndex
        $regionalSeed=$regionalSeeds[$regionalSeedIndex]
        Add-RegionalFrame ($regionalPrefix+'-standard') $regionalPalette $regionalSeed
        Add-RegionalFrame ($regionalPrefix+'-large') $regionalPalette $regionalSeed '0' .25 2560 1397
        Add-RegionalFrame ($regionalPrefix+'-small') $regionalPalette $regionalSeed '0' .25 481 320
        Add-RegionalFrame ($regionalPrefix+'-portrait') $regionalPalette $regionalSeed '0' .25 720 1280
        foreach ($regionalCell in @('256','512','1024')) {
            Add-RegionalFrame ($regionalPrefix+'-route-'+$regionalCell) $regionalPalette $regionalSeed $regionalCell
        }
        # Each crossing and both same-side controls advance exactly .0002 cells.
        # Keep city/solar time frozen to isolate streamed terrain motion.
        foreach ($regionalBoundary in @(32,128)) {
            Add-RegionalFrame ($regionalPrefix+'-r'+$regionalBoundary+'-control-before') $regionalPalette $regionalSeed ([string]($regionalBoundary-1)) .9997
            Add-RegionalFrame ($regionalPrefix+'-r'+$regionalBoundary+'-before') $regionalPalette $regionalSeed ([string]($regionalBoundary-1)) .9999
            Add-RegionalFrame ($regionalPrefix+'-r'+$regionalBoundary+'-after') $regionalPalette $regionalSeed ([string]$regionalBoundary) .0001
            Add-RegionalFrame ($regionalPrefix+'-r'+$regionalBoundary+'-control-after') $regionalPalette $regionalSeed ([string]$regionalBoundary) .0003
        }
        # Decimal strings never pass through double precision.
        Add-RegionalFrame ($regionalPrefix+'-huge-53') $regionalPalette $regionalSeed '9007199254740993'
        Add-RegionalFrame ($regionalPrefix+'-huge-maximum') $regionalPalette $regionalSeed '9223372036854774783'
        foreach ($regionalTransition in @(.4999,.5,.5001)) {
            Add-RegionalFrame ($regionalPrefix+'-transition-'+$regionalTransition.ToString('R',[Globalization.CultureInfo]::InvariantCulture)) $regionalPalette $regionalSeed '128' .25 1280 720 $regionalTransition .25
        }
        Add-RegionalFrame ($regionalPrefix+'-repeat') $regionalPalette $regionalSeed
    }
}
if ($RepeatReferencesOnly) { $regionalFrames.Clear() }
# Fixed five independent captures per standard/repeat path, including the
# original path image. Retain all raw samples; never choose a favorable subset.
foreach ($regionalPalette in 0..2) {
    foreach ($regionalSeedIndex in 0..1) {
        $regionalPrefix=$regionalNames[$regionalPalette]+'-s'+$regionalSeedIndex
        foreach ($regionalStem in @('standard','repeat')) {
            foreach ($regionalReference in 1..4) {
                Add-RegionalFrame ($regionalPrefix+'-'+$regionalStem+'-reference-'+$regionalReference) $regionalPalette $regionalSeeds[$regionalSeedIndex]
            }
        }
    }
}
if ($AdditionalFramesPath) {
    # Additional full poses selected from actual geometry metrics can cover
    # particular terrain regions. Supply this identical file to baseline/candidate.
    $regionalExtra=@(Get-Content -LiteralPath $AdditionalFramesPath -Raw | ConvertFrom-Json)
    foreach ($regionalPose in $regionalExtra) {
        foreach ($regionalKey in @('Name','Width','Height','Palette','Hint','Dpi','Cell','Phase','Seed','CityLightPhase','Transition')) {
            if (-not $regionalPose.PSObject.Properties[$regionalKey]) { throw "Additional pose lacks $regionalKey" }
        }
        if ($regionalPose.Cell -isnot [string] -or $regionalPose.Seed -isnot [string]) { throw 'Additional full route addresses/seeds must be decimal strings.' }
        $regionalFrames.Add($regionalPose)
    }
}
if ($Motion) {
    # 48 native samples across 8 seconds at 6fps; this measures sampled imagery,
    # not real-time presentation cadence. Keep the matrix identical for both builds.
    foreach ($regionalIndex in 0..47) {
        $regionalSeconds=$regionalIndex/6.0
        $regionalTravel=120.25+$regionalSeconds*1.5
        $regionalCell=[long][Math]::Floor($regionalTravel)
        Add-RegionalFrame ('motion-p0-{0:d2}' -f $regionalIndex) 0 $regionalSeeds[0] ([string]$regionalCell) ($regionalTravel-$regionalCell) 1280 720 0 ($regionalSeconds/42.0)
    }
}
$regionalPoses=@($regionalFrames | ForEach-Object { ConvertTo-VaporCruisePose $_ })
$regionalSeen=@{}
foreach ($regionalPose in $regionalPoses) {
    if ($regionalPose.Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $regionalSeen.ContainsKey($regionalPose.Name)) { throw 'Invalid or duplicate regional pose name.' }
    $regionalSeen[$regionalPose.Name]=$true
}
if ($MatrixOnly) {
    ConvertTo-Json -InputObject $regionalPoses -Depth 8
} else {
    if (-not $OutputDirectory) { throw 'OutputDirectory is required for native capture.' }
    Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
        -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $regionalPoses
}
