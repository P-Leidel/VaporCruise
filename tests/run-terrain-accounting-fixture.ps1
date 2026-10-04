param([Parameter(Mandatory=$true)][string]$Fixture,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$taskExe=[IO.Path]::GetFullPath($Fixture)
$taskOutput=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('terrain-accounting-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Split-Path $taskOutput -Parent) | Out-Null
$taskIdentity=(Get-FileHash -LiteralPath $taskExe).Hash
Invoke-VaporCruiseProcess -Executable $taskExe -Arguments @($taskOutput,'--terrain-only') -ExpectedOutputs @((Join-Path $taskOutput 'measurement.csv'),(Join-Path $taskOutput 'simulated-363.png'),(Join-Path $taskOutput 'simulated-4.png'))
$taskRows=@(Import-Csv (Join-Path $taskOutput 'measurement.csv'))
if($taskRows.Count -ne 2 -or $taskRows[0].terrain_rows_built -ne '363' -or $taskRows[1].terrain_rows_built -ne '4' -or $taskRows[0].gpu_ms -ne '' -or $taskRows[1].gpu_ms -ne '') { throw 'Simulated recovery lost or duplicated prepared CPU rows.' }
if((Get-FileHash -LiteralPath $taskExe).Hash -ne $taskIdentity) { throw 'Fixture changed during accounting verification.' }
$taskImages=@(Get-ChildItem -LiteralPath $taskOutput -Filter '*.png' | ForEach-Object { @{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash} })
@{completion=$true;executable=$taskExe;executable_sha256=$taskIdentity;rows=$taskRows;images=$taskImages;simulated_loss=$true;real_driver_removal_verified=$false} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'verification.json') -Encoding utf8
Write-Output 'PASS: simulated recovery accounts initialization and partial staging once.'
