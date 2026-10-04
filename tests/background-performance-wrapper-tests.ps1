$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot -Parent
$taskDirectory = Join-Path $taskProject 'build/evidence/tests/background-wrapper-preservation'
New-Item -ItemType Directory -Force -Path $taskDirectory | Out-Null
$taskSentinel = Join-Path $taskDirectory 'original.json'
Set-Content -LiteralPath $taskSentinel -Value '{"preserve":true}'
$taskHash = (Get-FileHash $taskSentinel).Hash
$taskErrorLog = Join-Path $taskProject 'build/background-wrapper-red.log'
$taskWrapper = Join-Path $taskProject 'tools/compare-background-performance.ps1'
if (-not (Test-Path $taskWrapper)) { throw 'FAIL: comparison wrapper missing' }
foreach ($taskInvalid in @('existing','outside','missing')) {
    $taskOutput = if ($taskInvalid -eq 'outside') { [IO.Path]::GetTempPath() } else { $taskDirectory }
    $taskBaseline = if ($taskInvalid -eq 'missing') { 'build/missing.exe' } else { 'build/evidence/city-background/baseline/VaporCruise.exe' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $taskWrapper -Baseline $taskBaseline -Candidate build/portable/bin/Release/VaporCruise.exe -OutputDirectory $taskOutput -SkipPresentMon *> $taskErrorLog
    if ($LASTEXITCODE -eq 0) { throw 'FAIL: invalid benchmark inputs accepted' }
    if ((Get-FileHash $taskSentinel).Hash -ne $taskHash) { throw 'FAIL: benchmark overwrote evidence' }
}
Write-Output 'PASS: failed comparisons preserve existing evidence and reject missing/outside paths with nonzero exit.'
