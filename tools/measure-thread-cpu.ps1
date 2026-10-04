<# Owned ordinary playback only. No query instrumentation, ETW session, thread suspension, foreground activation or global settings. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,600)][int]$WarmupSeconds=30,
    [ValidateRange(1,600)][int]$CaptureSeconds=30,
    [ValidateRange(100,10000)][int]$SampleMilliseconds=1000,
    [ValidateRange(240,7680)][int]$Width=1920,
    [ValidateRange(240,4320)][int]$Height=1080,
    [ValidateSet(0,60)][int]$FrameLimit=0
)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $taskProject 'tools/verification-harness.ps1')
$taskExe=(Resolve-Path -LiteralPath $Executable).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $taskOutput.StartsWith($taskProject+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be inside this project.'}
if(Test-Path -LiteralPath $taskOutput){if(-not(Test-Path -LiteralPath $taskOutput -PathType Container) -or @(Get-ChildItem -LiteralPath $taskOutput -Force).Count){throw 'Use a fresh empty output directory; existing evidence is preserved.'}}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
if(-not ('VaporCruiseMeasurementLaunch' -as [type])){Add-Type -Path (Join-Path $taskProject 'tools/nonactivating-process.cs')}
if(-not ('VaporCruiseThreadAccounting' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'thread-accounting.cs')}
$taskHash=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash
$taskManifest=[ordered]@{schema_version=1;completion=$false;timestamp_utc=[DateTime]::UtcNow.ToString('o');executable=$taskExe;executable_sha256=$taskHash;mode='ordinary no arguments';requested_frame_limit_fps=$FrameLimit;warmup_seconds=$WarmupSeconds;capture_seconds=$CaptureSeconds;sample_milliseconds=$SampleMilliseconds;requested_width=$Width;requested_height=$Height;note='Start-module identifies thread entry, not currently executing code. Thread snapshots can miss threads born and exited between samples; process CPU residual is retained.'}
$taskId=0; $taskProcess=$null; $taskWindow=[IntPtr]::Zero; $taskAccounting=$null
$taskSamples=[Collections.Generic.List[object]]::new(); $taskThreads=[Collections.Generic.List[object]]::new()
try {
    $taskId=[VaporCruiseMeasurementLaunch]::Start($taskExe,(ConvertTo-VaporCruiseArgument $taskExe),$taskProject)
    $taskProcess=Get-Process -Id $taskId; $taskProcessHandle=$taskProcess.Handle
    $taskManifest.process_id=$taskId; $taskManifest.process_start_utc=$taskProcess.StartTime.ToUniversalTime().ToString('o')
    $taskStartup=[Diagnostics.Stopwatch]::StartNew()
    while($taskStartup.Elapsed.TotalSeconds -lt 10){
        if($taskProcess.HasExited){throw 'Owned process exited before a visible window.'}
        $taskWindow=[VaporCruiseMeasurementLaunch]::FindWindow($taskId)
        if($taskWindow -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($taskWindow)){break}
        Start-Sleep -Milliseconds 50
    }
    if($taskWindow -eq [IntPtr]::Zero -or -not [VaporCruiseMeasurementLaunch]::IsWindowVisible($taskWindow)){throw 'Visible owned window was unavailable.'}
    [uint32]$taskOwner=0; $taskMain=[VaporCruiseThreadAccounting]::GetWindowThreadProcessId($taskWindow,[ref]$taskOwner)
    if($taskOwner -ne $taskId){throw 'HWND owner changed.'}
    $taskManifest.window_thread_id=$taskMain
    $taskRect=[VaporCruiseThreadAccounting+Rect]::new()
    if(-not [VaporCruiseThreadAccounting]::GetClientRect($taskWindow,[ref]$taskRect)){throw 'Owned client query failed.'}
    $taskOuter=[VaporCruiseMeasurementLaunch+RECT]::new()
    if(-not [VaporCruiseMeasurementLaunch]::GetWindowRect($taskWindow,[ref]$taskOuter)){throw 'Owned outer rectangle query failed.'}
    $taskOuterWidth=$Width+($taskOuter.right-$taskOuter.left)-($taskRect.Right-$taskRect.Left)
    $taskOuterHeight=$Height+($taskOuter.bottom-$taskOuter.top)-($taskRect.Bottom-$taskRect.Top)
    if(-not [VaporCruiseMeasurementLaunch]::SetWindowPos($taskWindow,[IntPtr]::Zero,0,0,$taskOuterWidth,$taskOuterHeight,0x16)){throw 'Owned nonactivating resize failed.'}
    if($FrameLimit -eq 60){
        if(-not [VaporCruiseMeasurementLaunch]::PostMessage($taskWindow,0x100,[UIntPtr]::new(0x4c),[IntPtr]::Zero)){throw 'Owned frame limit request failed.'}
        $taskControl=[Diagnostics.Stopwatch]::StartNew()
        do {
            $taskMenu=[VaporCruiseThreadAccounting]::GetSystemMenu($taskWindow,$false)
            $taskCheck=[VaporCruiseThreadAccounting]::GetMenuState($taskMenu,0x1000,0)
            if($taskCheck -ne [uint32]::MaxValue -and ($taskCheck -band 8)){break}
            if($taskProcess.WaitForExit(20)){throw 'Owned process exited while enabling cap.'}
        } while($taskControl.Elapsed.TotalSeconds -lt 2)
        if($taskCheck -eq [uint32]::MaxValue -or -not ($taskCheck -band 8)){throw 'Owned native menu did not confirm cap enabled.'}
        $taskManifest.frame_limit_control_confirmed=$true
    }
    $taskWarmup=[Diagnostics.Stopwatch]::StartNew()
    while($taskWarmup.Elapsed.TotalSeconds -lt $WarmupSeconds){if($taskProcess.WaitForExit(250)){throw 'Owned process exited during warmup.'}}
    if(-not [VaporCruiseThreadAccounting]::GetClientRect($taskWindow,[ref]$taskRect)){throw 'Actual client query failed.'}
    $taskManifest.actual_width=$taskRect.Right-$taskRect.Left; $taskManifest.actual_height=$taskRect.Bottom-$taskRect.Top
    if($taskManifest.actual_width -ne $Width -or $taskManifest.actual_height -ne $Height){throw 'Owned client dimensions did not settle to requested size.'}
    $taskAccounting=[VaporCruiseThreadAccounting]::new($taskId)
    $taskBaseline=$taskAccounting.Read($true); $taskPrevious=$taskBaseline
    $taskClock=[Diagnostics.Stopwatch]::StartNew(); $taskNextMilliseconds=[Math]::Min($CaptureSeconds*1000,$SampleMilliseconds)
    while($true){
        $taskDelay=[Math]::Max(1,[Math]::Min(250,$taskNextMilliseconds-[int]$taskClock.ElapsedMilliseconds))
        if($taskProcess.WaitForExit($taskDelay)){throw 'Owned process exited during capture.'}
        if($taskClock.ElapsedMilliseconds -lt $taskNextMilliseconds){continue}
        $taskCurrent=$taskAccounting.Read($false); $taskDelta=$taskCurrent.MonotonicSeconds-$taskPrevious.MonotonicSeconds
        $taskThreadUser=0.0; $taskThreadKernel=0.0
        foreach($taskThread in $taskCurrent.Threads){
            $taskThreadUser+=$taskThread.UserDeltaSeconds; $taskThreadKernel+=$taskThread.KernelDeltaSeconds
            $taskThreads.Add([pscustomobject]@{elapsed_seconds=$taskCurrent.MonotonicSeconds-$taskBaseline.MonotonicSeconds;interval_seconds=$taskDelta;identity=$taskThread.Identity;thread_id=$taskThread.ThreadId;creation_filetime=[string]$taskThread.CreationFileTime;window_thread=($taskThread.ThreadId -eq $taskMain);start_address=$taskThread.StartAddress;start_address_status=$taskThread.StartAddressStatus;start_module=$taskThread.StartModule;start_module_path=$taskThread.StartModulePath;enumerated=$taskThread.Enumerated;time_query_error=$taskThread.TimeQueryError;user_delta_seconds=$taskThread.UserDeltaSeconds;kernel_delta_seconds=$taskThread.KernelDeltaSeconds;cpu_cores=($taskThread.UserDeltaSeconds+$taskThread.KernelDeltaSeconds)/$taskDelta})
        }
        $taskUser=$taskCurrent.ProcessUserSeconds-$taskPrevious.ProcessUserSeconds; $taskKernel=$taskCurrent.ProcessKernelSeconds-$taskPrevious.ProcessKernelSeconds
        $taskSamples.Add([pscustomobject]@{elapsed_seconds=$taskCurrent.MonotonicSeconds-$taskBaseline.MonotonicSeconds;interval_seconds=$taskDelta;process_user_delta_seconds=$taskUser;process_kernel_delta_seconds=$taskKernel;process_cpu_cores=($taskUser+$taskKernel)/$taskDelta;observed_thread_cpu_cores=($taskThreadUser+$taskThreadKernel)/$taskDelta;uncovered_cpu_seconds=($taskUser+$taskKernel)-($taskThreadUser+$taskThreadKernel);private_bytes=$taskCurrent.PrivateBytes;working_set_bytes=$taskCurrent.WorkingSetBytes;handles=$taskCurrent.Handles;thread_count=$taskCurrent.EnumeratedThreadCount;retained_accounting_record_count=$taskCurrent.Threads.Length;thread_enumeration_error=$taskCurrent.ThreadEnumerationError;open_errors=($taskCurrent.OpenErrors -join '|');app_foreground=([VaporCruiseMeasurementLaunch]::GetForegroundWindow() -eq $taskWindow);app_visible=[VaporCruiseMeasurementLaunch]::IsWindowVisible($taskWindow)})
        $taskPrevious=$taskCurrent
        if($taskClock.Elapsed.TotalSeconds -ge $CaptureSeconds){break}
        $taskNextMilliseconds=[Math]::Min($CaptureSeconds*1000,$taskNextMilliseconds+$SampleMilliseconds)
    }
    $taskDuration=$taskPrevious.MonotonicSeconds-$taskBaseline.MonotonicSeconds
    $taskManifest.actual_capture_seconds=$taskDuration
    $taskManifest.mean_process_cpu_cores=(($taskPrevious.ProcessUserSeconds+$taskPrevious.ProcessKernelSeconds)-($taskBaseline.ProcessUserSeconds+$taskBaseline.ProcessKernelSeconds))/$taskDuration
    $taskManifest.foreground_samples=@($taskSamples | Where-Object app_foreground).Count
    $taskManifest.invisible_samples=@($taskSamples | Where-Object {-not $_.app_visible}).Count
    $taskAccounting.Modules | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'modules.json') -Encoding UTF8
    if((Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash -ne $taskHash){throw 'Executable changed during capture.'}
    if($taskManifest.foreground_samples -or $taskManifest.invisible_samples){throw 'Capture did not remain visible and nonforeground.'}
    $taskManifest.completion=$true
} catch {
    $taskManifest.failure=$_.Exception.Message
    throw
} finally {
    # Resource cleanup must run before filesystem writes, including failed CSV exports.
    try {
        try { if($taskAccounting){$taskAccounting.Dispose()} }
        finally {
            if($taskProcess){
                try {
                    if(-not $taskProcess.HasExited){
                        if($taskWindow -ne [IntPtr]::Zero){[VaporCruiseMeasurementLaunch]::PostMessage($taskWindow,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) | Out-Null}
                        if(-not $taskProcess.WaitForExit(5000)){$taskManifest.forced_cleanup=$true; $taskProcess.Kill(); $taskProcess.WaitForExit(5000) | Out-Null; $taskManifest.completion=$false}
                    }
                    $taskManifest.exit_code=$taskProcess.ExitCode; if($taskProcess.ExitCode -ne 0){$taskManifest.completion=$false}
                } finally { $taskProcess.Dispose() }
            }
        }
    } catch {
        $taskManifest.completion=$false; $taskManifest.cleanup_failure=$_.Exception.Message
        throw
    } finally {
        try {
            $taskSamples | Export-Csv -LiteralPath (Join-Path $taskOutput 'process-samples.csv') -NoTypeInformation -Encoding UTF8
            $taskThreads | Export-Csv -LiteralPath (Join-Path $taskOutput 'thread-samples.csv') -NoTypeInformation -Encoding UTF8
        } catch {
            $taskManifest.completion=$false; $taskManifest.serialization_failure=$_.Exception.Message
            if(-not $taskManifest.Contains('failure')){$taskManifest.failure=$_.Exception.Message}
            throw
        } finally {
            $taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
        }
    }
}
if(-not $taskManifest.completion){throw 'Ordinary thread CPU capture is incomplete; inspect the retained manifest.'}
Write-Output ('Captured owned ordinary CPU at '+$taskOutput)
