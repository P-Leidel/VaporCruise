param([Parameter(Mandatory=$true)][string]$Fixture,[string]$OutputRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
if (-not $OutputRoot) { $OutputRoot=Join-Path (Split-Path $Fixture) 'shader-fixture-runs' }
$output=Join-Path $OutputRoot ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source=Join-Path $output 'SceneComposite.ps.hlsl'
foreach ($mode in @('success','failure')) {
    $text=if ($mode -eq 'success') { 'float4 main() : SV_Target { return float4(1,0,0,1); }' } else { 'deliberately invalid HLSL source' }
    Set-Content -LiteralPath $source -Value $text -Encoding ascii
    $result=Join-Path $output ($mode+'.json')
    Invoke-VaporCruiseProcess -Executable $Fixture -Arguments @("--expect-$mode",$result) -Environment @{VAPORCRUISE_SHADER_SOURCE_OVERRIDE=$output} -ExpectedOutputs @($result)
    $record=Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
    if ($record.compilation_succeeded -ne ($mode -eq 'success') -or ($mode -eq 'success' -and $record.bytecode_size -le 0)) { throw "Shader $mode control behaved incorrectly." }
}
Write-Output 'PASS: disposable valid shader compiles, invalid shader fails promptly.'
