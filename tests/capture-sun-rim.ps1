param([string]$Executable='build/portable/bin/Debug/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/verification-harness.ps1')
$exe=(Resolve-Path -LiteralPath $Executable).Path
Assert-VaporCruiseDebugExecutable $exe
$output=[IO.Path]::GetFullPath($OutputDirectory)
Assert-VaporCruiseFreshOutputs -Directory $output -Names @('rim-on.png','rim-off.png','captures.json','capture-build.json','diagnostic-shaders')
$sourceDirectory=Join-Path $PSScriptRoot '../src/shaders'
$original=@(Get-ChildItem -LiteralPath $sourceDirectory -File | Get-FileHash)
$source=[IO.File]::ReadAllText((Join-Path $sourceDirectory 'scene-panorama.hlsli'))
$expression='solar.top * solar.glow * .024 * rim * backlight * facing'
if (-not $source.Contains($expression)) { throw 'Rim expression not found.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$diagnosticDirectory=Join-Path $output 'diagnostic-shaders'
New-Item -ItemType Directory -Path $diagnosticDirectory | Out-Null
Copy-Item -Path (Join-Path $sourceDirectory '*') -Destination $diagnosticDirectory
# Each child gets its own process-local override; tracked sources are never written.
$factory={
    param($Executable,$Arguments,$Environment)
    if ($Arguments[1] -like '*rim-off.png') {
        [IO.File]::WriteAllText((Join-Path $diagnosticDirectory 'scene-panorama.hlsli'),$source.Replace($expression,'float3(0,0,0)'))
    }
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Executable
    $info.Arguments=($Arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' '
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.EnvironmentVariables['VAPORCRUISE_SHADER_SOURCE_OVERRIDE']=$diagnosticDirectory
    [Diagnostics.Process]::Start($info)
}
try {
    Invoke-VaporCruiseCaptureMatrix -Executable $exe -OutputDirectory $output -Frames @(
        @{Name='rim-on';Width=1280;Height=720;Palette=0;Phase=0},
        @{Name='rim-off';Width=1280;Height=720;Palette=0;Phase=0}
    ) -ProcessFactory $factory
} finally {
    foreach ($record in $original) {
        if ((Get-FileHash -LiteralPath $record.Path).Hash -ne $record.Hash) { throw 'Tracked shader changed during diagnostic capture.' }
    }
}
Write-Output 'PASS: disposable Debug rim captures; tracked shader hashes retained.'
