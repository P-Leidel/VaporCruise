$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot -Parent
$taskFixture = Join-Path $taskProject ('build/evidence/build-wrapper-' + [Guid]::NewGuid().ToString('N'))
$taskTools = Join-Path $taskFixture 'tools'
New-Item -ItemType Directory -Path $taskTools | Out-Null
Copy-Item -LiteralPath (Join-Path $taskProject 'tools/build.ps1') -Destination $taskTools
# Observe the real wrapper's CMake boundary without compiling or changing a shared build.
@'
[pscustomobject]@{ arguments = @($args); parallel_environment = $env:CMAKE_BUILD_PARALLEL_LEVEL } |
    ConvertTo-Json -Compress | Add-Content -LiteralPath $env:VAPORCRUISE_BUILD_TEST_LOG
$global:LASTEXITCODE = 0
'@ | Set-Content -LiteralPath (Join-Path $taskTools 'cmake-stub.ps1') -Encoding utf8
@'
$VaporCruiseCMake = Join-Path $PSScriptRoot 'cmake-stub.ps1'
'@ | Set-Content -LiteralPath (Join-Path $taskTools 'Use-LocalToolchain.ps1') -Encoding utf8
$taskOriginalParallel = $env:CMAKE_BUILD_PARALLEL_LEVEL
$taskOriginalLog = $env:VAPORCRUISE_BUILD_TEST_LOG
try {
    foreach ($taskCase in @(
        @{ name = 'default'; environment = $null; parallel = $null; expected = @('--parallel', '4') },
        @{ name = 'environment'; environment = '7'; parallel = $null; expected = @() },
        @{ name = 'explicit'; environment = '7'; parallel = 2; expected = @('--parallel', '2') }
    )) {
        $env:CMAKE_BUILD_PARALLEL_LEVEL = $taskCase.environment
        $env:VAPORCRUISE_BUILD_TEST_LOG = Join-Path $taskFixture ($taskCase.name + '.jsonl')
        $taskParameters = @{ SkipTests = $true }
        if ($null -ne $taskCase.parallel) { $taskParameters.Parallel = $taskCase.parallel }
        & (Join-Path $taskTools 'build.ps1') @taskParameters
        $taskCalls = @(Get-Content -LiteralPath $env:VAPORCRUISE_BUILD_TEST_LOG | ForEach-Object { $_ | ConvertFrom-Json })
        if ($taskCalls.Count -ne 3 -or ($taskCalls[0].arguments -join ' ') -ne '--preset windows-portable') {
            throw "Configure/build call sequence changed for $($taskCase.name)."
        }
        foreach ($taskConfiguration in @('debug', 'release')) {
            $taskCall = @($taskCalls | Where-Object { $_.arguments -contains ('windows-portable-' + $taskConfiguration) })
            $taskExpected = @('--build', '--preset', ('windows-portable-' + $taskConfiguration)) + $taskCase.expected
            if ($taskCall.Count -ne 1 -or ($taskCall[0].arguments -join '|') -ne ($taskExpected -join '|') -or
                $taskCall[0].parallel_environment -ne $taskCase.environment) {
                throw "Wrong $taskConfiguration CMake parallel arguments for $($taskCase.name)."
            }
        }
    }
} finally {
    $env:CMAKE_BUILD_PARALLEL_LEVEL = $taskOriginalParallel
    $env:VAPORCRUISE_BUILD_TEST_LOG = $taskOriginalLog
}
Write-Output 'PASS: default bounded compilation, environment override and explicit parallel override.'
