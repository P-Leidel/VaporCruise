param([string]$Executable)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (-not $Executable) { $Executable = Join-Path $project 'build/portable/bin/Release/VaporCruise.exe' }
$fixture = Join-Path $project 'build/evidence/tests/wrapper-preservation'
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
$manifestPath = Join-Path $fixture 'manifest.json'
$csvPath = Join-Path $fixture 'run-01-app.csv'
Set-Content -LiteralPath $manifestPath -Value '{"evidence":"preserve original"}' -Encoding UTF8
Set-Content -LiteralPath $csvPath -Value 'existing capture' -Encoding UTF8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
$csvHash = (Get-FileHash -LiteralPath $csvPath -Algorithm SHA256).Hash
$ErrorActionPreference = 'Continue'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $project 'tools/measure-performance.ps1') -Executable $Executable -OutputDirectory $fixture -Runs 1 -CaptureSeconds 1 -SkipPresentMon *> (Join-Path $project 'build/evidence/tests/wrapper-preservation-command.log')
$exitStatus = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($exitStatus -eq 0) { throw 'FAIL: capture did not reject existing evidence.' }
if ((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ne $manifestHash -or
    (Get-FileHash -LiteralPath $csvPath -Algorithm SHA256).Hash -ne $csvHash) {
    throw 'FAIL: rejected capture overwrote original manifest or CSV.'
}
Write-Output 'PASS: rejected existing evidence retains original manifest and CSV hashes, without launching the app.'
