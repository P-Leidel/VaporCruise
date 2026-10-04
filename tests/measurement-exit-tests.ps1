param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($ownedPath in @($Executable, $OutputDirectory)) {
    if (-not $ownedPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Exit-test paths must stay in the project.' }
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh exit-test directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Add-Type -Path (Join-Path $project 'tools/nonactivating-process.cs')
$csv = Join-Path $OutputDirectory 'deadline-app.csv'
$appId = [VaporCruiseMeasurementLaunch]::Start($Executable, ('"' + $Executable + '" --measure "' + $csv + '" --duration 1 --warmup 0 --width 640 --height 360 --present-interval 1'), $project)
$appProcess = Get-Process -Id $appId
$appHandle = $appProcess.Handle
$timer = [Diagnostics.Stopwatch]::StartNew()
$sent = 0
try {
    while (-not $appProcess.WaitForExit(8)) {
        if ($timer.Elapsed.TotalSeconds -gt 6) { throw 'Finite one-second measurement did not finish within six seconds.' }
        $window = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
        if ($window -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($window)) {
            # WM_NULL does nothing to the app state, but its wParam must never
            # become the success exit status of a finite measurement.
            [VaporCruiseMeasurementLaunch]::PostMessage($window, 0, [UIntPtr]::new(123), [IntPtr]::Zero) | Out-Null
            ++$sent
        }
    }
    [pscustomobject]@{exit_code=$appProcess.ExitCode; wm_null_messages=$sent; sentinel_wparam=123} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
    if ($sent -lt 10 -or -not (Test-Path -LiteralPath $csv)) { throw 'Exit regression did not exercise a complete finite measurement with sentinel messages.' }
    if ($appProcess.ExitCode -ne 0) { throw "FAIL: successful finite measurement returned stale message wParam as exit $($appProcess.ExitCode) (sentinel123)." }
    Write-Output "PASS: finite measurement exits0 despite $sent harmless owned-window WM_NULL/wParam123 messages."
} finally {
    if (-not $appProcess.HasExited) { Stop-Process -Id $appId }
}

# Export must use the starting snapshot after WM_CLOSE destroys the owned HWND.
$closeCsv = Join-Path $OutputDirectory 'close-app.csv'
$closeId = [VaporCruiseMeasurementLaunch]::Start($Executable, ('"' + $Executable + '" --measure "' + $closeCsv + '" --duration 10 --warmup 0 --width 640 --height 360'), $project)
$closeProcess = Get-Process -Id $closeId
$closeHandle = $closeProcess.Handle
$closeTimer = [Diagnostics.Stopwatch]::StartNew()
try {
    $closeWindow = [IntPtr]::Zero
    while ($closeTimer.Elapsed.TotalSeconds -lt 6 -and -not $closeProcess.HasExited) {
        $closeWindow = [VaporCruiseMeasurementLaunch]::FindWindow($closeId)
        if ($closeWindow -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($closeWindow)) { break }
        Start-Sleep -Milliseconds 20
    }
    if ($closeWindow -eq [IntPtr]::Zero) { throw 'Close-export regression found no owned window.' }
    Start-Sleep -Milliseconds 500
    if (-not [VaporCruiseMeasurementLaunch]::PostMessage($closeWindow, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)) { throw 'Owned WM_CLOSE failed.' }
    if (-not $closeProcess.WaitForExit(5000) -or $closeProcess.ExitCode -ne 0) { throw 'WM_CLOSE did not export with a successful bounded exit.' }
    $closeRows = @(Import-Csv -LiteralPath $closeCsv)
    $closeMetadata = Get-Content -LiteralPath ($closeCsv + '.json') -Raw | ConvertFrom-Json
    if ($closeRows.Count -eq 0 -or $closeMetadata.retained_frames -ne $closeRows.Count -or
        $closeMetadata.monitor_metadata_snapshot -ne 'start of visible measurement session' -or
        $closeMetadata.measurement_start_monotonic_seconds -le 0) { throw 'Destroyed-window export lost rows or starting metadata.' }
    Write-Output "PASS: WM_CLOSE exports $($closeRows.Count) rows with the retained starting snapshot."
} finally {
    if (-not $closeProcess.HasExited) { Stop-Process -Id $closeId }
}
