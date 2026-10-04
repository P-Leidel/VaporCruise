param([string]$Directory = 'build/visual/road-terrain-final', [string]$PythonExecutable)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
$taskPython = Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
& $taskPython (Join-Path $PSScriptRoot 'terrain-quality-tests.py') --directory $Directory
if ($LASTEXITCODE -ne 0) { throw 'Continuous road marking and slope motion criteria failed.' }
