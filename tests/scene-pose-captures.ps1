param([Parameter(Mandatory=$true)][string]$Baseline,
      [Parameter(Mandatory=$true)][string]$Candidate,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$PythonExecutable,[string]$BaselineShaderDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$python=Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) { throw 'Use a fresh pose comparison directory.' }
$poses=@()
foreach($palette in 0..2) {
    $progress=@(0,.25,.5,1)
    foreach($index in 0..3) {
        $poses+=@{Name=('p{0}-pp{1}' -f $palette,$index);
            Width=1280;Height=720;Palette=$palette;Transition=$progress[$index];
            CityLightPhase=.25;Cell='36028797018963971';Phase=.25;Seed='6215337262362608466'}
    }
}
$poses+=@(
    @{Name='origin';Width=1280;Height=720;CityLightPhase=0;Cell='0';Phase=0},
    @{Name='bounded-clocks';Width=1280;Height=720;Palette=2;Transition=1;CityLightPhase=.999999;Seed='18446744073709551615'},
    @{Name='seam-before';Width=1280;Height=720;Cell='31';Phase=.9999;Transition=.25},
    @{Name='seam-after';Width=1280;Height=720;Cell='32';Phase=.0001;Transition=.5},
    @{Name='landscape';Width=1280;Height=720;Palette=1;Transition=.5},
    @{Name='portrait';Width=720;Height=1280;Palette=2;Transition=.25;Hint=1},
    @{Name='ultrawide';Width=2560;Height=1080;Palette=1;Transition=.5;Dpi=144;Hint=1},
    @{Name='high-dpi';Width=1280;Height=720;Palette=0;Transition=.5;Dpi=192;Hint=.5},
    @{Name='alternate-seed';Width=1280;Height=720;Palette=2;Seed='0';Cell='3200'}
)
$baselineOutput=Join-Path $output 'baseline'; $candidateOutput=Join-Path $output 'candidate'
Invoke-VaporCruiseCaptureMatrix -Executable $Baseline -OutputDirectory $baselineOutput -Frames $poses -AllowLegacyCamera -Environment $(if($BaselineShaderDirectory) { @{VAPORCRUISE_SHADER_SOURCE_OVERRIDE=$BaselineShaderDirectory} } else { @{} })
Invoke-VaporCruiseCaptureMatrix -Executable $Candidate -OutputDirectory $candidateOutput -Frames $poses
$hashes=@()
foreach($pose in $poses) {
    $first=Join-Path $baselineOutput ($pose.Name+'.png'); $second=Join-Path $candidateOutput ($pose.Name+'.png')
    & $python (Join-Path $PSScriptRoot 'compare-resolved-captures.py') $first $second
    if($LASTEXITCODE -ne 0) { throw "Pose changed: $($pose.Name)" }
    $hashes+=@{name=$pose.Name;baseline_sha256=(Get-FileHash $first).Hash;candidate_sha256=(Get-FileHash $second).Hash}
}
$firstManifest=Get-Content (Join-Path $baselineOutput 'capture-build.json') -Raw | ConvertFrom-Json
$secondManifest=Get-Content (Join-Path $candidateOutput 'capture-build.json') -Raw | ConvertFrom-Json
if(-not $firstManifest.completion -or -not $secondManifest.completion -or
    $firstManifest.frames.Count -ne $poses.Count -or $secondManifest.frames.Count -ne $poses.Count) { throw 'Incomplete pose manifests.' }
@{schema_version=1;completion=$true;poses=$poses.Count;baseline_sha256=$firstManifest.executable_sha256;
    candidate_sha256=$secondManifest.executable_sha256;maximum_channel_delta=1;maximum_changed_fraction=.001;images=$hashes} |
    ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'comparison.json') -Encoding utf8
Write-Output "PASS: $($poses.Count) baseline/candidate scene poses within existing sparse resolve limits."
