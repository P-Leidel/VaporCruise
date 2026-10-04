param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($ownedPath in @($Executable,$OutputDirectory)) {
    if (-not $ownedPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Quit-test paths must stay in the project.'
    }
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh quit-test directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Add-Type -Path (Join-Path $project 'tools/nonactivating-process.cs')
$csv = Join-Path $OutputDirectory 'quit-app.csv'
$appId = [VaporCruiseMeasurementLaunch]::Start($Executable,
    ('"' + $Executable + '" --measure "' + $csv + '" --duration 60 --warmup 0 --width 640 --height 360'),$project)
$appProcess = Get-Process -Id $appId
$appHandle = $appProcess.Handle
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    $window = [IntPtr]::Zero
    while ($window -eq [IntPtr]::Zero -and $timer.Elapsed.TotalSeconds -lt 10) {
        if ($appProcess.HasExited) { throw 'App exited before the quit test.' }
        $candidate = [VaporCruiseMeasurementLaunch]::FindWindow($appId)
        if ($candidate -ne [IntPtr]::Zero -and [VaporCruiseMeasurementLaunch]::IsWindowVisible($candidate)) {
            $window = $candidate
        }
        Start-Sleep -Milliseconds 50
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Live app window not found.' }
    # PeekMessage sees this owned-window WM_QUIT before DispatchMessage.
    if (-not [VaporCruiseMeasurementLaunch]::PostMessage($window,0x12,[UIntPtr]::new(37),[IntPtr]::Zero)) {
        throw 'Owned-window quit message failed.'
    }
    if (-not $appProcess.WaitForExit(5000)) { throw 'Quit did not terminate within five seconds.' }
    if ($appProcess.ExitCode -ne 37) { throw "Quit code changed: $($appProcess.ExitCode)." }
    if (-not (Test-Path -LiteralPath $csv)) { throw 'Quit skipped final export.' }
    [pscustomobject]@{exit_code=$appProcess.ExitCode; expected=37; exported=$true} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
    Write-Output 'PASS: native frame-loop adapter preserves WM_QUIT exit code 37 and final export.'
} finally {
    if (-not $appProcess.HasExited) { Stop-Process -Id $appId }
    $appProcess.Dispose()
}
