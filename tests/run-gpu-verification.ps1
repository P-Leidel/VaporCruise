param([ValidateSet('lifecycle','visual','rim')][string]$Mode,
      [Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$OutputRoot,
      [Parameter(Mandatory=$true)][string]$PythonExecutable,[string]$Fixture)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$python=Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
$output=Join-Path $OutputRoot ($Mode+'-'+[Guid]::NewGuid().ToString('N'))
if ($Mode -eq 'lifecycle') {
    if (-not $Fixture) { $Fixture=Join-Path (Split-Path $Executable) 'TerrainLifecycleFixture.exe' }
    Invoke-VaporCruiseProcess -Executable $Fixture -Arguments @($output) -ExpectedOutputs @(
        (Join-Path $output 'before.png'),(Join-Path $output 'resized.png'),
        (Join-Path $output 'restored.png'),(Join-Path $output 'lifecycle.json'))
    Assert-VaporCruisePng (Join-Path $output 'before.png') 640 360
    Assert-VaporCruisePng (Join-Path $output 'resized.png') 1280 720
    Assert-VaporCruisePng (Join-Path $output 'restored.png') 640 360
    $metadata=Get-Content (Join-Path $output 'lifecycle.json') -Raw | ConvertFrom-Json
    $results=@('restored.png')
    if ($metadata.debug_recovery) {
        if ($metadata.after_generation -le $metadata.before_generation) { throw 'Debug recovery did not replace device generation.' }
        $results+='recovered.png'
    }
    foreach ($result in $results) {
        & $python (Join-Path $PSScriptRoot 'compare-resolved-captures.py') (Join-Path $output 'before.png') (Join-Path $output $result)
        if ($LASTEXITCODE -ne 0) { throw "Lifecycle pose changed after $result." }
    }
    @{schema_version=1;completion=$true;executable_sha256=(Get-FileHash $Executable).Hash;
      fixture_sha256=(Get-FileHash $Fixture).Hash;results=$results;manual_hardware='Unverified'} |
      ConvertTo-Json | Set-Content (Join-Path $output 'verification.json') -Encoding utf8
} else {
    $suite=if ($Mode -eq 'rim') { 'sun-rim' } else { 'current' }
    & (Join-Path $PSScriptRoot '../tools/run-visual-verification.ps1') -Executable $Executable -OutputDirectory $output -PythonExecutable $python -Suite $suite
}
Write-Output "GPU verification artifacts: $output"
