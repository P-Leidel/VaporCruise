[CmdletBinding()]
param(
    [string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
    [string]$ExpectedVersion,
    [string]$ExpectedRevision
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$projectPrefix = $projectRoot + [IO.Path]::DirectorySeparatorChar

function Assert-ProjectPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must remain inside the project: $absolute"
    }
    return $absolute
}

function Invoke-ReleaseGit([string[]]$Arguments) {
    $gitOutput = & git -C $projectRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Release packaging requires an accessible Git checkout with a known commit.' }
    return $gitOutput
}

function Get-CleanReleaseRevision {
    $gitRoot = [IO.Path]::GetFullPath((Invoke-ReleaseGit @('rev-parse', '--show-toplevel')))
    if (-not $gitRoot.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Release source must be the root of its own Git checkout.'
    }
    $revision = Invoke-ReleaseGit @('rev-parse', '--verify', 'HEAD')
    if ($revision -notmatch '^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$') {
        throw 'Release source revision must be a full Git commit hash.'
    }
    $sourceStatus = @(Invoke-ReleaseGit @('status', '--porcelain', '--untracked-files=normal'))
    if ($sourceStatus.Count -ne 0) {
        throw 'Release packaging requires clean tracked source and no nonignored untracked files. Commit source changes, configure and rebuild.'
    }
    return $revision
}

$exePath = if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $projectRoot $Executable }
$exePath = Assert-ProjectPath $exePath
$inputs = [ordered]@{
    'VaporCruise.exe' = $exePath
    'README.md' = Join-Path $projectRoot 'README.md'
    'LICENSE' = Join-Path $projectRoot 'LICENSE'
    'THIRD-PARTY-NOTICES.md' = Join-Path $projectRoot 'THIRD-PARTY-NOTICES.md'
    'magenta.png' = Join-Path $projectRoot 'docs/current/progress/visuals/landscape-effects-2026-10-04/magenta.png'
    'cyan.png' = Join-Path $projectRoot 'docs/current/progress/visuals/landscape-effects-2026-10-04/cyan.png'
    'amber.png' = Join-Path $projectRoot 'docs/current/progress/visuals/landscape-effects-2026-10-04/amber.png'
    'motion-preview.gif' = Join-Path $projectRoot 'docs/current/progress/visuals/landscape-effects-2026-10-04/magenta-motion.gif'
}
foreach ($inputPath in $inputs.Values) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf) -or (Get-Item -LiteralPath $inputPath).Length -eq 0) {
        throw "Required nonempty release input missing: $inputPath"
    }
}

# This build-only sidecar is emitted even when BUILD_TESTING is OFF. Require
# the selected portable Release identity before creating any package outputs.
$identityPath = Join-Path (Split-Path $exePath -Parent) 'vaporcruise-build.json'
if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) {
    throw "Release build identity missing: $identityPath. Rebuild VaporCruise with the current CMake configuration."
}
$buildIdentity = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json
if ($buildIdentity.schema_version -ne 2 -or $buildIdentity.project -ne 'VaporCruise' -or
    $buildIdentity.configuration -ne 'Release' -or $buildIdentity.architecture -ne 'x64' -or
    $buildIdentity.runtime -ne 'MultiThreaded') {
    throw 'Packaging requires a VaporCruise Release x64 build with the static MSVC runtime.'
}
if ($buildIdentity.source_state -cne 'clean' -or
    $buildIdentity.source_revision -notmatch '^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$' -or
    $buildIdentity.version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw 'Release build identity requires a version and full clean Git source revision. Configure and rebuild from committed source.'
}
$sourceRevision = Get-CleanReleaseRevision
if ($sourceRevision -ne $buildIdentity.source_revision -or
    ($ExpectedRevision -and $sourceRevision -ne $ExpectedRevision)) {
    throw 'Release build source revision does not match the selected commit; configure and rebuild before packaging.'
}
$versionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw),
    '(?im)^\s*project\(\s*VaporCruise\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b')
if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -ne $buildIdentity.version -or
    ($ExpectedVersion -and $buildIdentity.version -ne $ExpectedVersion)) {
    throw 'Release build version does not match the selected CMake project version; configure and rebuild before packaging.'
}
$version = $buildIdentity.version
if ($buildIdentity.executable_sha256 -ne (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash) {
    throw 'Release build identity does not match the executable; rebuild before packaging.'
}
$inputHashes = @{}
foreach ($entry in $inputs.GetEnumerator()) {
    if ($entry.Key -ne 'VaporCruise.exe') {
        $relativeInput = $entry.Value.Substring($projectPrefix.Length).Replace('\', '/')
        Invoke-ReleaseGit @('ls-files', '--error-unmatch', '--', $relativeInput) | Out-Null
    }
    $inputHashes[$entry.Key] = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash
}

# Unique destinations preserve previous packages and need no recursive deletion.
$runName = 'portable-' + $version + '-' + $sourceRevision.Substring(0, 12) + '-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Assert-ProjectPath (Join-Path $projectRoot "build/release/$runName")
$stage = Assert-ProjectPath (Join-Path $runRoot 'stage')
$unpacked = Assert-ProjectPath (Join-Path $runRoot 'unpacked')
$zipPath = Assert-ProjectPath (Join-Path $runRoot "VaporCruise-$version-windows-x64.zip")
New-Item -ItemType Directory -Path $stage, $unpacked | Out-Null
foreach ($entry in $inputs.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $stage $entry.Key)
}
$stagedPaths = @($inputs.Keys | ForEach-Object { Join-Path $stage $_ })
Compress-Archive -LiteralPath $stagedPaths -DestinationPath $zipPath
Expand-Archive -LiteralPath $zipPath -DestinationPath $unpacked
$unpackedFiles = @(Get-ChildItem -LiteralPath $unpacked -Recurse -File)
if ($unpackedFiles.Count -ne $inputs.Count -or
    @(Get-ChildItem -LiteralPath $unpacked -Directory).Count -ne 0 -or
    @(Compare-Object @($inputs.Keys) @($unpackedFiles.Name)).Count -ne 0) {
    throw 'Unpacked package contains unexpected files or directories.'
}
$manifest = foreach ($entry in $inputs.GetEnumerator()) {
    $copy = Join-Path $unpacked $entry.Key
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf)) { throw "Unpacked file missing: $copy" }
    $originalHash = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash
    $copyHash = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
    if ((Get-Item -LiteralPath $copy).Length -eq 0 -or $originalHash -ne $copyHash -or
        $inputHashes[$entry.Key] -ne $copyHash) {
        throw "Unpacked file failed integrity verification: $copy"
    }
    if ($entry.Key -eq 'VaporCruise.exe' -and $copyHash -ne $buildIdentity.executable_sha256) {
        throw 'Packaged executable changed after build identity preflight; rebuild and package again.'
    }
    [pscustomobject]@{ file = $entry.Key; bytes = (Get-Item -LiteralPath $copy).Length; sha256 = $copyHash }
}
if ((Get-CleanReleaseRevision) -ne $sourceRevision) {
    throw 'Release source changed while packaging; configure, rebuild and package again.'
}
$manifestPath = Join-Path $runRoot 'package-manifest.json'
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
[pscustomobject]@{
    schemaVersion = 2
    version = $version
    sourceRevision = $sourceRevision
    createdUtc = [DateTime]::UtcNow.ToString('o')
    executableSource = $exePath
    buildIdentity = $buildIdentity
    zip = $zipPath
    zipSha256 = $zipHash
    unpacked = $unpacked
    integrityVerified = $true
    independentLaunchVerified = $false
    files = @($manifest)
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$publicManifestPath = Join-Path $runRoot 'release-manifest.json'
[ordered]@{
    schemaVersion = 2
    version = $version
    tag = 'v' + $version
    sourceRevision = $sourceRevision
    buildIdentity = [ordered]@{
        schema_version = 2
        project = 'VaporCruise'
        version = $version
        source_revision = $sourceRevision
        source_state = 'clean'
        configuration = $buildIdentity.configuration
        architecture = $buildIdentity.architecture
        runtime = $buildIdentity.runtime
        executable_sha256 = $buildIdentity.executable_sha256
    }
    zip = Split-Path $zipPath -Leaf
    zipSha256 = $zipHash
    files = @($manifest)
    unsigned = $true
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $publicManifestPath -Encoding utf8
$checksumPath = Join-Path $runRoot 'SHA256SUMS'
$checksumLines = @($zipHash.ToLowerInvariant() + '  ' + (Split-Path $zipPath -Leaf))
$checksumLines += (Get-FileHash -LiteralPath $publicManifestPath -Algorithm SHA256).Hash.ToLowerInvariant() + '  release-manifest.json'
$checksumLines += @($manifest | ForEach-Object { $_.sha256.ToLowerInvariant() + '  unpacked/' + $_.file })
$checksumLines | Set-Content -LiteralPath $checksumPath -Encoding ascii
Write-Output "Version: $version; source commit: $sourceRevision"
Write-Output "ZIP: $zipPath"
Write-Output "Manifest: $manifestPath"
Write-Output "Public manifest: $publicManifestPath"
Write-Output "Checksums: $checksumPath"
Write-Output "Unpacked executable: $(Join-Path $unpacked 'VaporCruise.exe')"
Write-Output 'Integrity verified. Independent launch and acceptance remain separate checks.'
