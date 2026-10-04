param([Parameter(Mandatory=$true)][string]$Fixture,[Parameter(Mandatory=$true)][string]$OutputRoot,[string]$PythonExecutable)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
$python=Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
$exe=[IO.Path]::GetFullPath($Fixture)
$output=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('recovery-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Split-Path $output -Parent) | Out-Null
$identity=(Get-FileHash -LiteralPath $exe).Hash
Invoke-VaporCruiseProcess -Executable $exe -Arguments @($output) -TimeoutMilliseconds 1200000
& $python (Join-Path $PSScriptRoot 'render-recovery-comparisons.py') $output
if($LASTEXITCODE -ne 0){throw 'Recovery comparisons failed.'}
if((Get-FileHash -LiteralPath $exe).Hash -ne $identity){throw 'Fixture changed during acceptance.'}
@{completion=$true;executable=$exe;executable_sha256=$identity;injected_loss=$true;real_driver_removal_verified=$false;output=$output} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Output "PASS: render recovery fixture $output"
