$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'qa-timing.ps1')
# The wrapper began 300ms before the app's warmed capture clock. Reusing
# elapsed_ms would incorrectly include resumed data in the paused interval.
$metadata = [pscustomobject]@{measurement_start_monotonic_seconds=100.0}
$pause = [pscustomobject]@{elapsed_ms=1500.0; monotonic_seconds=101.2}
$resume = [pscustomobject]@{elapsed_ms=8600.0; monotonic_seconds=108.3}
if ([Math]::Abs((Get-CaptureEventMilliseconds $pause $metadata) - 1200) -gt .0001 -or
    [Math]::Abs((Get-CaptureEventMilliseconds $resume $metadata) - 8300) -gt .0001) {
    throw 'FAIL: lifecycle event boundaries did not use the shared absolute monotonic origin.'
}
Write-Output 'PASS: event/capture alignment is independent of process warm-up and wrapper stopwatch origin.'
