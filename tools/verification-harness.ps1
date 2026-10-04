# Shared optional verification contracts; dot-source this file.
function ConvertTo-VaporCruiseArgument([AllowEmptyString()][string]$Value) {
    # Windows CommandLineToArgvW/CRT escaping, including quotes and trailing slashes.
    $escaped = [regex]::Replace($Value, '(\\*)"', [Text.RegularExpressions.MatchEvaluator]{
        param($match)
        return ('\' * ($match.Groups[1].Length * 2 + 1)) + '"'
    })
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Assert-VaporCruiseOutputs([string[]]$Paths) {
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Missing or empty output: $path"
        }
    }
}
function Assert-VaporCruiseFreshOutputs([string]$Directory,[string[]]$Names) {
    $seen = @{}
    foreach ($name in $Names) {
        if ($name -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $seen.ContainsKey($name)) { throw "Invalid or duplicate output name: $name" }
        $seen[$name] = $true
        if (Test-Path -LiteralPath (Join-Path $Directory $name)) { throw "Use fresh outputs; existing output collision: $name" }
    }
}
function Wait-VaporCruiseProcess {
    param($Process,[int]$TimeoutMilliseconds=15000,[string[]]$ExpectedOutputs=@())
    if (-not $Process.WaitForExit($TimeoutMilliseconds)) { throw "Process timeout after $TimeoutMilliseconds ms." }
    if ($Process.ExitCode -ne 0) { throw "Child failed: exit $($Process.ExitCode)." }
    Assert-VaporCruiseOutputs $ExpectedOutputs
}
function Invoke-VaporCruiseProcess {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Executable,[string[]]$Arguments=@(),
          [int]$TimeoutMilliseconds=15000,[string[]]$ExpectedOutputs=@(),
          [hashtable]$Environment=@{},[scriptblock]$ProcessFactory)
    $process = $null
    try {
        if ($ProcessFactory) { $process = & $ProcessFactory $Executable $Arguments $Environment }
        else {
            $info = New-Object Diagnostics.ProcessStartInfo
            $info.FileName = (Resolve-Path -LiteralPath $Executable).Path
            $info.Arguments = ($Arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' '
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
            foreach ($key in $Environment.Keys) { $info.EnvironmentVariables[$key] = $Environment[$key] }
            $process = [Diagnostics.Process]::Start($info)
            $retainedHandle = $process.Handle
        }
        if ($process -is [Diagnostics.Process]) { $retainedHandle=$process.Handle }
        Wait-VaporCruiseProcess -Process $process -TimeoutMilliseconds $TimeoutMilliseconds -ExpectedOutputs $ExpectedOutputs
    } finally {
        if ($process) {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
            $process.Dispose()
        }
    }
}
function ConvertTo-VaporCruisePose($Frame) {
    $inputValues = @{}
    if ($Frame -is [Collections.IDictionary]) { foreach ($key in $Frame.Keys) { $inputValues[$key]=$Frame[$key] } }
    else { foreach ($property in $Frame.PSObject.Properties) { $inputValues[$property.Name]=$property.Value } }
    foreach ($key in @('Interior','InteriorTransition','CabinSeconds','Blend','Motion')) {
        if ($inputValues.ContainsKey($key)) { throw "Removed capture pose field: $key" }
    }
    $defaults=[ordered]@{Name='';Width=1280;Height=720;Palette=0;Hint=0;Dpi=96;Phase=.25;Transition=0;
        Seed='6215337262362608466';Cell='0';CityLightPhase=0}
    foreach ($key in @($defaults.Keys)) {
        if ($inputValues.ContainsKey($key)) { $defaults[$key]=$inputValues[$key] }
    }
    foreach ($key in @('Phase','Transition','Hint','CityLightPhase')) {
        $defaults[$key]=([double]$defaults[$key]).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
    }
    $defaults.Seed=[string]$defaults.Seed
    $defaults.Cell=[string]$defaults.Cell
    return $defaults
}
function Assert-VaporCruisePng([string]$Path,[int]$Width,[int]$Height) {
    Assert-VaporCruiseOutputs @($Path)
    $stream = [IO.File]::OpenRead($Path)
    try {
        $header=New-Object byte[] 24
        if ($stream.Read($header,0,24) -ne 24 -or [BitConverter]::ToString($header,0,8) -ne '89-50-4E-47-0D-0A-1A-0A') { throw "Invalid PNG: $Path" }
        $actualWidth=($header[16]*16777216)+($header[17]*65536)+($header[18]*256)+$header[19]
        $actualHeight=($header[20]*16777216)+($header[21]*65536)+($header[22]*256)+$header[23]
        if ($actualWidth -ne $Width -or $actualHeight -ne $Height) { throw "PNG dimension mismatch: $Path" }
    } finally { $stream.Dispose() }
    $image=$null
    $bitmap=$null
    try {
        Add-Type -AssemblyName System.Drawing
        $image=[Drawing.Image]::FromFile($Path)
        $bitmap=New-Object Drawing.Bitmap $image
        $pixel=$bitmap.GetPixel($Width-1,$Height-1)
    } catch { throw "Invalid PNG decode: $Path ($_)"
    } finally {
        if ($bitmap) { $bitmap.Dispose() }
        if ($image) { $image.Dispose() }
    }
}
function Assert-VaporCruiseCamera($Camera) {
    if ($null -eq $Camera) { throw 'Missing camera metadata.' }
    foreach ($field in @('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')) {
        $value=$Camera.$field
        if ($null -eq $value -or $value -is [bool] -or $value -is [string] -or
            -not ($value -is [ValueType]) -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or [double]$value -le 0) {
            throw "Invalid camera metadata: $field"
        }
    }
    if ($Camera.horizon_fraction -ge 1 -or $Camera.near -ge $Camera.far) { throw 'Invalid camera ranges.' }
}

function Invoke-VaporCruiseCaptureMatrix {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$OutputDirectory,
          [Parameter(Mandatory=$true)][object[]]$Frames,[string]$ManifestName='captures.json',
          [hashtable]$Environment=@{},[scriptblock]$ProcessFactory,[switch]$AllowLegacyCamera)
    $exe=(Resolve-Path -LiteralPath $Executable).Path
    $output=[IO.Path]::GetFullPath($OutputDirectory)
    $poses=@($Frames | ForEach-Object { ConvertTo-VaporCruisePose $_ })
    if ($poses.Count -eq 0) { throw 'Capture matrix is empty.' }
    $names=@($poses | ForEach-Object { $_.Name+'.png' })
    Assert-VaporCruiseFreshOutputs -Directory $output -Names ($names+@($poses | ForEach-Object { 'camera-'+$_.Name+'.json' })+@($ManifestName))
    if ($ManifestName -eq 'captures.json') { Assert-VaporCruiseFreshOutputs -Directory $output -Names @('capture-build.json') }
    $hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $allPoses=@()
    $previous=$null
    $metadataPath=Join-Path $output 'capture-build.json'
    if (Test-Path -LiteralPath $metadataPath) {
        $previous=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        if (-not $previous.completion -or $previous.executable_sha256 -ne $hash) { throw 'Existing motion evidence is incomplete or executable changed.' }
        $allPoses=@($previous.frames)
        foreach ($prior in $allPoses) {
            Assert-VaporCruisePng -Path (Join-Path $output ($prior.Name+'.png')) -Width $prior.Width -Height $prior.Height
        }
    }
    $allPoses += $poses
    $sequences=@($allPoses | Where-Object { $_.Name -match '^motion-p[0-2]-[0-9]+$' } |
        Group-Object -Property { [int]$_.Palette } | ForEach-Object { @{palette=[int]$_.Name;count=$_.Count;width=$_.Group[0].Width;height=$_.Group[0].Height} })
    $textureBytes=$null
    $camera=$null
    $artworkFixture=Join-Path (Split-Path $exe) 'ArtworkMetadataFixture.exe'
    if ((Test-Path -LiteralPath $artworkFixture) -or -not $ProcessFactory) {
        if (-not (Test-Path -LiteralPath $artworkFixture)) { throw "Missing artwork metadata generator: $artworkFixture" }
        $artworkResult=& $artworkFixture
        if ($LASTEXITCODE -ne 0) { throw 'Artwork metadata generator failed.' }
        $artworkMetadata=$artworkResult | ConvertFrom-Json
        $textureBytes=$artworkMetadata.texture_bytes
        $camera=$artworkMetadata.camera
        if (-not $camera -and -not $AllowLegacyCamera) { throw 'Missing native camera metadata; use AllowLegacyCamera only for historical baseline evidence.' }
        if ($camera) { Assert-VaporCruiseCamera $camera }
    }
    if ($previous) {
        if ($previous.camera) { Assert-VaporCruiseCamera $previous.camera }
        elseif (($camera -or -not $ProcessFactory) -and -not $AllowLegacyCamera) { throw 'Missing camera metadata in prior captures.' }
        if ($previous.camera -and $camera) {
            foreach ($field in @('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')) {
                if ($previous.camera.$field -ne $camera.$field) { throw 'Camera changed between capture sequences.' }
            }
        }
        if ($previous.camera) {
            foreach ($prior in @($previous.frames)) {
                $priorCamera=Get-Content -LiteralPath (Join-Path $output ('camera-'+$prior.Name+'.json')) -Raw | ConvertFrom-Json
                Assert-VaporCruiseCamera $priorCamera.camera
                if ($priorCamera.width -ne $prior.Width -or $priorCamera.height -ne $prior.Height) { throw 'Prior capture camera dimensions disagree.' }
                foreach ($field in @('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')) {
                    if ($priorCamera.camera.$field -ne $previous.camera.$field) { throw "Prior capture camera mismatch: $field" }
                }
            }
        }
    }
    $metadata=[ordered]@{schema_version=1;Executable=$exe;Sha256=$hash;executable_sha256=$hash;
        frames=$allPoses;expected_sequences=$sequences;completion=$false;TextureBytes=$textureBytes;camera=$camera;
        Note='Actual hidden GPU captures; no displayed cadence or manual hardware certification.'}
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding utf8
    foreach ($pose in $poses) {
        if ((Get-FileHash -LiteralPath $exe).Hash -ne $hash) { throw 'Executable changed during capture.' }
        $path=Join-Path $output ($pose.Name+'.png')
        $arguments=@('--capture',$path,'--width',$pose.Width,'--height',$pose.Height,'--palette',$pose.Palette,
            '--hint',$pose.Hint,'--dpi',$pose.Dpi,'--phase',$pose.Phase,'--transition',$pose.Transition,
            '--seed',$pose.Seed,'--travel-cell',$pose.Cell,'--city-light-phase',$pose.CityLightPhase)
        Invoke-VaporCruiseProcess -Executable $exe -Arguments $arguments -ExpectedOutputs @($path) -Environment $Environment -ProcessFactory $ProcessFactory
        Assert-VaporCruisePng -Path $path -Width $pose.Width -Height $pose.Height
        if ($camera -and -not $ProcessFactory) {
            $actual=Get-Content -LiteralPath (Join-Path $output ('camera-'+$pose.Name+'.json')) -Raw | ConvertFrom-Json
            Assert-VaporCruiseCamera $actual.camera
            if ($actual.width -ne $pose.Width -or $actual.height -ne $pose.Height) { throw 'Capture camera dimensions disagree.' }
            foreach ($field in @('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')) {
                if ($null -eq $actual.camera.$field -or $actual.camera.$field -ne $camera.$field) { throw "Capture camera mismatch: $field" }
            }
        }

        if ((Get-FileHash -LiteralPath $exe).Hash -ne $hash) { throw 'Executable changed during capture.' }
    }
    ConvertTo-Json -InputObject $poses -Depth 8 | Set-Content -LiteralPath (Join-Path $output $ManifestName) -Encoding utf8
    $metadata.completion=$true
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding utf8
    Write-Output "Captured $($poses.Count) complete poses in $output"
}


function Get-VaporCruiseVisualSuites([string]$ManifestPath) {
    $catalog=Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ($catalog.schema_version -ne 1) { throw 'Unsupported visual suite manifest schema.' }
    $seen=@{}
    foreach ($suite in $catalog.suites) {
        if ($seen.ContainsKey($suite.id) -or $suite.classification -notin @('current','comparison','historical','utility')) { throw 'Invalid visual suite inventory.' }
        $seen[$suite.id]=$true
        if ($suite.retirement_reason) {
            if ($suite.classification -ne 'historical' -or $suite.capture_profile) { throw "Invalid retired suite: $($suite.id)" }
        } elseif ($suite.classification -ne 'utility' -and -not $catalog.profiles.PSObject.Properties[$suite.capture_profile]) { throw "Missing capture profile: $($suite.id)" }
    }
    return $catalog
}
function Select-VaporCruiseVisualSuites($Catalog,[string[]]$Suite=@('current')) {
    $selected=@{}
    foreach ($id in $Suite) {
        $matches=@($Catalog.suites | Where-Object { $_.id -eq $id -or $_.classification -eq $id })
        if (-not $matches.Count) { throw "Unknown visual suite: $id" }
        foreach ($entry in $matches) {
            if ($entry.retirement_reason) { throw "Retired suite $($entry.id): $($entry.retirement_reason)" }
            if ($entry.classification -eq 'utility') { throw "Utility $($entry.id) runs directly; it is not visual acceptance." }
            $selected[$entry.id]=$entry
        }
    }
    return @($selected.Values | Sort-Object id)
}


function Assert-VaporCruiseDebugExecutable([string]$Executable) {
    $marker=Join-Path (Split-Path ([IO.Path]::GetFullPath($Executable))) 'vaporcruise-debug-diagnostics.json'
    if (-not (Test-Path -LiteralPath $marker)) { throw 'Diagnostic source override requires a Debug executable.' }
    $metadata=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    if ($metadata.configuration -ne 'Debug' -or $metadata.executable_sha256 -ne (Get-FileHash -LiteralPath $Executable).Hash) { throw 'Debug diagnostic marker does not match executable hash.' }
}
