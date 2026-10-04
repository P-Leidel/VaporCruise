param([Parameter(Mandatory=$true)][string]$Executable, [Parameter(Mandatory=$true)][string]$OutputDirectory, [string]$PythonExecutable)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/python-runtime.ps1')
$taskPython = Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
& (Join-Path $PSScriptRoot 'terrain-render-smoke.ps1') -Executable $Executable -OutputDirectory $OutputDirectory -PythonExecutable $taskPython
if ($LASTEXITCODE -ne 0) { throw 'Terrain/lifecycle smoke failed.' }
& $taskPython (Join-Path $PSScriptRoot 'background-quality-tests.py') --directory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'City background smoke failed.' }
