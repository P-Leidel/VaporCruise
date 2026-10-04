param(
    [string]$Executable = 'build/portable/bin/Release/VaporCruise.exe',
    [string]$OutputDirectory = 'build/visual/road-terrain-final',
    [switch]$Motion,
    [uint64]$Seed = 6215337262362608466,
    [string]$PythonExecutable,
    [object[]]$AdditionalFrames = @()
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'python-runtime.ps1')
$taskPython = Resolve-VaporCruisePython -PythonExecutable $PythonExecutable -RequiredImports @('numpy','PIL')
. (Join-Path $PSScriptRoot 'verification-harness.ps1')
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$executablePath = (Resolve-Path $Executable).Path
$frames = @(
    @{Name='magenta'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Phase=.25; Transition=0},
    @{Name='cyan'; Width=1280; Height=720; Palette=1; Hint=0; Dpi=96; Phase=.25; Transition=0},
    @{Name='amber'; Width=1280; Height=720; Palette=2; Hint=0; Dpi=96; Phase=.25; Transition=0},
    @{Name='portrait'; Width=720; Height=1280; Palette=0; Hint=1; Dpi=96; Phase=.25; Transition=0},
    @{Name='ultrawide'; Width=2560; Height=1080; Palette=1; Hint=1; Dpi=144; Phase=.25; Transition=0},
    @{Name='small'; Width=480; Height=320; Palette=2; Hint=1; Dpi=96; Phase=.25; Transition=0},
    @{Name='high-dpi'; Width=1280; Height=720; Palette=0; Hint=1; Dpi=192; Phase=.25; Transition=0},
    @{Name='transition'; Width=1280; Height=720; Palette=0; Hint=.5; Dpi=96; Phase=.25; Transition=.5},
    @{Name='repeat'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Phase=.25; Transition=0},
    @{Name='hint'; Width=1280; Height=720; Palette=0; Hint=1; Dpi=96; Phase=.25; Transition=0},
    @{Name='seam-control-before'; Width=640; Height=360; Palette=0; Hint=0; Dpi=96; Cell='31'; Phase=.9997; Transition=0},
    @{Name='seam-before'; Width=640; Height=360; Palette=0; Hint=0; Dpi=96; Cell='31'; Phase=.9999; Transition=0},
    @{Name='seam-after'; Width=640; Height=360; Palette=0; Hint=0; Dpi=96; Cell='32'; Phase=.0001; Transition=0},
    @{Name='seam-control-after'; Width=640; Height=360; Palette=0; Hint=0; Dpi=96; Cell='32'; Phase=.0003; Transition=0},
    @{Name='far-edge-before'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Cell='1087'; Phase=.9999; Transition=0},
    @{Name='far-edge-after'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Cell='1088'; Phase=.0001; Transition=0},
    @{Name='world-320'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Cell='320'; Phase=.25; Transition=0},
    @{Name='world-3200'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Cell='3200'; Phase=.25; Transition=0},
    @{Name='world-far'; Width=1280; Height=720; Palette=0; Hint=0; Dpi=96; Cell='36028797018963971'; Phase=.25; Transition=0}
)
if ($Motion) {
    for ($frame = 0; $frame -lt 16; ++$frame) {
        $frames += @{Name="base-$frame"; Width=1280; Height=720; Palette=0;
            Hint=0; Dpi=96; Cell='0'; Phase=(.25 + .025 * $frame); Transition=0}
    }
    for ($palette = 0; $palette -lt 3; ++$palette) {
        for ($frame = 0; $frame -lt 24; ++$frame) {
            $offset = 2 * $frame / 23.0
            $carry = [int64][Math]::Floor($offset)
            $frames += @{Name=('motion-p{0}-{1:d2}' -f $palette,$frame); Width=640; Height=360;
                Palette=$palette; Hint=0; Dpi=96; Cell=([int64]31+$carry).ToString(); Phase=($offset-$carry); Transition=0}
        }
    }
}
$frames += $AdditionalFrames
foreach ($pose in $frames) {
    if (-not $pose.ContainsKey('Seed')) { $pose.Seed = $Seed.ToString([Globalization.CultureInfo]::InvariantCulture) }
}
Invoke-VaporCruiseCaptureMatrix -Executable $executablePath -OutputDirectory $outputPath -Frames $frames
& $taskPython (Join-Path $PSScriptRoot '../tests/compare-resolved-captures.py') (Join-Path $outputPath 'magenta.png') (Join-Path $outputPath 'repeat.png')
if ($LASTEXITCODE -ne 0) {
    throw 'Repeated deterministic capture differs.'
}
Write-Output 'PASS: repeated fixed snapshot agrees within sparse MSAA rounding.'
if ($Motion) {
    # Preview displays consecutive GPU PNGs once; replay explicitly resets the route.
    $html = @'
<!doctype html><html lang="en"><meta charset="utf-8"><title>VaporCruise GPU motion preview</title>
<style>body{margin:2rem;background:#090619;color:#ded7ea;font:16px system-ui}img{display:block;width:min(100%,1280px);margin:1rem 0}button,select{font:inherit;margin-right:1rem}p{max-width:70ch}</style>
<h1>VaporCruise GPU motion preview</h1><p>Actual consecutive backbuffer snapshots across cells 31-33, at 1.5 cells per second. Replay resets the route. This preview does not measure live presentation cadence.</p>
<select id="palette" aria-label="Palette"><option value="0">Midnight magenta</option><option value="1">Lagoon cyan</option><option value="2">Dusk amber</option></select><button id="pause">Pause</button>
<img id="scene" src="motion-p0-00.png" alt="Sunset cruise with moving perspective grid">
<p id="pose"></p>
<script>const cache=[];let poses=[];fetch('captures.json').then(r=>r.json()).then(r=>{poses=r;show()});for(let p=0;p<3;p++)for(let f=0;f<24;f++){const i=new Image;i.src=`motion-p${p}-${String(f).padStart(2,'0')}.png`;cache.push(i)}let frame=0,paused=false;function show(){const name=`motion-p${palette.value}-${String(frame).padStart(2,'0')}`;scene.src=cache[Number(palette.value)*24+frame].src;const r=poses.find(r=>r.Name===name);if(r)pose.textContent=`Seed ${r.Seed} | cell ${r.Cell} | phase ${r.Phase}`}palette.onchange=show;pause.onclick=()=>{if(frame===23)frame=0;paused=!paused;pause.textContent=paused?'Play':'Pause';show()};setInterval(()=>{if(!paused){frame=Math.min(frame+1,23);show();if(frame===23){paused=true;pause.textContent='Replay'}}},1000/17.25)</script>
</html>
'@
    Set-Content -LiteralPath (Join-Path $outputPath 'motion-preview.html') -Value $html -Encoding utf8
}
