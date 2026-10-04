param([string]$Executable='build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$Motion)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$solarFrames=[Collections.Generic.List[object]]::new()
function Add-SolarFrame([string]$Name,[int]$Palette,[double]$Light=0,
    [int]$Width=1280,[int]$Height=720,[double]$Transition=0,
    [string]$Cell='0',[double]$Phase=.25) {
    $solarFrames.Add(@{Name=$Name;Width=$Width;Height=$Height;Palette=$Palette;
        Hint=0;Dpi=96;Cell=$Cell;Phase=$Phase;Seed='6215337262362608466';
        CityLightPhase=$Light;Transition=$Transition})
}
$solarNames=@('magenta','cyan','amber')
foreach ($solarPalette in 0..2) {
    $solarName=$solarNames[$solarPalette]
    Add-SolarFrame $solarName $solarPalette
    foreach ($solarPhase in @(.25,.5,.75,.95,.9998,.9999,.0001,.0002)) {
        Add-SolarFrame ($solarName+'-light-'+$solarPhase) $solarPalette $solarPhase
    }
    Add-SolarFrame ($solarName+'-large') $solarPalette 0 2560 1397
    Add-SolarFrame ($solarName+'-small') $solarPalette .95 481 320
    Add-SolarFrame ($solarName+'-portrait') $solarPalette .25 720 1280
    Add-SolarFrame ($solarName+'-transition') $solarPalette .25 1280 720 .5
}
Add-SolarFrame 'repeat' 0
if ($Motion) {
    # Actual cruise/city time advances together at six sampled frames/second.
    # These native stills do not measure real-time presentation.
    foreach ($solarIndex in 0..47) {
        $solarSeconds=$solarIndex/6.0
        $solarTravel=.25+$solarSeconds*1.5
        $solarCell=[long][Math]::Floor($solarTravel)
        Add-SolarFrame ('motion-magenta-{0:d2}' -f $solarIndex) 0 ($solarSeconds/42.0) `
            1280 720 0 ([string]$solarCell) ($solarTravel-$solarCell)
    }
}
Invoke-VaporCruiseCaptureMatrix -Executable ((Resolve-Path -LiteralPath $Executable).Path) `
    -OutputDirectory ([IO.Path]::GetFullPath($OutputDirectory)) -Frames $solarFrames.ToArray()
