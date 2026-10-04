param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($localPath in @($Executable, $OutputDirectory)) {
    if (-not $localPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cursor-test paths must stay in the project.' }
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh cursor-test directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Add-Type -Path (Join-Path $project 'tools/nonactivating-process.cs')
Add-Type -Path (Join-Path $PSScriptRoot 'cursor-native.cs')
$oldDpi = [VaporCruiseCursorProbe]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$origin = [VaporCruiseCursorProbe]::Observe()
$foreground = [VaporCruiseMeasurementLaunch]::GetForegroundWindow()
$events = [Collections.Generic.List[object]]::new()
$messages = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()
$window = [IntPtr]::Zero
$appProcess = $null
$status = 'error'
function Check-Cursor([string]$name, [bool]$hidden, [int]$resource=32512) {
    $expected = [VaporCruiseCursorProbe]::StandardCursor($resource)
    $samples = [Collections.Generic.List[object]]::new()
    $observation = [Diagnostics.Stopwatch]::StartNew()
    $matchingSince = -1.0
    $pass = $false
    do {
        $info = [VaporCruiseCursorProbe]::Observe()
        if ($info.position.x -ne $origin.position.x -or $info.position.y -ne $origin.position.y) { throw 'Pointer moved externally; observation is invalid.' }
        if ([VaporCruiseCursorProbe]::WindowFromPoint($info.position) -ne $window) { throw 'Owned app is not beneath the stationary pointer; observation is invalid.' }
        $matches = if ($hidden) { $info.cursor -eq [IntPtr]::Zero -and ($info.flags -band 1) -eq 0 } else { $info.cursor -eq $expected -and ($info.flags -band 1) -ne 0 }
        $elapsed = $observation.Elapsed.TotalMilliseconds
        $samples.Add([pscustomobject]@{elapsed_ms=$elapsed; cursor_handle=$info.cursor.ToInt64(); flags=$info.flags; matches=$matches})
        if ($matches) {
            if ($matchingSince -lt 0) { $matchingSince = $elapsed }
            if ($elapsed - $matchingSince -ge 25) { $pass = $true; break }
        } else { $matchingSince = -1.0 }
        if ($elapsed -ge 150) { break }
        # Poll global cursor publication; never inject a second cursor message.
        Start-Sleep -Milliseconds 5
    } while ($true)
    $hit = [VaporCruiseCursorProbe]::Send($window, 0x84, [UIntPtr]::Zero, [VaporCruiseCursorProbe]::PointParameter($origin.position.x, $origin.position.y))
    $events.Add([pscustomobject]@{check=$name; expected_hidden=$hidden; expected_cursor_resource=$resource; cursor_handle=$info.cursor.ToInt64(); flags=$info.flags; hit_at_pointer=$hit; pass=$pass; samples=@($samples)})
    if (-not $pass) { $failures.Add($name) }
}
function Send-Observed([uint32]$message, [UIntPtr]$wp, [IntPtr]$lp) {
    $result = [VaporCruiseCursorProbe]::Send($window, $message, $wp, $lp)
    $info = [VaporCruiseCursorProbe]::Observe()
    $messages.Add([pscustomobject]@{message=$message; wparam=$wp.ToUInt64(); lparam=$lp.ToInt64(); result=$result; immediate_cursor_handle=$info.cursor.ToInt64(); immediate_flags=$info.flags})
}
function Send-Key([uint32]$key) {
    Send-Observed 0x100 ([UIntPtr]::new($key)) ([IntPtr]::Zero)
}
function Send-Cursor([int]$hit, [IntPtr]$owner) {
    Send-Observed 0x20 ([VaporCruiseCursorProbe]::WindowParameter($owner)) ([VaporCruiseCursorProbe]::CursorParameter($hit))
}
try {
    $appId = [VaporCruiseMeasurementLaunch]::Start($Executable, ('"' + $Executable + '"'), $project)
    $appProcess = Get-Process -Id $appId
    $appHandle = $appProcess.Handle
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 5) {
        $window = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
        if ($window -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($window)) { break }
        if ($appProcess.HasExited) { throw 'App exited before cursor checks.' }
        Start-Sleep -Milliseconds 40
    }
    if ($window -eq [IntPtr]::Zero -or -not [VaporCruiseMeasurementLaunch]::IsWindowVisible($window)) { throw 'Visible owned window not ready within five seconds.' }
    # Move only the owned window, never the physical pointer; no activation.
    if (-not [VaporCruiseMeasurementLaunch]::SetWindowPos($window, [IntPtr]::Zero, $origin.position.x - 320, $origin.position.y - 180, 640, 360, 0x10)) { throw 'Owned window placement failed.' }
    Send-Cursor 2 $window
    Check-Cursor 'windowed-drag-arrow' $false
    $rect = [VaporCruiseMeasurementLaunch+RECT]::new()
    [VaporCruiseMeasurementLaunch]::GetWindowRect($window, [ref]$rect) | Out-Null
    $centerHit = [VaporCruiseCursorProbe]::Send($window, 0x84, [UIntPtr]::Zero, [VaporCruiseCursorProbe]::PointParameter($origin.position.x, $origin.position.y))
    $cornerHit = [VaporCruiseCursorProbe]::Send($window, 0x84, [UIntPtr]::Zero, [VaporCruiseCursorProbe]::PointParameter($rect.left + 1, $rect.top + 1))
    if ($centerHit -ne 2 -or $cornerHit -ne 13) { throw 'Windowed drag/resize hit testing failed.' }
    Send-Cursor 13 $window
    Check-Cursor 'windowed-corner-resize' $false 32642
    Send-Cursor 2 $window
    Send-Key 0x46
    # No injected WM_SETCURSOR after entry: this checks automatic refresh.
    Check-Cursor 'stationary-fullscreen-entry' $true
    Send-Cursor 1 $window
    Check-Cursor 'fullscreen-client-message' $true
    Send-Cursor 13 $window
    Check-Cursor 'nonclient-message-not-hidden' $false 32642
    # Invalid containing-HWND must not invoke our fullscreen hide policy.
    # Seed a visible cursor first; delegating this message may leave it intact.
    Send-Cursor 1 ([IntPtr]::Zero)
    Check-Cursor 'foreign-owner-message-preserves-visible-cursor' $false 32642
    Send-Cursor 1 $window
    Send-Key 0x1B
    Check-Cursor 'stationary-Esc-restoration' $false
    Send-Key 0x46
    Check-Cursor 'second-stationary-entry' $true
    Send-Key 0x46
    Check-Cursor 'stationary-F-restoration' $false
    Send-Cursor 13 $window
    Check-Cursor 'resize-policy-after-fullscreen' $false 32642
    Send-Cursor 2 $window
    Check-Cursor 'drag-policy-after-fullscreen' $false
    $restored = [VaporCruiseMeasurementLaunch+RECT]::new()
    [VaporCruiseMeasurementLaunch]::GetWindowRect($window, [ref]$restored) | Out-Null
    if ($restored.left -ne $rect.left -or $restored.top -ne $rect.top -or $restored.right -ne $rect.right -or $restored.bottom -ne $rect.bottom) { throw 'Fullscreen did not restore owned window geometry.' }
    if ([VaporCruiseMeasurementLaunch]::GetForegroundWindow() -ne $foreground) { throw 'Foreground changed externally; no focus request was made.' }
    [VaporCruiseCursorProbe]::Send($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (-not $appProcess.WaitForExit(5000) -or $appProcess.ExitCode -ne 0) { throw 'Cursor-test app did not exit cleanly within five seconds.' }
    $status = if ($failures.Count -eq 0) { 'pass' } else { 'fail' }
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'error.log') -Encoding UTF8
    throw
} finally {
    [pscustomobject]@{status=$status; executable_sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash; failures=@($failures); checks=@($events); messages=@($messages);
        pointer_x=$origin.position.x; pointer_y=$origin.position.y;
        global_observation_timeout_ms=150; expected_stability_ms=25;
        scope='Real owned HWND and GetCursorInfo. No pointer movement, global input or focus request. Actual mouse movement, interactive drag/resize, focus transfer and outside-restored-rectangle refresh remain manual/unverified.'} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
    if ($null -ne $appProcess -and -not $appProcess.HasExited) {
        [VaporCruiseMeasurementLaunch]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (-not $appProcess.WaitForExit(3000)) { Stop-Process -Id $appProcess.Id }
    }
    if ($oldDpi -ne [IntPtr]::Zero) { [VaporCruiseCursorProbe]::SetThreadDpiAwarenessContext($oldDpi) | Out-Null }
}
if ($status -ne 'pass') { throw ('FAIL cursor checks: ' + ($failures -join ', ')) }
Write-Output 'PASS: stationary fullscreen entry/Esc/F restoration, scoped client cursor, windowed hit testing and resize/drag cursor policy.'
