param([Parameter(Mandatory=$true)][string]$PythonExecutable)
$ErrorActionPreference = 'Stop'
$PythonExecutable = (Resolve-Path -LiteralPath $PythonExecutable).Path
$project = Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'tools/python-runtime.ps1')
$fixture = Join-Path $project ('build/evidence/python-runtime-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
function Assert-Fails([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Unexpected failure: $_" }
        return
    }
    throw 'Expected selection/import preflight to fail.'
}
$originalPython = $env:VAPORCRUISE_PYTHON
$originalPath = $env:PATH
$originalHome = $env:PYTHONHOME
try {
    $env:VAPORCRUISE_PYTHON = Join-Path $fixture 'missing-env.exe'
    $selected = Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
    if ($selected -ne $PythonExecutable) { throw "Explicit interpreter lost precedence: $selected" }
    Assert-Fails { Resolve-VaporCruisePython -PythonExecutable (Join-Path $fixture 'missing.exe') } 'Python'
    Assert-Fails { Resolve-VaporCruisePython } 'Python'
    $env:VAPORCRUISE_PYTHON = $PythonExecutable
    Assert-Fails { Resolve-VaporCruisePython -RequiredImports @('vaporcruise_intentionally_missing_module') } 'import|Python'
    $env:VAPORCRUISE_PYTHON = $null
    $env:PATH = Split-Path $PythonExecutable
    if ((Resolve-VaporCruisePython) -ne $PythonExecutable) { throw 'PATH discovery selected another interpreter.' }
    $env:PATH = $fixture
    Assert-Fails { Resolve-VaporCruisePython } 'Python'
    $env:PATH = $originalPath
    $space = Join-Path $fixture 'runtime with spaces'
    New-Item -ItemType Directory $space | Out-Null
    Copy-Item -LiteralPath $PythonExecutable -Destination (Join-Path $space 'python.exe')
    Get-ChildItem -LiteralPath (Split-Path $PythonExecutable) -Filter '*.dll' | Copy-Item -Destination $space
    $env:PYTHONHOME = Split-Path $PythonExecutable
    $spacePython = Join-Path $space 'python.exe'
    if ((Resolve-VaporCruisePython -PythonExecutable $spacePython) -ne $spacePython) { throw 'Space-containing path failed.' }
    $env:PYTHONHOME = $originalHome
    $sentinel = Join-Path $fixture 'launched.txt'
    function Start-Process { Set-Content -LiteralPath $sentinel 'unexpected launch'; throw 'Unexpected process launch.' }
    $missing = Join-Path $fixture 'missing.exe'
    $exe = Join-Path $fixture 'dummy.exe'
    Set-Content -LiteralPath $exe 'not executable'
    foreach ($wrapper in @('tools/capture-visuals.ps1','tests/terrain-render-smoke.ps1','tests/background-render-smoke.ps1','tools/compare-background-performance.ps1')) {
        $output = Join-Path $fixture ([IO.Path]::GetFileNameWithoutExtension($wrapper))
        $wrapperParameters = @{PythonExecutable=$missing; OutputDirectory=$output}
        if ($wrapper -like '*compare-*') { $wrapperParameters.Baseline=$exe; $wrapperParameters.Candidate=$exe; $wrapperParameters.SkipPresentMon=$true }
        else { $wrapperParameters.Executable=$exe }
        Assert-Fails { & (Join-Path $project $wrapper) @wrapperParameters } 'Python'
        if ((Test-Path -LiteralPath $sentinel) -or (Test-Path -LiteralPath $output)) { throw "$wrapper created output/launched before preflight." }
    }
} finally {
    $env:VAPORCRUISE_PYTHON = $originalPython
    $env:PATH = $originalPath
    $env:PYTHONHOME = $originalHome
}
Write-Output 'PASS: runtime precedence, imports, spaces, discovery and wrapper preflight.'
