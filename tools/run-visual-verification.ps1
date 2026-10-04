param([Parameter(Mandatory=$true)][string]$Executable,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$PythonExecutable,
      [string[]]$Suite=@('current'),[string]$BaselineDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'python-runtime.ps1')
. (Join-Path $PSScriptRoot 'verification-harness.ps1')
$project=Split-Path $PSScriptRoot -Parent
$catalog=Get-VaporCruiseVisualSuites -ManifestPath (Join-Path $project 'tests/visual-suite-manifest.json')
$selected=@(Select-VaporCruiseVisualSuites -Catalog $catalog -Suite $Suite)
foreach ($entry in $selected) {
    if ($entry.baseline_required) {
        if (-not $BaselineDirectory -or -not (Test-Path -LiteralPath $BaselineDirectory -PathType Container)) { throw "Missing baseline directory for $($entry.id)." }
        foreach ($name in $entry.baseline_files) {
            if (-not (Test-Path -LiteralPath (Join-Path $BaselineDirectory $name) -PathType Leaf)) { throw "Missing baseline file $name for $($entry.id)." }
        }
    }
}
$python=Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count) { throw 'Use a fresh visual verification directory.' }
$profiles=@($selected.capture_profile | Sort-Object -Unique)
foreach ($id in $profiles) {
    $profile=$catalog.profiles.PSObject.Properties[$id].Value
    Assert-VaporCruiseFreshOutputs -Directory (Join-Path $output $id) -Names @($profile.frames | ForEach-Object { $_.Name+'.png' })
    if ($profile.debug_only) { Assert-VaporCruiseDebugExecutable $exe }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$report=[ordered]@{schema_version=1;executable=$exe;executable_sha256=(Get-FileHash $exe).Hash;
    suite_manifest_sha256=(Get-FileHash (Join-Path $project 'tests/visual-suite-manifest.json')).Hash;
    completion=$false;results=@();manual_hardware='Sleep/resume, locking, multi-monitor/DPI, physical pointer, actual driver removal and two-hour soak unverified.'}
$reportPath=Join-Path $output 'verification.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
foreach ($id in $profiles) {
    $profile=$catalog.profiles.PSObject.Properties[$id].Value
    $captureDirectory=Join-Path $output $id
    if ($id -eq 'rim') {
        & (Join-Path $project 'tests/capture-sun-rim.ps1') -Executable $exe -OutputDirectory $captureDirectory
    } else {
        Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $captureDirectory -Frames $profile.frames -ManifestName $profile.manifest_name
    }
}
$failed=@()
foreach ($entry in $selected) {
    $captureDirectory=Join-Path $output $entry.capture_profile
    foreach ($name in $entry.required_files) { Assert-VaporCruiseOutputs @((Join-Path $captureDirectory $name)) }
    $profile=$catalog.profiles.PSObject.Properties[$entry.capture_profile].Value
    foreach ($pose in $profile.frames) { Assert-VaporCruisePng -Path (Join-Path $captureDirectory ($pose.Name+'.png')) -Width $pose.Width -Height $pose.Height }
    $arguments=@((Join-Path $project $entry.script),'--directory',$captureDirectory)+@($entry.arguments)
    if ($entry.baseline_required) { $arguments+=@('--baseline',[IO.Path]::GetFullPath($BaselineDirectory)) }
    # Windows PowerShell treats redirected native stderr as an ErrorRecord.
    # Read both pipes concurrently so benign unittest progress cannot terminate
    # verification, and judge the checker by its actual process exit code.
    $checker=$null
    try {
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=$python
        $info.Arguments=($arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' '
        $info.UseShellExecute=$false
        $info.CreateNoWindow=$true
        $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $info.RedirectStandardOutput=$true
        $info.RedirectStandardError=$true
        $checker=[Diagnostics.Process]::Start($info)
        $stdout=$checker.StandardOutput.ReadToEndAsync()
        $stderr=$checker.StandardError.ReadToEndAsync()
        $finished=$checker.WaitForExit(60000)
        if (-not $finished) { $checker.Kill(); $checker.WaitForExit(5000) | Out-Null }
        Set-Content -LiteralPath (Join-Path $output ($entry.id+'.log')) -Encoding utf8 -Value (
            $stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult())
        if (-not $finished) { throw "Visual checker $($entry.id) timed out after 60 seconds." }
        $status=$checker.ExitCode
    } finally {
        if ($checker) {
            if (-not $checker.HasExited) { $checker.Kill(); $checker.WaitForExit(5000) | Out-Null }
            $checker.Dispose()
        }
    }
    Get-Content -LiteralPath (Join-Path $output ($entry.id+'.log')) -Tail 8 | Write-Output
    $report.results+=@{id=$entry.id;classification=$entry.classification;exit_code=$status}
    if ($status -ne 0) { $failed+=$entry.id }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
}
if ((Get-FileHash $exe).Hash -ne $report.executable_sha256) { throw 'Executable changed during visual verification.' }
$report.completion=($failed.Count -eq 0)
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
if ($failed.Count) { throw "Visual checks failed: $($failed -join ', '). See $output" }
Write-Output "PASS: selected visual checks ($($selected.id -join ', ')). Historical/comparison results are not current-design certification. $($report.manual_hardware)"
