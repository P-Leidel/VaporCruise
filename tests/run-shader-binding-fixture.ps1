param([Parameter(Mandatory=$true)][string]$Fixture,[Parameter(Mandatory=$true)][string]$PythonExecutable,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$output=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('bindings-'+[guid]::NewGuid().ToString('N'))
Invoke-VaporCruiseProcess -Executable $Fixture -Arguments @($output) -TimeoutMilliseconds 180000
& $PythonExecutable (Join-Path $PSScriptRoot 'shader-binding-comparisons.py') $output
if($LASTEXITCODE) { throw 'Shader binding consensus comparisons failed; raw captures retained.' }
@{completion=$true;explicit_bindings_verified=$true;executable_sha256=(Get-FileHash $Fixture).Hash;pairs=25;reference='median of five; raw captures and deltas retained'} | ConvertTo-Json | Set-Content (Join-Path $output 'bindings.json')
