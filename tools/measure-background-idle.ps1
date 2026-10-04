param([Parameter(Mandatory=$true)][string]$Baseline, [Parameter(Mandatory=$true)][string]$Candidate,
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$taskProject = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $taskOutput.StartsWith($taskProject + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must stay in project.' }
if (Test-Path -LiteralPath $taskOutput) { throw 'Use a fresh idle evidence directory.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'nonactivating-process.cs')
$taskResults = [Collections.Generic.List[object]]::new()
foreach ($taskCase in @(@('baseline',$Baseline),@('candidate',$Candidate))) {
    $taskExe = (Resolve-Path -LiteralPath $taskCase[1]).Path
    if (-not $taskExe.StartsWith($taskProject + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Executable must stay in project.' }
    $taskCsv = Join-Path $taskOutput ($taskCase[0]+'-app.csv')
    $taskId = [VaporCruiseMeasurementLaunch]::Start($taskExe, ('"'+$taskExe+'" --measure "'+$taskCsv+'" --duration 180 --warmup 0 --width 1280 --height 720'), $taskProject)
    $taskProcess = Get-Process -Id $taskId; $taskHandle = $taskProcess.Handle
    $taskWindow = [IntPtr]::Zero; $taskDeadline = [Diagnostics.Stopwatch]::StartNew()
    try {
        while ($taskDeadline.Elapsed.TotalSeconds -lt 10) {
            $taskWindow = [VaporCruiseMeasurementLaunch]::FindWindow($taskId)
            if ($taskWindow -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($taskWindow)) { break }
            if ($taskProcess.HasExited) { throw "App exited before visible startup: $($taskProcess.ExitCode)" }
            Start-Sleep -Milliseconds 50
        }
        if ($taskWindow -eq [IntPtr]::Zero) { throw 'Owned app window missing.' }
        Start-Sleep -Seconds 1
        foreach ($taskKey in @(0x20,0x50,0x48)) {
            if (-not [VaporCruiseMeasurementLaunch]::PostMessage($taskWindow,0x100,[UIntPtr]::new($taskKey),[IntPtr]::Zero)) { throw 'Owned pause/palette/hint failed.' }
            Start-Sleep -Milliseconds 150
        }
        Start-Sleep -Seconds 6
        $taskProcess.Refresh(); $taskCpu = $taskProcess.TotalProcessorTime.TotalSeconds
        $taskStart = [Diagnostics.Stopwatch]::GetTimestamp() / [double][Diagnostics.Stopwatch]::Frequency
        Start-Sleep -Seconds 60
        $taskProcess.Refresh()
        $taskUsed = $taskProcess.TotalProcessorTime.TotalSeconds-$taskCpu
        @{Case=$taskCase[0]; CpuSeconds=$taskUsed; HasExited=$taskProcess.HasExited; ExitCode=$taskProcess.ExitCode} |
            ConvertTo-Json | Set-Content (Join-Path $taskOutput ($taskCase[0]+'-probe.json')) -Encoding UTF8
        if ($taskProcess.HasExited -or $taskUsed -gt .2) { throw "Paused app did not remain idle: exited=$($taskProcess.HasExited), cpu=$taskUsed" }
        $taskEnd = [Diagnostics.Stopwatch]::GetTimestamp() / [double][Diagnostics.Stopwatch]::Frequency
        [VaporCruiseMeasurementLaunch]::PostMessage($taskWindow,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        if (-not $taskProcess.WaitForExit(5000) -or $taskProcess.ExitCode -ne 0) { throw 'Idle app shutdown failed.' }
        $taskMetadata = Get-Content ($taskCsv+'.json') -Raw | ConvertFrom-Json
        $taskBeginMs = ($taskStart-$taskMetadata.measurement_start_monotonic_seconds)*1000
        $taskEndMs = ($taskEnd-$taskMetadata.measurement_start_monotonic_seconds)*1000
        $taskIdleRows = @(Import-Csv $taskCsv | Where-Object { [double]$_.timestamp_ms -ge $taskBeginMs -and [double]$_.timestamp_ms -le $taskEndMs })
        if ($taskIdleRows.Count -ne 0) { throw 'Settled paused app submitted automatic frames.' }
        $taskResults.Add(@{Case=$taskCase[0]; Sha256=(Get-FileHash $taskExe).Hash; Seconds=$taskEnd-$taskStart;
            CpuSeconds=$taskUsed; AutomaticFrames=$taskIdleRows.Count})
    } finally {
        if (-not $taskProcess.HasExited) {
            [VaporCruiseMeasurementLaunch]::PostMessage($taskWindow,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) | Out-Null
            if (-not $taskProcess.WaitForExit(3000)) { Stop-Process -Id $taskId }
        }
    }
}
$taskResults | ConvertTo-Json | Set-Content (Join-Path $taskOutput 'idle-comparison.json') -Encoding UTF8
Write-Output 'PASS: baseline and candidate remain idle for 60 seconds after paused P/H transitions.'
