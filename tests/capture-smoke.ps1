param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$CheckComposition
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$capturePath = Join-Path (Resolve-Path $OutputDirectory).Path 'smoke.png'
if (Test-Path -LiteralPath $capturePath) { Remove-Item -LiteralPath $capturePath }
$process = Start-Process -FilePath (Resolve-Path $Executable).Path -ArgumentList @('--capture', ('"' + $capturePath + '"'), '--width', '640', '--height', '360', '--phase', '0.25') -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(15000)) {
    Stop-Process -Id $process.Id
    throw 'Capture did not exit after one frame within 15 seconds.'
}
if ($process.ExitCode -ne 0) { throw "Capture failed with exit $($process.ExitCode)." }
if (-not (Test-Path -LiteralPath $capturePath)) { throw 'Capture did not create the requested PNG.' }
Add-Type -AssemblyName System.Drawing
$image = [System.Drawing.Bitmap]::new($capturePath)
try {
    if ($image.Width -ne 640 -or $image.Height -ne 360) { throw 'Capture dimensions differ from the requested backbuffer.' }
    if ($CheckComposition) {
        # A bright sun above the horizon catches the legacy inverted sky/grid.
        $sun = $image.GetPixel(320, 95)
        if ($sun.R -lt 140 -or $sun.G -lt 80) { throw 'Sun must be bright and above the horizon (upper-sky orientation criterion).' }
        $sky = $image.GetPixel(320, 25)
        if ($sky.R -gt 100 -or $sky.G -gt 100) { throw 'Upper sky must remain dark.' }
    }
} finally { $image.Dispose() }
Write-Output 'PASS: actual GPU PNG has requested dimensions and capture exits successfully.'
