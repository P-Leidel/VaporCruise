param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) { throw 'Use a fresh fullscreen measurement directory.' }
New-Item -ItemType Directory -Path $output | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot '../tools/nonactivating-process.cs')
$csv=Join-Path $output 'fullscreen.csv'
$arguments=@('--measure',$csv,'--duration','1','--warmup','0','--width','640','--height','360','--palette','2','--fullscreen')
$command=(ConvertTo-VaporCruiseArgument $exe)+' '+(($arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' ')
$id=[VaporCruiseMeasurementLaunch]::Start($exe,$command,(Split-Path $PSScriptRoot -Parent))
$child=Get-Process -Id $id
try {
    $retainedHandle=$child.Handle
    if(-not $child.WaitForExit(15000) -or $child.ExitCode -ne 0) { throw 'Fullscreen measurement did not finish successfully.' }
    $metadata=Get-Content ($csv+'.json') -Raw | ConvertFrom-Json
    $rows=@(Import-Csv $csv)
    if($rows.Count -eq 0 -or $metadata.output_width -ne $metadata.desktop_width -or
        $metadata.output_height -ne $metadata.desktop_height -or
        $metadata.terrain_seed -ne '6215337262362608466' -or $metadata.present_interval -ne 1) {
        throw 'Fullscreen startup did not measure the monitor-sized deterministic scene.'
    }
    @{completion=$true;executable_sha256=(Get-FileHash $exe).Hash;output_width=$metadata.output_width;
      output_height=$metadata.output_height;retained_frames=$rows.Count;exit_code=$child.ExitCode} |
      ConvertTo-Json | Set-Content (Join-Path $output 'verification.json') -Encoding utf8
    Write-Output "PASS: explicit fullscreen startup measures $($metadata.output_width)x$($metadata.output_height), $($rows.Count) rows."
} finally {
    if(-not $child.HasExited) { $child.Kill(); $child.WaitForExit(5000) | Out-Null }
    $child.Dispose()
}
