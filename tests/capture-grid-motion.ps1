param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(240,7680)][int]$Width = 1280,
    [ValidateRange(240,7680)][int]$Height = 720,
    [ValidateRange(24,120)][int]$Frames = 48,
    [ValidateRange(0,2)][int]$Palette = 0,
    [uint64]$Seed = 6215337262362608466,
    [ValidateRange(0,9223372036854774783)][int64]$TravelCell = 0,
    [ValidateRange(.0001,10000)][double]$CellSpan = 1
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$poses = @()
for ($frame = 0; $frame -lt $Frames; ++$frame) {
    $path = Join-Path $outputPath ('motion-p{0}-{1:d2}.png' -f $Palette,$frame)
    $offset = $CellSpan * $frame / ($Frames - 1)
    $carry = [int64][Math]::Floor($offset)
    if ($TravelCell -gt [int64]::MaxValue - 1024 - $carry) { throw 'Capture range exceeds cruise headroom.' }
    $cell = $TravelCell + $carry
    $phase = ($offset - $carry).ToString('R', [Globalization.CultureInfo]::InvariantCulture)
    $poses += @{Name=('motion-p{0}-{1:d2}' -f $Palette,$frame); Seed=$Seed.ToString(); Cell=$cell.ToString(); Phase=$phase; Width=$Width; Height=$Height; Palette=$Palette; CityLightPhase=0}
}
Invoke-VaporCruiseCaptureMatrix -Executable $executablePath -OutputDirectory $outputPath -Frames $poses -ManifestName "motion-p$Palette.json"
Write-Output "Captured $Frames world poses at ${Width}x${Height}, palette $Palette."
