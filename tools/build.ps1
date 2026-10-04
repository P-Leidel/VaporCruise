[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [ValidateRange(1, 2147483647)]
    [int]$Parallel = 4,
    [switch]$SkipConfigure,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $projectRoot
try {
    . (Join-Path $PSScriptRoot 'Use-LocalToolchain.ps1')
    $ctest = Join-Path (Split-Path $VaporCruiseCMake) 'ctest.exe'
    if (-not $SkipTests -and -not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "Local test runner missing: $ctest"
    }
    if (-not $SkipConfigure) {
        & $VaporCruiseCMake --preset windows-portable
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }
    }
    $configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
    foreach ($buildConfiguration in $configurations) {
        $preset = 'windows-portable-' + $buildConfiguration.ToLowerInvariant()
        $buildArguments = @('--build', '--preset', $preset)
        # Keep explicit caller/environment choices; otherwise bound MSVC memory use.
        if ($PSBoundParameters.ContainsKey('Parallel') -or -not $env:CMAKE_BUILD_PARALLEL_LEVEL) {
            $buildArguments += @('--parallel', [string]$Parallel)
        }
        & $VaporCruiseCMake @buildArguments
        if ($LASTEXITCODE -ne 0) { throw "$buildConfiguration build failed (exit $LASTEXITCODE)." }
        if (-not $SkipTests) {
            & $ctest --test-dir build/portable -C $buildConfiguration --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw "$buildConfiguration tests failed (exit $LASTEXITCODE)." }
        }
    }
}
finally {
    Pop-Location
}
