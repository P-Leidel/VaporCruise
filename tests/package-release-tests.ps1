param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$CMakeExecutable
)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot -Parent
$taskFixture = Join-Path $taskProject ('build/evidence/package-release-' + [Guid]::NewGuid().ToString('N'))
$taskTools = Join-Path $taskFixture 'tools'
$taskVisuals = Join-Path $taskFixture 'docs/current/progress/visuals/landscape-effects-2026-10-04'
$taskBinary = Join-Path $taskFixture 'bin'
$taskSource = Join-Path $taskFixture 'src'
New-Item -ItemType Directory -Path $taskTools, $taskVisuals, $taskBinary, $taskSource | Out-Null
Copy-Item -LiteralPath (Join-Path $taskProject 'tools/package-release.ps1') -Destination $taskTools
Set-Content -LiteralPath (Join-Path $taskFixture 'CMakeLists.txt') -Value 'project(VaporCruise VERSION 1.0.0 LANGUAGES CXX RC)'
Set-Content -LiteralPath (Join-Path $taskFixture '.gitignore') -Value "build/`nbin/"
$taskSourceFile = Join-Path $taskSource 'fixture.cpp'
Set-Content -LiteralPath $taskSourceFile -Value 'clean source fixture'
foreach ($taskName in @('README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Set-Content -LiteralPath (Join-Path $taskFixture $taskName) -Value 'release test fixture'
}
foreach ($taskName in @('magenta.png', 'cyan.png', 'amber.png', 'magenta-motion.gif')) {
    Set-Content -LiteralPath (Join-Path $taskVisuals $taskName) -Value 'release test fixture'
}
$taskExe = Join-Path $taskBinary 'VaporCruise.exe'
Copy-Item -LiteralPath $Executable -Destination $taskExe
$taskIdentityPath = Join-Path $taskBinary 'vaporcruise-build.json'
$taskDebugMarker = Join-Path $taskBinary 'vaporcruise-debug-diagnostics.json'
$taskPackage = Join-Path $taskTools 'package-release.ps1'
function Invoke-FixtureGit([string[]]$Arguments) {
    $taskGitOutput = & git -c core.autocrlf=false -c core.safecrlf=false -C $taskFixture @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture git failed: $taskGitOutput" }
    return $taskGitOutput
}
Invoke-FixtureGit @('init', '--quiet') | Out-Null
Invoke-FixtureGit @('add', '.') | Out-Null
Invoke-FixtureGit @('-c', 'user.name=Release Test', '-c', 'user.email=release-test@example.invalid',
    '-c', 'commit.gpgSign=false', 'commit', '--quiet', '-m', 'isolated release test fixture') | Out-Null
$taskRevision = (Invoke-FixtureGit @('rev-parse', 'HEAD')).Trim()
function Assert-PackageRejected([string]$Reason, [string]$ExpectedVersion = '', [string]$ExpectedRevision = '') {
    $taskRejected = $false
    try { & $taskPackage -Executable $taskExe -ExpectedVersion $ExpectedVersion -ExpectedRevision $ExpectedRevision | Out-Null }
    catch { $taskRejected = $true }
    if (-not $taskRejected) { throw "Package accepted $Reason." }
    if (Test-Path -LiteralPath (Join-Path $taskFixture 'build/release')) {
        throw "Rejected $Reason created package output directories."
    }
}
function Write-TestIdentity([string]$Configuration = 'Release', [string]$Architecture = 'x64',
                            [string]$Runtime = 'MultiThreaded', [string]$Hash = '',
                            [string]$Version = '1.0.0', [string]$Revision = $taskRevision,
                            [string]$SourceState = 'clean', [int]$Schema = 2) {
    if (-not $Hash) { $Hash = (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash }
    [ordered]@{ schema_version = $Schema; project = 'VaporCruise'; version = $Version;
        source_revision = $Revision; source_state = $SourceState; configuration = $Configuration;
        architecture = $Architecture; runtime = $Runtime; executable_sha256 = $Hash } |
        ConvertTo-Json | Set-Content -LiteralPath $taskIdentityPath -Encoding utf8
}
Assert-PackageRejected 'missing build identity'
Write-TestIdentity -Schema 1
Assert-PackageRejected 'legacy identity without enforced source metadata'
Write-TestIdentity -SourceState dirty
Assert-PackageRejected 'dirty build source'
Write-TestIdentity -SourceState unknown
Assert-PackageRejected 'unknown build source'
Write-TestIdentity -Revision ''
Assert-PackageRejected 'missing source revision'
Write-TestIdentity -Revision ('z' * 40)
Assert-PackageRejected 'malformed source revision'
Write-TestIdentity -Revision ('0' * 40)
Assert-PackageRejected 'stale source revision'
Write-TestIdentity -Version ''
Assert-PackageRejected 'missing version'
Write-TestIdentity -Version '1.0.1'
Assert-PackageRejected 'mismatched project version'
Write-TestIdentity
Assert-PackageRejected 'unexpected requested version' -ExpectedVersion '2.0.0'
Assert-PackageRejected 'unexpected requested commit' -ExpectedRevision ('0' * 40)
$taskIncomplete = Get-Content -LiteralPath $taskIdentityPath -Raw | ConvertFrom-Json
$taskIncomplete.PSObject.Properties.Remove('source_state')
$taskIncomplete | ConvertTo-Json | Set-Content -LiteralPath $taskIdentityPath -Encoding utf8
Assert-PackageRejected 'missing source state'
Write-TestIdentity
Set-Content -LiteralPath $taskSourceFile -Value 'tracked source changed after build'
Assert-PackageRejected 'currently dirty tracked source'
Set-Content -LiteralPath $taskSourceFile -Value 'clean source fixture'
$taskUntracked = Join-Path $taskSource 'uncommitted.h'
Set-Content -LiteralPath $taskUntracked -Value 'untracked build input'
Assert-PackageRejected 'untracked source input'
Remove-Item -LiteralPath $taskUntracked
Write-TestIdentity -Configuration Debug
Assert-PackageRejected 'Debug identity'
Write-TestIdentity -Architecture x86
Assert-PackageRejected 'non-x64 identity'
Write-TestIdentity -Runtime MultiThreadedDLL
Assert-PackageRejected 'dynamic CRT identity'
Write-TestIdentity -Hash ('0' * 64)
Assert-PackageRejected 'mismatched executable hash'
Set-Content -LiteralPath $taskIdentityPath -Value '{broken-json'
Assert-PackageRejected 'malformed identity'

# Exercise the same post-build writer as CMake, including BUILD_TESTING=OFF targets.
foreach ($taskConfiguration in @('Debug', 'Release')) {
    $taskRuntime = if ($taskConfiguration -eq 'Debug') { 'MultiThreadedDebug' } else { 'MultiThreaded' }
    & $CMakeExecutable "-DCONFIGURATION=$taskConfiguration" "-DEXECUTABLE=$taskExe" "-DMARKER=$taskDebugMarker" `
        "-DBUILD_MARKER=$taskIdentityPath" '-DARCHITECTURE=x64' "-DRUNTIME=$taskRuntime" `
        '-DPROJECT_VERSION=1.0.0' "-DSOURCE_DIR=$taskFixture" "-DSOURCE_REVISION=$taskRevision" '-DSOURCE_STATE=clean' `
        -P (Join-Path $taskProject 'cmake/write-diagnostic-marker.cmake')
    if ($LASTEXITCODE -ne 0) { throw 'Build identity writer failed.' }
    $taskIdentity = Get-Content -LiteralPath $taskIdentityPath -Raw | ConvertFrom-Json
    if ($taskIdentity.project -ne 'VaporCruise' -or $taskIdentity.schema_version -ne 2 -or
        $taskIdentity.version -ne '1.0.0' -or $taskIdentity.source_revision -ne $taskRevision -or
        $taskIdentity.source_state -ne 'clean' -or
        $taskIdentity.configuration -ne $taskConfiguration -or $taskIdentity.architecture -ne 'x64' -or
        $taskIdentity.runtime -ne $taskRuntime -or
        $taskIdentity.executable_sha256 -ne (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash) {
        throw "Incorrect generated $taskConfiguration build identity."
    }
    if ((Test-Path -LiteralPath $taskDebugMarker) -ne ($taskConfiguration -eq 'Debug')) {
        throw 'Existing Debug diagnostic marker behavior changed.'
    }
}
& $taskPackage -Executable $taskExe | Out-Null
$taskManifestPaths = @(Get-ChildItem -LiteralPath (Join-Path $taskFixture 'build/release') -Filter package-manifest.json -Recurse -File)
if ($taskManifestPaths.Count -ne 1) { throw 'Expected one verified package.' }
$taskManifest = Get-Content -LiteralPath $taskManifestPaths[0].FullName -Raw | ConvertFrom-Json
$taskExpected = @('VaporCruise.exe', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md',
    'magenta.png', 'cyan.png', 'amber.png', 'motion-preview.gif')
if (-not $taskManifest.integrityVerified -or $taskManifest.independentLaunchVerified -or
    $taskManifest.version -ne '1.0.0' -or $taskManifest.sourceRevision -ne $taskRevision -or
    $taskManifest.buildIdentity.source_state -ne 'clean' -or
    $taskManifest.buildIdentity.configuration -ne 'Release' -or $taskManifest.buildIdentity.runtime -ne 'MultiThreaded' -or
    @(Compare-Object $taskExpected @($taskManifest.files.file)).Count -ne 0) {
    throw 'Package acceptance, identity or exact ZIP contents changed.'
}
$taskZipName = Split-Path $taskManifest.zip -Leaf
if ($taskZipName -ne 'VaporCruise-1.0.0-windows-x64.zip') { throw 'Package archive is not versioned.' }
$taskPublicPath = Join-Path $taskManifestPaths[0].Directory.FullName 'release-manifest.json'
if (-not (Test-Path -LiteralPath $taskPublicPath -PathType Leaf)) { throw 'Public release manifest missing.' }
$taskPublicJson = Get-Content -LiteralPath $taskPublicPath -Raw
$taskPublic = $taskPublicJson | ConvertFrom-Json
if ($taskPublic.version -ne '1.0.0' -or $taskPublic.tag -ne 'v1.0.0' -or $taskPublic.sourceRevision -ne $taskRevision -or
    $taskPublic.zip -ne $taskZipName -or $taskPublic.zipSha256 -ne $taskManifest.zipSha256 -or
    -not $taskPublic.unsigned -or $taskPublic.buildIdentity.executable_sha256 -ne $taskManifest.buildIdentity.executable_sha256 -or
    $taskPublicJson -match '[A-Za-z]:[\\/]' -or
    @(Compare-Object $taskExpected @($taskPublic.files.file)).Count -ne 0) {
    throw 'Public release manifest is not portable or does not match the packaged identity.'
}
$taskChecksums = Get-Content -LiteralPath (Join-Path $taskManifestPaths[0].Directory.FullName 'SHA256SUMS')
if ($taskChecksums -notcontains ($taskManifest.zipSha256.ToLowerInvariant() + '  ' + $taskZipName) -or
    $taskChecksums -notcontains ((Get-FileHash -LiteralPath $taskPublicPath -Algorithm SHA256).Hash.ToLowerInvariant() + '  release-manifest.json') -or
    $taskChecksums.Count -ne 10) {
    throw 'Public checksums must cover the ZIP, manifest and eight packaged files.'
}

# A post-configure edit must never inherit a clean configure snapshot.
Set-Content -LiteralPath $taskSourceFile -Value 'source changed after configure'
& $CMakeExecutable '-DCONFIGURATION=Release' "-DEXECUTABLE=$taskExe" "-DMARKER=$taskDebugMarker" `
    "-DBUILD_MARKER=$taskIdentityPath" '-DARCHITECTURE=x64' '-DRUNTIME=MultiThreaded' `
    '-DPROJECT_VERSION=1.0.0' "-DSOURCE_DIR=$taskFixture" "-DSOURCE_REVISION=$taskRevision" '-DSOURCE_STATE=clean' `
    -P (Join-Path $taskProject 'cmake/write-diagnostic-marker.cmake')
if ($LASTEXITCODE -ne 0 -or (Get-Content -LiteralPath $taskIdentityPath -Raw | ConvertFrom-Json).source_state -ne 'dirty') {
    throw 'Post-configure source edits were certified as clean.'
}
Set-Content -LiteralPath $taskSourceFile -Value 'clean source fixture'
Write-TestIdentity

# A stale configure revision or dirty configure snapshot must not be upgraded
# just because the source is clean when the post-build command runs.
foreach ($taskConfigureCase in @(
    @{ revision = ('0' * 40); state = 'clean'; expectedState = 'dirty' },
    @{ revision = $taskRevision; state = 'dirty'; expectedState = 'dirty' }
)) {
    & $CMakeExecutable '-DCONFIGURATION=Release' "-DEXECUTABLE=$taskExe" "-DMARKER=$taskDebugMarker" `
        "-DBUILD_MARKER=$taskIdentityPath" '-DARCHITECTURE=x64' '-DRUNTIME=MultiThreaded' `
        '-DPROJECT_VERSION=1.0.0' "-DSOURCE_DIR=$taskFixture" "-DSOURCE_REVISION=$($taskConfigureCase.revision)" `
        "-DSOURCE_STATE=$($taskConfigureCase.state)" -P (Join-Path $taskProject 'cmake/write-diagnostic-marker.cmake')
    $taskIdentity = Get-Content -LiteralPath $taskIdentityPath -Raw | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or $taskIdentity.source_state -ne $taskConfigureCase.expectedState -or
        $taskIdentity.source_revision -ne $taskConfigureCase.revision) {
        throw 'Build writer lost the configure source snapshot.'
    }
}
$taskNoGit = Join-Path $taskBinary 'source-archive'
New-Item -ItemType Directory -Path $taskNoGit | Out-Null
& $CMakeExecutable '-DCONFIGURATION=Release' "-DEXECUTABLE=$taskExe" "-DMARKER=$taskDebugMarker" `
    "-DBUILD_MARKER=$taskIdentityPath" '-DARCHITECTURE=x64' '-DRUNTIME=MultiThreaded' `
    '-DPROJECT_VERSION=1.0.0' "-DSOURCE_DIR=$taskNoGit" '-DSOURCE_REVISION=' '-DSOURCE_STATE=unknown' `
    -P (Join-Path $taskProject 'cmake/write-diagnostic-marker.cmake')
$taskIdentity = Get-Content -LiteralPath $taskIdentityPath -Raw | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $taskIdentity.source_state -ne 'unknown' -or $taskIdentity.source_revision -ne '') {
    throw 'Source archive developer builds must remain usable with unknown identity.'
}
Write-TestIdentity

# Change tracked source during staging. The package must not certify a clean
# commit merely because its binary and copied documentation still match.
function Copy-Item {
    param([string]$LiteralPath, [string]$Destination)
    if ($LiteralPath -eq $taskExe) {
        Set-Content -LiteralPath $taskSourceFile -Value 'source changed during staging'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination
}
$taskSourceRaceRejected = $false
try { & $taskPackage -Executable $taskExe | Out-Null } catch { $taskSourceRaceRejected = $true }
if (-not $taskSourceRaceRejected -or
    @(Get-ChildItem -LiteralPath (Join-Path $taskFixture 'build/release') -Filter package-manifest.json -Recurse -File).Count -ne 1) {
    throw 'Package certified source changed during staging.'
}
Remove-Item -LiteralPath Function:\Copy-Item
Set-Content -LiteralPath $taskSourceFile -Value 'clean source fixture'

# Simulate a rebuild between identity preflight and staging. Preserve the real
# file-copy behavior and change only the source bytes at that boundary.
function Copy-Item {
    param([string]$LiteralPath, [string]$Destination)
    if ($LiteralPath -eq $taskExe) {
        [byte[]]$taskReplacement = [IO.File]::ReadAllBytes($taskExe) + [byte]0
        [IO.File]::WriteAllBytes($taskExe, $taskReplacement)
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination
}
$taskRaceRejected = $false
try { & $taskPackage -Executable $taskExe | Out-Null } catch { $taskRaceRejected = $true }
if (-not $taskRaceRejected -or
    @(Get-ChildItem -LiteralPath (Join-Path $taskFixture 'build/release') -Filter package-manifest.json -Recurse -File).Count -ne 1) {
    throw 'Package certified source bytes changed after build identity preflight.'
}
Write-Output 'PASS: clean commit/version identity, tracked/untracked source preflight, configure freshness, source archive builds, checksums, markers, exact ZIP contents and staging-race rejection.'
