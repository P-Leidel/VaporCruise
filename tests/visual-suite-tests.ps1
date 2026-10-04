param([Parameter(Mandatory=$true)][string]$PythonExecutable)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'tools/verification-harness.ps1')
$catalog=Get-VaporCruiseVisualSuites -ManifestPath (Join-Path $PSScriptRoot 'visual-suite-manifest.json')
$current=@(Select-VaporCruiseVisualSuites -Catalog $catalog -Suite @('current'))
if ($current.Count -lt 2 -or @($current | Where-Object classification -ne 'current').Count) { throw 'Default suite mixes current and other classes.' }
$expected=@(Get-ChildItem $PSScriptRoot -Filter '*.py' | Where-Object { $_.Name -notlike '*evidence-tests*' } | ForEach-Object { 'tests/'+$_.Name })
$expected+='tools/analyze-performance.py'
foreach ($script in $expected) {
    if (-not @($catalog.suites | Where-Object script -eq $script).Count) { throw "Unclassified Python script: $script" }
}
$historical=@(Select-VaporCruiseVisualSuites -Catalog $catalog -Suite @('horizon-depth'))
if ($historical.Count -ne 1 -or $historical[0].classification -ne 'historical') { throw 'Historical selection failed.' }
$fixture=Join-Path $project ('build/evidence/suite-preflight-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $fixture | Out-Null
$exe=Join-Path $fixture 'dummy.exe'
Set-Content $exe 'no launch'
foreach ($selection in @('highlights','curved-haze')) {
    $output=Join-Path $fixture $selection
    $failed=$false
    try { & (Join-Path $project 'tools/run-visual-verification.ps1') -Executable $exe -OutputDirectory $output -PythonExecutable $PythonExecutable -Suite $selection }
    catch { if ($_ -notmatch 'baseline') { throw }; $failed=$true }
    if (-not $failed -or (Test-Path $output)) { throw 'Missing baseline did not fail before capture.' }
}
foreach ($selection in @('refined-lighting','interior-peripheral')) {
    $retired=@($catalog.suites | Where-Object id -eq $selection)
    if ($retired.Count -ne 1 -or $retired[0].classification -ne 'historical' -or -not $retired[0].retirement_reason -or $retired[0].capture_profile) { throw 'Removed interior suite lacks retirement history.' }
    $output=Join-Path $fixture $selection
    $failed=$false
    try { & (Join-Path $project 'tools/run-visual-verification.ps1') -Executable $exe -OutputDirectory $output -PythonExecutable $PythonExecutable -Suite $selection }
    catch { if ($_ -notmatch 'retired|Retired') { throw }; $failed=$true }
    if (-not $failed -or (Test-Path $output)) { throw 'Retired suite reached capture.' }
}
if ($catalog.profiles.PSObject.Properties['interiors'] -or $catalog.profiles.PSObject.Properties['wide-interior']) { throw 'Removed interior capture profiles remain executable.' }
foreach ($profile in $catalog.profiles.PSObject.Properties.Value) {
    foreach ($pose in $profile.frames) {
        if (@($pose.PSObject.Properties.Name | Where-Object { $_ -in @('Interior','InteriorTransition','CabinSeconds','Blend','Motion') }).Count) { throw 'Removed pose fields remain in capture profiles.' }
    }
}
$baseline=Join-Path $fixture 'empty-baseline'
New-Item -ItemType Directory $baseline | Out-Null
try {
    & (Join-Path $project 'tools/run-visual-verification.ps1') -Executable $exe -OutputDirectory (Join-Path $fixture 'absent-baseline-files') -PythonExecutable $PythonExecutable -Suite highlights -BaselineDirectory $baseline
    throw 'Missing baseline files accepted.'
} catch { if ($_ -notmatch 'baseline') { throw } }
$missingOutput=Join-Path $fixture 'bad-python'
try {
    & (Join-Path $project 'tools/run-visual-verification.ps1') -Executable $exe -OutputDirectory $missingOutput -PythonExecutable (Join-Path $fixture 'missing.exe')
    throw 'Bad Python accepted.'
} catch { if ($_ -notmatch 'Python') { throw } }
if (Test-Path $missingOutput) { throw 'Dependency failure created output.' }
foreach ($suite in $current) {
    $profile=$catalog.profiles.PSObject.Properties[$suite.capture_profile].Value
    if (-not $profile.frames -or -not $suite.required_files) { throw 'Active suite lacks complete pose/file inventory.' }
    foreach ($file in $suite.required_files) {
        if (-not @($profile.frames | Where-Object { ($_.Name+'.png') -eq $file }).Count) { throw "Required $file has no capture pose." }
    }
}
Write-Output 'PASS: suite inventory, classifications, baseline/dependency preflight and complete requests.'



$partialBaseline=Join-Path $fixture 'partial-curved-baseline'
New-Item -ItemType Directory -Path $partialBaseline | Out-Null
foreach ($name in @('magenta.png','cyan.png','amber.png')) { Set-Content (Join-Path $partialBaseline $name) 'baseline fixture' }
$partialOutput=Join-Path $fixture 'partial-curved-output'
$failed=$false
try { & (Join-Path $project 'tools/run-visual-verification.ps1') -Executable $exe -OutputDirectory $partialOutput -PythonExecutable $PythonExecutable -Suite curved-haze -BaselineDirectory $partialBaseline }
catch { if ($_ -notmatch 'Missing baseline file') { throw }; $failed=$true }
if (-not $failed -or (Test-Path $partialOutput)) { throw 'Partial curved-haze baseline reached capture.' }

# Execute the wrapper's actual pose construction without launching a GPU child.
$source=Get-Content (Join-Path $project 'tools/capture-visuals.ps1') -Raw
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
$statements=@($ast.EndBlock.Statements)
$start=@($statements | Where-Object { $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$frames' })[0].Extent.StartOffset
$end=@($statements | Where-Object { $_.Extent.Text -like 'Invoke-VaporCruiseCaptureMatrix *' })[0].Extent.StartOffset
$Seed=[uint64]42; $Motion=$true; $AdditionalFrames=@()
. ([scriptblock]::Create($source.Substring($start,$end-$start)))
$poses=@($frames | ForEach-Object { ConvertTo-VaporCruisePose $_ })
if ($poses.Count -ne 107 -or @($poses | Where-Object Seed -ne '42').Count) { throw 'Custom seed did not reach every static and motion pose.' }
Write-Output 'PASS: custom seed propagated to all 107 wrapper poses.'
