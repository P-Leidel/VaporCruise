param([Parameter(Mandatory=$true)][string]$Executable,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$DebugShaders)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) { throw 'Use a fresh run-mode smoke directory.' }
New-Item -ItemType Directory -Path $output | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot '../tools/nonactivating-process.cs')
$results=[Collections.Generic.List[object]]::new()
function Check-Rejected([string]$Name,[string[]]$Arguments,[hashtable]$Environment=@{}) {
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$exe
    $info.Arguments=($Arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' '
    $info.UseShellExecute=$false
    $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.CreateNoWindow=$true
    $info.RedirectStandardError=$true
    foreach($key in $Environment.Keys) { $info.EnvironmentVariables[$key]=$Environment[$key] }
    $child=[Diagnostics.Process]::Start($info)
    try {
        $retainedHandle=$child.Handle
        $clock=[Diagnostics.Stopwatch]::StartNew()
        $windowObserved=$false
        while(-not $child.WaitForExit(2)) {
            if($clock.Elapsed.TotalSeconds -gt 10) { throw "$Name did not exit promptly." }
            if([VaporCruiseMeasurementLaunch]::FindWindow($child.Id) -ne [IntPtr]::Zero) { $windowObserved=$true }
        }
        $diagnostic=$child.StandardError.ReadToEnd()
        if($child.ExitCode -ne 1 -or -not $diagnostic) { throw "$Name did not fail with a diagnostic." }
        if($Name -ne 'shader-failure' -and $windowObserved) { throw "$Name created a window before rejecting arguments." }
        $results.Add(@{name=$Name;exit_code=$child.ExitCode;window_observed=$windowObserved;diagnostic=$diagnostic})
    } finally {
        if(-not $child.HasExited) { $child.Kill(); $child.WaitForExit(5000) | Out-Null }
        $child.Dispose()
    }
}
$png=Join-Path $output 'rejected.png'; $csv=Join-Path $output 'rejected.csv'
Check-Rejected 'mixed-capture-first' @('--capture',$png,'--measure',$csv)
Check-Rejected 'mixed-measure-first' @('--measure',$csv,'--capture',$png)
Check-Rejected 'malformed' @('--capture',$png,'--phase','nan')
Check-Rejected 'missing-mode' @('--palette','1')
if((Test-Path $png) -or (Test-Path $csv)) { throw 'Rejected startup created output.' }

# Close the owned hidden window during initialization, before the first warm-up drain.
$warmupCsv=Join-Path $output 'pre-show.csv'
$args=@('--measure',$warmupCsv,'--warmup','0','--duration','10','--width','640','--height','360')
$command=(ConvertTo-VaporCruiseArgument $exe)+' '+(($args | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' ')
$id=[VaporCruiseMeasurementLaunch]::Start($exe,$command,(Split-Path $PSScriptRoot -Parent))
$child=Get-Process -Id $id
try {
    $retainedHandle=$child.Handle
    $clock=[Diagnostics.Stopwatch]::StartNew(); $window=[IntPtr]::Zero
    while($window -eq [IntPtr]::Zero -and -not $child.HasExited -and $clock.Elapsed.TotalSeconds -lt 5) {
        $window=[VaporCruiseMeasurementLaunch]::FindWindow($id)
        if($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 1 }
    }
    if($window -eq [IntPtr]::Zero -or [VaporCruiseMeasurementLaunch]::IsWindowVisible($window)) { throw 'Pre-show stop control did not find the hidden owned window.' }
    if(-not [VaporCruiseMeasurementLaunch]::PostMessage($window,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)) { throw 'Pre-show WM_CLOSE failed.' }
    if(-not $child.WaitForExit(10000) -or $child.ExitCode -ne 0 -or (Test-Path $warmupCsv)) { throw 'Pre-show warm-up stop changed exit/export policy.' }
    $results.Add(@{name='pre-show-stop';exit_code=$child.ExitCode;exported=$false})
} finally {
    if(-not $child.HasExited) { $child.Kill(); $child.WaitForExit(5000) | Out-Null }
    $child.Dispose()
}
if($DebugShaders) {
    $source=Join-Path $output 'invalid-shaders'
    New-Item -ItemType Directory -Path $source | Out-Null
    Set-Content (Join-Path $source 'SceneComposite.ps.hlsl') 'deliberately invalid HLSL' -Encoding ascii
    $failedCsv=Join-Path $output 'failed-render.csv'
    Check-Rejected 'shader-failure' @('--measure',$failedCsv,'--duration','1','--warmup','0') @{VAPORCRUISE_SHADER_SOURCE_OVERRIDE=$source}
    if(Test-Path $failedCsv) { throw 'Failed initialization exported measurements.' }
}
@{completion=$true;executable_sha256=(Get-FileHash $exe).Hash;results=@($results)} |
    ConvertTo-Json -Depth 6 | Set-Content (Join-Path $output 'verification.json') -Encoding utf8
Write-Output "PASS: rejected CLI, hidden pre-show stop and applicable shader failure controls ($($results.Count))."
