param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$DebugRecovery
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'qa-timing.ps1')
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($localPath in @($Executable, $OutputDirectory)) {
    if (-not $localPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'QA paths must stay in the project.' }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$csv = Join-Path $OutputDirectory 'lifecycle-app.csv'
if (Test-Path -LiteralPath $csv) { throw 'Use a fresh output directory for each QA run.' }
Add-Type -Path (Join-Path $project 'tools/nonactivating-process.cs')
$appId = [VaporCruiseMeasurementLaunch]::Start($Executable, ('"' + $Executable + '" --measure "' + $csv + '" --duration 60 --warmup 0 --width 640 --height 360 --present-interval 1'), $project)
$appProcess = Get-Process -Id $appId
$appHandle = $appProcess.Handle
$timer = [Diagnostics.Stopwatch]::StartNew()
$events = [Collections.Generic.List[object]]::new()
$window = [IntPtr]::Zero
function Event([string]$name) {
    $appProcess.Refresh()
    $events.Add([pscustomobject]@{event=$name; elapsed_ms=$timer.Elapsed.TotalMilliseconds; cpu_seconds=$appProcess.TotalProcessorTime.TotalSeconds;
        monotonic_seconds=([Diagnostics.Stopwatch]::GetTimestamp() / [double][Diagnostics.Stopwatch]::Frequency);
        private_bytes=$appProcess.PrivateMemorySize64; handles=$appProcess.HandleCount;
        app_foreground=([VaporCruiseMeasurementLaunch]::GetForegroundWindow() -eq $window)})
}
function Key([uint32]$key, [bool]$repeat=$false) {
    $bits = if ($repeat) { [IntPtr]::new(1073741824) } else { [IntPtr]::Zero }
    if (-not [VaporCruiseMeasurementLaunch]::PostMessage($window, 0x100, [UIntPtr]::new($key), $bits)) { throw 'Owned-window key message failed.' }
    Start-Sleep -Milliseconds 150
    if ($appProcess.HasExited) { throw 'App exited during lifecycle actions.' }
}
try {
    while ($window -eq [IntPtr]::Zero -and $timer.Elapsed.TotalSeconds -lt 5) {
        $window = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
        Start-Sleep -Milliseconds 50
    }
    if ($window -eq [IntPtr]::Zero) { throw 'App window not created within five seconds.' }
    Start-Sleep -Seconds 1
    Event 'running'
    Key 0x20; Event 'pause'
    Key 0x50; Event 'palette-while-paused'
    Key 0x50 $true; Event 'repeat-key-ignored'
    [VaporCruiseMeasurementLaunch]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, 720, 480, 0x16) | Out-Null
    Start-Sleep -Milliseconds 500; Event 'resize-while-paused'
    Key 0x46; Event 'fullscreen-while-paused'
    $fullscreenRect = [VaporCruiseMeasurementLaunch+RECT]::new()
    [VaporCruiseMeasurementLaunch]::GetWindowRect($window, [ref]$fullscreenRect) | Out-Null
    Key 0x1B; Event 'escape-windowed'
    $windowedRect = [VaporCruiseMeasurementLaunch+RECT]::new()
    [VaporCruiseMeasurementLaunch]::GetWindowRect($window, [ref]$windowedRect) | Out-Null
    if ($windowedRect.right - $windowedRect.left -ne 720 -or $windowedRect.bottom - $windowedRect.top -ne 480) { throw 'Esc did not restore requested window dimensions.' }
    if ($fullscreenRect.right - $fullscreenRect.left -le 720) { throw 'Fullscreen did not expand to monitor bounds.' }
    Start-Sleep -Seconds 4; Event 'paused-settled'
    $idleCpuStart = $appProcess.TotalProcessorTime.TotalSeconds
    Start-Sleep -Seconds 2; $appProcess.Refresh(); Event 'paused-idle-end'
    $idleCpu = $appProcess.TotalProcessorTime.TotalSeconds - $idleCpuStart
    if ($idleCpu -gt .2) { throw "Paused state used excessive active CPU over two seconds: $idleCpu" }
    Key 0x20; Event 'resume'
    Start-Sleep -Seconds 1
    [VaporCruiseMeasurementLaunch]::ShowWindow($window, 6) | Out-Null
    Start-Sleep -Milliseconds 500; Event 'minimized'
    $minimizedCpuStart = $appProcess.TotalProcessorTime.TotalSeconds
    Start-Sleep -Seconds 2; $appProcess.Refresh(); Event 'minimized-idle-end'
    $minimizedCpu = $appProcess.TotalProcessorTime.TotalSeconds - $minimizedCpuStart
    if ($minimizedCpu -gt .2) { throw "Minimized app used excessive CPU over two seconds: $minimizedCpu" }
    [VaporCruiseMeasurementLaunch]::ShowWindow($window, 4) | Out-Null
    Start-Sleep -Seconds 1; Event 'restored'
    Key 0x48; Event 'hint-redisplayed'
    if ($DebugRecovery) {
        for ($attempt = 1; $attempt -le 3; ++$attempt) { Key 0x58; Start-Sleep -Milliseconds 750; Event "simulated-recovery-$attempt" }
    }
    Key 0x50; Key 0x50; Key 0x50; Event 'repeated-palettes'
    Start-Sleep -Seconds 1
    Event 'close-request'
    [VaporCruiseMeasurementLaunch]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (-not $appProcess.WaitForExit(5000)) { throw 'Normal WM_CLOSE did not finish within five seconds.' }
    if ($appProcess.ExitCode -ne 0) { throw "Normal shutdown failed: $($appProcess.ExitCode)" }
    if (-not (Test-Path -LiteralPath $csv)) { throw 'Lifecycle measurement CSV missing.' }
    $rows = @(Import-Csv -LiteralPath $csv)
    foreach ($taskColumn in @('travel_cell','terrain_section','terrain_rows_built','city_light_phase')) {
        if ($rows.Count -eq 0 -or $rows[0].PSObject.Properties.Name -notcontains $taskColumn) { throw "Missing terrain diagnostic column: $taskColumn" }
    }
    foreach ($taskRow in $rows) {
        $taskCell = [int64]$taskRow.travel_cell
        if ([int64]$taskRow.terrain_section -ne [int64][Math]::Floor([decimal]$taskCell / 32)) { throw 'Terrain section differs from exact travel cell.' }
        if ([uint32]$taskRow.terrain_rows_built -gt 363) { throw 'Terrain preparation exceeded fixed storage.' }
    }
    $metadata = Get-Content -LiteralPath ($csv + '.json') -Raw | ConvertFrom-Json
    $pauseTime = Get-CaptureEventMilliseconds ($events | Where-Object event -eq 'pause') $metadata
    $resumeTime = Get-CaptureEventMilliseconds ($events | Where-Object event -eq 'resume') $metadata
    $pausedRows = @($rows | Where-Object { [double]$_.timestamp_ms -gt $pauseTime + 250 -and [double]$_.timestamp_ms -lt $resumeTime - 250 })
    if ($pausedRows.Count -lt 2) { throw 'No paused redraw data for state validation.' }
    $phases = @($pausedRows | ForEach-Object { [double]$_.grid_phase } | Measure-Object -Minimum -Maximum)
    if ($phases[0].Maximum - $phases[0].Minimum -gt .00001) { throw 'Cruise phase moved during paused palette/resize/fullscreen feedback.' }
    $taskCityPhases = @($pausedRows | ForEach-Object { [double]$_.city_light_phase } | Select-Object -Unique)
    if ($taskCityPhases.Count -ne 1) { throw 'City light phase moved during paused feedback.' }
    $taskPausedCells = @($pausedRows | ForEach-Object { [int64]$_.travel_cell } | Select-Object -Unique)
    $taskPausedSections = @($pausedRows | ForEach-Object { [int64]$_.terrain_section } | Select-Object -Unique)
    if ($taskPausedCells.Count -ne 1 -or $taskPausedSections.Count -ne 1) { throw 'World position moved during paused feedback.' }
    if ($metadata.terrain_seed -isnot [string] -or $metadata.terrain_seed -notmatch '^\d+$') { throw 'Exact terrain seed metadata missing.' }
    if ($DebugRecovery -and $metadata.resource_generation -lt 4) { throw 'Three Debug simulated recoveries did not rebuild resources.' }
    [pscustomobject]@{status='pass'; paused_cpu_seconds_over_2s=$idleCpu; minimized_cpu_seconds_over_2s=$minimizedCpu;
        paused_city_light_phase=$taskCityPhases[0]; paused_redraw_rows=$pausedRows.Count; paused_travel_cell=$taskPausedCells[0].ToString(); resource_generation=$metadata.resource_generation;
        windowed_width=720; windowed_height=480; fullscreen_width=($fullscreenRect.right-$fullscreenRect.left);
        note='Owned HWND messages validate state/recovery. No lock/sleep/virtual desktop/global input or real driver removal was performed.'} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
    Write-Output "PASS: pause/palette/resize/fullscreen/Esc/resume/minimize/restore/hint/repeated palettes/clean shutdown. Debug recovery=$DebugRecovery."
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'qa-error.log') -Encoding UTF8
    throw
} finally {
    $events | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'events.csv') -NoTypeInformation -Encoding UTF8
    if (-not $appProcess.HasExited) {
        [VaporCruiseMeasurementLaunch]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (-not $appProcess.WaitForExit(3000)) { Stop-Process -Id $appId }
    }
}
