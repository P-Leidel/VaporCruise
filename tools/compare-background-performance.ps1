param([Parameter(Mandatory=$true)][string]$Baseline, [Parameter(Mandatory=$true)][string]$Candidate,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [ValidateRange(1,3)][int]$Pairs = 3, [switch]$IncludeAdditionalCases, [switch]$SkipPresentMon, [string]$PythonExecutable)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'python-runtime.ps1')
$taskPython = Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @()
. (Join-Path $PSScriptRoot 'verification-harness.ps1')
$taskProject = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
function TaskProjectPath([string]$path) {
    $resolved = [IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith($taskProject + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Benchmark paths must stay in this project.' }
    return $resolved
}
$Baseline = TaskProjectPath $Baseline; $Candidate = TaskProjectPath $Candidate; $OutputDirectory = TaskProjectPath $OutputDirectory
foreach ($taskExe in @($Baseline,$Candidate)) { if (-not (Test-Path -LiteralPath $taskExe -PathType Leaf)) { throw "Missing executable: $taskExe" } }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh comparison directory; preserving all prior evidence.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$taskCases = [Collections.Generic.List[object]]::new()
for ($taskPair = 1; $taskPair -le $Pairs; ++$taskPair) {
    $taskOrder = if ($taskPair -eq 2) { @('candidate','baseline') } else { @('baseline','candidate') }
    foreach ($taskKind in $taskOrder) { $taskCases.Add(@{Name="1080-pair-$taskPair-$taskKind"; Kind=$taskKind; Width=1920; Height=1080; Transition=$false}) }
}
if ($IncludeAdditionalCases) {
    foreach ($taskKind in @('baseline','candidate')) { $taskCases.Add(@{Name="1440-$taskKind"; Kind=$taskKind; Width=2560; Height=1440; Transition=$false}) }
    foreach ($taskKind in @('candidate','baseline')) { $taskCases.Add(@{Name="transition-$taskKind"; Kind=$taskKind; Width=1920; Height=1080; Transition=$true}) }
}
$taskHashes = @{baseline=(Get-FileHash $Baseline).Hash; candidate=(Get-FileHash $Candidate).Hash}
@{Baseline=$Baseline; Candidate=$Candidate; Hashes=$taskHashes; Cases=$taskCases.ToArray(); WarmupSeconds=30; CaptureSeconds=60;
    schema_version=1; completion=$false; Seed='6215337262362608466'; PresentInterval=1; Palette=0; DisplayedEvidenceRequested=(-not [bool]$SkipPresentMon)} |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'comparison.json') -Encoding UTF8
try {
    foreach ($taskCase in $taskCases) {
        $taskExe = if ($taskCase.Kind -eq 'baseline') { $Baseline } else { $Candidate }
        if ((Get-FileHash $taskExe).Hash -ne $taskHashes[$taskCase.Kind]) { throw 'Executable changed during comparison.' }
        $taskRun = Join-Path $OutputDirectory $taskCase.Name
        & (Join-Path $PSScriptRoot 'measure-performance.ps1') -Executable $taskExe -OutputDirectory $taskRun -Runs 1 -CaptureSeconds 60 -WarmupSeconds 30 -Width $taskCase.Width -Height $taskCase.Height -Palette 0 -PresentInterval 1 -PaletteTransition:$taskCase.Transition -SkipPresentMon:$SkipPresentMon
        if ((Get-FileHash $taskExe).Hash -ne $taskHashes[$taskCase.Kind]) { throw 'Executable changed during comparison.' }
        $taskCsv = Join-Path $taskRun 'run-01-app.csv'
        Assert-VaporCruiseOutputs @($taskCsv,(Join-Path $taskRun 'run-01-app.csv.json'))
        $taskArgs = @((Join-Path $PSScriptRoot 'analyze-performance.py'),'--app',$taskCsv,'--output',(Join-Path $taskRun 'analysis.json'))
        if (-not $SkipPresentMon) { $taskArgs += @('--presentmon',(Join-Path $taskRun 'run-01-presentmon.csv')) }
        & $taskPython @taskArgs | Set-Content (Join-Path $taskRun 'analysis.log')
        if ($LASTEXITCODE -ne 0) { throw 'Comparison analyzer failed.' }
        Write-Output "Completed $($taskCase.Name)"
    }
    foreach ($taskKind in @('baseline','candidate')) {
        $taskExe=if ($taskKind -eq 'baseline') { $Baseline } else { $Candidate }
        if ((Get-FileHash $taskExe).Hash -ne $taskHashes[$taskKind]) { throw 'Executable changed during comparison.' }
    }
} catch {
    $_ | Out-String | Set-Content (Join-Path $OutputDirectory 'comparison-error.log') -Encoding UTF8
    throw
}
$comparisonPath=Join-Path $OutputDirectory 'comparison.json'
$comparison=Get-Content $comparisonPath -Raw | ConvertFrom-Json
$comparison.completion=$true
$comparison | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $comparisonPath -Encoding utf8
Write-Output 'PASS: requested matched measurements retained; review analysis against baseline variation before acceptance.'
