param([Parameter(Mandatory=$true)][string]$Fixture, [Parameter(Mandatory=$true)][string]$PythonExecutable)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$root=Join-Path $project ('build/evidence/measurement-contract-'+[Guid]::NewGuid().ToString('N'))
$old=$env:VAPORCRUISE_FIXTURE_OMIT_JSON
$oldCompletion=$env:VAPORCRUISE_FIXTURE_COMPLETION
$completionFailures=[Collections.Generic.List[string]]::new()
try {
    $env:VAPORCRUISE_FIXTURE_OMIT_JSON=$null
    $env:VAPORCRUISE_FIXTURE_COMPLETION=$null
    & (Join-Path $project 'tools/measure-performance.ps1') -Executable $Fixture -OutputDirectory (Join-Path $root 'complete') -Runs 1 -CaptureSeconds 1 -SkipPresentMon
    $manifest=Get-Content (Join-Path $root 'complete/manifest.json') -Raw | ConvertFrom-Json
    if (-not $manifest.completion) { throw 'Complete CSV/metadata pair rejected.' }
    if ($manifest.requested_frame_limit_fps -ne 0) { throw 'Default wrapper cap no longer selects display-refresh pacing.' }
    & (Join-Path $project 'tools/measure-performance.ps1') -Executable $Fixture -OutputDirectory (Join-Path $root 'capped') -Runs 1 -CaptureSeconds 1 -FrameLimit 60 -SkipPresentMon
    $cappedManifest=Get-Content (Join-Path $root 'capped/manifest.json') -Raw | ConvertFrom-Json
    $cappedMetadata=Get-Content (Join-Path $root 'capped/run-01-app.csv.json') -Raw | ConvertFrom-Json
    if (-not $cappedManifest.completion -or $cappedManifest.requested_frame_limit_fps -ne 60 -or $cappedMetadata.frame_limit_fps -ne 60) {
        throw 'Requested cap was not recorded and forwarded to the measurement process.'
    }
    # Exercise the real wrapper's evidence boundary with real external files.
    # Removing duration/count validation must make these premature/empty runs fail.
    foreach ($mode in @('legacy','precision-rounding')) {
        $env:VAPORCRUISE_FIXTURE_COMPLETION=$mode
        $output=Join-Path $root $mode
        & (Join-Path $project 'tools/measure-performance.ps1') -Executable $Fixture -OutputDirectory $output -Runs 1 -CaptureSeconds 1 -SkipPresentMon
        if (-not (Get-Content (Join-Path $output 'manifest.json') -Raw | ConvertFrom-Json).completion) {
            throw "Compatible complete measurement rejected: $mode"
        }
    }
    foreach ($mode in @('premature','just-short','zero-frames','negative-frames','fractional-frames',
        'nonfinite-duration','infinite-duration','string-duration','null-duration','bool-duration',
        'string-frames','bool-frames','missing-duration','missing-frames')) {
        $env:VAPORCRUISE_FIXTURE_COMPLETION=$mode
        $output=Join-Path $root $mode
        $failed=$false
        try { & (Join-Path $project 'tools/measure-performance.ps1') -Executable $Fixture -OutputDirectory $output -Runs 1 -CaptureSeconds 1 -SkipPresentMon }
        catch { if ($_ -notmatch '(Incomplete|Invalid) measurement.*(duration|frames|metadata)') { throw }; $failed=$true }
        if (-not $failed) { $completionFailures.Add("Incomplete measurement accepted: $mode") }
        if ((Get-Content (Join-Path $output 'manifest.json') -Raw | ConvertFrom-Json).completion) {
            $completionFailures.Add("Incomplete measurement marked complete: $mode")
        }
        foreach ($name in @('run-01-app.csv','run-01-app.csv.json')) {
            if (-not (Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf) -or
                (Get-Item -LiteralPath (Join-Path $output $name)).Length -eq 0) {
                throw "Rejected measurement evidence was not preserved: $mode/$name"
            }
        }
        if ($failed -and -not (Test-Path -LiteralPath (Join-Path $output 'capture-error.log'))) {
            throw "Rejected measurement lacks its retained failure reason: $mode"
        }
    }
    $env:VAPORCRUISE_FIXTURE_COMPLETION=$null
    $env:VAPORCRUISE_FIXTURE_OMIT_JSON='1'
    $failed=$false
    try { & (Join-Path $project 'tools/measure-performance.ps1') -Executable $Fixture -OutputDirectory (Join-Path $root 'missing') -Runs 1 -CaptureSeconds 1 -SkipPresentMon }
    catch { if ($_ -notmatch 'Missing.*json') { throw }; $failed=$true }
    if (-not $failed) { throw 'Missing metadata was accepted.' }
    $manifest=Get-Content (Join-Path $root 'missing/manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.completion) { throw 'Incomplete measurement marked complete.' }
} finally {
    $env:VAPORCRUISE_FIXTURE_OMIT_JSON=$old
    $env:VAPORCRUISE_FIXTURE_COMPLETION=$oldCompletion
}
Write-Output 'PASS: external measurement CSV/metadata outputs and manifest completion.'

$copy=Join-Path $root 'mutable.exe'
Copy-Item -LiteralPath $Fixture -Destination $copy
function Export-Csv {
    param([Parameter(ValueFromPipeline=$true)]$InputObject,[string]$LiteralPath,[switch]$NoTypeInformation,[string]$Encoding)
    end {
        Set-Content -LiteralPath $LiteralPath -Value 'fixture resources'
        [IO.File]::AppendAllText($copy,'replacement')
    }
}
try {
    $failed=$false
    try { & (Join-Path $project 'tools/measure-performance.ps1') -Executable $copy -OutputDirectory (Join-Path $root 'changed') -Runs 1 -CaptureSeconds 1 -SkipPresentMon }
    catch { if ($_ -notmatch 'Executable changed') { throw }; $failed=$true }
    if (-not $failed) { throw 'Changed measurement executable was accepted.' }
    if ((Get-Content (Join-Path $root 'changed/manifest.json') -Raw | ConvertFrom-Json).completion) { throw 'Changed executable marked complete.' }
} finally { Remove-Item Function:Export-Csv }
$mirror=Join-Path $root 'comparison-project'
$mirrorTools=Join-Path $mirror 'tools'
New-Item -ItemType Directory -Path $mirrorTools -Force | Out-Null
foreach ($name in @('compare-background-performance.ps1','verification-harness.ps1','python-runtime.ps1')) {
    Copy-Item -LiteralPath (Join-Path $project ('tools/'+$name)) -Destination $mirrorTools
}
Set-Content -LiteralPath (Join-Path $mirrorTools 'analyze-performance.py') -Value 'print("{}")'
Set-Content -LiteralPath (Join-Path $mirrorTools 'measure-performance.ps1') -Value @'
param($Executable,$OutputDirectory,$Runs,$CaptureSeconds,$WarmupSeconds,$Width,$Height,$Palette,$PresentInterval,[switch]$PaletteTransition,[switch]$SkipPresentMon)
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Set-Content (Join-Path $OutputDirectory 'run-01-app.csv') 'fixture'
Set-Content (Join-Path $OutputDirectory 'run-01-app.csv.json') '{}'
if ($OutputDirectory -like '*candidate') { [IO.File]::AppendAllText($Executable,'replacement') }
'@
$baseline=Join-Path $mirror 'baseline.exe'; $candidate=Join-Path $mirror 'candidate.exe'
Set-Content $baseline 'baseline'; Set-Content $candidate 'candidate'
$output=Join-Path $mirror 'results'
$failed=$false
try { & (Join-Path $mirrorTools 'compare-background-performance.ps1') -Baseline $baseline -Candidate $candidate -OutputDirectory $output -Pairs 1 -SkipPresentMon -PythonExecutable $PythonExecutable }
catch { if ($_ -notmatch 'Executable changed') { throw }; $failed=$true }
if (-not $failed) { throw 'Final comparison executable replacement accepted.' }
if ((Get-Content (Join-Path $output 'comparison.json') -Raw | ConvertFrom-Json).completion) { throw 'Changed comparison marked complete.' }
Write-Output 'PASS: changed measurement/comparison binaries retain incomplete evidence.'
if ($completionFailures.Count -ne 0) { throw ($completionFailures -join "`n") }
Write-Output 'PASS: incomplete/empty/nonfinite measurement metadata is rejected; historical metadata and serialization precision remain compatible.'
