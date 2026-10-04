param([Parameter(Mandatory=$true)][string]$PythonExecutable,[string]$ArtworkMetadataFixture)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'tools/verification-harness.ps1')
$fixture = Join-Path $project ('build/evidence/harness-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
function Assert-Fails([scriptblock]$Action,[string]$Pattern) {
    try { & $Action } catch {
        $diagnostic="$($_.FullyQualifiedErrorId): $_"
        if ($diagnostic -notmatch $Pattern) { throw "Unexpected error: $diagnostic" }
        return
    }
    throw 'Expected failure.'
}
# Removing native quoting breaks the actual child argv round trip.
$argvFile = Join-Path $fixture 'argv.json'
$values = @('a path with spaces','embedded"quote','tail\','x\"y','')
Invoke-VaporCruiseProcess -Executable $PythonExecutable -Arguments (@('-c','import json,sys; from pathlib import Path; Path(sys.argv[1]).write_text(json.dumps(sys.argv[2:]))',$argvFile) + $values) -ExpectedOutputs @($argvFile)
$actual = (ConvertFrom-Json -InputObject ('{"values":'+(Get-Content $argvFile -Raw)+'}')).values
if ($actual.Count -ne $values.Count) { throw 'Argument count changed.' }
for ($i=0;$i -lt $values.Count;++$i) { if ($actual[$i] -cne $values[$i]) { throw "Argument changed at $i." } }
Assert-Fails { Invoke-VaporCruiseProcess -Executable $PythonExecutable -Arguments @('-c','import sys; sys.exit(7)') } 'exit 7'
Assert-Fails { Invoke-VaporCruiseProcess -Executable $PythonExecutable -Arguments @('-c','import time; time.sleep(60)') -TimeoutMilliseconds 250 } 'timeout'
Assert-Fails { Invoke-VaporCruiseProcess -Executable $PythonExecutable -Arguments @('-c','pass') -ExpectedOutputs @((Join-Path $fixture 'absent.png')) } 'Missing'
$exe = Join-Path $fixture 'candidate.exe'
Set-Content $exe 'fixture executable'
$template = Join-Path $fixture 'template.png'
Invoke-VaporCruiseProcess -Executable $PythonExecutable -Arguments @('-c','from PIL import Image; import sys; Image.new("RGB",(64,48)).save(sys.argv[1])',$template) -ExpectedOutputs @($template)
$frames = @(@{Name='first';Width=64;Height=48;Palette=0},@{Name='second';Width=64;Height=48;Palette=1})
function New-FixtureProcess([int]$ExitCode=0,[bool]$Finished=$true) {
    $child = [pscustomobject]@{ExitCode=$ExitCode;HasExited=$Finished}
    $child | Add-Member ScriptMethod WaitForExit { param($Timeout) return $this.HasExited }
    $child | Add-Member ScriptMethod Kill { $this.HasExited=$true }
    $child | Add-Member ScriptMethod Dispose {}
    return $child
}
$adapter = {
    param($Executable,$Arguments,$Environment)
    if (@($Arguments | Where-Object { $_ -in @('--interior','--interior-transition','--cabin-seconds') }).Count) { throw 'Removed capture arguments reached the child.' }
    $path = $Arguments[1]
    Copy-Item -LiteralPath $template -Destination $path
    New-FixtureProcess
}
$output = Join-Path $fixture 'complete'
Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $output -Frames $frames -ProcessFactory $adapter
$manifest = Get-Content (Join-Path $output 'capture-build.json') -Raw | ConvertFrom-Json
if (-not $manifest.completion -or $manifest.frames.Count -ne 2 -or $manifest.frames[0].Seed -isnot [string] -or $manifest.frames[0].Cell -isnot [string]) { throw 'Complete pose/hash manifest missing.' }
if ($manifest.executable_sha256 -ne (Get-FileHash $exe).Hash) { throw 'Executable hash missing.' }
foreach ($field in @('Interior','InteriorTransition','CabinSeconds','Blend','Motion')) {
    if ($manifest.frames[0].PSObject.Properties[$field]) { throw "Removed pose field persisted: $field" }
    $legacyFrame=@{Name='legacy';Width=64;Height=48}
    $legacyFrame[$field]=0
    Assert-Fails { ConvertTo-VaporCruisePose $legacyFrame } 'Removed|removed'
}
$collision = Join-Path $fixture 'collision'
New-Item -ItemType Directory $collision | Out-Null
Set-Content (Join-Path $collision 'second.png') 'preserve'
$before = (Get-FileHash (Join-Path $collision 'second.png')).Hash
Assert-Fails { Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $collision -Frames $frames -ProcessFactory $adapter } 'existing|fresh|collision'
if ((Test-Path (Join-Path $collision 'first.png')) -or (Get-FileHash (Join-Path $collision 'second.png')).Hash -ne $before) { throw 'Late collision launched or overwrote.' }
foreach ($kind in @('failed','timeout','missing','hash')) {
    $failedOutput=Join-Path $fixture $kind
    $failedAdapter={
        param($Executable,$Arguments,$Environment)
        if ($kind -eq 'failed') { return New-FixtureProcess 7 }
        if ($kind -eq 'timeout') { return New-FixtureProcess 0 $false }
        if ($kind -eq 'hash') { Add-Content $Executable 'changed'; Copy-Item $template $Arguments[1] }
        New-FixtureProcess
    }
    Assert-Fails { Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $failedOutput -Frames $frames -ProcessFactory $failedAdapter } 'exit|timeout|Missing|changed'
    $manifest=Get-Content (Join-Path $failedOutput 'capture-build.json') -Raw | ConvertFrom-Json
    if ($manifest.completion) { throw "$kind run marked complete." }
}
$originalCulture=[Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]'de-DE'
    $pose = ConvertTo-VaporCruisePose @{Name='pose';Width=64;Height=48;Phase=.25;Seed='6215337262362608466';Cell='36028797018963971'}
    if ($pose.Phase -ne '0.25' -or $pose.Cell -ne '36028797018963971') { throw 'Culture or integer pose changed.' }
} finally { [Threading.Thread]::CurrentThread.CurrentCulture=$originalCulture }
Write-Output 'PASS: process argv, failure, timeout, collisions, manifests, hashes and invariant poses.'


$motionFrames=@(@{Name='motion-p0-00';Width=64;Height=48;Palette=0},@{Name='motion-p1-00';Width=64;Height=48;Palette=1})
$motionOutput=Join-Path $fixture 'motion-groups'
Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $motionOutput -Frames $motionFrames -ProcessFactory $adapter
$motionManifest=Get-Content (Join-Path $motionOutput 'capture-build.json') -Raw | ConvertFrom-Json
if ($motionManifest.expected_sequences.Count -ne 2 -or @($motionManifest.expected_sequences | Where-Object count -ne 1).Count) { throw 'Expected motion counts were merged across palettes.' }
$truncated=Join-Path $fixture 'truncated.png'
$bytes=[IO.File]::ReadAllBytes($template)
[IO.File]::WriteAllBytes($truncated,$bytes[0..23])
Assert-Fails { Assert-VaporCruisePng $truncated 64 48 } 'PNG|image|decode|invalid|parameter'


$missingPriorOutput=Join-Path $fixture 'motion-groups'
Remove-Item -LiteralPath (Join-Path $missingPriorOutput 'motion-p0-00.png')
Assert-Fails {
    Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $missingPriorOutput -Frames @(
        @{Name='motion-p2-00';Width=64;Height=48;Palette=2}
    ) -ManifestName 'motion-p2.json' -ProcessFactory $adapter
} 'Missing|missing'

$validCamera=[pscustomobject]@{height=3.2;focal_scale=.8;horizon_fraction=1.0/3;near=.1;far=640;cell_size=2;minimum_terrain_reach=512}
Assert-VaporCruiseCamera $validCamera
foreach($field in @('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')) {
    foreach($bad in @($null,$true,'3.2',[double]::NaN,[double]::PositiveInfinity,0,-1)) {
        $invalid=$validCamera | ConvertTo-Json | ConvertFrom-Json
        $invalid.$field=$bad
        Assert-Fails { Assert-VaporCruiseCamera $invalid } 'camera'
    }
}
$invalid=$validCamera | ConvertTo-Json | ConvertFrom-Json
$invalid.near=700
Assert-Fails { Assert-VaporCruiseCamera $invalid } 'camera'
Write-Output 'PASS: native camera metadata rejects missing, nonnumeric, nonfinite and invalid range values.'

if ($ArtworkMetadataFixture) {
    Copy-Item -LiteralPath $ArtworkMetadataFixture -Destination (Join-Path $fixture 'ArtworkMetadataFixture.exe')
    $nativeCamera=(& $ArtworkMetadataFixture | ConvertFrom-Json).camera
    foreach ($defect in @('missing-camera','invalid-camera','changed-camera','missing-sidecar','invalid-sidecar','malformed-sidecar','changed-sidecar','wrong-size')) {
        $appendOutput=Join-Path $fixture $defect
        Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $appendOutput -Frames @($frames[0]) -ProcessFactory $adapter
        $sidecar=Join-Path $appendOutput 'camera-first.json'
        @{width=64;height=48;camera=$nativeCamera} | ConvertTo-Json -Depth 5 | Set-Content $sidecar
        $priorPath=Join-Path $appendOutput 'capture-build.json'
        $prior=Get-Content $priorPath -Raw | ConvertFrom-Json
        if ($defect -eq 'missing-camera') { $prior.camera=$null }
        if ($defect -eq 'invalid-camera') { $prior.camera.near=-1 }
        if ($defect -eq 'changed-camera') { $prior.camera.height=99 }
        if ($defect -eq 'missing-sidecar') { Remove-Item -LiteralPath $sidecar }
        if ($defect -eq 'invalid-sidecar') { $s=Get-Content $sidecar -Raw | ConvertFrom-Json; $s.camera.near=-1; $s | ConvertTo-Json -Depth 5 | Set-Content $sidecar }
        if ($defect -eq 'malformed-sidecar') { Set-Content $sidecar '{' }
        if ($defect -eq 'changed-sidecar') { @{width=64;height=48;camera=($nativeCamera | ConvertTo-Json | ConvertFrom-Json)} | ConvertTo-Json -Depth 5 | Set-Content $sidecar; $s=Get-Content $sidecar -Raw | ConvertFrom-Json; $s.camera.height=99; $s | ConvertTo-Json -Depth 5 | Set-Content $sidecar }
        if ($defect -eq 'wrong-size') { @{width=65;height=48;camera=$nativeCamera} | ConvertTo-Json -Depth 5 | Set-Content $sidecar }
        $prior | ConvertTo-Json -Depth 8 | Set-Content $priorPath
        $before=(Get-FileHash $priorPath).Hash
        Assert-Fails { Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $appendOutput -Frames @($frames[1]) -ManifestName 'append.json' -ProcessFactory $adapter } 'camera|Camera|dimensions|cannot find|does not exist|JSON'
        if ((Test-Path (Join-Path $appendOutput 'second.png')) -or (Get-FileHash $priorPath).Hash -ne $before) { throw 'Invalid prior evidence launched capture or replaced manifest.' }
    }
    $validAppend=Join-Path $fixture 'valid-append'
    $cameraAdapter={
        param($Executable,$Arguments,$Environment)
        Copy-Item -LiteralPath $template -Destination $Arguments[1]
        $captureName=[IO.Path]::GetFileNameWithoutExtension($Arguments[1])
        @{width=64;height=48;camera=$nativeCamera} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path (Split-Path $Arguments[1]) ('camera-'+$captureName+'.json'))
        New-FixtureProcess
    }
    Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $validAppend -Frames @($frames[0]) -ProcessFactory $cameraAdapter
    Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $validAppend -Frames @($frames[1]) -ManifestName 'append.json' -ProcessFactory $cameraAdapter
    $validManifest=Get-Content (Join-Path $validAppend 'capture-build.json') -Raw | ConvertFrom-Json
    if (-not $validManifest.completion -or $validManifest.frames.Count -ne 2) { throw 'Valid camera append rejected.' }
    Write-Output 'PASS: append rejects missing/invalid/changed camera and missing/changed/mis-sized prior sidecars before mutation.'
}
