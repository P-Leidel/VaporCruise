param([string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
      [Parameter(Mandatory=$true)][string]$OutputDirectory, [switch]$Motion, [string]$PythonExecutable)
$ErrorActionPreference = 'Stop'
$taskExe = (Resolve-Path -LiteralPath $Executable).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
$taskFrames = [Collections.Generic.List[object]]::new()
foreach ($taskShape in @(@('tall',480,2560),@('minimum-aspect',240,7680),@('superwide',5120,1440))) {
    $taskFrames.Add(@{Name=$taskShape[0]; Width=$taskShape[1]; Height=$taskShape[2]; Palette=0; CityLightPhase=0})
}
foreach ($taskPalette in 0..2) {
    foreach ($taskShape in @(@(1280,720),@(1920,1080),@(2560,1080),@(720,1280),@(480,320))) {
        $taskFrames.Add(@{Name="shape-p$taskPalette-$($taskShape[0])x$($taskShape[1])"; Width=$taskShape[0]; Height=$taskShape[1]; Palette=$taskPalette; CityLightPhase=0})
    }
    foreach ($taskPhase in @(.25,.5,.75)) {
        $taskName = if ($taskPalette -eq 0 -and $taskPhase -eq .25) { 'light-quarter' } else { "light-p$taskPalette-$taskPhase" }
        $taskFrames.Add(@{Name=$taskName; Width=1280; Height=720; Palette=$taskPalette; CityLightPhase=$taskPhase})
    }
}
foreach ($taskValue in @(@('9998',.9998),@('9999',.9999),@('0001',.0001),@('0002',.0002))) {
    $taskFrames.Add(@{Name="light-$($taskValue[0])"; Width=1280; Height=720; Palette=0; CityLightPhase=$taskValue[1]})
}
& (Join-Path $PSScriptRoot 'capture-visuals.ps1') -Executable $taskExe -OutputDirectory $taskOutput -Motion:$Motion -PythonExecutable $PythonExecutable -AdditionalFrames $taskFrames.ToArray()
Write-Output "PASS: complete city palette/aspect/light-phase matrix at $taskOutput"
