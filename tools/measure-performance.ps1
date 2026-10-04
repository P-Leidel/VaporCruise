<#!
Launches only the requested app without activating it. CPU/GPU capture is opt-in;
PresentMon rows are separate display evidence. Run -Elevate once for ETW capture.
For the user-run two-hour resource soak: -Runs 1 -CaptureSeconds 7200 -SkipPresentMon.
#>
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,10)][int]$Runs = 3,
    [ValidateRange(1,28800)][int]$CaptureSeconds = 300,
    [ValidateRange(30,600)][int]$WarmupSeconds = 30,
    [ValidateRange(320,7680)][int]$Width = 1920,
    [ValidateRange(240,4320)][int]$Height = 1080,
    [ValidateRange(0,2)][int]$Palette = 0,
    [switch]$CyclePalettes,
    [switch]$PaletteTransition,
    [ValidateSet(0,1)][int]$PresentInterval = 1,
    [ValidateSet(0,60)][int]$FrameLimit = 0,
    [switch]$Fullscreen,
    [switch]$Focused,
    [switch]$GpuDetail,
    [switch]$SkipPresentMon,
    [switch]$Elevate,
    [string]$PresentMon
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'verification-harness.ps1')
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if (-not $PresentMon) { $PresentMon = Join-Path $project 'build/tools/presentmon/PresentMon-2.6.0-x64.exe' }
if ($Focused -and ($Runs -ne 1 -or $CaptureSeconds -ne 30 -or $WarmupSeconds -ne 30)) {
    throw 'The authorized focused comparison is one run with 30 seconds warm-up and 30 seconds capture.'
}
function ProjectPath([string]$path) {
    $resolved = [IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture paths must stay inside this project: $resolved"
    }
    return $resolved
}
function QuoteArgument([string]$value) { return ConvertTo-VaporCruiseArgument $value }
$Executable = ProjectPath $Executable
$OutputDirectory = ProjectPath $OutputDirectory
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Missing executable: $Executable" }
if (-not $SkipPresentMon) {
    $PresentMon = ProjectPath $PresentMon
    if (-not (Test-Path -LiteralPath $PresentMon -PathType Leaf)) { throw "Missing PresentMon: $PresentMon" }
}
if (Test-Path -LiteralPath $OutputDirectory) {
    if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container) -or
        @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0) {
        throw 'Capture output directory must be empty; preserving all existing evidence.'
    }
}
if ($Elevate) {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (QuoteArgument $PSCommandPath),
        '-Executable', (QuoteArgument $Executable), '-OutputDirectory', (QuoteArgument $OutputDirectory),
        '-Runs', $Runs, '-CaptureSeconds', $CaptureSeconds, '-WarmupSeconds', $WarmupSeconds,
        '-Width', $Width, '-Height', $Height, '-Palette', $Palette, '-PresentInterval', $PresentInterval, '-FrameLimit', $FrameLimit,
        '-PresentMon', (QuoteArgument $PresentMon))
    if ($Fullscreen) { $arguments += '-Fullscreen' }
    if ($Focused) { $arguments += '-Focused' }
    if ($GpuDetail) { $arguments += '-GpuDetail' }
    if ($CyclePalettes) { $arguments += '-CyclePalettes' }
    if ($PaletteTransition) { $arguments += '-PaletteTransition' }
    if ($SkipPresentMon) { $arguments += '-SkipPresentMon' }
    Write-Output "Requesting the authorized administrator prompt for project-local ETW capture. Results: $OutputDirectory"
    $helper = Start-Process -FilePath powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
    $helperHandle = $helper.Handle
    $helper.WaitForExit()
    if ($helper.ExitCode -ne 0) { throw "Elevated capture failed ($($helper.ExitCode)); inspect project-local run logs." }
    return
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
# CreateProcess's startup show value reaches wWinMain as SW_SHOWNOACTIVATE.
Add-Type -Path (Join-Path $PSScriptRoot 'nonactivating-process.cs')
$manifest = [ordered]@{
    schema_version=1; completion=$false; timestamp_utc = [DateTime]::UtcNow.ToString('o'); executable = $Executable;
    executable_sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash;
    warmup_seconds = $WarmupSeconds; capture_seconds = $CaptureSeconds; runs = $Runs;
    requested_width = $Width; requested_height = $Height; palette = $Palette;
    cycle_palettes = [bool]$CyclePalettes;
    palette_transition = [bool]$PaletteTransition;
    requested_present_interval = $PresentInterval; fullscreen = [bool]$Fullscreen;
    requested_frame_limit_fps = $FrameLimit;
    requested_focus = [bool]$Focused;
    gpu_detail_requested = [bool]$GpuDetail;
    displayed_evidence_requested = -not [bool]$SkipPresentMon;
    presentmon_version = if ($SkipPresentMon) { $null } else { '2.6.0' };
    presentmon_sha256 = if ($SkipPresentMon) { $null } else { (Get-FileHash -LiteralPath $PresentMon -Algorithm SHA256).Hash };
    note = 'Requested output is not measured monitor metadata. App capture metadata supplies actual adapter/output/refresh. Resource samples are buffered and flushed after each run.'
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
try {
    for ($run = 1; $run -le $Runs; ++$run) {
        if ((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash -ne $manifest.executable_sha256) { throw 'Executable changed during measurement.' }
        $prefix = Join-Path $OutputDirectory ('run-{0:d2}' -f $run)
        if (Test-Path -LiteralPath ($prefix + '-app.csv')) { throw "Refusing to replace existing capture: $prefix" }
        if ($GpuDetail) {
            foreach ($suffix in @('.gpu.csv','.gpu.json')) {
                if (Test-Path -LiteralPath ($prefix + '-app.csv' + $suffix)) { throw "Refusing to replace existing GPU detail: $prefix$suffix" }
            }
        }
        $runPalette = if ($CyclePalettes) { ($Palette + $run - 1) % 3 } else { $Palette }
        $command = (QuoteArgument $Executable) + ' --measure ' + (QuoteArgument ($prefix + '-app.csv')) +
            " --duration $CaptureSeconds --warmup $WarmupSeconds --width $Width --height $Height --palette $runPalette --present-interval $PresentInterval"
        if ($Fullscreen) { $command += ' --fullscreen' }
        # Default remains usable with historical baseline executables.
        if ($FrameLimit -eq 60) { $command += ' --frame-limit 60' }
        if ($GpuDetail) { $command += ' --gpu-detail' }
        $appId = [VaporCruiseMeasurementLaunch]::Start($Executable, $command, $project)
        $appProcess = Get-Process -Id $appId
        # WinPS 5.1 Get-Process does not otherwise retain the native exit status.
        $appHandle = $appProcess.Handle
        $presentProcess = $null
        $session = $null
        $resourceSamples = [Collections.Generic.List[object]]::new()
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $transitionSent = $false
        $ownedWindow = [IntPtr]::Zero
        try {
            if ($Focused) {
                # The visible HWND appears only after the app's actual hidden
                # warm draw. Focus only this owned window, once; never inject input.
                while ($timer.Elapsed.TotalSeconds -lt 5) {
                    $ownedWindow = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
                    if ($ownedWindow -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($ownedWindow)) { break }
                    Start-Sleep -Milliseconds 50
                }
                if ($ownedWindow -eq [IntPtr]::Zero -or -not [VaporCruiseMeasurementLaunch]::IsWindowVisible($ownedWindow) -or
                    -not [VaporCruiseMeasurementLaunch]::SetForegroundWindow($ownedWindow) -or
                    [VaporCruiseMeasurementLaunch]::GetForegroundWindow() -ne $ownedWindow) {
                    throw 'Authorized owned-window focus failed; no focused result can be claimed.'
                }
            }
            if (-not $SkipPresentMon) {
                $session = 'VaporCruise-' + [Guid]::NewGuid().ToString('N')
                $pmArgs = @('--process_id', $appId, '--session_name', $session,
                    '--output_file', (QuoteArgument ($prefix + '-presentmon.csv')),
                    '--v2_metrics', '--delay', $WarmupSeconds, '--timed', $CaptureSeconds,
                    '--terminate_after_timed', '--no_console_stats', '--no_track_input')
                $presentProcess = Start-Process -FilePath $PresentMon -ArgumentList $pmArgs -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput ($prefix + '-presentmon.stdout.log') -RedirectStandardError ($prefix + '-presentmon.stderr.log')
                $presentHandle = $presentProcess.Handle
            }
            while (-not $appProcess.WaitForExit(1000)) {
                if ($timer.Elapsed.TotalSeconds -gt $CaptureSeconds + $WarmupSeconds + 30) {
                    throw 'App exceeded its bounded measurement duration.'
                }
                if ($Focused -and [VaporCruiseMeasurementLaunch]::GetForegroundWindow() -ne $ownedWindow) {
                    throw 'Owned benchmark lost foreground focus; focused comparison is incomplete.'
                }
                if ($PaletteTransition -and -not $transitionSent -and $timer.Elapsed.TotalSeconds -ge $WarmupSeconds + $CaptureSeconds / 2) {
                    $transitionWindow = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
                    if ($transitionWindow -eq [IntPtr]::Zero -or -not [VaporCruiseMeasurementLaunch]::PostMessage($transitionWindow, 0x100, [UIntPtr]::new(0x50), [IntPtr]::Zero)) {
                        throw 'Owned-window palette transition message failed.'
                    }
                    $transitionSent = $true
                }
                if ($resourceSamples.Count -eq 0 -or $timer.Elapsed.TotalSeconds -ge 10 * $resourceSamples.Count) {
                    $appProcess.Refresh()
                    if ($ownedWindow -eq [IntPtr]::Zero) { $ownedWindow = [VaporCruiseMeasurementLaunch]::FindWindow($appId) }
                    $resourceSamples.Add([pscustomobject]@{
                        elapsed_seconds=$timer.Elapsed.TotalSeconds; private_bytes=$appProcess.PrivateMemorySize64;
                        working_set_bytes=$appProcess.WorkingSet64; handles=$appProcess.HandleCount;
                        cpu_total_seconds=$appProcess.TotalProcessorTime.TotalSeconds
                        app_foreground=($ownedWindow -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::GetForegroundWindow() -eq $ownedWindow)
                    })
                }
            }
            if ($appProcess.ExitCode -ne 0) { throw "App measurement failed: exit $($appProcess.ExitCode)." }
            Assert-VaporCruiseOutputs @(($prefix + '-app.csv'), ($prefix + '-app.csv.json'))
            try {
                $runMetadata = Get-Content -LiteralPath ($prefix + '-app.csv.json') -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
            } catch { throw "Invalid measurement completion metadata: $_" }
            # A normal early window close also exits 0 and preserves partial
            # files. Only the recorded duration/count prove a usable full run.
            # These fields predate the optional cap and GPU detail metadata.
            $numericTypes = @([byte],[sbyte],[int16],[uint16],[int32],[uint32],[int64],[uint64],[single],[double],[decimal])
            foreach ($value in @($runMetadata.capture_seconds, $runMetadata.retained_frames)) {
                if ($null -eq $value -or $numericTypes -notcontains $value.GetType() -or
                    [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value)) {
                    throw 'Invalid measurement completion metadata: capture_seconds and retained_frames must be finite numbers.'
                }
            }
            $recordedSeconds = [double]$runMetadata.capture_seconds
            $retainedFrames = [double]$runMetadata.retained_frames
            if ($retainedFrames -le 0 -or [Math]::Floor($retainedFrames) -ne $retainedFrames) {
                throw 'Incomplete measurement: retained frames must be a positive integer.'
            }
            # SerializeMeasurements uses setprecision(10): permit at most half
            # the last significant decimal place, never a frame/time allowance.
            $serializationTolerance = 0.5 * [Math]::Pow(10.0, [Math]::Floor([Math]::Log10([double]$CaptureSeconds)) - 9.0)
            if ([double]$CaptureSeconds - $recordedSeconds -gt $serializationTolerance) {
                throw "Incomplete measurement: recorded capture duration $recordedSeconds s is shorter than requested $CaptureSeconds s."
            }
            if ($GpuDetail) { Assert-VaporCruiseOutputs @(($prefix + '-app.csv.gpu.csv'), ($prefix + '-app.csv.gpu.json')) }
            if ($presentProcess) {
                if (-not $presentProcess.WaitForExit(15000)) { throw 'PresentMon did not finish within its expected bound.' }
                if ($presentProcess.ExitCode -ne 0) { throw "ETW capture failed: exit $($presentProcess.ExitCode). Inspect stderr log." }
                $pmCsv = $prefix + '-presentmon.csv'
                if (-not (Test-Path -LiteralPath $pmCsv) -or (Get-Item -LiteralPath $pmCsv).Length -lt 100) {
                    throw 'PresentMon produced no usable CSV; displayed cadence remains unavailable.'
                }
            }
            Write-Output "Captured run $run at $prefix"
        } finally {
            $resourceSamples | Export-Csv -LiteralPath ($prefix + '-resources.csv') -NoTypeInformation -Encoding UTF8
            if (-not $appProcess.HasExited) { Stop-Process -Id $appId }
            if ($presentProcess -and -not $presentProcess.HasExited) {
                # Stop only this wrapper's session so forced cleanup leaves no ETW session.
                $stopper = Start-Process -FilePath $PresentMon -ArgumentList @('--session_name', $session, '--terminate_existing_session', '--no_console_stats') -WindowStyle Hidden -PassThru
                $stopperHandle = $stopper.Handle
                if (-not $stopper.WaitForExit(5000)) { Stop-Process -Id $stopper.Id }
                if (-not $presentProcess.WaitForExit(5000)) { Stop-Process -Id $presentProcess.Id }
            }
        }
        if ((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash -ne $manifest.executable_sha256) { throw 'Executable changed during measurement.' }
    }
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'capture-error.log') -Encoding UTF8
    throw
}

$manifest.completion=$true
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding utf8
