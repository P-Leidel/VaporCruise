param([Parameter(Mandatory=$true)][string]$Executable,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$Fixture,[string]$PythonExecutable)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$taskPython=Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
$taskExe=(Resolve-Path -LiteralPath $Executable).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if (-not $Fixture) { $Fixture=Join-Path (Split-Path $taskExe) 'TerrainLifecycleFixture.exe' }
if (-not (Test-Path -LiteralPath $Fixture)) { throw 'Required GPU lifecycle fixture is missing.' }
$frames=@()
foreach ($name in @('magenta','repeat','hint')) {
    $frames+=@{Name=$name;Width=1280;Height=720;Hint=$(if ($name -eq 'hint') {1} else {0})}
}
foreach ($shape in @(@(480,320),@(720,1280),@(2560,1080))) {
    $frames+=@{Name="aspect-$($shape[0])x$($shape[1])";Width=$shape[0];Height=$shape[1]}
}
if (Test-Path -LiteralPath (Join-Path $taskOutput 'lifecycle')) { throw 'Use a fresh smoke directory.' }
Invoke-VaporCruiseCaptureMatrix -Executable $taskExe -OutputDirectory $taskOutput -Frames $frames
& $taskPython (Join-Path $PSScriptRoot 'compare-resolved-captures.py') (Join-Path $taskOutput 'magenta.png') (Join-Path $taskOutput 'repeat.png')
if ($LASTEXITCODE -ne 0) { throw 'Deterministic route snapshot differs.' }
& $taskPython (Join-Path $PSScriptRoot 'terrain-quality-tests.py') --directory $taskOutput --smoke
if ($LASTEXITCODE -ne 0) { throw 'Terrain GPU composition criteria failed.' }
$fixtureOutput=Join-Path $taskOutput 'lifecycle'
Invoke-VaporCruiseProcess -Executable $Fixture -Arguments @($fixtureOutput) -ExpectedOutputs @(
    (Join-Path $fixtureOutput 'before.png'),(Join-Path $fixtureOutput 'resized.png'),
    (Join-Path $fixtureOutput 'restored.png'),(Join-Path $fixtureOutput 'lifecycle.json'))
$metadata=Get-Content (Join-Path $fixtureOutput 'lifecycle.json') -Raw | ConvertFrom-Json
$results=@('restored.png')
if ($metadata.debug_recovery) {
    if ($metadata.after_generation -le $metadata.before_generation) { throw 'Recovery did not rebuild GPU resources.' }
    $results+='recovered.png'
}
foreach ($result in $results) {
    & $taskPython (Join-Path $PSScriptRoot 'compare-resolved-captures.py') (Join-Path $fixtureOutput 'before.png') (Join-Path $fixtureOutput $result)
    if ($LASTEXITCODE -ne 0) { throw "Route changed after $result" }
}
Write-Output 'PASS: actual GPU snapshots, resize/restoration and retained lifecycle pose.'
