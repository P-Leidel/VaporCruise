param([Parameter(Mandatory=$true)][string]$DebugExecutable,[Parameter(Mandatory=$true)][string]$ReleaseExecutable)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'tools/verification-harness.ps1')
$sourceDirectory=Join-Path $project 'src/shaders'
$before=@(Get-ChildItem $sourceDirectory -File | Get-FileHash)
$output=Join-Path $project ('build/evidence/diagnostic-contract-'+[Guid]::NewGuid().ToString('N'))
$failed=$false
try { & (Join-Path $PSScriptRoot 'capture-sun-rim.ps1') -Executable $ReleaseExecutable -OutputDirectory $output }
catch { if ($_ -notmatch 'Debug') { throw }; $failed=$true }
if (-not $failed -or (Test-Path $output)) { throw 'Release diagnostic launch was not rejected before output.' }
& (Join-Path $PSScriptRoot 'capture-sun-rim.ps1') -Executable $DebugExecutable -OutputDirectory $output
foreach ($record in $before) { if ((Get-FileHash $record.Path).Hash -ne $record.Hash) { throw 'Tracked shader mutated.' } }
$original=(Get-FileHash (Join-Path $output 'rim-on.png')).Hash
$failed=$false
try { & (Join-Path $PSScriptRoot 'capture-sun-rim.ps1') -Executable $DebugExecutable -OutputDirectory $output }
catch { if ($_ -notmatch 'fresh|existing|collision') { throw }; $failed=$true }
if (-not $failed -or (Get-FileHash (Join-Path $output 'rim-on.png')).Hash -ne $original) { throw 'Existing diagnostic evidence replaced.' }
foreach ($record in $before) { if ((Get-FileHash $record.Path).Hash -ne $record.Hash) { throw 'Tracked shader mutated after failure.' } }
Write-Output 'PASS: Debug override captured, Release rejected, collisions retained and tracked shaders unchanged.'
